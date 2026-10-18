#include "UI/BootShellWidget.h"
#include "Run/BootGameMode.h"
#include "Core/BootGameInstance.h"
#include "Player/BootRunner.h"
#include "Music/BootRhythmComponent.h"
#include "Music/BootMusicLibrarySubsystem.h"
#include "Music/BootMusicTransportComponent.h"
#include "Movement/BootMovementComponent.h"
#include "World/Interaction/BootSurfWindow.h"
#include "World/Threats/BootPopupProcess.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Input/Reply.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

namespace
{
    FLinearColor Color(uint32 Hex, float Alpha = 1.f)
    { FLinearColor C(FColor((Hex >> 16) & 255, (Hex >> 8) & 255, Hex & 255)); C.A = Alpha; return C; }
    const FLinearColor Ink = Color(0x14252e), Paper = Color(0xeef2dc), Gray = Color(0xcbd2cd),
        Teal = Color(0x0a4f55), Navy = Color(0x192d73), Green = Color(0x64c9a7), Yellow = Color(0xf5cd60), Red = Color(0xdc4141);
}

void UBootShellWidget::NativeOnInitialized() { Super::NativeOnInitialized(); SetIsFocusable(true); ForceVolatile(true); }
void UBootShellWidget::NativeDestruct()
{ if(auto* World=GetWorld())World->GetTimerManager().ClearTimer(HudTimer);Super::NativeDestruct(); }
void UBootShellWidget::RefreshMusicHud()
{
    const auto* Game=GetWorld()?GetWorld()->GetAuthGameMode<ABootGameMode>():nullptr;
    MusicHud=Game && (Game->IsMusicRun() || Game->IsFirstRun())?Game->GetRunSnapshot():FBootRunSnapshot{};
    const auto* Name=SongTitles.Find(MusicHud.Music.TrackHash);
    CurrentSongTitle=Name?*Name:MusicHud.Music.TrackHash.IsEmpty()?TEXT("OS AMBIENT / SAFE METRONOME"):TEXT("LOCAL TRACK");
    const auto* Runner=Cast<ABootRunner>(GetOwningPlayerPawn());
    const auto* Transport=Runner?Runner->GetMusicTransport():nullptr;
    const FString NextHash=Transport?Transport->GetNextTrackHash():FString();
    const auto* NextName=SongTitles.Find(NextHash);
    NextSongTitle=NextName?*NextName:!NextHash.IsEmpty()?TEXT("LOCAL TRACK"):
        MusicHud.Music.TrackHash.IsEmpty()&&MusicHud.Music.bSafeGrid?TEXT("SAFE GRID / NO PENDING SONG"):TEXT("PREPARING...");
}

FString UBootShellWidget::InteractionPrompt(const ABootSurfWindow* Target,const ABootRunner* Runner,const FBootInputPreferences& Inputs) const
{
    const auto* Popup=Cast<ABootPopupProcess>(Target);
    const TCHAR* Action=Popup && Popup->IsHostile()?TEXT("INTERCEPT"):
        Runner && Target && Runner->GetMovementBaseObject()==Target->GetRootComponent()?TEXT("SNAP"):TEXT("BOARD");
    return Inputs.Label(EBootInputAction::Snap).ToString()+TEXT(" > ")+Action;
}

void UBootShellWidget::ChangePage(EBootPage NewPage)
{
    Page = NewPage; Focus = 0; Drag = -1; InputScrollRow=0; Mouse = FVector2D(-1,-1);
    if(auto* World=GetWorld())World->GetTimerManager().ClearTimer(HudTimer);
    if(Page==EBootPage::Hud || Page==EBootPage::Results)
    {
        SongTitles.Reset();
        if(const auto* GI=GetGameInstance())if(const auto* Library=GI->GetSubsystem<UBootMusicLibrarySubsystem>())
            for(const auto& Entry:Library->Snapshot())SongTitles.Add(Entry.Hash,Entry.DisplayName);
        RefreshMusicHud();
        if(Page==EBootPage::Hud)if(auto* World=GetWorld())World->GetTimerManager().SetTimer(HudTimer,this,&UBootShellWidget::RefreshMusicHud,.1f,true);
    }
    OpenedAt = FPlatformTime::Seconds();
    if (Page == EBootPage::Settings)
        if (const auto* GI = GetGameInstance<UBootGameInstance>()) Draft = GI->GetOptions();
    InputDraft.CancelCapture(); InputMessage.Empty();
    if(Page==EBootPage::Inputs) if(const auto* GI=GetGameInstance<UBootGameInstance>()) InputDraft.Draft=GI->GetInputPreferences();
    SetVisibility(Page==EBootPage::Library?ESlateVisibility::Collapsed:Page == EBootPage::Hud || Page == EBootPage::Boot ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Visible);
}

