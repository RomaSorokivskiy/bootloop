#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Generation/BootEndlessTypes.h"
#include "World/Threats/BootScanProcess.h"
#include "BootEndlessChunk.generated.h"
class UInstancedStaticMeshComponent;
class UPrimitiveComponent;
class ABootSurfWindow;
class ABootScanProcess;
class ABootPopupProcess;
class ACharacter;

struct BOOTLOOP_API FBootEndlessReadiness
{
    static double RequiredDistanceCm(const FBootTraversalEnvelope&,double MeasuredPeakBaseCmps,double AssemblySeconds=1.2,double ReactionSeconds=.9);
    static double RailPeakCmps(double DistanceCm,double DurationSeconds);
};

UCLASS()
class BOOTLOOP_API ABootEndlessChunk : public AActor
{
    GENERATED_BODY()
public:
    ABootEndlessChunk();
    bool Configure(const FBootCommittedChunk&);
    bool FinishPreparation();
    bool IsReady() const { return bReady; }
    const FBootCommittedChunk& GetDescriptor() const { return Descriptor; }
    int32 GetGameplayChildCount() const;
    double GetPeakBaseCmps() const;
    bool OwnsSupport(const UPrimitiveComponent*) const;
    bool HasActiveRide() const;
    bool IsPreparedLanding(const ABootSurfWindow*,const ACharacter*) const;
    void SetProcessesActive(bool);
    void UpdateApproach(FVector PlayerPosition,const FBootTraversalEnvelope&);
    FBootRunThreatContact OnRunThreatContact;
    const TArray<TObjectPtr<ABootSurfWindow>>& GetWindows() const { return Windows; }
protected:
    virtual void EndPlay(const EEndPlayReason::Type) override;
private:
    FBootCommittedChunk Descriptor;
    bool bReady=false,bProcessesActive=true;
    TSet<int32> ArmedProcessIds;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Solids;
    UPROPERTY() TArray<TObjectPtr<ABootSurfWindow>> Windows;
    UPROPERTY() TArray<TObjectPtr<ABootScanProcess>> Scans;
    UPROPERTY() TArray<TObjectPtr<ABootPopupProcess>> Popups;
};

