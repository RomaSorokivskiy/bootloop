#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "World/Interaction/BootAssemblingPlatform.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerState.h"
#include <limits>

namespace BootAssemblyTest
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
        void Step(int32 Frames) { for (int32 I=0; I<Frames; ++I) { ++GFrameCounter; World->Tick(LEVELTICK_All, 1.f/60.f); } }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootAssemblySafetyTest, "Bootloop.V03.Assembly.LookaheadAndOccupiedCollider",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootAssemblySafetyTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Both legal branches prepare with character plus moving base worst speed"),
        FBootAssemblyLookahead::NeedsPreparation(11000.) && FBootAssemblyLookahead::NeedsPreparation(9000.));
    TestFalse(TEXT("Distant district can remain dormant"), FBootAssemblyLookahead::NeedsPreparation(40000.));
    TestTrue(TEXT("Invalid distance fails toward readiness"), FBootAssemblyLookahead::NeedsPreparation(std::numeric_limits<double>::quiet_NaN()));
    BootAssemblyTest::FWorld W;
    auto* Landing = W.World->SpawnActor<ABootAssemblingPlatform>();
    Landing->Configure(FTransform(FVector(0,0,0)), FVector(1200,900,80), 1.2f, 31);
    auto* Rider = W.World->SpawnActor<ACharacter>(FVector(0,0,-30), FRotator::ZeroRotator);
    Rider->GetCharacterMovement()->DisableMovement();
    W.Step(1);
    TestFalse(TEXT("Collider cannot appear inside a real pawn capsule"), Landing->FinishSafely());
    TestFalse(TEXT("Occupied landing stays non-ready"), Landing->IsReady());
    TestEqual(TEXT("Occupied landing remains noncolliding"), CastChecked<UBoxComponent>(Landing->GetRootComponent())->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    Rider->SetActorLocation(FVector(3000,0,200));
    TestTrue(TEXT("Checkpoint prewarm completes before caller teleports pawn"), Landing->FinishSafely());
    TestTrue(TEXT("Clear landing is ready"), Landing->IsReady());
    Rider->SetActorLocation(FVector(0,0,100));
    Rider->SetBase(CastChecked<UPrimitiveComponent>(Landing->GetRootComponent()));
    const FTransform SupportTransform = Landing->GetActorTransform();
    Landing->ResetAssembly(false);
    Landing->Configure(FTransform(FVector(5000,0,0)), FVector(600,600,80), 1.f, 32);
    TestTrue(TEXT("Reset/configure never withdraws current support"), Landing->IsReady());
    TestTrue(TEXT("Reset/configure never teleports current support through rider"), Landing->GetActorTransform().Equals(SupportTransform));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootAssemblyResetTest, "Bootloop.V03.Assembly.PauseResetAndBoundedParts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootAssemblyResetTest::RunTest(const FString& Parameters)
{
    BootAssemblyTest::FWorld W;
    auto* Landing = W.World->SpawnActor<ABootAssemblingPlatform>();
    Landing->Configure(FTransform(FVector(100,200,300)), FVector(1200,900,80), 1.1f, 7);
    const int32 Count = Landing->GetDecorativePartCount();
    TestTrue(TEXT("Casing has a bounded authored part budget"), Count >= 20 && Count <= 100);
    Landing->BeginAssembly();
    W.Step(20);
    TestTrue(TEXT("Assembly advances over gameplay time"), Landing->GetAssemblyProgress() > .1f);
    const float BeforePause = Landing->GetAssemblyProgress();
    W.World->GetWorldSettings()->SetPauserPlayerState(W.World->SpawnActor<APlayerState>());
    W.Step(30);
    TestEqual(TEXT("Pause freezes assembly"), Landing->GetAssemblyProgress(), BeforePause);
    W.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    W.Step(90);
    TestTrue(TEXT("Unpaused sequence completes within 1.2 seconds"), Landing->IsReady());
    for (int32 I=0; I<10; ++I)
    {
        Landing->ResetAssembly(false);
        TestFalse(TEXT("Reset returns to deterministic dormant state"), Landing->IsReady());
        Landing->BeginAssembly();
        W.Step(75);
        TestTrue(TEXT("Every retry reaches ready"), Landing->IsReady());
        TestEqual(TEXT("Ten resets allocate no extra decorative instances"), Landing->GetDecorativePartCount(), Count);
    }
    return true;
}
#endif
