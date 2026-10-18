#include "Core/BootGameInstance.h"
#include "Developer/BootAuditPaths.h"
#include "Player/BootRunner.h"
#include "Music/BootRhythmComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/BootDisplaySettings.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

static FString ProfileSlot()
{
    return FBootAuditPaths::ProfileSlot(FCommandLine::Get());
}

UBootGameInstance::UBootGameInstance()
{
    const TCHAR* Names[] = {TEXT("S_UI_Click"), TEXT("S_Boot"), TEXT("S_Jump"), TEXT("S_Slide"), TEXT("S_Perfect"),
        TEXT("S_Good"), TEXT("S_Error"), TEXT("S_Fatal"), TEXT("S_Combo")};
    for (const TCHAR* Name : Names)
    {
        ConstructorHelpers::FObjectFinder<USoundBase> Wave(*FString::Printf(TEXT("/Game/Assets/Audio/%s.%s"), Name, Name));
        Sounds.Add(Wave.Object);
    }
    for(const TCHAR* Name : {TEXT("S_PlasticStep"),TEXT("S_Land"),TEXT("S_Snap")})
    {
        ConstructorHelpers::FObjectFinder<USoundBase> Wave(*FString::Printf(TEXT("/Game/Assets/AudioV02/%s.%s"),Name,Name));
        Sounds.Add(Wave.Object);
    }
}

void UBootGameInstance::Init()
{
    Super::Init();
    if (UGameplayStatics::DoesSaveGameExist(ProfileSlot(), 0))
        Profile = Cast<UBootProfile>(UGameplayStatics::LoadGameFromSlot(ProfileSlot(), 0));
    if (!Profile)
    {
        Profile = NewObject<UBootProfile>(this);
        // Keep the old scoring record intact, while carrying over the player's preferences.
        if(!FBootAuditPaths::IsIsolatedProfile(FCommandLine::Get()) && UGameplayStatics::DoesSaveGameExist(TEXT("BootloopProfile_v1"),0))
            if(const auto* Old=Cast<UBootProfile>(UGameplayStatics::LoadGameFromSlot(TEXT("BootloopProfile_v1"),0))) Profile->Options=Old->Options;
    }
    Profile->Options.Clamp();
    RefreshDisplayModes();
    NormalizeDisplayOptions(Profile->Options);
    Profile->Inputs.Sanitize();
    Profile->BestScore = FMath::Max(Profile->BestScore, 0);
    Profile->BestDistance = FMath::IsFinite(Profile->BestDistance) ? FMath::Max(0.f, Profile->BestDistance) : 0.f;
}

bool UBootGameInstance::ApplyInputPreferences(const FBootInputPreferences& Preferences)
{
    FText Error; if(!Profile || !Preferences.IsValid(Error)) return false;
    Profile->Inputs=Preferences;
    if(auto* Runner=Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this,0))) Runner->ApplyInputPreferences();
    return SaveProfile();
}

bool UBootGameInstance::SaveProfile()
{
    bSaveError = !UGameplayStatics::SaveGameToSlot(Profile, ProfileSlot(), 0);
    if (bSaveError) UE_LOG(LogTemp, Warning, TEXT("BOOTLOOP: local profile could not save"));
    return !bSaveError;
}

void UBootGameInstance::RefreshDisplayModes()
{
    NativeResolution=FBootDisplaySettings::CurrentMonitorNative();
    TArray<FIntPoint> Supported;
    UKismetSystemLibrary::GetSupportedFullscreenResolutions(Supported);
    DisplayModes=FBootDisplaySettings::BuildModes(NativeResolution,Supported);
}

void UBootGameInstance::NormalizeDisplayOptions(FBootOptions& Options) const
{
    if(DisplayModes.IsEmpty())return;
    FIntPoint Preferred=Options.ResolutionSize;
    if(Preferred.X<=0 || Preferred.Y<=0)
    {
        const FIntPoint Legacy[]={FIntPoint(1280,720),FIntPoint(1600,900),FIntPoint(1920,1080)};
        Preferred=Legacy[FMath::Clamp(Options.Resolution,0,2)];
    }
    Options.Resolution=FBootDisplaySettings::ClosestMode(Preferred,DisplayModes);
    Options.ResolutionSize=DisplayModes[Options.Resolution];
}

void UBootGameInstance::ApplyOptions(const FBootOptions& Options, bool bDisplay)
{
    Profile->Options = Options;
    Profile->Options.Clamp();
    if(bDisplay)RefreshDisplayModes();
    NormalizeDisplayOptions(Profile->Options);
    const FBootOptions& O = Profile->Options;
    if (GEngine && bDisplay)
    {
        UGameUserSettings* User = GEngine->GetGameUserSettings();
        const float Limits[] = {60, 120, 144, 0};
        User->SetScreenResolution(O.ResolutionSize);
        User->SetFullscreenMode(O.WindowMode == 0 ? EWindowMode::Windowed : O.WindowMode == 1 ? EWindowMode::WindowedFullscreen : EWindowMode::Fullscreen);
        User->SetFrameRateLimit(Limits[O.FrameLimit]);
        User->ApplySettings(false);
        User->ConfirmVideoMode();
        User->SaveSettings();
    }
    if (ABootRunner* Runner = Cast<ABootRunner>(UGameplayStatics::GetPlayerPawn(this, 0))) Runner->ApplyPreferences(O);
    SaveProfile();
    UE_LOG(LogTemp, Display, TEXT("BOOTLOOP: preferences applied music=%.2f sfx=%.2f fov=%.0f"), O.Music, O.Sfx, O.Fov);
}

void UBootGameInstance::Sound(EBootSound Id,float Pitch)
{
    const int32 Index = static_cast<int32>(Id);
    if (Profile && Sounds.IsValidIndex(Index) && Sounds[Index] && GetWorld() && GetWorld()->AllowAudioPlayback())
        UGameplayStatics::PlaySound2D(this, Sounds[Index], Profile->Options.Sfx * .6f, Pitch);
}


