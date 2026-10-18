#pragma once
#include "CoreMinimal.h"
struct FBootDecodeRequest { FString SourcePath, TempPcmPath; uint64 JobId = 0; };
struct FBootDecodeResult { uint64 JobId = 0; FString ContentHash, PcmPath; int64 Frames = 0; int32 Channels = 0; FText Error; bool bCancelled = false; };
class BOOTLOOP_API IBootMusicDecoder
{
public:
 static constexpr uint32 SettingsVersion = 1;
 virtual ~IBootMusicDecoder() = default;
 virtual FBootDecodeResult Decode(const FBootDecodeRequest&, const TAtomic<bool>& Cancel, TFunctionRef<void(double)> Progress) = 0;
 static TUniquePtr<IBootMusicDecoder> Create();
};

