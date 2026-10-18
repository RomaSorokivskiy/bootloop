#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "World/Interaction/BootSurfWindow.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"

namespace BootSurfTest
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
        void Step(int32 Frames, float Dt = 1.f / 60.f)
        {
            for (int32 I = 0; I < Frames; ++I) { ++GFrameCounter; World->Tick(LEVELTICK_All, Dt); }
        }
        ACharacter* Rider(FVector Top)
        {
            ACharacter* Result = World->SpawnActor<ACharacter>(Top + FVector(0, 0, 110), FRotator::ZeroRotator);
            Result->GetCharacterMovement()->bRunPhysicsWithNoController = true;
            Result->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            return Result;
        }
        ABootSurfWindow* Window(FVector Start, FVector End, float Seconds, int32 Id)
        {
            ABootSurfWindow* Result = World->SpawnActor<ABootSurfWindow>();
            Result->Configure(Start, End, Seconds, Id);
            return Result;
        }
        void Depart(ACharacter* Rider)
        {
            Rider->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
            Rider->LaunchCharacter(FVector(0, 0, 650), false, true);
            Step(3);
        }
    };
}

// Catches a synced branch changing the route, missing base carry, or re-adding base speed on every frame.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootSurfMotionTest, "Bootloop.V02.Window.OffbeatMovesAndCarries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootSurfMotionTest::RunTest(const FString& Parameters)
{
    BootSurfTest::FWorld W;
    ABootSurfWindow* Off = W.Window(FVector::ZeroVector, FVector(600, 300, 160), 1.f, 11);
    ABootSurfWindow* Sync = W.Window(FVector(0, 3000, 0), FVector(600, 3300, 160), 1.f, 12);
    ACharacter* A = W.Rider(FVector::ZeroVector);
    ACharacter* B = W.Rider(FVector(0, 3000, 0));
    W.Step(24);
    TestTrue(TEXT("Rider naturally finds solid platform as movement base"), A->GetMovementBaseObject() == Off->GetRootComponent());
    const FVector Before = A->GetActorLocation();
    TestTrue(TEXT("Offbeat activation succeeds"), Off->Activate(A, false));
    TestTrue(TEXT("Synced activation succeeds"), Sync->Activate(B, true));
    TestFalse(TEXT("Repeated activation is refused"), Off->Activate(A, true));
    W.Step(30);
    TestTrue(TEXT("Offbeat and synced platforms follow the identical baseline trajectory"),
        (Off->GetActorLocation() - (Sync->GetActorLocation() - FVector(0, 3000, 0))).Size() < .1f);
    TestTrue(TEXT("Real moving base carries rider in horizontal and vertical directions"),
        A->GetActorLocation().X > Before.X + 200 && A->GetActorLocation().Z > Before.Z + 35);
    TestTrue(TEXT("Carry is recorded only for actual rider"), Off->HasCarried(A));
    TestFalse(TEXT("Another character cannot claim ride"), Off->HasCarried(B));
    TestFalse(TEXT("Cannot commit while still aboard"), Off->CommitRide(A));
    const FVector BaseVelocity = Off->GetVelocity();
    W.Depart(A);
    TestTrue(TEXT("Published platform velocity transfers once on departure"),
        A->GetVelocity().X > BaseVelocity.X * .8 && A->GetVelocity().X < BaseVelocity.X * 1.1);
    TestFalse(TEXT("Offbeat never creates rhythm overclock"), Off->ConsumeDepartureBoost(A));
    TestTrue(TEXT("Real ride followed by departure can commit"), Off->CommitRide(A));
    TestFalse(TEXT("Completion is deduplicated"), Off->CommitRide(A));
    W.Step(60);
    TestTrue(TEXT("Offbeat platform stays docked and walkable"),
        Off->GetActorLocation().Equals(FVector(600, 300, 120), .1f));
    Off->ResetWindow();
    TestTrue(TEXT("Retry makes the operation available"), Off->IsAvailable());
    TestFalse(TEXT("Retry clears rider history"), Off->HasCarried(A));
    TestTrue(TEXT("Retry restores start position"), Off->GetActorLocation().Equals(FVector(0, 0, -40), .1f));
    return true;
}

