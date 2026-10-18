#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/BootPoseTypes.h"
#include "Movement/BootTraversalEnvelope.h"
#include "BootFeedbackComponent.generated.h"

class UAudioComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;
class USoundBase;

struct FBootSupportSoundMix
{
    float Wind=0.f,Slide=0.f,Wall=0.f,Servo=0.f;
};

UCLASS(ClassGroup=(BOOTLOOP), meta=(BlueprintSpawnableComponent))
class BOOTLOOP_API UBootFeedbackComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBootFeedbackComponent();
    virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    void Notify(EBootFeedbackEvent Event, FVector Position, FVector Normal);
    void UpdateMovement(float Dt, const FBootPoseInput& Input);
    void ResetFeedback();
    void ApplyPreferences(float Intensity, float Sfx, bool bMotion);
    int32 GetActiveParticleCount() const;
    void SetTraversalEnvelope(const FBootTraversalEnvelope& Envelope);
    static FBootSupportSoundMix ComputeSupportSound(const FBootPoseInput& Input,const FBootTraversalEnvelope& Envelope);
    const FBootSupportSoundMix& GetSupportSoundMix() const { return SupportMix; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    struct FChip
    {
        FVector Position = FVector::ZeroVector;
        FVector Velocity = FVector::ZeroVector;
        float Remaining = 0.f;
        float Lifetime = 0.f;
        float Size = 1.f;
    };
    void Emit(FVector Position, FVector Normal, int32 Count, FLinearColor Color);
    void PlayOneShot(USoundBase* Sound, float Pitch = 1.f, float Volume = 1.f);
    void SetLoop(UAudioComponent* Audio, float Volume, float Pitch = 1.f);
    UAudioComponent* MakeLoop(const TCHAR* Name, USoundBase* Sound);

    UPROPERTY() TObjectPtr<UStaticMesh> ChipMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> ChipMaterial;
    UPROPERTY() TArray<TObjectPtr<USoundBase>> Sounds;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> ChipParts;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> ChipMaterials;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> WindAudio;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> EdgeAudio;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> ServoAudio;
    TArray<FChip> Chips;
    FBootTraversalEnvelope TraversalEnvelope;
    FBootSupportSoundMix SupportMix;
    FRandomStream CosmeticRandom;
    float EffectIntensity = 1.f;
    float SfxVolume = .65f;
    float TrailTime = 0.f;
    int32 StepIndex = 0;
    bool bMotionEnabled = true;
    bool bHasMovement = false;
    bool bWasGrounded = true;
    bool bWasSliding = false;
    bool bWasWallRunning = false;
};