int32 UBootShellWidget::NativePaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Culling,
    FSlateWindowElementList& E, int32 LayerId, const FWidgetStyle& Style, bool bEnabled) const
{
    const auto* GI = GetGameInstance<UBootGameInstance>();
    const auto* PC = Cast<ABootPlayerController>(GetOwningPlayer());
    const auto* Game = GetWorld() ? GetWorld()->GetAuthGameMode<ABootGameMode>() : nullptr;
    const auto* Runner = PC ? Cast<ABootRunner>(PC->GetPawn()) : nullptr;
    const UBootRhythmComponent* Rhythm = Runner ? Runner->GetRhythm() : nullptr;
    const UBootProfile* Profile = GI ? GI->GetProfile() : nullptr;
    const FBootInputPreferences Inputs=GI?GI->GetInputPreferences():FBootInputPreferences();
    auto KeyLabel=[&](EBootInputAction A){return Inputs.Label(A).ToString();};
    const FName DisplayCourse=Page==EBootPage::Desktop?FName(TEXT("RooftopCircuitV03")):Game?Game->GetCourseId():NAME_None;
    const FBootCourseRecord* CourseRecord=Profile?Profile->CourseRecords.Find(DisplayCourse):nullptr;
    const bool NativeRecord=DisplayCourse==TEXT("RooftopCircuitV03") || DisplayCourse==TEXT("FirstLoginV03");
    const int32 Best=CourseRecord?CourseRecord->BestScore:!NativeRecord && Profile?Profile->BestScore:0;
    const bool V04=Game && (Game->IsMusicRun() || Game->IsFirstRun());
    const int64 MusicBest=Profile?Profile->MusicRunRecord.BestScore:0;
    const FBootOptions Options = GI ? GI->GetOptions() : FBootOptions{};
    const FVector2D Size = G.GetLocalSize();
    DrawScale = FMath::Min(Size.X / 1280., Size.Y / 720.);
    // Extra UI scale is possible on roomy displays; always fit the whole menu.
    if (Page == EBootPage::Hud) DrawScale *= Options.UiScale;
    DrawOrigin = (Size - FVector2D(1280,720) * DrawScale) * .5;
    Areas.Reset();
    int32 Layer = LayerId;
    auto Rect = [&](float X, float Y, float W, float H, FLinearColor C)
    {
        FSlateDrawElement::MakeBox(E, ++Layer, G.ToPaintGeometry(FVector2D(W,H), FSlateLayoutTransform(DrawScale, DrawOrigin + FVector2D(X,Y)*DrawScale)),
            FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, C);
    };
    auto Text = [&](const FString& T, float X, float Y, int32 FontSize, FLinearColor C, bool Bold = false)
    {
        FSlateDrawElement::MakeText(E, ++Layer, G.ToPaintGeometry(FVector2D(1200,120), FSlateLayoutTransform(DrawScale, DrawOrigin + FVector2D(X,Y)*DrawScale)),
            T, FSlateFontInfo(FPaths::ProjectContentDir()/TEXT("Slate/BootPixel.ttf"),FMath::Max(FontSize,11),EFontHinting::Monochrome), ESlateDrawEffect::None, C);
    };
    auto Frame = [&](float X, float Y, float W, float H, const FString& Title, FLinearColor TitleColor)
    {
        Rect(X+8,Y+9,W,H,Color(0x000000,.22f));
        Rect(X,Y,W,H,Ink); Rect(X+2,Y+2,W-4,H-4,Gray);
        Rect(X+2,Y+2,W-4,2,Paper); Rect(X+2,Y+2,2,H-4,Paper);
        Rect(X+6,Y+6,W-12,34,TitleColor);
        Text(Title,X+18,Y+12,14,Paper,true);
        Text(TEXT("SYS"),X+W-48,Y+15,10,Paper);
    };
    auto Button = [&](int32 Id, const FString& Label, float X, float Y, float W, float H, bool Primary = false)
    {
        FSlateRect Bounds(X,Y,X+W,Y+H);
        const int32 AreaIndex = Areas.Num();
        const bool Hot = Bounds.ContainsPoint(Mouse) || (Mouse.X < 0 && AreaIndex == Focus);
        Areas.Add({Bounds,Id,false});
        Rect(X,Y,W,H,Ink);
        Rect(X+2,Y+2,W-4,H-4,Primary ? (Hot ? Paper : Yellow) : (Hot ? Paper : Gray));
        Rect(X+2,Y+2,W-4,2,Paper);
        Rect(X+2,Y+2,2,H-4,Paper);
        if (Hot) { Rect(X+7,Y+7,3,H-14,Navy); }
        Text(Label,X+19,Y+(H-22)/2,Primary ? 18 : 14,Ink,true);
    };
    auto Slider = [&](int32 Id, const FString& Label, const FString& Value, float X, float Y)
    {
        const FSlateRect Bounds(X-7,Y+18,X+355,Y+52);
        if (Bounds.ContainsPoint(Mouse) || (Mouse.X < 0 && Areas.Num() == Focus)) Rect(X-14,Y+24,4,21,Navy);
        Text(Label,X,Y,13,Ink,true); Text(Value,X+275,Y,13,Navy,true);
        const float V = GetSlider(Id);
        Rect(X,Y+31,348,7,Color(0x708582)); Rect(X,Y+31,348*V,7,Navy);
        Rect(X+348*V-5,Y+24,12,21,Ink); Rect(X+348*V-3,Y+26,8,17,Paper);
        Areas.Add({Bounds,Id,true});
    };

    if(Page==EBootPage::Hud && V04)
    {
        DrawScale=FMath::Min(Size.X/1280.,Size.Y/720.);DrawOrigin=(Size-FVector2D(1280,720)*DrawScale)*.5;
        const float S=Options.UiScale;const bool Training=Game->IsFirstRun();const auto& View=MusicHud;
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        auto Fitted=[&](FString Value,float X,float Y,int32 FontSize,float Width,FLinearColor C)
        {
            FSlateFontInfo Font(FPaths::ProjectContentDir()/TEXT("Slate/BootPixel.ttf"),FMath::Max(FontSize,11),EFontHinting::Monochrome);
            while(Font.Size>11 && Measure->Measure(Value,Font).X>Width)--Font.Size;
            while(Value.Len()>1 && Measure->Measure(Value,Font).X>Width)Value.LeftChopInline(1);
            Text(Value,X,Y,Font.Size,C);
        };
        Rect(24,22,624,76,Color(0x14252e,.9f));Rect(24,22,4,76,Green);
        Text(Training?TEXT("FIRST RUN / UNRANKED"):TEXT("MUSIC RUN / ENDLESS"),40,Training?31:29,(Training?14:12)*S,Green,true);
        Fitted(Training?TEXT("LEARN / TRIAL ABILITIES RESET ON EXIT"):CurrentSongTitle,40,Training?55:48,12*S,590,Paper);
        if(!Training)Fitted(TEXT("NEXT / ")+NextSongTitle,40,65,10*S,590,Gray);
        Text(FString::Printf(TEXT("%.0f BPM / %s / %.0f SEC"),View.Music.EffectiveBpm,Training?TEXT("TRAINING GRID"):View.Music.bSafeGrid?TEXT("SAFE GRID"):TEXT("TRACK GRID"),View.ActiveSeconds),40,Training?79:82,9,Gray);
        Rect(870,22,386,76,Color(0x14252e,.9f));
        Text(Training?TEXT("TRAINING / NO SCORE"):TEXT("RUN SCORE"),888,31,10*S,Green);
        Fitted(Training?TEXT("UNRANKED"):FString::Printf(TEXT("%lld"),View.Score),888,47,28*S,348,Paper);
        if(!Training)Fitted(FString::Printf(TEXT("BEST %lld"),MusicBest),888,82,9,348,Gray);
        const FString Objective=Game->GetObjective();
        if(!Objective.IsEmpty())
        {
            Rect(140,108,1000,50,Color(0x14252e,.92f));
            TArray<FString> Words;Objective.ParseIntoArrayWS(Words);FString Line;int32 Row=0;
            const FSlateFontInfo Font(FPaths::ProjectContentDir()/TEXT("Slate/BootPixel.ttf"),11,EFontHinting::Monochrome);
            for(const auto& Word:Words)
            {
                const FString Candidate=Line.IsEmpty()?Word:Line+TEXT(" ")+Word;
                if(!Line.IsEmpty() && Measure->Measure(Candidate,Font).X>960)
                {Text(Line,156,118+Row*18,11,Paper,true);Line=Word;if(++Row>=2)break;}
                else Line=Candidate;
            }
            if(Row<2)Text(Line,156,118+Row*18,11,Paper,true);
        }
        Rect(638,358,4,4,Paper);
        if(auto* Target=Runner?Runner->FindSurfTarget():nullptr)
        {
            const FString Prompt=InteractionPrompt(Target,Runner,Inputs);
            Rect(420,408,440,46,Ink);Rect(423,411,434,40,Yellow);Fitted(Prompt,438,422,14,404,Ink);
        }
        if(Rhythm && Rhythm->GetFeedbackAge()<.65f && !Training)
        {
            const auto Rating=View.LastRating;
            Fitted(FString::Printf(TEXT("SURF %s +%lld"),Rating==EBootBeatRating::Perfect?TEXT("PERFECT"):Rating==EBootBeatRating::Good?TEXT("SYNCED"):TEXT("COMPLETE"),View.LastPoints),440,490,16,520,Rating==EBootBeatRating::Perfect?Green:Yellow);
        }
        if(Runner && !Runner->GetSurfHint().IsEmpty())Fitted(Runner->GetSurfHint(),390,468,11,580,Yellow);
        if(Runner && Runner->GetParkour()->IsWallRunning())Fitted(TEXT("WALLRUN / ")+KeyLabel(EBootInputAction::Jump)+TEXT(" TO KICK"),440,525,12,480,Green);
        else if(Runner && Runner->bIsCrouched)Text(Runner->IsSliding()?TEXT("SLIDE"):TEXT("LOW CLEARANCE"),585,525,12,Yellow);
        FString Build;TArray<FName> CardNames;View.ActiveModifiers.Stacks.GetKeys(CardNames);
        CardNames.Sort([](const FName& A,const FName& B){return A.LexicalLess(B);});
        for(const auto& Id:CardNames){if(!Build.IsEmpty())Build+=TEXT(" / ");Build+=FString::Printf(TEXT("%s x%d"),*Id.ToString(),View.ActiveModifiers.Stacks.FindRef(Id));}
        if(Build.IsEmpty())Build=Training?TEXT("TRIAL LOADOUT / NOTHING CARRIES INTO MUSIC RUN"):TEXT("BUILD / FIRST CARDS AFTER 25 ACTIVE SEC + 100 M");
        Rect(280,556,720,38,Color(0x14252e,.85f));Fitted(Build,294,564,10,692,Gray);
        Text(View.bPendingMutation?TEXT("PENDING / KEEP MOVING TO A SAFE CONNECTOR"):TEXT("ACTIVE BUILD"),294,581,8,View.bPendingMutation?Yellow:Green);
        Rect(24,608,245,86,Color(0x14252e,.9f));
        Text(FString::Printf(TEXT("%.0f"),Runner?Runner->GetVelocity().Size2D()/100.:0.),40,618,30*S,Paper,true);Text(TEXT("m/s"),145,640,12,Green);
        Fitted(FString::Printf(TEXT("DISTANCE %.0f m"),View.DistanceMetres),40,674,11,214,Gray);
        Rect(1034,608,222,86,Color(0x14252e,.9f));Text(Training?TEXT("PRACTICE"):TEXT("SURF COMBO"),1050,619,11,Green);
        Text(Training?TEXT("FREE"):FString::Printf(TEXT("%d"),View.Combo),1050,646,26,Paper,true);
        Rect(288,608,704,32,Color(0x14252e,.9f));
        Fitted(FString::Printf(TEXT("ALT-TAB %d/%d  |  [%s] DASH %d/%d  |  GUARD %d/%d"),View.Abilities.RedirectCharges,View.Abilities.MaxRedirectCharges,*KeyLabel(EBootInputAction::BufferDash),View.Abilities.DashCharges,View.Abilities.MaxDashCharges,View.Abilities.GuardCharges,View.Abilities.MaxGuardCharges),300,620,11*S,680,Paper);
        const float Pulse=Rhythm?Rhythm->GetPulse():0.f;
        for(int32 I=0;I<8;++I)Rect(528+I*28,655,22,10,FMath::Lerp(Color(0x385451),Green,FMath::Clamp(Pulse*1.4f-I*.09f,0.f,1.f)));
        Fitted(TEXT("MOVEMENT WORKS ON EVERY BEAT"),366,681,9,548,Gray);
        return Layer;
    }

    if (Page == EBootPage::Hud)
    {
        // HUD stays anchored to screen edges even when the user requests larger text.
        DrawScale = FMath::Min(Size.X / 1280., Size.Y / 720.);
        DrawOrigin = (Size - FVector2D(1280,720) * DrawScale) * .5;
        const float S = Options.UiScale;
        const float Pulse = Rhythm ? Rhythm->GetPulse() : 0.f;
        Rect(24,22,250*S,48*S,Color(0x14252e,.88f)); Rect(24,22,4,48*S,Green);
        Text(TEXT("BOOTLOOP.EXE"),40,29,16*S,Paper,true);
        Text(Game && Game->IsTraining()?TEXT("FIRST LOGIN / TRAINING"):TEXT("ROOFTOP CIRCUIT / 130 BPM"),40,53,9*S,Green);
        Rect(1000-90*(S-1),22,256+90*(S-1),80*S,Color(0x14252e,.88f));
        Text(TEXT("SCORE"),1018-90*(S-1),31,10*S,Green);
        Text(FString::Printf(TEXT("%07d"),Game ? Game->GetTotalScore() : 0),1016-90*(S-1),46,29*S,Paper,true);
        Text(FString::Printf(TEXT("BEST %07d"),Best),1018-90*(S-1),83,9*S,Gray);
        const float Y = 694-86*S;
        Rect(24,Y,245*S,86*S,Color(0x14252e,.88f));
        Text(FString::Printf(TEXT("%.0f"),Runner ? Runner->GetVelocity().Size2D()/100. : 0.),40,Y+8,31*S,Paper,true);
        Text(TEXT("m/s"),128,Y+27,12*S,Green);
        Text(FString::Printf(TEXT("DISTANCE  %05.0f m"),Game ? Game->GetDistance() : 0.f),40,Y+55*S,12*S,Gray);
        Rect(1034-60*(S-1),Y,222+60*(S-1),86*S,Color(0x14252e,.88f));
        Text(TEXT("SYNC CHAIN"),1050-60*(S-1),Y+12,10*S,Green);
        Text(FString::Printf(TEXT("%02d"),Rhythm ? Rhythm->GetScore().Combo : 0),1050-60*(S-1),Y+30,31*S,Paper,true);
        Text(TEXT("SURFS"),1124,Y+50,11*S,Gray);
        Rect(520,653,240,28,Color(0x14252e,.85f));
        for (int32 I=0;I<8;++I) Rect(528+I*28,661,22,12,FMath::Lerp(Color(0x385451),Green,FMath::Clamp(Pulse*1.4f-I*.09f,0.f,1.f)));
        Text(Rhythm && Rhythm->IsReady() ? TEXT("130 / IN SYNC") : TEXT("130 / INITIALIZING"),585,687,9,Paper);
        Rect(638,358,4,4,Paper);
        if (Rhythm && Rhythm->GetFeedbackAge() < .65f)
        {
            const auto& Score=Rhythm->GetScore();
            const bool Perfect=Score.LastRating==EBootBeatRating::Perfect;
            const bool Good=Score.LastRating==EBootBeatRating::Good;
            Text(FString::Printf(TEXT("SURF %s  +%d"),Perfect?TEXT("PERFECT"):Good?TEXT("SYNCED"):TEXT("COMPLETE"),Score.LastPoints),510,490,18,Perfect?Green:Good?Yellow:Gray,true);
        }
        if(auto* Target=Runner?Runner->FindSurfTarget():nullptr)
        {
            const FString Prompt=InteractionPrompt(Target,Runner,Inputs);
            const FSlateFontInfo PromptFont(FPaths::ProjectContentDir()/TEXT("Slate/BootPixel.ttf"),14,EFontHinting::Monochrome);
            const float Width=FMath::Clamp(float(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Prompt,PromptFont).X)+44.f,240.f,1000.f);
            const float X=(1280.f-Width)*.5f;
            Rect(X,408,Width,46,Ink); Rect(X+3,411,Width-6,40,Yellow);
            Text(Prompt,X+22,422,14,Ink,true);
        }
        if(Runner && Runner->GetParkour()->IsWallRunning()) Text(TEXT("WALLRUN / ")+KeyLabel(EBootInputAction::Jump)+TEXT(" TO KICK"),450,535,13,Green,true);
        if(Runner && !Runner->GetSurfHint().IsEmpty()) Text(Runner->GetSurfHint(),480,468,12,Yellow,true);
        if (Runner && Runner->bIsCrouched) Text(Runner->IsSliding()?TEXT("SLIDE"):TEXT("LOW CLEARANCE"),585,535,12,Yellow,true);
        if(Game && !Game->GetObjective().IsEmpty())
        {
            Rect(300,102,680,53,Color(0x14252e,.88f));
            Text(Game->GetObjective(),316,113,11,Paper,true);
            Text(TEXT("JUMP ")+KeyLabel(EBootInputAction::Jump)+TEXT(" / SLIDE ")+KeyLabel(EBootInputAction::Slide)+TEXT(" / SNAP ")+KeyLabel(EBootInputAction::Snap),316,135,10,Green);
        }
        if(Runner)
        {
            Text(KeyLabel(EBootInputAction::AirRedirect)+ (Runner->GetParkour()->IsAirRedirectReady()?TEXT(" / ALT-TAB READY"):TEXT(" / LAND TO RECHARGE")),35,572,11,Runner->GetParkour()->IsAirRedirectReady()?Green:Gray,true);
        }
        return Layer;
    }

    // Desktop fills any aspect-ratio margins too.
    FSlateDrawElement::MakeBox(E,++Layer,G.ToPaintGeometry(),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,Color(0x0a333f,.96f));
    for (int32 X=0;X<1280;X+=64) Rect(X,0,1,720,Color(0x64c9a7,.07f));
    for (int32 Y=0;Y<720;Y+=64) Rect(0,Y,1280,1,Color(0x64c9a7,.07f));
    // Sparse system marks provide depth without visual noise.
    Text(TEXT("BOOT/OS 95"),38,28,13,Green,true);
    Text(TEXT("PERSONAL COMPUTING / UNRESTRICTED MOTION"),760,31,10,Color(0xa4bdb3));
    Rect(1185,28,54,18,Green); Text(TEXT("ONLINE"),1191,31,8,Ink,true);

    if (Page==EBootPage::Desktop)
    {
        Text(TEXT("BOOT"),80,137,88,Paper,true);
        Text(TEXT("LOOP"),80,225,88,Paper,true);
        Rect(87,332,238,3,Green); Text(TEXT(".EXE / v0.4"),347,314,22,Yellow,true);
        Text(TEXT("A RUNAWAY PROCESS."),88,371,15,Green,true);
        Text(TEXT("Your music. Endless momentum."),88,410,20,Paper);
        Text(TEXT("Surf windows. Build your run."),88,443,17,Color(0xa4bdb3));
        Rect(88,510,450,96,Color(0x14252e,.7f));
        Text(TEXT("PERSONAL BEST"),106,524,10,Green,true);
        Text(FString::Printf(TEXT("%lld"),MusicBest),104,546,MusicBest>99999999?16:24,Paper,true);
        Rect(312,528,1,59,Color(0x385451));
        Text(TEXT("FURTHEST PROCESS"),331,524,10,Green,true);
        Text(FString::Printf(TEXT("%.0f m"),Profile?Profile->MusicRunRecord.BestDistanceMetres:0.),330,549,22,Paper,true);
        Frame(650,126,500,480,TEXT("BOOT/OS 95  /  EXECUTE PROGRAM"),Navy);
        Text(TEXT("C:\\SYSTEM\\BOOTLOOP.EXE"),682,192,12,Ink,true);
        Rect(682,230,8,8,Green); Text(TEXT("READY TO EXECUTE"),700,225,13,Ink,true);
        Text(TEXT("YOUR PLAYLIST / ENDLESS TRACK / RUN-LOCAL CARDS"),682,253,11,Color(0x385451));
        Button(13,Profile && Profile->HasCurrentOnboarding()?TEXT("START MUSIC RUN   >"):TEXT("START / FIRST RUN   >"),682,290,436,58,true);
        Button(15,TEXT("MUSIC LIBRARY / PLAY WITHOUT TUTORIAL"),682,360,436,42);
        Button(12,TEXT("LEARN / FIRST RUN"),682,414,436,42);
        Button(2,TEXT("SETTINGS"),682,468,210,40); Button(11,TEXT("INPUTS"),908,468,210,40);
        Button(3,TEXT("HELP"),682,520,210,40); Button(4,TEXT("SHUT DOWN"),908,520,210,40);
        Text(TEXT("MOVE FREELY / CHOOSE CARDS WHILE RUNNING"),696,578,10,Ink,true);
    }
    else if(Page==EBootPage::Inputs)
    {
        Frame(120,50,1040,610,TEXT("CONTROL PANEL / INPUTS"),Navy);
        Text(TEXT("ACTION"),160,107,12,Navy,true); Text(TEXT("PRIMARY"),390,107,12,Navy,true); Text(TEXT("SECONDARY"),730,107,12,Navy,true);
        const TCHAR* Names[]={TEXT("FORWARD"),TEXT("BACK"),TEXT("LEFT"),TEXT("RIGHT"),TEXT("JUMP"),TEXT("SLIDE"),TEXT("SNAP"),TEXT("ALT-TAB"),TEXT("RETRY"),TEXT("CARD 1"),TEXT("CARD 2"),TEXT("CARD 3"),TEXT("BUFFER DASH")};
        static_assert(UE_ARRAY_COUNT(Names)==int32(EBootInputAction::Count));
        // The painted list is a clipped, row-scrolled container. Only visible
        // controls enter hit testing, so hidden bindings cannot intercept clicks.
        E.PushClip(FSlateClippingZone(G.ToPaintGeometry(FVector2D(980,344),FSlateLayoutTransform(DrawScale,DrawOrigin+FVector2D(150,133)*DrawScale))));
        for(int32 I=InputScrollRow;I<FMath::Min(InputScrollRow+8,int32(EBootInputAction::Count));++I)
        {
            const float Y=137+(I-InputScrollRow)*43;
            const int32 RowId=I<9?300+I*4:400+(I-9)*4;
            Text(Names[I],160,Y+9,14,Ink,true);
            for(int32 KeySlot=0;KeySlot<2;++KeySlot)
            {
                const FKey Key=InputDraft.Draft.GetKey(EBootInputAction(I),KeySlot);
                const bool Selected=InputDraft.Action==I && InputDraft.Slot==KeySlot;
                const float X=KeySlot==0?375:715;
                Button(RowId+KeySlot,Selected?TEXT("PRESS KEY..."):Key.IsValid()?Key.GetDisplayName().ToString():TEXT("UNBOUND"),X,Y,220,34,Selected);
                Button(RowId+2+KeySlot,TEXT("CLEAR"),X+230,Y,94,34);
            }
        }
        E.PopClip();
        Button(356,TEXT("^"),1090,137,40,32);Button(357,TEXT("v"),1090,441,40,32);
        Text(FString::Printf(TEXT("%d-%d / %d  WHEEL OR PAGE UP/DOWN TO SCROLL"),InputScrollRow+1,FMath::Min(InputScrollRow+8,int32(EBootInputAction::Count)),int32(EBootInputAction::Count)),160,493,10,Navy);
        const TCHAR* Wheel[]={TEXT("OFF"),TEXT("UP"),TEXT("DOWN"),TEXT("BOTH")};
        Button(350,FString(TEXT("WHEEL JUMP / "))+Wheel[int32(InputDraft.Draft.WheelPolicy)],160,537,390,34);
        Button(351,InputDraft.Draft.bInvertY?TEXT("INVERT Y / ON"):TEXT("INVERT Y / OFF"),570,537,320,34);
        Text(InputDraft.IsCapturing()?(InputDraft.bArmed?TEXT("PRESS KEY OR MOUSE BUTTON / ESC CANCELS CAPTURE"):TEXT("RELEASE OPENING KEY OR BUTTON...")):InputMessage.IsEmpty()?TEXT("Two slots per action. Wheel policy is separate. Escape always pauses."):InputMessage,160,583,11,Navy);
        Button(352,TEXT("DEFAULTS"),160,614,210,34);
        Button(7,TEXT("CANCEL"),730,614,150,34); Button(353,TEXT("APPLY"),900,614,220,34,true);
        if(InputDraft.bConflict)
        {
            Areas.Reset();
            Frame(330,255,620,200,TEXT("KEY ALREADY ASSIGNED"),Navy);
            Text(InputDraft.PendingKey.GetDisplayName().ToString()+TEXT(" IS ASSIGNED / SWAP?"),365,318,15,Ink);
            Button(354,TEXT("SWAP"),365,380,250,44,true); Button(355,TEXT("CANCEL"),640,380,270,44);
        }
    }
    else if (Page==EBootPage::Settings)
    {
        Frame(180,65,920,570,TEXT("CONTROL PANEL  /  PREFERENCES"),Navy);
        Text(TEXT("AUDIO & MOVEMENT"),214,123,13,Navy,true);
        Text(TEXT("DISPLAY & COMFORT"),660,123,13,Navy,true);
        Slider(101,TEXT("MUSIC"),FString::Printf(TEXT("%.0f %%"),Draft.Music*100),214,161);
        Slider(102,TEXT("SOUND EFFECTS"),FString::Printf(TEXT("%.0f %%"),Draft.Sfx*100),214,229);
        Slider(103,TEXT("MOUSE SENSITIVITY"),FString::Printf(TEXT("%.2f"),Draft.Sensitivity),214,317);
        Slider(104,TEXT("FIELD OF VIEW"),FString::Printf(TEXT("%.0f"),Draft.Fov),214,385);
        Slider(105,TEXT("RHYTHM OFFSET"),FString::Printf(TEXT("%+.0f ms"),Draft.InputOffsetMs),214,453);
        const TCHAR* Res[]={TEXT("1280 x 720"),TEXT("1600 x 900"),TEXT("1920 x 1080")};
        const TCHAR* Modes[]={TEXT("WINDOWED"),TEXT("BORDERLESS"),TEXT("FULLSCREEN")};
        const TCHAR* Caps[]={TEXT("60 FPS"),TEXT("120 FPS"),TEXT("144 FPS"),TEXT("UNLIMITED")};
        Button(201,FString(TEXT("RESOLUTION   "))+Res[Draft.Resolution],660,151,404,34);
        Button(202,FString(TEXT("MODE   "))+Modes[Draft.WindowMode],660,195,404,34);
        Button(203,FString(TEXT("LIMIT   "))+Caps[Draft.FrameLimit],660,239,404,34);
        Button(204,FString::Printf(TEXT("HUD SCALE   %.0f %%"),Draft.UiScale*100),660,283,404,34);
        Button(205,Draft.bPulse?TEXT("BEAT PULSE   ON"):TEXT("BEAT PULSE   OFF"),660,327,404,34);
        Button(206,Draft.bBody?TEXT("SHELL AVATAR   ON"):TEXT("SHELL AVATAR   OFF"),660,371,404,34);
        Button(207,Draft.bMotion?TEXT("CAMERA MOTION   ON"):TEXT("CAMERA MOTION   REDUCED"),660,415,404,34);
        const TCHAR* Pixels[]={TEXT("CLASSIC"),TEXT("COARSE"),TEXT("FINE")};
        Button(208,FString(TEXT("PIXELS   "))+Pixels[Draft.PixelQuality],660,459,404,34);
        Button(209,FString::Printf(TEXT("EFFECTS / %.0f %%"),Draft.EffectsIntensity*100),660,502,404,30);
        Text(TEXT("Positive offset judges inputs later. Off-beat movement is always allowed."),214,543,10,Color(0x385451));
        Button(10,TEXT("DEFAULTS"),214,568,160,44);
        Button(7,TEXT("BACK"),660,568,130,44);
        Button(8,TEXT("APPLY & SAVE"),806,568,258,44,true);
    }
    else if (Page==EBootPage::Controls)
    {
        Frame(240,80,800,550,TEXT("HELP.EXE  /  KEEP THE PROCESS ALIVE"),Navy);
        const FString Keys[]={KeyLabel(EBootInputAction::Forward)+TEXT(" / ")+KeyLabel(EBootInputAction::Left),KeyLabel(EBootInputAction::Jump),KeyLabel(EBootInputAction::Slide),KeyLabel(EBootInputAction::Snap),KeyLabel(EBootInputAction::AirRedirect),KeyLabel(EBootInputAction::Retry)+TEXT(" / ESC")};
        const TCHAR* Lines[]={TEXT("Move and air strafe. Carry momentum."),TEXT("Jump / wallkick. Fresh landing input builds bhop."),TEXT("Duck in air, slide on landing. Jump when clear."),TEXT("SNAP amber windows / intercept red POPUP."),TEXT("ALT-TAB redirects in air. Land to recharge."),TEXT("Retry / pause. Inputs remap on their own page.")};
        for(int32 I=0;I<6;++I)
        {
            Rect(274,146+I*53,160,36,Paper); Text(Keys[I],285,155+I*53,12,Navy,true);
            Text(Lines[I],456,155+I*53,12,Ink);
        }
        Text(TEXT("PLAY YOUR WAY"),274,492,16,Navy,true);
        Text(TEXT("SNAP on beat: one short overclock at departure. Off beat still works."),274,522,11,Ink);
        Text(TEXT("Points for completed surfs. Jumps, bhop and slides give no points."),274,544,11,Ink);
        Button(7,TEXT("GOT IT"),794,572,210,40,true);
    }
    else if(Page==EBootPage::Pause)
    {
        Frame(390,130,500,460,TEXT("PROCESS SUSPENDED"),Navy);
        Text(TEXT("TAKE A BREATH."),423,193,27,Ink,true);
        Text(TEXT("Your process is safe here."),425,233,13,Color(0x385451));
        Button(5,TEXT("RESUME PROCESS   >"),424,278,432,58,true);
        Button(2,TEXT("SETTINGS"),424,352,208,46);
        Button(3,TEXT("CONTROLS"),648,352,208,46);
        Button(11,TEXT("INPUTS"),424,534,208,34);
        if(Game && Game->IsTraining()) Button(14,TEXT("SKIP LESSON"),648,534,208,34);
        else if(V04)Button(15,TEXT("MUSIC LIBRARY"),648,534,208,34);
        Button(1,TEXT("RESTART RUN"),424,414,432,44);
        Button(6,TEXT("BACK TO DESKTOP"),424,474,432,44);
        Text(TEXT("ESC TO RESUME / AUDIO & PHYSICS PAUSED"),425,578,10,Color(0x385451));
    }
    else if(Page==EBootPage::Results)
    {
        const bool Complete=Game && Game->GetRunState()==EBootRunState::Complete;
        Frame(370,90,540,540,Complete?TEXT("SUCCESS  /  PROCESS ESCAPED"):TEXT("SYSTEM ERROR  /  PROCESS TERMINATED"),Complete?Teal:Red);
        Text(Complete?TEXT("FREE PROCESS."):TEXT("NICE RUN."),406,157,35,Ink,true);
        Text(Complete?FString::Printf(TEXT("Trial complete in %.2f s. Try a faster line."),Game->GetRunSeconds()):TEXT("The system won this time. Try another process."),408,208,12,Color(0x385451));
        Text(TEXT("RUN SCORE"),408,254,11,Navy,true);
        const int64 ResultScore=V04?MusicHud.Score:Game?Game->GetTotalScore():0;
        Text(FString::Printf(TEXT("%lld"),ResultScore),404,276,ResultScore>999999999?25:53,Ink,true);
        Rect(406,354,472,73,Paper);
        Text(TEXT("DISTANCE"),423,365,10,Navy,true);
        Text(FString::Printf(TEXT("%.0f m"),V04?MusicHud.DistanceMetres:Game?double(Game->GetDistance()):0.),423,386,21,Ink,true);
        Text(TEXT("PERSONAL BEST"),642,365,10,Navy,true);
        const int64 ResultBest=V04?MusicBest:Best;
        Text(FString::Printf(TEXT("%lld"),ResultBest),642,386,ResultBest>999999999?12:21,Ink,true);
        Button(1,TEXT("RETRY PROCESS / ")+KeyLabel(EBootInputAction::Retry),406,450,472,58,true);
        Button(6,TEXT("BACK TO DESKTOP"),406,526,472,44);
        Text(GI&&GI->HasSaveError()?TEXT("LOCAL SAVE FAILED / RECORD KEPT THIS SESSION"):TEXT("RECORD SAVED LOCALLY  /  NO NETWORK REQUIRED"),408,593,10,Color(0x385451));
    }
    else if(Page==EBootPage::Boot)
    {
        const float Progress=FMath::Clamp(static_cast<float>((FPlatformTime::Seconds()-OpenedAt)/1.1),0.f,1.f);
        Frame(340,235,600,250,TEXT("BOOT/OS 95  /  STARTING PROCESS"),Navy);
        Text(TEXT("BOOTLOOP.EXE"),376,300,32,Ink,true);
        Text(TEXT("Allocating track sectors. Synchronizing audio clock..."),378,352,12,Ink);
        Rect(376,399,528,28,Ink); Rect(380,403,520,20,Paper);
        for(int32 I=0;I<24*Progress;++I) Rect(383+I*21,406,17,14,Navy);
    }
    Rect(0,678,1280,42,Ink); Rect(0,680,1280,40,Gray); Rect(0,680,1280,2,Paper);
    Rect(12,686,106,28,Ink); Rect(14,688,102,24,Paper);
    Rect(22,694,12,12,Navy); Text(TEXT("BOOT/OS"),42,692,11,Ink,true);
    Rect(132,686,282,28,Color(0xa4bdb3)); Text(TEXT("BOOTLOOP.EXE / ACTIVE DESKTOP"),144,693,10,Ink,true);
    Text(TEXT("BUILD 0.4 / MUSIC RUN"),954,694,10,Ink);
    return Layer;
}

