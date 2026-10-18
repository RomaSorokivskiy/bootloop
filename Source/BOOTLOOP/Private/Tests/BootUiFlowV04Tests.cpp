#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/BootRunCardsWidget.h"
#include "UI/BootMusicLibraryWidget.h"
#include "UI/BootPlayerController.h"
#include "Run/BootGameMode.h"
#include "Core/BootGameInstance.h"
#include "Player/BootRunner.h"
#include "Music/BootMusicLibrarySubsystem.h"
#include "Tutorial/BootFirstRunTrack.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWidget.h"

struct FBootV04UiTestAccess
{
    static const FBootRunSnapshot& Cards(const UBootRunCardsWidget* Widget){return Widget->View;}
    static void SeedStaleRow(UBootMusicLibraryWidget* Widget){FBootLibraryEntry E;E.Hash=TEXT("stale-sentinel");Widget->Items={E};}
    static int32 Rows(const UBootMusicLibraryWidget* Widget){return Widget->Items.Num();}
};
namespace
{
class FBootPausedLibraryCommand : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    UWorld* World=nullptr;
    UBootGameInstance* GI=nullptr;
    ABootPlayerController* PC=nullptr;
    ULocalPlayer* Local=nullptr;
    TStrongObjectPtr<UBootMusicLibraryWidget> LibraryWidget;
    TStrongObjectPtr<UBootRunCardsWidget> CardsWidget;
    TSharedPtr<SWidget> CardsSlate,LibrarySlate;
    FString Root;
    double WaitUntil=0.;
    bool bHidden=false;
public:
    explicit FBootPausedLibraryCommand(FAutomationTestBase* In):Test(In){}
    bool Update() override
    {
        if(!World)
        {
            UWorld::InitializationValues Init;Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
            GI=NewObject<UBootGameInstance>(GEngine);GI->InitializeStandalone();UWorld* Dummy=GI->GetWorld();
            World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
            World->SetGameInstance(GI);GI->GetWorldContext()->SetCurrentWorld(World);Dummy->DestroyWorld(false);
            Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("V04Audit/UIFlow")/FGuid::NewGuid().ToString());
            Test->TestTrue(TEXT("isolated empty library"),GI->GetSubsystem<UBootMusicLibrarySubsystem>()->InitializeFixture(Root));
            World->GetWorldSettings()->DefaultGameMode=ABootFirstRunGameMode::StaticClass();World->SetGameMode(FURL());World->InitializeActorsForPlay(FURL());
            auto* Mode=World->GetAuthGameMode<ABootGameMode>();Mode->DispatchBeginPlay();
            PC=World->SpawnActor<ABootPlayerController>();PC->SetAsLocalPlayerController();
            Local=NewObject<ULocalPlayer>(GEngine);Local->PlayerController=PC;PC->Player=Local;GI->AddLocalPlayer(Local,FPlatformUserId::CreateFromInternalId(0));
            auto* Runner=World->SpawnActor<ABootRunner>(FVector(350,0,100),FRotator::ZeroRotator);PC->Possess(Runner);
            CardsWidget.Reset(CreateWidget<UBootRunCardsWidget>(PC));LibraryWidget.Reset(CreateWidget<UBootMusicLibraryWidget>(PC));
            CardsSlate=CardsWidget->TakeWidget();LibrarySlate=LibraryWidget->TakeWidget();
            Mode->StartRun();++GFrameCounter;World->GetTimerManager().Tick(.001f);++GFrameCounter;World->GetTimerManager().Tick(1.2f);
            auto* Track=CastChecked<ABootFirstRunTrack>(Mode->GetTrack());
            for(int32 I=0;I<7;++I)Mode->SkipLesson();
            Test->TestEqual(TEXT("native lesson seven reached"),Track->GetTutorial().GetLesson(),7);
            const auto Page=PC->GetPage();CardsWidget->SetActive(true);
            Test->TestTrue(TEXT("trial offer rendered by cards view"),FBootV04UiTestAccess::Cards(CardsWidget.Get()).CardOffer.IsSet());
            Test->TestEqual(TEXT("card overlay retains gameplay page"),PC->GetPage(),Page);
            Test->TestFalse(TEXT("cards do not pause"),UGameplayStatics::IsGamePaused(World));
            Test->TestEqual(TEXT("cards never consume pointer input"),CardsWidget->GetVisibility(),ESlateVisibility::HitTestInvisible);
            CardsWidget->SetActive(false);Mode->PauseRun(true);LibraryWidget->SetActive(true);
            FBootV04UiTestAccess::SeedStaleRow(LibraryWidget.Get());WaitUntil=FPlatformTime::Seconds()+.4;return false;
        }
        if(FPlatformTime::Seconds()<WaitUntil)return false;
        if(!bHidden)
        {
            Test->TestTrue(TEXT("world remains paused during library refresh"),UGameplayStatics::IsGamePaused(World));
            Test->TestEqual(TEXT("real-time refresh replaced stale rows while gameplay timer is frozen"),FBootV04UiTestAccess::Rows(LibraryWidget.Get()),0);
            LibraryWidget->SetActive(false);FBootV04UiTestAccess::SeedStaleRow(LibraryWidget.Get());
            bHidden=true;WaitUntil=FPlatformTime::Seconds()+.3;return false;
        }
        Test->TestEqual(TEXT("hidden library no longer refreshes rows"),FBootV04UiTestAccess::Rows(LibraryWidget.Get()),1);
        World->GetAuthGameMode<ABootGameMode>()->ReturnToDesktop();CardsSlate.Reset();LibrarySlate.Reset();CardsWidget.Reset();LibraryWidget.Reset();
        GI->RemoveLocalPlayer(Local);Local->PlayerController=nullptr;PC->Player=nullptr;GI->Shutdown();
        GEngine->DestroyWorldContext(World);World->DestroyWorld(false);IFileManager::Get().DeleteDirectory(*Root,false,true);return true;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootUiPauseV04,"Bootloop.V04.Flow.PausedLibraryAndTrialCards",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootUiPauseV04::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(FBootPausedLibraryCommand(this));return true;}
#endif
