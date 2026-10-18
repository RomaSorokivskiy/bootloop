#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Run/BootRunTypes.h"
#include "BootSurfWindow.generated.h"

class ACharacter;
class UBoxComponent;
class UPrimitiveComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UAudioComponent;

/** A single-use GUI platform. Rhythm changes only the departure charge, never its route. */
UCLASS()
class BOOTLOOP_API ABootSurfWindow : public AActor
{
    GENERATED_BODY()
public:
    ABootSurfWindow();
    virtual void Tick(float DeltaSeconds) override;

    /** Positions are top-surface centres, in world centimetres. The solid platform is 1000 x 750 x 80. */
    void Configure(FVector StartTop, FVector EndTop, float Seconds, int32 Id);
    void ConfigureOriented(const FTransform& StartTop, FVector EndTop, float Seconds, int32 Id);
    void SetReady(bool bInReady) { bReady = bInReady; }
    virtual bool Activate(ACharacter* Rider, bool bSynced);
    virtual void ResetWindow();
    /** Freeze the existing ride in place and silence its servo, without resetting capture or support. */
    void SetRideSuspended(bool bSuspended);
    virtual bool IsAvailable() const { return bReady && !bActivated && !bRideSuspended; }
    bool IsMoving() const { return bMoving && !bRideSuspended; }
    void SetFlowRide(bool bEnabled);
    bool IsFlowRide() const { return bFlowRide; }
    bool HasChargeFor(ACharacter* Rider) const;
    FVector GetHandleLocation() const;
    bool HasCarried(ACharacter* Rider) const;
    bool ConsumeDepartureBoost(ACharacter* Rider);
    /** Caller additionally validates arrival in its next course zone before committing. */
    bool CommitRide(ACharacter* Rider);
    int32 GetWindowId() const { return WindowId; }
    FVector GetEndTop() const { return EndTop; }
    void SetRunOperation(const FBootOperationKey& Key) { RunOperation=Key; }
    const FBootOperationKey* GetRunOperation() const { return RunOperation.IsSet()?&RunOperation.GetValue():nullptr; }
    bool HasActiveCommitment() const;
    void SetPreparedLandingComponent(UPrimitiveComponent*);
    bool WasRideBlocked() const { return bBlocked; }
    double GetPeakTravelSpeedCmps() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void BuildVisuals();
    void ObserveRider(float DeltaSeconds = 0.f, const FVector& CarriedDelta = FVector::ZeroVector);
    UFUNCTION() void HandleRiderLanded(const FHitResult& Hit);
    void ReleaseRiderPrerequisite();
    bool RiderIsAboard() const;
    void SetChargeAppearance(bool bCharged);

    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Platform;
    UPROPERTY() TArray<TObjectPtr<UBoxComponent>> FlowEntrySteps;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAudioComponent> Servo;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> BodyParts;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> RouteParts;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Colors;
    UPROPERTY() TObjectPtr<UMaterialInterface> SurfaceMaterial;
    UPROPERTY() TWeakObjectPtr<ACharacter> ActivatingRider;

    FVector StartTop = FVector::ZeroVector;
    FVector EndTop = FVector(600, 450, 160);
    float TravelSeconds = 1.f;
    float TravelElapsed = 0.f;
    float CarriedSeconds = 0.f;
    float CarriedDistance = 0.f;
    double DockedAt = -1.;
    int32 WindowId = INDEX_NONE;
    bool bActivated = false;
    bool bMoving = false;
    bool bBlocked = false;
    bool bSynced = false;
    bool bWasAboard = false;
    bool bCarried = false;
    bool bDeparted = false;
    bool bDepartureLanded = false;
    bool bChargeConsumed = false;
    bool bCommitted = false;
    bool bReady = true;
    bool bRideSuspended = false;
    bool bFlowRide = false;
    FQuat WindowRotation = FQuat::Identity;
    TOptional<FBootOperationKey> RunOperation;
    UPROPERTY() TWeakObjectPtr<UPrimitiveComponent> PreparedLandingComponent;
};


