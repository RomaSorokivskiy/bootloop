#include "Player/BootAvatarComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    enum EShellMesh { TorsoMesh, HipMesh, UpperArmMesh, ForearmMesh, ThighMesh,
        ShinMesh, JointMesh, PalmMesh, FingerMesh, WristMesh, BootMesh, RedirectCueMesh };
}

UBootAvatarComponent::UBootAvatarComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    const TCHAR* Names[] = {TEXT("Torso"), TEXT("Hip"), TEXT("UpperArm"), TEXT("Forearm"), TEXT("Thigh"),
        TEXT("Shin"), TEXT("Joint"), TEXT("Palm"), TEXT("Finger"), TEXT("Wrist"), TEXT("Boot")};
    for (const TCHAR* Name : Names)
    {
        ConstructorHelpers::FObjectFinder<UStaticMesh> Asset(*FString::Printf(
            TEXT("/Game/Assets/V04/Avatar/SM_Shell%s.SM_Shell%s"), Name, Name));
        MeshAssets.Add(Asset.Object);
    }
    ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Game/Assets/V04/Avatar/M_PixelBody.M_PixelBody"));
    BodyMaterial = Material.Object;
    ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    MeshAssets.Add(Cube.Object);
    ConstructorHelpers::FObjectFinder<UMaterialInterface> CueMaterial(
        TEXT("/Game/Assets/V02/M_PixelSurface.M_PixelSurface"));
    RedirectCueMaterial = CueMaterial.Object;
}

UStaticMeshComponent* UBootAvatarComponent::MakePart(const TCHAR* Name, int32 MeshIndex)
{
    AActor* Owner = GetOwner();
    if (!Owner || !BodyRoot || !MeshAssets.IsValidIndex(MeshIndex) || !MeshAssets[MeshIndex]) return nullptr;
    UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner, Name);
    Owner->AddInstanceComponent(Part);
    Part->SetupAttachment(BodyRoot);
    Part->SetStaticMesh(MeshAssets[MeshIndex]);
    if (BodyMaterial)
        for (int32 Slot = 0; Slot < FMath::Max(1, Part->GetNumMaterials()); ++Slot)
            Part->SetMaterial(Slot, BodyMaterial);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Part->SetGenerateOverlapEvents(false);
    Part->SetCanEverAffectNavigation(false);
    Part->SetCastShadow(false);
    Part->SetOnlyOwnerSee(true);
    Part->bReceivesDecals = false;
    Part->RegisterComponent();
    Parts.Add(Part);
    return Part;
}

void UBootAvatarComponent::BeginPlay()
{
    Super::BeginPlay();
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (!Character || BodyRoot) return;
    BodyRoot = NewObject<USceneComponent>(Character, TEXT("ShellRunnerBody"));
    Character->AddInstanceComponent(BodyRoot);
    BodyRoot->SetupAttachment(Character->GetCapsuleComponent());
    BodyRoot->RegisterComponent();
    Torso = MakePart(TEXT("ShellTorso"), TorsoMesh);
    Hip = MakePart(TEXT("ShellHip"), HipMesh);
    for (int32 Side = 0; Side < 2; ++Side)
    {
        auto Part = [&](const TCHAR* Name, int32 Mesh)
        { return MakePart(*FString::Printf(TEXT("Shell%s%d"), Name, Side), Mesh); };
        FArmParts& Arm = Arms[Side];
        Arm.Upper = Part(TEXT("UpperArm"), UpperArmMesh);
        Arm.Elbow = Part(TEXT("Elbow"), JointMesh);
        Arm.Forearm = Part(TEXT("Forearm"), ForearmMesh);
        Arm.Wrist = Part(TEXT("Wrist"), WristMesh);
        Arm.Palm = Part(TEXT("Palm"), PalmMesh);
        for (int32 Finger = 0; Finger < 10; ++Finger)
            Arm.Fingers.Add(MakePart(*FString::Printf(TEXT("ShellFinger%d_%d"), Side, Finger), FingerMesh));
        FLegParts& Leg = Legs[Side];
        Leg.Thigh = Part(TEXT("Thigh"), ThighMesh);
        Leg.Knee = Part(TEXT("Knee"), JointMesh);
        Leg.Shin = Part(TEXT("Shin"), ShinMesh);
        Leg.Boot = Part(TEXT("Boot"), BootMesh);
    }
    // A small physical status mark sits within the left CRT screen. Its shape
    // as well as its color changes, without replacing the authored shell mesh.
    RedirectCue = MakePart(TEXT("ShellRedirectCue"), RedirectCueMesh);
    if (RedirectCue && Arms[0].Wrist)
    {
        RedirectCue->AttachToComponent(Arms[0].Wrist, FAttachmentTransformRules::KeepRelativeTransform);
        RedirectCue->SetRelativeLocation(FVector(0, 0, 3.8f));
        RedirectCueMID = RedirectCueMaterial ? UMaterialInstanceDynamic::Create(RedirectCueMaterial, this) : nullptr;
        if (RedirectCueMID) RedirectCue->SetMaterial(0, RedirectCueMID);
    }
    SetBodyVisible(bBodyVisible);
    ResetPose();
}

