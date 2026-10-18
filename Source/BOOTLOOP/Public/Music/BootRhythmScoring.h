#pragma once
#include "CoreMinimal.h"

enum class EBootRhythmAction : uint8 { Jump, Slide };
enum class EBootBeatRating : uint8 { OffBeat, Good, Perfect };

// Pure scoring rules. Movement never consumes or depends on their return value.
struct FBootRhythmScore
{
    static constexpr double BeatSeconds = 60.0 / 130.0;
    int32 Score = 0;
    int32 Combo = 0;
    int32 LastPoints = 0;
    EBootBeatRating LastRating = EBootBeatRating::OffBeat;
    int64 LastRewardedBeat[2] = {-1, -1};
    TSet<int32> CompletedWindows;

    bool AwardSurf(int32 WindowId, EBootBeatRating InputRating)
    {
        if (WindowId < 0 || CompletedWindows.Contains(WindowId)) return false;
        CompletedWindows.Add(WindowId);
        LastRating = InputRating;
        if (InputRating == EBootBeatRating::OffBeat) Combo = 0;
        const int32 Base = InputRating == EBootBeatRating::Perfect ? 300 : InputRating == EBootBeatRating::Good ? 150 : 75;
        LastPoints = Base * (10 + FMath::Min(Combo, 20)) / 10;
        Score += LastPoints;
        if (InputRating != EBootBeatRating::OffBeat) ++Combo;
        return true;
    }

    static EBootBeatRating Rate(double TransportSeconds, double OffsetMs = 0)
    {
        if (!FMath::IsFinite(TransportSeconds) || !FMath::IsFinite(OffsetMs) || TransportSeconds < 0)
            return EBootBeatRating::OffBeat;
        const double Time = TransportSeconds - OffsetMs / 1000.;
        const double Beat = FMath::RoundToDouble(Time / BeatSeconds);
        if (Beat < 0) return EBootBeatRating::OffBeat;
        const double Delta = FMath::Abs(Time - Beat * BeatSeconds);
        if (Delta <= .080 + 1.e-9) return EBootBeatRating::Perfect;
        if (Delta <= .150 + 1.e-9) return EBootBeatRating::Good;
        return EBootBeatRating::OffBeat;
    }

    bool Award(EBootRhythmAction Action, double TransportSeconds, double OffsetMs = 0)
    {
        LastPoints = 0;
        if (!FMath::IsFinite(TransportSeconds) || !FMath::IsFinite(OffsetMs) || TransportSeconds < 0)
            return false;
        LastRating = Rate(TransportSeconds, OffsetMs);
        if (LastRating == EBootBeatRating::OffBeat)
        {
            Combo = 0;
            return false;
        }
        const int32 Index = static_cast<int32>(Action);
        if (Index < 0 || Index >= 2) return false;
        const int64 Beat = FMath::RoundToInt64((TransportSeconds - OffsetMs / 1000.) / BeatSeconds);
        if (Beat <= LastRewardedBeat[Index]) return false;
        LastRewardedBeat[Index] = Beat;
        const int32 Base = LastRating == EBootBeatRating::Perfect ? 100 : 40;
        LastPoints = Base * (10 + FMath::Min(Combo, 20)) / 10;
        Score += LastPoints;
        ++Combo;
        return true;
    }
};
