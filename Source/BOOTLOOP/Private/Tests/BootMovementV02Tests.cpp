#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Movement/BootMovementComponent.h"
#include "Player/BootRunner.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

namespace
{
    struct FBootParkourWorld
    {
        UWorld* World = nullptr;
        ABootRunner* Runner = nullptr;
        UBootMovementComponent* Movement = nullptr;

        FBootParkourWorld()
        {
            UWorld::InitializationValues Init;
            Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
                .CreateAISystem(false).ShouldSimulatePhysics(true).SetTransactional(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
                true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Box(FVector(0, 0, -50), FVector(1000, 1000, 1));
            Runner = World->SpawnActor<ABootRunner>(FVector(0, 0, 100), FRotator::ZeroRotator);
            Movement = Cast<UBootMovementComponent>(Runner->GetCharacterMovement());
            if (Movement) Movement->bRunPhysicsWithNoController = true;
            World->InitializeActorsForPlay(FURL());
            World->BeginPlay();
            World->GetWorldSettings()->NotifyBeginPlay();
        }

        ~FBootParkourWorld()
        {
            World->EndPlay(EEndPlayReason::Quit);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        AStaticMeshActor* Box(FVector Location, FVector Scale, bool bRunnable = false)
        {
            AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
            UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
            Mesh->SetMobility(EComponentMobility::Movable);
            Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
            Mesh->SetCollisionProfileName(TEXT("BlockAll"));
            if (bRunnable) Mesh->ComponentTags.Add(TEXT("BootWallRun"));
            Actor->SetActorScale3D(Scale);
            return Actor;
        }

        void Step(int32 Frames, bool bForward = false, float Dt = 1.f / 60.f)
        {
            for (int32 Frame = 0; Frame < Frames; ++Frame)
            {
                if (bForward) Runner->AddMovementInput(FVector::ForwardVector, 1.f, true);
                ++GFrameCounter;
                World->Tick(LEVELTICK_All, Dt);
            }
        }

        bool JumpAndLand()
        {
            Runner->RequestJump();
            Step(2, true);
            const bool bLifted = Movement->IsFalling();
            for (int32 Frame = 0; Frame < 100 && !Movement->IsMovingOnGround(); ++Frame) Step(1, true);
            return bLifted && Movement->IsMovingOnGround();
        }

        void AirborneAt(FVector Location, FVector Velocity)
        {
            Movement->ResetParkour();
            Runner->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
            Movement->SetMovementMode(MOVE_Falling);
            Movement->Velocity = Velocity;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootManualBhopV02Test,
    "Bootloop.V02.ManualBhopAndTemporaryOverclock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBootManualBhopV02Test::RunTest(const FString& Parameters)
{
    FBootParkourWorld Scene;
    if (!TestNotNull(TEXT("Runner installs the dedicated parkour component"), Scene.Movement)) return false;
    auto* Movement = Scene.Movement;
    Scene.Step(30);
    TestTrue(TEXT("Fixture starts grounded"), Movement->IsMovingOnGround());
    Scene.Runner->RequestJump();
    Scene.Step(65);
    TestTrue(TEXT("One input returns to the floor without an automatic next hop"), Movement->IsMovingOnGround());
    TestTrue(TEXT("Stationary jumps do not manufacture horizontal speed"), Movement->Velocity.Size2D() < 1.f);
    Scene.Step(60, true);
    TestTrue(TEXT("Ordinary held run reaches the900cm/s base"), FMath::IsNearlyEqual(
        static_cast<float>(Movement->Velocity.Size2D()), 900.f, 2.f));
    TestTrue(TEXT("First deliberate running jump lands"), Scene.JumpAndLand());
    const float FirstLandingSpeed = static_cast<float>(Movement->Velocity.Size2D());
    Scene.Runner->RequestJump();
    Scene.Step(2, true);
    TestTrue(TEXT("Fresh landing plus a new jump input adds bhop speed"), Movement->Velocity.Size2D() > FirstLandingSpeed + 70.f);
    for (int32 Hop = 0; Hop < 9; ++Hop)
    {
        for (int32 Frame = 0; Frame < 100 && !Movement->IsMovingOnGround(); ++Frame) Scene.Step(1, true);
        Scene.Runner->RequestJump();
        Scene.Step(2, true);
    }
    TestTrue(TEXT("Repeated deliberate hops reach the ordinary1600cm/s cap"), Movement->Velocity.Size2D() >= 1590.f);
    TestTrue(TEXT("Bhop never exceeds its ordinary cap"), Movement->Velocity.Size2D() <= 1600.1f);
    for (int32 Frame = 0; Frame < 100 && !Movement->IsMovingOnGround(); ++Frame) Scene.Step(1, true);
    Scene.Step(45, true);
    TestTrue(TEXT("Missing the next hop settles back to normal running speed"), Movement->Velocity.Size2D() <= 905.f);

    // A tall clear fall isolates the temporary impulse from floor friction.
    Scene.AirborneAt(FVector(0, 4000, 5000), FVector(1600, 0, 0));
    Movement->GrantOverclock();
    TestTrue(TEXT("Overclock briefly adds speed up to1800cm/s"), FMath::IsNearlyEqual(
        static_cast<float>(Movement->Velocity.Size2D()), 1800.f, .1f));
    Scene.Step(20, false, 1.f / 30.f);
    TestTrue(TEXT("Boost visibly decays even with no ground friction"), Movement->Velocity.Size2D() < 1630.f);
    Scene.Step(6, false, 1.f / 30.f);
    TestTrue(TEXT("Temporary speed is removed instead of becoming permanent momentum"), FMath::IsNearlyEqual(
        static_cast<float>(Movement->Velocity.Size2D()), 1600.f, 1.f));
    Movement->GrantOverclock();
    Scene.Runner->ResetRun();
    Scene.Step(30);
    TestTrue(TEXT("Retry clears the impulse and horizontal momentum"), Movement->Velocity.Size2D() < 1.f);
    Scene.Step(60, true);
    TestTrue(TEXT("Retry cannot retain a raised speed limit"), Movement->Velocity.Size2D() <= 901.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootWallRunV02Test,
    "Bootloop.V02.WallRunKickAndCollision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBootWallRunV02Test::RunTest(const FString& Parameters)
{
    FBootParkourWorld Scene;
    if (!TestNotNull(TEXT("Runner installs the dedicated parkour component"), Scene.Movement)) return false;
    auto* Movement = Scene.Movement;
    Scene.Step(30);
    AStaticMeshActor* Wall = Scene.Box(FVector(2000, 100, 500), FVector(60, 1, 10));
    Scene.AirborneAt(FVector(0, 0, 400), FVector(1000, 0, 0));
    Scene.Step(3);
    TestFalse(TEXT("Ordinary untagged geometry never starts wallrun"), Movement->IsWallRunning());
    Wall->GetStaticMeshComponent()->ComponentTags.Add(TEXT("BootWallRun"));
    Scene.AirborneAt(FVector(0, 0, 400), FVector(1000, 0, 0));
    Scene.Step(3);
    TestTrue(TEXT("A tagged solid component enters wallrun"), Movement->IsWallRunning());
    TestTrue(TEXT("Right wall reports a positive presentation side"), Movement->GetWallSide() > 0.f);
    TestTrue(TEXT("Wallrun moves forward on the actual collision wall"), Scene.Runner->GetActorLocation().X > 35.f);
    TestTrue(TEXT("Wallrun applies reduced gravity"), Movement->Velocity.Z > -60.f);
    TestTrue(TEXT("Fresh wallkick input is handled"), Movement->TryWallKick());
    TestFalse(TEXT("Wallkick leaves the wallrun mode immediately"), Movement->IsWallRunning());
    TestTrue(TEXT("Wallkick pushes away from the right wall"), Movement->Velocity.Y < -300.f);
    TestTrue(TEXT("Wallkick has positive lift"), Movement->Velocity.Z > 400.f);
    TestFalse(TEXT("Repeated input cannot wallkick again in free air"), Movement->TryWallKick());

    // Force the capsule close again during the same airtime to test the exploit.
    Scene.Runner->SetActorLocation(FVector(200, 0, 400), false, nullptr, ETeleportType::TeleportPhysics);
    Movement->Velocity = FVector(1000, 0, 0);
    Scene.Step(25);
    TestFalse(TEXT("The same wall cannot be used for repeated airborne climbing"), Movement->IsWallRunning());

    Scene.AirborneAt(FVector(0, 0, 400), FVector(1000, 0, 0));
    Scene.Step(2);
    TestTrue(TEXT("A reset clears previous wall cooldown"), Movement->IsWallRunning());
    Scene.Step(76);
    TestFalse(TEXT("Wallrun duration is finite"), Movement->IsWallRunning());
    Scene.AirborneAt(FVector(0, 0, 400), FVector(1000, 0, 0));
    Scene.Step(2);
    Wall->Destroy();
    Scene.Step(2);
    TestFalse(TEXT("Losing a wall immediately restores falling"), Movement->IsWallRunning());

    Scene.Box(FVector(2000, 100, 500), FVector(60, 1, 10), true);
    Scene.Box(FVector(300, 0, 500), FVector(1, 6, 10));
    Scene.AirborneAt(FVector(0, 0, 400), FVector(1600, 0, 0));
    Scene.Step(20, false, 1.f / 30.f);
    TestTrue(TEXT("Sweeps prevent a30FPS wallrun tunneling through a front obstacle"), Scene.Runner->GetActorLocation().X < 225.f);
    TestTrue(TEXT("A front collision removes horizontal speed"), Movement->Velocity.Size2D() < 650.f);
    Scene.Runner->ResetRun();
    TestFalse(TEXT("Retry clears custom wall movement"), Movement->IsWallRunning());
    TestEqual(TEXT("Retry clears presentation roll direction"), Movement->GetWallSide(), 0.f);
    return true;
}
#endif
