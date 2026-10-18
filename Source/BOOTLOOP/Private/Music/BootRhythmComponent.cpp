#include "Music/BootRhythmComponent.h"
#include "Music/BootMusicTransportComponent.h"
#include "Music/BootBeatJudge.h"
#include "Run/BootGameMode.h"
#include "Core/BootGameInstance.h"
#include "World/Generation/BootTrack.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Quartz/AudioMixerClockHandle.h"
#include "Quartz/QuartzSubsystem.h"
#include "Sound/SoundWave.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

static TAutoConsoleVariable<float> CVarBootInputOffset(TEXT("bootloop.InputOffsetMs"), 0.f,
    TEXT("Rhythm calibration in milliseconds. Positive values move the judgement centre later."));

void UBootRhythmCommandProxy::OnCommand(EQuartzCommandDelegateSubType EventType, FName EventName)
{
    if (UBootRhythmComponent* Owner = RhythmOwner.Get()) Owner->OnMusicCommand(EventType, ClockId);
}

UBootRhythmComponent::UBootRhythmComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    static ConstructorHelpers::FObjectFinder<USoundWave> Loop(TEXT("/Game/Assets/AudioV02/loop_130bpm_16bars.loop_130bpm_16bars"));
    MusicLoop = Loop.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Title(TEXT("/Game/Assets/PrototypeMaterials/MI_Title.MI_Title"));
    TitleMaterial = Title.Object;
}

void UBootRhythmComponent::BeginPlay()
{
    Super::BeginPlay();
    if (TitleMaterial)
    {
        PulseMaterial = UMaterialInstanceDynamic::Create(TitleMaterial, this);
        BaseColor = PulseMaterial->K2_GetVectorParameterValue(TEXT("FlatColor"));
        for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
        {
            UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
            if (Mesh && Mesh->GetMaterial(0) == TitleMaterial) Mesh->SetMaterial(0, PulseMaterial);
        }
    }
    if (!GetWorld()->GetAuthGameMode<ABootGameMode>()) ResetRun();
}

void UBootRhythmComponent::ResetRun()
{
    StopRun();
    ResetScoring();
    bRunPaused = false;
    if(ExternalTransport.IsValid())return;
    // Movement remains independent of audio availability (including headless tests).
    if (!GetWorld()->AllowAudioPlayback() || !GetWorld()->GetAudioDeviceRaw() || !MusicLoop) return;
    UQuartzSubsystem* Quartz = GetWorld()->GetSubsystem<UQuartzSubsystem>();
    if (!Quartz) return;
    if (!Music)
    {
        Music = NewObject<UAudioComponent>(GetOwner(), TEXT("BootMusic"));
        Music->bAutoActivate = false;
        Music->bAutoDestroy = false;
        Music->bAllowSpatialization = false;
        Music->SetSound(MusicLoop);
        Music->SetVolumeMultiplier(MusicVolume * .65f);
        Music->RegisterComponent();
    }
    Music->SetPaused(false);
    ActiveClockName = FName(*FString::Printf(TEXT("Bootloop_%u_%d"), GetUniqueID(), ++RunSerial));
    Clock = Quartz->CreateNewClock(this, ActiveClockName, FQuartzClockSettings{});
    if (!Clock) { ActiveClockName = NAME_None; return; }
    UQuartzClockHandle* Handle = Clock;
    FQuartzQuantizationBoundary Boundary;
    FOnQuartzCommandEventBP TempoCallback;
    Handle->SetBeatsPerMinute(this, Boundary, TempoCallback, Handle, 130.f);
    FOnQuartzMetronomeEventBP BeatCallback;
    BeatCallback.BindDynamic(this, &UBootRhythmComponent::OnBeat);
    Handle->SubscribeToQuantizationEvent(this, EQuartzCommandQuantization::Beat, BeatCallback, Handle);
    FOnQuartzCommandEventBP MusicCallback;
    CommandProxy = NewObject<UBootRhythmCommandProxy>(this);
    CommandProxy->RhythmOwner = this;
    CommandProxy->ClockId = ActiveClockName;
    MusicCallback.BindDynamic(CommandProxy, &UBootRhythmCommandProxy::OnCommand);
    Boundary.Quantization = EQuartzCommandQuantization::Bar;
    Boundary.bFireOnClockStart = true;
    Music->PlayQuantized(this, Handle, Boundary, MusicCallback);
    Handle->StartClock(this, Handle);
    UE_LOG(LogTemp, Display, TEXT("BOOTLOOP RHYTHM: queued %s at 130 BPM, loop %.6fs"), *ActiveClockName.ToString(), MusicLoop->Duration);
}

