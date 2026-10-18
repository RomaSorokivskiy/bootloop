#include "World/Presentation/BootDesktopBackdrop.h"
#include "Music/BootMusicTypes.h"
#include "Movement/BootTraversalEnvelope.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace BootDistrict
{
    // Background amber/mint are deliberately dimmer than the route's live interaction colors.
    const FColor Palette[]={FColor(202,197,169),FColor(120,131,119),FColor(28,45,56),
        FColor(36,53,91),FColor(223,219,190),FColor(169,139,76),FColor(83,148,127),
        FColor(34,105,111),FColor(29,93,101),FColor(50,118,121),FColor(67,131,130),FColor(24,84,94)};
}
ABootDesktopBackdrop::ABootDesktopBackdrop()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bStartWithTickEnabled=false;
    PrimaryActorTick.bTickEvenWhenPaused=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("DistrictOrigin")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Game/Assets/V02/M_PixelSurface.M_PixelSurface"));
    SurfaceMaterial=Mat.Object;
    for(int32 I=0;I<UE_ARRAY_COUNT(BootDistrict::Palette);++I)
    {
        auto* Mesh=CreateDefaultSubobject<UInstancedStaticMeshComponent>(FName(*FString::Printf(TEXT("DistrictLayerColor%d"),I)));
        Mesh->SetupAttachment(RootComponent); Mesh->SetStaticMesh(Cube.Object); Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCastShadow(false); Mesh->SetCanEverAffectNavigation(false); Parts.Add(Mesh);
    }
    Tags.Add(TEXT("BootDesktopBackdrop"));
}
void ABootDesktopBackdrop::BeginPlay()
{
    Super::BeginPlay();
    for(int32 I=0;I<Parts.Num();++I)
    {
        auto* Mat=UMaterialInstanceDynamic::Create(SurfaceMaterial,this);
        // Scenery stays below live mint route edges and red/amber hazard signals.
        const FLinearColor Color=FLinearColor(BootDistrict::Palette[I])*.63f;
        if(Mat) Mat->SetVectorParameterValue(TEXT("FlatColor"),Color);
        Parts[I]->SetMaterial(0,Mat);
        DistrictMaterials.Add(Mat);PaletteCurrent.Add(Color);PaletteTarget.Add(Color);
    }
}
void ABootDesktopBackdrop::Box(FVector P,FVector Size,int32 Color,FQuat Rotation)
{
    Parts[Color]->AddInstance(FTransform(Rotation,P,Size/100.));
}
void ABootDesktopBackdrop::Facade(const FTransform& Frame,double Width,double Height,int32 Variant)
{
    auto B=[&](FVector P,FVector Size,int32 C) { Box(Frame.TransformPosition(P),Size,C,Frame.GetRotation()); };
    // Local X projects outward. Entire facade remains volumetric and reads from corner approaches.
    B({-24,0,Height*.5},{48,Width,Height},0);
    B({-52,-Width*.5+26,Height*.5},{16,24,Height-30},4);
    B({-54,0,Height-120},{20,Width-50,180},3);
    B({-58,Width*.5-28,Height*.5},{24,24,Height-30},2);
    B({-60,0,70},{30,Width-30,60},1);
    for(int32 I=0;I<3;++I) B({-70,Width*.5-90-I*86.,Height-120},{18,52,65},4);
    B({-66,-Width*.5+105,Height-120},{20,90,70},5);
    // Embedded scrollbar, including its thumb, follows the architecture rather than the horizon.
    B({-59,Width*.5-105,Height*.47},{14,72,Height*.65},1);
    B({-69,Width*.5-105,Height*.57},{14,56,Height*.15},4);
    const double W=Width*.52,H=FMath::Min(Height*.48,Width*.75), Z=Height*.5;
    switch(Variant%5)
    {
    case 0: // original floppy/archive mark
        B({-62,0,Z},{24,W,H},3); B({-78,0,Z+H*.28},{14,W*.65,H*.26},1);
        B({-90,W*.16,Z+H*.28},{12,W*.12,H*.19},2); B({-80,0,Z-H*.19},{16,W*.72,H*.33},4);
        for(int32 I=0;I<3;++I) B({-91,0,Z-H*.12-I*H*.07},{8,W*.5,H*.025},1);
        break;
    case 1: // folded document stack, no borrowed OS logo
        B({-62,-W*.14,Z+H*.06},{20,W*.65,H*.92},3); B({-82,W*.08,Z-H*.05},{20,W*.65,H*.92},4);
        B({-94,W*.28,Z+H*.28},{12,W*.22,H*.22},1);
        for(int32 I=0;I<4;++I) B({-96,W*.03,Z+H*.18-I*H*.13},{10,W*.4,H*.028},1);
        break;
    case 2: // process bars, clear quiet industrial rhythm
        B({-62,0,Z},{18,W,H},2);
        for(int32 I=0;I<5;++I) B({-76,-W*.38+I*W*.19,Z-H*.24+I*H*.07},{12,W*.09,H*(.28+I*.12)},I%2?6:5);
        break;
    case 3: // nested application windows
        B({-62,-W*.1,Z+H*.1},{20,W*.85,H*.8},3); B({-77,W*.1,Z-H*.1},{20,W*.85,H*.8},1);
        B({-91,W*.1,Z-H*.14},{10,W*.65,H*.48},0); B({-93,W*.1,Z+H*.16},{14,W*.8,H*.13},3);
        break;
    default: // port/network glyph, large simple shape visible at the coarse world raster
        B({-65,0,Z},{20,W*.26,H*.68},3); B({-65,0,Z},{20,W*.72,H*.18},3);
        for(double S:{-1.,1.}) { B({-78,S*W*.36,Z},{14,W*.22,H*.34},6); B({-78,0,Z+S*H*.36},{14,W*.3,H*.2},5); }
        break;
    }
    // Architectural lower taskbar adds depth when the player looks down from a return deck.
    B({-65,0,Height*.11},{40,Width*.8,90},3);
    for(int32 I=0;I<4;++I) B({-90,-Width*.28+I*Width*.18,Height*.11},{16,Width*.12,45},1);
}
void ABootDesktopBackdrop::Tower(FVector Base,FVector Size,int32 Variant,bool Far)
{
    Box(Base+FVector(0,0,Size.Z*.5),Size,Far?8:2);
    Box(Base+FVector(-Size.X*.5-3,0,Size.Z*.5),{18,Size.Y,Size.Z},Far?7:1);
    Box(Base+FVector(0,0,Size.Z+25),{Size.X+120,Size.Y+120,50},Far?9:4);
    if(Far)
    {
        Box(Base+FVector(0,0,Size.Z+420),{Size.X*.3,Size.Y*.5,800},7);
        Box(Base+FVector(Size.X*.25,0,Size.Z+1050),{40,40,1300},9);
        return;
    }
    for(int32 Side=0;Side<4;++Side)
    {
        const double Yaw=Side*90.; const FQuat Q(FRotator(0,Yaw,0));
        const double Depth=Side%2?Size.Y:Size.X, Width=Side%2?Size.X:Size.Y;
        Facade(FTransform(Q,Base+Q.RotateVector(FVector(-Depth*.5,0,0))),Width,Size.Z,Variant+Side);
    }
    // Roof terraces and comb antennas replace repeated identical roof caps.
    Box(Base+FVector(Size.X*.18,Size.Y*.12,Size.Z+130),{Size.X*.6,Size.Y*.55,180},3);
    Box(Base+FVector(Size.X*.18,Size.Y*.12,Size.Z+235),{Size.X*.62,Size.Y*.57,30},5);
    for(int32 I=0;I<3;++I) Box(Base+FVector(-Size.X*.32+I*120.,Size.Y*.3,Size.Z+250+I*120.),{35,35,500+I*240.},I%2?1:2);
}
void ABootDesktopBackdrop::Cable(FVector A,FVector B,double Sag)
{
    for(int32 Lane=0;Lane<2;++Lane) for(int32 I=0;I<12;++I)
    {
        auto P=[&](double T) { return FMath::Lerp(A,B,T)+FVector(0,Lane*90.,-Sag*4*T*(1-T)); };
        FVector Start=P(I/12.),End=P((I+1)/12.),D=End-Start;
        Box((Start+End)*.5,{D.Size()+10,38,38},Lane?5:2,FQuat::FindBetweenNormals(FVector::ForwardVector,D.GetSafeNormal()));
    }
}
void ABootDesktopBackdrop::ConfigureDistrict(const FTransform& Center,int32 Seed)
{
    for(UInstancedStaticMeshComponent* Mesh:Parts) Mesh->ClearInstances();
    SetActorTransform(Center); FRandomStream R(Seed);
    // Course reserves x[-8500,10000],y[-13500,13000] relative to the district centre.
    // Near service islands sit outside that footprint, never on an apparent landing line.
    const FVector Near[]={FVector(-11000,-3500,-6500),FVector(12000,6000,-6800),FVector(0,15500,-7500),FVector(9500,-15500,-8000)};
    for(int32 I=0;I<4;++I)
    {
        Tower(Near[I],FVector(1700+I*160,2300,7200+I*350),I);
        const FVector P=Near[I]+FVector(0,0,5300);
        Box(P,{2800,2700,220},2); Box(P+FVector(0,0,130),{2700,2600,50},0);
        Box(P+FVector(0,-1310,130),{2800,75,110},4);
    }
    // Uneven groups with broad empty intervals; no uniform corridor of identical facades.
    const FVector Clusters[]={FVector(21000,3000,-9500),FVector(15000,20500,-10000),FVector(-18000,18000,-10000),
        FVector(-22000,-4000,-11000),FVector(-7000,-23000,-11000),FVector(18000,-23000,-9500)};
    for(int32 C=0;C<6;++C)
    {
        for(int32 I=0;I<3;++I)
        {
            FVector Base=Clusters[C]+FVector((I-1)*3100,R.FRandRange(-1600,1600),R.FRandRange(-700,300));
            FVector Size(R.FRandRange(1800,2900),R.FRandRange(2400,3600),R.FRandRange(7200,14000));
            Tower(Base,Size,C*3+I);
            if(I>0) Cable(Base+FVector(0,0,Size.Z*.7),Base+FVector(-3100,600,Size.Z*.7+R.FRandRange(-700,700)),700);
        }
    }
    // Boot-spire is the east-facing spawn landmark; archive crown opens after the first turn.
    const FVector Spire(20000,11000,-10000);
    Tower(Spire,{3400,4200,18500},2);
    Box(Spire+FVector(0,0,19900),{1200,1600,2800},3);
    Box(Spire+FVector(0,0,21400),{1700,2100,160},5);
    for(int32 I=0;I<3;++I) Box(Spire+FVector(0,0,21800+I*350),{1200-I*250.,1500-I*300.,110},4);
    Tower({-18000,-18500,-10000},{4700,3000,15500},0);
    // Far skyline: deliberately lower contrast in three receding silhouettes.
    for(int32 Ring=0;Ring<3;++Ring) for(int32 I=0;I<24;++I)
    {
        const double Angle=(I*15.+Ring*7.)*PI/180.,Radius=35000.+Ring*11500.;
        const FVector Base(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,-13000.-Ring*1300.);
        Tower(Base,{R.FRandRange(1900,4200),R.FRandRange(2000,4500),R.FRandRange(10000,20500)},I,true);
    }
    // Quiet stepped pixel clouds/haze, arranged around the horizon with no transparent sorting.
    for(int32 I=0;I<18;++I)
    {
        const double A=I*20.*PI/180.; const FQuat Q(FRotator(0,I*20.,0));
        const FVector P(FMath::Cos(A)*59000.,FMath::Sin(A)*59000.,R.FRandRange(8000,12500));
        for(int32 S=0;S<4;++S) Box(P+Q.RotateVector(FVector(0,(S-1.5)*2600.,S%2*450.)),{1400,5500,800+S*160.},I%3?9:10,Q);
    }
}
int32 ABootDesktopBackdrop::GetDecorativePartCount() const
{
    int32 N=0; for(UInstancedStaticMeshComponent* Mesh:Parts) N+=Mesh->GetInstanceCount(); return N;
}

