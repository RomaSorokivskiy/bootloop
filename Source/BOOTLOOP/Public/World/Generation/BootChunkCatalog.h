#pragma once
#include "World/Generation/BootEndlessTypes.h"
struct BOOTLOOP_API FBootChunkCatalog
{
    static constexpr uint32 Version = 1;
    static TArray<FBootChunkDefinition> Build();
    static bool Validate(const FBootChunkDefinition& Definition, FString& Error);
};
