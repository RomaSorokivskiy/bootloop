#include "Input/BootInputPreferences.h"
namespace
{
    constexpr int32 ActionCount = static_cast<int32>(EBootInputAction::Count);
    bool AllowedKey(FKey Key)
    {
        return Key.IsValid() && Key.IsBindableToActions() && !Key.IsGamepadKey() && !Key.IsAnalog()
            && Key != EKeys::Escape && Key != EKeys::MouseScrollUp && Key != EKeys::MouseScrollDown
            && Key != EKeys::MouseWheelAxis && !Key.IsTouch();
    }
    bool ValidAction(EBootInputAction Action) { return static_cast<int32>(Action) < ActionCount; }
    FKey& SlotKey(FBootInputBinding& Binding, int32 Slot) { return Slot == 0 ? Binding.Primary : Binding.Secondary; }
    bool ValidWheel(EBootWheelPolicy Policy) { return static_cast<uint8>(Policy) <= static_cast<uint8>(EBootWheelPolicy::Both); }
}

FBootInputPreferences::FBootInputPreferences()
{
    Bindings.SetNum(ActionCount);
    const FKey Primary[] = {EKeys::W,EKeys::S,EKeys::A,EKeys::D,EKeys::SpaceBar,EKeys::LeftControl,EKeys::E,EKeys::LeftShift,EKeys::R,EKeys::One,EKeys::Two,EKeys::Three,EKeys::F};
    static_assert(UE_ARRAY_COUNT(Primary)==ActionCount,"Every input action needs a default");
    for (int32 I=0; I<ActionCount; ++I) Bindings[I].Primary=Primary[I];
    Bindings[static_cast<int32>(EBootInputAction::Slide)].Secondary=EKeys::C;
    Bindings[static_cast<int32>(EBootInputAction::Snap)].Secondary=EKeys::RightMouseButton;
}
FKey FBootInputPreferences::GetKey(EBootInputAction Action, int32 Slot) const
{
    const int32 Index = static_cast<int32>(Action);
    if (!Bindings.IsValidIndex(Index) || Slot < 0 || Slot > 1) return FKey();
    return Slot == 0 ? Bindings[Index].Primary : Bindings[Index].Secondary;
}
void FBootInputPreferences::Sanitize()
{
    const FBootInputPreferences Defaults;
    if (!ValidWheel(WheelPolicy)) WheelPolicy=EBootWheelPolicy::Both;
    const int32 SavedCount=Bindings.Num();
    Bindings.SetNum(ActionCount);
    TSet<FKey> Used;
    // Reserve every good saved key before repairing empty actions; defaults may not steal custom keys.
    for (FBootInputBinding& Binding : Bindings)
        for (int32 Slot=0; Slot<2; ++Slot)
        {
            FKey& Key=SlotKey(Binding,Slot);
            if (!AllowedKey(Key) || Used.Contains(Key)) Key=FKey();
            else Used.Add(Key);
        }
    for (int32 I=FMath::Clamp(SavedCount,0,ActionCount); I<ActionCount; ++I)
        for (int32 Slot=0; Slot<2; ++Slot)
        {
            const FKey Key=Defaults.GetKey(static_cast<EBootInputAction>(I),Slot);
            if (AllowedKey(Key) && !Used.Contains(Key)) { SlotKey(Bindings[I],Slot)=Key; Used.Add(Key); }
        }
    for (int32 I=0; I<ActionCount; ++I)
    {
        FBootInputBinding& Binding=Bindings[I];
        if (Binding.Primary.IsValid() || Binding.Secondary.IsValid()
            || (I==static_cast<int32>(EBootInputAction::Jump) && WheelPolicy!=EBootWheelPolicy::Off)) continue;
        FKey Replacement;
        for (int32 Slot=0; Slot<2; ++Slot)
        {
            const FKey Candidate=Defaults.GetKey(static_cast<EBootInputAction>(I),Slot);
            if (AllowedKey(Candidate) && !Used.Contains(Candidate)) { Replacement=Candidate; break; }
        }
        if (!Replacement.IsValid())
        {
            // Deterministic fallback when a custom binding already uses this action's normal default.
            const FKey Fallbacks[]={EKeys::W,EKeys::S,EKeys::A,EKeys::D,EKeys::SpaceBar,EKeys::LeftControl,EKeys::E,EKeys::LeftShift,EKeys::R,
                EKeys::C,EKeys::F,EKeys::Q,EKeys::Z,EKeys::X,EKeys::V,EKeys::B,EKeys::N,EKeys::M,EKeys::J,EKeys::K,EKeys::L,EKeys::Up,EKeys::Down,EKeys::Left,EKeys::Right};
            for (FKey Candidate:Fallbacks) if (!Used.Contains(Candidate)) { Replacement=Candidate; break; }
        }
        Binding.Primary=Replacement; Used.Add(Replacement);
    }
}

