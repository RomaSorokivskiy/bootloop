#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Input/BootInputPreferences.h"
#include "BootInputMappingComponent.generated.h"

class UInputAction;
class UInputMappingContext;
class APlayerController;

UCLASS(ClassGroup=(Input))
class BOOTLOOP_API UBootInputMappingComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBootInputMappingComponent();
    void EnsureActions();
    void Apply(const FBootInputPreferences& Preferences, APlayerController* Player);
    void RemoveFromPlayer(APlayerController* Player);
    UInputAction* GetMoveAction() const { return MoveAction; }
    UInputAction* GetLookXAction() const { return LookXAction; }
    UInputAction* GetLookYAction() const { return LookYAction; }
    UInputAction* GetWheelAction() const { return WheelAction; }
    UInputAction* GetAction(EBootInputAction Action) const;
    UInputMappingContext* GetMappingContext() const { return MappingContext; }
private:
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> MappingContext;
    UPROPERTY(Transient) TObjectPtr<UInputAction> MoveAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> LookXAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> LookYAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> WheelAction;
    UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> Actions;
    TWeakObjectPtr<APlayerController> AppliedPlayer;
};
