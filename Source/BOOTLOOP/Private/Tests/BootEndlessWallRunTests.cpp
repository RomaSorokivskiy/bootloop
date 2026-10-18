#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BootMovementV04TestWorld.h"
#include "World/Generation/BootEndlessPlanner.h"
#include "World/Generation/BootEndlessChunk.h"
#include "Components/InstancedStaticMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootEarlyEndlessWalls,
    "Bootloop.WallPatch.EarlyWalls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootEarlyEndlessWalls::RunTest(const FString&)
{
    for (int32 Seed = 0; Seed < 8; ++Seed)
    {
        FBootEndlessPlanner Planner;
        FBootPlanRequest Request;
        Request.Run.RunId = FGuid(1, 2, 3, 4);
        Request.CurrentEnvelope = FBootTraversalPolicy::FromTempo(130., 1);
        Request.Tier = 5;
        Planner.Reset(Seed, Request.CurrentEnvelope);
        bool bHasWall = false;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const auto Plan = Planner.PlanNext(Request);
            if (!TestTrue(TEXT("Early continuation plans"), Plan.Committed.IsSet())) return false;
            const auto Chunk = Plan.Committed.GetValue();
            bHasWall |= Chunk.Recipe.Boxes.ContainsByPredicate([](const auto& Box)
                { return Box.Role == EBootRecipeRole::Wall; });
            Request.LiveDescriptors.Add(Chunk);
            Request.Connector = Chunk.WorldExit;
            Request.CanonicalStartMetres = Chunk.CanonicalEndMetres;
            ++Request.NextInstanceId;
        }
        TestTrue(*FString::Printf(TEXT("Seed %d includes a wall within the first three chunks"), Seed), bHasWall);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootNativeEndlessWallRun,
    "Bootloop.WallPatch.NativeWallRun", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootNativeEndlessWallRun::RunTest(const FString&)
{
    for (const FName Id : {FName(TEXT("wall.left")), FName(TEXT("wall.right")),
        FName(TEXT("flow.wall_exit.left")), FName(TEXT("flow.wall_exit.right"))})
    for (const double Yaw : {0., 90.})
    {
        FBootMovementV04World Scene;
        FBootEndlessPlanner Planner;
        FBootPlanRequest Request;
        Request.Run.RunId = FGuid(1, 2, 3, 4);
        Request.CurrentEnvelope = FBootTraversalPolicy::FromTempo(130., 1);
        Request.Connector = FTransform(FRotator(0., Yaw, 0.));
        Request.Tier = 5;
        FBootReplayDecision Replay;
        Replay.DefinitionId = Id;
        Request.ReplayDecision = Replay;
        Planner.Reset(1, Request.CurrentEnvelope);
        const auto Plan = Planner.PlanNext(Request);
        if (!TestTrue(TEXT("Actual wall recipe plans"), Plan.Committed.IsSet())) return false;
        const auto Descriptor = Plan.Committed.GetValue();
        auto* Chunk = Scene.World->SpawnActor<ABootEndlessChunk>();
        if (!TestTrue(TEXT("Actual wall collision prepared"), Chunk->Configure(Descriptor) && Chunk->FinishPreparation())) return false;
        TArray<UInstancedStaticMeshComponent*> Meshes;
        Chunk->GetComponents(Meshes);
        for (const auto* Mesh : Meshes)
            if (Mesh->GetFName() != TEXT("RecipeSolids1"))
                TestFalse(TEXT("Floors, ceilings and decoration cannot attach wallrun"), Mesh->ComponentHasTag(TEXT("BootWallRun")));
        const auto* Wall = Descriptor.Recipe.Boxes.FindByPredicate([](const auto& Box)
            { return Box.Role == EBootRecipeRole::Wall; });
        if (!TestNotNull(TEXT("Recipe includes a physical wall"), Wall)) return false;
        const double Side = FMath::Sign(Wall->Center.Y);
        const FVector LocalStart(Wall->Center.X - Wall->HalfSize.X + 150.,
            Wall->Center.Y - Side * (Wall->HalfSize.Y + 50.), 280.);
        Scene.Move->ApplyTraversalEnvelope(Request.CurrentEnvelope);
        Scene.Runner->SetActorRotation(Request.Connector.Rotator());
        Scene.Air(Request.Connector.TransformVectorNoScale(FVector(1100., 0., 50.)),
            Request.Connector.TransformPosition(LocalStart));
        Scene.Step(12);
        const FString Label = FString::Printf(TEXT("%s yaw %.0f"), *Id.ToString(), Yaw);
        TestTrue(*(Label + TEXT(" enters and sustains wallrun on generated collision")), Scene.Move->IsWallRunning());
        const FVector LocalEnd = Request.Connector.InverseTransformPosition(Scene.Runner->GetActorLocation());
        TestTrue(*(Label + TEXT(" advances along the wall")), LocalEnd.X > LocalStart.X + 180.);
        TestTrue(*(Label + TEXT(" permits a wallkick")), Scene.Move->TryWallKick());
        const FVector LocalVelocity = Request.Connector.InverseTransformVectorNoScale(Scene.Move->Velocity);
        TestTrue(*(Label + TEXT(" kick pushes away from the wall")), LocalVelocity.Y * Side < -300.);
    }
    return true;
}
#endif
