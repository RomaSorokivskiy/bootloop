#pragma once
#include "CoreMinimal.h"

struct FBootRunToken
{
    FGuid RunId;
    uint32 Generation = 0;
    bool operator==(const FBootRunToken& Other) const { return RunId == Other.RunId && Generation == Other.Generation; }
    bool operator!=(const FBootRunToken& Other) const { return !(*this == Other); }
    friend uint32 GetTypeHash(const FBootRunToken& Token) { return HashCombine(GetTypeHash(Token.RunId), GetTypeHash(Token.Generation)); }
};
struct FBootOperationKey
{
    FBootRunToken Run;
    int64 ChunkId = 0;
    int32 LocalId = 0;
    bool operator==(const FBootOperationKey& Other) const { return Run == Other.Run && ChunkId == Other.ChunkId && LocalId == Other.LocalId; }
    friend uint32 GetTypeHash(const FBootOperationKey& Key) { return HashCombine(HashCombine(GetTypeHash(Key.Run), GetTypeHash(Key.ChunkId)), GetTypeHash(Key.LocalId)); }
};
struct FBootSafeBoundary
{
    FBootRunToken Run;
    int64 ChunkId = 0;
    uint32 PreparedRevision = 0;
    bool bSupported = false;
    bool bNoActiveCommitment = false;
    bool bFutureReady = false;
};