bool FBootInputPreferences::IsValid(FText& Error) const
{
    Error=FText::GetEmpty();
    if (Bindings.Num()!=ActionCount || !ValidWheel(WheelPolicy))
    { Error=FText::FromString(TEXT("Invalid input preferences.")); return false; }
    TSet<FKey> Used;
    for (int32 I=0; I<ActionCount; ++I)
    {
        bool bBound=false;
        for (int32 Slot=0; Slot<2; ++Slot)
        {
            FKey Key=GetKey(static_cast<EBootInputAction>(I),Slot);
            if (Key==FKey()) continue;
            if (!AllowedKey(Key)) { Error=FText::FromString(TEXT("Choose a keyboard key or mouse button; Escape and wheel are reserved.")); return false; }
            if (Used.Contains(Key)) { Error=FText::FromString(TEXT("A key is assigned more than once.")); return false; }
            Used.Add(Key); bBound=true;
        }
        if (!bBound && !(I==static_cast<int32>(EBootInputAction::Jump) && WheelPolicy!=EBootWheelPolicy::Off))
        { Error=FText::FromString(TEXT("Every action needs a binding. Jump may use an enabled wheel policy.")); return false; }
    }
    return true;
}

EBootBindingResult FBootInputPreferences::Assign(EBootInputAction Action, int32 Slot, FKey Key, bool bSwap)
{
    if (!ValidAction(Action) || Slot<0 || Slot>1 || !Bindings.IsValidIndex(static_cast<int32>(Action)) || !AllowedKey(Key)) return EBootBindingResult::Invalid;
    const int32 Target=static_cast<int32>(Action);
    FBootInputPreferences Candidate=*this;
    const FKey Old=GetKey(Action,Slot);
    bool bCrossActionConflict=false;
    for (int32 I=0; I<Bindings.Num(); ++I)
        for (int32 S=0; S<2; ++S)
        {
            if (I==Target && S==Slot) continue;
            if (GetKey(static_cast<EBootInputAction>(I),S)!=Key) continue;
            if (I!=Target)
            {
                if (!bSwap) return EBootBindingResult::Conflict;
                if (bCrossActionConflict) return EBootBindingResult::Invalid;
                bCrossActionConflict=true;
                SlotKey(Candidate.Bindings[I],S)=Old;
            }
            else SlotKey(Candidate.Bindings[I],S)=FKey();
        }
    SlotKey(Candidate.Bindings[Target],Slot)=Key;
    FText Error;
    if (bCrossActionConflict && !Candidate.IsValid(Error)) return EBootBindingResult::Invalid;
    *this=MoveTemp(Candidate);
    return EBootBindingResult::Applied;
}

void FBootInputPreferences::Clear(EBootInputAction Action, int32 Slot)
{
    if (ValidAction(Action) && Slot>=0 && Slot<2 && Bindings.IsValidIndex(static_cast<int32>(Action)))
        SlotKey(Bindings[static_cast<int32>(Action)],Slot)=FKey();
}

FText FBootInputPreferences::Label(EBootInputAction Action) const
{
    TArray<FString> Parts;
    for (int32 Slot=0; Slot<2; ++Slot)
    {
        const FKey Key=GetKey(Action,Slot);
        if (AllowedKey(Key)) Parts.AddUnique(Key.GetDisplayName().ToString());
    }
    if (Action==EBootInputAction::Jump)
    {
        if (WheelPolicy==EBootWheelPolicy::Up) Parts.Add(TEXT("Wheel Up"));
        if (WheelPolicy==EBootWheelPolicy::Down) Parts.Add(TEXT("Wheel Down"));
        if (WheelPolicy==EBootWheelPolicy::Both) Parts.Add(TEXT("Wheel Up/Down"));
    }
    return FText::FromString(Parts.IsEmpty()?TEXT("Unbound"):FString::Join(Parts,TEXT(" / ")));
}

bool FBootInputPreferences::AcceptsWheel(float Axis) const
{
    return FMath::IsFinite(Axis) && ((Axis>0 && (WheelPolicy==EBootWheelPolicy::Up || WheelPolicy==EBootWheelPolicy::Both))
        || (Axis<0 && (WheelPolicy==EBootWheelPolicy::Down || WheelPolicy==EBootWheelPolicy::Both)));
}
