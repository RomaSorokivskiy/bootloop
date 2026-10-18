#include "Misc/AutomationTest.h"
#include "Music/BootMusicSoundGenerator.h"
#include "Music/BootMusicQueue.h"
#include "Music/BootBeatJudge.h"
#include "Async/Async.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
 FBootRunToken Run(uint32 Generation=1) { return {FGuid(1,2,3,4),Generation}; }
 FBootPlaybackSource Source(const TCHAR* Hash, int64 Frames, double Bpm)
 {
  FBootPlaybackSource S; S.Hash=Hash; S.Frames=Frames; S.Channels=2;
  auto A=MakeShared<FBootTrackAnalysis,ESPMode::ThreadSafe>();
  A->TrackHash=Hash; A->CanonicalFrames=Frames; A->DurationSeconds=double(Frames)/48000.;
  A->Grid={Bpm,0.}; A->Source=EBootGridSource::Authored;
  A->Energy={.3f,.9f}; A->OnsetDensity={.2f,.7f}; A->SectionStartSeconds={0.,.5};
  S.Analysis=A; return S;
 }
 bool Feed(FBootMusicRenderState& State,int32 Slot,const FBootPlaybackSource& S,uint64 Id=1)
 {
  if(!State.PrepareSlot(Slot,S,Id)) return false;
  TArray<int16> Pcm; Pcm.Init(8192,static_cast<int32>(S.Frames)*S.Channels);
  if(State.WriteFrames(Slot,Pcm.GetData(),static_cast<int32>(S.Frames))!=S.Frames) return false;
  State.MarkEof(Slot); State.PublishReady(Slot); return true;
 }
 void Render(FBootMusicRenderState& State,int32 Frames,int32 Rate=48000)
 { TArray<float> Out;Out.SetNumUninitialized(Frames*2);State.Render(Out.GetData(),Out.Num(),Rate); }
 FBootLibraryEntry Entry(const TCHAR* Hash,bool Included=true)
 { FBootLibraryEntry E; E.Hash=Hash;E.State=EBootLibraryState::Ready;E.bIncluded=Included;return E; }
}

// Catches decode/write-cursor clock authority, pause advancing, and stale render state after Retry.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTransportCursorTest,"Bootloop.V04.Transport.CursorPauseRetry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootTransportCursorTest::RunTest(const FString&)
{
 FBootMusicRenderState State(Run());auto S=Source(TEXT("first"),96000,130.);
 if(!TestTrue(TEXT("stream accepted"),Feed(State,0,S))) return false;
 TestFalse(TEXT("prefetch is not playback clock"),State.Snapshot().bValid);
 Render(State,4800); auto Before=State.Snapshot();
 TestTrue(TEXT("render publishes valid timeline"),Before.bValid);
 TestEqual(TEXT("rendered source frame"),Before.RenderedSourceFrame,int64(4800));
 TestTrue(TEXT("run identity"),Before.Run==Run());
 State.SetPaused(true);Render(State,9600);auto After=State.Snapshot();
 TestEqual(TEXT("paused cursor"),After.RenderedSourceFrame,Before.RenderedSourceFrame);
 TestTrue(TEXT("pause visible"),After.bPaused);
 State.SetPaused(false);Render(State,4800);
 TestEqual(TEXT("resume continues without restart"),State.Snapshot().RenderedSourceFrame,int64(9600));
 State.Stop();Render(State,4800);
 TestFalse(TEXT("stopped callback invalidated"),State.Snapshot().bValid);
 FBootMusicRenderState Retry(Run(2));TestTrue(TEXT("retry prepares own source"),Feed(Retry,0,S));Render(Retry,480);
 TestTrue(TEXT("new generation only"),Retry.Snapshot().Run==Run(2));
 TestEqual(TEXT("fresh retry cursor"),Retry.Snapshot().RenderedSourceFrame,int64(480));
 return true;
}

