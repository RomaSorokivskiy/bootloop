#include "Developer/BootV04Audit.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV04AuditFixture,"Bootloop.V04.Audit.IntentionalBadFixture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV04AuditFixture::RunTest(const FString&)
{
    BootloopV04Audit::FConstraints V; FString E;
    V.LiveChunks=40;V.Effects=64;V.WallSeconds=1200.;V.ActiveSeconds=1200.;V.TravelCm=600000.;V.Tempos=3;V.Transitions=2;
    TestTrue(TEXT("valid measured limits"),BootloopV04Audit::Validate(V,true,E));
    V.LiveChunks=41;TestFalse(TEXT("intentional invalid fixture really fails"),BootloopV04Audit::Validate(V,true,E));
    V.LiveChunks=40;V.WallSeconds=1199.;TestFalse(TEXT("accelerated time cannot prove real soak"),BootloopV04Audit::Validate(V,true,E));
    V.WallSeconds=1200.;V.TravelCm=0.;TestFalse(TEXT("stationary cannot prove moving soak"),BootloopV04Audit::Validate(V,true,E));
    V.TravelCm=600000.;V.bSameRun=false;TestFalse(TEXT("retries cannot prove continuous run"),BootloopV04Audit::Validate(V,true,E));
    return true;
}
#endif
