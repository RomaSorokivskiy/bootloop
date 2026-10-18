#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "World/Interaction/BootSurfWindow.h"
#include "World/Threats/BootScanProcess.h"
#include "World/Threats/BootPopupProcess.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"

namespace BootProcessTest
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
            Result->GetCapsuleComponent()->SetCapsuleSize(34, 96);
            Result->GetCharacterMovement()->bRunPhysicsWithNoController = true;
            Result->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            return Result;
        }
        void Depart(ACharacter* Rider)
        {
            Rider->LaunchCharacter(FVector(0, 0, 650), false, true);
            Step(3);
        }
    };
}

// Catches yaw ignored by collision/handle/reset, readiness bypass, remote charge and rhythm-dependent routing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootOrientedSurfV03Test, "Bootloop.V03.Process.OrientedSurfAndCharge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootOrientedSurfV03Test::RunTest(const FString& Parameters)
{
    for (const int32 Fps : {30, 60, 120})
    for (const double Yaw : {90., 180.})
    {
        BootProcessTest::FWorld W;
        const FQuat Rotation(FRotator(0, Yaw, 0));
        const FVector Offset(0, 3000, 0);
        const FVector End = Rotation.RotateVector(FVector(500, 0, 120));
        ABootSurfWindow* Off = W.World->SpawnActor<ABootSurfWindow>();
        ABootSurfWindow* Sync = W.World->SpawnActor<ABootSurfWindow>();
        Off->ConfigureOriented(FTransform(Rotation, FVector::ZeroVector), End, 1.f, 601);
        Sync->ConfigureOriented(FTransform(Rotation, Offset), End + Offset, 1.f, 602);
        TestTrue(TEXT("Handle rotates with local yaw"), Off->GetHandleLocation().Equals(
            Rotation.RotateVector(FVector(-416, 0, 47)) - FVector(0, 0, 40), .1));
        ACharacter* A = W.Rider(FVector::ZeroVector);
        ACharacter* B = W.Rider(Offset);
        W.Step(Fps / 2, 1.f / Fps);
        Off->SetReady(false);
        TestFalse(TEXT("Unconstructed window rejects activation"), Off->Activate(A, false));
        Off->SetReady(true);
        TestTrue(TEXT("Offbeat starts ready operation"), Off->Activate(A, false));
        TestTrue(TEXT("Synced starts identical operation"), Sync->Activate(B, true));
        TestFalse(TEXT("Accepted SNAP alone is not earned charge"), Sync->HasChargeFor(B));
        W.Step(Fps / 2, 1.f / Fps);
        TestTrue(TEXT("Ratings use identical oriented trajectory"), Off->GetActorLocation().Equals(Sync->GetActorLocation() - Offset, .1));
        TestTrue(TEXT("Oriented solid carries real rider"), Sync->HasCarried(B));
        TestTrue(TEXT("Carry earns synced charge"), Sync->HasChargeFor(B));
        TestFalse(TEXT("Charge belongs only to activating rider"), Sync->HasChargeFor(A));
        W.Depart(B);
        TestTrue(TEXT("Departure spends earned charge once"), Sync->ConsumeDepartureBoost(B));
        TestFalse(TEXT("Charge deduplicates"), Sync->ConsumeDepartureBoost(B));
        W.Step(Fps * 2, 1.f / Fps);
        TestTrue(TEXT("Both windows dock at same endpoint"), Off->GetActorLocation().Equals(End - FVector(0, 0, 40), .1));
        Off->ResetWindow();
        TestTrue(TEXT("Reset preserves yaw"), Off->GetActorQuat().Equals(Rotation, .001));
        TestFalse(TEXT("Reset clears carry"), Off->HasCarried(A));
    }
    return true;
}

