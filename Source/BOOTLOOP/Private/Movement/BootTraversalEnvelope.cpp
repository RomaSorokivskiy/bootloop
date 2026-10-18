#include "Movement/BootTraversalEnvelope.h"

FBootTraversalEnvelope FBootTraversalPolicy::FromTempo(double EffectiveBpm, uint32 Revision)
{
    FBootTraversalEnvelope Result;
    Result.Revision = Revision;
    if (!FMath::IsFinite(EffectiveBpm) || EffectiveBpm <= 0.) return Result;
    const double Multiplier = FMath::Clamp(EffectiveBpm / 130., .70, 1.40);
    Result.RunCmps = static_cast<float>(1100. * Multiplier);
    Result.MomentumCmps = static_cast<float>(2000. * Multiplier);
    Result.TemporaryCmps = static_cast<float>(2400. * Multiplier);
    Result.bValid = true;
    return Result;
}
bool FBootTraversalPolicy::Validate(const FBootTraversalEnvelope& E, FText& Error)
{
    auto Reject = [&Error](const TCHAR* Reason) { Error = FText::FromString(Reason); return false; };
    if (!E.bValid) return Reject(TEXT("Traversal envelope has no valid tempo."));
    const float Values[] = {E.RunCmps, E.MomentumCmps, E.TemporaryCmps, E.AbsoluteCmps,
        E.JumpZCmps, E.GravityScale, E.AirControl, E.WallRunSeconds, E.HopGraceSeconds,
        E.SlideBrakingScale, E.OverclockSeconds, E.MaxBaseCmps};
    for (float Value : Values)
        if (!FMath::IsFinite(Value)) return Reject(TEXT("Traversal envelope contains a nonfinite value."));
    if (E.AbsoluteCmps != 3600.f || E.RunCmps <= 0.f || E.RunCmps > E.MomentumCmps
        || E.MomentumCmps > E.TemporaryCmps || E.TemporaryCmps > E.AbsoluteCmps)
        return Reject(TEXT("Traversal speed caps must be ordered within the 3600 cm/s guard."));
    if (E.JumpZCmps <= 0.f || E.GravityScale <= 0.f || E.AirControl < 0.f || E.AirControl > 1.f
        || E.WallRunSeconds < 0.f || E.HopGraceSeconds < 0.f || E.SlideBrakingScale < 0.f
        || E.OverclockSeconds < 0.f || E.MaxBaseCmps < 0.f)
        return Reject(TEXT("Traversal physics and durations must be valid nonnegative values."));
    if (E.AirRedirectCharges < 0 || E.DashCharges < 0 || E.GuardCharges < 0)
        return Reject(TEXT("Traversal charges cannot be negative."));
    Error = FText::GetEmpty();
    return true;
}
