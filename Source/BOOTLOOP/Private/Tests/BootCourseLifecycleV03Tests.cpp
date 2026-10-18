#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "World/Generation/BootCourseTrack.h"
#include "Player/BootRunner.h"
#include "Movement/BootMovementComponent.h"
#include "World/Interaction/BootSurfWindow.h"
#include "Run/BootGameMode.h"
#include "Music/BootRhythmComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"

namespace BootCourseLifecycleV03
{
struct FWorld
{
    UWorld* World;
    FWorld(bool bWithGameMode=false)
    {
        UWorld::InitializationValues Init;
        Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        if(bWithGameMode)
        {
            World->SetGameInstance(NewObject<UGameInstance>(GEngine));
            World->GetWorldSettings()->DefaultGameMode=ABootFirstLoginGameMode::StaticClass();
            World->SetGameMode(FURL());
        }
        World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
    }
    ~FWorld() { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
    void Step(int32 Frames)
    { for(int32 I=0;I<Frames;++I) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60.f); } }
};
void Place(ABootCourseTrack* Track,ABootRunner* Runner,FVector P)
{
    Runner->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);
    Track->UpdateCourse(Runner,1.f/60.f);
}
UBoxComponent* Solid(UWorld* World,const FTransform& Transform,FVector Extent)
{
    auto* Actor=World->SpawnActor<AActor>();
    auto* Box=NewObject<UBoxComponent>(Actor); Actor->SetRootComponent(Box); Actor->AddInstanceComponent(Box);
    Box->SetBoxExtent(Extent); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
    Actor->SetActorTransform(Transform); return Box;
}
FBootCourseNode& Node(ABootCourseTrack* Track,int32 Id)
{
    // Test authoring fixture: alter the candidate through the existing read-only inspection view,
    // while exercising the production checkpoint/arrival methods and real collision scene.
    return *const_cast<TArray<FBootCourseNode>&>(Track->GetNodes()).FindByPredicate([Id](const FBootCourseNode& N){return N.Id==Id;});
}
}

// Catches SkipLesson unpausing UWorld while leaving the existing rhythm run paused.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03PausedSkipResumesRhythmTest,"Bootloop.V03.CourseLifecycle.PausedSkipResumesExistingRhythm",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV03PausedSkipResumesRhythmTest::RunTest(const FString&)
{
    BootCourseLifecycleV03::FWorld W(true);
    auto* Mode=W.World->GetAuthGameMode<ABootFirstLoginGameMode>();
    if(!TestNotNull(TEXT("Real training GameMode is authoritative"),Mode)) return false;
    auto* PC=W.World->SpawnActor<APlayerController>(); PC->SetAsLocalPlayerController();
    auto* Local=NewObject<ULocalPlayer>(GEngine);
    Local->PlayerController=PC; PC->Player=Local;
    W.World->GetGameInstance()->AddLocalPlayer(Local,FPlatformUserId::CreateFromInternalId(0));
    auto* Course=Cast<ABootCourseTrack>(Mode->GetTrack());
    auto* Runner=W.World->SpawnActor<ABootRunner>(ABootRunner::StaticClass(),Course->GetStartTransform()); PC->Possess(Runner);
    Mode->StartRun(); W.Step(80);
    TestEqual(TEXT("Public boot timer enters running"),Mode->GetRunState(),EBootRunState::Running);
    auto* Rhythm=Runner->GetRhythm();
    TestTrue(TEXT("Existing rhythm run accepts a completion before pause"),Rhythm->CompleteSurf(99001,EBootBeatRating::OffBeat));
    Mode->PauseRun(true);
    TestTrue(TEXT("PauseRun pauses the actual world"),W.World->IsPaused());
    TestFalse(TEXT("Paused rhythm rejects completion"),Rhythm->CompleteSurf(99002,EBootBeatRating::OffBeat));
    Mode->SkipLesson();
    TestFalse(TEXT("Skip from Pause unpauses the actual world"),W.World->IsPaused());
    TestEqual(TEXT("Skip resumes running state"),Mode->GetRunState(),EBootRunState::Running);
    TestTrue(TEXT("Skip resumes existing rhythm completion path"),Rhythm->CompleteSurf(99002,EBootBeatRating::OffBeat));
    TestTrue(TEXT("Skip preserves existing rhythm history instead of restarting"),Rhythm->GetScore().CompletedWindows.Contains(99001));
    TestFalse(TEXT("Preserved run still deduplicates earlier completion"),Rhythm->CompleteSurf(99001,EBootBeatRating::OffBeat));
    TestEqual(TEXT("Tutorial remains unranked after resume"),Mode->GetTotalScore(),0);
    TestEqual(TEXT("Skip safely prepares next section"),Course->GetTutorial().CurrentLesson,2);
    TestTrue(TEXT("Skip restores active movement"),Runner->GetParkour()->IsMovingOnGround());
    Runner->Destroy();
    W.World->GetGameInstance()->RemoveLocalPlayer(Local);
    return true;
}

