#include "Movement/BootMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/ScopedMovementUpdate.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

namespace
{
    constexpr float LandingGrace = .10f;
    constexpr float MinimumHopAirTime = .18f;
    constexpr float MinimumParkourSpeed = 650.f;
    constexpr float OverclockAmount = 200.f;
    constexpr float OverclockSeconds = .75f;
    constexpr float AirRedirectSeconds = .12f;
    constexpr float AirRedirectRechargeSeconds = .08f;
    constexpr float AirRedirectMaximumDegrees = 60.f;
    constexpr float DashImpulseCmps = 600.f;
    constexpr float DashDecaySeconds = .60f;
    constexpr float RampJumpGraceSeconds = .45f;
    constexpr float RampAttachDistanceCm = 14.f;
    constexpr float RampSeamNormalDot = .97f;
    constexpr float RampSeamStepCm = 40.f;

    FVector CarryRampVelocity(FVector Speed,FVector OldNormal,FVector NewNormal)
    {
        // Adjacent facets describe one banked curve. Rotate its tangent frame
        // without losing energy to a fresh projection at every short facet.
        if(FVector::DotProduct(OldNormal,NewNormal)<RampSeamNormalDot)
            return FVector::VectorPlaneProject(Speed,NewNormal);
        const double Yaw=FMath::Atan2(NewNormal.Y,NewNormal.X)-FMath::Atan2(OldNormal.Y,OldNormal.X);
        const FQuat HeadingTurn(FVector::UpVector,Yaw);
        // Constant-bank bends turn about world up, retaining horizontal speed
        // as well as total speed. A bank change only rotates the remaining tilt.
        const FQuat BankTurn=FQuat::FindBetweenNormals(HeadingTurn.RotateVector(OldNormal),NewNormal);
        return BankTurn.RotateVector(HeadingTurn.RotateVector(Speed));
    }

    void SetHorizontalSpeed(FVector& Velocity, float Speed)
    {
        const FVector Direction = Velocity.GetSafeNormal2D();
        Velocity.X = Direction.X * Speed;
        Velocity.Y = Direction.Y * Speed;
    }
}

UBootMovementComponent::UBootMovementComponent()
{
    MaxWalkSpeed = 900.f;
    MaxWalkSpeedCrouched = 350.f;
    JumpZVelocity = 600.f;
    GravityScale = 1.7f;
    AirControl = .6f;
    FallingLateralFriction = 0.f;
    BrakingDecelerationFalling = 0.f;
    MaxAcceleration = 7000.f;
    GroundFriction = 6.f;
    BrakingDecelerationWalking = 2200.f;
    MaxSimulationTimeStep = 1.f / 60.f;
    MaxSimulationIterations = 8;
}

void UBootMovementComponent::ApplyTraversalEnvelope(const FBootTraversalEnvelope& Envelope)
{
    FText Error;
    if (!FBootTraversalPolicy::Validate(Envelope, Error)) return;
    if (!TraversalEnvelope.bValid)
    {
        // Preserve the legacy map's configured movement, rather than assuming
        // every old asset uses the constructor defaults.
        LegacyPhysics.RunCmps = MaxWalkSpeed;
        LegacyPhysics.MomentumCmps = MomentumCap;
        LegacyPhysics.TemporaryCmps = OverclockCap;
        LegacyPhysics.JumpZCmps = JumpZVelocity;
        LegacyPhysics.GravityScale = GravityScale;
        LegacyPhysics.AirControl = AirControl;
        LegacyPhysics.WallRunSeconds = WallRunSeconds;
        LegacyHopGain = HopGain;
        TraversalEnvelope = Envelope;
        InstallEnvelopePhysics(TraversalEnvelope, true);
        return;
    }
    if (Envelope.Revision <= TraversalEnvelope.Revision
        || (PreparedEnvelope.bValid && Envelope.Revision <= PreparedEnvelope.Revision)) return;
    PreparedEnvelope = Envelope;
}

bool UBootMovementComponent::HasStationaryGroundSupport() const
{
    if (!HasValidData() || !IsMovingOnGround() || !CurrentFloor.IsWalkableFloor()) return false;
    const UPrimitiveComponent* Base = CharacterOwner->GetMovementBase();
    return !Base || (!Base->IsSimulatingPhysics()
        && Base->GetComponentVelocity().IsNearlyZero(1.f));
}

bool UBootMovementComponent::CanCommitTraversalEnvelope(const FBootSafeBoundary& Boundary,float TransitionSeconds) const
{
    return PreparedEnvelope.bValid && Boundary.PreparedRevision == PreparedEnvelope.Revision
        && Boundary.bSupported && Boundary.bNoActiveCommitment && Boundary.bFutureReady
        && HasStationaryGroundSupport() && FMath::IsFinite(TransitionSeconds) && TransitionSeconds>0.f;
}
bool UBootMovementComponent::CommitTraversalEnvelope(const FBootSafeBoundary& Boundary, float TransitionSeconds)
{
    if (!CanCommitTraversalEnvelope(Boundary,TransitionSeconds))return false;
    BlendFromEnvelope = TraversalEnvelope;
    BlendTargetEnvelope = PreparedEnvelope;
    EnvelopeBlendElapsed = 0.f;
    EnvelopeBlendSeconds = TransitionSeconds;
    // A new revision owns one target. Its speed fields start from the current
    // values and progress only on safe support; jumping freezes that blend.
    TraversalEnvelope = PreparedEnvelope;
    TraversalEnvelope.RunCmps = BlendFromEnvelope.RunCmps;
    TraversalEnvelope.MomentumCmps = BlendFromEnvelope.MomentumCmps;
    TraversalEnvelope.TemporaryCmps = BlendFromEnvelope.TemporaryCmps;
    PreparedEnvelope = {};
    InstallEnvelopePhysics(TraversalEnvelope, false);
    return true;
}

