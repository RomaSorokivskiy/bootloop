#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Movement/BootTraversalEnvelope.h"
#include "Run/BootRunTypes.h"
#include "BootMovementComponent.generated.h"

/** Momentum belongs to movement, never to score or the presentation rig. */
UCLASS(ClassGroup=(BOOTLOOP), meta=(BlueprintSpawnableComponent))
class BOOTLOOP_API UBootMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()

public:
    UBootMovementComponent();

    /** Caller has already validated ground/coyote and will queue its vertical launch. */
    void PrepareGroundJump();
    bool TryWallKick();
    void ResetParkour();
    void GrantOverclock();
    /** A single chain reserve: never accelerates the player or stacks with another window. */
    bool GrantSurfMomentum();
    float GetSurfMomentumBonus() const { return bSurfMomentum ? FBootTraversalPolicy::SurfMomentumCmps : 0.f; }
    float GetEffectiveMomentumCap() const;
    /** First envelope initializes a run; later calls only prepare a revision. */
    void ApplyTraversalEnvelope(const FBootTraversalEnvelope& Envelope);
    bool CommitTraversalEnvelope(const FBootSafeBoundary& Boundary, float TransitionSeconds);
    bool CanCommitTraversalEnvelope(const FBootSafeBoundary& Boundary,float TransitionSeconds) const;
    const FBootTraversalEnvelope& GetTraversalEnvelope() const { return TraversalEnvelope; }
    bool TryBufferDash(FVector DesiredWorldDirection);
    int32 GetDashCharges() const { return DashCharges; }
    int32 GetMaxDashCharges() const { return MaximumDashCharges; }
    int32 GetAirRedirectCharges() const { return AirRedirectCharges; }
    int32 GetMaxAirRedirectCharges() const { return MaximumAirRedirectCharges; }
    int32 GetDashSerial() const { return DashSerial; }
    float GetManualHopGrace() const;
    /** 0 at ordinary run, 1 at momentum cap; camera boost, not jog cadence. */
    float GetMovementPaceAlpha() const;
    bool GetWallContact(FVector& OutWorldPoint, FVector& OutWorldNormal) const;

    UFUNCTION(BlueprintCallable, Category="BOOTLOOP|Movement")
    bool TryAirRedirect(FVector DesiredWorldDirection);
    UFUNCTION(BlueprintPure, Category="BOOTLOOP|Movement")
    bool IsAirRedirectReady() const { return AirRedirectCharges > 0; }
    UFUNCTION(BlueprintPure, Category="BOOTLOOP|Movement")
    bool IsAirRedirecting() const { return AirRedirectRemaining > 0.f; }
    /** Changes only when a redirect is accepted; survives resets for presentation polling. */
    UFUNCTION(BlueprintPure, Category="BOOTLOOP|Movement")
    int32 GetAirRedirectSerial() const { return AirRedirectSerial; }

    UFUNCTION(BlueprintPure, Category="BOOTLOOP|Movement")
    bool IsWallRunning() const { return MovementMode == MOVE_Custom && CustomMovementMode == WallRunMode; }
    UFUNCTION(BlueprintPure, Category="BOOTLOOP|Movement")
    float GetWallSide() const { return IsWallRunning() ? WallSide : 0.f; }

    virtual float GetMaxSpeed() const override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, Category="BOOTLOOP|Movement") float MomentumCap = 1600.f;
    UPROPERTY(EditAnywhere, Category="BOOTLOOP|Movement") float OverclockCap = 1800.f;
    UPROPERTY(EditAnywhere, Category="BOOTLOOP|Movement") float HopGain = 110.f;
    UPROPERTY(EditAnywhere, Category="BOOTLOOP|Movement") float WallRunSeconds = 1.15f;

protected:
    virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
    virtual void PhysFalling(float DeltaTime, int32 Iterations) override;
    virtual void PhysCustom(float DeltaTime, int32 Iterations) override;

private:
    FBootTraversalEnvelope TraversalEnvelope;
    FBootTraversalEnvelope PreparedEnvelope;
    FBootTraversalEnvelope BlendFromEnvelope;
    FBootTraversalEnvelope BlendTargetEnvelope;
    FBootTraversalEnvelope LegacyPhysics;
    float LegacyHopGain = 110.f;
    float EnvelopeBlendElapsed = 0.f;
    float EnvelopeBlendSeconds = 0.f;
    int32 DashCharges = 0;
    int32 MaximumDashCharges = 0;
    int32 AirRedirectCharges = 1;
    int32 MaximumAirRedirectCharges = 1;
    int32 DashSerial = 0;
    float DashExtraSpeed = 0.f;
    float GroundAbilitySeconds = 0.f;
    void InstallEnvelopePhysics(const FBootTraversalEnvelope& Envelope, bool bInitialize);
    void UpdateEnvelopeTransition(float DeltaTime);
    bool HasStationaryGroundSupport() const;
    float TechniqueMinimumSpeed() const;
    float EffectiveHorizontalCap() const;
    float EffectiveTemporaryCap() const;
    void DecayDash(float DeltaTime);
    static constexpr uint8 WallRunMode = 1;
    bool FindRunnableWall(FHitResult& OutHit) const;
    bool TryStartWallRun();
    void LeaveWallRun();
    void ClampHorizontalSpeed();
    void DecayOverclock(float DeltaTime);
    float Now() const;

    TWeakObjectPtr<UPrimitiveComponent> Wall;
    TWeakObjectPtr<UPrimitiveComponent> LastExitedWall;
    TWeakObjectPtr<UPrimitiveComponent> KickedWallUntilLanding;
    int32 WallItem = INDEX_NONE;
    int32 LastExitedWallItem = INDEX_NONE;
    int32 KickedWallItem = INDEX_NONE;
    FVector WallNormal = FVector::ZeroVector;
    FVector LandingVelocity = FVector::ZeroVector;
    float WallSide = 0.f;
    float WallElapsed = 0.f;
    float WallBlockedUntil = -100.f;
    float AirStartedAt = -100.f;
    float LandedAt = -100.f;
    float OverclockExtraSpeed = 0.f;
    bool bSurfMomentum = false;
    float SurfGrantedAt = -100.f;
    bool bLandingHopAvailable = false;
    FVector AirRedirectTarget = FVector::ZeroVector;
    float AirRedirectRemaining = 0.f;
    float AirRedirectDegreesPerSecond = 0.f;
    int32 AirRedirectSerial = 0;
    bool bAirRedirectContactChanged = false;
};
