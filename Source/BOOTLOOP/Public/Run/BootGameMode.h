#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Run/BootRunCoordinator.h"
#include "Tutorial/BootFirstRunTutorial.h"
#include "BootGameMode.generated.h"

class ABootTrack;
class ABootRunner;
class ACharacter;
class UBootRunCoordinator;
class UBootMusicTransportComponent;
struct FBootRunSnapshot;
enum class EBootTutorialEvent : uint8;
enum class EBootRunState : uint8 { Desktop, Booting, Running, Paused, Dead, Complete };

UCLASS()
class BOOTLOOP_API ABootGameMode : public AGameModeBase
{
    GENERATED_BODY()
    friend struct FBootMusicLifecycleTestAccess;
    friend struct FBootTask14ReviewAccess;
public:
    ABootGameMode();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void StartRun();
    void PauseRun(bool bPause);
    void ReturnToDesktop();
    void HandleDeath();
    void HandleFinish();
    EBootRunState GetRunState() const { return State; }
    int32 GetTotalScore() const;
    float GetDistance() const;
    float GetRunSeconds() const;
    int32 GetSeed() const { return Seed; }
    ABootTrack* GetTrack() const { return Track; }
    bool IsTraining() const { return bTrainingCourse; }
    FName GetCourseId() const;
    int32 GetAttemptGeneration() const { return AttemptGeneration; }
    void NotifyObjective(EBootTutorialEvent Event);
    void SkipLesson();
    FString GetObjective() const;
    void HandleThreat(ACharacter* Target,int32 Generation);
    bool IsMusicRun() const { return bMusicRun; }
    bool IsFirstRun() const { return bFirstRun; }
    UBootRunCoordinator* GetMusicRun() const { return MusicRun; }
    int32 GetRegisteredProcessCount() const { return ProcessIds.Num(); }
    UBootMusicTransportComponent* GetMusicTransport() const;
    FBootRunSnapshot GetRunSnapshot() const;
    void ChooseRunCard(int32 Index);
    FBootTutorialEvidence CaptureFirstRunEvidence(EBootFirstRunEvent Event);
    void SubmitFirstRunEvidence(const FBootTutorialEvidence& Evidence);
protected:
    bool bAuthoredCourse=false, bTrainingCourse=false;
    bool bMusicRun=false,bFirstRun=false;
    virtual ABootTrack* CreateTrack();
    virtual bool PrepareRun(FText& Error);
private:
    void UpdateRun();
    void FinishBoot();
    void SaveResult();
    UPROPERTY() TObjectPtr<ABootTrack> Track;
    UPROPERTY() TObjectPtr<UBootRunCoordinator> MusicRun;
    EBootRunState State = EBootRunState::Desktop;
    FTimerHandle RunTimer, BootTimer;
    float MaxDistance = 0, RunSeconds = 0;
    int32 Seed = 1301995;
    bool bResultSaved = false;
    int32 AttemptGeneration=1;
    void RestoreCheckpoint();
    void SetProcessesActive(bool bActive);
    void BindMusicTrack(const FBootRunToken& Token);
    void UpdateMusicRun(ABootRunner* Runner,double Now);
    void RefreshCardEligibility();
    void RefreshFirstRunLesson();
    void ResetTrialLoadout(ABootRunner* Runner);
    void StopV04Run(bool bReset);
    FBootRunToken FirstRunToken;
    FBootActiveRunClock FirstRunClock;
    FBootTraversalEnvelope TrialEnvelope,TrialPendingEnvelope;
    FBootRunModifiers TrialModifiers,TrialPendingModifiers;
    TOptional<FBootCardOffer> TrialOffer;
    int32 PreparedLesson=INDEX_NONE;
    uint64 ActionSerial=uint64(1)<<32,CardPressSerial=0;
    double LastUpdateSeconds=0.;
    double NextEligibilitySeconds=0.;
    // Catalog contract: at most 40 live chunks, each with at most eight actual processes.
    TMap<FBootOperationKey,int64> ProcessIds;
    int64 NextProcessId=1;
};

UCLASS()
class BOOTLOOP_API ABootMusicRunGameMode : public ABootGameMode
{
    GENERATED_BODY()
public: ABootMusicRunGameMode() { bMusicRun=true; }
};
UCLASS()
class BOOTLOOP_API ABootFirstRunGameMode : public ABootGameMode
{
    GENERATED_BODY()
public: ABootFirstRunGameMode() { bFirstRun=true;bTrainingCourse=true; }
};

UCLASS()
class BOOTLOOP_API ABootFirstLoginGameMode : public ABootGameMode
{
    GENERATED_BODY()
public: ABootFirstLoginGameMode() { bAuthoredCourse=true; bTrainingCourse=true; }
};
UCLASS()
class BOOTLOOP_API ABootCircuitGameMode : public ABootGameMode
{
    GENERATED_BODY()
public: ABootCircuitGameMode() { bAuthoredCourse=true; }
};
