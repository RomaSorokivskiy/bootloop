#include "Music/BootMusicSoundGenerator.h"
#include "Misc/ScopeLock.h"

namespace { using AM=std::memory_order; }
struct FBootMusicRenderState::FImpl
{
 struct FSlot
 {
  TArray<int16> Pcm;
  std::atomic<EBootPcmSlotState> State{EBootPcmSlotState::Free};
  std::atomic<int64> Written{0}, Consumed{0};
  std::atomic<bool> Allowed{true}, Eof{false}, Failed{false};
  mutable FCriticalSection MetadataLock;
  FString Hash;
  FBootTrackAnalysisPtr Analysis;
  FBootBeatGrid Grid;
  int64 Frames=0;
  int32 Channels=2;
  std::atomic<uint64> RequestId{0};
  bool bSafe=true;
  FSlot() { Pcm.SetNumZeroed(CapacityFrames*2); }
 } Slots[SlotCount];
 FBootRunToken Run;
 std::atomic<bool> Paused{false},Stopped{false},FallbackRequested{false};
 // Atomic payloads are required: a sequence check alone does not prevent C++ data races.
 std::atomic<uint64> Sequence{0};
 std::atomic<int32> PublishedSlot{-1};
 std::atomic<uint64> PublishedRequest{0};
 std::atomic<int64> PublishedFrame{0},PublishedEpoch{0};
 std::atomic<bool> PublishedUnderflow{false},PublishedValid{false};
 std::atomic<int64> RenderBlocks{0},OutputFrames{0},Underflows{0};
 std::atomic<int32> DeviceRate{0};
 // The following values have exactly one writer: the sound generator.
 int32 Current=INDEX_NONE;
 int64 PhaseNumerator=0,Epoch=0;
 bool HadTrack=false,InBridge=false;
 double Fade=0.,Tail=0.;
 float LastLeft=0.f,LastRight=0.f;
 void Publish(bool Underflow)
 {
  Sequence.fetch_add(1,AM::acq_rel);
  PublishedSlot.store(Current,AM::relaxed);
  PublishedRequest.store(Current!=INDEX_NONE?Slots[Current].RequestId.load(AM::relaxed):0,AM::relaxed);
  const int32 Rate=DeviceRate.load(AM::relaxed);
  PublishedFrame.store(Rate>0?PhaseNumerator/Rate:0,AM::relaxed);
  PublishedEpoch.store(Epoch,AM::relaxed);
  PublishedUnderflow.store(Underflow,AM::relaxed);
  PublishedValid.store(HadTrack&&!Stopped.load(AM::acquire),AM::relaxed);
  Sequence.fetch_add(1,AM::release);
 }
 bool ActivateNext()
 {
  int32 Next=INDEX_NONE; uint64 Lowest=MAX_uint64;
  for(int32 I=0;I<SlotCount;++I)
  {
   auto& S=Slots[I];
   if(S.State.load(AM::acquire)!=EBootPcmSlotState::Ready)continue;
   if(!S.Allowed.load(AM::acquire)||S.Failed.load(AM::acquire))
   {auto Expected=EBootPcmSlotState::Ready;S.State.compare_exchange_strong(Expected,EBootPcmSlotState::Retired,AM::acq_rel);continue;}
   const uint64 Id=S.RequestId.load(AM::relaxed);
   if(Id<Lowest) {Lowest=Id;Next=I;}
  }
  if(Next==INDEX_NONE)return false;
  auto Expected=EBootPcmSlotState::Ready;
  if(!Slots[Next].State.compare_exchange_strong(Expected,EBootPcmSlotState::Playing,AM::acq_rel))return false;
  if(!Slots[Next].Allowed.load(AM::acquire)||Slots[Next].Failed.load(AM::acquire))
  {Slots[Next].State.store(EBootPcmSlotState::Retired,AM::release);return false;}
  Current=Next;PhaseNumerator=0;++Epoch;HadTrack=true;InBridge=false;Fade=0.;Tail=0.;return true;
 }
};