// Catches initial beam contact before warning and tunnelling when player and beam cross between 30 FPS samples.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootScannerV03Test, "Bootloop.V03.Process.ScannerWarningAndRelativeSweep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootScannerV03Test::RunTest(const FString& Parameters)
{
    BootProcessTest::FWorld W;
    for (const double Yaw : {90., 180.})
    {
        const FQuat Rotation(FRotator(0, Yaw, 0));
        ABootScanProcess* Scan = W.World->SpawnActor<ABootScanProcess>();
        Scan->Configure(FTransform(Rotation, FVector(5000, 0, 100)), FVector(1200, 0, 0), 8);
        ACharacter* Target = W.Rider(FVector(5000, 0, -10) + Rotation.RotateVector(FVector(200, 0, 0)));
        Target->GetCharacterMovement()->DisableMovement();
        int32 Contacts = 0;
        Scan->OnThreatContact.AddLambda([&](ACharacter*, int32 Generation) { if (Generation == 17) ++Contacts; });
        Scan->Arm(17);
        TestTrue(TEXT("Every branch starts with visible warning"), Scan->GetPhase() == EBootScanPhase::Warning);
        W.Step(12, 1.f / 30.f);
        TestEqual(TEXT("Warning is harmless"), Contacts, 0);
        TestTrue(TEXT("First warning lasts at least 0.4 seconds"), Scan->GetPhase() == EBootScanPhase::Warning);
        W.Step(18, 1.f / 30.f);
        TestTrue(TEXT("Warning advances to finite sweep"), Scan->GetPhase() == EBootScanPhase::Sweep);
        W.Step(40, 1.f / 30.f);
        TestTrue(TEXT("Sweep has a finite clear phase"), Scan->GetPhase() == EBootScanPhase::Clear);
        TestEqual(TEXT("Actual swept beam emits one generation-bearing contact"), Contacts, 1);
        Scan->ResetProcess(18);
        TestTrue(TEXT("Restore discards old phase"), Scan->GetPhase() == EBootScanPhase::Dormant);
        Scan->Arm(18);
        Scan->SetRunActive(false);
        W.Step(60);
        TestTrue(TEXT("Inactive run cannot advance threat"), Scan->GetPhase() == EBootScanPhase::Warning);
        Scan->Destroy();
        Target->Destroy();
    }
    for (const double Yaw : {0., 90., 180.})
    {
        const FQuat Rotation(FRotator(0, Yaw, 0));
        TestTrue(TEXT("Opposite 1800cm/s player and beam cross in one 30FPS frame"),
            ABootScanProcess::SweptCapsuleContact(FVector::ZeroVector, Rotation.RotateVector(FVector(60, 0, 0)), Rotation,
                FVector(8, 300, 100), Rotation.RotateVector(FVector(100, 0, 0)), Rotation.RotateVector(FVector(40, 0, 0)), 34, 96));
        TestFalse(TEXT("Visible beam cannot hit a capsule outside its lateral reach"),
            ABootScanProcess::SweptCapsuleContact(FVector::ZeroVector, Rotation.RotateVector(FVector(60, 0, 0)), Rotation,
                FVector(8, 300, 100), Rotation.RotateVector(FVector(100, 400, 0)), Rotation.RotateVector(FVector(40, 400, 0)), 34, 96));
    }
    return true;
}

