#include "UI/BootPlayerController.h"
#include "Run/BootGameMode.h"
#include "Core/BootGameInstance.h"
#include "UI/BootShellWidget.h"
#include "UI/BootMusicLibraryWidget.h"
#include "UI/BootRunCardsWidget.h"
#include "Music/BootMusicLibrarySubsystem.h"
#include "Player/BootRunner.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"

void ABootPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    Shell = CreateWidget<UBootShellWidget>(this, UBootShellWidget::StaticClass());
    Shell->AddToViewport(10);
    LibraryWidget=CreateWidget<UBootMusicLibraryWidget>(this,UBootMusicLibraryWidget::StaticClass());LibraryWidget->AddToViewport(20);
    CardsWidget=CreateWidget<UBootRunCardsWidget>(this,UBootRunCardsWidget::StaticClass());CardsWidget->AddToViewport(30);
    if(auto* GI=GetGameInstance())if(auto* Library=GI->GetSubsystem<UBootMusicLibrarySubsystem>())Library->BootstrapStarterLibrary();
    ShowPage(EBootPage::Desktop);
}

void ABootPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ABootPlayerController::TogglePause).bExecuteWhenPaused = true;
}

bool ABootPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    // UE reconciles a repeat after FlushPressedKeys into a synthetic press. Menu
    // capture can consume every repeat until Resume rebuilds our mappings, losing
    // their old release gates. A repeat must never restart a flushed digital key.
    // Already-down keys keep normal repeat/continuous Enhanced Input behavior.
    if(Params.Event==IE_Repeat && Params.Key.IsDigital() && PlayerInput && !PlayerInput->IsPressed(Params.Key)) return true;
    return Super::InputKey(Params);
}

void ABootPlayerController::ShowPage(EBootPage NewPage)
{
    Page = NewPage;
    bShowMouseCursor = Page != EBootPage::Hud && Page != EBootPage::Boot;
    if(NewPage==EBootPage::Hud) if(auto* Runner=Cast<ABootRunner>(GetPawn())) Runner->ApplyInputPreferences();
    FlushPressedKeys();
    if (Shell) Shell->ChangePage(Page);
    if(LibraryWidget)LibraryWidget->SetActive(Page==EBootPage::Library);
    if(CardsWidget)CardsWidget->SetActive(Page==EBootPage::Hud);
    if (bShowMouseCursor)
    {
        FInputModeGameAndUI Mode;
        if(Page==EBootPage::Library&&LibraryWidget)Mode.SetWidgetToFocus(LibraryWidget->TakeWidget());
        else if (Shell) Mode.SetWidgetToFocus(Shell->TakeWidget());
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(Mode);
        if(Page==EBootPage::Library&&LibraryWidget)LibraryWidget->SetKeyboardFocus();
        else if (Shell) Shell->SetKeyboardFocus();
    }
    else SetInputMode(FInputModeGameOnly());
}

void ABootPlayerController::StartRun()
{
    if (auto* Game = GetWorld()->GetAuthGameMode<ABootGameMode>()) Game->StartRun();
}

void ABootPlayerController::TogglePause()
{
    if (Page == EBootPage::Settings || Page == EBootPage::Controls || Page==EBootPage::Inputs || Page==EBootPage::Library) { Back(); return; }
    if (auto* Game = GetWorld()->GetAuthGameMode<ABootGameMode>())
    {
        if (Game->GetRunState() == EBootRunState::Running) { Game->PauseRun(true); ShowPage(EBootPage::Pause); }
        else if (Game->GetRunState() == EBootRunState::Paused) { Game->PauseRun(false); ShowPage(EBootPage::Hud); }
    }
}

void ABootPlayerController::Desktop()
{
    if (auto* Game = GetWorld()->GetAuthGameMode<ABootGameMode>()) Game->ReturnToDesktop();
    ShowPage(EBootPage::Desktop);
}

void ABootPlayerController::ShowSettings()
{
    ReturnPage = Page;
    ShowPage(EBootPage::Settings);
}

void ABootPlayerController::ShowControls()
{
    ReturnPage = Page;
    ShowPage(EBootPage::Controls);
}

void ABootPlayerController::ShowInputs() { ReturnPage=Page; ShowPage(EBootPage::Inputs); }
void ABootPlayerController::StartTutorial()
{
    const auto* Game=GetWorld()->GetAuthGameMode<ABootGameMode>();
    if(Game && Game->IsFirstRun()) StartRun();
    else
    {
        if(auto* Active=GetWorld()->GetAuthGameMode<ABootGameMode>())Active->ReturnToDesktop();
        UGameplayStatics::SetGamePaused(this,false);
        UGameplayStatics::OpenLevel(this,TEXT("/Game/Game/Levels/L_FirstRun"),true,TEXT("BootAutoStart=true"));
    }
}
void ABootPlayerController::StartPrimaryRun()
{
    const auto* GI=GetGameInstance<UBootGameInstance>();
    if(GI && GI->GetProfile()->HasCurrentOnboarding())StartMusicRun();else StartTutorial();
}
void ABootPlayerController::StartCircuit()
{
    const auto* Game=GetWorld()->GetAuthGameMode<ABootGameMode>();
    if(Game && Game->GetCourseId()==TEXT("RooftopCircuitV03")) StartRun();
    else UGameplayStatics::OpenLevel(this,TEXT("/Game/Game/Levels/L_RooftopCircuit"),true,TEXT("BootAutoStart=true"));
}
void ABootPlayerController::StartMusicRun()
{
    const auto* Game=GetWorld()->GetAuthGameMode<ABootGameMode>();
    if(Game&&Game->IsMusicRun())StartRun();
    else
    {
        if(auto* Active=GetWorld()->GetAuthGameMode<ABootGameMode>())Active->ReturnToDesktop();
        UGameplayStatics::SetGamePaused(this,false);
        UGameplayStatics::OpenLevel(this,TEXT("/Game/Game/Levels/L_MusicRun"),true,TEXT("BootAutoStart=true"));
    }
}
void ABootPlayerController::ShowMusicLibrary()
{
    if(Page!=EBootPage::Library)ReturnPage=Page;
    if(auto* Game=GetWorld()->GetAuthGameMode<ABootGameMode>())
    {
        if(Game->GetRunState()==EBootRunState::Running)Game->PauseRun(true);
        if(Game->GetRunState()==EBootRunState::Paused)ReturnPage=EBootPage::Pause;
        else if(Game->GetRunState()==EBootRunState::Desktop)ReturnPage=EBootPage::Desktop;
    }
    ShowPage(EBootPage::Library);
}
void ABootPlayerController::SkipLesson()
{ if(auto* Game=GetWorld()->GetAuthGameMode<ABootGameMode>()) Game->SkipLesson(); }

void ABootPlayerController::Back() { ShowPage(ReturnPage); }
void ABootPlayerController::Quit() { UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false); }
