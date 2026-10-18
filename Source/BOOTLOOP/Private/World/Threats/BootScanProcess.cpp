#include "World/Threats/BootScanProcess.h"
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

namespace BootScan
{
    constexpr float WarningSeconds = .95f;
    constexpr float SweepSeconds = 1.f;
    constexpr float ClearSeconds = .65f;
    const FVector Extent(8, 300, 100);
}

ABootScanProcess::ABootScanProcess()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    Beam = CreateDefaultSubobject<UBoxComponent>(TEXT("ScanBeam"));
    SetRootComponent(Beam);
    Beam->SetBoxExtent(BootScan::Extent);
    Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Beam->SetGenerateOverlapEvents(false);
    Beam->SetCanEverAffectNavigation(false);
    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisibleScanVolume"));
    Visual->SetupAttachment(Beam);
    Visual->SetRelativeScale3D(BootScan::Extent * .02);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetCastShadow(false);
    WarningRoute = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("WarningSweepOutline"));
    WarningRoute->SetupAttachment(Beam);
    WarningRoute->SetAbsolute(true, true, true);
    WarningRoute->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WarningRoute->SetCastShadow(false);
    WarningRoute->SetCanEverAffectNavigation(false);
    WarningAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("TwoToneScanWarning"));
    WarningAudio->SetupAttachment(Beam);
    WarningAudio->bAutoActivate = false;
    WarningAudio->bAllowSpatialization = false;
    WarningAudio->bIsUISound = false;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Material(TEXT("/Game/Assets/V02/M_PixelSurface.M_PixelSurface"));
    static ConstructorHelpers::FObjectFinderOptional<USoundWave> Sound(TEXT("/Game/Assets/Audio/S_Error.S_Error"));
    Visual->SetStaticMesh(Cube.Object);
    WarningRoute->SetStaticMesh(Cube.Object);
    Visual->SetMaterial(0, Material.Get());
    WarningRoute->SetMaterial(0, Material.Get());
    WarningAudio->SetSound(Sound.Get());
    Tags.Add(TEXT("BootScanProcess"));
}

void ABootScanProcess::BeginPlay()
{
    Super::BeginPlay();
    Color = UMaterialInstanceDynamic::Create(Visual->GetMaterial(0), this);
    if (Color) { Visual->SetMaterial(0, Color); WarningRoute->SetMaterial(0, Color); }
    UpdateAppearance();
}

void ABootScanProcess::Configure(const FTransform& Frame, FVector LocalSweepEnd, int32 GroupId)
{
    const FVector Origin = Frame.GetLocation().ContainsNaN() ? FVector::ZeroVector : Frame.GetLocation();
    const double Yaw = Frame.ContainsNaN() ? 0. : Frame.Rotator().Yaw;
    ScanFrame = FTransform(FRotator(0, Yaw, 0), Origin);
    SweepEnd = LocalSweepEnd.ContainsNaN() ? FVector::ZeroVector : LocalSweepEnd;
    ScanGroupId = GroupId;
    ResetProcess(RunGeneration);
    WarningRoute->ClearInstances();
    WarningRoute->SetWorldTransform(FTransform::Identity);
    auto Line = [&](FVector A, FVector B)
    {
        A = ScanFrame.TransformPosition(A);
        B = ScanFrame.TransformPosition(B);
        const FVector Delta = B - A;
        if (Delta.IsNearlyZero()) return;
        WarningRoute->AddInstance(FTransform(FQuat::FindBetweenNormals(FVector::ForwardVector, Delta.GetSafeNormal()),
            (A + B) * .5, FVector(Delta.Size(), 10, 10) / 100.), true);
    };
    for (double Side : {-1., 1.})
        Line(FVector(0, Side * 300, -100), SweepEnd + FVector(0, Side * 300, -100));
    Line(FVector(0, -300, -100), FVector(0, 300, -100));
    Line(SweepEnd + FVector(0, -300, -100), SweepEnd + FVector(0, 300, -100));
}

