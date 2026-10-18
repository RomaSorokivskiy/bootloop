#pragma once
#include "Music/BootMusicLibrarySubsystem.h"
class BOOTLOOP_API FBootMusicLibraryStore
{
public:
 static constexpr int64 CacheBudget = 2ll * 1024 * 1024 * 1024;
 static bool Load(const FString& Root, TArray<FBootLibraryEntry>&, FText& Error);
 static bool Save(const FString& Root, const TArray<FBootLibraryEntry>&, FText& Error);
 static bool IsOwnedPath(const FString& Root, const FString& Path);
 /** Conservative Windows boundary: reject every existing reparse-point ancestor, including Root itself. */
 static bool IsSafePath(const FString& Path);
 static int64 ManagedBytes(const FString& Root);
 static bool CanReserve(const FString& Root, int64 AdditionalBytes);
 static void DeleteOwned(const FString& Root, const FString& Path);
};
