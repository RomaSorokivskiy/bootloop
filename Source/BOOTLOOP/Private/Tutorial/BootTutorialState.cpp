#include "Tutorial/BootTutorialState.h"
FBootTutorialState::FBootTutorialState() { Outcomes.Init(EBootLessonState::NotStarted,8); }
void FBootTutorialState::ActivateLesson(int32 Id)
{
    if(!Outcomes.IsValidIndex(Id-1)) return;
    CurrentLesson=Id; ObjectiveIndex=0;
    if(Outcomes[Id-1]==EBootLessonState::NotStarted) Outcomes[Id-1]=EBootLessonState::Active;
}
bool FBootTutorialState::NotifyObjective(EBootTutorialEvent Event,int32 SectionId,int32 InGeneration)
{
    if(SectionId!=CurrentLesson || InGeneration!=Generation || GetOutcome(CurrentLesson)!=EBootLessonState::Active) return false;
    const auto Required=Objectives(CurrentLesson);
    if(!Required.IsValidIndex(ObjectiveIndex) || Required[ObjectiveIndex]!=Event) return false;
    if(++ObjectiveIndex>=Required.Num()) Outcomes[CurrentLesson-1]=EBootLessonState::Completed;
    return true;
}
bool FBootTutorialState::SkipCurrent()
{
    if(GetOutcome(CurrentLesson)!=EBootLessonState::Active) return false;
    Outcomes[CurrentLesson-1]=EBootLessonState::Skipped; return true;
}
void FBootTutorialState::RestoreCurrentSection() { ++Generation; ObjectiveIndex=0; }
EBootLessonState FBootTutorialState::GetOutcome(int32 Id) const
{ return Outcomes.IsValidIndex(Id-1)?Outcomes[Id-1]:EBootLessonState::NotStarted; }
bool FBootTutorialState::IsComplete() const
{ return !Outcomes.Contains(EBootLessonState::NotStarted) && !Outcomes.Contains(EBootLessonState::Active); }
TArray<EBootTutorialEvent> FBootTutorialState::Objectives(int32 Lesson)
{
    using E=EBootTutorialEvent;
    switch(Lesson)
    {
    case 1:return {E::Move,E::JumpLanded}; case 2:return {E::SlideCompleted};
    case 3:return {E::ManualHop}; case 4:return {E::SurfCompleted};
    case 5:return {E::WallKickLanded}; case 6:return {E::AirRedirectLanded};
    case 7:return {E::ScannerPassed,E::PopupSurfCompleted}; case 8:return {E::ExitGate};
    default:return {};
    }
}