void UBootMovementComponent::InstallEnvelopePhysics(const FBootTraversalEnvelope& Envelope, bool bInitialize)
{
    MaxWalkSpeed = Envelope.RunCmps;
    MomentumCap = Envelope.MomentumCmps;
    OverclockCap = Envelope.TemporaryCmps;
    JumpZVelocity = Envelope.JumpZCmps;
    GravityScale = Envelope.GravityScale;
    AirControl = Envelope.AirControl;
    WallRunSeconds = Envelope.WallRunSeconds;
    HopGain = LegacyHopGain * Envelope.RunCmps / 1100.f;
    const int32 NewRedirectMax = FMath::Clamp(Envelope.AirRedirectCharges, 0, 3);
    const int32 NewDashMax = FMath::Clamp(Envelope.DashCharges, 0, 2);
    AirRedirectCharges = bInitialize ? NewRedirectMax
        : FMath::Clamp(AirRedirectCharges + FMath::Max(0, NewRedirectMax - MaximumAirRedirectCharges), 0, NewRedirectMax);
    DashCharges = bInitialize ? NewDashMax
        : FMath::Clamp(DashCharges + FMath::Max(0, NewDashMax - MaximumDashCharges), 0, NewDashMax);
    MaximumAirRedirectCharges = NewRedirectMax;
    MaximumDashCharges = NewDashMax;
}

void UBootMovementComponent::UpdateEnvelopeTransition(float DeltaTime)
{
    if (EnvelopeBlendSeconds <= 0.f || !HasStationaryGroundSupport()) return;
    EnvelopeBlendElapsed = FMath::Min(EnvelopeBlendSeconds,
        EnvelopeBlendElapsed + FMath::Max(0.f, DeltaTime));
    const float Alpha = EnvelopeBlendElapsed / EnvelopeBlendSeconds;
    TraversalEnvelope.RunCmps = FMath::Lerp(BlendFromEnvelope.RunCmps, BlendTargetEnvelope.RunCmps, Alpha);
    TraversalEnvelope.MomentumCmps = FMath::Lerp(BlendFromEnvelope.MomentumCmps, BlendTargetEnvelope.MomentumCmps, Alpha);
    TraversalEnvelope.TemporaryCmps = FMath::Lerp(BlendFromEnvelope.TemporaryCmps, BlendTargetEnvelope.TemporaryCmps, Alpha);
    MaxWalkSpeed = TraversalEnvelope.RunCmps;
    MomentumCap = TraversalEnvelope.MomentumCmps;
    OverclockCap = TraversalEnvelope.TemporaryCmps;
    HopGain = LegacyHopGain * TraversalEnvelope.RunCmps / 1100.f;
    if (EnvelopeBlendElapsed >= EnvelopeBlendSeconds) EnvelopeBlendSeconds = 0.f;
}

float UBootMovementComponent::GetManualHopGrace() const
{
    return TraversalEnvelope.bValid ? TraversalEnvelope.HopGraceSeconds : LandingGrace;
}

float UBootMovementComponent::TechniqueMinimumSpeed() const
{
    return MinimumParkourSpeed * (TraversalEnvelope.bValid ? TraversalEnvelope.RunCmps / 1100.f : 1.f);
}

float UBootMovementComponent::GetMovementPaceAlpha() const
{
    const float Run = TraversalEnvelope.bValid ? TraversalEnvelope.RunCmps : 900.f;
    return FMath::Clamp((static_cast<float>(Velocity.Size2D()) - Run)
        / FMath::Max(1.f, GetEffectiveMomentumCap() - Run), 0.f, 1.f);
}

bool UBootMovementComponent::GetWallContact(FVector& OutWorldPoint, FVector& OutWorldNormal) const
{
    FHitResult Hit;
    if (!IsWallRunning() || !FindRunnableWall(Hit) || Hit.GetComponent() != Wall.Get()) return false;
    OutWorldPoint = Hit.ImpactPoint;
    OutWorldNormal = Hit.ImpactNormal;
    return true;
}

float UBootMovementComponent::GetEffectiveMomentumCap() const
{
    const float Absolute = TraversalEnvelope.bValid ? TraversalEnvelope.AbsoluteCmps : OverclockCap;
    return FMath::Min(Absolute, MomentumCap + GetSurfMomentumBonus());
}
float UBootMovementComponent::EffectiveTemporaryCap() const
{
    const float Absolute = TraversalEnvelope.bValid ? TraversalEnvelope.AbsoluteCmps : OverclockCap;
    return FMath::Min(Absolute, OverclockCap + GetSurfMomentumBonus());
}
float UBootMovementComponent::EffectiveHorizontalCap() const
{
    return FMath::Max(0.f, FMath::Min(EffectiveTemporaryCap(),
        GetEffectiveMomentumCap() + OverclockExtraSpeed + DashExtraSpeed));
}
bool UBootMovementComponent::GrantSurfMomentum()
{
    if (!HasValidData() || !TraversalEnvelope.bValid || Velocity.ContainsNaN()
        || Velocity.Size2D() < TechniqueMinimumSpeed()) return false;
    bSurfMomentum = true;
    SurfGrantedAt = Now();
    return true;
}

