#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "World/Generation/BootCourseTrack.h"
#include "Player/BootRunner.h"
#include "Movement/BootMovementComponent.h"
#include "Music/BootRhythmComponent.h"
#include "World/Interaction/BootAssemblingPlatform.h"
#include "World/Interaction/BootSurfWindow.h"
#include "Profile/BootProfile.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"

namespace BootV03Flow
{
struct FWorld
{
    UWorld* World;
    FWorld()
    {
        UWorld::InitializationValues Init;
        Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
    }
    ~FWorld() { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
    void Step(float Dt) { ++GFrameCounter; World->Tick(LEVELTICK_All,Dt); }
};
void Place(ABootCourseTrack* Track,ABootRunner* Runner,FVector P)
{ Runner->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics); Track->UpdateCourse(Runner,1.f/60.f); }
bool FloorAt(UWorld* World,FVector P)
{ FHitResult H; return World->LineTraceSingleByChannel(H,P,P-FVector(0,0,150),ECC_Visibility) && H.ImpactNormal.Z>.7; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03NativeGeometryTest,"Bootloop.V03.Flow.AuthoredGeometryAndOrderedGates",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV03NativeGeometryTest::RunTest(const FString&)
{
    BootV03Flow::FWorld W;
    auto* Track=W.World->SpawnActor<ABootCourseTrack>(); Track->ResetTrack(1301995);
    auto* Runner=W.World->SpawnActor<ABootRunner>(FVector(350,0,100),FRotator::ZeroRotator);
    Runner->GetParkour()->DisableMovement();
    const auto& Nodes=Track->GetNodes();
    TestEqual(TEXT("Circuit has authored main route and optional fork"),Nodes.Num(),11);
    TestTrue(TEXT("Start has actual solid walkable geometry"),BootV03Flow::FloorAt(W.World,Track->GetStartTransform().GetLocation()));
    TestFalse(TEXT("Future elevated return cannot finish from proximity"),Track->IsFinished(FVector(1000,14500,700)));
    int32 ExpectedSequence=0;
    const int32 Route[]={11,2,3,4,5,6,7,8,9};
    for(int32 Id:Route)
    {
        const auto* N=Nodes.FindByPredicate([Id](const FBootCourseNode& X){return X.Id==Id;});
        if(!TestNotNull(TEXT("Named route node exists"),N)) continue;
        BootV03Flow::Place(Track,Runner,N->Transform.TransformPosition(FVector(-80,0,100)));
        BootV03Flow::Place(Track,Runner,N->Transform.TransformPosition(FVector(80,0,100)));
        TestEqual(TEXT("World-space yaw gate selects correct successor"),Track->GetCourseState().CurrentNodeId,Id);
        TestEqual(TEXT("Exactly one canonical crossing"),Track->GetCourseSerial(),++ExpectedSequence);
        const float Progress=Track->GetRouteProgress(Runner->GetActorLocation());
        BootV03Flow::Place(Track,Runner,N->Transform.TransformPosition(FVector(-80,0,100)));
        TestEqual(TEXT("Backtracking awards no extra crossing"),Track->GetCourseSerial(),ExpectedSequence);
        TestEqual(TEXT("Backtracking cannot farm route distance"),Track->GetRouteProgress(Runner->GetActorLocation()),Progress);
    }
    const auto* Last=Nodes.FindByPredicate([](const FBootCourseNode& N){return N.Id==9;});
    BootV03Flow::Place(Track,Runner,Last->Transform.TransformPosition(FVector(Last->LengthCm+80,0,100)));
    TestTrue(TEXT("Only ordered final exit finishes native course"),Track->IsFinished(Runner->GetActorLocation()));
    Track->ResetTrack(1301995);
    TestEqual(TEXT("Retry clears canonical sequence"),Track->GetCourseSerial(),0);
    TestFalse(TEXT("Retry clears finish"),Track->IsFinished(Runner->GetActorLocation()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03NativeCheckpointTest,"Bootloop.V03.Flow.TenNativeCheckpointRestores",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV03NativeCheckpointTest::RunTest(const FString&)
{
    BootV03Flow::FWorld W;
    auto* Track=W.World->SpawnActor<ABootCourseTrack>(); Track->SetTraining(true); Track->ResetTrack(1301995);
    auto* Runner=W.World->SpawnActor<ABootRunner>(FVector(350,0,100),FRotator::ZeroRotator);
    int32 Generation=Track->GetTutorial().Generation;
    Track->NotifyObjective(EBootTutorialEvent::Move,Generation);
    Track->NotifyObjective(EBootTutorialEvent::JumpLanded,Generation);
    TestEqual(TEXT("Active lesson accepts correct section events"),Track->GetTutorial().GetOutcome(1),EBootLessonState::Completed);
    // Activate the turned lesson with the actual checkpoint API; previous outcome must survive.
    Track->SkipLesson(++Generation,Runner); const FTransform Checkpoint=Track->SkipLesson(++Generation,Runner);
    TestEqual(TEXT("Third lesson starts at yaw90 checkpoint"),Track->GetTutorial().CurrentLesson,3);
    TestTrue(TEXT("Turned checkpoint yaw"),FMath::IsNearlyEqual(Checkpoint.Rotator().Yaw,90.f,.1f));
    const int32 ActorCount=W.World->GetActorCount();
    for(int32 I=0;I<10;++I)
    {
        auto* Move=Runner->GetParkour(); Move->SetMovementMode(MOVE_Falling); Move->Velocity=FVector(900,0,20);
        Move->TryAirRedirect(FVector(0,1,0)); Runner->StartSlide(); Runner->LaunchCharacter(FVector(0,0,600),true,true);
        const int32 OldGeneration=Generation;
        const FTransform Safe=Track->PrepareCheckpoint(++Generation,Runner); Runner->ResetToTransform(Safe,false);
        TestTrue(TEXT("Checkpoint retains authored position/facing"),Safe.Equals(Checkpoint,.1f));
        TestTrue(TEXT("Checkpoint has actual walkable floor"),BootV03Flow::FloorAt(W.World,Safe.GetLocation()));
        TestFalse(TEXT("Standing capsule is clear"),W.World->OverlapBlockingTestByChannel(Safe.GetLocation(),Safe.GetRotation(),ECC_Pawn,
            FCollisionShape::MakeCapsule(34,94),FCollisionQueryParams(NAME_None,false,Runner)));
        TestTrue(TEXT("Reset clears movement base"),Runner->GetMovementBaseObject()==nullptr);
        TestTrue(TEXT("Reset restores redirect charge"),Move->IsAirRedirectReady());
        TestFalse(TEXT("Reset clears redirect and slide"),Move->IsAirRedirecting() || Runner->IsSliding());
        TestTrue(TEXT("Reset clears velocity and pending launch"),Move->Velocity.IsNearlyZero() && Move->PendingLaunchVelocity.IsNearlyZero());
        Track->NotifyObjective(EBootTutorialEvent::ManualHop,OldGeneration);
        TestEqual(TEXT("Stale generation cannot complete current objective"),Track->GetTutorial().GetOutcome(3),EBootLessonState::Active);
        TestEqual(TEXT("Prior completed outcome retained"),Track->GetTutorial().GetOutcome(1),EBootLessonState::Completed);
        TestEqual(TEXT("Restore creates no gate events"),Track->GetCourseSerial(),0);
        TestEqual(TEXT("Restore allocates no extra actors"),W.World->GetActorCount(),ActorCount);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03NativeReadyRecordsTest,"Bootloop.V03.Flow.ForkReadinessAndRecordIsolation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV03NativeReadyRecordsTest::RunTest(const FString&)
{
    BootV03Flow::FWorld W;
    auto* Track=W.World->SpawnActor<ABootCourseTrack>(); Track->ResetTrack(1301995);
    auto* Runner=W.World->SpawnActor<ABootRunner>(FVector(350,0,100),FRotator::ZeroRotator); Runner->GetParkour()->DisableMovement();
    BootV03Flow::Place(Track,Runner,FVector(8000,8000,320));
    int32 Upper=0,Lower=0;
    for(TActorIterator<ABootAssemblingPlatform> It(W.World);It;++It)
    {
        if(It->GetGroupId()==30 && FVector::Dist(It->GetActorLocation(),Runner->GetActorLocation())<5280)
        { ++Upper; TestTrue(TEXT("Upper fork landing ready before max-speed arrival"),It->IsReady()); }
        if(It->GetGroupId()==3 && FVector::Dist(It->GetActorLocation(),Runner->GetActorLocation())<5280)
        { ++Lower; TestTrue(TEXT("Lower fork landing ready before max-speed arrival"),It->IsReady()); }
    }
    TestTrue(TEXT("Both physical fork branches were inspected"),Upper>0 && Lower>0);
    Track->ResetTrack(1301995);
    for(TActorIterator<ABootSurfWindow> It(W.World);It;++It)
        if(It->GetWindowId()==1001) TestTrue(TEXT("Initial ride exposes Ready only after landing prewarm"),It->IsAvailable());
    auto* Profile=NewObject<UBootProfile>(); Profile->BestScore=17; Profile->Runs=3;
    Profile->UpdateCourseRecord(TEXT("FirstLoginV03"),9999,270,10,true);
    Profile->UpdateCourseRecord(TEXT("RooftopCircuitV03"),200,900,100,true);
    TestFalse(TEXT("Tutorial creates no competitive entry"),Profile->CourseRecords.Contains(TEXT("FirstLoginV03")));
    TestEqual(TEXT("Circuit cannot overwrite legacy score"),Profile->BestScore,17);
    TestEqual(TEXT("Local record update cannot count tutorial challenge runs"),Profile->Runs,3);
    TArray<uint8> Memory; TestTrue(TEXT("Records serialize in memory without user disk slot"),UGameplayStatics::SaveGameToMemory(Profile,Memory));
    auto* Loaded=Cast<UBootProfile>(UGameplayStatics::LoadGameFromMemory(Memory));
    TestNotNull(TEXT("Isolated profile roundtrip"),Loaded);
    if(Loaded) TestTrue(TEXT("Circuit record remains course-specific"),Loaded->CourseRecords.Contains(TEXT("RooftopCircuitV03")) && !Loaded->CourseRecords.Contains(TEXT("WindowSurfV02")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03NativeBypassTest,"Bootloop.V03.Flow.BaseSpeedLowerBypassesReachJunction",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV03NativeBypassTest::RunTest(const FString&)
{
    for(int32 SectionId:{4,7})
    {
        BootV03Flow::FWorld W;
        auto* Track=W.World->SpawnActor<ABootCourseTrack>(); Track->SetTraining(true); Track->ResetTrack(1301995);
        const auto* N=Track->GetNodes().FindByPredicate([SectionId](const FBootCourseNode& X){return X.Id==SectionId;});
        if(!TestNotNull(TEXT("Authored tutorial surf/POPUP bypass node"),N)) continue;
        const FVector Start=N->Transform.TransformPosition(FVector(SectionId==4?1800:2400,-650,-60));
        auto* Runner=W.World->SpawnActor<ABootRunner>(Start,N->Transform.Rotator());
        auto* Move=Runner->GetParkour(); Move->bRunPhysicsWithNoController=true; Move->SetMovementMode(MOVE_Walking);
        Track->UpdateCourse(Runner,1.f/60.f);
        for(int32 I=0;I<600;++I)
        {
            Runner->AddMovementInput(N->Transform.GetUnitAxis(EAxis::X),1,true);
            W.Step(1.f/60.f); Track->UpdateCourse(Runner,1.f/60.f);
            if(N->Transform.InverseTransformPosition(Runner->GetActorLocation()).X>N->LengthCm+100 || Runner->IsDead()) break;
        }
        const FVector End=N->Transform.InverseTransformPosition(Runner->GetActorLocation());
        AddInfo(FString::Printf(TEXT("Base-speed tutorial bypass section=%d local=%s speed=%.1f"),SectionId,*End.ToString(),Move->Velocity.Size2D()));
        TestTrue(FString::Printf(TEXT("Section %d lower bypass reaches next solid junction by running only"),SectionId),End.X>N->LengthCm+100 && End.Z>90 && Move->IsMovingOnGround() && !Runner->IsDead());
        TestEqual(TEXT("Lower bypass never requires scored movement"),Runner->GetRhythm()->GetScore().Score,0);
        TestTrue(TEXT("No hop or departure boost required"),Move->Velocity.Size2D()<=900.1);
    }
    return true;
}
#endif
