#include "Player/BootRunner.h"
#include "World/Presentation/BootRunEffectsComponent.h"
#include "World/Generation/BootEndlessTrack.h"
#include "Tutorial/BootFirstRunTrack.h"
#include "Music/BootRhythmComponent.h"
#include "Music/BootMusicTransportComponent.h"
#include "Player/BootAbilityComponent.h"
#include "Movement/BootMovementComponent.h"
#include "Player/BootAvatarComponent.h"
#include "Input/BootInputMappingComponent.h"
#include "Player/BootFeedbackComponent.h"
#include "World/Interaction/BootSurfWindow.h"
#include "World/Threats/BootPopupProcess.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "UObject/ConstructorHelpers.h"
#include "Core/BootGameInstance.h"
#include "Run/BootGameMode.h"
#include "World/Generation/BootTrack.h"
#include "World/Generation/BootCourseTrack.h"
#include "Tutorial/BootTutorialState.h"
#include "UI/BootPlayerController.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"

ABootRunner::ABootRunner(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UBootMovementComponent>(ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick = true;
    Rhythm = CreateDefaultSubobject<UBootRhythmComponent>(TEXT("Rhythm"));
    Avatar = CreateDefaultSubobject<UBootAvatarComponent>(TEXT("ShellAvatar"));
    InputMapping=CreateDefaultSubobject<UBootInputMappingComponent>(TEXT("InputMapping"));
    Feedback=CreateDefaultSubobject<UBootFeedbackComponent>(TEXT("MovementFeedback"));
    MusicTransport=CreateDefaultSubobject<UBootMusicTransportComponent>(TEXT("MusicTransport"));
    Abilities=CreateDefaultSubobject<UBootAbilityComponent>(TEXT("RunAbilities"));
    RunEffects=CreateDefaultSubobject<UBootRunEffectsComponent>(TEXT("RunEffects"));
    GetCapsuleComponent()->InitCapsuleSize(34.f, 96.f);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    Camera->SetupAttachment(GetCapsuleComponent());
    Camera->SetRelativeLocation(FVector(0, 0, 64));
    Camera->bUsePawnControlRotation = true;
    Camera->SetFieldOfView(95.f);
    bUseControllerRotationYaw = true;
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->MaxWalkSpeed = RunSpeed;
    Movement->MaxWalkSpeedCrouched = 350.f;
    Movement->JumpZVelocity = JumpSpeed;
    Movement->GravityScale = 1.7f;
    Movement->AirControl = .6f;
    Movement->MaxAcceleration = 7000.f;
    Movement->GroundFriction = 6.f;
    Movement->BrakingDecelerationWalking = 2200.f;
    Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
    Movement->SetCrouchedHalfHeight(44.f);
    Movement->bCanWalkOffLedgesWhenCrouching = true;
}

void ABootRunner::BeginPlay()
{
    Super::BeginPlay();
    Avatar->OnFootContact.AddUObject(this,&ABootRunner::HandleFootContact);
    StartTransform = GetActorTransform();
    GetCharacterMovement()->MaxWalkSpeed = RunSpeed;
    if (const auto* GI = GetGameInstance<UBootGameInstance>()) ApplyPreferences(GI->GetOptions());
    if (APlayerController* PC = Cast<APlayerController>(Controller))
    {
        if (PC->PlayerCameraManager)
        {
            PC->PlayerCameraManager->ViewPitchMin = -80.f;
            PC->PlayerCameraManager->ViewPitchMax = 80.f;
        }
        if (!Cast<ABootPlayerController>(PC)) CaptureMouse();
    }
}

void ABootRunner::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
    BoundInputs.RemoveAll([](const TWeakObjectPtr<UInputComponent>& Bound){return !Bound.IsValid();});
    if(BoundInputs.ContainsByPredicate([Input](const TWeakObjectPtr<UInputComponent>& Bound){return Bound.Get()==Input;})) return;
    BoundInputs.Add(Input);
    InputMapping->EnsureActions();
    Input->BindAction(InputMapping->GetMoveAction(),ETriggerEvent::Triggered,this,&ABootRunner::Move);
    Input->BindAction(InputMapping->GetLookXAction(),ETriggerEvent::Triggered,this,&ABootRunner::LookX);
    Input->BindAction(InputMapping->GetLookYAction(),ETriggerEvent::Triggered,this,&ABootRunner::LookY);
    Input->BindAction(InputMapping->GetWheelAction(),ETriggerEvent::Triggered,this,&ABootRunner::WheelJump);
    Input->BindAction(InputMapping->GetAction(EBootInputAction::Jump),ETriggerEvent::Started,this,&ABootRunner::RequestJump);
    Input->BindAction(InputMapping->GetAction(EBootInputAction::Slide),ETriggerEvent::Started,this,&ABootRunner::StartSlide);
    Input->BindAction(InputMapping->GetAction(EBootInputAction::Slide),ETriggerEvent::Completed,this,&ABootRunner::EndSlide);
    Input->BindAction(InputMapping->GetAction(EBootInputAction::Slide),ETriggerEvent::Canceled,this,&ABootRunner::EndSlide);
    Input->BindAction(InputMapping->GetAction(EBootInputAction::Snap),ETriggerEvent::Started,this,&ABootRunner::RequestSnap);
    Input->BindAction(InputMapping->GetAction(EBootInputAction::AirRedirect),ETriggerEvent::Started,this,&ABootRunner::RequestAirRedirect);
    Input->BindAction(InputMapping->GetAction(EBootInputAction::Retry),ETriggerEvent::Started,this,&ABootRunner::RetryInput);
    Input->BindAction(InputMapping->GetAction(EBootInputAction::CardOne),ETriggerEvent::Started,this,&ABootRunner::ChooseCardOne);
    Input->BindAction(InputMapping->GetAction(EBootInputAction::CardTwo),ETriggerEvent::Started,this,&ABootRunner::ChooseCardTwo);
    Input->BindAction(InputMapping->GetAction(EBootInputAction::CardThree),ETriggerEvent::Started,this,&ABootRunner::ChooseCardThree);
    Input->BindAction(InputMapping->GetAction(EBootInputAction::BufferDash),ETriggerEvent::Started,this,&ABootRunner::RequestBufferDash);
    ApplyInputPreferences();
}