// Catches monotonic gate state being treated as proof of present supported arrival.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03SupportedSurfDestinationTest,"Bootloop.V03.CourseLifecycle.SupportedOrientedSurfDestination",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV03SupportedSurfDestinationTest::RunTest(const FString&)
{
    using namespace BootCourseLifecycleV03;
    FWorld W;
    auto* Track=W.World->SpawnActor<ABootCourseTrack>(); Track->SetTraining(true); Track->ResetTrack(1301995);
    int32 Generation=1;
    for(int32 I=0;I<3;++I) Track->SkipLesson(++Generation);
    const auto& Source=Node(Track,4); const auto& Destination=Node(Track,5);
    auto* Runner=W.World->SpawnActor<ABootRunner>(ABootRunner::StaticClass(),Track->GetStartTransform());
    Runner->GetParkour()->bRunPhysicsWithNoController=true;
    ABootSurfWindow* Window=nullptr;
    for(TActorIterator<ABootSurfWindow> It(W.World);It;++It) if(It->GetWindowId()==4001) Window=*It;
    if(!TestNotNull(TEXT("Actual authored tutorial window exists"),Window)) return false;
    const int32 ActivatedAt=Track->GetCourseSerial();
    Runner->GetParkour()->SetMovementMode(MOVE_Falling);
    Place(Track,Runner,Destination.Transform.TransformPosition(FVector(-80,0,350)));
    Place(Track,Runner,Destination.Transform.TransformPosition(FVector(80,0,350)));
    TestEqual(TEXT("Airborne yaw90 gate crossing is recorded"),Track->GetCourseState().CurrentNodeId,5);
    TestFalse(TEXT("Airborne crossing alone cannot complete surf"),Track->CanCompleteSurf(Window,Runner,ActivatedAt));

    Place(Track,Runner,Source.Transform.TransformPosition(FVector(1800,-850,-50)));
    Runner->GetParkour()->Velocity=FVector::ZeroVector; W.Step(45); Track->UpdateCourse(Runner,.05f);
    TestTrue(TEXT("Backtracking fixture really lands on source bypass"),Runner->GetParkour()->IsMovingOnGround());
    TestEqual(TEXT("Canonical destination deliberately stays monotonic"),Track->GetCourseState().CurrentNodeId,5);
    TestFalse(TEXT("Source landing after airborne destination crossing is rejected"),Track->CanCompleteSurf(Window,Runner,ActivatedAt));

    const FVector Upper=Destination.Transform.TransformPosition(FVector(350,0,600));
    Solid(W.World,FTransform(Destination.Transform.GetRotation(),Upper-FVector(0,0,20)),FVector(250,250,20));
    Place(Track,Runner,Upper+FVector(0,0,110)); Runner->GetParkour()->SetMovementMode(MOVE_Falling);
    Runner->GetParkour()->Velocity=FVector::ZeroVector; W.Step(45); Track->UpdateCourse(Runner,.05f);
    TestTrue(TEXT("Stacked fixture really lands on an upper support"),Runner->GetParkour()->IsMovingOnGround());
    TestFalse(TEXT("Support stacked above the named destination deck is rejected"),Track->CanCompleteSurf(Window,Runner,ActivatedAt));

    Place(Track,Runner,Destination.Transform.TransformPosition(FVector(650,0,110)));
    Runner->GetParkour()->SetMovementMode(MOVE_Falling); Runner->GetParkour()->Velocity=FVector::ZeroVector;
    W.Step(45); Track->UpdateCourse(Runner,.05f);
    TestTrue(TEXT("Valid fixture lands on the named destination's real deck"),Runner->GetParkour()->IsMovingOnGround());
    TestTrue(TEXT("Supported arrival in oriented destination is accepted"),Track->CanCompleteSurf(Window,Runner,ActivatedAt));
    TestFalse(TEXT("Gate crossing from before activation is still rejected"),Track->CanCompleteSurf(Window,Runner,Track->GetCourseSerial()));
    return true;
}

