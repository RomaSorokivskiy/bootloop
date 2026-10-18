#pragma once
#include "CoreMinimal.h"
class ABootGameMode;
namespace BootloopV04Audit
{
    BOOTLOOP_API void Start(ABootGameMode* Game);
    BOOTLOOP_API void Stop();
    // Shared by native acceptance and the deliberately invalid regression fixture.
    struct FConstraints
    {
        double WallSeconds=0., ActiveSeconds=0., TravelCm=0.;
        int32 LiveChunks=0, Effects=0, Tempos=0, Transitions=0;
        bool bSameRun=true, bAlive=true, bFinite=true;
    };
    BOOTLOOP_API bool Validate(const FConstraints& Value, bool bFinalSoak, FString& Error);
}