void ABootRunner::ApplyInputPreferences()
{
    const auto* GI=GetGameInstance<UBootGameInstance>();
    InputMapping->Apply(GI?GI->GetInputPreferences():FBootInputPreferences(),Cast<APlayerController>(Controller));
}
void ABootRunner::WheelJump(const FInputActionValue& Value)
{
    const auto* GI=GetGameInstance<UBootGameInstance>();
    const auto& Prefs=GI?GI->GetInputPreferences():FBootInputPreferences();
    if(Prefs.AcceptsWheel(Value.Get<float>())) { bFreshWheelJump=true; RequestJump(); bFreshWheelJump=false; }
}
void ABootRunner::RequestAirRedirect()
{
    if(!CanUseMovementInput()) return;
    FVector Direction=GetLastMovementInputVector();
    if(Direction.IsNearlyZero()) Direction=Camera->GetForwardVector();
    CaptureFirstRunObjective(EBootFirstRunEvent::AirRedirect);
    CaptureLessonObjective(EBootTutorialEvent::AirRedirectLanded);
    if(GetParkour()->TryAirRedirect(Direction))
    {
        EmitFeedback(EBootFeedbackEvent::AirRedirect,GetActorLocation(),FVector::UpVector);
    }
    else { PendingFirstRunObjectives.Remove(EBootFirstRunEvent::AirRedirect); PendingLessonObjectives.Remove(EBootTutorialEvent::AirRedirectLanded); }
}
void ABootRunner::HandleFootContact(int32 Foot,FVector Position)
{ EmitFeedback(EBootFeedbackEvent::FootContact,Position,FVector::UpVector); }
void ABootRunner::CaptureLessonObjective(EBootTutorialEvent Event)
{
    PendingLessonObjectives.Remove(Event);
    const auto* Game=GetWorld()->GetAuthGameMode<ABootGameMode>();
    const auto* Course=Game?Cast<ABootCourseTrack>(Game->GetTrack()):nullptr;
    if(!Game || Game->GetRunState()!=EBootRunState::Running || !Course || !Course->IsInActiveLesson(this)) return;
    FPendingLessonObjective Origin;
    Origin.Lesson=Course->GetTutorial().CurrentLesson;
    Origin.Section=Course->GetCourseState().CurrentNodeId;
    Origin.Generation=Game->GetAttemptGeneration();
    PendingLessonObjectives.Add(Event,Origin);
}
void ABootRunner::CompleteLessonObjective(EBootTutorialEvent Event)
{
    FPendingLessonObjective Origin;
    if(!PendingLessonObjectives.RemoveAndCopyValue(Event,Origin)) return;
    const auto* Game=GetWorld()->GetAuthGameMode<ABootGameMode>();
    auto* Course=Game?Cast<ABootCourseTrack>(Game->GetTrack()):nullptr;
    if(Game && Game->GetRunState()==EBootRunState::Running && Course && Course->IsInActiveLesson(this))
        Course->NotifyObjective(Event,Origin.Generation,Origin.Lesson,Origin.Section);
}
void ABootRunner::EmitFeedback(EBootFeedbackEvent Event,FVector Position,FVector Normal)
{
    Feedback->Notify(Event,Position,Normal);
    RunEffects->Emit(Event,FTransform(Normal.Rotation(),Position));
}
void ABootRunner::CaptureFirstRunObjective(EBootFirstRunEvent Event)
{
    PendingFirstRunObjectives.Remove(Event);
    if(auto* Mode=GetWorld()->GetAuthGameMode<ABootGameMode>())
        if(Mode->IsFirstRun() && Mode->GetRunState()==EBootRunState::Running)
        {
            auto Evidence=Mode->CaptureFirstRunEvidence(Event);
            if(Evidence.ActionSerial) PendingFirstRunObjectives.Add(Event,Evidence);
        }
}
void ABootRunner::CompleteFirstRunObjective(EBootFirstRunEvent Event)
{
    FBootTutorialEvidence Evidence;
    if(PendingFirstRunObjectives.RemoveAndCopyValue(Event,Evidence))
        if(auto* Mode=GetWorld()->GetAuthGameMode<ABootGameMode>()) Mode->SubmitFirstRunEvidence(Evidence);
}
void ABootRunner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(SlideTimer);
    InputMapping->RemoveFromPlayer(Cast<APlayerController>(Controller));
    Feedback->ResetFeedback(); RunEffects->ResetRun();
    Super::EndPlay(EndPlayReason);
}
bool ABootRunner::CanUseMovementInput() const
{
    const APlayerController* PC = Cast<APlayerController>(Controller);
    return !bDead && (!PC || !PC->bShowMouseCursor);
}