void ABootScanProcess::Arm(int32 Generation)
{
    if (!bRunActive) return;
    // Every legal staging approach gets a full warning, including a newly entered branch.
    ResetProcess(Generation);
    Phase = EBootScanPhase::Warning;
    SetActorTickEnabled(true);
    UpdateAppearance();
    if (GetWorld()->AllowAudioPlayback())
    {
        const auto* GI = GetGameInstance<UBootGameInstance>();
        WarningAudio->SetVolumeMultiplier((GI ? GI->GetOptions().Sfx : .65f) * .25f);
        WarningAudio->SetPitchMultiplier(.8f);
        WarningAudio->Play();
    }
}

void ABootScanProcess::ResetProcess(int32 Generation)
{
    RunGeneration = Generation;
    Phase = EBootScanPhase::Dormant;
    PhaseSeconds = 0.f;
    bSecondWarningSound = false;
    PreviousPlayers.Empty();
    ContactedPlayers.Empty();
    WarningAudio->Stop();
    SetActorTransform(ScanFrame, false, nullptr, ETeleportType::TeleportPhysics);
    SetActorTickEnabled(false);
    UpdateAppearance();
}

void ABootScanProcess::SetRunActive(bool bActive)
{
    bRunActive = bActive;
    PreviousPlayers.Empty();
    if (!bActive) WarningAudio->Stop();
    SetActorTickEnabled(bActive && Phase != EBootScanPhase::Dormant);
}

FVector ABootScanProcess::GetBeamLocation() const { return Beam->GetComponentLocation(); }

void ABootScanProcess::UpdateAppearance()
{
    const bool bVisible = Phase != EBootScanPhase::Dormant;
    Visual->SetVisibility(bVisible);
    WarningRoute->SetVisibility(bVisible);
    if (Color) Color->SetVectorParameterValue(TEXT("FlatColor"), Phase == EBootScanPhase::Sweep
        ? FLinearColor(.86f, .12f, .12f) : FLinearColor(1.f, .65f, .12f));
}

bool ABootScanProcess::SweptCapsuleContact(FVector BeamBefore, FVector BeamAfter, FQuat Yaw,
    FVector HalfExtent, FVector PlayerBefore, FVector PlayerAfter, float Radius, float HalfHeight)
{
    if (BeamBefore.ContainsNaN() || BeamAfter.ContainsNaN() || PlayerBefore.ContainsNaN()
        || PlayerAfter.ContainsNaN() || HalfExtent.ContainsNaN() || Yaw.ContainsNaN()
        || !FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius <= 0 || HalfHeight < Radius)
        return false;
    const FVector A = Yaw.UnrotateVector(PlayerBefore - BeamBefore);
    const FVector B = Yaw.UnrotateVector(PlayerAfter - BeamAfter);
    HalfExtent = HalfExtent.GetAbs();
    // Expand by the capsule's central segment only in Z; retain its round corners in XY.
    HalfExtent.Z += HalfHeight - Radius;
    auto DistanceSquared = [&](double T)
    {
        const FVector P = FMath::Lerp(A, B, T).GetAbs() - HalfExtent;
        return FMath::Square(FMath::Max(0., P.X)) + FMath::Square(FMath::Max(0., P.Y))
            + FMath::Square(FMath::Max(0., P.Z));
    };
    double Lo = 0., Hi = 1.;
    // Distance to a convex box along a segment is convex. Minimize it continuously,
    // so opposite fast motion cannot jump across the entire beam between samples.
    for (int32 I = 0; I < 36; ++I)
    {
        const double Left = Lo + (Hi - Lo) / 3., Right = Hi - (Hi - Lo) / 3.;
        if (DistanceSquared(Left) <= DistanceSquared(Right)) Hi = Right; else Lo = Left;
    }
    const double Minimum = FMath::Min3(DistanceSquared(0.), DistanceSquared(1.), DistanceSquared((Lo + Hi) * .5));
    return Minimum <= FMath::Square(static_cast<double>(Radius)) + .001;
}

