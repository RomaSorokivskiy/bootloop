#pragma once
#include "CoreMinimal.h"
#include "Run/BootRunTypes.h"

struct FBootBeatGrid { double Bpm = 130.; double FirstBeatSeconds = 0.; };
enum class EBootGridSource : uint8 { Estimated, Authored, Manual, Safe };
struct FBootTrackAnalysis
{
    static constexpr int32 MaxFeatureSamples = 12000;
    static constexpr int32 MaxSections = 1024;
    static constexpr int32 MaxBpmCandidates = 16;
    uint32 Version = 1;
    FString TrackHash;
    double DurationSeconds = 0.;
    int64 CanonicalFrames = 0;
    TArray<double> BpmCandidates;
    FBootBeatGrid Grid;
    float Confidence = 0.f;
    EBootGridSource Source = EBootGridSource::Estimated;
    double FeatureStepSeconds = .05;
    TArray<float> Energy;
    TArray<float> OnsetDensity;
    TArray<double> SectionStartSeconds;
    bool HasBoundedFeatures() const
    {
        return Energy.Num() <= MaxFeatureSamples && OnsetDensity.Num() <= MaxFeatureSamples
            && SectionStartSeconds.Num() <= MaxSections && BpmCandidates.Num() <= MaxBpmCandidates;
    }
};
/** Bounded playback block or test fixture; streaming owners enforce memory budgets. */
struct FBootCanonicalPcm
{
    static constexpr int32 SampleRate = 48000;
    int32 Channels = 2;
    TArray<int16> Samples;
    int64 Frames() const { return Channels > 0 ? Samples.Num() / Channels : 0; }
};
using FBootTrackAnalysisPtr = TSharedPtr<const FBootTrackAnalysis, ESPMode::ThreadSafe>;
using FBootCanonicalPcmPtr = TSharedPtr<const FBootCanonicalPcm, ESPMode::ThreadSafe>;
struct FBootMusicContext
{
    int64 TrackEpoch = 0;
    FString TrackHash;
    double LocalSeconds = 0.;
    FBootBeatGrid Grid;
    double EffectiveBpm = 130.;
    float Energy = 0.f, OnsetDensity = 0.f;
    int32 Section = 0;
    bool bSafeGrid = true;
};
struct FBootTransportSnapshot : FBootMusicContext
{
    FBootRunToken Run;
    int64 RenderedSourceFrame = 0;
    bool bPaused = false, bUnderflow = false, bValid = false;
};
using FBootTransportSnapshotPtr = TSharedPtr<const FBootTransportSnapshot, ESPMode::ThreadSafe>;

/** Render counters and bounded transport resources; read from the game thread for native audits. */
struct FBootTransportDiagnostics
{
    int64 RenderBlocks = 0;
    int64 RenderedOutputFrames = 0;
    int64 UnderflowBlocks = 0;
    int64 RingCapacityFrames = 0;
    int64 TotalRingBytes = 0;
    int32 DeviceSampleRate = 0;
    int32 LeaseSlots = 0;
    double EstimatedDeviceLatencySeconds = 0.;
    bool bLatencyMeasured = false;
};

