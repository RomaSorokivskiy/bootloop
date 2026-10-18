#pragma once
#include "CoreMinimal.h"
#include "World/Generation/BootTrack.h"
#include "World/Generation/BootEndlessPlanner.h"
#include "Music/BootRhythmScoring.h"
#include "World/Threats/BootScanProcess.h"
#include "BootEndlessTrack.generated.h"
class ABootEndlessChunk;
enum class EBootChunkPin : uint8 { Support=1,ActiveRide=2,ReservedLanding=4,PendingDestination=8 };
DECLARE_MULTICAST_DELEGATE_OneParam(FBootChunkCommittedEvent,const FBootCommittedChunk&);
DECLARE_MULTICAST_DELEGATE_OneParam(FBootChunkRetiredEvent,int64);
DECLARE_MULTICAST_DELEGATE_TwoParams(FBootRunOperationEvent,const FBootOperationKey&,EBootBeatRating);

UCLASS()
class BOOTLOOP_API ABootEndlessTrack : public ABootTrack
{
    GENERATED_BODY()
    friend struct FBootTask14ReviewAccess;
public:
    virtual void ResetTrack(int32 Seed) override;
    virtual void Advance(double) override {} // Legacy world-X generation never runs in this mode.
    virtual void UpdateCourse(ABootRunner*,float) override;
    virtual bool IsFatal(FVector,float,float) const override;
    virtual bool IsFinished(FVector) const override { return false; }
    virtual float GetRouteProgress(FVector) const override { return float(GetProgressMetres()); }
    virtual bool CanCompleteSurf(const ABootSurfWindow*,const ABootRunner*,int32) const override { return false; }
    void BeginMusicRun(const FBootRunToken&,uint64 Seed,const FBootTraversalEnvelope&);
    double GetProgressMetres() const { return Cursor.CanonicalMetres; }
    void SetDirectorContext(const FBootMusicContext&,double ActiveSeconds,const FBootRunModifiers&);
    bool PrepareRevision(const FBootTraversalEnvelope&,FText&);
    bool CanPrepareBuild(const FBootRunModifiers&,const FBootTraversalEnvelope&) const;
    bool IsRecoveryReady(const ABootRunner* Runner) const;
    bool PeekRevisionBoundary(FBootSafeBoundary&) const;
    bool CommitPreparedRevision(const FBootSafeBoundary&);
    bool TryCommitRevision(FBootSafeBoundary&);
    void SetProcessesActive(bool);
    bool CompleteRunOperation(ABootSurfWindow*,ABootRunner*,EBootBeatRating);
    bool PinChunk(int64,EBootChunkPin);
    void UnpinChunk(int64,EBootChunkPin);
    bool IsChunkPinned(int64) const;
    int32 GetLiveChunkCount() const { return LiveChunks.Num(); }
    double GetPreparedHorizonCm() const;
    double GetRequiredHorizonCm() const;
    const FBootTraversalEnvelope& GetActiveEnvelope() const { return Envelope; }
    const FBootRunToken& GetRunToken() const { return Run; }
    const TArray<TObjectPtr<ABootEndlessChunk>>& GetLiveChunks() const { return LiveChunks; }
    FBootChunkCommittedEvent OnChunkCommitted;
    FBootChunkRetiredEvent OnChunkRetired;
    FBootRunOperationEvent OnRunOperation;
    FBootRunThreatContact OnRunThreatContact;
protected:
    virtual void EndPlay(const EEndPlayReason::Type) override;
private:
    void FillHorizon();
    void RetireReleased();
    FBootPlanRequest MakeRequest() const;
    FBootTraversalEnvelope GetConservativeEnvelope(bool IncludePending=true) const;
    FBootRunToken Run;
    FBootTraversalEnvelope Envelope;
    TOptional<FBootTraversalEnvelope> PendingEnvelope;
    FBootMusicContext Music;
    FBootRunModifiers Modifiers;
    FBootEndlessPlanner Planner;
    FBootRouteCursor Cursor;
    FTransform NextConnector=FTransform::Identity;
    double PlannedMetres=0.,ActiveSeconds=0.;
    int64 NextId=1;
    FVector PreviousPosition=FVector::ZeroVector;
    bool bObservedPosition=false,bProcessesActive=true;
    TMap<int64,uint8> ExternalPins;
    UPROPERTY() TArray<TObjectPtr<ABootEndlessChunk>> LiveChunks;
    UPROPERTY() TWeakObjectPtr<ABootRunner> LastRunner;
};



