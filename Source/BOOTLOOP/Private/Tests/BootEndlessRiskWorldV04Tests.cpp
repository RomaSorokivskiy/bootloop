#include "Misc/AutomationTest.h"
#include "World/Generation/BootEndlessPlanner.h"
#include "World/Generation/BootEndlessChunk.h"
#include "World/Threats/BootScanProcess.h"
#include "World/Interaction/BootSurfWindow.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootEndlessRiskProcessArmingV04,"Bootloop.V04.World.ProcessSequenceArming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootEndlessRiskProcessArmingV04::RunTest(const FString&)
{
    const auto Catalog=FBootChunkCatalog::Build();const auto* Definition=Catalog.FindByPredicate([](const auto& D){return D.ProcessCost==3;});
    if(!TestTrue(TEXT("three-process authored sequence exists"),Definition!=nullptr))return false;
    UWorld::InitializationValues Init;Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());World->BeginPlay();World->GetWorldSettings()->NotifyBeginPlay();
    auto Step=[&](int32 Frames){for(int32 I=0;I<Frames;++I){++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60.f);}};
    FBootPlanRequest R;R.Run.RunId=FGuid(40,30,20,10);R.CurrentEnvelope=FBootTraversalPolicy::FromTempo(182.,1);R.ActiveSeconds=900.;R.Tier=5;R.Modifiers.ExtraProcessBudget=2;
    FBootReplayDecision Replay;Replay.DefinitionId=Definition->Id;Replay.DefinitionVersion=Definition->Version;Replay.MusicContext=R.MusicContext;R.ReplayDecision=Replay;
    FBootEndlessPlanner Planner;Planner.Reset(15,R.CurrentEnvelope);const auto Planned=Planner.PlanNext(R);
    if(TestTrue(TEXT("multi-process native plan legal"),Planned.Committed.IsSet()))
    {
        auto* Chunk=World->SpawnActor<ABootEndlessChunk>();Chunk->Configure(Planned.Committed.GetValue());Chunk->FinishPreparation();
        TMap<int32,ABootScanProcess*> Scans;
        for(TActorIterator<ABootScanProcess> It(World);It;++It)if(It->GetOwner()==Chunk&&It->GetRunOperation())Scans.Add(It->GetRunOperation()->LocalId,*It);
        if(TestEqual(TEXT("three distinct native threats created"),Scans.Num(),3))
        {
            const auto& Hazards=Definition->Recipe.Hazards;
            Chunk->UpdateApproach(FVector(2500.,0.,98.),R.CurrentEnvelope);
            TestEqual(TEXT("near process starts its own warning"),Scans[Hazards[0].OperationId]->GetPhase(),EBootScanPhase::Warning);
            TestEqual(TEXT("distant process retains future warning"),Scans[Hazards[1].OperationId]->GetPhase(),EBootScanPhase::Dormant);
            TestEqual(TEXT("third process remains dormant"),Scans[Hazards[2].OperationId]->GetPhase(),EBootScanPhase::Dormant);
            Step(240);Chunk->UpdateApproach(FVector(Hazards[1].Sweep.Min.X-1000.,-400.,98.),R.CurrentEnvelope);
            TestEqual(TEXT("second approach starts a fresh full warning"),Scans[Hazards[1].OperationId]->GetPhase(),EBootScanPhase::Warning);
            TestEqual(TEXT("third process still waits for its own approach"),Scans[Hazards[2].OperationId]->GetPhase(),EBootScanPhase::Dormant);
            Chunk->SetProcessesActive(false);Step(120);
            TestEqual(TEXT("pause preserves second warning"),Scans[Hazards[1].OperationId]->GetPhase(),EBootScanPhase::Warning);
            Chunk->SetProcessesActive(true);Step(90);
            TestTrue(TEXT("resume advances the armed process"),Scans[Hazards[1].OperationId]->GetPhase()!=EBootScanPhase::Warning);
        }
    }
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootEndlessAbortedRidePinV04,"Bootloop.V04.World.AbortedRideReleasesPin",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootEndlessAbortedRidePinV04::RunTest(const FString&)
{
    UWorld::InitializationValues Init;Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());World->BeginPlay();World->GetWorldSettings()->NotifyBeginPlay();
    auto Step=[&](int32 Frames){for(int32 I=0;I<Frames;++I){++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60.f);}};
    auto* Floor=World->SpawnActor<AActor>();auto* Collision=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Collision);
    Collision->SetBoxExtent(FVector(5000.,5000.,40.));Collision->SetCollisionProfileName(TEXT("BlockAll"));Collision->RegisterComponent();Floor->SetActorLocation(FVector(0.,0.,-40.));
    auto* Window=World->SpawnActor<ABootSurfWindow>();Window->Configure(FVector(0.,0.,200.),FVector(600.,0.,200.),1.f,INDEX_NONE);
    FBootOperationKey Key;Key.Run.RunId=FGuid(7,6,5,4);Key.ChunkId=17;Key.LocalId=1;Window->SetRunOperation(Key);
    auto* Rider=World->SpawnActor<ACharacter>(FVector(0.,0.,310.),FRotator::ZeroRotator);auto* Move=Rider->GetCharacterMovement();Move->bRunPhysicsWithNoController=true;Move->SetMovementMode(MOVE_Walking);
    Step(20);TestTrue(TEXT("real platform supports abort fixture"),Rider->GetMovementBase()==Window->GetRootComponent());
    TestTrue(TEXT("offbeat carry begins"),Window->Activate(Rider,false));Step(75);
    TestTrue(TEXT("actual ride was carried"),Window->HasCarried(Rider));
    Rider->LaunchCharacter(FVector(0.,1000.,300.),true,true);Step(4);
    TestTrue(TEXT("first airborne departure remains pinned"),Move->IsFalling()&&Window->HasActiveCommitment());
    for(int32 Frame=0;Frame<180&&!Move->IsMovingOnGround();++Frame)Step(1);
    TestTrue(TEXT("rider physically lands on different static support"),Move->IsMovingOnGround()&&Rider->GetMovementBase()==Collision);
    Step(2);TestFalse(TEXT("aborted ride releases after first landing"),Window->HasActiveCommitment());
    Rider->LaunchCharacter(FVector(0.,0.,400.),true,true);Step(4);
    TestTrue(TEXT("later jump is physically airborne"),Move->IsFalling());
    TestFalse(TEXT("later jump cannot repin an ancient ride"),Window->HasActiveCommitment());
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}

