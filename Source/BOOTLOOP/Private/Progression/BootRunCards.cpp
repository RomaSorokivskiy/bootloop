#include "Progression/BootRunCards.h"

namespace
{
int32 Stack(const FBootRunModifiers& M, const TCHAR* Id, int32 Maximum)
{
    return FMath::Clamp(M.Stacks.FindRef(FName(Id)), 0, Maximum);
}
bool IsMeaningful(const FBootCardDefinition& D, const FBootCardEligibilityContext& C)
{
    if (!C.Envelope.bValid || C.BlockedCardIds.Contains(D.Id) || C.Modifiers.Stacks.FindRef(D.Id) >= D.MaxStacks) return false;
    for (const auto& Required : D.Prerequisites) if (!C.AvailablePrerequisites.Contains(Required)) return false;
    for (const auto& Conflict : D.Conflicts) if (C.Modifiers.Stacks.FindRef(Conflict) > 0) return false;
    const auto& E = C.Envelope;
    switch (D.Effect)
    {
    case EBootCardEffect::AirCache: return E.AirRedirectCharges < 3;
    case EBootCardEffect::BufferDash: return E.DashCharges < 2;
    case EBootCardEffect::PulseGuard: return E.GuardCharges < 2;
    case EBootCardEffect::WallCache: return E.WallRunSeconds < 2.4f - KINDA_SMALL_NUMBER;
    case EBootCardEffect::AeroBuffer: return E.AirControl < .85f - KINDA_SMALL_NUMBER;
    case EBootCardEffect::ServoReserve: return true;
    case EBootCardEffect::SlideBuffer: return E.SlideBrakingScale > .1f + KINDA_SMALL_NUMBER;
    case EBootCardEffect::SoftLanding: return E.HopGraceSeconds < .16f - KINDA_SMALL_NUMBER;
    case EBootCardEffect::PriorityEscalation: return C.bCanPrepareProcessBudget;
    case EBootCardEffect::HotLoop: return C.bCanPrepareCombinations;
    case EBootCardEffect::ThinCache:
    {
        const float Desired = 1.f - .1f * (Stack(C.Modifiers, TEXT("THIN CACHE"), 2) + 1);
        return C.bCanPrepareThinCache && FMath::IsFinite(C.MinimumWidthScale) && C.MinimumWidthScale > 0.f
            && Desired + KINDA_SMALL_NUMBER >= C.MinimumWidthScale;
    }
    case EBootCardEffect::UnsafeWindows: return C.bCanPrepareRiskySurf;
    }
    return false;
}
}

TConstArrayView<FBootCardDefinition> FBootRunCardRules::Catalog()
{
    static const TArray<FBootCardDefinition> Cards = {
        {TEXT("AIR CACHE"), EBootCardKind::Buff, 2, EBootCardEffect::AirCache, 1., 0., {}, {}},
        {TEXT("BUFFER DASH"), EBootCardKind::Buff, 2, EBootCardEffect::BufferDash, 1., 0., {}, {}},
        {TEXT("PULSE GUARD"), EBootCardKind::Buff, 2, EBootCardEffect::PulseGuard, 1., 0., {}, {}},
        {TEXT("WALL CACHE"), EBootCardKind::Buff, 3, EBootCardEffect::WallCache, .2, 0., {}, {}},
        {TEXT("AERO BUFFER"), EBootCardKind::Buff, 3, EBootCardEffect::AeroBuffer, .1, 0., {}, {}},
        {TEXT("SERVO RESERVE"), EBootCardKind::Buff, 3, EBootCardEffect::ServoReserve, .2, 0., {}, {}},
        {TEXT("SLIDE BUFFER"), EBootCardKind::Buff, 2, EBootCardEffect::SlideBuffer, -.15, 0., {}, {}},
        {TEXT("SOFT LANDING"), EBootCardKind::Buff, 3, EBootCardEffect::SoftLanding, .02, 0., {}, {}},
        {TEXT("PRIORITY ESCALATION"), EBootCardKind::Risk, 2, EBootCardEffect::PriorityEscalation, 1., .2, {}, {}},
        {TEXT("HOT LOOP"), EBootCardKind::Risk, 2, EBootCardEffect::HotLoop, 1., .15, {}, {}},
        {TEXT("THIN CACHE"), EBootCardKind::Risk, 2, EBootCardEffect::ThinCache, -.1, .2, {}, {}},
        {TEXT("UNSAFE WINDOWS"), EBootCardKind::Risk, 2, EBootCardEffect::UnsafeWindows, .2, .25, {}, {}}
    };
    return Cards;
}

