#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Music/BootMusicTypes.h"
#include "BootMusicLibrarySubsystem.generated.h"

enum class EBootLibraryState : uint8 { Queued, Decoding, Analyzing, Ready, NeedsReview, Failed };
struct FBootLibraryEntry
{
 FString Hash, DisplayName, SourcePath, PcmPath, Error;
 EBootLibraryState State = EBootLibraryState::Queued;
 bool bIncluded = true, bBuiltIn = false, bHasGridOverride = false, bSafeGridAccepted = false;
 double DurationSeconds = 0., Progress = 0.;
 FBootBeatGrid Grid;
 uint64 Revision = 1;
 int64 Frames = 0;
 int32 Channels = 0;
 FBootTrackAnalysisPtr Analysis;
};
/** Immutable file/analysis snapshot; retaining this value pins owned PCM through removal. No audio callback I/O. */
struct FBootLibraryFileLifetime;
struct FBootPlaybackSource
{
 FString Hash, PcmPath;
 int64 Frames = 0;
 int32 Channels = 0;
 FBootTrackAnalysisPtr Analysis;
 TSharedPtr<FBootLibraryFileLifetime, ESPMode::ThreadSafe> Lifetime;
 TFunction<void()> Cleanup;
 ~FBootPlaybackSource() { if (Cleanup) Cleanup(); }
};
struct FBootPlaybackLease
{
 TSharedPtr<const FBootPlaybackSource, ESPMode::ThreadSafe> Source;
 bool IsValid() const { return Source.IsValid(); }
};
struct FBootLibraryJob;
UCLASS()
class BOOTLOOP_API UBootMusicLibrarySubsystem : public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase&) override;
 virtual void Deinitialize() override;
 uint64 ImportFiles(const TArray<FString>&);
 void CancelImport(uint64 JobId);
 bool SetIncluded(const FString& Hash, bool bIncluded);
 bool SetDisplayName(const FString& Hash, const FString& Name);
 bool Remove(const FString& Hash, FText& Error);
 bool SetGridOverride(const FString&, double Bpm, double FirstBeat, FText&);
 bool AcceptSafeGrid(const FString&, FText&);
 TArray<FBootLibraryEntry> Snapshot() const { return Entries; }
 FBootPlaybackLease Acquire(const FString& Hash, FText&);
 /** Explicit startup, never triggered by subsystem Initialize or legacy automation GI initialization. */
 void BootstrapStarterLibrary();
 /** Explicit gameplay repair; Initialize only loads metadata and never starts import workers. */
 void RepairLibrary();
 /** Explicit isolated fixture entry: accepts a descendant of Saved/V04Audit only. */
 bool InitializeFixture(const FString& IsolatedRoot);
 FString GetLibraryRoot() const { return Root; }
private:
 void StartNext();
 void Publish(uint64 Job, uint64 Generation, FBootLibraryEntry Entry);
 void Persist();
 FString Root;
 TArray<FBootLibraryEntry> Entries;
 TArray<TSharedPtr<FBootLibraryJob, ESPMode::ThreadSafe>> Jobs;
 TMap<FString, TWeakPtr<FBootPlaybackSource, ESPMode::ThreadSafe>> Sources;
 TMap<FString, TWeakPtr<FBootLibraryFileLifetime, ESPMode::ThreadSafe>> FileLifetimes;
 uint64 NextJob = 1, Generation = 1;
 bool bStopping = false, bBusy = false;
};
