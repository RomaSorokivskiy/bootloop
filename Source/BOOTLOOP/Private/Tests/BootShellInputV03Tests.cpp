#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Developer/BootAuditPaths.h"
#include "UI/BootShellWidget.h"
#include "UI/BootPlayerController.h"
#include "Run/BootGameMode.h"
#include "Core/BootGameInstance.h"
#include "Player/BootRunner.h"
#include "World/Interaction/BootSurfWindow.h"
#include "World/Threats/BootPopupProcess.h"
#include "Components/PrimitiveComponent.h"
#include "Input/BootInputMappingComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

// Test-only bridge to the real native widget handlers. Hit regions stand in for a
// completed 1280x720 paint; input, capture, controller transitions and actions are real.
struct FBootShellInputTestAccess
{
    static FString Prompt(UBootShellWidget* Shell,const ABootSurfWindow* Target,const ABootRunner* Runner,const FBootInputPreferences& Inputs)
    { return Shell->InteractionPrompt(Target,Runner,Inputs); }
    static void Attach(ABootPlayerController* PC, UBootShellWidget* Shell) { PC->Shell=Shell; }
    static FReply Down(UBootShellWidget* Shell,FKey Key,bool Repeat=false,bool Shift=false)
    { return Shell->NativeOnKeyDown(FGeometry(),FKeyEvent(Key,FModifierKeysState(Shift,false,false,false,false,false,false,false,false),0,Repeat,0,0)); }
    static void Up(UBootShellWidget* Shell,FKey Key)
    { Shell->NativeOnKeyUp(FGeometry(),FKeyEvent(Key,FModifierKeysState(),0,false,0,0)); }
    static FReply Click(UBootShellWidget* Shell,FKey Key,FVector2D Point=FVector2D(5,5))
    { return Shell->NativeOnMouseButtonDown(FGeometry(),FPointerEvent(0,Point,Point,TSet<FKey>{Key},Key,0,FModifierKeysState())); }
    static FBootInputCaptureState& Draft(UBootShellWidget* Shell) { return Shell->InputDraft; }
    static int32 Focus(UBootShellWidget* Shell) { return Shell->Focus; }
    static void Areas(UBootShellWidget* Shell,std::initializer_list<int32> Ids,int32 Focus=0)
    {
        Shell->Areas.Reset(); Shell->Focus=Focus; Shell->DrawScale=1; Shell->DrawOrigin=FVector2D::ZeroVector;
        float X=100;
        for(int32 Id:Ids) { Shell->Areas.Add({FSlateRect(X,100,X+50,150),Id,false}); X+=60; }
    }
};

namespace BootShellInputV03
{
struct FFixture
{
    UWorld* World;
    UBootGameInstance* GI;
    ABootPlayerController* PC;
    ABootGameMode* Mode;
    ABootRunner* Runner;
    UBootShellWidget* Shell;
    ULocalPlayer* Local;
    UEnhancedPlayerInput* PlayerInput;
    UEnhancedInputComponent* Input;
    TArray<UInputComponent*> Stack;
    FFixture()
    {
        UWorld::InitializationValues Init;
        Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
        GI=NewObject<UBootGameInstance>(GEngine); GI->InitializeStandalone();
        UWorld* DummyWorld=GI->GetWorld();
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        World->SetGameInstance(GI); GI->GetWorldContext()->SetCurrentWorld(World); DummyWorld->DestroyWorld(false);
        GI->GetProfile()->Inputs=FBootInputPreferences();
        World->GetWorldSettings()->DefaultGameMode=ABootFirstLoginGameMode::StaticClass();
        World->SetGameMode(FURL()); World->InitializeActorsForPlay(FURL());
        Mode=World->GetAuthGameMode<ABootGameMode>(); Mode->DispatchBeginPlay();
        PC=World->SpawnActor<ABootPlayerController>(); PC->SetAsLocalPlayerController();
        Local=NewObject<ULocalPlayer>(GEngine); Local->PlayerController=PC; PC->Player=Local;
        GI->AddLocalPlayer(Local,FPlatformUserId::CreateFromInternalId(0));
        PC->PlayerInput=PlayerInput=NewObject<UEnhancedPlayerInput>(PC);
        Runner=World->SpawnActor<ABootRunner>(); PC->Possess(Runner);
        Input=NewObject<UEnhancedInputComponent>(PC); Runner->SetupPlayerInputComponent(Input); Stack.Add(Input);
        Shell=CreateWidget<UBootShellWidget>(PC); FBootShellInputTestAccess::Attach(PC,Shell);
        Start();
    }
    ~FFixture()
    {
        Runner->Destroy(); GI->RemoveLocalPlayer(Local);
        Local->PlayerController=nullptr; PC->Player=nullptr; GI->Shutdown();
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    void Start()
    {
        Mode->StartRun();
        // First tick installs pending timers; only a subsequent tick can expire BootTimer.
        ++GFrameCounter; World->GetTimerManager().Tick(.001f);
        ++GFrameCounter; World->GetTimerManager().Tick(1.2f); Frame();
    }
    void Frame() { ++GFrameCounter; PlayerInput->Tick(1.f/60.f); PlayerInput->ProcessInputStack(Stack,1.f/60.f,false); }
    void Key(FKey Key,EInputEvent Event)
    { PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,Event,Event==IE_Released?0.f:1.f)); Frame(); }
};
bool AuditOnly(FAutomationTestBase& Test)
{
    if(FBootAuditPaths::IsV04Audit(FCommandLine::Get()) || FParse::Param(FCommandLine::Get(),TEXT("BootloopV03Audit"))) return true;
    Test.AddError(TEXT("Shell input integration writes only isolated audit profile: run with -BootloopV03Audit or -BootloopV04Audit.")); return false;
}
}

