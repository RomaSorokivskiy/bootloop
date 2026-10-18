#include "Player/BootFeedbackComponent.h"
#include "World/Presentation/BootRunEffectsComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    enum EFeedbackSound { StepSound, LandSound, SnapSound, WindSound, WallSound, ServoSound };
    const FLinearColor Ivory(.83f, .79f, .65f);
    const FLinearColor Amber(1.f, .64f, .12f);
    const FLinearColor Mint(.25f, .85f, .59f);
}

UBootFeedbackComponent::UBootFeedbackComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.bTickEvenWhenPaused = false;
    CosmeticRandom.Initialize(9503);
    TraversalEnvelope=FBootTraversalPolicy::FromTempo(130.,0);
    ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    ChipMesh = Cube.Object;
    ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Assets/V02/M_PixelSurface.M_PixelSurface"));
    ChipMaterial = Material.Object;
    for (const TCHAR* Name : {TEXT("S_PlasticStep"), TEXT("S_Land"), TEXT("S_Snap"),
        TEXT("S_WindLoop"), TEXT("S_WallLoop"), TEXT("S_ServoLoop")})
    {
        ConstructorHelpers::FObjectFinder<USoundBase> Sound(*FString::Printf(
            TEXT("/Game/Assets/AudioV02/%s.%s"), Name, Name));
        Sounds.Add(Sound.Object);
    }
}

UAudioComponent* UBootFeedbackComponent::MakeLoop(const TCHAR* Name, USoundBase* Sound)
{
    AActor* Owner = GetOwner();
    if (!Owner) return nullptr;
    UAudioComponent* Audio = NewObject<UAudioComponent>(Owner, Name);
    Owner->AddInstanceComponent(Audio);
    if (Owner->GetRootComponent()) Audio->SetupAttachment(Owner->GetRootComponent());
    Audio->bAutoActivate = false; Audio->bAllowSpatialization = false;
    Audio->bIsUISound = false; Audio->SetSound(Sound);
    Audio->RegisterComponent();
    return Audio;
}

void UBootFeedbackComponent::BeginPlay()
{
    Super::BeginPlay();
    WindAudio = MakeLoop(TEXT("FeedbackWind"), Sounds[WindSound]);
    EdgeAudio = MakeLoop(TEXT("FeedbackScrape"), Sounds[WallSound]);
    ServoAudio = MakeLoop(TEXT("FeedbackServo"), Sounds[ServoSound]);
}

void UBootFeedbackComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ResetFeedback();
    for (UStaticMeshComponent* Part : ChipParts) if (IsValid(Part)) Part->DestroyComponent();
    for (UAudioComponent* Audio : {WindAudio.Get(), EdgeAudio.Get(), ServoAudio.Get()})
        if (IsValid(Audio)) Audio->DestroyComponent();
    ChipParts.Reset(); ChipMaterials.Reset(); Chips.Reset();
    Super::EndPlay(Reason);
}

void UBootFeedbackComponent::PlayOneShot(USoundBase* Sound, float Pitch, float Volume)
{
    if (Sound && SfxVolume > 0.f && GetWorld() && GetWorld()->AllowAudioPlayback() && !GetWorld()->IsPaused())
        UGameplayStatics::PlaySound2D(this, Sound, SfxVolume * Volume * .6f, Pitch,
            0.f, nullptr, GetOwner(), false);
}

void UBootFeedbackComponent::SetLoop(UAudioComponent* Audio, float Volume, float Pitch)
{
    if (!Audio) return;
    Volume = FMath::Clamp(Volume * SfxVolume, 0.f, 1.f);
    Audio->SetVolumeMultiplier(Volume); Audio->SetPitchMultiplier(Pitch);
    if (Volume > .005f && GetWorld() && GetWorld()->AllowAudioPlayback() && !GetWorld()->IsPaused())
    { if (!Audio->IsPlaying()) Audio->Play(); }
    else Audio->Stop();
}