void UBootRhythmComponent::StopRun()
{
    bReady = false;
    bRunPaused = false;
    LastBeatTime = -100.;
    GetWorld()->GetTimerManager().ClearTimer(PulseTimer);
    if (PulseMaterial) PulseMaterial->SetVectorParameterValue(TEXT("FlatColor"), BaseColor);
    if (auto* Game = GetWorld()->GetAuthGameMode<ABootGameMode>()) if (Game->GetTrack()) Game->GetTrack()->Pulse(0.f);
    const FName OldName = ActiveClockName;
    ActiveClockName = NAME_None; // Ignore any game-thread callbacks already in flight.
    if (Music) Music->Stop();
    if (Clock)
    {
        UQuartzClockHandle* Handle = Clock;
        Handle->UnsubscribeFromAllTimeDivisions(this, Handle);
        Handle->StopClock(this, true, Handle);
        if (UQuartzSubsystem* Quartz = GetWorld()->GetSubsystem<UQuartzSubsystem>()) Quartz->DeleteClockByName(this, OldName);
        Clock = nullptr;
    }
    CommandProxy = nullptr;
}

void UBootRhythmComponent::OnMusicCommand(EQuartzCommandDelegateSubType EventType, FName ClockName)
{
    if (ClockName != ActiveClockName || ActiveClockName.IsNone()) return;
    if (EventType == EQuartzCommandDelegateSubType::CommandOnStarted)
    {
        bReady = true;
        UE_LOG(LogTemp, Display, TEXT("BOOTLOOP RHYTHM: audio started %s"), *ClockName.ToString());
    }
    else if (EventType == EQuartzCommandDelegateSubType::CommandOnFailedToQueue)
    {
        UE_LOG(LogTemp, Warning, TEXT("BOOTLOOP RHYTHM: music could not queue"));
        StopRun();
    }
}

void UBootRhythmComponent::OnBeat(FName ClockName, EQuartzCommandQuantization Quantization, int32 Bars, int32 Beat, float BeatFraction)
{
    if (ClockName != ActiveClockName || ActiveClockName.IsNone() || bRunPaused) return;
    LastBeatTime = FPlatformTime::Seconds();
    FadePulse();
    GetWorld()->GetTimerManager().SetTimer(PulseTimer, this, &UBootRhythmComponent::FadePulse, .025f, true);
    if (++BeatCount <= 4 || BeatCount % 16 == 0)
        UE_LOG(LogTemp, Display, TEXT("BOOTLOOP RHYTHM: beat %d (%d:%d), transport %.3f, playing=%d"), BeatCount, Bars, Beat, Clock ? Clock->GetEstimatedRunTime(this) : -1.f, Music && Music->IsPlaying());
}

float UBootRhythmComponent::GetPulse() const
{
    if(ExternalTransport.IsValid())return bEnablePulse&&!bRunPaused?FBootBeatJudge::Pulse(ExternalTransport->Snapshot()):0.f;
    return bEnablePulse && !bRunPaused ? FMath::Clamp(1.f - static_cast<float>((FPlatformTime::Seconds() - LastBeatTime) / .18), 0.f, 1.f) : 0.f;
}

float UBootRhythmComponent::GetFeedbackAge() const
{
    return static_cast<float>(FPlatformTime::Seconds() - LastFeedbackTime);
}

