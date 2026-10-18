#include "Player/BootAbilityComponent.h"
#include "Movement/BootMovementComponent.h"
#include "GameFramework/Actor.h"

namespace
{
    constexpr int32 MaximumContactCount = 32;
    constexpr int32 MaximumSurfChunks = 40;
    constexpr int32 MaximumLocalOperations = 32;
    constexpr double ProtectionSeconds = .25;
    int32 NextSerial(int32 Serial) { return Serial == MAX_int32 ? 1 : Serial + 1; }
}

UBootAbilityComponent::UBootAbilityComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}
void UBootAbilityComponent::ResetRun(const FBootRunToken& Token)
{
    Run = Token;
    GuardCharges = MaxGuardCharges = RechargeProgress = 0;
    ProtectionUntil = LastContactSeconds = -1.;
    ProtectedContacts.Reset();
    SurfLocalIds.Reset();
    RetiredSurfChunk = -1;
}

void UBootAbilityComponent::ApplyEnvelope(const FBootTraversalEnvelope& Envelope)
{
    FText Error;
    if (!FBootTraversalPolicy::Validate(Envelope, Error)) return;
    const int32 NewMaximum = FMath::Clamp(Envelope.GuardCharges, 0, 2);
    // Unlocking a new slot grants that slot once. Republishing the same
    // revision, tempo or stack never refills a previously spent charge.
    GuardCharges = FMath::Clamp(GuardCharges + FMath::Max(0, NewMaximum - MaxGuardCharges), 0, NewMaximum);
    MaxGuardCharges = NewMaximum;
    if (GuardCharges == MaxGuardCharges) RechargeProgress = 0;
}

bool UBootAbilityComponent::TryAbsorbProcess(const FBootRunToken& Token, int64 ProcessId, double ActiveSeconds)
{
    if (!Run.RunId.IsValid() || Token != Run || ProcessId < 0
        || !FMath::IsFinite(ActiveSeconds) || ActiveSeconds < 0.
        || ActiveSeconds < LastContactSeconds) return false;
    LastContactSeconds = ActiveSeconds;
    if (ProtectedContacts.Contains(ProcessId)) return true;
    if (ProtectedContacts.Num() >= MaximumContactCount) return false;
    if (ActiveSeconds < ProtectionUntil)
    {
        ProtectedContacts.Add(ProcessId);
        return true;
    }
    if (GuardCharges <= 0) return false;
    --GuardCharges;
    ProtectedContacts.Add(ProcessId);
    ProtectionUntil = ActiveSeconds + ProtectionSeconds;
    GuardEventSerial = NextSerial(GuardEventSerial);
    return true;
}

void UBootAbilityComponent::NotifyProcessSeparated(int64 ProcessId)
{
    ProtectedContacts.Remove(ProcessId);
}

void UBootAbilityComponent::NotifySurfCompleted(const FBootOperationKey& Operation)
{
    if (Operation.Run != Run || !Run.RunId.IsValid() || Operation.ChunkId < 0
        || Operation.ChunkId <= RetiredSurfChunk || Operation.LocalId < 0) return;
    auto* KnownLocals = SurfLocalIds.Find(Operation.ChunkId);
    if (KnownLocals && (KnownLocals->Contains(Operation.LocalId)
        || KnownLocals->Num() >= MaximumLocalOperations)) return;
    if (!KnownLocals && SurfLocalIds.Num() >= MaximumSurfChunks)
    {
        int64 Oldest = MAX_int64;
        for (const auto& Pair : SurfLocalIds) Oldest = FMath::Min(Oldest, Pair.Key);
        // A delayed operation from an older-than-live chunk cannot evict a
        // current record and then manufacture recharge from retired callbacks.
        if (Operation.ChunkId <= Oldest) return;
        SurfLocalIds.Remove(Oldest);
        RetiredSurfChunk = FMath::Max(RetiredSurfChunk, Oldest);
    }
    SurfLocalIds.FindOrAdd(Operation.ChunkId).Add(Operation.LocalId);
    if (GuardCharges >= MaxGuardCharges || MaxGuardCharges <= 0)
    {
        RechargeProgress = 0;
        return;
    }
    ++RechargeProgress;
    if (RechargeProgress == 3)
    {
        GuardCharges = FMath::Min(MaxGuardCharges, GuardCharges + 1);
        RechargeProgress = 0;
        GuardRechargeSerial = NextSerial(GuardRechargeSerial);
    }
}

FBootAbilitySnapshot UBootAbilityComponent::Snapshot() const
{
    FBootAbilitySnapshot Result;
    if (const AActor* Owner = GetOwner())
        if (const auto* Movement = Owner->FindComponentByClass<UBootMovementComponent>())
        {
            Result.DashCharges = Movement->GetDashCharges();
            Result.MaxDashCharges = Movement->GetMaxDashCharges();
            Result.RedirectCharges = Movement->GetAirRedirectCharges();
            Result.MaxRedirectCharges = Movement->GetMaxAirRedirectCharges();
            Result.DashEventSerial = Movement->GetDashSerial();
            Result.RedirectEventSerial = Movement->GetAirRedirectSerial();
        }
    Result.GuardCharges = GuardCharges;
    Result.MaxGuardCharges = MaxGuardCharges;
    Result.GuardRechargeProgress = RechargeProgress;
    Result.GuardEventSerial = GuardEventSerial;
    Result.GuardRechargeSerial = GuardRechargeSerial;
    return Result;
}
