#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/BootPoseTypes.h"
#include "Player/BootAvatarPose.h"
#include "BootAvatarComponent.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;

DECLARE_MULTICAST_DELEGATE_TwoParams(FBootFootContact, int32, FVector);

// Rigid, authored plastic segments intentionally articulate like an OS service robot.
// Presentation has no collision and never parents or drives the first-person camera.
UCLASS(ClassGroup=(BOOTLOOP), meta=(BlueprintSpawnableComponent))
class BOOTLOOP_API UBootAvatarComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBootAvatarComponent();
    void UpdatePose(float Dt, const FBootPoseInput& Input);
    void UpdatePose(float Dt, float Speed, bool bGrounded, bool bSliding, bool bWallRunning, float WallSide);
    void ResetPose();
    void SetBodyVisible(bool bVisible);
    void TriggerSnap();
    FBootFootContact OnFootContact;

protected:
    virtual void BeginPlay() override;

private:
    UStaticMeshComponent* MakePart(const TCHAR* Name, int32 MeshIndex);
    void Place(UStaticMeshComponent* Part, const FVector& Position, const FRotator& Rotation = FRotator::ZeroRotator,
        const FVector& Scale = FVector::OneVector);
    void Segment(UStaticMeshComponent* Part, const FVector& Start, const FVector& End, float Length, float Width = 1.f);

    struct FArmParts
    {
        UStaticMeshComponent* Upper = nullptr;
        UStaticMeshComponent* Elbow = nullptr;
        UStaticMeshComponent* Forearm = nullptr;
        UStaticMeshComponent* Wrist = nullptr;
        UStaticMeshComponent* Palm = nullptr;
        TArray<UStaticMeshComponent*> Fingers;
    };
    struct FLegParts
    {
        UStaticMeshComponent* Thigh = nullptr;
        UStaticMeshComponent* Knee = nullptr;
        UStaticMeshComponent* Shin = nullptr;
        UStaticMeshComponent* Boot = nullptr;
    };

    UPROPERTY() TArray<TObjectPtr<UStaticMesh>> MeshAssets;
    UPROPERTY() TObjectPtr<UMaterialInterface> BodyMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> RedirectCueMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RedirectCueMID;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RedirectCue;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> BodyRoot;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UStaticMeshComponent* Torso = nullptr;
    UStaticMeshComponent* Hip = nullptr;
    FArmParts Arms[2];
    FLegParts Legs[2];
    FBootAvatarPoseState PoseState;
    bool bPendingSnap = false;
    bool bBodyVisible = true;
    bool bHasPreviousInput = false;
    FVector PreviousOwnerPosition = FVector::ZeroVector;
};
