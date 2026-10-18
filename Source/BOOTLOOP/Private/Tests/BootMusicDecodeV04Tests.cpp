#include "Misc/AutomationTest.h"
#include "Music/BootMusicDecoder.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Async/Async.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
void U16(TArray<uint8>& B, uint16 V) { B.Add(V & 255); B.Add(V >> 8); }
void U32(TArray<uint8>& B, uint32 V) { U16(B, V & 65535); U16(B, V >> 16); }
void Tag(TArray<uint8>& B, const ANSICHAR* S) { for (int I = 0; I < 4; ++I) B.Add(S[I]); }
FString AuditDir()
{
    const FString D = FPaths::ProjectSavedDir() / TEXT("V04Audit/Decode");
    IFileManager::Get().MakeDirectory(*D, true);
    return D;
}
TArray<uint8> Wav(int Bits, int Channels, int Rate, int Frames, bool Float = false)
{
    const int Bytes = Frames * Channels * (Bits / 8);
    TArray<uint8> B;
    Tag(B, "RIFF"); U32(B, 36 + Bytes); Tag(B, "WAVE");
    Tag(B, "fmt "); U32(B, 16); U16(B, Float ? 3 : 1); U16(B, Channels);
    U32(B, Rate); U32(B, Rate * Channels * (Bits / 8));
    U16(B, Channels * (Bits / 8)); U16(B, Bits);
    Tag(B, "data"); U32(B, Bytes); B.AddZeroed(Bytes);
    return B;
}
FString SaveWav(const FString& Name, const TArray<uint8>& Bytes)
{
    const FString P = AuditDir() / Name;
    FFileHelper::SaveArrayToFile(Bytes, *P);
    return P;
}
FString Fixture(int Bits, int Channels, int Rate, bool Float = false)
{
    return SaveWav(FString::Printf(TEXT("fixture-%d-%d-%d-%d.wav"), Bits, Channels, Rate, Float),
        Wav(Bits, Channels, Rate, Rate, Float));
}
FBootDecodeResult Run(const FString& Path, bool Cancel = false)
{
    FBootDecodeRequest R;
    R.SourcePath = Path;
    R.TempPcmPath = AuditDir() / FString::Printf(TEXT("decode-%u.pcm"), GetTypeHash(Path));
    return Async(EAsyncExecution::ThreadPool, [R, Cancel]()
    {
        TAtomic<bool> C(Cancel);
        return IBootMusicDecoder::Create()->Decode(R, C, [](double) {});
    }).Get();
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootWavContracts, "Bootloop.V04.Decode.WavContracts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootWavContracts::RunTest(const FString&)
{
    for (int B : {16, 24, 32}) for (int C : {1, 2}) for (int R : {44100, 48000})
    {
        const auto V = Run(Fixture(B, C, R));
        TestTrue(TEXT("valid PCM"), V.Error.IsEmpty());
        TestEqual(TEXT("frames at 48k"), V.Frames, int64(48000));
        TestEqual(TEXT("channels retained"), V.Channels, C);
        TestEqual(TEXT("SHA256 length"), V.ContentHash.Len(), 64);
        TestEqual(TEXT("canonical byte count"), IFileManager::Get().FileSize(*V.PcmPath), int64(48000 * C * 2));
    }
    for (int C : {1, 2})
    {
        const auto V = Run(Fixture(32, C, 44100, true));
        TestEqual(TEXT("float frames"), V.Frames, int64(48000));
        TestTrue(TEXT("valid float PCM"), V.Error.IsEmpty());
    }
    // Nonzero signed extrema and stereo ordering ensure conversion is tested, rather than silence alone.
    auto Bytes = Wav(24, 2, 48000, 2);
    Bytes[44] = 255; Bytes[45] = 255; Bytes[46] = 127;
    Bytes[47] = 0; Bytes[48] = 0; Bytes[49] = 128;
    const auto V = Run(SaveWav(TEXT("extrema24.wav"), Bytes));
    TArray<uint8> Pcm; FFileHelper::LoadFileToArray(Pcm, *V.PcmPath);
    TestTrue(TEXT("24-bit stereo extrema preserved"), Pcm.Num() == 8 && Pcm[0] == 255 && Pcm[1] == 127 && Pcm[2] == 0 && Pcm[3] == 128);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootCorrupt, "Bootloop.V04.Decode.CorruptAndBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootCorrupt::RunTest(const FString&)
{
    const FString CorruptPath = AuditDir() / TEXT("corrupt.wav");
    FFileHelper::SaveStringToFile(TEXT("RIFFbroken"), *CorruptPath);
    const auto Corrupt = Run(CorruptPath);
    TestFalse(TEXT("invalid RIFF"), Corrupt.Error.IsEmpty());
    // All header fields agree: these exercise distinct limits using tiny, valid low-rate sources.
    const auto Long = Run(SaveWav(TEXT("601seconds.wav"), Wav(16, 1, 1, 601)));
    TestTrue(TEXT("valid source duration budget"), Long.Error.ToString().Contains(TEXT("600 second")));
    const auto LargePcm = Run(SaveWav(TEXT("701seconds-stereo.wav"), Wav(16, 2, 1, 701)));
    TestTrue(TEXT("canonical PCM preallocation budget"), LargePcm.Error.ToString().Contains(TEXT("128 MiB")));
    const FString HugePath = AuditDir() / TEXT("source-overbudget.wav");
    IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
    {
        TUniquePtr<IFileHandle> File(Platform.OpenWrite(*HugePath));
        const uint8 Byte = 0;
        const bool Made = File && File->Seek(256ll * 1024 * 1024) && File->Write(&Byte, 1);
        TestTrue(TEXT("logical source budget fixture created"), Made);
    }
    const auto Huge = Run(HugePath);
    TestTrue(TEXT("source preallocation budget"), Huge.Error.ToString().Contains(TEXT("256 MiB")));
    Platform.DeleteFile(*HugePath);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootCancel, "Bootloop.V04.Decode.Cancel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootCancel::RunTest(const FString&)
{
    const auto Cancelled = Run(Fixture(16, 1, 48000), true);
    TestTrue(TEXT("cancel"), Cancelled.bCancelled);
    TestTrue(TEXT("cancel has no PCM result"), Cancelled.PcmPath.IsEmpty());
    // Cancel after hash/progress and after output opens: cleanup must also work mid-stream.
    FBootDecodeRequest R;
    R.SourcePath = Fixture(16, 2, 48000); R.TempPcmPath = AuditDir() / TEXT("mid-cancel.pcm");
    TAtomic<bool> C(false);
    const auto Mid = IBootMusicDecoder::Create()->Decode(R, C, [&](double P) { if (P > .2) C.Store(true); });
    TestTrue(TEXT("mid-stream cancel"), Mid.bCancelled);
    TestFalse(TEXT("mid-stream temp removed"), FPaths::FileExists(R.TempPcmPath));
    // A caller path mistake must never delete or overwrite the original.
    TArray<uint8> Before; FFileHelper::LoadFileToArray(Before, *R.SourcePath);
    R.TempPcmPath = R.SourcePath; C.Store(false);
    const auto Alias = IBootMusicDecoder::Create()->Decode(R, C, [](double) {});
    TArray<uint8> After; FFileHelper::LoadFileToArray(After, *R.SourcePath);
    TestFalse(TEXT("source output alias rejected"), Alias.Error.IsEmpty());
    TestTrue(TEXT("original preserved"), Before == After);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootWavSignalOrigin,"Bootloop.V04.Decode.NonzeroResampling",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootWavSignalOrigin::RunTest(const FString&)
{
    auto Bytes=Wav(16,2,44100,44100);
    for(int32 I=0;I<44100;++I)
    {
        const int16 Tone=int16(12000.*FMath::Sin(2.*PI*440.*I/44100.));
        for(int32 Channel=0;Channel<2;++Channel){const int32 At=44+I*4+Channel*2;Bytes[At]=uint8(Tone);Bytes[At+1]=uint8(uint16(Tone)>>8);}
    }
    const auto D=Run(SaveWav(TEXT("nonzero-44100.wav"),Bytes));
    if(!TestTrue(TEXT("nonzero source decodes"),D.Error.IsEmpty()))return false;
    TestEqual(TEXT("one second canonical frames"),D.Frames,int64(48000));
    TArray<uint8> Output;FFileHelper::LoadFileToArray(Output,*D.PcmPath);
    if(!TestEqual(TEXT("stereo byte length"),Output.Num(),192000))return false;
    int32 First=-1,Last=-1,Crossings=0,Peak=0;
    auto Value=[&](int32 Frame,int32 Channel){const int32 At=Frame*4+Channel*2;return int16(uint16(Output[At])|(uint16(Output[At+1])<<8));};
    for(int32 I=1;I<48000;++I)
    {
        Peak=FMath::Max(Peak,FMath::Abs(int32(Value(I,0))));
        if(Value(I-1,0)<=0&&Value(I,0)>0){if(First<0)First=I;Last=I;++Crossings;}
    }
    const double Frequency=Crossings>1?48000.*(Crossings-1)/(Last-First):0.;
    TestTrue(TEXT("resample preserves 440Hz pitch"),FMath::Abs(Frequency-440.)<1.);
    TestTrue(TEXT("resample preserves nonzero amplitude"),Peak>11500&&Peak<=12000);
    TestEqual(TEXT("stereo origin aligned"),Value(1234,0),Value(1234,1));
    TestEqual(TEXT("decoded frame zero preserved"),Value(0,0),int16(0));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootMp3Origin, "Bootloop.V04.Decode.Mp3Origin", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootMp3Origin::RunTest(const FString&)
{
    const FString P = FPaths::ProjectDir() / TEXT("Source/BOOTLOOP/Private/Tests/Fixtures/Music/system-encoder.mp3");
    TArray<uint8> Before; FFileHelper::LoadFileToArray(Before, *P);
    auto A = Run(P);
    if (!A.Error.IsEmpty()) AddError(FString::Printf(TEXT("MP3 decoder error: %s"), *A.Error.ToString()));
    TestTrue(TEXT("system MP3 decoded"), A.Error.IsEmpty());
    // MF retains decoder priming/padding. Container duration is not a gapless
    // decoded-length contract; this 1.05s authored tone yields 53341 frames here.
    TestTrue(TEXT("MP3 bounded decoded frames including codec padding"), A.Frames >= 48000 && A.Frames <= 57600);
    TArray<uint8> X; FFileHelper::LoadFileToArray(X, *A.PcmPath);
    TestEqual(TEXT("PCM bytes agree with canonical metadata"),int64(X.Num()),A.Frames*A.Channels*2);
    if(A.Channels==2&&A.Frames>20000&&X.Num()==A.Frames*4)
    {
        int32 First=INDEX_NONE,Last=INDEX_NONE,Crossings=0;
        auto Sample=[&](int32 Frame){const int32 Byte=Frame*4;return int16(uint16(X[Byte])|(uint16(X[Byte+1])<<8));};
        for(int32 I=5001;I<int32(A.Frames)-5000;++I)
            if(Sample(I-1)<=0&&Sample(I)>0){if(First==INDEX_NONE)First=I;Last=I;++Crossings;}
        const double Frequency=Crossings>1?48000.*(Crossings-1)/(Last-First):0.;
        TestTrue(TEXT("440Hz source retains pitch at canonical 48kHz"),FMath::Abs(Frequency-440.)<2.);
        AddInfo(FString::Printf(TEXT("MF canonical frames=%lld frequency=%.3fHz; priming/padding retained"),A.Frames,Frequency));
    }
    auto B = Run(P);
    TArray<uint8> Y; FFileHelper::LoadFileToArray(Y, *B.PcmPath);
    TestTrue(TEXT("analysis and playback share identical frame zero"), X.Num() > 0 && X == Y);
    TestTrue(TEXT("source hash stable"), A.ContentHash.Len() == 64 && A.ContentHash == B.ContentHash);
    TestEqual(TEXT("SHA256 matches independent system hash"), A.ContentHash, FString(TEXT("6e798525aee8c5d940d3a81b76d487bdc5a6b86549024a784ded42a11ac3e0a0")));
    TArray<uint8> After; FFileHelper::LoadFileToArray(After, *P);
    TestTrue(TEXT("MP3 original preserved"), Before == After);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootMalformed, "Bootloop.V04.Decode.MalformedFixtures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootMalformed::RunTest(const FString&)
{
    const TArray<uint8> Base = Wav(32, 1, 48000, 48000, true);
    auto Check = [&](const TCHAR* Label, const TArray<uint8>& Bytes)
    {
        const auto V = Run(SaveWav(TEXT("malformed.wav"), Bytes));
        TestFalse(Label, V.Error.IsEmpty());
        TestFalse(TEXT("failed output removed"), FPaths::FileExists(AuditDir() / FString::Printf(TEXT("decode-%u.pcm"), GetTypeHash(AuditDir() / TEXT("malformed.wav")))));
    };
    auto B = Base; B[44] = 0; B[45] = 0; B[46] = 192; B[47] = 127;
    Check(TEXT("NaN rejected"), B);
    B[46] = 128; Check(TEXT("Inf rejected"), B);
    B = Base; B[20] = 2; Check(TEXT("compressed WAV rejected"), B);
    B = Base; B[40] = 255; B[41] = 255; B[42] = 255; B[43] = 255;
    Check(TEXT("overflow data length rejected"), B);
    B = Base; B.SetNum(45); Check(TEXT("truncation rejected"), B);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootOdd, "Bootloop.V04.Decode.OddChunks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootOdd::RunTest(const FString&)
{
    auto B = Wav(16, 1, 48000, 48000);
    TArray<uint8> J; Tag(J, "JUNK"); U32(J, 1); J.Add(13); J.Add(0);
    B.Insert(J.GetData(), J.Num(), 12);
    const uint32 N = B.Num() - 8;
    for (int I = 0; I < 4; ++I) B[4 + I] = (N >> (8 * I)) & 255;
    const auto V = Run(SaveWav(TEXT("odd.wav"), B));
    TestTrue(TEXT("odd chunk padding accepted"), V.Error.IsEmpty());
    TestEqual(TEXT("odd chunk frames"), V.Frames, int64(48000));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootUnsafeTempOwnership, "Bootloop.V04.Decode.UnsafeTempOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootUnsafeTempOwnership::RunTest(const FString&)
{
    const FString Outside = FPaths::ProjectIntermediateDir() / TEXT("V04Audit/DecodeOwnership");
    IFileManager::Get().MakeDirectory(*Outside, true);
    const TArray<uint8> Original = {17, 29, 43, 71, 113};
    for (const FString& Destination : {Outside / TEXT("original.pcm"), AuditDir() / TEXT("original.wav")})
    {
        FFileHelper::SaveArrayToFile(Original, *Destination);
        FBootDecodeRequest Request;
        Request.SourcePath = Fixture(16, 1, 48000);
        Request.TempPcmPath = Destination;
        TAtomic<bool> Cancel(false);
        const auto Result = IBootMusicDecoder::Create()->Decode(Request, Cancel, [](double) {});
        TArray<uint8> After;
        FFileHelper::LoadFileToArray(After, *Destination);
        TestFalse(TEXT("unsafe temp destination rejected"), Result.Error.IsEmpty());
        TestTrue(TEXT("unowned original preserved byte for byte"), Original == After);
        IFileManager::Get().Delete(*Destination);
    }
    return true;
}
#endif

