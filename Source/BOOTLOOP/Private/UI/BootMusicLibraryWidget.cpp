#include "UI/BootMusicLibraryWidget.h"
#include "Core/BootGameInstance.h"
#include "Music/BootMusicPicker.h"
#include "Music/BootMusicTransportComponent.h"
#include "Player/BootRunner.h"
#include "Run/BootGameMode.h"
#include "UI/BootPlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Layout/Clipping.h"
#include "String/LexFromString.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "Widgets/SWidget.h"

namespace
{
    constexpr int32 VisibleRows = 7;
    FLinearColor Color(uint32 Hex, float Alpha = 1.f)
    { FLinearColor C(FColor((Hex >> 16) & 255, (Hex >> 8) & 255, Hex & 255)); C.A = Alpha; return C; }
    const FLinearColor Ink=Color(0x14252e), Paper=Color(0xeef2dc), Gray=Color(0xcbd2cd),
        Navy=Color(0x192d73), Mint=Color(0x64c9a7), Amber=Color(0xf5cd60), Red=Color(0xdc4141);
    bool Pending(const FBootLibraryEntry& E)
    { return E.State==EBootLibraryState::Queued || E.State==EBootLibraryState::Decoding || E.State==EBootLibraryState::Analyzing; }
    FString StateLabel(const FBootLibraryEntry& E)
    {
        switch(E.State)
        {
        case EBootLibraryState::Queued:return TEXT("QUEUED");
        case EBootLibraryState::Decoding:return TEXT("DECODING");
        case EBootLibraryState::Analyzing:return TEXT("ANALYZING");
        case EBootLibraryState::Ready:return TEXT("READY");
        case EBootLibraryState::NeedsReview:return TEXT("CHECK GRID");
        case EBootLibraryState::Failed:return TEXT("FAILED");
        }
        return TEXT("UNKNOWN");
    }
    FString Duration(double Seconds)
    { const int32 S=FMath::Max(0,FMath::FloorToInt(Seconds));return FString::Printf(TEXT("%d:%02d"),S/60,S%60); }
}

void UBootMusicLibraryWidget::NativeOnInitialized()
{ Super::NativeOnInitialized(); SetIsFocusable(true); ForceVolatile(true); SetVisibility(ESlateVisibility::Collapsed); }

UBootMusicLibrarySubsystem* UBootMusicLibraryWidget::Library() const
{ const auto* GI=GetGameInstance(); return GI?GI->GetSubsystem<UBootMusicLibrarySubsystem>():nullptr; }

UBootMusicTransportComponent* UBootMusicLibraryWidget::Transport() const
{
    const auto* PC=GetOwningPlayer();
    const auto* Runner=PC?Cast<ABootRunner>(PC->GetPawn()):nullptr;
    return Runner?Runner->GetMusicTransport():nullptr;
}

void UBootMusicLibraryWidget::SetActive(bool Value)
{
    bActive=Value;
    FTSTicker::RemoveTicker(RefreshTicker);RefreshTicker.Reset();
    if(!Value)
    {
        EditField=0; EditText.Reset(); StopPreview(); SetVisibility(ESlateVisibility::Collapsed);return;
    }
    SetVisibility(ESlateVisibility::Visible); Refresh(); SetUserFocus(GetOwningPlayer());
    RefreshTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UBootMusicLibraryWidget::RefreshRealtime),.15f);
}
bool UBootMusicLibraryWidget::RefreshRealtime(float)
{ if(!bActive)return false;Refresh();return bActive; }

void UBootMusicLibraryWidget::NativeDestruct()
{
    SetActive(false); Items.Reset(); Areas.Reset(); Super::NativeDestruct();
}

const FBootLibraryEntry* UBootMusicLibraryWidget::Selected() const
{ return Items.FindByPredicate([this](const FBootLibraryEntry& E){return E.Hash==SelectedHash;}); }