FBootMusicRenderState::FBootMusicRenderState(const FBootRunToken& Run) : Impl(MakeUnique<FImpl>()) {Impl->Run=Run;}
FBootMusicRenderState::~FBootMusicRenderState()=default;
bool FBootMusicRenderState::PrepareSlot(int32 Slot,const FBootPlaybackSource& Source,uint64 RequestId)
{
 if(Slot<0||Slot>=SlotCount||Impl->Stopped.load()||Source.Frames<=0||Source.Frames>48000LL*600||Source.Channels<1||Source.Channels>2||!Source.Analysis.IsValid())return false;
 auto& S=Impl->Slots[Slot];if(S.State.load(AM::acquire)!=EBootPcmSlotState::Free)return false;
 if(!FMath::IsFinite(Source.Analysis->Grid.Bpm)||Source.Analysis->Grid.Bpm<=0.||!FMath::IsFinite(Source.Analysis->Grid.FirstBeatSeconds))return false;
 {
  FScopeLock Lock(&S.MetadataLock);
  S.Hash=Source.Hash;S.Analysis=Source.Analysis;S.Grid=Source.Analysis->Grid;
  S.Frames=Source.Frames;S.Channels=Source.Channels;S.RequestId.store(RequestId,AM::relaxed);S.bSafe=Source.Analysis->Source==EBootGridSource::Safe;
 }
 S.Written.store(0,AM::relaxed);S.Consumed.store(0,AM::relaxed);S.Eof.store(false,AM::relaxed);S.Allowed.store(true,AM::relaxed);S.Failed.store(false,AM::relaxed);
 S.State.store(EBootPcmSlotState::Filling,AM::release);return true;
}
int32 FBootMusicRenderState::WriteFrames(int32 Slot,const int16* In,int32 Frames)
{
 if(Slot<0||Slot>=SlotCount||!In||Frames<=0||Impl->Stopped.load(AM::acquire))return 0;
 auto& S=Impl->Slots[Slot];const auto Status=S.State.load(AM::acquire);
 if(Status!=EBootPcmSlotState::Filling&&Status!=EBootPcmSlotState::Ready&&Status!=EBootPcmSlotState::Playing)return 0;
 const int64 W=S.Written.load(AM::relaxed),R=S.Consumed.load(AM::acquire);
 const int32 Count=static_cast<int32>(FMath::Min3<int64>(Frames,CapacityFrames-(W-R),S.Frames-W));
 for(int32 I=0;I<Count;++I)
 {
  const int64 At=((W+I)%CapacityFrames)*2;
  S.Pcm[At]=In[I*S.Channels];S.Pcm[At+1]=In[I*S.Channels+(S.Channels==2?1:0)];
 }
 S.Written.store(W+Count,AM::release);return Count;
}
void FBootMusicRenderState::PublishReady(int32 Slot)
{
 if(Slot<0||Slot>=SlotCount)return;auto& S=Impl->Slots[Slot];
 if(S.Written.load(AM::acquire)>0&&S.State.load(AM::acquire)==EBootPcmSlotState::Filling)
 {
#if WITH_DEV_AUTOMATION_TESTS
  if(BeforeReadyCommitAudit)BeforeReadyCommitAudit();
#endif
  auto Expected=EBootPcmSlotState::Filling;
  S.State.compare_exchange_strong(Expected,EBootPcmSlotState::Ready,AM::acq_rel);
 }
}
void FBootMusicRenderState::MarkEof(int32 Slot) {if(Slot>=0&&Slot<SlotCount)Impl->Slots[Slot].Eof.store(true,AM::release);}
void FBootMusicRenderState::MarkFailed(int32 Slot)
{
 if(Slot<0||Slot>=SlotCount)return;auto& S=Impl->Slots[Slot];S.Failed.store(true,AM::release);
 auto Expected=EBootPcmSlotState::Filling;
 if(!S.State.compare_exchange_strong(Expected,EBootPcmSlotState::Retired,AM::acq_rel))
 {Expected=EBootPcmSlotState::Ready;S.State.compare_exchange_strong(Expected,EBootPcmSlotState::Retired,AM::acq_rel);}
}
void FBootMusicRenderState::SetAllowed(int32 Slot,bool V){if(Slot>=0&&Slot<SlotCount)Impl->Slots[Slot].Allowed.store(V,AM::release);}
EBootPcmSlotState FBootMusicRenderState::SlotState(int32 Slot)const{return Slot>=0&&Slot<SlotCount?Impl->Slots[Slot].State.load(AM::acquire):EBootPcmSlotState::Free;}
int64 FBootMusicRenderState::WritableFrames(int32 Slot)const
{
 if(Slot<0||Slot>=SlotCount)return 0;const auto& S=Impl->Slots[Slot];
 return FMath::Max<int64>(0,FMath::Min<int64>(CapacityFrames-(S.Written.load(AM::acquire)-S.Consumed.load(AM::acquire)),S.Frames-S.Written.load(AM::acquire)));
}
void FBootMusicRenderState::RetireSlot(int32 Slot)
{
 if(Slot<0||Slot>=SlotCount)return;auto& S=Impl->Slots[Slot];
 if(S.State.load(AM::acquire)!=EBootPcmSlotState::Retired)return;
 {FScopeLock Lock(&S.MetadataLock);S.RequestId.store(0,AM::release);S.Hash.Reset();S.Analysis.Reset();S.Grid={130.,0.};S.bSafe=true;}
 S.State.store(EBootPcmSlotState::Free,AM::release);
}
void FBootMusicRenderState::SetPaused(bool V){Impl->Paused.store(V,AM::release);}
bool FBootMusicRenderState::IsPaused()const{return Impl->Paused.load(AM::acquire);}
void FBootMusicRenderState::Stop(){Impl->Stopped.store(true,AM::release);}
bool FBootMusicRenderState::IsStopped()const{return Impl->Stopped.load(AM::acquire);}