void ABootRunner::Move(const FInputActionValue& Value)
{
    if (!CanUseMovementInput() || !Controller) return;
    const FVector2D Axes = Value.Get<FVector2D>().GetClampedToMaxSize(1.f);
    const FRotator Yaw(0, Controller->GetControlRotation().Yaw, 0);
    AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Axes.Y);
    AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Axes.X);
}

void ABootRunner::LookX(const FInputActionValue& Value)
{
    if (CanUseMovementInput()) AddControllerYawInput(Value.Get<float>() * LookSensitivity);
}

void ABootRunner::LookY(const FInputActionValue& Value)
{
    const auto* GI=GetGameInstance<UBootGameInstance>();
    const float Sign=GI && GI->GetInputPreferences().bInvertY?1.f:-1.f;
    if (CanUseMovementInput()) AddControllerPitchInput(Sign*Value.Get<float>() * LookSensitivity);
}

void ABootRunner::RequestJump()
{
    if (!CanUseMovementInput()) return;
    if(GetParkour()->TryRampJump())
    {
        bJumpConsumed=true;JumpBufferedUntil=-100.f;
        if(auto* GI=GetGameInstance<UBootGameInstance>())GI->Sound(EBootSound::Jump);
        return;
    }
    CaptureFirstRunObjective(EBootFirstRunEvent::WallKick);
    CaptureLessonObjective(EBootTutorialEvent::WallKickLanded);
    if (GetParkour()->TryWallKick())
    {
        PendingPoseAbility=EBootPoseAbilityEvent::WallKick;
        bJumpConsumed = true; JumpBufferedUntil = -100.f;
        if (auto* GI = GetGameInstance<UBootGameInstance>()) GI->Sound(EBootSound::Jump);
        return;
    }
    PendingFirstRunObjectives.Remove(EBootFirstRunEvent::WallKick);
    PendingLessonObjectives.Remove(EBootTutorialEvent::WallKickLanded);
    if (bSliding || bIsCrouched)
    {
        const bool Held=bSlideHeld;
        EndSlide();
        bSlideHeld=Held;
        GetCharacterMovement()->UnCrouch(false);
        if (bIsCrouched) {if(Held)Crouch();return;}
    }
    bBufferedWheelJump=bFreshWheelJump;
    JumpBufferedUntil = GetWorld()->GetTimeSeconds() + (GetParkour()->GetTraversalEnvelope().bValid?GetParkour()->GetManualHopGrace():.10f);
    TryBufferedJump();
}

void ABootRunner::TryBufferedJump()
{
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    const float Now = GetWorld()->GetTimeSeconds();
    const bool bGroundOrCoyote = Movement->IsMovingOnGround()
        || (Movement->IsFalling() && Now - LastGroundedTime <= (GetParkour()->GetTraversalEnvelope().bValid?GetParkour()->GetManualHopGrace():.10f));
    if (!bDead && !bJumpConsumed && !bIsCrouched && !bSliding
        && JumpBufferedUntil >= Now && bGroundOrCoyote)
    {
        const float Before=Movement->Velocity.Size2D();
        CaptureFirstRunObjective(EBootFirstRunEvent::Jumped);
        if(bBufferedWheelJump) CaptureFirstRunObjective(EBootFirstRunEvent::ManualHop);
        GetParkour()->PrepareGroundJump();
        if(Movement->Velocity.Size2D()>Before+50.f) CaptureLessonObjective(EBootTutorialEvent::ManualHop);
        CaptureLessonObjective(EBootTutorialEvent::JumpLanded);
        LaunchCharacter(FVector(0, 0, GetParkour()->GetTraversalEnvelope().bValid?GetParkour()->GetTraversalEnvelope().JumpZCmps:JumpSpeed), false, true);
        if(!bBufferedWheelJump || Movement->Velocity.Size2D()<=Before+50.f) PendingFirstRunObjectives.Remove(EBootFirstRunEvent::ManualHop);
        bBufferedWheelJump=false;
        bJumpConsumed = true;
        if(bSlideHeld)bAirSlideQueued=true;
        JumpBufferedUntil = -100.f;
        LastGroundedTime = -100.f;
        if (auto* GI = GetGameInstance<UBootGameInstance>()) GI->Sound(EBootSound::Jump);
    }
}

