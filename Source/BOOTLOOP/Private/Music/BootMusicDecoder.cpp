#include "Music/BootMusicDecoder.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
bool BootWindowsMusicHash(const FString&, const TAtomic<bool>&, FString&, FString&);
bool BootWindowsDecodeMp3(const FBootDecodeRequest&, const TAtomic<bool>&, TFunctionRef<void(double)>, FBootDecodeResult&);
#endif
namespace
{
constexpr int64 MaxSourceBytes = 256ll * 1024 * 1024;
constexpr int64 MaxPcmBytes = 128ll * 1024 * 1024;
constexpr int64 MaxFrames = 48000ll * 600;
uint16 Read16(const uint8* P) { return uint16(P[0]) | (uint16(P[1]) << 8); }
uint32 Read32(const uint8* P) { return uint32(Read16(P)) | (uint32(Read16(P + 2)) << 16); }
bool Tag(const uint8* P, const ANSICHAR* S) { return FMemory::Memcmp(P, S, 4) == 0; }
bool Fail(FBootDecodeResult& R, const TCHAR* Message)
{
    R.Error = FText::FromString(Message);
    return false;
}
bool Budget(int64 Frames, int32 Channels, FBootDecodeResult& R)
{
    if (Frames <= 0 || Channels < 1 || Channels > 2)
        return Fail(R, TEXT("Invalid audio frame/channel count"));
    if (Frames > MaxPcmBytes / (Channels * 2))
        return Fail(R, TEXT("Canonical PCM exceeds 128 MiB budget"));
    if (Frames > MaxFrames)
        return Fail(R, TEXT("Audio exceeds 600 second duration budget"));
    return true;
}
// Fixed-size source cache: every source sample is visited, including samples skipped by downsampling.
class FWavSamples
{
public:
    FWavSamples(IFileHandle& InFile, int64 InOffset, int64 InBytes)
        : File(InFile), Position(InOffset), Remaining(InBytes) { Buffer.SetNumUninitialized(65536); }
    bool Read(uint8* Out, int32 Count)
    {
        for (int32 I = 0; I < Count; ++I)
        {
            if (Cursor == Available)
            {
                Available = int32(FMath::Min<int64>(Remaining, Buffer.Num()));
                if (!Available || !File.Seek(Position) || !File.Read(Buffer.GetData(), Available)) return false;
                Cursor = 0; Position += Available; Remaining -= Available;
            }
            Out[I] = Buffer[Cursor++];
        }
        return true;
    }
private:
    IFileHandle& File;
    int64 Position, Remaining;
    TArray<uint8> Buffer;
    int32 Cursor = 0, Available = 0;
};
bool DecodeWav(const FBootDecodeRequest& Request, const TAtomic<bool>& Cancel,
    TFunctionRef<void(double)> Progress, FBootDecodeResult& Result)
{
    IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
    TUniquePtr<IFileHandle> Source(Platform.OpenRead(*Request.SourcePath));
    uint8 Header[12];
    if (!Source || Source->Size() < 12 || !Source->Read(Header, 12)
        || !Tag(Header, "RIFF") || !Tag(Header + 8, "WAVE"))
        return Fail(Result, TEXT("Invalid RIFF/WAVE header"));
    const int64 End = int64(Read32(Header + 4)) + 8;
    if (End < 12 || End > Source->Size()) return Fail(Result, TEXT("Truncated RIFF container"));
    bool HasFormat = false, HasData = false;
    uint16 Format = 0, Channels = 0, Bits = 0, Alignment = 0;
    uint32 Rate = 0, ByteRate = 0;
    int64 DataOffset = 0, DataBytes = 0;
    for (int64 Offset = 12; Offset < End;)
    {
        if (Cancel.Load()) { Result.bCancelled = true; return false; }
        uint8 Chunk[8];
        if (End - Offset < 8 || !Source->Seek(Offset) || !Source->Read(Chunk, 8))
            return Fail(Result, TEXT("Truncated RIFF chunk header"));
        const int64 Size = Read32(Chunk + 4);
        const int64 Payload = Offset + 8;
        if (Size > End - Payload || (Size & 1) > End - Payload - Size)
            return Fail(Result, TEXT("Truncated or overflowing RIFF chunk"));
        if (Tag(Chunk, "fmt "))
        {
            uint8 Fmt[40] = {};
            if (HasFormat || Size < 16 || !Source->Read(Fmt, FMath::Min<int64>(Size, 40)))
                return Fail(Result, TEXT("Invalid WAV format chunk"));
            Format = Read16(Fmt); Channels = Read16(Fmt + 2); Rate = Read32(Fmt + 4);
            ByteRate = Read32(Fmt + 8); Alignment = Read16(Fmt + 12); Bits = Read16(Fmt + 14);
            if (Format == 0xfffe)
            {
                const uint8 GuidTail[12] = {0,0,16,0,128,0,0,170,0,56,155,113};
                if (Size < 40 || Read16(Fmt + 16) < 22 || Read16(Fmt + 18) != Bits
                    || FMemory::Memcmp(Fmt + 28, GuidTail, 12) != 0 || Read32(Fmt + 24) > 3)
                    return Fail(Result, TEXT("Unsupported extensible WAV format"));
                Format = uint16(Read32(Fmt + 24));
            }
            HasFormat = true;
        }
        else if (Tag(Chunk, "data"))
        {
            if (HasData) return Fail(Result, TEXT("Multiple WAV data chunks are unsupported"));
            DataOffset = Payload; DataBytes = Size; HasData = true;
        }
        Offset = Payload + Size + (Size & 1);
    }
    if (!HasFormat || !HasData || Channels < 1 || Channels > 2 || Rate == 0 || Rate > 768000
        || !((Format == 1 && (Bits == 16 || Bits == 24 || Bits == 32)) || (Format == 3 && Bits == 32))
        || Alignment != Channels * (Bits / 8) || uint64(ByteRate) != uint64(Rate) * Alignment
        || DataBytes == 0 || DataBytes % Alignment != 0)
        return Fail(Result, TEXT("Unsupported or inconsistent WAV format (PCM16/24/32 or float32 mono/stereo required)"));
    const int64 SourceFrames = DataBytes / Alignment;
    const int64 OutputFrames = (SourceFrames * 48000ll) / Rate;
    if (!Budget(OutputFrames, Channels, Result)) return false;
    // Check source duration too: integer output rounding must not admit >600 seconds.
    if (SourceFrames > int64(Rate) * 600) return Fail(Result, TEXT("Audio exceeds 600 second duration budget"));
    TUniquePtr<IFileHandle> Output(Platform.OpenWrite(*Request.TempPcmPath));
    if (!Output) return Fail(Result, TEXT("Cannot create canonical PCM temp file"));
    FWavSamples Samples(*Source, DataOffset, DataBytes);
    auto ReadFrame = [&](double* Values)
    {
        for (int32 C = 0; C < Channels; ++C)
        {
            uint8 Bytes[4] = {};
            if (!Samples.Read(Bytes, Bits / 8)) return false;
            if (Format == 3)
            {
                float V; FMemory::Memcpy(&V, Bytes, 4);
                if (!FMath::IsFinite(V)) return false;
                Values[C] = FMath::Clamp(double(V), -1., 1.);
            }
            else if (Bits == 16) Values[C] = double(int16(Read16(Bytes))) / 32768.;
            else if (Bits == 24)
            {
                int32 V = int32(Bytes[0]) | (int32(Bytes[1]) << 8) | (int32(Bytes[2]) << 16);
                if (V & 0x800000) V |= int32(0xff000000);
                Values[C] = double(V) / 8388608.;
            }
            else Values[C] = double(int32(Read32(Bytes))) / 2147483648.;
        }
        return true;
    };
    TArray<uint8> Block; Block.Reserve(32768);
    auto Flush = [&]() { const bool Ok = Block.IsEmpty() || Output->Write(Block.GetData(), Block.Num()); Block.Reset(); return Ok; };
    double Previous[2] = {}, Next[2] = {};
    if (!ReadFrame(Previous)) return Fail(Result, TEXT("Truncated WAV or nonfinite float sample"));
    int64 Written = 0;
    for (int64 Input = 0; Input < SourceFrames; ++Input)
    {
        if (Cancel.Load()) { Result.bCancelled = true; return false; }
        if (Input + 1 < SourceFrames)
        {
            if (!ReadFrame(Next)) return Fail(Result, TEXT("Truncated WAV or nonfinite float sample"));
        }
        else { Next[0] = Previous[0]; Next[1] = Previous[1]; }
        while (Written < OutputFrames && (Written * int64(Rate)) / 48000 <= Input)
        {
            const double Position = double(Written) * double(Rate) / 48000.;
            const double Fraction = FMath::Clamp(Position - double(Input), 0., 1.);
            for (int32 C = 0; C < Channels; ++C)
            {
                const double V = FMath::Lerp(Previous[C], Next[C], Fraction);
                const int32 S = FMath::Clamp(FMath::RoundToInt(V * 32768.), -32768, 32767);
                Block.Add(uint8(S & 255)); Block.Add(uint8((S >> 8) & 255));
            }
            ++Written;
            if (Block.Num() >= 32768 && !Flush()) return Fail(Result, TEXT("Cannot write canonical PCM"));
        }
        Previous[0] = Next[0]; Previous[1] = Next[1];
        if ((Input & 4095) == 0) Progress(.1 + .9 * double(Input) / double(SourceFrames));
    }
    if (!Flush() || !Output->Flush()) return Fail(Result, TEXT("Cannot flush canonical PCM"));
    Result.Frames = Written; Result.Channels = Channels;
    return Written == OutputFrames;
}
class FBootMusicDecoder final : public IBootMusicDecoder
{
public:
    FBootDecodeResult Decode(const FBootDecodeRequest& Request, const TAtomic<bool>& Cancel,
        TFunctionRef<void(double)> Progress) override
    {
        FBootDecodeResult Result; Result.JobId = Request.JobId;
        IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
        FString SourcePath = FPaths::ConvertRelativePathToFull(Request.SourcePath);
        FString TempPath = FPaths::ConvertRelativePathToFull(Request.TempPcmPath);
        FPaths::NormalizeFilename(SourcePath); FPaths::CollapseRelativeDirectories(SourcePath);
        FPaths::NormalizeFilename(TempPath); FPaths::CollapseRelativeDirectories(TempPath);
        if (Request.SourcePath.IsEmpty() || Request.TempPcmPath.IsEmpty() || SourcePath.Equals(TempPath, ESearchCase::IgnoreCase))
        { Fail(Result, TEXT("Source and temp output paths must be distinct")); return Result; }
        FString SavedPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
        FPaths::NormalizeDirectoryName(SavedPath);
        FPaths::CollapseRelativeDirectories(SavedPath);
        if (!FPaths::IsUnderDirectory(TempPath, SavedPath)
            || !FPaths::GetExtension(TempPath).Equals(TEXT("pcm"), ESearchCase::IgnoreCase))
        {
            Fail(Result, TEXT("Canonical PCM temp must be a .pcm file under the project's Saved directory"));
            return Result;
        }
        // TempPcmPath is now validated job scratch; discard a previous partial job before retrying.
        if (Platform.FileExists(*TempPath)) Platform.DeleteFile(*TempPath);
        if (Cancel.Load()) { Result.bCancelled = true; return Result; }
        const int64 Size = Platform.FileSize(*SourcePath);
        if (Size <= 0 || Size > MaxSourceBytes)
        { Fail(Result, TEXT("Source is missing/empty or exceeds 256 MiB budget")); return Result; }
        FBootDecodeRequest Normalized = Request; Normalized.SourcePath = SourcePath; Normalized.TempPcmPath = TempPath;
        bool Ok = false;
#if PLATFORM_WINDOWS
        FString HashError;
        if (!BootWindowsMusicHash(SourcePath, Cancel, Result.ContentHash, HashError))
        {
            Result.bCancelled = Cancel.Load();
            if (!Result.bCancelled) Result.Error = FText::FromString(HashError);
            return Result;
        }
        Progress(.1);
        const FString Extension = FPaths::GetExtension(SourcePath).ToLower();
        if (Extension == TEXT("wav")) Ok = DecodeWav(Normalized, Cancel, Progress, Result);
        else if (Extension == TEXT("mp3")) Ok = BootWindowsDecodeMp3(Normalized, Cancel, Progress, Result);
        else Fail(Result, TEXT("Only WAV and MP3 sources are supported"));
#else
        Fail(Result, TEXT("Runtime music decoding is only supported on Windows"));
#endif
        if (Cancel.Load()) { Ok = false; Result.bCancelled = true; }
        if (!Ok)
        {
            Platform.DeleteFile(*TempPath);
            Result.PcmPath.Reset(); Result.Frames = 0; Result.Channels = 0;
            if (!Result.bCancelled && Result.Error.IsEmpty()) Fail(Result, TEXT("Audio decode failed"));
        }
        else { Result.PcmPath = TempPath; Progress(1.); }
        return Result;
    }
};
}
TUniquePtr<IBootMusicDecoder> IBootMusicDecoder::Create() { return MakeUnique<FBootMusicDecoder>(); }


