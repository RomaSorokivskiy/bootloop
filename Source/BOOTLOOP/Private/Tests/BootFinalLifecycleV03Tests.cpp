#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "World/Generation/BootCourseTrack.h"
#include "Run/BootGameMode.h"
#include "Player/BootRunner.h"
#include "Movement/BootMovementComponent.h"
#include "World/Threats/BootPopupProcess.h"
#include "World/Threats/BootScanProcess.h"
#include "World/Interaction/BootAssemblingPlatform.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"

namespace BootFinalLifecycleV03
{
struct FWorld
{
    UWorld* World;
    ABootGameMode* Mode;
    ABootCourseTrack* Course;
    APlayerController* PC;
    ULocalPlayer* Local;
    ABootRunner* Runner;
    explicit FWorld(bool bTraining, bool bAudio=false)
    {
        UWorld::InitializationValues Init;
        Init.AllowAudioPlayback(bAudio).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        World->GetWorldSettings()->DefaultGameMode=bTraining?ABootFirstLoginGameMode::StaticClass():ABootCircuitGameMode::StaticClass();
        World->SetGameMode(FURL());
        World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
        Mode=World->GetAuthGameMode<ABootGameMode>(); Course=CastChecked<ABootCourseTrack>(Mode->GetTrack());
        PC=World->SpawnActor<APlayerController>(); PC->SetAsLocalPlayerController();
        Local=NewObject<ULocalPlayer>(GEngine); Local->PlayerController=PC; PC->Player=Local;
        World->GetGameInstance()->AddLocalPlayer(Local,FPlatformUserId::CreateFromInternalId(0));
        Runner=World->SpawnActor<ABootRunner>(ABootRunner::StaticClass(),Course->GetStartTransform()); PC->Possess(Runner);
        Runner->GetParkour()->bRunPhysicsWithNoController=true;
        Mode->StartRun(); Step(80);
    }
    ~FWorld()
    {
        World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        Runner->Destroy(); World->GetGameInstance()->RemoveLocalPlayer(Local);
        World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    void Step(int32 Frames)
    { for(int32 I=0;I<Frames;++I) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60.f); } }
    const FBootCourseNode& Node(int32 Id) const
    { return *Course->GetNodes().FindByPredicate([Id](const FBootCourseNode& N){return N.Id==Id;}); }
    void Place(int32 Id,FVector LocalPosition)
    {
        Runner->SetActorLocation(Node(Id).Transform.TransformPosition(LocalPosition),false,nullptr,ETeleportType::TeleportPhysics);
        Course->UpdateCourse(Runner,1.f/60.f);
    }
    void Cross(int32 Id,float Z=350.f)
    { Place(Id,{-80,0,Z}); Place(Id,{80,0,Z}); }
    void ReachLesson(int32 Id)
    { while(Course->GetTutorial().CurrentLesson<Id) Mode->SkipLesson(); }
    void Land(int32 Id)
    {
        Runner->GetParkour()->PendingLaunchVelocity=FVector::ZeroVector;
        Runner->GetParkour()->Velocity=FVector::ZeroVector;
        Place(Id,{700,-500,350});
        Runner->GetParkour()->SetMovementMode(MOVE_Falling);
        Step(60); Course->UpdateCourse(Runner,1.f/60.f);
    }
    ABootPopupProcess* ArmCircuitPopup()
    {
        Runner->GetParkour()->DisableMovement();
        for(int32 Id:{11,2,3,4,5,6,7,8}) Cross(Id,100.f);
        Place(8,{1500,-700,100});
        // Prewarm the real distant destination colliders at safe staging; this
        // focused lifecycle fixture does not simulate the intervening approach.
        for(TActorIterator<ABootAssemblingPlatform> It(World);It;++It) It->FinishSafely();
        Course->UpdateCourse(Runner,1.f/60.f);
        for(TActorIterator<ABootPopupProcess> It(World);It;++It) return *It;
        return nullptr;
    }
};
UAudioComponent* FindAudioComponent(AActor* Actor,const TCHAR* Name)
{
    TArray<UAudioComponent*> Components; Actor->GetComponents(Components);
    for(auto* Component:Components) if(Component->GetFName()==FName(Name)) return Component;
    return nullptr;
}
}