// Fails when conflict capture intercepts navigation/activation before the modal.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootShellConflictKeyboardTest,"Bootloop.V03.ShellInput.ConflictKeyboardSwapAndEscape",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootShellConflictKeyboardTest::RunTest(const FString&)
{
    if(!BootShellInputV03::AuditOnly(*this)) return false;
    BootShellInputV03::FFixture F; using A=FBootShellInputTestAccess;
    TestEqual(TEXT("Fixture enters running through real boot timer"),F.Mode->GetRunState(),EBootRunState::Running);
    F.PC->TogglePause(); F.PC->ShowInputs();
    // A non-zero focused binding must normalize to the modal's two choices.
    A::Areas(F.Shell,{300,304,308,312,316},4);
    A::Down(F.Shell,EKeys::Enter); A::Up(F.Shell,EKeys::Enter); A::Down(F.Shell,EKeys::E);
    TestTrue(TEXT("Conflicting physical key opens modal"),A::Draft(F.Shell).bConflict);
    TestEqual(TEXT("New modal focuses Swap"),A::Focus(F.Shell),0);
    A::Areas(F.Shell,{354,355},A::Focus(F.Shell));
    A::Down(F.Shell,EKeys::Tab);
    TestEqual(TEXT("Tab selects Cancel within conflict modal"),A::Focus(F.Shell),1);
    A::Down(F.Shell,EKeys::Tab,false,true); A::Down(F.Shell,EKeys::Enter);
    TestFalse(TEXT("Enter completes modal swap"),A::Draft(F.Shell).IsCapturing());
    TestTrue(TEXT("Jump draft receives E"),A::Draft(F.Shell).Draft.GetKey(EBootInputAction::Jump,0)==EKeys::E);
    TestTrue(TEXT("Snap draft atomically receives Space"),A::Draft(F.Shell).Draft.GetKey(EBootInputAction::Snap,0)==EKeys::SpaceBar);
    TestTrue(TEXT("Live mapping remains unchanged before Apply"),F.GI->GetInputPreferences().GetKey(EBootInputAction::Jump,0)==EKeys::SpaceBar);
    A::Areas(F.Shell,{316}); A::Down(F.Shell,EKeys::Enter); A::Up(F.Shell,EKeys::Enter);
    A::Down(F.Shell,EKeys::W); A::Down(F.Shell,EKeys::Escape);
    TestFalse(TEXT("Escape cancels capture/modal"),A::Draft(F.Shell).IsCapturing());
    TestEqual(TEXT("Capture Escape stays on Inputs"),F.PC->GetPage(),EBootPage::Inputs);
    TestEqual(TEXT("Inputs keeps paused run"),F.Mode->GetRunState(),EBootRunState::Paused);
    return true;
}

