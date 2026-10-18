#pragma once
#include "CoreMinimal.h"

/** Replace a closed sibling temp file without deleting the previous final first. */
struct BOOTLOOP_API FBootAtomicFile
{
    static bool Replace(const FString& TempPath,const FString& FinalPath);
};
