#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tutorial/BootFirstRunTrack.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Tutorial/BootFirstRunTutorial.h"
#include "Profile/BootProfile.h"
#include "Player/BootAbilityComponent.h"
#include "Movement/BootTraversalEnvelope.h"
namespace
{
FBootRunToken FirstRunToken() { FBootRunToken T; T.RunId=FGuid::NewGuid(); T.Generation=1; return T; }
FBootTutorialEvidence Evidence(const FBootFirstRunTutorial& T,EBootFirstRunEvent E,uint64 S)
{ return {T.GetRunToken(),T.GetLesson(),T.GetSection(),S,E}; }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootFirstRunEvidenceTest,"Bootloop.V04.FirstRun.LessonEvidence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootFirstRunEvidenceTest::RunTest(const FString&)
{
    using E=EBootFirstRunEvent; FBootFirstRunTutorial T; const auto Run=FirstRunToken(); T.ResetLesson(Run,0,1);
    TestTrue(TEXT("actual move accepted"),T.Accept(Evidence(T,E::Moved,1)));
    TestFalse(TEXT("wrong objective rejected"),T.Accept(Evidence(T,E::SurfCompleted,2)));
    TestFalse(TEXT("one action cannot satisfy two objectives"),T.Accept(Evidence(T,E::Jumped,1)));
    auto Old=Evidence(T,E::Jumped,3); auto Other=Old; Other.Run=FirstRunToken();
    TestFalse(TEXT("other run rejected"),T.Accept(Other));
    Other=Old; ++Other.Run.Generation; TestFalse(TEXT("old run generation rejected"),T.Accept(Other));
    TestTrue(TEXT("jump advances move lesson"),T.Accept(Old));
    TestEqual(TEXT("wheel lesson"),T.GetLesson(),1);
    TestFalse(TEXT("old lesson"),T.Accept(Old));
    for(const auto Event:{E::AirSlideLanded,E::WallKick,E::SurfCompleted})
    {
        const int32 L=Event==E::AirSlideLanded?2:Event==E::WallKick?3:5;
        T.ResetLesson(Run,L,10);
        if(L==2) T.Accept(Evidence(T,E::GroundSlide,100));
        if(L==3) T.Accept(Evidence(T,E::Wallrun,100));
        const auto Delayed=Evidence(T,Event,110+L);
        T.ResetLesson(Run,L,11);
        if(L==2) T.Accept(Evidence(T,E::GroundSlide,120));
        if(L==3) T.Accept(Evidence(T,E::Wallrun,120));
        TestFalse(TEXT("delayed previous-section action rejected at matching objective"),T.Accept(Delayed));
        auto WrongLesson=Evidence(T,Event,130+L); --WrongLesson.Lesson;
        TestFalse(TEXT("old lesson cannot complete matching current objective"),T.Accept(WrongLesson));
        TestTrue(TEXT("fresh section evidence is accepted"),T.Accept(Evidence(T,Event,140+L)));
    }
    T=FBootFirstRunTutorial(); T.ResetLesson(Run,0,1); uint64 Serial=20;
    for(int32 L=0;L<9;++L) for(const E Event:FBootFirstRunTutorial::Objectives(L))
        TestTrue(TEXT("ordered off-beat evidence progresses"),T.Accept(Evidence(T,Event,++Serial)));
    TestTrue(TEXT("all nine lessons complete"),T.IsComplete());
    TestFalse(TEXT("completed tutorial accepts no callbacks"),T.Accept(Evidence(T,E::MusicFlowAcknowledged,++Serial)));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootFirstRunCheckpointTest,"Bootloop.V04.FirstRun.CheckpointAndSkip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootFirstRunCheckpointTest::RunTest(const FString&)
{
    FBootFirstRunTutorial T; const auto Run=FirstRunToken(); T.ResetLesson(Run,0,1);
    T.Accept(Evidence(T,EBootFirstRunEvent::Moved,1)); T.ResetLesson(Run,0,2);
    TestEqual(TEXT("checkpoint restarts current objectives"),T.GetObjectiveIndex(),0);
    for(int32 L=0;L<9;++L) { T.SkipLesson(); TestEqual(TEXT("explicit skip remains skipped"),T.GetOutcome(L),EBootFirstRunOutcome::Skipped); }
    TestTrue(TEXT("skip can leave tutorial"),T.IsComplete());
    T=FBootFirstRunTutorial(); T.ResetLesson(Run,0,1);
    TestFalse(TEXT("replay starts clean"),T.IsComplete());
    FBootInputPreferences Inputs; Inputs.Assign(EBootInputAction::Jump,0,EKeys::J,true);
    TestTrue(TEXT("current remapped jump label"),T.GetPrompt(Inputs).ToString().Contains(Inputs.Label(EBootInputAction::Jump).ToString()));
    T.ResetLesson(Run,2,4); Inputs.Assign(EBootInputAction::Slide,0,EKeys::K,true);
    TestTrue(TEXT("current remapped slide label"),T.GetPrompt(Inputs).ToString().Contains(Inputs.Label(EBootInputAction::Slide).ToString()));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootFirstRunVersionTest,"Bootloop.V04.FirstRun.VersionAndTrialReset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootFirstRunVersionTest::RunTest(const FString&)
{
    auto* P=NewObject<UBootProfile>(); P->bTutorialComplete=true;
    TestFalse(TEXT("v03 is not v04 onboarding"),P->HasCurrentOnboarding());
    const auto TutorialRun=FirstRunToken(); auto* A=NewObject<UBootAbilityComponent>(); A->ResetRun(TutorialRun);
    auto Trial=FBootTraversalPolicy::FromTempo(130.,1); Trial.GuardCharges=1; A->ApplyEnvelope(Trial);
    TestEqual(TEXT("tutorial trial guard available"),A->Snapshot().GuardCharges,1);
    A->ResetRun(FirstRunToken());
    TestEqual(TEXT("scored run has no trial guard"),A->Snapshot().GuardCharges,0);
    TestFalse(TEXT("old trial token cannot trigger guard"),A->TryAbsorbProcess(TutorialRun,1,1.));
    TestEqual(TEXT("tutorial does not modify scored profile"),P->MusicRunRecord.BestScore,int64(0));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootFirstRunPhysicalCheckpointTest,"Bootloop.V04.FirstRun.PhysicalCheckpoints",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootFirstRunPhysicalCheckpointTest::RunTest(const FString&)
{
    UWorld::InitializationValues Init; Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
    UWorld* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    W->InitializeActorsForPlay(FURL()); W->BeginPlay(); W->GetWorldSettings()->NotifyBeginPlay();
    auto* T=W->SpawnActor<ABootFirstRunTrack>(); const auto Run=FirstRunToken(); T->BeginFirstRun(Run);
    TestEqual(TEXT("nine real local sections"),T->GetSections().Num(),9);
    for(const auto& Section:T->GetSections())
    {
        FHitResult Hit; const FVector P=Section.Checkpoint.GetLocation();
        TestTrue(TEXT("checkpoint is supported by real floor"),W->LineTraceSingleByChannel(Hit,P,P-FVector(0,0,200),ECC_Visibility));
        TestFalse(TEXT("full upright capsule clears every checkpoint"),W->OverlapBlockingTestByChannel(P,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34.f,96.f)));
        const FVector Bypass=Section.Frame.TransformPosition(FVector(2300,-1250,100));
        TestTrue(TEXT("explicit safe bypass has real floor"),W->LineTraceSingleByChannel(Hit,Bypass,Bypass-FVector(0,0,350),ECC_Visibility));
    }
    auto Start=T->PrepareCheckpoint(Run); TestTrue(TEXT("validated checkpoint is above floor"),Start.GetLocation().Z>80.f);
    if(!T->GetSections().IsEmpty())
    {
        auto* Blocker=W->SpawnActor<AActor>(); auto* Obstruction=NewObject<UBoxComponent>(Blocker);
        Blocker->SetRootComponent(Obstruction); Obstruction->SetBoxExtent(FVector(90,90,200));
        Obstruction->SetCollisionProfileName(TEXT("BlockAll")); Obstruction->RegisterComponent();
        Blocker->SetActorLocation(T->GetSections()[0].Checkpoint.GetLocation());
        const auto Alternate=T->PrepareCheckpoint(Run);
        TestFalse(TEXT("blocked authored checkpoint selects safe alternative"),Alternate.GetLocation().Equals(Start.GetLocation(),1.f));
        TestFalse(TEXT("alternate clears actual upright capsule"),W->OverlapBlockingTestByChannel(Alternate.GetLocation(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34.f,96.f)));
        TestTrue(TEXT("fallback stays in current local lesson"),FMath::Abs(Alternate.GetLocation().Y)<1800.);
    }
    W->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(W); W->DestroyWorld(false);
    return true;
}
#endif