void ABootRunner::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
    Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);
    if (GetCharacterMovement()->IsMovingOnGround())
    {
        if(PrevMovementMode==MOVE_Falling && !bDead)
        {
            CompleteFirstRunObjective(EBootFirstRunEvent::Jumped);
            CompleteFirstRunObjective(EBootFirstRunEvent::ManualHop);
            CompleteFirstRunObjective(EBootFirstRunEvent::WallKick);
            CompleteFirstRunObjective(EBootFirstRunEvent::AirRedirect);
            CompleteLessonObjective(EBootTutorialEvent::JumpLanded);
            CompleteLessonObjective(EBootTutorialEvent::ManualHop);
            CompleteLessonObjective(EBootTutorialEvent::WallKickLanded);
            CompleteLessonObjective(EBootTutorialEvent::AirRedirectLanded);
        }
        bJumpConsumed = false;
        LastGroundedTime = GetWorld()->GetTimeSeconds();
        if (bAirSlideQueued && !bDead)
        {
            BeginGroundSlide();
        }
        else
        {
            // Capture the landing velocity before PhysWalking applies ground braking.
            // The queued LaunchCharacter then restores it for a deliberately buffered hop.
            TryBufferedJump();
        }
    }
}

void ABootRunner::StartSlide()
{
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    if (!CanUseMovementInput() || bSliding) return;
    bSlideHeld=true;
    JumpBufferedUntil = -100.f;
    if(GetParkour()->IsWallRunning())Movement->SetMovementMode(MOVE_Falling);
    if (Movement->IsFalling())
    {
        CaptureLessonObjective(EBootTutorialEvent::SlideCompleted);
        CaptureFirstRunObjective(EBootFirstRunEvent::AirSlideLanded);
        bAirSlideQueued = true;
        Crouch();
        Movement->Crouch(false);
        return;
    }
    if (Movement->IsMovingOnGround())
    {
        CaptureLessonObjective(EBootTutorialEvent::SlideCompleted);
        CaptureFirstRunObjective(EBootFirstRunEvent::GroundSlide);
        BeginGroundSlide();
    }
}

void ABootRunner::BeginGroundSlide()
{
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    if(bAirSlideQueued) CompleteFirstRunObjective(EBootFirstRunEvent::AirSlideLanded);
    bAirSlideQueued = false;
    // Even after a collision removes speed, ducking must remain available.
    bSliding = Movement->Velocity.Size2D() >= 300.f;
    Crouch();
    Movement->Crouch(false);
    Movement->GroundFriction = bSliding ? .15f*(GetParkour()->GetTraversalEnvelope().bValid?GetParkour()->GetTraversalEnvelope().SlideBrakingScale:1.f) : 6.f;
    Movement->BrakingDecelerationWalking = bSliding ? 0.f : 2200.f;
    Movement->MaxWalkSpeedCrouched = bSliding ? FMath::Max(RunSpeed, Movement->Velocity.Size2D()) : 350.f;
    SlideStartedAt=GetWorld()->GetTimeSeconds();
    GetWorldTimerManager().SetTimer(SlideTimer, this, &ABootRunner::FinishSlideGlide, SlideSeconds, false);
}

void ABootRunner::FinishSlideGlide()
{
    if(bDead)return;
    if(bSliding)CompleteLessonObjective(EBootTutorialEvent::SlideCompleted);
    if(bSliding && GetCharacterMovement()->IsMovingOnGround())CompleteFirstRunObjective(EBootFirstRunEvent::GroundSlide);
    if(!bSlideHeld)EndSlide();
}

void ABootRunner::EndSlide()
{
    GetWorldTimerManager().ClearTimer(SlideTimer);
    bSlideHeld=false;
    if(bSliding && !bDead) CompleteLessonObjective(EBootTutorialEvent::SlideCompleted);
    if(bSliding && !bDead && GetCharacterMovement()->IsMovingOnGround() && GetWorld()->GetTimeSeconds()-SlideStartedAt>=SlideSeconds-.01f) CompleteFirstRunObjective(EBootFirstRunEvent::GroundSlide);
    else PendingFirstRunObjectives.Remove(EBootFirstRunEvent::GroundSlide);
    if(!bSliding || bDead) PendingLessonObjectives.Remove(EBootTutorialEvent::SlideCompleted);
    bSliding = false;
    bAirSlideQueued = false;
    GetCharacterMovement()->GroundFriction = 6.f;
    GetCharacterMovement()->BrakingDecelerationWalking = 2200.f;
    GetCharacterMovement()->MaxWalkSpeedCrouched = 350.f;
    // CharacterMovement retries uncrouch and keeps the short capsule while a ceiling blocks it.
    UnCrouch();
}

