#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Movement/BootTraversalEnvelope.h"
#include "Run/BootActiveRunClock.h"
#include "Run/BootRunTypes.h"
#include "Developer/BootAuditPaths.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTempoEnvelopeV04Test, "Bootloop.V04.Contracts.TempoEnvelope", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootTempoEnvelopeV04Test::RunTest(const FString&)
{
    FText Error;
    const auto Envelope130 = FBootTraversalPolicy::FromTempo(130., 7);
    TestEqual(TEXT("run130"), Envelope130.RunCmps, 1100.f);
    TestEqual(TEXT("momentum130"), Envelope130.MomentumCmps, 2000.f);
    TestEqual(TEXT("temporary130"), Envelope130.TemporaryCmps, 2400.f);
    TestEqual(TEXT("revision"), Envelope130.Revision, 7u);
    TestTrue(TEXT("valid baseline"), FBootTraversalPolicy::Validate(Envelope130, Error));
    const auto Fast = FBootTraversalPolicy::FromTempo(168., 8);
    TestTrue(TEXT("run168"), FMath::IsNearlyEqual(Fast.RunCmps, 1421.53846f, .01f));
    TestTrue(TEXT("momentum168"), FMath::IsNearlyEqual(Fast.MomentumCmps, 2584.61538f, .01f));
    TestTrue(TEXT("temporary168"), FMath::IsNearlyEqual(Fast.TemporaryCmps, 3101.53846f, .01f));
    TestEqual(TEXT("lower clamp"), FBootTraversalPolicy::FromTempo(1., 0).RunCmps, 770.f);
    TestEqual(TEXT("upper clamp"), FBootTraversalPolicy::FromTempo(10000., 0).TemporaryCmps, 3360.f);
    TestEqual(TEXT("absolute guard"), Fast.AbsoluteCmps, 3600.f);
    for (double Invalid : {0., -130., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        const auto Envelope = FBootTraversalPolicy::FromTempo(Invalid, 1);
        TestFalse(TEXT("invalid tempo rejected"), FBootTraversalPolicy::Validate(Envelope, Error));
        TestFalse(TEXT("invalid reason"), Error.IsEmpty());
    }
    auto Corrupt = Envelope130; Corrupt.RunCmps = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("nonfinite envelope rejected"), FBootTraversalPolicy::Validate(Corrupt, Error));
    Corrupt = Envelope130; Corrupt.TemporaryCmps = 3601.f;
    TestFalse(TEXT("overspeed rejected"), FBootTraversalPolicy::Validate(Corrupt, Error));
    Corrupt = Envelope130; Corrupt.MaxBaseCmps = 5000.f;
    TestTrue(TEXT("moving base speed independent of character cap"), FBootTraversalPolicy::Validate(Corrupt, Error));
    Corrupt.MaxBaseCmps = -1.f;
    TestFalse(TEXT("negative moving base speed rejected"), FBootTraversalPolicy::Validate(Corrupt, Error));
    Corrupt = Envelope130; Corrupt.GuardCharges = -1;
    TestFalse(TEXT("negative charges rejected"), FBootTraversalPolicy::Validate(Corrupt, Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootActiveClockTokensV04Test, "Bootloop.V04.Contracts.ActiveClockTokens", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootActiveClockTokensV04Test::RunTest(const FString&)
{
    FBootActiveRunClock Clock;
    TestEqual(TEXT("boot excluded"), Clock.Seconds(100.), 0.);
    Clock.Start(0.); Clock.SetPaused(true, 10.); Clock.SetPaused(true, 12.);
    TestEqual(TEXT("pause frozen"), Clock.Seconds(15.), 10.);
    Clock.SetPaused(false, 15.);
    TestEqual(TEXT("active"), Clock.Seconds(17.), 12.);
    TestEqual(TEXT("backward source does not rewind"), Clock.Seconds(16.), 12.);
    Clock.Start(20.); TestEqual(TEXT("restart resets"), Clock.Seconds(20.), 0.);
    const FBootRunToken OldToken{FGuid::NewGuid(), 1};
    const FBootRunToken NewToken{OldToken.RunId, 2};
    TestFalse(TEXT("retry identity"), OldToken == NewToken);
    TSet<FBootOperationKey> Keys;
    Keys.Add({OldToken, 5000000000LL, 1}); Keys.Add({NewToken, 5000000000LL, 1});
    TestEqual(TEXT("operations isolated by generation"), Keys.Num(), 2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootAuditIsolationV04Test, "Bootloop.V04.Contracts.AuditIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootAuditIsolationV04Test::RunTest(const FString&)
{
    TestEqual(TEXT("v04 slot"), FBootAuditPaths::ProfileSlot(TEXT("-BootloopV04Audit")), FString(TEXT("BootloopAudit_v04")));
    TestEqual(TEXT("v03 preserved"), FBootAuditPaths::ProfileSlot(TEXT("-BootloopV03Audit")), FString(TEXT("BootloopAudit_v03")));
    TestEqual(TEXT("smoke preserved"), FBootAuditPaths::ProfileSlot(TEXT("-BootloopSmoke")), FString(TEXT("BootloopSmoke_v2")));
    TestEqual(TEXT("normal preserved"), FBootAuditPaths::ProfileSlot(TEXT("")), FString(TEXT("BootloopProfile_v2")));
    TestEqual(TEXT("v04 precedence"), FBootAuditPaths::ProfileSlot(TEXT("-BootloopV04Audit -BootloopV03Audit -BootloopSmoke")), FString(TEXT("BootloopAudit_v04")));
    TestTrue(TEXT("v04 library isolated"), FBootAuditPaths::LibraryRoot(TEXT("-BootloopV04Audit")).EndsWith(TEXT("V04Audit/Library")));
    TestTrue(TEXT("v04 cache isolated"), FBootAuditPaths::CacheRoot(TEXT("-BootloopV04Audit")).EndsWith(TEXT("V04Audit/Library/Cache")));
    TestTrue(TEXT("v03 library isolated"), FBootAuditPaths::LibraryRoot(TEXT("-BootloopV03Audit")).EndsWith(TEXT("V03Audit/Library")));
    TestTrue(TEXT("smoke library isolated"), FBootAuditPaths::LibraryRoot(TEXT("-BootloopSmoke")).EndsWith(TEXT("SmokeAudit/Library")));
    TestTrue(TEXT("normal library preserved"), FBootAuditPaths::LibraryRoot(TEXT("")).EndsWith(TEXT("MusicLibrary")));
    TestFalse(TEXT("normal is not isolated"), FBootAuditPaths::IsIsolatedProfile(TEXT("")));
    return true;
}
// Accelerated data test; this is not a wall-clock physics soak.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootActiveClockLongDurationV04Test,"Bootloop.V04.Contracts.ActiveClockLongDuration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootActiveClockLongDurationV04Test::RunTest(const FString&)
{
    constexpr double Origin=3700000000.;
    constexpr int64 Fps=120,Hours=24,PauseHour=6,ResumeHour=9;
    FBootActiveRunClock Clock;Clock.Start(Origin);
    double Value=0.;
    for(int64 Frame=1;Frame<=Hours*3600*Fps;++Frame)
    {
        const double Now=Origin+double(Frame)/double(Fps);
        if(Frame==PauseHour*3600*Fps)Clock.SetPaused(true,Now);
        if(Frame==ResumeHour*3600*Fps)Clock.SetPaused(false,Now);
        Value=Clock.Seconds(Now);
        if(Frame%(3600*Fps)==0)
        {
            const int64 Hour=Frame/(3600*Fps);
            const double Expected=double(Hour-FMath::Clamp<int64>(Hour-PauseHour,0,ResumeHour-PauseHour))*3600.;
            if(!TestTrue(TEXT("large-uptime hourly active time remains precise"),FMath::Abs(Value-Expected)<.000002))return false;
            TestEqual(TEXT("repeated timestamp does not accrue"),Clock.Seconds(Now),Value);
            TestEqual(TEXT("older timestamp does not accrue"),Clock.Seconds(Now-1.),Value);
        }
    }
    TestEqual(TEXT("24 simulated hours exclude the three-hour pause"),Value,75600.);
    Clock.Start(Origin+100000.);
    TestEqual(TEXT("retry resets long accumulated duration"),Clock.Seconds(Origin+100000.),0.);
    return true;
}

#endif