bool UBootMovementComponent::TryBufferDash(FVector DesiredWorldDirection)
{
    if (!HasValidData() || !TraversalEnvelope.bValid || !IsFalling() || DashCharges <= 0
        || DesiredWorldDirection.ContainsNaN() || Velocity.ContainsNaN()) return false;
    const FVector Direction = DesiredWorldDirection.GetSafeNormal2D();
    if (Direction.IsNearlyZero()) return false;
    const FVector Before(Velocity.X, Velocity.Y, 0);
    FVector Candidate = Before + Direction * DashImpulseCmps;
    Candidate = Candidate.GetClampedToMaxSize(EffectiveTemporaryCap());
    const FVector Change = Candidate - Before;
    if (Change.Size2D() < 25. || Change.Size2D() > DashImpulseCmps + .01) return false;

    // The impulse itself never moves/teleports the capsule. A short real sweep
    // rejects a flush wall with no available motion; CharacterMovement owns all
    // subsequent displacement, including the collision that dissipates reserve.
    const auto* Capsule = CharacterOwner->GetCapsuleComponent();
    FHitResult Hit;
    const FVector Start = UpdatedComponent->GetComponentLocation();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BootDashClearance), false, CharacterOwner);
    const bool bBlocked = GetWorld()->SweepSingleByChannel(Hit, Start,
        Start + Candidate.GetSafeNormal2D() * 8.f, UpdatedComponent->GetComponentQuat(),
        Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(), Query,
        FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
    if (bBlocked && (Hit.bStartPenetrating || Hit.Time * 8.f < 2.f)) return false;

    DashExtraSpeed += FMath::Max(0.f, static_cast<float>(Candidate.Size2D() - Before.Size2D()));
    Velocity.X = Candidate.X; Velocity.Y = Candidate.Y;
    --DashCharges;
    GroundAbilitySeconds = 0.f;
    DashSerial = DashSerial == MAX_int32 ? 1 : DashSerial + 1;
    UpdateComponentVelocity();
    return true;
}

void UBootMovementComponent::DecayDash(float DeltaTime)
{
    if (DashExtraSpeed <= 0.f) return;
    const float Removed = FMath::Min(DashExtraSpeed,
        FMath::Max(0.f, DeltaTime) * DashImpulseCmps * FMath::Max(1, MaximumDashCharges) / DashDecaySeconds);
    SetHorizontalSpeed(Velocity, FMath::Max(0.f, static_cast<float>(Velocity.Size2D()) - Removed));
    DashExtraSpeed -= Removed;
}