void UBootAvatarComponent::Place(UStaticMeshComponent* Part, const FVector& Position,
    const FRotator& Rotation, const FVector& Scale)
{
    if (!Part) return;
    Part->SetRelativeLocationAndRotation(Position, Rotation);
    Part->SetRelativeScale3D(Scale);
}

void UBootAvatarComponent::Segment(UStaticMeshComponent* Part, const FVector& Start, const FVector& End, float Length, float Width)
{
    const FVector Direction = End - Start;
    Place(Part, (Start + End) * .5f, FRotationMatrix::MakeFromZ(Direction).Rotator(),
        FVector(Width, Width, Direction.Size() / Length));
}

void UBootAvatarComponent::UpdatePose(float Dt, float Speed, bool bGrounded, bool bSliding,
    bool bWallRunning, float WallSide)
{
    FBootPoseInput Input;
    Input.LocalVelocity = FVector(Speed, 0, 0); Input.WorldPlanarSpeed = Speed;
    Input.bGrounded = bGrounded; Input.bSliding = bSliding;
    Input.bWallRunning = bWallRunning; Input.WallSide = WallSide;
    if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        Input.AimPitch = FRotator::NormalizeAxis(Character->GetBaseAimRotation().Pitch);
        Input.bCrouched = Character->bIsCrouched;
        Input.VerticalVelocity = Character->GetVelocity().Z;
    }
    UpdatePose(Dt, Input);
}


