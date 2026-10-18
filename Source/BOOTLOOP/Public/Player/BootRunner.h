#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "Tutorial/BootFirstRunTutorial.h"
#include "Player/BootPoseTypes.h"
#include "BootRunner.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UBootRhythmComponent;
class UBootMovementComponent;
class UBootAvatarComponent;
class ABootSurfWindow;
class UAudioComponent;
class UBootInputMappingComponent;
class UBootFeedbackComponent;
class UBootMusicTransportComponent;
class UBootAbilityComponent;
class UBootRunEffectsComponent;
struct FBootOptions;
enum class EBootTutorialEvent : uint8;

UCLASS(Blueprintable)
class BOOTLOOP_API ABootRunner : public ACharacter
{
    GENERATED_BODY()
    friend struct FBootTask14ReviewAccess;

public:
    ABootRunner(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode = 0) override;

    UFUNCTION(BlueprintCallable, Category="BOOTLOOP") void RequestJump();
    UFUNCTION(BlueprintCallable, Category="BOOTLOOP") void StartSlide();
    UFUNCTION(BlueprintCallable, Category="BOOTLOOP") void RequestSnap();
    UFUNCTION(BlueprintCallable, Category="BOOTLOOP") void Die();
    UFUNCTION(BlueprintCallable, Category="BOOTLOOP") void ResetRun();
    void ResetToTransform(const FTransform& Transform,bool bRestartRhythm);
    UFUNCTION(BlueprintPure, Category="BOOTLOOP") bool IsDead() const { return bDead; }
    UFUNCTION(BlueprintPure, Category="BOOTLOOP") bool IsSliding() const { return bSliding; }
    UBootRhythmComponent* GetRhythm() const { return Rhythm; }
    UBootMovementComponent* GetParkour() const;
    UBootAvatarComponent* GetAvatar() const { return Avatar; }
    ABootSurfWindow* FindSurfTarget() const;
    FString GetSurfHint() const;
    void ApplyPreferences(const FBootOptions& Options);
    void ApplyInputPreferences();
    UBootInputMappingComponent* GetInputMapping() const { return InputMapping; }
    UBootFeedbackComponent* GetFeedback() const { return Feedback; }
    void RequestAirRedirect();
    UBootMusicTransportComponent* GetMusicTransport() const { return MusicTransport; }
    UBootAbilityComponent* GetAbilities() const { return Abilities; }
    void RequestBufferDash();
    void ChooseRunCard(int32 Index);
    UBootRunEffectsComponent* GetRunEffects() const { return RunEffects; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BOOTLOOP|Movement") float RunSpeed = 900.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BOOTLOOP|Movement") float JumpSpeed = 600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BOOTLOOP|Movement") float SlideSeconds = .65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BOOTLOOP|Camera") float LookSensitivity = .12f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BOOTLOOP") float DeathZ = -800.f;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void ChooseCardOne() { ChooseRunCard(0); }
    void ChooseCardTwo() { ChooseRunCard(1); }
    void ChooseCardThree() { ChooseRunCard(2); }
    void EmitFeedback(EBootFeedbackEvent Event,FVector Position,FVector Normal);
    void CaptureFirstRunObjective(EBootFirstRunEvent Event);
    void CompleteFirstRunObjective(EBootFirstRunEvent Event);
    void Move(const FInputActionValue& Value);
    void LookX(const FInputActionValue& Value);
    void LookY(const FInputActionValue& Value);
    void WheelJump(const FInputActionValue& Value);
    void HandleFootContact(int32 Foot,FVector Position);
    void EndSlide();
    void BeginGroundSlide();
    void TryBufferedJump();
    void ReleaseMouse();
    void CaptureMouse();
    void RetryInput();
    bool CanUseMovementInput() const;

    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBootRhythmComponent> Rhythm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBootAvatarComponent> Avatar;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBootInputMappingComponent> InputMapping;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBootFeedbackComponent> Feedback;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBootMusicTransportComponent> MusicTransport;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBootAbilityComponent> Abilities;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBootRunEffectsComponent> RunEffects;
    TMap<EBootFirstRunEvent,FBootTutorialEvidence> PendingFirstRunObjectives;
    EBootPoseAbilityEvent PendingPoseAbility=EBootPoseAbilityEvent::None;
    bool bFreshWheelJump=false,bBufferedWheelJump=false,bWasWallRunning=false;
    FTransform StartTransform;
    FTimerHandle SlideTimer;
    float SlideStartedAt=-100.f;
    float LastGroundedTime = -100.f;
    float JumpBufferedUntil = -100.f;
    bool bJumpConsumed = false;
    bool bSliding = false;
    bool bAirSlideQueued = false;
    bool bDead = false;
    struct FPendingSurf
    {
        TWeakObjectPtr<ABootSurfWindow> Window;
        uint8 Rating = 0;
        int32 ActivationGateSequence = 0;
        FBootTutorialEvidence Evidence;
    };
    TArray<FPendingSurf> PendingSurf;
    float BaseFov = 95.f, CameraRoll = 0.f;
    bool bMotion = true;
    int32 PixelQuality = 0;
    FVector2D LastViewportSize = FVector2D::ZeroVector;
    void UpdatePixelRaster();
    FVector PreviousPoseVelocity=FVector::ZeroVector;
    float PreviousPoseYaw=0.f;
    bool bPoseHistory=false;
    TArray<TWeakObjectPtr<UInputComponent>> BoundInputs;
    struct FPendingLessonObjective
    {
        int32 Lesson=0;
        int32 Section=INDEX_NONE;
        int32 Generation=0;
    };
    TMap<EBootTutorialEvent,FPendingLessonObjective> PendingLessonObjectives;
    void CaptureLessonObjective(EBootTutorialEvent Event);
    void CompleteLessonObjective(EBootTutorialEvent Event);
};