float UBootMovementComponent::Now() const
{
    return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

bool UBootMovementComponent::TryAirRedirect(FVector DesiredWorldDirection)
{
    if (!HasValidData() || !IsFalling() || AirRedirectCharges <= 0 || IsAirRedirecting()
        || CharacterOwner->bIsCrouched || bWantsToCrouch || Velocity.Size2D() < 300.f
        || DesiredWorldDirection.ContainsNaN()) return false;

    const FVector Direction = DesiredWorldDirection.GetSafeNormal2D();
    if (Direction.IsNearlyZero()) return false;
    const FVector Heading = Velocity.GetSafeNormal2D();
    const float DesiredAngle = FMath::RadiansToDegrees(FMath::Atan2(
        static_cast<float>(FVector::CrossProduct(Heading, Direction).Z),
        static_cast<float>(FVector::DotProduct(Heading, Direction))));
    if (FMath::Abs(DesiredAngle) < 5.f) return false;

    const float Angle = FMath::Clamp(DesiredAngle, -AirRedirectMaximumDegrees, AirRedirectMaximumDegrees);
    AirRedirectTarget = Heading.RotateAngleAxis(Angle, FVector::UpVector);
    AirRedirectRemaining = AirRedirectSeconds;
    AirRedirectDegreesPerSecond = FMath::Abs(Angle) / AirRedirectSeconds;
    GroundAbilitySeconds = 0.f;
    --AirRedirectCharges;
    AirRedirectSerial = AirRedirectSerial == MAX_int32 ? 1 : AirRedirectSerial + 1;
    return true;
}

float UBootMovementComponent::GetMaxSpeed() const
{
    const float Cap = EffectiveHorizontalCap();
    if (IsWallRunning() || IsRampSurfing()) return Cap;
    const float ConfiguredSpeed = Super::GetMaxSpeed();
    // Preserve a freshly landed hop and airborne steering. Holding ordinary run
    // on the floor for longer than the landing window returns to base run speed.
    const bool bRetainMomentum = IsFalling()
        || (IsMovingOnGround() && Now() - LandedAt <= GetManualHopGrace())
        || (IsMovingOnGround() && IsCrouching() && MaxWalkSpeedCrouched >= MaxWalkSpeed);
    if (bRetainMomentum)
        return FMath::Clamp(FMath::Max(ConfiguredSpeed, static_cast<float>(Velocity.Size2D())), 0.f, Cap);
    return FMath::Min(ConfiguredSpeed + OverclockExtraSpeed, Cap);
}

void UBootMovementComponent::ClampHorizontalSpeed()
{
    const float Cap = EffectiveHorizontalCap();
    if (Velocity.SizeSquared2D() > FMath::Square(Cap)) SetHorizontalSpeed(Velocity, Cap);
}

void UBootMovementComponent::PrepareGroundJump()
{
    const float Speed = static_cast<float>(Velocity.Size2D());
    const float TemporaryReserve = OverclockExtraSpeed + DashExtraSpeed;
    const float BaseSpeed = FMath::Max(0.f, Speed - TemporaryReserve);
    if (bLandingHopAvailable && Now() - LandedAt <= GetManualHopGrace() && BaseSpeed >= TechniqueMinimumSpeed())
    {
        // Incoming velocity was captured before ground braking and before the
        // Runner's buffered-jump callback. The temporary impulse never compounds.
        const float IncomingBase = static_cast<float>(LandingVelocity.Size2D());
        const float NewBase = FMath::Min(GetEffectiveMomentumCap(), FMath::Max(BaseSpeed, IncomingBase) + HopGain);
        if (Speed > UE_KINDA_SMALL_NUMBER)
            SetHorizontalSpeed(Velocity, FMath::Min(EffectiveTemporaryCap(), NewBase + TemporaryReserve));
    }
    bLandingHopAvailable = false;
    ClampHorizontalSpeed();
}

void UBootMovementComponent::GrantOverclock()
{
    const float Speed = static_cast<float>(Velocity.Size2D());
    if (Speed < 100.f) return;
    const float BaseSpeed = FMath::Clamp(Speed - OverclockExtraSpeed - DashExtraSpeed, 0.f, GetEffectiveMomentumCap());
    const float Amount = OverclockAmount * (TraversalEnvelope.bValid ? TraversalEnvelope.RunCmps / 1100.f : 1.f);
    OverclockExtraSpeed = FMath::Min(Amount, FMath::Max(0.f, EffectiveTemporaryCap() - GetEffectiveMomentumCap()));
    SetHorizontalSpeed(Velocity, FMath::Min(EffectiveTemporaryCap(), BaseSpeed + OverclockExtraSpeed + DashExtraSpeed));
}

void UBootMovementComponent::DecayOverclock(float DeltaTime)
{
    if (OverclockExtraSpeed <= 0.f) return;
    const float Amount = OverclockAmount * (TraversalEnvelope.bValid ? TraversalEnvelope.RunCmps / 1100.f : 1.f);
    const float Seconds = TraversalEnvelope.bValid ? TraversalEnvelope.OverclockSeconds : OverclockSeconds;
    const float Removed = FMath::Min(OverclockExtraSpeed,
        FMath::Max(0.f, DeltaTime) * Amount / FMath::Max(.01f, Seconds));
    const float Speed = static_cast<float>(Velocity.Size2D());
    SetHorizontalSpeed(Velocity, FMath::Max(0.f, Speed - Removed));
    OverclockExtraSpeed -= Removed;
    if (Velocity.IsNearlyZero()) OverclockExtraSpeed = 0.f;
}

void UBootMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    const bool bStartedOnWalkableGround = IsMovingOnGround() && CurrentFloor.IsWalkableFloor();
    const auto* Support = CharacterOwner ? CharacterOwner->GetMovementBase() : nullptr;
    const bool bMovingSupport = Support && !Support->GetComponentVelocity().IsNearlyZero(1.f);
    // A brief landing remains a manual-hop opportunity. Sliding and actual rides
    // preserve momentum; an ordinary sustained run ends this one chain reserve.
    if (bSurfMomentum && bStartedOnWalkableGround && !IsCrouching() && !bMovingSupport
        && Now()-FMath::Max(LandedAt,SurfGrantedAt)>GetManualHopGrace()) bSurfMomentum=false;
    bAirRedirectContactChanged = false;
    UpdateEnvelopeTransition(DeltaTime);
    DecayOverclock(DeltaTime);
    DecayDash(DeltaTime);
    const float BeforePhysicsSpeed = static_cast<float>(Velocity.Size2D());
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    const float AfterPhysicsSpeed = static_cast<float>(Velocity.Size2D());
    if (bSurfMomentum && (AfterPhysicsSpeed<TechniqueMinimumSpeed()
        || (BeforePhysicsSpeed>TechniqueMinimumSpeed() && AfterPhysicsSpeed<BeforePhysicsSpeed*.8f))) bSurfMomentum=false;
    // A swept collision or braking also dissipates its share of the additive
    // impulse. Do not subtract a full old impulse after the wall removed it.
    if (BeforePhysicsSpeed > UE_KINDA_SMALL_NUMBER && AfterPhysicsSpeed < BeforePhysicsSpeed)
    {
        OverclockExtraSpeed *= AfterPhysicsSpeed / BeforePhysicsSpeed;
        DashExtraSpeed *= AfterPhysicsSpeed / BeforePhysicsSpeed;
    }
    ClampHorizontalSpeed();
    if ((AirRedirectCharges < MaximumAirRedirectCharges || DashCharges < MaximumDashCharges)
        && !bAirRedirectContactChanged && bStartedOnWalkableGround
        && IsMovingOnGround() && CurrentFloor.IsWalkableFloor())
    {
        // Do not count an entire landing frame as contact. Any movement-mode
        // transition also clears this history, so short touches cannot stack.
        GroundAbilitySeconds += FMath::Max(0.f, DeltaTime);
        if (GroundAbilitySeconds + UE_SMALL_NUMBER >= AirRedirectRechargeSeconds)
        {
            AirRedirectCharges = MaximumAirRedirectCharges;
            DashCharges = MaximumDashCharges;
            GroundAbilitySeconds = 0.f;
        }
    }
    else
    {
        GroundAbilitySeconds = 0.f;
    }
}

void UBootMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
    GroundAbilitySeconds = 0.f;
    bAirRedirectContactChanged = true;
    if (!IsFalling()) AirRedirectRemaining = 0.f;
    const bool bWasWallRunning = PreviousMovementMode == MOVE_Custom && PreviousCustomMode == WallRunMode;
    if (IsMovingOnGround())
    {
        LandedAt = Now();
        LandingVelocity = Velocity;
        SetHorizontalSpeed(LandingVelocity, FMath::Max(0.f,
            static_cast<float>(Velocity.Size2D()) - OverclockExtraSpeed - DashExtraSpeed));
        bLandingHopAvailable = (PreviousMovementMode == MOVE_Falling || bWasWallRunning)
            && Now() - AirStartedAt >= MinimumHopAirTime
            && Velocity.Size2D() - OverclockExtraSpeed - DashExtraSpeed >= TechniqueMinimumSpeed();
        KickedWallUntilLanding.Reset();
        KickedWallItem = INDEX_NONE;
    }
    else if (IsFalling() && (PreviousMovementMode == MOVE_Walking || PreviousMovementMode == MOVE_NavWalking
        || PreviousMovementMode == MOVE_None))
    {
        AirStartedAt = Now();
    }
    if (bWasWallRunning && !IsWallRunning())
    {
        LastExitedWall = Wall;
        LastExitedWallItem = WallItem;
        WallBlockedUntil = Now() + .35f;
        Wall.Reset();
        WallItem = INDEX_NONE;
        WallSide = 0.f;
        WallElapsed = 0.f;
    }
    // Super synchronously calls ACharacter::OnMovementModeChanged. Its buffered
    // jump must see the landing snapshot above, rather than the previous landing.
    Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}

