#include "Misc/AutomationTest.h"
#include "World/Generation/BootEndlessPlanner.h"
#include "World/Generation/BootEndlessChunk.h"
#include "World/Threats/BootScanProcess.h"
#include "Player/BootRunner.h"
#include "Movement/BootMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootScanEncounterTimingV04,"Bootloop.V04.World.ScanEncounterTiming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootScanEncounterTimingV04::RunTest(const FString&)
{
    for(float Speed:{1100.f,1540.f,3360.f})for(int32 Fps:{30,60,120})
    {
        UWorld::InitializationValues Init;Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
        UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());World->BeginPlay();World->GetWorldSettings()->NotifyBeginPlay();
        const float Dt=1.f/Fps;auto Step=[&](){++GFrameCounter;World->Tick(LEVELTICK_All,Dt);};
        auto E=FBootTraversalPolicy::FromTempo(Speed==1100.f?130.:182.,1);E.RunCmps=Speed;E.MomentumCmps=FMath::Max(E.MomentumCmps,Speed);
        FBootPlanRequest R;R.Run.RunId=FGuid(1,5,9,13);R.CurrentEnvelope=E;R.ActiveSeconds=400.;R.Tier=5;
        FBootReplayDecision Replay;Replay.DefinitionId=TEXT("process.scan");Replay.MusicContext=R.MusicContext;R.ReplayDecision=Replay;
        FBootEndlessPlanner Planner;Planner.Reset(14,E);const auto Planned=Planner.PlanNext(R);
        if(TestTrue(TEXT("encounter has native descriptor"),Planned.Committed.IsSet()))
        {
            auto* Chunk=World->SpawnActor<ABootEndlessChunk>();Chunk->Configure(*Planned.Committed);Chunk->FinishPreparation();
            ABootScanProcess* Scan=nullptr;for(TActorIterator<ABootScanProcess> It(World);It;++It)if(It->GetOwner()==Chunk)Scan=*It;
            auto* Runner=World->SpawnActor<ABootRunner>(FVector(350.,0.,98.),FRotator::ZeroRotator);auto* Move=Runner->GetParkour();
            Move->bRunPhysicsWithNoController=true;Move->SetMovementMode(MOVE_Walking);Move->ApplyTraversalEnvelope(E);
            for(int32 I=0;I<12;++I)Step();
            bool StayedDormant=true;
            for(int32 I=0;I<3*Fps;++I){Chunk->UpdateApproach(Runner->GetActorLocation(),E);Step();StayedDormant&=Scan&&Scan->GetPhase()==EBootScanPhase::Dormant;}
            TestTrue(TEXT("stopping before encounter does not spend its warning or sweep"),StayedDormant);
            Move->Velocity=FVector(Speed,0.,0.);
            double WarningAt=-1.,SweepAt=-1.,ContactAt=-1.;int32 Contacts=0;
            Chunk->OnRunThreatContact.AddLambda([&](ACharacter*,const FBootOperationKey&){++Contacts;if(ContactAt<0.)ContactAt=World->GetTimeSeconds();});
            for(int32 Frame=0;Frame<int32(12./Dt)&&Runner->GetActorLocation().X<6500.;++Frame)
            {
                Chunk->UpdateApproach(Runner->GetActorLocation(),E);
                if(Scan&&Scan->GetPhase()==EBootScanPhase::Warning&&WarningAt<0.)WarningAt=World->GetTimeSeconds();
                Runner->AddMovementInput(FVector::ForwardVector,1.f,true);Step();
                if(Scan&&Scan->GetPhase()==EBootScanPhase::Sweep&&SweepAt<0.)SweepAt=World->GetTimeSeconds();
            }
            const FString Context=FString::Printf(TEXT("speed%.0f fps%d"),Speed,Fps);
            TestTrue(*FString::Printf(TEXT("center-lane ordinary/max arrival faces real active sweep %s"),*Context),Contacts>0);
            TestTrue(*FString::Printf(TEXT("warning precedes sweep by at least .9 seconds %s"),*Context),WarningAt>=0.&&SweepAt-WarningAt>=.9);
            TestTrue(*FString::Printf(TEXT("contact never precedes full warning %s"),*Context),ContactAt>=0.&&ContactAt-WarningAt>=.9);
        }
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootRecoveryCadenceV04,"Bootloop.V04.Planner.RecoveryCadence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootRecoveryCadenceV04::RunTest(const FString&)
{
    const auto Catalog=FBootChunkCatalog::Build();FBootPlanRequest R;R.Run.RunId=FGuid(1,2,8,9);R.CurrentEnvelope=FBootTraversalPolicy::FromTempo(130.,1);R.ActiveSeconds=900.;R.Tier=5;
    R.Modifiers.ExtraCombinationBudget=2;R.Modifiers.ExtraProcessBudget=2;FBootEndlessPlanner Planner,ReplayPlanner;Planner.Reset(193,R.CurrentEnvelope);ReplayPlanner.Reset(193,R.CurrentEnvelope);
    auto ReplayRequest=R;
    for(int32 I=0;I<48;++I)
    {
        const auto Planned=Planner.PlanNext(R);if(!TestTrue(TEXT("cadence route remains legal"),Planned.Committed.IsSet()))return false;
        const auto& C=*Planned.Committed;const auto* Definition=Catalog.FindByPredicate([&](const auto& D){return D.Id==C.DefinitionId;});
        if(C.InstanceId%4==0)TestTrue(TEXT("every fourth continuation is a real recovery opportunity"),Definition&&Definition->bSafeContinuation);
        FBootReplayDecision Decision;Decision.DefinitionId=C.DefinitionId;Decision.DefinitionVersion=C.DefinitionVersion;Decision.MusicContext=C.MusicContext;ReplayRequest.ReplayDecision=Decision;
        const auto Replayed=ReplayPlanner.PlanNext(ReplayRequest);TestTrue(TEXT("cadence remains exact under recorded replay"),Replayed.Committed.IsSet()&&FBootEndlessPlanner::DescriptorHash(*Replayed.Committed)==FBootEndlessPlanner::DescriptorHash(C));
        R.Connector=C.WorldExit;R.CanonicalStartMetres=C.CanonicalEndMetres;R.NextInstanceId=C.InstanceId+1;R.LiveDescriptors.Add(C);if(R.LiveDescriptors.Num()>=40)R.LiveDescriptors.RemoveAt(0);ReplayRequest=R;
    }
    return true;
}

