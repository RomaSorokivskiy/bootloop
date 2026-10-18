#include "World/Threats/BootPopupProcess.h"
#include "Core/BootGameInstance.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundWave.h"
#include "UObject/ConstructorHelpers.h"

namespace BootPopup
{
    constexpr float WarningSeconds = .95f;
    const FVector PanelCentre(60, 0, 50);
    const FVector PanelExtent(360, 300, 4);
    const FVector TopOffset(0, 0, 40);
}

ABootPopupProcess::ABootPopupProcess()
{
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    HostilePanel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisibleHostileVolume"));
    HostilePanel->SetupAttachment(GetRootComponent());
    HostilePanel->SetRelativeLocation(BootPopup::PanelCentre);
    HostilePanel->SetRelativeScale3D(BootPopup::PanelExtent * .02);
    HostilePanel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HostilePanel->SetCastShadow(false);
    HostilePanel->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Material(TEXT("/Game/Assets/V02/M_PixelSurface.M_PixelSurface"));
    static ConstructorHelpers::FObjectFinderOptional<USoundWave> Sound(TEXT("/Game/Assets/Audio/S_Error.S_Error"));
    HostilePanel->SetStaticMesh(Cube.Object);
    HostilePanel->SetMaterial(0, Material.Get());
    HostileRail = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("HostileTravelRail"));
    HostileRail->SetupAttachment(GetRootComponent());
    HostileRail->SetAbsolute(true, true, true);
    HostileRail->SetStaticMesh(Cube.Object);
    HostileRail->SetMaterial(0, Material.Get());
    HostileRail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HostileRail->SetCastShadow(false);
    HostileRail->SetCanEverAffectNavigation(false);
    PopupWarningAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("PopupWarning"));
    PopupWarningAudio->SetupAttachment(GetRootComponent());
    PopupWarningAudio->bAutoActivate = false;
    PopupWarningAudio->bAllowSpatialization = false;
    PopupWarningAudio->bIsUISound = false;
    PopupWarningAudio->SetSound(Sound.Get());
    Tags.Add(TEXT("BootPopupProcess"));
}

void ABootPopupProcess::ConfigurePopup(const FTransform& Frame, FVector LocalHostileEnd,
    FVector RideEnd, float Seconds, int32 Id, int32 Generation)
{
    const FVector Origin = Frame.GetLocation().ContainsNaN() ? FVector::ZeroVector : Frame.GetLocation();
    PopupFrame = FTransform(FRotator(0, Frame.ContainsNaN() ? 0. : Frame.Rotator().Yaw, 0), Origin);
    HostileEnd = LocalHostileEnd.ContainsNaN() ? Origin : PopupFrame.TransformPosition(LocalHostileEnd);
    FriendlyEnd = RideEnd.ContainsNaN() ? Origin : RideEnd;
    OperationSeconds = FMath::IsFinite(Seconds) ? FMath::Clamp(Seconds, .25f, 8.f) : 1.f;
    PopupId = Id;
    HostileRail->ClearInstances();
    HostileRail->SetWorldTransform(FTransform::Identity);
    auto Line = [&](FVector A, FVector B)
    {
        const FVector Delta = B - A;
        if (Delta.IsNearlyZero()) return;
        HostileRail->AddInstance(FTransform(FQuat::FindBetweenNormals(FVector::ForwardVector, Delta.GetSafeNormal()),
            (A + B) * .5, FVector(Delta.Size(), 14, 14) / 100.), true);
    };
    for (double Side : {-1., 1.})
    {
        const FVector Offset = PopupFrame.GetRotation().RotateVector(FVector(0, Side * 415, -65));
        Line(Origin + Offset, HostileEnd + Offset);
    }
    bConfigured = true;
    ResetProcess(Generation);
}

bool ABootPopupProcess::Activate(ACharacter* Rider, bool bInSynced)
{
    if (!bHostile || bConverting || !IsAvailable() || !IsValid(Rider)) return false;
    bConverting = true;
    // Input is resolved before pending hazard consequences. Remove hostile geometry first and
    // rebuild only the safe route from the exact current physical state, preserving the rating.
    bHostile = false;
    PendingContacts.Empty();
    PreviousPlayers.Empty();
    PopupWarningAudio->Stop();
    SetHostileAppearance(false);
    const FTransform CurrentTop(GetActorQuat(), GetActorLocation() + BootPopup::TopOffset);
    Super::ConfigureOriented(CurrentTop, FriendlyEnd, OperationSeconds, PopupId);
    const bool bAccepted = Super::Activate(Rider, bInSynced);
    bConverting = false;
    return bAccepted;
}

void ABootPopupProcess::ResetWindow()
{
    if (!bConfigured) { Super::ResetWindow(); return; }
    Super::ConfigureOriented(PopupFrame, FriendlyEnd, OperationSeconds, PopupId);
    bHostile = true;
    bConverting = false;
    bSecondWarningSound = false;
    HostileElapsed = -TelegraphSeconds;
    PendingContacts.Empty();
    PreviousPlayers.Empty();
    ContactedPlayers.Empty();
    SetHostileAppearance(true);
    SetRideSuspended(!bRunActive);
    SetActorTickEnabled(bRunActive);
    if (bRunActive && GetWorld()->AllowAudioPlayback())
    {
        const auto* GI = GetGameInstance<UBootGameInstance>();
        PopupWarningAudio->SetVolumeMultiplier((GI ? GI->GetOptions().Sfx : .65f) * .25f);
        PopupWarningAudio->SetPitchMultiplier(.8f);
        PopupWarningAudio->Play();
    }
}

bool ABootPopupProcess::IsAvailable() const { return bRunActive && Super::IsAvailable(); }

