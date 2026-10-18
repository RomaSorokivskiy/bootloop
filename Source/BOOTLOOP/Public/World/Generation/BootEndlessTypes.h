#pragma once
#include "CoreMinimal.h"
#include "Run/BootRunTypes.h"
#include "Movement/BootTraversalEnvelope.h"
#include "Music/BootMusicTypes.h"
#include "Progression/BootRunModifiers.h"

enum class EBootChunkFamily : uint8 { Recovery, GapTurn, Slide, Wall, Surf, Process };
enum class EBootPlanFailure : uint8 { None, InvalidRequest, LiveBudget, NoLegalContinuation, ReplayMismatch };
enum class EBootRecipeRole : uint8 { Floor, Wall, SlideCeiling, Decoration };
enum class EBootBaselineMove : uint8 { Run, Jump, Slide, WallRun, Surf };
struct FBootBaselineMoveRecipe
{
    EBootBaselineMove Kind = EBootBaselineMove::Run;
    FVector Start = FVector::ZeroVector, End = FVector::ZeroVector;
};
enum class EBootVolumeKind : uint8 { Occupied, BodyClearance, Rail, Hazard };
struct FBootOrientedVolume
{
    FTransform Frame = FTransform::Identity;
    FVector HalfSize = FVector(1.);
    EBootVolumeKind Kind = EBootVolumeKind::Occupied;
};
struct FBootRecipeBox
{
    FVector Center = FVector::ZeroVector;
    FVector HalfSize = FVector(1.);
    EBootRecipeRole Role = EBootRecipeRole::Floor;
};
struct FBootRailRecipe
{
    // Start/End are top-surface centres. SpeedCmps is PEAK smoothstep speed;
    // native ABootSurfWindow duration = 1.5 * distance / SpeedCmps.
    FVector Start = FVector::ZeroVector, End = FVector::ZeroVector;
    FVector PlatformHalfSize = FVector(200., 150., 20.);
    float SpeedCmps = 200.f;
    int32 OperationId = 0;
    bool bFlowRide = false;
};
struct FBootHazardRecipe
{
    FBox Sweep = FBox(ForceInit);
    double TelegraphSeconds = 1.2;
    int32 OperationId = 0;
    bool bPopup = false;
};
/** Local cm, +X connector forward, floor surface z=0. Optional lane always rejoins Exit. */
struct FBootChunkRecipe
{
    TArray<FBootRecipeBox> Boxes;
    TArray<FVector> Baseline;
    TArray<FVector> OptionalBranch;
    TArray<FBootBaselineMoveRecipe> BaselineMoves;
    TArray<FBootRailRecipe> Rails;
    TArray<FBootHazardRecipe> Hazards;
    uint32 Variant = 0;
};
struct FBootChunkDefinition
{
    FName Id;
    uint32 Version = 1;
    EBootChunkFamily Family = EBootChunkFamily::Recovery;
    FTransform Entry = FTransform::Identity, Exit = FTransform::Identity;
    TArray<FBox> Occupied, BodyClearance, RailSweeps, HazardSweeps;
    float MinSpeedCmps = 0.f, MaxSpeedCmps = 3600.f, MinWidthScale = .75f;
    uint32 RequiredAbilityMask = 0;
    bool bSafeContinuation = false;
    float DifficultyCost = 0.f;
    int32 CombinationCost = 0;
    int32 ProcessCost = 0;
    uint32 MusicalTags = 0;
    FBootChunkRecipe Recipe;
};
struct FBootCommittedChunk
{
    FBootRunToken Run;
    int64 InstanceId = 0;
    FName DefinitionId;
    uint32 DefinitionVersion = 0;
    FTransform WorldTransform = FTransform::Identity, WorldEntry = FTransform::Identity, WorldExit = FTransform::Identity;
    double CanonicalStartMetres = 0., CanonicalEndMetres = 0.;
    FBootMusicContext MusicContext;
    uint32 EnvelopeRevision = 0;
    TArray<FBootOperationKey> OperationKeys;
    int64 BranchId = 0, MergeId = 0;
    double BranchMergeMetres = 0.;
    FBootChunkRecipe Recipe;
    TArray<FBox> WorldOccupied, WorldBodyClearance, WorldRailSweeps, WorldHazardSweeps;
    TArray<FBootOrientedVolume> WorldVolumes;
    FBootOrientedVolume ReservedVolume;
    FBox ReservedContinuation = FBox(ForceInit);
};
struct FBootReplayDecision
{
    FName DefinitionId;
    uint32 DefinitionVersion = 1;
    FBootMusicContext MusicContext;
    uint32 Variant = 0;
    bool bOptionalBranch = false;
};
struct FBootPlanRequest
{
    FBootRunToken Run;
    int64 NextInstanceId = 1;
    FTransform Connector = FTransform::Identity;
    TArray<FBootCommittedChunk> LiveDescriptors;
    FBootTraversalEnvelope CurrentEnvelope;
    TOptional<FBootTraversalEnvelope> PendingEnvelope;
    double ActiveSeconds = 0., CanonicalStartMetres = 0.;
    int32 Tier = 0;
    FBootRunModifiers Modifiers;
    FBootMusicContext MusicContext;
    TOptional<FBootReplayDecision> ReplayDecision;
};
struct FBootPlanResult
{
    TOptional<FBootCommittedChunk> Committed;
    EBootPlanFailure Failure = EBootPlanFailure::None;
    int32 AttemptCount = 0;
    bool bUsedFallback = false;
    FString Diagnostics;
    FBox ReservedContinuation = FBox(ForceInit);
};
struct FBootGateCrossing
{
    int64 FromInstanceId = 0, ToInstanceId = 0, Sequence = 0;
    double CanonicalMetres = 0.;
};
struct FBootRunReplay
{
    uint32 RulesVersion = 1, CatalogVersion = 1, AnalysisVersion = 1, RngVersion = 1;
    uint64 Seed = 0;
    TArray<FString> TrackHashOrder;
    TArray<FBootReplayDecision> Decisions;
    TArray<FName> CardDecisions;
    TArray<int64> BranchDecisions;
};
struct BOOTLOOP_API FBootRouteCursor
{
    int64 CurrentInstanceId = 0, Sequence = 0;
    double CanonicalMetres = 0.;
    void Reset(int64 FirstInstanceId = 1);
    TArray<FBootGateCrossing> Advance(FVector Previous, FVector Current, const TArray<FBootCommittedChunk>& Live);
};

