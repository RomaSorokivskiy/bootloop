#include "Run/BootGameMode.h"
#include "Run/BootRunCoordinator.h"
#include "Movement/BootMovementComponent.h"
#include "Player/BootAbilityComponent.h"
#include "HAL/PlatformTime.h"
#include "Music/BootMusicTransportComponent.h"
#include "Music/BootMusicLibrarySubsystem.h"
#include "World/Generation/BootEndlessTrack.h"
#include "World/Generation/BootEndlessChunk.h"
#include "World/Presentation/BootRunEffectsComponent.h"
#include "Tutorial/BootFirstRunTrack.h"
#include "UI/BootPlayerController.h"
#include "Player/BootRunner.h"
#include "World/Generation/BootTrack.h"
#include "World/Generation/BootSurfTrack.h"
#include "World/Generation/BootCourseTrack.h"
#include "Music/BootRhythmComponent.h"
#include "Core/BootGameInstance.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Developer/BootSmoke.h"
#include "Developer/BootV03Audit.h"
#include "Developer/BootV04Audit.h"

ABootGameMode::ABootGameMode()
{
    DefaultPawnClass = ABootRunner::StaticClass();
    PlayerControllerClass = ABootPlayerController::StaticClass();
    HUDClass = AHUD::StaticClass();
}

void ABootGameMode::BeginPlay()
{
    Super::BeginPlay();
    Track=CreateTrack();
    Track->ResetTrack(Seed);
    SetProcessesActive(false);
    GetWorldTimerManager().SetTimer(RunTimer, this, &ABootGameMode::UpdateRun, .05f, true);
    if (ABootRunner* Runner = Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this, 0)))
    {
        Runner->GetRhythm()->StopRun();
        Runner->GetCharacterMovement()->DisableMovement();
    }
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(),TEXT("BootloopSmoke"))) StartBootSmoke(this);
    if (FParse::Param(FCommandLine::Get(),TEXT("BootloopV03Audit")) && !FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))) StartBootV03Audit(this);
    BootloopV04Audit::Start(this);
#endif
    if(UGameplayStatics::ParseOption(OptionsString,TEXT("BootAutoStart"))==TEXT("true"))
        GetWorldTimerManager().SetTimer(BootTimer,this,&ABootGameMode::StartRun,.1f,false);
}

ABootTrack* ABootGameMode::CreateTrack()
{
    if(bFirstRun)return GetWorld()->SpawnActor<ABootFirstRunTrack>();
    if(bMusicRun)return GetWorld()->SpawnActor<ABootEndlessTrack>();
    if(bAuthoredCourse){auto* Course=GetWorld()->SpawnActor<ABootCourseTrack>();Course->SetTraining(bTrainingCourse);return Course;}
    return GetWorld()->GetMapName().Contains(TEXT("WindowSurf"))?GetWorld()->SpawnActor<ABootSurfTrack>():GetWorld()->SpawnActor<ABootTrack>();
}
bool ABootGameMode::PrepareRun(FText& Error)
{
    if(!bMusicRun)return true;
    const auto* GI=GetGameInstance();
    const auto* Library=GI?GI->GetSubsystem<UBootMusicLibrarySubsystem>():nullptr;
    if(Library)for(const auto& Entry:Library->Snapshot())
        if(Entry.bIncluded&&Entry.State==EBootLibraryState::Ready){Error=FText::GetEmpty();return true;}
    Error=FText::FromString(TEXT("Include at least one ready track in MUSIC LIBRARY."));return false;
}
UBootMusicTransportComponent* ABootGameMode::GetMusicTransport() const
{
    const auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0));return Runner?Runner->GetMusicTransport():nullptr;
}
FBootRunSnapshot ABootGameMode::GetRunSnapshot() const
{
    if(!bFirstRun)return MusicRun?MusicRun->Snapshot():FBootRunSnapshot{};
    FBootRunSnapshot S;S.Run=FirstRunToken;S.bRunning=State==EBootRunState::Running||State==EBootRunState::Paused;
    S.bPaused=State==EBootRunState::Paused;S.ActiveSeconds=FirstRunClock.Seconds(FPlatformTime::Seconds());
    S.DistanceMetres=MaxDistance;S.Envelope=TrialEnvelope;S.PendingEnvelope=TrialPendingEnvelope;
    S.ActiveModifiers=TrialModifiers;S.PendingModifiers=TrialPendingModifiers;S.bPendingMutation=TrialPendingEnvelope.bValid;S.CardOffer=TrialOffer;
    if(const auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0)))
    {
        S.Abilities=Runner->GetAbilities()->Snapshot();S.Abilities.DashCharges=Runner->GetParkour()->GetDashCharges();
        S.Abilities.MaxDashCharges=Runner->GetParkour()->GetMaxDashCharges();S.Abilities.RedirectCharges=Runner->GetParkour()->GetAirRedirectCharges();
        S.Abilities.MaxRedirectCharges=Runner->GetParkour()->GetMaxAirRedirectCharges();
    }
    return S;
}
float ABootGameMode::GetDistance() const
{ return bMusicRun&&MusicRun?float(FMath::Min(MusicRun->Snapshot().DistanceMetres,double(MAX_flt))):MaxDistance; }
float ABootGameMode::GetRunSeconds() const
{ return bMusicRun&&MusicRun?float(FMath::Min(MusicRun->Snapshot().ActiveSeconds,double(MAX_flt))):RunSeconds; }

