#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Generation/BootTrackRules.h"
#include "BootTrack.generated.h"

class UInstancedStaticMeshComponent;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class ABootRunner;
class ABootSurfWindow;
class ABootDesktopBackdrop;
struct FBootMusicContext;
struct FBootTraversalEnvelope;

UCLASS()
class BOOTLOOP_API ABootTrackChunk : public AActor
{
    GENERATED_BODY()
public:
    ABootTrackChunk();
    void BuildChunk(int32 InType, int32 InIndex, const TArray<TObjectPtr<UMaterialInstanceDynamic>>& Materials);
    int32 Type = 0;
    int32 Index = 0;
    double EndX() const { return GetActorLocation().X + FBootTrackSequence::Length; }
    bool IsFatal(FVector CapsuleCenter, float Radius, float HalfHeight) const;
private:
    void Box(FVector Position, FVector Size, int32 Material, bool bSolid = false);
    void Floor(double Start, double End, double Width = 1000., double Y = 0.);
    void Sign(const FString& Text, FVector Position, float Size = 52.f);
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Decor;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FloorCollision;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> BeamCollision;
    UPROPERTY() TObjectPtr<UMaterialInterface> TextMaterial;
    FBox FatalBounds = FBox(ForceInit);
};

UCLASS()
class BOOTLOOP_API ABootTrack : public AActor
{
    GENERATED_BODY()
public:
    ABootTrack();
    virtual void ResetTrack(int32 Seed);
    virtual void Advance(double PlayerX);
    virtual void UpdateCourse(ABootRunner* Runner,float Dt);
    virtual bool CanCompleteSurf(const ABootSurfWindow* Window,const ABootRunner* Runner,int32 ActivationGateSequence) const;
    virtual int32 GetCourseSerial() const { return 0; }
    void Pulse(float Amount);
    virtual bool IsFatal(FVector Center, float Radius, float HalfHeight) const;
    virtual float GetRouteProgress(FVector Position) const { return FMath::Max(0.f, float((Position.X - 350.) / 100.)); }
    virtual bool IsFinished(FVector Position) const { return false; }
    int32 GetActiveCount() const { return Chunks.Num(); }
    const TArray<TObjectPtr<ABootTrackChunk>>& GetChunks() const { return Chunks; }
    /** One reused, collision-free city per v0.4 track; legacy courses opt out. */
    void InitializeRunPresentation(int32 Seed,AActor* FollowTarget);
    void ApplyRunPresentation(const FBootMusicContext& Context,const FBootTraversalEnvelope& Envelope);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    const TArray<TObjectPtr<UMaterialInstanceDynamic>>& GetPalette() const { return Materials; }
    void SetSurfaceMaterial(UMaterialInterface* Material) { Master = Material; }
    void UpdatePresentationOrigin(const FVector& Origin);
private:
    friend class FBootV01WorldTest;
    void SpawnNext(int32 Tier);
    UPROPERTY() TArray<TObjectPtr<ABootTrackChunk>> Chunks;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    UPROPERTY() TObjectPtr<UMaterialInterface> Master;
    UPROPERTY() TObjectPtr<UStaticMesh> Sphere;
    UPROPERTY() TObjectPtr<AActor> Background;
    UPROPERTY() TObjectPtr<ABootDesktopBackdrop> RunDistrict;
    int32 RunDistrictSeed=0;
    bool bRunDistrictConfigured=false;
    TUniquePtr<FBootTrackSequence> Sequence;
    int32 NextIndex = 0;
    FLinearColor Navy;
};