// Catches clear/walkable checkpoints placed inside future scanner or popup travel.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03CheckpointThreatSweepTest,"Bootloop.V03.CourseLifecycle.CheckpointThreatSweepAndFallbackHistory",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV03CheckpointThreatSweepTest::RunTest(const FString&)
{
    using namespace BootCourseLifecycleV03;
    FWorld W;
    auto* Track=W.World->SpawnActor<ABootCourseTrack>(); Track->SetTraining(true); Track->ResetTrack(1301995);
    int32 Generation=1;
    for(int32 I=0;I<6;++I) Track->SkipLesson(++Generation);
    auto& Section=Node(Track,7);
    const FTransform Original=Section.CheckpointTransform;
    auto* Runner=W.World->SpawnActor<ABootRunner>(ABootRunner::StaticClass(),Track->GetStartTransform()); Runner->GetParkour()->DisableMovement();
    auto CheckFallback=[&](const TCHAR* Label,FVector Local)
    {
        Section.CheckpointTransform=FTransform(Section.Transform.GetRotation(),Section.Transform.TransformPosition(Local));
        const FTransform Safe=Track->PrepareCheckpoint(++Generation);
        TestTrue(Label,Safe.Equals(Track->GetStartTransform(),.1f));
        TestEqual(TEXT("Fallback resets active course node to start"),Track->GetCourseState().CurrentNodeId,1);
        TestEqual(TEXT("Fallback clears old gate sequence"),Track->GetCourseSerial(),0);
        Place(Track,Runner,Safe.GetLocation());
        TestEqual(TEXT("Stationary update after restore fabricates no crossing"),Track->GetCourseSerial(),0);
        TestEqual(TEXT("Position history is reset at fallback"),Track->GetCourseState().CurrentNodeId,1);
        TestEqual(TEXT("Fallback retains intended lesson and generation"),Track->GetTutorial().CurrentLesson,7);
        TestEqual(TEXT("Fallback retains new attempt generation"),Track->GetTutorial().Generation,Generation);
    };
    CheckFallback(TEXT("Walkable clear checkpoint inside scanner sweep falls back"),FVector(900,0,100));
    // Supply an otherwise valid floor in the popup gap, isolating future threat-envelope validation.
    Solid(W.World,FTransform(Section.Transform.GetRotation(),Section.Transform.TransformPosition(FVector(2500,150,-20))),FVector(250,350,20));
    CheckFallback(TEXT("Walkable clear checkpoint inside popup sweep falls back"),FVector(2500,150,100));
    Section.CheckpointTransform=Original;
    const FTransform Safe=Track->PrepareCheckpoint(++Generation);
    TestTrue(TEXT("Authored checkpoint outside both future sweeps is retained"),Safe.Equals(Original,.1f));
    TestEqual(TEXT("Valid checkpoint restores its real node"),Track->GetCourseState().CurrentNodeId,7);
    Runner->SetActorTransform(Original,false,nullptr,ETeleportType::TeleportPhysics);
    TestTrue(TEXT("Restoring runner's own capsule does not invalidate a clear checkpoint"),Track->PrepareCheckpoint(++Generation,Runner).Equals(Original,.1f));
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* OtherPawn=W.World->SpawnActor<ACharacter>(Original.GetLocation(),Original.Rotator(),Spawn);
    if(TestNotNull(TEXT("Real second pawn overlaps candidate checkpoint"),OtherPawn))
        TestTrue(TEXT("Other pawn still obstructs checkpoint standing clearance"),Track->PrepareCheckpoint(++Generation,Runner).Equals(Track->GetStartTransform(),.1f));
    return true;
}

