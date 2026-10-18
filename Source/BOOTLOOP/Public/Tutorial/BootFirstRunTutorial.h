#pragma once
#include "CoreMinimal.h"
#include "Run/BootRunTypes.h"
#include "Input/BootInputPreferences.h"

enum class EBootFirstRunLesson : uint8 { MoveJump, WheelHop, Slide, WallrunKick, AirRedirect, WindowSurf, Processes, CardsAbilities, MusicFlow };
enum class EBootFirstRunEvent : uint8 { Moved, Jumped, ManualHop, GroundSlide, AirSlideLanded, Wallrun, WallKick, AirRedirect, SurfCompleted, ScanAvoided, PopupIntercepted, CardChosen, DashUsed, GuardTriggered, MusicFlowAcknowledged };
enum class EBootFirstRunOutcome : uint8 { Pending, Completed, Skipped };
struct BOOTLOOP_API FBootTutorialEvidence
{
    FBootRunToken Run;
    int32 Lesson=0, Section=1;
    uint64 ActionSerial=0;
    EBootFirstRunEvent Event=EBootFirstRunEvent::Moved;
};
/** A section is an attempt epoch, not a proximity-selected route. Capture evidence at action start. */
struct BOOTLOOP_API FBootFirstRunTutorial
{
    static constexpr int32 LessonCount=9;
    FBootFirstRunTutorial();
    bool Accept(const FBootTutorialEvidence& Evidence);
    void ResetLesson(const FBootRunToken& Run,int32 Lesson,int32 Section);
    void SkipLesson();
    bool IsComplete() const;
    FText GetPrompt(const FBootInputPreferences& Inputs) const;
    int32 GetLesson() const { return CurrentLesson; }
    int32 GetSection() const { return CurrentSection; }
    int32 GetObjectiveIndex() const { return Objective; }
    const FBootRunToken& GetRunToken() const { return Run; }
    EBootFirstRunOutcome GetOutcome(int32 Lesson) const;
    static TArray<EBootFirstRunEvent> Objectives(int32 Lesson);
private:
    void Advance();
    FBootRunToken Run;
    int32 CurrentLesson=0,CurrentSection=1,Objective=0;
    TArray<EBootFirstRunOutcome> Outcomes;
    TSet<uint64> ConsumedSerials;
};
