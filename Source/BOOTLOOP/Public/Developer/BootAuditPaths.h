#pragma once
#include "CoreMinimal.h"

/** Explicit opt-in audit roots; no filesystem mutation and no mutable global override. */
struct BOOTLOOP_API FBootAuditPaths
{
    static bool IsV04Audit(const TCHAR* CommandLine);
    static bool IsIsolatedProfile(const TCHAR* CommandLine);
    static FString ProfileSlot(const TCHAR* CommandLine);
    static FString LibraryRoot(const TCHAR* CommandLine);
    static FString CacheRoot(const TCHAR* CommandLine);
};
