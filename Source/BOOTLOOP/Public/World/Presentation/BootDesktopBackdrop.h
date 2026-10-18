#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BootDesktopBackdrop.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FBootMusicContext;
struct FBootTraversalEnvelope;

/** An original four-sided GUI city; every instance is cosmetic and collision-free. */
UCLASS()
class BOOTLOOP_API ABootDesktopBackdrop : public AActor
{
    GENERATED_BODY()
public:
    ABootDesktopBackdrop();
    void ConfigureDistrict(const FTransform& Center,int32 Seed);
    int32 GetDecorativePartCount() const;
    void ApplyMusicContext(const FBootMusicContext& Context,const FBootTraversalEnvelope& Envelope);
    void SetFollowTarget(AActor* Target);
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void BeginPlay() override;
private:
    void Box(FVector Position,FVector Size,int32 Color,FQuat Rotation=FQuat::Identity);
    void Tower(FVector Base,FVector Size,int32 Variant,bool bFar=false);
    void Facade(const FTransform& Frame,double Width,double Height,int32 Variant);
    void Cable(FVector A,FVector B,double Sag);
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Parts;
    UPROPERTY() TObjectPtr<UMaterialInterface> SurfaceMaterial;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> DistrictMaterials;
    UPROPERTY(Transient) TWeakObjectPtr<AActor> FollowTarget;
    TArray<FLinearColor> PaletteCurrent,PaletteTarget;
    int32 PaletteSection=INDEX_NONE;
    int64 PaletteEpoch=MIN_int64;
};
