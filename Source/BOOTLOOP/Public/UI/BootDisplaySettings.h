#pragma once
#include "CoreMinimal.h"

/** Monitor-limited display modes shared by the preferences menu and Apply. */
struct BOOTLOOP_API FBootDisplaySettings
{
    static TArray<FIntPoint> BuildModes(FIntPoint Native, const TArray<FIntPoint>& Supported);
    static int32 ClosestMode(FIntPoint Preferred, const TArray<FIntPoint>& Modes);
    static FIntPoint CurrentMonitorNative();
};