// Fails when a flushed physical hold can be resurrected by a post-resume OS repeat.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootShellHeldJumpTest,"Bootloop.V03.ShellInput.HeldJumpAcrossPauseAndInputsApply",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootShellHeldJumpTest::RunTest(const FString&)
{
    if(!BootShellInputV03::AuditOnly(*this)) return false;
    BootShellInputV03::FFixture F; using A=FBootShellInputTestAccess;
    TestEqual(TEXT("Fixture enters running through real boot timer"),F.Mode->GetRunState(),EBootRunState::Running);
    int32 Starts=0;
    F.Input->BindActionValueLambda(F.Runner->GetInputMapping()->GetAction(EBootInputAction::Jump),ETriggerEvent::Started,[&](const FInputActionValue&){++Starts;});
    for(bool Inputs:{false,true})
    {
        const int32 Previous=Starts;
        F.Key(EKeys::SpaceBar,IE_Pressed); const int32 Before=Starts;
        TestEqual(TEXT("Physical precondition: fresh Space reaches real Jump mapping"),Before,Previous+1);
        F.Key(EKeys::SpaceBar,IE_Repeat); F.Frame();
        TestEqual(TEXT("A normally held key remains one Started action"),Starts,Before);
        F.PC->TogglePause();
        TestFalse(TEXT("Real ShowPage flush clears PlayerInput pressed state"),F.PlayerInput->IsPressed(EKeys::SpaceBar));
        A::Areas(F.Shell,{5}); A::Down(F.Shell,EKeys::SpaceBar,true); F.Frame();
        if(Inputs)
        {
            F.PC->ShowInputs(); A::Areas(F.Shell,{316}); A::Down(F.Shell,EKeys::Enter); A::Up(F.Shell,EKeys::Enter);
            A::Down(F.Shell,EKeys::SpaceBar,true); A::Down(F.Shell,EKeys::Escape);
            A::Areas(F.Shell,{353}); A::Click(F.Shell,EKeys::LeftMouseButton,FVector2D(110,110));
            F.PC->Back();
            TestEqual(TEXT("Apply/Back returns to paused page"),F.PC->GetPage(),EBootPage::Pause);
            TestEqual(TEXT("Apply/Back preserves paused run"),F.Mode->GetRunState(),EBootRunState::Paused);
        }
        A::Areas(F.Shell,{5}); A::Click(F.Shell,EKeys::LeftMouseButton,FVector2D(110,110));
        TestEqual(TEXT("Mouse Resume reaches HUD"),F.PC->GetPage(),EBootPage::Hud);
        F.Key(EKeys::SpaceBar,IE_Repeat); F.Key(EKeys::SpaceBar,IE_Repeat);
        TestEqual(TEXT("Held Space repeat cannot create Jump after real flush/resume"),Starts,Before);
        F.Key(EKeys::SpaceBar,IE_Released); F.Key(EKeys::SpaceBar,IE_Pressed);
        TestEqual(TEXT("Release and fresh press creates exactly one Jump"),Starts,Before+1);
        F.Key(EKeys::SpaceBar,IE_Released);
    }
    return true;
}

