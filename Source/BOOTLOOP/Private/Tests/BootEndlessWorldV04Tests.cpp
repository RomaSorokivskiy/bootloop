#include "Misc/AutomationTest.h"
#include "World/Generation/BootEndlessTrack.h"
#include "World/Generation/BootEndlessChunk.h"
#include "Player/BootRunner.h"
#include "Movement/BootMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

namespace BootEndlessWorldTest
{
struct FWorld
{
    UWorld* World=nullptr;
    int64 SteeringChunk=0;TSet<int32> SteeringActions;
    FWorld()
    {
        UWorld::InitializationValues Init;Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());World->BeginPlay();World->GetWorldSettings()->NotifyBeginPlay();
    }
    ~FWorld(){World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Step(ABootEndlessTrack* Track,ABootRunner* Runner,int32 Frames,bool Move=false)
    {
        for(int32 I=0;I<Frames;++I)
        {
            if(Move)for(const ABootEndlessChunk* Chunk:Track->GetLiveChunks())
            {
                const auto& C=Chunk->GetDescriptor();if(C.CanonicalEndMetres<=Track->GetProgressMetres()+.01)continue;
                if(SteeringChunk!=C.InstanceId){SteeringChunk=C.InstanceId;SteeringActions.Empty();}
                const FVector P=C.WorldTransform.InverseTransformPosition(Runner->GetActorLocation());
                double Best=TNumericLimits<double>::Max();int32 Segment=1;double Along=0.;const auto& Path=C.Recipe.Baseline;
                for(int32 J=1;J<Path.Num();++J)
                {
                    const FVector D=(Path[J]-Path[J-1])*FVector(1,1,0),Q=(P-Path[J-1])*FVector(1,1,0);
                    const double A=FMath::Clamp(FVector::DotProduct(Q,D)/FMath::Max(1.,D.SizeSquared()),0.,1.),Distance=(Q-D*A).SizeSquared();
                    if(Distance<Best){Best=Distance;Segment=J;Along=A;}
                }
                FVector Target=FMath::Lerp(Path[Segment-1],Path[Segment],Along);double Ahead=180.;
                for(int32 J=Segment;J<Path.Num();++J){const double L=FVector::Dist2D(Target,Path[J]);if(L>=Ahead){Target+=(Path[J]-Target).GetSafeNormal2D()*Ahead;break;}Target=Path[J];Ahead-=L;}
                if(FVector::Dist2D(P,Path.Last())<120.)Target=Path.Last()+C.WorldTransform.InverseTransformVectorNoScale(C.WorldExit.GetRotation().GetForwardVector())*300.;
                for(int32 J=0;J<C.Recipe.BaselineMoves.Num();++J)
                {const auto& A=C.Recipe.BaselineMoves[J];if(P.X>=A.Start.X&&P.X<A.End.X&&!SteeringActions.Contains(J)){if(A.Kind==EBootBaselineMove::Jump)Runner->RequestJump();if(A.Kind==EBootBaselineMove::Slide)Runner->StartSlide();SteeringActions.Add(J);}}
                Runner->AddMovementInput((C.WorldTransform.TransformPosition(Target)-Runner->GetActorLocation()).GetSafeNormal2D(),1.f,true);break;
            }
            ++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60.f);Track->UpdateCourse(Runner,1.f/60.f);
        }
    }
};
FBootRunToken Token(){FBootRunToken R;R.RunId=FGuid(10,20,30,40);R.Generation=1;return R;}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootEndlessReadinessV04,"Bootloop.V04.World.ReadinessAndPinnedRetirement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootEndlessReadinessV04::RunTest(const FString&)
{
    const auto Envelope=FBootTraversalPolicy::FromTempo(182.,1);
    TestEqual(TEXT("smoothstep measured peak contribution"),FBootEndlessReadiness::RailPeakCmps(1500.,5.),450.);
    TestTrue(TEXT("readiness includes character cap plus actual moving support"),FBootEndlessReadiness::RequiredDistanceCm(Envelope,450.)>=8001.);
    BootEndlessWorldTest::FWorld W;
    auto* Track=W.World->SpawnActor<ABootEndlessTrack>();Track->BeginMusicRun(BootEndlessWorldTest::Token(),9,Envelope);
    if(!TestTrue(TEXT("prepared physical horizon exists"),Track->GetLiveChunkCount()>0))return false;
    TestTrue(TEXT("prepared horizon meets physical lookahead"),Track->GetPreparedHorizonCm()>=FBootEndlessReadiness::RequiredDistanceCm(Envelope,0.));
    const int64 First=Track->GetLiveChunks()[0]->GetDescriptor().InstanceId;
    TestTrue(TEXT("pending destination can pin a live chunk"),Track->PinChunk(First,EBootChunkPin::PendingDestination));
    TestTrue(TEXT("pin is retained"),Track->IsChunkPinned(First));
    auto* Runner=W.World->SpawnActor<ABootRunner>(FVector(350.,0.,98.),FRotator::ZeroRotator);
    Runner->GetParkour()->bRunPhysicsWithNoController=true;Runner->GetParkour()->SetMovementMode(MOVE_Walking);
    W.Step(Track,Runner,2400,true);
    TestTrue(TEXT("real movement passed multiple gates"),Track->GetProgressMetres()>120.);
    TestTrue(TEXT("passed pending destination is not retired"),Track->GetLiveChunks().ContainsByPredicate([&](const auto& C){return C->GetDescriptor().InstanceId==First;}));
    Track->UnpinChunk(First,EBootChunkPin::PendingDestination);W.Step(Track,Runner,2);
    TestFalse(TEXT("released passed destination retires"),Track->GetLiveChunks().ContainsByPredicate([&](const auto& C){return C->GetDescriptor().InstanceId==First;}));
    TestTrue(TEXT("chunk budget"),Track->GetLiveChunkCount()<=40);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootEndlessRevisionV04,"Bootloop.V04.World.RevisionCommit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootEndlessRevisionV04::RunTest(const FString&)
{
    BootEndlessWorldTest::FWorld W;auto* Track=W.World->SpawnActor<ABootEndlessTrack>();
    const auto Original=FBootTraversalPolicy::FromTempo(130.,1),Pending=FBootTraversalPolicy::FromTempo(168.,2);
    Track->BeginMusicRun(BootEndlessWorldTest::Token(),11,Original);
    FText Error;TestTrue(TEXT("future revision preparation"),Track->PrepareRevision(Pending,Error));
    TestEqual(TEXT("prepare does not mutate active commitment"),Track->GetActiveEnvelope().Revision,uint32(1));
    FBootSafeBoundary Boundary;TestFalse(TEXT("no supported runner means no safe commit"),Track->TryCommitRevision(Boundary));
    if(!TestTrue(TEXT("revision test has physical route"),Track->GetLiveChunkCount()>0))return false;
    const auto CommittedBefore=Track->GetLiveChunks()[0]->GetDescriptor();
    auto* Runner=W.World->SpawnActor<ABootRunner>(FVector(350.,0.,98.),FRotator::ZeroRotator);
    Runner->GetParkour()->bRunPhysicsWithNoController=true;Runner->GetParkour()->SetMovementMode(MOVE_Walking);W.Step(Track,Runner,20);
    TestTrue(TEXT("real collision supports runner"),Runner->GetCharacterMovement()->IsMovingOnGround());
    TestTrue(TEXT("supported boundary can be inspected without mutation"),Track->PeekRevisionBoundary(Boundary));
    TestEqual(TEXT("preflight leaves active revision unchanged"),Track->GetActiveEnvelope().Revision,uint32(1));
    auto InvalidBoundary=Boundary;InvalidBoundary.PreparedRevision=999;
    TestFalse(TEXT("invalid boundary cannot mutate prepared revision"),Track->CommitPreparedRevision(InvalidBoundary));
    TestEqual(TEXT("rejected transaction preserves active revision"),Track->GetActiveEnvelope().Revision,uint32(1));
    TestTrue(TEXT("safe supported connector commits prepared revision"),Track->CommitPreparedRevision(Boundary));
    TestTrue(TEXT("boundary contains readiness proof"),Boundary.bSupported&&Boundary.bNoActiveCommitment&&Boundary.bFutureReady);
    TestEqual(TEXT("prepared revision active"),Track->GetActiveEnvelope().Revision,uint32(2));
    TestTrue(TEXT("old descriptor geometry immutable"),Track->GetLiveChunks()[0]->GetDescriptor().WorldExit.Equals(CommittedBefore.WorldExit));
    TestEqual(TEXT("old descriptor revision immutable"),Track->GetLiveChunks()[0]->GetDescriptor().EnvelopeRevision,uint32(1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootEndlessLifecycleV04,"Bootloop.V04.World.BoundedLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootEndlessLifecycleV04::RunTest(const FString&)
{
    BootEndlessWorldTest::FWorld W;auto* Track=W.World->SpawnActor<ABootEndlessTrack>();
    auto Run=BootEndlessWorldTest::Token();const auto Envelope=FBootTraversalPolicy::FromTempo(130.,1);
    for(int32 I=0;I<10;++I)
    {
        ++Run.Generation;Track->BeginMusicRun(Run,uint64(I),Envelope);
        TestTrue(TEXT("reset creates bounded nonempty live world"),Track->GetLiveChunkCount()>0&&Track->GetLiveChunkCount()<=40);
        for(const auto& C:Track->GetLiveChunks())
        {
            TestTrue(TEXT("native geometry fully prepared"),C->IsReady());
            TestTrue(TEXT("gameplay child actor budget"),C->GetGameplayChildCount()<=8);
            TestTrue(TEXT("generation replaced every descriptor"),C->GetDescriptor().Run==Run);
        }
        Track->SetProcessesActive(false);Track->SetProcessesActive(true);
        TestFalse(TEXT("endless never reports a finish"),Track->IsFinished(FVector(1.e9,0.,0.)));
    }
    FHitResult Hit;
    TestTrue(TEXT("runtime recipes make real floor collision"),W.World->SweepSingleByChannel(Hit,FVector(1000.,0.,300.),FVector(1000.,0.,-100.),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeCapsule(34.f,96.f)));
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootEndlessDecreaseReadinessV04,"Bootloop.V04.World.DecreasingTempoReadiness",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootEndlessDecreaseReadinessV04::RunTest(const FString&)
{
    BootEndlessWorldTest::FWorld W;auto* Track=W.World->SpawnActor<ABootEndlessTrack>();
    const auto Fast=FBootTraversalPolicy::FromTempo(182.,1),Slow=FBootTraversalPolicy::FromTempo(91.,2);
    Track->BeginMusicRun(BootEndlessWorldTest::Token(),11,Fast);
    auto* Runner=W.World->SpawnActor<ABootRunner>(FVector(350.,0.,98.),FRotator::ZeroRotator);
    auto* Move=Runner->GetParkour();Move->bRunPhysicsWithNoController=true;Move->SetMovementMode(MOVE_Walking);Move->ApplyTraversalEnvelope(Fast);
    W.Step(Track,Runner,20);FText Error;TestTrue(TEXT("decreasing revision can prepare"),Track->PrepareRevision(Slow,Error));
    FBootSafeBoundary Boundary;TestTrue(TEXT("decreasing revision preflight"),Track->PeekRevisionBoundary(Boundary));
    Move->ApplyTraversalEnvelope(Slow);TestTrue(TEXT("movement begins gradual decrease"),Move->CommitTraversalEnvelope(Boundary,.5f));
    TestTrue(TEXT("track commits matching target"),Track->CommitPreparedRevision(Boundary));
    TestTrue(TEXT("readiness retains actual fast cap at blend start"),Track->GetRequiredHorizonCm()>=FBootEndlessReadiness::RequiredDistanceCm(Fast,450.));
    W.Step(Track,Runner,15);
    const double ActualRequired=FBootEndlessReadiness::RequiredDistanceCm(Move->GetTraversalEnvelope(),450.);
    TestTrue(TEXT("readiness follows conservative actual cap during decrease"),Track->GetRequiredHorizonCm()>=ActualRequired-.01);
    TestTrue(TEXT("prepared distance covers blend"),Track->GetPreparedHorizonCm()>=Track->GetRequiredHorizonCm());
    return true;
}
