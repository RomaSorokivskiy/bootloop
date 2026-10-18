#pragma once
#include "CoreMinimal.h"
#include "Music/BootMusicLibrarySubsystem.h"
#include "Sound/SoundGenerator.h"
#include <atomic>

enum class EBootPcmSlotState : uint8 { Free, Filling, Ready, Playing, Retired };
/** Non-UObject state shared by one stream worker, one render consumer, and GT controls.
 * Allocates two 8-second canonical PCM16 stereo rings at construction, never in Render.
 * Prepare/Retire are worker-only. Snapshot is GT-only and never called by audio.
 */
class FBootMusicRenderState
{
public:
 static constexpr int32 SlotCount = 2;
 static constexpr int32 CapacityFrames = 384000;
 explicit FBootMusicRenderState(const FBootRunToken&);
 ~FBootMusicRenderState();
 bool PrepareSlot(int32 Slot, const FBootPlaybackSource&, uint64 RequestId);
 int32 WriteFrames(int32 Slot, const int16* Interleaved, int32 Frames);
 void PublishReady(int32 Slot);
 void MarkEof(int32 Slot);
 /** Worker failure/cancel; a playing slot is retired by its render owner. */
 void MarkFailed(int32 Slot);
 /** Returns false when playback already owns this slot; a current song remains immutable. */
 bool InvalidatePending(int32 Slot);
 void RequestSafeFallback();
 void SetAllowed(int32 Slot, bool);
 EBootPcmSlotState SlotState(int32 Slot) const;
 int64 WritableFrames(int32 Slot) const;
 void RetireSlot(int32 Slot);
 void SetPaused(bool);
 bool IsPaused() const;
 void Stop();
 bool IsStopped() const;
 int32 Render(float* Out, int32 NumSamples, int32 DeviceRate);
 FBootTransportSnapshot Snapshot() const;
 FBootTransportDiagnostics Diagnostics() const;
private:
#if WITH_DEV_AUTOMATION_TESTS
 friend class FBootTransportReadyCancellationTest;
 // Test-owned interleaving barrier; absent in Shipping and never read by Render.
 TFunction<void()> BeforeReadyCommitAudit;
#endif
 struct FImpl;
 TUniquePtr<FImpl> Impl;
};
/** Control-plane handoff. Native Stop is fenced before a different generation is published. */
class FBootMusicGeneratorHandoff
{
public:
 void Publish(const FBootRunToken&,TSharedRef<FBootMusicRenderState,ESPMode::ThreadSafe>);
 void Clear(const FBootRunToken&);
 TSharedPtr<FBootMusicRenderState,ESPMode::ThreadSafe> Acquire(const FBootRunToken&) const;
 TSharedPtr<FBootMusicRenderState,ESPMode::ThreadSafe> AcquireCurrent() const;
private:
 mutable FCriticalSection Lock;
 FBootRunToken Generation;
 TSharedPtr<FBootMusicRenderState,ESPMode::ThreadSafe> Current;
};
class FBootMusicSoundGenerator final : public ISoundGenerator
{
public:
 FBootMusicSoundGenerator(TSharedRef<FBootMusicRenderState, ESPMode::ThreadSafe>, int32 DeviceRate);
 virtual int32 OnGenerateAudio(float*, int32 NumSamples) override;
 virtual int32 GetNumChannels() const override { return 2; }
private:
 TSharedRef<FBootMusicRenderState, ESPMode::ThreadSafe> State;
 int32 Rate;
};