void ABootGameMode::StartRun()
{
    FText Error;
    if(!PrepareRun(Error))
    {
        if(auto* PC=Cast<ABootPlayerController>(UGameplayStatics::GetPlayerController(this,0)))PC->ShowMusicLibrary();
        return;
    }
    GetWorldTimerManager().ClearTimer(BootTimer);
    UGameplayStatics::SetGamePaused(this, false);
    if (State != EBootRunState::Desktop && State != EBootRunState::Booting) SaveResult();
    SetProcessesActive(false);
    StopV04Run(true);
    if(auto* Transport=GetMusicTransport())Transport->StopPreview();
    State = EBootRunState::Booting;
    bResultSaved = false;
    MaxDistance = 0; RunSeconds = 0;
    ++AttemptGeneration;
    if(bMusicRun || bFirstRun)
    {
        auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0));
        if(!Runner){State=EBootRunState::Desktop;return;}
        const double Now=FPlatformTime::Seconds();LastUpdateSeconds=Now;NextEligibilitySeconds=0.;CardPressSerial=0;
        Runner->GetRhythm()->StopRun();
        if(bMusicRun)
        {
            if(!MusicRun)MusicRun=NewObject<UBootRunCoordinator>(this);
            const auto Token=MusicRun->BeginRunAt(uint64(uint32(Seed)),Now);MusicRun->SetPaused(true,Now);
            Runner->GetRhythm()->SetMusicTransport(Runner->GetMusicTransport());Runner->GetRhythm()->ResetScoring();
            BindMusicTrack(Token);
            auto* Endless=CastChecked<ABootEndlessTrack>(Track);
            Endless->BeginMusicRun(Token,uint64(uint32(Seed)),MusicRun->Snapshot().Envelope);
            Runner->ResetToTransform(FTransform(Track->GetActorRotation(),Track->GetActorTransform().TransformPosition(FVector(350,0,100))),false);
            Runner->GetParkour()->ApplyTraversalEnvelope(MusicRun->Snapshot().Envelope);
            Runner->GetAbilities()->ResetRun(Token);Runner->GetAbilities()->ApplyEnvelope(MusicRun->Snapshot().Envelope);
        }
        else
        {
            FirstRunToken={FGuid::NewGuid(),uint32(AttemptGeneration)};FirstRunClock.Start(Now);FirstRunClock.SetPaused(true,Now);
            auto* First=CastChecked<ABootFirstRunTrack>(Track);First->BeginFirstRun(FirstRunToken);
            Runner->ResetToTransform(First->GetStartTransform(),false);PreparedLesson=0;ResetTrialLoadout(Runner);
        }
        Track->InitializeRunPresentation(Seed,Runner);
        SetProcessesActive(false);Runner->GetCharacterMovement()->DisableMovement();
    }
    else
    {
    // Same seed on Retry allows learning; next desktop launch chooses a fresh run.
    if(auto* Course=Cast<ABootCourseTrack>(Track))
        if(auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0))) Runner->ResetToTransform(Course->GetStartTransform(),true);
    Track->ResetTrack(Seed);
    if (ABootRunner* Runner = Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this, 0)))
    {
        if(!Cast<ABootCourseTrack>(Track)) Runner->ResetRun();
        Runner->GetCharacterMovement()->DisableMovement();
    }
    }
    if (auto* GI = GetGameInstance<UBootGameInstance>()) GI->Sound(EBootSound::Boot);
    if (auto* PC = Cast<ABootPlayerController>(UGameplayStatics::GetPlayerController(this, 0))) PC->ShowPage(EBootPage::Boot);
    GetWorldTimerManager().SetTimer(BootTimer, this, &ABootGameMode::FinishBoot, 1.1f, false);
    UE_LOG(LogTemp, Display, TEXT("BOOTLOOP V01: boot seed=%d"), Seed);
}

void ABootGameMode::FinishBoot()
{
    if(State!=EBootRunState::Booting)return;
    double Now=FPlatformTime::Seconds();
    if(bMusicRun)
    {
        FText Error;auto* Transport=GetMusicTransport();
        if(!Transport || !MusicRun || !Transport->StartRun(MusicRun->Snapshot().Run,uint64(uint32(Seed)),Error))
        {
            UE_LOG(LogTemp,Warning,TEXT("Music Run could not start: %s"),*Error.ToString());
            StopV04Run(true);State=EBootRunState::Desktop;SetProcessesActive(false);
            if(auto* PC=Cast<ABootPlayerController>(UGameplayStatics::GetPlayerController(this,0)))PC->ShowMusicLibrary();
            return;
        }
        Now=FPlatformTime::Seconds();MusicRun->SetPaused(false,Now);
        if(auto* GI=GetGameInstance<UBootGameInstance>())Transport->SetVolumeMultiplier(GI->GetProfile()->Options.Music);
    }
    if(bFirstRun)
    {
        FirstRunClock.SetPaused(false,Now);
        if(auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0)))Runner->GetRhythm()->ResetRun();
    }
    LastUpdateSeconds=Now;
    State = EBootRunState::Running;
    SetProcessesActive(true);
    if (ABootRunner* Runner = Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this, 0))) Runner->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    if (auto* PC = Cast<ABootPlayerController>(UGameplayStatics::GetPlayerController(this, 0))) PC->ShowPage(EBootPage::Hud);
    UE_LOG(LogTemp, Display, TEXT("BOOTLOOP V01: running"));
}

