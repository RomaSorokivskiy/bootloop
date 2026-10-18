#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "BootInputPreferences.generated.h"

UENUM()
enum class EBootInputAction : uint8 { Forward, Back, Left, Right, Jump, Slide, Snap, AirRedirect, Retry, CardOne, CardTwo, CardThree, BufferDash, Count };
UENUM()
enum class EBootWheelPolicy : uint8 { Off, Up, Down, Both };
UENUM()
enum class EBootBindingResult : uint8 { Applied, Conflict, Invalid };

USTRUCT()
struct BOOTLOOP_API FBootInputBinding
{
    GENERATED_BODY()
    UPROPERTY() FKey Primary;
    UPROPERTY() FKey Secondary;
};

USTRUCT()
struct BOOTLOOP_API FBootInputPreferences
{
    GENERATED_BODY()
    FBootInputPreferences();
    UPROPERTY() TArray<FBootInputBinding> Bindings;
    UPROPERTY() EBootWheelPolicy WheelPolicy = EBootWheelPolicy::Both;
    UPROPERTY() bool bInvertY = false;
    FKey GetKey(EBootInputAction Action, int32 Slot) const;
    void Sanitize();
    bool IsValid(FText& Error) const;
    EBootBindingResult Assign(EBootInputAction Action, int32 Slot, FKey Key, bool bSwap);
    void Clear(EBootInputAction Action, int32 Slot);
    FText Label(EBootInputAction Action) const;
    bool AcceptsWheel(float Axis) const;
};
