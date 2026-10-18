#include "UI/BootPrototypeHUD.h"
#include "Player/BootRunner.h"
#include "Music/BootRhythmComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"

void ABootPrototypeHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !PlayerOwner) return;
    const ABootRunner* Runner = Cast<ABootRunner>(PlayerOwner->GetPawn());
    const float Width = Canvas->SizeX;
    const float Height = Canvas->SizeY;
    const float Scale = FMath::Clamp(Width / 1280.f, .65f, 2.f);
    const UBootRhythmComponent* Rhythm = Runner ? Runner->GetRhythm() : nullptr;
    const float Pulse = Rhythm ? Rhythm->GetPulse() : 0.f;
    const FLinearColor Navy = FMath::Lerp(FLinearColor(0, 0, .22f), FLinearColor(.10f, .22f, .65f), Pulse * .55f);
    DrawRect(Navy, 12, 12, Width - 24, 32 * Scale);
    DrawText(TEXT("BOOTLOOP.EXE / 130 BPM"), FLinearColor::White, 24, 17, GEngine->GetSmallFont(), Scale);
    if (Rhythm)
    {
        const FBootRhythmScore& Score = Rhythm->GetScore();
        const FString Status = FString::Printf(TEXT("%06d pts   COMBO %d   %s"), Score.Score, Score.Combo,
            Rhythm->IsReady() ? TEXT("SYNC") : (Runner->IsDead() ? TEXT("STOPPED") : TEXT("AUDIO WAIT")));
        DrawText(Status, FLinearColor::White, Width - 370 * Scale, 17, GEngine->GetSmallFont(), Scale);
        DrawRect(FLinearColor(.08f, .08f, .10f, .8f), Width / 2 - 70 * Scale, Height - 82 * Scale, 140 * Scale, 14 * Scale);
        DrawRect(FLinearColor(.2f + Pulse * .8f, .65f + Pulse * .35f, .65f), Width / 2 - 66 * Scale,
            Height - 79 * Scale, 132 * Scale * Pulse, 8 * Scale);
        if (Rhythm->GetFeedbackAge() < .8f && !Runner->IsDead())
        {
            const TCHAR* Rating = Score.LastRating == EBootBeatRating::Perfect ? TEXT("PERFECT")
                : Score.LastRating == EBootBeatRating::Good ? TEXT("GOOD") : TEXT("OFF BEAT");
            DrawText(FString::Printf(TEXT("%s  +%d"), Rating, Score.LastPoints), FLinearColor(1, .85f, .2f),
                Width / 2 - 60 * Scale, Height * .58f, GEngine->GetSmallFont(), Scale);
        }
    }
    DrawRect(FLinearColor(.12f, .12f, .12f, .9f), 12, Height - 42 * Scale, Width - 24, 30 * Scale);
    DrawText(TEXT("WASD: MOVE   SPACE / WHEEL: JUMP   CTRL / C: SLIDE + AIR DUCK   R: RETRY   ESC: MOUSE"),
        FLinearColor::White, 24, Height - 36 * Scale, GEngine->GetSmallFont(), Scale);
    if (!Runner) return;
    if (Runner->IsDead())
    {
        const float BoxWidth = FMath::Min(540.f * Scale, Width - 32);
        const float X = (Width - BoxWidth) / 2;
        const float Y = Height * .35f;
        DrawRect(FLinearColor(.015f, .015f, .015f), X - 3, Y - 3, BoxWidth + 6, 150 * Scale + 6);
        DrawRect(FLinearColor(.75f, .75f, .75f), X, Y, BoxWidth, 150 * Scale);
        DrawRect(Navy, X + 4, Y + 4, BoxWidth - 8, 32 * Scale);
        DrawText(TEXT("BOOT/OS 95 - SYSTEM ERROR"), FLinearColor::White, X + 16, Y + 9, GEngine->GetSmallFont(), Scale);
        DrawText(TEXT("PROCESS TERMINATED"), FLinearColor(.7f, .03f, .03f), X + 20, Y + 54 * Scale, GEngine->GetMediumFont(), Scale);
        DrawText(TEXT("Press R to restart the process."), FLinearColor::Black, X + 20, Y + 110 * Scale, GEngine->GetSmallFont(), Scale);
    }
    else
    {
        DrawRect(FLinearColor::White, Width / 2 - 2, Height / 2 - 2, 4, 4);
        if (Runner->IsSliding() || Runner->bIsCrouched)
            DrawText(Runner->IsSliding() ? TEXT("SLIDING") : TEXT("LOW CLEARANCE"),
                FLinearColor(1, .8f, .2f), 24, Height - 78 * Scale, GEngine->GetSmallFont(), Scale);
        if (PlayerOwner->bShowMouseCursor)
            DrawText(TEXT("CLICK TO CAPTURE MOUSE"), FLinearColor::White, Width * .36f, Height * .65f, GEngine->GetSmallFont(), Scale);
    }
}