int32 FBootMusicRenderState::Render(float* Out,int32 NumSamples,int32 Rate)
{
 if(!Out||NumSamples<=0)return 0;
 FMemory::Memzero(Out,NumSamples*sizeof(float));
 if(Rate<8000||Rate>384000)return NumSamples;
 auto& P=*Impl;P.DeviceRate.store(Rate,AM::relaxed);P.RenderBlocks.fetch_add(1,AM::relaxed);
 if(P.Stopped.load(AM::acquire)){P.Publish(false);return NumSamples;}
 if(P.Paused.load(AM::acquire))return NumSamples;
 bool Underflow=false;const double FadeStep=1./(.020*Rate);
 for(int32 F=0;F<NumSamples/2;++F)
 {
  if(P.Current!=INDEX_NONE)
  {
   auto& S=P.Slots[P.Current];
   if(P.PhaseNumerator>=S.Frames*int64(Rate)||S.Failed.load(AM::acquire))
   {Underflow|=S.Failed.load(AM::acquire);S.Consumed.store(S.Frames,AM::release);S.State.store(EBootPcmSlotState::Retired,AM::release);P.Current=INDEX_NONE;}
  }
  if(P.Current==INDEX_NONE&&!P.ActivateNext())
  {
   if(!P.HadTrack&&!P.FallbackRequested.load(AM::acquire))continue;
   P.HadTrack=true; // A terminal startup failure has an explicit safe clock, unlike pending prebuffer.
   if(!P.InBridge){P.InBridge=true;P.PhaseNumerator=0;++P.Epoch;}
   const double Seconds=double(P.PhaseNumerator)/Rate/48000.;
   const double BeatPhase=FMath::Fmod(Seconds,60./130.);
   const float Click=BeatPhase<.025?float(.035*FMath::Sin(Seconds*2.*PI*880.)*(1.-BeatPhase/.025)):0.f;
   Out[F*2]=Out[F*2+1]=Click;P.PhaseNumerator+=48000;Underflow=true;P.OutputFrames.fetch_add(1,AM::relaxed);continue;
  }
  auto& S=P.Slots[P.Current];
  const int64 First=P.PhaseNumerator/Rate,Next=FMath::Min(First+1,S.Frames-1),Written=S.Written.load(AM::acquire);
  if(First>=Written||Next>=Written)
  {
   Underflow=true;P.Fade=0.;P.Tail=FMath::Max(0.,P.Tail-FadeStep);
   Out[F*2]=P.LastLeft*P.Tail;Out[F*2+1]=P.LastRight*P.Tail;continue;
  }
  const double Fraction=double(P.PhaseNumerator%Rate)/Rate;
  const int64 A=(First%CapacityFrames)*2,B=(Next%CapacityFrames)*2;
  P.Fade=FMath::Min(1.,P.Fade+FadeStep);P.Tail=1.;
  const double FadeOut=FMath::Clamp(double(S.Frames-First)/960.,0.,1.);
  const double Gain=P.Fade*FadeOut/32768.;
  P.LastLeft=float(FMath::Lerp(double(S.Pcm[A]),double(S.Pcm[B]),Fraction)*Gain);
  P.LastRight=float(FMath::Lerp(double(S.Pcm[A+1]),double(S.Pcm[B+1]),Fraction)*Gain);
  Out[F*2]=P.LastLeft;Out[F*2+1]=P.LastRight;P.PhaseNumerator+=48000;
  S.Consumed.store(FMath::Min(P.PhaseNumerator/Rate,S.Frames),AM::release);P.OutputFrames.fetch_add(1,AM::relaxed);
 }
 if(Underflow)P.Underflows.fetch_add(1,AM::relaxed);P.Publish(Underflow);return NumSamples;
}