void ABootRunner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(bSlideHeld && !CanUseMovementInput())EndSlide();
    if(bSlideHeld && !bDead && GetCharacterMovement()->IsMovingOnGround())
    {
        auto* Movement=GetCharacterMovement();
        if(bSliding && GetWorld()->GetTimeSeconds()-SlideStartedAt>SlideSeconds)
        {
            const float Speed=Movement->Velocity.Size2D();
            const float Braking=1000.f*(GetParkour()->GetTraversalEnvelope().bValid?GetParkour()->GetTraversalEnvelope().SlideBrakingScale:1.f);
            const float Target=FMath::Max(350.f,Speed-Braking*DeltaSeconds);
            if(Speed>350.f){Movement->Velocity.X*=Target/Speed;Movement->Velocity.Y*=Target/Speed;}
            Movement->MaxWalkSpeedCrouched=Target;
            if(Speed<=365.f){bSliding=false;Movement->GroundFriction=6.f;Movement->BrakingDecelerationWalking=2200.f;Movement->MaxWalkSpeedCrouched=350.f;}
        }
    }
    if (!bDead)
    {
        if (GetActorLocation().Z < DeathZ)
        {
            Die();
        }
        else
        {
            if (GetCharacterMovement()->IsMovingOnGround() && !bJumpConsumed)
                LastGroundedTime = GetWorld()->GetTimeSeconds();
            TryBufferedJump();
        }
    }
    const float TargetEyeZ = bIsCrouched ? 28.f : 64.f;
    FVector Eye = Camera->GetRelativeLocation();
    // Snap down immediately for clearance; smooth the return upward after standing.
    Eye.Z = TargetEyeZ < Eye.Z ? TargetEyeZ : FMath::FInterpTo(Eye.Z, TargetEyeZ, DeltaSeconds, 14.f);
    Camera->SetRelativeLocation(Eye);
    const auto* Parkour = GetParkour();
    const auto* CurrentGame=GetWorld()->GetAuthGameMode<ABootGameMode>();
    const bool Active=!bDead && (!CurrentGame || CurrentGame->GetRunState()==EBootRunState::Running);
    FBootPoseInput Pose;
    Pose.CapsuleHalfHeight=GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    Pose.bMotionEnabled=bMotion;
    // Walking Velocity is relative to the movement base; UpdateBasedMovement
    // applies support translation separately, so standing on a window stays idle.
    const FRotator Yaw(0,GetActorRotation().Yaw,0);
    const FVector RelativeVelocity=Active?GetVelocity():FVector::ZeroVector;
    Pose.LocalVelocity=Yaw.UnrotateVector(RelativeVelocity);
    Pose.LocalAcceleration=bPoseHistory?Yaw.UnrotateVector((RelativeVelocity-PreviousPoseVelocity)/FMath::Max(DeltaSeconds,.001f)):FVector::ZeroVector;
    Pose.VerticalVelocity=RelativeVelocity.Z;
    Pose.AimPitch=FMath::UnwindDegrees(GetViewRotation().Pitch);
    Pose.TurnRateDegrees=bPoseHistory?FMath::FindDeltaAngleDegrees(PreviousPoseYaw,Yaw.Yaw)/FMath::Max(DeltaSeconds,.001f):0.f;
    FVector WorldVelocity=RelativeVelocity;
    if(Parkour->IsMovingOnGround()) if(auto* Base=Cast<UPrimitiveComponent>(GetMovementBaseObject())) WorldVelocity+=Base->GetComponentVelocity();
    Pose.WorldPlanarSpeed=Active?WorldVelocity.Size2D():0.f;
    Pose.WallSide=Parkour->GetWallSide(); Pose.bGrounded=Parkour->IsMovingOnGround();
    Pose.bAirRedirectReady=Parkour->IsAirRedirectReady();
    Pose.bCrouched=bIsCrouched; Pose.bSliding=bSliding; Pose.bWallRunning=Parkour->IsWallRunning();
    Pose.NormalizedPace=Parkour->GetTraversalEnvelope().bValid?FMath::Clamp(Pose.LocalVelocity.Size2D()/FMath::Max(1.f,Parkour->GetTraversalEnvelope().RunCmps),0.f,2.f):-1.f;
    FVector Contact,Normal;
    Pose.bHasWallContact=Parkour->GetWallContact(Contact,Normal);
    if(Pose.bHasWallContact) Pose.LocalWallContact=Yaw.UnrotateVector(Contact-GetActorLocation());
    Pose.AbilityEvent=PendingPoseAbility; PendingPoseAbility=EBootPoseAbilityEvent::None;
    if(Active && Pose.bWallRunning && !bWasWallRunning) { CaptureFirstRunObjective(EBootFirstRunEvent::Wallrun); CompleteFirstRunObjective(EBootFirstRunEvent::Wallrun); }
    bWasWallRunning=Pose.bWallRunning;
    Avatar->UpdatePose(DeltaSeconds,Pose);
    Feedback->SetTraversalEnvelope(Parkour->GetTraversalEnvelope());
    if(Active) Feedback->UpdateMovement(DeltaSeconds,Pose);
    else { Feedback->ResetFeedback(); RunEffects->ResetRun(); }
    PreviousPoseVelocity=RelativeVelocity; PreviousPoseYaw=Yaw.Yaw; bPoseHistory=true;
    const float SpeedAlpha = Parkour->GetTraversalEnvelope().bValid?Parkour->GetMovementPaceAlpha():FMath::Clamp((GetVelocity().Size2D()-900.f)/700.f,0.f,1.f);
    Camera->SetFieldOfView(FMath::FInterpTo(Camera->FieldOfView, BaseFov+(bMotion?12.f*SpeedAlpha:0.f),DeltaSeconds,5.f));
    CameraRoll = FMath::FInterpTo(CameraRoll,bMotion && Parkour->IsWallRunning()?Parkour->GetWallSide()*3.f:0.f,DeltaSeconds,7.f);
    Camera->ClearAdditiveOffset();
    Camera->AddAdditiveOffset(FTransform(FRotator(0,0,CameraRoll)),0.f);
    UpdatePixelRaster();
    if (!bDead)
        for (int32 I=PendingSurf.Num()-1;I>=0;--I)
        {
            auto* Window=PendingSurf[I].Window.Get();
            if (!Window) { PendingSurf.RemoveAt(I); continue; }
            if(Window->ConsumeDepartureBoost(this))
            {
                if(Window->IsFlowRide()) GetParkour()->GrantSurfMomentum();
                else GetParkour()->GrantOverclock();
                EmitFeedback(EBootFeedbackEvent::Overclock,GetActorLocation(),FVector::UpVector);
            }
            const auto* Game=GetWorld()->GetAuthGameMode<ABootGameMode>();
            const auto* Track=Game?Game->GetTrack():nullptr;
            if(auto* Endless=Cast<ABootEndlessTrack>(Game?Game->GetTrack():nullptr))
            {
                if(Endless->CompleteRunOperation(Window,this,static_cast<EBootBeatRating>(PendingSurf[I].Rating))) PendingSurf.RemoveAt(I);
                continue;
            }
            if(Track && Track->CanCompleteSurf(Window,this,PendingSurf[I].ActivationGateSequence) && Window->CommitRide(this))
            {
                if(Game->IsFirstRun())
                {
                    const auto Evidence=PendingSurf[I].Evidence;
                    if(auto* Mode=GetWorld()->GetAuthGameMode<ABootGameMode>()) Mode->SubmitFirstRunEvidence(Evidence);
                }
                else Rhythm->CompleteSurf(Window->GetWindowId(),static_cast<EBootBeatRating>(PendingSurf[I].Rating));
                if(auto* Mode=GetWorld()->GetAuthGameMode<ABootGameMode>()) Mode->NotifyObjective(Window->IsA<ABootPopupProcess>()?EBootTutorialEvent::PopupSurfCompleted:EBootTutorialEvent::SurfCompleted);
                PendingSurf.RemoveAt(I);
            }
        }
}

