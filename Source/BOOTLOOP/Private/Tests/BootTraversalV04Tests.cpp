#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BootMovementV04TestWorld.h"
#include "Components/CapsuleComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTraversalTempoV04Test,
    "Bootloop.V04.Traversal.TempoCapsAndSafeRevision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootTraversalTempoV04Test::RunTest(const FString&)
{
    FBootMovementV04World Scene;
    auto* Move = Scene.Move;
    Scene.Step(30);
    const auto Fast = FBootTraversalPolicy::FromTempo(168., 1);
    Move->ApplyTraversalEnvelope(Fast);
    Scene.Step(90, 1.f / 60.f, true);
    TestTrue(TEXT("168 BPM ordinary run exceeds the legacy 900 pace"), Move->Velocity.Size2D() > 1400.);
    TestTrue(TEXT("168 BPM momentum exceeds the old 1600 ceiling"), Move->MomentumCap > 2500.f);
    TestTrue(TEXT("Tempo caps remain within the 3600 engineering guard"), Move->OverclockCap <= 3600.f);

    Scene.Air(FVector(2400, 0, 123)); Move->ApplyTraversalEnvelope(Fast); Move->GravityScale = 0.f;
    const auto Slow = FBootTraversalPolicy::FromTempo(96., 2);
    Move->ApplyTraversalEnvelope(Slow);
    Scene.Step(30);
    TestTrue(TEXT("Preparing slower music preserves current falling momentum"), FMath::IsNearlyEqual(Move->Velocity.Size2D(), 2400., 1.));
    TestEqual(TEXT("An airborne preparation does not publish the new physics revision"), Move->GetTraversalEnvelope().Revision, uint32(1));
    FBootSafeBoundary Boundary; Boundary.PreparedRevision = 2;
    Boundary.bSupported = Boundary.bNoActiveCommitment = Boundary.bFutureReady = true;
    TestFalse(TEXT("Forged supported flag cannot commit in midair"), Move->CommitTraversalEnvelope(Boundary, 2.5f));
    Scene.Ground(); Scene.Step(20, .01f);
    TestEqual(TEXT("An arbitrary landing never auto-commits a lower cap"), Move->GetTraversalEnvelope().Revision, uint32(1));
    Boundary.bFutureReady = false;
    TestFalse(TEXT("Missing prepared future landing rejects revision"), Move->CommitTraversalEnvelope(Boundary, 2.5f));
    Boundary.bFutureReady = true;
    TestTrue(TEXT("A real supported prepared connector commits"), Move->CommitTraversalEnvelope(Boundary, 2.5f));
    Scene.Step(1, .01f);
    TestTrue(TEXT("Tempo target changes gradually, not on the first frame"), Move->MaxWalkSpeed > 1400.f);
    Scene.Step(300, .01f);
    TestTrue(TEXT("One-bar transition settles at the new run target"), FMath::IsNearlyEqual(Move->MaxWalkSpeed, 812.3077f, 1.f));
    TestTrue(TEXT("Lower momentum cap applies only after the safe commitment"), FMath::IsNearlyEqual(Move->MomentumCap, 1476.9231f, 1.f));
    Move->ApplyTraversalEnvelope(Fast);
    TestEqual(TEXT("Stale revisions cannot rewind physics"), Move->GetTraversalEnvelope().Revision, uint32(2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTraversalManualV04Test,
    "Bootloop.V04.Traversal.ManualHopAndSlide",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootTraversalManualV04Test::RunTest(const FString&)
{
    FBootMovementV04World Scene;
    Scene.Step(30);
    Scene.Move->ApplyTraversalEnvelope(FBootTraversalPolicy::FromTempo(168., 1));
    Scene.Runner->RequestJump(); Scene.Step(65);
    TestTrue(TEXT("One fresh input lands without automatic next jump"), Scene.Move->IsMovingOnGround());
    Scene.Step(60, 1.f / 60.f, true);
    for (int32 Hop = 0; Hop < 12; ++Hop)
    {
        Scene.Runner->RequestJump(); Scene.Step(2, 1.f / 60.f, true);
        if (Hop == 11) break;
        for (int32 I = 0; I < 100 && !Scene.Move->IsMovingOnGround(); ++I) Scene.Step(1, 1.f / 60.f, true);
    }
    TestTrue(TEXT("Fresh manual hops unlock momentum beyond sixteen metres per second"), Scene.Move->Velocity.Size2D() > 2300.);
    TestTrue(TEXT("Manual hopping stays under the active momentum cap"), Scene.Move->Velocity.Size2D() <= 2585.);

    Scene.Air(FVector(900, 0, -50), FVector(0, 0, 230));
    Scene.Move->ApplyTraversalEnvelope(FBootTraversalPolicy::FromTempo(130., 1));
    Scene.Box(FVector(150, 0, 250), FVector(.2, 8, 2.8)); // underside 110 cm.
    Scene.Runner->StartSlide(); Scene.Step(1);
    TestTrue(TEXT("Fresh airborne slide immediately obtains real capsule clearance"), Scene.Runner->bIsCrouched && Scene.Runner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() < 50.f);
    for (int32 I = 0; I < 90 && !Scene.Move->IsMovingOnGround(); ++I) Scene.Step(1);
    TestTrue(TEXT("The crouched landing remains valid after an airborne slide"), Scene.Move->IsMovingOnGround());
    TestFalse(TEXT("Blocked overhead geometry never forces standing penetration"), Scene.Runner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() > 50.f && Scene.Runner->GetActorLocation().X < 190.);
    return true;
}
#endif
