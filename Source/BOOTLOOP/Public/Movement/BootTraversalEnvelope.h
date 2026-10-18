#pragma once
#include "CoreMinimal.h"

struct FBootTraversalEnvelope
{
    uint32 Revision = 0;
    bool bValid = false;
    float RunCmps = 0.f, MomentumCmps = 0.f, TemporaryCmps = 0.f, AbsoluteCmps = 3600.f;
    float JumpZCmps = 600.f, GravityScale = 1.7f, AirControl = .6f, WallRunSeconds = 1.15f;
    float HopGraceSeconds = .12f, SlideBrakingScale = 1.f, OverclockSeconds = .75f;
    int32 AirRedirectCharges = 1, DashCharges = 0, GuardCharges = 0;
    /** Measured moving support speed, independent of the character caps. */
    float MaxBaseCmps = 0.f;
};
struct BOOTLOOP_API FBootTraversalPolicy
{
    static constexpr float SurfMomentumCmps = 350.f;
    static FBootTraversalEnvelope FromTempo(double EffectiveBpm, uint32 Revision);
    static bool Validate(const FBootTraversalEnvelope& Envelope, FText& Error);
};

