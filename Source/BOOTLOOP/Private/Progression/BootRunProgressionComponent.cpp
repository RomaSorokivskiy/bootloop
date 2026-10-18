#include "Progression/BootRunProgressionComponent.h"
UBootRunProgressionComponent::UBootRunProgressionComponent() { PrimaryComponentTick.bCanEverTick = false; }
void UBootRunProgressionComponent::ResetRun(const FBootRunToken& Token, uint64 Seed, const FBootTraversalEnvelope& Base)
{
    Run = Token;
    CardRandom.Initialize(int32(uint32(Seed) ^ uint32(Seed >> 32) ^ 0x43415244u));
    Eligibility = {}; Active = {}; Selected = {}; Offer.Reset();
    BaseEnvelope = ActiveEnvelope = ProposedEnvelope = Base;
    ProposalRevision = Base.Revision; RewardRevision = 0;
    LastAcceptedPress = 0; NextOfferId = 1;
    LastChoiceSeconds = LastChoiceMetres = ObservedSeconds = ObservedMetres = 0.;
    bHasChosen = bPending = false;
    FText Error; bRunning = Run.RunId.IsValid() && FBootTraversalPolicy::Validate(Base, Error);
}
void UBootRunProgressionComponent::EndRun()
{
    bRunning = false; bPending = false; Offer.Reset(); Active = {}; Selected = {};
}
void UBootRunProgressionComponent::SetEligibilityContext(const FBootCardEligibilityContext& Context) { Eligibility = Context; }
FBootCardEligibilityContext UBootRunProgressionComponent::CurrentContext() const
{
    auto C = Eligibility; C.Modifiers = Selected; C.Envelope = ProposedEnvelope; return C;
}
void UBootRunProgressionComponent::MaybeOffer(double Seconds, double Metres, bool bRecoveryReady)
{
    if (!bRunning || !FMath::IsFinite(Seconds) || !FMath::IsFinite(Metres) || Seconds < 0. || Metres < 0.) return;
    ObservedSeconds = FMath::Max(ObservedSeconds, Seconds); ObservedMetres = FMath::Max(ObservedMetres, Metres);
    if (Offer.IsSet() || bPending || !bRecoveryReady || NextOfferId == MAX_int64) return;
    const double RequiredTime = bHasChosen ? LastChoiceSeconds + 45. : 25.;
    const double RequiredMetres = bHasChosen ? LastChoiceMetres + 200. : 100.;
    if (ObservedSeconds < RequiredTime || ObservedMetres < RequiredMetres) return;
    Offer = FBootRunCardRules::BuildOffer(CurrentContext(), Run, NextOfferId, ObservedSeconds, CardRandom);
    if (Offer.IsSet()) ++NextOfferId;
}
bool UBootRunProgressionComponent::TryChoose(const FBootCardChoice& Choice, FText& Error)
{
    auto Reject = [&Error](const TCHAR* Why) { Error = FText::FromString(Why); return false; };
    if (!bRunning || !Offer.IsSet() || Choice.Run != Run || Choice.OfferId != Offer->OfferId
        || !Offer->CardIds.IsValidIndex(Choice.Index)) return Reject(TEXT("Card offer is stale or invalid."));
    if (Choice.PressSerial == 0 || Choice.PressSerial <= LastAcceptedPress || !FMath::IsFinite(Choice.PressActiveSeconds)
        || Choice.PressActiveSeconds < Offer->OpenedActiveSeconds || Choice.PressActiveSeconds > ObservedSeconds + .25)
        return Reject(TEXT("Card selection requires a fresh press after this offer opened."));
    if (ProposalRevision == MAX_uint32 || RewardRevision == MAX_uint32) return Reject(TEXT("Run revision budget exhausted."));
    FBootRunModifiers Result;
    if (!FBootRunCardRules::Apply(Offer->CardIds[Choice.Index], CurrentContext(), Result, Error)) return false;
    const auto Envelope = FBootRunCardRules::ApplyToEnvelope(BaseEnvelope, Result);
    if (!Envelope.bValid) return Reject(TEXT("Card cannot prepare a legal traversal envelope."));
    Selected = MoveTemp(Result); ProposedEnvelope = Envelope;
    ProposedEnvelope.Revision = ++ProposalRevision; ++RewardRevision;
    LastAcceptedPress = Choice.PressSerial;
    LastChoiceSeconds = ObservedSeconds; LastChoiceMetres = ObservedMetres;
    bHasChosen = bPending = true; Offer.Reset(); Error = FText::GetEmpty(); return true;
}
bool UBootRunProgressionComponent::HasPendingMutation() const { return bPending; }
bool UBootRunProgressionComponent::PrepareBaseEnvelope(const FBootTraversalEnvelope& Base)
{
    FText Error;
    if (!bRunning || ProposalRevision == MAX_uint32 || !FBootTraversalPolicy::Validate(Base, Error)) return false;
    const auto Candidate = FBootRunCardRules::ApplyToEnvelope(Base, Selected);
    if (!Candidate.bValid) return false;
    BaseEnvelope = Base; ProposedEnvelope = Candidate;
    ProposedEnvelope.Revision = ++ProposalRevision; bPending = true; return true;
}
bool UBootRunProgressionComponent::TryCommit(const FBootSafeBoundary& Boundary)
{
    if (!bRunning || !bPending || Boundary.Run != Run || Boundary.ChunkId <= 0 || Boundary.PreparedRevision != ProposalRevision
        || !Boundary.bSupported || !Boundary.bNoActiveCommitment || !Boundary.bFutureReady) return false;
    Active = Selected; ActiveEnvelope = ProposedEnvelope; bPending = false; return true;
}
