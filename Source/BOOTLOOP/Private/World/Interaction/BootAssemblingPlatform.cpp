#include "World/Interaction/BootAssemblingPlatform.h"
#include "Core/BootGameInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace BootAssembly
{
    constexpr int32 ColorCount = 7;
    const FColor Palette[] = {FColor(232,223,195), FColor(143,150,127), FColor(16,31,48),
        FColor(23,43,103), FColor(249,241,215), FColor(242,188,83), FColor(118,212,174)};
}
bool FBootAssemblyLookahead::NeedsPreparation(double Distance, double CharacterCap, double BaseSpeed, double Duration)
{
    if (!FMath::IsFinite(Distance) || !FMath::IsFinite(CharacterCap)
        || !FMath::IsFinite(BaseSpeed) || !FMath::IsFinite(Duration)) return true;
    return Distance <= (FMath::Max(1800., CharacterCap)+FMath::Max(0.,BaseSpeed))*(FMath::Max(3.,Duration)+.5);
}
ABootAssemblingPlatform::ABootAssemblingPlatform()
{
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bStartWithTickEnabled=false;
    PrimaryActorTick.TickGroup=TG_PrePhysics;
    Solid=CreateDefaultSubobject<UBoxComponent>(TEXT("StationaryLanding")); SetRootComponent(Solid);
    Solid->SetMobility(EComponentMobility::Movable); Solid->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Solid->SetCollisionObjectType(ECC_WorldDynamic); Solid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Solid->SetGenerateOverlapEvents(false); Solid->SetCanEverAffectNavigation(false); Solid->CanCharacterStepUpOn=ECB_Yes;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Game/Assets/V02/M_PixelSurface.M_PixelSurface"));
    SurfaceMaterial=Mat.Object;
    static ConstructorHelpers::FObjectFinder<USoundBase> Click(TEXT("/Game/Assets/Audio/S_UI_Click.S_UI_Click"));
    ReadySound=Click.Object;
    for (int32 Stage=0;Stage<4;++Stage) for (int32 Color=0;Color<BootAssembly::ColorCount;++Color)
    {
        auto* Mesh=CreateDefaultSubobject<UInstancedStaticMeshComponent>(FName(*FString::Printf(TEXT("AssemblyStage%dColor%d"),Stage,Color)));
        Mesh->SetupAttachment(Solid); Mesh->SetStaticMesh(Cube.Object); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false); Mesh->SetCanEverAffectNavigation(false); Mesh->SetCastShadow(false);
        Mesh->SetMobility(EComponentMobility::Movable); Parts.Add(Mesh);
    }
    Tags.Add(TEXT("BootAssemblingPlatform"));
}
void ABootAssemblingPlatform::BeginPlay()
{
    Super::BeginPlay();
    for(int32 Color=0;Color<BootAssembly::ColorCount;++Color)
    {
        auto* Mat=UMaterialInstanceDynamic::Create(SurfaceMaterial,this);
        if(Mat) Mat->SetVectorParameterValue(TEXT("FlatColor"),FLinearColor(BootAssembly::Palette[Color]));
        for(int32 Stage=0;Stage<4;++Stage) Parts[Stage*BootAssembly::ColorCount+Color]->SetMaterial(0,Mat);
    }
    BuildVisuals();
}
bool ABootAssemblingPlatform::IsCurrentSupport() const
{
    if(!GetWorld()) return false;
    for(TActorIterator<ACharacter> It(GetWorld());It;++It) if(It->GetMovementBaseObject()==Solid) return true;
    return false;
}
bool ABootAssemblingPlatform::IsOccupied() const
{
    if(!GetWorld()) return true;
    FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_Pawn); Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BootAssemblyClearance),false,this);
    return GetWorld()->OverlapAnyTestByObjectType(Solid->GetComponentLocation(),Solid->GetComponentQuat(),Objects,
        FCollisionShape::MakeBox(Solid->GetScaledBoxExtent()+FVector(.5)),Params);
}
void ABootAssemblingPlatform::Configure(const FTransform& Top,FVector Size,float Duration,int32 Id)
{
    // Never relocate/withdraw a solid supporting or intersecting a pawn.
    if(bReady && (IsCurrentSupport() || IsOccupied())) return;
    Solid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if(Size.ContainsNaN()) Size=FVector(1200,900,80);
    PlatformSize=FVector(FMath::Clamp(FMath::Abs(Size.X),100.,20000.),FMath::Clamp(FMath::Abs(Size.Y),100.,20000.),FMath::Clamp(FMath::Abs(Size.Z),20.,600.));
    BuildSeconds=FMath::IsFinite(Duration)?FMath::Clamp(Duration,.9f,1.2f):1.1f; GroupId=Id;
    const FVector Location=Top.ContainsNaN()?FVector::ZeroVector:Top.GetLocation();
    const FQuat Rotation=Top.ContainsNaN()?FQuat::Identity:FQuat(FRotator(0,Top.Rotator().Yaw,0));
    SetActorLocationAndRotation(Location-FVector(0,0,PlatformSize.Z*.5),Rotation,false,nullptr,ETeleportType::TeleportPhysics);
    Solid->SetBoxExtent(PlatformSize*.5,true); bReady=bBuilding=bAnimatedAssembly=false; Elapsed=0.f; SetActorTickEnabled(false); BuildVisuals();
}
void ABootAssemblingPlatform::BeginAssembly()
{
    if(bReady || bBuilding) return;
    bBuilding=bAnimatedAssembly=true; SetActorTickEnabled(true); UpdateVisuals();
}
bool ABootAssemblingPlatform::FinishSafely()
{
    // Explicit prewarm/restore completion remains silent, including a deferred occupied-volume retry.
    bAnimatedAssembly=false;
    return CompleteSafely(false);
}
bool ABootAssemblingPlatform::CompleteSafely(bool bAudibleTransition)
{
    if(bReady) return true;
    // Collider already occupies its final transform; clearance is checked before enabling it.
    if(IsOccupied()) { bBuilding=true; Elapsed=BuildSeconds; SetActorTickEnabled(true); UpdateVisuals(); return false; }
    Elapsed=BuildSeconds; bBuilding=false; bReady=true; Solid->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    bAnimatedAssembly=false; SetActorTickEnabled(false); UpdateVisuals();
    if(bAudibleTransition) PlayReadyClick();
    return true;
}
void ABootAssemblingPlatform::PlayReadyClick()
{
    if(!ReadySound || !GetWorld() || !GetWorld()->AllowAudioPlayback() || GetWorld()->IsPaused()) return;
    const APlayerController* PC=GetWorld()->GetFirstPlayerController();
    if(!PC || !PC->IsLocalController() || !PC->PlayerCameraManager) return;
    const FVector Listener=PC->PlayerCameraManager->GetCameraLocation();
    const double Distance=FVector::Dist(Listener,Solid->Bounds.GetBox().GetClosestPointTo(Listener));
    constexpr double AudibleRange=1800.;
    if(Distance>=AudibleRange) return;
    const auto* GI=GetGameInstance<UBootGameInstance>();
    const float Sfx=GI && GI->GetProfile()?GI->GetOptions().Sfx:.65f;
    const float Nearness=1.f-static_cast<float>(Distance/AudibleRange);
    // Manual listener falloff keeps distant simultaneous chunk reveals silent. False marks gameplay audio.
    UGameplayStatics::PlaySound2D(this,ReadySound,Sfx*.18f*Nearness*Nearness,1.f,0.f,nullptr,this,false);
}
void ABootAssemblingPlatform::ResetAssembly(bool Ready)
{
    if(bReady && (IsCurrentSupport() || IsOccupied())) return;
    if(Ready) { FinishSafely(); return; }
    bReady=bBuilding=bAnimatedAssembly=false; Elapsed=0.f; Solid->SetCollisionEnabled(ECollisionEnabled::NoCollision); SetActorTickEnabled(false); UpdateVisuals();
}
void ABootAssemblingPlatform::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!bBuilding || !GetWorld() || GetWorld()->IsPaused()) return;
    Elapsed=FMath::Min(BuildSeconds,Elapsed+FMath::Max(0.f,Dt));
    if(Elapsed>=BuildSeconds) CompleteSafely(bAnimatedAssembly); else UpdateVisuals();
}
int32 ABootAssemblingPlatform::GetDecorativePartCount() const
{
    int32 Count=0; for(UInstancedStaticMeshComponent* Mesh:Parts) Count+=Mesh->GetInstanceCount(); return Count;
}
void ABootAssemblingPlatform::BuildVisuals()
{
    for(UInstancedStaticMeshComponent* Mesh:Parts) Mesh->ClearInstances();
    const double X=PlatformSize.X*.5,Y=PlatformSize.Y*.5,Z=PlatformSize.Z*.5;
    auto Box=[&](int32 Stage,int32 Color,FVector P,FVector Size) { Parts[Stage*BootAssembly::ColorCount+Color]->AddInstance(FTransform(FQuat::Identity,P,Size/100.)); };
    for(double Side:{-1.,1.}) { Box(0,6,{Side*X,0,Z+3},{10,PlatformSize.Y,10}); Box(0,6,{0,Side*Y,Z+3},{PlatformSize.X,10,10}); }
    Box(1,2,{0,0,-5},PlatformSize+FVector(12,12,-10)); Box(1,1,{0,-Y+2,0},{PlatformSize.X,8,PlatformSize.Z-16});
    Box(2,0,{0,0,Z-10},{PlatformSize.X-16,PlatformSize.Y-16,20}); Box(2,1,{X-5,0,0},{10,PlatformSize.Y,PlatformSize.Z-16});
    Box(2,4,{-X+8,0,Z-7},{12,PlatformSize.Y-16,12}); Box(3,4,{0,-Y+8,Z-5},{PlatformSize.X-16,12,10});
    Box(3,2,{0,Y-8,Z-6},{PlatformSize.X-16,12,12}); Box(3,3,{-X+60,0,Z+1},{90,PlatformSize.Y-38,2});
    for(int32 I=0;I<3;++I) Box(3,4,{-X+60,Y-65-I*44.,Z+3},{36,24,2});
    Box(3,2,{50,Y-48,Z+1},{FMath::Max(100.,PlatformSize.X-220),24,2}); Box(3,4,{50,Y-48,Z+3},{FMath::Max(50.,PlatformSize.X*.22),16,2});
    // Original stepped READY glyph is essential feedback, independent of cosmetic effects settings.
    for(int32 I=0;I<3;++I) { Box(3,6,{X-100-I*50.,-Y+90+I*18.,Z+4},{28,56+I*18.,4}); Box(2,1,{-X+210+I*100.,-Y+100,Z+1},{60,12,2}); }
    Box(2,1,{0,0,Z+1},{PlatformSize.X*.4,PlatformSize.Y*.38,2}); Box(2,0,{0,0,Z+2},{PlatformSize.X*.4-16,PlatformSize.Y*.38-16,2});
    UpdateVisuals();
}
void ABootAssemblingPlatform::UpdateVisuals()
{
    const float Alpha=FMath::Clamp(Elapsed/BuildSeconds,0.f,1.f);
    for(int32 Stage=0;Stage<4;++Stage)
    {
        const bool Visible=Stage==0?!bReady:bReady || (bBuilding && Alpha>=.12f+(Stage-1)*.25f);
        const float StageAlpha=bReady?1.f:FMath::Clamp((Alpha-(.12f+(Stage-1)*.25f))/.25f,0.f,1.f);
        const float Ease=StageAlpha*StageAlpha*(3.f-2.f*StageAlpha);
        FVector Offset=FVector::ZeroVector;
        if(Stage==1) Offset.Z=-180*(1-Ease); if(Stage==2) Offset.Y=-150*(1-Ease); if(Stage==3) Offset.Z=90*(1-Ease);
        for(int32 Color=0;Color<BootAssembly::ColorCount;++Color)
        {
            UInstancedStaticMeshComponent* Mesh=Parts[Stage*BootAssembly::ColorCount+Color];
            Mesh->SetVisibility(Visible && !(Stage==3 && Color==6 && !bReady)); Mesh->SetRelativeLocation(Offset);
        }
    }
}
