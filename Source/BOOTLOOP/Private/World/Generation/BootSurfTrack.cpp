#include "World/Generation/BootSurfTrack.h"
#include "World/Interaction/BootSurfWindow.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ABootSurfTrack::ABootSurfTrack()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("CourseRoot")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Game/Assets/V02/M_PixelSurface.M_PixelSurface"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Text(TEXT("/Engine/EngineMaterials/UnlitText.UnlitText"));
    SetSurfaceMaterial(Mat.Object);
    TextMaterial = Text.Object;
    auto Mesh = [&](FName Name, bool bSolid)
    {
        auto* C = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
        C->SetupAttachment(RootComponent); C->SetStaticMesh(Cube.Object);
        C->SetMobility(EComponentMobility::Movable); C->SetCastShadow(false);
        C->SetCollisionProfileName(bSolid ? TEXT("BlockAll") : TEXT("NoCollision"));
        C->SetCanEverAffectNavigation(false);
        return C;
    };
    Floors = Mesh(TEXT("WindowFloors"), true);
    Walls = Mesh(TEXT("WallrunEdges"), true);
    Walls->ComponentTags.Add(TEXT("BootWallRun"));
    Beams = Mesh(TEXT("SlideDialogs"), true);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Bevel(TEXT("/Game/Assets/V02/SM_GUIBevel.SM_GUIBevel"));
    for (int32 I=0; I<10; ++I)
    {
        auto* Part=Mesh(FName(*FString::Printf(TEXT("Pixel%d"),I)), false);
        Part->SetStaticMesh(Bevel.Object); Tiles.Add(Part);
    }
}

void ABootSurfTrack::BeginPlay()
{
    Super::BeginPlay();
    const auto& M = GetPalette();
    const FColor Palette[]={FColor(232,223,195),FColor(143,150,127),FColor(16,31,48),FColor(23,43,103),
        FColor(249,241,215),FColor(242,188,83),FColor(218,74,75),FColor(118,212,174),FColor(6,76,84),FColor(65,107,105)};
    for(int32 I=0;I<10;++I) M[I]->SetVectorParameterValue(TEXT("FlatColor"),FLinearColor(Palette[I]));
    for (int32 I=0; I<Tiles.Num(); ++I) Tiles[I]->SetMaterial(0,M[I]);
    Floors->SetMaterial(0,M[0]); Walls->SetMaterial(0,M[3]); Beams->SetMaterial(0,M[3]);
    BuildCourse();
}

void ABootSurfTrack::Box(FVector P,FVector Size,int32 Color,int32 Collision)
{
    auto* Mesh = Collision==1 ? Floors.Get() : Collision==2 ? Walls.Get() : Collision==3 ? Beams.Get() : Tiles[Color].Get();
    Mesh->AddInstance(FTransform(FQuat::Identity,P,Size/100.));
}

void ABootSurfTrack::Sign(const FString& Text,FVector P,float Size,FColor Color)
{
    auto* C = NewObject<UTextRenderComponent>(this); AddInstanceComponent(C);
    C->SetupAttachment(RootComponent); C->SetRelativeLocation(P); C->SetRelativeRotation(FRotator(0,180,0));
    C->SetText(FText::FromString(Text)); C->SetTextMaterial(TextMaterial); C->SetTextRenderColor(Color);
    C->SetWorldSize(Size); C->SetHorizontalAlignment(EHTA_Center); C->SetVerticalAlignment(EVRTA_TextCenter);
    C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->RegisterComponent();
}

