#pragma once
#include "CoreMinimal.h"

// Gates are local oriented rectangles; +X is the allowed crossing direction.
struct FBootCourseNode
{
    int32 Id = INDEX_NONE;
    FTransform Transform;
    float LengthCm = 1000.f, WidthCm = 1000.f;
    float ProgressStartMetres = 0.f, ProgressEndMetres = 10.f;
    FBox EntryBounds = FBox(FVector(-20,-500,-250), FVector(20,500,450));
    FBox ExitBounds = FBox(FVector(980,-500,-250), FVector(1020,500,450));
    TArray<int32> SuccessorIds;
    FTransform CheckpointTransform;
    int32 GroupId = 0, LessonId = 0;
};

struct FBootGateTransition { int32 FromId, ToId, Sequence; };

struct BOOTLOOP_API FBootCourseState
{
    void Initialize(const TArray<FBootCourseNode>& Nodes, int32 StartId, FVector Position);
    TArray<FBootGateTransition> Advance(const TArray<FBootCourseNode>& Nodes, FVector Previous, FVector Current);
    float GetProgressMetres() const { return ProgressMetres; }
    int32 GetGateSequence() const { return GateSequence; }
    bool WasGateCrossedAfter(int32 NodeId, int32 Sequence) const;
    bool IsFinished() const { return bFinished; }
    int32 CurrentNodeId = INDEX_NONE;
private:
    float ProgressMetres = 0.f;
    int32 GateSequence = 0;
    bool bFinished = false;
    TMap<int32,int32> CrossedAt;
};
