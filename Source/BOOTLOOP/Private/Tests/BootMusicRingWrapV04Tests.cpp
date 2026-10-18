#include "Misc/AutomationTest.h"
#include "Music/BootMusicSoundGenerator.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTransportRingWrapTest,"Bootloop.V04.Transport.ConcurrentRingWrap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootTransportRingWrapTest::RunTest(const FString&)
{
 // 32 seconds consumes four complete 8s ring capacities; the last sample proves wrap data survived.
 for(int32 Rate:{48000,44100})
 {
  FBootRunToken Token{FGuid(7,7,7,7),1};FBootMusicRenderState State(Token);
  FBootPlaybackSource Source;Source.Hash=TEXT("wrap");Source.Frames=1600000;Source.Channels=2;
  auto Analysis=MakeShared<FBootTrackAnalysis,ESPMode::ThreadSafe>();Analysis->Grid={130.,0.};Source.Analysis=Analysis;
  if(!TestTrue(TEXT("wrap prepare"),State.PrepareSlot(0,Source,1)))return false;
  TArray<int16> Prime;Prime.Init(12345,384000*2);
  TestEqual(TEXT("prime full capacity"),State.WriteFrames(0,Prime.GetData(),384000),384000);State.PublishReady(0);
  std::atomic<bool> Stop{false};
  auto Producer=Async(EAsyncExecution::Thread,[&]
  {
   TArray<int16> Block;Block.Init(12345,8192*2);
   while(!Stop.load(std::memory_order_acquire))
   {if(State.WriteFrames(0,Block.GetData(),8192)==0)FPlatformProcess::YieldThread();}
  });
  const int32 BlockFrames=Rate==48000?512:1470;
  const int32 Blocks=Rate==48000?3000:960;
  TArray<float> Out;Out.SetNumUninitialized(BlockFrames*2);
  for(int32 I=0;I<Blocks;++I)State.Render(Out.GetData(),Out.Num(),Rate);
  Stop.store(true,std::memory_order_release);Producer.Get();
  TestEqual(TEXT("four wraps preserve canonical cursor"),State.Snapshot().RenderedSourceFrame,int64(1536000));
  TestTrue(TEXT("wrapped PCM audible rather than stale/zero ring"),FMath::Abs(Out.Last()-12345.f/32768.f)<.00001f);
 }
 return true;
}
#endif
