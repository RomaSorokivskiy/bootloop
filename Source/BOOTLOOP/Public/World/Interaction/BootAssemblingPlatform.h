#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BootAssemblingPlatform.generated.h"

class UBoxComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USoundBase;

/** Physical distance, including relative motion of a moving base; never scored route distance. */
struct BOOTLOOP_API FBootAssemblyLookahead
{
    static bool NeedsPreparation(double DistanceCm, double CharacterCap = 1800.,
        double BaseSpeed = 1500., double BuildSeconds = 1.2);
};

/** A stationary landing collider with independently animated, collision-free GUI casing. */
UCLASS()
class BOOTLOOP_API ABootAssemblingPlatform : public AActor
{
    GENERATED_BODY()
public:
    ABootAssemblingPlatform();
    virtual void Tick(float DeltaSeconds) override;
    void Configure(const FTransform& Top, FVector Size, float Duration, int32 GroupId);
    void BeginAssembly();
    bool FinishSafely();
    void ResetAssembly(bool bReady);
    bool IsReady() const { return bReady; }
    float GetAssemblyProgress() const { return Elapsed / BuildSeconds; }
    int32 GetDecorativePartCount() const;
    int32 GetGroupId() const { return GroupId; }
protected:
    virtual void BeginPlay() override;
private:
    bool IsCurrentSupport() const;
    bool IsOccupied() const;
    bool CompleteSafely(bool bAudibleTransition);
    void PlayReadyClick();
    void BuildVisuals();
    void UpdateVisuals();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Solid;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Parts;
    UPROPERTY() TObjectPtr<UMaterialInterface> SurfaceMaterial;
    UPROPERTY() TObjectPtr<USoundBase> ReadySound;
    FVector PlatformSize = FVector(1200, 900, 80);
    float BuildSeconds = 1.1f;
    float Elapsed = 0.f;
    int32 GroupId = INDEX_NONE;
    bool bReady = false;
    bool bBuilding = false;
    bool bAnimatedAssembly = false;
};
