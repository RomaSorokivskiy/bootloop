#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Progression/BootRunCards.h"
#include "BootRunProgressionComponent.generated.h"

/** Event-driven run-local build. It never pauses the world or reads SaveGame. */
UCLASS(ClassGroup=(BOOTLOOP))
class BOOTLOOP_API UBootRunProgressionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBootRunProgressionComponent();
    void ResetRun(const FBootRunToken&, uint64 Seed, const FBootTraversalEnvelope& Base);
    void EndRun();
    void SetEligibilityContext(const FBootCardEligibilityContext&);
    void MaybeOffer(double ActiveSeconds, double ProgressMetres, bool bRecoveryReady);
    bool TryChoose(const FBootCardChoice&, FText& Error);
    bool HasPendingMutation() const;
    /** Tempo proposals also preserve current commitments until this handshake. */
    bool PrepareBaseEnvelope(const FBootTraversalEnvelope&);
    bool TryCommit(const FBootSafeBoundary&);
    const TOptional<FBootCardOffer>& GetOffer() const { return Offer; }
    const FBootRunModifiers& GetActiveModifiers() const { return Active; }
    const FBootRunModifiers& GetSelectedModifiers() const { return Selected; }
    const FBootTraversalEnvelope& GetActiveEnvelope() const { return ActiveEnvelope; }
    const FBootTraversalEnvelope& GetProposedEnvelope() const { return ProposedEnvelope; }
    uint32 GetRewardRevision() const { return RewardRevision; }
    uint32 GetProposalRevision() const { return ProposalRevision; }
private:
    FBootCardEligibilityContext CurrentContext() const;
    FBootRunToken Run;
    FRandomStream CardRandom;
    FBootCardEligibilityContext Eligibility;
    FBootRunModifiers Active, Selected;
    FBootTraversalEnvelope BaseEnvelope, ActiveEnvelope, ProposedEnvelope;
    TOptional<FBootCardOffer> Offer;
    uint64 LastAcceptedPress = 0;
    int64 NextOfferId = 1;
    uint32 ProposalRevision = 0, RewardRevision = 0;
    double LastChoiceSeconds = 0., LastChoiceMetres = 0., ObservedSeconds = 0., ObservedMetres = 0.;
    bool bRunning = false, bHasChosen = false, bPending = false;
};
