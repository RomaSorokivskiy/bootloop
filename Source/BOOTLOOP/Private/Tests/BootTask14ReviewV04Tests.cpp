#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Run/BootGameMode.h"
#include "Player/BootRunner.h"
#include "Player/BootAbilityComponent.h"
#include "Movement/BootMovementComponent.h"
#include "Tutorial/BootFirstRunTrack.h"
#include "World/Generation/BootEndlessTrack.h"
#include "World/Generation/BootEndlessChunk.h"
#include "World/Interaction/BootSurfWindow.h"
#include "World/Threats/BootPopupProcess.h"
#include "Core/BootGameInstance.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"

struct FBootTask14ReviewAccess
{
    static void FinishBoot(ABootGameMode* Mode){Mode->FinishBoot();}
    static void MusicUpdate(ABootGameMode* Mode,ABootRunner* Runner,double Now){Mode->UpdateMusicRun(Runner,Now);}
    static void Checkpoint(ABootGameMode* Mode){Mode->RestoreCheckpoint();}
    static FBootTutorialEvidence PendingEvidence(ABootRunner* Runner)
    {return Runner->PendingSurf.IsEmpty()?FBootTutorialEvidence{}:Runner->PendingSurf.Last().Evidence;}
    static FBootRunToken BeginMusicFixture(ABootGameMode* Mode,ABootRunner* Runner,double Start)
    {
        Mode->MusicRun=NewObject<UBootRunCoordinator>(Mode);const auto Run=Mode->MusicRun->BeginRunAt(998,Start);
        Mode->State=EBootRunState::Running;Mode->bResultSaved=true;Mode->NextEligibilitySeconds=TNumericLimits<double>::Max();
        Mode->BindMusicTrack(Run);auto* Track=CastChecked<ABootEndlessTrack>(Mode->Track);
        Track->BeginMusicRun(Run,998,Mode->MusicRun->Snapshot().Envelope);Track->SetProcessesActive(false);
        Runner->ResetToTransform(FTransform(FVector(350,0,100)),false);Runner->GetParkour()->SetMovementMode(MOVE_Walking);
        Runner->GetAbilities()->ResetRun(Run);return Run;
    }
    static void StopWithoutSave(ABootGameMode* Mode){Mode->bResultSaved=true;Mode->ReturnToDesktop();}
    static void AddChunk(ABootEndlessTrack* Track,ABootEndlessChunk* Chunk){Track->LiveChunks.Add(Chunk);}
    static bool SupportChunk(ABootEndlessTrack* Track,ABootEndlessChunk* Chunk,ABootRunner* Runner)
    {
        const auto& D=Chunk->GetDescriptor();
        const auto* Floor=D.Recipe.Boxes.FindByPredicate([](const auto& B){return B.Role==EBootRecipeRole::Floor;});
        if(!Floor)return false;
        TArray<UInstancedStaticMeshComponent*> Meshes;Chunk->GetComponents(Meshes);
        UPrimitiveComponent* Base=nullptr;for(auto* Mesh:Meshes)if(Mesh->GetFName()==TEXT("RecipeSolids0"))Base=Mesh;
        if(!Base)return false;
        const FVector Feet=Floor->Center+FVector(0.,0.,Floor->HalfSize.Z);
        Runner->SetActorLocation(D.WorldTransform.TransformPosition(Feet)+FVector(0.,0.,Runner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.),false,nullptr,ETeleportType::TeleportPhysics);
        Runner->GetParkour()->SetMovementMode(MOVE_Walking);Runner->GetParkour()->Velocity=FVector::ZeroVector;Runner->SetBase(Base);
        Track->Cursor.Reset(D.InstanceId);Track->Cursor.CanonicalMetres=150.;return true;
    }
    // Add #include "TimerManager.h" to this test translation unit.
    // Explicit fixture setup: this test owns a synthetic clock and one update per physics step.
    // Keep gameplay components/physics ticking; remove only GameMode's wall-clock lifecycle timers.
    static bool UseCommitmentFixtureClock(ABootGameMode* Mode)
    {
        auto& Timers=Mode->GetWorldTimerManager();
        Timers.ClearTimer(Mode->RunTimer);Timers.ClearTimer(Mode->BootTimer);
        Mode->LastUpdateSeconds=0.;
        return !Timers.IsTimerActive(Mode->RunTimer)&&!Timers.IsTimerActive(Mode->BootTimer);
    }
    // Fixture construction only: call before the observed physics interval, with pawn out of the way.
    static bool InstallCommitmentFixture(ABootEndlessTrack* T,const FBootRunSnapshot& S)
    {
        T->BeginMusicRun(S.Run,998,S.Envelope);
        for(auto C:T->LiveChunks)if(IsValid(C))C->Destroy();
        T->LiveChunks.Empty();T->ExternalPins.Empty();T->Cursor.Reset(1);
        T->NextId=1;T->NextConnector=FTransform::Identity;T->PlannedMetres=0.;
        T->bObservedPosition=false;T->LastRunner.Reset();T->ActiveSeconds=400.;
        T->Music=S.Music;T->Modifiers=S.ActiveModifiers;
        for(int32 I=0;I<3;++I)
        {
            auto R=T->MakeRequest();FBootReplayDecision D;
            D.DefinitionId=I==0?TEXT("surf.crossing"):TEXT("recovery.clean");
            D.DefinitionVersion=1;D.MusicContext=R.MusicContext;R.ReplayDecision=D;
            auto Planned=T->Planner.PlanNext(R);if(!Planned.Committed.IsSet())return false;
            FActorSpawnParameters P;P.Owner=T;
            auto C=T->GetWorld()->SpawnActor<ABootEndlessChunk>(FVector::ZeroVector,FRotator::ZeroRotator,P);
            if(!C||!C->Configure(Planned.Committed.GetValue())||!C->FinishPreparation())return false;
            C->SetProcessesActive(true);T->LiveChunks.Add(C);
            T->NextConnector=Planned.Committed->WorldExit;T->PlannedMetres=Planned.Committed->CanonicalEndMetres;++T->NextId;
            T->OnChunkCommitted.Broadcast(C->GetDescriptor());
        }
        T->SetProcessesActive(true);return true;
    }
};
namespace
{
struct FReviewWorld
{
    UWorld* World=nullptr;UBootGameInstance* GI=nullptr;ABootGameMode* Mode=nullptr;ABootRunner* Runner=nullptr;
    explicit FReviewWorld(bool First)
    {
        UWorld::InitializationValues Init;Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        GI=NewObject<UBootGameInstance>(GEngine);World->SetGameInstance(GI);GI->Init();
        World->GetWorldSettings()->DefaultGameMode=First?ABootFirstRunGameMode::StaticClass():ABootMusicRunGameMode::StaticClass();
        World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());World->BeginPlay();World->GetWorldSettings()->NotifyBeginPlay();Mode=World->GetAuthGameMode<ABootGameMode>();
        auto* PC=World->SpawnActor<APlayerController>();PC->SetAsLocalPlayerController();PC->bShowMouseCursor=false;
        FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Runner=World->SpawnActor<ABootRunner>(FVector(350,0,100),FRotator::ZeroRotator,Spawn);PC->Possess(Runner);
    }
    ~FReviewWorld()
    {
        FBootTask14ReviewAccess::StopWithoutSave(Mode);World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootPopupEvidenceReview,"Bootloop.V04.Flow.ReviewPopupEvidence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootPopupEvidenceReview::RunTest(const FString&)
{
    FReviewWorld F(true);F.Mode->StartRun();FBootTask14ReviewAccess::FinishBoot(F.Mode);
    auto* Track=CastChecked<ABootFirstRunTrack>(F.Mode->GetTrack());for(int32 I=0;I<6;++I)F.Mode->SkipLesson();
    F.Mode->SubmitFirstRunEvidence(F.Mode->CaptureFirstRunEvidence(EBootFirstRunEvent::ScanAvoided));
    TestEqual(TEXT("process lesson is waiting for interception"),Track->GetTutorial().GetObjectiveIndex(),1);
    ABootPopupProcess* Popup=nullptr;for(TActorIterator<ABootPopupProcess> It(F.World);It;++It)if(It->GetWindowId()==46001)Popup=*It;
    if(!TestNotNull(TEXT("native tutorial popup"),Popup))return false;
    Popup->SetRunActive(true);
    F.Runner->SetActorLocation(Popup->GetActorLocation()+FVector(0,0,136),false,nullptr,ETeleportType::TeleportPhysics);
    F.Runner->SetBase(CastChecked<UPrimitiveComponent>(Popup->GetRootComponent()));F.Runner->RequestSnap();
    const auto Origin=FBootTask14ReviewAccess::PendingEvidence(F.Runner);
    TestEqual(TEXT("popup action captures PopupIntercepted at activation"),Origin.Event,EBootFirstRunEvent::PopupIntercepted);
    TestEqual(TEXT("activation has original lesson"),Origin.Lesson,6);
    TestFalse(TEXT("actual popup activation removes hostility"),Popup->IsHostile());
    Popup->Tick(.1f);F.Runner->SetActorLocation(Popup->GetActorLocation()+FVector(0,0,136),false,nullptr,ETeleportType::TeleportPhysics);Popup->Tick(.1f);
    TestTrue(TEXT("native servo movement carried its based rider"),Popup->HasCarried(F.Runner));
    F.Runner->SetBase(static_cast<UPrimitiveComponent*>(nullptr));F.Runner->SetActorLocation(FVector(4700,36300,256),false,nullptr,ETeleportType::TeleportPhysics);
    F.Runner->GetParkour()->SetMovementMode(MOVE_Walking);F.Runner->GetParkour()->Velocity=FVector::ZeroVector;
    Popup->Tick(0.f);TestTrue(TEXT("real destination floor validates surf arrival"),Track->CanCompleteSurf(Popup,F.Runner,Origin.Section));
    F.Runner->Tick(.016f);
    TestEqual(TEXT("deferred Runner landing advances process lesson"),Track->GetTutorial().GetLesson(),7);
    // Re-enter the original lesson under a fresh section; captured old callbacks cannot complete it.
    Track->BeginFirstRun(Origin.Run);for(int32 I=0;I<6;++I)Track->SkipCurrentLesson(Origin.Run,F.Runner);
    Track->PrepareCheckpoint(Origin.Run,F.Runner);
    Track->AcceptEvidence(Track->CaptureEvidence(EBootFirstRunEvent::ScanAvoided,999));
    F.Mode->SubmitFirstRunEvidence(Origin);
    TestEqual(TEXT("old section remains rejected at same popup objective"),Track->GetTutorial().GetLesson(),6);
    TestEqual(TEXT("old evidence cannot advance fresh objective"),Track->GetTutorial().GetObjectiveIndex(),1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootUnseenOfferReview,"Bootloop.V04.Flow.ReviewUnseenOffer",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootUnseenOfferReview::RunTest(const FString&)
{
    FReviewWorld F(false);const double Start=FPlatformTime::Seconds()-25.1;
    FBootTask14ReviewAccess::BeginMusicFixture(F.Mode,F.Runner,Start);auto* Run=F.Mode->GetMusicRun();
    auto* RecoveryTrack=CastChecked<ABootEndlessTrack>(F.Mode->GetTrack());
    if(!TestTrue(TEXT("generated recovery provides actual support"),FBootTask14ReviewAccess::SupportChunk(RecoveryTrack,RecoveryTrack->GetLiveChunks()[0],F.Runner)))return false;
    FBootCardEligibilityContext Eligibility;Eligibility.Envelope=Run->Snapshot().Envelope;
    // Keep a deterministic reward-bearing choice without inventing higher-budget risk eligibility.
    // Derive its candidate first, then require the actual live track to accept that build.
    Eligibility.bCanPrepareThinCache=true;FBootRunModifiers ThinCandidate;FText Error;
    if(!TestTrue(TEXT("derive thin-cache candidate"),FBootRunCardRules::Apply(TEXT("THIN CACHE"),Eligibility,ThinCandidate,Error)))return false;
    auto ThinEnvelope=FBootRunCardRules::ApplyToEnvelope(Eligibility.Envelope,ThinCandidate);++ThinEnvelope.Revision;
    Eligibility.bCanPrepareThinCache=CastChecked<ABootEndlessTrack>(F.Mode->GetTrack())->CanPrepareBuild(ThinCandidate,ThinEnvelope);
    if(!TestTrue(TEXT("native track can prepare the offered risk"),Eligibility.bCanPrepareThinCache))return false;
    for(const auto& Card:FBootRunCardRules::Catalog())
        if(Card.Id!=TEXT("AIR CACHE") && Card.Id!=TEXT("WALL CACHE") && Card.Id!=TEXT("THIN CACHE"))Eligibility.BlockedCardIds.Add(Card.Id);
    Run->SetCardEligibility(Eligibility);Run->SetRecoveryReady(true);Run->Update(Start+24.9,150.,{});
    TestFalse(TEXT("no offer exists just before threshold"),Run->Snapshot().CardOffer.IsSet());
    F.Mode->ChooseRunCard(0);
    TestTrue(TEXT("press cannot create and consume unseen offer"),Run->Snapshot().RewardModifiers.Stacks.IsEmpty());
    TestEqual(TEXT("unseen press cannot mutate reward revision"),Run->Snapshot().RewardRevision,uint32(0));
    FBootTask14ReviewAccess::MusicUpdate(F.Mode,F.Runner,FPlatformTime::Seconds());
    const auto Offered=Run->Snapshot();if(!TestTrue(TEXT("subsequent mode update opens offer"),Offered.CardOffer.IsSet()))return false;
    const int32 Index=Offered.CardOffer->CardIds.IndexOfByKey(FName(TEXT("THIN CACHE")));
    if(!TestTrue(TEXT("existing offer contains the preflighted risk"),Index!=INDEX_NONE))return false;
    const FName Selected=Offered.CardOffer->CardIds[Index];const int64 Before=Offered.Score;
    F.Mode->ChooseRunCard(Index);const auto Chosen=Run->Snapshot();
    TestEqual(TEXT("existing offer accepts exactly the requested card"),Chosen.RewardModifiers.Stacks.FindRef(Selected),1);
    TestTrue(TEXT("selected risk has a real future distance reward"),Chosen.RewardModifiers.DistanceMultiplier>1.);
    TestEqual(TEXT("choice never multiplies previous distance points"),Chosen.Score,Before);
    Run->Update(FPlatformTime::Seconds()+1.,160.,{});
    TestEqual(TEXT("only future distance receives selected multiplier"),Run->Snapshot().Score-Before,int64(FMath::FloorToDouble(100.*Chosen.RewardModifiers.DistanceMultiplier)));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootRecoveryOfferReview,"Bootloop.V04.Flow.RecoveryOfferSupport",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootRecoveryOfferReview::RunTest(const FString&)
{
    for(const FName Id:{FName(TEXT("slide.short")),FName(TEXT("process.scan")),FName(TEXT("surf.crossing"))})
    {
        FReviewWorld F(false);const double Start=FPlatformTime::Seconds()-25.1;
        const auto Token=FBootTask14ReviewAccess::BeginMusicFixture(F.Mode,F.Runner,Start);
        auto* Track=CastChecked<ABootEndlessTrack>(F.Mode->GetTrack());auto* Run=F.Mode->GetMusicRun();
        ABootEndlessChunk* Recovery=Track->GetLiveChunks()[0];
        // Assemble the actual catalog recipe with its collision and native child actors.
        FBootEndlessPlanner Planner;Planner.Reset(99,Run->Snapshot().Envelope);
        FBootPlanRequest Request;Request.Run=Token;Request.NextInstanceId=1000;Request.Connector=FTransform(FVector(0.,30000.,0.));
        Request.CurrentEnvelope=Run->Snapshot().Envelope;Request.ActiveSeconds=300.;Request.Tier=5;
        FBootReplayDecision Replay;Replay.DefinitionId=Id;Request.ReplayDecision=Replay;
        const auto Plan=Planner.PlanNext(Request);
        if(!TestTrue(TEXT("native challenge fixture has a validated placement"),Plan.Committed.IsSet()))return false;
        auto* Challenge=F.World->SpawnActor<ABootEndlessChunk>();
        if(!TestTrue(TEXT("native challenge collision prepared"),Challenge->Configure(Plan.Committed.GetValue())&&Challenge->FinishPreparation()))return false;
        Challenge->SetProcessesActive(false);FBootTask14ReviewAccess::AddChunk(Track,Challenge);
        if(!TestTrue(TEXT("runner stands on challenge floor"),FBootTask14ReviewAccess::SupportChunk(Track,Challenge,F.Runner)))return false;
        FBootCardEligibilityContext Eligibility;Eligibility.Envelope=Run->Snapshot().Envelope;Run->SetCardEligibility(Eligibility);
        Run->SetRecoveryReady(false);Run->Update(Start+24.9,150.,{});
        FBootTask14ReviewAccess::MusicUpdate(F.Mode,F.Runner,FPlatformTime::Seconds());
        TestFalse(*FString::Printf(TEXT("due offer waits while grounded in %s"),*Id.ToString()),Run->Snapshot().CardOffer.IsSet());
        ABootSurfWindow* Window=nullptr;
        if(Id==TEXT("surf.crossing"))
        {
            Window=Challenge->GetWindows()[0];Challenge->SetProcessesActive(true);
            F.Runner->SetActorLocation(Window->GetActorLocation()+FVector(0.,0.,136.),false,nullptr,ETeleportType::TeleportPhysics);
            F.Runner->SetBase(CastChecked<UPrimitiveComponent>(Window->GetRootComponent()));
            if(!TestTrue(TEXT("native ride commitment is active"),Window->Activate(F.Runner,false)&&Window->HasActiveCommitment()))return false;
        }
        if(!TestTrue(TEXT("ordinary generated recovery has actual static support"),FBootTask14ReviewAccess::SupportChunk(Track,Recovery,F.Runner)))return false;
        if(Window)
        {
            FBootTask14ReviewAccess::MusicUpdate(F.Mode,F.Runner,FPlatformTime::Seconds());
            TestFalse(TEXT("uncommitted ride still blocks offer on recovery floor"),Run->Snapshot().CardOffer.IsSet());
            Window->ResetWindow();
        }
        // A walking mode flag alone is insufficient even inside a recovery descriptor.
        F.Runner->SetBase(static_cast<UPrimitiveComponent*>(nullptr));
        FBootTask14ReviewAccess::MusicUpdate(F.Mode,F.Runner,FPlatformTime::Seconds());
        TestFalse(TEXT("unbased walking state cannot open recovery offer"),Run->Snapshot().CardOffer.IsSet());
        FBootTask14ReviewAccess::SupportChunk(Track,Recovery,F.Runner);
        FBootTask14ReviewAccess::MusicUpdate(F.Mode,F.Runner,FPlatformTime::Seconds());
        TestTrue(TEXT("due offer opens on supported generated recovery without a ride"),Run->Snapshot().CardOffer.IsSet());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootIndependentThreatReview,"Bootloop.V04.Flow.ReviewIndependentThreats",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootIndependentThreatReview::RunTest(const FString&)
{
    FReviewWorld F(false);const double Start=FPlatformTime::Seconds();const auto Token=FBootTask14ReviewAccess::BeginMusicFixture(F.Mode,F.Runner,Start);
    auto* Track=CastChecked<ABootEndlessTrack>(F.Mode->GetTrack());auto* Run=F.Mode->GetMusicRun();auto* Abilities=F.Runner->GetAbilities();
    auto Envelope=Run->Snapshot().Envelope;Envelope.GuardCharges=2;Abilities->ApplyEnvelope(Envelope);
    FBootCommittedChunk Chunk;Chunk.Run=Token;Chunk.InstanceId=1000;
    FBootHazardRecipe A;A.OperationId=1;FBootHazardRecipe B;B.OperationId=2;Chunk.Recipe.Hazards={A,B};
    Track->OnChunkCommitted.Broadcast(Chunk);const FBootOperationKey One{Token,1000,1},Two{Token,1000,2};
    Run->Update(Start+1.,0.,{});Track->OnRunThreatContact.Broadcast(F.Runner,One);
    TestEqual(TEXT("first process consumes one charge"),Abilities->Snapshot().GuardCharges,1);
    Run->Update(Start+2.,0.,{});Track->OnRunThreatContact.Broadcast(F.Runner,One);
    TestEqual(TEXT("same contact remains deduplicated"),Abilities->Snapshot().GuardCharges,1);
    Run->Update(Start+3.,0.,{});Track->OnRunThreatContact.Broadcast(F.Runner,Two);
    TestEqual(TEXT("independent process in same chunk consumes distinct charge"),Abilities->Snapshot().GuardCharges,0);
    TestFalse(TEXT("two charges protected both contacts"),F.Runner->IsDead());
    Abilities->ResetRun(Token);Abilities->ApplyEnvelope(Envelope);
    auto Stale=One;Stale.Run.Generation++;Track->OnRunThreatContact.Broadcast(F.Runner,Stale);
    TestEqual(TEXT("stale run cannot consume a charge"),Abilities->Snapshot().GuardCharges,2);
    Track->OnChunkRetired.Broadcast(1000);Track->OnRunThreatContact.Broadcast(F.Runner,One);
    TestEqual(TEXT("retired process no longer has a live identity"),Abilities->Snapshot().GuardCharges,2);
    TestFalse(TEXT("retired callback cannot kill runner"),F.Runner->IsDead());
    return true;
}
// Append before the final #endif in BootTask14ReviewV04Tests.cpp; access helper belongs
// inside its existing FBootTask14ReviewAccess. Add #include "TimerManager.h".
// No new production friend is required.
// Direct setup: legal card selections with accelerated eligibility and coordinator-only
// setup boundaries, replay-selected authored geometry, initial placement over the window,
// and removal of GameMode's wall-clock timers so only Step owns the synthetic update clock.
// Observed interval: real gravity, based carry, movement input, jump and supported landing;
// no teleport, velocity assignment, SetBase or movement-mode forcing during observation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootCombinedCommitmentReview,"Bootloop.V04.Flow.CombinedBuildPinnedRevision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootCombinedCommitmentReview::RunTest(const FString&)
{
    for(const int32 Fps:{30,60,120})
    {
        AddInfo(FString::Printf(TEXT("Combined maximum legal buff fixture fps=%d; direct setup, then native physics"),Fps));
        FReviewWorld F(false);
        if(!TestTrue(TEXT("fixture removes competing wall-clock lifecycle timers"),FBootTask14ReviewAccess::UseCommitmentFixtureClock(F.Mode)))return false;
        const auto Token=FBootTask14ReviewAccess::BeginMusicFixture(F.Mode,F.Runner,0.);
        auto Run=F.Mode->GetMusicRun();auto Track=CastChecked<ABootEndlessTrack>(F.Mode->GetTrack());auto Move=F.Runner->GetParkour();
        double Now=0.,SetupMetres=0.;uint64 Press=0;FText Error;
        FBootMusicContext Music;Music.EffectiveBpm=168.;
        auto CommitSetup=[&](){const auto S=Run->Snapshot();FBootSafeBoundary B;B.Run=Token;B.ChunkId=1;B.PreparedRevision=S.PendingEnvelope.Revision;B.bSupported=B.bNoActiveCommitment=B.bFutureReady=true;return Run->CommitPreparedRevision(B);};
        auto OpenSetupOffer=[&](FName Wanted)
        {
            FBootCardEligibilityContext E;E.bCanPrepareProcessBudget=E.bCanPrepareCombinations=true;
            for(const auto& C:FBootRunCardRules::Catalog())
                if(C.Id!=Wanted&&C.Id!=TEXT("PRIORITY ESCALATION")&&C.Id!=TEXT("HOT LOOP"))E.BlockedCardIds.Add(C.Id);
            Run->SetCardEligibility(E);Run->SetRecoveryReady(true);Now+=50.;SetupMetres+=250.;Run->Update(Now,SetupMetres,Music);
            return Run->Snapshot().CardOffer.IsSet();
        };
        Now=1.;Run->Update(Now,0.,Music);
        if(!TestTrue(TEXT("setup fast tempo proposal commits"),CommitSetup()))return false;
        for(const auto& C:FBootRunCardRules::Catalog())if(C.Kind==EBootCardKind::Buff)
        {
            const int32 Stacks=C.MaxStacks-(C.Id==TEXT("SERVO RESERVE")?1:0);
            for(int32 Stack=0;Stack<Stacks;++Stack)
            {
                FBootCardEligibilityContext Actual;Actual.Envelope=Run->Snapshot().Envelope;Actual.Modifiers=Run->Snapshot().ActiveModifiers;
                if(!FBootRunCardRules::Eligible(Actual).Contains(C.Id))
                {
                    // This envelope reaches .16s after two stacks; the third nominal stack
                    // must remain ineligible rather than becoming a dead card in the fixture.
                    TestEqual(TEXT("only capped landing grace stops before nominal stack limit"),C.Id,FName(TEXT("SOFT LANDING")));
                    TestTrue(TEXT("physical landing grace is already at cap"),FMath::IsNearlyEqual(Actual.Envelope.HopGraceSeconds,.16f));
                    break;
                }
                if(!TestTrue(TEXT("setup legal buff offer exists"),OpenSetupOffer(C.Id)))return false;
                const auto S=Run->Snapshot();const int32 Index=S.CardOffer->CardIds.IndexOfByKey(C.Id);
                if(!TestTrue(TEXT("setup legal buff selected through coordinator"),Run->TryChoose({Token,S.CardOffer->OfferId,Index,++Press,S.ActiveSeconds},Error)))return false;
                if(!TestTrue(TEXT("setup buff commits"),CommitSetup()))return false;
            }
        }
        if(!TestTrue(TEXT("last servo offer opened before ride"),OpenSetupOffer(TEXT("SERVO RESERVE"))))return false;
        const auto Initial=Run->Snapshot();
        TestEqual(TEXT("setup maximum dash capacity"),Initial.Envelope.DashCharges,2);
        TestEqual(TEXT("setup maximum redirect capacity"),Initial.Envelope.AirRedirectCharges,3);
        TestEqual(TEXT("setup maximum guard capacity"),Initial.Envelope.GuardCharges,2);
        F.Runner->SetActorLocation(FVector(0,0,60000),false,nullptr,ETeleportType::TeleportPhysics);
        if(!TestTrue(TEXT("three real native fixture chunks prepared"),FBootTask14ReviewAccess::InstallCommitmentFixture(Track,Initial)))return false;
        auto Chunk=Track->GetLiveChunks()[0];auto Window=Chunk->GetWindows()[0];const auto Original=Chunk->GetDescriptor();
        if(!TestFalse(TEXT("ResetToTransform cleared old traversal before initial setup install"),Move->GetTraversalEnvelope().bValid))return false;
        Move->ApplyTraversalEnvelope(Initial.Envelope);F.Runner->GetAbilities()->ApplyEnvelope(Initial.Envelope);
        if(!TestEqual(TEXT("initial setup envelope installs as first valid revision"),Move->GetTraversalEnvelope().Revision,Initial.Envelope.Revision))return false;
        F.Runner->SetActorLocation(Window->GetActorLocation()+FVector(0,0,140),false,nullptr,ETeleportType::TeleportPhysics);
        Move->Velocity=FVector::ZeroVector;Move->SetMovementMode(MOVE_Falling);
        const float Dt=1.f/Fps;
        bool ClockStayedSynthetic=true;
        auto Step=[&](bool Forward)
        {
            if(Forward)F.Runner->AddMovementInput(FVector::ForwardVector,1.f,true);
            ++GFrameCounter;F.World->Tick(LEVELTICK_All,Dt);Now+=Dt;
            // Exactly one course/coordinator update after each native physics step.
            // Runner::Tick does not call either method, and the Mode timers are disabled above.
            Track->UpdateCourse(F.Runner,Dt);FBootTask14ReviewAccess::MusicUpdate(F.Mode,F.Runner,Now);
            ClockStayedSynthetic&=FMath::IsNearlyEqual(Run->Snapshot().ActiveSeconds,Now,.001);
            // Preserve UpdateRun's real fatal-volume check despite replacing its timer clock.
            if(!F.Runner->IsDead()&&Track->IsFatal(F.Runner->GetActorLocation(),F.Runner->GetCapsuleComponent()->GetScaledCapsuleRadius(),F.Runner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()))F.Runner->Die();
        };
        for(int32 I=0;I<Fps/2;++I)Step(false);
        if(!TestTrue(TEXT("gravity settles onto authored window"),F.Runner->GetMovementBaseObject()==Window->GetRootComponent()))return false;
        if(!TestTrue(TEXT("offbeat native activation starts"),Window->Activate(F.Runner,false)))return false;
        const FVector Boarded=F.Runner->GetActorLocation();for(int32 I=0;I<Fps;++I)Step(false);
        if(!TestTrue(TEXT("observed actual carried movement"),Window->HasCarried(F.Runner)&&F.Runner->GetActorLocation().X>Boarded.X+50.))return false;
        const auto Offer=Run->Snapshot();const int32 Servo=Offer.CardOffer->CardIds.IndexOfByKey(TEXT("SERVO RESERVE"));
        if(!TestTrue(TEXT("existing card choice accepted while carried"),Run->TryChoose({Token,Offer.CardOffer->OfferId,Servo,++Press,Offer.ActiveSeconds},Error)))return false;
        Music.EffectiveBpm=96.;Run->Update(Now,SetupMetres,Music);Step(false);
        const uint32 Proposed=Run->Snapshot().PendingEnvelope.Revision;
        TestTrue(TEXT("card and slower tempo remain pending during ride"),Run->Snapshot().bPendingMutation);
        TestEqual(TEXT("ride keeps movement revision"),Move->GetTraversalEnvelope().Revision,Initial.Envelope.Revision);
        TestEqual(TEXT("ride keeps track revision"),Track->GetActiveEnvelope().Revision,Initial.Envelope.Revision);
        TestTrue(TEXT("actual occupied ride pins its chunk"),Track->IsChunkPinned(Original.InstanceId));
        for(int32 I=0;I<6*Fps&&Window->IsMoving();++I)
        {
            Step(false);
            if(!TestTrue(TEXT("rider support and reserved landing remain live"),Track->GetLiveChunks().Contains(Chunk)&&Chunk->IsReady()&&Track->IsChunkPinned(Original.InstanceId)))return false;
            if(!TestEqual(TEXT("revision never publishes during active carry"),Move->GetTraversalEnvelope().Revision,Initial.Envelope.Revision))return false;
        }
        TestFalse(TEXT("native window not blocked"),Window->WasRideBlocked());
        TestFalse(TEXT("standing aboard cannot complete"),Window->CommitRide(F.Runner));
        bool Landed=false,Completed=false;
        for(int32 I=0;I<3*Fps;++I)
        {
            Step(true);if(Chunk->IsPreparedLanding(Window,F.Runner))
            {Landed=true;Completed=Track->CompleteRunOperation(Window,F.Runner,EBootBeatRating::OffBeat);break;}
        }
        if(!TestTrue(TEXT("physical departure reaches prepared static landing"),Landed&&Completed))return false;
        TestFalse(TEXT("completed physical ride cannot reward twice"),Track->CompleteRunOperation(Window,F.Runner,EBootBeatRating::OffBeat));
        if(!TestTrue(TEXT("landing away from connector still leaves revision pending"),Run->Snapshot().bPendingMutation))return false;
        TestTrue(TEXT("old collision descriptor unchanged"),Chunk->GetDescriptor().WorldExit.Equals(Original.WorldExit)&&Chunk->GetDescriptor().EnvelopeRevision==Original.EnvelopeRevision);
        F.Runner->RequestJump();Step(true);
        if(!TestTrue(TEXT("offbeat jump actually enters falling"),Move->IsFalling()))return false;
        for(int32 I=0;I<3*Fps&&Move->IsFalling();++I)
        {
            Step(true);
            if(Move->IsFalling()&&!TestEqual(TEXT("pending revision preserves airborne commitment"),Move->GetTraversalEnvelope().Revision,Initial.Envelope.Revision))return false;
        }
        if(!TestTrue(TEXT("jump lands on real support"),Move->IsMovingOnGround()))return false;
        for(int32 I=0;I<8*Fps&&Run->Snapshot().bPendingMutation;++I)Step(true);
        TestFalse(TEXT("production coordinator eventually commits at safe connector"),Run->Snapshot().bPendingMutation);
        TestEqual(TEXT("track publishes same prepared revision"),Track->GetActiveEnvelope().Revision,Proposed);
        TestEqual(TEXT("movement publishes same prepared revision"),Move->GetTraversalEnvelope().Revision,Proposed);
        TestEqual(TEXT("last selected buff now active"),Run->Snapshot().ActiveModifiers.Stacks.FindRef(TEXT("SERVO RESERVE")),3);
        for(int32 I=0;I<3*Fps;++I)Step(false);
        TestTrue(TEXT("slower tempo blend settles on support"),FMath::IsNearlyEqual(Move->GetTraversalEnvelope().RunCmps,float(1100.*96./130.),1.f));
        TestFalse(TEXT("physical observed sequence survives"),F.Runner->IsDead());
        TestTrue(TEXT("all observed updates used only the synthetic fixture clock"),ClockStayedSynthetic);
        TestTrue(TEXT("track remains bounded"),Track->GetLiveChunkCount()<=40);
    }
    return true;
}

#endif
