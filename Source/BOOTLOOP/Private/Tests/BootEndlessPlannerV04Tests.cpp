#include "Misc/AutomationTest.h"
#include "World/Generation/BootEndlessPlanner.h"
#include "Algo/Reverse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
void DumpFailure(uint64 Seed,int32 Index,const FBootPlanRequest& R)
{
    FString Json=FString::Printf(TEXT("{\"seed\":%llu,\"index\":%d,\"rulesVersion\":1,\"live\":["),Seed,Index);
    for(int32 I=0;I<R.LiveDescriptors.Num();++I){if(I)Json+=TEXT(",");Json+=FBootEndlessPlanner::BoundsJson(R.LiveDescriptors[I]);}
    Json+=TEXT("]}");
    FFileHelper::SaveStringToFile(Json,*(FPaths::ProjectSavedDir()/TEXT("V04PlannerFailingSeed.json")));
}
FBootPlanRequest Request()
{
    FBootPlanRequest R;
    R.Run.RunId = FGuid(1, 2, 3, 4);
    R.CurrentEnvelope = FBootTraversalPolicy::FromTempo(130., 1);
    R.MusicContext.TrackHash = TEXT("fixture-track");
    R.MusicContext.bSafeGrid = false;
    R.ActiveSeconds = 400.; R.Tier = 5;
    return R;
}
void Append(FBootPlanRequest& R, const FBootCommittedChunk& C)
{
    R.Connector = C.WorldExit; R.CanonicalStartMetres = C.CanonicalEndMetres;
    R.NextInstanceId = C.InstanceId + 1; R.LiveDescriptors.Add(C);
    // Simulated consumer passed and released oldest dependencies, never all historical geometry.
    if (R.LiveDescriptors.Num() >= 40) R.LiveDescriptors.RemoveAt(0);
    R.MusicContext.LocalSeconds += 2.; R.MusicContext.Section = int32(R.NextInstanceId / 8);
    R.MusicContext.Energy = float(R.NextInstanceId % 5) / 4.f;
    R.MusicContext.OnsetDensity = 1.f - R.MusicContext.Energy;
}
FBootCommittedChunk Gate(int64 Id, FVector End, double Yaw, double Progress)
{
    FBootCommittedChunk C; C.InstanceId = Id;
    C.WorldExit = FTransform(FRotator(0., Yaw, 0.), End);
    C.CanonicalStartMetres = Progress - 10.; C.CanonicalEndMetres = Progress;
    return C;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootPlannerSweepsV04, "Bootloop.V04.Planner.ConnectorsSweeps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootPlannerSweepsV04::RunTest(const FString&)
{
    const auto Catalog = FBootChunkCatalog::Build();
    if (!TestTrue(TEXT("at least twelve authored definitions"), Catalog.Num() >= 12)) return false;
    TSet<EBootChunkFamily> Families;
    for (const auto& D : Catalog)
    {
        FString Error; TestTrue(*FString::Printf(TEXT("catalog %s: %s"), *D.Id.ToString(), *Error), FBootChunkCatalog::Validate(D, Error));
        Families.Add(D.Family); TestEqual(TEXT("baseline requires no optional ability"), D.RequiredAbilityMask, uint32(0));
        TestTrue(TEXT("runtime solid recipe"), !D.Recipe.Boxes.IsEmpty());
        TestTrue(TEXT("baseline path"), D.Recipe.Baseline.Num() >= 2);
        for (const auto& H : D.Recipe.Hazards) TestTrue(TEXT("hazard warning >= .9s"), H.TelegraphSeconds >= .9);
    }
    TestEqual(TEXT("six authored families"), Families.Num(), 6);
    for (uint64 Seed = 0; Seed < 100; ++Seed)
    {
        FBootEndlessPlanner P; auto R = Request(); P.Reset(Seed, R.CurrentEnvelope);
        R.Connector = FTransform(FRotator(0., 37., 0.), FVector(100., -300., 70.));
        const FVector Frontier=R.Connector.GetUnitAxis(EAxis::X);
        for (int32 I = 0; I < 200; ++I)
        {
            const auto Result = P.PlanNext(R);
            if (!Result.Committed.IsSet()) { DumpFailure(Seed,I,R); AddError(FString::Printf(TEXT("seed=%llu index=%d %s; Saved/V04PlannerFailingSeed.json"), Seed, I, *Result.Diagnostics)); return false; }
            const auto& C = Result.Committed.GetValue();
            TestTrue(TEXT("connector"), C.WorldEntry.Equals(R.Connector, .1));
            TestTrue(TEXT("bounded selection"), Result.AttemptCount <= 16);
            TestFalse(TEXT("full swept unrelated overlaps"), FBootEndlessPlanner::HasUnrelatedOverlap(C, R.LiveDescriptors));
            TestTrue(TEXT("reserved continuation"), C.ReservedContinuation.IsValid != 0);
            TestTrue(TEXT("positive canonical interval"), C.CanonicalEndMetres > C.CanonicalStartMetres);
            TestTrue(TEXT("live bounded"), R.LiveDescriptors.Num() < 40);
            TestTrue(TEXT("escape frontier admits lateral directions but no backward exit"),FVector::DotProduct(C.WorldExit.GetUnitAxis(EAxis::X),Frontier)>=-.001);
            for(int32 Point=1;Point<C.Recipe.Baseline.Num();++Point)
                TestTrue(TEXT("baseline preserves rotated escape frontier"),FVector::DotProduct(C.WorldTransform.TransformVectorNoScale(C.Recipe.Baseline[Point]-C.Recipe.Baseline[Point-1]),Frontier)>=-.01);
            if (C.BranchId != 0) TestEqual(TEXT("same merge progress"), C.BranchMergeMetres, C.CanonicalEndMetres);
            Append(R, C);
        }
    }
    auto C = Gate(1, FVector::ZeroVector, 0., 10.);
    C.WorldOccupied.Add(FBox(FVector(0.,0.,0.), FVector(100.,100.,100.)));
    auto Other = Gate(9, FVector::ZeroVector, 0., 20.);
    Other.WorldRailSweeps.Add(FBox(FVector(50.,50.,50.), FVector(200.,200.,200.)));
    TestTrue(TEXT("full rail sweep collision matters"), FBootEndlessPlanner::HasUnrelatedOverlap(C, {Other}));
    Other.WorldRailSweeps.Empty(); Other.WorldHazardSweeps.Add(FBox(FVector(50.,50.,50.), FVector(200.,200.,200.)));
    TestTrue(TEXT("full hazard sweep collision matters"), FBootEndlessPlanner::HasUnrelatedOverlap(C, {Other}));
    Other.WorldHazardSweeps.Empty(); Other.WorldBodyClearance.Add(FBox(FVector(50.,50.,50.), FVector(200.,200.,200.)));
    TestTrue(TEXT("body clearance is protected"), FBootEndlessPlanner::HasUnrelatedOverlap(C, {Other}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootPlannerReplayV04, "Bootloop.V04.Planner.DeterministicReplay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootPlannerReplayV04::RunTest(const FString&)
{
    for (uint64 Seed = 0; Seed < 100; ++Seed)
    {
        FBootEndlessPlanner A, B, Replay; auto RA = Request(), RB = RA, RR = RA;
        A.Reset(Seed, RA.CurrentEnvelope); B.Reset(Seed, RA.CurrentEnvelope); Replay.Reset(999, RA.CurrentEnvelope);
        auto Catalog = FBootChunkCatalog::Build(); Algo::Reverse(Catalog); B.SetCatalog(Catalog);
        for (int32 I = 0; I < 200; ++I)
        {
            B.NextCosmeticRandom(); B.NextCardRandom(); B.NextCosmeticRandom();
            auto CA = A.PlanNext(RA), CB = B.PlanNext(RB);
            if (!TestTrue(TEXT("both deterministic runs commit"), CA.Committed.IsSet() && CB.Committed.IsSet())) return false;
            const uint64 ReplayHashA = FBootEndlessPlanner::DescriptorHash(CA.Committed.GetValue());
            const uint64 ReplayHashB = FBootEndlessPlanner::DescriptorHash(CB.Committed.GetValue());
            TestEqual(TEXT("replay"), ReplayHashA, ReplayHashB);
            if(I==0)
            {
                auto Changed=CA.Committed.GetValue();Changed.MusicContext.TrackHash=TEXT("different-context");
                TestTrue(TEXT("hash detects assigned music changes"),ReplayHashA!=FBootEndlessPlanner::DescriptorHash(Changed));
            }
            FBootReplayDecision Decision; Decision.DefinitionId = CA.Committed->DefinitionId;
            Decision.DefinitionVersion = CA.Committed->DefinitionVersion; Decision.MusicContext = CA.Committed->MusicContext;
            Decision.Variant = CA.Committed->Recipe.Variant; RR.ReplayDecision = Decision;
            RR.MusicContext.TrackHash = TEXT("different-live-music");
            auto CR = Replay.PlanNext(RR);
            if (!TestTrue(TEXT("recorded context replay commits"), CR.Committed.IsSet())) return false;
            TestEqual(TEXT("recorded immutable context hash"), ReplayHashA, FBootEndlessPlanner::DescriptorHash(CR.Committed.GetValue()));
            Append(RA, CA.Committed.GetValue()); Append(RB, CB.Committed.GetValue()); Append(RR, CR.Committed.GetValue());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootPlannerFallbackV04, "Bootloop.V04.Planner.FallbackBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootPlannerFallbackV04::RunTest(const FString&)
{
    FBootEndlessPlanner P; auto R = Request(); P.Reset(1, R.CurrentEnvelope);
    R.ActiveSeconds = 0.; R.Tier = 0;
    auto First = P.PlanNext(R);
    if (!TestTrue(TEXT("initial safe continuation"), First.Committed.IsSet())) return false;
    Append(R, First.Committed.GetValue());
    TArray<FBootChunkDefinition> SafeOnly;
    for (auto D : FBootChunkCatalog::Build()) if (D.bSafeContinuation) SafeOnly.Add(D);
    P.SetCatalog(SafeOnly);
    auto Safe = P.PlanNext(R);
    TestTrue(TEXT("reserved fallback commits"), Safe.Committed.IsSet());
    TestTrue(TEXT("fallback surfaced"), Safe.bUsedFallback);
    TestTrue(TEXT("bounded fallback"), Safe.AttemptCount <= 16);
    R.LiveDescriptors.SetNum(40);
    TestTrue(TEXT("hard live budget"), P.PlanNext(R).Failure == EBootPlanFailure::LiveBudget);
    R = Request(); R.CurrentEnvelope.bValid = false;
    TestTrue(TEXT("invalid envelope rejected"), P.PlanNext(R).Failure == EBootPlanFailure::InvalidRequest);
    R = Request(); auto InvalidPending = R.CurrentEnvelope; InvalidPending.GravityScale = 0.f;
    TestFalse(TEXT("pending card geometry eligibility"), P.CanPrepareEnvelope(R, InvalidPending));
    TestTrue(TEXT("saturating difficulty"), FBootEndlessPlanner::DifficultyBudget(1.e10, 99) <= 5.f);
    TestTrue(TEXT("difficulty grows with actual active time"), FBootEndlessPlanner::DifficultyBudget(300., 5) > FBootEndlessPlanner::DifficultyBudget(0., 5));
    R=Request();
    FBootCommittedChunk Blocker;Blocker.Run=R.Run;Blocker.InstanceId=1;
    Blocker.WorldOccupied.Add(FBox(FVector(0.,-10000.,-10000.),FVector(20000.,10000.,10000.)));
    R.LiveDescriptors.Add(Blocker);R.NextInstanceId=2;
    const auto Blocked=P.PlanNext(R);
    TestFalse(TEXT("blocked fallback never forces overlapping spawn"),Blocked.Committed.IsSet());
    TestTrue(TEXT("blocked selection bounded"),Blocked.AttemptCount<=16);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootPlannerDirectedV04, "Bootloop.V04.Planner.DirectedMerge", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootPlannerDirectedV04::RunTest(const FString&)
{
    FBootRouteCursor Cursor; Cursor.Reset();
    TArray<FBootCommittedChunk> Gates{Gate(1, FVector(1000.,0.,0.), 0.,10.), Gate(2,FVector(1000.,1000.,0.),90.,20.), Gate(3,FVector(0.,1000.,0.),180.,30.), Gate(4,FVector(0.,0.,600.),-90.,40.)};
    TestEqual(TEXT("reverse cannot award"), Cursor.Advance(FVector(1100.,0.,90.), FVector(900.,0.,90.), Gates).Num(), 0);
    TestEqual(TEXT("stacked crossing cannot skip expected gate"), Cursor.Advance(FVector(1000.,900.,690.),FVector(1000.,1100.,690.),Gates).Num(),0);
    TestEqual(TEXT("first gate"),Cursor.Advance(FVector(900.,0.,90.),FVector(1100.,0.,90.),Gates).Num(),1);
    TestEqual(TEXT("turn gate"),Cursor.Advance(FVector(1000.,900.,90.),FVector(1000.,1100.,90.),Gates).Num(),1);
    TestEqual(TEXT("return leg decreasing world X"),Cursor.Advance(FVector(100.,1000.,90.),FVector(-100.,1000.,90.),Gates).Num(),1);
    TestEqual(TEXT("directed canonical progress"),Cursor.CanonicalMetres,30.);
    TestEqual(TEXT("reverse old gate remains ignored"),Cursor.Advance(FVector(-100.,1000.,90.),FVector(100.,1000.,90.),Gates).Num(),0);
    Cursor.Reset();
    Gates={Gate(1,FVector(1000.,0.,0.),0.,10.),Gate(2,FVector(2000.,0.,0.),0.,20.),Gate(3,FVector(3000.,0.,0.),0.,30.)};
    TestEqual(TEXT("high speed crosses two prepared gates in order"),Cursor.Advance(FVector(900.,0.,90.),FVector(2100.,0.,90.),Gates).Num(),2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootPlannerSpatialV04, "Bootloop.V04.Planner.CatalogSpatialVariety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootPlannerSpatialV04::RunTest(const FString&)
{
    bool Left=false,Right=false,Raised=false;
    for(const auto& D:FBootChunkCatalog::Build())
    {
        const double Yaw=FMath::FindDeltaAngleDegrees(D.Entry.Rotator().Yaw,D.Exit.Rotator().Yaw);
        const bool IsLeft=FMath::IsNearlyEqual(Yaw,90.,.1),IsRight=FMath::IsNearlyEqual(Yaw,-90.,.1);
        bool HasRaisedSolid=false;
        for(const auto& B:D.Recipe.Boxes)
            if(B.Role==EBootRecipeRole::Floor && B.Center.Z+B.HalfSize.Z>=200.)HasRaisedSolid=true;
        const bool IsRaised=D.Exit.GetLocation().Z-D.Entry.GetLocation().Z>=200. && HasRaisedSolid;
        Left|=IsLeft;Right|=IsRight;Raised|=IsRaised;
        if(!(IsLeft||IsRight||IsRaised))continue;
        auto R=Request();FBootEndlessPlanner P;P.Reset(44,R.CurrentEnvelope);
        FBootReplayDecision Choice;Choice.DefinitionId=D.Id;Choice.DefinitionVersion=D.Version;Choice.MusicContext=R.MusicContext;R.ReplayDecision=Choice;
        const auto Result=P.PlanNext(R);
        if(!TestTrue(TEXT("spatial recipe is actually placeable"),Result.Committed.IsSet()))continue;
        const auto& C=Result.Committed.GetValue();
        TestTrue(TEXT("spatial entry alignment"),C.WorldEntry.Equals(R.Connector,.1));
        TestTrue(TEXT("actual transformed exit orientation/elevation"),C.WorldExit.Equals(D.Exit,.1));
        // Every authored baseline point at a connector or turn must have a real floor underfoot.
        for(const auto& Point:D.Recipe.Baseline)
        {
            bool Supported=false;
            for(const auto& B:D.Recipe.Boxes)
                if(B.Role==EBootRecipeRole::Floor && FMath::Abs(Point.X-B.Center.X)<=B.HalfSize.X+.1
                    &&FMath::Abs(Point.Y-B.Center.Y)<=B.HalfSize.Y+.1 &&FMath::Abs(Point.Z-(B.Center.Z+B.HalfSize.Z))<.1)Supported=true;
            TestTrue(TEXT("authored spatial waypoint has matching real support height"),Supported);
        }
    }
    TestTrue(TEXT("catalog has real +90 degree connector"),Left);
    TestTrue(TEXT("catalog has real -90 degree connector"),Right);
    TestTrue(TEXT("catalog has physically raised supported exit >=200cm"),Raised);
    auto Catalog=FBootChunkCatalog::Build();
    if(!Catalog.IsEmpty())
    {
        auto Bad=Catalog[0];Bad.Recipe.Boxes[0].Center+=FVector(0.,0.,10000.);FString Error;
        TestFalse(TEXT("declared bounds must cover actual recipe solids"),FBootChunkCatalog::Validate(Bad,Error));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootPlannerNativeFootprintsV04, "Bootloop.V04.Planner.NativeFootprints", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootPlannerNativeFootprintsV04::RunTest(const FString&)
{
    bool PopupFound=false;
    for(const auto& D:FBootChunkCatalog::Build())for(const auto& H:D.Recipe.Hazards)if(H.bPopup)
    {
        PopupFound=true;
        // ABootPopupProcess inherits the actual1000x750x80 Surf solid; hostile panel is smaller.
        TestTrue(TEXT("popup reservation includes native platform width and length"),H.Sweep.GetSize().X>=1000.&&H.Sweep.GetSize().Y>=750.&&H.Sweep.GetSize().Z>=80.);
        for(const auto& Clearance:D.BodyClearance)
        {
            const FVector Min=Clearance.Min.ComponentMax(H.Sweep.Min),Max=Clearance.Max.ComponentMin(H.Sweep.Max);
            TestFalse(TEXT("baseline bypass clear of complete popup sweep"),(Max-Min).GetMin()>.01);
        }
    }
    TestTrue(TEXT("native popup recipe exists"),PopupFound);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootPlannerRiskConsumersV04,"Bootloop.V04.Planner.RiskCardConsumers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootPlannerRiskConsumersV04::RunTest(const FString&)
{
    const auto Catalog=FBootChunkCatalog::Build();const auto R=Request();
    const auto* Crossing=Catalog.FindByPredicate([](const auto& D){return D.Id==TEXT("surf.crossing");});
    const auto* Branch=Catalog.FindByPredicate([](const auto& D){return D.Id==TEXT("surf.branch");});
    if(!TestTrue(TEXT("mandatory and optional surf definitions exist"),Crossing&&Branch))return false;
    FBootRunModifiers Normal,Unsafe;Unsafe.RiskySurfWeightScale=1.4f;
    const double CrossingNormal=FBootEndlessPlanner::SelectionWeight(*Crossing,R.MusicContext,Normal);
    const double BranchNormal=FBootEndlessPlanner::SelectionWeight(*Branch,R.MusicContext,Normal);
    TestTrue(TEXT("mandatory crossing retains positive ordinary weight"),CrossingNormal>0.);
    TestEqual(TEXT("unsafe windows never increases mandatory crossing weight"),FBootEndlessPlanner::SelectionWeight(*Crossing,R.MusicContext,Unsafe),CrossingNormal);
    TestTrue(TEXT("unsafe windows increases only optional risky branch weight"),FBootEndlessPlanner::SelectionWeight(*Branch,R.MusicContext,Unsafe)>BranchNormal*1.39);
    for(int32 Cost:{1,2})
    {
        const auto* Definition=Catalog.FindByPredicate([&](const auto& D){return D.CombinationCost==Cost;});
        if(!TestTrue(*FString::Printf(TEXT("authored combination exists for HOT LOOP stack %d"),Cost),Definition!=nullptr))continue;
        TSet<EBootBaselineMove> Mechanics;for(const auto& Move:Definition->Recipe.BaselineMoves)Mechanics.Add(Move.Kind);
        TestTrue(TEXT("combination contains multiple actual required baseline mechanics"),Mechanics.Num()>=2);
        for(int32 Stacks=0;Stacks<=2;++Stacks)
        {
            auto Candidate=Request();Candidate.ActiveSeconds=900.;Candidate.Tier=5;Candidate.Modifiers.ExtraCombinationBudget=Stacks;
            FBootReplayDecision Replay;Replay.DefinitionId=Definition->Id;Replay.DefinitionVersion=Definition->Version;Replay.MusicContext=Candidate.MusicContext;Candidate.ReplayDecision=Replay;
            FBootEndlessPlanner Planner;Planner.Reset(812,Candidate.CurrentEnvelope);
            TestEqual(*FString::Printf(TEXT("HOT LOOP saturated tier cost%d stacks%d"),Cost,Stacks),Planner.PlanNext(Candidate).Committed.IsSet(),Stacks>=Cost);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootPlannerProcessBudgetV04,"Bootloop.V04.Planner.ProcessCardBudget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootPlannerProcessBudgetV04::RunTest(const FString&)
{
    const auto Catalog=FBootChunkCatalog::Build();
    for(int32 Cost:{2,3})
    {
        const auto* Definition=Catalog.FindByPredicate([&](const auto& D){return D.ProcessCost==Cost;});
        if(!TestTrue(*FString::Printf(TEXT("authored process sequence exists with %d physical threats"),Cost),Definition!=nullptr))continue;
        TestEqual(TEXT("process budget counts actual hazard actors"),Definition->Recipe.Hazards.Num(),Cost);
        TSet<int32> LocalIds;for(const auto& Hazard:Definition->Recipe.Hazards)LocalIds.Add(Hazard.OperationId);
        TestEqual(TEXT("each process has a distinct local operation ID"),LocalIds.Num(),Cost);
        for(int32 Stacks=0;Stacks<=2;++Stacks)
        {
            auto Candidate=Request();Candidate.ActiveSeconds=900.;Candidate.Modifiers.ExtraProcessBudget=Stacks;
            FBootReplayDecision Replay;Replay.DefinitionId=Definition->Id;Replay.DefinitionVersion=Definition->Version;Replay.MusicContext=Candidate.MusicContext;Candidate.ReplayDecision=Replay;
            FBootEndlessPlanner Planner;Planner.Reset(901,Candidate.CurrentEnvelope);
            const auto Planned=Planner.PlanNext(Candidate);
            TestEqual(*FString::Printf(TEXT("PRIORITY saturated tier cost%d stacks%d"),Cost,Stacks),Planned.Committed.IsSet(),1+Stacks>=Cost);
            if(Planned.Committed.IsSet())TestEqual(TEXT("all native threat keys preserved"),Planned.Committed->OperationKeys.Num(),Cost);
        }
    }
    for(int32 Stacks=0;Stacks<=2;++Stacks)
    {
        int32 MaxCombo=0,MaxProcess=0;
        for(uint64 Seed=0;Seed<256;++Seed)
        {
            auto Candidate=Request();Candidate.Modifiers.ExtraCombinationBudget=Stacks;Candidate.Modifiers.ExtraProcessBudget=Stacks;
            FBootEndlessPlanner Planner;Planner.Reset(Seed,Candidate.CurrentEnvelope);const auto Planned=Planner.PlanNext(Candidate);
            if(!Planned.Committed.IsSet())continue;
            const auto* Definition=Catalog.FindByPredicate([&](const auto& D){return D.Id==Planned.Committed->DefinitionId;});
            if(Definition){MaxCombo=FMath::Max(MaxCombo,Definition->CombinationCost);MaxProcess=FMath::Max(MaxProcess,Definition->ProcessCost);}
        }
        TestEqual(TEXT("normal generator actually selects each unlocked combination tier"),MaxCombo,Stacks);
        TestEqual(TEXT("normal generator actually selects each unlocked process tier"),MaxProcess,1+Stacks);
    }
    return true;
}