FVector2D UBootShellWidget::LogicalPoint(const FGeometry& G, FVector2D Absolute) const
{ return (G.AbsoluteToLocal(Absolute)-DrawOrigin)/FMath::Max(DrawScale,.01f); }

FReply UBootShellWidget::NativeOnMouseMove(const FGeometry& G, const FPointerEvent& Event)
{
    Mouse=LogicalPoint(G,Event.GetScreenSpacePosition());
    if(Drag>=0 && Areas.IsValidIndex(Drag))
    {
        const auto& A=Areas[Drag];
        SetSlider(A.Id,FMath::Clamp(static_cast<float>((Mouse.X-(A.Rect.Left+7))/348.),0.f,1.f));
    }
    return FReply::Handled();
}

FReply UBootShellWidget::NativeOnMouseButtonDown(const FGeometry& G, const FPointerEvent& Event)
{
    if(Page==EBootPage::Inputs && InputDraft.IsCapturing() && !InputDraft.bConflict)
    {
        InputDraft.Press(Event.GetEffectingButton(),false);
        if(InputDraft.bConflict) { Focus=0; Mouse=FVector2D(-1,-1); }
        return FReply::Handled();
    }
    Mouse=LogicalPoint(G,Event.GetScreenSpacePosition());
    if(Page==EBootPage::Results)
        if(const auto* GI=GetGameInstance<UBootGameInstance>())
            if(Event.GetEffectingButton()==GI->GetInputPreferences().GetKey(EBootInputAction::Retry,0) || Event.GetEffectingButton()==GI->GetInputPreferences().GetKey(EBootInputAction::Retry,1))
            {
                // An explicit left-clicked control keeps its visible action (e.g.
                // Back to Desktop); a configured Retry press anywhere else retries.
                if(Event.GetEffectingButton()==EKeys::LeftMouseButton)
                    for(const auto& Area:Areas) if(Area.Rect.ContainsPoint(Mouse))
                    { ActivationKey=Event.GetEffectingButton(); Activate(Area.Id); return FReply::Handled(); }
                Activate(1); return FReply::Handled();
            }
    if(Event.GetEffectingButton()!=EKeys::LeftMouseButton) return FReply::Unhandled();
    for(int32 I=0;I<Areas.Num();++I) if(Areas[I].Rect.ContainsPoint(Mouse))
    {
        Focus=I;
        const auto A=Areas[I];
        if(A.bSlider)
        {
            Drag=I; SetSlider(A.Id,FMath::Clamp(static_cast<float>((Mouse.X-(A.Rect.Left+7))/348.),0.f,1.f));
            return FReply::Handled().CaptureMouse(TakeWidget());
        }
        ActivationKey=Event.GetEffectingButton(); Activate(A.Id); return FReply::Handled();
    }
    return FReply::Handled();
}