// Catches subclass tick suspension leaving the inherited real looping AudioComponent active,
// and challenge death failing to suspend course-owned processes before Results.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03FriendlyPopupShutdownTest,"Bootloop.V03.FinalLifecycle.FriendlyPopupDesktopAndDeath",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV03FriendlyPopupShutdownTest::RunTest(const FString&)
{
    using namespace BootFinalLifecycleV03;
    for(bool bDeath:{false,true})
    {
        FWorld W(false,true);
        if(!TestTrue(TEXT("Regression requires a real audio-enabled world/device"),GEngine->UseSound() && W.World->GetAudioDevice().IsValid())) return false;
        auto* Popup=W.ArmCircuitPopup();
        if(!TestNotNull(TEXT("Course owns the actual authored POPUP"),Popup)) return false;
        auto* Servo=FindAudioComponent(Popup,TEXT("WindowServo")); auto* Warning=FindAudioComponent(Popup,TEXT("PopupWarning"));
        if(!TestNotNull(TEXT("Inherited servo is a real audio component"),Servo) || !TestNotNull(TEXT("POPUP warning audio exists"),Warning)) return false;
        AddInfo(FString::Printf(TEXT("POPUP fixture node=%d available=%d hostile=%d tick=%d location=%s"),
            W.Course->GetCourseState().CurrentNodeId,Popup->IsAvailable(),Popup->IsHostile(),Popup->IsActorTickEnabled(),*Popup->GetActorLocation().ToString()));
        TestTrue(TEXT("Legal course entry makes POPUP ready"),Popup->IsAvailable());
        TestTrue(TEXT("Actual hostile POPUP accepts interception"),Popup->Activate(W.Runner,false));
        TestTrue(TEXT("Interception starts inherited ride audio"),Servo->IsPlaying());
        W.Step(6);
        TestTrue(TEXT("Intercepted POPUP is moving before shutdown"),Popup->IsMoving());
        if(!bDeath)
        {
            W.Mode->PauseRun(true); const FTransform Paused=Popup->GetActorTransform(); W.Step(20);
            TestTrue(TEXT("Ordinary Pause freezes the friendly ride"),Popup->GetActorTransform().Equals(Paused,.01f));
            W.Mode->PauseRun(false); W.Step(6);
            TestFalse(TEXT("Ordinary Resume continues the same friendly ride"),Popup->GetActorTransform().Equals(Paused,.01f));
            W.Mode->PauseRun(true); W.Mode->ReturnToDesktop();
        }
        else W.Runner->Die();
        const FTransform Stopped=Popup->GetActorTransform();
        TestFalse(bDeath?TEXT("Circuit death stops inherited servo before Results"):TEXT("Pause to Desktop stops inherited servo"),Servo->IsPlaying());
        TestFalse(TEXT("Shutdown stops warning audio"),Warning->IsPlaying());
        TestFalse(TEXT("Shutdown suspends POPUP tick"),Popup->IsActorTickEnabled());
        for(TActorIterator<ABootScanProcess> It(W.World);It;++It) TestFalse(TEXT("Shutdown suspends SCAN tick"),It->IsActorTickEnabled());
        W.Step(120);
        TestTrue(TEXT("Suspended friendly platform stays at its captured transform past docking duration"),Popup->GetActorTransform().Equals(Stopped,.01f));
        TestFalse(TEXT("Suspension preserves the captured friendly platform"),Popup->IsHostile());
        W.Mode->StartRun(); W.Step(80); Popup=W.ArmCircuitPopup();
        TestTrue(TEXT("Retry restores hostile warning audio on legal re-entry"),Warning->IsPlaying() && Popup->IsHostile());
        TestTrue(TEXT("Retry restores interception"),Popup->Activate(W.Runner,false));
        TestTrue(TEXT("Retry restores inherited ride audio"),Servo->IsPlaying());
    }
    return true;
}

// Catches Runner stamping an accepted redirect with the destination lesson at landing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03RedirectOriginTest,"Bootloop.V03.FinalLifecycle.RedirectMustBeginInActiveLesson",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV03RedirectOriginTest::RunTest(const FString&)
{
    using namespace BootFinalLifecycleV03;
    FWorld W(true); W.ReachLesson(5);
    W.Mode->NotifyObjective(EBootTutorialEvent::WallKickLanded);
    TestEqual(TEXT("Source lesson is completed before takeoff"),W.Course->GetTutorial().GetOutcome(5),EBootLessonState::Completed);
    W.Place(6,{-80,0,350}); W.Runner->SetActorRotation(FRotator(0,180,0)); W.PC->SetControlRotation(FRotator(0,180,0));
    W.Runner->GetParkour()->SetMovementMode(MOVE_Falling); W.Runner->GetParkour()->Velocity=FVector(0,900,0);
    W.Runner->RequestAirRedirect();
    TestTrue(TEXT("Source lesson redirect was really accepted"),W.Runner->GetParkour()->IsAirRedirecting());
    W.Place(6,{80,0,350});
    TestEqual(TEXT("Legal gate activates destination lesson"),W.Course->GetTutorial().CurrentLesson,6);
    W.Land(6);
    TestTrue(TEXT("Delayed outcome uses a real supported landing"),W.Runner->GetParkour()->IsMovingOnGround());
    TestEqual(TEXT("Redirect begun before lesson6 cannot complete it"),W.Course->GetTutorial().GetOutcome(6),EBootLessonState::Active);
    W.Place(6,{700,-500,350}); W.Runner->GetParkour()->SetMovementMode(MOVE_Falling); W.Runner->GetParkour()->Velocity=FVector(0,900,0);
    W.Runner->RequestAirRedirect(); TestTrue(TEXT("Fresh redirect accepted in lesson6"),W.Runner->GetParkour()->IsAirRedirecting());
    W.Land(6);
    TestEqual(TEXT("Fresh redirect and landing completes active lesson6"),W.Course->GetTutorial().GetOutcome(6),EBootLessonState::Completed);
    return true;
}

