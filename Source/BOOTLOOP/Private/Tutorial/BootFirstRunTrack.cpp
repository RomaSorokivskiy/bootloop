#include "Tutorial/BootFirstRunTrack.h"
#include "Player/BootRunner.h"
#include "Player/BootAbilityComponent.h"
#include "Movement/BootMovementComponent.h"
#include "World/Interaction/BootSurfWindow.h"
#include "World/Threats/BootScanProcess.h"
#include "World/Threats/BootPopupProcess.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ABootFirstRunTrack::ABootFirstRunTrack()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("FirstRunDesktop")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Game/Assets/V02/M_PixelSurface.M_PixelSurface"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Text(TEXT("/Engine/EngineMaterials/UnlitText.UnlitText"));
    SetSurfaceMaterial(Mat.Object); TextMaterial=Text.Object;
    auto Mesh=[&](FName Name,bool Solid)
    {
        auto* C=CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
        C->SetupAttachment(RootComponent); C->SetStaticMesh(Cube.Object); C->SetMobility(EComponentMobility::Movable);
        C->SetCollisionProfileName(Solid?TEXT("BlockAll"):TEXT("NoCollision"));
        C->SetCastShadow(false); C->SetCanEverAffectNavigation(false); return C;
    };
    Floors=Mesh(TEXT("FirstRunFloors"),true); Walls=Mesh(TEXT("FirstRunWallrun"),true); Beams=Mesh(TEXT("FirstRunLowMemory"),true);
    Walls->ComponentTags.Add(TEXT("BootWallRun"));
    for(int32 I=0;I<10;++I) Decor.Add(Mesh(FName(*FString::Printf(TEXT("FirstRunColor%d"),I)),false));
}
void ABootFirstRunTrack::BeginPlay()
{
    Super::BeginPlay(); const auto& Palette=GetPalette();
    for(int32 I=0;I<Decor.Num() && I<Palette.Num();++I) Decor[I]->SetMaterial(0,Palette[I]);
    Floors->SetMaterial(0,Palette[0]); Walls->SetMaterial(0,Palette[3]); Beams->SetMaterial(0,Palette[3]);
}
void ABootFirstRunTrack::Box(FVector P,FVector Size,int32 Color,int32 Collision)
{
    auto* M=Collision==1?Floors.Get():Collision==2?Walls.Get():Collision==3?Beams.Get():Decor[FMath::Clamp(Color,0,9)].Get();
    M->AddInstance(FTransform(FQuat::Identity,P,Size/100.f),true);
}
void ABootFirstRunTrack::Panel(FVector O,float A,float B,float Y,float Width,float Z)
{
    const float Length=B-A; if(Length<=0.f) return;
    Box(O+FVector((A+B)*.5f,Y,Z-42),FVector(Length,Width,84),0,1);
    Box(O+FVector((A+B)*.5f,Y,Z-102),FVector(Length+20,Width+20,36),2);
    Box(O+FVector((A+B)*.5f,Y-Width*.5f+12,Z+3),FVector(Length,24,6),4);
    Box(O+FVector((A+B)*.5f,Y+Width*.5f-12,Z+3),FVector(Length,24,6),9);
    Box(O+FVector(A+55,Y,Z+4),FVector(110,Width-32,8),3);
    for(float X=A+350;X<B-80;X+=550.f)
    {
        Box(O+FVector(X,Y,Z+5),FVector(90,100,10),7);
        Box(O+FVector(X-45,Y-70,Z+5),FVector(70,35,10),7);
        Box(O+FVector(X-45,Y+70,Z+5),FVector(70,35,10),7);
    }
}
void ABootFirstRunTrack::Sign(FVector P,const FString& Text,float Size)
{
    auto* T=NewObject<UTextRenderComponent>(this); AddInstanceComponent(T); T->SetupAttachment(RootComponent);
    T->SetWorldLocation(P); T->SetWorldRotation(FRotator(0,180,0)); T->SetText(FText::FromString(Text));
    T->SetTextMaterial(TextMaterial); T->SetTextRenderColor(FColor(245,205,96)); T->SetWorldSize(Size);
    T->SetHorizontalAlignment(EHTA_Center); T->SetVerticalAlignment(EVRTA_TextCenter);
    T->SetCollisionEnabled(ECollisionEnabled::NoCollision); T->SetCullDistance(5400); T->RegisterComponent(); Signs.Add(T);
}
void ABootFirstRunTrack::BuildSections()
{
    if(bBuilt) return;
    const TCHAR* Names[]={TEXT("FIRST LOGIN"),TEXT("MANUAL HOP"),TEXT("LOW MEMORY"),TEXT("NAVY EDGE"),TEXT("ALT-TAB"),TEXT("WINDOW SURF"),TEXT("SCAN + POPUP"),TEXT("LIVE CARDS"),TEXT("MUSIC FLOW")};
    for(int32 L=0;L<FBootFirstRunTutorial::LessonCount;++L)
    {
        FBootFirstRunSection S; S.Lesson=L; S.RouteName=FName(Names[L]); const FVector O(0,L*6000.,0);
        S.Frame=FTransform(O); S.Checkpoint=FTransform(FRotator::ZeroRotator,O+FVector(350,0,100)); Sections.Add(S);
        // Amber side lane is a continuous physical bypass, not invisible support under a gap.
        Panel(O,0,5200,-1200,800); Box(O+FVector(2300,-1250,8),FVector(800,34,12),5);
        Sign(O+FVector(850,-1250,270),TEXT("AMBER / SAFE BYPASS"),30);
        if(L==5 || L==6)
        {
            const float LaunchEnd=L==5?1400.f:2700.f;
            // Leave 10cm beyond the swept window endpoint: docking must not overlap its solid floor.
            const float LandingStart=L==5?3210.f:4410.f;
            Panel(O,0,LaunchEnd); Panel(O,LandingStart,5200,300,1000,160);
            // Short, reachable steps join the side bypass to the elevated landing.
            for(int32 I=0;I<4;++I) Panel(O,4400+I*200.f,4600+I*200.f,-550,700,(I+1)*40.f);
        }
        else if(L==0 || L==4)
        {
            Panel(O,0,1600); Panel(O,1820,5200);
            Sign(O+FVector(1350,0,280),L==0?TEXT("JUMP / MINT LANDING"):TEXT("AIM IN AIR / ALT-TAB"),32);
        }
        else Panel(O,0,5200);
        // Mint departure gate communicates where the current route goes, with no challenge timer.
        Box(O+FVector(5100,760,400),FVector(70,70,800),7);
        Box(O+FVector(5100,-760,400),FVector(70,70,800),7);
        Sign(O+FVector(800,0,450),FString::Printf(TEXT("%02d / %s"),L+1,Names[L]),45);
        if(L==2)
        {
            Box(O+FVector(1800,0,220),FVector(300,1050,220),3,3); // 110cm underside: crouch capsule clears.
            Sign(O+FVector(1460,-420,310),TEXT("SLIDE / THEN AIR SLIDE"),28);
            Panel(O,2300,2900,0,1100,80);
        }
        if(L==3)
        {
            Box(O+FVector(2200,740,300),FVector(3000,80,600),3,2);
            Box(O+FVector(2200,690,605),FVector(3000,32,30),5);
            Sign(O+FVector(1150,430,390),TEXT("JUMP ALONG NAVY / KICK AWAY"),28);
        }
        if(L==7) Sign(O+FVector(2400,0,420),TEXT("CHOOSE / DASH / GUARD"),40);
        if(L==8) Sign(O+FVector(2300,0,350),TEXT("QUEUE + TEMPO / NO FINISH LINE"),40);
    }
    const auto O=[&](int32 L,FVector P){return Sections[L].Frame.TransformPosition(P);};
    Surf=GetWorld()->SpawnActor<ABootSurfWindow>();
    Surf->ConfigureOriented(FTransform(O(5,{1900,0,0})),O(5,{2700,300,160}),1.45f,45001); OwnedActors.Add(Surf);
    Scan=GetWorld()->SpawnActor<ABootScanProcess>(); Scan->SetRunActive(false);
    Scan->Configure(FTransform(O(6,{1700,0,100})),FVector(-500,0,0),6); OwnedActors.Add(Scan);
    Popup=GetWorld()->SpawnActor<ABootPopupProcess>(); Popup->SetRunActive(false);
    Popup->ConfigurePopup(FTransform(O(6,{3200,0,0})),FVector(200,300,0),O(6,{3900,300,160}),1.45f,46001,Tutorial.GetSection()); OwnedActors.Add(Popup);
    GuardScan=GetWorld()->SpawnActor<ABootScanProcess>(); GuardScan->SetRunActive(false);
    GuardScan->Configure(FTransform(O(7,{3300,0,100})),FVector(-500,0,0),7); OwnedActors.Add(GuardScan);
    Scan->OnThreatContact.AddLambda([this](ACharacter* C,int32 G){ThreatContact(C,G,int64(Scan->GetUniqueID()),false);});
    Popup->OnThreatContact.AddLambda([this](ACharacter* C,int32 G){ThreatContact(C,G,int64(Popup->GetUniqueID()),false);});
    GuardScan->OnThreatContact.AddLambda([this](ACharacter* C,int32 G){ThreatContact(C,G,int64(GuardScan->GetUniqueID()),true);});
    bBuilt=true;
}
void ABootFirstRunTrack::ResetTrack(int32)
{
    BuildSections(); FBootRunToken Run; Run.RunId=FGuid::NewGuid(); Run.Generation=1; BeginFirstRun(Run);
}
void ABootFirstRunTrack::BeginFirstRun(const FBootRunToken& Run)
{
    BuildSections(); Tutorial=FBootFirstRunTutorial(); Tutorial.ResetLesson(Run,0,1);
    MovementSerial=uint64(1)<<63; PreparedSection=Tutorial.GetSection(); MovedCm=0.; bHistory=false;
    bProcessesActive=false; ResetProcesses(); UpdatePresentationOrigin(GetStartTransform().GetLocation());
}
bool ABootFirstRunTrack::AcceptEvidence(const FBootTutorialEvidence& Evidence) { return Tutorial.Accept(Evidence); }
FBootTutorialEvidence ABootFirstRunTrack::CaptureEvidence(EBootFirstRunEvent Event,uint64 Serial) const
{ return {Tutorial.GetRunToken(),Tutorial.GetLesson(),Tutorial.GetSection(),Serial,Event}; }
bool ABootFirstRunTrack::IsInActiveLesson(const ABootRunner* Runner) const
{
    if(!Runner || Tutorial.IsComplete() || !Sections.IsValidIndex(Tutorial.GetLesson())) return false;
    const auto& S=Sections[Tutorial.GetLesson()];
    return S.LocalBounds.IsInsideOrOn(S.Frame.InverseTransformPosition(Runner->GetActorLocation()));
}
FTransform ABootFirstRunTrack::GetStartTransform() const
{ return Sections.IsEmpty()?FTransform(FVector(350,0,100)):Sections[0].Checkpoint; }
FTransform ABootFirstRunTrack::ValidatedCheckpoint(const FTransform& Candidate,const ABootRunner* Runner) const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(FirstRunCheckpoint),false);
    if(Runner) Query.AddIgnoredActor(Runner);
    const float Radius=Runner?Runner->GetCapsuleComponent()->GetUnscaledCapsuleRadius():34.f;
    // Test standing clearance even when the restoring player died crouched.
    const float HalfHeight=FMath::Max(96.f,Runner?Runner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight():96.f);
    const auto Capsule=FCollisionShape::MakeCapsule(Radius,HalfHeight);
    const auto Safe=[&](const FVector& P)
    {
        FHitResult Floor,Ceiling;
        if(!GetWorld()->LineTraceSingleByChannel(Floor,P,P-FVector(0,0,180),ECC_Visibility,Query) || Floor.ImpactNormal.Z<.7f) return false;
        if(GetWorld()->OverlapBlockingTestByChannel(P,FQuat::Identity,ECC_Pawn,Capsule,Query)) return false;
        if(GetWorld()->SweepSingleByChannel(Ceiling,P+FVector(0,0,8),P,FQuat::Identity,ECC_Pawn,Capsule,Query)) return false;
        if(Sections.IsValidIndex(Tutorial.GetLesson()))
        {
            const auto Local=Sections[Tutorial.GetLesson()].Frame.InverseTransformPosition(P);
            // Authored full process sweeps are excluded even while those processes are dormant.
            const bool ScanEnvelope=Tutorial.GetLesson()==6 && Local.X>1100-Radius && Local.X<1750+Radius && FMath::Abs(Local.Y)<350+Radius;
            const bool PopupEnvelope=Tutorial.GetLesson()==6 && Local.X>2700-Radius && Local.X<3950+Radius && Local.Y>-420-Radius && Local.Y<720+Radius;
            const bool GuardEnvelope=Tutorial.GetLesson()==7 && Local.X>2750-Radius && Local.X<3350+Radius && FMath::Abs(Local.Y)<350+Radius;
            if(ScanEnvelope || PopupEnvelope || GuardEnvelope) return false;
        }
        return true;
    };
    FTransform Result=Candidate;
    for(const FVector Offset:{FVector::ZeroVector,FVector(0,-1200,0),FVector(350,-1200,0),FVector(650,-1200,0)})
    {
        const FVector P=Candidate.GetLocation()+Offset;
        if(Safe(P)) { Result.SetLocation(P); return Result; }
    }
    // An unexpected external blocker must not silently teleport to another lesson.
    UE_LOG(LogTemp,Error,TEXT("FirstRun: no safe checkpoint in lesson %d"),Tutorial.GetLesson());
    return Candidate;
}
FTransform ABootFirstRunTrack::PrepareCheckpoint(const FBootRunToken& Run,const ABootRunner* Runner)
{
    BuildSections(); if(Tutorial.IsComplete()) return Sections.Last().Checkpoint;
    const int32 Section=Tutorial.GetSection()==MAX_int32?1:Tutorial.GetSection()+1;
    Tutorial.ResetLesson(Run,Tutorial.GetLesson(),Section); PreparedSection=Section;
    MovedCm=0.; bHistory=false; ResetProcesses();
    const FTransform Safe=ValidatedCheckpoint(Sections[Tutorial.GetLesson()].Checkpoint,Runner);
    UpdatePresentationOrigin(Safe.GetLocation()); return Safe;
}
FTransform ABootFirstRunTrack::SkipCurrentLesson(const FBootRunToken& Run,const ABootRunner* Runner)
{ Tutorial.SkipLesson(); return PrepareCheckpoint(Run,Runner); }
void ABootFirstRunTrack::ResetProcesses()
{
    bScanArmed=bPopupArmed=bGuardArmed=bScanContact=bScanCleared=false;
    if(Scan) { Scan->SetRunActive(false); Scan->ResetProcess(Tutorial.GetSection()); }
    if(GuardScan) { GuardScan->SetRunActive(false); GuardScan->ResetProcess(Tutorial.GetSection()); }
    if(Popup) { Popup->SetRunActive(false); Popup->ResetProcess(Tutorial.GetSection()); }
    if(Surf) Surf->ResetWindow();
}
void ABootFirstRunTrack::SetProcessesActive(bool bActive)
{
    bProcessesActive=bActive;
    if(Scan) Scan->SetRunActive(bActive && bScanArmed);
    if(Popup) Popup->SetRunActive(bActive && bPopupArmed);
    if(GuardScan) GuardScan->SetRunActive(bActive && bGuardArmed);
    if(Surf) Surf->SetRideSuspended(!bActive);
}
void ABootFirstRunTrack::UpdateCourse(ABootRunner* Runner,float Dt)
{
    if(!Runner || !bBuilt || Dt<=0.f || !IsInActiveLesson(Runner) || Runner->IsDead()) { bHistory=false; return; }
    const FVector P=Runner->GetActorLocation(); UpdatePresentationOrigin(P);
    if(!bHistory) { PreviousPosition=P; bHistory=true; }
    const double Delta=FVector::Dist2D(P,PreviousPosition); PreviousPosition=P;
    // Large teleports are never movement evidence.
    if(Delta<500.) MovedCm+=Delta;
    if(Tutorial.GetLesson()==0 && MovedCm>=220.) AcceptEvidence(CaptureEvidence(EBootFirstRunEvent::Moved,++MovementSerial));
    if(!bProcessesActive) return;
    const FVector Local=Sections[Tutorial.GetLesson()].Frame.InverseTransformPosition(P);
    if(Tutorial.GetLesson()==6)
    {
        if(!bScanArmed && Local.X>=700.) { Scan->SetRunActive(true); Scan->Arm(Tutorial.GetSection()); bScanArmed=true; }
        if(bScanArmed && Scan->GetPhase()==EBootScanPhase::Clear) bScanCleared=true;
        if(bScanCleared && !bScanContact && Local.X>=1850.)
            AcceptEvidence(CaptureEvidence(EBootFirstRunEvent::ScanAvoided,++MovementSerial));
        if(Tutorial.GetObjectiveIndex()>=1 && !bPopupArmed && Local.X>=2100.)
        { Popup->SetRunActive(true); bPopupArmed=true; }
    }
    if(Tutorial.GetLesson()==7 && Tutorial.GetObjectiveIndex()>=2 && Local.X>=1800.
        && (!bGuardArmed || GuardScan->GetPhase()==EBootScanPhase::Dormant))
    { GuardScan->SetRunActive(true); GuardScan->Arm(Tutorial.GetSection()); bGuardArmed=true; }
}
void ABootFirstRunTrack::ThreatContact(ACharacter* Character,int32 Generation,int64 ProcessId,bool bGuardLesson)
{
    auto* Runner=Cast<ABootRunner>(Character);
    const int32 ExpectedLesson=bGuardLesson?7:6;
    if(!bProcessesActive || !Runner || Runner->IsDead() || Generation!=Tutorial.GetSection()
        || Tutorial.GetLesson()!=ExpectedLesson || !IsInActiveLesson(Runner)) return;
    if(bGuardLesson && Runner->GetAbilities() && Runner->GetAbilities()->TryAbsorbProcess(Tutorial.GetRunToken(),ProcessId,double(GetWorld()->GetTimeSeconds())))
    { AcceptEvidence(CaptureEvidence(EBootFirstRunEvent::GuardTriggered,++MovementSerial)); return; }
    if(!bGuardLesson) bScanContact=true;
    Runner->Die();
}
bool ABootFirstRunTrack::CanCompleteSurf(const ABootSurfWindow* Window,const ABootRunner* Runner,int32 ActivationSection) const
{
    if(!Window || !Runner || ActivationSection!=Tutorial.GetSection() || !IsInActiveLesson(Runner)
        || !Window->HasCarried(const_cast<ABootRunner*>(Runner)) || !Runner->GetParkour()->IsMovingOnGround()) return false;
    const int32 L=Tutorial.GetLesson();
    if(!((L==5 && Window==Surf.Get()) || (L==6 && Window==Popup.Get()))) return false;
    const FVector Feet=Runner->GetActorLocation()-FVector(0,0,Runner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const FVector Local=Sections[L].Frame.InverseTransformPosition(Feet);
    if(Local.X<(L==5?3210.:4410.) || Local.X>5200. || Local.Y<-200. || Local.Y>800. || FMath::Abs(Local.Z-160.)>Runner->GetParkour()->MaxStepHeight) return false;
    FHitResult Support; FCollisionQueryParams Query(SCENE_QUERY_STAT(FirstRunSurfLanding),false,Runner);
    return GetWorld()->LineTraceSingleByChannel(Support,Feet+FVector(0,0,5),Feet-FVector(0,0,20),ECC_Visibility,Query)
        && Runner->GetParkour()->IsWalkable(Support) && Support.GetActor()!=Window;
}