bool UBootMovementComponent::FindSurfRamp(FHitResult& OutHit) const
{
    if(!HasValidData() || !CharacterOwner || !GetWorld())return false;
    const auto* Capsule=CharacterOwner->GetCapsuleComponent();
    const FVector Location=UpdatedComponent->GetComponentLocation();
    const FVector Right=FVector::CrossProduct(FVector::UpVector,Velocity.GetSafeNormal2D());
    const FVector Down(0,0,-Capsule->GetScaledCapsuleHalfHeight()-75.);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BootRampSurf),false,CharacterOwner);
    float BestDistance=TNumericLimits<float>::Max();bool Found=false;
    // A normal-directed probe keeps the contact through a turning seam even
    // when a vertical probe first sees the neighbouring slab's narrow end face.
    for(const FVector Offset:{Down,Down*.65+Right*110.,Down*.65-Right*110.,
        -RampNormal*(Capsule->GetScaledCapsuleHalfHeight()+75.)})
    {
        FHitResult Hit;
        if(GetWorld()->LineTraceSingleByChannel(Hit,Location,Location+Offset,Capsule->GetCollisionObjectType(),Query)
            && Hit.IsValidBlockingHit() && Hit.GetComponent()
            && Hit.GetComponent()->ComponentHasTag(TEXT("BootSurfRamp"))
            && Hit.ImpactNormal.Z>.25 && Hit.ImpactNormal.Z<.8)
        {
            const FVector Normal=Hit.ImpactNormal.GetSafeNormal();
            const float Support=Capsule->GetScaledCapsuleRadius()
                +(Capsule->GetScaledCapsuleHalfHeight()-Capsule->GetScaledCapsuleRadius())*Normal.Z+2.f;
            const float Distance=FMath::Abs(float(FVector::DotProduct(Location-Hit.ImpactPoint,Normal))-Support);
            if(Distance<BestDistance){BestDistance=Distance;OutHit=Hit;Found=true;}
        }
    }
    return Found;
}

bool UBootMovementComponent::TryStartRampSurf()
{
    if(!IsFalling() || Now()<RampBlockedUntil || Velocity.Size2D()<300.f)return false;
    FHitResult Hit;if(!FindSurfRamp(Hit))return false;
    const FVector Normal=Hit.ImpactNormal.GetSafeNormal();
    // The long probes maintain contact while traversing a curve. Enter surf
    // only at the actual capsule surface, never by pulling an airborne jump
    // back from the probe's full reach, or while travelling away from the plane.
    const auto* Capsule=CharacterOwner->GetCapsuleComponent();
    const float Support=Capsule->GetScaledCapsuleRadius()
        +(Capsule->GetScaledCapsuleHalfHeight()-Capsule->GetScaledCapsuleRadius())*Normal.Z+2.f;
    const float Distance=FVector::DotProduct(UpdatedComponent->GetComponentLocation()-Hit.ImpactPoint,Normal);
    if(Distance>Support+RampAttachDistanceCm || FVector::DotProduct(Velocity,Normal)>10.f)return false;
    RampNormal=Normal;
    Velocity=FVector::VectorPlaneProject(Velocity,RampNormal);
    SetMovementMode(MOVE_Custom,RampSurfMode);
    return true;
}

bool UBootMovementComponent::TryRampJump()
{
    if(!IsRampSurfing() || !HasValidData())return false;
    const FVector Outward=RampNormal;
    RampBlockedUntil=Now()+RampJumpGraceSeconds;
    SetMovementMode(MOVE_Falling);
    Velocity+=Outward*120.f;
    Velocity.Z=FMath::Max(float(Velocity.Z),JumpZVelocity);
    ClampHorizontalSpeed();return true;
}

