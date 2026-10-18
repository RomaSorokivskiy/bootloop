#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Music/BootRhythmScoring.h"
#include "Music/BootRhythmComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootRhythmScoringTest, "Bootloop.Rhythm.JudgementAndDedup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBootRhythmScoringTest::RunTest(const FString& Parameters)
{
    using State = FBootRhythmScore;
    const double Beat = State::BeatSeconds;
    TestTrue(TEXT("Exact beat is Perfect"), State::Rate(0) == EBootBeatRating::Perfect);
    TestTrue(TEXT("80ms boundary is Perfect"), State::Rate(.080) == EBootBeatRating::Perfect);
    TestTrue(TEXT("After 80ms is Good"), State::Rate(.0801) == EBootBeatRating::Good);
    TestTrue(TEXT("150ms boundary is Good"), State::Rate(.150) == EBootBeatRating::Good);
    TestTrue(TEXT("After 150ms is OffBeat"), State::Rate(.1501) == EBootBeatRating::OffBeat);
    TestTrue(TEXT("Before next beat wraps correctly"), State::Rate(Beat - .070) == EBootBeatRating::Perfect);
    TestTrue(TEXT("Offset compensates delayed input"), State::Rate(Beat + .100, 100.) == EBootBeatRating::Perfect);
    TestTrue(TEXT("Negative transport not valid"), State::Rate(-1) == EBootBeatRating::OffBeat);
    TestTrue(TEXT("NaN transport not valid"), State::Rate(std::numeric_limits<double>::quiet_NaN()) == EBootBeatRating::OffBeat);
    State Score;
    TestTrue(TEXT("First jump rewards"), Score.Award(EBootRhythmAction::Jump, 0));
    TestEqual(TEXT("First Perfect is 100"), Score.Score, 100);
    TestFalse(TEXT("Same jump/beat cannot farm"), Score.Award(EBootRhythmAction::Jump, .01));
    TestEqual(TEXT("Spam keeps score"), Score.Score, 100);
    TestEqual(TEXT("Spam keeps combo"), Score.Combo, 1);
    TestTrue(TEXT("Different executed action can reward on same beat"), Score.Award(EBootRhythmAction::Slide, .01));
    TestEqual(TEXT("Second combo has x1.1"), Score.Score, 210);
    TestFalse(TEXT("OffBeat has no points"), Score.Award(EBootRhythmAction::Jump, Beat * .5));
    TestEqual(TEXT("OffBeat resets combo only"), Score.Combo, 0);
    TestEqual(TEXT("OffBeat keeps prior score"), Score.Score, 210);
    TestTrue(TEXT("Next beat still rewards"), Score.Award(EBootRhythmAction::Jump, Beat));
    TestEqual(TEXT("Combo starts again at x1"), Score.Score, 310);
    TestFalse(TEXT("Out-of-order beat cannot reward"), Score.Award(EBootRhythmAction::Jump, 0));
    for (int32 I = 2; I < 100; ++I) Score.Award(EBootRhythmAction::Jump, I * Beat);
    TestEqual(TEXT("Multiplier caps at x3"), Score.LastPoints, 300);
    const int32 BeforeInvalid = Score.Score;
    TestFalse(TEXT("Infinity cannot award"), Score.Award(EBootRhythmAction::Slide, std::numeric_limits<double>::infinity()));
    TestEqual(TEXT("Invalid time keeps score"), Score.Score, BeforeInvalid);
    Score = State{};
    TestEqual(TEXT("Retry state has zero score"), Score.Score, 0);
    TestEqual(TEXT("Retry state has zero combo"), Score.Combo, 0);
    TestTrue(TEXT("Retry clears dedup for beat zero"), Score.Award(EBootRhythmAction::Jump, 0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootRhythmCallbackTest, "Bootloop.Rhythm.CallbackAndRetry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBootRhythmCallbackTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues Init;
    Init.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
        true, ERHIFeatureLevel::Num, &Init);
    AActor* Actor = World->SpawnActor<AActor>();
    UBootRhythmComponent* Rhythm = NewObject<UBootRhythmComponent>(Actor);
    UBootRhythmCommandProxy* OldProxy = NewObject<UBootRhythmCommandProxy>(Rhythm);
    OldProxy->RhythmOwner = Rhythm;
    OldProxy->ClockId = TEXT("TestRun1");
    Rhythm->ActiveClockName = OldProxy->ClockId;
    // Actual UE5.8 command payload name, deliberately different from a clock id.
    OldProxy->OnCommand(EQuartzCommandDelegateSubType::CommandOnStarted, TEXT("Quartz Event"));
    TestTrue(TEXT("Music started callback enables scoring with generic event name"), Rhythm->IsReady());
    Rhythm->Score.Award(EBootRhythmAction::Jump, 0);
    Rhythm->StopRun();
    TestFalse(TEXT("Death stops scoring readiness"), Rhythm->IsReady());
    TestEqual(TEXT("Death retains score"), Rhythm->GetScore().Score, 100);
    Rhythm->ResetRun();
    TestEqual(TEXT("Retry clears score"), Rhythm->GetScore().Score, 0);
    Rhythm->ActiveClockName = TEXT("TestRun2");
    OldProxy->OnCommand(EQuartzCommandDelegateSubType::CommandOnStarted, TEXT("Quartz Event"));
    TestFalse(TEXT("Delayed old run callback cannot enable new run"), Rhythm->IsReady());
    UBootRhythmCommandProxy* NewProxy = NewObject<UBootRhythmCommandProxy>(Rhythm);
    NewProxy->RhythmOwner = Rhythm;
    NewProxy->ClockId = Rhythm->ActiveClockName;
    NewProxy->OnCommand(EQuartzCommandDelegateSubType::CommandOnStarted, TEXT("Quartz Event"));
    TestTrue(TEXT("Current run callback still enables scoring"), Rhythm->IsReady());
    Rhythm->StopRun();
    World->DestroyWorld(false);
    return true;
}
#endif
