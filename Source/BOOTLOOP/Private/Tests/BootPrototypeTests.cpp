#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Player/BootRunner.h"
#include "Music/BootRhythmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "UObject/UObjectHash.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootPrototypeLifecycleTest,
    "Bootloop.Prototype.MovementSlideAndRetry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBootPrototypeLifecycleTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues Init;
    Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(true).SetTransactional(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
        true, ERHIFeatureLevel::Num, &Init);
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto Box = [&](FVector Location, FVector Scale)
    {
        AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
        Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
        Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        Actor->SetActorScale3D(Scale);
        return Actor;
    };
    Box(FVector(0, 0, -50), FVector(200, 200, 1));
    ABootRunner* Runner = World->SpawnActor<ABootRunner>(FVector(0, 0, 100), FRotator::ZeroRotator);
    Runner->GetCharacterMovement()->bRunPhysicsWithNoController = true;
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();
    // This isolated world has no GameMode to forward StartPlay to WorldSettings.
    World->GetWorldSettings()->NotifyBeginPlay();
    const FVector SpawnLocation = Runner->GetActorLocation();
    auto Step = [&](int32 Frames)
    {
        for (int32 Frame = 0; Frame < Frames; ++Frame)
        {
            // TickFunction/TimerManager suppress duplicate ticks within one engine frame.
            ++GFrameCounter;
            World->Tick(LEVELTICK_All, 1.f / 60.f);
        }
    };
    Step(30);
    TestFalse(TEXT("No audio device creates no pretend rhythm clock"), Runner->GetRhythm()->IsReady());
    AddInfo(FString::Printf(TEXT("Ground: %s velocity=%s begun=%d time=%f"),
        *Runner->GetActorLocation().ToString(), *Runner->GetVelocity().ToString(),
        Runner->HasActorBegunPlay(), World->GetTimeSeconds()));
    TestTrue(TEXT("Runner grounded"), Runner->GetCharacterMovement()->IsMovingOnGround());
    Runner->RequestJump();
    Step(6);
    AddInfo(FString::Printf(TEXT("Jump: %s velocity=%s"), *Runner->GetActorLocation().ToString(), *Runner->GetVelocity().ToString()));
    TestTrue(TEXT("Jump lifts capsule"), Runner->GetActorLocation().Z > 130.f);
    const float RisingVelocity = Runner->GetVelocity().Z;
    Runner->RequestJump();
    Step(1);
    TestTrue(TEXT("Repeated jump cannot reset airborne velocity"), Runner->GetVelocity().Z < RisingVelocity);
    Step(70);
    Runner->GetCharacterMovement()->Velocity = FVector(900, 0, 0);
    Runner->RequestJump();
    Step(6);
    Runner->StartSlide();
    Step(2);
    TestTrue(TEXT("Air slide input immediately ducks capsule"), Runner->bIsCrouched);
    TestFalse(TEXT("Air duck is not a scored ground slide"), Runner->IsSliding());
    Step(43);
    TestTrue(TEXT("Air slide request survives until landing"), Runner->IsSliding());
    Step(70);
    Runner->ResetRun();
    Step(30);
    Runner->StartSlide();
    Step(2);
    TestTrue(TEXT("Slow movement can still duck under an obstacle"), Runner->bIsCrouched);
    TestFalse(TEXT("Stationary crouch is not a slide"), Runner->IsSliding());
    Step(45);
    Runner->ResetRun();
    Step(30);
    AStaticMeshActor* Ceiling = Box(FVector(600, 0, 155), FVector(35, 10, .9));
    Runner->GetCharacterMovement()->Velocity = FVector(900, 0, 0);
    Runner->StartSlide();
    Step(2);
    AddInfo(FString::Printf(TEXT("Slide: %s velocity=%s grounded=%d crouched=%d"),
        *Runner->GetActorLocation().ToString(), *Runner->GetVelocity().ToString(),
        Runner->GetCharacterMovement()->IsMovingOnGround(), Runner->bIsCrouched));
    TestTrue(TEXT("Slide enters crouch"), Runner->bIsCrouched);
    TestTrue(TEXT("Slide active"), Runner->IsSliding());
    Step(43);
    AddInfo(FString::Printf(TEXT("After slide: %s velocity=%s ceiling=%s scale=%s crouched=%d"),
        *Runner->GetActorLocation().ToString(), *Runner->GetVelocity().ToString(),
        *Ceiling->GetActorLocation().ToString(), *Ceiling->GetActorScale3D().ToString(), Runner->bIsCrouched));
    TestFalse(TEXT("Slide timer ends"), Runner->IsSliding());
    TestTrue(TEXT("Ceiling prevents standing"), Runner->bIsCrouched);
    TestTrue(TEXT("Short capsule retained"), Runner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() <= 44.1f);
    Ceiling->Destroy();
    Step(6);
    TestFalse(TEXT("Runner stands when ceiling clears"), Runner->bIsCrouched);
    for (int32 Attempt = 0; Attempt < 10; ++Attempt)
    {
        Runner->Die();
        Runner->Die();
        TestTrue(TEXT("Death is idempotent"), Runner->IsDead());
        TestTrue(TEXT("Death stops velocity"), Runner->GetVelocity().IsNearlyZero());
        Runner->ResetRun();
        TestFalse(TEXT("Retry clears death"), Runner->IsDead());
        TestFalse(TEXT("Retry clears slide"), Runner->IsSliding());
        TestTrue(TEXT("Retry resets velocity"), Runner->GetVelocity().IsNearlyZero());
        TestTrue(TEXT("Retry resets location"), Runner->GetActorLocation().Equals(SpawnLocation, .1f));
        TestEqual(TEXT("Retry resets rhythm score"), Runner->GetRhythm()->GetScore().Score, 0);
        TestEqual(TEXT("Retry resets rhythm combo"), Runner->GetRhythm()->GetScore().Combo, 0);
    }
    Runner->SetActorLocation(FVector(0, 0, -900));
    Step(1);
    TestTrue(TEXT("Fall terminates run"), Runner->IsDead());
    Runner->ResetRun();
    Step(30);
    Runner->RequestJump();
    Runner->ResetRun();
    Step(1);
    TestTrue(TEXT("Retry clears a launch queued in the same frame"), Runner->GetVelocity().Z <= 0.f);
    APlayerController* PC = World->SpawnActor<APlayerController>();
    // Synthetic input is processed explicitly; there is no viewport/local player to tick.
    PC->SetActorTickEnabled(false);
    PC->SetAsLocalPlayerController();
    PC->Possess(Runner);
    UEnhancedInputComponent* Input = NewObject<UEnhancedInputComponent>(Runner);
    Runner->SetupPlayerInputComponent(Input);
    UInputAction* MoveAction = nullptr;
    ForEachObjectWithOuter(Runner, [&](UObject* Object)
    {
        if (UInputAction* Action = Cast<UInputAction>(Object))
            if (Action->ValueType == EInputActionValueType::Axis2D) MoveAction = Action;
    });
    if (TestNotNull(TEXT("Runner creates a movement action"), MoveAction))
    {
        UEnhancedPlayerInput* PlayerInput = NewObject<UEnhancedPlayerInput>(PC);
        PC->PlayerInput = PlayerInput;
        TArray<UInputComponent*> Stack{Input};
        for (const FVector2D Direction : {FVector2D(0, 1), FVector2D(1, 0)})
        {
            PlayerInput->InjectInputForAction(MoveAction, FInputActionValue(Direction));
            PlayerInput->InjectInputForAction(MoveAction, FInputActionValue(-Direction));
            PlayerInput->ProcessInputStack(Stack, 1.f / 60.f, false);
            TestTrue(TEXT("Opposing movement inputs cancel"), PlayerInput->GetActionValue(MoveAction).Get<FVector2D>().IsNearlyZero());
        }
        UInputAction* WheelAction = nullptr;
        ForEachObjectWithOuter(Runner, [&](UObject* Object)
        {
            if (const UInputMappingContext* IMC = Cast<UInputMappingContext>(Object))
                for (const auto& KeyMapping : IMC->GetMappings())
                    if (KeyMapping.Key == EKeys::MouseWheelAxis)
                        WheelAction = const_cast<UInputAction*>(KeyMapping.Action.Get());
        });
        if (TestNotNull(TEXT("Wheel axis mapped for both scroll directions"), WheelAction))
        {
            Runner->ResetRun();
            Step(30);
            PlayerInput->InjectInputForAction(WheelAction, FInputActionValue(1.f));
            PlayerInput->ProcessInputStack(Stack, 1.f / 60.f, false);
            Step(6);
            AddInfo(FString::Printf(TEXT("Wheel: action=%f location=%s velocity=%s grounded=%d cursor=%d local=%d"),
                PlayerInput->GetActionValue(WheelAction).Get<float>(), *Runner->GetActorLocation().ToString(),
                *Runner->GetVelocity().ToString(), Runner->GetCharacterMovement()->IsMovingOnGround(),
                PC->bShowMouseCursor, PC->IsLocalController()));
            TestTrue(TEXT("Wheel pulse triggers jump"), Runner->GetVelocity().Z > 0);
            PlayerInput->ProcessInputStack(Stack, 1.f / 60.f, false);
            Step(80);
            TestTrue(TEXT("No fresh wheel pulse means no automatic bhop"), Runner->GetCharacterMovement()->IsMovingOnGround());
            Runner->GetCharacterMovement()->Velocity = FVector(900, 0, 0);
            Runner->RequestJump();
            Step(39);
            PlayerInput->InjectInputForAction(WheelAction, FInputActionValue(-1.f));
            PlayerInput->ProcessInputStack(Stack, 1.f / 60.f, false);
            Step(8);
            TestTrue(TEXT("Wheel buffer performs next jump after landing"), Runner->GetVelocity().Z > 0);
            TestTrue(TEXT("Manual bhop retains horizontal momentum"), Runner->GetVelocity().Size2D() >= 850.f);
        }
    }
    World->EndPlay(EEndPlayReason::Quit);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
