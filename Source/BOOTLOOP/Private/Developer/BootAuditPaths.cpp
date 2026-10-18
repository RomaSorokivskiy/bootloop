#include "Developer/BootAuditPaths.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

bool FBootAuditPaths::IsV04Audit(const TCHAR* CommandLine)
{
#if !UE_BUILD_SHIPPING
    return FParse::Param(CommandLine, TEXT("BootloopV04Audit"));
#else
    return false;
#endif
}
bool FBootAuditPaths::IsIsolatedProfile(const TCHAR* CommandLine)
{
#if !UE_BUILD_SHIPPING
    return IsV04Audit(CommandLine) || FParse::Param(CommandLine, TEXT("BootloopV03Audit")) || FParse::Param(CommandLine, TEXT("BootloopSmoke"));
#else
    return false;
#endif
}
FString FBootAuditPaths::ProfileSlot(const TCHAR* CommandLine)
{
#if !UE_BUILD_SHIPPING
    if (IsV04Audit(CommandLine)) return TEXT("BootloopAudit_v04");
    if (FParse::Param(CommandLine, TEXT("BootloopV03Audit"))) return TEXT("BootloopAudit_v03");
    if (FParse::Param(CommandLine, TEXT("BootloopSmoke"))) return TEXT("BootloopSmoke_v2");
#endif
    return TEXT("BootloopProfile_v2");
}
FString FBootAuditPaths::LibraryRoot(const TCHAR* CommandLine)
{
#if !UE_BUILD_SHIPPING
    if (IsV04Audit(CommandLine)) return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("V04Audit/Library"));
    if (FParse::Param(CommandLine, TEXT("BootloopV03Audit"))) return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("V03Audit/Library"));
    if (FParse::Param(CommandLine, TEXT("BootloopSmoke"))) return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SmokeAudit/Library"));
#endif
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MusicLibrary"));
}
FString FBootAuditPaths::CacheRoot(const TCHAR* CommandLine)
{
    return FPaths::Combine(LibraryRoot(CommandLine), TEXT("Cache"));
}

