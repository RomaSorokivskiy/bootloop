#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Input/BootInputPreferences.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootInputV04Migration,"Bootloop.V04.Input.LegacyKeysAndNewActions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootInputV04Migration::RunTest(const FString&)
{
    FBootInputPreferences Saved;
    Saved.Bindings.SetNum(9); // Serialized v0.3 ordinal layout.
    Saved.Bindings[int32(EBootInputAction::Jump)].Primary=EKeys::One;
    Saved.Bindings[int32(EBootInputAction::Snap)].Primary=EKeys::F;
    Saved.bInvertY=true;Saved.WheelPolicy=EBootWheelPolicy::Down;
    Saved.Sanitize();FText Error;
    TestTrue(TEXT("expanded save remains usable"),Saved.IsValid(Error));
    TestEqual(TEXT("four new actions added"),Saved.Bindings.Num(),13);
    TestTrue(TEXT("custom 1 not stolen by card"),Saved.GetKey(EBootInputAction::Jump,0)==EKeys::One);
    TestTrue(TEXT("custom F not stolen by dash"),Saved.GetKey(EBootInputAction::Snap,0)==EKeys::F);
    TestTrue(TEXT("invert retained"),Saved.bInvertY);
    TestTrue(TEXT("wheel policy retained"),Saved.WheelPolicy==EBootWheelPolicy::Down);
    FBootInputPreferences Fresh;
    const FKey Defaults[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::F};
    for(int32 I=0;I<4;++I)
        TestTrue(*FString::Printf(TEXT("new default %d"),I),Fresh.GetKey(static_cast<EBootInputAction>(9+I),0)==Defaults[I]);
    return true;
}
#endif
