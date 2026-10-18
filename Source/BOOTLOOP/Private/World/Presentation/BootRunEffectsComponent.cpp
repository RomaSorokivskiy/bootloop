#include "World/Presentation/BootRunEffectsComponent.h"
#include "Profile/BootProfile.h"
#include "Components/SceneComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    template<typename T> T* OptionalAsset(const FString& Package)
    {
        if (!FPackageName::DoesPackageExist(Package)) return nullptr;
        return LoadObject<T>(nullptr,*(Package+TEXT(".")+FPackageName::GetShortName(Package)));
    }
    const FLinearColor Mint(.25f,.85f,.59f), Amber(1.f,.64f,.12f), Ivory(.83f,.79f,.65f);
}
UBootRunEffectsComponent::UBootRunEffectsComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.bTickEvenWhenPaused=false;
    ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube")); Mesh=Cube.Object;
    ConstructorHelpers::FObjectFinder<UStaticMesh> Quad(TEXT("/Engine/BasicShapes/Plane.Plane")); Plane=Quad.Object;
    ConstructorHelpers::FObjectFinder<UMaterialInterface> Flat(TEXT("/Game/Assets/V02/M_PixelSurface.M_PixelSurface")); Material=Flat.Object;
}
void UBootRunEffectsComponent::BeginPlay()
{
    Super::BeginPlay();
    MaskMaterial=OptionalAsset<UMaterialInterface>(TEXT("/Game/Assets/V04/FX/M_RunFxMask"));
    for(const TCHAR* Name:{TEXT("Ribbon"),TEXT("Afterimage"),TEXT("Guard"),TEXT("AltTab"),TEXT("Snap"),TEXT("Intercept"),TEXT("Card"),TEXT("Danger")})
        Masks.Add(OptionalAsset<UTexture>(FString(TEXT("/Game/Assets/V04/FX/T_FX_"))+Name));
    for(const TCHAR* Name:{TEXT("BufferDash"),TEXT("GuardHit"),TEXT("GuardRecharge"),TEXT("AltTab"),TEXT("Snap"),TEXT("Intercept"),TEXT("Overclock"),TEXT("CardBuff"),TEXT("CardRisk"),TEXT("Danger")})
        EventSounds.Add(OptionalAsset<USoundBase>(FString(TEXT("/Game/Audio/V04/S_"))+Name));
    if(GetOwner())for(int32 I=0;I<4;++I)
    {
        auto* Audio=NewObject<UAudioComponent>(GetOwner(),FName(*FString::Printf(TEXT("RunFxVoice%d"),I)));
        GetOwner()->AddInstanceComponent(Audio);
        if(GetOwner()->GetRootComponent())Audio->SetupAttachment(GetOwner()->GetRootComponent());
        Audio->bAutoActivate=false;Audio->bAllowSpatialization=false;Audio->bIsUISound=false;
        Audio->RegisterComponent();Voices.Add(Audio);
    }
}
int32 UBootRunEffectsComponent::Acquire(bool bEssential)
{
    for(int32 I=0;I<Effects.Num();++I)if(Effects[I].Remaining<=0.f)return I;
    if(Parts.Num()<MaxPoolSize && Mesh && GetOwner() && GetOwner()->GetRootComponent())
    {
        const int32 I=Effects.AddDefaulted();auto* Owner=GetOwner();
        auto* Part=NewObject<UStaticMeshComponent>(Owner,FName(*FString::Printf(TEXT("RunFx%d"),I)));
        Owner->AddInstanceComponent(Part);Part->SetupAttachment(Owner->GetRootComponent());
        Part->SetAbsolute(true,true,true);Part->SetStaticMesh(Mesh);Part->SetMobility(EComponentMobility::Movable);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false);Part->SetCastShadow(false);Part->bReceivesDecals=false;
        Part->SetVisibility(false);Part->RegisterComponent();Parts.Add(Part);
        Materials.Add(Material?UMaterialInstanceDynamic::Create(Material,this):nullptr);
        MaskMaterials.Add(MaskMaterial?UMaterialInstanceDynamic::Create(MaskMaterial,this):nullptr);
        return I;
    }
    // Never evict a warning for decoration; warning events may recycle decoration first.
    for(int32 N=0;N<Effects.Num();++N)
    {
        const int32 I=(ReuseCursor+N)%Effects.Num();
        if(!Effects[I].bEssential){ReuseCursor=(I+1)%Effects.Num();return I;}
    }
    if(bEssential && Effects.Num()>0){const int32 I=ReuseCursor++%Effects.Num();return I;}
    return INDEX_NONE;
}
void UBootRunEffectsComponent::Emit(EBootFeedbackEvent Event,const FTransform& Frame,float Strength)
{
    if(!Frame.IsValid() || !FMath::IsFinite(Strength) || Strength<=0.f || (GetWorld() && GetWorld()->IsPaused()))return;
    const bool Essential=Event==EBootFeedbackEvent::Danger;
    Strength=FMath::Clamp(Strength,.1f,1.f);
    int32 SoundIndex=INDEX_NONE,Icon=INDEX_NONE;FLinearColor Color=Mint;
    float Lifetime=.30f;
    switch(Event)
    {
    case EBootFeedbackEvent::BufferDash:SoundIndex=0;Icon=1;Color=Mint;Lifetime=.25f;break;
    case EBootFeedbackEvent::GuardHit:SoundIndex=1;Icon=2;Color=Amber;Lifetime=.32f;break;
    case EBootFeedbackEvent::GuardRecharge:SoundIndex=2;Icon=2;Color=Mint;Lifetime=.40f;break;
    case EBootFeedbackEvent::AirRedirect:SoundIndex=3;Icon=3;Color=FLinearColor(.35f,.65f,1.f);break;
    case EBootFeedbackEvent::Snap:SoundIndex=4;Icon=4;Color=Amber;Lifetime=.18f;break;
    case EBootFeedbackEvent::Intercept:SoundIndex=5;Icon=5;Color=Ivory;break;
    case EBootFeedbackEvent::Overclock:SoundIndex=6;Icon=0;Color=Mint;Lifetime=.42f;break;
    case EBootFeedbackEvent::CardBuff:SoundIndex=7;Icon=6;Color=Mint;Lifetime=.45f;break;
    case EBootFeedbackEvent::CardRisk:SoundIndex=8;Icon=6;Color=FLinearColor(1.f,.32f,.18f);Lifetime=.45f;break;
    case EBootFeedbackEvent::Danger:SoundIndex=9;Icon=7;Color=FLinearColor(1.f,.18f,.12f);Lifetime=.9f;break;
    default:return;
    }
    if(EventSounds.IsValidIndex(SoundIndex) && EventSounds[SoundIndex] && Sfx>0.f && Voices.Num()>0 && GetWorld() && GetWorld()->AllowAudioPlayback())
    {
        UAudioComponent* Audio=Voices[AudioCursor++%Voices.Num()];Audio->Stop();
        Audio->SetSound(EventSounds[SoundIndex]);Audio->SetVolumeMultiplier(Sfx*.38f);Audio->Play();
    }
    if(!Essential && Intensity<=0.f)return;
    auto Piece=[&](FVector Local,FVector Size,FQuat Rotation=FQuat::Identity,bool Mask=false)
    {
        const int32 I=Acquire(Essential);if(I==INDEX_NONE)return;
        FEffect& E=Effects[I];E.Frame=FTransform(Frame.GetRotation()*Rotation,Frame.TransformPositionNoScale(Local));
        E.Size=Size;E.Lifetime=E.Remaining=Lifetime;E.bEssential=Essential;
        // Reduced Motion retains a stable readable glyph; it has no position excursion.
        E.Velocity=bMotion&&!Essential?Frame.GetRotation().RotateVector(FVector(-45.f,0.f,12.f)):FVector::ZeroVector;
        const bool UseMask=Mask && Plane && MaskMaterials[I] && Masks.IsValidIndex(Icon) && Masks[Icon];
        Parts[I]->SetStaticMesh(UseMask?Plane.Get():Mesh.Get());
        auto* Mat=UseMask?MaskMaterials[I].Get():Materials[I].Get();
        if(Mat){Mat->SetVectorParameterValue(TEXT("FlatColor"),Color);if(UseMask)Mat->SetTextureParameterValue(TEXT("PixelMask"),Masks[Icon]);Parts[I]->SetMaterial(0,Mat);}
        Parts[I]->SetWorldTransform(E.Frame);Parts[I]->SetWorldScale3D(Size/100.);Parts[I]->SetVisibility(true);
    };
    const bool CanMask=MaskMaterial && Masks.IsValidIndex(Icon) && Masks[Icon];
    // Engine Plane UV-up lies along -Y after the pitch. Turn about its facing
    // axis so authored PNG-up becomes frame +Z, retaining the same face normal.
    const FQuat MaskRotation=FQuat(FVector::ForwardVector,-PI/2)*FQuat(FRotator(90,0,0));
    if(Event==EBootFeedbackEvent::BufferDash || Event==EBootFeedbackEvent::Overclock)
    {
        const int32 Lanes=Intensity<.5f?2:3;
        for(int32 Side:{-1,1})for(int32 N=0;N<Lanes;++N)
            Piece(FVector(-18-N*10,Side*(30+N*7),18+N*15),FVector(48-N*8,2.5,2.5));
        if(CanMask)Piece(FVector(-38,0,36),FVector(72,96,1),MaskRotation,true);
    }
    else if(CanMask)
    {
        // Quad lies in YZ, facing the event frame; silhouettes stay outside the aim corridor.
        const float IconSize=Essential?28.f:48.f;
        Piece(FVector(70,Essential?0:48,Essential?65:15),FVector(IconSize,IconSize,1),MaskRotation,true);
    }
    else
    {
        // Actual 3D fallback also makes pre-import native testing useful.
        const FVector C(70,Essential?0:48,Essential?65:15);
        if(Essential)
        {
            Piece(C+FVector(0,0,6),FVector(3,4,24));Piece(C-FVector(0,0,13),FVector(3,5,5));
            for(int32 Side:{-1,1})Piece(C+FVector(0,Side*16,-3),FVector(3,3,42),FQuat(FVector::ForwardVector,Side*PI/6));
            Piece(C+FVector(0,0,-22),FVector(3,45,3));
        }
        else
        {
            const bool Diamond=Event==EBootFeedbackEvent::GuardHit || Event==EBootFeedbackEvent::GuardRecharge;
            const FQuat Q=Diamond?FQuat(FVector::ForwardVector,PI/4):FQuat::Identity;
            for(int32 Side:{-1,1})
            {
                Piece(C+Q.RotateVector(FVector(0,Side*18,0)),FVector(3,3,36),Q);
                Piece(C+Q.RotateVector(FVector(0,0,Side*18)),FVector(3,36,3),Q);
            }
            if(Event==EBootFeedbackEvent::CardRisk)Piece(C,FVector(3,5,20));
        }
    }
    SetComponentTickEnabled(GetActiveEffectCount()>0);
}
void UBootRunEffectsComponent::ApplyPreferences(const FBootOptions& O)
{
    Intensity=FMath::IsFinite(O.EffectsIntensity)?FMath::Clamp(O.EffectsIntensity,0.f,1.f):0.f;
    Sfx=FMath::IsFinite(O.Sfx)?FMath::Clamp(O.Sfx,0.f,1.f):0.f;bMotion=O.bMotion;
    for(UAudioComponent* Voice:Voices)if(Voice){Voice->SetVolumeMultiplier(Sfx*.38f);if(Sfx<=0.f)Voice->Stop();}
    for(int32 I=0;I<Effects.Num();++I)
    {
        if(!bMotion)Effects[I].Velocity=FVector::ZeroVector;
        if(Intensity<=0.f && !Effects[I].bEssential){Effects[I].Remaining=0.f;Parts[I]->SetVisibility(false);}
    }
    SetComponentTickEnabled(GetActiveEffectCount()>0);
}
void UBootRunEffectsComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);
    if(!FMath::IsFinite(Dt) || Dt<=0.f || (GetWorld() && GetWorld()->IsPaused()))return;
    Dt=FMath::Min(Dt,.1f);
    for(int32 I=0;I<Effects.Num();++I)
    {
        FEffect& E=Effects[I];if(E.Remaining<=0.f)continue;
        E.Remaining=FMath::Max(0.f,E.Remaining-Dt);
        if(E.Remaining<=0.f){Parts[I]->SetVisibility(false);continue;}
        E.Frame.AddToTranslation(E.Velocity*Dt);Parts[I]->SetWorldTransform(E.Frame);
        // Essential danger is stable through its full minimum telegraph interval.
        const float Fade=E.bEssential||!bMotion?1.f:FMath::Min(1.f,E.Remaining/.09f);
        Parts[I]->SetWorldScale3D(E.Size*(Fade/100.f));
    }
    SetComponentTickEnabled(GetActiveEffectCount()>0);
}
int32 UBootRunEffectsComponent::GetActiveEffectCount() const
{int32 N=0;for(const FEffect& E:Effects)if(E.Remaining>0.f)++N;return N;}
bool UBootRunEffectsComponent::HasEssentialWarning() const
{for(const FEffect& E:Effects)if(E.Remaining>0.f && E.bEssential)return true;return false;}
void UBootRunEffectsComponent::ResetRun()
{
    for(int32 I=0;I<Effects.Num();++I){Effects[I].Remaining=0.f;Parts[I]->SetVisibility(false);}
    for(UAudioComponent* Voice:Voices)if(Voice)Voice->Stop();
    ReuseCursor=AudioCursor=0;SetComponentTickEnabled(false);
}
void UBootRunEffectsComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ResetRun();for(UStaticMeshComponent* P:Parts)if(IsValid(P))P->DestroyComponent();
    for(UAudioComponent* Voice:Voices)if(IsValid(Voice))Voice->DestroyComponent();Voices.Reset();
    Parts.Reset();Materials.Reset();MaskMaterials.Reset();Effects.Reset();Super::EndPlay(Reason);
}