void UBootFeedbackComponent::Emit(FVector Position, FVector Normal, int32 Count, FLinearColor Color)
{
    if (EffectIntensity <= 0.f || !GetOwner() || !GetOwner()->GetRootComponent() ||
        Position.ContainsNaN() || Normal.ContainsNaN() || (GetWorld() && GetWorld()->IsPaused())) return;
    Normal = Normal.GetSafeNormal();
    if (Normal.IsNearlyZero()) Normal = FVector::UpVector;
    Count = FMath::CeilToInt(float(Count) * EffectIntensity);
    for (int32 I = 0; I < Count; ++I)
    {
        int32 Slot = INDEX_NONE;
        for (int32 J = 0; J < Chips.Num(); ++J)
            if (Chips[J].Remaining <= 0.f) { Slot = J; break; }
        if (Slot == INDEX_NONE)
        {
            if (Chips.Num() >= 24 || !ChipMesh) break;
            Slot = Chips.AddDefaulted();
            AActor* Owner = GetOwner();
            UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner,
                *FString::Printf(TEXT("FeedbackChip%d"), Slot));
            Owner->AddInstanceComponent(Part);
            Part->SetupAttachment(Owner->GetRootComponent());
            Part->SetAbsolute(true, true, true);
            Part->SetStaticMesh(ChipMesh);
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->SetGenerateOverlapEvents(false); Part->SetCanEverAffectNavigation(false);
            Part->SetCastShadow(false); Part->bReceivesDecals = false;
            Part->SetOnlyOwnerSee(true); Part->RegisterComponent();
            UMaterialInstanceDynamic* Material = ChipMaterial ? UMaterialInstanceDynamic::Create(ChipMaterial, this) : nullptr;
            if (Material) Part->SetMaterial(0, Material);
            ChipParts.Add(Part); ChipMaterials.Add(Material);
        }
        FChip& Chip = Chips[Slot];
        Chip.Position = Position + Normal * 3.f;
        Chip.Velocity = (Normal * CosmeticRandom.FRandRange(35.f, 90.f) +
            FVector(CosmeticRandom.FRandRange(-55.f, 55.f), CosmeticRandom.FRandRange(-55.f, 55.f), 0.f)) *
            (bMotionEnabled ? 1.f : .35f);
        Chip.Lifetime = Chip.Remaining = CosmeticRandom.FRandRange(.20f, .42f);
        Chip.Size = CosmeticRandom.FRandRange(1.2f, 2.8f);
        if (ChipMaterials[Slot]) ChipMaterials[Slot]->SetVectorParameterValue(TEXT("FlatColor"), Color);
        ChipParts[Slot]->SetWorldLocation(Chip.Position);
        ChipParts[Slot]->SetWorldScale3D(FVector(Chip.Size / 100.f));
        ChipParts[Slot]->SetVisibility(true);
    }
    SetComponentTickEnabled(GetActiveParticleCount() > 0);
}

void UBootFeedbackComponent::Notify(EBootFeedbackEvent Event, FVector Position, FVector Normal)
{
    if (GetWorld() && GetWorld()->IsPaused()) return;
    // v0.4 has its own event silhouettes and original motifs; legacy actors keep
    // their established feedback without playing two sounds on the v0.4 Runner.
    if ((Event==EBootFeedbackEvent::Snap || Event==EBootFeedbackEvent::AirRedirect || Event==EBootFeedbackEvent::Overclock)
        && GetOwner() && GetOwner()->FindComponentByClass<UBootRunEffectsComponent>())return;
    switch (Event)
    {
    case EBootFeedbackEvent::FootContact:
        PlayOneShot(Sounds[StepSound], (++StepIndex % 2) ? .96f : 1.04f, .65f);
        Emit(Position, Normal, 2, Ivory); break;
    case EBootFeedbackEvent::Takeoff: Emit(Position, Normal, 3, Ivory); break;
    case EBootFeedbackEvent::Land:
        PlayOneShot(Sounds[LandSound], 1.f, .8f); Emit(Position, Normal, 8, Ivory); break;
    case EBootFeedbackEvent::SlideBegin:
        PlayOneShot(Sounds[StepSound], .75f, .5f); Emit(Position, Normal, 5, Ivory); break;
    case EBootFeedbackEvent::WallBegin: Emit(Position, Normal, 4, Ivory); break;
    case EBootFeedbackEvent::SlideEnd:
    case EBootFeedbackEvent::WallEnd: break;
    case EBootFeedbackEvent::Snap:
        PlayOneShot(Sounds[SnapSound]); Emit(Position, Normal, 10, Amber); break;
    case EBootFeedbackEvent::Overclock:
        PlayOneShot(Sounds[SnapSound], 1.2f, .65f); Emit(Position, Normal, 12, Mint); break;
    case EBootFeedbackEvent::AirRedirect:
        PlayOneShot(Sounds[SnapSound], .85f, .65f); Emit(Position, Normal, 8, Amber); break;
    case EBootFeedbackEvent::Checkpoint:
        PlayOneShot(Sounds[SnapSound], 1.35f, .5f); Emit(Position, Normal, 8, Mint); break;
    }
}

