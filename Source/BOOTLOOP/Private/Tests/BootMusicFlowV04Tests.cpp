#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Run/BootGameMode.h"
#include "Core/BootGameInstance.h"
#include "Music/BootMusicLibrarySubsystem.h"
#include "World/Generation/BootEndlessTrack.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootNoReadyFlow,"Bootloop.V04.Flow.DesktopLibraryRun",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootNoReadyFlow::RunTest(const FString&)
{
    UWorld::InitializationValues Init;Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* GI=NewObject<UBootGameInstance>(GEngine);World->SetGameInstance(GI);GI->Init();
    auto* Library=GI->GetSubsystem<UBootMusicLibrarySubsystem>();
    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("V04Audit/Flow")/FGuid::NewGuid().ToString());
    if(Library)TestTrue(TEXT("isolated library"),Library->InitializeFixture(Root));
    World->GetWorldSettings()->DefaultGameMode=ABootMusicRunGameMode::StaticClass();World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());World->BeginPlay();World->GetWorldSettings()->NotifyBeginPlay();
    auto* Mode=World->GetAuthGameMode<ABootGameMode>();
    if(TestNotNull(TEXT("native mode"),Mode))
    {
        TestTrue(TEXT("music mode through base pointer"),Mode->IsMusicRun());
        TestTrue(TEXT("native endless factory"),Mode->GetTrack()->IsA<ABootEndlessTrack>());
        Mode->StartRun();TestTrue(TEXT("no ready track leaves desktop available"),Mode->GetRunState()==EBootRunState::Desktop);
    }
    if(Library)TestEqual(TEXT("headless map does not queue ten imports"),Library->Snapshot().Num(),0);
    World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    IFileManager::Get().DeleteDirectory(*Root,false,true);return true;
}
#endif
