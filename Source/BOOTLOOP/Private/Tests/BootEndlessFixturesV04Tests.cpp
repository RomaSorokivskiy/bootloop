#include "Misc/AutomationTest.h"
#include "World/Generation/BootEndlessChunk.h"
#include "World/Generation/BootEndlessPlanner.h"
#include "World/Interaction/BootSurfWindow.h"
#include "World/Threats/BootPopupProcess.h"
#include "Player/BootRunner.h"
#include "Movement/BootMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

namespace BootEndlessFixture
{
struct FWorld
{
    UWorld* World;
    FWorld()
    {
        UWorld::InitializationValues Init;Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());World->BeginPlay();World->GetWorldSettings()->NotifyBeginPlay();
    }
    ~FWorld(){World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Step(float Dt){++GFrameCounter;World->Tick(LEVELTICK_All,Dt);}
};
bool Traverse(FAutomationTestBase& Test,const FBootChunkDefinition& Definition,float Speed,int32 Fps,bool Optional)
{
    FWorld W;const float Dt=1.f/Fps;
    // Holding each authored speed band is a deliberately strict physical fixture, not a claim
    // that the shipped tempo policy changes normal run pace to temporary cap.
    auto E=FBootTraversalPolicy::FromTempo(130.,1);E.RunCmps=Speed;E.MomentumCmps=FMath::Max(Speed,E.MomentumCmps);E.TemporaryCmps=FMath::Max(Speed,E.TemporaryCmps);
    FBootEndlessPlanner Planner;Planner.Reset(71,E);FBootPlanRequest R;R.Run.RunId=FGuid(11,22,33,44);R.Run.Generation=1;
    R.CurrentEnvelope=E;R.ActiveSeconds=400.;R.Tier=5;
    // A descending recipe starts above desktop height and returns toward it.
    // Keep the fixture on the same legal side of the global emergency fall plane.
    if(Definition.Exit.GetLocation().Z<0.)R.Connector=FTransform(FVector(0.,0.,-Definition.Exit.GetLocation().Z));
    R.Modifiers.ExtraCombinationBudget=Definition.CombinationCost;R.Modifiers.ExtraProcessBudget=FMath::Max(0,Definition.ProcessCost-1);
    FBootReplayDecision Choice;Choice.DefinitionId=Definition.Id;Choice.DefinitionVersion=Definition.Version;Choice.MusicContext=R.MusicContext;R.ReplayDecision=Choice;
    auto Planned=Planner.PlanNext(R);if(!Test.TestTrue(TEXT("native fixture descriptor available"),Planned.Committed.IsSet()))return false;
    auto* Chunk=W.World->SpawnActor<ABootEndlessChunk>();
    if(!Test.TestTrue(TEXT("recipe creates native geometry"),Chunk->Configure(Planned.Committed.GetValue())&&Chunk->FinishPreparation()))return false;
    R.Connector=Planned.Committed->WorldExit;R.NextInstanceId=2;R.CanonicalStartMetres=Planned.Committed->CanonicalEndMetres;R.LiveDescriptors.Add(Planned.Committed.GetValue());Choice.DefinitionId=TEXT("recovery.clean");R.ReplayDecision=Choice;
    auto Next=Planner.PlanNext(R);if(!Test.TestTrue(TEXT("native seam continuation available"),Next.Committed.IsSet()))return false;
    auto* Landing=W.World->SpawnActor<ABootEndlessChunk>();
    if(!Test.TestTrue(TEXT("native seam collision ready"),Landing->Configure(Next.Committed.GetValue())&&Landing->FinishPreparation()))return false;
    const auto& C=Chunk->GetDescriptor();
    auto* Runner=W.World->SpawnActor<ABootRunner>(C.WorldEntry.TransformPosition(FVector(350.,0.,98.)),C.WorldEntry.Rotator());
    auto* Movement=Runner->GetParkour();Movement->bRunPhysicsWithNoController=true;Movement->SetMovementMode(MOVE_Walking);Movement->ApplyTraversalEnvelope(E);
    for(int32 I=0;I<12;++I)W.Step(Dt);
    if(!Test.TestTrue(TEXT("capsule settles on real fixture support"),Movement->IsMovingOnGround()))return false;
    Movement->Velocity=C.WorldEntry.GetUnitAxis(EAxis::X)*Speed;
    TArray<FVector> Path=Optional?C.Recipe.OptionalBranch:C.Recipe.Baseline;
    if(Path.Num()<2)return false;
    for(auto& Point:Path)Point=C.WorldTransform.TransformPosition(Point);
    Path.Add(Next.Committed->WorldEntry.TransformPosition(FVector(350.,0.,0.)));
    int32 Waypoint=1,Contacts=0;bool Activated=false,Carried=false,Committed=false;TSet<int32> Actions;
    Chunk->OnRunThreatContact.AddLambda([&](ACharacter*,const FBootOperationKey&){++Contacts;});
    ABootSurfWindow* Ride=nullptr;
    if(Optional||C.Recipe.BaselineMoves.ContainsByPredicate([](const auto& Move){return Move.Kind==EBootBaselineMove::Surf;}))
        if(!Chunk->GetWindows().IsEmpty())Ride=Chunk->GetWindows()[0];
    FBootRouteCursor Cursor;Cursor.Reset();TArray<FBootCommittedChunk> Live{C,Next.Committed.GetValue()};FVector Previous=Runner->GetActorLocation();
    double PathCm=0.;for(int32 I=1;I<Path.Num();++I)PathCm+=FVector::Distance(Path[I-1],Path[I]);
    const int32 Limit=int32(FMath::Max(30.,PathCm/Speed+20.)/Dt);
    bool Reached=false;
    for(int32 Frame=0;Frame<Limit&&!Runner->IsDead();++Frame)
    {
        const FVector Position=Runner->GetActorLocation(),Local=C.WorldTransform.InverseTransformPosition(Position);
        for(int32 I=0;I<C.Recipe.BaselineMoves.Num();++I)
        {
            const auto& Action=C.Recipe.BaselineMoves[I];if(Actions.Contains(I)||Local.X<Action.Start.X)continue;
            if(Action.Kind==EBootBaselineMove::Jump){Runner->RequestJump();Actions.Add(I);}
            if(Action.Kind==EBootBaselineMove::Slide){Runner->StartSlide();Actions.Add(I);}
        }
        bool WaitForRide=false;
        if(Ride)
        {
            if(!Activated&&Runner->GetMovementBase()==Ride->GetRootComponent())Activated=Ride->Activate(Runner,false);
            Carried|=Ride->HasCarried(Runner);
            WaitForRide=Activated&&Ride->IsMoving();
            if(Activated&&!WaitForRide&&Optional&&!C.Recipe.Rails.IsEmpty())
                while(Waypoint<Path.Num()-1&&C.WorldTransform.InverseTransformPosition(Path[Waypoint]).X<=C.Recipe.Rails[0].End.X)++Waypoint;
            if(!Committed&&Chunk->IsPreparedLanding(Ride,Runner))Committed=Ride->CommitRide(Runner);
        }
        while(Waypoint<Path.Num()-1&&FVector::Dist2D(Position,Path[Waypoint])<FMath::Max(140.,double(Speed*Dt*1.3f)))++Waypoint;
        if(!WaitForRide)
        {
            const FVector Direction=(Path[Waypoint]-Position).GetSafeNormal2D();
            Runner->SetActorRotation(Direction.Rotation());Runner->AddMovementInput(Direction,1.f,true);
        }
        Chunk->UpdateApproach(Position,E);W.Step(Dt);
        Cursor.Advance(Previous,Runner->GetActorLocation(),Live);Previous=Runner->GetActorLocation();
        if(Cursor.CurrentInstanceId==2&&Movement->IsMovingOnGround()&&Landing->OwnsSupport(Runner->GetMovementBase())){Reached=true;break;}
    }
    const FString Label=FString::Printf(TEXT("%s speed%.0f fps%d optional%d xyz%s"),*Definition.Id.ToString(),Speed,Fps,Optional,*Runner->GetActorLocation().ToString());
    Test.TestTrue(*Label,Reached);Test.TestEqual(TEXT("baseline bypass avoids actual process contact"),Contacts,0);
    Test.TestTrue(TEXT("native child actors bounded"),Chunk->GetGameplayChildCount()<=8);
    if(Ride){Test.TestTrue(TEXT("offbeat native activation accepted"),Activated);Test.TestTrue(TEXT("real movement base carried capsule"),Carried);Test.TestTrue(*FString::Printf(TEXT("offbeat ride commits on prepared native landing: %s blocked%d platform%s end%s"),*Label,Ride->WasRideBlocked(),*Ride->GetActorLocation().ToString(),*Ride->GetEndTop().ToString()),Committed);}
    return Reached;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootEndlessNativeMatrixV04,"Bootloop.V04.World.NativeCatalogFixtures",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootEndlessNativeMatrixV04::RunTest(const FString&)
{
    for(const auto& Definition:FBootChunkCatalog::Build())
        for(float Speed:{770.f,1100.f,3360.f})for(int32 Fps:{30,60,120})
            if(!BootEndlessFixture::Traverse(*this,Definition,Speed,Fps,false))return false;
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootEndlessNativeBranchV04,"Bootloop.V04.World.NativeBranchAndIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootEndlessNativeBranchV04::RunTest(const FString&)
{
    for(const auto& Definition:FBootChunkCatalog::Build())if(!Definition.Recipe.OptionalBranch.IsEmpty())
        for(int32 Fps:{30,60,120})if(!BootEndlessFixture::Traverse(*this,Definition,1100.f,Fps,true))return false;
    BootEndlessFixture::FWorld W;auto* Window=W.World->SpawnActor<ABootSurfWindow>();FBootOperationKey Key;Key.Run.RunId=FGuid(9,8,7,6);Key.ChunkId=int64(MAX_int32)+500;Key.LocalId=17;
    Window->SetRunOperation(Key);Window->Configure(FVector::ZeroVector,FVector(1000.,0.,0.),2.f,INDEX_NONE);
    TestTrue(TEXT("native operation keeps full int64 identity across configuration"),Window->GetRunOperation()&&Window->GetRunOperation()->ChunkId==Key.ChunkId);
    TestEqual(TEXT("v04 operation never truncates into legacy ID"),Window->GetWindowId(),INDEX_NONE);
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootEndlessDormantPopupV04,"Bootloop.V04.World.DormantPopupArming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootEndlessDormantPopupV04::RunTest(const FString&)
{
    BootEndlessFixture::FWorld W;auto* Popup=W.World->SpawnActor<ABootPopupProcess>();
    Popup->SetRunActive(false);Popup->ConfigureTelegraph(1.2);
    Popup->ConfigurePopup(FTransform(FVector(0.,0.,80.)),FVector(1200.,0.,0.),FVector(1200.,0.,80.),4.f,INDEX_NONE,0);
    const FVector Start=Popup->GetActorLocation();
    for(int32 I=0;I<120;++I)W.Step(1.f/60.f);
    TestTrue(TEXT("dormant process does not advance"),Popup->GetActorLocation().Equals(Start,.01));
    Popup->SetRunActive(true);
    for(int32 I=0;I<60;++I)W.Step(1.f/60.f);
    TestTrue(TEXT("configured warning lasts at least 1.2 seconds"),Popup->GetActorLocation().Equals(Start,.01));
    for(int32 I=0;I<90;++I)W.Step(1.f/60.f);
    TestTrue(TEXT("armed native hostile popup advances after warning"),Popup->GetActorLocation().X>Start.X+50.);
    Popup->SetRunActive(false);const FVector Paused=Popup->GetActorLocation();
    for(int32 I=0;I<120;++I)W.Step(1.f/60.f);
    TestTrue(TEXT("hostile pause preserves physical phase"),Popup->GetActorLocation().Equals(Paused,.01));
    Popup->SetRunActive(true);for(int32 I=0;I<30;++I)W.Step(1.f/60.f);
    TestTrue(TEXT("hostile resume continues same phase"),Popup->GetActorLocation().X>Paused.X+50.);
    return true;
}



