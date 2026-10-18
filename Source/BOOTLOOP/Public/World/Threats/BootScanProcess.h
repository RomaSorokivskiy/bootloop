#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Run/BootRunTypes.h"
#include "BootScanProcess.generated.h"

class ACharacter;
class UBoxComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;
class UAudioComponent;
DECLARE_MULTICAST_DELEGATE_TwoParams(FBootThreatContact, ACharacter*, int32);
DECLARE_MULTICAST_DELEGATE_TwoParams(FBootRunThreatContact,ACharacter*,const FBootOperationKey&);

enum class EBootScanPhase : uint8 { Dormant, Warning, Sweep, Clear };

/** A visible, finite warning/sweep/clear threat. Callers own consequences and generation validation. */
UCLASS()
class BOOTLOOP_API ABootScanProcess : public AActor
{
    GENERATED_BODY()
public:
    ABootScanProcess();
    virtual void Tick(float DeltaSeconds) override;
    void Configure(const FTransform& Frame, FVector LocalSweepEnd, int32 GroupId);
    void Arm(int32 Generation);
    void ResetProcess(int32 Generation);
    void SetRunActive(bool bActive);
    EBootScanPhase GetPhase() const { return Phase; }
    int32 GetGeneration() const { return RunGeneration; }
    int32 GetGroupId() const { return ScanGroupId; }
    FVector GetBeamLocation() const;
    FBootThreatContact OnThreatContact;
    FBootRunThreatContact OnRunThreatContact;
    void SetRunOperation(const FBootOperationKey& Key) { RunOperation=Key; }
    const FBootOperationKey* GetRunOperation() const { return RunOperation.IsSet()?&RunOperation.GetValue():nullptr; }
    void ConfigureSweepDuration(double Seconds) { SweepDurationSeconds=FMath::IsFinite(Seconds)?float(FMath::Clamp(Seconds,1.,10.)):1.f; }
    void ConfigureTelegraph(double Seconds) { TelegraphSeconds=FMath::IsFinite(Seconds)?float(FMath::Max(.9,Seconds)):.95f; }

    /** Continuous relative translation of a yaw-oriented box and an upright character capsule. */
    static bool SweptCapsuleContact(FVector BeamBefore, FVector BeamAfter, FQuat Yaw,
        FVector HalfExtent, FVector PlayerBefore, FVector PlayerAfter, float Radius, float HalfHeight);
protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY() TObjectPtr<UBoxComponent> Beam;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Color;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WarningRoute;
    UPROPERTY() TObjectPtr<UAudioComponent> WarningAudio;
    FTransform ScanFrame;
    FVector SweepEnd = FVector(1200, 0, 0);
    TMap<TWeakObjectPtr<ACharacter>, FVector> PreviousPlayers;
    TSet<TWeakObjectPtr<ACharacter>> ContactedPlayers;
    EBootScanPhase Phase = EBootScanPhase::Dormant;
    float PhaseSeconds = 0.f;
    int32 RunGeneration = 0;
    int32 ScanGroupId = INDEX_NONE;
    bool bRunActive = true;
    bool bSecondWarningSound = false;
    float TelegraphSeconds=.95f;
    float SweepDurationSeconds=1.f;
    TOptional<FBootOperationKey> RunOperation;
    void UpdateAppearance();
};

