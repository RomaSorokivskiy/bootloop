#include "Misc/AutomationTest.h"
#include "World/Generation/BootEndlessTrack.h"
#include "World/Generation/BootEndlessPlanner.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootMeaningfulRiskPureV04,"Bootloop.V04.Planner.MeaningfulRiskEligibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootMeaningfulRiskPureV04::RunTest(const FString&)
{
    FBootPlanRequest R;R.Run.RunId=FGuid(1,8,3,9);R.CurrentEnvelope=FBootTraversalPolicy::FromTempo(130.,1);R.ActiveSeconds=900.;R.Tier=5;
    FBootEndlessPlanner Full,Recovery;Full.Reset(92,R.CurrentEnvelope);Recovery.Reset(92,R.CurrentEnvelope);
    auto OnlyRecovery=FBootChunkCatalog::Build();OnlyRecovery.RemoveAll([](const auto& D){return !D.bSafeContinuation;});Recovery.SetCatalog(OnlyRecovery);
    for(int32 Kind=0;Kind<3;++Kind)
    {
        FBootRunModifiers Candidate;if(Kind==0)Candidate.ExtraCombinationBudget=1;if(Kind==1)Candidate.ExtraProcessBudget=1;if(Kind==2)Candidate.RiskySurfWeightScale=1.2f;
        TestTrue(TEXT("full catalog can prepare meaningful added risk"),Full.CanPrepareBuild(R,Candidate,R.CurrentEnvelope));
        TestFalse(TEXT("recovery fallback cannot justify risk rewards"),Recovery.CanPrepareBuild(R,Candidate,R.CurrentEnvelope));
    }
    FBootRunModifiers Ordinary;Ordinary.DistanceMultiplier=1.1;
    TestTrue(TEXT("ordinary buff still needs only legal continuation"),Recovery.CanPrepareBuild(R,Ordinary,R.CurrentEnvelope));
    auto OneTier=FBootChunkCatalog::Build();OneTier.RemoveAll([](const auto& D){return D.CombinationCost==2;});Recovery.SetCatalog(OneTier);
    R.Modifiers.ExtraCombinationBudget=1;auto Second=R.Modifiers;Second.ExtraCombinationBudget=2;
    TestFalse(TEXT("second HOT LOOP needs newly unlocked tier, not existing tier"),Recovery.CanPrepareBuild(R,Second,R.CurrentEnvelope));
    TestTrue(TEXT("full catalog supplies second combination tier"),Full.CanPrepareBuild(R,Second,R.CurrentEnvelope));
    FBootEndlessPlanner Control;Control.Reset(92,R.CurrentEnvelope);R.Modifiers={};
    for(int32 I=0;I<20;++I)Full.CanPrepareBuild(R,Ordinary,R.CurrentEnvelope);
    const auto A=Full.PlanNext(R),B=Control.PlanNext(R);
    TestTrue(TEXT("eligibility preflight does not consume world RNG"),A.Committed.IsSet()&&B.Committed.IsSet()&&FBootEndlessPlanner::DescriptorHash(*A.Committed)==FBootEndlessPlanner::DescriptorHash(*B.Committed));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootMeaningfulRiskWorldV04,"Bootloop.V04.World.MeaningfulRiskEligibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootMeaningfulRiskWorldV04::RunTest(const FString&)
{
    UWorld::InitializationValues Init;Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());World->BeginPlay();World->GetWorldSettings()->NotifyBeginPlay();
    auto* Track=World->SpawnActor<ABootEndlessTrack>();FBootRunToken Run;Run.RunId=FGuid(9,6,3,1);const auto Envelope=FBootTraversalPolicy::FromTempo(130.,1);Track->BeginMusicRun(Run,44,Envelope);
    FBootRunModifiers Thin;Thin.WidthScale=.9f;Track->SetDirectorContext(FBootMusicContext(),900.,Thin);
    auto Candidate=Thin;Candidate.ExtraProcessBudget=1;TestFalse(TEXT("PRIORITY cannot add excluded processes after THIN CACHE"),Track->CanPrepareBuild(Candidate,Envelope));
    Candidate=Thin;Candidate.RiskySurfWeightScale=1.2f;TestFalse(TEXT("UNSAFE cannot add excluded optional rails after THIN CACHE"),Track->CanPrepareBuild(Candidate,Envelope));
    Candidate=Thin;Candidate.ExtraCombinationBudget=1;TestTrue(TEXT("compatible HOT LOOP remains meaningful on thin route"),Track->CanPrepareBuild(Candidate,Envelope));
    Candidate=Thin;Candidate.DistanceMultiplier=1.1;TestTrue(TEXT("ordinary buff on thin route remains available"),Track->CanPrepareBuild(Candidate,Envelope));
    TestTrue(TEXT("tempo-only candidate remains available"),Track->CanPrepareBuild(Thin,FBootTraversalPolicy::FromTempo(168.,2)));
    for(int32 Kind=0;Kind<2;++Kind)
    {
        FBootRunModifiers Current;if(Kind==0)Current.ExtraProcessBudget=1;else Current.RiskySurfWeightScale=1.2f;
        Track->SetDirectorContext(FBootMusicContext(),900.,Current);Candidate=Current;Candidate.WidthScale=.9f;
        TestFalse(TEXT("THIN CACHE cannot erase existing interaction risk"),Track->CanPrepareBuild(Candidate,Envelope));
        Candidate=Current;Candidate.DistanceMultiplier=1.1;TestTrue(TEXT("ordinary buff preserves interaction risk"),Track->CanPrepareBuild(Candidate,Envelope));
    }
    TestEqual(TEXT("eligibility never mutates active envelope"),Track->GetActiveEnvelope().Revision,uint32(1));
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}
