#include "World/Generation/BootCourseTrack.h"
#include "Player/BootRunner.h"
#include "World/Interaction/BootSurfWindow.h"
#include "Movement/BootMovementComponent.h"
#include "Run/BootGameMode.h"
#include "World/Threats/BootScanProcess.h"
#include "World/Threats/BootPopupProcess.h"
#include "World/Interaction/BootAssemblingPlatform.h"
#include "World/Presentation/BootDesktopBackdrop.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ABootCourseTrack::ABootCourseTrack()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("DesktopCourse")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Bevel(TEXT("/Game/Assets/V02/SM_GUIBevel.SM_GUIBevel"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Game/Assets/V02/M_PixelSurface.M_PixelSurface"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Text(TEXT("/Engine/EngineMaterials/UnlitText.UnlitText"));
    SetSurfaceMaterial(Mat.Object); TextMaterial=Text.Object;
    auto Mesh=[&](FName Name,bool Solid)
    {
        auto* C=CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
        C->SetupAttachment(RootComponent); C->SetStaticMesh(Cube.Object); C->SetMobility(EComponentMobility::Movable);
        C->SetCollisionProfileName(Solid?TEXT("BlockAll"):TEXT("NoCollision")); C->SetCastShadow(false); C->SetCanEverAffectNavigation(false); return C;
    };
    Floors=Mesh(TEXT("Foundations"),true); Walls=Mesh(TEXT("RunnableFrames"),true); Beams=Mesh(TEXT("LowMemory"),true);
    Walls->ComponentTags.Add(TEXT("BootWallRun"));
    for(int32 I=0;I<10;++I) { auto* M=Mesh(FName(*FString::Printf(TEXT("CourseColor%d"),I)),false); M->SetStaticMesh(Bevel.Object); Tiles.Add(M); }
}
void ABootCourseTrack::BeginPlay()
{
    Super::BeginPlay(); const auto& P=GetPalette();
    const FColor C[]={FColor(232,223,195),FColor(143,150,127),FColor(16,31,48),FColor(23,43,103),FColor(249,241,215),FColor(242,188,83),FColor(218,74,75),FColor(118,212,174),FColor(6,76,84),FColor(65,107,105)};
    for(int32 I=0;I<10;++I) { P[I]->SetVectorParameterValue(TEXT("FlatColor"),FLinearColor(C[I])); Tiles[I]->SetMaterial(0,P[I]); }
    Floors->SetMaterial(0,P[0]); Walls->SetMaterial(0,P[3]); Beams->SetMaterial(0,P[3]);
}
void ABootCourseTrack::Box(const FTransform& F,FVector P,FVector Size,int32 Color,int32 Collision)
{
    auto* M=Collision==1?Floors.Get():Collision==2?Walls.Get():Collision==3?Beams.Get():Tiles[Color].Get();
    M->AddInstance(FTransform(F.GetRotation(),F.TransformPosition(P),Size/100.),true);
}
void ABootCourseTrack::Sign(const FTransform& Frame,FVector P,const FString& Text,float Size)
{
    auto* T=NewObject<UTextRenderComponent>(this); AddInstanceComponent(T); T->SetupAttachment(RootComponent);
    T->SetWorldLocation(Frame.TransformPosition(P)); T->SetWorldRotation(Frame.Rotator()+FRotator(0,180,0));
    T->SetText(FText::FromString(Text)); T->SetTextMaterial(TextMaterial); T->SetTextRenderColor(FColor(245,205,96));
    T->SetWorldSize(Size); T->SetHorizontalAlignment(EHTA_Center); T->SetVerticalAlignment(EVRTA_TextCenter);
    T->SetCollisionEnabled(ECollisionEnabled::NoCollision); T->RegisterComponent(); Signs.Add(T);
}
void ABootCourseTrack::Panel(const FBootCourseNode& N,float A,float B,float Y,float Width,float Z)
{
    const float Mid=(A+B)*.5f,L=B-A; const auto& F=N.Transform;
    // The panel owns a stationary final collider and animates only its casing.
    // Stair risers stay visible/solid; wide landings reveal ahead of physical arrival.
    if(!(N.Id==4 && !bTraining) && L>=450.f)
    {
        auto* Platform=GetWorld()->SpawnActor<ABootAssemblingPlatform>();
        Platform->Configure(FTransform(F.GetRotation(),F.TransformPosition({Mid,Y,Z})),FVector(L,Width,84),1.1f,N.GroupId);
        AssemblingPlatforms.Add(Platform); OwnedActors.Add(Platform);
        ActorGroups.Add(Platform->GetUniqueID(),N.GroupId);
        return;
    }
    Box(F,{Mid,Y,Z-42},{L,Width,84},0,1);
    Box(F,{Mid,Y,Z-105},{L+24,Width+24,46},2);
    Box(F,{Mid,Y-Width*.5+14,Z+3},{L,28,6},4);
    Box(F,{Mid,Y+Width*.5-14,Z+3},{L,28,6},1);
    Box(F,{A+60,Y,Z+3},{120,Width-40,6},3);
    for(float X=A+360;X<B-100;X+=500.f)
    {
        Box(F,{X,Y,Z+5},{90,100,8},7);
        Box(F,{X-45,Y-70,Z+5},{75,40,8},7); Box(F,{X-45,Y+70,Z+5},{75,40,8},7);
    }
}
void ABootCourseTrack::AddNode(int32 Id,FVector Origin,float Yaw,float Length,float From,float To,TArray<int32> Next,int32 Lesson)
{
    FBootCourseNode N; N.Id=Id; N.Transform=FTransform(FRotator(0,Yaw,0),Origin); N.LengthCm=Length; N.WidthCm=1600;
    N.ProgressStartMetres=From; N.ProgressEndMetres=To; N.SuccessorIds=Next;
    N.EntryBounds=FBox(FVector(-40,-900,-250),FVector(40,900,1000));
    N.ExitBounds=FBox(FVector(Length-40,-900,-250),FVector(Length+40,900,1000));
    N.CheckpointTransform=FTransform(N.Transform.GetRotation(),N.Transform.TransformPosition({350,0,100}));
    N.GroupId=Id; N.LessonId=Lesson; Nodes.Add(N);
}
void ABootCourseTrack::BuildCourse(int32 Seed)
{
    Nodes.Reset(); SurfDestinations.Reset(); CheckpointThreatSweeps.Reset();
    if(bTraining)
    {
        AddNode(1,{0,0,0},0,3000,0,30,{2},1);
        AddNode(2,{3000,0,0},0,3000,30,60,{3},2);
        AddNode(3,{6000,0,0},90,3000,60,90,{4},3);
        AddNode(4,{6000,3000,0},0,3600,90,126,{5},4);
        AddNode(5,{9600,3000,0},90,3000,126,156,{6},5);
        AddNode(6,{9600,6000,0},180,3000,156,186,{7},6);
        AddNode(7,{6600,6000,0},90,4200,186,228,{8},7);
        AddNode(8,{6600,10200,0},180,4200,228,270,{},8);
        // These turned entries sit alongside the previous elevated surf/popup
        // landing. Start farther along the new floor, beyond that ledge's width.
        for(auto& N:Nodes) if(N.Id==5 || N.Id==8)
            N.CheckpointTransform.SetLocation(N.Transform.TransformPosition({1000,0,100}));
    }
    else
    {
        AddNode(1,{0,0,0},0,4000,0,40,{11}); AddNode(11,{4000,0,0},0,2500,40,65,{2});
        AddNode(2,{6500,0,0},90,8000,65,145,{3});
        AddNode(3,{6500,8000,0},0,8500,145,230,{4,30});
        AddNode(4,{15000,8000,0},90,6500,230,295,{5});
        AddNode(5,{15000,14500,600},180,13000,295,425,{6});
        AddNode(6,{2000,14500,600},-90,14500,425,570,{7});
        AddNode(7,{2000,0,600},0,14000,570,710,{8});
        AddNode(8,{16000,0,600},-90,9000,710,800,{9});
        AddNode(9,{16000,-9000,600},180,10000,800,900,{});
        AddNode(30,{8100,7520,220},0,5700,161,218,{4});
        Nodes.Last().WidthCm=660;
        Nodes.Last().EntryBounds=FBox(FVector(-20,-240,-80),FVector(20,240,500));
    }
    const TCHAR* Lessons[]={TEXT("01 / FIRST LOGIN"),TEXT("02 / LOW MEMORY"),TEXT("03 / HOP BUFFER"),TEXT("04 / WINDOW SURF"),TEXT("05 / NAVY EDGE"),TEXT("06 / ALT-TAB"),TEXT("07 / SCAN + POPUP"),TEXT("08 / FREE PROCESS")};
    auto LowerBypass=[&](const FBootCourseNode& N,float From)
    {
        Panel(N,From,N.LengthCm,-850,900,-160);
        // Four readable risers stay below MaxStepHeight, with a level return pad
        // into the broad junction. Y<=-400 leaves the initial window casing clear.
        const float RampStart=N.LengthCm-1640.f;
        for(int32 I=0;I<4;++I) Panel(N,RampStart+I*160.f,RampStart+(I+1)*160.f,-850,900,-120.f+I*40.f);
        Panel(N,N.LengthCm-1000.f,N.LengthCm,-850,900,0);
    };
    for(const auto& N:Nodes)
    {
        const float Step=bTraining?1500.f:1800.f;
        const bool SurfSection=bTraining?N.Id==4:N.Id==1;
        const bool PopupSection=bTraining?N.Id==7:N.Id==8;
        for(float A=0;!SurfSection && !PopupSection && A<N.LengthCm;A+=Step)
        {
            float B=FMath::Min(N.LengthCm,A+Step);
            // Short readable gaps have a full-width launch and landing at base speed.
            const bool Gap=!bTraining && N.Id!=4 && N.Id!=30 && A>0 && int32(A/Step)%3==2;
            Panel(N,A+(Gap?240.f:0.f),B,0,N.WidthCm-200.f);
        }
        if(SurfSection)
        {
            Panel(N,0,1300);
            LowerBypass(N,1300);
            Panel(N,3010,N.LengthCm,380,900,160);
            Sign(N.Transform,{1100,-450,250},TEXT("AMBER / BYPASS"),32);
        }
        if(PopupSection)
        {
            Panel(N,0,1900);
            LowerBypass(N,1900);
            Panel(N,3610,N.LengthCm,380,900,160);
            Sign(N.Transform,{1550,350,250},TEXT("POPUP / SNAP"),32);
        }
        // Broad junctions let the player choose when to turn; the camera stays free.
        if(N.Id!=30) Box(N.Transform,{0,0,-42},{1700,1800,84},0,1);
        // Special sections already have a short caption beside their object.
        // Avoid a second centred title projecting over the same approach view.
        const bool NamedSection=SurfSection || PopupSection || N.Id==2 || N.Id==(bTraining?7:4);
        if(!NamedSection) Sign(N.Transform,{N.Id==1?1400.f:850.f,0,400},bTraining?Lessons[N.LessonId-1]:FString::Printf(TEXT("SECTOR %02d"),N.Id),42.f);
        // Frame edges support genuine CharacterMovement wallrun, with an open floor bypass.
        if(N.Id==5 || N.Id==3 || (!bTraining && N.Id==7))
        {
            Box(N.Transform,{1600,740,280},{2100,80,560},3,2);
            Box(N.Transform,{1600,690,565},{2100,28,30},5);
        }
    }
    const auto& Low=Nodes[bTraining?1:2];
    Box(Low.Transform,{1600,0,220},{220,900,220},3,3);
    Sign(Low.Transform,{1300,-400,300},TEXT("LOW MEMORY"),32);
    if(!bTraining)
    {
        // A gentle solid staircase rises to the return deck; each riser is below MaxStepHeight.
        const auto& Rise=Nodes[4];
        for(int32 I=0;I<20;++I) Panel(Rise,I*325.f,(I+1)*325.f,0,1400,(I+1)*30.f);
        // Elevated optional line over the gallery. Lower floors remain the visible safe route.
        const auto& Gallery=Nodes[3];
        for(int32 I=0;I<6;++I) Panel(Gallery,900+I*120.f,1020+I*120.f,-480,460,(I+1)*(220.f/6.f));
        Sign(Gallery.Transform,{1150,-450,420},TEXT("UPPER CACHE / OPTIONAL"),48);
    }
    const auto& SurfNode=Nodes[bTraining?3:0];
    auto* Window=GetWorld()->SpawnActor<ABootSurfWindow>();
    const FVector Start=SurfNode.Transform.TransformPosition({1800,0,0});
    const FVector End=SurfNode.Transform.TransformPosition({2500,380,160});
    Window->ConfigureOriented(FTransform(SurfNode.Transform.GetRotation(),Start),End,1.45f,bTraining?4001:1001); OwnedActors.Add(Window);
    SurfDestinations.Add(Window->GetWindowId(),bTraining?5:11);
    ActorGroups.Add(Window->GetUniqueID(),SurfNode.GroupId);
    const auto& ScanNode=*Nodes.FindByPredicate([&](const FBootCourseNode& N){return N.Id==(bTraining?7:4);});
    auto* Scan=GetWorld()->SpawnActor<ABootScanProcess>();
    Scan->SetRunActive(false);
    const FTransform ScanFrame(ScanNode.Transform.GetRotation(),ScanNode.Transform.TransformPosition({1450,0,100}));
    const FVector ScanEnd(-950,0,0);
    Scan->Configure(ScanFrame,ScanEnd,ScanNode.GroupId);
    // Include the whole authored path, with a margin around the scan's 8x300x100 beam.
    AddCheckpointThreatSweep(ScanFrame,ScanEnd,FVector(16,320,110));
    if(auto* Mode=GetWorld()->GetAuthGameMode<ABootGameMode>()) Scan->OnThreatContact.AddUObject(Mode,&ABootGameMode::HandleThreat);
    OwnedActors.Add(Scan); ActorGroups.Add(Scan->GetUniqueID(),ScanNode.GroupId);
    Sign(ScanNode.Transform,{1000,-450,300},TEXT("SCAN / BYPASS"),32);
    const auto& PopupNode=*Nodes.FindByPredicate([&](const FBootCourseNode& N){return N.Id==(bTraining?7:8);});
    auto* Popup=GetWorld()->SpawnActor<ABootPopupProcess>(); Popup->SetRunActive(false);
    const FTransform PopupFrame(PopupNode.Transform.GetRotation(),PopupNode.Transform.TransformPosition({2400,0,0}));
    const FVector PopupEnd(200,300,0);
    Popup->ConfigurePopup(PopupFrame,PopupEnd,PopupNode.Transform.TransformPosition({3100,380,160}),1.45f,bTraining?7001:8001,AttemptGeneration);
    // Conservatively cover both the hostile panel and the solid moving casing.
    AddCheckpointThreatSweep(PopupFrame,PopupEnd,FVector(500,415,100));
    if(auto* Mode=GetWorld()->GetAuthGameMode<ABootGameMode>()) Popup->OnThreatContact.AddUObject(Mode,&ABootGameMode::HandleThreat);
    OwnedActors.Add(Popup); ActorGroups.Add(Popup->GetUniqueID(),PopupNode.GroupId);
    SurfDestinations.Add(Popup->GetWindowId(),bTraining?8:9);
    const auto& Last=*Nodes.FindByPredicate([&](const FBootCourseNode& N){return N.Id==(bTraining?8:9);});
    Box(Last.Transform,{Last.LengthCm,-850,520},{90,90,1200},7);
    Box(Last.Transform,{Last.LengthCm,850,520},{90,90,1200},7);
    Box(Last.Transform,{Last.LengthCm,0,1120},{90,1800,90},7);
    Sign(Last.Transform,{Last.LengthCm-80,0,1120},TEXT("PROCESS ESCAPED"),100);
    auto* Backdrop=GetWorld()->SpawnActor<ABootDesktopBackdrop>();
    Backdrop->ConfigureDistrict(FTransform(FVector(7500,3000,0)),Seed); OwnedActors.Add(Backdrop);
    bBuilt=true;
}
void ABootCourseTrack::ResetTrack(int32 Seed)
{
    if(!bBuilt) BuildCourse(Seed);
    Tutorial=FBootTutorialState(); Tutorial.ActivateLesson(1);
    if(auto* Mode=GetWorld()->GetAuthGameMode<ABootGameMode>()) AttemptGeneration=Mode->GetAttemptGeneration();
    Tutorial.Generation=AttemptGeneration;
    ArmedProcesses.Reset(); bProcessesActive=false;
    for(AActor* A:OwnedActors)
    {
        if(auto* P=Cast<ABootPopupProcess>(A)) { P->SetRunActive(false); P->ResetProcess(AttemptGeneration); }
        else if(auto* S=Cast<ABootScanProcess>(A)) { S->SetRunActive(false); S->ResetProcess(AttemptGeneration); }
        else if(auto* W=Cast<ABootSurfWindow>(A)) W->ResetWindow();
    }
    Course.Initialize(Nodes,Nodes[0].Id,GetStartTransform().GetLocation());
    PreviousPosition=GetStartTransform().GetLocation(); bHistoryValid=true; SectionMovedCm=0;
    for(ABootAssemblingPlatform* Platform:AssemblingPlatforms) if(IsValid(Platform)) Platform->ResetAssembly(false);
    PrepareNearby(GetStartTransform().GetLocation(),true);
}
FTransform ABootCourseTrack::GetStartTransform() const
{ return Nodes.IsEmpty()?FTransform(FRotator::ZeroRotator,FVector(350,0,100)):Nodes[0].CheckpointTransform; }
void ABootCourseTrack::UpdateCourse(ABootRunner* Runner,float Dt)
{
    if(!Runner || !bBuilt) return;
    const FVector P=Runner->GetActorLocation();
    if(!bHistoryValid) { PreviousPosition=P; bHistoryValid=true; }
    SectionMovedCm+=FVector::Dist2D(P,PreviousPosition);
    Course.Advance(Nodes,PreviousPosition,P); PreviousPosition=P;
    for(AActor* A:OwnedActors)
    {
        if(!bProcessesActive || !IsValid(A) || ActorGroups.FindRef(A->GetUniqueID())!=Course.CurrentNodeId) continue;
        if(auto* S=Cast<ABootScanProcess>(A))
        {
            if(!ArmedProcesses.Contains(A->GetUniqueID()))
            { S->SetRunActive(true); S->Arm(AttemptGeneration); ArmedProcesses.Add(A->GetUniqueID()); }
            const auto* Section=Nodes.FindByPredicate([&](const FBootCourseNode& X){return X.Id==Course.CurrentNodeId;});
            if(bTraining && Section && Section->Transform.InverseTransformPosition(P).X>1900)
                NotifyObjective(EBootTutorialEvent::ScannerPassed,AttemptGeneration);
        }
        else if(auto* Popup=Cast<ABootPopupProcess>(A))
        {
            const auto* N=Nodes.FindByPredicate([&](const FBootCourseNode& X){return X.Id==Course.CurrentNodeId;});
            if(N && N->Transform.InverseTransformPosition(P).X>=1400 && !ArmedProcesses.Contains(A->GetUniqueID()))
            { Popup->ResetProcess(AttemptGeneration); Popup->SetRunActive(true); ArmedProcesses.Add(A->GetUniqueID()); }
        }
    }
    if(bTraining)
    {
        if(Tutorial.CurrentLesson==1 && SectionMovedCm>200.f) NotifyObjective(EBootTutorialEvent::Move,AttemptGeneration);
        const auto* Active=Nodes.FindByPredicate([&](const FBootCourseNode& N){return N.Id==Course.CurrentNodeId;});
        const auto Outcome=Tutorial.GetOutcome(Tutorial.CurrentLesson);
        if(Active && Active->LessonId>Tutorial.CurrentLesson && (Outcome==EBootLessonState::Completed || Outcome==EBootLessonState::Skipped))
        { Tutorial.ActivateLesson(Active->LessonId); SectionMovedCm=0; }
        if(Course.IsFinished()) NotifyObjective(EBootTutorialEvent::ExitGate,AttemptGeneration);
    }
    PrepareNearby(P,false);
}
bool ABootCourseTrack::CanCompleteSurf(const ABootSurfWindow* Window,const ABootRunner* Runner,int32 ActivationGateSequence) const
{
    if(!Window || !Runner) return false;
    const auto* Destination=SurfDestinations.Find(Window->GetWindowId());
    if(!Destination || Course.CurrentNodeId!=*Destination || !Course.WasGateCrossedAfter(*Destination,ActivationGateSequence)) return false;
    const auto* Node=Nodes.FindByPredicate([&](const FBootCourseNode& N){return N.Id==*Destination;});
    const auto* Movement=Runner->GetParkour();
    if(!Node || (!Movement->IsMovingOnGround() && !Movement->IsWallRunning())) return false;
    const FVector Feet=Runner->GetActorLocation()-FVector(0,0,Runner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const FVector Local=Node->Transform.InverseTransformPosition(Feet);
    // Gate history is monotonic. Arrival must also be physically inside the named
    // destination now, including its height, rather than on an earlier/stacked deck.
    if(Local.X<0 || Local.X>Node->LengthCm || FMath::Abs(Local.Y)>Node->WidthCm*.5f) return false;
    if(Movement->IsWallRunning()) return Local.Z>=-45.f && Local.Z<=560.f;
    if(FMath::Abs(Local.Z)>Movement->MaxStepHeight) return false;
    FHitResult Support;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(SurfDestinationSupport),false,Runner);
    return GetWorld()->LineTraceSingleByChannel(Support,Feet+FVector(0,0,5),Feet-FVector(0,0,20),ECC_Visibility,Query)
        && Movement->IsWalkable(Support)
        && FMath::Abs(Node->Transform.InverseTransformPosition(Support.ImpactPoint).Z)<=Movement->MaxStepHeight;
}
void ABootCourseTrack::NotifyObjective(EBootTutorialEvent Event,int32 Generation)
{
    if(!bTraining) return;
    const auto* Active=Nodes.FindByPredicate([&](const FBootCourseNode& N){return N.Id==Course.CurrentNodeId;});
    const bool NamedArrival=(Event==EBootTutorialEvent::SurfCompleted && Tutorial.CurrentLesson==4 && Course.CurrentNodeId==5)
        || (Event==EBootTutorialEvent::PopupSurfCompleted && Tutorial.CurrentLesson==7 && Course.CurrentNodeId==8);
    if(!Active || (Active->LessonId!=Tutorial.CurrentLesson && !NamedArrival)) return;
    Tutorial.NotifyObjective(Event,Tutorial.CurrentLesson,Generation);
}
bool ABootCourseTrack::IsInActiveLesson(const ABootRunner* Runner) const
{
    if(!bTraining || !Runner || Tutorial.GetOutcome(Tutorial.CurrentLesson)!=EBootLessonState::Active) return false;
    const auto* Section=Nodes.FindByPredicate([&](const FBootCourseNode& N){return N.Id==Course.CurrentNodeId;});
    if(!Section || Section->LessonId!=Tutorial.CurrentLesson) return false;
    const FVector Local=Section->Transform.InverseTransformPosition(Runner->GetActorLocation());
    return Local.X>=0.f && Local.X<=Section->LengthCm && FMath::Abs(Local.Y)<=Section->WidthCm*.5f
        && Local.Z>=Section->EntryBounds.Min.Z && Local.Z<=Section->EntryBounds.Max.Z;
}
void ABootCourseTrack::NotifyObjective(EBootTutorialEvent Event,int32 Generation,int32 OriginLesson,int32 OriginSection)
{
    // Delayed movement evidence belongs to its accepted action, rather than the
    // lesson that happens to be current when its timer/landing callback runs.
    if(OriginLesson!=Tutorial.CurrentLesson || OriginSection!=Course.CurrentNodeId) return;
    NotifyObjective(Event,Generation);
}
FString ABootCourseTrack::GetObjective() const
{
    if(!bTraining) return TEXT("ROOFTOP CIRCUIT / FOLLOW MINT ARROWS");
    const TCHAR* Text[]={TEXT("Move, then jump and land"),TEXT("Duck in air, then complete a slide"),TEXT("Fresh jump near landing to gain speed"),TEXT("Board, SNAP, ride, depart and land past the gate"),TEXT("Run along the navy frame, kick and land"),TEXT("Redirect in air, then land"),TEXT("Pass SCAN, then intercept and complete a POPUP ride"),TEXT("Follow the final loop to EXIT")};
    if(Tutorial.GetOutcome(Tutorial.CurrentLesson)==EBootLessonState::Completed) return TEXT("LESSON COMPLETE / FOLLOW MINT TO NEXT SECTION");
    return FString::Printf(TEXT("FIRST LOGIN %d/8 / %s"),Tutorial.CurrentLesson,Text[FMath::Clamp(Tutorial.CurrentLesson-1,0,7)]);
}
void ABootCourseTrack::AddCheckpointThreatSweep(const FTransform& Frame,FVector LocalEnd,FVector HalfExtent)
{
    FBox Bounds(-HalfExtent,HalfExtent);
    Bounds+=LocalEnd-HalfExtent; Bounds+=LocalEnd+HalfExtent;
    CheckpointThreatSweeps.Emplace(Frame,Bounds);
}
FTransform ABootCourseTrack::ValidatedCheckpoint(const FTransform& Candidate,const ABootRunner* RestoringRunner) const
{
    FHitResult Floor; const FVector P=Candidate.GetLocation();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(Checkpoint),false);
    if(RestoringRunner) Query.AddIgnoredActor(RestoringRunner);
    const bool FloorHit=GetWorld()->LineTraceSingleByChannel(Floor,P,P-FVector(0,0,150),ECC_Visibility,Query);
    const bool Clear=!GetWorld()->OverlapBlockingTestByChannel(P,Candidate.GetRotation(),ECC_Pawn,FCollisionShape::MakeCapsule(34,94),Query);
    bool ThreatClear=true;
    // These frames use yaw only, so an upright standing capsule has the same
    // conservative 34x34x94 extent in every threat's local coordinates.
    for(const auto& Sweep:CheckpointThreatSweeps)
        if(Sweep.Value.ExpandBy(FVector(34,34,94)).IsInsideOrOn(Sweep.Key.InverseTransformPosition(P))) { ThreatClear=false; break; }
    return FloorHit && Floor.ImpactNormal.Z>.7 && Clear && ThreatClear?Candidate:GetStartTransform();
}
FTransform ABootCourseTrack::PrepareCheckpoint(int32 Generation,const ABootRunner* RestoringRunner)
{
    AttemptGeneration=Generation; Tutorial.RestoreCurrentSection(); Tutorial.Generation=Generation;
    const auto* N=Nodes.FindByPredicate([&](const FBootCourseNode& X){return X.LessonId==Tutorial.CurrentLesson;});
    const FTransform Target=N?N->CheckpointTransform:GetStartTransform();
    PrepareNearby(Target.GetLocation(),true);
    for(AActor* A:OwnedActors) if(IsValid(A) && N && ActorGroups.FindRef(A->GetUniqueID())==N->GroupId)
    {
        ArmedProcesses.Remove(A->GetUniqueID());
        if(auto* P=Cast<ABootPopupProcess>(A)) { P->SetRunActive(false); P->ResetProcess(Generation); }
        else if(auto* S=Cast<ABootScanProcess>(A)) { S->SetRunActive(false); S->ResetProcess(Generation); }
        else if(auto* W=Cast<ABootSurfWindow>(A)) W->ResetWindow();
    }
    const FTransform Safe=ValidatedCheckpoint(Target,RestoringRunner);
    const bool UsedFallback=!Safe.GetLocation().Equals(Target.GetLocation(),1.f);
    Course.Initialize(Nodes,N && !UsedFallback?N->Id:1,Safe.GetLocation()); PreviousPosition=Safe.GetLocation(); bHistoryValid=true; SectionMovedCm=0;
    return Safe;
}
FTransform ABootCourseTrack::SkipLesson(int32 Generation,const ABootRunner* RestoringRunner)
{
    Tutorial.SkipCurrent(); const int32 Next=FMath::Min(Tutorial.CurrentLesson+1,8);
    Tutorial.ActivateLesson(Next); return PrepareCheckpoint(Generation,RestoringRunner);
}
void ABootCourseTrack::PrepareNearby(FVector P,bool bInstant)
{
    // Euclidean arrival distance includes every fork/stacked approach, including
    // routes with different canonical lengths. Character + smooth moving-base cap.
    for(ABootAssemblingPlatform* Platform:AssemblingPlatforms)
    {
        if(!IsValid(Platform) || Platform->IsReady()) continue;
        FVector Origin,Extent; Platform->GetActorBounds(false,Origin,Extent);
        const double Distance=FVector::Dist(P,Origin)-Extent.Size();
        if(FBootAssemblyLookahead::NeedsPreparation(Distance,1800,1500,1.1))
        {
            if(bInstant || Distance<3300.*1.6) Platform->FinishSafely();
            else Platform->BeginAssembly();
        }
    }
    for(AActor* A:OwnedActors)
        if(auto* W=Cast<ABootSurfWindow>(A))
        {
            const auto* Dest=SurfDestinations.Find(W->GetWindowId()); bool Ready=Dest!=nullptr;
            const auto* DestNode=Dest?Nodes.FindByPredicate([&](const FBootCourseNode& N){return N.Id==*Dest;}):nullptr;
            for(ABootAssemblingPlatform* Platform:AssemblingPlatforms)
            {
                const FVector At=Platform->GetActorLocation();
                const bool Landing=FVector::Dist(At,W->GetEndTop())<3500;
                const bool EntryLanding=DestNode && Platform->GetGroupId()==*Dest && FVector::Dist(At,DestNode->Transform.GetLocation())<2500;
                if((Landing || EntryLanding) && !Platform->IsReady()) Ready=false;
            }
            W->SetReady(Ready);
        }
}
void ABootCourseTrack::SetProcessesActive(bool bActive)
{
    bProcessesActive=bActive;
    for(AActor* A:OwnedActors)
    {
        const bool Resume=bActive && IsValid(A) && ArmedProcesses.Contains(A->GetUniqueID());
        if(auto* S=Cast<ABootScanProcess>(A)) S->SetRunActive(Resume);
        else if(auto* P=Cast<ABootPopupProcess>(A)) P->SetRunActive(Resume);
        else if(auto* W=Cast<ABootSurfWindow>(A)) W->SetRideSuspended(!bActive);
    }
}
