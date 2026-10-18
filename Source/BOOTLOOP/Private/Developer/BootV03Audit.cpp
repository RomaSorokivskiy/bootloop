#include "Developer/BootV03Audit.h"
#if !UE_BUILD_SHIPPING
#include "Run/BootGameMode.h"
#include "UI/BootPlayerController.h"
#include "Core/BootGameInstance.h"
#include "World/Generation/BootCourseTrack.h"
#include "Player/BootRunner.h"
#include "Music/BootRhythmComponent.h"
#include "Movement/BootMovementComponent.h"
#include "World/Interaction/BootSurfWindow.h"
#include "World/Threats/BootPopupProcess.h"
#include "World/Threats/BootScanProcess.h"
#include "Player/BootAvatarComponent.h"
#include "Player/BootFeedbackComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"

namespace
{
FString JsonString(FString Value)
{
    Value.ReplaceInline(TEXT("\\"),TEXT("\\\\")); Value.ReplaceInline(TEXT("\""),TEXT("\\\""));
    Value.ReplaceInline(TEXT("\n"),TEXT("\\n")); Value.ReplaceInline(TEXT("\r"),TEXT("\\r"));
    return TEXT("\"")+Value+TEXT("\"");
}
struct FBootV03Audit : TSharedFromThis<FBootV03Audit>
{
    TWeakObjectPtr<ABootGameMode> Game;
    TWeakObjectPtr<ABootSurfWindow> Ride;
    TWeakObjectPtr<ABootPopupProcess> Popup;
    TWeakObjectPtr<ABootScanProcess> Scan;
    int32 Stage=0,Failures=0,RestoreCount=0,BeforeGeneration=0,BeforeRuns=0,BeforeRecords=0,BeforeLegacyScore=0;
    int32 BeforeRhythmScore=0,FrameIndex=0,FpsIndex=0,FootCount=0,BeforeFootCount=0;
    bool bRestorePending=false,bJumpRequested=false,bContactBound=false,bSawAir=false,bSawLanding=false;
    bool bWristRedirectRequested=false;
    double Entered=FPlatformTime::Seconds(),Started=Entered,PausedTransport=0.;
    FVector PausedPosition,PausedProcess;
    FVector DriveDirection=FVector::ZeroVector;
    EBootScanPhase PausedPhase=EBootScanPhase::Dormant;
    FBootOptions OriginalOptions;
    FString Checks,Shots,Contacts,Map,Dir,ReportDir,PendingCapture;
    double CaptureRequestedAt=0.;
    TSet<FString> SeenChecks;
    TArray<FString> ShotFiles;
    void Check(const FString& Name,bool bPass,const FString& Detail=FString())
    {
        if(SeenChecks.Contains(Name)) return; SeenChecks.Add(Name);
        if(!Checks.IsEmpty()) Checks+=TEXT(",\n");
        Checks+=FString::Printf(TEXT("    {\"check\":%s,\"pass\":%s,\"detail\":%s}"),*JsonString(Name),bPass?TEXT("true"):TEXT("false"),*JsonString(Detail));
        if(!bPass) ++Failures;
        UE_LOG(LogTemp,Display,TEXT("BOOTLOOP V03 AUDIT: %s %s %s"),bPass?TEXT("PASS"):TEXT("FAIL"),*Name,*Detail);
    }
    void Go(int32 Next) { Stage=Next; Entered=FPlatformTime::Seconds(); DriveDirection=FVector::ZeroVector; }
    void Drive(UWorld* World,ELevelTick,float)
    {
        // Input is consumed every gameplay frame. A 40Hz audit ticker cannot hold
        // movement by feeding only one frame in a fast rendered game.
        auto* G=Game.Get();
        if(!G || G->GetWorld()!=World || World->IsPaused() || G->GetRunState()!=EBootRunState::Running || DriveDirection.IsNearlyZero()) return;
        if(auto* PC=World->GetFirstPlayerController())
            if(auto* Runner=Cast<ABootRunner>(PC->GetPawn())) Runner->AddMovementInput(DriveDirection,1,true);
    }
    void FootContact(int32 Foot,FVector Position)
    {
        ++FootCount; if(!Contacts.IsEmpty()) Contacts+=TEXT(",\n");
        Contacts+=FString::Printf(TEXT("    {\"foot\":%d,\"stage\":%d,\"elapsed_seconds\":%.4f,\"position_cm\":[%.3f,%.3f,%.3f]}"),
            Foot,Stage,FPlatformTime::Seconds()-Started,Position.X,Position.Y,Position.Z);
    }
    void Shot(ABootPlayerController* PC,ABootRunner* Runner,const FString& Label)
    {
        const FString File=Dir/(Label+TEXT(".png"));
        ShotFiles.Add(File);
        if(!Shots.IsEmpty()) Shots+=TEXT(",\n");
        const FVector P=Runner->GetActorLocation();
        const auto* Mode=Game.Get(); const auto* Course=Mode?Cast<ABootCourseTrack>(Mode->GetTrack()):nullptr;
        Shots+=FString::Printf(TEXT("    {\"label\":%s,\"file\":%s,\"elapsed_seconds\":%.4f,\"game_delta_seconds\":%.6f,\"position_cm\":[%.3f,%.3f,%.3f],\"grounded\":%s,\"sliding\":%s,\"wallrun\":%s,\"course_node\":%d,\"tutorial_lesson\":%d,\"attempt_generation\":%d}"),
            *JsonString(Label),*JsonString(File),FPlatformTime::Seconds()-Started,Runner->GetWorld()->GetDeltaSeconds(),P.X,P.Y,P.Z,
            Runner->GetParkour()->IsMovingOnGround()?TEXT("true"):TEXT("false"),Runner->IsSliding()?TEXT("true"):TEXT("false"),Runner->GetParkour()->IsWallRunning()?TEXT("true"):TEXT("false"),
            Course?Course->GetCourseState().CurrentNodeId:INDEX_NONE,Course?Course->GetTutorial().CurrentLesson:0,Mode?Mode->GetAttemptGeneration():0);
        // Real viewport screenshot command, including UMG. Filename is deterministic for review.
        PC->ConsoleCommand(FString::Printf(TEXT("Shot ShowUI -nosuffix filename=\"%s\""),*File),false);
        UE_LOG(LogTemp,Display,TEXT("BOOTLOOP V03 AUDIT: screenshot %s"),*Label);
    }
    bool Capture(ABootPlayerController* PC,ABootRunner* Runner,const FString& Label)
    {
        // Shot is fulfilled by a later viewport draw. Keep the page, pose and location
        // intact until the render has had several frames to consume the request.
        if(PendingCapture!=Label) { PendingCapture=Label; CaptureRequestedAt=FPlatformTime::Seconds(); Shot(PC,Runner,Label); return false; }
        return FPlatformTime::Seconds()-CaptureRequestedAt>.2;
    }
    const FBootCourseNode* Node(ABootCourseTrack* Course,int32 Id) const
    { return Course->GetNodes().FindByPredicate([Id](const FBootCourseNode& N){return N.Id==Id;}); }
    void Place(ABootRunner* Runner,ABootCourseTrack* Course,const FTransform& At)
    { Runner->ResetToTransform(At,false); Course->UpdateCourse(Runner,1.f/60.f); }
    void GateTo(ABootRunner* Runner,ABootCourseTrack* Course,int32 Target)
    {
        // These are staged gate crossings, explicitly not evidence of a physically played full route.
        const int32 Route[]={1,11,2,3,4,5,6,7,8,9}; bool bFound=false;
        for(int32 Id:Route)
        {
            if(Id==Course->GetCourseState().CurrentNodeId) { bFound=true; if(Id==Target) break; continue; }
            if(!bFound) continue;
            if(const auto* N=Node(Course,Id))
            {
                Place(Runner,Course,FTransform(N->Transform.GetRotation(),N->Transform.TransformPosition(FVector(-80,0,100))));
                Place(Runner,Course,FTransform(N->Transform.GetRotation(),N->Transform.TransformPosition(FVector(80,0,100))));
            }
            if(Id==Target) break;
        }
    }
    bool Finish()
    {
        if(Dir.IsEmpty()) Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("V03Audit")/TEXT("FailedPrerequisites"));
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Dir);
        for(const FString& File:ShotFiles) Check(TEXT("viewport PNG exists: ")+FPaths::GetCleanFilename(File),IFileManager::Get().FileExists(*File),File);
        if(auto* G=Game.Get())
        {
            if(auto* GI=G->GetGameInstance<UBootGameInstance>()) GI->ApplyOptions(OriginalOptions,false);
            if(auto* PC=Cast<ABootPlayerController>(G->GetWorld()->GetFirstPlayerController())) PC->ConsoleCommand(TEXT("t.MaxFPS 60"),false);
        }
        const FString Result=FString::Printf(TEXT("{\"status\":\"%s\",\"failures\":%d,\"map\":%s,\"profile_slot\":\"BootloopAudit_v03\",\"scope\":\"Native rendered audio-enabled game; direct gameplay/menu API; staged teleports between sections; real movement/base ride after staging. No OS physical keys, full human route, listening quality or manual feel claim. No packaging.\",\"checks\":[\n%s\n],\"screenshots\":[\n%s\n],\"foot_contacts\":[\n%s\n]}"),Failures?TEXT("FAIL"):TEXT("PASS"),Failures,*JsonString(Map),*Checks,*Shots,*Contacts);
        bool bSaved=FFileHelper::SaveStringToFile(Result,*(Dir/TEXT("LiveAudit.json")));
        if(!ReportDir.IsEmpty()) bSaved=FFileHelper::SaveStringToFile(Result,*(ReportDir/TEXT("LiveAudit.json"))) && bSaved;
        if(!bSaved) { ++Failures; UE_LOG(LogTemp,Error,TEXT("BOOTLOOP V03 AUDIT: evidence JSON write failed: %s"),*Dir); }
        UE_LOG(LogTemp,Display,TEXT("BOOTLOOP_V03_AUDIT_%s map=%s failures=%d"),Failures?TEXT("FAIL"):TEXT("PASS"),*Map,Failures);
        // The opt-in audit is a bounded command-line Editor-game run and closes itself.
        FPlatformMisc::RequestExit(false); return false;
    }
    bool Tick()
    {
        auto* G=Game.Get(); if(!G) return false;
        if(FPlatformTime::Seconds()-Started>180.) { Check(TEXT("whole audit bounded by 180 seconds"),false); return Finish(); }
        UWorld* World=G->GetWorld();
        auto* PC=Cast<ABootPlayerController>(World->GetFirstPlayerController());
        auto* Runner=PC?Cast<ABootRunner>(PC->GetPawn()):nullptr;
        auto* GI=G->GetGameInstance<UBootGameInstance>();
        const double Age=FPlatformTime::Seconds()-Entered;
        if(!PC || !Runner || !GI)
        { if(Age>15.) { Check(TEXT("local controller runner and profile available"),false); return Finish(); } return true; }
        auto* Course=Cast<ABootCourseTrack>(G->GetTrack());
        if(!Course) { Check(TEXT("authored native course mode required"),false); return Finish(); }
        auto* Rhythm=Runner->GetRhythm(); auto* Move=Runner->GetParkour();
        if(Age>18.) { Check(FString::Printf(TEXT("stage %d timeout"),Stage),false); return Finish(); }
        switch(Stage)
        {
        case 0:
            if(Age<1.) break;
            if(Dir.IsEmpty())
            {
                Map=G->GetCourseId().ToString(); ReportDir=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("V03Audit")/Map);
                // A previous run's screenshot must never satisfy a new request.
                Dir=ReportDir/(FDateTime::UtcNow().ToString(TEXT("Run-%Y%m%dT%H%M%S-"))+LexToString(FPlatformTime::Cycles64()));
                FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Dir);
            }
            OriginalOptions=GI->GetOptions(); BeforeRuns=GI->GetProfile()->Runs; BeforeRecords=GI->GetProfile()->CourseRecords.Num(); BeforeLegacyScore=GI->GetProfile()->BestScore;
            if(!bContactBound) { Runner->GetAvatar()->OnFootContact.AddSP(AsShared(),&FBootV03Audit::FootContact); bContactBound=true; }
            Check(TEXT("native desktop and UMG shell"),G->GetRunState()==EBootRunState::Desktop && PC->GetPage()==EBootPage::Desktop && PC->GetShell()!=nullptr);
            Check(TEXT("desktop has no active music transport"),!Rhythm->IsReady()); if(!Capture(PC,Runner,TEXT("00_desktop"))) break; PC->ShowInputs(); Go(1); break;
        case 1:
            if(Age<.35) break;
            Check(TEXT("inputs page opens from desktop"),PC->GetPage()==EBootPage::Inputs); if(!Capture(PC,Runner,TEXT("01_inputs"))) break;
            PC->Back(); Check(TEXT("inputs Back returns to desktop"),PC->GetPage()==EBootPage::Desktop); PC->ShowSettings(); Go(2); break;
        case 2:
            if(Age<.35) break;
            Check(TEXT("settings page opens"),PC->GetPage()==EBootPage::Settings); if(!Capture(PC,Runner,TEXT("02_settings"))) break; PC->Back(); PC->StartRun(); Go(3); break;
        case 3:
            if(G->GetRunState()!=EBootRunState::Running || !Rhythm->IsReady()) break;
            Check(TEXT("native run and actual audio Quartz ready"),true);
            Check(TEXT("new run has zero operation score"),Rhythm->GetScore().Score==0); if(!Capture(PC,Runner,TEXT("03_spawn_forward"))) break;
            PC->TogglePause(); PC->ShowInputs(); Go(4); break;
        case 4:
            if(Age<.3) break;
            PausedTransport=Rhythm->GetTransportSeconds(); PausedPosition=Runner->GetActorLocation();
            Check(TEXT("pause inputs retain paused game"),PC->GetPage()==EBootPage::Inputs && G->GetRunState()==EBootRunState::Paused && World->IsPaused()); Go(5); break;
        case 5:
            if(Age<.65) break;
            Check(TEXT("pause freezes actual audio transport"),FMath::Abs(Rhythm->GetTransportSeconds()-PausedTransport)<.06);
            Check(TEXT("pause freezes character physics"),Runner->GetActorLocation().Equals(PausedPosition,.1));
            PC->Back(); Check(TEXT("inputs Back restores pause page"),PC->GetPage()==EBootPage::Pause); if(!Capture(PC,Runner,TEXT("04_pause"))) break; PC->TogglePause();
            Go(G->IsTraining()?100:10); break;
        case 10:
            if(Age<.25) break;
            for(TActorIterator<ABootSurfWindow> It(World);It;++It) if(It->GetWindowId()==1001) Ride=*It;
            Check(TEXT("authored starting surf window exists"),Ride.IsValid());
            if(!Ride.IsValid()) return Finish();
            Place(Runner,Course,FTransform(FRotator::ZeroRotator,Ride->GetActorLocation()+FVector(0,0,140)));
            PC->SetControlRotation(FRotator(-35,0,0)); Go(11); break;
        case 11:
            if(Age<.3) break;
            Check(TEXT("staged rider lands on actual window base"),Runner->GetMovementBaseObject()==Ride->GetRootComponent());
            Check(TEXT("SNAP chooses real based window"),Runner->FindSurfTarget()==Ride.Get()); BeforeFootCount=FootCount; Runner->RequestSnap(); Shot(PC,Runner,TEXT("05_board_snap")); Go(12); break;
        case 12:
            if(Age<1.8) break;
            Check(TEXT("standing ride adds no jog foot contacts"),FootCount==BeforeFootCount);
            Check(TEXT("actual moving base carried rider"),Ride->HasCarried(Runner)); if(!Capture(PC,Runner,TEXT("06_docked_body"))) break;
            Move->Velocity=FVector(900,0,0); Runner->RequestJump(); Go(13); break;
        case 13:
            DriveDirection=FVector::ForwardVector;
            if(Age<2.2) break;
            Check(TEXT("physical departure lands beyond named destination"),Course->GetCourseState().CurrentNodeId==11 && Move->IsMovingOnGround(),Runner->GetActorLocation().ToString());
            Check(TEXT("real completed ride awards operation score"),Rhythm->GetScore().Score>0);
            BeforeRhythmScore=Rhythm->GetScore().Score; if(!Capture(PC,Runner,TEXT("07_departure_landing"))) break;
            GateTo(Runner,Course,2); Go(14); break;
        case 14:
            if(Age<.3) break;
            if(!Capture(PC,Runner,TEXT("08_first_turn"))) break; GateTo(Runner,Course,4);
            if(const auto* N=Node(Course,4)) Place(Runner,Course,FTransform(N->Transform.GetRotation(),N->Transform.TransformPosition(FVector(350,-600,160))));
            for(TActorIterator<ABootScanProcess> It(World);It;++It) Scan=*It;
            Go(15); break;
        case 15:
            if(Age<.2) break;
            Check(TEXT("SCAN arms with readable warning before sweep"),Scan.IsValid() && Scan->GetPhase()==EBootScanPhase::Warning);
            if(!Capture(PC,Runner,TEXT("09_scan_warning"))) break;
            if(Scan.IsValid()) { PausedPhase=Scan->GetPhase(); PausedProcess=Scan->GetBeamLocation(); }
            PC->TogglePause(); Go(16); break;
        case 16:
            if(Age<.5) break;
            Check(TEXT("pause freezes active SCAN phase and geometry"),Scan.IsValid() && Scan->GetPhase()==PausedPhase && Scan->GetBeamLocation().Equals(PausedProcess,.1));
            PC->TogglePause(); Go(17); break;
        case 17:
            if(Age<2.7) break;
            Check(TEXT("side bypass survives active SCAN"),G->GetRunState()==EBootRunState::Running && !Runner->IsDead());
            GateTo(Runner,Course,6); if(!Capture(PC,Runner,TEXT("10_elevated_return"))) break; GateTo(Runner,Course,8);
            for(TActorIterator<ABootPopupProcess> It(World);It;++It) Popup=*It;
            if(!Popup.IsValid()) { Check(TEXT("authored POPUP exists"),false); return Finish(); }
            // Arm at a safe staging point. The next frame boards during its full warning.
            if(const auto* N=Node(Course,8)) Place(Runner,Course,FTransform(N->Transform.GetRotation(),N->Transform.TransformPosition(FVector(1500,-600,100))));
            Place(Runner,Course,FTransform(Popup->GetActorQuat(),Popup->GetActorLocation()+FVector(0,0,140))); Go(18); break;
        case 18:
            if(Age<.2) break;
            Check(TEXT("POPUP starts hostile with real rider base"),Popup->IsHostile() && Runner->GetMovementBaseObject()==Popup->GetRootComponent());
            Check(TEXT("SNAP selects POPUP interception"),Runner->FindSurfTarget()==Popup.Get()); Runner->RequestSnap();
            Check(TEXT("accepted interception atomically removes hostile state"),!Popup->IsHostile()); Shot(PC,Runner,TEXT("11_popup_interception")); Go(19); break;
        case 19:
            if(Age<1.8) break;
            Check(TEXT("intercepted POPUP physically carries rider"),Popup->HasCarried(Runner) && !Runner->IsDead());
            Check(TEXT("ride without departure earns no new points"),Rhythm->GetScore().Score==BeforeRhythmScore);
            Move->Velocity=Popup->GetActorForwardVector()*900; Runner->RequestJump(); Go(20); break;
        case 20:
            DriveDirection=Popup->GetActorForwardVector();
            if(Age<1.) break;
            Check(TEXT("POPUP departure lands on real solid platform"),Move->IsMovingOnGround() && !Popup->IsHostile(),Runner->GetActorLocation().ToString());
            Check(TEXT("departure before named gate does not prematurely score"),Rhythm->GetScore().Score==BeforeRhythmScore);
            if(!Capture(PC,Runner,TEXT("12_popup_departure"))) break;
            if(const auto* N=Node(Course,8))
                if(N->Transform.InverseTransformPosition(Runner->GetActorLocation()).X<9000 && Age<12.) break;
            Go(21); break;
        case 21:
            if(const auto* N=Node(Course,9)) DriveDirection=N->Transform.GetUnitAxis(EAxis::X);
            if(Age<1.1) break;
            Check(TEXT("physical named destination completes pending POPUP operation"),Course->GetCourseState().CurrentNodeId==9 && Move->IsMovingOnGround() && Rhythm->GetScore().Score>BeforeRhythmScore,Runner->GetActorLocation().ToString());
            if(const auto* N=Node(Course,5))
            {
                Place(Runner,Course,FTransform(N->Transform.GetRotation(),N->Transform.TransformPosition(FVector(1600,0,100))));
                PC->SetControlRotation(FRotator(0,N->Transform.Rotator().Yaw,0));
            }
            Go(22); break;
        case 22:
            if(Age<.3) break;
            Check(TEXT("forward wrist sample starts with authoritative ready charge"),Move->IsAirRedirectReady());
            if(!Capture(PC,Runner,TEXT("12a_wrist_ready_forward"))) break;
            if(const auto* N=Node(Course,5)) Move->Velocity=N->Transform.GetUnitAxis(EAxis::X)*900;
            Runner->RequestJump(); bWristRedirectRequested=false; Go(23); break;
        case 23:
            if(Age<.12) break;
            if(!bWristRedirectRequested)
            {
                const auto* N=Node(Course,5);
                Check(TEXT("forward wrist sample spends actual airborne redirect"),N && Move->TryAirRedirect(N->Transform.GetUnitAxis(EAxis::Y)));
                bWristRedirectRequested=true;
            }
            Check(TEXT("forward wrist spent state follows movement charge"),!Move->IsAirRedirectReady());
            if(!Capture(PC,Runner,TEXT("12b_wrist_spent_forward"))) break; Go(24); break;
        case 24:
            if(Age<.9) break;
            Check(TEXT("forward wrist recharges after actual ground contact"),Move->IsMovingOnGround() && Move->IsAirRedirectReady());
            if(!Capture(PC,Runner,TEXT("12c_wrist_recharged_forward"))) break; Go(30); break;
        case 30:
            if(const auto* N=Node(Course,5))
            {
                Place(Runner,Course,FTransform(N->Transform.GetRotation(),N->Transform.TransformPosition(FVector(1600,0,100))));
                PC->SetControlRotation(FRotator(-55,N->Transform.Rotator().Yaw,0));
            }
            { const int32 Fps[]={30,60,120}; PC->ConsoleCommand(FString::Printf(TEXT("t.MaxFPS %d"),Fps[FpsIndex]),false); }
            FrameIndex=0; bJumpRequested=false; bSawAir=false; bSawLanding=false; BeforeFootCount=FootCount; Go(31); break;
        case 31:
        {
            const auto* N=Node(Course,5); if(!N) return Finish();
            DriveDirection=N->Transform.GetUnitAxis(EAxis::X);
            if(Age>.55 && !bJumpRequested) { Runner->RequestJump(); bJumpRequested=true; }
            if(bJumpRequested && Move->IsFalling()) bSawAir=true;
            if(bSawAir && Move->IsMovingOnGround()) bSawLanding=true;
            const double Times[]={.25,.50,.70,1.0,1.35,1.75}; const int32 Fps[]={30,60,120};
            if(FrameIndex<6 && Age>=Times[FrameIndex])
            { Shot(PC,Runner,FString::Printf(TEXT("13_jog_%dfps_%02d"),Fps[FpsIndex],FrameIndex)); ++FrameIndex; }
            if(Age<1.95) break;
            Check(FString::Printf(TEXT("jog takeoff landing finite at target %d FPS"),Fps[FpsIndex]),!Runner->GetActorLocation().ContainsNaN() && !Move->Velocity.ContainsNaN() && !Runner->IsDead());
            Check(FString::Printf(TEXT("actual jog contacts takeoff and landing at target %d FPS"),Fps[FpsIndex]),FootCount>BeforeFootCount && bSawAir && bSawLanding);
            if(++FpsIndex<3) Go(30); else { PC->ConsoleCommand(TEXT("t.MaxFPS 60"),false); Runner->StartSlide(); Go(32); }
            break;
        }
        case 32:
            if(Age<.16) break;
            Check(TEXT("slide presentation uses actual slide state"),Runner->IsSliding()); if(!Capture(PC,Runner,TEXT("14_slide_body"))) break;
            if(const auto* N=Node(Course,5))
            {
                Place(Runner,Course,FTransform(N->Transform.GetRotation(),N->Transform.TransformPosition(FVector(1100,650,260))));
                Move->SetMovementMode(MOVE_Falling); Move->Velocity=N->Transform.GetUnitAxis(EAxis::X)*900;
                PC->SetControlRotation(FRotator(-55,N->Transform.Rotator().Yaw,0));
            }
            Go(33); break;
        case 33:
            if(Age<.16) break;
            Check(TEXT("wallrun body capture uses actual wallrun state"),Move->IsWallRunning()); if(!Capture(PC,Runner,TEXT("15_wallrun_body"))) break;
            { FBootOptions Options=OriginalOptions; Options.bBody=false; Options.bMotion=false; Options.EffectsIntensity=0; GI->ApplyOptions(Options,false); }
            Go(34); break;
        case 34:
            if(Age<.2) break;
            Check(TEXT("body off reduced motion effects zero preferences applied"),!GI->GetOptions().bBody && !GI->GetOptions().bMotion && GI->GetOptions().EffectsIntensity==0 && Runner->GetFeedback()->GetActiveParticleCount()==0);
            if(!Capture(PC,Runner,TEXT("16_body_off_reduced_motion"))) break; GI->ApplyOptions(OriginalOptions,false);
            BeforeGeneration=G->GetAttemptGeneration(); PC->StartRun(); Go(35); break;
        case 35:
            if(G->GetRunState()!=EBootRunState::Running || !Rhythm->IsReady()) break;
            Check(TEXT("full Retry clears route score and increments generation"),G->GetAttemptGeneration()>BeforeGeneration && Course->GetCourseSerial()==0 && Rhythm->GetScore().Score==0);
            Check(TEXT("full Retry restores authored start and ability"),Runner->GetActorLocation().Equals(Course->GetStartTransform().GetLocation(),10) && Move->IsAirRedirectReady());
            Check(TEXT("full Retry resets POPUP hostile state"),Popup.IsValid() && Popup->IsHostile());
            if(!Capture(PC,Runner,TEXT("16a_retry_wrist_ready"))) break;
            GateTo(Runner,Course,9);
            if(const auto* Last=Node(Course,9)) Place(Runner,Course,FTransform(Last->Transform.GetRotation(),Last->Transform.TransformPosition(FVector(Last->LengthCm-100,0,100)))); Go(36); break;
        case 36:
            if(Age<.3) break;
            if(!Capture(PC,Runner,TEXT("17_exit_vista"))) break;
            if(const auto* Last=Node(Course,9)) Place(Runner,Course,FTransform(Last->Transform.GetRotation(),Last->Transform.TransformPosition(FVector(Last->LengthCm+100,0,100)))); Go(37); break;
        case 37:
            if(Age<.35) break;
            Check(TEXT("ordered native final gate opens complete results"),G->GetRunState()==EBootRunState::Complete && PC->GetPage()==EBootPage::Results);
            Check(TEXT("Circuit record isolated from legacy score"),GI->GetProfile()->BestScore==BeforeLegacyScore && GI->GetProfile()->CourseRecords.Contains(TEXT("RooftopCircuitV03")));
            Check(TEXT("complete stops actual music transport"),!Rhythm->IsReady()); if(!Capture(PC,Runner,TEXT("18_results"))) break; PC->Desktop(); Go(200); break;
        case 100:
            if(Age<.25) break;
            if(!bRestorePending)
            {
                BeforeGeneration=G->GetAttemptGeneration(); Runner->Die(); bRestorePending=true; Go(100); break;
            }
            if(G->GetRunState()!=EBootRunState::Running) break;
            Check(FString::Printf(TEXT("tutorial local restore %02d generation transient charge"),RestoreCount+1),G->GetAttemptGeneration()>BeforeGeneration && Move->IsAirRedirectReady() && !Move->IsAirRedirecting() && !Runner->IsSliding() && !Runner->IsDead());
            Check(FString::Printf(TEXT("tutorial local restore %02d keeps Quartz transport alive"),RestoreCount+1),Rhythm->IsReady());
            bRestorePending=false;
            if(++RestoreCount<10) Go(100); else { Shot(PC,Runner,TEXT("05_tutorial_ten_restores")); Go(101); } break;
        case 101:
            if(Age<.3) break;
            if(Course->GetTutorial().CurrentLesson<8)
            {
                const int32 PreviousLesson=Course->GetTutorial().CurrentLesson;
                PC->SkipLesson(); const int32 Lesson=Course->GetTutorial().CurrentLesson; const auto* Expected=Node(Course,Lesson);
                if(Lesson<=PreviousLesson) { Check(TEXT("tutorial skip advances without stalling"),false); return Finish(); }
                Check(FString::Printf(TEXT("tutorial skip reaches authored checkpoint %d"),Lesson),Expected && Course->GetCourseState().CurrentNodeId==Lesson && Runner->GetActorLocation().Equals(Expected->CheckpointTransform.GetLocation(),2),
                    FString::Printf(TEXT("node=%d lesson=%d position=%s expected=%s"),Course->GetCourseState().CurrentNodeId,Lesson,*Runner->GetActorLocation().ToString(),Expected?*Expected->CheckpointTransform.GetLocation().ToString():TEXT("missing")));
                Go(101);
            }
            else
            {
                Check(TEXT("explicit tutorial skips remain distinguishable"),Course->GetTutorial().GetOutcome(3)==EBootLessonState::Skipped && Course->GetTutorial().GetOutcome(6)==EBootLessonState::Skipped);
                if(!Capture(PC,Runner,TEXT("06_tutorial_final_section"))) break;
                if(const auto* Last=Node(Course,8)) Place(Runner,Course,FTransform(Last->Transform.GetRotation(),Last->Transform.TransformPosition(FVector(Last->LengthCm-100,0,100)))); Go(102);
            }
            break;
        case 102:
            if(Age<.3) break;
            if(!Capture(PC,Runner,TEXT("07_tutorial_exit"))) break;
            if(const auto* Last=Node(Course,8)) Place(Runner,Course,FTransform(Last->Transform.GetRotation(),Last->Transform.TransformPosition(FVector(Last->LengthCm+100,0,100)))); Go(103); break;
        case 103:
            if(Age<.35) break;
            Check(TEXT("tutorial completion and outcomes persist"),G->GetRunState()==EBootRunState::Complete && GI->GetProfile()->bTutorialComplete && GI->GetProfile()->TutorialOutcomes.Num()==8);
            Check(TEXT("tutorial never changes challenge runs records legacy score"),GI->GetProfile()->Runs==BeforeRuns && GI->GetProfile()->CourseRecords.Num()==BeforeRecords && GI->GetProfile()->BestScore==BeforeLegacyScore);
            if(!Capture(PC,Runner,TEXT("08_tutorial_results"))) break; PC->Desktop(); Go(200); break;
        case 200:
            if(Age<.4) break;
            Check(TEXT("return to desktop clears particles and music"),PC->GetPage()==EBootPage::Desktop && !Rhythm->IsReady() && Runner->GetFeedback()->GetActiveParticleCount()==0);
            Check(TEXT("isolated audit profile saved without error"),!GI->HasSaveError()); return Finish();
        }
        return true;
    }
};
}
void StartBootV03Audit(ABootGameMode* Game)
{
    if(!Game || !FParse::Param(FCommandLine::Get(),TEXT("BootloopV03Audit")) || FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))) return;
    auto Audit=MakeShared<FBootV03Audit>(); Audit->Game=Game;
    FWorldDelegates::OnWorldPreActorTick.AddSP(Audit,&FBootV03Audit::Drive);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Audit](float){return Audit->Tick();}),.025f);
}
#endif
