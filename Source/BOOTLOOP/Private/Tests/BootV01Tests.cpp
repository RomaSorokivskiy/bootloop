#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "World/Generation/BootTrackRules.h"
#include "Profile/BootProfile.h"
#include "World/Generation/BootTrack.h"
#include "Player/BootRunner.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV01RulesTest, "Bootloop.V01.SeedsAndSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootV01RulesTest::RunTest(const FString& Parameters)
{
    for (int32 Seed = 0; Seed < 50; ++Seed)
    {
        FBootTrackSequence A(Seed), B(Seed);
        int32 Last = -1, Repeats = 0, Heavy = 0;
        for (int32 I = 0; I < 100; ++I)
        {
            const int32 Type = A.Next(I / 12);
            TestEqual(TEXT("Seed is reproducible"), Type, B.Next(I / 12));
            TestTrue(TEXT("Type valid"), Type >= 0 && Type < 6);
            Repeats = Type == Last ? Repeats + 1 : 1;
            Heavy = FBootTrackSequence::IsHeavy(Type) ? Heavy + 1 : 0;
            TestTrue(TEXT("No three identical chunks"), Repeats <= 2);
            TestTrue(TEXT("No three heavy chunks"), Heavy <= 2);
            Last = Type;
        }
    }
    FBootOptions Options;
    Options.Music = -10; Options.Sfx = 10; Options.Sensitivity = 100;
    Options.Fov = 999; Options.InputOffsetMs = -999; Options.UiScale = 9;
    Options.Clamp();
    TestEqual(TEXT("Music clamp"), Options.Music, 0.f);
    TestEqual(TEXT("SFX clamp"), Options.Sfx, 1.f);
    TestEqual(TEXT("Sensitivity clamp"), Options.Sensitivity, .4f);
    TestEqual(TEXT("FOV clamp"), Options.Fov, 110.f);
    TestEqual(TEXT("Offset clamp"), Options.InputOffsetMs, -250.f);
    TestEqual(TEXT("UI scale clamp"), Options.UiScale, 1.25f);
    UBootProfile* Profile = NewObject<UBootProfile>();
    Profile->UpdateBest(500, 120.f);
    Profile->UpdateBest(100, 20.f);
    TestEqual(TEXT("Smaller run never overwrites best"), Profile->BestScore, 500);
    TestEqual(TEXT("Distance best retained"), Profile->BestDistance, 120.f);
    TArray<uint8> Data;
    TestTrue(TEXT("Profile serializes"), UGameplayStatics::SaveGameToMemory(Profile, Data));
    UBootProfile* Loaded=Cast<UBootProfile>(UGameplayStatics::LoadGameFromMemory(Data));
    if(TestNotNull(TEXT("Profile class survives load"),Loaded)) TestEqual(TEXT("Record survives serialization"),Loaded->BestScore,500);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV01WorldTest,"Bootloop.V01.ChunkCollisionAndTraversal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootV01WorldTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues Init;
    Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
    auto Step=[&](int32 Frames) { for(int32 I=0;I<Frames;++I) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60.f); } };
    ABootTrack* Track=World->SpawnActor<ABootTrack>(); Track->ResetTrack(1301995);
    TestEqual(TEXT("Eight chunks ahead at start"),Track->GetActiveCount(),8);
    for(int32 I=1;I<Track->GetChunks().Num();++I)
        TestEqual(TEXT("Exit exactly meets next Entry"),Track->GetChunks()[I-1]->EndX(),Track->GetChunks()[I]->GetActorLocation().X);
    auto Hits=[&](FVector Start,FVector End) { FHitResult Hit; return World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility); };
    for(int32 Type=0;Type<6;++Type)
    {
        const FVector Origin(40000+Type*3000,0,0);
        ABootTrackChunk* Chunk=World->SpawnActor<ABootTrackChunk>(Origin,FRotator::ZeroRotator);
        Chunk->BuildChunk(Type,Type,Track->Materials);
        TestTrue(TEXT("Entry floor collision"),Hits(Origin+FVector(20,0,80),Origin+FVector(20,0,-80)));
        TestTrue(TEXT("Exit floor collision"),Hits(Origin+FVector(1780,0,80),Origin+FVector(1780,0,-80)));
        if(Type==1) TestFalse(TEXT("Gap contains no hidden floor"),Hits(Origin+FVector(1000,0,80),Origin+FVector(1000,0,-80)));
        if(Type==2)
        {
            TestTrue(TEXT("Standing height hits beam"),Hits(Origin+FVector(700,0,160),Origin+FVector(1300,0,160)));
            TestFalse(TEXT("Crouched height clears beam"),Hits(Origin+FVector(700,0,90),Origin+FVector(1300,0,90)));
        }
        if(Type==4)
        {
            TestTrue(TEXT("Red lane is fatal"),Chunk->IsFatal(Origin+FVector(960,-270,100),34,96));
            TestFalse(TEXT("Right lane is safe"),Chunk->IsFatal(Origin+FVector(960,270,100),34,96));
        }
        if(Type==1 || Type==2 || Type==5)
        {
            ABootRunner* Runner=World->SpawnActor<ABootRunner>(Origin+FVector(Type==1?650:Type==5?350:700,0,100),FRotator::ZeroRotator);
            Runner->GetCharacterMovement()->bRunPhysicsWithNoController=true;
            Runner->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            Step(20);
            AddInfo(FString::Printf(TEXT("Type %d before: %s velocity=%s grounded=%d begun=%d"),Type,*Runner->GetActorLocation().ToString(),*Runner->GetVelocity().ToString(),Runner->GetCharacterMovement()->IsMovingOnGround(),Runner->HasActorBegunPlay()));
            Runner->GetCharacterMovement()->Velocity=FVector(900,0,0);
            if(Type==2) Runner->StartSlide(); else Runner->RequestJump();
            Step(45);
            AddInfo(FString::Printf(TEXT("Type %d after: %s relative=%s velocity=%s grounded=%d dead=%d"),Type,*Runner->GetActorLocation().ToString(),*(Runner->GetActorLocation()-Origin).ToString(),*Runner->GetVelocity().ToString(),Runner->GetCharacterMovement()->IsMovingOnGround(),Runner->IsDead()));
            TestFalse(TEXT("Base-speed traversal remains alive"),Runner->IsDead());
            TestTrue(TEXT("Gap/ceiling traversal reaches safe ground"),Runner->GetCharacterMovement()->IsMovingOnGround());
            TestTrue(TEXT("Traversal moved beyond obstacle"),Runner->GetActorLocation().X-Origin.X>(Type==1?1150:Type==5?800:1100));
            if(Type==5)
            {
                Runner->GetCharacterMovement()->Velocity=FVector(900,0,0);
                Runner->RequestJump(); Step(45);
                TestTrue(TEXT("Second gap lands beyond1480"),Runner->GetActorLocation().X-Origin.X>1480);
                TestTrue(TEXT("Second gap landing is grounded"),Runner->GetCharacterMovement()->IsMovingOnGround());
            }
            Runner->Destroy();
        }
        Chunk->Destroy();
    }
    for(int32 I=0;I<100;++I) Track->Advance(I*1800.);
    TestTrue(TEXT("Long run keeps bounded chunks"),Track->GetActiveCount()<=12);
    Track->ResetTrack(1301995); TestEqual(TEXT("Retry restores eight fresh chunks"),Track->GetActiveCount(),8);
    World->EndPlay(EEndPlayReason::Quit); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}
#endif
