#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BootMovementV04TestWorld.h"
#include "World/Generation/BootEndlessPlanner.h"
#include "World/Generation/BootEndlessChunk.h"
#include "World/Interaction/BootSurfWindow.h"
#include "World/Threats/BootScanProcess.h"
#include "World/Threats/BootPopupProcess.h"
#include "EngineUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootFlowOpening,"Bootloop.FlowPatch.OpeningAndVariety",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootFlowOpening::RunTest(const FString&)
{
    TestTrue(TEXT("basic challenges available from start"),FBootEndlessPlanner::DifficultyBudget(0.,5)>=1.f);
    TestTrue(TEXT("difficulty grows before a full minute"),FBootEndlessPlanner::DifficultyBudget(30.,5)>=2.f);
    const auto Catalog=FBootChunkCatalog::Build();
    for(int32 Seed=0;Seed<8;++Seed)
    {
        FBootEndlessPlanner Planner;FBootPlanRequest R;R.Run.RunId=FGuid(1,2,3,4);R.CurrentEnvelope=FBootTraversalPolicy::FromTempo(130.,1);R.Tier=5;Planner.Reset(Seed,R.CurrentEnvelope);
        TSet<int32> Families;
        for(int32 I=0;I<12;++I)
        {
            auto P=Planner.PlanNext(R);if(!TestTrue(TEXT("bounded valid continuation exists"),P.Committed.IsSet()))return false;
            const auto C=P.Committed.GetValue();const auto* D=Catalog.FindByPredicate([&](const auto& V){return V.Id==C.DefinitionId;});
            if(!TestNotNull(TEXT("catalog identity"),D))return false;Families.Add(int32(D->Family));
            if(I==0){TestTrue(TEXT("opening has an actual direction change"),D->Recipe.Baseline.Num()>2);if(D->Recipe.Baseline.Num()>2)TestTrue(TEXT("first bend within two seconds at 130 BPM"),D->Recipe.Baseline[1].Size2D()<=2200.);}
            TestTrue(TEXT("normal route excludes the old forced-wait crossings"),D->Id!=TEXT("surf.crossing")&&D->Id!=TEXT("surf.branch"));
            if((I+1)%4==0)TestTrue(TEXT("recovery is a short breathing space"),C.CanonicalEndMetres-C.CanonicalStartMetres<=36.01);
            R.LiveDescriptors.Add(C);R.Connector=C.WorldExit;R.CanonicalStartMetres=C.CanonicalEndMetres;++R.NextInstanceId;
        }
        TestTrue(TEXT("opening mixes at least three families"),Families.Num()>=3);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootFlowMomentum,"Bootloop.FlowPatch.SurfMomentum",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootFlowMomentum::RunTest(const FString&)
{
    FBootMovementV04World W;W.Step(20);W.Move->ApplyTraversalEnvelope(FBootTraversalPolicy::FromTempo(130.,1));W.Move->Velocity=FVector(1500,0,0);
    const FVector Before=W.Move->Velocity;
    TestTrue(TEXT("moving rider can claim flow reserve"),W.Move->GrantSurfMomentum());
    TestTrue(TEXT("reserve never applies an automatic impulse"),W.Move->Velocity.Equals(Before,.01));
    TestEqual(TEXT("fixed reserve"),W.Move->GetSurfMomentumBonus(),350.f);
    TestEqual(TEXT("bhop ceiling plus 3.5 metres per second"),W.Move->GetEffectiveMomentumCap(),2350.f);
    W.Move->GrantSurfMomentum();TestEqual(TEXT("repeated windows cannot stack reserve"),W.Move->GetSurfMomentumBonus(),350.f);
    for(int32 Hop=0;Hop<14;++Hop)
    {
        W.Runner->RequestJump();W.Step(2,1.f/60.f,true);
        if(Hop==13)break;
        for(int32 I=0;I<100&&!W.Move->IsMovingOnGround();++I)W.Step(1,1.f/60.f,true);
    }
    TestTrue(TEXT("manual hops reach the extended ceiling"),W.Move->Velocity.Size2D()>2250.&&W.Move->Velocity.Size2D()<=2351.);
    TestEqual(TEXT("chain retains reserve without a timer"),W.Move->GetSurfMomentumBonus(),350.f);
    for(int32 I=0;I<100&&!W.Move->IsMovingOnGround();++I)W.Step(1,1.f/60.f,true);
    W.Step(30,1.f/60.f,true);TestEqual(TEXT("ordinary grounded running ends the chain"),W.Move->GetSurfMomentumBonus(),0.f);
    W.Move->GrantSurfMomentum();W.Move->ResetParkour();TestEqual(TEXT("retry clears reserve"),W.Move->GetSurfMomentumBonus(),0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootFlowPass,"Bootloop.FlowPatch.SurfPassThrough",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootFlowPass::RunTest(const FString&)
{
    FBootMovementV04World W;W.Move->ApplyTraversalEnvelope(FBootTraversalPolicy::FromTempo(130.,1));
    auto* Window=W.World->SpawnActor<ABootSurfWindow>();Window->Configure(FVector(1000,0,82),FVector(2500,0,82),2.5f,91);Window->SetFlowRide(true);
    W.Runner->SetActorLocation(FVector(500,0,350),false,nullptr,ETeleportType::TeleportPhysics);W.Move->SetMovementMode(MOVE_Falling);W.Move->Velocity=FVector(1100,0,-300);
    TestTrue(TEXT("offbeat activation accepted"),Window->Activate(W.Runner,false));
    bool Claimed=false,Carried=false;float MinimumSpeed=1100.f;
    for(int32 I=0;I<180&&!Claimed;++I)
    {
        W.Step(1,1.f/60.f,true);MinimumSpeed=FMath::Min(MinimumSpeed,float(W.Move->Velocity.Size2D()));Carried|=Window->HasCarried(W.Runner);
        if(Window->ConsumeDepartureBoost(W.Runner)){Claimed=true;TestTrue(TEXT("boost available before the platform finishes"),Window->IsMoving());}
    }
    TestTrue(TEXT("real support and carry observed"),Carried);TestTrue(TEXT("short pass earns offbeat flow charge"),Claimed);
    TestTrue(TEXT("no stop needed to cross the platform"),MinimumSpeed>900.f);
    TestFalse(TEXT("one platform cannot award twice"),Window->ConsumeDepartureBoost(W.Runner));
    // A manual hop may leave on the very first supported frame: real contact
    // must suffice for flow windows, without the legacy minimum carry duration.
    Window->ResetWindow();W.Move->ResetParkour();W.Move->ApplyTraversalEnvelope(FBootTraversalPolicy::FromTempo(130.,1));
    W.Runner->SetActorLocation(FVector(1000,0,300),false,nullptr,ETeleportType::TeleportPhysics);W.Move->SetMovementMode(MOVE_Falling);W.Move->Velocity=FVector::ZeroVector;
    for(int32 I=0;I<120&&W.Runner->GetMovementBaseObject()!=Window->GetRootComponent();++I)W.Step(1);
    if(!TestTrue(TEXT("brief touch is real collision support"),W.Runner->GetMovementBaseObject()==Window->GetRootComponent()))return false;
    Window->Activate(W.Runner,false);TestTrue(TEXT("flow accepts a single supported frame before buffered hop"),Window->HasCarried(W.Runner));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootFlowHazards,"Bootloop.FlowPatch.LocalHazardDirection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootFlowHazards::RunTest(const FString&)
{
    for(const double Yaw:{0.,90.,180.})for(const FName Id:{FName(TEXT("process.scan")),FName(TEXT("process.popup"))})
    {
        FBootMovementV04World W;FBootEndlessPlanner Planner;FBootPlanRequest R;R.Run.RunId=FGuid(1,2,3,4);R.CurrentEnvelope=FBootTraversalPolicy::FromTempo(130.,1);R.Connector=FTransform(FRotator(0,Yaw,0));R.Tier=5;R.ActiveSeconds=400.;Planner.Reset(1,R.CurrentEnvelope);
        FBootReplayDecision Choice;Choice.DefinitionId=Id;R.ReplayDecision=Choice;auto P=Planner.PlanNext(R);if(!TestTrue(TEXT("process fixture plans"),P.Committed.IsSet()))return false;
        auto* Chunk=W.World->SpawnActor<ABootEndlessChunk>();if(!TestTrue(TEXT("process fixture prepared"),Chunk->Configure(P.Committed.GetValue())&&Chunk->FinishPreparation()))return false;
        Chunk->SetProcessesActive(true);Chunk->UpdateApproach(R.Connector.TransformPosition(FVector(4400,-400,98)),R.CurrentEnvelope);
        AActor* Threat=nullptr;if(Id==TEXT("process.scan")){for(TActorIterator<ABootScanProcess> It(W.World);It;++It)Threat=*It;}else for(TActorIterator<ABootPopupProcess> It(W.World);It;++It)Threat=*It;
        if(!TestNotNull(TEXT("actual moving threat"),Threat))return false;const FVector Before=Threat->GetActorLocation();W.Step(96);
        TestTrue(*FString::Printf(TEXT("%s yaw%.0f approaches local route: %s -> %s"),*Id.ToString(),Yaw,*Before.ToString(),*Threat->GetActorLocation().ToString()),FVector::DotProduct(Threat->GetActorLocation()-Before,R.Connector.GetUnitAxis(EAxis::X))<-1.);
    }
    return true;
}
#endif
