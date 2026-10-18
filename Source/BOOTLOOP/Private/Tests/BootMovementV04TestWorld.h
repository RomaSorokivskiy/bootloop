#pragma once
#include "Movement/BootMovementComponent.h"
#include "Player/BootRunner.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

/** Real collision and CharacterMovement fixture; no mock physics or charge setters. */
struct FBootMovementV04World
{
    UWorld* World = nullptr;
    ABootRunner* Runner = nullptr;
    UBootMovementComponent* Move = nullptr;
    FBootMovementV04World()
    {
        UWorld::InitializationValues Init;
        Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
            .CreateAISystem(false).ShouldSimulatePhysics(true).SetTransactional(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
            true, ERHIFeatureLevel::Num, &Init);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        Box(FVector(0, 0, -50), FVector(10000, 1000, 1));
        Runner = World->SpawnActor<ABootRunner>(FVector(0, 0, 100), FRotator::ZeroRotator);
        Move = CastChecked<UBootMovementComponent>(Runner->GetCharacterMovement());
        Move->bRunPhysicsWithNoController = true;
        World->InitializeActorsForPlay(FURL()); World->BeginPlay();
        World->GetWorldSettings()->NotifyBeginPlay();
    }
    ~FBootMovementV04World()
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }
    AStaticMeshActor* Box(FVector Position, FVector Scale)
    {
        auto* Actor = World->SpawnActor<AStaticMeshActor>(Position, FRotator::ZeroRotator);
        auto* Mesh = Actor->GetStaticMeshComponent();
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Mesh->SetCollisionProfileName(TEXT("BlockAll")); Actor->SetActorScale3D(Scale);
        return Actor;
    }
    void Step(int32 Count, float Dt = 1.f / 60.f, bool bForward = false)
    {
        for (int32 I = 0; I < Count; ++I)
        {
            if (bForward) Runner->AddMovementInput(FVector::ForwardVector, 1.f, true);
            ++GFrameCounter; World->Tick(LEVELTICK_All, Dt);
        }
    }
    void Air(FVector Velocity = FVector(900, 0, 123), FVector Position = FVector(0, 0, 5000))
    {
        Move->ResetParkour(); Runner->UnCrouch();
        Runner->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
        Move->SetMovementMode(MOVE_Falling); Move->Velocity = Velocity;
    }
    void Ground()
    {
        Move->GravityScale = 1.7f;
        Runner->SetActorLocation(FVector(0, 0, 98), false, nullptr, ETeleportType::TeleportPhysics);
        Move->Velocity = FVector::ZeroVector; Move->SetMovementMode(MOVE_Falling);
        for (int32 I = 0; I < 12 && !Move->IsMovingOnGround(); ++I) Step(1, .01f);
    }
};
