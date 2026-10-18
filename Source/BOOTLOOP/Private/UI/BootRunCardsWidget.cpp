#include "UI/BootRunCardsWidget.h"
#include "Core/BootGameInstance.h"
#include "Input/BootInputPreferences.h"
#include "Run/BootGameMode.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"
#include "Layout/Clipping.h"
#include "TimerManager.h"
#include "Widgets/SWidget.h"

namespace
{
    FLinearColor Color(uint32 Hex,float Alpha=1.f)
    { FLinearColor C(FColor((Hex>>16)&255,(Hex>>8)&255,Hex&255));C.A=Alpha;return C; }
    const FLinearColor Ink=Color(0x14252e),Paper=Color(0xeef2dc),Gray=Color(0xcbd2cd),
        Navy=Color(0x192d73),Mint=Color(0x64c9a7),Amber=Color(0xf5cd60);
    struct FCardCopy { const TCHAR* Effect; const TCHAR* Risk; const TCHAR* Reward; };
    FCardCopy Describe(EBootCardEffect Effect)
    {
        switch(Effect)
        {
        case EBootCardEffect::AirCache:return {TEXT("+1 ALT-TAB CHARGE"),TEXT("UP TO 3 CHARGES"),TEXT("NO ADDED RISK")};
        case EBootCardEffect::BufferDash:return {TEXT("+1 BUFFER DASH CHARGE"),TEXT("UP TO 2 CHARGES"),TEXT("NO ADDED RISK")};
        case EBootCardEffect::PulseGuard:return {TEXT("+1 PROCESS GUARD CHARGE"),TEXT("SURF TO RECHARGE"),TEXT("NO ADDED RISK")};
        case EBootCardEffect::WallCache:return {TEXT("+0.20 SEC WALLRUN"),TEXT("UP TO 2.4 SECONDS"),TEXT("NO ADDED RISK")};
        case EBootCardEffect::AeroBuffer:return {TEXT("+0.10 AIR CONTROL"),TEXT("UP TO 0.85 CONTROL"),TEXT("NO ADDED RISK")};
        case EBootCardEffect::ServoReserve:return {TEXT("+0.20 SEC OVERCLOCK"),TEXT("DEPARTURE BOOST WINDOW"),TEXT("NO ADDED RISK")};
        case EBootCardEffect::SlideBuffer:return {TEXT("-15% SLIDE BRAKING"),TEXT("KEEP MORE MOMENTUM"),TEXT("NO ADDED RISK")};
        case EBootCardEffect::SoftLanding:return {TEXT("+0.02 SEC HOP GRACE"),TEXT("FRESH MANUAL INPUT ONLY"),TEXT("NO ADDED RISK")};
        case EBootCardEffect::PriorityEscalation:return {TEXT("+1 PROCESS BUDGET"),TEXT("MORE HOSTILE PROCESSES"),TEXT("+20% OPERATION REWARD")};
        case EBootCardEffect::HotLoop:return {TEXT("+1 COMBINATION BUDGET"),TEXT("MORE COMBINED HAZARDS"),TEXT("+15% DISTANCE REWARD")};
        case EBootCardEffect::ThinCache:return {TEXT("-10% FUTURE ROUTE WIDTH"),TEXT("NARROWER LANDINGS"),TEXT("+20% DISTANCE REWARD")};
        case EBootCardEffect::UnsafeWindows:return {TEXT("+20% RISKY SURF WEIGHT"),TEXT("MORE DANGEROUS WINDOWS"),TEXT("+25% OPERATION REWARD")};
        }
        return {TEXT("RUN UPGRADE"),TEXT(""),TEXT("")};
    }
}

void UBootRunCardsWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();SetIsFocusable(false);ForceVolatile(true);
    SetVisibility(ESlateVisibility::Collapsed);
}