void UBootMusicLibraryWidget::Refresh()
{
    if(!bActive)return;
    if(!IsVisible()) { SetActive(false); return; }
    const auto* FocusArea=Areas.FindByPredicate([this](const FBootLibraryHitArea& A){return A.Id==FocusId;});
    const FString FocusHash=FocusArea?FocusArea->Hash:FString();
    const bool bCheckboxFocus=FocusId>=2000;
    const int32 PreviousIndex=Items.IndexOfByPredicate([this](const FBootLibraryEntry& E){return E.Hash==SelectedHash;});
    if(auto* L=Library()) Items=L->Snapshot(); else Items.Reset();
    IncludedReady=0;
    for(const auto& E:Items)if(E.bIncluded && E.State==EBootLibraryState::Ready)++IncludedReady;
    if(!Selected())
    {
        EditField=0; EditText.Reset();
        SelectedHash=Items.IsEmpty()?FString():Items[FMath::Clamp(PreviousIndex,0,Items.Num()-1)].Hash;
    }
    if(!FocusHash.IsEmpty())
    {
        const int32 FocusIndex=Items.IndexOfByPredicate([&](const FBootLibraryEntry& E){return E.Hash==FocusHash;});
        FocusId=FocusIndex==INDEX_NONE?1:(bCheckboxFocus?2000:1000)+FocusIndex;
    }
    ScrollRow=FMath::Clamp(ScrollRow,0,FMath::Max(0,Items.Num()-VisibleRows));
    if(auto* T=Transport())
        if(const auto* GI=GetGameInstance<UBootGameInstance>();GI && GI->GetProfile())
            T->SetVolumeMultiplier(GI->GetOptions().Music);
    if(const auto Cached=GetCachedWidget())Cached->Invalidate(EInvalidateWidgetReason::Paint);
}

void UBootMusicLibraryWidget::StopPreview()
{ if(auto* T=Transport())T->StopPreview(); PreviewHash.Reset(); }

void UBootMusicLibraryWidget::Select(int32 Index)
{
    if(!Items.IsValidIndex(Index) || !CommitEdit())return;
    SelectedHash=Items[Index].Hash; FocusId=1000+Index;
    if(Index<ScrollRow)ScrollRow=Index;
    if(Index>=ScrollRow+VisibleRows)ScrollRow=Index-VisibleRows+1;
}

void UBootMusicLibraryWidget::Scroll(int32 Delta)
{
    if(!CommitEdit())return;
    ScrollRow=FMath::Clamp(ScrollRow+Delta,0,FMath::Max(0,Items.Num()-VisibleRows));
    Mouse=FVector2D(-1,-1);
    if(FocusId>=1000)FocusId=1000+ScrollRow;
}

void UBootMusicLibraryWidget::BeginEdit(int32 Field)
{
    if(!CommitEdit())return;
    const auto* E=Selected();if(!E)return;
    if(Field!=10 && !E->Analysis)return;
    EditField=Field;FocusId=Field;bReplaceEdit=true;
    EditText=Field==10?E->DisplayName:Field==11?FString::Printf(TEXT("%.3f"),E->Grid.Bpm):FString::Printf(TEXT("%.4f"),E->Grid.FirstBeatSeconds);
    Message=TEXT("TYPE TO REPLACE / ENTER TO SAVE / ESC TO CANCEL");
}

bool UBootMusicLibraryWidget::CommitEdit()
{
    if(!EditField)return true;
    auto* L=Library();const auto* E=Selected();if(!L || !E){EditField=0;return true;}
    FText Error;bool bSaved=false;
    if(EditField==10)
    {
        bSaved=L->SetDisplayName(SelectedHash,EditText.TrimStartAndEnd());
        if(!bSaved) Error=FText::FromString(TEXT("Enter a non-empty track name."));
    }
    else
    {
        double Value=0.;
        if(LexTryParseString(Value,*EditText) && FMath::IsFinite(Value))
            bSaved=L->SetGridOverride(SelectedHash,EditField==11?Value:E->Grid.Bpm,EditField==12?Value:E->Grid.FirstBeatSeconds,Error);
        else Error=FText::FromString(TEXT("Enter a valid number."));
    }
    if(!bSaved){Message=Error.IsEmpty()?TEXT("Could not save this edit."):Error.ToString();return false;}
    EditField=0;EditText.Reset();Message=TEXT("SAVED / GRID CHANGES APPLY ON NEXT PLAYBACK");Refresh();return true;
}