void ABootGameMode::UpdateRun()
{
    if (State != EBootRunState::Running) return;
    ABootRunner* Runner = Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!Runner) return;
    if(bMusicRun || bFirstRun)
    {
        const double Now=FPlatformTime::Seconds();const float Dt=float(FMath::Clamp(Now-LastUpdateSeconds,0.,.25));LastUpdateSeconds=Now;
        Track->UpdateCourse(Runner,Dt);
        if(bMusicRun)UpdateMusicRun(Runner,Now);
        else
        {
            RunSeconds=float(FirstRunClock.Seconds(Now));MaxDistance=Track->GetRouteProgress(Runner->GetActorLocation());
            RefreshFirstRunLesson();
            FBootMusicContext LessonMusic;LessonMusic.TrackHash=TEXT("first_run");LessonMusic.TrackEpoch=1;
            LessonMusic.Section=PreparedLesson;LessonMusic.LocalSeconds=RunSeconds;
            Track->ApplyRunPresentation(LessonMusic,TrialEnvelope);
            if(TrialPendingEnvelope.bValid && State==EBootRunState::Running)
            {
                FBootSafeBoundary B{FirstRunToken,int64(PreparedLesson+1),TrialPendingEnvelope.Revision,true,true,true};
                if(Runner->GetParkour()->CanCommitTraversalEnvelope(B,240.f/130.f) && CastChecked<ABootFirstRunTrack>(Track)->IsInActiveLesson(Runner))
                {Runner->GetParkour()->CommitTraversalEnvelope(B,240.f/130.f);TrialEnvelope=TrialPendingEnvelope;TrialModifiers=TrialPendingModifiers;TrialPendingEnvelope={};Runner->GetAbilities()->ApplyEnvelope(TrialEnvelope);}
            }
        }
        if(State==EBootRunState::Running && Track->IsFatal(Runner->GetActorLocation(),Runner->GetCapsuleComponent()->GetScaledCapsuleRadius(),Runner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()))Runner->Die();
        return;
    }
    RunSeconds += .05f;
    Track->UpdateCourse(Runner,.05f);
    MaxDistance = FMath::Max(MaxDistance, Track->GetRouteProgress(Runner->GetActorLocation()));
    if (Track->IsFatal(Runner->GetActorLocation(), Runner->GetCapsuleComponent()->GetScaledCapsuleRadius(), Runner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())) Runner->Die();
    if (State==EBootRunState::Running && Track->IsFinished(Runner->GetActorLocation())) HandleFinish();
}

int32 ABootGameMode::GetTotalScore() const
{
    if(bTrainingCourse) return 0;
    if(bMusicRun && MusicRun)return int32(FMath::Clamp<int64>(MusicRun->Snapshot().Score,0,MAX_int32));
    const auto* Runner = Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this, 0));
    return FMath::FloorToInt(MaxDistance) * 10 + (Runner ? Runner->GetRhythm()->GetScore().Score : 0);
}

void ABootGameMode::SaveResult()
{
    if (bResultSaved || bTrainingCourse) return;
    bResultSaved = true;
    if (auto* GI = GetGameInstance<UBootGameInstance>())
    {
        if(bMusicRun && MusicRun)
        {
            MusicRun->SetPaused(true,FPlatformTime::Seconds());
            const auto S=MusicRun->Snapshot();GI->GetProfile()->UpdateMusicRunRecord(S.Score,S.DistanceMetres,S.ActiveSeconds);
            ++GI->GetProfile()->Runs;GI->SaveProfile();return;
        }
        if(!bAuthoredCourse) GI->GetProfile()->UpdateBest(GetTotalScore(), MaxDistance);
        GI->GetProfile()->UpdateCourseRecord(GetCourseId(),GetTotalScore(),MaxDistance,RunSeconds,State==EBootRunState::Complete);
        ++GI->GetProfile()->Runs;
        GI->SaveProfile();
    }
}

