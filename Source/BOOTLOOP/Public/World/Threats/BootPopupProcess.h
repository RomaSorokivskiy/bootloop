#pragma once

#include "CoreMinimal.h"
#include "World/Interaction/BootSurfWindow.h"
#include "World/Threats/BootScanProcess.h"
#include "BootPopupProcess.generated.h"

/** One-way hostile window interception; capture preserves current position and accepted SNAP rating. */
UCLASS()
class BOOTLOOP_API ABootPopupProcess : public ABootSurfWindow
{
    GENERATED_BODY()
public:
    ABootPopupProcess();
    virtual void Tick(float DeltaSeconds) override;
    void ConfigurePopup(const FTransform& Frame, FVector LocalHostileEnd, FVector RideEnd,
        float Seconds, int32 Id, int32 Generation);
    virtual bool Activate(ACharacter* Rider, bool bInSynced) override;
    virtual void ResetWindow() override;
    virtual bool IsAvailable() const override;
    void ResetProcess(int32 Generation);
    void SetRunActive(bool bActive);
    bool IsHostile() const { return bHostile; }
    int32 GetGeneration() const { return RunGeneration; }
    FBootThreatContact OnThreatContact;
    FBootRunThreatContact OnRunThreatContact;
    void ConfigureTelegraph(double Seconds) { TelegraphSeconds=FMath::IsFinite(Seconds)?float(FMath::Max(.9,Seconds)):.95f; }
private:
    UPROPERTY() TObjectPtr<UStaticMeshComponent> HostilePanel;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HostileColor;
    UPROPERTY() TObjectPtr<UAudioComponent> PopupWarningAudio;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> HostileRail;
    FTransform PopupFrame;
    FVector HostileEnd = FVector::ZeroVector;
    FVector FriendlyEnd = FVector::ZeroVector;
    float OperationSeconds = 1.f;
    float HostileElapsed = 0.f;
    int32 PopupId = INDEX_NONE;
    int32 RunGeneration = 0;
    bool bHostile = true;
    bool bRunActive = true;
    bool bConfigured = false;
    bool bConverting = false;
    bool bSecondWarningSound = false;
    TMap<TWeakObjectPtr<ACharacter>, FVector> PreviousPlayers;
    struct FPendingContact { TWeakObjectPtr<ACharacter> Target; int32 Generation; uint64 Frame; TOptional<FBootOperationKey> Operation; };
    TArray<FPendingContact> PendingContacts;
    TSet<TWeakObjectPtr<ACharacter>> ContactedPlayers;
    float TelegraphSeconds=.95f;
    void SetHostileAppearance(bool bInHostile);
};