// Catches remote score farming, bonus banking, reboarding recharge, and docking-time charge that never expires.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootSurfChargeTest, "Bootloop.V02.Window.ChargeIdentityAndExpiry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootSurfChargeTest::RunTest(const FString& Parameters)
{
    BootSurfTest::FWorld W;
    ABootSurfWindow* Window = W.Window(FVector::ZeroVector, FVector(400, 0, 0), 1.f, 21);
    ACharacter* Rider = W.Rider(FVector::ZeroVector);
    ACharacter* Remote = W.Rider(FVector(5000, 0, 0));
    W.Step(20);
    TestTrue(TEXT("Synced activation accepts rider"), Window->Activate(Rider, true));
    W.Step(20);
    TestFalse(TEXT("Charge cannot be spent on the platform"), Window->ConsumeDepartureBoost(Rider));
    TestFalse(TEXT("Remote character cannot spend another rider's charge"), Window->ConsumeDepartureBoost(Remote));
    TestFalse(TEXT("Remote character cannot commit operation"), Window->CommitRide(Remote));
    W.Depart(Rider);
    TestTrue(TEXT("Synced actual rider receives one departure charge"), Window->ConsumeDepartureBoost(Rider));
    TestFalse(TEXT("Charge is consumed once"), Window->ConsumeDepartureBoost(Rider));
    W.Step(120);
    Rider->SetActorLocation(Window->GetEndTop() + FVector(0, 0, 110), false, nullptr, ETeleportType::TeleportPhysics);
    Rider->GetCharacterMovement()->Velocity = FVector::ZeroVector;
    Rider->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    W.Step(24);
    W.Depart(Rider);
    TestFalse(TEXT("Reboarding never recharges consumed bonus"), Window->ConsumeDepartureBoost(Rider));

    Window->ResetWindow();
    Rider->SetActorLocation(FVector(0, 0, 110), false, nullptr, ETeleportType::TeleportPhysics);
    Rider->GetCharacterMovement()->Velocity = FVector::ZeroVector;
    Rider->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    W.Step(24);
    Window->Activate(Rider, true);
    W.Step(140);
    W.Depart(Rider);
    TestFalse(TEXT("Charge expires one second after docking, even if rider waits aboard"), Window->ConsumeDepartureBoost(Rider));

    ABootSurfWindow* RemoteWindow = W.Window(FVector(0, 6000, 0), FVector(400, 6000, 0), 1.f, 22);
    RemoteWindow->Activate(Remote, true);
    W.Step(70);
    TestFalse(TEXT("Remote activation without a real ride cannot commit"), RemoteWindow->CommitRide(Remote));
    TestFalse(TEXT("Remote activation without a real ride cannot boost"), RemoteWindow->ConsumeDepartureBoost(Remote));
    return true;
}

// Catches teleports through blockers when a low-frame-rate move spans the obstacle.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootSurfSweepTest, "Bootloop.V02.Window.SweepsAtLowFrameRate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootSurfSweepTest::RunTest(const FString& Parameters)
{
    BootSurfTest::FWorld W;
    ABootSurfWindow* Window = W.Window(FVector::ZeroVector, FVector(2000, 0, 0), .5f, 31);
    AActor* Barrier = W.World->SpawnActor<AActor>();
    UBoxComponent* Box = NewObject<UBoxComponent>(Barrier);
    Barrier->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(25, 1000, 1000));
    Box->SetCollisionProfileName(TEXT("BlockAll"));
    Box->RegisterComponent();
    Barrier->SetActorLocation(FVector(900, 0, 0));
    ACharacter* Rider = W.Rider(FVector::ZeroVector);
    W.Step(24);
    Window->Activate(Rider, false);
    W.Step(20, 1.f / 30.f);
    TestTrue(TEXT("Swept root stops before blocker instead of tunneling"), Window->GetActorLocation().X < 390);
    TestTrue(TEXT("Blocked platform publishes no phantom velocity"), Window->GetVelocity().Size() < 1.f);
    return true;
}

// Catches a platform ignoring its own rider and lifting that capsule through a low ceiling.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootSurfCeilingTest, "Bootloop.V02.Window.ProtectsRiderClearance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootSurfCeilingTest::RunTest(const FString& Parameters)
{
    BootSurfTest::FWorld W;
    ABootSurfWindow* Window = W.Window(FVector::ZeroVector, FVector(400, 0, 160), 1.f, 41);
    AActor* Ceiling = W.World->SpawnActor<AActor>();
    UBoxComponent* Box = NewObject<UBoxComponent>(Ceiling);
    Ceiling->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(2000, 1000, 25));
    Box->SetCollisionProfileName(TEXT("BlockAll"));
    Box->RegisterComponent();
    Ceiling->SetActorLocation(FVector(0, 0, 260));
    ACharacter* Rider = W.Rider(FVector::ZeroVector);
    W.Step(24);
    Window->Activate(Rider, false);
    W.Step(45, 1.f / 30.f);
    const double RiderTop = Rider->GetActorLocation().Z + Rider->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    TestTrue(TEXT("Ascending root stops when the carried capsule loses clearance"), Window->GetActorLocation().Z < 40);
    TestTrue(TEXT("Rider capsule remains below the ceiling"), RiderTop <= 236);
    return true;
}
#endif