void ABootGameMode::HandleDeath()
{
    if (State != EBootRunState::Running) return;
    SetProcessesActive(false);
    if(bFirstRun)FirstRunClock.SetPaused(true,FPlatformTime::Seconds());
    if(bMusicRun)
    {
        if(auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0)))UpdateMusicRun(Runner,FPlatformTime::Seconds());
        StopV04Run(false);
    }
    if(bTrainingCourse)
    {
        State=EBootRunState::Booting;
        GetWorldTimerManager().SetTimer(BootTimer,this,&ABootGameMode::RestoreCheckpoint,.45f,false);
        return;
    }
    State = EBootRunState::Dead;
    SaveResult();
    if (auto* GI = GetGameInstance<UBootGameInstance>()) GI->Sound(EBootSound::Fatal);
    if (auto* PC = Cast<ABootPlayerController>(UGameplayStatics::GetPlayerController(this, 0))) PC->ShowPage(EBootPage::Results);
    UE_LOG(LogTemp, Display, TEXT("BOOTLOOP V01: results score=%d distance=%.1f"), GetTotalScore(), MaxDistance);
}

void ABootGameMode::PauseRun(bool bPause)
{
    if (bPause && State != EBootRunState::Running) return;
    if (!bPause && State != EBootRunState::Paused) return;
    const double Now=FPlatformTime::Seconds();
    if(bMusicRun && MusicRun){MusicRun->SetPaused(bPause,Now);if(auto* Transport=GetMusicTransport())Transport->SetRunPaused(bPause);}
    if(bFirstRun)FirstRunClock.SetPaused(bPause,Now);
    SetProcessesActive(!bPause);LastUpdateSeconds=Now;
    if (auto* Runner = Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this, 0))) Runner->GetRhythm()->SetRunPaused(bPause);
    State = bPause ? EBootRunState::Paused : EBootRunState::Running;
    UGameplayStatics::SetGamePaused(this, bPause);
    UE_LOG(LogTemp, Display, TEXT("BOOTLOOP V01: pause=%d"), bPause);
}

void ABootGameMode::HandleFinish()
{
    if(State!=EBootRunState::Running) return;
    if(bMusicRun)return;
    if(bFirstRun)
    {
        const auto* First=Cast<ABootFirstRunTrack>(Track);if(!First || !First->GetTutorial().IsComplete())return;
        if(auto* GI=GetGameInstance<UBootGameInstance>()){GI->GetProfile()->OnboardingVersion=4;GI->SaveProfile();}
        if(auto* PC=Cast<ABootPlayerController>(UGameplayStatics::GetPlayerController(this,0))){PC->Desktop();PC->ShowMusicLibrary();}
        else ReturnToDesktop();
        return;
    }
    State=EBootRunState::Complete;
    if(auto* Course=Cast<ABootCourseTrack>(Track))
    {
        Course->SetProcessesActive(false);
        if(bTrainingCourse) if(auto* GI=GetGameInstance<UBootGameInstance>())
        {
            GI->GetProfile()->bTutorialComplete=Course->GetTutorial().IsComplete();
            GI->GetProfile()->TutorialOutcomes.Reset();
            for(auto O:Course->GetTutorial().Outcomes) GI->GetProfile()->TutorialOutcomes.Add(int32(O));
            GI->SaveProfile();
        }
    }
    if(auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0)))
    {
        Runner->GetRhythm()->StopRun();
        Runner->GetCharacterMovement()->StopMovementImmediately();
        Runner->GetCharacterMovement()->DisableMovement();
    }
    SaveResult();
    if(auto* GI=GetGameInstance<UBootGameInstance>()) GI->Sound(EBootSound::Combo);
    if(auto* PC=Cast<ABootPlayerController>(UGameplayStatics::GetPlayerController(this,0))) PC->ShowPage(EBootPage::Results);
    UE_LOG(LogTemp,Display,TEXT("BOOTLOOP V02: trial complete time=%.2f score=%d"),RunSeconds,GetTotalScore());
}

void ABootGameMode::ReturnToDesktop()
{
    SetProcessesActive(false);
    if (State == EBootRunState::Running || State == EBootRunState::Paused || State == EBootRunState::Dead) SaveResult();
    StopV04Run(true);
    GetWorldTimerManager().ClearTimer(BootTimer);
    UGameplayStatics::SetGamePaused(this, false);
    State = EBootRunState::Desktop;
    if (auto* Runner = Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this, 0)))
    {
        Runner->GetRhythm()->StopRun();
        Runner->GetCharacterMovement()->StopMovementImmediately();
        Runner->GetCharacterMovement()->DisableMovement();
    }
    Seed = FMath::RandRange(1, MAX_int32);
}