double UBootRhythmComponent::GetTransportSeconds() const
{
    if(ExternalTransport.IsValid()){const auto S=ExternalTransport->Snapshot();return S.bValid?S.LocalSeconds:-1.;}
    return Clock ? Clock->GetEstimatedRunTime(this) : -1.;
}
bool UBootRhythmComponent::IsReady() const
{return ExternalTransport.IsValid()?ExternalTransport->Snapshot().bValid:bReady;}
void UBootRhythmComponent::SetMusicTransport(UBootMusicTransportComponent* Transport)
{
    StopRun();ExternalTransport=Transport;
    if(Transport)GetWorld()->GetTimerManager().SetTimer(PulseTimer,this,&UBootRhythmComponent::FadePulse,.025f,true);
}
void UBootRhythmComponent::ResetScoring()
{Score=FBootRhythmScore{};LastFeedbackTime=-100.;BeatCount=0;}
void UBootRhythmComponent::ShowSurfFeedback(EBootBeatRating Rating,int64 Points,int32 Combo)
{
    Score.LastRating=Rating;Score.LastPoints=int32(FMath::Clamp<int64>(Points,0,MAX_int32));Score.Combo=FMath::Max(0,Combo);
    LastFeedbackTime=FPlatformTime::Seconds();
    if(auto* GI=GetWorld()->GetGameInstance<UBootGameInstance>())GI->Sound(Rating==EBootBeatRating::Perfect?EBootSound::Perfect:EBootSound::Good);
}

void UBootRhythmComponent::FadePulse()
{
    const float Pulse = GetPulse();
    if (PulseMaterial) PulseMaterial->SetVectorParameterValue(TEXT("FlatColor"), FMath::Lerp(BaseColor, FLinearColor(.10f, .22f, .65f), Pulse * .55f));
    if (auto* Game = GetWorld()->GetAuthGameMode<ABootGameMode>()) if (Game->GetTrack()) Game->GetTrack()->Pulse(Pulse);
    if (Pulse <= 0.f && !ExternalTransport.IsValid()) GetWorld()->GetTimerManager().ClearTimer(PulseTimer);
}

void UBootRhythmComponent::ScoreAction(EBootRhythmAction Action)
{
    // Kept as a compatibility hook for the earlier prototype. Locomotion has no score.
}

EBootBeatRating UBootRhythmComponent::CaptureSnap() const
{
    if(ExternalTransport.IsValid())return !bRunPaused?FBootBeatJudge::Rate(ExternalTransport->Snapshot(),FMath::Clamp(CVarBootInputOffset.GetValueOnGameThread(),-250.f,250.f)):EBootBeatRating::OffBeat;
    return bReady && Clock && !bRunPaused ? FBootRhythmScore::Rate(GetTransportSeconds(),
        FMath::Clamp(CVarBootInputOffset.GetValueOnGameThread(), -250.f, 250.f)) : EBootBeatRating::OffBeat;
}

bool UBootRhythmComponent::CompleteSurf(int32 WindowId, EBootBeatRating InputRating)
{
    if(ExternalTransport.IsValid())return false;
    if (bRunPaused || !Score.AwardSurf(WindowId, InputRating)) return false;
    if (auto* GI = Cast<UBootGameInstance>(GetWorld()->GetGameInstance()))
        GI->Sound(InputRating == EBootBeatRating::Perfect ? EBootSound::Perfect : EBootSound::Good);
    LastFeedbackTime = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Display, TEXT("BOOTLOOP SURF: window=%d inputRating=%d +%d score=%d"), WindowId, int32(InputRating), Score.LastPoints, Score.Score);
    return true;
}

void UBootRhythmComponent::SetRunPaused(bool bPause)
{
    bRunPaused = bPause;
    if (Music) Music->SetPaused(bPause);
    if (Clock)
    {
        UQuartzClockHandle* Handle = Clock;
        if (bPause) Handle->PauseClock(this, Handle); else Handle->ResumeClock(this, Handle);
    }
    if (bPause) { LastBeatTime = -100.; FadePulse(); }
}

void UBootRhythmComponent::ApplyPreferences(const FBootOptions& Options)
{
    bEnablePulse = Options.bPulse;
    MusicVolume = Options.Music;
    CVarBootInputOffset->Set(Options.InputOffsetMs, ECVF_SetByGameSetting);
    if (Music) Music->SetVolumeMultiplier(MusicVolume * .65f);
    if(ExternalTransport.IsValid())ExternalTransport->SetVolumeMultiplier(MusicVolume);
    if (!bEnablePulse) { LastBeatTime = -100.; FadePulse(); }
}

void UBootRhythmComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopRun();
    if (Music) Music->DestroyComponent();
    Super::EndPlay(Reason);
}