void UBootAvatarComponent::UpdatePose(float Dt, const FBootPoseInput& Input)
{
    ACharacter* Character=Cast<ACharacter>(GetOwner());
    if(!Character) return;
    if(RedirectCue)
    {
        RedirectCue->SetRelativeScale3D(Input.bAirRedirectReady?FVector(.045f,.012f,.005f):FVector(.012f,.012f,.005f));
        if(RedirectCueMID) RedirectCueMID->SetVectorParameterValue(TEXT("FlatColor"),Input.bAirRedirectReady?
            FLinearColor(.18f,.85f,.55f):FLinearColor(.18f,.09f,.025f));
    }
    if(GetWorld() && GetWorld()->IsPaused()) Dt=0.f;
    Dt=FMath::IsFinite(Dt)?FMath::Clamp(Dt,0.f,.1f):0.f;
    FBootPoseInput Local=Input;
    Local.CapsuleHalfHeight=Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    const FVector OwnerPosition=Character->GetActorLocation();
    const float Speed=Local.LocalVelocity.ContainsNaN()?0.f:float(Local.LocalVelocity.Size2D());
    const float WorldSpeed=FMath::IsFinite(Local.WorldPlanarSpeed)?FMath::Abs(Local.WorldPlanarSpeed):0.f;
    const float Travel=FMath::Max(Speed,WorldSpeed)*Dt;
    const bool bTeleported=bHasPreviousInput && FVector::DistSquared(OwnerPosition,PreviousOwnerPosition)>
        FMath::Square(FMath::Max(250.f,Travel*2.f+100.f));
    if(bTeleported) PoseState=FBootAvatarPoseState{};
    if(bPendingSnap && Dt>0.f){Local.AbilityEvent=EBootPoseAbilityEvent::Snap;bPendingSnap=false;}
    const FBootAvatarPose Pose=FBootAvatarPoseSolver::Solve(Local,Dt,PoseState);
    if(Dt>0.f){bHasPreviousInput=true;PreviousOwnerPosition=OwnerPosition;}
    Place(Hip,Pose.Pelvis.GetLocation(),Pose.Pelvis.Rotator());
    Place(Torso,Pose.Torso.GetLocation(),Pose.Torso.Rotator());
    for(int32 Side=0;Side<2;++Side)
    {
        FLegParts& Leg=Legs[Side];
        Segment(Leg.Thigh,Pose.Hips[Side],Pose.Knees[Side],FBootAvatarPose::ThighCm);
        Place(Leg.Knee,Pose.Knees[Side]);
        Segment(Leg.Shin,Pose.Knees[Side],Pose.Ankles[Side],FBootAvatarPose::ShinCm);
        Place(Leg.Boot,Pose.Ankles[Side]+FVector(0,0,-6.f),Pose.Boots[Side]);
        FArmParts& Arm=Arms[Side];
        const FVector Shoulder=Pose.Shoulders[Side].GetLocation(),Wrist=Pose.Wrists[Side];
        Segment(Arm.Upper,Shoulder,Pose.Elbows[Side],FBootAvatarPose::UpperArmCm,.85f);
        Place(Arm.Elbow,Pose.Elbows[Side],FRotationMatrix::MakeFromZ(Wrist-Pose.Elbows[Side]).Rotator());
        Segment(Arm.Forearm,Pose.Elbows[Side],Wrist,FBootAvatarPose::ForearmCm,.85f);
        const FRotator HandRotation=Pose.Hands[Side]; const FQuat Hand=HandRotation.Quaternion();
        Place(Arm.Wrist,Wrist+Hand.RotateVector(FVector(-5,0,9)),HandRotation);
        Place(Arm.Palm,Wrist+Hand.RotateVector(FVector(9,0,0)),HandRotation);
        const float Sign=Side==0?-1.f:1.f;
        for(int32 Finger=0;Finger<5 && Arm.Fingers.Num()>=10;++Finger)
        {
            const bool bThumb=Finger==4;
            const float FingerY=bThumb?-Sign*10.f:(Finger-1.5f)*4.2f;
            FRotator Rotation=HandRotation;
            Rotation.Yaw+=bThumb?-Sign*30.f:(Finger-1.5f)*2.f;
            const FVector Knuckle=Wrist+Hand.RotateVector(FVector(bThumb?7.f:19.f,FingerY,bThumb?-2.f:0.f));
            Place(Arm.Fingers[Finger*2],Knuckle,Rotation);
            Rotation.Pitch-=18.f+PoseState.JogBlend*8.f;
            Place(Arm.Fingers[Finger*2+1],Knuckle+Rotation.RotateVector(FVector(5.4f,0,0)),Rotation);
        }
        if(Pose.ContactFoot==Side && !bTeleported)
            OnFootContact.Broadcast(Side,Character->GetActorTransform().TransformPosition(Pose.Ankles[Side]+FVector(5,0,-15)));
    }
}

void UBootAvatarComponent::ResetPose()
{
    PoseState=FBootAvatarPoseState{};
    bPendingSnap=false; bHasPreviousInput=false;
    PreviousOwnerPosition=GetOwner()?GetOwner()->GetActorLocation():FVector::ZeroVector;
    UpdatePose(0.f,FBootPoseInput{});
}

void UBootAvatarComponent::SetBodyVisible(bool bVisible)
{
    bBodyVisible = bVisible;
    if (BodyRoot) BodyRoot->SetVisibility(bVisible, true);
}

void UBootAvatarComponent::TriggerSnap()
{
    bPendingSnap = true;
}
