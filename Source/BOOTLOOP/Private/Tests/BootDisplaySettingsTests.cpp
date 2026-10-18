#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/BootDisplaySettings.h"
#include "Profile/BootProfile.h"
#include "Kismet/GameplayStatics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootMonitorModesTest, "Bootloop.ShellPolish.MonitorModes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootMonitorModesTest::RunTest(const FString&)
{
    const TArray<FIntPoint> Supported = {FIntPoint(1920,1080), FIntPoint(2560,1440),
        FIntPoint(3840,2160), FIntPoint(2560,1440), FIntPoint(7680,4320), FIntPoint(0,0)};
    for (const FIntPoint Native : {FIntPoint(1366,768), FIntPoint(2560,1440), FIntPoint(3440,1440), FIntPoint(3840,2160)})
    {
        const auto Modes = FBootDisplaySettings::BuildModes(Native, Supported);
        if (!TestTrue(TEXT("Monitor always has a selectable mode"), !Modes.IsEmpty())) return false;
        TestEqual(TEXT("Maximum selectable mode equals the monitor native resolution"), Modes.Last(), Native);
        TSet<FIntPoint> Seen;
        int64 PreviousArea = 0;
        for (const auto Mode : Modes)
        {
            TestTrue(TEXT("Modes never exceed either native dimension"), Mode.X > 0 && Mode.Y > 0 && Mode.X <= Native.X && Mode.Y <= Native.Y);
            TestFalse(TEXT("Refresh-rate variants do not duplicate resolutions"), Seen.Contains(Mode));
            Seen.Add(Mode);
            const int64 Area = int64(Mode.X) * Mode.Y;
            TestTrue(TEXT("Modes cycle from smaller to larger"), Area >= PreviousArea);
            PreviousArea = Area;
        }
    }
    TestEqual(TEXT("Missing monitor data has a safe fallback"),
        FBootDisplaySettings::BuildModes(FIntPoint::ZeroValue, {}).Last(), FIntPoint(1280,720));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootDisplayPreferenceTest, "Bootloop.ShellPolish.ResolutionPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootDisplayPreferenceTest::RunTest(const FString&)
{
    auto* Profile=NewObject<UBootProfile>();
    Profile->Options.Resolution=7;
    Profile->Options.ResolutionSize=FIntPoint(3440,1440);
    Profile->Options.Clamp();
    TArray<uint8> Data;
    if(!TestTrue(TEXT("Display preference serializes"),UGameplayStatics::SaveGameToMemory(Profile,Data)))return false;
    auto* Loaded=Cast<UBootProfile>(UGameplayStatics::LoadGameFromMemory(Data));
    if(!TestNotNull(TEXT("Display preference restores"),Loaded))return false;
    TestEqual(TEXT("Ultrawide dimensions survive restart"),Loaded->Options.ResolutionSize,FIntPoint(3440,1440));
    const auto Small=FBootDisplaySettings::BuildModes(FIntPoint(1366,768),{});
    TestEqual(TEXT("Moving to a smaller display selects its maximum instead of stale index"),
        Small[FBootDisplaySettings::ClosestMode(Loaded->Options.ResolutionSize,Small)],FIntPoint(1366,768));
    return true;
}
#endif
