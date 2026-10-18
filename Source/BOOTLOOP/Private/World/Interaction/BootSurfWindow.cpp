#include "World/Interaction/BootSurfWindow.h"

#include "Components/BoxComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Core/BootGameInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace BootSurf
{
    constexpr double HalfThickness = 40.;
    const FVector TopOffset(0, 0, HalfThickness);
    const FLinearColor Palette[] = {
        FLinearColor(.79f, .77f, .63f), // warm aged plastic
        FLinearColor(.022f, .045f, .075f), // deep navy joints and inset
        FLinearColor(1.f, .56f, .08f), // amber draggable titlebar
        FLinearColor(.25f, .91f, .69f), // mint navigation
        FLinearColor(.96f, .94f, .79f), // highlighted bevels
    };
}

ABootSurfWindow::ABootSurfWindow()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    Platform = CreateDefaultSubobject<UBoxComponent>(TEXT("SolidMovingWindow"));
    SetRootComponent(Platform);
    Platform->SetBoxExtent(FVector(500, 375, BootSurf::HalfThickness));
    Platform->SetMobility(EComponentMobility::Movable);
    Platform->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Platform->SetCollisionObjectType(ECC_WorldDynamic);
    Platform->SetGenerateOverlapEvents(false);
    Platform->CanCharacterStepUpOn = ECB_Yes;
    Platform->SetCanEverAffectNavigation(false);
    Servo=CreateDefaultSubobject<UAudioComponent>(TEXT("WindowServo"));
    Servo->SetupAttachment(Platform); Servo->bAutoActivate=false; Servo->bAllowSpatialization=false;
    static ConstructorHelpers::FObjectFinder<USoundWave> Motor(TEXT("/Game/Assets/AudioV02/S_ServoLoop.S_ServoLoop"));
    Servo->SetSound(Motor.Object);
    Tags.Add(TEXT("BootSurfWindow"));

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Pixel(TEXT("/Game/Assets/V02/M_PixelSurface.M_PixelSurface"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Fallback(TEXT("/Game/Assets/PrototypeMaterials/M_FlatMaster.M_FlatMaster"));
    SurfaceMaterial = Pixel.Get() ? Pixel.Get() : Fallback.Get();

    for (int32 I = 0; I < UE_ARRAY_COUNT(BootSurf::Palette); ++I)
    {
        auto MakeParts = [&](const FString& Prefix)
        {
            auto* Parts = CreateDefaultSubobject<UInstancedStaticMeshComponent>(
                FName(*FString::Printf(TEXT("%s%d"), *Prefix, I)));
            Parts->SetupAttachment(Platform);
            Parts->SetStaticMesh(Cube.Object);
            Parts->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Parts->SetGenerateOverlapEvents(false);
            Parts->SetCanEverAffectNavigation(false);
            Parts->SetCastShadow(false);
            Parts->SetMobility(EComponentMobility::Movable);
            return Parts;
        };
        BodyParts.Add(MakeParts(TEXT("WindowPlastic")));
        auto* Route = MakeParts(TEXT("FixedRail"));
        Route->SetAbsolute(true, true, true);
        RouteParts.Add(Route);
    }
}

void ABootSurfWindow::BeginPlay()
{
    Super::BeginPlay();
    if (Colors.IsEmpty())
    {
        for (int32 I = 0; I < UE_ARRAY_COUNT(BootSurf::Palette); ++I)
        {
            UMaterialInstanceDynamic* Color = UMaterialInstanceDynamic::Create(SurfaceMaterial, this);
            if (Color) Color->SetVectorParameterValue(TEXT("FlatColor"), BootSurf::Palette[I]);
            Colors.Add(Color);
            BodyParts[I]->SetMaterial(0, Color);
            RouteParts[I]->SetMaterial(0, Color);
        }
    }
    BuildVisuals();
}

void ABootSurfWindow::Configure(FVector InStartTop, FVector InEndTop, float Seconds, int32 Id)
{
    ConfigureOriented(FTransform(InStartTop), InEndTop, Seconds, Id);
}

void ABootSurfWindow::ConfigureOriented(const FTransform& InStartTop, FVector InEndTop, float Seconds, int32 Id)
{
    StartTop = InStartTop.GetLocation().ContainsNaN() ? FVector::ZeroVector : InStartTop.GetLocation();
    EndTop = InEndTop.ContainsNaN() ? StartTop : InEndTop;
    const double Yaw = InStartTop.ContainsNaN() ? 0. : InStartTop.Rotator().Yaw;
    WindowRotation = FQuat(FRotator(0, Yaw, 0));
    TravelSeconds = FMath::IsFinite(Seconds) ? FMath::Clamp(Seconds, .25f, 8.f) : 1.f;
    WindowId = Id;
    // Configuration resets base ride state without dispatching a derived process lifecycle reset.
    ABootSurfWindow::ResetWindow();
    BuildVisuals();
}

void ABootSurfWindow::BuildVisuals()
{
    for (UInstancedStaticMeshComponent* Parts : BodyParts) Parts->ClearInstances();
    for (UInstancedStaticMeshComponent* Parts : RouteParts)
    {
        Parts->ClearInstances();
        Parts->SetWorldTransform(FTransform::Identity);
    }
    auto Box = [&](FVector Position, FVector Size, int32 Color)
    {
        BodyParts[Color]->AddInstance(FTransform(FQuat::Identity, Position, Size / 100.));
    };
    if(bFlowRide)
    {
        Box(FVector(-800,0,-26),FVector(200,750,28),2);
        Box(FVector(-600,0,-11),FVector(200,750,58),3);
    }
    // Thick GUI casing; the continuous collision top stays flat and predictable.
    Box(FVector(0, 0, -2), FVector(1000, 750, 76), 1);
    Box(FVector(0, 0, 30), FVector(984, 734, 20), 0);
    Box(FVector(0, 0, 40), FVector(900, 644, 2), 1);
    Box(FVector(28, 0, 41), FVector(792, 620, 2), 0);
    Box(FVector(0, -364, 32), FVector(980, 14, 14), 4);
    Box(FVector(-489, 0, 32), FVector(14, 718, 14), 4);
    Box(FVector(0, 364, 25), FVector(984, 16, 22), 1);
    Box(FVector(489, 0, 25), FVector(16, 730, 22), 1);
    // Broad amber titlebar faces an approaching runner. Three dark grip marks are the original icon.
    Box(FVector(-416, 0, 43), FVector(80, 680, 6), 2);
    for (int32 I = 0; I < 3; ++I)
        Box(FVector(-416, -42 + I * 42, 47), FVector(38, 12, 2), 1);
    Box(FVector(-416, -302, 47), FVector(38, 34, 2), 4);
    Box(FVector(-416, 302, 47), FVector(38, 34, 2), 1);
    // Segmented scroll edge and nested task panel remain readable at the low-res world raster.
    Box(FVector(30, 318, 43), FVector(786, 18, 4), 1);
    Box(FVector(-160, 318, 46), FVector(220, 14, 2), 4);
    Box(FVector(52, 0, 43), FVector(516, 330, 3), 4);
    Box(FVector(64, 0, 45), FVector(492, 306, 2), 0);
    for (int32 I = 0; I < 5; ++I)
        Box(FVector(-96 + I * 70, 0, 47), FVector(34, 202 - I * 22, 2), I == 4 ? 3 : 1);

    auto RouteBox = [&](FVector Position, FVector Size, int32 Color, FQuat Rotation = FQuat::Identity)
    {
        RouteParts[Color]->AddInstance(FTransform(Rotation, Position, Size / 100.), true);
    };
    auto Line = [&](FVector A, FVector B, double Width, int32 Color)
    {
        const FVector Delta = B - A;
        const double Length = Delta.Size();
        if (Length > UE_KINDA_SMALL_NUMBER)
            RouteBox((A + B) * .5, FVector(Length, Width, Width), Color,
                FQuat::FindBetweenNormals(FVector::ForwardVector, Delta / Length));
    };
    // Rails are anchored to the world, not children that slide away with their platform.
    for (double Side : {-1., 1.})
    {
        const FVector A = StartTop + WindowRotation.RotateVector(FVector(0, Side * 425, -75));
        const FVector B = EndTop + WindowRotation.RotateVector(FVector(0, Side * 425, -75));
        Line(A, B, 24, 1);
        Line(A + FVector(0, 0, 14), B + FVector(0, 0, 14), 10, 3);
        RouteBox(A, FVector(50, 50, 80), 2, WindowRotation);
        RouteBox(B, FVector(50, 50, 80), 3, WindowRotation);
    }
    // A solid mint outline shows B before the interaction; no invisible floor or hazard collider.
    const FVector Ghost = EndTop + FVector(0, 0, 3);
    RouteBox(Ghost + WindowRotation.RotateVector(FVector(-500, 0, 0)), FVector(12, 750, 12), 3, WindowRotation);
    RouteBox(Ghost + WindowRotation.RotateVector(FVector(500, 0, 0)), FVector(12, 750, 12), 3, WindowRotation);
    RouteBox(Ghost + WindowRotation.RotateVector(FVector(0, -375, 0)), FVector(1000, 12, 12), 3, WindowRotation);
    RouteBox(Ghost + WindowRotation.RotateVector(FVector(0, 375, 0)), FVector(1000, 12, 12), 3, WindowRotation);
    RouteBox(Ghost + FVector(0, 0, -42), FVector(14, 300, 14), 1, WindowRotation);
}

bool ABootSurfWindow::Activate(ACharacter* Rider, bool bInSynced)
{
    if (!IsAvailable() || !IsValid(Rider)) return false;
    ActivatingRider = Rider;
    Rider->LandedDelegate.AddUniqueDynamic(this,&ABootSurfWindow::HandleRiderLanded);
    bSynced = bInSynced;
    bActivated = true;
    bMoving = true;
    Rider->GetCharacterMovement()->AddTickPrerequisiteActor(this);
    ObserveRider();
    SetChargeAppearance(bSynced);
    SetActorTickEnabled(true);
    if(GetWorld()->AllowAudioPlayback())
    {
        const auto* GI=GetGameInstance<UBootGameInstance>();
        Servo->SetVolumeMultiplier((GI?GI->GetOptions().Sfx:.65f)*.14f); Servo->Play();
    }
    return true;
}

bool ABootSurfWindow::RiderIsAboard() const
{
    if(!ActivatingRider.IsValid())return false;
    const auto* Base=ActivatingRider->GetMovementBase();
    return Base==Platform || (bFlowRide&&Base&&Base->GetOwner()==this);
}

void ABootSurfWindow::SetFlowRide(bool bEnabled)
{
    bFlowRide=bEnabled;
    if(bEnabled&&FlowEntrySteps.IsEmpty())
    {
        // Visible 30/30/22cm steps travel with the window. They can be boarded
        // at any run speed without a speed-specific jump or a stationary wait.
        for(int32 I=0;I<2;++I)
        {
            auto* Step=NewObject<UBoxComponent>(this);AddInstanceComponent(Step);Step->SetupAttachment(Platform);
            Step->SetBoxExtent(FVector(100,375,I==0?14.:29.));Step->SetRelativeLocation(FVector(I==0?-800.:-600.,0,I==0?-26.:-11.));
            Step->SetMobility(EComponentMobility::Movable);Step->SetCollisionProfileName(TEXT("BlockAll"));Step->CanCharacterStepUpOn=ECB_Yes;
            Step->SetGenerateOverlapEvents(false);Step->SetCanEverAffectNavigation(false);Step->RegisterComponent();FlowEntrySteps.Add(Step);
        }
    }
    for(UBoxComponent* Step:FlowEntrySteps)Step->SetCollisionEnabled(bEnabled?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    BuildVisuals();
}

void ABootSurfWindow::ObserveRider(float DeltaSeconds, const FVector& CarriedDelta)
{
    ACharacter* Rider = ActivatingRider.Get();
    if (!Rider) return;
    const bool bAboard = RiderIsAboard();
    if (bFlowRide && bActivated && bAboard) bCarried=true;
    // Sweeping an ascending platform must not collide with the capsule that it is supporting.
    Platform->IgnoreActorWhenMoving(Rider, bAboard);
    if (bAboard && !CarriedDelta.IsNearlyZero())
    {
        CarriedSeconds += DeltaSeconds;
        CarriedDistance += CarriedDelta.Size();
        if (CarriedSeconds >= .035f && CarriedDistance >= 8.f) bCarried = true;
    }
    if (bWasAboard && !bAboard && bCarried) bDeparted = true;
    if(bCarried&&bDeparted&&!bAboard&&Rider->GetCharacterMovement()->IsMovingOnGround())bDepartureLanded=true;
    bWasAboard = bAboard;
}

void ABootSurfWindow::HandleRiderLanded(const FHitResult& Hit)
{
    if (bFlowRide && bActivated && Hit.GetComponent() && Hit.GetComponent()->GetOwner()==this)
    {
        // A buffered manual hop can depart inside this landing callback, before
        // another platform tick. Record the actual contact, never a remote SNAP.
        bCarried=true;bWasAboard=true;
    }
    // The movement event catches even a queued wheel-hop landing that lasts less than a world tick.
    if(bCarried&&Hit.GetComponent()&&Hit.GetComponent()->GetOwner()!=this){bDeparted=true;bDepartureLanded=true;}
}
void ABootSurfWindow::SetPreparedLandingComponent(UPrimitiveComponent* Component)
{
    PreparedLandingComponent=Component;
}

void ABootSurfWindow::Tick(float DeltaSeconds)
{
    if (bRideSuspended) return;
    Super::Tick(DeltaSeconds);
    ObserveRider();
    if (bMoving && DeltaSeconds > UE_SMALL_NUMBER)
    {
        TravelElapsed = FMath::Min(TravelElapsed + DeltaSeconds, TravelSeconds);
        const double Alpha = TravelElapsed / TravelSeconds;
        // Smoothstep gives a visible servo start/stop and predictable positive inertia in the middle.
        const double Ease = Alpha * Alpha * (3. - 2. * Alpha);
        const FVector Target = FMath::Lerp(StartTop, EndTop, Ease) - BootSurf::TopOffset;
        const FVector Before = GetActorLocation();
        FVector RequestedDelta = Target - Before;
        bool bRiderBlocked = false;
        if (RiderIsAboard())
        {
            // The platform ignores its based rider during its own root sweep. Preflight that rider's
            // capsule separately so an upward/sideways servo move cannot squeeze it into a ceiling.
            ACharacter* Rider = ActivatingRider.Get();
            UCapsuleComponent* Capsule = Rider->GetCapsuleComponent();
            FCollisionQueryParams Params(SCENE_QUERY_STAT(BootSurfRiderClearance), false, this);
            Params.AddIgnoredActor(Rider);
            FHitResult RiderHit;
            if (GetWorld()->SweepSingleByProfile(RiderHit, Rider->GetActorLocation(),
                Rider->GetActorLocation() + RequestedDelta, Capsule->GetComponentQuat(),
                Capsule->GetCollisionProfileName(), FCollisionShape::MakeCapsule(
                    Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params))
            {
                RequestedDelta *= FMath::Max(0.f, RiderHit.Time - .002f);
                bRiderBlocked = true;
            }
        }
        FHitResult Hit;
        Platform->MoveComponent(RequestedDelta, GetActorQuat(), true, &Hit, MOVECOMP_NoFlags);
        const FVector Delta = GetActorLocation() - Before;
        Platform->ComponentVelocity = Delta / DeltaSeconds;
        ObserveRider(DeltaSeconds, Delta);
        if (Hit.bBlockingHit || bRiderBlocked || TravelElapsed >= TravelSeconds)
        {
            bMoving = false;
            // The authored platform ends face-to-face with its named floor. Chaos can report
            // that terminal contact a fraction early. Only accept this exact prepared dock,
            // within 1 cm of the endpoint, with no penetration or rider-clearance obstruction.
            const bool bPreparedDockTouch=RunOperation.IsSet()&&PreparedLandingComponent.IsValid()
                &&Hit.GetComponent()==PreparedLandingComponent.Get()&&!Hit.bStartPenetrating
                &&FVector::DistSquared(GetActorLocation()+BootSurf::TopOffset,EndTop)<=1.
                &&FMath::Abs(Hit.ImpactNormal.Z)<.1
                &&FVector::DotProduct(Hit.ImpactNormal,(EndTop-StartTop).GetSafeNormal())<-.9;
            bBlocked = (Hit.bBlockingHit&&!bPreparedDockTouch) || bRiderBlocked;
            DockedAt = GetWorld()->GetTimeSeconds();
            // A dock is a stationary support. Do not leave a stale last-step velocity for a later hop.
            Platform->ComponentVelocity = FVector::ZeroVector;
            Servo->Stop();
        }
    }
    else Platform->ComponentVelocity = FVector::ZeroVector;
    if (bSynced && DockedAt >= 0. && GetWorld()->GetTimeSeconds() > DockedAt + 1.)
        SetChargeAppearance(false);
}

FVector ABootSurfWindow::GetHandleLocation() const
{
    return GetActorTransform().TransformPosition(FVector(-416, 0, 47));
}

bool ABootSurfWindow::HasCarried(ACharacter* Rider) const
{
    return IsValid(Rider) && Rider == ActivatingRider.Get() && bCarried;
}

bool ABootSurfWindow::HasActiveCommitment() const
{
    if(!bActivated||bCommitted||bBlocked||bDepartureLanded)return false;
    if(bMoving||RiderIsAboard())return true;
    const ACharacter* Rider=ActivatingRider.Get();
    return bCarried&&bDeparted&&Rider&&Rider->GetCharacterMovement()->IsFalling();
}
double ABootSurfWindow::GetPeakTravelSpeedCmps() const
{
    return TravelSeconds>0.f?1.5*FVector::Distance(StartTop,EndTop)/TravelSeconds:0.;
}

bool ABootSurfWindow::ConsumeDepartureBoost(ACharacter* Rider)
{
    ObserveRider();
    if (!HasCarried(Rider) || !bDeparted || RiderIsAboard() || (!bSynced && !bFlowRide) || bChargeConsumed || bBlocked)
        return false;
    if (!bFlowRide && DockedAt >= 0. && GetWorld()->GetTimeSeconds() > DockedAt + 1.) return false;
    bChargeConsumed = true;
    SetChargeAppearance(false);
    return true;
}

bool ABootSurfWindow::HasChargeFor(ACharacter* Rider) const
{
    return HasCarried(Rider) && (bSynced || bFlowRide) && !bChargeConsumed && !bBlocked
        && (bFlowRide || DockedAt<0. || GetWorld()->GetTimeSeconds()<=DockedAt+1.);
}

bool ABootSurfWindow::CommitRide(ACharacter* Rider)
{
    ObserveRider();
    if (!HasCarried(Rider) || !bDeparted || RiderIsAboard() || bCommitted || bBlocked) return false;
    bCommitted = true;
    return true;
}

void ABootSurfWindow::SetChargeAppearance(bool bCharged)
{
    if (Colors.IsValidIndex(2) && Colors[2])
        Colors[2]->SetVectorParameterValue(TEXT("FlatColor"), bCharged ? BootSurf::Palette[3] : BootSurf::Palette[2]);
}

void ABootSurfWindow::ReleaseRiderPrerequisite()
{
    if (ACharacter* Rider = ActivatingRider.Get())
    {
        Rider->LandedDelegate.RemoveDynamic(this,&ABootSurfWindow::HandleRiderLanded);
        Platform->IgnoreActorWhenMoving(Rider, false);
        if (UCharacterMovementComponent* Movement = Rider->GetCharacterMovement())
            Movement->RemoveTickPrerequisiteActor(this);
    }
}

void ABootSurfWindow::ResetWindow()
{
    Servo->Stop();
    ReleaseRiderPrerequisite();
    ActivatingRider.Reset();
    bRideSuspended = false;
    bDepartureLanded = false;
    bActivated = bMoving = bBlocked = bSynced = bWasAboard = bCarried = bDeparted = bChargeConsumed = bCommitted = false;
    TravelElapsed = CarriedSeconds = CarriedDistance = 0.f;
    DockedAt = -1.;
    Platform->ComponentVelocity = FVector::ZeroVector;
    SetActorLocationAndRotation(StartTop - BootSurf::TopOffset, WindowRotation, false, nullptr, ETeleportType::TeleportPhysics);
    SetChargeAppearance(false);
    SetActorTickEnabled(false);
}

void ABootSurfWindow::SetRideSuspended(bool bSuspended)
{
    if (bRideSuspended == bSuspended) return;
    bRideSuspended = bSuspended;
    Platform->ComponentVelocity = FVector::ZeroVector;
    SetActorTickEnabled(!bSuspended && bActivated);
    if (bSuspended) Servo->Stop();
    else if (bMoving && GetWorld()->AllowAudioPlayback())
    {
        const auto* GI = GetGameInstance<UBootGameInstance>();
        Servo->SetVolumeMultiplier((GI ? GI->GetOptions().Sfx : .65f) * .14f);
        Servo->Play();
    }
}

void ABootSurfWindow::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ReleaseRiderPrerequisite();
    Super::EndPlay(EndPlayReason);
}


