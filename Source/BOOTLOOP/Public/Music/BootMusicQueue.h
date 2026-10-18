#pragma once
#include "CoreMinimal.h"
#include "Music/BootMusicLibrarySubsystem.h"

/** Control-plane identity of an actual occupied stream request, never a speculative queue choice. */
struct FBootPendingPlaybackIdentity
{
 FString Hash;
 uint64 RequestId=0,LibraryRevision=0;
 bool bOccupied=false,bAllowed=false,bFailed=false,bCancelled=false,bPlaying=false,bRetired=false;
};

/** Game-thread shuffle bag. The bag survives selection edits; disabled hashes are discarded. */
class BOOTLOOP_API FBootMusicQueue
{
public:
 TOptional<FString> ChooseNext(const TArray<FBootLibraryEntry>& ReadySelected, const FString& CurrentHash, FRandomStream& Random);
 FString PeekPendingTrackHash(const TArray<FBootPendingPlaybackIdentity>&, const TArray<FBootLibraryEntry>&) const;
 void Reset();
private:
 TArray<FString> Bag;
};
