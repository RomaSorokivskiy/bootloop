#include "Developer/BootV04Audit.h"
bool BootloopV04Audit::Validate(const FConstraints& V,bool Final,FString& Error)
{
    Error.Empty();
    if(!V.bFinite||!V.bAlive||!V.bSameRun||!FMath::IsFinite(V.WallSeconds)||!FMath::IsFinite(V.ActiveSeconds)||!FMath::IsFinite(V.TravelCm)) Error=TEXT("nonfinite, dead, or changed run");
    else if(V.LiveChunks>40||V.Effects>64) Error=TEXT("bounded chunk/effect budget exceeded");
    else if(Final&&(V.WallSeconds<1200.||V.ActiveSeconds<1199.||V.TravelCm<120000.||V.Tempos<3||V.Transitions<2)) Error=TEXT("continuous 20 minute moving soak / three tempos not established");
    return Error.IsEmpty();
}
#if !UE_BUILD_SHIPPING
#include "Run/BootGameMode.h"
#include "Player/BootRunner.h"
#include "UI/BootPlayerController.h"
#include "World/Generation/BootEndlessTrack.h"
#include "World/Generation/BootEndlessChunk.h"
#include "World/Interaction/BootSurfWindow.h"
#include "World/Threats/BootPopupProcess.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "World/Presentation/BootRunEffectsComponent.h"
#include "Player/BootFeedbackComponent.h"
#include "Music/BootMusicTransportComponent.h"
#include "Music/BootMusicLibrarySubsystem.h"
#include "Core/BootGameInstance.h"
#include "Tutorial/BootFirstRunTrack.h"
#include "Movement/BootMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "Player/BootAvatarComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "Materials/MaterialInterface.h"
#endif
namespace
{
struct FAudit : TSharedFromThis<FAudit>
{
    TWeakObjectPtr<ABootGameMode> Game;
    FString Scenario,Dir,Captures,Failure;
    FDelegateHandle DriveHandle;
    FTSTicker::FDelegateHandle TickHandle;
    BootloopV04Audit::FConstraints Limits;
    FBootRunToken Run;
    TArray<TSharedPtr<FJsonValue>> Checks,Shots;
    TSet<int32> Tempos;
    TSet<FString> PlayedHashes;
    double Started=FPlatformTime::Seconds(),PhaseAt=Started,RunAt=0.,LastSample=0.,LastProgressAt=0.,LastProgress=0.;
    double Travel=0.,MaxFrame=0.,PausedFrame=0.,LastCanonical=0.,CanonicalAt=0.;
    FVector Previous=FVector::ZeroVector,PausedPosition;
    double LastTutorialTrace=0.,LastTutorialSnap=-1.;int32 TutorialSnapAttempts=0;
    int32 Lesson=-1,Objective=-1,LessonPhase=0; double LessonAt=0.,ActionAt=0.,LastWheel=0.; FVector TutorialTarget;
    int32 Stage=0,Transitions=0,Underflows=0,FrameHitches=0,CardChoices=0,MaxActors=0,AvatarIndex=0;
    int64 LastEpoch=0,CurrentChunk=0,Retired=0,LastRetired=0;
    uint64 PeakMemory=0;
    TSet<int32> Actions;
    TSet<int32> CapturedFamilies;
    TWeakObjectPtr<UCameraComponent> StateLookCamera;FRotator SavedStateLookRotation;bool bStateLookActive=false,bSavedStateControlCamera=false;
    TSet<FString> CapturedMovementStates;double LastShotAt=0.,LastStateTrace=0.,WallCaptureAt=-1.;bool bLeftWallAudit=false;
    bool bDone=false,bDriving=false,bFirstPosition=true,bAvatarView=false; double AvatarFrameAt=0.;int32 AvatarFrame=0,FootContacts=0;
    FString Mp3Original,Mp3Input,Mp3OriginalDigest,Mp3Label,CorruptInput,CorruptLabel,Mp3Hash,CorruptHash,Mp3ManagedSource,Mp3ManagedPcm;double PreflightAt=0.;bool bPreflightStarted=false;
    FBootTransportSnapshot BeforeMuted;FBootRunToken BeforeMutedRun;
    FBootOptions OriginalOptions;
    bool bCameraFrozen=false,bSavedRunnerTick=false,bSavedMovementTick=false,bSavedControllerTick=false,bSavedControlCamera=false;
    TWeakObjectPtr<UCameraComponent> FrozenCamera;TWeakObjectPtr<ABootRunner> FrozenRunner;TWeakObjectPtr<APlayerController> FrozenPC;
    FTransform SavedCameraRelative,SavedCameraAdditive,FrozenActorTransform;FRotator SavedControlRotation;float SavedAdditiveFov=0.f,OriginalMaxFps=0.f;
    int64 MaxAudioRingBytes=0,AudioRenderBlocks=0,AudioOutputFrames=0,AudioUnderflowBlocks=0;int32 MaxLeaseSlots=0;
    int32 MaxScoreHistory=0,MaxProcessHistory=0,MaxGuardHistory=0,MaxSurfChunks=0,MaxSurfOperations=0;
    bool bCardPending=false,bCardHeld=false;FKey CardKey;double CardPressedAt=0.;FBootRunSnapshot BeforeCard;FBootTransportSnapshot BeforeCardAudio;FVector BeforeCardPosition;
    void SaveJson(const FString& File,const TSharedRef<FJsonObject>& Obj)
    {
        FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Obj,Writer);
        FFileHelper::SaveStringToFile(Text,*(Dir/File));
    }
    void Check(const FString& Name,bool Pass,const FString& Detail=FString())
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("check"),Name);J->SetBoolField(TEXT("pass"),Pass);J->SetStringField(TEXT("detail"),Detail);Checks.Add(MakeShared<FJsonValueObject>(J));
        UE_LOG(LogTemp,Display,TEXT("V04 AUDIT %s %s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),*Name,*Detail);
        if(!Pass&&Failure.IsEmpty())Failure=Name+TEXT(": ")+Detail;
    }
    void Shot(ABootPlayerController* PC,const FString& Label)
    {
        LastShotAt=FPlatformTime::Seconds();FString File=Captures/(Label+TEXT(".png"));PC->ConsoleCommand(FString::Printf(TEXT("Shot ShowUI -nosuffix filename=\"%s\""),*File),false);
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("file"),File);J->SetStringField(TEXT("label"),Label);J->SetNumberField(TEXT("wall_seconds"),FPlatformTime::Seconds()-Started);Shots.Add(MakeShared<FJsonValueObject>(J));
    }
    void Go(int32 Next){Stage=Next;PhaseAt=FPlatformTime::Seconds();}
    void RestoreStateLook()
    {
        if(!bStateLookActive)return;
        if(auto Camera=StateLookCamera.Get()){Camera->SetRelativeRotation(SavedStateLookRotation);Camera->bUsePawnControlRotation=bSavedStateControlCamera;}
        bStateLookActive=false;StateLookCamera.Reset();
    }
    void BeginStateLook(ABootRunner* R)
    {
        RestoreStateLook();if(auto Camera=R->FindComponentByClass<UCameraComponent>()){
            StateLookCamera=Camera;SavedStateLookRotation=Camera->GetRelativeRotation();bSavedStateControlCamera=Camera->bUsePawnControlRotation;
            bStateLookActive=true;Camera->bUsePawnControlRotation=false;Camera->SetRelativeRotation(FRotator(-65.,SavedStateLookRotation.Yaw,SavedStateLookRotation.Roll));
        }
    }
    void RestoreCamera()
    {
        if(!bCameraFrozen)return;
        if(auto Camera=FrozenCamera.Get()){Camera->SetRelativeTransform(SavedCameraRelative);Camera->bUsePawnControlRotation=bSavedControlCamera;Camera->ClearAdditiveOffset();Camera->AddAdditiveOffset(SavedCameraAdditive,SavedAdditiveFov);}
        if(auto R=FrozenRunner.Get()){R->SetActorTickEnabled(bSavedRunnerTick);R->GetCharacterMovement()->SetComponentTickEnabled(bSavedMovementTick);}
        if(auto PC=FrozenPC.Get()){PC->SetControlRotation(SavedControlRotation);PC->SetActorTickEnabled(bSavedControllerTick);if(PC->PlayerCameraManager)PC->PlayerCameraManager->UpdateCamera(1.f/60.f);}
        bCameraFrozen=false;
    }
    bool FreezeCamera(ABootRunner* R,APlayerController* PC)
    {
        auto Camera=R->FindComponentByClass<UCameraComponent>();if(!Camera){Check(TEXT("avatar_owned_camera"),false);return false;}
        FrozenCamera=Camera;FrozenRunner=R;FrozenPC=PC;SavedCameraRelative=Camera->GetRelativeTransform();SavedControlRotation=PC->GetControlRotation();FrozenActorTransform=R->GetActorTransform();
        Camera->GetAdditiveOffset(SavedCameraAdditive,SavedAdditiveFov);bSavedControlCamera=Camera->bUsePawnControlRotation;bSavedRunnerTick=R->IsActorTickEnabled();bSavedMovementTick=R->GetCharacterMovement()->IsComponentTickEnabled();bSavedControllerTick=PC->IsActorTickEnabled();
        bCameraFrozen=true;bDriving=false;R->SetActorTickEnabled(false);R->GetCharacterMovement()->SetComponentTickEnabled(false);PC->SetActorTickEnabled(false);Camera->bUsePawnControlRotation=false;Camera->ClearAdditiveOffset();
        Camera->SetRelativeLocationAndRotation(FVector(250,0,0),FRotator(0,180,0));
        if(PC->PlayerCameraManager)PC->PlayerCameraManager->UpdateCamera(1.f/60.f);return true;
    }
    void Finish()
    {
        if(bDone)return;bDone=true;bDriving=false;RestoreStateLook();RestoreCamera();if(auto Fps=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))Fps->Set(OriginalMaxFps,ECVF_SetByConsole);
        auto G=Game.Get();if(G)if(auto GI=Cast<UBootGameInstance>(G->GetGameInstance()))if(auto PC=G->GetWorld()->GetFirstPlayerController())if(auto R=Cast<ABootRunner>(PC->GetPawn()))R->ApplyPreferences(OriginalOptions);
        if(AudioRenderBlocks>0)Check(TEXT("bounded_native_audio_resources"),MaxAudioRingBytes<=3072000&&MaxLeaseSlots<=2);
        if(AudioRenderBlocks>0)Check(TEXT("bounded_native_history"),MaxScoreHistory<=320&&MaxProcessHistory<=320&&MaxGuardHistory<=32&&MaxSurfChunks<=40&&MaxSurfOperations<=1280);
        for(auto& S:Shots) { auto J=S->AsObject();const bool Exists=IFileManager::Get().FileSize(*J->GetStringField(TEXT("file")))>0;J->SetBoolField(TEXT("exists"),Exists);Check(TEXT("capture_")+J->GetStringField(TEXT("label")),Exists); }
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("scenario"),Scenario);J->SetBoolField(TEXT("full_task15_acceptance"),false);J->SetBoolField(TEXT("passed"),Failure.IsEmpty());J->SetStringField(TEXT("failure"),Failure);J->SetArrayField(TEXT("checks"),Checks);J->SetArrayField(TEXT("captures"),Shots);
        J->SetNumberField(TEXT("wall_seconds"),RunAt>0.?FPlatformTime::Seconds()-RunAt:0.);J->SetNumberField(TEXT("active_seconds"),Limits.ActiveSeconds);J->SetNumberField(TEXT("physical_travel_cm"),Travel);J->SetNumberField(TEXT("transitions"),Transitions);J->SetNumberField(TEXT("distinct_tempos"),Tempos.Num());J->SetNumberField(TEXT("distinct_played_tracks"),PlayedHashes.Num());J->SetNumberField(TEXT("underflow_samples"),Underflows);J->SetNumberField(TEXT("frame_hitches_over_100ms"),FrameHitches);J->SetNumberField(TEXT("max_frame_seconds"),MaxFrame);J->SetNumberField(TEXT("peak_process_bytes"),double(PeakMemory));J->SetNumberField(TEXT("max_actors"),MaxActors);J->SetNumberField(TEXT("retired_chunks"),double(Retired));J->SetNumberField(TEXT("last_retired_id"),double(LastRetired));J->SetNumberField(TEXT("simulated_enhanced_input_card_choices"),CardChoices);J->SetNumberField(TEXT("foot_contacts"),FootContacts);J->SetNumberField(TEXT("max_audio_ring_bytes"),double(MaxAudioRingBytes));J->SetNumberField(TEXT("max_audio_lease_slots"),MaxLeaseSlots);J->SetNumberField(TEXT("audio_render_blocks"),double(AudioRenderBlocks));J->SetNumberField(TEXT("audio_output_frames"),double(AudioOutputFrames));J->SetNumberField(TEXT("audio_underflow_blocks"),double(AudioUnderflowBlocks));J->SetBoolField(TEXT("hardware_latency_measured"),false);TArray<TSharedPtr<FJsonValue>> MovementStates;for(const FString& State:CapturedMovementStates)MovementStates.Add(MakeShared<FJsonValueString>(State));J->SetArrayField(TEXT("captured_movement_states"),MovementStates);J->SetBoolField(TEXT("reverse_wall_driver"),bLeftWallAudit);
        J->SetStringField(TEXT("evidence_scope"),TEXT("Native system-input route; AddMovementInput, RequestJump, StartSlide, RequestSnap. Cards use remapped PlayerController simulated InputKey press/release through EnhancedInput; not OS physical keyboard. No human playthrough/listening claim. No teleport substitutes for measured maneuvers; FirstRun has normal lesson checkpoint relocations."));
        J->SetNumberField(TEXT("max_score_operation_history"),MaxScoreHistory);J->SetNumberField(TEXT("max_registered_process_history"),MaxProcessHistory);J->SetNumberField(TEXT("max_guard_contact_history"),MaxGuardHistory);J->SetNumberField(TEXT("max_surf_history_chunks"),MaxSurfChunks);J->SetNumberField(TEXT("max_surf_history_operations"),MaxSurfOperations);
        J->SetStringField(TEXT("unobserved"),TEXT("hardware audible sync; human music quality; OS physical card keys; all maximum builds; pinned mutation acceptance"));J->SetBoolField(TEXT("packaging_performed"),false);SaveJson(TEXT("checks.json"),J);
        FPlatformMisc::RequestExitWithStatus(false,Failure.IsEmpty()?0:1);
    }
    static FString FileDigest(const FString& Path)
    {
        TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Path))return FString();uint8 Digest[20];FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Digest);return BytesToHex(Digest,20);
    }
    void BeginImportPreflight(ABootGameMode* G)
    {
        auto Library=G->GetGameInstance()->GetSubsystem<UBootMusicLibrarySubsystem>();
        Mp3Original=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Source/BOOTLOOP/Private/Tests/Fixtures/Music/system-encoder.mp3"));Mp3OriginalDigest=FileDigest(Mp3Original);
        const FString Id=FGuid::NewGuid().ToString(EGuidFormats::Digits);Mp3Label=TEXT("audit-mp3-")+Id;CorruptLabel=TEXT("audit-corrupt-")+Id;Mp3Input=Dir/(Mp3Label+TEXT(".mp3"));CorruptInput=Dir/(CorruptLabel+TEXT(".wav"));
        const uint8 Corrupt[]={ 'R','I','F','F',0xff,0xff,0xff,0x7f,'W','A','V','E','b','a','d','!' };
        TArray<uint8> Bytes;Bytes.Append(Corrupt,UE_ARRAY_COUNT(Corrupt));
        Check(TEXT("native_import_fixture_setup"),Library&&!Mp3OriginalDigest.IsEmpty()&&IFileManager::Get().Copy(*Mp3Input,*Mp3Original,true,true)==COPY_OK&&FFileHelper::SaveArrayToFile(Bytes,*CorruptInput));
        if(!Failure.IsEmpty()){Go(90);return;}
        Check(TEXT("native_import_jobs_queued"),Library->ImportFiles({Mp3Input,CorruptInput})!=0);if(!Failure.IsEmpty()){Go(90);return;}bPreflightStarted=true;PreflightAt=FPlatformTime::Seconds();Go(22);
    }
    void ImportPreflight(ABootGameMode* G,ABootRunner* R,ABootPlayerController* PC,double Now,double Age)
    {
        auto Library=G->GetGameInstance()->GetSubsystem<UBootMusicLibrarySubsystem>();auto Transport=R->GetMusicTransport();
        if(!Library||Now-PreflightAt>90.){Check(TEXT("native_import_preview_timeout"),false);Transport->StopPreview();Go(90);return;}
        if(Stage==22){
            const auto Entries=Library->Snapshot();const auto Mp3=Entries.FindByPredicate([&](const FBootLibraryEntry& E){return E.DisplayName==Mp3Label;});const auto Bad=Entries.FindByPredicate([&](const FBootLibraryEntry& E){return E.DisplayName==CorruptLabel;});
            if(!Mp3||!Bad)return;
            if(Mp3->State==EBootLibraryState::Failed){Check(TEXT("native_mp3_decode"),false,Mp3->Error);Go(90);return;}
            if(Mp3->State==EBootLibraryState::NeedsReview){FText Error;Check(TEXT("native_safe_grid_choice"),Library->AcceptSafeGrid(Mp3->Hash,Error),TEXT("Explicit runtime safe-grid option via API: ")+Error.ToString());if(!Failure.IsEmpty())Go(90);return;}
            if(Mp3->State!=EBootLibraryState::Ready||Bad->State!=EBootLibraryState::Failed)return;
            Mp3Hash=Mp3->Hash;CorruptHash=Bad->Hash;Mp3ManagedSource=Mp3->SourcePath;Mp3ManagedPcm=Mp3->PcmPath;
            Check(TEXT("native_mp3_ready"),Mp3->Frames>10000&&Mp3->Channels>0&&Mp3->Analysis.IsValid());Check(TEXT("native_corrupt_riff_failed"),!Bad->Error.IsEmpty(),Bad->Error);
            Check(TEXT("native_mp3_excluded_from_run_queue"),Library->SetIncluded(Mp3Hash,false));FText Error;const bool Playing=Transport->StartPreview(Mp3Hash,Error);Check(TEXT("native_mp3_preview_started"),Playing,Error.ToString());
            // Match production UI: transport resets its synth gain in StartPreview, then apply saved gain.
            if(auto GI=Cast<UBootGameInstance>(G->GetGameInstance()))Transport->SetVolumeMultiplier(GI->GetOptions().Music);
            if(!Playing){Go(90);return;}Go(23);
        }else if(Stage==23){const auto A=Transport->Snapshot();const auto D=Transport->Diagnostics();
            if(A.bValid&&A.TrackHash==Mp3Hash&&A.RenderedSourceFrame>10000){Check(TEXT("native_mp3_actual_rendered_preview"),D.RenderedOutputFrames>0,FString::Printf(TEXT("source_frames=%lld output_frames=%lld underflow_blocks=%lld; device audibility/listening not established"),A.RenderedSourceFrame,D.RenderedOutputFrames,D.UnderflowBlocks));Shot(PC,TEXT("native_mp3_preview_library"));Go(24);}
        }else if(Stage==24&&Age>.4){
            Transport->StopPreview();FText Error;Check(TEXT("native_mp3_managed_remove"),Library->Remove(Mp3Hash,Error),Error.ToString());Error=FText::GetEmpty();Check(TEXT("native_failed_entry_remove"),Library->Remove(CorruptHash,Error),Error.ToString());Go(25);
        }else if(Stage==25&&Age>.5){
            Check(TEXT("native_original_mp3_unchanged"),FileDigest(Mp3Original)==Mp3OriginalDigest&&!Mp3OriginalDigest.IsEmpty());
            Check(TEXT("native_managed_copies_removed"),!IFileManager::Get().FileExists(*Mp3ManagedSource)&&!IFileManager::Get().FileExists(*Mp3ManagedPcm));
            // Delete only these two generated inputs beneath this audit's fresh directory. Never the repository fixture.
            Check(TEXT("native_audit_inputs_cleanup"),FPaths::IsUnderDirectory(Mp3Input,Dir)&&FPaths::IsUnderDirectory(CorruptInput,Dir)&&IFileManager::Get().Delete(*Mp3Input,false,true)&&IFileManager::Get().Delete(*CorruptInput,false,true));
            auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("original_fixture"),Mp3Original);J->SetStringField(TEXT("original_sha1_before"),Mp3OriginalDigest);J->SetStringField(TEXT("original_sha1_after"),FileDigest(Mp3Original));J->SetStringField(TEXT("scope"),TEXT("Real native library import, decode, safe-grid choice, synth preview, exclude, managed removal via runtime APIs; no OS file-picker or human listening claim."));SaveJson(TEXT("import_preview.json"),J);
            Go(Failure.IsEmpty()?21:90);
        }
    }
    void PressCard(ABootGameMode* G,ABootRunner* R,APlayerController* PC,int32 Index)
    {
        if(bCardPending)return;
        BeforeCard=G->GetRunSnapshot();if(!BeforeCard.CardOffer.IsSet())return;
        auto GI=Cast<UBootGameInstance>(G->GetGameInstance());if(!GI)return;
        CardKey=GI->GetInputPreferences().GetKey(static_cast<EBootInputAction>(int32(EBootInputAction::CardOne)+Index),0);
        if(!CardKey.IsValid())CardKey=GI->GetInputPreferences().GetKey(static_cast<EBootInputAction>(int32(EBootInputAction::CardOne)+Index),1);
        if(!CardKey.IsValid()){Check(TEXT("card_binding_available"),false);Go(90);return;}
        BeforeCardAudio=R->GetMusicTransport()->Snapshot();BeforeCardPosition=R->GetActorLocation();CardPressedAt=FPlatformTime::Seconds();bCardPending=true;bCardHeld=true;
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(CardKey,IE_Pressed,1.f));
    }
    void ObserveCard(ABootGameMode* G,ABootRunner* R,APlayerController* PC,double Now)
    {
        if(!bCardPending)return;
        if(bCardHeld&&Now-CardPressedAt>=.05){PC->InputKey(FInputKeyEventArgs::CreateSimulated(CardKey,IE_Released,0.f));bCardHeld=false;}
        if(Now-CardPressedAt<.2)return;
        const auto After=G->GetRunSnapshot();const auto Audio=R->GetMusicTransport()->Snapshot();
        const bool Accepted=After.Run==BeforeCard.Run&&!After.CardOffer.IsSet()
            &&(G->IsFirstRun()?(After.PendingEnvelope.Revision>BeforeCard.Envelope.Revision||After.Envelope.Revision>BeforeCard.Envelope.Revision):After.RewardRevision==BeforeCard.RewardRevision+1);
        const bool Moving=FVector::Dist2D(BeforeCardPosition,R->GetActorLocation())>1.;
        const bool AudioAdvancing=!G->IsMusicRun()||(BeforeCardAudio.bValid&&Audio.bValid&&(Audio.TrackEpoch>BeforeCardAudio.TrackEpoch||Audio.RenderedSourceFrame>BeforeCardAudio.RenderedSourceFrame));
        Check(FString::Printf(TEXT("simulated_enhanced_input_card_%d"),CardChoices+1),Accepted&&Moving&&AudioAdvancing&&G->GetRunState()==EBootRunState::Running,
            FString::Printf(TEXT("key=%s accepted=%d moving=%d audio_checked=%d audio_advancing=%d; PlayerController InputKey press/release; not OS or human input"),*CardKey.ToString(),Accepted,Moving,G->IsMusicRun()?1:0,G->IsMusicRun()?(AudioAdvancing?1:0):-1));
        bCardPending=false;if(Accepted)++CardChoices;if(!Failure.IsEmpty()){bDriving=false;Go(90);}
    }
    void OnFoot(int32 Foot,FVector Position)
    {
        ++FootContacts;
        if(Scenario!=TEXT("Avatar")&&Scenario!=TEXT("FirstRun"))return;
        FString Row=FString::Printf(TEXT("%.6f,%d,%d,%.3f,%.3f,%.3f\n"),FPlatformTime::Seconds()-RunAt,Scenario==TEXT("FirstRun")?Lesson:AvatarIndex,Foot,Position.X,Position.Y,Position.Z);
        FFileHelper::SaveStringToFile(Row,*(Dir/TEXT("foot_contacts.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    }
    void TraceTutorial(ABootFirstRunTrack* First,ABootRunner* R,double Now)
    {
        if(First->GetTutorial().GetLesson()!=6||Now-LastTutorialTrace<.05)return;LastTutorialTrace=Now;
        const auto& Frame=First->GetSections()[6].Frame;const FVector P=Frame.InverseTransformPosition(R->GetActorLocation()),V=Frame.InverseTransformVectorNoScale(R->GetVelocity());
        ABootPopupProcess* Popup=nullptr;for(TActorIterator<ABootPopupProcess> It(R->GetWorld());It;++It)if(It->GetWindowId()==46001){Popup=*It;break;}
        const FVector Center=Popup?Frame.InverseTransformPosition(Popup->GetActorLocation()):FVector::ZeroVector;
        const auto Base=R->GetMovementBase();FString BaseName=Base&&Base->GetOwner()?Base->GetOwner()->GetName():TEXT("none");BaseName.ReplaceInline(TEXT(","),TEXT("_"));
        const FString Row=FString::Printf(TEXT("%.6f,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%s,%d,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%.6f,%d,%.3f,%.3f,%.3f,%.3f,%.3f\n"),
            Now-RunAt,First->GetTutorial().GetObjectiveIndex(),First->GetTutorial().GetSection(),P.X,P.Y,P.Z,V.X,V.Y,V.Z,R->GetCharacterMovement()->IsMovingOnGround()?1:0,*BaseName,Popup?1:0,
            Center.X,Center.Y,Center.Z,Popup&&Popup->IsAvailable()?1:0,Popup&&Popup->IsMoving()?1:0,Popup&&Popup->IsHostile()?1:0,Popup&&Popup->HasCarried(R)?1:0,Popup&&Popup->WasRideBlocked()?1:0,Popup&&Base==Popup->GetRootComponent()?1:0,
            LastTutorialSnap<0.?-1.:LastTutorialSnap-RunAt,TutorialSnapAttempts,R->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),R->GetCapsuleComponent()->GetScaledCapsuleRadius(),TutorialTarget.X,TutorialTarget.Y,TutorialTarget.Z);
        FFileHelper::SaveStringToFile(Row,*(Dir/TEXT("tutorial_trace.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    }
    void ObserveMovementState(ABootFirstRunTrack* First,ABootRunner* R,APlayerController* PC,double Now)
    {
        auto M=R->GetParkour();auto Base=R->GetMovementBase();auto Ride=Base?Cast<ABootSurfWindow>(Base->GetOwner()):nullptr;
        const FVector Velocity=R->GetVelocity();const double Speed=Velocity.Size2D();const auto& E=M->GetTraversalEnvelope();
        FString State;
        if(M->IsWallRunning())State=M->GetWallSide()<0.f?TEXT("wallrun_left"):TEXT("wallrun_right");
        else if(Ride&&Ride->IsMoving()&&Ride->HasCarried(R))State=TEXT("moving_base_ride");
        else if(R->IsSliding()&&M->IsMovingOnGround()&&Speed>300.)State=TEXT("ground_slide");
        else if(M->IsFalling()&&Velocity.Z>100.)State=TEXT("jump_ascent");
        else if(M->IsFalling()&&Velocity.Z<-100.)State=TEXT("jump_descent");
        else if(M->IsMovingOnGround()&&!R->bIsCrouched&&Speed>E.RunCmps+50.)State=TEXT("sprint_grounded_above_run");
        else if(M->IsMovingOnGround()&&!R->bIsCrouched&&Speed>500.)State=TEXT("run_grounded");
        if(!State.IsEmpty()&&!CapturedMovementStates.Contains(State)&&Now-LastShotAt>.3)
        {
            if(auto B=Cast<ABootPlayerController>(PC)){
                BeginStateLook(R);const int32 Fps=FMath::RoundToInt(OriginalMaxFps);Shot(B,FString::Printf(TEXT("native_state_%dfps_%s_body_lookdown"),Fps,*State));CapturedMovementStates.Add(State);
                if(M->IsWallRunning())WallCaptureAt=Now;
                UE_LOG(LogTemp,Display,TEXT("V04 AUDIT state capture %s lesson=%d speed=%.2f wall_side=%.1f frame_dt=%.6f"),*State,First->GetTutorial().GetLesson(),Speed,M->GetWallSide(),R->GetWorld()->GetDeltaSeconds());
            }
        }
        if(!bStateLookActive&&Now-LastStateTrace<.05)return;LastStateTrace=Now;
        FString BaseName=Base&&Base->GetOwner()?Base->GetOwner()->GetName():TEXT("none");BaseName.ReplaceInline(TEXT(","),TEXT("_"));
        const FString Row=FString::Printf(TEXT("%.6f,%d,%d,%.6f,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%.1f,%s,%d,%d,%.1f\n"),Now-RunAt,First->GetTutorial().GetLesson(),First->GetTutorial().GetSection(),R->GetWorld()->GetDeltaSeconds(),*State,Speed,double(E.RunCmps),Velocity.X,Velocity.Y,Velocity.Z,M->IsMovingOnGround()?1:0,R->IsSliding()?1:0,M->IsWallRunning()?1:0,double(M->GetWallSide()),*BaseName,Ride&&Ride->IsMoving()?1:0,Ride&&Ride->HasCarried(R)?1:0,bStateLookActive?-65.:double(PC->GetControlRotation().Pitch));
        FFileHelper::SaveStringToFile(Row,*(Dir/TEXT("movement_states.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    }
    void TutorialDrive(ABootFirstRunTrack* First,ABootRunner* R,APlayerController* PC)
    {
        const auto& Tutorial=First->GetTutorial();const int32 L=Tutorial.GetLesson(),O=Tutorial.GetObjectiveIndex();
        if(Tutorial.IsComplete()||!First->GetSections().IsValidIndex(L)||!First->IsInActiveLesson(R))return;
        const double Now=FPlatformTime::Seconds();const auto& Section=First->GetSections()[L];
        FVector P=Section.Frame.InverseTransformPosition(R->GetActorLocation());auto M=R->GetParkour();TraceTutorial(First,R,Now);ObserveMovementState(First,R,PC,Now);
        if(Lesson!=L){if(Lesson>=0)Check(FString::Printf(TEXT("physical_lesson_%d"),Lesson),Tutorial.GetOutcome(Lesson)==EBootFirstRunOutcome::Completed);Lesson=L;Objective=O;LessonPhase=0;LessonAt=Now;ActionAt=Now;Actions.Empty();if(auto B=Cast<ABootPlayerController>(PC))Shot(B,FString::Printf(TEXT("lesson_%d_native_start"),L));}
        if(Objective!=O){Objective=O;LessonPhase=0;ActionAt=Now;Actions.Empty();}
        if(Now-LessonAt>45.){Check(TEXT("tutorial_technique_timeout"),false,FString::Printf(TEXT("lesson=%d objective=%d phase=%d local=%s"),L,O,LessonPhase,*P.ToString()));Go(90);bDriving=false;return;}
        FVector Target(4600,0,0);
        auto Once=[&](int32 Id){if(Actions.Contains(Id))return false;Actions.Add(Id);return true;};
        auto Jump=[&](int32 Id,double X){if(P.X>=X&&M->IsMovingOnGround()&&Once(Id)){R->RequestJump();ActionAt=Now;}};
        switch(L)
        {
        case 0:Jump(1,1380.);break;
        case 1:
            Jump(1,700.);
            // Feed a real wheel-axis binding through PlayerController/EnhancedInput on descent.
            // This is simulated device input, explicitly distinct from a human wheel action.
            if(M->IsFalling()&&R->GetVelocity().Z<0.&&P.Z<200.&&Now-LastWheel>.045){PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseWheelAxis,IE_Axis,-1.f));LastWheel=Now;}
            break;
        case 2:
            // Same authored section, walk around the beam's side to isolate ground/air slide actions.
            Target.Y=-650.;
            if(O==0&&P.X>850.&&Once(1))R->StartSlide();
            if(O==1){Jump(2,2100.);if(M->IsFalling()&&Once(3))R->StartSlide();}
            break;
        case 3:
            // Reach the wall lane before jumping; a long diagonal to the exit leaves the first jump metres away from the wall.
                        if(bLeftWallAudit){
                // Walk to the far end and reverse along the same authored wall: its actual normal
                // is then on the runner's left. No actor, collision, position or velocity edits.
                if(O==0&&LessonPhase==0){Target=P.Y<650.?FVector(1100.,680.,0.):FVector(3100.,660.,0.);if(P.X>=3000.)LessonPhase=1;}
                else {Target=FVector(1000.,660.,0.);if(M->IsMovingOnGround()&&R->GetVelocity().X<-500.&&Once(1))R->RequestJump();}
            }else{Target=P.Y<650.&&!Actions.Contains(1)?FVector(1100.,680.,0.):FVector(4600.,660.,0.);Jump(1,1200.);}
            if(M->IsWallRunning()&&O==1&&WallCaptureAt>=0.&&Now-WallCaptureAt>.2&&Once(2)){R->RequestJump();Target.Y=0.;}
            if(Actions.Contains(2))Target.Y=0.;
            break;
        case 4:
                        Jump(1,1360.);
            if(M->IsFalling()&&P.Z>150.&&LessonPhase==0){LessonPhase=1;ActionAt=Now;}
            if(LessonPhase==1){
                // Runner reads last frame's consumed movement input. Establish a changed heading
                // through ordinary input before pressing ALT-TAB; forward-only is a valid no-op.
                Target=P+FVector(1000.,450.,0.);
                if(Now-ActionAt>=.05){R->RequestAirRedirect();LessonPhase=2;}
            }
            break;
        case 5:
        case 6:
        {
            if(L==6&&O==0){Target=FVector(P.X<1000.?1100.:2000.,-650.,0.);if(P.X>1000.&&Now-LessonAt<3.)Target=FVector(1100.,-650.,0.);break;}
            ABootSurfWindow* Ride=nullptr;const int32 Id=L==5?45001:46001;
            for(TActorIterator<ABootSurfWindow> It(R->GetWorld());It;++It)if(It->GetWindowId()==Id){Ride=*It;break;}
            if(!Ride)break;
            FVector Center=Section.Frame.InverseTransformPosition(Ride->GetActorLocation());
            Target=Center;
            const bool Aboard=R->GetMovementBaseObject()==Ride->GetRootComponent();
            // Jump toward the hostile popup, then intercept in flight before its panel touches the capsule.
            if(L==6)Jump(10,2450.);
            if((L==5&&Aboard)||(L==6&&M->IsFalling()&&P.X>2500.)){if(L==6){LastTutorialSnap=Now;++TutorialSnapAttempts;}R->RequestSnap();}
            if(!Ride->IsAvailable()&&!Ride->IsMoving()){Target=FVector(L==5?3600.:4800.,300.,160.);}
            break;
        }
        case 7:
            if(O==0&&P.X>700.&&Once(1))if(auto G=Game.Get())PressCard(G,R,PC,0);
            if(O==1){Jump(2,1100.);if(M->IsFalling()&&Once(3))R->RequestBufferDash();}
            if(O==2){Target=FVector(3000.,0.,0.);}
            break;
        case 8:
            if(P.X>750.&&Once(1))R->RequestSnap();break;
        default:break;
        }
        TutorialTarget=Target;FVector Aim=Section.Frame.TransformPosition(Target)-R->GetActorLocation();Aim.Z=0.;
        if(Aim.Size2D()>40.){const FVector Direction=Aim.GetSafeNormal();PC->SetControlRotation(Direction.Rotation());R->AddMovementInput(Direction,1.f,true);}
    }
    void Drive(UWorld* W,ELevelTick,float Dt)
    {
        auto G=Game.Get();if(!G||W!=G->GetWorld())return;RestoreStateLook();if(!bDriving||W->IsPaused()||G->GetRunState()!=EBootRunState::Running)return;
        auto PC=W->GetFirstPlayerController();auto R=PC?Cast<ABootRunner>(PC->GetPawn()):nullptr;auto T=Cast<ABootEndlessTrack>(G->GetTrack());if(!R)return;if(auto First=Cast<ABootFirstRunTrack>(G->GetTrack())){TutorialDrive(First,R,PC);return;}if(!T)return;
        MaxFrame=FMath::Max(MaxFrame,double(Dt));if(Dt>.1f)++FrameHitches;
        const FVector P=R->GetActorLocation();if(!bFirstPosition)Travel+=FVector::Dist(P,Previous);Previous=P;bFirstPosition=false;
        const FBootCommittedChunk* D=nullptr;
        for(auto C:T->GetLiveChunks())if(C&&C->GetDescriptor().CanonicalEndMetres>T->GetProgressMetres()+.01){D=&C->GetDescriptor();break;}
        if(!D)return;
        if(CurrentChunk!=D->InstanceId){CurrentChunk=D->InstanceId;Actions.Empty();}
        FVector Local=D->WorldTransform.InverseTransformPosition(P);double Best=TNumericLimits<double>::Max();int32 Segment=0;double Along=0.;
        if(Scenario!=TEXT("Avatar")&&Local.X>1200.)
        {
            static const auto CaptureCatalog=FBootChunkCatalog::Build();
            const auto* Definition=CaptureCatalog.FindByPredicate([&](const auto& C){return C.Id==D->DefinitionId;});
            if(Definition&&!CapturedFamilies.Contains(int32(Definition->Family)))if(auto BootPC=Cast<ABootPlayerController>(PC))
            {CapturedFamilies.Add(int32(Definition->Family));Shot(BootPC,FString::Printf(TEXT("route_family_%d_%s"),int32(Definition->Family),*D->DefinitionId.ToString()));}
        }
        const auto& Path=D->Recipe.Baseline;
        for(int32 I=1;I<Path.Num();++I){FVector V=Path[I]-Path[I-1];V.Z=0.;FVector Q=Local-Path[I-1];Q.Z=0.;double A=FMath::Clamp(FVector::DotProduct(Q,V)/FMath::Max(V.SizeSquared(),1.),0.,1.);double Dist=(Q-A*V).SizeSquared();if(Dist<Best){Best=Dist;Segment=I;Along=A;}}
        if(Segment==0)return;
        const double Speed=R->GetVelocity().Size2D();double Remaining=FMath::Clamp(Speed*.15,100.,230.);FVector Target=FMath::Lerp(Path[Segment-1],Path[Segment],Along);
        for(int32 I=Segment;I<Path.Num();++I){const double Len=FVector::Dist2D(Target,Path[I]);if(Len>=Remaining){Target+= (Path[I]-Target).GetSafeNormal2D()*Remaining;break;}Target=Path[I];Remaining-=Len;}
        // Once at the connector, continue into the next actor instead of aiming behind the capsule.
        if(FVector::Dist2D(Local,Path.Last())<120.)Target=Path.Last()+D->WorldTransform.InverseTransformVectorNoScale(D->WorldExit.GetRotation().GetForwardVector())*300.;
        for(int32 I=0;I<D->Recipe.BaselineMoves.Num();++I){const auto& M=D->Recipe.BaselineMoves[I];
            if((M.Kind==EBootBaselineMove::Jump||M.Kind==EBootBaselineMove::Slide)&&!Actions.Contains(I)&&Local.X>=M.Start.X&&Local.X<M.End.X){if(M.Kind==EBootBaselineMove::Jump)R->RequestJump();else R->StartSlide();Actions.Add(I);}
            if(M.Kind==EBootBaselineMove::Surf&&Local.X>1850.&&Local.X<4500.){
                ABootEndlessChunk* Owner=nullptr;for(auto C:T->GetLiveChunks())if(C&&C->GetDescriptor().InstanceId==D->InstanceId){Owner=C;break;}
                if(Owner&&!Owner->GetWindows().IsEmpty()){auto Ride=Owner->GetWindows()[0];FVector Center=D->WorldTransform.InverseTransformPosition(Ride->GetActorLocation());
                    if(Ride->IsAvailable()&&Local.X>=2050.)R->RequestSnap();
                    if(Ride->IsAvailable()||Ride->IsMoving())Target=Center;
                    else if(Local.X<4300.){Target=M.End+FVector(600.,0.,0.);}
                }
            }
        }
        FVector Aim=D->WorldTransform.TransformPosition(Target)-P;Aim.Z=0.;
        if(Aim.Size2D()>45.){FVector Direction=Aim.GetSafeNormal();PC->SetControlRotation(Direction.Rotation());R->AddMovementInput(Direction,1.f,true);}
    }
    void Sample(ABootGameMode* G,ABootRunner* R,ABootEndlessTrack* T,double Now)
    {
        const int32 ScoreHistory=G->GetMusicRun()?G->GetMusicRun()->GetTrackedScoreOperationCount():0;
        const int32 ProcessHistory=G->GetRegisteredProcessCount();
        const auto Ability=R->GetAbilities();
        const int32 GuardHistory=Ability?Ability->GetProtectedContactCount():0,SurfChunks=Ability?Ability->GetSurfHistoryChunkCount():0,SurfOperations=Ability?Ability->GetSurfHistoryOperationCount():0;
        MaxScoreHistory=FMath::Max(MaxScoreHistory,ScoreHistory);MaxProcessHistory=FMath::Max(MaxProcessHistory,ProcessHistory);MaxGuardHistory=FMath::Max(MaxGuardHistory,GuardHistory);MaxSurfChunks=FMath::Max(MaxSurfChunks,SurfChunks);MaxSurfOperations=FMath::Max(MaxSurfOperations,SurfOperations);
        if(ScoreHistory>320||ProcessHistory>320||GuardHistory>32||SurfChunks>40||SurfOperations>1280){Check(TEXT("bounded_native_history"),false,FString::Printf(TEXT("score=%d process=%d guard=%d surf_chunks=%d surf_operations=%d"),ScoreHistory,ProcessHistory,GuardHistory,SurfChunks,SurfOperations));Finish();return;}
        const auto S=G->GetRunSnapshot();const auto A=R->GetMusicTransport()->Snapshot();const auto Audio=R->GetMusicTransport()->Diagnostics();
        MaxAudioRingBytes=FMath::Max(MaxAudioRingBytes,Audio.TotalRingBytes);MaxLeaseSlots=FMath::Max(MaxLeaseSlots,Audio.LeaseSlots);AudioRenderBlocks=Audio.RenderBlocks;AudioOutputFrames=Audio.RenderedOutputFrames;AudioUnderflowBlocks=Audio.UnderflowBlocks;
        if(Audio.TotalRingBytes>3072000||Audio.LeaseSlots>2){Check(TEXT("bounded_native_audio_resources"),false,FString::Printf(TEXT("ring_bytes=%lld lease_slots=%d"),Audio.TotalRingBytes,Audio.LeaseSlots));Finish();return;}
        Limits.WallSeconds=Now-RunAt;Limits.ActiveSeconds=S.ActiveSeconds;Limits.TravelCm=Travel;Limits.LiveChunks=T?T->GetLiveChunkCount():0;Limits.bSameRun=S.Run==Run;Limits.bAlive=!R->IsDead();Limits.bFinite=!R->GetActorLocation().ContainsNaN();
        auto FX=R->FindComponentByClass<UBootRunEffectsComponent>();Limits.Effects=(FX?FX->GetPoolSize():0)+(R->GetFeedback()?R->GetFeedback()->GetActiveParticleCount():0);
        if(A.bValid&&A.RenderedSourceFrame>0){Tempos.Add(FMath::RoundToInt(A.EffectiveBpm));PlayedHashes.Add(A.TrackHash);if(LastEpoch&&LastEpoch!=A.TrackEpoch)++Transitions;LastEpoch=A.TrackEpoch;}if(A.bUnderflow)++Underflows;
        Limits.Tempos=Tempos.Num();Limits.Transitions=Transitions;
        int32 Actors=0,Pins=0;for(TActorIterator<AActor> It(G->GetWorld());It;++It)++Actors;if(T)for(auto C:T->GetLiveChunks())if(C&&T->IsChunkPinned(C->GetDescriptor().InstanceId))++Pins;
        MaxActors=FMath::Max(MaxActors,Actors);PeakMemory=FMath::Max(PeakMemory,uint64(FPlatformMemory::GetStats().UsedPhysical));
        FString Row=FString::Printf(TEXT("%.3f,%.3f,%.3f,%lld,%lld,%.3f,%d,%d,%d,%d,%llu,%.3f,%.3f,%.3f,%lld,%s,%lld,%lld,%lld,%lld,%lld,%d,%d\n"),Now-RunAt,S.ActiveSeconds,S.DistanceMetres,A.TrackEpoch,A.RenderedSourceFrame,A.EffectiveBpm,Limits.LiveChunks,Limits.Effects,Actors,Pins,PeakMemory,R->GetActorLocation().X,R->GetActorLocation().Y,R->GetActorLocation().Z,CurrentChunk,*A.TrackHash,Audio.RenderBlocks,Audio.RenderedOutputFrames,Audio.UnderflowBlocks,Audio.RingCapacityFrames,Audio.TotalRingBytes,Audio.LeaseSlots,Audio.DeviceSampleRate);
        Row.RemoveFromEnd(TEXT("\n"));Row+=FString::Printf(TEXT(",%d,%d,%d,%d,%d\n"),ScoreHistory,ProcessHistory,GuardHistory,SurfChunks,SurfOperations);
        FFileHelper::SaveStringToFile(Row,*(Dir/TEXT("metrics.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
        FString Error;if(!BootloopV04Audit::Validate(Limits,false,Error)){Check(TEXT("continuous_native_limits"),false,Error);Finish();}
        if(S.DistanceMetres>LastCanonical+.1){LastCanonical=S.DistanceMetres;CanonicalAt=Now;}else if(T&&Now-CanonicalAt>45.){Check(TEXT("canonical_route_not_stuck"),false,FString::Printf(TEXT("chunk=%lld travelled=%.2f canonical=%.2f"),CurrentChunk,Travel,S.DistanceMetres));Finish();}
        if(Travel>LastProgress+100.){LastProgress=Travel;LastProgressAt=Now;}else if(Now-LastProgressAt>15.){Check(TEXT("route_not_stuck"),false,FString::Printf(TEXT("chunk=%lld position=%s"),CurrentChunk,*R->GetActorLocation().ToString()));Finish();}
    }
    bool Tick()
    {
        if(bDone)return false;
        auto G=Game.Get();if(!G)return false;
        auto PC=Cast<ABootPlayerController>(G->GetWorld()->GetFirstPlayerController());auto R=PC?Cast<ABootRunner>(PC->GetPawn()):nullptr;
        if(!R||!PC)return true;
        double Now=FPlatformTime::Seconds(),Age=Now-PhaseAt;ObserveCard(G,R,PC,Now);auto T=Cast<ABootEndlessTrack>(G->GetTrack());
        if(Now-Started>1500.){Check(TEXT("scenario_timeout"),false);Finish();return false;}
        if(Stage==0&&Age>2.){
#if WITH_EDITOR
            // Match Editor screenshot capture: do not mistake pending default shaders for final art.
            UMaterialInterface::SubmitRemainingJobsForWorld(G->GetWorld());
            FAssetCompilingManager::Get().FinishAllCompilation();
            Check(TEXT("initial_asset_compilation_finished"),true,TEXT("Editor shader/asset jobs completed before native captures and observed run"));
#endif
            auto GI=Cast<UBootGameInstance>(G->GetGameInstance());if(GI)OriginalOptions=GI->GetOptions();if(auto Fps=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))OriginalMaxFps=Fps->GetFloat();
            Check(TEXT("scenario_mode"),Scenario==TEXT("FirstRun")?G->IsFirstRun():G->IsMusicRun());
            if(!Failure.IsEmpty()){Finish();return false;}Shot(PC,TEXT("01_desktop"));Go(1);
        }else if(Stage==1&&Age>.5){PC->ShowMusicLibrary();Shot(PC,TEXT("02_library"));Go(2);
        }else if(Stage==2&&Age>.5){
            auto Library=G->GetGameInstance()->GetSubsystem<UBootMusicLibrarySubsystem>();int32 Ready=0;
            if(Library)for(const auto& E:Library->Snapshot())if(E.bBuiltIn&&E.State==EBootLibraryState::Ready)++Ready;
            if(Ready<10){if(Age>90.){Check(TEXT("ten_starter_tracks_ready"),false,FString::Printf(TEXT("ready=%d of10"),Ready));Finish();return false;}return true;}
            Check(TEXT("ten_starter_tracks_ready"),true);Shot(PC,TEXT("02b_library_all_ready"));Go(21);
        }else if(Stage==21&&Age>.5){if(Scenario==TEXT("MusicRun")&&!bPreflightStarted&&!FParse::Param(FCommandLine::Get(),TEXT("BootloopV04BadFixture"))){BeginImportPreflight(G);return true;}PC->Back();G->StartRun();Go(3);
        }else if(Stage>=22&&Stage<=25){ImportPreflight(G,R,PC,Now,Age);
        }else if(Stage==3){
            if(Age>30.){Check(TEXT("start_running"),false,TEXT("PrepareRun or boot did not finish"));Finish();return false;}
            if(G->GetRunState()!=EBootRunState::Running)return true;
            Shot(PC,TEXT("03_hud"));Run=G->GetRunSnapshot().Run;RunAt=Now;LastProgressAt=Now;CanonicalAt=Now;LastSample=Now;bDriving=true;
            if(R->GetAvatar())R->GetAvatar()->OnFootContact.AddSP(AsShared(),&FAudit::OnFoot);
            if(T){T->OnChunkRetired.AddSP(AsShared(),&FAudit::OnRetired);T->OnChunkCommitted.AddSP(AsShared(),&FAudit::OnCommitted);for(auto C:T->GetLiveChunks())if(C)OnCommitted(C->GetDescriptor());}
            if(FParse::Param(FCommandLine::Get(),TEXT("BootloopV04BadFixture"))){Limits.LiveChunks=41;FString E;Check(TEXT("intentional_bad_runtime_fixture"),BootloopV04Audit::Validate(Limits,false,E),E);bDriving=false;Go(90);return true;}
            bDriving=false;Go(31);
        }else if(Stage==31&&Age>.35){bDriving=true;Go(4);
        }else if(Stage==4){
            if(Scenario==TEXT("FirstRun")){auto First=Cast<ABootFirstRunTrack>(G->GetTrack());if(First&&First->GetTutorial().IsComplete()){for(int32 I=0;I<9;++I)Check(FString::Printf(TEXT("physical_lesson_%d_completed"),I),First->GetTutorial().GetOutcome(I)==EBootFirstRunOutcome::Completed);if(PC->GetPage()==EBootPage::Library){Check(TEXT("tutorial_completion_to_library"),true);Shot(PC,TEXT("tutorial_complete_library"));Go(90);}return true;}if(G->GetRunState()!=EBootRunState::Running){const int32 FailedLesson=First?First->GetTutorial().GetLesson():Lesson;FVector FailedLocal=R->GetActorLocation();if(First&&First->GetSections().IsValidIndex(FailedLesson))FailedLocal=First->GetSections()[FailedLesson].Frame.InverseTransformPosition(FailedLocal);Check(TEXT("tutorial_alive"),false,FString::Printf(TEXT("lesson=%d objective=%d section=%d local=%s velocity=%s state=%d"),FailedLesson,First?First->GetTutorial().GetObjectiveIndex():Objective,First?First->GetTutorial().GetSection():-1,*FailedLocal.ToString(),*R->GetVelocity().ToString(),int32(G->GetRunState())));Shot(PC,TEXT("tutorial_failed"));Go(90);}return true;}
            if(G->GetRunState()!=EBootRunState::Running){Check(TEXT("continuous_running"),false,TEXT("death or unexpected lifecycle transition"));Shot(PC,TEXT("failure"));Go(90);return true;}
            if(Now-LastSample>=1.){LastSample=Now;Sample(G,R,T,Now);if(bDone)return false;}

            if(Scenario==TEXT("Avatar")){
                const int32 Frames[]={30,60,120};const TCHAR* Views[]={TEXT("forward"),TEXT("down"),TEXT("side"),TEXT("motion_off"),TEXT("body_off")};
                if(AvatarIndex<15){const int32 F=AvatarIndex/5,V=AvatarIndex%5;
                    if(!bAvatarView){PC->ConsoleCommand(FString::Printf(TEXT("t.MaxFPS %d"),Frames[F]),false);auto Options=OriginalOptions;Options.bMotion=V!=3;Options.bBody=V!=4;R->ApplyPreferences(Options);FRotator Rotation=PC->GetControlRotation();Rotation.Pitch=V==1?-78.f:0.f;if(V==2)Rotation.Yaw+=90.f;PC->SetControlRotation(Rotation);bDriving=V==0;PhaseAt=Now;AvatarFrameAt=Now;AvatarFrame=0;bAvatarView=true;return true;}
                    if(V==0&&Age<=3.25&&Now-AvatarFrameAt>=.25){AvatarFrameAt=Now;Shot(PC,FString::Printf(TEXT("avatar_%d_jog_raw_%02d"),Frames[F],AvatarFrame++));}
                    if(Age>(V==0?3.25:.75)){Shot(PC,FString::Printf(TEXT("avatar_%d_%s"),Frames[F],Views[V]));++AvatarIndex;bAvatarView=false;PhaseAt=Now;}
                }
                if(AvatarIndex==15&&Age>.75){Check(TEXT("avatar_capture_requests"),true,TEXT("Native camera views plus 3.25 seconds jog frames per FPS; foot contacts streamed separately. Side is first-person side-look."));Check(TEXT("avatar_actual_foot_contacts"),FootContacts>=12);auto Options=OriginalOptions;Options.bBody=true;R->ApplyPreferences(Options);bDriving=false;Go(35);}return true;
            }            auto S=G->GetRunSnapshot();if(S.CardOffer.IsSet()&&CardChoices<3&&!bCardPending)PressCard(G,R,PC,CardChoices%3);
            if(Scenario==TEXT("Soak")&&Now-RunAt>=1200.&&PlayedHashes.Num()>=10){Sample(G,R,T,Now);if(bDone)return false;FString E;Check(TEXT("real_20_minute_continuous_soak"),BootloopV04Audit::Validate(Limits,true,E),E);Check(TEXT("all_ten_tracks_playback"),PlayedHashes.Num()>=10);Shot(PC,TEXT("soak_complete"));bDriving=false;PausedPosition=R->GetActorLocation();G->PauseRun(true);PausedFrame=R->GetMusicTransport()->Snapshot().RenderedSourceFrame;Go(5);}
            else if(Scenario==TEXT("MusicRun")&&Transitions>=2&&Tempos.Num()>=3){Check(TEXT("three_tempos_same_run"),S.Run==Run);bDriving=false;PausedPosition=R->GetActorLocation();G->PauseRun(true);PausedFrame=R->GetMusicTransport()->Snapshot().RenderedSourceFrame;Shot(PC,TEXT("04_pause"));Go(5);}
        }else if(Stage==5&&Age>1.){Check(TEXT("pause_physics"),R->GetActorLocation().Equals(PausedPosition,.01));Check(TEXT("pause_audio_cursor"),FMath::Abs(double(R->GetMusicTransport()->Snapshot().RenderedSourceFrame)-PausedFrame)<=1024.);G->PauseRun(false);G->HandleDeath();Shot(PC,TEXT("05_results"));Go(6);
        }else if(Stage==6&&Age>.5){G->StartRun();Go(7);
        }else if(Stage==7&&G->GetRunState()==EBootRunState::Running){auto S=G->GetRunSnapshot();Check(TEXT("retry_new_run_zero_score"),S.Run!=Run&&S.Score==0&&S.DistanceMetres<1.&&!S.CardOffer.IsSet()&&!S.bPendingMutation);Shot(PC,TEXT("06_retry"));Go(90);
        }else if(Stage==35&&Age>.3){
            BeforeMuted=R->GetMusicTransport()->Snapshot();BeforeMutedRun=G->GetRunSnapshot().Run;
            auto Options=OriginalOptions;Options.Music=0.f;Options.Sfx=0.f;Options.EffectsIntensity=0.f;Options.bMotion=false;Options.bBody=true;R->ApplyPreferences(Options);
            if(auto FX=R->FindComponentByClass<UBootRunEffectsComponent>()){
                const int32 Before=FX->GetActiveEffectCount();FX->Emit(EBootFeedbackEvent::BufferDash,R->GetActorTransform());
                Check(TEXT("native_fx0_nonessential_dash_suppressed"),FX->GetActiveEffectCount()==Before,TEXT("Direct presentation event; not evidence of gameplay dash activation"));
                FX->Emit(EBootFeedbackEvent::Danger,R->GetActorTransform());Check(TEXT("native_fx0_essential_warning_created"),FX->HasEssentialWarning(),TEXT("Direct presentation event with Effects0, MotionOff, SFX0, Music0 applied"));
            }else Check(TEXT("native_fx_component_present"),false);
            Go(36);
        }else if(Stage==36&&Age>.55){
            const auto Audio=R->GetMusicTransport()->Snapshot();auto FX=R->FindComponentByClass<UBootRunEffectsComponent>();
            Check(TEXT("native_music0_transport_keeps_rendering"),BeforeMuted.bValid&&Audio.bValid&&G->GetRunSnapshot().Run==BeforeMutedRun&&(Audio.TrackEpoch>BeforeMuted.TrackEpoch||Audio.RenderedSourceFrame>BeforeMuted.RenderedSourceFrame),TEXT("Music gain0 applied through Runner preferences; source cursor continued; hardware silence/audibility not measured"));
            Check(TEXT("native_fx0_essential_warning_persists"),FX&&FX->HasEssentialWarning(),TEXT("Warning remains after0.55s; nonessential dash suppressed. SFX0 and MotionOff applied through actual component preferences."));
            Shot(PC,TEXT("avatar_fx0_motionoff_sfx0_music0_essential_warning"));Go(37);
        }else if(Stage==37&&Age>.3){R->ApplyPreferences(OriginalOptions);auto Options=OriginalOptions;Options.bBody=true;R->ApplyPreferences(Options);Go(39);
        }else if(Stage==39&&Age>.3){if(FreezeCamera(R,PC))Go(40);else Go(90);
        }else if(Stage==40&&Age>.3){if(PC->PlayerCameraManager)PC->PlayerCameraManager->UpdateCamera(1.f/60.f);Shot(PC,TEXT("avatar_external_front_frozen_actual_pose"));Go(41);
        }else if(Stage==41&&Age>.35){if(auto Camera=FrozenCamera.Get()){Camera->SetRelativeLocationAndRotation(FVector(0,250,0),FRotator(0,-90,0));if(PC->PlayerCameraManager)PC->PlayerCameraManager->UpdateCamera(1.f/60.f);}Go(42);
        }else if(Stage==42&&Age>.3){Shot(PC,TEXT("avatar_external_side_frozen_actual_pose"));Go(43);
        }else if(Stage==43&&Age>.35){Check(TEXT("avatar_external_pose_fixed"),R->GetActorTransform().Equals(FrozenActorTransform,.001));RestoreCamera();R->ApplyPreferences(OriginalOptions);Check(TEXT("avatar_camera_restored"),!bCameraFrozen&&R->IsActorTickEnabled()==bSavedRunnerTick&&R->GetCharacterMovement()->IsComponentTickEnabled()==bSavedMovementTick&&PC->IsActorTickEnabled()==bSavedControllerTick);Go(90);
        }else if(Stage==90&&Age>.5){Finish();return false;}
        return true;
    }
    void OnRetired(int64 Id){++Retired;LastRetired=Id;}
    void OnCommitted(const FBootCommittedChunk& D)
    {
        // Stream decisions to disk; no unbounded audit-owned descriptor history.
        FString Row=FString::Printf(TEXT("%lld,%s,%u,%u,%.3f,%.3f,%.3f,%.3f\n"),D.InstanceId,*D.DefinitionId.ToString(),D.DefinitionVersion,D.Recipe.Variant,D.WorldEntry.GetLocation().X,D.WorldEntry.GetLocation().Y,D.WorldEntry.GetLocation().Z,D.WorldEntry.Rotator().Yaw);
        FFileHelper::SaveStringToFile(Row,*(Dir/TEXT("replay.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    }
};
TSharedPtr<FAudit> Active;
}
void BootloopV04Audit::Start(ABootGameMode* Game)
{
    if(!Game||!FParse::Param(FCommandLine::Get(),TEXT("BootloopV04Audit"))||FParse::Param(FCommandLine::Get(),TEXT("nullrhi")))return;
    Stop();Active=MakeShared<FAudit>();Active->Game=Game;Active->bLeftWallAudit=FParse::Param(FCommandLine::Get(),TEXT("BootloopV04LeftWall"));Active->Scenario=TEXT("MusicRun");FParse::Value(FCommandLine::Get(),TEXT("BootloopV04Scenario="),Active->Scenario);
    const FString Stamp=FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Active->Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("V04Audit")/Stamp);
    Active->Captures=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../assets/Captures/V04")/Stamp);
    IFileManager::Get().MakeDirectory(*Active->Dir,true);IFileManager::Get().MakeDirectory(*Active->Captures,true);
      FFileHelper::SaveStringToFile(TEXT("wall_s,active_s,distance_m,track_epoch,source_frame,bpm,live_chunks,fx,actors,pins,peak_bytes,x,y,z,chunk,track_hash,audio_render_blocks,audio_output_frames,audio_underflow_blocks,ring_capacity_frames,ring_bytes,lease_slots,device_sample_rate,score_operation_history,registered_process_history,guard_contact_history,surf_history_chunks,surf_history_operations\n"),*(Active->Dir/TEXT("metrics.csv")));
    FFileHelper::SaveStringToFile(TEXT("instance,definition,version,variant,entry_x,entry_y,entry_z,entry_yaw\n"),*(Active->Dir/TEXT("replay.csv")));
    FFileHelper::SaveStringToFile(TEXT("wall_s,objective,section,x,y,z,vx,vy,vz,grounded,base_owner,popup_present,popup_x,popup_y,popup_z,available,moving,hostile,has_carried,ride_blocked,based_on_popup,last_snap_wall_s,snap_attempts,capsule_halfheight,capsule_radius,previous_target_x,previous_target_y,previous_target_z\n"),*(Active->Dir/TEXT("tutorial_trace.csv")));
    FFileHelper::SaveStringToFile(TEXT("wall_s,lesson,section,frame_dt,state,speed_cmps,run_envelope_cmps,vx,vy,vz,grounded,sliding,wallrunning,wall_side,base_owner,base_moving,has_carried,view_pitch\n"),*(Active->Dir/TEXT("movement_states.csv")));
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("scenario"),Active->Scenario);J->SetStringField(TEXT("map"),Game->GetWorld()->GetMapName());J->SetStringField(TEXT("capture_directory"),Active->Captures);J->SetNumberField(TEXT("seed"),Game->GetSeed());J->SetBoolField(TEXT("no_teleport_during_run"),Active->Scenario!=TEXT("FirstRun"));J->SetBoolField(TEXT("no_teleport_substitutes_for_measured_maneuvers"),true);J->SetBoolField(TEXT("human_playthrough"),false);J->SetStringField(TEXT("state"),TEXT("started; checks.json required for completion"));Active->SaveJson(TEXT("manifest.json"),J);
    if(Active->Scenario!=TEXT("MusicRun")&&Active->Scenario!=TEXT("FirstRun")&&Active->Scenario!=TEXT("Avatar")&&Active->Scenario!=TEXT("Soak")){Active->Check(TEXT("known_scenario"),false);Active->Finish();return;}
    Active->DriveHandle=FWorldDelegates::OnWorldPreActorTick.AddSP(Active.ToSharedRef(),&FAudit::Drive);
    TWeakPtr<FAudit> Weak=Active;Active->TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float){auto A=Weak.Pin();return A&&A->Tick();}),.025f);
    UE_LOG(LogTemp,Display,TEXT("V04 AUDIT output: %s"),*Active->Dir);
}
void BootloopV04Audit::Stop()
{
    if(!Active)return;Active->RestoreStateLook();Active->RestoreCamera();FWorldDelegates::OnWorldPreActorTick.Remove(Active->DriveHandle);FTSTicker::GetCoreTicker().RemoveTicker(Active->TickHandle);Active.Reset();
}
#else
void BootloopV04Audit::Start(ABootGameMode*){}
void BootloopV04Audit::Stop(){}
#endif



















