#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BootPlayerController.generated.h"

class UBootShellWidget;
class UBootMusicLibraryWidget;
class UBootRunCardsWidget;
enum class EBootPage : uint8 { Desktop, Pause, Settings, Controls, Results, Boot, Hud, Inputs, Library };

UCLASS()
class BOOTLOOP_API ABootPlayerController : public APlayerController
{
    GENERATED_BODY()
    friend struct FBootShellInputTestAccess;
public:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    void StartRun();
    void TogglePause();
    void Desktop();
    void ShowPage(EBootPage Page);
    EBootPage GetPage() const { return Page; }
    void ShowSettings();
    void ShowControls();
    void ShowInputs();
    void StartTutorial();
    void StartCircuit();
    void StartMusicRun();
    void StartPrimaryRun();
    void ShowMusicLibrary();
    void SkipLesson();
    void Back();
    void Quit();
    UBootShellWidget* GetShell() const { return Shell; }
private:
    UPROPERTY() TObjectPtr<UBootShellWidget> Shell;
    UPROPERTY() TObjectPtr<UBootMusicLibraryWidget> LibraryWidget;
    UPROPERTY() TObjectPtr<UBootRunCardsWidget> CardsWidget;
    EBootPage Page = EBootPage::Desktop;
    EBootPage ReturnPage = EBootPage::Desktop;
};