void ABootRunner::NotifyHit(UPrimitiveComponent* MyComp,AActor* Other,UPrimitiveComponent* OtherComp,bool bSelfMoved,
    FVector HitLocation,FVector HitNormal,FVector NormalImpulse,const FHitResult& Hit)
{
    Super::NotifyHit(MyComp,Other,OtherComp,bSelfMoved,HitLocation,HitNormal,NormalImpulse,Hit);
    if(!bDead && OtherComp && OtherComp->ComponentHasTag(TEXT("BootFatal")))Die();
}

void ABootRunner::Die()
{
    if (bDead) return;
    bDead = true;
    PendingLessonObjectives.Reset(); PendingFirstRunObjectives.Reset(); PendingPoseAbility=EBootPoseAbilityEvent::None; bBufferedWheelJump=false; bWasWallRunning=false;
    Feedback->ResetFeedback(); RunEffects->ResetRun();
    PendingSurf.Reset();
    GetParkour()->ResetParkour();
    const auto* Mode=GetWorld()->GetAuthGameMode<ABootGameMode>();
    if(!Mode || !Mode->IsTraining()) Rhythm->StopRun();
    JumpBufferedUntil = -100.f;
    GetWorldTimerManager().ClearTimer(SlideTimer);
    EndSlide();
    GetCharacterMovement()->ClearAccumulatedForces();
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    if (auto* Game = GetWorld()->GetAuthGameMode<ABootGameMode>()) Game->HandleDeath();
    UE_LOG(LogTemp, Display, TEXT("BOOTLOOP: PROCESS TERMINATED"));
}

void ABootRunner::ResetRun()
{ ResetToTransform(StartTransform,true); }

