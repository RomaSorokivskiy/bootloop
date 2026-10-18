#pragma once
#include "CoreMinimal.h"
#include "Player/BootPoseTypes.h"

// Pure yaw-local rigid-body solver. Coordinates are relative to capsule centre.
struct BOOTLOOP_API FBootAvatarPoseState
{
    double GaitPhase = 0.;
    double Clock = 0.;
    float JogBlend = 0.f, AirBlend = 0.f, SlideBlend = 0.f, WallBlend = 0.f, CrouchBlend = 0.f;
    float GestureTime = 0.f, TakeoffTime = 0.f, LandingTime = 0.f;
    float LastWallSide = 1.f;
    EBootPoseAbilityEvent ActiveAbility = EBootPoseAbilityEvent::None;
    bool bInitialized = false, bWasGrounded = true;
    FVector ContactAnchors[2] = {FVector::ZeroVector, FVector::ZeroVector};
    float ContactAge[2] = {-1.f, -1.f};
};
struct BOOTLOOP_API FBootAvatarPose
{
    static constexpr float ThighCm = 40.f;
    static constexpr float ShinCm = 40.f;
    static constexpr float UpperArmCm = 28.f;
    static constexpr float ForearmCm = 30.f;
    FTransform Pelvis = FTransform::Identity, Torso = FTransform::Identity;
    FTransform Shoulders[2] = {FTransform::Identity, FTransform::Identity};
    FVector Hips[2] = {FVector::ZeroVector, FVector::ZeroVector};
    FVector Knees[2] = {FVector::ZeroVector, FVector::ZeroVector};
    FVector Ankles[2] = {FVector::ZeroVector, FVector::ZeroVector};
    FVector Elbows[2] = {FVector::ZeroVector, FVector::ZeroVector};
    FVector Wrists[2] = {FVector::ZeroVector, FVector::ZeroVector};
    FRotator Hands[2] = {FRotator::ZeroRotator, FRotator::ZeroRotator};
    FRotator Boots[2] = {FRotator::ZeroRotator, FRotator::ZeroRotator};
    bool bContactActive[2] = {false, false};
    int32 ContactFoot = INDEX_NONE;
    float StanceSeconds = 0.f;
};
class BOOTLOOP_API FBootAvatarPoseSolver
{
public:
    static FBootAvatarPose Solve(const FBootPoseInput& Input, float DeltaSeconds, FBootAvatarPoseState& State);
};



