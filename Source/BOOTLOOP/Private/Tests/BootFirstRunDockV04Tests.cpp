#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tutorial/BootFirstRunTrack.h"
#include "World/Interaction/BootSurfWindow.h"
#include "World/Threats/BootPopupProcess.h"
#include "Player/BootRunner.h"
#include "Movement/BootMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"

namespace BootFirstRunDockTest
{
struct FWorld
{
    UWorld* World=nullptr;
    FWorld()
    {
        UWorld::InitializationValues Init;
        Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
    }
    ~FWorld() { World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
    void Step(int32 Frames,float Dt,ABootRunner* Runner=nullptr)
    {
        for(int32 I=0;I<Frames;++I)
        {
            if(Runner) Runner->AddMovementInput(FVector::ForwardVector,1.f,true);
            ++GFrameCounter; World->Tick(LEVELTICK_All,Dt);
        }
    }
};
}

// Component/world regression: lesson skips and initial spawn are explicit fixture setup.
// The observed activation, actual based carry, docking, departure and grounded landing never teleport.
// This does not claim a clean-profile full-tutorial or OS-keyboard playthrough.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootFirstRunSurfDockTest,"Bootloop.V04.FirstRun.AuthoredSurfDockAndDeparture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootFirstRunSurfDockTest::RunTest(const FString&)
{
    for(const float Dt:{1.f/30.f,1.f/60.f,1.f/120.f})
    for(const int32 Lesson:{5,6})
    {
        BootFirstRunDockTest::FWorld W;
        auto* Track=W.World->SpawnActor<ABootFirstRunTrack>();
        FBootRunToken Run; Run.RunId=FGuid::NewGuid(); Run.Generation=1; Track->BeginFirstRun(Run);
        for(int32 L=0;L<Lesson;++L) Track->SkipCurrentLesson(Run);
        Track->SetProcessesActive(true);
        AddInfo(FString::Printf(TEXT("Authored dock fixture: lesson=%d fps=%.0f"),Lesson,1.f/Dt));
        ABootSurfWindow* Window=nullptr;
        for(TActorIterator<ABootSurfWindow> It(W.World);It;++It)
            if(It->GetWindowId()==(Lesson==5?45001:46001)) Window=*It;
        if(!TestNotNull(TEXT("actual authored FirstRun surf or popup exists"),Window)) return false;
        auto* Runner=W.World->SpawnActor<ABootRunner>(Window->GetActorLocation()+FVector(0,0,140),FRotator::ZeroRotator);
        if(!TestNotNull(TEXT("fixture rider spawned"),Runner)) return false;
        Runner->GetParkour()->bRunPhysicsWithNoController=true;
        // A spawned, unpossessed character may start in MOVE_None. Let real gravity find the base.
        Runner->GetParkour()->SetMovementMode(MOVE_Falling);
        W.Step(FMath::RoundToInt(.5f/Dt),Dt);
        if(!TestTrue(TEXT("fixture rider naturally settles onto actual moving window"),Runner->GetMovementBaseObject()==Window->GetRootComponent())) continue;
        if(!TestTrue(TEXT("fixture rider is within current lesson section"),Track->IsInActiveLesson(Runner))) continue;
        const FVector Before=Runner->GetActorLocation();
        const int32 Section=Track->GetCourseSerial();
        if(auto* Popup=Cast<ABootPopupProcess>(Window)) Popup->SetRunActive(true);
        TestTrue(TEXT("off-beat activation accepted"),Window->Activate(Runner,false));
        W.Step(FMath::RoundToInt(2.2f/Dt),Dt);
        TestTrue(TEXT("real authored window carries rider"),Window->HasCarried(Runner)&&Runner->GetActorLocation().X>Before.X+500.);
        TestFalse(TEXT("authored dock must not block its own swept window"),Window->WasRideBlocked());
        TestTrue(TEXT("authored window reaches exact endpoint"),(Window->GetActorLocation()+FVector(0,0,40)).Equals(Window->GetEndTop(),.1f));
        TestFalse(TEXT("remaining aboard cannot complete surf"),Window->CommitRide(Runner));
        bool bNamedLanding=false;
        for(int32 Frame=0;Frame<FMath::RoundToInt(2.f/Dt);++Frame)
        {
            W.Step(1,Dt,Runner);
            if(Track->CanCompleteSurf(Window,Runner,Section)) { bNamedLanding=true; break; }
        }
        TestTrue(TEXT("actual forward input reaches authored static landing"),bNamedLanding);
        TestTrue(TEXT("carried departed landed offbeat ride can commit"),Window->CommitRide(Runner));
        TestFalse(TEXT("ride completion remains once-only"),Window->CommitRide(Runner));
    }
    return true;
}
#endif