FReply UBootShellWidget::NativeOnMouseButtonUp(const FGeometry& G, const FPointerEvent& Event)
{ InputDraft.Release(Event.GetEffectingButton()); Drag=-1; return FReply::Handled().ReleaseMouseCapture(); }

FReply UBootShellWidget::NativeOnKeyDown(const FGeometry& G, const FKeyEvent& Event)
{
    const FKey K=Event.GetKey();
    if(Page==EBootPage::Inputs && InputDraft.IsCapturing())
    {
        if(K==EKeys::Escape) InputDraft.CancelCapture();
        else if(InputDraft.bConflict)
        {
            if(K==EKeys::Tab || K==EKeys::Down || K==EKeys::Up || K==EKeys::Left || K==EKeys::Right)
            { Focus=(FMath::Clamp(Focus,0,1)+1)%2; Mouse=FVector2D(-1,-1); }
            else if((K==EKeys::Enter || K==EKeys::SpaceBar) && !Event.IsRepeat()) Activate(Focus==1?355:354);
        }
        else
        {
            InputDraft.Press(K,Event.IsRepeat());
            if(InputDraft.bConflict) { Focus=0; Mouse=FVector2D(-1,-1); }
        }
        return FReply::Handled();
    }
    if(Page==EBootPage::Inputs&&(K==EKeys::PageUp||K==EKeys::PageDown))
    {InputScrollRow=FMath::Clamp(InputScrollRow+(K==EKeys::PageDown?6:-6),0,FMath::Max(0,int32(EBootInputAction::Count)-8));Focus=0;Mouse=FVector2D(-1,-1);return FReply::Handled();}
    if(K==EKeys::Escape) { if(auto* PC=Cast<ABootPlayerController>(GetOwningPlayer())) PC->TogglePause(); return FReply::Handled(); }
    if(Page==EBootPage::Results)
        if(const auto* GI=GetGameInstance<UBootGameInstance>())
            if(!Event.IsRepeat() && (K==GI->GetInputPreferences().GetKey(EBootInputAction::Retry,0) || K==GI->GetInputPreferences().GetKey(EBootInputAction::Retry,1))) { Activate(1); return FReply::Handled(); }
    if(Areas.IsEmpty()) return FReply::Unhandled();
    if(K==EKeys::Tab || K==EKeys::Down || K==EKeys::Up)
    {
        const int32 Direction=(K==EKeys::Up || (K==EKeys::Tab && Event.IsShiftDown()))?-1:1;
        Focus=(Focus+Direction+Areas.Num())%Areas.Num(); Mouse=FVector2D(-1,-1); return FReply::Handled();
    }
    Focus=FMath::Clamp(Focus,0,Areas.Num()-1);
    if(K==EKeys::Left || K==EKeys::Right)
    {
        const auto A=Areas[Focus];
        if(A.bSlider) SetSlider(A.Id,GetSlider(A.Id)+(K==EKeys::Right?.025f:-.025f));
        else if((A.Id>=201 && A.Id<=209) || A.Id==350 || A.Id==351) Activate(A.Id);
        return FReply::Handled();
    }
    if(K==EKeys::Enter || K==EKeys::SpaceBar) { if(!Event.IsRepeat() && !Areas[Focus].bSlider) { ActivationKey=K; Activate(Areas[Focus].Id); } return FReply::Handled(); }
    return Super::NativeOnKeyDown(G,Event);
}

