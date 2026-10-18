#include "Developer/BootSmoke.h"
#if !UE_BUILD_SHIPPING
#include "Run/BootGameMode.h"
#include "UI/BootPlayerController.h"
#include "Core/BootGameInstance.h"
#include "UI/BootShellWidget.h"
#include "Player/BootRunner.h"
#include "Music/BootRhythmComponent.h"
#include "World/Generation/BootTrack.h"
#include "World/Generation/BootSurfTrack.h"
#include "World/Interaction/BootSurfWindow.h"
#include "Containers/Ticker.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Opt-in real-world integration test. Never synthesizes OS input or runs in Shipping.
// A separate save slot isolates diagnostics from the player's records/preferences.
struct FBootSmoke : TSharedFromThis<FBootSmoke>
{
    TWeakObjectPtr<ABootGameMode> Game;
    int32 Stage=0, Failures=0, JumpScore=0;
    double Entered=FPlatformTime::Seconds(), PausedTransport=0.;
    FVector PausedPosition;
    FString Checks;
    TSet<FString> SeenChecks;
    bool bCaptured=false;
    double CapturedAt=0.;
    void Check(const TCHAR* Name,bool Passed)
    {
        if(SeenChecks.Contains(Name)) return;
        SeenChecks.Add(Name);
        if(!Checks.IsEmpty()) Checks+=TEXT(",\n");
        Checks+=FString::Printf(TEXT("    {\"check\":\"%s\",\"pass\":%s}"),Name,Passed?TEXT("true"):TEXT("false"));
        if(!Passed) ++Failures;
        UE_LOG(LogTemp,Display,TEXT("BOOTLOOP SMOKE: %s %s"),Passed?TEXT("PASS"):TEXT("FAIL"),Name);
    }
    void Next() { ++Stage; Entered=FPlatformTime::Seconds(); bCaptured=false; }
    void Shot(ABootPlayerController* PC,const TCHAR* Name)
    {
        UE_LOG(LogTemp,Display,TEXT("BOOTLOOP SMOKE: screenshot %s"),Name);
        PC->ConsoleCommand(TEXT("Shot ShowUI"),false);
    }
    bool Capture(ABootPlayerController* PC,const TCHAR* Name)
    {
        if(!bCaptured) { Shot(PC,Name); bCaptured=true; CapturedAt=FPlatformTime::Seconds(); return false; }
        return FPlatformTime::Seconds()-CapturedAt>.25;
    }
    bool Tick()
    {
        ABootGameMode* G=Game.Get(); if(!G) return false;
        UWorld* World=G->GetWorld();
        auto* PC=Cast<ABootPlayerController>(World->GetFirstPlayerController());
        auto* Runner=PC?Cast<ABootRunner>(PC->GetPawn()):nullptr;
        auto* GI=G->GetGameInstance<UBootGameInstance>();
        if(!PC || !Runner || !GI) return true;
        auto* Rhythm=Runner->GetRhythm();
        const double Age=FPlatformTime::Seconds()-Entered;
        if(Age>15.) { Check(TEXT("stage timeout"),false); return Finish(); }
        switch(Stage)
        {
        case 0:
            if(Age<1.)break;
            Check(TEXT("starts on desktop"),G->GetRunState()==EBootRunState::Desktop && PC->GetPage()==EBootPage::Desktop);
            Check(TEXT("menu stops music"),!Rhythm->IsReady());
            Check(TEXT("UMG shell created"),PC->GetShell()!=nullptr);
            Shot(PC,TEXT("desktop")); Next(); break;
        case 1:
            if(Age<.4)break; PC->ShowSettings(); Next(); break;
        case 2:
            if(Age<.4)break;
            Check(TEXT("settings page"),PC->GetPage()==EBootPage::Settings);
            if(!Capture(PC,TEXT("settings")))break;
            { FBootOptions O=GI->GetOptions(); O.Fov=100; O.Music=.42f; GI->ApplyOptions(O,false); }
            PC->Back(); PC->ShowControls(); Next(); break;
        case 3:
            if(Age<.4 || !Capture(PC,TEXT("controls")))break; PC->Back(); PC->StartRun(); Next(); break;
        case 4:
            if(G->GetRunState()!=EBootRunState::Running || !Rhythm->IsReady())break;
            Check(TEXT("run and real audio ready"),true);
            Check(TEXT("fresh score is zero"),G->GetTotalScore()==0);
            Next(); break;
        case 5:
            if(!Runner->GetCharacterMovement()->IsMovingOnGround())break;
            if(FMath::Fmod(Rhythm->GetTransportSeconds(),FBootRhythmScore::BeatSeconds)>.065)break;
            Runner->RequestJump(); Next(); break;
        case 6:
            if(Age<.9)break;
            if(FMath::Fmod(Rhythm->GetTransportSeconds(),FBootRhythmScore::BeatSeconds)>.065)break;
            JumpScore=Rhythm->GetScore().Score;
            Check(TEXT("jump never awards rhythm points"),JumpScore==0);
            Runner->GetCharacterMovement()->Velocity=FVector(900,0,0); Runner->StartSlide(); Next(); break;
        case 7:
            if(Age<.12)break;
            if(!Capture(PC,TEXT("gameplay")))break;
            Check(TEXT("slide never awards rhythm points"),Rhythm->GetScore().Score==0);
            PC->TogglePause(); PC->ShowSettings(); Next(); break;
        case 8:
            if(Age<.4)break;
            PausedTransport=Rhythm->GetTransportSeconds(); PausedPosition=Runner->GetActorLocation();
            Check(TEXT("settings retain paused state"),G->GetRunState()==EBootRunState::Paused && World->IsPaused());
            Next(); break;
        case 9:
            if(Age<.8)break;
            Check(TEXT("pause freezes audio clock"),FMath::Abs(Rhythm->GetTransportSeconds()-PausedTransport)<.06);
            Check(TEXT("pause freezes physics"),Runner->GetActorLocation().Equals(PausedPosition,.1));
            if(PC->GetPage()==EBootPage::Settings) PC->Back();
            if(!Capture(PC,TEXT("pause")))break;
            PC->TogglePause(); Next(); break;
        case 10:
            if(Age<.4)break; Runner->Die(); Next(); break;
        case 11:
            if(Age<.3)break;
            Check(TEXT("death opens results"),G->GetRunState()==EBootRunState::Dead && PC->GetPage()==EBootPage::Results);
            Check(TEXT("death retains earned score"),G->GetTotalScore()>=JumpScore && GI->GetProfile()->BestScore>=G->GetTotalScore());
            Check(TEXT("death stops music"),!Rhythm->IsReady());
            Check(TEXT("local profile saved"),!GI->HasSaveError());
            if(!Capture(PC,TEXT("results")))break;
            PC->StartRun(); Next(); break;
        case 12:
            if(G->GetRunState()!=EBootRunState::Running || !Rhythm->IsReady())break;
            Check(TEXT("retry clears score distance"),G->GetTotalScore()==0 && G->GetDistance()==0);
            Check(TEXT("retry resets music transport"),Rhythm->GetTransportSeconds()<4.);
            Check(TEXT("retry resets track pool"),G->GetTrack()->GetActiveCount()<=9);
            Next(); break;
        case 13:
            if(auto* Surf=Cast<ABootSurfTrack>(G->GetTrack()))
            {
                Runner->SetActorLocation(FVector(2300,0,100),false,nullptr,ETeleportType::TeleportPhysics);
                Runner->GetCharacterMovement()->StopMovementImmediately();
                Runner->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
                // Handle sits behind the platform centre; the assisted interaction accepts the based rider.
                PC->SetControlRotation(FRotator(-15,0,0));
                Next();
            }
            else { PC->Desktop(); return Finish(); }
            break;
        case 14:
            if(Age<.2)break;
            Check(TEXT("snap window selected from its real base"),Runner->FindSurfTarget()!=nullptr);
            Runner->RequestSnap(); Next(); break;
        case 15:
            if(Age<1.7)break;
            Check(TEXT("actual window carried the runner"),Cast<ABootSurfTrack>(G->GetTrack())->GetWindows()[0]->HasCarried(Runner));
            if(!Capture(PC,TEXT("window surf")))break;
            Runner->GetCharacterMovement()->Velocity=FVector(900,0,0); Runner->RequestJump(); Next(); break;
        case 16:
            if(Age<.9)break;
            Check(TEXT("completed surf awards operation points"),Rhythm->GetScore().Score>0);
            Check(TEXT("surf departure reaches solid upper landing"),Runner->GetCharacterMovement()->IsMovingOnGround() && Runner->GetActorLocation().X>3510);
            PC->SetControlRotation(FRotator(-75,0,0));
            if(!Capture(PC,TEXT("body and upper route")))break;
            Next(); break;
        case 17:
            if(Age<.3)break;
            Runner->SetActorLocation(FVector(26100,0,-220),false,nullptr,ETeleportType::TeleportPhysics);
            Runner->GetCharacterMovement()->StopMovementImmediately(); Next(); break;
        case 18:
            if(Age<.2)break;
            Check(TEXT("finish opens successful results"),G->GetRunState()==EBootRunState::Complete && PC->GetPage()==EBootPage::Results);
            if(!Capture(PC,TEXT("complete")))break;
            PC->Desktop(); return Finish();
        }
        return true;
    }
    bool Finish()
    {
        const FString Dir=FPaths::ProjectSavedDir()/TEXT("V02Audit");
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Dir);
        FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"failures\":%d,\"checks\":[\n%s\n]}"),Failures,*Checks),*(Dir/TEXT("LiveSmoke.json")));
        UE_LOG(LogTemp,Display,TEXT("BOOTLOOP_SMOKE_%s failures=%d"),Failures?TEXT("FAIL"):TEXT("PASS"),Failures);
        if(FParse::Param(FCommandLine::Get(),TEXT("QuitAfterSmoke"))) FPlatformMisc::RequestExit(false);
        return false;
    }
};

void StartBootSmoke(ABootGameMode* Game)
{
    auto Smoke=MakeShared<FBootSmoke>(); Smoke->Game=Game;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Smoke](float){ return Smoke->Tick(); }),.025f);
}
#endif