void ABootPopupProcess::ResetProcess(int32 Generation)
{
    RunGeneration = Generation;
    ResetWindow();
}

void ABootPopupProcess::SetRunActive(bool bActive)
{
    bRunActive = bActive;
    PreviousPlayers.Empty();
    PendingContacts.Empty();
    SetRideSuspended(!bActive);
    if (!bActive) PopupWarningAudio->Stop();
    else if (bConfigured && bHostile && HostileElapsed < 0.f && !PopupWarningAudio->IsPlaying()
        && GetWorld()->AllowAudioPlayback())
    {
        const auto* GI = GetGameInstance<UBootGameInstance>();
        PopupWarningAudio->SetVolumeMultiplier((GI ? GI->GetOptions().Sfx : .65f) * .25f);
        PopupWarningAudio->SetPitchMultiplier(bSecondWarningSound ? 1.3f : .8f);
        PopupWarningAudio->Play();
    }
    SetActorTickEnabled(bActive && bConfigured);
}

void ABootPopupProcess::SetHostileAppearance(bool bInHostile)
{
    HostilePanel->SetVisibility(bInHostile);
    HostileRail->SetVisibility(bInHostile);
    if (!HostileColor) HostileColor = UMaterialInstanceDynamic::Create(HostilePanel->GetMaterial(0), this);
    if (HostileColor)
    {
        HostilePanel->SetMaterial(0, HostileColor);
        HostileRail->SetMaterial(0, HostileColor);
        HostileColor->SetVectorParameterValue(TEXT("FlatColor"), HostileElapsed < 0.f
            ? FLinearColor(1.f, .65f, .12f) : FLinearColor(.86f, .12f, .12f));
    }
}

void ABootPopupProcess::Tick(float DeltaSeconds)
{
    if (!bRunActive || !bConfigured || DeltaSeconds <= 0.f) return;
    if (!bHostile) { Super::Tick(DeltaSeconds); return; }
    const int32 TickGeneration = RunGeneration;
    // One-frame contact deferral gives accepted input from the contact frame precedence. Restores
    // clear the queue; callbacks always retain the generation of the original physical contact.
    TArray<FPendingContact> ReadyContacts;
    for (const FPendingContact& Pending : PendingContacts)
        if (Pending.Frame < GFrameCounter) ReadyContacts.Add(Pending);
    PendingContacts.RemoveAll([](const FPendingContact& Pending) { return Pending.Frame < GFrameCounter; });
    for (const FPendingContact& Pending : ReadyContacts)
    {
        if (RunGeneration != TickGeneration || !bHostile || !bRunActive) return;
        if (Pending.Generation == RunGeneration && Pending.Target.IsValid())
        {
            const FBootOperationKey* CurrentOperation=GetRunOperation();
            if(Pending.Operation.IsSet())
            {
                if(CurrentOperation&&*CurrentOperation==Pending.Operation.GetValue())OnRunThreatContact.Broadcast(Pending.Target.Get(),Pending.Operation.GetValue());
            }
            else if(!CurrentOperation)OnThreatContact.Broadcast(Pending.Target.Get(),Pending.Generation);
        }
    }
    if (RunGeneration != TickGeneration || !bHostile || !bRunActive) return;
    const FVector Before = HostilePanel->GetComponentLocation();
    const float PreviousElapsed = HostileElapsed;
    HostileElapsed += DeltaSeconds;
    if (!bSecondWarningSound && HostileElapsed >= -TelegraphSeconds + .45f)
    {
        bSecondWarningSound = true;
        if (GetWorld()->AllowAudioPlayback()) { PopupWarningAudio->SetPitchMultiplier(1.3f); PopupWarningAudio->Play(); }
    }
    if (HostileElapsed >= 0.f)
    {
        const double Alpha = FMath::Clamp(HostileElapsed / OperationSeconds, 0.f, 1.f);
        const double Ease = Alpha * Alpha * (3. - 2. * Alpha);
        const FVector Target = FMath::Lerp(PopupFrame.GetLocation(), HostileEnd, Ease) - BootPopup::TopOffset;
        UBoxComponent* Solid = CastChecked<UBoxComponent>(GetRootComponent());
        const FVector PreviousPosition = GetActorLocation();
        FHitResult Hit;
        Solid->MoveComponent(Target - PreviousPosition, GetActorQuat(), true, &Hit, MOVECOMP_NoFlags);
        Solid->ComponentVelocity = (GetActorLocation() - PreviousPosition) / DeltaSeconds;
    }
    const float ActiveStartFraction = PreviousElapsed < 0.f ? FMath::Clamp(-PreviousElapsed / DeltaSeconds, 0.f, 1.f) : 0.f;
    for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
    {
        ACharacter* Target = *It;
        const FVector Current = Target->GetActorLocation();
        const FVector* Previous = PreviousPlayers.Find(Target);
        const FVector Start = Previous ? *Previous : Current;
        const UCapsuleComponent* Capsule = Target->GetCapsuleComponent();
        if (HostileElapsed >= 0.f && !ContactedPlayers.Contains(Target)
            && ABootScanProcess::SweptCapsuleContact(Before, HostilePanel->GetComponentLocation(), GetActorQuat(),
                BootPopup::PanelExtent, FMath::Lerp(Start, Current, ActiveStartFraction), Current,
                Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()))
        {
            ContactedPlayers.Add(Target);
            FPendingContact Contact{Target,TickGeneration,GFrameCounter,{}};
            if(const auto* Key=GetRunOperation())Contact.Operation=*Key;
            PendingContacts.Add(Contact);
        }
        PreviousPlayers.Add(Target, Current);
    }
    SetHostileAppearance(true);
}

