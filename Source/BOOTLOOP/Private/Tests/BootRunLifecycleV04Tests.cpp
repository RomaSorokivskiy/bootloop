#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Run/BootRunCoordinator.h"
// Catches song-boundary run reset, paused accrual, stale callbacks and retry build leakage.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootRunLifecycleV04Test, "Bootloop.V04.Lifecycle.RetryAndTrackEpoch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootRunLifecycleV04Test::RunTest(const FString&)
{
    auto* C = NewObject<UBootRunCoordinator>(); auto Run = C->BeginRunAt(19, 100.); FBootMusicContext Music;
    Music.TrackEpoch = 1; Music.EffectiveBpm = 130.; C->Update(110., 20., Music);
    TestEqual(TEXT("active clock"), C->Snapshot().ActiveSeconds, 10.); TestEqual(TEXT("distance score"), C->Snapshot().Score, 200LL);
    C->SetPaused(true, 110.); C->Update(200., 100., Music);
    TestEqual(TEXT("pause freezes clock"), C->Snapshot().ActiveSeconds, 10.); TestEqual(TEXT("pause freezes distance"), C->Snapshot().DistanceMetres, 20.);
    C->SetPaused(false, 200.); Music.TrackEpoch = 2; Music.EffectiveBpm = 96.; C->Update(215., 100., Music);
    TestTrue(TEXT("song keeps token"), C->Snapshot().Run == Run); TestEqual(TEXT("song keeps score"), C->Snapshot().Score, 1000LL);
    TestEqual(TEXT("song clock continuous"), C->Snapshot().ActiveSeconds, 25.);
    TestEqual(TEXT("slower tempo does not mutate active commitment"), C->Snapshot().Envelope.RunCmps, 1100.f);
    TestTrue(TEXT("slower tempo proposed"), C->Snapshot().bPendingMutation);
    const auto Old = Run;
    for (int32 I = 0; I < 1000; ++I)
    {
        Run = C->BeginRunAt(I, 300. + I); C->SetRecoveryReady(true); C->Update(325. + I, 100., FBootMusicContext{});
        TestTrue(TEXT("retry generates distinct identity"), Run != Old);
        TestTrue(TEXT("fresh run offers"), C->Snapshot().CardOffer.IsSet());
        if (!C->Snapshot().CardOffer.IsSet()) return false;
        FText Error; const auto Offer = C->Snapshot().CardOffer.GetValue();
        TestFalse(TEXT("old callback cannot choose"), C->TryChoose({Old, Offer.OfferId, 0, uint64(I+1), 25.}, Error));
        TestTrue(TEXT("fresh retry choice"), C->TryChoose({Run, Offer.OfferId, 0, uint64(I+1), 25.}, Error));
        C->RegisterChunk(1, {{Run, 1, 0}});
        TestFalse(TEXT("stale operation rejected"), C->AwardOperation({Old, 1, 0}, EBootBeatRating::Perfect));
        TestTrue(TEXT("registered current operation"), C->AwardOperation({Run, 1, 0}, EBootBeatRating::Perfect));
        C->EndRun(); TestFalse(TEXT("death clears build"), C->Snapshot().bPendingMutation);
        TestEqual(TEXT("death clears cards"), C->Snapshot().ActiveModifiers.Stacks.Num(), 0);
    }
    C->Reset(); TestFalse(TEXT("reset inactive"), C->Snapshot().bRunning); TestEqual(TEXT("reset score"), C->Snapshot().Score, 0LL);
    return true;
}
#endif
