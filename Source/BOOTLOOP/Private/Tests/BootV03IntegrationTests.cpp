#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Input/BootInputCaptureState.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootInputCaptureTest,"Bootloop.V03.Integration.InputDraftCapture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootInputCaptureTest::RunTest(const FString&)
{
    FBootInputCaptureState S;
    S.Begin(EBootInputAction::Jump,0,EKeys::LeftMouseButton);
    S.Press(EKeys::LeftMouseButton,false);
    TestTrue(TEXT("opener is never captured"),S.Draft.GetKey(EBootInputAction::Jump,0)==EKeys::SpaceBar);
    TestTrue(TEXT("capture waits release"),S.IsCapturing());
    S.Release(EKeys::LeftMouseButton);
    S.Press(EKeys::P,true);
    TestTrue(TEXT("repeat ignored"),S.IsCapturing());
    TestEqual(TEXT("new digital key accepted"),S.Press(EKeys::Delete,false),EBootBindingResult::Applied);
    TestTrue(TEXT("Delete remains bindable"),S.Draft.GetKey(EBootInputAction::Jump,0)==EKeys::Delete);
    S.Begin(EBootInputAction::Jump,0,EKeys::Enter); S.Release(EKeys::Enter);
    TestEqual(TEXT("conflict draft modal"),S.Press(EKeys::E,false),EBootBindingResult::Conflict);
    TestTrue(TEXT("conflict changes neither action"),S.Draft.GetKey(EBootInputAction::Jump,0)==EKeys::Delete && S.Draft.GetKey(EBootInputAction::Snap,0)==EKeys::E);
    TestTrue(TEXT("atomic explicit swap"),S.Swap());
    TestTrue(TEXT("previous key transferred"),S.Draft.GetKey(EBootInputAction::Snap,0)==EKeys::Delete);
    S.Begin(EBootInputAction::Retry,0,EKeys::Enter); S.Release(EKeys::Enter); S.CancelCapture();
    S.Press(EKeys::F,false);
    TestTrue(TEXT("focus loss cancels capture"),S.Draft.GetKey(EBootInputAction::Retry,0)==EKeys::R);
    S.Begin(EBootInputAction::Retry,0,EKeys::Enter); S.Release(EKeys::Enter); S.Press(EKeys::Escape,false);
    TestFalse(TEXT("Escape cancels capture only"),S.IsCapturing());
    return true;
}
#endif