// Catches POPUP teleporting to its original A, retaining hostile collision, pre-capture carry and stale callbacks.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootPopupV03Test, "Bootloop.V03.Process.PopupInterceptionAndGeneration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootPopupV03Test::RunTest(const FString& Parameters)
{
    BootProcessTest::FWorld W;
    ABootPopupProcess* Popup = W.World->SpawnActor<ABootPopupProcess>();
    Popup->ConfigurePopup(FTransform(FRotator(0, 90, 0), FVector::ZeroVector), FVector(900, 0, 0), FVector(0, 1600, 120), 1.f, 701, 21);
    ACharacter* Remote = W.Rider(FVector(5000, 0, 0));
    W.Step(75);
    const FVector CapturePosition = Popup->GetActorLocation();
    TestTrue(TEXT("Hostile popup moves before interception"), CapturePosition.Y > 100);
    int32 Contacts = 0;
    Popup->OnThreatContact.AddLambda([&](ACharacter*, int32 Generation) { if (Generation == 21) ++Contacts; });
    TestTrue(TEXT("Visible handle accepts remote SNAP"), Popup->Activate(Remote, true));
    TestFalse(TEXT("Capture atomically removes hostility"), Popup->IsHostile());
    TestTrue(TEXT("Capture starts at current physical location"), Popup->GetActorLocation().Equals(CapturePosition, .1));
    TestFalse(TEXT("Remote capture has not earned carry"), Popup->HasCarried(Remote));
    TestFalse(TEXT("Remote capture has not earned charge"), Popup->HasChargeFor(Remote));
    TestFalse(TEXT("Capture cannot repeat"), Popup->Activate(Remote, true));
    W.Step(70);
    TestEqual(TEXT("Captured hostile geometry never emits contact"), Contacts, 0);
    TestTrue(TEXT("Friendly ride docks at authored endpoint"), Popup->GetActorLocation().Equals(FVector(0, 1600, 80), .1));
    Popup->ResetProcess(22);
    TestTrue(TEXT("Restore gives new hostile generation"), Popup->IsHostile() && Popup->GetGeneration() == 22);

    ABootPopupProcess* ContactPopup = W.World->SpawnActor<ABootPopupProcess>();
    ContactPopup->ConfigurePopup(FTransform(FRotator(0, 90, 0), FVector(0, 3000, 0)),
        FVector::ZeroVector, FVector(0, 4400, 120), 1.f, 702, 22);
    TArray<int32> ContactGenerations;
    ContactPopup->OnThreatContact.AddLambda([&](ACharacter*, int32 Generation) { ContactGenerations.Add(Generation); });
    ACharacter* Target = W.Rider(FVector(5000, 5000, 0));
    Target->GetCharacterMovement()->DisableMovement();
    auto EnterVisiblePanel = [&]()
    {
        // Hand-authored panel local centre is (60,0,50), dimensions720x600x8.
        Target->SetActorLocation(ContactPopup->GetActorTransform().TransformPosition(FVector(60, 0, 50))
            + FVector(0, 0, 90), false, nullptr, ETeleportType::TeleportPhysics);
    };
    W.Step(60);
    EnterVisiblePanel();
    W.Step(1);
    TestEqual(TEXT("Physical contact waits until the next frame for input precedence"), ContactGenerations.Num(), 0);
    TestTrue(TEXT("Same-frame SNAP wins before queued hostile resolution"), ContactPopup->Activate(Target, false));
    W.Step(2);
    TestEqual(TEXT("Capture cancels actual same-frame pending contacts"), ContactGenerations.Num(), 0);

    Target->SetActorLocation(FVector(5000, 5000, 110), false, nullptr, ETeleportType::TeleportPhysics);
    ContactPopup->ResetProcess(23);
    W.Step(60);
    EnterVisiblePanel();
    W.Step(1);
    TestEqual(TEXT("Contact was queued under old attempt without premature callback"), ContactGenerations.Num(), 0);
    ContactPopup->ResetProcess(24);
    Target->SetActorLocation(FVector(5000, 5000, 110), false, nullptr, ETeleportType::TeleportPhysics);
    W.Step(2);
    TestEqual(TEXT("Restore discards real queued old-generation contact"), ContactGenerations.Num(), 0);

    ContactPopup->ResetProcess(25);
    W.Step(60);
    EnterVisiblePanel();
    W.Step(1);
    TestEqual(TEXT("Current contact also observes the one-frame deferral"), ContactGenerations.Num(), 0);
    W.Step(1);
    if (TestEqual(TEXT("Uncaptured visible hazard emits a real contact"), ContactGenerations.Num(), 1))
        TestEqual(TEXT("Callback preserves physical contact generation"), ContactGenerations[0], 25);
    W.Step(3);
    TestEqual(TEXT("Continuous contact is deduplicated"), ContactGenerations.Num(), 1);

    // Capture both ratings from the same stationary hostile state, then observe real carry.
    ABootPopupProcess* Off = W.World->SpawnActor<ABootPopupProcess>();
    ABootPopupProcess* Sync = W.World->SpawnActor<ABootPopupProcess>();
    Off->ConfigurePopup(FTransform(FRotator(0, 180, 0), FVector(0, 6000, 0)), FVector::ZeroVector,
        FVector(-600, 6000, 120), 1.f, 703, 26);
    Sync->ConfigurePopup(FTransform(FRotator(0, 180, 0), FVector(0, 9000, 0)), FVector::ZeroVector,
        FVector(-600, 9000, 120), 1.f, 704, 26);
    ACharacter* A = W.Rider(FVector(0, 6000, 0));
    ACharacter* B = W.Rider(FVector(0, 9000, 0));
    W.Step(24);
    TestTrue(TEXT("Offbeat interception accepts actual rider"), Off->Activate(A, false));
    TestTrue(TEXT("Synced interception accepts actual rider"), Sync->Activate(B, true));
    TestFalse(TEXT("Pre-capture support is not post-capture carry"), Sync->HasCarried(B));
    W.Step(30);
    TestTrue(TEXT("Popup ratings have identical physical route and duration"),
        Off->GetActorLocation().Equals(Sync->GetActorLocation() - FVector(0, 3000, 0), .1));
    TestTrue(TEXT("Friendly popup earns charge only after real activating-rider carry"), Sync->HasChargeFor(B));
    TestFalse(TEXT("Offbeat friendly popup has no charge"), Off->HasChargeFor(A));
    W.Step(120);
    W.Depart(B);
    TestFalse(TEXT("Popup charge expires one second after docking"), Sync->ConsumeDepartureBoost(B));
    TestFalse(TEXT("Expired popup charge cannot be renewed by SNAP"), Sync->Activate(B, true));
    return true;
}
#endif