void ABootRunner::ResetToTransform(const FTransform& Transform,bool bRestartRhythm)
{
    PendingLessonObjectives.Reset(); PendingFirstRunObjectives.Reset(); PendingPoseAbility=EBootPoseAbilityEvent::None; bBufferedWheelJump=false; bWasWallRunning=false;
    PendingSurf.Reset();
    GetParkour()->ResetParkour();
    Avatar->ResetPose();
    Feedback->ResetFeedback(); RunEffects->ResetRun(); bPoseHistory=false;
    GetWorldTimerManager().ClearTimer(SlideTimer);
    EndSlide();
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->ClearAccumulatedForces();
    Movement->StopMovementImmediately();
    SetBase(static_cast<UPrimitiveComponent*>(nullptr));
    SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
    Movement->UnCrouch(false);
    SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
    Movement->SetMovementMode(MOVE_Walking);
    Movement->MaxWalkSpeed = RunSpeed;
    Camera->SetRelativeLocation(FVector(0, 0, 64));
    Camera->SetFieldOfView(BaseFov); CameraRoll=0; Camera->ClearAdditiveOffset();
    SetBase(static_cast<UPrimitiveComponent*>(nullptr));
    bDead = false;
    bJumpConsumed = false;
    LastGroundedTime = -100.f;
    JumpBufferedUntil = -100.f;
    ConsumeMovementInputVector();
    if (APlayerController* PC = Cast<APlayerController>(Controller))
    {
        PC->SetControlRotation(Transform.Rotator());
        PC->FlushPressedKeys();
    }
    CaptureMouse();
    if(bRestartRhythm) Rhythm->ResetRun();
    UE_LOG(LogTemp, Display, TEXT("BOOTLOOP: RETRY reset complete"));
}

void ABootRunner::ReleaseMouse()
{
    if (APlayerController* PC = Cast<APlayerController>(Controller))
    {
        PC->bShowMouseCursor = true;
        PC->SetInputMode(FInputModeGameAndUI());
        PC->FlushPressedKeys();
    }
}

void ABootRunner::CaptureMouse()
{
    if (auto* PC = Cast<ABootPlayerController>(Controller))
        if (PC->GetPage() != EBootPage::Hud) return;
    if (APlayerController* PC = Cast<APlayerController>(Controller))
    {
        PC->bShowMouseCursor = false;
        PC->SetInputMode(FInputModeGameOnly());
    }
}

void ABootRunner::RetryInput()
{
    if (auto* PC = Cast<ABootPlayerController>(Controller))
    {
        if (PC->GetPage() == EBootPage::Hud || PC->GetPage() == EBootPage::Results) PC->StartRun();
    }
    else ResetRun();
}

void ABootRunner::ApplyPreferences(const FBootOptions& Options)
{
    LookSensitivity = Options.Sensitivity;
    BaseFov=Options.Fov; bMotion=Options.bMotion; PixelQuality=Options.PixelQuality;
    Feedback->ApplyPreferences(Options.EffectsIntensity,Options.Sfx,Options.bMotion);
    RunEffects->ApplyPreferences(Options);
    Camera->SetFieldOfView(BaseFov);
    Avatar->SetBodyVisible(Options.bBody);
    LastViewportSize=FVector2D::ZeroVector;
    Rhythm->ApplyPreferences(Options);
}

UBootMovementComponent* ABootRunner::GetParkour() const { return CastChecked<UBootMovementComponent>(GetCharacterMovement()); }
void ABootRunner::RequestBufferDash()
{
    if(!CanUseMovementInput()) return;
    auto* Mode=GetWorld()->GetAuthGameMode<ABootGameMode>();
    if(!Mode || Mode->GetRunState()!=EBootRunState::Running) return;
    CaptureFirstRunObjective(EBootFirstRunEvent::DashUsed);
    FVector Direction=GetLastMovementInputVector();
    if(Direction.IsNearlyZero()) Direction=Camera->GetForwardVector();
    if(GetParkour()->TryBufferDash(Direction))
    {
        PendingPoseAbility=EBootPoseAbilityEvent::Dash;
        EmitFeedback(EBootFeedbackEvent::BufferDash,GetActorLocation(),FVector::UpVector);
        CompleteFirstRunObjective(EBootFirstRunEvent::DashUsed);
    }
    else PendingFirstRunObjectives.Remove(EBootFirstRunEvent::DashUsed);
}
void ABootRunner::ChooseRunCard(int32 Index)
{
    if(!CanUseMovementInput() || Index<0 || Index>2) return;
    if(auto* Mode=GetWorld()->GetAuthGameMode<ABootGameMode>())
        if(Mode->GetRunState()==EBootRunState::Running) Mode->ChooseRunCard(Index);
}

