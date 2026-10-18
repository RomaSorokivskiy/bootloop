#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Player/BootRunner.h"
#include "Movement/BootMovementComponent.h"
#include "Music/BootRhythmComponent.h"
#include "World/Generation/BootSurfTrack.h"
#include "World/Interaction/BootSurfWindow.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV02RulesTest,"Bootloop.V02.CompletedSurfScoring",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV02RulesTest::RunTest(const FString&)
{
    FBootRhythmScore Score;
    TestTrue(TEXT("Real offbeat operation still rewarded"),Score.AwardSurf(1,EBootBeatRating::OffBeat));
    TestEqual(TEXT("Offbeat base reward"),Score.Score,75);
    TestFalse(TEXT("Same window cannot farm"),Score.AwardSurf(1,EBootBeatRating::Perfect));
    TestTrue(TEXT("Next synced operation rewarded"),Score.AwardSurf(2,EBootBeatRating::Perfect));
    TestEqual(TEXT("One synced completed operation"),Score.Combo,1);
    Score=FBootRhythmScore{};
    TestTrue(TEXT("Retry clears IDs"),Score.AwardSurf(1,EBootBeatRating::Good));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV02CourseTest,"Bootloop.V02.AuthoredLowerRouteAndRetry",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV02CourseTest::RunTest(const FString&)
{
    UWorld::InitializationValues Init;
    Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
    auto* Track=World->SpawnActor<ABootSurfTrack>();
    auto* Runner=World->SpawnActor<ABootRunner>(FVector(350,0,100),FRotator::ZeroRotator);
    auto* Move=Runner->GetParkour(); Move->bRunPhysicsWithNoController=true; Move->SetMovementMode(MOVE_Walking);
    auto Step=[&](float Dt){ ++GFrameCounter; World->Tick(LEVELTICK_All,Dt); };
    for(int32 I=0;I<20;++I) Step(1.f/60.f);
    TestEqual(TEXT("Two authored surf opportunities"),Track->GetWindows().Num(),2);
    Runner->RequestJump(); Runner->StartSlide();
    TestEqual(TEXT("Basic movement never earns rhythm points"),Runner->GetRhythm()->GetScore().Score,0);
    Runner->ResetRun();
    bool Jump1=false,Jump2=false,Slide=false;
    for(int32 I=0;I<3000 && !Runner->IsDead() && !Track->IsFinished(Runner->GetActorLocation());++I)
    {
        const FVector P=Runner->GetActorLocation();
        const double TargetY=P.X<5500?-650:P.X<9000?0:P.X<13400?650:0;
        const FVector Direction=FVector(1,FMath::Clamp((TargetY-P.Y)/250.,-.8,.8),0).GetSafeNormal();
        Runner->AddMovementInput(Direction,1,true);
        if(!Slide && P.X>7800) { Runner->StartSlide(); Slide=true; }
        if(!Jump1 && P.X>16200) { Runner->RequestJump(); Jump1=true; }
        if(!Jump2 && P.X>21700) { Runner->RequestJump(); Jump2=true; }
        Step(1.f/60.f);
    }
    AddInfo(FString::Printf(TEXT("Route end=%s dead=%d"),*Runner->GetActorLocation().ToString(),Runner->IsDead()));
    TestFalse(TEXT("Lower route playable without a SNAP or overclock"),Runner->IsDead());
    TestTrue(TEXT("Base-speed path reaches finish"),Track->IsFinished(Runner->GetActorLocation()));
    TestEqual(TEXT("All basic traversal earns zero rhythm score"),Runner->GetRhythm()->GetScore().Score,0);
    TestTrue(TEXT("Parallel route progress independent of height/side"),
        FMath::IsNearlyEqual(Track->GetRouteProgress(FVector(5000,-650,-160)),Track->GetRouteProgress(FVector(5000,450,160))));
    for(int32 I=0;I<10;++I)
    {
        Track->ResetTrack(1301995); Runner->ResetRun();
        TestTrue(TEXT("Retry returns every window to available"),Track->GetWindows()[0]->IsAvailable()&&Track->GetWindows()[1]->IsAvailable());
        TestTrue(TEXT("Retry returns runner to spawn"),Runner->GetActorLocation().Equals(FVector(350,0,100),1));
        TestFalse(TEXT("Retry clears wallrun"),Move->IsWallRunning());
        TestTrue(TEXT("Retry clears moving base"),Runner->GetMovementBaseObject()==nullptr);
    }
    World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}
#endif