void ABootScanProcess::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bRunActive || Phase == EBootScanPhase::Dormant || DeltaSeconds <= 0.f) return;
    const int32 TickGeneration = RunGeneration;
    const TOptional<FBootOperationKey> TickOperation=RunOperation;
    const FVector Before = GetBeamLocation();
    const EBootScanPhase PreviousPhase = Phase;
    const float OldSeconds = PhaseSeconds;
    PhaseSeconds += DeltaSeconds;
    float ActiveStartFraction = 0.f;
    float ActiveEndFraction = 1.f;
    bool bSweepThisFrame = PreviousPhase == EBootScanPhase::Sweep;
    if (Phase == EBootScanPhase::Warning)
    {
        if (!bSecondWarningSound && PhaseSeconds >= .45f)
        {
            bSecondWarningSound = true;
            if (GetWorld()->AllowAudioPlayback()) { WarningAudio->SetPitchMultiplier(1.3f); WarningAudio->Play(); }
        }
        if (PhaseSeconds >= TelegraphSeconds)
        {
            ActiveStartFraction = FMath::Clamp((TelegraphSeconds - OldSeconds) / DeltaSeconds, 0.f, 1.f);
            PhaseSeconds -= TelegraphSeconds;
            Phase = EBootScanPhase::Sweep;
            bSweepThisFrame = true;
        }
    }
    if (Phase == EBootScanPhase::Sweep)
    {
        const double Alpha = FMath::Clamp(PhaseSeconds / SweepDurationSeconds, 0.f, 1.f);
        SetActorLocation(ScanFrame.TransformPosition(SweepEnd * Alpha));
        if (PhaseSeconds >= SweepDurationSeconds)
        {
            ActiveEndFraction = FMath::Clamp((SweepDurationSeconds - (PreviousPhase == EBootScanPhase::Sweep ? OldSeconds : 0.f))
                / DeltaSeconds + ActiveStartFraction, ActiveStartFraction, 1.f);
            PhaseSeconds -= SweepDurationSeconds;
            Phase = EBootScanPhase::Clear;
        }
    }
    else if (Phase == EBootScanPhase::Clear && PhaseSeconds >= BootScan::ClearSeconds)
    {
        Phase = EBootScanPhase::Dormant;
        SetActorTickEnabled(false);
    }
    TArray<TWeakObjectPtr<ACharacter>> Hits;
    for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
    {
        ACharacter* Target = *It;
        const FVector Current = Target->GetActorLocation();
        const FVector* Previous = PreviousPlayers.Find(Target);
        const FVector Start = Previous ? *Previous : Current;
        const UCapsuleComponent* Capsule = Target->GetCapsuleComponent();
        if (bSweepThisFrame && !ContactedPlayers.Contains(Target) && SweptCapsuleContact(Before, GetBeamLocation(),
            ScanFrame.GetRotation(), BootScan::Extent, FMath::Lerp(Start, Current, ActiveStartFraction),
            FMath::Lerp(Start, Current, ActiveEndFraction), Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()))
        {
            ContactedPlayers.Add(Target);
            Hits.Add(Target);
        }
        PreviousPlayers.Add(Target, Current);
    }
    UpdateAppearance();
    // Consumers may restore/destroy actors: dispatch after iterating and recheck generation each time.
    for (const TWeakObjectPtr<ACharacter>& Target : Hits)
    {
        if (RunGeneration != TickGeneration || !bRunActive) break;
        if(RunOperation.IsSet()!=TickOperation.IsSet()||(RunOperation.IsSet()&&!(RunOperation.GetValue()==TickOperation.GetValue())))break;
        if(Target.IsValid())
        {
            if(TickOperation.IsSet())OnRunThreatContact.Broadcast(Target.Get(),TickOperation.GetValue());
            else OnThreatContact.Broadcast(Target.Get(),TickGeneration);
        }
    }
}


