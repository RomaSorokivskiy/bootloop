#include <limits>
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Music/BootMusicAnalysis.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace {
FBootDecodeResult Fixture(double Bpm, double Offset, bool Silent=false)
{
 FBootDecodeResult D; D.Channels=1; D.Frames=48000*18; D.ContentHash=FString::Printf(TEXT("fixture-%g-%g-%d"),Bpm,Offset,Silent);
 D.PcmPath=FPaths::ProjectSavedDir()/TEXT("V04Work")/(D.ContentHash+TEXT(".pcm"));
 TArray<int16> P; P.SetNumZeroed(int32(D.Frames));
 if(!Silent) for(double T=Offset;T<18.;T+=60./Bpm) for(int32 J=0;J<240;++J) { const int32 I=int32(T*48000)+J; if(P.IsValidIndex(I)) P[I]=int16(26000.*FMath::Exp(-J/45.)*((J%7)<3?1.:-1.)); }
 FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(reinterpret_cast<const uint8*>(P.GetData()),P.Num()*2),*D.PcmPath); return D;
}
FBootAnalysisResult Run(const FBootDecodeResult& D) { TAtomic<bool> C(false); return FBootMusicAnalyzer::Analyze(D,C,[](double){}); }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootKnownTempo,"Bootloop.V04.Analysis.KnownTempo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootKnownTempo::RunTest(const FString&) { for(double B:{96.,130.,168.}) { const auto D=Fixture(B,.237); const auto R=Run(D); TestTrue(TEXT("analysis"),R.Analysis.IsValid()); if(R.Analysis) { TestTrue(TEXT("tempo"),FMath::Abs(R.Analysis->Grid.Bpm-B)<=1.); TestTrue(TEXT("offset"),FMath::Abs(R.Analysis->Grid.FirstBeatSeconds-.237)<=.020); } IFileManager::Get().Delete(*D.PcmPath); } return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootSafeGrid,"Bootloop.V04.Analysis.SafeGrid",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootSafeGrid::RunTest(const FString&) { const auto D=Fixture(130.,.237,true);const auto R=Run(D);TestTrue(TEXT("safe analysis"),R.Analysis.IsValid());if(R.Analysis){TestTrue(TEXT("review"),R.bNeedsReview);TestTrue(TEXT("safe"),R.Analysis->Source==EBootGridSource::Safe);TestTrue(TEXT("finite"),FMath::IsFinite(R.Analysis->Grid.Bpm));}IFileManager::Get().Delete(*D.PcmPath);return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootCacheVersion,"Bootloop.V04.Analysis.CacheVersion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootCacheVersion::RunTest(const FString&) {auto D=Fixture(130.,.237);auto R=Run(D);TestTrue(TEXT("analysis"),R.Analysis.IsValid());if(R.Analysis){auto A=FBootMusicGridRules::WithOverride(*R.Analysis,140.,.3); auto Stale=A; Stale.Version=FBootMusicAnalyzer::AnalysisVersion-1; TestFalse(TEXT("stale version rejected"),FBootMusicAnalyzer::Validate(Stale));const FString P=D.PcmPath+TEXT(".features");TestTrue(TEXT("save"),FBootMusicAnalyzer::SaveCache(P,D,A));FBootTrackAnalysis Loaded;TestTrue(TEXT("load"),FBootMusicAnalyzer::LoadCache(P,D,Loaded));TestEqual(TEXT("manual preserved"),Loaded.Grid.Bpm,140.);D.ContentHash+=TEXT("different");TestFalse(TEXT("hash invalidation"),FBootMusicAnalyzer::LoadCache(P,D,Loaded));IFileManager::Get().Delete(*P);}IFileManager::Get().Delete(*D.PcmPath);return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootDecodedOrigin,"Bootloop.V04.Analysis.DecodedOrigin",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootDecodedOrigin::RunTest(const FString&) {auto D=Fixture(130.,.237);auto R=Run(D);TestTrue(TEXT("analysis"),R.Analysis.IsValid());if(R.Analysis){for(double B:{65.,260.}){const auto A=FBootMusicGridRules::WithOverride(*R.Analysis,B,.237);TestEqual(TEXT("origin unchanged"),A.Grid.FirstBeatSeconds,.237);TestEqual(TEXT("frames unchanged"),A.CanonicalFrames,D.Frames);}const auto Bad=FBootMusicGridRules::WithOverride(*R.Analysis,std::numeric_limits<double>::quiet_NaN(),0.);TestEqual(TEXT("invalid rejected"),Bad.Grid.Bpm,R.Analysis->Grid.Bpm);}IFileManager::Get().Delete(*D.PcmPath);return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootWorkerBoundary,"Bootloop.V04.Analysis.RuntimeWorkerBoundary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootWorkerBoundary::RunTest(const FString&)
{
 // Exercise the engine C++ filter boundary path, where raw default4800-window
 // peak settings previously corrupted18-second envelopes during Finalize.
 IConsoleVariable* Simd=IConsoleManager::Get().FindConsoleVariable(TEXT("au.FloatArrayMath.ISPC"));
 const int32 Old=Simd?Simd->GetInt():0;if(Simd)Simd->Set(0,ECVF_SetByCode);
 for(int64 Frames:{1LL,137LL,4800LL,47999LL})for(int32 Channels:{1,2})
 {
  FBootDecodeResult D;D.Frames=Frames;D.Channels=Channels;D.ContentHash=FString::Printf(TEXT("boundary-%lld-%d"),Frames,Channels);
  D.PcmPath=FPaths::ProjectSavedDir()/TEXT("V04Work")/(D.ContentHash+TEXT(".pcm"));
  TArray<uint8> Pcm;Pcm.SetNumZeroed(int32(Frames*Channels*2));
  TestTrue(TEXT("fixture saved"),FFileHelper::SaveArrayToFile(Pcm,*D.PcmPath));
  const auto R=Run(D);TestTrue(TEXT("real worker tiny silence finalized"),R.Analysis.IsValid());
  if(R.Analysis){TestEqual(TEXT("canonical frames excludes padding"),R.Analysis->CanonicalFrames,Frames);TestEqual(TEXT("duration excludes padding"),R.Analysis->DurationSeconds,double(Frames)/48000.);TestTrue(TEXT("safe tiny grid"),R.bNeedsReview);TestEqual(TEXT("origin unchanged"),R.Analysis->Grid.FirstBeatSeconds,0.);}
  IFileManager::Get().Delete(*D.PcmPath);
 }
 // Repeated real-worker finalization also catches corruption surfacing in later
 // tracks rather than at the first call that overwrote the allocator's block.
 for(int32 Pass=0;Pass<2;++Pass){const auto D=Fixture(130.,.237);const auto R=Run(D);TestTrue(TEXT("repeated real worker"),R.Analysis.IsValid());if(R.Analysis)TestTrue(TEXT("spectral worker produced onsets"),R.Analysis->OnsetDensity.ContainsByPredicate([](float V){return V>0.f;}));IFileManager::Get().Delete(*D.PcmPath);}
 if(Simd)Simd->Set(Old,ECVF_SetByCode);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootAmbiguousEvidence,"Bootloop.V04.Analysis.AmbiguousEvidence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootAmbiguousEvidence::RunTest(const FString&)
{
 for(int32 Case=0;Case<2;++Case)
 {
  TArray<double> Events;
  if(Case==0){for(int32 I=0;I<9;++I)Events.Add(.237+.5*I);for(int32 I=0;I<34;++I)Events.Add(6.+1.61*I);}
  else {for(int32 I=0;I<20;++I)Events.Add(.237+I*(60./65.));}
  FBootDecodeResult D;D.Channels=1;D.Frames=int64((Events.Last()+1.)*48000.);D.ContentHash=FString::Printf(TEXT("ambiguous-%d"),Case);
  D.PcmPath=FPaths::ProjectSavedDir()/TEXT("V04Work")/(D.ContentHash+TEXT(".pcm"));
  TArray<int16> P;P.SetNumZeroed(int32(D.Frames));
  for(double T:Events)for(int32 J=0;J<240;++J){const int32 I=int32(T*48000)+J;if(P.IsValidIndex(I))P[I]=int16(26000.*FMath::Exp(-J/45.)*((J%7)<3?1.:-1.));}
  TestTrue(TEXT("ambiguous fixture saved"),FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(reinterpret_cast<const uint8*>(P.GetData()),P.Num()*2),*D.PcmPath));
  const auto R=Run(D);TestTrue(TEXT("weak evidence still yields usable features"),R.Analysis.IsValid());
  if(R.Analysis){TestTrue(TEXT("ambiguous evidence requires review"),R.bNeedsReview);TestTrue(TEXT("ambiguous evidence safe grid"),R.Analysis->Source==EBootGridSource::Safe);TestTrue(TEXT("confidence is conservative"),R.Analysis->Confidence<.8f);TestTrue(TEXT("features retained"),!R.Analysis->Energy.IsEmpty());}
  IFileManager::Get().Delete(*D.PcmPath);
 }
 return true;
}
#endif




