#pragma once
#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Music/BootMusicTypes.h"
#include "BootMusicTransportComponent.generated.h"

struct FBootMusicTransportImpl;
struct FBootMusicTransportDeleter { void operator()(FBootMusicTransportImpl*) const; };
UCLASS(ClassGroup=Audio, meta=(BlueprintSpawnableComponent))
class BOOTLOOP_API UBootMusicTransportComponent : public USynthComponent
{
 GENERATED_BODY()
public:
 UBootMusicTransportComponent(const FObjectInitializer&);
 virtual ~UBootMusicTransportComponent() override;
 bool StartRun(const FBootRunToken&, uint64 QueueSeed, FText&);
 void SetRunPaused(bool);
 void StopRun(const FBootRunToken&);
 FBootTransportSnapshot Snapshot() const;
 /** GT-only peek at the actual next occupied request; empty for preview/stopped/no eligible pending song. */
 FString GetNextTrackHash() const;
 /** Game-thread-only audit snapshot; no lease/file/UObject reads on the render path. */
 FBootTransportDiagnostics Diagnostics() const;
 /** Same transport owner, exclusive with run playback. StopPreview never stops a run. */
 bool StartPreview(const FString& Hash, FText&);
 void StopPreview();
 void SetEstimatedDeviceLatency(double Seconds);
 double GetEstimatedDeviceLatency() const;
 /** Control-plane error for UI; safe fallback remains playable and never fabricates a hidden song. */
 FText GetPlaybackError() const;
 virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams&) override;
protected:
 virtual void EndPlay(const EEndPlayReason::Type) override;
private:
 void Pump();
 void StopPlayback();
 TUniquePtr<FBootMusicTransportImpl, FBootMusicTransportDeleter> Impl;
 FTimerHandle PumpTimer;
};
