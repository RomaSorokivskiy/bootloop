#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Player/BootRunner.h"
#include "Movement/BootMovementComponent.h"
#include "Input/BootInputMappingComponent.h"
#include "Run/BootGameMode.h"
#include "Core/BootGameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "TimerManager.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootRunnerActionsV04,"Bootloop.V04.Input.RunnerActions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootRunnerActionsV04::RunTest(const FString&)
{
    UWorld::InitializationValues Init; Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* GI=NewObject<UBootGameInstance>(GEngine); GI->InitializeStandalone();
    UWorld* Dummy=GI->GetWorld();
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    World->SetGameInstance(GI); GI->GetWorldContext()->SetCurrentWorld(World); Dummy->DestroyWorld(false);
    World->GetWorldSettings()->DefaultGameMode=ABootFirstRunGameMode::StaticClass(); World->SetGameMode(FURL());
    auto* Runner=World->SpawnActor<ABootRunner>();
    auto* PC=World->SpawnActor<APlayerController>(); PC->SetActorTickEnabled(false); PC->SetAsLocalPlayerController(); PC->Possess(Runner);
    auto* Local=NewObject<ULocalPlayer>(GEngine); Local->PlayerController=PC; PC->Player=Local;
    GI->AddLocalPlayer(Local,FPlatformUserId::CreateFromInternalId(0));
    auto* Input=NewObject<UEnhancedInputComponent>(Runner);
    Runner->SetupPlayerInputComponent(Input); Runner->SetupPlayerInputComponent(Input);
    for(auto Action:{EBootInputAction::CardOne,EBootInputAction::CardTwo,EBootInputAction::CardThree,EBootInputAction::BufferDash})
    {
        int32 Count=0;
        for(const auto& Binding:Input->GetActionEventBindings())
            if(Binding->GetAction()==Runner->GetInputMapping()->GetAction(Action) && Binding->GetTriggerEvent()==ETriggerEvent::Started) ++Count;
        TestEqual(TEXT("one fresh Started binding per run action after repeated setup"),Count,1);
    }
    World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
    auto* Mode=World->GetAuthGameMode<ABootGameMode>(); Mode->StartRun();
    ++GFrameCounter; World->GetTimerManager().Tick(.001f);
    ++GFrameCounter; World->GetTimerManager().Tick(1.2f);
    TestTrue(TEXT("real first-run boot finishes"),Mode->GetRunState()==EBootRunState::Running);
    auto* Movement=Runner->GetParkour(); Movement->ResetParkour();
    auto Envelope=FBootTraversalPolicy::FromTempo(130.,123); Envelope.DashCharges=1;
    Movement->ApplyTraversalEnvelope(Envelope); Movement->SetMovementMode(MOVE_Falling); Movement->Velocity=FVector(1100,0,0);
    const int32 Before=Movement->GetDashSerial();
    Runner->RequestBufferDash();
    TestEqual(TEXT("runner request reaches accepted airborne dash"),Movement->GetDashSerial(),Before+1);
    TestEqual(TEXT("accepted dash consumes one charge"),Movement->GetDashCharges(),0);
    Runner->RequestBufferDash();
    TestEqual(TEXT("empty charge cannot emit another accepted dash"),Movement->GetDashSerial(),Before+1);
    Mode->ReturnToDesktop();
    World->EndPlay(EEndPlayReason::Quit);
    GI->RemoveLocalPlayer(Local); Local->PlayerController=nullptr; PC->Player=nullptr; GI->Shutdown();
    GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}
#endif
