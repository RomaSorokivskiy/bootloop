#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Run/BootGameMode.h"
#include "Player/BootRunner.h"
#include "Player/BootAbilityComponent.h"
#include "Movement/BootMovementComponent.h"
#include "Tutorial/BootFirstRunTrack.h"
#include "UI/BootPlayerController.h"
#include "Core/BootGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

struct FBootMusicLifecycleTestAccess
{
    static void FinishBoot(ABootGameMode* Mode){Mode->FinishBoot();}
    static void Update(ABootGameMode* Mode){Mode->UpdateRun();}
    static void Checkpoint(ABootGameMode* Mode){Mode->RestoreCheckpoint();}
};
// Exercises native GameMode/Runner/Track integration, without audio rendering or input simulation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootFirstRunLifecycleV04,"Bootloop.V04.Flow.FirstRunLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootFirstRunLifecycleV04::RunTest(const FString&)
{
    UWorld::InitializationValues Init;Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* GI=NewObject<UBootGameInstance>(GEngine);World->SetGameInstance(GI);GI->Init();
    World->GetWorldSettings()->DefaultGameMode=ABootFirstRunGameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());World->BeginPlay();World->GetWorldSettings()->NotifyBeginPlay();
    auto* Mode=World->GetAuthGameMode<ABootGameMode>();
    auto* PC=World->SpawnActor<APlayerController>();PC->SetAsLocalPlayerController();
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Runner=World->SpawnActor<ABootRunner>(FVector(350,0,100),FRotator::ZeroRotator,Spawn);PC->Possess(Runner);
    const int32 OldOnboarding=GI->GetProfile()->OnboardingVersion;
    const auto BeforeRecord=GI->GetProfile()->MusicRunRecord;
    Mode->StartRun();
    TestTrue(TEXT("first-run boot through base lifecycle"),Mode->GetRunState()==EBootRunState::Booting);
    TestEqual(TEXT("boot has no active time"),Mode->GetRunSnapshot().ActiveSeconds,0.);
    TestFalse(TEXT("boot cannot capture evidence"),Mode->CaptureFirstRunEvidence(EBootFirstRunEvent::Moved).Run.RunId.IsValid());
    FBootMusicLifecycleTestAccess::FinishBoot(Mode);
    auto* Track=CastChecked<ABootFirstRunTrack>(Mode->GetTrack());
    const auto OldEvidence=Mode->CaptureFirstRunEvidence(EBootFirstRunEvent::Moved);
    TestTrue(TEXT("running section captures origin"),OldEvidence.Run==Mode->GetRunSnapshot().Run);
    Runner->SetActorLocation(FVector(350,6000,100),false,nullptr,ETeleportType::TeleportPhysics);
    TestFalse(TEXT("wrong physical lesson cannot stamp action"),Mode->CaptureFirstRunEvidence(EBootFirstRunEvent::Jumped).Run.RunId.IsValid());
    Runner->SetActorTransform(Track->GetStartTransform(),false,nullptr,ETeleportType::TeleportPhysics);
    Mode->HandleDeath();FBootMusicLifecycleTestAccess::Checkpoint(Mode);
    Mode->SubmitFirstRunEvidence(OldEvidence);
    TestEqual(TEXT("old checkpoint evidence rejected"),Track->GetTutorial().GetObjectiveIndex(),0);
    Mode->SubmitFirstRunEvidence(Mode->CaptureFirstRunEvidence(EBootFirstRunEvent::Moved));
    Mode->SubmitFirstRunEvidence(Mode->CaptureFirstRunEvidence(EBootFirstRunEvent::Jumped));
    FBootMusicLifecycleTestAccess::Update(Mode);
    TestEqual(TEXT("completed lesson moves to wheel section"),Track->GetTutorial().GetLesson(),1);
    TestTrue(TEXT("physical checkpoint follows advanced lesson"),Track->IsInActiveLesson(Runner));
    while(Track->GetTutorial().GetLesson()<7)Mode->SkipLesson();
    TestTrue(TEXT("trial offers three live cards"),Mode->GetRunSnapshot().CardOffer.IsSet() && Mode->GetRunSnapshot().CardOffer->CardIds.Num()==3);
    TestEqual(TEXT("trial dash loadout"),Runner->GetParkour()->GetDashCharges(),1);
    TestEqual(TEXT("trial guard loadout"),Runner->GetAbilities()->Snapshot().GuardCharges,1);
    Mode->ChooseRunCard(0);
    TestFalse(TEXT("trial choice consumed once"),Mode->GetRunSnapshot().CardOffer.IsSet());
    TestTrue(TEXT("choice is pending until real supported boundary"),Mode->GetRunSnapshot().bPendingMutation);
    TestEqual(TEXT("choice objective moves to dash"),Track->GetTutorial().GetObjectiveIndex(),1);
    Mode->PauseRun(true);const double PausedSeconds=Mode->GetRunSnapshot().ActiveSeconds;
    TestFalse(TEXT("pause cannot capture action"),Mode->CaptureFirstRunEvidence(EBootFirstRunEvent::DashUsed).Run.RunId.IsValid());
    TestEqual(TEXT("paused active clock stable"),Mode->GetRunSnapshot().ActiveSeconds,PausedSeconds);
    Mode->PauseRun(false);Mode->SkipLesson();
    TestEqual(TEXT("trial charges removed entering music explanation"),Runner->GetParkour()->GetDashCharges(),0);
    TestEqual(TEXT("trial guard removed"),Runner->GetAbilities()->Snapshot().GuardCharges,0);
    TestEqual(TEXT("music explanation requires explicit ack"),Track->GetTutorial().GetLesson(),8);
    FBootMusicLifecycleTestAccess::Update(Mode);
    TestFalse(TEXT("standing in final section is not completion"),Track->GetTutorial().IsComplete());
    TestEqual(TEXT("unfinished tutorial does not persist completion"),GI->GetProfile()->OnboardingVersion,OldOnboarding);
    TestEqual(TEXT("training cannot mutate music score record"),GI->GetProfile()->MusicRunRecord.BestScore,BeforeRecord.BestScore);
    TestEqual(TEXT("training score always zero"),Mode->GetTotalScore(),0);
    Mode->ReturnToDesktop();TestFalse(TEXT("desktop removes trial offer"),Mode->GetRunSnapshot().CardOffer.IsSet());
    World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    return true;
}
#endif