void UBootMovementComponent::PhysRampSurf(float DeltaTime,int32 Iterations)
{
    if(DeltaTime<MIN_TICK_TIME || !HasValidData())return;
    if(!CharacterOwner->Controller && !bRunPhysicsWithNoController)return;
    float Remaining=DeltaTime;
    while(Remaining>=MIN_TICK_TIME && Iterations<MaxSimulationIterations && IsRampSurfing())
    {
        ++Iterations;
        const float Step=GetSimulationTimeStep(Remaining,Iterations);Remaining-=Step;
        FHitResult Surface;
        if(!FindSurfRamp(Surface))
        {SetMovementMode(MOVE_Falling);StartNewPhysics(Remaining+Step,Iterations);return;}
        const FVector SurfaceNormal=Surface.ImpactNormal.GetSafeNormal();
        Velocity=CarryRampVelocity(Velocity,RampNormal,SurfaceNormal);
        RampNormal=SurfaceNormal;
        // Arcade Source-style surf: no floor friction, gravity projected onto
        // the incline, and player strafe acceleration along its actual plane.
        const FVector Force=FVector(0,0,GetGravityZ()*.45f)+Acceleration*.28f;
        Velocity=FVector::VectorPlaneProject(Velocity+Force*Step,RampNormal);
        Velocity.Z=FMath::Clamp(float(Velocity.Z),-700.f,500.f);
        ClampHorizontalSpeed();
        const auto* Capsule=CharacterOwner->GetCapsuleComponent();
        const float Support=Capsule->GetScaledCapsuleRadius()
            +(Capsule->GetScaledCapsuleHalfHeight()-Capsule->GetScaledCapsuleRadius())*RampNormal.Z+2.f;
        const float Distance=FVector::DotProduct(UpdatedComponent->GetComponentLocation()-Surface.ImpactPoint,RampNormal);
        const FVector Correction=RampNormal*FMath::Clamp(Support-Distance,-12.f,12.f);
        FHitResult Hit;SafeMoveUpdatedComponent(Velocity*Step+Correction,UpdatedComponent->GetComponentQuat(),true,Hit);
        if(!HasValidData() || !IsRampSurfing())return;
        if(Hit.IsValidBlockingHit())
        {
            if(Velocity.Z<=0.f && IsValidLandingSpot(UpdatedComponent->GetComponentLocation(),Hit))
            {SetMovementMode(MOVE_Falling);ProcessLanded(Hit,Remaining,Iterations);return;}
            auto CrossInternalSeam=[&]()
            {
                // Only a neighbouring slab of the current surf component may
                // use this bounded step. Floors, walls, hazards and separate
                // ramps retain their ordinary blocking collision.
                auto* Ramp=Cast<UInstancedStaticMeshComponent>(Hit.GetComponent());
                if(!Ramp || Ramp!=Surface.GetComponent() || Hit.Item<0 || Surface.Item<0
                    || FMath::Abs(Hit.Item-Surface.Item)>2 || Hit.bStartPenetrating)return false;
                FTransform Facet;
                if(!Ramp->GetInstanceTransform(Hit.Item,Facet,true))return false;
                const FVector NextNormal=Facet.GetUnitAxis(EAxis::Z);
                if(FVector::DotProduct(RampNormal,NextNormal)<RampSeamNormalDot)return false;
                const FVector Travel=CarryRampVelocity(Velocity,RampNormal,NextNormal)*Step*(1.f-Hit.Time);
                if(Travel.IsNearlyZero())return false;
                FScopedMovementUpdate SeamMove(UpdatedComponent,EScopedUpdate::DeferredUpdates);
                auto Fail=[&](){SeamMove.RevertMove();return false;};
                const FVector Before=UpdatedComponent->GetComponentLocation();
                const FQuat Rotation=UpdatedComponent->GetComponentQuat();
                const float Lift=FMath::Min(MaxStepHeight,RampSeamStepCm);
                if(Lift<=0.f)return Fail();
                FHitResult LiftHit,ForwardHit,SettleHit;
                SafeMoveUpdatedComponent(FVector(0,0,Lift),Rotation,true,LiftHit);
                if(LiftHit.bBlockingHit)return Fail();
                SafeMoveUpdatedComponent(Travel,Rotation,true,ForwardHit);
                if(ForwardHit.bBlockingHit)return Fail();
                const auto* Capsule=CharacterOwner->GetCapsuleComponent();
                const float Support=Capsule->GetScaledCapsuleRadius()
                    +(Capsule->GetScaledCapsuleHalfHeight()-Capsule->GetScaledCapsuleRadius())*NextNormal.Z+2.f;
                const FVector PlanePoint=Facet.TransformPosition(FVector(0,0,50.));
                const float Distance=FVector::DotProduct(UpdatedComponent->GetComponentLocation()-PlanePoint,NextNormal);
                if(FMath::Abs(Support-Distance)>Lift+RampAttachDistanceCm)return Fail();
                SafeMoveUpdatedComponent(NextNormal*(Support-Distance),Rotation,true,SettleHit);
                if(SettleHit.bBlockingHit && (SettleHit.GetComponent()!=Ramp
                    || FVector::DotProduct(SettleHit.ImpactNormal,NextNormal)<RampSeamNormalDot))return Fail();
                FHitResult Contact;
                if(!FindSurfRamp(Contact) || Contact.GetComponent()!=Ramp
                    || FMath::Abs(Contact.Item-Hit.Item)>1
                    || FVector::DotProduct(Contact.ImpactNormal,RampNormal)<RampSeamNormalDot
                    || FVector::DotProduct(UpdatedComponent->GetComponentLocation()-Before,Travel)<=0.)return Fail();
                const FVector ContactNormal=Contact.ImpactNormal.GetSafeNormal();
                const float ContactSupport=Capsule->GetScaledCapsuleRadius()
                    +(Capsule->GetScaledCapsuleHalfHeight()-Capsule->GetScaledCapsuleRadius())*ContactNormal.Z+2.f;
                const float ContactDistance=FVector::DotProduct(UpdatedComponent->GetComponentLocation()-Contact.ImpactPoint,ContactNormal);
                if(FMath::Abs(ContactDistance-ContactSupport)>RampAttachDistanceCm)return Fail();
                Velocity=CarryRampVelocity(Velocity,RampNormal,ContactNormal);
                RampNormal=ContactNormal;ClampHorizontalSpeed();return true;
            };
            if(CrossInternalSeam())continue;
            HandleImpact(Hit,Step,Velocity*Step);
            if(!IsRampSurfing())return;
            SlideAlongSurface(Velocity*Step,1.f-Hit.Time,Hit.Normal,Hit,true);
            if(FMath::Abs(FVector::DotProduct(Hit.Normal,RampNormal))<.7f)
                Velocity=FVector::VectorPlaneProject(Velocity,Hit.Normal);
        }
    }
}

