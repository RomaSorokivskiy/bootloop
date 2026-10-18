#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Movement/BootMovementComponent.h"
#include "Player/BootRunner.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"

namespace
{
    // Real CharacterMovement, collision scene and world ticks: restoring launch
    // speed, accumulating short landings or skipping sweeps must fail these tests.
    struct FBootRedirectWorld
    {
        UWorld* World = nullptr;
        ABootRunner* Runner = nullptr;
        UBootMovementComponent* Move = nullptr;

        FBootRedirectWorld()
        {
            UWorld::InitializationValues Init;
            Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
                .CreateAISystem(false).ShouldSimulatePhysics(true).SetTransactional(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
                true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Box(FVector(0, 0, -50), FVector(1000, 1000, 1));
            Runner = World->SpawnActor<ABootRunner>(FVector(0, 0, 100), FRotator::ZeroRotator);
            Move = Cast<UBootMovementComponent>(Runner->GetCharacterMovement());
            Move->bRunPhysicsWithNoController = true;
            World->InitializeActorsForPlay(FURL());
            World->BeginPlay();
            World->GetWorldSettings()->NotifyBeginPlay();
        }

        ~FBootRedirectWorld()
        {
            World->EndPlay(EEndPlayReason::Quit);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        AStaticMeshActor* Box(FVector Location, FVector Scale, bool bRunnable = false)
        {
            auto* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
            auto* Mesh = Actor->GetStaticMeshComponent();
            Mesh->SetMobility(EComponentMobility::Movable);
            Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
            Mesh->SetCollisionProfileName(TEXT("BlockAll"));
            if (bRunnable) Mesh->ComponentTags.Add(TEXT("BootWallRun"));
            Actor->SetActorScale3D(Scale);
            return Actor;
        }

        void Step(int32 Count, float Dt = 1.f / 60.f, ELevelTick Tick = LEVELTICK_All)
        {
            for (int32 I = 0; I < Count; ++I)
            {
                ++GFrameCounter;
                World->Tick(Tick, Dt);
            }
        }

        void Air(FVector Velocity = FVector(900, 0, 123), FVector Location = FVector(0, 0, 5000))
        {
            Move->ResetParkour();
            Runner->UnCrouch();
            Runner->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
            Move->SetMovementMode(MOVE_Falling);
            Move->Velocity = Velocity;
        }

        void Ground()
        {
            Runner->SetActorLocation(FVector(0, 0, 98), false, nullptr, ETeleportType::TeleportPhysics);
            Move->Velocity = FVector::ZeroVector;
            Move->SetMovementMode(MOVE_Falling);
            for (int32 I = 0; I < 12 && !Move->IsMovingOnGround(); ++I) Step(1, .01f);
        }

        float Yaw() const
        {
            return FMath::RadiansToDegrees(FMath::Atan2(
                static_cast<float>(Move->Velocity.Y), static_cast<float>(Move->Velocity.X)));
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootAirRedirectCapsTest,
    "Bootloop.V03.Movement.AirRedirectCapsAndCollision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBootAirRedirectCapsTest::RunTest(const FString& Parameters)
{
    for (const int32 Fps : {30, 60, 120})
    {
        FBootRedirectWorld Scene;
        auto* Move = Scene.Move;
        const float Dt = 1.f / Fps;
        Scene.Air();
        Move->GravityScale = 0.f; // Isolate redirect Z from ordinary gravity.
        const int32 Serial = Move->GetAirRedirectSerial();
        TestTrue(TEXT("Airborne900cm/s accepts one redirect"), Move->TryAirRedirect(FVector(0, 1, 50)));
        TestTrue(TEXT("Accepted redirect is spent and turning"), !Move->IsAirRedirectReady() && Move->IsAirRedirecting());
        TestEqual(TEXT("Acceptance publishes exactly one presentation event"), Move->GetAirRedirectSerial(), Serial + 1);
        TestFalse(TEXT("Repeated input cannot queue a second target"), Move->TryAirRedirect(FVector(0, -1, 0)));
        Scene.Step(Fps / 30, Dt); // One thirtieth second at every tested rate.
        TestTrue(TEXT("Turn interpolates instead of snapping"), Scene.Yaw() > 5.f && Scene.Yaw() < 40.f);
        TestTrue(TEXT("Turn does not modify vertical velocity"), FMath::IsNearlyEqual(Move->Velocity.Z, 123., .01));
        Scene.Step(Fps / 15, Dt);
        TestTrue(TEXT("Turn is still interpolating at0.10seconds"), Move->IsAirRedirecting());
        Scene.Step(Fps / 30, Dt);
        TestTrue(TEXT("Frozen input target caps the heading at60degrees"), FMath::IsNearlyEqual(Scene.Yaw(), 60.f, .1f));
        TestTrue(TEXT("Redirect preserves900cm/s horizontal magnitude"), FMath::IsNearlyEqual(Move->Velocity.Size2D(), 900., .1));
        TestFalse(TEXT("0.12second maneuver completes"), Move->IsAirRedirecting());
        TestFalse(TEXT("Finishing does not refill charge"), Move->IsAirRedirectReady());
        TestEqual(TEXT("Rejected inputs do not publish events"), Move->GetAirRedirectSerial(), Serial + 1);

        Scene.Air();
        TestTrue(TEXT("A smaller desired turn is accepted"), Move->TryAirRedirect(FVector(.8660254, -.5, 0)));
        Scene.Step(Fps / 5, Dt);
        TestTrue(TEXT("A30degree input ends at minus30degrees"), FMath::IsNearlyEqual(Scene.Yaw(), -30.f, .1f));

        Scene.Air();
        TestFalse(TEXT("Less than5degrees is a no-op"), Move->TryAirRedirect(FVector(1, .06, 0)));
        TestFalse(TEXT("Vertical-only input is a no-op"), Move->TryAirRedirect(FVector(0, 0, 1)));
        TestTrue(TEXT("No-op inputs retain charge"), Move->IsAirRedirectReady());
        Move->Velocity = FVector(299, 0, 123);
        TestFalse(TEXT("Below300cm/s cannot manufacture a turn"), Move->TryAirRedirect(FVector::RightVector));
        TestTrue(TEXT("Slow rejection retains charge"), Move->IsAirRedirectReady());
        Move->Velocity = FVector(300, 0, 123);
        TestTrue(TEXT("300cm/s threshold is inclusive"), Move->TryAirRedirect(FVector::RightVector));

        Scene.Air(FVector(1600, 0, 0));
        Move->GrantOverclock();
        TestTrue(TEXT("Overclock redirect accepted"), Move->TryAirRedirect(FVector::RightVector));
        Scene.Step(Fps / 5, Dt);
        TestTrue(TEXT("Turning never restores decayed overclock"), Move->Velocity.Size2D() < 1755.);
        Scene.Step(Fps, Dt);
        TestTrue(TEXT("Boost returns to1600 after turn"), FMath::IsNearlyEqual(Move->Velocity.Size2D(), 1600., 1.));

        Scene.Air(FVector(900, 0, 0));
        Move->BrakingDecelerationFalling = 1200.f;
        TestTrue(TEXT("Braking redirect accepted"), Move->TryAirRedirect(FVector::RightVector));
        Scene.Step(Fps / 5, Dt);
        TestTrue(TEXT("Braking loss is not restored by heading rotation"), Move->Velocity.Size2D() < 725.);
        Move->BrakingDecelerationFalling = 0.f;

        // Front face at75cm leaves41cm from the capsule center to impact,
        // reached during the0.12s turn even at the30FPS sampling interval.
        Scene.Box(FVector(80, 0, 5000), FVector(.1, 100, 100));
        Scene.Air(FVector(900, 0, 0));
        TestTrue(TEXT("Front collision maneuver starts"), Move->TryAirRedirect(FVector::RightVector));
        Scene.Step(Fps / 15, Dt);
        const float CollisionSpeed = static_cast<float>(Move->Velocity.Size2D());
        TestTrue(TEXT("Mid-turn collision removes forward speed"), CollisionSpeed < 700.f);
        TestTrue(TEXT("Swept redirect cannot cross front wall"), Scene.Runner->GetActorLocation().X < 65.);
        Scene.Step(Fps / 5, Dt);
        TestTrue(TEXT("Remaining turn never restores pre-collision magnitude"), Move->Velocity.Size2D() <= CollisionSpeed + 1.);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootAirRedirectRechargeTest,
    "Bootloop.V03.Movement.RechargeAndReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBootAirRedirectRechargeTest::RunTest(const FString& Parameters)
{
    FBootRedirectWorld Scene;
    auto* Move = Scene.Move;
    Scene.Step(30);
    Move->Velocity = FVector(900, 0, 0);
    TestTrue(TEXT("Fixture has walkable ground"), Move->IsMovingOnGround());
    TestFalse(TEXT("Ground rejects redirect"), Move->TryAirRedirect(FVector::RightVector));
    Scene.Air();
    Scene.Runner->Crouch();
    Scene.Step(1);
    TestTrue(TEXT("Fixture crouches in air"), Scene.Runner->bIsCrouched);
    TestFalse(TEXT("Crouch/air slide rejects redirect"), Move->TryAirRedirect(FVector::RightVector));
    Scene.Runner->UnCrouch();
    Scene.Step(1);

    Scene.Box(FVector(2000, 100, 500), FVector(60, 1, 10), true);
    Scene.Air(FVector(1000, 0, 0), FVector(0, 0, 400));
    Scene.Step(2);
    TestTrue(TEXT("Fixture wallrun starts"), Move->IsWallRunning());
    TestFalse(TEXT("Wallrun rejects redirect"), Move->TryAirRedirect(FVector::RightVector));
    TestTrue(TEXT("Wallkick leaves wall"), Move->TryWallKick());
    TestTrue(TEXT("Wallkick keeps unspent redirect"), Move->TryAirRedirect(FVector::ForwardVector));
    Scene.Step(20);
    TestFalse(TEXT("Wall contact/kick never recharges spent redirect"), Move->IsAirRedirectReady());

    Scene.Air();
    TestTrue(TEXT("New airborne turn accepted"), Move->TryAirRedirect(FVector::RightVector));
    Scene.Step(1, .02f);
    const FVector BeforePause = Move->Velocity;
    const FVector PositionBeforePause = Scene.Runner->GetActorLocation();
    const double TimeBeforePause = Scene.World->GetTimeSeconds();
    const float YawBeforePause = Scene.Yaw();
    const int32 SerialBeforePause = Move->GetAirRedirectSerial();
    // Tick's enum is not a pause request. UWorld::IsPaused reads the same
    // PauserPlayerState that the engine's GameMode pause path installs. This
    // isolated world has no local GameInstance/controller for SetGamePaused.
    auto* Pauser = Scene.World->SpawnActor<APlayerState>();
    Scene.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
    TestTrue(TEXT("Engine world pause is active"), Scene.World->IsPaused());
    Scene.Step(30, 1.f / 30.f);
    TestTrue(TEXT("Paused world does not rotate velocity"), Move->Velocity.Equals(BeforePause, .001));
    TestTrue(TEXT("Paused world does not move capsule"), Scene.Runner->GetActorLocation().Equals(PositionBeforePause, .001));
    TestEqual(TEXT("Pause freezes gameplay time"), Scene.World->GetTimeSeconds(), TimeBeforePause);
    TestEqual(TEXT("Pause does not publish another acceptance event"), Move->GetAirRedirectSerial(), SerialBeforePause);
    TestFalse(TEXT("Pause does not refill spent charge"), Move->IsAirRedirectReady());
    TestTrue(TEXT("Pause preserves unfinished turn"), Move->IsAirRedirecting());
    Scene.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    TestFalse(TEXT("Engine world resumes"), Scene.World->IsPaused());
    Scene.Step(1, .02f);
    TestTrue(TEXT("Resuming advances the same turn"), Scene.Yaw() > YawBeforePause + 5.f && Move->IsAirRedirecting());

    Scene.Ground();
    TestFalse(TEXT("Landing ends interpolation immediately"), Move->IsAirRedirecting());
    Scene.Step(5, .01f);
    TestFalse(TEXT("A brief walkable touch does not recharge"), Move->IsAirRedirectReady());
    Scene.Runner->SetActorLocation(FVector(0, 0, 5000), false, nullptr, ETeleportType::TeleportPhysics);
    Move->SetMovementMode(MOVE_Falling);
    Scene.Step(1, .01f);
    Scene.Ground();
    Scene.Step(5, .01f);
    TestFalse(TEXT("Broken contact intervals cannot accumulate"), Move->IsAirRedirectReady());
    Scene.Step(4, .01f);
    TestTrue(TEXT("Continuous0.08second walkable contact recharges"), Move->IsAirRedirectReady());

    for (int32 Reset = 0; Reset < 10; ++Reset)
    {
        Scene.Air();
        TestTrue(TEXT("Reset starts ready"), Move->IsAirRedirectReady());
        TestTrue(TEXT("Reset permits a fresh turn"), Move->TryAirRedirect(FVector::RightVector));
        Scene.Step(1);
        const int32 Serial = Move->GetAirRedirectSerial();
        Move->ResetParkour();
        TestFalse(TEXT("Reset cancels turn"), Move->IsAirRedirecting());
        TestTrue(TEXT("Reset clears charge and partial recharge history"), Move->IsAirRedirectReady());
        TestTrue(TEXT("Reset stops accumulated momentum"), Move->Velocity.IsNearlyZero());
        TestEqual(TEXT("Reset does not create an acceptance event"), Move->GetAirRedirectSerial(), Serial);
    }
    return true;
}
#endif
