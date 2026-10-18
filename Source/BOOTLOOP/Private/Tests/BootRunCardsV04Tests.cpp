#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Progression/BootRunProgressionComponent.h"
namespace
{
FBootCardEligibilityContext CardContext()
{
    FBootCardEligibilityContext C; C.Envelope = FBootTraversalPolicy::FromTempo(130., 1);
    C.bCanPrepareProcessBudget = C.bCanPrepareCombinations = C.bCanPrepareThinCache = C.bCanPrepareRiskySurf = true;
    return C;
}
FBootRunToken CardToken() { return {FGuid::NewGuid(), 1}; }
}
// Catches missing effects, nonlinear reward stacking and cap/no-effect offers.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootCardCatalogV04Test, "Bootloop.V04.Cards.CatalogCapsAndRisk", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootCardCatalogV04Test::RunTest(const FString&)
{
    TestEqual(TEXT("catalog"), FBootRunCardRules::Catalog().Num(), 12);
    int32 Buffs = 0, Risks = 0;
    for (const auto& D : FBootRunCardRules::Catalog()) { Buffs += D.Kind == EBootCardKind::Buff; Risks += D.Kind == EBootCardKind::Risk; }
    TestEqual(TEXT("eight buffs"), Buffs, 8); TestEqual(TEXT("four risks"), Risks, 4);
    auto C = CardContext(); FBootRunModifiers Out; FText Error;
    for (const auto& D : FBootRunCardRules::Catalog())
    {
        C = CardContext();
        for (int32 I = 0; I < D.MaxStacks; ++I)
        {
            if (!FBootRunCardRules::Eligible(C).Contains(D.Id)) break; // Numeric caps may precede stack caps.
            TestTrue(TEXT("legal effect applies"), FBootRunCardRules::Apply(D.Id, C, Out, Error));
            C.Modifiers = Out; C.Envelope = FBootRunCardRules::ApplyToEnvelope(CardContext().Envelope, Out);
        }
        TestFalse(TEXT("saturated card is ineligible"), FBootRunCardRules::Eligible(C).Contains(D.Id));
    }
    C = CardContext(); C.Modifiers.Stacks.Add(TEXT("HOT LOOP"), 2); C.Modifiers.Stacks.Add(TEXT("THIN CACHE"), 1);
    TestTrue(TEXT("second thin stack"), FBootRunCardRules::Apply(TEXT("THIN CACHE"), C, Out, Error));
    TestTrue(TEXT("distance bonuses add from base"), FMath::IsNearlyEqual(Out.DistanceMultiplier, 1.7));
    TestTrue(TEXT("width reduction from original base"), FMath::IsNearlyEqual(Out.WidthScale, .8f));
    C = CardContext(); C.Modifiers.Stacks.Add(TEXT("PRIORITY ESCALATION"), 2); C.Modifiers.Stacks.Add(TEXT("UNSAFE WINDOWS"), 1);
    TestTrue(TEXT("second unsafe stack"), FBootRunCardRules::Apply(TEXT("UNSAFE WINDOWS"), C, Out, Error));
    TestTrue(TEXT("operation bonuses additive"), FMath::IsNearlyEqual(Out.OperationMultiplier, 1.9));
    TestTrue(TEXT("relative surf weight"), FMath::IsNearlyEqual(Out.RiskySurfWeightScale, 1.4f));
    C = CardContext(); C.Envelope.AirControl = .85f; C.Envelope.HopGraceSeconds = .16f;
    TestFalse(TEXT("air control at cap omitted"), FBootRunCardRules::Eligible(C).Contains(TEXT("AERO BUFFER")));
    TestFalse(TEXT("hop grace at cap omitted"), FBootRunCardRules::Eligible(C).Contains(TEXT("SOFT LANDING")));
    C = CardContext(); C.bCanPrepareThinCache = false;
    TestFalse(TEXT("unvalidated narrow geometry omitted"), FBootRunCardRules::Eligible(C).Contains(TEXT("THIN CACHE")));
    FBootRunModifiers Full;
    Full.Stacks = {{TEXT("AIR CACHE"), 2}, {TEXT("BUFFER DASH"), 2}, {TEXT("PULSE GUARD"), 2},
        {TEXT("WALL CACHE"), 3}, {TEXT("AERO BUFFER"), 3}, {TEXT("SERVO RESERVE"), 3},
        {TEXT("SLIDE BUFFER"), 2}, {TEXT("SOFT LANDING"), 3}};
    const auto E = FBootRunCardRules::ApplyToEnvelope(CardContext().Envelope, Full);
    TestEqual(TEXT("air cache bounded capacity"), E.AirRedirectCharges, 3);
    TestEqual(TEXT("buffer dash capacity"), E.DashCharges, 2);
    TestEqual(TEXT("pulse guard capacity"), E.GuardCharges, 2);
    TestTrue(TEXT("wall duration"), FMath::IsNearlyEqual(E.WallRunSeconds, 1.75f));
    TestTrue(TEXT("air control cap"), FMath::IsNearlyEqual(E.AirControl, .85f));
    TestTrue(TEXT("overclock reserve duration"), FMath::IsNearlyEqual(E.OverclockSeconds, 1.35f));
    TestTrue(TEXT("slide braking never zero"), FMath::IsNearlyEqual(E.SlideBrakingScale, .7f));
    TestTrue(TEXT("fresh-hop grace cap"), FMath::IsNearlyEqual(E.HopGraceSeconds, .16f));
    TestEqual(TEXT("cards never raise absolute speed guard"), E.AbsoluteCmps, 3600.f);
    return true;
}
// Catches duplicates, lack of a buff, illegal drafts and consuming stale/held presses.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootCardDraftV04Test, "Bootloop.V04.Cards.DraftAndFreshChoice", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootCardDraftV04Test::RunTest(const FString&)
{
    auto C = CardContext(); const auto Run = CardToken(); FRandomStream R(51), R2(51);
    const auto O = FBootRunCardRules::BuildOffer(C, Run, 7, 25., R);
    const auto O2 = FBootRunCardRules::BuildOffer(C, Run, 7, 25., R2);
    TestTrue(TEXT("draft exists"), O.IsSet());
    if (!O.IsSet() || !O2.IsSet()) return false;
    TestEqual(TEXT("three choices"), O->CardIds.Num(), 3); TestTrue(TEXT("card seed stable"), O->CardIds == O2->CardIds);
    TSet<FName> Unique(O->CardIds); TestEqual(TEXT("no duplicates"), Unique.Num(), 3);
    bool bBuff = false;
    for (const auto& D : FBootRunCardRules::Catalog()) bBuff |= D.Kind == EBootCardKind::Buff && O->CardIds.Contains(D.Id);
    TestTrue(TEXT("at least one buff"), bBuff);
    for (const auto& D : FBootRunCardRules::Catalog()) if (D.Id != TEXT("AIR CACHE") && D.Id != TEXT("BUFFER DASH")) C.BlockedCardIds.Add(D.Id);
    TestFalse(TEXT("two legal defers"), FBootRunCardRules::BuildOffer(C, Run, 8, 25., R).IsSet());
    auto* P = NewObject<UBootRunProgressionComponent>(); P->ResetRun(Run, 51, CardContext().Envelope); P->SetEligibilityContext(CardContext()); P->MaybeOffer(25., 100., true);
    TestTrue(TEXT("progression draft"), P->GetOffer().IsSet()); if (!P->GetOffer().IsSet()) return false;
    FBootCardChoice Choice{Run, P->GetOffer()->OfferId, 0, 1, 25.}; FText Error;
    auto Bad = Choice; Bad.PressActiveSeconds = 24.9; TestFalse(TEXT("press before open"), P->TryChoose(Bad, Error));
    Bad = Choice; ++Bad.Run.Generation; TestFalse(TEXT("old run"), P->TryChoose(Bad, Error));
    Bad = Choice; --Bad.OfferId; TestFalse(TEXT("stale offer"), P->TryChoose(Bad, Error));
    TestTrue(TEXT("fresh press chooses atomically"), P->TryChoose(Choice, Error));
    TestFalse(TEXT("repeat"), P->TryChoose(Choice, Error));
    TestFalse(TEXT("chosen offer removed"), P->GetOffer().IsSet());
    return true;
}
// Catches time-only farming, modal timeout/autopick and pending draw accumulation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootCardLiveV04Test, "Bootloop.V04.Cards.LiveOffer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootCardLiveV04Test::RunTest(const FString&)
{
    const auto Run = CardToken();
    auto* TimeGate = NewObject<UBootRunProgressionComponent>(); TimeGate->ResetRun(Run, 3, CardContext().Envelope);
    TimeGate->MaybeOffer(24., 100., true); TestFalse(TEXT("needs time"), TimeGate->GetOffer().IsSet());
    TimeGate->MaybeOffer(25., 99., true);
    TestTrue(TEXT("backtracking cannot delay already earned first-offer distance"), TimeGate->GetOffer().IsSet());
    auto* DistanceGate = NewObject<UBootRunProgressionComponent>(); DistanceGate->ResetRun(Run, 3, CardContext().Envelope);
    DistanceGate->MaybeOffer(25., 99., true); TestFalse(TEXT("needs distance"), DistanceGate->GetOffer().IsSet());
    DistanceGate->MaybeOffer(25., 100., true); TestTrue(TEXT("distance threshold opens offer"), DistanceGate->GetOffer().IsSet());
    auto* P = NewObject<UBootRunProgressionComponent>(); P->ResetRun(Run, 3, CardContext().Envelope);
    P->MaybeOffer(25., 100., false); TestFalse(TEXT("needs recovery"), P->GetOffer().IsSet());
    P->MaybeOffer(25., 100., true); TestTrue(TEXT("both gates open"), P->GetOffer().IsSet()); if (!P->GetOffer().IsSet()) return false;
    const int64 Id = P->GetOffer()->OfferId; P->MaybeOffer(10000., 10000., true);
    TestTrue(TEXT("offer never times out"), P->GetOffer().IsSet()); TestEqual(TEXT("no queued replacement"), P->GetOffer()->OfferId, Id);
    FText Error; TestTrue(TEXT("late choice remains legal"), P->TryChoose({Run, Id, 0, 8, 10000.}, Error));
    P->MaybeOffer(20000., 20000., true); TestFalse(TEXT("pending blocks accumulation"), P->GetOffer().IsSet());
    return true;
}
// Catches committing without future-ready support or mixing run/revision handshakes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootCardPendingV04Test, "Bootloop.V04.Cards.PendingRevision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootCardPendingV04Test::RunTest(const FString&)
{
    auto* P = NewObject<UBootRunProgressionComponent>(); const auto Run = CardToken(); auto C = CardContext();
    for (const auto& D : FBootRunCardRules::Catalog()) if (D.Id != TEXT("HOT LOOP") && D.Id != TEXT("AIR CACHE") && D.Id != TEXT("BUFFER DASH")) C.BlockedCardIds.Add(D.Id);
    P->ResetRun(Run, 3, C.Envelope); P->SetEligibilityContext(C); P->MaybeOffer(25., 100., true);
    TestTrue(TEXT("constrained offer"), P->GetOffer().IsSet()); if (!P->GetOffer().IsSet()) return false;
    const int32 Index = P->GetOffer()->CardIds.IndexOfByKey(FName(TEXT("HOT LOOP"))); FText Error;
    TestTrue(TEXT("risk accepted"), P->TryChoose({Run, P->GetOffer()->OfferId, Index, 1, 25.}, Error));
    TestTrue(TEXT("world mutation pending"), P->HasPendingMutation());
    TestEqual(TEXT("old geometry active"), P->GetActiveModifiers().ExtraCombinationBudget, 0);
    TestTrue(TEXT("future reward immediate"), FMath::IsNearlyEqual(P->GetSelectedModifiers().DistanceMultiplier, 1.15));
    FBootSafeBoundary B{Run, 5, P->GetProposalRevision(), true, true, false};
    TestFalse(TEXT("unprepared future rejected"), P->TryCommit(B)); B.bFutureReady = true; B.bNoActiveCommitment = false;
    TestFalse(TEXT("ride commitment rejected"), P->TryCommit(B)); B.bNoActiveCommitment = true; ++B.PreparedRevision;
    TestFalse(TEXT("wrong revision rejected"), P->TryCommit(B)); B.PreparedRevision = P->GetProposalRevision(); ++B.Run.Generation;
    TestFalse(TEXT("old token rejected"), P->TryCommit(B)); B.Run = Run;
    TestTrue(TEXT("matching safe connector commits"), P->TryCommit(B)); TestFalse(TEXT("no pending after commit"), P->HasPendingMutation());
    TestEqual(TEXT("future geometry now active"), P->GetActiveModifiers().ExtraCombinationBudget, 1);
    P->MaybeOffer(69., 300., true); TestFalse(TEXT("45s from choice"), P->GetOffer().IsSet());
    P->MaybeOffer(70., 299., true);
    TestTrue(TEXT("backtracking cannot delay already earned next-offer distance"), P->GetOffer().IsSet());
    auto* DistanceGate = NewObject<UBootRunProgressionComponent>(); DistanceGate->ResetRun(Run, 3, C.Envelope);
    DistanceGate->SetEligibilityContext(C); DistanceGate->MaybeOffer(25., 100., true);
    TestTrue(TEXT("independent initial distance fixture"), DistanceGate->GetOffer().IsSet());
    if (!DistanceGate->GetOffer().IsSet()) return false;
    const int32 RiskIndex = DistanceGate->GetOffer()->CardIds.IndexOfByKey(FName(TEXT("HOT LOOP")));
    TestTrue(TEXT("independent risk choice"), DistanceGate->TryChoose({Run, DistanceGate->GetOffer()->OfferId, RiskIndex, 1, 25.}, Error));
    B.PreparedRevision = DistanceGate->GetProposalRevision();
    TestTrue(TEXT("independent safe commit"), DistanceGate->TryCommit(B));
    DistanceGate->MaybeOffer(70., 299., true); TestFalse(TEXT("200m from choice"), DistanceGate->GetOffer().IsSet());
    DistanceGate->MaybeOffer(70., 300., true); TestTrue(TEXT("next gates"), DistanceGate->GetOffer().IsSet());
    if (DistanceGate->GetOffer().IsSet()) TestFalse(TEXT("held press cannot consume new offer"), DistanceGate->TryChoose({Run, DistanceGate->GetOffer()->OfferId, 0, 1, 70.}, Error));
    return true;
}
#endif