// Catches source/device rate confusion and beat thresholds derived from a different clock.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTransportRateTest,"Bootloop.V04.Transport.DeviceRate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootTransportRateTest::RunTest(const FString&)
{
 FBootMusicRenderState At48(Run()),At44(Run());auto S=Source(TEXT("tone"),96000,130.);
 if(!TestTrue(TEXT("48k feed"),Feed(At48,0,S))||!TestTrue(TEXT("44k feed"),Feed(At44,0,S)))return false;
 Render(At48,48000,48000);Render(At44,44100,44100);
 TestEqual(TEXT("48k one second"),At48.Snapshot().RenderedSourceFrame,int64(48000));
 TestEqual(TEXT("44.1k one second"),At44.Snapshot().RenderedSourceFrame,int64(48000));
 auto Snap=At48.Snapshot();Snap.Grid={130.,.237};Snap.LocalSeconds=.317;
 TestEqual(TEXT("perfect edge"),FBootBeatJudge::Rate(Snap,0.),EBootBeatRating::Perfect);
 Snap.LocalSeconds=.3171;TestEqual(TEXT("after perfect"),FBootBeatJudge::Rate(Snap,0.),EBootBeatRating::Good);
 Snap.LocalSeconds=.387;TestEqual(TEXT("good edge"),FBootBeatJudge::Rate(Snap,0.),EBootBeatRating::Good);
 Snap.LocalSeconds=.3871;TestEqual(TEXT("off beat edge"),FBootBeatJudge::Rate(Snap,0.),EBootBeatRating::OffBeat);
 Snap.LocalSeconds=.337;TestEqual(TEXT("explicit input calibration"),FBootBeatJudge::Rate(Snap,100.),EBootBeatRating::Perfect);
 Snap.LocalSeconds=.237;TestEqual(TEXT("pulse grid origin"),FBootBeatJudge::Pulse(Snap),1.f);
 Snap.bPaused=true;TestEqual(TEXT("paused judging disabled"),FBootBeatJudge::Rate(Snap,0.),EBootBeatRating::OffBeat);
 return true;
}

// Catches disabled prefetched playback and bag mutation/no-repeat regressions.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTransportQueueTest,"Bootloop.V04.Transport.QueueMutation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootTransportQueueTest::RunTest(const FString&)
{
 FBootMusicQueue Q;FRandomStream Random(1515);TArray<FBootLibraryEntry> Entries={Entry(TEXT("a")),Entry(TEXT("b")),Entry(TEXT("hidden"),false)};
 FString Current;TSet<FString> FirstCycle;
 for(int32 I=0;I<12;++I)
 {
  auto Pick=Q.ChooseNext(Entries,Current,Random);
  if(!TestTrue(TEXT("ready choice"),Pick.IsSet()))return false;
  TestTrue(TEXT("selected only"),Pick.GetValue()==TEXT("a")||Pick.GetValue()==TEXT("b"));
  TestNotEqual(TEXT("no immediate repeat with alternatives"),Pick.GetValue(),Current);
  if(I<2)FirstCycle.Add(Pick.GetValue());Current=Pick.GetValue();
 }
 TestEqual(TEXT("bag first cycle"),FirstCycle.Num(),2);
 Entries={Entry(TEXT("a"))};TestEqual(TEXT("one track repeats"),Q.ChooseNext(Entries,TEXT("a"),Random).Get(FString()),FString(TEXT("a")));
 Entries[0].bIncluded=false;TestFalse(TEXT("all disabled gives fallback"),Q.ChooseNext(Entries,TEXT("a"),Random).IsSet());
 FBootMusicRenderState State(Run());auto A=Source(TEXT("a"),480,96.),B=Source(TEXT("b"),480,168.);
 if(!TestTrue(TEXT("current prepared"),Feed(State,0,A,1))||!TestTrue(TEXT("next prepared"),Feed(State,1,B,2)))return false;
 Render(State,240);State.SetAllowed(0,false);State.SetAllowed(1,false);Render(State,240);Render(State,480);
 const auto Bridge=State.Snapshot();TestTrue(TEXT("safe fallback after removal"),Bridge.bSafeGrid);
 TestTrue(TEXT("fallback honest underflow flag"),Bridge.bUnderflow);
 TestTrue(TEXT("disabled next not played"),Bridge.TrackHash.IsEmpty());
 TestEqual(TEXT("safe bridge tempo"),Bridge.EffectiveBpm,130.);
 TestTrue(TEXT("current removal preserves run"),Bridge.Run==Run());
 return true;
}

