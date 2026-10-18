#pragma once
#include "CoreMinimal.h"
#include "Run/BootRunTypes.h"
#include "Progression/BootRunModifiers.h"
#include "Movement/BootTraversalEnvelope.h"

enum class EBootCardKind : uint8 { Buff, Risk };
enum class EBootCardEffect : uint8 { AirCache, BufferDash, PulseGuard, WallCache, AeroBuffer, ServoReserve, SlideBuffer, SoftLanding, PriorityEscalation, HotLoop, ThinCache, UnsafeWindows };
struct FBootCardDefinition
{
    FName Id;
    EBootCardKind Kind = EBootCardKind::Buff;
    int32 MaxStacks = 1;
    EBootCardEffect Effect = EBootCardEffect::AirCache;
    double EffectPerStack = 0.;
    double RewardPerStack = 0.;
    TArray<FName> Prerequisites;
    TArray<FName> Conflicts;
};
struct FBootCardEligibilityContext
{
    FBootRunModifiers Modifiers;
    /** Current projected envelope, including the already selected stacks. */
    FBootTraversalEnvelope Envelope;
    bool bCanPrepareProcessBudget = false;
    bool bCanPrepareCombinations = false;
    bool bCanPrepareThinCache = false;
    bool bCanPrepareRiskySurf = false;
    /** Scale of the catalog's validated narrowest future definition. */
    float MinimumWidthScale = .8f;
    TSet<FName> AvailablePrerequisites;
    TSet<FName> BlockedCardIds;
};
struct FBootCardOffer
{
    FBootRunToken Run;
    int64 OfferId = 0;
    double OpenedActiveSeconds = 0.;
    TArray<FName> CardIds;
};
struct FBootCardChoice
{
    FBootRunToken Run;
    int64 OfferId = 0;
    int32 Index = INDEX_NONE;
    uint64 PressSerial = 0;
    double PressActiveSeconds = 0.;
};
class BOOTLOOP_API FBootRunCardRules
{
public:
    static TConstArrayView<FBootCardDefinition> Catalog();
    static TArray<FName> Eligible(const FBootCardEligibilityContext&);
    static TOptional<FBootCardOffer> BuildOffer(const FBootCardEligibilityContext&, const FBootRunToken&, int64 OfferId, double ActiveSeconds, FRandomStream&);
    static bool Apply(FName CardId, const FBootCardEligibilityContext&, FBootRunModifiers& Out, FText& Error);
    static FBootTraversalEnvelope ApplyToEnvelope(const FBootTraversalEnvelope&, const FBootRunModifiers&);
};
