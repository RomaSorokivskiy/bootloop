#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "World/Generation/BootCourseTypes.h"
#include "Tutorial/BootTutorialState.h"
#include "Profile/BootProfile.h"
#include "Kismet/GameplayStatics.h"

namespace
{
FBootCourseNode Node(int32 Id,FVector Origin,float Yaw,float From,float To,TArray<int32> Next={})
{
    FBootCourseNode N; N.Id=Id; N.Transform=FTransform(FRotator(0,Yaw,0),Origin);
    N.ProgressStartMetres=From; N.ProgressEndMetres=To; N.SuccessorIds=Next; return N;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTurnedGateTest,"Bootloop.V03.Course.TurnedGatesAndStackedReturn",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootTurnedGateTest::RunTest(const FString&)
{
    TArray<FBootCourseNode> Nodes={Node(1,{0,0,0},0,0,10,{2}),Node(2,{1000,0,0},90,10,20,{3}),Node(3,{1000,1000,400},180,20,30)};
    FBootCourseState S; S.Initialize(Nodes,1,{0,0,100});
    // A stacked future segment is never selected by proximity.
    S.Advance(Nodes,{0,0,100},{500,1000,500});
    TestEqual(TEXT("future return rejected"),S.CurrentNodeId,1);
    S.Initialize(Nodes,1,{950,-80,100});
    auto Cross=S.Advance(Nodes,{950,-80,100},{1020,160,100});
    TestEqual(TEXT("swept yaw90 gate"),Cross.Num(),1);
    TestEqual(TEXT("turn changes active node"),S.CurrentNodeId,2);
    const int32 Seq=S.GetGateSequence();
    S.Advance(Nodes,{1020,160,100},{1020,-80,100});
    TestEqual(TEXT("backward crossing cannot farm"),S.GetGateSequence(),Seq);
    S.Advance(Nodes,{1020,1050,500},{900,1020,500});
    TestEqual(TEXT("yaw180 upper return"),S.CurrentNodeId,3);
    TestFalse(TEXT("entry is not final exit"),S.IsFinished());
    S.Advance(Nodes,{900,1020,500},{-100,1020,500});
    TestTrue(TEXT("ordered exit finishes"),S.IsFinished());
    S.Initialize(Nodes,2,{1050,900,100});
    TestEqual(TEXT("teleport initialize awards no crossing"),S.GetGateSequence(),0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootForkGateTest,"Bootloop.V03.Course.ForkProgressAndSurfDestination",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootForkGateTest::RunTest(const FString&)
{
    TArray<FBootCourseNode> N={Node(1,{0,0,0},0,0,10,{2,3}),Node(2,{1000,-300,0},0,10,20,{4}),Node(3,{1000,300,400},0,10,20,{4}),Node(4,{2000,0,0},0,20,30)};
    N[1].EntryBounds=FBox(FVector(-20,-200,-200),FVector(20,200,450));
    N[2].EntryBounds=N[1].EntryBounds;
    FBootCourseState S; S.Initialize(N,1,{900,300,500});
    S.Advance(N,{900,300,500},{1150,300,500});
    TestEqual(TEXT("upper fork selected"),S.CurrentNodeId,3);
    TestTrue(TEXT("named arrival after activation"),S.WasGateCrossedAfter(3,0));
    TestFalse(TEXT("unvisited lower gate not accepted"),S.WasGateCrossedAfter(2,0));
    TestFalse(TEXT("preactivation crossing not accepted"),S.WasGateCrossedAfter(3,S.GetGateSequence()));
    S.Advance(N,{1900,0,100},{2100,0,100});
    TestEqual(TEXT("canonical merge"),S.CurrentNodeId,4);
    TestTrue(TEXT("merge progress"),FMath::IsNearlyEqual(S.GetProgressMetres(),21.f,.01f));
    S.Advance(N,{2100,0,100},{1900,0,100});
    TestTrue(TEXT("distance never farms backwards"),FMath::IsNearlyEqual(S.GetProgressMetres(),21.f,.01f));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootLessonStateTest,"Bootloop.V03.Course.CheckpointGenerationAndLessonSkip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootLessonStateTest::RunTest(const FString&)
{
    FBootTutorialState S; S.ActivateLesson(1);
    TestTrue(TEXT("move event"),S.NotifyObjective(EBootTutorialEvent::Move,1,1));
    TestFalse(TEXT("future section event ignored"),S.NotifyObjective(EBootTutorialEvent::JumpLanded,2,1));
    S.RestoreCurrentSection();
    TestFalse(TEXT("old generation cannot complete"),S.NotifyObjective(EBootTutorialEvent::JumpLanded,1,1));
    S.NotifyObjective(EBootTutorialEvent::Move,1,S.Generation);
    S.NotifyObjective(EBootTutorialEvent::JumpLanded,1,S.Generation);
    TestEqual(TEXT("lesson completed by real events"),S.GetOutcome(1),EBootLessonState::Completed);
    S.ActivateLesson(2); TestTrue(TEXT("explicit technique skip"),S.SkipCurrent());
    TestEqual(TEXT("skip never means completed"),S.GetOutcome(2),EBootLessonState::Skipped);
    S.RestoreCurrentSection();
    TestEqual(TEXT("previous outcome preserved"),S.GetOutcome(1),EBootLessonState::Completed);
    FBootTutorialState NewSession; NewSession.ActivateLesson(1);
    TestEqual(TEXT("new session starts lesson1"),NewSession.CurrentLesson,1);
    auto* Profile=NewObject<UBootProfile>();
    Profile->UpdateCourseRecord(TEXT("FirstLoginV03"),9999,900,10,true);
    TestEqual(TEXT("tutorial never creates competitive record"),Profile->CourseRecords.Num(),0);
    Profile->UpdateCourseRecord(TEXT("RooftopCircuitV03"),1234,700,120,false);
    Profile->UpdateCourseRecord(TEXT("RooftopCircuitV03"),1100,900,115,true);
    Profile->UpdateCourseRecord(TEXT("RooftopCircuitV03"),1000,900,130,true);
    TArray<uint8> Saved; UGameplayStatics::SaveGameToMemory(Profile,Saved);
    const auto* Loaded=Cast<UBootProfile>(UGameplayStatics::LoadGameFromMemory(Saved));
    TestNotNull(TEXT("course record roundtrip"),Loaded);
    if(Loaded) { const auto* R=Loaded->CourseRecords.Find(TEXT("RooftopCircuitV03")); TestNotNull(TEXT("record is course-specific"),R); if(R) { TestEqual(TEXT("best score independent of fastest run"),R->BestScore,1234); TestEqual(TEXT("only finished time counts"),R->BestTimeSeconds,115.f); } }
    return true;
}
#endif
