#include "World/Generation/BootTrack.h"
#include "World/Presentation/BootDesktopBackdrop.h"
#include "Music/BootMusicTypes.h"
#include "Movement/BootTraversalEnvelope.h"
#include "Player/BootRunner.h"
#include "World/Interaction/BootSurfWindow.h"
#include "Movement/BootMovementComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ABootTrackChunk::ABootTrackChunk()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Text(TEXT("/Engine/EngineMaterials/UnlitText.UnlitText"));
    TextMaterial = Text.Object;
    auto Mesh = [&](FName Name, bool Solid)
    {
        auto* Comp = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
        Comp->SetupAttachment(RootComponent);
        Comp->SetStaticMesh(Cube.Object);
        Comp->SetMobility(EComponentMobility::Movable);
        Comp->SetCastShadow(false);
        Comp->SetCollisionProfileName(Solid ? TEXT("BlockAll") : TEXT("NoCollision"));
        Comp->SetCanEverAffectNavigation(false);
        Comp->SetCullDistances(0, 19000);
        return Comp;
    };
    FloorCollision = Mesh(TEXT("Walkable"), true);
    BeamCollision = Mesh(TEXT("LowCeilings"), true);
    for (int32 I = 0; I < 10; ++I) Decor.Add(Mesh(FName(*FString::Printf(TEXT("Color%d"), I)), false));
}

void ABootTrackChunk::Box(FVector Position, FVector Size, int32 Material, bool bSolid)
{
    UInstancedStaticMeshComponent* Comp = bSolid ? (Material == 3 ? BeamCollision : FloorCollision) : Decor[Material];
    Comp->AddInstance(FTransform(FQuat::Identity, Position, Size / 100.));
}

void ABootTrackChunk::Floor(double Start, double End, double Width, double Y)
{
    const double Length = End - Start;
    Box(FVector((Start + End) / 2, Y, -21), FVector(Length, Width, 42), 0, true);
    Box(FVector((Start + End) / 2, Y, -55), FVector(Length, Width + 16, 26), 2);
    Box(FVector((Start + End) / 2, Y - Width / 2, -15), FVector(Length, 12, 45), 1);
    Box(FVector((Start + End) / 2, Y + Width / 2, -15), FVector(Length, 12, 45), 9);
    Box(FVector((Start + End) / 2, Y - Width / 2 + 5, 2), FVector(Length, 8, 4), 4);
    Box(FVector((Start + End) / 2, Y + Width / 2 - 5, 2), FVector(Length, 8, 4), 2);
}

void ABootTrackChunk::Sign(const FString& Text, FVector Position, float Size)
{
    auto* Label = NewObject<UTextRenderComponent>(this);
    AddInstanceComponent(Label);
    Label->SetupAttachment(RootComponent);
    Label->SetRelativeLocation(Position);
    Label->SetRelativeRotation(FRotator(0, 180, 0));
    Label->SetText(FText::FromString(Text));
    Label->SetTextMaterial(TextMaterial);
    Label->SetTextRenderColor(FColor(238, 242, 220));
    Label->SetWorldSize(Size);
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetVerticalAlignment(EVRTA_TextCenter);
    Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Label->SetCullDistance(5500);
    Label->RegisterComponent();
}

