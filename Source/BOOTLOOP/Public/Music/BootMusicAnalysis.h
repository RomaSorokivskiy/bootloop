#pragma once
#include "CoreMinimal.h"
#include "Music/BootMusicTypes.h"
#include "Music/BootMusicDecoder.h"
struct FBootAnalysisResult
{
 FBootTrackAnalysisPtr Analysis;
 FText Error;
 bool bCancelled = false;
 bool bNeedsReview = true;
};
class BOOTLOOP_API FBootMusicAnalyzer
{
public:
 static constexpr uint32 AnalysisVersion = 4;
 static FBootAnalysisResult Analyze(const FBootDecodeResult&, const TAtomic<bool>& Cancel, TFunctionRef<void(double)> Progress);
 static FString CacheKey(const FBootDecodeResult&);
 static bool Validate(const FBootTrackAnalysis&);
 static bool LoadCache(const FString& Path, const FBootDecodeResult&, FBootTrackAnalysis& Out);
 static bool SaveCache(const FString& Path, const FBootDecodeResult&, const FBootTrackAnalysis&);
};
class BOOTLOOP_API FBootMusicGridRules
{
public:
 static FBootTrackAnalysis WithOverride(const FBootTrackAnalysis&, double Bpm, double FirstBeatSeconds);
};