void UBootFeedbackComponent::UpdateMovement(float Dt, const FBootPoseInput& Input)
{
    if (!GetOwner()) return;
    if (GetWorld() && GetWorld()->IsPaused())
    {
        for (UAudioComponent* Audio : {WindAudio.Get(), EdgeAudio.Get(), ServoAudio.Get()})
            if (Audio) Audio->SetPaused(true);
        return;
    }
    if (!FMath::IsFinite(Dt) || Dt <= 0.f) return;
    Dt = FMath::Min(Dt, .1f);
    for (UAudioComponent* Audio : {WindAudio.Get(), EdgeAudio.Get(), ServoAudio.Get()})
        if (Audio) Audio->SetPaused(false);
    const float Speed = Input.LocalVelocity.ContainsNaN() ? 0.f : FMath::Clamp(float(Input.LocalVelocity.Size2D()), 0.f, TraversalEnvelope.AbsoluteCmps);
    const bool SlideSupported=Input.bSliding && Input.bGrounded;
    const bool WallSupported=Input.bWallRunning && Input.bHasWallContact;
    FVector FootPosition = GetOwner()->GetActorLocation();
    if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
        if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
            FootPosition = Capsule->GetComponentLocation() - Capsule->GetUpVector() * Capsule->GetScaledCapsuleHalfHeight();
    if (bHasMovement)
    {
        if (bWasGrounded && !Input.bGrounded) Notify(EBootFeedbackEvent::Takeoff, FootPosition, FVector::UpVector);
        if (!bWasGrounded && Input.bGrounded) Notify(EBootFeedbackEvent::Land, FootPosition, FVector::UpVector);
        if (bWasSliding != SlideSupported) Notify(SlideSupported ? EBootFeedbackEvent::SlideBegin : EBootFeedbackEvent::SlideEnd, FootPosition, FVector::UpVector);
        if (bWasWallRunning != WallSupported) Notify(WallSupported ? EBootFeedbackEvent::WallBegin : EBootFeedbackEvent::WallEnd, FootPosition, FVector::UpVector);
    }
    bHasMovement = true; bWasGrounded = Input.bGrounded;
    bWasSliding = SlideSupported; bWasWallRunning = WallSupported;
    SupportMix=ComputeSupportSound(Input,TraversalEnvelope);
    SetLoop(WindAudio, SupportMix.Wind * .28f, FMath::Lerp(.9f, 1.18f, SupportMix.Wind));
    SetLoop(EdgeAudio, FMath::Max(SupportMix.Slide,SupportMix.Wall));
    SetLoop(ServoAudio, SupportMix.Servo);
    if ((SlideSupported || WallSupported) && Speed > 100.f)
    {
        TrailTime += Dt;
        if (TrailTime >= .065f)
        {
            TrailTime = FMath::Fmod(TrailTime, .065f);
            FVector Normal = WallSupported ? GetOwner()->GetActorRightVector() * -FMath::Sign(Input.WallSide) : FVector::UpVector;
            Emit(FootPosition, Normal, 2, Ivory);
        }
    }
    else TrailTime = 0.f;
}

void UBootFeedbackComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(Dt, TickType, ThisTickFunction);
    if (!FMath::IsFinite(Dt) || Dt <= 0.f || (GetWorld() && GetWorld()->IsPaused())) return;
    Dt = FMath::Min(Dt, .1f);
    for (int32 I = 0; I < Chips.Num(); ++I)
    {
        FChip& Chip = Chips[I];
        if (Chip.Remaining <= 0.f) continue;
        Chip.Remaining = FMath::Max(0.f, Chip.Remaining - Dt);
        if (Chip.Remaining <= 0.f) { ChipParts[I]->SetVisibility(false); continue; }
        Chip.Velocity.Z -= 160.f * Dt;
        Chip.Position += Chip.Velocity * Dt;
        ChipParts[I]->SetWorldLocation(Chip.Position);
        ChipParts[I]->SetWorldScale3D(FVector(Chip.Size / 100.f * FMath::Min(1.f, Chip.Remaining / .10f)));
    }
    SetComponentTickEnabled(GetActiveParticleCount() > 0);
}