void UBootMusicLibraryWidget::ApplyGrid(double Bpm,double FirstBeat)
{
    if(auto* L=Library())
    {
        FText Error;
        Message=L->SetGridOverride(SelectedHash,Bpm,FirstBeat,Error)?TEXT("GRID SAVED / NEXT PLAYBACK ONLY"):Error.ToString();
        Refresh();
    }
}

void UBootMusicLibraryWidget::Activate(int32 Id,const FString& Hash)
{
    if(Id>=1000)
    {
        if(!CommitEdit())return;
        const FString TargetHash=Hash.IsEmpty()?SelectedHash:Hash;
        const int32 Index=Items.IndexOfByPredicate([&](const FBootLibraryEntry& E){return E.Hash==TargetHash;});
        if(Index==INDEX_NONE)return;
        Select(Index);
        if(Id>=2000 && SelectedHash==TargetHash)
            if(auto* L=Library())L->SetIncluded(TargetHash,!Items[Index].bIncluded);
        Refresh();return;
    }
    if(Id==10 || Id==11 || Id==12){BeginEdit(Id);return;}
    if(Id==4)
    {
        // Back/Escape may always leave even if a draft number is invalid.
        EditField=0;StopPreview();if(auto* PC=Cast<ABootPlayerController>(GetOwningPlayer()))PC->Back();return;
    }
    if(!CommitEdit())return;
    auto* L=Library(); const auto* E=Selected();auto* T=Transport();
    if(Id==1 && L)
    {
        StopPreview();
        const void* Parent=FSlateApplication::Get().FindBestParentWindowHandleForDialogs(TakeWidget());
        const auto Files=FBootMusicPicker::PickFiles(const_cast<void*>(Parent));
        if(!Files.IsEmpty()) Message=L->ImportFiles(Files)?TEXT("IMPORT QUEUED / SELECT A TRACK TO CANCEL"):TEXT("IMPORT NOT QUEUED / MAX 32 FILES PER REQUEST");
        SetUserFocus(GetOwningPlayer());
    }
    else if(Id==2 && E && T)
    {
        FText Error; if(T->StartPreview(E->Hash,Error))
        {
            if(const auto* GI=GetGameInstance<UBootGameInstance>())T->SetVolumeMultiplier(GI->GetOptions().Music);
            PreviewHash=E->Hash;Message=TEXT("PREVIEW PLAYING");
        }
        else Message=Error.ToString();
    }
    else if(Id==3){StopPreview();Message=TEXT("PREVIEW STOPPED");}
    else if(Id==5)
    {
        if(!Items.ContainsByPredicate([](const FBootLibraryEntry& Item){return Item.bIncluded && Item.State==EBootLibraryState::Ready;}))
            Message=TEXT("INCLUDE AT LEAST ONE READY TRACK TO START");
        else {StopPreview();if(auto* PC=Cast<ABootPlayerController>(GetOwningPlayer()))PC->StartMusicRun();}
    }
    else if(Id==6) Scroll(-VisibleRows);
    else if(Id==7) Scroll(VisibleRows);
    else if(Id==13 && L && E)
    {
        if(!Pending(*E) && E->bBuiltIn){Message=TEXT("STARTER TRACKS STAY IN THE LIBRARY / UNCHECK TO EXCLUDE");return;}
        FText Error;
        Message=L->Remove(E->Hash,Error)?TEXT("LIBRARY UPDATED / YOUR ORIGINAL FILE IS KEPT"):Error.ToString();
    }
    else if(Id==14 && L && E)
    {
        FText Error;Message=L->AcceptSafeGrid(E->Hash,Error)?TEXT("SAFE GRID ACCEPTED / NEXT PLAYBACK ONLY"):Error.ToString();
    }
    else if(E && E->Analysis)
    {
        const double B=E->Grid.Bpm, F=E->Grid.FirstBeatSeconds;
        switch(Id)
        {
        case 20:ApplyGrid(B-1.,F);break;case 21:ApplyGrid(B+1.,F);break;
        case 22:ApplyGrid(B*.5,F);break;case 23:ApplyGrid(B*2.,F);break;
        case 24:ApplyGrid(B,F-.01);break;case 25:ApplyGrid(B,F+.01);break;
        }
    }
    Refresh();
}

