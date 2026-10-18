#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Movement/BootTraversalEnvelope.h"
#include "Run/BootRunTypes.h"
#include "BootAbilityComponent.generated.h"

struct FBootAbilitySnapshot
{
    int32 DashCharges = 0;
    int32 MaxDashCharges = 0;
    int32 RedirectCharges = 1;
    int32 MaxRedirectCharges = 1;
    int32 GuardCharges = 0;
    int32 MaxGuardCharges = 0;
    int32 GuardRechargeProgress = 0;
    int32 DashEventSerial = 0;
    int32 RedirectEventSerial = 0;
    int32 GuardEventSerial = 0;
    int32 GuardRechargeSerial = 0;
};

/** Guard owns process protection; dash and redirect remain in CharacterMovement. */
UCLASS(ClassGroup=(BOOTLOOP), meta=(BlueprintSpawnableComponent))
class BOOTLOOP_API UBootAbilityComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBootAbilityComponent();
    void ResetRun(const FBootRunToken& Token);
    void ApplyEnvelope(const FBootTraversalEnvelope& Envelope);
    bool TryAbsorbProcess(const FBootRunToken& Token, int64 ProcessId, double ActiveSeconds);
    /** Called by the process overlap/contact lifecycle, including retirement. */
    void NotifyProcessSeparated(int64 ProcessId);
    /** Accepted score-ledger operations only. Bounded monotone chunk/local dedup. */
    void NotifySurfCompleted(const FBootOperationKey& Operation);
    FBootAbilitySnapshot Snapshot() const;
    int32 GetProtectedContactCount() const { return ProtectedContacts.Num(); }
    int32 GetSurfHistoryChunkCount() const { return SurfLocalIds.Num(); }
    int32 GetSurfHistoryOperationCount() const
    {
        int32 Count = 0;
        for (const auto& Pair : SurfLocalIds) Count += Pair.Value.Num();
        return Count;
    }

private:
    FBootRunToken Run;
    int32 GuardCharges = 0;
    int32 MaxGuardCharges = 0;
    int32 RechargeProgress = 0;
    int32 GuardEventSerial = 0;
    int32 GuardRechargeSerial = 0;
    double ProtectionUntil = -1.;
    double LastContactSeconds = -1.;
    TSet<int64> ProtectedContacts;
    int64 RetiredSurfChunk = -1;
    TMap<int64, TSet<int32>> SurfLocalIds;
};