bool UBootMovementComponent::FindRunnableWall(FHitResult& OutHit) const
{
    if (!HasValidData() || !CharacterOwner || !GetWorld()) return false;
    const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
    const FVector Location = UpdatedComponent->GetComponentLocation();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BootWallRun), false, CharacterOwner);
    const ECollisionChannel Channel = Capsule->GetCollisionObjectType();

    // Do not glue the player to a wall while they are about to touch the floor.
    FHitResult FloorHit;
    const FVector Feet = Location - FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() - 2.f);
    if (GetWorld()->LineTraceSingleByChannel(FloorHit, Feet, Feet - FVector(0, 0, 65.f), Channel, Params)
        && FloorHit.IsValidBlockingHit() && IsWalkable(FloorHit)) return false;

    const FVector Direction = Velocity.GetSafeNormal2D();
    if (Direction.IsNearlyZero()) return false;
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Direction);
    const float Reach = Capsule->GetScaledCapsuleRadius() + 24.f;
    for (const float Side : {-1.f, 1.f})
    {
        FHitResult Hit;
        if (!GetWorld()->LineTraceSingleByChannel(Hit, Location, Location + Right * Side * Reach, Channel, Params)
            || !Hit.IsValidBlockingHit()) continue;
        UPrimitiveComponent* HitComponent = Hit.GetComponent();
        const AActor* HitActor = Hit.GetActor();
        if (!HitComponent || (!HitComponent->ComponentHasTag(TEXT("BootWallRun"))
            && (!HitActor || !HitActor->ActorHasTag(TEXT("BootWallRun"))))) continue;
        if (FMath::Abs(Hit.ImpactNormal.Z) > .2f
            || FMath::Abs(FVector::DotProduct(Direction, Hit.ImpactNormal)) > .45f) continue;
        if (HitComponent == KickedWallUntilLanding.Get() && Hit.Item == KickedWallItem) continue;
        if (HitComponent == LastExitedWall.Get() && Hit.Item == LastExitedWallItem && Now() < WallBlockedUntil) continue;
        OutHit = Hit;
        return true;
    }
    return false;
}

bool UBootMovementComponent::TryStartWallRun()
{
    if (!IsFalling() || !HasValidData() || CharacterOwner->bIsCrouched
        || Velocity.Size2D() < TechniqueMinimumSpeed() || Velocity.Z < -650.f) return false;
    FHitResult Hit;
    if (!FindRunnableWall(Hit)) return false;
    Wall = Hit.GetComponent();
    WallItem = Hit.Item;
    WallNormal = Hit.ImpactNormal.GetSafeNormal2D();
    WallSide = FVector::DotProduct(-WallNormal, CharacterOwner->GetActorRightVector()) >= 0.f ? 1.f : -1.f;
    WallElapsed = 0.f;
    Velocity = FVector::VectorPlaneProject(Velocity, WallNormal);
    Velocity.Z = FMath::Clamp(Velocity.Z, -150.f, 100.f);
    ClampHorizontalSpeed();
    SetMovementMode(MOVE_Custom, WallRunMode);
    return true;
}

void UBootMovementComponent::PhysFalling(float DeltaTime, int32 Iterations)
{
    if (DeltaTime < MIN_TICK_TIME || !HasValidData()) return;
    if(TryStartRampSurf()){StartNewPhysics(DeltaTime,Iterations);return;}
    float Remaining = DeltaTime;
    while (Remaining >= MIN_TICK_TIME && Iterations < MaxSimulationIterations && HasValidData() && IsFalling())
    {
        if (TryStartWallRun())
        {
            StartNewPhysics(Remaining, Iterations);
            return;
        }
        if (CharacterOwner->bIsCrouched || bWantsToCrouch) AirRedirectRemaining = 0.f;
        if (!IsAirRedirecting())
        {
            Super::PhysFalling(Remaining, Iterations);
            return;
        }

        const float Step = FMath::Min(GetSimulationTimeStep(Remaining, Iterations + 1), AirRedirectRemaining);
        // A heading transform uses the CURRENT magnitude. Native sweeps,
        // braking and overclock decay may remove speed; never save/restore it.
        const FVector Heading = Velocity.GetSafeNormal2D();
        const float AngleToTarget = FMath::RadiansToDegrees(FMath::Atan2(
            static_cast<float>(FVector::CrossProduct(Heading, AirRedirectTarget).Z),
            static_cast<float>(FVector::DotProduct(Heading, AirRedirectTarget))));
        const float Angle = FMath::Clamp(AngleToTarget,
            -AirRedirectDegreesPerSecond * Step, AirRedirectDegreesPerSecond * Step);
        const FVector Rotated = Velocity.RotateAngleAxis(Angle, FVector::UpVector);
        Velocity.X = Rotated.X;
        Velocity.Y = Rotated.Y;
        AirRedirectRemaining = FMath::Max(0.f, AirRedirectRemaining - Step);
        Remaining -= Step;
        {
            // Input chose a fixed target at acceptance. Ordinary air steering
            // resumes afterward; it cannot expand the maneuver's angle/speed.
            TGuardValue<FVector> RestoreAcceleration(Acceleration, FVector::ZeroVector);
            Super::PhysFalling(Step, Iterations);
        }
        ++Iterations;
        if (!IsFalling() && Remaining >= MIN_TICK_TIME)
        {
            StartNewPhysics(Remaining, Iterations);
            return;
        }
    }
}

void UBootMovementComponent::LeaveWallRun()
{
    if (IsWallRunning()) SetMovementMode(MOVE_Falling);
}

