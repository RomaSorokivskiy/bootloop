#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Input/BootInputPreferences.h"
#include "Input/BootInputMappingComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputKeyEventArgs.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootInputDefaultsTest,"Bootloop.V03.Input.DefaultsAndAtomicSwap",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootInputDefaultsTest::RunTest(const FString&)
{
    FBootInputPreferences P; FText Error;
    TestTrue(TEXT("Fresh bindings usable"),P.IsValid(Error));
    TestTrue(TEXT("Space default"),P.GetKey(EBootInputAction::Jump,0)==EKeys::SpaceBar);
    TestTrue(TEXT("Defaults offer both wheel directions"),P.AcceptsWheel(1)&&P.AcceptsWheel(-1));
    TestTrue(TEXT("Conflict reports without replacing destination"),P.Assign(EBootInputAction::Jump,0,EKeys::W,false)==EBootBindingResult::Conflict);
    TestTrue(TEXT("Cancel preserved jump"),P.GetKey(EBootInputAction::Jump,0)==EKeys::SpaceBar);
    TestTrue(TEXT("Cancel preserved movement"),P.GetKey(EBootInputAction::Forward,0)==EKeys::W);
    TestTrue(TEXT("Swap succeeds atomically"),P.Assign(EBootInputAction::Jump,0,EKeys::W,true)==EBootBindingResult::Applied);
    TestTrue(TEXT("Forward receives old jump key"),P.GetKey(EBootInputAction::Forward,0)==EKeys::SpaceBar);
    TestTrue(TEXT("Jump receives W"),P.GetKey(EBootInputAction::Jump,0)==EKeys::W);
    for (FKey Key : {EKeys::Escape,EKeys::MouseX,EKeys::MouseY,EKeys::MouseWheelAxis,EKeys::MouseScrollUp,EKeys::MouseScrollDown,EKeys::Gamepad_FaceButton_Bottom,FKey(TEXT("MissingKeyV03"))})
        TestTrue(TEXT("Reserved/axis/wheel/gamepad/unknown rejected"),P.Assign(EBootInputAction::Jump,0,Key,true)==EBootBindingResult::Invalid);
    TestTrue(TEXT("Delete bindable"),P.Assign(EBootInputAction::Jump,0,EKeys::Delete,false)==EBootBindingResult::Applied);
    TestTrue(TEXT("Backspace bindable"),P.Assign(EBootInputAction::Jump,1,EKeys::BackSpace,false)==EBootBindingResult::Applied);
    FBootInputPreferences EmptySwap; EmptySwap.Clear(EBootInputAction::Jump,0);
    TestTrue(TEXT("Swap cannot strand mandatory movement"),EmptySwap.Assign(EBootInputAction::Jump,0,EKeys::W,true)==EBootBindingResult::Invalid);
    TestTrue(TEXT("Rejected swap keeps original Forward"),EmptySwap.GetKey(EBootInputAction::Forward,0)==EKeys::W);
    TestFalse(TEXT("Rejected swap keeps target empty"),EmptySwap.GetKey(EBootInputAction::Jump,0).IsValid());
    TestTrue(TEXT("Same-action duplicate assignment collapses"),P.Assign(EBootInputAction::Snap,1,EKeys::E,false)==EBootBindingResult::Applied);
    TestFalse(TEXT("Duplicate action key removed from previous slot"),P.GetKey(EBootInputAction::Snap,0).IsValid());
    P.Clear(EBootInputAction::AirRedirect,0);
    TestFalse(TEXT("Missing mandatory action cannot Apply"),P.IsValid(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootInputWheelTest,"Bootloop.V03.Input.WheelPolicies",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootInputWheelTest::RunTest(const FString&)
{
    FBootInputPreferences P; FText Error;
    P.Clear(EBootInputAction::Jump,0); P.Clear(EBootInputAction::Jump,1);
    TestTrue(TEXT("Wheel-only Jump usable"),P.IsValid(Error));
    P.WheelPolicy=EBootWheelPolicy::Off;
    TestFalse(TEXT("Jump needs a button or enabled wheel"),P.IsValid(Error));
    TestFalse(TEXT("Wheel Off rejects positive"),P.AcceptsWheel(1));
    TestFalse(TEXT("Wheel Off rejects negative"),P.AcceptsWheel(-1));
    P.WheelPolicy=EBootWheelPolicy::Up;
    TestTrue(TEXT("Up permits positive only"),P.AcceptsWheel(.1f)&&!P.AcceptsWheel(-1));
    P.WheelPolicy=EBootWheelPolicy::Down;
    TestTrue(TEXT("Down permits negative only"),!P.AcceptsWheel(1)&&P.AcceptsWheel(-.1f));
    P.WheelPolicy=EBootWheelPolicy::Both;
    TestFalse(TEXT("Zero wheel not an impulse"),P.AcceptsWheel(0));
    TestFalse(TEXT("Nonfinite wheel rejected"),P.AcceptsWheel(std::numeric_limits<float>::quiet_NaN()));
    TestTrue(TEXT("Jump label includes wheel policy"),!P.Label(EBootInputAction::Jump).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootInputSerializationTest,"Bootloop.V03.Input.SerializationAndSanitize",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootInputSerializationTest::RunTest(const FString&)
{
    FBootInputPreferences P;
    P.Bindings.SetNum(9); // Original serialized v0.3 save predates the F dash default.
    P.Assign(EBootInputAction::Snap,0,EKeys::F,false); P.bInvertY=true; P.WheelPolicy=EBootWheelPolicy::Down;
    TArray<uint8> Bytes;
    { FMemoryWriter Writer(Bytes); FBootInputPreferences::StaticStruct()->SerializeItem(Writer,&P,nullptr); }
    FBootInputPreferences Loaded;
    { FMemoryReader Reader(Bytes); FBootInputPreferences::StaticStruct()->SerializeItem(Reader,&Loaded,nullptr); }
    TestTrue(TEXT("Saved custom key round trips"),Loaded.GetKey(EBootInputAction::Snap,0)==EKeys::F);
    TestTrue(TEXT("Invert saved"),Loaded.bInvertY);
    TestTrue(TEXT("Wheel saved"),Loaded.WheelPolicy==EBootWheelPolicy::Down);
    Loaded.Bindings[0].Primary=EKeys::Escape;
    Loaded.Bindings[1].Primary=EKeys::F; // A corrupt cross-action duplicate must not erase every good custom key.
    Loaded.Bindings[static_cast<int32>(EBootInputAction::Slide)].Secondary=EKeys::LeftControl;
    Loaded.WheelPolicy=static_cast<EBootWheelPolicy>(255);
    Loaded.Sanitize(); FText Error;
    TestTrue(TEXT("Corrupt save becomes usable"),Loaded.IsValid(Error));
    TestTrue(TEXT("Unrelated valid custom mouse slot preserved"),Loaded.GetKey(EBootInputAction::Snap,1)==EKeys::RightMouseButton);
    TestTrue(TEXT("Duplicate within action collapsed"),!Loaded.GetKey(EBootInputAction::Slide,1).IsValid());
    FBootInputPreferences Partial; Partial.Bindings.SetNum(2); Partial.Bindings[0].Primary=EKeys::Up;
    Partial.Sanitize();
    TestTrue(TEXT("Legacy partial save receives missing actions"),Partial.IsValid(Error));
    TestTrue(TEXT("Sanitize preserves custom valid key"),Partial.GetKey(EBootInputAction::Forward,0)==EKeys::Up);
    TestTrue(TEXT("Legacy missing Jump receives Space"),Partial.GetKey(EBootInputAction::Jump,0)==EKeys::SpaceBar);
    FBootInputPreferences Corrupt; Corrupt.Bindings.SetNum(9); Corrupt.Assign(EBootInputAction::Snap,0,EKeys::F,false);
    Corrupt.Bindings[0].Primary=FKey(TEXT("MissingKeyV03")); Corrupt.Sanitize();
    TestTrue(TEXT("Invalid saved action does not erase unrelated custom key"),Corrupt.GetKey(EBootInputAction::Snap,0)==EKeys::F);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootInputPhysicalTest,"Bootloop.V03.Input.PhysicalMappingRebuild",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootInputPhysicalTest::RunTest(const FString&)
{
    UWorld::InitializationValues Init;
    Init.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    APlayerController* PC=World->SpawnActor<APlayerController>();
    ULocalPlayer* Local=NewObject<ULocalPlayer>(GEngine);
    Local->PlayerController=PC; PC->Player=Local;
    PC->PlayerInput=NewObject<UEnhancedPlayerInput>(PC);
    Local->PlayerAdded(nullptr,0);
    auto* PlayerInput=CastChecked<UEnhancedPlayerInput>(PC->PlayerInput);
    auto* Subsystem=Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
    auto* Mapping=NewObject<UBootInputMappingComponent>(PC); Mapping->EnsureActions();
    auto* Input=NewObject<UEnhancedInputComponent>(PC);
    int32 JumpStarts=0, WheelAccepted=0;
    FBootInputPreferences P;
    Input->BindActionValueLambda(Mapping->GetAction(EBootInputAction::Jump),ETriggerEvent::Started,[&](const FInputActionValue&){++JumpStarts;});
    Input->BindActionValueLambda(Mapping->GetWheelAction(),ETriggerEvent::Triggered,[&](const FInputActionValue& Value){if(P.AcceptsWheel(Value.Get<float>())) ++WheelAccepted;});
    auto* ForeignContext=NewObject<UInputMappingContext>(PC);
    auto* ForeignAction=NewObject<UInputAction>(PC); ForeignContext->MapKey(ForeignAction,EKeys::F12);
    int32 ForeignStarts=0;
    Input->BindActionValueLambda(ForeignAction,ETriggerEvent::Started,[&](const FInputActionValue&){++ForeignStarts;});
    if(Subsystem) Subsystem->AddMappingContext(ForeignContext,10);
    TArray<UInputComponent*> Stack{Input};
    auto Frame=[&](){ ++GFrameCounter; PlayerInput->Tick(1.f/60.f); PlayerInput->ProcessInputStack(Stack,1.f/60.f,false); };
    auto Key=[&](FKey K,EInputEvent Event){PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(K,Event,Event==IE_Released?0.f:1.f)); Frame();};
    Mapping->Apply(P,PC);
    Key(EKeys::SpaceBar,IE_Pressed); Key(EKeys::SpaceBar,IE_Released);
    TestEqual(TEXT("Default physical Space reaches Jump"),JumpStarts,1);
    P.Assign(EBootInputAction::Jump,0,EKeys::J,false); Mapping->Apply(P,PC);
    Key(EKeys::SpaceBar,IE_Pressed); Key(EKeys::SpaceBar,IE_Released);
    TestEqual(TEXT("Old physical key removed"),JumpStarts,1);
    Key(EKeys::J,IE_Pressed); Key(EKeys::J,IE_Repeat); Key(EKeys::J,IE_Released);
    TestEqual(TEXT("New key fires exactly one Started despite repeat"),JumpStarts,2);
    Key(EKeys::K,IE_Pressed); P.Assign(EBootInputAction::Jump,0,EKeys::K,false); Mapping->Apply(P,PC); Frame();
    TestEqual(TEXT("Apply ignores already held new key"),JumpStarts,2);
    Key(EKeys::K,IE_Released); Key(EKeys::K,IE_Pressed);
    TestEqual(TEXT("Release then fresh press fires"),JumpStarts,3);
    Mapping->Apply(P,PC); Frame();
    TestEqual(TEXT("Apply ignores already held unchanged key"),JumpStarts,3);
    Key(EKeys::K,IE_Released);
    UInputAction* Persistent=Mapping->GetAction(EBootInputAction::Jump);
    UInputMappingContext* Context=Mapping->GetMappingContext();
    for(int32 I=0;I<20;++I) Mapping->Apply(P,PC);
    Key(EKeys::K,IE_Pressed); Key(EKeys::K,IE_Released);
    TestEqual(TEXT("20 Apply cycles do not duplicate callbacks"),JumpStarts,4);
    TestTrue(TEXT("Action identity survives Apply"),Mapping->GetAction(EBootInputAction::Jump)==Persistent);
    TestTrue(TEXT("Context identity survives Apply"),Mapping->GetMappingContext()==Context);
    Key(EKeys::W,IE_Pressed); Key(EKeys::D,IE_Pressed);
    FVector2D Move=PlayerInput->GetActionValue(Mapping->GetMoveAction()).Get<FVector2D>();
    TestTrue(TEXT("Forward plus right accumulates both axes"),Move.Equals(FVector2D(1,1),.01));
    Key(EKeys::S,IE_Pressed); Key(EKeys::A,IE_Pressed);
    TestTrue(TEXT("Opposing keys cancel"),PlayerInput->GetActionValue(Mapping->GetMoveAction()).Get<FVector2D>().IsNearlyZero());
    for(FKey K : {EKeys::W,EKeys::S,EKeys::A,EKeys::D}) Key(K,IE_Released);
    Key(EKeys::W,IE_Pressed); Mapping->Apply(P,PC); Frame();
    TestTrue(TEXT("Apply suppresses held movement until release"),PlayerInput->GetActionValue(Mapping->GetMoveAction()).Get<FVector2D>().IsNearlyZero());
    Key(EKeys::W,IE_Released); Key(EKeys::W,IE_Pressed);
    TestTrue(TEXT("Movement resumes on fresh press"),PlayerInput->GetActionValue(Mapping->GetMoveAction()).Get<FVector2D>().Equals(FVector2D(0,1),.01));
    Key(EKeys::W,IE_Released);
    int32 WheelAxes=0,WheelButtons=0;
    for(const FEnhancedActionKeyMapping& M:Mapping->GetMappingContext()->GetMappings())
    { WheelAxes+=M.Key==EKeys::MouseWheelAxis; WheelButtons+=M.Key==EKeys::MouseScrollUp||M.Key==EKeys::MouseScrollDown; }
    TestEqual(TEXT("One wheel axis mapping"),WheelAxes,1); TestEqual(TEXT("No duplicate wheel button path"),WheelButtons,0);
    auto Wheel=[&](float Delta){PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseWheelAxis,IE_Axis,Delta)); Frame(); Frame();};
    for(EBootWheelPolicy Policy:{EBootWheelPolicy::Both,EBootWheelPolicy::Up,EBootWheelPolicy::Down,EBootWheelPolicy::Off})
    {
        P.WheelPolicy=Policy; Mapping->Apply(P,PC); const int32 Before=WheelAccepted;
        Wheel(1); Wheel(-1);
        const int32 Expected=Policy==EBootWheelPolicy::Both?2:Policy==EBootWheelPolicy::Off?0:1;
        TestEqual(TEXT("Physical wheel respects one policy path"),WheelAccepted-Before,Expected);
    }
    Key(EKeys::F12,IE_Pressed); Key(EKeys::F12,IE_Released);
    TestEqual(TEXT("Apply preserves unrelated input context"),ForeignStarts,1);
    Mapping->RemoveFromPlayer(PC); Key(EKeys::K,IE_Pressed); Key(EKeys::K,IE_Released);
    TestEqual(TEXT("Removal stops gameplay key"),JumpStarts,4);
    if(Subsystem) TestFalse(TEXT("Own context removed"),Subsystem->HasMappingContext(Context));
    Key(EKeys::F12,IE_Pressed); Key(EKeys::F12,IE_Released);
    TestEqual(TEXT("Removal preserves unrelated input context"),ForeignStarts,2);
    if(Subsystem) Subsystem->RemoveMappingContext(ForeignContext);
    Local->PlayerRemoved(); Local->PlayerController=nullptr; PC->Player=nullptr;
    GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}
#endif
