#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Run/BootActiveRunClock.h"
#include "Run/BootRunScore.h"
#include "Progression/BootRunProgressionComponent.h"
#include "Player/BootAbilityComponent.h"
#include "Music/BootMusicTypes.h"
#include "BootRunCoordinator.generated.h"

struct FBootRunSnapshot
{
    FBootRunToken Run;
    bool bRunning = false, bPaused = false, bPendingMutation = false;
    int64 Score = 0, LastPoints = 0;
    int32 Combo = 0;
    EBootBeatRating LastRating = EBootBeatRating::OffBeat;
    double DistanceMetres = 0., ActiveSeconds = 0.;
    FBootRunModifiers ActiveModifiers, PendingModifiers, RewardModifiers;
    FBootTraversalEnvelope Envelope, PendingEnvelope;
    uint32 RewardRevision = 0;
    TOptional<FBootCardOffer> CardOffer;
    FBootAbilitySnapshot Abilities;
    FBootMusicContext Music;
};
/** Lifecycle/data owner. External track/transport/movement adapters consume snapshots. */
UCLASS()
class BOOTLOOP_API UBootRunCoordinator : public UObject
{
    GENERATED_BODY()
public:
    FBootRunToken BeginRun(uint64 Seed);
    FBootRunToken BeginRunAt(uint64 Seed, double Now);
    void SetPaused(bool, double Now);
    void Update(double Now, double CanonicalMetres, const FBootMusicContext&);
    void EndRun();
    void Reset();
    FBootRunSnapshot Snapshot() const;
    void SetRecoveryReady(bool bReady) { bRecoveryReady = bReady; }
    void SetCardEligibility(const FBootCardEligibilityContext&);
    bool TryChoose(const FBootCardChoice&, FText& Error);
    bool CommitPreparedRevision(const FBootSafeBoundary&);
    void RegisterChunk(int64, const TArray<FBootOperationKey>&);
    void RetireChunk(int64);
    bool AwardOperation(const FBootOperationKey&, EBootBeatRating);
    void SetAbilitySnapshot(const FBootAbilitySnapshot& Value) { Abilities = Value; }
    UBootRunProgressionComponent* GetProgression() const { return Progression; }
    int32 GetTrackedScoreOperationCount() const { return Score.GetTrackedOperationCount(); }
private:
    UPROPERTY() TObjectPtr<UBootRunProgressionComponent> Progression;
    FBootRunToken Run;
    FBootActiveRunClock Clock;
    FBootRunScore Score;
    FBootMusicContext Music;
    FBootAbilitySnapshot Abilities;
    double LastNow = 0., ActiveSeconds = 0.;
    double ProposedBpm = 130.;
    uint32 Generation = 0;
    bool bRunning = false, bPaused = false, bRecoveryReady = false;
};