void UBootShellWidget::Activate(int32 Id)
{
    auto* PC=Cast<ABootPlayerController>(GetOwningPlayer());
    auto* GI=GetGameInstance<UBootGameInstance>();
    if(!PC || !GI) return;
    GI->Sound(EBootSound::Click);
    if(Page==EBootPage::Inputs && ((Id>=300 && Id<336)||(Id>=400&&Id<416)))
    {
        const int32 Action=Id<400?(Id-300)/4:9+(Id-400)/4, Field=Id<400?(Id-300)%4:(Id-400)%4;
        if(Field<2) InputDraft.Begin(EBootInputAction(Action),Field,ActivationKey);
        else InputDraft.Draft.Clear(EBootInputAction(Action),Field-2);
        return;
    }
    switch(Id)
    {
    case 1: PC->StartRun(); break;
    case 2: PC->ShowSettings(); break;
    case 3: PC->ShowControls(); break;
    case 4: PC->Quit(); break;
    case 5: PC->TogglePause(); break;
    case 6: PC->Desktop(); break;
    case 7: PC->Back(); break;
    case 8: GI->ApplyOptions(Draft); PC->Back(); break;
    case 10: Draft=FBootOptions{}; break;
    case 11: PC->ShowInputs(); break;
    case 12: PC->StartTutorial(); break;
    case 13: PC->StartPrimaryRun(); break;
    case 14: PC->SkipLesson(); break;
    case 15: PC->ShowMusicLibrary(); break;
    case 201: Draft.Resolution=(Draft.Resolution+1)%3; break;
    case 202: Draft.WindowMode=(Draft.WindowMode+1)%3; break;
    case 203: Draft.FrameLimit=(Draft.FrameLimit+1)%4; break;
    case 204: Draft.UiScale=Draft.UiScale<1.1f?1.25f:1.f; break;
    case 205: Draft.bPulse=!Draft.bPulse; break;
    case 206: Draft.bBody=!Draft.bBody; break;
    case 207: Draft.bMotion=!Draft.bMotion; break;
    case 208: Draft.PixelQuality=(Draft.PixelQuality+1)%3; break;
    case 209: Draft.EffectsIntensity=Draft.EffectsIntensity>.6f?.5f:Draft.EffectsIntensity>.1f?0.f:1.f; break;
    case 350: InputDraft.Draft.WheelPolicy=EBootWheelPolicy((int32(InputDraft.Draft.WheelPolicy)+1)%4); break;
    case 351: InputDraft.Draft.bInvertY=!InputDraft.Draft.bInvertY; break;
    case 352: InputDraft.Draft=FBootInputPreferences(); InputDraft.CancelCapture(); break;
    case 353:
    {
        FText Error;
        if(!InputDraft.Draft.IsValid(Error)) InputMessage=Error.ToString();
        else if(!GI->ApplyInputPreferences(InputDraft.Draft)) InputMessage=TEXT("SAVE FAILED / LIVE INPUTS KEPT FOR THIS SESSION");
        else InputMessage=TEXT("INPUTS APPLIED AND SAVED");
        break;
    }
    case 354: if(!InputDraft.Swap()) InputMessage=TEXT("SWAP WOULD LEAVE AN ACTION UNBOUND"); break;
    case 355: InputDraft.CancelCapture(); break;
    case 356: InputScrollRow=FMath::Max(0,InputScrollRow-3);Focus=0;break;
    case 357: InputScrollRow=FMath::Min(FMath::Max(0,int32(EBootInputAction::Count)-8),InputScrollRow+3);Focus=0;break;
    }
}

