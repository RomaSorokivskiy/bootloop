#include "Input/BootInputMappingComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
UBootInputMappingComponent::UBootInputMappingComponent() { PrimaryComponentTick.bCanEverTick = false; }
void UBootInputMappingComponent::EnsureActions()
{
    if (MappingContext) return;
    MappingContext = NewObject<UInputMappingContext>(this);
    MoveAction = NewObject<UInputAction>(this); MoveAction->ValueType = EInputActionValueType::Axis2D;
    MoveAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
    LookXAction = NewObject<UInputAction>(this); LookXAction->ValueType = EInputActionValueType::Axis1D;
    LookYAction = NewObject<UInputAction>(this); LookYAction->ValueType = EInputActionValueType::Axis1D;
    WheelAction = NewObject<UInputAction>(this); WheelAction->ValueType = EInputActionValueType::Axis1D;
    Actions.SetNum(static_cast<int32>(EBootInputAction::Count));
    for (int32 I=0; I<Actions.Num(); ++I) Actions[I] = I<4 ? MoveAction.Get() : NewObject<UInputAction>(this);
}
void UBootInputMappingComponent::Apply(const FBootInputPreferences& Preferences, APlayerController* Player)
{
    EnsureActions();
    if (AppliedPlayer.IsValid() && AppliedPlayer.Get()!=Player) RemoveFromPlayer(AppliedPlayer.Get());
    ULocalPlayer* Local=Player?Player->GetLocalPlayer():nullptr;
    auto* Subsystem=Local?Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr;
    auto* PlayerInput=Player?Cast<UEnhancedPlayerInput>(Player->PlayerInput):nullptr;
    FModifyContextOptions Deferred;
    Deferred.bIgnoreAllPressedKeysUntilRelease=true;
    FModifyContextOptions Immediate=Deferred; Immediate.bForceImmediately=true;
    if (Subsystem)
    {
        // Flush with this context absent; adding it again creates fresh held-key gates in UE5.8.
        Subsystem->RemoveMappingContext(MappingContext,Deferred);
        Subsystem->RequestRebuildControlMappings(Immediate,EInputMappingRebuildType::RebuildWithFlush);
    }
    FBootInputPreferences Safe=Preferences; Safe.Sanitize();
    MappingContext->UnmapAll();
    for (int32 I=0; I<static_cast<int32>(EBootInputAction::Count); ++I)
        for (int32 Slot=0; Slot<2; ++Slot)
        {
            FKey Key=Safe.GetKey(static_cast<EBootInputAction>(I),Slot);
            if (!Key.IsValid()) continue;
            FEnhancedActionKeyMapping& M=MappingContext->MapKey(Actions[I],Key);
            // The engine option gates boolean actions only. Preserve the same release contract for Move.
            M.bShouldBeIgnored=PlayerInput && PlayerInput->IsPressed(Key);
            if (I==static_cast<int32>(EBootInputAction::Back) || I==static_cast<int32>(EBootInputAction::Left))
                M.Modifiers.Add(NewObject<UInputModifierNegate>(MappingContext));
            if (I==static_cast<int32>(EBootInputAction::Forward) || I==static_cast<int32>(EBootInputAction::Back))
                M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(MappingContext));
        }
    MappingContext->MapKey(LookXAction,EKeys::MouseX);
    MappingContext->MapKey(LookYAction,EKeys::MouseY);
    // The sign/policy is consumed once by Runner; directional wheel buttons are never mapped.
    if (Safe.WheelPolicy!=EBootWheelPolicy::Off) MappingContext->MapKey(WheelAction,EKeys::MouseWheelAxis);
    if (Subsystem) { Subsystem->AddMappingContext(MappingContext,0,Immediate); AppliedPlayer=Player; }
}

void UBootInputMappingComponent::RemoveFromPlayer(APlayerController* Player)
{
    ULocalPlayer* Local=Player?Player->GetLocalPlayer():nullptr;
    if (auto* Subsystem=Local?Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr)
    {
        FModifyContextOptions Options; Options.bForceImmediately=true;
        if (MappingContext) Subsystem->RemoveMappingContext(MappingContext,Options);
    }
    if (AppliedPlayer.Get()==Player) AppliedPlayer.Reset();
}
UInputAction* UBootInputMappingComponent::GetAction(EBootInputAction Action) const
{
    const int32 Index = static_cast<int32>(Action);
    return Actions.IsValidIndex(Index) ? Actions[Index].Get() : nullptr;
}