// Fails when Results swallows configured LMB outside its drawn button.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootShellMouseRetryTest,"Bootloop.V03.ShellInput.ResultsConfiguredMouseRetryOnce",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootShellMouseRetryTest::RunTest(const FString&)
{
    if(!BootShellInputV03::AuditOnly(*this)) return false;
    BootShellInputV03::FFixture F; using A=FBootShellInputTestAccess;
    for(FKey Key:{EKeys::LeftMouseButton,EKeys::RightMouseButton,EKeys::ThumbMouseButton,EKeys::R})
    {
        F.PC->ShowInputs(); A::Areas(F.Shell,{332});
        A::Down(F.Shell,EKeys::Enter); A::Up(F.Shell,EKeys::Enter);
        if(Key.IsMouseButton()) A::Click(F.Shell,Key); else A::Down(F.Shell,Key);
        // RMB is Snap secondary by default: choose explicit Swap through the visible modal.
        if(A::Draft(F.Shell).bConflict) { A::Areas(F.Shell,{354,355}); A::Click(F.Shell,EKeys::LeftMouseButton,FVector2D(110,110)); }
        A::Areas(F.Shell,{353}); A::Click(F.Shell,EKeys::LeftMouseButton,FVector2D(110,110));
        TestTrue(TEXT("Applied Retry key comes from real capture"),F.GI->GetInputPreferences().GetKey(EBootInputAction::Retry,0)==Key);
        F.PC->ShowPage(EBootPage::Results); A::Areas(F.Shell,{1,6});
        const int32 Before=F.Mode->GetAttemptGeneration();
        if(Key.IsMouseButton()) A::Click(F.Shell,Key); else A::Down(F.Shell,Key);
        TestEqual(TEXT("Advertised Retry outside buttons restarts exactly once"),F.Mode->GetAttemptGeneration(),Before+1);
        F.Start();
        if(Key==EKeys::LeftMouseButton)
        {
            F.PC->ShowPage(EBootPage::Results); A::Areas(F.Shell,{6});
            const int32 BeforeDesktop=F.Mode->GetAttemptGeneration();
            A::Click(F.Shell,Key,FVector2D(110,110));
            TestEqual(TEXT("Explicit Desktop control keeps its visible action with LMB Retry"),F.PC->GetPage(),EBootPage::Desktop);
            TestEqual(TEXT("Desktop control does not also Retry"),F.Mode->GetAttemptGeneration(),BeforeDesktop);
            F.Start();
        }
    }
    return true;
}

// Fails if Runner remembers only the most recently used component rather than each bound component.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootShellInputReuseTest,"Bootloop.V03.ShellInput.ReusedInputComponentBindsOnce",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootShellInputReuseTest::RunTest(const FString&)
{
    if(!BootShellInputV03::AuditOnly(*this)) return false;
    BootShellInputV03::FFixture F;
    const int32 Original=F.Input->GetActionEventBindings().Num();
    auto* Replacement=NewObject<UEnhancedInputComponent>(F.PC);
    F.Runner->SetupPlayerInputComponent(Replacement); F.Runner->SetupPlayerInputComponent(F.Input);
    TestEqual(TEXT("A B A setup retains one binding set on A"),F.Input->GetActionEventBindings().Num(),Original);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootShellSurfPromptTest,"Bootloop.V03.ShellInput.ContextualSurfPrompts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootShellSurfPromptTest::RunTest(const FString&)
{
    if(!BootShellInputV03::AuditOnly(*this)) return false;
    BootShellInputV03::FFixture F;
    auto* Window=F.World->SpawnActor<ABootSurfWindow>();
    Window->Configure(FVector(10000,0,0),FVector(10600,0,160),1.45f,99001);
    auto* Popup=F.World->SpawnActor<ABootPopupProcess>();
    Popup->SetRunActive(true);
    Popup->ConfigurePopup(FTransform(FVector(12000,0,0)),FVector(200,300,0),FVector(12700,380,160),1.45f,99002,1);
    FBootInputPreferences Inputs;
    Inputs.Assign(EBootInputAction::Snap,0,EKeys::Delete,false);
    F.Runner->SetBase(static_cast<UPrimitiveComponent*>(nullptr));
    const FString Board=FBootShellInputTestAccess::Prompt(F.Shell,Window,F.Runner,Inputs);
    TestTrue(TEXT("Off-support ready window offers BOARD"),Board.EndsWith(TEXT(" > BOARD")));
    TestTrue(TEXT("Context label uses current remapped SNAP binding"),Board.StartsWith(Inputs.Label(EBootInputAction::Snap).ToString()));
    F.Runner->SetBase(Cast<UPrimitiveComponent>(Window->GetRootComponent()));
    TestTrue(TEXT("Current support offers SNAP"),FBootShellInputTestAccess::Prompt(F.Shell,Window,F.Runner,Inputs).EndsWith(TEXT(" > SNAP")));
    TestTrue(TEXT("Hostile popup offers INTERCEPT"),FBootShellInputTestAccess::Prompt(F.Shell,Popup,F.Runner,Inputs).EndsWith(TEXT(" > INTERCEPT")));
    return true;
}
#endif

