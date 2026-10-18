#include "Core/BootAtomicFile.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
bool FBootAtomicFile::Replace(const FString& TempPath,const FString& FinalPath)
{
    FString Temp=FPaths::ConvertRelativePathToFull(TempPath),Final=FPaths::ConvertRelativePathToFull(FinalPath);
    FPaths::NormalizeFilename(Temp);FPaths::NormalizeFilename(Final);
    FPaths::CollapseRelativeDirectories(Temp);FPaths::CollapseRelativeDirectories(Final);
    if(Temp.Equals(Final,ESearchCase::IgnoreCase)
        ||!FPaths::GetPath(Temp).Equals(FPaths::GetPath(Final),ESearchCase::IgnoreCase))return false;
#if PLATFORM_WINDOWS
    // The OS performs one replacement; failure retains the previous final.
    // FFileManagerGeneric::Move(Replace=true) deletes that final before renaming.
    return MoveFileExW(*Temp,*Final,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    return false; // v0.4 runtime music is Windows-only.
#endif
}