bool UBootMovementComponent::TryWallKick()
{
    if (!IsWallRunning() || !HasValidData()) return false;
    const FVector Normal = WallNormal;
    KickedWallUntilLanding = Wall;
    KickedWallItem = WallItem;
    const FVector Tangent = FVector::VectorPlaneProject(Velocity, Normal).GetSafeNormal2D();
    const float TangentSpeed = static_cast<float>(Velocity.Size2D());
    LeaveWallRun();
    Velocity = Tangent * TangentSpeed + Normal * 380.f;
    Velocity.Z = 520.f;
    ClampHorizontalSpeed();
    UpdateComponentVelocity();
    return true;
}

void UBootMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
    if(IsRampSurfing()){PhysRampSurf(DeltaTime,Iterations);return;}
    if (!IsWallRunning())
    {
        Super::PhysCustom(DeltaTime, Iterations);
        return;
    }
    if (DeltaTime < MIN_TICK_TIME || !HasValidData()) return;
    if (!CharacterOwner->Controller && !bRunPhysicsWithNoController)
    {
        Velocity = FVector::ZeroVector;
        return;
    }
    float Remaining = DeltaTime;
    while (Remaining >= MIN_TICK_TIME && Iterations < MaxSimulationIterations && HasValidData())
    {
        ++Iterations;
        const float Step = GetSimulationTimeStep(Remaining, Iterations);
        Remaining -= Step;
        FHitResult WallHit;
        const bool bPullingAway = FVector::DotProduct(Acceleration, WallNormal) > GetMaxAcceleration() * .35f;
        if (WallElapsed >= WallRunSeconds || CharacterOwner->bIsCrouched || bPullingAway
            || !FindRunnableWall(WallHit) || WallHit.GetComponent() != Wall.Get() || WallHit.Item != WallItem
            || FVector::DotProduct(WallHit.ImpactNormal, WallNormal) < .85f)
        {
            LeaveWallRun();
            StartNewPhysics(Remaining + Step, Iterations);
            return;
        }
        WallElapsed += Step;
        const FVector Before = UpdatedComponent->GetComponentLocation();
        const float Speed = static_cast<float>(Velocity.Size2D());
        const FVector Along = FVector::VectorPlaneProject(Velocity, WallNormal).GetSafeNormal2D();
        const FVector TangentAcceleration = FVector::VectorPlaneProject(Acceleration, WallNormal);
        const float AlongAcceleration = static_cast<float>(FVector::DotProduct(TangentAcceleration, Along));
        // Input may recover base run speed, but cannot silently add hop speed.
        const float DesiredSpeed = FMath::Clamp(Speed + AlongAcceleration * Step * .3f,
            0.f, FMath::Max(MaxWalkSpeed, Speed));
        const float Vertical = FMath::Max(-320.f, static_cast<float>(Velocity.Z) + GetGravityZ() * .20f * Step);
        Velocity = Along * DesiredSpeed;
        Velocity.Z = Vertical;
        ClampHorizontalSpeed();
        const FVector Delta = Velocity * Step - WallNormal * 80.f * Step;
        FHitResult Hit;
        SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
        if (Hit.IsValidBlockingHit())
        {
            if (Velocity.Z <= 0.f && IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit))
            {
                LeaveWallRun();
                ProcessLanded(Hit, Remaining, Iterations);
                return;
            }
            HandleImpact(Hit, Step, Delta);
            if (!HasValidData() || !IsWallRunning()) return;
            SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);
        }
        if (!HasValidData() || !IsWallRunning()) return;
        // Use actual swept displacement: a front collision must remove speed.
        Velocity = (UpdatedComponent->GetComponentLocation() - Before) / Step;
        Velocity = FVector::VectorPlaneProject(Velocity, WallNormal);
        ClampHorizontalSpeed();
        if (Velocity.Size2D() < TechniqueMinimumSpeed() * .7f)
        {
            LeaveWallRun();
            StartNewPhysics(Remaining, Iterations);
            return;
        }
    }
}

void UBootMovementComponent::ResetParkour()
{
    bSurfMomentum=false;SurfGrantedAt=-100.f;
    AirRedirectTarget = FVector::ZeroVector;
    AirRedirectRemaining = AirRedirectDegreesPerSecond = GroundAbilitySeconds = 0.f;
    if (TraversalEnvelope.bValid)
    {
        MaxWalkSpeed = LegacyPhysics.RunCmps;
        MomentumCap = LegacyPhysics.MomentumCmps;
        OverclockCap = LegacyPhysics.TemporaryCmps;
        JumpZVelocity = LegacyPhysics.JumpZCmps;
        GravityScale = LegacyPhysics.GravityScale;
        AirControl = LegacyPhysics.AirControl;
        WallRunSeconds = LegacyPhysics.WallRunSeconds;
        HopGain = LegacyHopGain;
    }
    TraversalEnvelope = PreparedEnvelope = BlendFromEnvelope = BlendTargetEnvelope = {};
    EnvelopeBlendSeconds = EnvelopeBlendElapsed = DashExtraSpeed = 0.f;
    MaximumAirRedirectCharges = AirRedirectCharges = 1;
    MaximumDashCharges = DashCharges = 0;
    if (IsWallRunning() || IsRampSurfing()) SetMovementMode(MOVE_Falling);
    RampNormal=FVector::UpVector;RampBlockedUntil=-100.f;
    Wall.Reset();
    LastExitedWall.Reset();
    KickedWallUntilLanding.Reset();
    WallItem = LastExitedWallItem = KickedWallItem = INDEX_NONE;
    WallNormal = LandingVelocity = FVector::ZeroVector;
    WallSide = WallElapsed = OverclockExtraSpeed = 0.f;
    WallBlockedUntil = AirStartedAt = LandedAt = -100.f;
    bLandingHopAvailable = false;
    ClearAccumulatedForces();
    StopMovementImmediately();
    if (CharacterOwner) CharacterOwner->SetBase(static_cast<UPrimitiveComponent*>(nullptr));
}
