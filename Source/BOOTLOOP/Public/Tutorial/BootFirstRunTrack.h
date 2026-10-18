#pragma once
#include "CoreMinimal.h"
#include "World/Generation/BootTrack.h"
#include "Tutorial/BootFirstRunTutorial.h"
#include "BootFirstRunTrack.generated.h"
class ACharacter;
class UInstancedStaticMeshComponent;
class UTextRenderComponent;
class ABootScanProcess;
class ABootPopupProcess;

struct BOOTLOOP_API FBootFirstRunSection
{
    int32 Lesson=0;
    FName RouteName;
    FTransform Frame;
    FTransform Checkpoint;
    FBox LocalBounds=FBox(FVector(0,-1800,-250),FVector(5200,1800,950));
};
/** Nine independent, short local training routes. Movement evidence is supplied by the real Runner. */
UCLASS()
class BOOTLOOP_API ABootFirstRunTrack : public ABootTrack
{
    GENERATED_BODY()
public:
    ABootFirstRunTrack();
    virtual void ResetTrack(int32 Seed) override;
    virtual void Advance(double) override {}
    virtual void UpdateCourse(ABootRunner* Runner,float Dt) override;
    virtual bool IsFatal(FVector Center,float,float) const override { return Center.Z < -700.f; }
    virtual bool IsFinished(FVector) const override { return Tutorial.IsComplete(); }
    virtual int32 GetCourseSerial() const override { return Tutorial.GetSection(); }
    virtual float GetRouteProgress(FVector) const override { return Tutorial.GetLesson()*52.f; }
    virtual bool CanCompleteSurf(const ABootSurfWindow*,const ABootRunner*,int32 ActivationSection) const override;
    void BeginFirstRun(const FBootRunToken& Run);
    bool AcceptEvidence(const FBootTutorialEvidence& Evidence);
    FBootTutorialEvidence CaptureEvidence(EBootFirstRunEvent Event,uint64 Serial) const;
    bool IsInActiveLesson(const ABootRunner* Runner) const;
    FTransform GetStartTransform() const;
    FTransform PrepareCheckpoint(const FBootRunToken& Run,const ABootRunner* Runner=nullptr);
    FTransform SkipCurrentLesson(const FBootRunToken& Run,const ABootRunner* Runner=nullptr);
    void SetProcessesActive(bool bActive);
    const FBootFirstRunTutorial& GetTutorial() const { return Tutorial; }
    const TArray<FBootFirstRunSection>& GetSections() const { return Sections; }
protected:
    virtual void BeginPlay() override;
private:
    void BuildSections();
    void Box(FVector Position,FVector Size,int32 Color,int32 Collision=0);
    void Panel(FVector Origin,float From,float To,float Y=0.f,float Width=1600.f,float Z=0.f);
    void Sign(FVector Position,const FString& Text,float Size=50.f);
    void ResetProcesses();
    FTransform ValidatedCheckpoint(const FTransform&,const ABootRunner*) const;
    void ThreatContact(ACharacter* Character,int32 Generation,int64 ProcessId,bool bGuardLesson);
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Decor;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Floors;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Walls;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Beams;
    UPROPERTY() TObjectPtr<UMaterialInterface> TextMaterial;
    UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> Signs;
    UPROPERTY() TArray<TObjectPtr<AActor>> OwnedActors;
    UPROPERTY() TObjectPtr<ABootSurfWindow> Surf;
    UPROPERTY() TObjectPtr<ABootPopupProcess> Popup;
    UPROPERTY() TObjectPtr<ABootScanProcess> Scan;
    UPROPERTY() TObjectPtr<ABootScanProcess> GuardScan;
    FBootFirstRunTutorial Tutorial;
    TArray<FBootFirstRunSection> Sections;
    FVector PreviousPosition=FVector::ZeroVector;
    double MovedCm=0.;
    uint64 MovementSerial=1;
    int32 PreparedSection=0;
    bool bBuilt=false,bHistory=false,bProcessesActive=false,bScanArmed=false,bPopupArmed=false,bGuardArmed=false,bScanContact=false,bScanCleared=false;
};