void UBootRunCardsWidget::SetActive(bool Value)
{
    bActive=Value;
    if(auto* W=GetWorld())W->GetTimerManager().ClearTimer(RefreshTimer);
    SetVisibility(Value?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(Value)
    {
        Refresh();
        if(auto* W=GetWorld())W->GetTimerManager().SetTimer(RefreshTimer,this,&UBootRunCardsWidget::Refresh,.15f,true);
    }
    else {View={};KeyLabels.Reset();}
}

void UBootRunCardsWidget::NativeDestruct()
{ SetActive(false);Super::NativeDestruct(); }

void UBootRunCardsWidget::Refresh()
{
    if(!bActive)return;
    const auto* Game=GetWorld()?GetWorld()->GetAuthGameMode<ABootGameMode>():nullptr;
    bTrial=Game && Game->IsFirstRun();
    View=Game && (Game->IsMusicRun() || Game->IsFirstRun())?Game->GetRunSnapshot():FBootRunSnapshot{};
    const auto* GI=GetGameInstance<UBootGameInstance>();
    const FBootInputPreferences Inputs=GI && GI->GetProfile()?GI->GetInputPreferences():FBootInputPreferences();
    KeyLabels={Inputs.Label(EBootInputAction::CardOne).ToString(),Inputs.Label(EBootInputAction::CardTwo).ToString(),Inputs.Label(EBootInputAction::CardThree).ToString()};
    if(const auto Cached=GetCachedWidget())Cached->Invalidate(EInvalidateWidgetReason::Paint);
}

int32 UBootRunCardsWidget::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Culling,
    FSlateWindowElementList& Elements,int32 LayerId,const FWidgetStyle& Style,bool bEnabled) const
{
    if(!bActive || !View.bRunning || View.bPaused)return LayerId;
    const FVector2D Size=G.GetLocalSize();const float Scale=FMath::Min(Size.X/1280.,Size.Y/720.);
    const FVector2D Origin=(Size-FVector2D(1280,720)*Scale)*.5;int32 Layer=LayerId;
    auto Geometry=[&](float X,float Y,float W,float H){return G.ToPaintGeometry(FVector2D(W,H),FSlateLayoutTransform(Scale,Origin+FVector2D(X,Y)*Scale));};
    auto Rect=[&](float X,float Y,float W,float H,FLinearColor C)
    {FSlateDrawElement::MakeBox(Elements,++Layer,Geometry(X,Y,W,H),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,C);};
    auto Text=[&](const FString& Value,float X,float Y,int32 FontSize,FLinearColor C)
    {FSlateDrawElement::MakeText(Elements,++Layer,Geometry(X,Y,720,30),Value,FSlateFontInfo(FPaths::ProjectContentDir()/TEXT("Slate/BootPixel.ttf"),FontSize,EFontHinting::Monochrome),ESlateDrawEffect::None,C);};
    if(!View.CardOffer.IsSet())
    {
        if(View.bPendingMutation)
        {
            Rect(366,174,548,34,Color(0x14252e,.94f));Rect(366,174,3,34,Amber);
            Text(TEXT("UPGRADE QUEUED / APPLIES AT A SAFE BOUNDARY"),382,186,10,Amber);
        }
        return Layer;
    }
    const auto& Offer=View.CardOffer.GetValue();
    if(Offer.CardIds.Num()!=3)return Layer;
    Rect(278,170,724,28,Color(0x14252e,.94f));
    Text(TEXT("CHOOSE ON THE MOVE / NO TIMER"),290,180,10,Mint);
    Text(bTrial?TEXT("UNRANKED / TRIAL LOADOUT"):FString::Printf(TEXT("REWARDS  DIST X%.2f / SURF X%.2f"),View.RewardModifiers.DistanceMultiplier,View.RewardModifiers.OperationMultiplier),652,181,8,Gray);
    const auto Catalog=FBootRunCardRules::Catalog();
    for(int32 I=0;I<3;++I)
    {
        const FBootCardDefinition* Definition=nullptr;
        for(const auto& Candidate:Catalog)if(Candidate.Id==Offer.CardIds[I]){Definition=&Candidate;break;}
        if(!Definition)continue;
        const float X=278+I*244,Y=204,W=236,H=132;
        const bool Risk=Definition->Kind==EBootCardKind::Risk;const auto Accent=Risk?Amber:Mint;
        Rect(X+3,Y+4,W,H,Color(0x000000,.22f));Rect(X,Y,W,H,Ink);
        Rect(X+2,Y+2,W-4,H-4,Color(0x14252e,.95f));Rect(X+2,Y+2,W-4,2,Accent);
        Rect(X+6,Y+7,W-12,23,Accent);
        Elements.PushClip(FSlateClippingZone(Geometry(X+10,Y+8,W-20,22)));
        Text(TEXT("[")+(KeyLabels.IsValidIndex(I)?KeyLabels[I]:FString::FromInt(I+1))+TEXT("] SELECT"),X+12,Y+15,9,Ink);
        Elements.PopClip();
        Elements.PushClip(FSlateClippingZone(Geometry(X+10,Y+33,W-20,22)));
        Text(Definition->Id.ToString(),X+12,Y+38,10,Paper);
        Elements.PopClip();
        Text(FString::Printf(TEXT("%s  /  STACK %d OF %d"),Risk?TEXT("RISK"):TEXT("BUFF"),View.RewardModifiers.Stacks.FindRef(Definition->Id)+1,Definition->MaxStacks),X+12,Y+57,8,Accent);
        const auto Copy=Describe(Definition->Effect);
        Text(Copy.Effect,X+12,Y+76,9,Paper);
        Text(Copy.Risk,X+12,Y+94,8,Gray);
        Text(Copy.Reward,X+12,Y+114,8,Accent);
    }
    return Layer;
}