void ABootTrackChunk::BuildChunk(int32 InType, int32 InIndex, const TArray<TObjectPtr<UMaterialInstanceDynamic>>& Mats)
{
    Type = InType; Index = InIndex;
    for (int32 I = 0; I < Decor.Num(); ++I) Decor[I]->SetMaterial(0, Mats[I]);
    FloorCollision->SetMaterial(0, Mats[0]);
    BeamCollision->SetMaterial(0, Mats[3]);
    if (Type == 1)
    {
        Floor(0, 850); Floor(1150, 1800);
        for (int32 I = 0; I < 3; ++I) Box(FVector(680 + I * 55, 0, 3), FVector(22, 930, 6), 5);
        Box(FVector(1180, 0, 3), FVector(30, 930, 6), 7);
    }
    else if (Type == 5)
    {
        Floor(0, 540); Floor(800, 1220); Floor(1480, 1800);
        for (double X : {400., 460., 1080., 1140.}) Box(FVector(X, 0, 3), FVector(20, 930, 6), 5);
    }
    else if (Type == 3)
    {
        Floor(0, 500); Floor(500, 1000, 700, -90); Floor(1000, 1400, 700, 90); Floor(1400, 1800);
        for (int32 I = 0; I < 5; ++I) Box(FVector(550 + I * 210, -90 + I * 35, 4), FVector(50, 26, 8), 7);
    }
    else Floor(0, 1800);
    // Header tabs and inset path markers are decorative; collision stays predictable.
    Box(FVector(70, 0, 3), FVector(70, 970, 6), 3);
    for (int32 I = 0; I < 3; ++I) Box(FVector(70, 385 + I * 30, 8), FVector(36, 20, 4), 4);
    if (Type == 2)
    {
        Box(FVector(1000, 0, 220), FVector(200, 1000, 220), 3, true);
        Box(FVector(890, 0, 228), FVector(14, 1020, 250), 2);
        Box(FVector(878, 0, 326), FVector(12, 990, 36), 4);
        Sign(TEXT("LOW MEMORY / CTRL"), FVector(874, 0, 268), 45);
        for (double X : {560., 640., 720.}) Box(FVector(X, 0, 4), FVector(24, 380, 8), 5);
    }
    if (Type == 4)
    {
        FatalBounds = FBox(FVector(840, -470, 0), FVector(1080, -70, 140));
        Box(FVector(960, -270, 48), FVector(240, 400, 96), 6);
        Box(FVector(832, -270, 54), FVector(8, 380, 76), 2);
        Sign(TEXT("X"), FVector(822, -270, 58), 70);
        Sign(TEXT("FATAL / KEEP RIGHT"), FVector(700, 0, 310), 46);
        for (double X : {550., 670., 790., 1150., 1300.}) Box(FVector(X, 240, 5), FVector(60, 55, 10), 7);
    }
    // Floating file windows sit safely outside the playable corridor.
    const double Side = Index % 2 ? -1. : 1.;
    const double Y = Side * (1450. + (Index % 3) * 350.);
    Box(FVector(1450, Y, -30), FVector(50, 920, 510), 2);
    Box(FVector(1418, Y, -20), FVector(18, 890, 480), 0);
    Box(FVector(1404, Y, 175), FVector(14, 870, 65), 3);
    for (int32 I = 0; I < 3; ++I)
    {
        Box(FVector(1400, Y - 290 + I * 240, 30), FVector(20, 130, 115), 5);
        Box(FVector(1392, Y - 320 + I * 240, 95), FVector(18, 70, 25), 5);
        Box(FVector(1390, Y - 290 + I * 240, -100), FVector(12, 150, 12), 1);
    }
    static const TCHAR* Titles[] = {TEXT("SHELL.EXE"), TEXT("JUMP / SPACE + WHEEL"), TEXT("DIALOG.SYS"),
        TEXT("EXPLORER / FOLLOW PATH"), TEXT("ERROR HANDLER"), TEXT("DISK FRAGMENT / JUMP")};
    Sign(FString::Printf(TEXT("%02d  /  %s"), Index + 1, Titles[Type]), FVector(1250, Y, 174), 46);
    // Guideposts mark safe edges without blocking lateral movement.
    for (double X : {250., 1550.})
        for (double Edge : {-560., 560.})
        {
            Box(FVector(X, Edge, 35), FVector(18, 18, 70), 2);
            Box(FVector(X, Edge, 75), FVector(32, 32, 14), Type == 1 || Type == 5 ? 5 : 7);
        }
}

bool ABootTrackChunk::IsFatal(FVector Center, float Radius, float HalfHeight) const
{
    if (!FatalBounds.IsValid) return false;
    Center -= GetActorLocation();
    const FVector Closest = FatalBounds.GetClosestPointTo(Center);
    const FVector2D Delta(Center.X - Closest.X, Center.Y - Closest.Y);
    return Delta.SizeSquared() < FMath::Square(Radius) && Center.Z - HalfHeight < FatalBounds.Max.Z
        && Center.Z + HalfHeight > FatalBounds.Min.Z;
}

ABootTrack::ABootTrack()
{
    PrimaryActorTick.bCanEverTick = false;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Assets/PrototypeMaterials/M_FlatMaster.M_FlatMaster"));
    Master = Material.Object;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Sphere = Mesh.Object;
}

void ABootTrack::BeginPlay()
{
    Super::BeginPlay();
    const FColor Colors[] = {FColor(203,210,205), FColor(104,124,122), FColor(20,37,46), FColor(25,45,115),
        FColor(238,242,220), FColor(245,205,96), FColor(220,65,65), FColor(100,201,167), FColor(10,79,85), FColor(56,84,81)};
    for (const FColor Color : Colors)
    {
        UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(Master, this);
        Mat->SetVectorParameterValue(TEXT("FlatColor"), FLinearColor(Color));
        Materials.Add(Mat);
    }
    Navy = FLinearColor(Colors[3]);
    AStaticMeshActor* Sky = GetWorld()->SpawnActor<AStaticMeshActor>();
    Sky->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetStaticMeshComponent()->SetStaticMesh(Sphere);
    Sky->GetStaticMeshComponent()->SetMaterial(0, Materials[8]);
    Sky->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Sky->GetStaticMeshComponent()->SetCastShadow(false);
    Sky->SetActorScale3D(FVector(1800));
    Background = Sky;
}

