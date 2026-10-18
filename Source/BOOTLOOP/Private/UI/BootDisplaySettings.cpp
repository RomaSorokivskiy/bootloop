#include "UI/BootDisplaySettings.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GenericPlatform/GenericApplication.h"
#include "Widgets/SWindow.h"

TArray<FIntPoint> FBootDisplaySettings::BuildModes(FIntPoint Native, const TArray<FIntPoint>& Supported)
{
    if (Native.X <= 0 || Native.Y <= 0) Native = FIntPoint(1280,720);
    TArray<FIntPoint> Result;
    auto Add = [&](FIntPoint Size)
    {
        if (Size.X >= 960 && Size.Y >= 540 && Size.X <= Native.X && Size.Y <= Native.Y)
            Result.AddUnique(Size);
    };
    for (const auto Size : {FIntPoint(1280,720), FIntPoint(1600,900), FIntPoint(1920,1080),
        FIntPoint(2560,1440), FIntPoint(3840,2160)}) Add(Size);
    for (const auto Size : Supported) Add(Size);
    Result.AddUnique(Native);
    Result.Sort([](FIntPoint A, FIntPoint B)
    {
        const int64 AreaA = int64(A.X)*A.Y, AreaB = int64(B.X)*B.Y;
        return AreaA == AreaB ? A.X < B.X : AreaA < AreaB;
    });
    return Result;
}

int32 FBootDisplaySettings::ClosestMode(FIntPoint Preferred, const TArray<FIntPoint>& Modes)
{
    int32 Best = INDEX_NONE;
    double Distance = TNumericLimits<double>::Max();
    for (int32 I = 0; I < Modes.Num(); ++I)
    {
        const double Candidate = FMath::Square(double(Modes[I].X)-Preferred.X)
            + FMath::Square(double(Modes[I].Y)-Preferred.Y);
        if (Candidate < Distance) { Best=I; Distance=Candidate; }
    }
    return Best;
}

FIntPoint FBootDisplaySettings::CurrentMonitorNative()
{
    FDisplayMetrics Metrics;
    FDisplayMetrics::RebuildDisplayMetrics(Metrics);
    const TSharedPtr<SWindow> Window = GEngine && GEngine->GameViewport
        ? GEngine->GameViewport->GetWindow() : nullptr;
    const FSlateRect WindowRect = Window.IsValid() ? Window->GetRectInScreen() : FSlateRect();
    const FMonitorInfo* Selected = nullptr;
    double BestArea = -1.;
    for (const auto& Monitor : Metrics.MonitorInfo)
    {
        const auto& Rect = Monitor.DisplayRect;
        const double Area = Window.IsValid()
            ? FMath::Max(0., FMath::Min(double(WindowRect.Right),double(Rect.Right))-FMath::Max(double(WindowRect.Left),double(Rect.Left)))
                * FMath::Max(0., FMath::Min(double(WindowRect.Bottom),double(Rect.Bottom))-FMath::Max(double(WindowRect.Top),double(Rect.Top)))
            : Monitor.bIsPrimary ? 1. : 0.;
        if (Area > BestArea) { Selected=&Monitor; BestArea=Area; }
    }
    if (Selected)
    {
        // DisplayRect is in desktop pixels; NativeWidth/Height may come from EDID.
        // Preserve portrait orientation instead of offering landscape dimensions.
        const int32 Width = Selected->DisplayRect.Right-Selected->DisplayRect.Left;
        const int32 Height = Selected->DisplayRect.Bottom-Selected->DisplayRect.Top;
        FIntPoint Native(Selected->NativeWidth,Selected->NativeHeight);
        if (Native.X <= 0 || Native.Y <= 0) Native=FIntPoint(Width,Height);
        if ((Width < Height) != (Native.X < Native.Y)) Swap(Native.X,Native.Y);
        if (Native.X > 0 && Native.Y > 0) return Native;
    }
    return Metrics.PrimaryDisplayWidth>0 && Metrics.PrimaryDisplayHeight>0
        ? FIntPoint(Metrics.PrimaryDisplayWidth,Metrics.PrimaryDisplayHeight) : FIntPoint(1280,720);
}
