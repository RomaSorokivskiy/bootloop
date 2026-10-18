#pragma once
#include "CoreMinimal.h"
#include "Input/BootInputPreferences.h"

// Menu-only draft. Capturing never mutates the live profile or runtime context.
struct BOOTLOOP_API FBootInputCaptureState
{
    FBootInputPreferences Draft;
    int32 Action=INDEX_NONE, Slot=0;
    FKey OpeningKey, PendingKey;
    bool bArmed=false,bConflict=false;
    void Begin(EBootInputAction InAction,int32 InSlot,FKey Opener);
    void Release(FKey Key);
    EBootBindingResult Press(FKey Key,bool bRepeat);
    bool Swap();
    void CancelCapture();
    bool IsCapturing() const { return Action!=INDEX_NONE; }
};
