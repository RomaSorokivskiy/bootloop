#pragma once
#include "CoreMinimal.h"
#include "World/Generation/BootTrack.h"
#include "World/Generation/BootCourseTypes.h"
#include "Tutorial/BootTutorialState.h"
#include "BootCourseTrack.generated.h"

class UInstancedStaticMeshComponent;
class UTextRenderComponent;
class ABootAssemblingPlatform;
class ABootScanProcess;
class ABootPopupProcess;

UCLASS()
class BOOTLOOP_API ABootCourseTrack : public ABootTrack
{
    GENERATED_BODY()
public:
    ABootCourseTrack();
    void SetTraining(bool bValue) { bTraining=bValue; }
    virtual void ResetTrack(int32 Seed) override;
    virtual void Advance(double) override {}
    virtual void UpdateCourse(ABootRunner* Runner,float Dt) override;
    virtual bool IsFatal(FVector Center,float Radius,float HalfHeight) const override { return Center.Z < -700.f; }
    virtual float GetRouteProgress(FVector) const override { return Course.GetProgressMetres(); }
    virtual bool IsFinished(FVector) const override { return Course.IsFinished() && (!bTraining || Tutorial.IsComplete()); }
    virtual int32 GetCourseSerial() const override { return Course.GetGateSequence(); }
    virtual bool CanCompleteSurf(const ABootSurfWindow* Window,const ABootRunner* Runner,int32 ActivationGateSequence) const override;
    const TArray<FBootCourseNode>& GetNodes() const { return Nodes; }
    const FBootCourseState& GetCourseState() const { return Course; }
    const FBootTutorialState& GetTutorial() const { return Tutorial; }
    FTransform GetStartTransform() const;
    FTransform PrepareCheckpoint(int32 Generation,const ABootRunner* RestoringRunner=nullptr);
    FTransform SkipLesson(int32 Generation,const ABootRunner* RestoringRunner=nullptr);
    void NotifyObjective(EBootTutorialEvent Event,int32 Generation);
    /** Accepted technique starts must be inside the currently active lesson's physical section. */
    bool IsInActiveLesson(const ABootRunner* Runner) const;
    void NotifyObjective(EBootTutorialEvent Event,int32 Generation,int32 OriginLesson,int32 OriginSection);
    FString GetObjective() const;
    void SetProcessesActive(bool bActive);
    int32 GetSectionId() const { return bTraining?Tutorial.CurrentLesson:Course.CurrentNodeId; }
protected:
    virtual void BeginPlay() override;
private:
    void BuildCourse(int32 Seed);
    void AddNode(int32 Id,FVector Origin,float Yaw,float Length,float From,float To,TArray<int32> Next,int32 Lesson=0);
    void Box(const FTransform& Frame,FVector Local,FVector Size,int32 Color,int32 Collision=0);
    void Panel(const FBootCourseNode& N,float A,float B,float Y=0.f,float Width=1400.f,float Z=0.f);
    void Sign(const FTransform& Frame,FVector P,const FString& Text,float Size=54.f);
    void PrepareNearby(FVector P,bool bInstant);
    FTransform ValidatedCheckpoint(const FTransform& Candidate,const ABootRunner* RestoringRunner) const;
    void AddCheckpointThreatSweep(const FTransform& Frame,FVector LocalEnd,FVector HalfExtent);
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Tiles;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Floors;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Walls;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Beams;
    UPROPERTY() TObjectPtr<UMaterialInterface> TextMaterial;
    UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> Signs;
    UPROPERTY() TArray<TObjectPtr<AActor>> OwnedActors;
    TArray<FBootCourseNode> Nodes;
    FBootCourseState Course;
    FBootTutorialState Tutorial;
    TMap<int32,int32> SurfDestinations;
    TMap<int32,int32> ActorGroups;
    // Authored envelopes persist while the processes are dormant/reset/paused.
    TArray<TPair<FTransform,FBox>> CheckpointThreatSweeps;
    FVector PreviousPosition=FVector::ZeroVector;
    bool bHistoryValid=false, bTraining=false, bBuilt=false;
    int32 AttemptGeneration=1;
    float SectionMovedCm=0.f;
    bool bProcessesActive=false;
    TSet<uint32> ArmedProcesses;
    UPROPERTY() TArray<TObjectPtr<ABootAssemblingPlatform>> AssemblingPlatforms;
};