void ABootTrack::UpdatePresentationOrigin(const FVector& Origin)
{
    if(Background&&!Origin.ContainsNaN())Background->SetActorLocation(Origin);
}

void ABootTrack::ResetTrack(int32 Seed)
{
    for (ABootTrackChunk* Chunk : Chunks) if (Chunk) Chunk->Destroy();
    Chunks.Reset();
    NextIndex = 0;
    Sequence = MakeUnique<FBootTrackSequence>(Seed);
    Advance(0.);
}

void ABootTrack::SpawnNext(int32 Tier)
{
    ABootTrackChunk* Chunk = GetWorld()->SpawnActor<ABootTrackChunk>(FVector(NextIndex * FBootTrackSequence::Length, 0, 0), FRotator::ZeroRotator);
    Chunk->BuildChunk(Sequence->Next(Tier), NextIndex++, Materials);
    Chunks.Add(Chunk);
}

void ABootTrack::Advance(double PlayerX)
{
    if (!Sequence || Materials.IsEmpty()) return;
    const int32 Tier = FMath::Clamp(static_cast<int32>(PlayerX / 22500.), 0, 3);
    while (NextIndex * FBootTrackSequence::Length < PlayerX + 8 * FBootTrackSequence::Length) SpawnNext(Tier);
    for (int32 I = Chunks.Num() - 1; I >= 0; --I)
        if (Chunks[I]->EndX() < PlayerX - 2 * FBootTrackSequence::Length)
        { Chunks[I]->Destroy(); Chunks.RemoveAt(I); }
    if (Background) Background->SetActorLocation(FVector(PlayerX, 0, 0));
}

void ABootTrack::Pulse(float Amount)
{
    if (Materials.Num() > 3) Materials[3]->SetVectorParameterValue(TEXT("FlatColor"),
        FMath::Lerp(Navy, FLinearColor(FColor(70, 103, 160)), Amount * .55f));
}

bool ABootTrack::IsFatal(FVector Center, float Radius, float HalfHeight) const
{
    for (ABootTrackChunk* Chunk : Chunks) if (Chunk->IsFatal(Center, Radius, HalfHeight)) return true;
    return false;
}

void ABootTrack::UpdateCourse(ABootRunner* Runner,float Dt)
{ if(Runner) Advance(Runner->GetActorLocation().X); }

bool ABootTrack::CanCompleteSurf(const ABootSurfWindow* Window,const ABootRunner* Runner,int32) const
{
    if(!Window || !Runner) return false;
    const FVector P=Runner->GetActorLocation(), Exit=Window->GetEndTop();
    return (Runner->GetParkour()->IsMovingOnGround() || Runner->GetParkour()->IsWallRunning())
        && P.X>Exit.X+600 && P.X<Exit.X+2600 && FMath::Abs(P.Y)<1300 && P.Z>Exit.Z-450 && P.Z<Exit.Z+750;
}

void ABootTrack::InitializeRunPresentation(int32 Seed,AActor* FollowTarget)
{
    if(!GetWorld() || IsActorBeingDestroyed())return;
    if(!IsValid(RunDistrict))
    {
        FActorSpawnParameters Params;Params.Owner=this;
        Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        RunDistrict=GetWorld()->SpawnActor<ABootDesktopBackdrop>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
        bRunDistrictConfigured=false;
    }
    if(!RunDistrict)return;
    if(!bRunDistrictConfigured || RunDistrictSeed!=Seed)
    {
        const FVector Origin=IsValid(FollowTarget)?FollowTarget->GetActorLocation():GetActorLocation();
        RunDistrict->ConfigureDistrict(FTransform(Origin),Seed);
        RunDistrictSeed=Seed;bRunDistrictConfigured=true;
    }
    RunDistrict->SetFollowTarget(IsValid(FollowTarget)?FollowTarget:nullptr);
}

void ABootTrack::ApplyRunPresentation(const FBootMusicContext& Context,const FBootTraversalEnvelope& Envelope)
{
    if(IsValid(RunDistrict))RunDistrict->ApplyMusicContext(Context,Envelope);
}

void ABootTrack::EndPlay(const EEndPlayReason::Type Reason)
{
    if(IsValid(RunDistrict))
    {
        RunDistrict->SetFollowTarget(nullptr);
        RunDistrict->Destroy();
    }
    RunDistrict=nullptr;bRunDistrictConfigured=false;
    Super::EndPlay(Reason);
}
