#include "World/Generation/BootEndlessChunk.h"
#include "World/Interaction/BootSurfWindow.h"
#include "World/Threats/BootPopupProcess.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

double FBootEndlessReadiness::RailPeakCmps(double DistanceCm,double DurationSeconds)
{
    return FMath::IsFinite(DistanceCm)&&FMath::IsFinite(DurationSeconds)&&DistanceCm>=0.&&DurationSeconds>0.?1.5*DistanceCm/DurationSeconds:0.;
}
double FBootEndlessReadiness::RequiredDistanceCm(const FBootTraversalEnvelope& E,double Peak,double Assembly,double Reaction)
{
    FText Error;if(!FBootTraversalPolicy::Validate(E,Error)||!FMath::IsFinite(Peak)||Peak<0.||!FMath::IsFinite(Assembly)||Assembly<0.||!FMath::IsFinite(Reaction)||Reaction<.9)return TNumericLimits<double>::Max();
    return (double(E.TemporaryCmps)+FMath::Max(double(E.MaxBaseCmps),Peak))*(Assembly+Reaction+.3);
}
ABootEndlessChunk::ABootEndlessChunk()
{
    PrimaryActorTick.bCanEverTick=false;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("ChunkRoot"));SetRootComponent(Root);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Material(TEXT("/Game/Assets/V02/M_PixelSurface.M_PixelSurface"));
    for(int32 I=0;I<4;++I)
    {
        auto* Mesh=CreateDefaultSubobject<UInstancedStaticMeshComponent>(FName(*FString::Printf(TEXT("RecipeSolids%d"),I)));
        Mesh->SetupAttachment(Root);Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(Cube.Object);Mesh->SetMaterial(0,Material.Get());
        Mesh->SetCollisionProfileName(TEXT("BlockAll"));Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if(I==int32(EBootRecipeRole::Wall))Mesh->ComponentTags.AddUnique(TEXT("BootWallRun"));
        Mesh->CanCharacterStepUpOn=ECB_Yes;Mesh->SetGenerateOverlapEvents(false);Mesh->SetCanEverAffectNavigation(false);Mesh->SetCastShadow(false);Solids.Add(Mesh);
    }
}
bool ABootEndlessChunk::Configure(const FBootCommittedChunk& In)
{
    if(bReady||In.InstanceId<=0||!In.Run.RunId.IsValid()||In.WorldTransform.ContainsNaN()||In.Recipe.Boxes.Num()>32||In.Recipe.Rails.Num()+In.Recipe.Hazards.Num()>8)return false;
    Descriptor=In;SetActorTransform(In.WorldTransform);
    const FLinearColor Palette[]={FLinearColor(.78f,.8f,.78f),FLinearColor(.02f,.04f,.38f),FLinearColor(1.f,.65f,.1f),FLinearColor(.25f,.8f,.6f)};
    for(int32 I=0;I<Solids.Num();++I)
    {
        Solids[I]->ClearInstances();auto* Mat=UMaterialInstanceDynamic::Create(Solids[I]->GetMaterial(0),this);
        if(Mat){Mat->SetVectorParameterValue(TEXT("FlatColor"),Palette[I]);Solids[I]->SetMaterial(0,Mat);}
    }
    for(const auto& B:In.Recipe.Boxes)
    {
        if(B.Center.ContainsNaN()||B.HalfSize.ContainsNaN()||B.HalfSize.GetMin()<=0.)return false;
        const int32 RecipeRole=FMath::Clamp(int32(B.Role),0,3);
        Solids[RecipeRole]->AddInstance(FTransform(FQuat::Identity,B.Center,B.HalfSize*.02));
        if(B.Role==EBootRecipeRole::Floor)
        {
            // Essential lane edges are visible geometry without extra collision or actors.
            for(double Side:{-1.,1.})Solids[3]->AddInstance(FTransform(FQuat::Identity,
                B.Center+FVector(0.,Side*(B.HalfSize.Y-10.),B.HalfSize.Z+1.),FVector(B.HalfSize.X*2.,12.,2.)/100.));
        }
        else if(B.Role==EBootRecipeRole::Wall)
        {
            // Mint rails and end caps identify runnable surfaces from either side.
            // Decoration shares the chunk's transform and never changes collision.
            for(double Side:{-1.,1.})
            {
                const double FaceY=B.Center.Y+Side*(B.HalfSize.Y+1.5);
                for(double Height:{.25,.7})Solids[3]->AddInstance(FTransform(FQuat::Identity,
                    FVector(B.Center.X,FaceY,B.Center.Z-B.HalfSize.Z+2.*B.HalfSize.Z*Height),
                    FVector(2.*B.HalfSize.X,3.,10.)/100.));
                for(double End:{-1.,1.})Solids[3]->AddInstance(FTransform(FQuat::Identity,
                    FVector(B.Center.X+End*(B.HalfSize.X-8.),FaceY,B.Center.Z),
                    FVector(12.,3.,2.*B.HalfSize.Z-12.)/100.));
            }
        }
    }
    FActorSpawnParameters Params;Params.Owner=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for(const auto& Rail:In.Recipe.Rails)
    {
        const FTransform Start=FTransform(Rail.Start)*In.WorldTransform;
        auto* Window=GetWorld()->SpawnActor<ABootSurfWindow>(Start.GetLocation(),Start.Rotator(),Params);if(!Window)return false;
        Window->ConfigureOriented(Start,In.WorldTransform.TransformPosition(Rail.End),float(1.5*FVector::Distance(Rail.Start,Rail.End)/Rail.SpeedCmps),INDEX_NONE);
        Window->SetFlowRide(Rail.bFlowRide);
        FBootOperationKey Key;Key.Run=In.Run;Key.ChunkId=In.InstanceId;Key.LocalId=Rail.OperationId;Window->SetRunOperation(Key);Window->SetPreparedLandingComponent(Solids[0]);Window->SetReady(false);
        Window->SetActorEnableCollision(false);Windows.Add(Window);
    }
    for(const auto& H:In.Recipe.Hazards)
    {
        FBootOperationKey Key;Key.Run=In.Run;Key.ChunkId=In.InstanceId;Key.LocalId=H.OperationId;
        if(H.bPopup)
        {
            const FVector Start(H.Sweep.Max.X-500.,H.Sweep.GetCenter().Y,H.Sweep.Min.Z+82.);
            const FVector End(H.Sweep.Min.X+500.,H.Sweep.GetCenter().Y,H.Sweep.Min.Z+82.);
            const FTransform Frame=FTransform(Start)*In.WorldTransform;
            auto* Popup=GetWorld()->SpawnActor<ABootPopupProcess>(Frame.GetLocation(),Frame.Rotator(),Params);if(!Popup)return false;
            Popup->SetRunActive(false);Popup->ConfigureTelegraph(H.TelegraphSeconds);
            // Hostile motion approaches the runner; a successful interception heads
            // back toward the forward exit. Both use this chunk's local orientation.
            Popup->ConfigurePopup(Frame,End-Start,In.WorldTransform.TransformPosition(Start),4.f,INDEX_NONE,0);
            Popup->SetRunOperation(Key);Popup->SetReady(false);Popup->SetActorEnableCollision(false);
            Popup->OnRunThreatContact.AddWeakLambda(this,[this](ACharacter* Rider,const FBootOperationKey& Operation){if(Operation.Run==Descriptor.Run)OnRunThreatContact.Broadcast(Rider,Operation);});
            Windows.Add(Popup);Popups.Add(Popup);
        }
        else
        {
            const FVector Start(H.Sweep.Max.X-8.,H.Sweep.GetCenter().Y,H.Sweep.Min.Z+100.);
            const FVector End(H.Sweep.Min.X+8.,H.Sweep.GetCenter().Y,H.Sweep.Min.Z+100.);
            const FTransform Frame=FTransform(Start)*In.WorldTransform;
            auto* Scan=GetWorld()->SpawnActor<ABootScanProcess>(Frame.GetLocation(),Frame.Rotator(),Params);if(!Scan)return false;
            Scan->SetRunActive(false);Scan->Configure(Frame,End-Start,INDEX_NONE);Scan->ConfigureTelegraph(H.TelegraphSeconds);Scan->SetRunOperation(Key);
            Scan->OnRunThreatContact.AddWeakLambda(this,[this](ACharacter* Rider,const FBootOperationKey& Operation){if(Operation.Run==Descriptor.Run)OnRunThreatContact.Broadcast(Rider,Operation);});Scans.Add(Scan);
        }
    }
    return true;
}
bool ABootEndlessChunk::FinishPreparation()
{
    if(bReady)return true;if(Descriptor.Recipe.Boxes.IsEmpty())return false;
    FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_Pawn);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(BootEndlessAssembly),false,this);
    for(const auto& B:Descriptor.Recipe.Boxes)
    {
        if(B.Role==EBootRecipeRole::Decoration)continue;
        if(GetWorld()->OverlapAnyTestByObjectType(Descriptor.WorldTransform.TransformPosition(B.Center),Descriptor.WorldTransform.GetRotation(),Objects,FCollisionShape::MakeBox(B.HalfSize),Params))return false;
    }
    for(int32 I=0;I<3;++I)Solids[I]->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    for(ABootSurfWindow* Window:Windows){Window->SetActorEnableCollision(true);Window->SetReady(true);}
    bReady=true;SetProcessesActive(bProcessesActive);return true;
}
int32 ABootEndlessChunk::GetGameplayChildCount() const{return Windows.Num()+Scans.Num();}
double ABootEndlessChunk::GetPeakBaseCmps() const
{
    double Peak=0.;for(const ABootSurfWindow* Window:Windows)if(IsValid(Window))Peak=FMath::Max(Peak,Window->GetPeakTravelSpeedCmps());return Peak;
}
bool ABootEndlessChunk::OwnsSupport(const UPrimitiveComponent* Component) const
{
    if(!Component)return false;if(Component->GetOwner()==this)return true;
    for(const ABootSurfWindow* Window:Windows)if(IsValid(Window)&&Component->GetOwner()==Window)return true;return false;
}
bool ABootEndlessChunk::HasActiveRide() const
{
    for(const ABootSurfWindow* Window:Windows)if(IsValid(Window)&&Window->HasActiveCommitment())return true;return false;
}
bool ABootEndlessChunk::IsPreparedLanding(const ABootSurfWindow* Window,const ACharacter* Rider) const
{
    if(!bReady||!IsValid(Window)||!IsValid(Rider)||!Rider->GetCharacterMovement()->IsMovingOnGround())return false;
    const auto* Key=Window->GetRunOperation();if(!Key||Key->Run!=Descriptor.Run||Key->ChunkId!=Descriptor.InstanceId)return false;
    // Require a static recipe floor owned by this destination, not the departing platform.
    const UPrimitiveComponent* Base=Rider->GetMovementBase();if(!Base||Base->GetOwner()!=this||Base!=Solids[0])return false;
    const FVector Feet=Descriptor.WorldTransform.InverseTransformPosition(Rider->GetActorLocation()-FVector(0.,0.,Rider->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    for(const auto& Rail:Descriptor.Recipe.Rails)if(Rail.OperationId==Key->LocalId)
          return Feet.X>=(Rail.bFlowRide?Rail.Start.X:Rail.End.X)+Rail.PlatformHalfSize.X-50.&&FMath::Abs(Feet.Y-Rail.End.Y)<=650.;
    for(const auto& H:Descriptor.Recipe.Hazards)if(H.bPopup&&H.OperationId==Key->LocalId)
        return Feet.X>=H.Sweep.Max.X-550.&&Feet.X<=Descriptor.Recipe.Baseline.Last().X+50.;
    return false;
}
void ABootEndlessChunk::SetProcessesActive(bool Active)
{
    bProcessesActive=Active;for(ABootSurfWindow* Window:Windows)Window->SetRideSuspended(!Active);
    for(ABootScanProcess* Scan:Scans)Scan->SetRunActive(Active&&bReady&&Scan->GetRunOperation()&&ArmedProcessIds.Contains(Scan->GetRunOperation()->LocalId));
    for(ABootPopupProcess* Popup:Popups)Popup->SetRunActive(Active&&bReady&&Popup->GetRunOperation()&&ArmedProcessIds.Contains(Popup->GetRunOperation()->LocalId));
}
void ABootEndlessChunk::UpdateApproach(FVector Player,const FBootTraversalEnvelope& Envelope)
{
    if(!bReady||!bProcessesActive)return;
    const FVector Local=Descriptor.WorldTransform.InverseTransformPosition(Player);
    const double PreparationDistance=FBootEndlessReadiness::RequiredDistanceCm(Envelope,GetPeakBaseCmps());
    const double ApproachCap=double(Envelope.TemporaryCmps)+FMath::Max(double(Envelope.MaxBaseCmps),GetPeakBaseCmps());
    for(const auto& Hazard:Descriptor.Recipe.Hazards)
    {
        // Preparation happens earlier. An encounter begins only within a full warning's
        // maximum-speed lead, including a 100ms update margin and capsule clearance.
        const double Required=Hazard.bPopup?PreparationDistance:ApproachCap*(Hazard.TelegraphSeconds+.1)+100.;
        if(ArmedProcessIds.Contains(Hazard.OperationId)||Hazard.Sweep.ComputeSquaredDistanceToPoint(Local)>Required*Required)continue;
        ArmedProcessIds.Add(Hazard.OperationId);
        for(ABootScanProcess* Scan:Scans)
                        if(Scan->GetRunOperation()&&Scan->GetRunOperation()->LocalId==Hazard.OperationId)
            {
                // Keep a finite sweep present through ordinary run-speed arrival too.
                // This preserves maximum-speed warning safety without spending the threat
                // while a normal runner is still crossing the preparation horizon.
                const double Duration=(Required+Hazard.Sweep.GetSize().X)/FMath::Max(1.,double(Envelope.RunCmps))-Hazard.TelegraphSeconds+.25;
                Scan->ConfigureSweepDuration(Duration);Scan->SetRunActive(true);Scan->Arm(0);
            }
        for(ABootPopupProcess* Popup:Popups)
            if(Popup->GetRunOperation()&&Popup->GetRunOperation()->LocalId==Hazard.OperationId)Popup->SetRunActive(true);
    }
}
void ABootEndlessChunk::EndPlay(const EEndPlayReason::Type Reason)
{
    for(ABootSurfWindow* Window:Windows)if(IsValid(Window))Window->Destroy();for(ABootScanProcess* Scan:Scans)if(IsValid(Scan))Scan->Destroy();
    Windows.Empty();Scans.Empty();Popups.Empty();Super::EndPlay(Reason);
}