FName ABootGameMode::GetCourseId() const
{ return bMusicRun?TEXT("MusicRunV04"):bFirstRun?TEXT("FirstRunV04"):bTrainingCourse?TEXT("FirstLoginV03"):bAuthoredCourse?TEXT("RooftopCircuitV03"):TEXT("WindowSurfV02"); }
void ABootGameMode::NotifyObjective(EBootTutorialEvent Event)
{ if(State==EBootRunState::Running) if(auto* Course=Cast<ABootCourseTrack>(Track)) Course->NotifyObjective(Event,AttemptGeneration); }
FString ABootGameMode::GetObjective() const
{
    if(const auto* First=Cast<ABootFirstRunTrack>(Track))
    {
        const auto* GI=GetGameInstance<UBootGameInstance>();return First->GetTutorial().GetPrompt(GI?GI->GetProfile()->Inputs:FBootInputPreferences{}).ToString();
    }
    const auto* Course=Cast<ABootCourseTrack>(Track);return Course?Course->GetObjective():FString();
}
void ABootGameMode::RestoreCheckpoint()
{
    if(bFirstRun)
    {
        auto* First=Cast<ABootFirstRunTrack>(Track);auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0));if(!First||!Runner)return;
        ++AttemptGeneration;Runner->ResetToTransform(First->PrepareCheckpoint(FirstRunToken,Runner),false);PreparedLesson=First->GetTutorial().GetLesson();ResetTrialLoadout(Runner);
        State=EBootRunState::Running;FirstRunClock.SetPaused(false,FPlatformTime::Seconds());LastUpdateSeconds=FPlatformTime::Seconds();SetProcessesActive(true);
        if(auto* PC=Cast<ABootPlayerController>(Runner->GetController()))PC->ShowPage(EBootPage::Hud);return;
    }
    auto* Course=Cast<ABootCourseTrack>(Track); auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!Course || !Runner || !bTrainingCourse) return;
    ++AttemptGeneration;
    Runner->ResetToTransform(Course->PrepareCheckpoint(AttemptGeneration,Runner),false);
    State=EBootRunState::Running; Course->SetProcessesActive(true);
    if(auto* PC=Cast<ABootPlayerController>(Runner->GetController())) PC->ShowPage(EBootPage::Hud);
}
void ABootGameMode::SkipLesson()
{
    if(!bTrainingCourse || (State!=EBootRunState::Paused && State!=EBootRunState::Running)) return;
    if(bFirstRun)
    {
        auto* First=Cast<ABootFirstRunTrack>(Track);auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0));if(!First||!Runner)return;
        UGameplayStatics::SetGamePaused(this,false);State=EBootRunState::Running;++AttemptGeneration;
        const auto Destination=First->SkipCurrentLesson(FirstRunToken,Runner);
        if(First->GetTutorial().IsComplete()){HandleFinish();return;}
        Runner->ResetToTransform(Destination,false);Runner->GetRhythm()->SetRunPaused(false);
        PreparedLesson=First->GetTutorial().GetLesson();ResetTrialLoadout(Runner);FirstRunClock.SetPaused(false,FPlatformTime::Seconds());LastUpdateSeconds=FPlatformTime::Seconds();SetProcessesActive(true);
        if(auto* PC=Cast<ABootPlayerController>(Runner->GetController()))PC->ShowPage(EBootPage::Hud);return;
    }
    auto* Course=Cast<ABootCourseTrack>(Track); auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!Course || !Runner) return;
    const bool bWasPaused=State==EBootRunState::Paused;
    UGameplayStatics::SetGamePaused(this,false); ++AttemptGeneration;
    Runner->ResetToTransform(Course->SkipLesson(AttemptGeneration,Runner),false);
    if(bWasPaused) Runner->GetRhythm()->SetRunPaused(false);
    State=EBootRunState::Running; Course->SetProcessesActive(true);
    if(auto* PC=Cast<ABootPlayerController>(Runner->GetController())) PC->ShowPage(EBootPage::Hud);
}
void ABootGameMode::HandleThreat(ACharacter* Target,int32 Generation)
{
    if(Generation!=AttemptGeneration || State!=EBootRunState::Running) return;
    if(auto* Runner=Cast<ABootRunner>(Target)) Runner->Die();
}

void ABootGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
#if !UE_BUILD_SHIPPING
    BootloopV04Audit::Stop();
#endif
    SetProcessesActive(false);StopV04Run(true);
    GetWorldTimerManager().ClearTimer(RunTimer);
    GetWorldTimerManager().ClearTimer(BootTimer);
    Super::EndPlay(Reason);
}

