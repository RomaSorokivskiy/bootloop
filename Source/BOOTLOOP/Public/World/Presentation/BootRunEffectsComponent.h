#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/BootPoseTypes.h"
#include "BootRunEffectsComponent.generated.h"
struct FBootOptions;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class USoundBase;
class UTexture;
class UAudioComponent;

/** Event-only run presentation. Never changes movement, score, generation or camera. */
UCLASS(ClassGroup=(BOOTLOOP), meta=(BlueprintSpawnableComponent))
class BOOTLOOP_API UBootRunEffectsComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBootRunEffectsComponent();
    void Emit(EBootFeedbackEvent Event, const FTransform& Transform, float Strength = 1.f);
    void ApplyPreferences(const FBootOptions& Options);
    void ResetRun();
    int32 GetActiveEffectCount() const;
    int32 GetPoolSize() const { return Parts.Num(); }
    bool HasEssentialWarning() const;
    static constexpr int32 MaxPoolSize = 40; // +24 contact chips =64 per runner.
    virtual void TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* Tick) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    struct FEffect
    {
        FTransform Frame;
        FVector Size = FVector::OneVector;
        FVector Velocity = FVector::ZeroVector;
        float Remaining=0.f, Lifetime=0.f;
        bool bEssential=false;
    };
    int32 Acquire(bool bEssential);
    UPROPERTY() TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY() TObjectPtr<UStaticMesh> Plane;
    UPROPERTY() TObjectPtr<UMaterialInterface> Material;
    UPROPERTY() TObjectPtr<UMaterialInterface> MaskMaterial;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> MaskMaterials;
    UPROPERTY() TArray<TObjectPtr<UTexture>> Masks;
    UPROPERTY() TArray<TObjectPtr<USoundBase>> EventSounds;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> Voices;
    TArray<FEffect> Effects;
    float Intensity=1.f,Sfx=.65f;
    bool bMotion=true;
    int32 ReuseCursor=0;
    int32 AudioCursor=0;
};