FReply UBootShellWidget::NativeOnKeyUp(const FGeometry& G,const FKeyEvent& Event)
{ InputDraft.Release(Event.GetKey()); return Super::NativeOnKeyUp(G,Event); }
FReply UBootShellWidget::NativeOnMouseWheel(const FGeometry& G,const FPointerEvent& Event)
{
    if(Page!=EBootPage::Inputs||InputDraft.IsCapturing())return Super::NativeOnMouseWheel(G,Event);
    InputScrollRow=FMath::Clamp(InputScrollRow+(Event.GetWheelDelta()<0?3:-3),0,FMath::Max(0,int32(EBootInputAction::Count)-8));
    Focus=0;return FReply::Handled();
}
void UBootShellWidget::NativeOnFocusLost(const FFocusEvent& Event)
{ InputDraft.CancelCapture(); Drag=-1; Super::NativeOnFocusLost(Event); }

float UBootShellWidget::GetSlider(int32 Id) const
{
    switch(Id)
    {
    case 101:return Draft.Music; case 102:return Draft.Sfx;
    case 103:return (Draft.Sensitivity-.03f)/.37f; case 104:return (Draft.Fov-80)/30;
    case 105:return (Draft.InputOffsetMs+250)/500;
    default:return 0;
    }
}

void UBootShellWidget::SetSlider(int32 Id,float Value)
{
    Value=FMath::Clamp(Value,0.f,1.f);
    switch(Id)
    {
    case 101:Draft.Music=Value;break;case 102:Draft.Sfx=Value;break;
    case 103:Draft.Sensitivity=.03f+Value*.37f;break;case 104:Draft.Fov=80+Value*30;break;
    case 105:Draft.InputOffsetMs=FMath::RoundToFloat(-250+Value*500);break;
    }
}
