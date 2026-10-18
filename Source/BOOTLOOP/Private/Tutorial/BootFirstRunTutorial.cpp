#include "Tutorial/BootFirstRunTutorial.h"

FBootFirstRunTutorial::FBootFirstRunTutorial()
{ Outcomes.Init(EBootFirstRunOutcome::Pending,LessonCount); }
TArray<EBootFirstRunEvent> FBootFirstRunTutorial::Objectives(int32 Lesson)
{
    using E=EBootFirstRunEvent;
    switch(static_cast<EBootFirstRunLesson>(Lesson))
    {
    case EBootFirstRunLesson::MoveJump:return {E::Moved,E::Jumped};
    case EBootFirstRunLesson::WheelHop:return {E::ManualHop};
    case EBootFirstRunLesson::Slide:return {E::GroundSlide,E::AirSlideLanded};
    case EBootFirstRunLesson::WallrunKick:return {E::Wallrun,E::WallKick};
    case EBootFirstRunLesson::AirRedirect:return {E::AirRedirect};
    case EBootFirstRunLesson::WindowSurf:return {E::SurfCompleted};
    case EBootFirstRunLesson::Processes:return {E::ScanAvoided,E::PopupIntercepted};
    case EBootFirstRunLesson::CardsAbilities:return {E::CardChosen,E::DashUsed,E::GuardTriggered};
    case EBootFirstRunLesson::MusicFlow:return {E::MusicFlowAcknowledged};
    default:return {};
    }
}
bool FBootFirstRunTutorial::Accept(const FBootTutorialEvidence& E)
{
    if(IsComplete() || !Run.RunId.IsValid() || E.Run!=Run || E.Lesson!=CurrentLesson
        || E.Section!=CurrentSection || E.ActionSerial==0 || ConsumedSerials.Contains(E.ActionSerial)) return false;
    const auto Required=Objectives(CurrentLesson);
    if(!Required.IsValidIndex(Objective) || Required[Objective]!=E.Event) return false;
    ConsumedSerials.Add(E.ActionSerial);
    if(++Objective==Required.Num()) { Outcomes[CurrentLesson]=EBootFirstRunOutcome::Completed; Advance(); }
    return true;
}
void FBootFirstRunTutorial::ResetLesson(const FBootRunToken& Token,int32 Lesson,int32 Section)
{
    if(!Token.RunId.IsValid() || Lesson<0 || Lesson>=LessonCount || Section<1) return;
    if(Run.RunId!=Token.RunId) Outcomes.Init(EBootFirstRunOutcome::Pending,LessonCount);
    Run=Token; CurrentLesson=Lesson; CurrentSection=Section; Objective=0;
    Outcomes[Lesson]=EBootFirstRunOutcome::Pending; ConsumedSerials.Reset();
}
void FBootFirstRunTutorial::Advance()
{
    ++CurrentLesson; CurrentSection=CurrentSection==MAX_int32?1:CurrentSection+1;
    Objective=0; ConsumedSerials.Reset();
}
void FBootFirstRunTutorial::SkipLesson()
{
    if(IsComplete() || !Run.RunId.IsValid()) return;
    Outcomes[CurrentLesson]=EBootFirstRunOutcome::Skipped; Advance();
}
bool FBootFirstRunTutorial::IsComplete() const { return CurrentLesson>=LessonCount; }
EBootFirstRunOutcome FBootFirstRunTutorial::GetOutcome(int32 Lesson) const
{ return Outcomes.IsValidIndex(Lesson)?Outcomes[Lesson]:EBootFirstRunOutcome::Pending; }
FText FBootFirstRunTutorial::GetPrompt(const FBootInputPreferences& Inputs) const
{
    const auto Key=[&](EBootInputAction Action){return Inputs.Label(Action).ToString();};
    FString Prompt;
    switch(static_cast<EBootFirstRunLesson>(CurrentLesson))
    {
    case EBootFirstRunLesson::MoveJump:
        Prompt=Objective==0?FString::Printf(TEXT("LOOK with mouse. MOVE with %s / %s / %s / %s. Follow mint arrows."),*Key(EBootInputAction::Forward),*Key(EBootInputAction::Back),*Key(EBootInputAction::Left),*Key(EBootInputAction::Right))
            :FString::Printf(TEXT("JUMP with %s. Any beat timing works. The amber side lane is a safe bypass."),*Key(EBootInputAction::Jump));
        // Keep the next action visible while learning movement, too.
        if(Objective==0) Prompt+=FString::Printf(TEXT(" Then JUMP: %s."),*Key(EBootInputAction::Jump));
        break;
    case EBootFirstRunLesson::WheelHop:
        Prompt=Inputs.WheelPolicy==EBootWheelPolicy::Off?TEXT("WHEEL HOP is disabled in INPUTS. Enable wheel Up/Down/Both, or use SKIP LESSON.")
            :FString::Printf(TEXT("MANUAL HOP: jump (%s), release, then a fresh wheel %s pulse near landing. Each hop needs a new pulse; holding jump never repeats."),*Key(EBootInputAction::Jump),Inputs.WheelPolicy==EBootWheelPolicy::Up?TEXT("UP"):Inputs.WheelPolicy==EBootWheelPolicy::Down?TEXT("DOWN"):TEXT("UP / DOWN"));
        break;
    case EBootFirstRunLesson::Slide:
        Prompt=Objective==0?FString::Printf(TEXT("GROUND SLIDE: move, then %s under LOW MEMORY. Stand up in clear space."),*Key(EBootInputAction::Slide))
            :FString::Printf(TEXT("AIR / LANDING SLIDE: jump with %s, press %s in the air. Land on the broad mint pad to begin the slide."),*Key(EBootInputAction::Jump),*Key(EBootInputAction::Slide));
        break;
    case EBootFirstRunLesson::WallrunKick:
        Prompt=Objective==0?FString::Printf(TEXT("WALLRUN: jump (%s) beside the navy wall while moving along it. Keep moving forward."),*Key(EBootInputAction::Jump))
            :FString::Printf(TEXT("WALL KICK: press %s while wallrunning, then land. The open lower lane stays safe."),*Key(EBootInputAction::Jump));
        break;
    case EBootFirstRunLesson::AirRedirect:
        Prompt=FString::Printf(TEXT("ALT-TAB: jump (%s), aim a new direction, press %s in the air, then land. Works off beat."),*Key(EBootInputAction::Jump),*Key(EBootInputAction::AirRedirect));break;
    case EBootFirstRunLesson::WindowSurf:
        Prompt=FString::Printf(TEXT("WINDOW SURF: board the amber window, %s to SNAP, ride, then leave onto the raised mint landing. Perfect is optional."),*Key(EBootInputAction::Snap));break;
    case EBootFirstRunLesson::Processes:
        Prompt=Objective==0?TEXT("SCAN: amber warning precedes the red sweep. Jump or take the amber side lane; cross after the sweep clears.")
            :FString::Printf(TEXT("POPUP: jump (%s) toward the window, press %s before touching red, then land, ride and leave onto the mint landing."),*Key(EBootInputAction::Jump),*Key(EBootInputAction::Snap));break;
    case EBootFirstRunLesson::CardsAbilities:
        if(Objective==0) Prompt=FString::Printf(TEXT("LIVE CARDS: choose %s / %s / %s while moving. Music and movement continue; the offer waits for you."),*Key(EBootInputAction::CardOne),*Key(EBootInputAction::CardTwo),*Key(EBootInputAction::CardThree));
        else if(Objective==1) Prompt=FString::Printf(TEXT("TRIAL BUFFER DASH: jump with %s, aim, then press %s in the air. Trial cards and charges end when you leave LEARN."),*Key(EBootInputAction::Jump),*Key(EBootInputAction::BufferDash));
        else Prompt=TEXT("TRIAL PROCESS GUARD: approach the red SCAN after its amber warning. Your trial guard absorbs one contact. No score is at stake.");
        break;
    case EBootFirstRunLesson::MusicFlow:
        Prompt=FString::Printf(TEXT("Press %s to continue to MUSIC LIBRARY. MUSIC RUN has no finish line: songs and tempo may change, your run and cards continue. Enable/import tracks there; trial abilities reset."),*Key(EBootInputAction::Snap));break;
    default:Prompt=TEXT("FIRST RUN complete. Continue to MUSIC LIBRARY.");break;
    }
    return FText::FromString(Prompt);
}