FVector2D UBootMusicLibraryWidget::LogicalPoint(const FGeometry& G,FVector2D Absolute) const
{ return (G.AbsoluteToLocal(Absolute)-DrawOrigin)/FMath::Max(DrawScale,.001f); }

FReply UBootMusicLibraryWidget::NativeOnMouseMove(const FGeometry& G,const FPointerEvent& Event)
{ Mouse=LogicalPoint(G,Event.GetScreenSpacePosition());return FReply::Handled(); }

FReply UBootMusicLibraryWidget::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& Event)
{
    if(Event.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
    Mouse=LogicalPoint(G,Event.GetScreenSpacePosition());
    for(const auto& Area:Areas)if(Area.Rect.ContainsPoint(Mouse))
    {
        const auto Copy=Area;FocusId=Copy.Id;Activate(Copy.Id,Copy.Hash);
        return bActive?FReply::Handled().SetUserFocus(TakeWidget()):FReply::Handled();
    }
    return FReply::Handled().SetUserFocus(TakeWidget());
}

FReply UBootMusicLibraryWidget::NativeOnMouseWheel(const FGeometry& G,const FPointerEvent& Event)
{ Scroll(Event.GetWheelDelta()<0?3:-3);return FReply::Handled(); }

FReply UBootMusicLibraryWidget::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& Event)
{
    const FKey K=Event.GetKey();Mouse=FVector2D(-1,-1);
    if(EditField)
    {
        if(K==EKeys::Escape){EditField=0;EditText.Reset();Message=TEXT("EDIT CANCELLED");return FReply::Handled();}
        if(K==EKeys::Enter){CommitEdit();return FReply::Handled();}
        if(K==EKeys::BackSpace){if(bReplaceEdit)EditText.Reset();else if(!EditText.IsEmpty())EditText.LeftChopInline(1);bReplaceEdit=false;return FReply::Handled();}
        if(K!=EKeys::Tab)return FReply::Handled();
        if(!CommitEdit())return FReply::Handled();
    }
    if(K==EKeys::Escape){if(!Event.IsRepeat())Activate(4);return FReply::Handled();}
    if(K==EKeys::PageUp || K==EKeys::PageDown){Scroll(K==EKeys::PageUp?-VisibleRows:VisibleRows);return FReply::Handled();}
    if(K==EKeys::Up || K==EKeys::Down || K==EKeys::Home || K==EKeys::End)
    {
        const int32 Index=Items.IndexOfByPredicate([this](const FBootLibraryEntry& E){return E.Hash==SelectedHash;});
        Select(K==EKeys::Home?0:K==EKeys::End?Items.Num()-1:FMath::Clamp(Index+(K==EKeys::Down?1:-1),0,Items.Num()-1));
        return FReply::Handled();
    }
    if(K==EKeys::Tab && !Areas.IsEmpty())
    {
        const int32 I=Areas.IndexOfByPredicate([this](const FBootLibraryHitArea& A){return A.Id==FocusId;});
        FocusId=Areas[(FMath::Max(0,I)+(Event.IsShiftDown()?-1:1)+Areas.Num())%Areas.Num()].Id;
        return FReply::Handled();
    }
    if((K==EKeys::Enter || K==EKeys::SpaceBar) && !Event.IsRepeat())
    {
        if(const auto* A=Areas.FindByPredicate([this](const FBootLibraryHitArea& Area){return Area.Id==FocusId;}))
        {const auto Copy=*A;Activate(Copy.Id,Copy.Hash);}
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(G,Event);
}

FReply UBootMusicLibraryWidget::NativeOnKeyChar(const FGeometry& G,const FCharacterEvent& Event)
{
    if(!EditField)return Super::NativeOnKeyChar(G,Event);
    const TCHAR C=Event.GetCharacter();
    const bool Allowed=EditField==10?(C>=32 && C<127):(FChar::IsDigit(C) || C==TEXT('.') || C==TEXT('-'));
    if(Allowed)
    {
        if(bReplaceEdit){EditText.Reset();bReplaceEdit=false;}
        if(EditText.Len()<(EditField==10?80:16))EditText.AppendChar(C);
    }
    return FReply::Handled();
}

void UBootMusicLibraryWidget::NativeOnFocusLost(const FFocusEvent& Event)
{ EditField=0;EditText.Reset();Super::NativeOnFocusLost(Event); }

int32 UBootMusicLibraryWidget::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Culling,
    FSlateWindowElementList& Elements,int32 LayerId,const FWidgetStyle& Style,bool bEnabled) const
{
    if(!bActive)return LayerId;
    const FVector2D Size=G.GetLocalSize();DrawScale=FMath::Min(Size.X/1280.,Size.Y/720.);
    DrawOrigin=(Size-FVector2D(1280,720)*DrawScale)*.5;Areas.Reset();int32 Layer=LayerId;
    auto Geometry=[&](float X,float Y,float W,float H){return G.ToPaintGeometry(FVector2D(W,H),FSlateLayoutTransform(DrawScale,DrawOrigin+FVector2D(X,Y)*DrawScale));};
    auto Rect=[&](float X,float Y,float W,float H,FLinearColor C)
    {FSlateDrawElement::MakeBox(Elements,++Layer,Geometry(X,Y,W,H),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,C);};
    auto Text=[&](FString Value,float X,float Y,int32 FontSize,FLinearColor C,float Width=1100.f)
    {
        const FSlateFontInfo Font(FPaths::ProjectContentDir()/TEXT("Slate/BootPixel.ttf"),FontSize,EFontHinting::Monochrome);
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        if(Measure->Measure(Value,Font).X>Width)
        {
            while(Value.Len()>1 && Measure->Measure(Value+TEXT("..."),Font).X>Width)Value.LeftChopInline(1);
            Value+=TEXT("...");
        }
        FSlateDrawElement::MakeText(Elements,++Layer,Geometry(X,Y,Width,30),Value,Font,ESlateDrawEffect::None,C);
    };
    auto Button=[&](int32 Id,const FString& Label,float X,float Y,float W,float H,bool bPrimary=false,bool bAvailable=true)
    {
        const FSlateRect Bounds(X,Y,X+W,Y+H);const bool Hot=Bounds.ContainsPoint(Mouse)||(Mouse.X<0 && FocusId==Id);
        Rect(X,Y,W,H,Ink);Rect(X+2,Y+2,W-4,H-4,bAvailable?(bPrimary?Amber:Hot?Paper:Gray):Color(0x9caeaa));
        Rect(X+2,Y+2,W-4,2,Paper);Rect(X+2,Y+2,2,H-4,Paper);
        if(Hot && bAvailable)Rect(X+5,Y+6,3,H-12,Navy);
        Text(Label,X+12,Y+(H-18)*.5,12,bAvailable?Ink:Color(0x526763),W-22);
        if(bAvailable)Areas.Add({Bounds,Id,{}});
    };
    auto Field=[&](int32 Id,const FString& Value,const FString& Label,float X,float Y,float W,bool bAvailable=true)
    {
        Text(Label,X,Y,10,Ink,W);Rect(X,Y+20,W,32,Ink);Rect(X+2,Y+22,W-4,28,bAvailable?Paper:Gray);
        const bool Active=EditField==Id || (Mouse.X<0 && FocusId==Id);
        if(Active)Rect(X+3,Y+23,3,26,Navy);
        Text(EditField==Id?EditText+TEXT("_"):Value,X+11,Y+29,12,Ink,W-20);
        if(bAvailable)Areas.Add({FSlateRect(X,Y+20,X+W,Y+52),Id,{}});
    };
    FSlateDrawElement::MakeBox(Elements,++Layer,G.ToPaintGeometry(),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,Color(0x0a333f,.98f));
    for(int32 X=0;X<1280;X+=64)Rect(X,0,1,720,Color(0x64c9a7,.07f));
    for(int32 Y=0;Y<720;Y+=64)Rect(0,Y,1280,1,Color(0x64c9a7,.07f));
    Text(TEXT("BOOT/OS 95"),38,28,13,Mint);Text(TEXT("LOCAL AUDIO / UNRESTRICTED MOTION"),810,31,10,Gray,430);
    Rect(56,84,1184,588,Color(0x000000,.25f));Rect(48,76,1184,588,Ink);Rect(50,78,1180,584,Gray);
    Rect(50,78,1180,2,Paper);Rect(50,78,2,584,Paper);Rect(54,82,1172,36,Navy);
    Text(TEXT("MUSIC LIBRARY"),70,92,16,Paper);Text(TEXT("WAV + MP3 / ON YOUR MACHINE"),854,96,10,Paper,350);
    Button(1,TEXT("+ ADD MUSIC"),70,134,182,38,true);
    const auto* Entry=Selected();
    const bool Ready=Entry && Entry->State==EBootLibraryState::Ready;
    Button(2,TEXT("PREVIEW"),264,134,136,38,false,Ready);
    Button(3,TEXT("STOP"),410,134,100,38,false,!PreviewHash.IsEmpty());
    Text(FString::Printf(TEXT("%d TRACKS / %d READY IN RUN"),Items.Num(),IncludedReady),740,145,11,Ink,470);
    Rect(70,188,644,28,Ink);Text(TEXT("IN RUN"),82,197,9,Mint,70);Text(TEXT("TRACK"),162,197,9,Mint);
    Text(TEXT("TIME"),466,197,9,Mint);Text(TEXT("BPM"),528,197,9,Mint);Text(TEXT("STATUS"),595,197,9,Mint);
    Rect(70,216,644,322,Color(0xe1e6d3));
    Elements.PushClip(FSlateClippingZone(Geometry(70,216,644,322)));
    for(int32 Row=0;Row<VisibleRows;++Row)
    {
        const int32 I=ScrollRow+Row;if(!Items.IsValidIndex(I))break;
        const auto& E=Items[I];const float Y=216+Row*46;
        const bool Chosen=E.Hash==SelectedHash;
        Rect(71,Y+1,642,44,Chosen?Navy:Row%2?Color(0xd6ded1):Color(0xe1e6d3));
        const FLinearColor Fore=Chosen?Paper:Ink;
        const FSlateRect RowRect(148,Y+1,713,Y+45),CheckRect(80,Y+8,132,Y+37);
        if((FocusId==1000+I && Mouse.X<0) || RowRect.ContainsPoint(Mouse))Rect(147,Y+8,3,30,Chosen?Mint:Navy);
        Rect(84,Y+12,22,22,Ink);Rect(86,Y+14,18,18,Paper);
        if(E.bIncluded)Text(TEXT("X"),89,Y+18,12,Navy,18);
        if(FocusId==2000+I && Mouse.X<0)Rect(112,Y+14,3,18,Chosen?Mint:Navy);
        Text(E.DisplayName,162,Y+9,12,Fore,286);
        Text(E.bBuiltIn?TEXT("STARTER / AUTHORED"):Pending(E)?TEXT("BACKGROUND IMPORT"):TEXT("LOCAL / MANAGED COPY"),162,Y+28,8,Chosen?Mint:Color(0x48635d),286);
        Text(Duration(E.DurationSeconds),466,Y+16,10,Fore,55);
        Text(E.Analysis?FString::Printf(TEXT("%.0f"),E.Grid.Bpm):TEXT("--"),528,Y+16,10,Fore,59);
        Text(StateLabel(E),595,Y+10,8,E.State==EBootLibraryState::Failed?(Chosen?Amber:Red):E.State==EBootLibraryState::NeedsReview?(Chosen?Amber:Navy):Fore,108);
        if(Pending(E))
        {Rect(595,Y+30,100,4,Color(0x80918a));Rect(595,Y+30,100*FMath::Clamp(E.Progress,0.,1.),4,Mint);}
        Areas.Add({RowRect,1000+I,E.Hash});Areas.Add({CheckRect,2000+I,E.Hash});
    }
    if(Items.IsEmpty())
    {Text(TEXT("YOUR NEXT RUN STARTS HERE"),110,310,14,Navy,570);Text(TEXT("ADD WAV OR MP3 FILES TO BUILD YOUR PLAYLIST"),110,345,10,Ink,570);}
    Elements.PopClip();
    Button(6,TEXT("UP"),70,548,74,28,false,ScrollRow>0);
    Button(7,TEXT("DOWN"),154,548,88,28,false,ScrollRow+VisibleRows<Items.Num());
    Text(Items.IsEmpty()?TEXT("0 / 0"):FString::Printf(TEXT("%d-%d / %d"),ScrollRow+1,FMath::Min(ScrollRow+VisibleRows,Items.Num()),Items.Num()),266,558,9,Ink,210);
    Text(TEXT("WHEEL / ARROWS / TAB / ENTER"),70,590,10,Ink,640);
    Rect(732,188,478,410,Ink);Rect(734,190,474,406,Color(0xe1e6d3));
    Rect(736,192,470,28,Color(0x0a4f55));Text(TEXT("TRACK PROPERTIES"),750,201,11,Paper,440);
    if(Entry)
    {
        const bool HasGrid=Entry->Analysis.IsValid();
        const FString GridType=Entry->bHasGridOverride?TEXT("MANUAL GRID"):Entry->bSafeGridAccepted?TEXT("SAFE GRID ACCEPTED"):HasGrid && Entry->Analysis->Source==EBootGridSource::Authored?TEXT("AUTHORED GRID"):HasGrid && Entry->Analysis->Source==EBootGridSource::Safe?TEXT("SAFE GRID / REVIEW"):TEXT("ESTIMATED GRID");
        Text(StateLabel(*Entry)+TEXT(" / ")+GridType,750,235,10,Entry->State==EBootLibraryState::Failed?Red:Navy,440);
        Text(FString::Printf(TEXT("%s / CONFIDENCE %.0f%%"),*Duration(Entry->DurationSeconds),HasGrid?Entry->Analysis->Confidence*100.f:0.f),750,257,10,Ink,440);
        Field(10,Entry->DisplayName,TEXT("NAME / SELECT TO RENAME"),750,283,442);
        Field(11,FString::Printf(TEXT("%.3f"),Entry->Grid.Bpm),TEXT("BPM / 30-300"),750,345,202,HasGrid);
        Field(12,FString::Printf(TEXT("%.4f"),Entry->Grid.FirstBeatSeconds),TEXT("FIRST BEAT / SECONDS"),974,345,218,HasGrid);
        Button(20,TEXT("-1"),750,405,46,30,false,HasGrid);Button(21,TEXT("+1"),802,405,46,30,false,HasGrid);
        Button(22,TEXT("/2"),854,405,46,30,false,HasGrid);Button(23,TEXT("X2"),906,405,46,30,false,HasGrid);
        Button(24,TEXT("-.01"),974,405,102,30,false,HasGrid);Button(25,TEXT("+.01"),1084,405,108,30,false,HasGrid);
        Text(TEXT("EDITS APPLY TO THE NEXT PLAYBACK."),750,451,10,Navy,442);
        Text(TEXT("CURRENT MUSIC / LANDING STAY STABLE."),750,473,9,Ink,442);
        if(Entry->State==EBootLibraryState::NeedsReview)
            Button(14,TEXT("ACCEPT SAFE GRID"),750,500,442,34,true,HasGrid);
        else if(!Entry->Error.IsEmpty()) Text(Entry->Error,750,505,10,Red,442);
        else if(Pending(*Entry)) Text(FString::Printf(TEXT("BACKGROUND IMPORT / %.0f%%"),Entry->Progress*100.),750,505,10,Navy,442);
        Button(13,Pending(*Entry)?TEXT("CANCEL IMPORT BATCH"):Entry->bBuiltIn?TEXT("STARTER TRACK / UNCHECK TO EXCLUDE"):TEXT("REMOVE FROM LIBRARY"),750,548,442,32,false,Pending(*Entry)||!Entry->bBuiltIn);
    }
    else Text(TEXT("SELECT A TRACK TO VIEW ITS GRID"),750,252,11,Ink,440);
    Rect(70,614,858,30,Ink);Text(Message.IsEmpty()?TEXT("LOCAL ONLY / ORIGINAL FILES ARE ALWAYS KEPT"):Message,82,624,9,Message.IsEmpty()?Mint:Paper,834);
    Button(4,TEXT("BACK"),944,614,108,32);
    Button(5,TEXT("RUN >"),1064,614,146,32,true);
    Text(TEXT("CANONICAL 48 KHZ / UP TO 10 MIN / 256 MIB SOURCE / 2 GIB CACHE"),70,679,9,Gray,1140);
    return Layer;
}