void ABootGameMode::SetProcessesActive(bool bActive)
{
    if(auto* Endless=Cast<ABootEndlessTrack>(Track))Endless->SetProcessesActive(bActive);
    if(auto* First=Cast<ABootFirstRunTrack>(Track))First->SetProcessesActive(bActive);
    if(auto* Course=Cast<ABootCourseTrack>(Track))Course->SetProcessesActive(bActive);
}
void ABootGameMode::StopV04Run(bool bReset)
{
    if(!bMusicRun && !bFirstRun)return;
    auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0));
    if(MusicRun)
    {
        const auto Token=MusicRun->Snapshot().Run;
        if(auto* Transport=GetMusicTransport())Transport->StopRun(Token);
        MusicRun->SetPaused(true,FPlatformTime::Seconds());MusicRun->EndRun();
        if(bReset)MusicRun->Reset();
    }
    FirstRunClock.SetPaused(true,FPlatformTime::Seconds());
    TrialOffer.Reset();TrialEnvelope={};TrialPendingEnvelope={};TrialModifiers={};TrialPendingModifiers={};
    if(Runner)
    {
        Runner->GetRhythm()->StopRun();Runner->GetRhythm()->SetMusicTransport(nullptr);
        Runner->GetAbilities()->ResetRun({});
        Runner->GetParkour()->ResetParkour();
        if(auto* Effects=Runner->FindComponentByClass<UBootRunEffectsComponent>())Effects->ResetRun();
    }
    if(MusicRun)MusicRun->SetAbilitySnapshot({});
    ProcessIds.Reset();NextProcessId=1;
    if(bReset){FirstRunToken={};FirstRunClock={};PreparedLesson=INDEX_NONE;}
}
void ABootGameMode::BindMusicTrack(const FBootRunToken& Token)
{
    ProcessIds.Reset();NextProcessId=1;
    auto* Endless=CastChecked<ABootEndlessTrack>(Track);
    Endless->OnChunkCommitted.Clear();Endless->OnChunkRetired.Clear();Endless->OnRunOperation.Clear();Endless->OnRunThreatContact.Clear();
    Endless->OnChunkCommitted.AddWeakLambda(this,[this,Token](const FBootCommittedChunk& Chunk)
    {
        if(!MusicRun || !MusicRun->Snapshot().bRunning || MusicRun->Snapshot().Run!=Token || Chunk.Run!=Token)return;
        MusicRun->RegisterChunk(Chunk.InstanceId,Chunk.OperationKeys);
        TArray<FBootOperationKey> NewProcesses;
        for(const auto& Hazard:Chunk.Recipe.Hazards)
        {
            const FBootOperationKey Key{Token,Chunk.InstanceId,Hazard.OperationId};
            if(!ProcessIds.Contains(Key))NewProcesses.AddUnique(Key);
        }
        if(!ensureMsgf(Chunk.Recipe.Hazards.Num()<=8 && ProcessIds.Num()+NewProcesses.Num()<=40*8
            && NextProcessId<=MAX_int64-NewProcesses.Num(),TEXT("Validated live process identity budget exceeded")))return;
        for(const auto& Key:NewProcesses)ProcessIds.Add(Key,NextProcessId++);
    });
    Endless->OnChunkRetired.AddWeakLambda(this,[this,Token](int64 Id)
    {
        if(!MusicRun || MusicRun->Snapshot().Run!=Token)return;
        MusicRun->RetireChunk(Id);
        auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0));
        for(auto It=ProcessIds.CreateIterator();It;++It)
            if(It.Key().Run==Token && It.Key().ChunkId==Id)
            {
                if(Runner)Runner->GetAbilities()->NotifyProcessSeparated(It.Value());
                It.RemoveCurrent();
            }
    });
    Endless->OnRunOperation.AddWeakLambda(this,[this,Token](const FBootOperationKey& Key,EBootBeatRating Rating)
    {
        if(State!=EBootRunState::Running || !MusicRun || MusicRun->Snapshot().Run!=Token || Key.Run!=Token || !MusicRun->AwardOperation(Key,Rating))return;
        if(auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0)))
        {
            const auto S=MusicRun->Snapshot();Runner->GetRhythm()->ShowSurfFeedback(Rating,S.LastPoints,S.Combo);
            const int32 PreviousRecharge=Runner->GetAbilities()->Snapshot().GuardRechargeSerial;
            Runner->GetAbilities()->NotifySurfCompleted(Key);
            if(auto* Effects=Runner->FindComponentByClass<UBootRunEffectsComponent>())
            {
                if(Runner->GetAbilities()->Snapshot().GuardRechargeSerial!=PreviousRecharge)Effects->Emit(EBootFeedbackEvent::GuardRecharge,Runner->GetActorTransform());
                if(const auto* Endless=Cast<ABootEndlessTrack>(Track))for(const ABootEndlessChunk* Chunk:Endless->GetLiveChunks())
                    if(Chunk && Chunk->GetDescriptor().InstanceId==Key.ChunkId)
                        for(const auto& Hazard:Chunk->GetDescriptor().Recipe.Hazards)
                            if(Hazard.bPopup && Hazard.OperationId==Key.LocalId)Effects->Emit(EBootFeedbackEvent::Intercept,Runner->GetActorTransform());
            }
        }
    });
    Endless->OnRunThreatContact.AddWeakLambda(this,[this,Token](ACharacter* Character,const FBootOperationKey& Key)
    {
        if(State!=EBootRunState::Running || !MusicRun || MusicRun->Snapshot().Run!=Token || Key.Run!=Token)return;
        const int64* ProcessId=ProcessIds.Find(Key);if(!ProcessId)return;
        if(auto* Runner=Cast<ABootRunner>(Character))
        {
            // Distinct process instances in one chunk must never share guard contact immunity.
            const int32 PreviousGuard=Runner->GetAbilities()->Snapshot().GuardEventSerial;
            if(!Runner->GetAbilities()->TryAbsorbProcess(Token,*ProcessId,MusicRun->Snapshot().ActiveSeconds))Runner->Die();
            else if(Runner->GetAbilities()->Snapshot().GuardEventSerial!=PreviousGuard)
                if(auto* Effects=Runner->FindComponentByClass<UBootRunEffectsComponent>())Effects->Emit(EBootFeedbackEvent::GuardHit,Runner->GetActorTransform());
        }
    });
}
void ABootGameMode::RefreshCardEligibility()
{
    auto* Endless=Cast<ABootEndlessTrack>(Track);if(!MusicRun || !Endless)return;
    const auto S=MusicRun->Snapshot();FBootCardEligibilityContext C;
    C.Modifiers=S.RewardModifiers;C.Envelope=S.bPendingMutation?S.PendingEnvelope:S.Envelope;
    C.bCanPrepareProcessBudget=C.bCanPrepareCombinations=C.bCanPrepareThinCache=C.bCanPrepareRiskySurf=true;
    // Each advertised risk must have a legal future continuation under its actual candidate build.
    for(const auto& Definition:FBootRunCardRules::Catalog())
    {
        FBootRunModifiers Candidate;FText Error;
        if(!FBootRunCardRules::Apply(Definition.Id,C,Candidate,Error))continue;
        auto Envelope=FBootRunCardRules::ApplyToEnvelope(FBootTraversalPolicy::FromTempo(S.Music.EffectiveBpm,C.Envelope.Revision+1),Candidate);
        if(!Endless->CanPrepareBuild(Candidate,Envelope))C.BlockedCardIds.Add(Definition.Id);
    }
    C.bCanPrepareProcessBudget=!C.BlockedCardIds.Contains(TEXT("PRIORITY ESCALATION"));
    C.bCanPrepareCombinations=!C.BlockedCardIds.Contains(TEXT("HOT LOOP"));
    C.bCanPrepareThinCache=!C.BlockedCardIds.Contains(TEXT("THIN CACHE"));
    C.bCanPrepareRiskySurf=!C.BlockedCardIds.Contains(TEXT("UNSAFE WINDOWS"));
    MusicRun->SetCardEligibility(C);
}
void ABootGameMode::UpdateMusicRun(ABootRunner* Runner,double Now)
{
    if(!MusicRun || !Runner)return;auto* Endless=CastChecked<ABootEndlessTrack>(Track);
    FBootMusicContext Context=MusicRun->Snapshot().Music;
    if(auto* Transport=GetMusicTransport())
    {
        const auto T=Transport->Snapshot();if(T.bValid && T.Run==MusicRun->Snapshot().Run)Context=T;
    }
    MusicRun->SetRecoveryReady(Endless->IsRecoveryReady(Runner));
    if(Now>=NextEligibilitySeconds){RefreshCardEligibility();NextEligibilitySeconds=Now+.5;}
    MusicRun->Update(Now,Endless->GetProgressMetres(),Context);
    auto S=MusicRun->Snapshot();
    Endless->ApplyRunPresentation(S.Music,S.Envelope);
    Endless->SetDirectorContext(S.Music,S.ActiveSeconds,S.bPendingMutation?S.PendingModifiers:S.ActiveModifiers);
    if(S.bPendingMutation)
    {
        FText Error;
        if(Endless->CanPrepareBuild(S.PendingModifiers,S.PendingEnvelope) && Endless->PrepareRevision(S.PendingEnvelope,Error))
        {
            Runner->GetParkour()->ApplyTraversalEnvelope(S.PendingEnvelope);
            FBootSafeBoundary B;const float BarSeconds=float(240./FMath::Clamp(S.Music.EffectiveBpm,40.,300.));
            // These predicates are all non-mutating and execute on the same game-thread turn.
            // The coordinator preflight mirrors TryCommit's token/revision contract.
            if(Endless->PeekRevisionBoundary(B) && B.Run==S.Run && B.PreparedRevision==S.PendingEnvelope.Revision
                && B.ChunkId>0 && B.bSupported && B.bNoActiveCommitment && B.bFutureReady
                && S.bRunning && !S.bPaused && Runner->GetParkour()->CanCommitTraversalEnvelope(B,BarSeconds))
            {
                if(MusicRun->CommitPreparedRevision(B))
                {
                    const bool bTrackCommitted=Endless->CommitPreparedRevision(B);
                    const bool bMovementCommitted=Runner->GetParkour()->CommitTraversalEnvelope(B,BarSeconds);
                    ensureMsgf(bTrackCommitted && bMovementCommitted,TEXT("Preflighted traversal commit must remain atomic on game thread"));
                    Runner->GetAbilities()->ApplyEnvelope(S.PendingEnvelope);
                }
            }
        }
    }
    auto Abilities=Runner->GetAbilities()->Snapshot();Abilities.DashCharges=Runner->GetParkour()->GetDashCharges();
    Abilities.MaxDashCharges=Runner->GetParkour()->GetMaxDashCharges();Abilities.RedirectCharges=Runner->GetParkour()->GetAirRedirectCharges();
    Abilities.MaxRedirectCharges=Runner->GetParkour()->GetMaxAirRedirectCharges();Abilities.DashEventSerial=Runner->GetParkour()->GetDashSerial();
    Abilities.RedirectEventSerial=Runner->GetParkour()->GetAirRedirectSerial();MusicRun->SetAbilitySnapshot(Abilities);
}
void ABootGameMode::ChooseRunCard(int32 Index)
{
    if(State!=EBootRunState::Running || Index<0 || Index>2)return;
    if(bMusicRun && MusicRun)
    {
        const double PressNow=FPlatformTime::Seconds();
        const auto Existing=MusicRun->Snapshot();
        if(!Existing.CardOffer.IsSet() || !Existing.CardOffer->CardIds.IsValidIndex(Index))return;
        const FName Card=Existing.CardOffer->CardIds[Index];
        const FBootRunToken PressRun=Existing.Run;const int64 PressOffer=Existing.CardOffer->OfferId;
        auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0));if(!Runner)return;
        // Preserve the offer that existed at input entry. Update cannot manufacture the identity
        // for this press; it only flushes old-multiplier distance and maps PressNow to active time.
        UpdateMusicRun(Runner,PressNow);RefreshCardEligibility();
        const double PressActiveSeconds=MusicRun->Snapshot().ActiveSeconds;FText Error;
        if(MusicRun->TryChoose({PressRun,PressOffer,Index,++CardPressSerial,PressActiveSeconds},Error))
            if(auto* Effects=Runner->FindComponentByClass<UBootRunEffectsComponent>())
                for(const auto& Definition:FBootRunCardRules::Catalog())if(Definition.Id==Card)
                    Effects->Emit(Definition.Kind==EBootCardKind::Risk?EBootFeedbackEvent::CardRisk:EBootFeedbackEvent::CardBuff,Runner->GetActorTransform());
        return;
    }
    if(!bFirstRun || !TrialOffer.IsSet() || !TrialOffer->CardIds.IsValidIndex(Index))return;
    const auto Evidence=CaptureFirstRunEvidence(EBootFirstRunEvent::CardChosen);if(!Evidence.Run.RunId.IsValid())return;
    FBootCardEligibilityContext C;C.Envelope=TrialEnvelope;C.Modifiers=TrialModifiers;FBootRunModifiers Chosen;FText Error;
    if(!FBootRunCardRules::Apply(TrialOffer->CardIds[Index],C,Chosen,Error))return;
    auto Base=FBootTraversalPolicy::FromTempo(130.,TrialEnvelope.Revision+1);Base.DashCharges=1;Base.GuardCharges=1;
    TrialPendingEnvelope=FBootRunCardRules::ApplyToEnvelope(Base,Chosen);TrialPendingModifiers=Chosen;TrialOffer.Reset();
    if(auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0)))Runner->GetParkour()->ApplyTraversalEnvelope(TrialPendingEnvelope);
    SubmitFirstRunEvidence(Evidence);
    if(auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0)))
        if(auto* Effects=Runner->FindComponentByClass<UBootRunEffectsComponent>())Effects->Emit(EBootFeedbackEvent::CardBuff,Runner->GetActorTransform());
}
FBootTutorialEvidence ABootGameMode::CaptureFirstRunEvidence(EBootFirstRunEvent Event)
{
    auto* First=Cast<ABootFirstRunTrack>(Track);auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!bFirstRun || State!=EBootRunState::Running || !First || !First->IsInActiveLesson(Runner))return {};
    return First->CaptureEvidence(Event,++ActionSerial);
}
void ABootGameMode::SubmitFirstRunEvidence(const FBootTutorialEvidence& Evidence)
{
    if(!bFirstRun || State!=EBootRunState::Running)return;
    // Advance/teleport on the mode update, after Runner finishes its pending-action iteration.
    if(auto* First=Cast<ABootFirstRunTrack>(Track))First->AcceptEvidence(Evidence);
}
void ABootGameMode::ResetTrialLoadout(ABootRunner* Runner)
{
    TrialModifiers={};TrialPendingModifiers={};TrialPendingEnvelope={};TrialOffer.Reset();
    TrialEnvelope=FBootTraversalPolicy::FromTempo(130.,1);
    if(PreparedLesson==int32(EBootFirstRunLesson::CardsAbilities))
    {
        TrialEnvelope.DashCharges=1;TrialEnvelope.GuardCharges=1;
        const auto* First=CastChecked<ABootFirstRunTrack>(Track);
        TrialOffer=FBootCardOffer{FirstRunToken,int64(First->GetTutorial().GetSection()),FirstRunClock.Seconds(FPlatformTime::Seconds()),{TEXT("AIR CACHE"),TEXT("WALL CACHE"),TEXT("AERO BUFFER")}};
    }
    Runner->GetParkour()->ApplyTraversalEnvelope(TrialEnvelope);
    Runner->GetAbilities()->ResetRun(FirstRunToken);Runner->GetAbilities()->ApplyEnvelope(TrialEnvelope);
}
void ABootGameMode::RefreshFirstRunLesson()
{
    auto* First=Cast<ABootFirstRunTrack>(Track);auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!First || !Runner || State!=EBootRunState::Running)return;
    if(First->GetTutorial().IsComplete()){HandleFinish();return;}
    if(PreparedLesson==First->GetTutorial().GetLesson())return;
    SetProcessesActive(false);++AttemptGeneration;PreparedLesson=First->GetTutorial().GetLesson();
    Runner->ResetToTransform(First->PrepareCheckpoint(FirstRunToken,Runner),false);ResetTrialLoadout(Runner);SetProcessesActive(true);
}
