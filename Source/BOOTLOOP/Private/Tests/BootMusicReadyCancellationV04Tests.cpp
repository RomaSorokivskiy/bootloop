#include "Misc/AutomationTest.h"
#include "Music/BootMusicSoundGenerator.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS
// Catches worker publication resurrecting a slot cancelled after its initial Filling observation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTransportReadyCancellationTest,"Bootloop.V04.Transport.ReadyCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootTransportReadyCancellationTest::RunTest(const FString&)
{
 FBootMusicRenderState State({FGuid(9,9,9,9),1});FBootPlaybackSource Source;
 Source.Hash=TEXT("cancelled-prefetch");Source.Frames=48000;Source.Channels=2;
 auto Analysis=MakeShared<FBootTrackAnalysis,ESPMode::ThreadSafe>();Analysis->Grid={130.,0.};Source.Analysis=Analysis;
 if(!TestTrue(TEXT("filling source prepared"),State.PrepareSlot(0,Source,1)))return false;
 TArray<int16> Pcm;Pcm.Init(1234,960);TestEqual(TEXT("worker has initial PCM"),State.WriteFrames(0,Pcm.GetData(),480),480);
 std::atomic<bool> ObservedFilling{false},ReleaseWorker{false};
 State.BeforeReadyCommitAudit=[&]
 {
  ObservedFilling.store(true,std::memory_order_release);
  while(!ReleaseWorker.load(std::memory_order_acquire))FPlatformProcess::YieldThread();
 };
 auto Publishing=Async(EAsyncExecution::Thread,[&]{State.PublishReady(0);});
 const double Deadline=FPlatformTime::Seconds()+5.;
 while(!ObservedFilling.load(std::memory_order_acquire)&&FPlatformTime::Seconds()<Deadline)FPlatformProcess::YieldThread();
 const bool Observed=ObservedFilling.load(std::memory_order_acquire);
 if(Observed)TestTrue(TEXT("GT cancels pending Filling source"),State.InvalidatePending(0));
 ReleaseWorker.store(true,std::memory_order_release);Publishing.Get();State.BeforeReadyCommitAudit=nullptr;
 TestTrue(TEXT("publication barrier reached"),Observed);
 TestTrue(TEXT("cancelled source cannot be resurrected Ready"),State.SlotState(0)==EBootPcmSlotState::Retired);
 TArray<float> Output;Output.SetNumUninitialized(960);State.Render(Output.GetData(),Output.Num(),48000);
 TestFalse(TEXT("cancelled prefetch never obtains a playback epoch"),State.Snapshot().bValid);
 return true;
}
#endif