FBootTransportSnapshot FBootMusicRenderState::Snapshot()const
{
 const auto& P=*Impl;FBootTransportSnapshot Result;Result.Run=P.Run;
 for(int32 Attempt=0;Attempt<8;++Attempt)
 {
  const uint64 Before=P.Sequence.load(AM::acquire);if(Before&1)continue;
  const int32 Slot=P.PublishedSlot.load(AM::relaxed);const uint64 Request=P.PublishedRequest.load(AM::relaxed);
  Result.TrackEpoch=P.PublishedEpoch.load(AM::relaxed);Result.RenderedSourceFrame=P.PublishedFrame.load(AM::relaxed);
  Result.bValid=P.PublishedValid.load(AM::relaxed)&&!P.Stopped.load(AM::acquire);
  Result.bPaused=P.Paused.load(AM::acquire);Result.bUnderflow=P.PublishedUnderflow.load(AM::relaxed);
  Result.TrackHash.Reset();Result.Grid={130.,0.};Result.bSafeGrid=true;Result.Energy=0.f;Result.OnsetDensity=0.f;Result.Section=0;
  Result.LocalSeconds=double(Result.RenderedSourceFrame)/48000.;
  if(Slot>=0&&Slot<SlotCount)
  {
   const auto& S=P.Slots[Slot];FScopeLock Lock(&S.MetadataLock);
   if(Request==0||S.RequestId.load(AM::relaxed)!=Request||S.Hash.IsEmpty()||!S.Analysis.IsValid()){Result.bValid=false;return Result;}
   Result.TrackHash=S.Hash;Result.Grid=S.Grid;Result.bSafeGrid=S.bSafe;
   if(S.Analysis.IsValid())
   {
    const auto& A=*S.Analysis;const int32 Index=A.FeatureStepSeconds>0.?FMath::FloorToInt(Result.LocalSeconds/A.FeatureStepSeconds):0;
    if(A.Energy.IsValidIndex(Index))Result.Energy=A.Energy[Index];
    if(A.OnsetDensity.IsValidIndex(Index))Result.OnsetDensity=A.OnsetDensity[Index];
    for(int32 I=0;I<A.SectionStartSeconds.Num()&&A.SectionStartSeconds[I]<=Result.LocalSeconds;++I)Result.Section=I;
   }
  }
  Result.EffectiveBpm=Result.Grid.Bpm;
  if(Before==P.Sequence.load(AM::acquire))return Result;
 }
 Result.bValid=false;return Result;
}
FBootTransportDiagnostics FBootMusicRenderState::Diagnostics()const
{
 FBootTransportDiagnostics D;D.RenderBlocks=Impl->RenderBlocks.load();D.RenderedOutputFrames=Impl->OutputFrames.load();D.UnderflowBlocks=Impl->Underflows.load();D.DeviceSampleRate=Impl->DeviceRate.load();
 D.RingCapacityFrames=FMath::Min(Impl->Slots[0].Pcm.Num(),Impl->Slots[1].Pcm.Num())/2;
 D.TotalRingBytes=int64(Impl->Slots[0].Pcm.GetAllocatedSize())+int64(Impl->Slots[1].Pcm.GetAllocatedSize());
 return D;
}
FBootMusicSoundGenerator::FBootMusicSoundGenerator(TSharedRef<FBootMusicRenderState,ESPMode::ThreadSafe> S,int32 R):State(S),Rate(R){}
int32 FBootMusicSoundGenerator::OnGenerateAudio(float* Out,int32 N){return State->Render(Out,N,Rate);}
bool FBootMusicRenderState::InvalidatePending(int32 Slot)
{
 if(Slot<0||Slot>=SlotCount)return false;auto& S=Impl->Slots[Slot];
 auto Status=S.State.load(AM::acquire);
 while(Status==EBootPcmSlotState::Filling||Status==EBootPcmSlotState::Ready)
  if(S.State.compare_exchange_weak(Status,EBootPcmSlotState::Retired,AM::acq_rel))return true;
 return Status!=EBootPcmSlotState::Playing;
}
void FBootMusicRenderState::RequestSafeFallback(){Impl->FallbackRequested.store(true,AM::release);}
void FBootMusicGeneratorHandoff::Publish(const FBootRunToken& Run,TSharedRef<FBootMusicRenderState,ESPMode::ThreadSafe> State)
{FScopeLock Guard(&Lock);Generation=Run;Current=MoveTemp(State);}
void FBootMusicGeneratorHandoff::Clear(const FBootRunToken& Run)
{FScopeLock Guard(&Lock);if(Generation==Run)Current.Reset();}
TSharedPtr<FBootMusicRenderState,ESPMode::ThreadSafe> FBootMusicGeneratorHandoff::Acquire(const FBootRunToken& Run)const
{FScopeLock Guard(&Lock);return Generation==Run&&Current.IsValid()&&!Current->IsStopped()?Current:TSharedPtr<FBootMusicRenderState,ESPMode::ThreadSafe>{};}
TSharedPtr<FBootMusicRenderState,ESPMode::ThreadSafe> FBootMusicGeneratorHandoff::AcquireCurrent()const
{FScopeLock Guard(&Lock);return Current.IsValid()&&!Current->IsStopped()?Current:TSharedPtr<FBootMusicRenderState,ESPMode::ThreadSafe>{};}