// Catches transition resets, stale grid metadata, and unreported underruns.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTransportTransitionTest,"Bootloop.V04.Transport.Transition",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootTransportTransitionTest::RunTest(const FString&)
{
 FBootMusicRenderState State(Run());auto A=Source(TEXT("96"),480,96.),B=Source(TEXT("130"),480,130.),C=Source(TEXT("168"),480,168.);
 if(!TestTrue(TEXT("first"),Feed(State,0,A,1))||!TestTrue(TEXT("second"),Feed(State,1,B,2)))return false;
 Render(State,240);auto One=State.Snapshot();TestEqual(TEXT("96 tempo"),One.EffectiveBpm,96.);
 Render(State,480);auto Two=State.Snapshot();TestEqual(TEXT("130 tempo"),Two.EffectiveBpm,130.);
 TestEqual(TEXT("second local cursor"),Two.RenderedSourceFrame,int64(240));TestTrue(TEXT("epoch increments"),Two.TrackEpoch>One.TrackEpoch);
 TestTrue(TEXT("first slot retired"),State.SlotState(0)==EBootPcmSlotState::Retired);State.RetireSlot(0);
 TestTrue(TEXT("third"),Feed(State,0,C,3));Render(State,480);auto Three=State.Snapshot();
 TestEqual(TEXT("168 tempo"),Three.EffectiveBpm,168.);TestTrue(TEXT("third epoch increments"),Three.TrackEpoch>Two.TrackEpoch);
 TestTrue(TEXT("run identity survives all tracks"),Three.Run==Run());
 const auto D=State.Diagnostics();TestEqual(TEXT("fixed ring allocation"),D.TotalRingBytes,int64(3072000));
 TestEqual(TEXT("no underflow with next ready"),D.UnderflowBlocks,int64(0));
 FBootMusicRenderState Starved(Run());auto Long=Source(TEXT("starve"),48000,130.);
 TestTrue(TEXT("starve source"),Starved.PrepareSlot(0,Long,1));TArray<int16>Pcm;Pcm.Init(4096,960);
 TestEqual(TEXT("short prebuffer"),Starved.WriteFrames(0,Pcm.GetData(),480),480);Starved.PublishReady(0);
 Render(Starved,960);const auto Stalled=Starved.Snapshot();
 TestTrue(TEXT("underflow reported"),Stalled.bUnderflow);TestTrue(TEXT("underflow counted"),Starved.Diagnostics().UnderflowBlocks>0);
 TestTrue(TEXT("missing samples never counted as heard"),Stalled.RenderedSourceFrame<=480);
 const auto BeforeRefill=Stalled.RenderedSourceFrame;Starved.WriteFrames(0,Pcm.GetData(),480);Render(Starved,240);
 TestEqual(TEXT("refill resumes same source position"),Starved.Snapshot().RenderedSourceFrame,BeforeRefill+240);
 return true;
}
// Catches a delayed create or clear accidentally borrowing the next run's sole render writer.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTransportHandoffTest,"Bootloop.V04.Transport.GenerationHandoff",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootTransportHandoffTest::RunTest(const FString&)
{
 FBootMusicGeneratorHandoff H;auto A=MakeShared<FBootMusicRenderState,ESPMode::ThreadSafe>(Run(1));
 auto B=MakeShared<FBootMusicRenderState,ESPMode::ThreadSafe>(Run(2));
 H.Publish(Run(1),A);
 if(!TestTrue(TEXT("first generation create receives its state"),H.Acquire(Run(1))==A))return false;
 auto DelayedA=H.Acquire(Run(1));A->Stop();H.Clear(Run(1));H.Publish(Run(2),B);
 H.Clear(Run(1));TestTrue(TEXT("stale clear preserves new generation"),H.Acquire(Run(2))==B);
 TestFalse(TEXT("delayed old create never receives new state"),H.Acquire(Run(1)).IsValid());
 TestTrue(TEXT("old generator remains stopped"),DelayedA->IsStopped());
 TestTrue(TEXT("native create gets current only after control fence"),H.AcquireCurrent()==B);return true;
}
// Catches old prefetched grids surviving a library revision and mutation touching the playing lease.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTransportGridRevisionTest,"Bootloop.V04.Transport.PrefetchGridRevision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootTransportGridRevisionTest::RunTest(const FString&)
{
 for(bool SameSong:{false,true})
 {
  FBootMusicRenderState S(Run());auto A=Source(TEXT("a"),480,130.);
  auto Old=Source(SameSong?TEXT("a"):TEXT("b"),480,96.);
  auto New=Source(SameSong?TEXT("a"):TEXT("b"),480,168.);
  if(!TestTrue(TEXT("current prepared"),Feed(S,0,A,1))||!TestTrue(TEXT("prefetch prepared"),Feed(S,1,Old,2)))return false;
  Render(S,240);TestFalse(TEXT("revision cannot cancel playing song"),S.InvalidatePending(0));
  if(!TestTrue(TEXT("stale prefetched grid invalidated"),S.InvalidatePending(1)))return false;
  S.RetireSlot(1);TestTrue(TEXT("updated grid re-acquired"),Feed(S,1,New,3));
  TestEqual(TEXT("current grid preserved"),S.Snapshot().EffectiveBpm,130.);
  Render(S,480);TestEqual(TEXT("next playback uses edited BPM"),S.Snapshot().EffectiveBpm,168.);
 }
 return true;
}
// Catches cleared metadata being returned as a valid old epoch before the next render publication.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTransportSnapshotRetireTest,"Bootloop.V04.Transport.RetiredSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootTransportSnapshotRetireTest::RunTest(const FString&)
{
 FBootMusicRenderState S(Run());auto A=Source(TEXT("a"),480,96.),B=Source(TEXT("b"),384000,168.);
 if(!TestTrue(TEXT("short current"),Feed(S,0,A,1))||!TestTrue(TEXT("next ring"),Feed(S,1,B,2)))return false;
 Render(S,240);
 TArray<float> Output;Output.SetNumUninitialized(8000000);
 auto Rendering=Async(EAsyncExecution::Thread,[&]{S.Render(Output.GetData(),Output.Num(),48000);});
 const double Deadline=FPlatformTime::Seconds()+5.;
 while(S.SlotState(0)!=EBootPcmSlotState::Retired&&!Rendering.IsReady()&&FPlatformTime::Seconds()<Deadline)FPlatformProcess::YieldThread();
 S.RetireSlot(0);const auto Between=S.Snapshot();
 TestFalse(TEXT("valid publication never mixes cleared metadata"),Between.bValid&&Between.TrackEpoch==1&&Between.TrackHash.IsEmpty());
 Rendering.Get();return true;
}
// Catches permanent initial starvation being disguised as an indefinitely pending prebuffer.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTransportStartupFailureTest,"Bootloop.V04.Transport.StartupFailure",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootTransportStartupFailureTest::RunTest(const FString&)
{
 FBootMusicRenderState S(Run());Render(S,480);
 TestFalse(TEXT("pending prebuffer has no fabricated timeline"),S.Snapshot().bValid);
 S.RequestSafeFallback();Render(S,480);const auto Fallback=S.Snapshot();
 TestTrue(TEXT("terminal startup failure enters explicit fallback"),Fallback.bValid&&Fallback.bSafeGrid&&Fallback.bUnderflow);
 TestTrue(TEXT("failure is counted"),S.Diagnostics().UnderflowBlocks>0);
 TestEqual(TEXT("fallback source is metronome"),Fallback.TrackHash,FString());return true;
}
#endif
