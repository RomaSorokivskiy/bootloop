#include "Music/BootMusicTransportComponent.h"
#include "Music/BootMusicSoundGenerator.h"
#include "Music/BootMusicQueue.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "TimerManager.h"
#include "AudioThread.h"
#include <atomic>

namespace
{
 /** One dedicated stream worker. Lease destruction and file access never run in Render. */
 class FBootPcmStreamWorker final : public FRunnable
 {
 public:
  explicit FBootPcmStreamWorker(TSharedRef<FBootMusicRenderState,ESPMode::ThreadSafe> InState):State(InState)
  {
   Scratch.SetNumUninitialized(16384*2);
   Thread.Reset(FRunnableThread::Create(this,TEXT("BootMusicPCM"),0,TPri_BelowNormal));
  }
  ~FBootPcmStreamWorker() { Stop();if(Thread)Thread->WaitForCompletion(); }
  bool IsRunning()const {return Thread.IsValid();}
  bool IsOccupied(int32 I)const {return Slots[I].Occupied.load(std::memory_order_acquire);}
  bool HasFailed(int32 I)const {return Slots[I].Failed.load(std::memory_order_acquire);}
  bool IsAllowed(int32 I)const {return Slots[I].Allowed.load(std::memory_order_acquire);}
  bool IsRefreshing(int32 I)const {return Slots[I].Refresh.load(std::memory_order_acquire);}
  bool Enqueue(int32 I,FBootPlaybackLease Lease,uint64 Id)
  {
   auto& S=Slots[I];if(S.Occupied.load(std::memory_order_acquire)||!Lease.IsValid())return false;
   S.Lease=MoveTemp(Lease);S.Id=Id;S.Position=0;S.Prepared=false;
   S.Allowed.store(true,std::memory_order_relaxed);S.Failed.store(false,std::memory_order_relaxed);S.Refresh.store(false,std::memory_order_relaxed);
   S.Occupied.store(true,std::memory_order_release);return true;
  }
  void SetAllowed(int32 I,bool Allowed)
  {Slots[I].Allowed.store(Allowed,std::memory_order_release);State->SetAllowed(I,Allowed);}
  bool CancelPending(int32 I)
  {
   // Publish cancellation before checking the render slot: a worker not prepared yet must also see it.
   Slots[I].Refresh.store(true,std::memory_order_release);
   if(!State->InvalidatePending(I))
   {Slots[I].Refresh.store(false,std::memory_order_release);return false;}
   return true;
  }
  virtual void Stop()override {Stopping.store(true,std::memory_order_release);}
  virtual uint32 Run()override
  {
   auto& Files=FPlatformFileManager::Get().GetPlatformFile();
   while(!Stopping.load(std::memory_order_acquire)&&!State->IsStopped())
   {
    if(State->IsPaused()){FPlatformProcess::Sleep(.005f);continue;}
    for(int32 I=0;I<2;++I)
    {
     auto& S=Slots[I];if(!S.Occupied.load(std::memory_order_acquire))continue;
     if(S.Refresh.load(std::memory_order_acquire))
     {
      if(State->SlotState(I)==EBootPcmSlotState::Playing)S.Refresh.store(false,std::memory_order_release);
      else if(!S.Prepared)
      {
       S.File.Reset();S.Lease.Source.Reset();State->RetireSlot(I);S.Occupied.store(false,std::memory_order_release);continue;
      }
      else State->InvalidatePending(I);
     }
     if(State->SlotState(I)==EBootPcmSlotState::Retired)
     {
      S.File.Reset();S.Lease.Source.Reset();S.Prepared=false;State->RetireSlot(I);
      S.Occupied.store(false,std::memory_order_release);continue;
     }
     if(S.Failed.load(std::memory_order_acquire))continue;
     if(!S.Prepared)
     {
      if(!State->PrepareSlot(I,*S.Lease.Source,S.Id))
      {S.Failed.store(true,std::memory_order_release);S.Lease.Source.Reset();S.Occupied.store(false,std::memory_order_release);continue;}
      S.Prepared=true;State->SetAllowed(I,S.Allowed.load(std::memory_order_acquire));
      if(S.Refresh.load(std::memory_order_acquire)){State->InvalidatePending(I);continue;}
      S.File.Reset(Files.OpenRead(*S.Lease.Source->PcmPath));
      const int64 RequiredBytes=S.Lease.Source->Frames*S.Lease.Source->Channels*int64(sizeof(int16));
      if(!S.File||S.File->Size()!=RequiredBytes)
      {S.Failed.store(true,std::memory_order_release);State->MarkFailed(I);continue;}
     }
     const int32 Count=static_cast<int32>(FMath::Min<int64>(16384,State->WritableFrames(I)));
     if(Count>0)
     {
      if(!S.File->Read(reinterpret_cast<uint8*>(Scratch.GetData()),Count*S.Lease.Source->Channels*sizeof(int16)))
      {S.Failed.store(true,std::memory_order_release);State->MarkFailed(I);continue;}
      const int32 Written=State->WriteFrames(I,Scratch.GetData(),Count);S.Position+=Written;
      if(Written!=Count){S.File->Seek(S.Position*S.Lease.Source->Channels*int64(sizeof(int16)));}
      if(S.Position==S.Lease.Source->Frames)State->MarkEof(I);
      // >=0.34s initial prebuffer, then refill toward a hard maximum of8s.
      State->PublishReady(I);
     }
    }
    FPlatformProcess::Sleep(.005f);
   }
   // The worker owns leases through shutdown. No final release can occur in audio callback.
   for(auto& S:Slots){S.File.Reset();S.Lease.Source.Reset();S.Occupied.store(false,std::memory_order_release);}
   return 0;
  }
 private:
  struct FStreamSlot
  {
   std::atomic<bool> Occupied{false},Allowed{true},Failed{false},Refresh{false};
   FBootPlaybackLease Lease;TUniquePtr<IFileHandle> File;
   uint64 Id=0;int64 Position=0;bool Prepared=false;
  } Slots[2];
  TSharedRef<FBootMusicRenderState,ESPMode::ThreadSafe> State;
  TArray<int16> Scratch;
  std::atomic<bool> Stopping{false};
  TUniquePtr<FRunnableThread> Thread;
 };
 UBootMusicLibrarySubsystem* Library(const UBootMusicTransportComponent* Owner)
 {
  const UWorld* W=Owner->GetWorld();auto* GI=W?W->GetGameInstance():nullptr;
  return GI?GI->GetSubsystem<UBootMusicLibrarySubsystem>():nullptr;
 }
}
struct FBootMusicTransportImpl
{
 TSharedPtr<FBootMusicRenderState,ESPMode::ThreadSafe> State;
 // Created once with the component; CreateSoundGenerator never reads the GT-owned State pointer.
 FBootMusicGeneratorHandoff Handoff;
 TUniquePtr<FBootPcmStreamWorker> Worker;
 FBootMusicQueue Queue;FRandomStream Random;
 FBootRunToken Run;
 FString SlotHashes[2],RefreshHashes[2],PreviewHash;
 uint64 SlotRevisions[2]={0,0};
 uint64 SlotRequestIds[2]={0,0};
 TMap<FString,uint64> FailedRevisions;
 uint64 NextRequest=1;
 bool Active=false,Preview=false,RefreshBeforeResume=false;
 FText PlaybackError;
 double EstimatedDeviceLatency=.040;
};
void FBootMusicTransportDeleter::operator()(FBootMusicTransportImpl* P)const {delete P;}
UBootMusicTransportComponent::UBootMusicTransportComponent(const FObjectInitializer& O):Super(O),Impl(new FBootMusicTransportImpl())
{NumChannels=2;PreferredBufferLength=1024;bAutoActivate=false;bAllowSpatialization=false;bIsUISound=true;PrimaryComponentTick.bCanEverTick=false;}
UBootMusicTransportComponent::~UBootMusicTransportComponent()
{if(Impl->State)Impl->State->Stop();Impl->Worker.Reset();}
bool UBootMusicTransportComponent::StartRun(const FBootRunToken& Token,uint64 Seed,FText& Error)
{
 auto* L=Library(this);
 if(!L||!Token.RunId.IsValid()){Error=FText::FromString(TEXT("Music library or run identity is unavailable."));return false;}
 bool Any=false;for(const auto& E:L->Snapshot())if(E.State==EBootLibraryState::Ready&&E.bIncluded){Any=true;break;}
 if(!Any){Error=FText::FromString(TEXT("Enable at least one ready song in MUSIC LIBRARY."));return false;}
 StopPlayback();Impl->Run=Token;Impl->Random.Initialize(static_cast<int32>(Seed^(Seed>>32)));Impl->Queue.Reset();
 Impl->Preview=false;Impl->Active=true;Impl->NextRequest=1;Impl->FailedRevisions.Reset();Impl->PlaybackError=FText::GetEmpty();
 Impl->State=MakeShared<FBootMusicRenderState,ESPMode::ThreadSafe>(Token);
 Impl->Worker=MakeUnique<FBootPcmStreamWorker>(Impl->State.ToSharedRef());
 if(!Impl->Worker->IsRunning()){Error=FText::FromString(TEXT("Could not start music streaming worker."));StopPlayback();return false;}
 Pump();Impl->Handoff.Publish(Token,Impl->State.ToSharedRef());Initialize();Start();
 if(auto* W=GetWorld())W->GetTimerManager().SetTimer(PumpTimer,this,&UBootMusicTransportComponent::Pump,.05f,true);
 Error=FText::GetEmpty();return true;
}
void UBootMusicTransportComponent::SetRunPaused(bool Paused)
{
 if(!Impl->Active||!Impl->State)return;
 if(!Paused&&Impl->State->IsPaused())
 {
  // Apply edits made over the paused run before audio can activate a stale prefetched song.
  Impl->RefreshBeforeResume=true;Pump();Impl->RefreshBeforeResume=false;
 }
 if(Impl->State)Impl->State->SetPaused(Paused);
}
void UBootMusicTransportComponent::StopRun(const FBootRunToken& Token)
{if(Impl->Active&&!Impl->Preview&&Token==Impl->Run)StopPlayback();}
FBootTransportSnapshot UBootMusicTransportComponent::Snapshot()const
{return Impl->State?Impl->State->Snapshot():FBootTransportSnapshot{};}
FString UBootMusicTransportComponent::GetNextTrackHash()const
{
 if(!Impl->Active||Impl->Preview||!Impl->State||Impl->State->IsStopped()||!Impl->Worker)return {};
 const auto* L=Library(this);if(!L)return {};
 TArray<FBootPendingPlaybackIdentity> Pending;Pending.Reserve(2);
 for(int32 I=0;I<2;++I)
 {
  if(!Impl->Worker->IsOccupied(I))continue;
  const auto Status=Impl->State->SlotState(I);
  // Free can be initial preparation or the worker's final retirement handoff. Until Filling,
  // present PREPARING instead of risking an identity whose lease has just been released.
  if(Status!=EBootPcmSlotState::Filling&&Status!=EBootPcmSlotState::Ready)continue;
  FBootPendingPlaybackIdentity Request;
  Request.Hash=Impl->SlotHashes[I];Request.RequestId=Impl->SlotRequestIds[I];Request.LibraryRevision=Impl->SlotRevisions[I];
  Request.bOccupied=true;Request.bAllowed=Impl->Worker->IsAllowed(I);Request.bFailed=Impl->Worker->HasFailed(I);Request.bCancelled=Impl->Worker->IsRefreshing(I);
  const auto ConfirmedStatus=Impl->State->SlotState(I);
  if((ConfirmedStatus!=EBootPcmSlotState::Filling&&ConfirmedStatus!=EBootPcmSlotState::Ready)||!Impl->Worker->IsOccupied(I)||Impl->Worker->HasFailed(I))continue;
  Pending.Add(MoveTemp(Request));
 }
 return Impl->Queue.PeekPendingTrackHash(Pending,L->Snapshot());
}
FBootTransportDiagnostics UBootMusicTransportComponent::Diagnostics()const
{
 FBootTransportDiagnostics Result;
 if(!Impl->State||Impl->State->IsStopped())
 {
  Result.RingCapacityFrames=0;Result.TotalRingBytes=0;
  return Result;
 }
 Result=Impl->State->Diagnostics();
 if(Impl->Worker)Result.LeaseSlots=int32(Impl->Worker->IsOccupied(0))+int32(Impl->Worker->IsOccupied(1));
 Result.EstimatedDeviceLatencySeconds=Impl->EstimatedDeviceLatency;
 return Result;
}
bool UBootMusicTransportComponent::StartPreview(const FString& Hash,FText& Error)
{
 if(Impl->Active&&!Impl->Preview){Error=FText::FromString(TEXT("Preview is available outside a run."));return false;}
 auto* L=Library(this);if(!L){Error=FText::FromString(TEXT("Music library is unavailable."));return false;}
 auto Lease=L->Acquire(Hash,Error);if(!Lease.IsValid())return false;
 StopPlayback();Impl->Run={FGuid::NewGuid(),1};Impl->Preview=true;Impl->PreviewHash=Hash;Impl->Active=true;Impl->NextRequest=1;Impl->PlaybackError=FText::GetEmpty();
 Impl->State=MakeShared<FBootMusicRenderState,ESPMode::ThreadSafe>(Impl->Run);
 Impl->Worker=MakeUnique<FBootPcmStreamWorker>(Impl->State.ToSharedRef());
 if(!Impl->Worker->IsRunning()){Error=FText::FromString(TEXT("Could not start music preview."));StopPlayback();return false;}
 Impl->SlotHashes[0]=Hash;Impl->SlotRequestIds[0]=Impl->NextRequest++;Impl->Worker->Enqueue(0,MoveTemp(Lease),Impl->SlotRequestIds[0]);
 Impl->Handoff.Publish(Impl->Run,Impl->State.ToSharedRef());Initialize();Start();if(auto* W=GetWorld())W->GetTimerManager().SetTimer(PumpTimer,this,&UBootMusicTransportComponent::Pump,.05f,true);
 Error=FText::GetEmpty();return true;
}
void UBootMusicTransportComponent::StopPreview(){if(Impl->Preview)StopPlayback();}
void UBootMusicTransportComponent::SetEstimatedDeviceLatency(double Seconds)
{if(FMath::IsFinite(Seconds))Impl->EstimatedDeviceLatency=FMath::Clamp(Seconds,0.,.5);}
double UBootMusicTransportComponent::GetEstimatedDeviceLatency()const{return Impl->EstimatedDeviceLatency;}
FText UBootMusicTransportComponent::GetPlaybackError()const{return Impl->PlaybackError;}
ISoundGeneratorPtr UBootMusicTransportComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& Params)
{
 auto State=Impl->Handoff.AcquireCurrent();if(!State)return nullptr;
 return MakeShared<FBootMusicSoundGenerator,ESPMode::ThreadSafe>(State.ToSharedRef(),FMath::RoundToInt(Params.SampleRate));
}
void UBootMusicTransportComponent::EndPlay(const EEndPlayReason::Type Reason){StopPlayback();Super::EndPlay(Reason);}
void UBootMusicTransportComponent::Pump()
{
 if(!Impl->Active||!Impl->State||!Impl->Worker||(Impl->State->IsPaused()&&!Impl->RefreshBeforeResume))return;
 auto* L=Library(this);if(!L)return;
 auto Entries=L->Snapshot();
 for(int32 I=0;I<2;++I)
 {
  if(Impl->Worker->HasFailed(I)&&!Impl->SlotHashes[I].IsEmpty())
  {
   for(const auto& E:Entries)if(E.Hash==Impl->SlotHashes[I])Impl->FailedRevisions.Add(E.Hash,E.Revision);
   Impl->PlaybackError=FText::FromString(TEXT("Prepared audio is unreadable. Using other selected songs or Safe Grid."));
  }
 }
 for(auto& E:Entries)
  if(const uint64* Revision=Impl->FailedRevisions.Find(E.Hash);Revision&&*Revision==E.Revision)E.bIncluded=false;
 for(int32 I=0;I<2;++I)
 {
  const bool Allowed=Impl->Preview?Impl->SlotHashes[I]==Impl->PreviewHash:Entries.ContainsByPredicate([&](const FBootLibraryEntry& E){return E.Hash==Impl->SlotHashes[I]&&E.bIncluded&&E.State==EBootLibraryState::Ready;});
  if(Impl->Worker->IsOccupied(I))
  {
   Impl->Worker->SetAllowed(I,Allowed);
   if(!Impl->Preview)
    for(const auto& E:Entries)
     if(E.Hash==Impl->SlotHashes[I]&&Allowed&&E.Revision!=Impl->SlotRevisions[I])
     {
      // The CAS decides whether the edit precedes playback. Playing leases are left untouched.
      if(Impl->Worker->CancelPending(I))Impl->RefreshHashes[I]=E.Hash;
      break;
     }
  }
 }
 if(Impl->Preview)
 {
  // A preview is a single playback; the owning UI may request another explicitly.
  if(Impl->Worker->HasFailed(0))
  {Impl->PlaybackError=FText::FromString(TEXT("Preview could not read the prepared audio. Try importing it again."));StopPlayback();}
  else if(Impl->State->Snapshot().TrackEpoch>1)StopPlayback();
  return;
 }
 for(int32 I=0;I<2;++I)
 {
  if(Impl->Worker->IsOccupied(I)||Impl->State->SlotState(I)!=EBootPcmSlotState::Free)continue;
  const int32 Other=1-I;
  FString Previous=Impl->State->Snapshot().TrackHash;
  if(Impl->Worker->IsOccupied(Other)&&Entries.ContainsByPredicate([&](const FBootLibraryEntry& E){return E.Hash==Impl->SlotHashes[Other]&&E.bIncluded&&E.State==EBootLibraryState::Ready;}))Previous=Impl->SlotHashes[Other];
  TOptional<FString> Choice;
  if(!Impl->RefreshHashes[I].IsEmpty()&&Entries.ContainsByPredicate([&](const FBootLibraryEntry& E){return E.Hash==Impl->RefreshHashes[I]&&E.bIncluded&&E.State==EBootLibraryState::Ready;}))Choice=Impl->RefreshHashes[I];
  else Choice=Impl->Queue.ChooseNext(Entries,Previous,Impl->Random);
  if(!Choice.IsSet())continue;
  FText Error;auto Lease=L->Acquire(Choice.GetValue(),Error);
  if(!Lease.IsValid())
  {
   Impl->PlaybackError=Error;
   for(const auto& E:Entries)if(E.Hash==Choice.GetValue())Impl->FailedRevisions.Add(E.Hash,E.Revision);
   continue;
  }
  Impl->SlotHashes[I]=Choice.GetValue();Impl->RefreshHashes[I].Reset();
  for(const auto& E:Entries)if(E.Hash==Choice.GetValue()){Impl->SlotRevisions[I]=E.Revision;break;}
  Impl->SlotRequestIds[I]=Impl->NextRequest++;Impl->Worker->Enqueue(I,MoveTemp(Lease),Impl->SlotRequestIds[I]);
 }
 if(!Impl->Worker->IsOccupied(0)&&!Impl->Worker->IsOccupied(1))
 {
  Impl->State->RequestSafeFallback();
  if(Impl->PlaybackError.IsEmpty())Impl->PlaybackError=FText::FromString(TEXT("No selected music is available. Safe Grid metronome is active."));
 }
}
void UBootMusicTransportComponent::StopPlayback()
{
 if(auto* W=GetWorld())W->GetTimerManager().ClearTimer(PumpTimer);
 if(Impl->State)Impl->State->Stop();Impl->Worker.Reset();
 // UE Stop queues StopSoundsUsingResource. Complete it before a new generation can be published;
 // otherwise a delayed old native Create callback could attach to the next run's state.
 Stop();FAudioCommandFence NativeStop;NativeStop.BeginFence();NativeStop.Wait();
 Impl->Handoff.Clear(Impl->Run);Impl->State.Reset();
 Impl->Active=false;Impl->Preview=false;Impl->PreviewHash.Reset();
 for(int32 I=0;I<2;++I){Impl->SlotHashes[I].Reset();Impl->RefreshHashes[I].Reset();Impl->SlotRevisions[I]=0;Impl->SlotRequestIds[I]=0;}
 Impl->Queue.Reset();
}
