#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "World/Generation/BootSurfTrack.h"
#include "World/Interaction/BootSurfWindow.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"

namespace BootSurfRouteTest
{
    struct FWorld
    {
        UWorld* World;
        FWorld()
        {
            UWorld::InitializationValues Init;
            Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
                .CreateAISystem(false).ShouldSimulatePhysics(true);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
                ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
            World->BeginPlay();
            World->GetWorldSettings()->NotifyBeginPlay();
        }
        ~FWorld()
        {
            World->EndPlay(EEndPlayReason::Quit);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
        void Step(int32 Frames, float Dt)
        {
            for (int32 I = 0; I < Frames; ++I) { ++GFrameCounter; World->Tick(LEVELTICK_All, Dt); }
        }
        ACharacter* Rider(FVector Top)
        {
            ACharacter* Result = World->SpawnActor<ACharacter>(Top + FVector(0, 0, 110), FRotator::ZeroRotator);
            Result->GetCapsuleComponent()->SetCapsuleSize(34, 96);
            UCharacterMovementComponent* Movement = Result->GetCharacterMovement();
            Movement->bRunPhysicsWithNoController = true;
            Movement->MaxWalkSpeed = 900.f;
            Movement->JumpZVelocity = 600.f;
            Movement->GravityScale = 1.7f;
            // Fixed launch momentum represents holding forward; no input/controller or audio mocks.
            Movement->GroundFriction = 0.f;
            Movement->BrakingDecelerationWalking = 0.f;
            Movement->BrakingDecelerationFalling = 0.f;
            Movement->FallingLateralFriction = 0.f;
            Movement->SetMovementMode(MOVE_Walking);
            return Result;
        }
    };
}

// Catches authored landing solids intersecting the swept window before B, which would silently abort SNAP.
// Also establishes that both upper connections can be reached at base speed without the rhythm boost.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV02SurfRouteTest, "Bootloop.V02.Course.SurfDockAndUpperTransitions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootV02SurfRouteTest::RunTest(const FString& Parameters)
{
    const FVector Starts[] = {FVector(2300, 0, 0), FVector(10150, 0, -160)};
    const FVector Ends[] = {FVector(3000, 450, 160), FVector(10800, -450, 0)};
    const double LandingStarts[] = {3510., 11310.};
    for (const int32 Fps : {30, 60, 120})
    {
        const float Dt = 1.f / Fps;
        BootSurfRouteTest::FWorld W;
        ABootSurfTrack* Track = W.World->SpawnActor<ABootSurfTrack>();
        if (!TestEqual(TEXT("Authored trial supplies both moving windows"), Track->GetWindows().Num(), 2)) return false;
        for (const bool bSynced : {false, true})
        {
            Track->ResetTrack(1301995);
            TArray<ACharacter*> Riders;
            for (int32 I = 0; I < 2; ++I) Riders.Add(W.Rider(Starts[I]));
            W.Step(Fps / 2, Dt);
            for (int32 I = 0; I < 2; ++I)
            {
                ABootSurfWindow* Window = Track->GetWindows()[I];
                TestTrue(TEXT("Rider settles on the actual authored platform"),
                    Riders[I]->GetMovementBaseObject() == Window->GetRootComponent());
                TestTrue(TEXT("SNAP starts the authored operation"), Window->Activate(Riders[I], bSynced));
            }
            W.Step(FMath::CeilToInt(1.6f * Fps), Dt);
            for (int32 I = 0; I < 2; ++I)
            {
                ABootSurfWindow* Window = Track->GetWindows()[I];
                const FVector ExpectedCentre = Ends[I] - FVector(0, 0, 40);
                AddInfo(FString::Printf(TEXT("%d FPS synced=%d window=%d dock=%s rider=%s"), Fps, bSynced,
                    I + 1, *Window->GetActorLocation().ToString(), *Riders[I]->GetActorLocation().ToString()));
                TestTrue(TEXT("Window reaches B with its real onboard capsule and all course collisions"),
                    Window->GetActorLocation().Equals(ExpectedCentre, .15f));
                TestTrue(TEXT("Actual authored ride is recognized"), Window->HasCarried(Riders[I]));
                TestTrue(TEXT("Rider remains on the docked platform"),
                    Riders[I]->GetMovementBaseObject() == Window->GetRootComponent());
                TestFalse(TEXT("Dock publishes no stale base velocity"), Window->GetVelocity().Size() > .01f);
                Riders[I]->GetCharacterMovement()->Velocity = FVector(900, 0, 0);
                Riders[I]->LaunchCharacter(FVector(0, 0, 600), false, true);
            }
            W.Step(3, Dt);
            for (int32 I = 0; I < 2; ++I)
            {
                ABootSurfWindow* Window = Track->GetWindows()[I];
                TestEqual(TEXT("Only synced departure supplies the optional charge"),
                    Window->ConsumeDepartureBoost(Riders[I]), bSynced);
                TestFalse(TEXT("Departure charge cannot repeat"), Window->ConsumeDepartureBoost(Riders[I]));
            }
            W.Step(Fps, Dt);
            for (int32 I = 0; I < 2; ++I)
            {
                ACharacter* Rider = Riders[I];
                ABootSurfWindow* Window = Track->GetWindows()[I];
                AddInfo(FString::Printf(TEXT("%d FPS synced=%d window=%d upper arrival=%s grounded=%d"),
                    Fps, bSynced, I + 1, *Rider->GetActorLocation().ToString(),
                    Rider->GetCharacterMovement()->IsMovingOnGround()));
                TestTrue(TEXT("Base-speed departure reaches the authored upper landing"),
                    Rider->GetActorLocation().X > LandingStarts[I] + 200
                    && FMath::Abs(Rider->GetActorLocation().Y - Ends[I].Y) < 2);
                TestTrue(TEXT("Upper landing is real solid geometry at the expected height"),
                    Rider->GetCharacterMovement()->IsMovingOnGround()
                    && FMath::Abs(Rider->GetActorLocation().Z - (Ends[I].Z + 98.15)) < 3);
                TestTrue(TEXT("A ride followed by the actual upper transition can commit"), Window->CommitRide(Rider));
                TestFalse(TEXT("The same authored window completion cannot repeat"), Window->CommitRide(Rider));
                Rider->Destroy();
            }
        }
    }
    return true;
}
#endif
