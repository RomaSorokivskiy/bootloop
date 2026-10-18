#pragma once
#include "Run/BootRunTypes.h"
#include "Music/BootRhythmScoring.h"

/** Only registered live operations can award points. No movement action API exists. */
class BOOTLOOP_API FBootRunScore
{
public:
    void Reset(FBootRunToken);
    void AdvanceDistance(double NewCanonicalMetres,double FutureDistanceMultiplier);
    bool AwardOperation(const FBootOperationKey&,EBootBeatRating,double FutureOperationMultiplier);
    void RegisterChunk(int64,const TArray<FBootOperationKey>&);
    void RetireChunk(int64);
    int64 GetScore() const { return Score; }
    double GetDistance() const { return Distance; }
    int32 GetCombo() const { return Combo; }
    int64 GetLastPoints() const { return LastPoints; }
    EBootBeatRating GetLastRating() const { return LastRating; }
    int32 GetTrackedOperationCount() const { return LiveOperations.Num(); }
private:
    FBootRunToken Run;
    TMap<FBootOperationKey,bool> LiveOperations;
    int64 Score=0,LastPoints=0,RetiredWatermark=-1;
    int32 Combo=0;
    double Distance=0.,DistancePoints=0.;
    EBootBeatRating LastRating=EBootBeatRating::OffBeat;
};
