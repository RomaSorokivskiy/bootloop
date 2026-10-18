#pragma once
#include "CoreMinimal.h"
#include "World/Generation/BootTrack.h"
#include "BootSurfTrack.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class ABootSurfWindow;

// An authored, repeatable proving ground. Old endless chunks remain on L_Endless.
UCLASS()
class BOOTLOOP_API ABootSurfTrack : public ABootTrack
{
    GENERATED_BODY()
public:
    ABootSurfTrack();
    virtual void ResetTrack(int32 Seed) override;
    virtual void Advance(double PlayerX) override;
    virtual bool IsFatal(FVector Center, float Radius, float HalfHeight) const override;
    virtual float GetRouteProgress(FVector Position) const override;
    virtual bool IsFinished(FVector Position) const override;
    const TArray<TObjectPtr<ABootSurfWindow>>& GetWindows() const { return Windows; }
protected:
    virtual void BeginPlay() override;
private:
    void Box(FVector P, FVector Size, int32 Color, int32 Collision = 0);
    void Panel(double A, double B, double Y, double Width, double Top, const FString& Label);
    void Sign(const FString& Text, FVector P, float Size = 72.f, FColor Color = FColor(238,242,220));
    void Tower(FVector P, float Width, float Height, int32 Variant);
    void BuildCourse();
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Tiles;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Floors;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Walls;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Beams;
    UPROPERTY() TObjectPtr<UMaterialInterface> TextMaterial;
    UPROPERTY() TArray<TObjectPtr<ABootSurfWindow>> Windows;
};