void ABootDesktopBackdrop::SetFollowTarget(AActor* Target)
{
    FollowTarget=Target;
    if(Target)SetActorLocation(Target->GetActorLocation());
    SetActorTickEnabled(Target!=nullptr);
}

void ABootDesktopBackdrop::ApplyMusicContext(const FBootMusicContext& Context,const FBootTraversalEnvelope& Envelope)
{
    if(PaletteSection==Context.Section && PaletteEpoch==Context.TrackEpoch)return;
    PaletteSection=Context.Section;PaletteEpoch=Context.TrackEpoch;
    // A private deterministic cosmetic stream, completely independent of planner RNG.
    FRandomStream Cosmetic(int32(GetTypeHash(Context.TrackHash)^GetTypeHash(Context.Section)^0x9504));
    const int32 Variant=Cosmetic.RandRange(0,3);
    const float Pace=Envelope.bValid&&Envelope.RunCmps>0.f?FMath::Clamp(Envelope.RunCmps/1100.f,.7f,1.4f):1.f;
    const float Energy=FMath::IsFinite(Context.Energy)?FMath::Clamp(Context.Energy,0.f,1.f):0.f;
    const FLinearColor Tints[]={FLinearColor(.72f,.86f,1.f),FLinearColor(.8f,1.f,.85f),FLinearColor(1.f,.91f,.73f),FLinearColor(.82f,.80f,1.f)};
    PaletteTarget.SetNum(Parts.Num());
    for(int32 I=0;I<Parts.Num();++I)
    {
        const float Quiet=.53f+.06f*Energy+.02f*(Pace-.7f);
        PaletteTarget[I]=FLinearColor(BootDistrict::Palette[I])*Tints[Variant]*Quiet;
        PaletteTarget[I].A=1.f;
    }
    SetActorTickEnabled(true);
}

void ABootDesktopBackdrop::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!FMath::IsFinite(Dt)||Dt<=0.f)return;
    if(AActor* Target=FollowTarget.Get())SetActorLocation(Target->GetActorLocation());
    bool Transitioning=false;
    const float Alpha=1.f-FMath::Exp(-FMath::Min(Dt,.1f)/1.8f);
    for(int32 I=0;I<DistrictMaterials.Num();++I)
    {
        if(!PaletteTarget.IsValidIndex(I)||!PaletteCurrent.IsValidIndex(I))continue;
        PaletteCurrent[I]=FMath::Lerp(PaletteCurrent[I],PaletteTarget[I],Alpha);
        if(!PaletteCurrent[I].Equals(PaletteTarget[I],.001f))Transitioning=true;
        if(DistrictMaterials[I])DistrictMaterials[I]->SetVectorParameterValue(TEXT("FlatColor"),PaletteCurrent[I]);
    }
    // Tick only moves scenery and blends cosmetic colors; no UI/data/scoring Tick.
    SetActorTickEnabled(FollowTarget.IsValid()||Transitioning);
}
