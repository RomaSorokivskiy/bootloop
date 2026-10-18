#include "Run/BootRunCoordinator.h"
#include "HAL/PlatformTime.h"

FBootRunToken UBootRunCoordinator::BeginRun(uint64 Seed) { return BeginRunAt(Seed, FPlatformTime::Seconds()); }
FBootRunToken UBootRunCoordinator::BeginRunAt(uint64 Seed, double Now)
{
    Reset();
    if (!FMath::IsFinite(Now)) return Run;
    ++Generation; if (Generation == 0) ++Generation;
    Run = {FGuid::NewGuid(), Generation}; Score.Reset(Run);
    Clock.Start(Now); LastNow = Now; ActiveSeconds = 0.; Music = {}; Abilities = {};
    ProposedBpm = 130.; bRunning = true; bPaused = bRecoveryReady = false;
    if (!Progression) Progression = NewObject<UBootRunProgressionComponent>(this);
    Progression->ResetRun(Run, Seed, FBootTraversalPolicy::FromTempo(130., 1));
    return Run;
}
void UBootRunCoordinator::SetPaused(bool bValue, double Now)
{
    if (!bRunning || !FMath::IsFinite(Now)) return;
    LastNow = FMath::Max(LastNow, Now); Clock.SetPaused(bValue, LastNow);
    ActiveSeconds = Clock.Seconds(LastNow); bPaused = bValue;
}
void UBootRunCoordinator::Update(double Now, double Metres, const FBootMusicContext& Context)
{
    if (!bRunning || !FMath::IsFinite(Now)) return;
    LastNow = FMath::Max(LastNow, Now); ActiveSeconds = Clock.Seconds(LastNow);
    if (bPaused) return;
    Music = Context;
    Score.AdvanceDistance(Metres, Progression->GetSelectedModifiers().DistanceMultiplier);
    if (FMath::IsFinite(Music.EffectiveBpm) && Music.EffectiveBpm > 0.
        && !FMath::IsNearlyEqual(ProposedBpm, Music.EffectiveBpm, .001))
    {
        if (Progression->PrepareBaseEnvelope(FBootTraversalPolicy::FromTempo(Music.EffectiveBpm, 0))) ProposedBpm = Music.EffectiveBpm;
    }
    Progression->MaybeOffer(ActiveSeconds, Score.GetDistance(), bRecoveryReady);
}
void UBootRunCoordinator::EndRun()
{
    if (Progression) Progression->EndRun();
    bRunning = bPaused = bRecoveryReady = false;
}
void UBootRunCoordinator::Reset()
{
    EndRun(); Run = {}; Score.Reset(Run); Clock = {}; Music = {}; Abilities = {};
    LastNow = ActiveSeconds = 0.; ProposedBpm = 130.;
}
FBootRunSnapshot UBootRunCoordinator::Snapshot() const
{
    FBootRunSnapshot S; S.Run = Run; S.bRunning = bRunning; S.bPaused = bPaused;
    S.Score = Score.GetScore(); S.DistanceMetres = Score.GetDistance(); S.ActiveSeconds = ActiveSeconds;
    S.Combo = Score.GetCombo(); S.LastPoints = Score.GetLastPoints(); S.LastRating = Score.GetLastRating();
    S.Music = Music; S.Abilities = Abilities;
    if (Progression)
    {
        S.ActiveModifiers = Progression->GetActiveModifiers(); S.RewardModifiers = Progression->GetSelectedModifiers();
        S.bPendingMutation = Progression->HasPendingMutation(); S.RewardRevision = Progression->GetRewardRevision();
        S.Envelope = Progression->GetActiveEnvelope(); S.PendingEnvelope = Progression->GetProposedEnvelope();
        if (S.bPendingMutation) S.PendingModifiers = Progression->GetSelectedModifiers();
        S.CardOffer = Progression->GetOffer();
    }
    return S;
}
void UBootRunCoordinator::SetCardEligibility(const FBootCardEligibilityContext& C)
{
    if (Progression) Progression->SetEligibilityContext(C);
}
bool UBootRunCoordinator::TryChoose(const FBootCardChoice& Choice, FText& Error)
{
    if (!bRunning || bPaused || !Progression) { Error = FText::FromString(TEXT("Run cannot accept a card now.")); return false; }
    return Progression->TryChoose(Choice, Error);
}
bool UBootRunCoordinator::CommitPreparedRevision(const FBootSafeBoundary& Boundary)
{
    return bRunning && !bPaused && Progression && Progression->TryCommit(Boundary);
}
void UBootRunCoordinator::RegisterChunk(int64 Id, const TArray<FBootOperationKey>& Keys)
{
    if (bRunning) Score.RegisterChunk(Id, Keys);
}
void UBootRunCoordinator::RetireChunk(int64 Id) { Score.RetireChunk(Id); }
bool UBootRunCoordinator::AwardOperation(const FBootOperationKey& Key, EBootBeatRating Rating)
{
    return bRunning && !bPaused && Progression && Score.AwardOperation(Key, Rating, Progression->GetSelectedModifiers().OperationMultiplier);
}