void UBootFeedbackComponent::ResetFeedback()
{
    for (int32 I = 0; I < Chips.Num(); ++I)
    {
        Chips[I].Remaining = 0.f;
        if (ChipParts[I]) ChipParts[I]->SetVisibility(false);
    }
    for (UAudioComponent* Audio : {WindAudio.Get(), EdgeAudio.Get(), ServoAudio.Get()})
        if (Audio) { Audio->Stop(); Audio->SetPaused(false); }
    StepIndex = 0; TrailTime = 0.f; bHasMovement = false;
    bWasGrounded = true; bWasSliding = bWasWallRunning = false;
    SupportMix=FBootSupportSoundMix();
    CosmeticRandom.Initialize(9503);
    SetComponentTickEnabled(false);
}

void UBootFeedbackComponent::ApplyPreferences(float Intensity, float Sfx, bool bMotion)
{
    EffectIntensity = FMath::IsFinite(Intensity) ? FMath::Clamp(Intensity, 0.f, 1.f) : 0.f;
    SfxVolume = FMath::IsFinite(Sfx) ? FMath::Clamp(Sfx, 0.f, 1.f) : 0.f;
    bMotionEnabled = bMotion;
    if (EffectIntensity <= 0.f)
    {
        for (int32 I = 0; I < Chips.Num(); ++I) { Chips[I].Remaining = 0.f; ChipParts[I]->SetVisibility(false); }
        SetComponentTickEnabled(false);
    }
    if (SfxVolume <= 0.f)
        for (UAudioComponent* Audio : {WindAudio.Get(), EdgeAudio.Get(), ServoAudio.Get()}) if (Audio) Audio->Stop();
}

int32 UBootFeedbackComponent::GetActiveParticleCount() const
{
    int32 Count = 0;
    for (const FChip& Chip : Chips) if (Chip.Remaining > 0.f) ++Count;
    return Count;
}

void UBootFeedbackComponent::SetTraversalEnvelope(const FBootTraversalEnvelope& Envelope)
{
    FText Error;if(FBootTraversalPolicy::Validate(Envelope,Error))TraversalEnvelope=Envelope;
}

FBootSupportSoundMix UBootFeedbackComponent::ComputeSupportSound(const FBootPoseInput& Input,const FBootTraversalEnvelope& Envelope)
{
    FBootSupportSoundMix Mix;
    if(!Envelope.bValid || !FMath::IsFinite(Envelope.RunCmps) || !FMath::IsFinite(Envelope.TemporaryCmps) || Envelope.RunCmps<=0.f || Envelope.TemporaryCmps<=Envelope.RunCmps*.6f)return Mix;
    const float WorldSpeed=FMath::IsFinite(Input.WorldPlanarSpeed)?FMath::Max(0.f,Input.WorldPlanarSpeed):0.f;
    const float SupportSpeed=Input.LocalVelocity.ContainsNaN()?0.f:float(Input.LocalVelocity.Size2D());
    // Actual world speed, including a moving base, drives wind. Support-relative
    // movement alone drives contact loops, with the authoritative envelope ratio.
    Mix.Wind=FMath::Clamp((WorldSpeed-Envelope.RunCmps*.6f)/(Envelope.TemporaryCmps-Envelope.RunCmps*.6f),0.f,1.f);
    if(Input.bGrounded && Input.bSliding && SupportSpeed>100.f)Mix.Slide=.10f;
    if(Input.bWallRunning && Input.bHasWallContact && SupportSpeed>100.f)Mix.Wall=.18f;
    if(Input.bGrounded && !Input.bSliding)Mix.Servo=FMath::Clamp(SupportSpeed/Envelope.MomentumCmps,0.f,1.f)*.045f;
    return Mix;
}
