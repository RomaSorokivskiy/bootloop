#pragma once
#include "World/Generation/BootChunkCatalog.h"
class BOOTLOOP_API FBootEndlessPlanner
{
public:
    static constexpr int32 MaxLive = 40;
    static constexpr int32 MaxAttempts = 16;
    void Reset(uint64 Seed, const FBootTraversalEnvelope& Envelope);
    void SetCatalog(TArray<FBootChunkDefinition> Definitions);
    FBootPlanResult PlanNext(const FBootPlanRequest& Request);
    bool CanPrepareEnvelope(const FBootPlanRequest& Request, const FBootTraversalEnvelope& Pending) const;
    bool CanPrepareBuild(const FBootPlanRequest&,const FBootRunModifiers&,const FBootTraversalEnvelope&) const;
    static bool HasUnrelatedOverlap(const FBootCommittedChunk& Candidate, const TArray<FBootCommittedChunk>& Live);
    static uint64 DescriptorHash(const FBootCommittedChunk& Chunk);
    static FString BoundsJson(const FBootCommittedChunk& Chunk);
    static float DifficultyBudget(double ActiveSeconds, int32 Tier);
    static double SelectionWeight(const FBootChunkDefinition&,const FBootMusicContext&,const FBootRunModifiers&);
    uint32 NextCosmeticRandom();
    uint32 NextCardRandom();
private:
    TArray<FBootChunkDefinition> Catalog;
    uint64 WorldState = 1, CardState = 2, CosmeticState = 3;
    FBootTraversalEnvelope InitialEnvelope;
    // Geometric escape frontier, captured from first connector; never a progress measurement.
    FVector GrowthAxis = FVector::ForwardVector;
    bool bHasGrowthAxis = false;
};