void ABootSurfTrack::Panel(double A,double B,double Y,double Width,double Top,const FString& Label)
{
    const double X=(A+B)*.5, L=B-A;
    Box(FVector(X,Y,Top-42),FVector(L,Width,84),0,1);
    Box(FVector(X+8,Y+8,Top-105),FVector(L+24,Width+24,48),2);
    Box(FVector(X,Y-Width*.5+10,Top+2),FVector(L,20,4),4);
    Box(FVector(X,Y+Width*.5-10,Top+2),FVector(L,20,4),1);
    Box(FVector(A+85,Y,Top+2),FVector(170,Width-40,4),3);
    for(int32 I=0;I<3;++I) Box(FVector(A+85,Y+Width*.5-70-I*60,Top+6),FVector(80,40,6),4);
    // Big offset route markers remain readable at a coarse raster.
    for(double Marker=A+400;Marker<B-150;Marker+=350)
    {
        Box(FVector(Marker,Y,Top+3),FVector(65,120,5),7);
        Box(FVector(Marker-30,Y-65,Top+3),FVector(50,45,5),7);
        Box(FVector(Marker-30,Y+65,Top+3),FVector(50,45,5),7);
    }
    if(!Label.IsEmpty()) Sign(Label,FVector(A+180,Y,Top+250),54,FColor(245,205,96));
}

void ABootSurfTrack::Tower(FVector P,float Width,float Height,int32 Variant)
{
    Box(P,FVector(90,Width+70,Height+70),2);
    Box(P+FVector(-55,0,0),FVector(35,Width,Height),0);
    Box(P+FVector(-78,0,Height*.5-65),FVector(16,Width-28,100),3);
    Box(P+FVector(-90,-Width*.5+20,0),FVector(12,12,Height-36),4);
    if(Variant%4==0)
    {
        // Oversized floppy: original OS95 glyph, built in chunky clusters.
        Box(P+FVector(-83,0,-20),FVector(14,Width*.42,Height*.57),3);
        Box(P+FVector(-93,0,Height*.13),FVector(12,Width*.30,Height*.20),1);
        Box(P+FVector(-101,Width*.07,Height*.13),FVector(8,Width*.055,Height*.16),2);
        Box(P+FVector(-94,0,-Height*.16),FVector(10,Width*.30,Height*.18),4);
        for(int32 I=0;I<3;++I) Box(P+FVector(-101,0,-Height*.12-I*Height*.045),FVector(6,Width*.22,Height*.015),1);
    }
    else if(Variant%4==1)
    {
        Box(P+FVector(-83,0,-20),FVector(14,Width*.80,Height*.53),2);
        for(int32 I=0;I<6;++I)
        {
            const float LineWidth=Width*(I%3==0?.48f:I%3==1?.67f:.36f);
            Box(P+FVector(-94,-Width*.32+LineWidth*.5,Height*.12-I*Height*.055),FVector(10,LineWidth,Height*.018),I%2?7:5);
        }
    }
    else for(int32 I=0;I<3;++I)
    {
        const float Y=-Width*.32+I*Width*.32;
        Box(P+FVector(-83,Y,0),FVector(14,Width*.18,Height*.23),Variant%2?7:5);
        Box(P+FVector(-85,Y-Width*.035,Height*.14),FVector(14,Width*.11,Height*.05),Variant%2?7:5);
        Box(P+FVector(-85,Y,-Height*.24),FVector(14,Width*.2,18),1);
    }
    const TCHAR* Names[]={TEXT("DISK / A:"),TEXT("TERMINAL / SYS"),TEXT("SHELL / DESKTOP"),TEXT("CACHE / 95")};
    Sign(Names[Variant%4],P+FVector(-94,0,Height*.5-65),Height*.075);
}

