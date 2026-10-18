#pragma once
#include "CoreMinimal.h"

enum class EBootLessonState : uint8 { NotStarted, Active, Completed, Skipped };
enum class EBootTutorialEvent : uint8
{
    Move, JumpLanded, SlideCompleted, ManualHop, SurfCompleted, WallKickLanded,
    AirRedirectLanded, ScannerPassed, PopupSurfCompleted, ExitGate
};

struct BOOTLOOP_API FBootTutorialState
{
    FBootTutorialState();
    void ActivateLesson(int32 Id);
    bool NotifyObjective(EBootTutorialEvent Event, int32 SectionId, int32 InGeneration);
    bool SkipCurrent();
    void RestoreCurrentSection();
    EBootLessonState GetOutcome(int32 Id) const;
    bool IsComplete() const;
    int32 CurrentLesson = 1, Generation = 1, ObjectiveIndex = 0;
    FTransform CurrentCheckpoint;
    TArray<EBootLessonState> Outcomes;
    static TArray<EBootTutorialEvent> Objectives(int32 Lesson);
};
