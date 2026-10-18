#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BootMovementV04TestWorld.h"
#include "Player/BootAbilityComponent.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootDashV04Test,
    "Bootloop.V04.Abilities.DashCollision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootDashV04Test::RunTest(const FString&)
{
    for (const int32 Fps : {30, 60, 120})
    {
        FBootMovementV04World Scene;
        auto* Move = Scene.Move;
        auto E = FBootTraversalPolicy::FromTempo(168., 1); E.DashCharges = 2; E.AirRedirectCharges = 3;
        Scene.Air(); Move->ApplyTraversalEnvelope(E); Move->GravityScale = 0.f;
        TestEqual(TEXT("Opening dash stacks grants the finite capacity"), Move->GetDashCharges(), 2);
        const FVector Before = Move->Velocity;
        TestTrue(TEXT("Unlocked airborne dash is accepted off beat"), Move->TryBufferDash(FVector(1, 0, 80)));
        TestTrue(TEXT("Accepted impulse is at most six metres per second"), (Move->Velocity - Before).Size2D() <= 600.01);
        TestEqual(TEXT("Dash never adds vertical velocity"), Move->Velocity.Z, Before.Z);
        TestEqual(TEXT("Only one dash charge is spent"), Move->GetDashCharges(), 1);
        TestTrue(TEXT("A second stack permits a second controlled dash"), Move->TryBufferDash(FVector::ForwardVector));
        TestFalse(TEXT("Finite capacity rejects a third dash"), Move->TryBufferDash(FVector::ForwardVector));
        Move->ApplyTraversalEnvelope(E);
        TestEqual(TEXT("Repeated envelope publication cannot refill charges"), Move->GetDashCharges(), 0);
        Scene.Step(Fps, 1.f / Fps);
        TestTrue(TEXT("The impulse decays smoothly to the original horizontal speed"), FMath::IsNearlyEqual(Move->Velocity.Size2D(), 900., 2.));
        Scene.Ground(); Scene.Step(5, .01f);
        TestEqual(TEXT("Brief support does not refill dash"), Move->GetDashCharges(), 0);
        Scene.Runner->SetActorLocation(FVector(0, 0, 5000), false, nullptr, ETeleportType::TeleportPhysics);
        Move->SetMovementMode(MOVE_Falling); Scene.Step(1, .01f);
        Scene.Ground(); Scene.Step(5, .01f);
        TestEqual(TEXT("Broken contacts cannot accumulate into a refill"), Move->GetDashCharges(), 0);
        Scene.Step(4, .01f);
        TestEqual(TEXT("Continuous eighty-millisecond support restores bounded capacity"), Move->GetDashCharges(), 2);
        TestFalse(TEXT("Grounded dash is rejected"), Move->TryBufferDash(FVector::ForwardVector));

        Scene.Air(FVector(E.TemporaryCmps, 0, 123)); Move->ApplyTraversalEnvelope(E); Move->GravityScale = 0.f;
        TestFalse(TEXT("A same-heading dash at cap has no effect and is rejected"), Move->TryBufferDash(FVector::ForwardVector));
        TestEqual(TEXT("No-effect input retains both charges"), Move->GetDashCharges(), 2);
        TestFalse(TEXT("Nonfinite direction is rejected"), Move->TryBufferDash(FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0)));
        TestEqual(TEXT("Invalid direction retains both charges"), Move->GetDashCharges(), 2);

        Scene.Air(FVector(900, 0, 123)); Move->ApplyTraversalEnvelope(E); Move->GravityScale = 0.f;
        Scene.Box(FVector(80, 0, 5000), FVector(.1, 100, 100));
        TestTrue(TEXT("Dash with free approach space starts"), Move->TryBufferDash(FVector::ForwardVector));
        Scene.Step(Fps / 5, 1.f / Fps);
        const float CollisionSpeed = Move->Velocity.Size2D();
        TestTrue(TEXT("Native swept capsule cannot tunnel through the front wall"), Scene.Runner->GetActorLocation().X < 65.);
        TestTrue(TEXT("Front collision removes dash speed"), CollisionSpeed < 100.f);
        TestFalse(TEXT("A flush blocked dash is a no-effect input"), Move->TryBufferDash(FVector::ForwardVector));
        TestEqual(TEXT("Blocked dash preserves its final charge"), Move->GetDashCharges(), 1);
        Scene.Step(Fps, 1.f / Fps);
        TestTrue(TEXT("Reserve decay cannot restore speed removed by collision"), Move->Velocity.Size2D() <= CollisionSpeed + 1.);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootGuardV04Test,
    "Bootloop.V04.Abilities.GuardContacts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootGuardV04Test::RunTest(const FString&)
{
    FBootMovementV04World Scene;
    auto* Guard = NewObject<UBootAbilityComponent>(Scene.Runner);
    Guard->RegisterComponent();
    FBootRunToken Run; Run.RunId = FGuid::NewGuid(); Run.Generation = 7;
    Guard->ResetRun(Run);
    auto E = FBootTraversalPolicy::FromTempo(130., 1); E.GuardCharges = 2; E.DashCharges = 1;
    Scene.Move->ApplyTraversalEnvelope(E); Guard->ApplyEnvelope(E);
    TestEqual(TEXT("The snapshot reads physical dash capacity from movement"), Guard->Snapshot().DashCharges, 1);
    TestEqual(TEXT("Two guard stacks open exactly two charges"), Guard->Snapshot().GuardCharges, 2);
    TestFalse(TEXT("Falling is never treated as a protected process"), Guard->TryAbsorbProcess(Run, -1, 1.));
    TestTrue(TEXT("A process contact consumes one guard"), Guard->TryAbsorbProcess(Run, 42, 1.));
    TestEqual(TEXT("Guard contact spends only one charge"), Guard->Snapshot().GuardCharges, 1);
    TestTrue(TEXT("Immediate overlap repetition is protected"), Guard->TryAbsorbProcess(Run, 42, 1.1));
    TestTrue(TEXT("The same unseparated sweep stays protected after the grace interval"), Guard->TryAbsorbProcess(Run, 42, 2.));
    TestEqual(TEXT("Persistent contact never drains the second guard"), Guard->Snapshot().GuardCharges, 1);
    Guard->NotifyProcessSeparated(42);
    TestTrue(TEXT("A new contact after separation uses the next guard"), Guard->TryAbsorbProcess(Run, 42, 2.1));
    TestEqual(TEXT("Separated recontact spends the second charge"), Guard->Snapshot().GuardCharges, 0);
    Guard->ApplyEnvelope(E);
    TestEqual(TEXT("Reapplying a stack cannot produce unlimited guard refill"), Guard->Snapshot().GuardCharges, 0);
    TestFalse(TEXT("An unprotected distinct later process remains fatal"), Guard->TryAbsorbProcess(Run, 43, 3.));
    FBootRunToken Stale = Run; --Stale.Generation;
    TestFalse(TEXT("Stale generation cannot absorb contacts"), Guard->TryAbsorbProcess(Stale, 99, 3.));
    Guard->NotifySurfCompleted({Run, 10, 0});
    Guard->NotifySurfCompleted({Run, 10, 0});
    TestEqual(TEXT("Duplicate accepted callback cannot count twice"), Guard->Snapshot().GuardRechargeProgress, 1);
    Guard->NotifySurfCompleted({Stale, 10, 1});
    Guard->NotifySurfCompleted({Run, 10, 1});
    TestEqual(TEXT("Two unique accepted operations do not refill yet"), Guard->Snapshot().GuardCharges, 0);
    Guard->NotifySurfCompleted({Run, 10, 2});
    TestEqual(TEXT("Three unique accepted surf operations refill one charge"), Guard->Snapshot().GuardCharges, 1);
    TestEqual(TEXT("The next refill starts at zero progress"), Guard->Snapshot().GuardRechargeProgress, 0);
    const int32 Serial = Guard->Snapshot().GuardEventSerial;
    ++Run.Generation; Guard->ResetRun(Run);
    TestEqual(TEXT("Retry clears guard build and recharge history"), Guard->Snapshot().MaxGuardCharges, 0);
    TestEqual(TEXT("Reset does not fake a new visual guard event"), Guard->Snapshot().GuardEventSerial, Serial);
    return true;
}
#endif
