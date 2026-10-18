#pragma once
#include "CoreMinimal.h"
#include "Music/BootMusicTypes.h"
#include "Music/BootRhythmScoring.h"

/** Judging and pulse share the render-owned canonical timeline. Offset is user calibration, not measured device latency. */
class BOOTLOOP_API FBootBeatJudge
{
public:
 static EBootBeatRating Rate(const FBootTransportSnapshot&, double InputOffsetMs);
 static float Pulse(const FBootTransportSnapshot&);
};
