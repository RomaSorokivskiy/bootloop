#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include "Music/BootRhythmScoring.h"
#include "BootRhythmComponent.generated.h"

class UAudioComponent;
class UQuartzClockHandle;
class USoundWave;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UBootRhythmComponent;
class UBootMusicTransportComponent;
struct FBootOptions;

// Quartz command callbacks carry "Quartz Event", unlike metronome callbacks.
// Each run binds its own UObject so a delayed callback retains the right run id.
UCLASS()
class UBootRhythmCommandProxy : public UObject
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<UBootRhythmComponent> RhythmOwner;
    FName ClockId;
    UFUNCTION() void OnCommand(EQuartzCommandDelegateSubType EventType, FName EventName);
};

UCLASS()
class BOOTLOOP_API UBootRhythmComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBootRhythmComponent();
    void ResetRun();
    void StopRun();
    void ScoreAction(EBootRhythmAction Action);
    EBootBeatRating CaptureSnap() const;
    bool CompleteSurf(int32 WindowId, EBootBeatRating InputRating);
    void SetRunPaused(bool bPause);
    void ApplyPreferences(const FBootOptions& Options);
    const FBootRhythmScore& GetScore() const { return Score; }
    bool IsReady() const;
    void SetMusicTransport(UBootMusicTransportComponent* Transport);
    void ResetScoring();
    void ShowSurfFeedback(EBootBeatRating Rating,int64 Points,int32 Combo);
    float GetPulse() const;
    float GetFeedbackAge() const;
    double GetTransportSeconds() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    friend class UBootRhythmCommandProxy;
    friend class FBootRhythmCallbackTest;
    void OnMusicCommand(EQuartzCommandDelegateSubType EventType, FName ClockName);
    UFUNCTION() void OnBeat(FName ClockName, EQuartzCommandQuantization Quantization, int32 Bars, int32 Beat, float BeatFraction);
    void FadePulse();
    UPROPERTY() TObjectPtr<USoundWave> MusicLoop;
    UPROPERTY() TWeakObjectPtr<UBootMusicTransportComponent> ExternalTransport;
    UPROPERTY() TObjectPtr<UAudioComponent> Music;
    UPROPERTY() TObjectPtr<UQuartzClockHandle> Clock;
    UPROPERTY() TObjectPtr<UBootRhythmCommandProxy> CommandProxy;
    UPROPERTY() TObjectPtr<UMaterialInterface> TitleMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PulseMaterial;
    FName ActiveClockName;
    FLinearColor BaseColor;
    FBootRhythmScore Score;
    FTimerHandle PulseTimer;
    double LastBeatTime = -100.;
    double LastFeedbackTime = -100.;
    int32 RunSerial = 0;
    int32 BeatCount = 0;
    bool bReady = false;
    bool bRunPaused = false;
    bool bEnablePulse = true;
    float MusicVolume = .55f;
};