// Catches a genuine manual-hop gain accepted before lesson3 activation surviving its gate.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03HopOriginTest,"Bootloop.V03.FinalLifecycle.ManualHopMustBeginInActiveLesson",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV03HopOriginTest::RunTest(const FString&)
{
    using namespace BootFinalLifecycleV03;
    FWorld W(true); W.ReachLesson(2); W.Mode->NotifyObjective(EBootTutorialEvent::SlideCompleted);
    auto Hop=[&](int32 Section)
    {
        auto* Move=W.Runner->GetParkour();
        W.Place(Section,{700,-500,350}); Move->SetMovementMode(MOVE_Falling);
        // Stage a qualifying .2-second incoming flight through production movement-mode APIs;
        // the following hop is accepted by Runner and its final landing is simulated normally.
        Move->SetComponentTickEnabled(false); W.Step(13); Move->Velocity=FVector(900,0,0);
        Move->SetMovementMode(MOVE_Walking); Move->SetComponentTickEnabled(true);
        const double Before=Move->Velocity.Size2D(); W.Runner->RequestJump();
        TestTrue(TEXT("Runner accepted an actual manual-hop speed gain"),Move->Velocity.Size2D()>Before+50.);
    };
    Hop(2); W.Cross(3); W.Land(3);
    TestEqual(TEXT("Legal gate activated hop lesson"),W.Course->GetTutorial().CurrentLesson,3);
    TestEqual(TEXT("Hop begun before lesson3 cannot complete it"),W.Course->GetTutorial().GetOutcome(3),EBootLessonState::Active);
    Hop(3); W.Land(3);
    TestEqual(TEXT("Fresh qualifying hop completes active lesson3"),W.Course->GetTutorial().GetOutcome(3),EBootLessonState::Completed);
    return true;
}

// Catches both an ongoing slide and an old airborne queue being relabelled at the new lesson.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03SlideOriginTest,"Bootloop.V03.FinalLifecycle.SlideAndAirQueueMustBeginInActiveLesson",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV03SlideOriginTest::RunTest(const FString&)
{
    using namespace BootFinalLifecycleV03;
    for(bool bAirQueued:{false,true})
    {
        FWorld W(true); W.Mode->NotifyObjective(EBootTutorialEvent::Move); W.Mode->NotifyObjective(EBootTutorialEvent::JumpLanded);
        W.Place(2,{-80,0,bAirQueued?350.f:96.f});
        auto* Move=W.Runner->GetParkour(); Move->SetMovementMode(bAirQueued?MOVE_Falling:MOVE_Walking); Move->Velocity=FVector(900,0,0);
        W.Runner->StartSlide();
        TestTrue(TEXT("Source slide request was accepted"),bAirQueued?W.Runner->bIsCrouched:W.Runner->IsSliding());
        W.Place(2,{80,0,bAirQueued?350.f:44.f});
        if(bAirQueued)
        {
            // Preserve speed through the real airborne landing into the queued slide.
            Move->Velocity=FVector(900,0,-100);
        }
        W.Step(100); W.Course->UpdateCourse(W.Runner,1.f/60.f);
        TestEqual(TEXT("Legal gate activates slide lesson"),W.Course->GetTutorial().CurrentLesson,2);
        TestEqual(bAirQueued?TEXT("Air-slide queued before lesson2 cannot complete it"):TEXT("Slide begun before lesson2 cannot complete it"),W.Course->GetTutorial().GetOutcome(2),EBootLessonState::Active);
        const FTransform Fresh(W.Node(2).Transform.GetRotation(),W.Node(2).Transform.TransformPosition({700,-500,96}));
        W.Runner->ResetToTransform(Fresh,false); Move->Velocity=FVector(900,0,0); W.Runner->StartSlide();
        TestTrue(TEXT("Fresh active-lesson slide begins"),W.Runner->IsSliding()); W.Step(50);
        TestEqual(TEXT("Fresh slide completes active lesson2"),W.Course->GetTutorial().GetOutcome(2),EBootLessonState::Completed);
    }
    return true;
}
#endif