ABootSurfWindow* ABootRunner::FindSurfTarget() const
{
    for(TActorIterator<ABootSurfWindow> It(GetWorld());It;++It)
        if(It->IsAvailable() && GetMovementBaseObject()==It->GetRootComponent()) return *It;
    ABootSurfWindow* Best=nullptr; double BestDistance=1500.;
    const FVector Eye=Camera->GetComponentLocation();
    const FVector View=Camera->GetForwardVector();
    for(TActorIterator<ABootSurfWindow> It(GetWorld());It;++It)
    {
        if(!It->IsAvailable()) continue;
        const FVector Delta=It->GetHandleLocation()-Eye;
        const double Distance=Delta.Size();
        const bool Based=GetMovementBaseObject()==It->GetRootComponent();
        if(Distance>=BestDistance || (!Based && FVector::DotProduct(Delta.GetSafeNormal(),View)<.15)) continue;
        FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(SnapVisibility),false,this);
        if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,It->GetHandleLocation(),ECC_Visibility,Query) && Hit.GetActor()!=*It) continue;
        Best=*It; BestDistance=Distance;
    }
    return Best;
}

void ABootRunner::RequestSnap()
{
    if(!CanUseMovementInput()) return;
    auto* Window=FindSurfTarget();
    if(!Window)
    {
        if(auto* Mode=GetWorld()->GetAuthGameMode<ABootGameMode>())
            if(auto* First=Cast<ABootFirstRunTrack>(Mode->GetTrack()))
                if(First->GetTutorial().GetLesson()==8) { CaptureFirstRunObjective(EBootFirstRunEvent::MusicFlowAcknowledged); CompleteFirstRunObjective(EBootFirstRunEvent::MusicFlowAcknowledged); }
        return;
    }
    FBootTutorialEvidence Origin;
    if(auto* Mode=GetWorld()->GetAuthGameMode<ABootGameMode>())
        Origin=Mode->CaptureFirstRunEvidence(Window->IsA<ABootPopupProcess>()?EBootFirstRunEvent::PopupIntercepted:EBootFirstRunEvent::SurfCompleted);
    const auto Rating=Rhythm->CaptureSnap();
    if(!Window->Activate(this,Rating!=EBootBeatRating::OffBeat)) return;
    FPendingSurf Ride; Ride.Window=Window; Ride.Rating=static_cast<uint8>(Rating);
    if(auto* Game=GetWorld()->GetAuthGameMode<ABootGameMode>()) if(Game->GetTrack()) Ride.ActivationGateSequence=Game->GetTrack()->GetCourseSerial();
    Ride.Evidence=Origin;
    PendingSurf.Add(Ride);
    PendingPoseAbility=EBootPoseAbilityEvent::Snap;
    Avatar->TriggerSnap();
    EmitFeedback(EBootFeedbackEvent::Snap,Window->GetHandleLocation(),FVector::UpVector);
}

FString ABootRunner::GetSurfHint() const
{
    if(GetParkour()->IsRampSurfing())return TEXT("RAMP SURF / A-D TO STEER / JUMP TO EXIT");
    for(const auto& Ride:PendingSurf)
        if(const auto* Window=Ride.Window.Get())
            if(GetMovementBaseObject()==Window->GetRootComponent())
                return Window->IsFlowRide()?TEXT("FLOW SURF / KEEP MOVING, HOP THROUGH"):
                    Window->HasChargeFor(const_cast<ABootRunner*>(this))?TEXT("SYNC CHARGED / JUMP TO DEPART"):TEXT("RIDING / JUMP TO DEPART");
    for(int32 I=PendingSurf.Num()-1;I>=0;--I)
    {
        const auto& Ride=PendingSurf[I];
        if(const auto* Window=Ride.Window.Get())
        {
            if(Window->HasCarried(const_cast<ABootRunner*>(this))) return TEXT("LAND FORWARD / COMPLETE SURF");
            return Window->IsMoving()?TEXT("WINDOW MOVING / GET ABOARD"):TEXT("WINDOW DOCKED / FOLLOW THE ROUTE");
        }
    }
    return GetParkour()->GetSurfMomentumBonus()>0.f?TEXT("SURF CAP +3.5 M/S / KEEP HOPPING"):FString();
}

void ABootRunner::UpdatePixelRaster()
{
    if(!GEngine || !GEngine->GameViewport) return;
    FVector2D Size; GEngine->GameViewport->GetViewportSize(Size);
    if(Size.X<1 || Size.Equals(LastViewportSize,.1)) return;
    LastViewportSize=Size;
    const float TargetWidth=PixelQuality==1?480.f:PixelQuality==2?960.f:640.f;
    const int32 Divider=FMath::Max(1,FMath::RoundToInt(Size.X/TargetWidth));
    auto Set=[](const TCHAR* Name,float Value){ if(auto* V=IConsoleManager::Get().FindConsoleVariable(Name)) V->Set(Value,ECVF_SetByCode); };
    Set(TEXT("r.ScreenPercentage"),100.f/Divider); Set(TEXT("r.Upscale.Quality"),0);
    Set(TEXT("r.DynamicRes.OperationMode"),0);
}

