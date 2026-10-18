#include "Music/BootMusicDecoder.h"
#include "HAL/PlatformFileManager.h"
#include "String/BytesToHex.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <bcrypt.h>
#include <wrl/client.h>
#include "Windows/HideWindowsPlatformTypes.h"
using Microsoft::WRL::ComPtr;
namespace
{
struct FComMusicScope
{
    HRESULT Com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT Media = E_FAIL;
    FComMusicScope() { if (SUCCEEDED(Com)) Media = MFStartup(MF_VERSION, MFSTARTUP_LITE); }
    ~FComMusicScope() { if (SUCCEEDED(Media)) MFShutdown(); if (SUCCEEDED(Com)) CoUninitialize(); }
};
bool MediaError(FBootDecodeResult& R, const TCHAR* Message, HRESULT Code = E_FAIL)
{
    R.Error = FText::FromString(FString::Printf(TEXT("%s (0x%08x)"), Message, uint32(Code)));
    return false;
}
}
bool BootWindowsMusicHash(const FString& Path, const TAtomic<bool>& Cancel, FString& Out, FString& Error)
{
    BCRYPT_ALG_HANDLE Algorithm = nullptr;
    BCRYPT_HASH_HANDLE Hash = nullptr;
    auto Cleanup = [&]() { if (Hash) BCryptDestroyHash(Hash); if (Algorithm) BCryptCloseAlgorithmProvider(Algorithm, 0); };
    if (BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    { Error = TEXT("Cannot open SHA256 provider"); return false; }
    DWORD ObjectBytes = 0, Returned = 0;
    if (BCryptGetProperty(Algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&ObjectBytes), sizeof(ObjectBytes), &Returned, 0) < 0
        || ObjectBytes > 1024 * 1024)
    { Cleanup(); Error = TEXT("Invalid SHA256 provider buffer size"); return false; }
    TArray<uint8> Object; Object.SetNumUninitialized(ObjectBytes);
    if (BCryptCreateHash(Algorithm, &Hash, Object.GetData(), ObjectBytes, nullptr, 0, 0) < 0)
    { Cleanup(); Error = TEXT("Cannot create SHA256 hash"); return false; }
    TUniquePtr<IFileHandle> Source(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path));
    bool Ok = Source.IsValid();
    TArray<uint8> Block; Block.SetNumUninitialized(65536);
    int64 Remaining = Source ? Source->Size() : 0;
    while (Ok && Remaining > 0)
    {
        if (Cancel.Load()) { Ok = false; break; }
        const int32 Count = int32(FMath::Min<int64>(Remaining, Block.Num()));
        Ok = Source->Read(Block.GetData(), Count) && BCryptHashData(Hash, Block.GetData(), Count, 0) >= 0;
        Remaining -= Count;
    }
    uint8 Digest[32];
    if (Ok) Ok = BCryptFinishHash(Hash, Digest, sizeof(Digest), 0) >= 0;
    Cleanup();
    if (Ok) Out = BytesToHex(Digest, sizeof(Digest)).ToLower();
    else if (!Cancel.Load()) Error = TEXT("Cannot read/hash music source");
    return Ok;
}
bool BootWindowsDecodeMp3(const FBootDecodeRequest& Request, const TAtomic<bool>& Cancel,
    TFunctionRef<void(double)> Progress, FBootDecodeResult& Result)
{
    FComMusicScope Scope;
    if (FAILED(Scope.Com) || FAILED(Scope.Media)) return MediaError(Result, TEXT("Cannot initialize worker COM/Media Foundation"), FAILED(Scope.Com) ? Scope.Com : Scope.Media);
    ComPtr<IMFAttributes> Attributes;
    HRESULT Hr = MFCreateAttributes(&Attributes, 2);
    if (FAILED(Hr)) return MediaError(Result, TEXT("Cannot create decoder attributes"), Hr);
    Attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, 0);
    ComPtr<IMFSourceReader> Reader;
    Hr = MFCreateSourceReaderFromURL(*Request.SourcePath, Attributes.Get(), &Reader);
    if (FAILED(Hr)) return MediaError(Result, TEXT("Cannot open MP3 source"), Hr);
    ComPtr<IMFMediaType> Native;
    Hr = Reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &Native);
    UINT32 Channels = 0;
    if (FAILED(Hr) || FAILED(Native->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &Channels)) || Channels < 1 || Channels > 2)
        return MediaError(Result, TEXT("MP3 must contain mono or stereo audio"));
    PROPVARIANT Duration; PropVariantInit(&Duration);
    Hr = Reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &Duration);
    double Seconds = 0.;
    if (SUCCEEDED(Hr) && Duration.vt == VT_UI8) Seconds = double(Duration.uhVal.QuadPart) / 10000000.;
    PropVariantClear(&Duration);
    if (Seconds > 600.) return MediaError(Result, TEXT("MP3 exceeds 600 second duration budget"));
    ComPtr<IMFMediaType> Pcm;
    Hr = MFCreateMediaType(&Pcm);
    if (FAILED(Hr)) return MediaError(Result, TEXT("Cannot create PCM media type"), Hr);
    Pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    Pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    Pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, Channels);
    Pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    Pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    Pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, Channels * 2);
    Pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 48000 * Channels * 2);
    Reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, 0);
    Reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 1);
    Hr = Reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, Pcm.Get());
    if (FAILED(Hr)) return MediaError(Result, TEXT("Cannot negotiate 48kHz PCM16 MP3 output"), Hr);
    ComPtr<IMFMediaType> Actual;
    UINT32 ActualRate=0,ActualChannels=0,ActualBits=0,ActualAlignment=0;
    Hr=Reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,&Actual);
    if(FAILED(Hr)||FAILED(Actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&ActualRate))
        ||FAILED(Actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&ActualChannels))
        ||FAILED(Actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,&ActualBits))
        ||FAILED(Actual->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,&ActualAlignment))
        ||ActualRate!=48000||ActualChannels!=Channels||ActualBits!=16||ActualAlignment!=Channels*2)
        return MediaError(Result,TEXT("MP3 decoder did not provide canonical 48kHz PCM16"),Hr);
    IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
    TUniquePtr<IFileHandle> Output(Platform.OpenWrite(*Request.TempPcmPath));
    if (!Output) return MediaError(Result, TEXT("Cannot create canonical MP3 PCM temp"));
    int64 Frames = 0;
    int32 EmptyReads = 0;
    for (;;)
    {
        if (Cancel.Load()) { Result.bCancelled = true; return false; }
        DWORD Flags = 0;
        LONGLONG Timestamp = 0;
        ComPtr<IMFSample> Sample;
        Hr = Reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &Flags, &Timestamp, &Sample);
        if (FAILED(Hr) || (Flags & MF_SOURCE_READERF_ERROR)) return MediaError(Result, TEXT("MP3 source read failed"), Hr);
        if (Flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) return MediaError(Result, TEXT("MP3 format changed during decode"));
        if (Sample)
        {
            EmptyReads = 0;
            ComPtr<IMFMediaBuffer> Buffer;
            Hr = Sample->ConvertToContiguousBuffer(&Buffer);
            if (FAILED(Hr)) return MediaError(Result, TEXT("Cannot access decoded MP3 buffer"), Hr);
            DWORD Bytes = 0;
            Hr = Buffer->GetCurrentLength(&Bytes);
            if (FAILED(Hr) || Bytes % (Channels * 2) != 0 || Bytes > 4 * 1024 * 1024)
                return MediaError(Result, TEXT("Invalid or oversized MP3 output buffer"), Hr);
            const int64 NextFrames = Frames + Bytes / (Channels * 2);
            if (NextFrames > (128ll * 1024 * 1024) / (Channels * 2)) return MediaError(Result, TEXT("Canonical MP3 PCM exceeds 128 MiB budget"));
            if (NextFrames > 48000ll * 600) return MediaError(Result, TEXT("MP3 exceeds 600 second duration budget"));
            BYTE* Data = nullptr;
            Hr = Buffer->Lock(&Data, nullptr, nullptr);
            if (FAILED(Hr)) return MediaError(Result, TEXT("Cannot lock decoded MP3 buffer"), Hr);
            const bool Written = Output->Write(Data, Bytes);
            Buffer->Unlock();
            if (!Written) return MediaError(Result, TEXT("Cannot write decoded MP3 PCM"));
            Frames = NextFrames;
            Progress(.1 + .9 * FMath::Min(1., double(Frames) / (48000. * (Seconds > 0. ? Seconds : 600.))));
        }
        else if (++EmptyReads > 4096) return MediaError(Result, TEXT("MP3 decoder made no progress"));
        if (Flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    }
    if (Frames == 0 || !Output->Flush()) return MediaError(Result, TEXT("Empty or unwritable MP3 PCM"));
    Result.Frames = Frames; Result.Channels = int32(Channels);
    return true;
}
#endif



