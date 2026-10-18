#pragma once

#include "CoreMinimal.h"

// Velocity/acceleration are yaw-local and relative to the current support.
// World speed is separate because wind can be audible while riding a moving base.
enum class EBootPoseAbilityEvent : uint8 { None, Snap, Dash, WallKick };

struct BOOTLOOP_API FBootPoseInput
{
    FVector LocalVelocity = FVector::ZeroVector;
    FVector LocalAcceleration = FVector::ZeroVector;
    float VerticalVelocity = 0.f;
    float AimPitch = 0.f;
    float TurnRateDegrees = 0.f;
    float WorldPlanarSpeed = 0.f;
    float WallSide = 0.f;
    // Presentation consumes normalized pace; movement remains the speed authority.
    float NormalizedPace = -1.f;
    float CapsuleHalfHeight = 96.f;
    EBootPoseAbilityEvent AbilityEvent = EBootPoseAbilityEvent::None;
    FVector LocalWallContact = FVector::ZeroVector;
    bool bHasWallContact = false;
    bool bMotionEnabled = true;
    bool bGrounded = true;
    bool bCrouched = false;
    bool bSliding = false;
    bool bWallRunning = false;
    bool bAirRedirectReady = true;
};

enum class EBootFeedbackEvent : uint8
{
    FootContact, Takeoff, Land, SlideBegin, SlideEnd, WallBegin, WallEnd,
    Snap, Overclock, AirRedirect, Checkpoint,
    BufferDash, GuardHit, GuardRecharge, CardBuff, CardRisk, Intercept, Danger
};