void ABootSurfTrack::BuildCourse()
{
    Panel(0,1800,0,1800,0,TEXT("01 / MOVE THE WINDOW"));
    Sign(TEXT("BOARD / SNAP / RIDE / JUMP"),FVector(1450,0,310),48,FColor(245,205,96));
    Panel(1800,5400,-650,1250,-160,TEXT("LOWER / SAFE LINE"));
    Panel(3510,5400,450,900,160,TEXT("UPPER / KEEP MOMENTUM"));
    Box(FVector(3900,940,420),FVector(1500,80,520),3,2);
    Box(FVector(3900,890,665),FVector(1500,20,30),5);
    Sign(TEXT("WALLRUN  >  WHEEL KICK"),FVector(3700,460,620),46);
    Panel(5400,7200,0,2400,-160,TEXT("02 / LOW MEMORY"));
    Panel(7200,9100,0,1200,-160,TEXT("CTRL / AIR DUCK + SLIDE"));
    // Floor top -160, beam bottom -50: standing blocked, crouch safely fits.
    Box(FVector(8150,0,60),FVector(260,1200,220),3,3);
    Box(FVector(8008,0,65),FVector(20,1240,250),2);
    Sign(TEXT("LOW MEMORY"),FVector(7990,0,110),55);
    Panel(9100,9650,0,1400,-160,TEXT("03 / SURF AGAIN"));
    Panel(9650,13300,650,1250,-320,TEXT("LOWER / KEEP RIGHT"));
    Panel(11310,13300,-450,900,0,TEXT("UPPER / LEFT EDGE"));
    Box(FVector(11750,-940,260),FVector(1500,80,520),3,2);
    Box(FVector(11750,-890,505),FVector(1500,20,30),5);
    Panel(13300,15100,0,2400,-320,TEXT("04 / FREE YOUR PROCESS"));
    Panel(15100,16500,0,1400,-320,TEXT("JUMP / KEEP SPEED"));
    Panel(16780,18800,0,1400,-320,TEXT("BUFFER CLEARED"));
    Panel(18800,20700,0,1400,-320,TEXT("MANUAL BHOP / WHEEL"));
    Box(FVector(19600,750,-60),FVector(1400,80,520),3,2);
    Box(FVector(19600,700,185),FVector(1400,20,30),5);
    Panel(20700,22000,0,1400,-320,TEXT("FINAL SECTOR"));
    Panel(22260,24600,0,1400,-320,TEXT("PROCESS ESCAPED"));
    Panel(24600,26800,0,2000,-320,TEXT("EXIT / RETRY TO MASTER"));
    // Finish portal is open geometry; only the zone ends the run.
    Box(FVector(26000,-850,300),FVector(100,100,1240),7);
    Box(FVector(26000,850,300),FVector(100,100,1240),7);
    Box(FVector(26000,0,900),FVector(100,1800,100),7);
    Sign(TEXT("PROCESS ESCAPED"),FVector(25930,0,900),100,FColor(100,201,167));
    for(int32 I=0;I<20;++I)
    {
        const double X=1000+I*1450.; const double Side=I%2?1.:-1.;
        Tower(FVector(X,Side*(2250+(I%3)*600),200+(I%4)*130),1300+(I%3)*250,900+(I%4)*350,I);
        // Pixel city silhouettes at a farther layer establish depth.
        Box(FVector(X+650,Side*6500,-200),FVector(500,800,1500+(I%5)*300),2);
    }
    for(int32 I=0;I<2;++I)
    {
        auto* W=GetWorld()->SpawnActor<ABootSurfWindow>();
        W->Configure(I==0?FVector(2300,0,0):FVector(10150,0,-160),
            I==0?FVector(3000,450,160):FVector(10800,-450,0),1.45f,I+1);
        Windows.Add(W);
    }
}

void ABootSurfTrack::ResetTrack(int32 Seed)
{
    for(ABootSurfWindow* W:Windows) if(IsValid(W)) W->ResetWindow();
}
void ABootSurfTrack::Advance(double PlayerX) { /* Authored trial has no streaming mutations. */ }
bool ABootSurfTrack::IsFatal(FVector Center,float Radius,float HalfHeight) const { return Center.Z < -1200; }
float ABootSurfTrack::GetRouteProgress(FVector P) const
{
    // Parallel routes share the same longitudinal checkpoints, so height/side cannot farm distance.
    return FMath::Clamp(float((P.X-350.)/100.),0.f,256.5f);
}
bool ABootSurfTrack::IsFinished(FVector P) const { return P.X>=26000 && FMath::Abs(P.Y)<1000 && P.Z>-600; }