// Catches authored checkpoints intersecting earlier elevated landings, causing
// silent start fallback while the tutorial advances to another lesson.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03AllAuthoredCheckpointSkipsTest,"Bootloop.V03.CourseLifecycle.AllEightAuthoredCheckpointSkips",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootV03AllAuthoredCheckpointSkipsTest::RunTest(const FString&)
{
    using namespace BootCourseLifecycleV03;
    FWorld W;
    auto* Track=W.World->SpawnActor<ABootCourseTrack>(); Track->SetTraining(true); Track->ResetTrack(1301995);
    auto* Runner=W.World->SpawnActor<ABootRunner>(ABootRunner::StaticClass(),Track->GetStartTransform());
    Runner->GetParkour()->bRunPhysicsWithNoController=true;
    int32 Generation=1;
    for(int32 Lesson=1;Lesson<=8;++Lesson)
    {
        const auto& N=Node(Track,Lesson);
        const FTransform Authored=N.CheckpointTransform;
        const FTransform Safe=Lesson==1?Track->PrepareCheckpoint(++Generation,Runner):Track->SkipLesson(++Generation,Runner);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(AuthoredCheckpointSkipTest),false,Runner);
        FHitResult Floor;
        const bool FloorHit=W.World->LineTraceSingleByChannel(Floor,Authored.GetLocation(),Authored.GetLocation()-FVector(0,0,150),ECC_Visibility,Query);
        TestTrue(FString::Printf(TEXT("Lesson %d authored candidate has walkable floor"),Lesson),FloorHit && Floor.ImpactNormal.Z>.7);
        TestFalse(FString::Printf(TEXT("Lesson %d authored standing capsule is clear of actual geometry"),Lesson),
            W.World->OverlapBlockingTestByChannel(Authored.GetLocation(),Authored.GetRotation(),ECC_Pawn,FCollisionShape::MakeCapsule(34,94),Query));
        AddInfo(FString::Printf(TEXT("Checkpoint skip lesson=%d node=%d authored=%s returned=%s"),Lesson,Track->GetCourseState().CurrentNodeId,
            *Authored.GetLocation().ToString(),*Safe.GetLocation().ToString()));
        TestEqual(FString::Printf(TEXT("Skip activates lesson %d"),Lesson),Track->GetTutorial().CurrentLesson,Lesson);
        TestEqual(FString::Printf(TEXT("Lesson %d restore keeps its matching authored node"),Lesson),Track->GetCourseState().CurrentNodeId,N.Id);
        TestTrue(FString::Printf(TEXT("Lesson %d restore uses its authored checkpoint without fallback"),Lesson),Safe.Equals(Authored,.1f));
        TestEqual(TEXT("Checkpoint restore does not fabricate a gate crossing"),Track->GetCourseSerial(),0);
        Runner->ResetToTransform(Safe,false); W.Step(12); Track->UpdateCourse(Runner,.05f);
        TestTrue(FString::Printf(TEXT("Lesson %d reset settles on actual support"),Lesson),Runner->GetParkour()->IsMovingOnGround());
        TestTrue(FString::Printf(TEXT("Lesson %d reset remains at its authored position"),Lesson),Runner->GetActorLocation().Equals(Authored.GetLocation(),10.f));
        TestEqual(FString::Printf(TEXT("Lesson %d stationary restore keeps its node"),Lesson),Track->GetCourseState().CurrentNodeId,N.Id);
    }
    const auto& Last=Node(Track,8);
    Runner->GetParkour()->DisableMovement();
    Place(Track,Runner,Last.Transform.TransformPosition(FVector(Last.LengthCm-80,0,100)));
    Place(Track,Runner,Last.Transform.TransformPosition(FVector(Last.LengthCm+80,0,100)));
    TestTrue(TEXT("After valid authored skips the final ordered exit completes tutorial"),Track->IsFinished(Runner->GetActorLocation()));
    TestEqual(TEXT("Final lesson completion remains distinguishable from earlier explicit skips"),Track->GetTutorial().GetOutcome(8),EBootLessonState::Completed);
    return true;
}
#endif