TArray<FName> FBootRunCardRules::Eligible(const FBootCardEligibilityContext& C)
{
    TArray<FName> Result;
    FText Error;
    if (!FBootTraversalPolicy::Validate(C.Envelope, Error)) return Result;
    for (const auto& D : Catalog()) if (IsMeaningful(D, C)) Result.Add(D.Id);
    return Result;
}

TOptional<FBootCardOffer> FBootRunCardRules::BuildOffer(const FBootCardEligibilityContext& C,
    const FBootRunToken& Run, int64 Id, double Seconds, FRandomStream& Random)
{
    if (!Run.RunId.IsValid() || Id <= 0 || !FMath::IsFinite(Seconds) || Seconds < 0.) return {};
    auto Pool = Eligible(C);
    if (Pool.Num() < 3) return {};
    TArray<FName> Buffs;
    for (const auto& D : Catalog()) if (D.Kind == EBootCardKind::Buff && Pool.Contains(D.Id)) Buffs.Add(D.Id);
    if (Buffs.IsEmpty()) return {};
    FBootCardOffer Result{Run, Id, Seconds, {}};
    const FName Buff = Buffs[Random.RandRange(0, Buffs.Num() - 1)];
    Result.CardIds.Add(Buff); Pool.Remove(Buff);
    while (Result.CardIds.Num() < 3)
    {
        const int32 Index = Random.RandRange(0, Pool.Num() - 1);
        Result.CardIds.Add(Pool[Index]); Pool.RemoveAt(Index);
    }
    for (int32 I = 2; I > 0; --I) Result.CardIds.Swap(I, Random.RandRange(0, I));
    return Result;
}

bool FBootRunCardRules::Apply(FName Id, const FBootCardEligibilityContext& C, FBootRunModifiers& Out, FText& Error)
{
    if (!Eligible(C).Contains(Id))
    {
        Error = FText::FromString(TEXT("Card is no longer a legal meaningful choice.")); return false;
    }
    FBootRunModifiers Result = C.Modifiers;
    ++Result.Stacks.FindOrAdd(Id);
    const int32 Priority = Stack(Result, TEXT("PRIORITY ESCALATION"), 2);
    const int32 Hot = Stack(Result, TEXT("HOT LOOP"), 2);
    const int32 Thin = Stack(Result, TEXT("THIN CACHE"), 2);
    const int32 Unsafe = Stack(Result, TEXT("UNSAFE WINDOWS"), 2);
    Result.ExtraProcessBudget = Priority;
    Result.ExtraCombinationBudget = Hot;
    Result.WidthScale = FMath::Max(C.MinimumWidthScale, 1.f - .1f * Thin);
    Result.RiskySurfWeightScale = 1.f + .2f * Unsafe;
    Result.DistanceMultiplier = FMath::Min(3., 1. + .15 * Hot + .2 * Thin);
    Result.OperationMultiplier = FMath::Min(3., 1. + .2 * Priority + .25 * Unsafe);
    Out = MoveTemp(Result); Error = FText::GetEmpty(); return true;
}

FBootTraversalEnvelope FBootRunCardRules::ApplyToEnvelope(const FBootTraversalEnvelope& Base, const FBootRunModifiers& M)
{
    FBootTraversalEnvelope E = Base;
    E.AirRedirectCharges = FMath::Min(3, Base.AirRedirectCharges + Stack(M, TEXT("AIR CACHE"), 2));
    E.DashCharges = FMath::Min(2, Base.DashCharges + Stack(M, TEXT("BUFFER DASH"), 2));
    E.GuardCharges = FMath::Min(2, Base.GuardCharges + Stack(M, TEXT("PULSE GUARD"), 2));
    E.WallRunSeconds = FMath::Min(2.4f, Base.WallRunSeconds + .2f * Stack(M, TEXT("WALL CACHE"), 3));
    E.AirControl = FMath::Min(.85f, Base.AirControl + .1f * Stack(M, TEXT("AERO BUFFER"), 3));
    E.OverclockSeconds = Base.OverclockSeconds + .2f * Stack(M, TEXT("SERVO RESERVE"), 3);
    E.SlideBrakingScale = FMath::Max(.1f, Base.SlideBrakingScale * (1.f - .15f * Stack(M, TEXT("SLIDE BUFFER"), 2)));
    E.HopGraceSeconds = FMath::Min(.16f, Base.HopGraceSeconds + .02f * Stack(M, TEXT("SOFT LANDING"), 3));
    FText Error; E.bValid = FBootTraversalPolicy::Validate(E, Error);
    return E;
}
