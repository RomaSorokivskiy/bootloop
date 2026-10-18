#include "Music/BootMusicAnalysis.h"
#include "Core/BootAtomicFile.h"
#include "OnsetNRTFactory.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Misc/FileHelper.h"
namespace {
constexpr uint32 Magic = 0x424D4134;
bool GridValid(double B,double T,double Duration) { return FMath::IsFinite(B)&&B>=30.&&B<=300.&&FMath::IsFinite(T)&&T>=0.&&T<=Duration; }
template<class T> void BoundedArray(FArchive& Ar,TArray<T>& A,int32 Max)
{
 int32 N=A.Num(); Ar<<N; if(N<0||N>Max){Ar.SetError();return;} if(Ar.IsLoading()) A.SetNum(N);for(T& V:A) Ar<<V;
}
void Serialize(FArchive& Ar,FBootTrackAnalysis& A)
{
 Ar<<A.Version<<A.DurationSeconds<<A.CanonicalFrames<<A.Grid.Bpm<<A.Grid.FirstBeatSeconds<<A.Confidence<<A.FeatureStepSeconds;
 uint8 S=uint8(A.Source);Ar<<S;A.Source=EBootGridSource(S);
 BoundedArray(Ar,A.BpmCandidates,FBootTrackAnalysis::MaxBpmCandidates);BoundedArray(Ar,A.Energy,FBootTrackAnalysis::MaxFeatureSamples);BoundedArray(Ar,A.OnsetDensity,FBootTrackAnalysis::MaxFeatureSamples);BoundedArray(Ar,A.SectionStartSeconds,FBootTrackAnalysis::MaxSections);
}
}
FString FBootMusicAnalyzer::CacheKey(const FBootDecodeResult& D)
{
 return FString::Printf(TEXT("%s-dec%u-pcm16le48000-ch%d-analysis%u"),*D.ContentHash,IBootMusicDecoder::SettingsVersion,D.Channels,AnalysisVersion);
}
bool FBootMusicAnalyzer::Validate(const FBootTrackAnalysis& A)
{
 if(A.Version!=AnalysisVersion||A.TrackHash.IsEmpty()||!A.HasBoundedFeatures()||A.CanonicalFrames<=0||A.CanonicalFrames>48000LL*600||!FMath::IsFinite(A.DurationSeconds)||FMath::Abs(A.DurationSeconds-double(A.CanonicalFrames)/48000.)>1.e-6||!GridValid(A.Grid.Bpm,A.Grid.FirstBeatSeconds,A.DurationSeconds)||!FMath::IsFinite(A.Confidence)||A.Confidence<0||A.Confidence>1||uint8(A.Source)>uint8(EBootGridSource::Safe)||A.FeatureStepSeconds!=.05) return false;
 const int32 N=FMath::CeilToInt(A.DurationSeconds/A.FeatureStepSeconds);if(A.Energy.Num()!=N||A.OnsetDensity.Num()!=N)return false;
 for(float V:A.Energy)if(!FMath::IsFinite(V)||V<0||V>1)return false;
 for(float V:A.OnsetDensity)if(!FMath::IsFinite(V)||V<0||V>1)return false;
 for(double V:A.BpmCandidates)if(!FMath::IsFinite(V)||V<30||V>300)return false;
 double Prev=-1.;for(double V:A.SectionStartSeconds){if(!FMath::IsFinite(V)||V<0||V>A.DurationSeconds||V<=Prev)return false;Prev=V;}return true;
}
FBootTrackAnalysis FBootMusicGridRules::WithOverride(const FBootTrackAnalysis& A,double B,double T)
{
 FBootTrackAnalysis Out=A;if(GridValid(B,T,A.DurationSeconds)){Out.Grid={B,T};Out.Source=EBootGridSource::Manual;}return Out;
}
bool FBootMusicAnalyzer::SaveCache(const FString& Path,const FBootDecodeResult& D,const FBootTrackAnalysis& In)
{
 if(!Validate(In)||In.TrackHash!=D.ContentHash||In.CanonicalFrames!=D.Frames)return false;
 TArray<uint8> Bytes;FMemoryWriter W(Bytes);uint32 M=Magic;W<<M;FString Key=CacheKey(D);W<<Key;FBootTrackAnalysis A=In;Serialize(W,A);if(W.IsError())return false;
 const FString Temp=Path+TEXT(".tmp");if(!FFileHelper::SaveArrayToFile(Bytes,*Temp))return false;const bool Ok=FBootAtomicFile::Replace(Temp,Path);if(!Ok)IFileManager::Get().Delete(*Temp);return Ok;
}
bool FBootMusicAnalyzer::LoadCache(const FString& Path,const FBootDecodeResult& D,FBootTrackAnalysis& Out)
{
 const int64 Size=IFileManager::Get().FileSize(*Path);if(Size<16||Size>256*1024)return false;
 TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Path))return false;FMemoryReader R(Bytes);uint32 M=0;R<<M;if(M!=Magic)return false;
 // Limit string allocation before archive FString deserialize (cache is untrusted).
 int32 KeyLength=0;R<<KeyLength;if(KeyLength<=0||KeyLength>512)return false;R.Seek(sizeof(uint32));FString Key;R<<Key;if(R.IsError()||Key!=CacheKey(D))return false;
 FBootTrackAnalysis A;Serialize(R,A);A.TrackHash=D.ContentHash;if(R.IsError()||R.Tell()!=R.TotalSize()||A.CanonicalFrames!=D.Frames||!Validate(A))return false;Out=MoveTemp(A);return true;
}
FBootAnalysisResult FBootMusicAnalyzer::Analyze(const FBootDecodeResult& D,const TAtomic<bool>& Cancel,TFunctionRef<void(double)> Progress)
{
 FBootAnalysisResult R;
 auto Fail=[&](const TCHAR* E){R.Error=FText::FromString(E);return R;};
 if(D.Frames<=0||D.Frames>48000LL*600||(D.Channels!=1&&D.Channels!=2)||D.ContentHash.IsEmpty())return Fail(TEXT("Invalid canonical PCM metadata"));
 auto File=TUniquePtr<IFileHandle>(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*D.PcmPath));
 if(!File||File->Size()!=D.Frames*D.Channels*2)return Fail(TEXT("Canonical PCM frame count mismatch"));
 FBootTrackAnalysis A;A.Version=AnalysisVersion;A.TrackHash=D.ContentHash;A.CanonicalFrames=D.Frames;A.DurationSeconds=double(D.Frames)/48000.;A.Source=EBootGridSource::Safe;A.Grid={130.,0.};
 const int32 Bins=FMath::CeilToInt(A.DurationSeconds/.05);A.Energy.SetNumZeroed(Bins);A.OnsetDensity.SetNumZeroed(Bins);TArray<int32> BinCounts;BinCounts.SetNumZeroed(Bins);
 // The exported worker takes peak windows in onset-envelope indices, not PCM frames.
 // Its raw defaults (4800/4801) exceed short envelopes and UE ArrayMeanFilter can
 // write past its output on the non-ISPC boundary path. Match runtime100Hz units.
 Audio::FOnsetNRTSettings Settings;Settings.bDownmixToMono=true;
 Settings.OnsetStrengthSettings.NumHopFrames=480;
 Settings.OnsetStrengthSettings.NumWindowFrames=1920;
 Settings.OnsetStrengthSettings.FFTSize=2048;
 Settings.PeakPickerSettings.NumPreMax=3;
 Settings.PeakPickerSettings.NumPostMax=1;
 Settings.PeakPickerSettings.NumPreMean=10;
 Settings.PeakPickerSettings.NumPostMean=10;
 Settings.PeakPickerSettings.NumWait=12;
 Settings.PeakPickerSettings.MeanDelta=.07f;
 Audio::FOnsetNRTWorker Worker(Audio::FAnalyzerNRTParameters(48000.f,D.Channels),Settings);Audio::FOnsetNRTResult Onsets;
 TArray<int16> Pcm;Pcm.SetNumUninitialized(4096*D.Channels);TArray<float> Floats;Floats.SetNumUninitialized(Pcm.Num());TArray<double> Attacks;double Fast=0.,Slow=0.,LastAttack=-1.;double PeakEnergy=0.;
 for(int64 Frame=0;Frame<D.Frames;)
 {
  if(Cancel.Load()){R.bCancelled=true;return R;}const int32 Count=int32(FMath::Min<int64>(4096,D.Frames-Frame));
  if(!File->Read(reinterpret_cast<uint8*>(Pcm.GetData()),Count*D.Channels*2))return Fail(TEXT("Canonical PCM read failed"));
  for(int32 I=0;I<Count;++I)
  {
   double Mono=0.;for(int32 C=0;C<D.Channels;++C){const float S=Pcm[I*D.Channels+C]/32768.f;Floats[I*D.Channels+C]=S;Mono+=FMath::Abs(S)/D.Channels;}
   const double T=double(Frame+I)/48000.;const int32 Bin=FMath::Min(Bins-1,int32((Frame+I)/2400));A.Energy[Bin]+=float(Mono*Mono);++BinCounts[Bin];
   Fast+=.04*(Mono-Fast);Slow+=.0005*(Mono-Slow);
   // Causal attack edges retain decoded sample origin; spectral worker supports softer onsets below.
   if(Fast>.015&&Fast>Slow*3.&&T-LastAttack>.12&&Attacks.Num()<6000){Attacks.Add(T);LastAttack=T;}
  }
  Worker.Analyze(TArrayView<const float>(Floats.GetData(),Count*D.Channels),&Onsets);Frame+=Count;Progress(double(Frame)/D.Frames*.9);
 }
 // Ensure even a single-frame source yields more envelope windows than either
 // peak-filter boundary extent. This zero tail is analysis-only: metadata and
 // every returned onset still refer to the original decoded sample timeline.
 if(D.Frames<48000)
 {
  Floats.Init(0.f,4096*D.Channels);
  for(int64 Pad=D.Frames;Pad<48000;)
  {
   if(Cancel.Load()){R.bCancelled=true;return R;}
   const int32 Count=int32(FMath::Min<int64>(4096,48000-Pad));
   Worker.Analyze(TArrayView<const float>(Floats.GetData(),Count*D.Channels),&Onsets);Pad+=Count;
  }
 }
 if(Cancel.Load()){R.bCancelled=true;return R;}Worker.Finalize(&Onsets);
 for(int32 I=0;I<Bins;++I){A.Energy[I]=FMath::Sqrt(A.Energy[I]/FMath::Max(1,BinCounts[I]));PeakEnergy=FMath::Max(PeakEnergy,double(A.Energy[I]));}
 TArray<double> Spectral;
 if(Onsets.ContainsChannel(0))for(const Audio::FOnset& O:Onsets.GetOnsetsForChannel(0))if(O.Timestamp>=0&&O.Timestamp<A.DurationSeconds&&Spectral.Num()<6000){Spectral.Add(O.Timestamp);A.OnsetDensity[FMath::Min(Bins-1,int32(O.Timestamp/.05))]=FMath::Min(1.f,A.OnsetDensity[FMath::Min(Bins-1,int32(O.Timestamp/.05))]+.25f);}
 const TArray<double>& Events=Attacks.Num()>=8?Attacks:Spectral;
 if(Events.Num()>=8&&PeakEnergy>.005)
 {
  TArray<double> Intervals;for(int32 I=1;I<Events.Num();++I){const double Delta=Events[I]-Events[I-1];if(Delta>=.2&&Delta<=1.5)Intervals.Add(Delta);}
  if(Intervals.Num()>=6)
  {
   Intervals.Sort();const double BasePeriod=Intervals[Intervals.Num()/2];
   struct FCandidate { double Bpm=130.,Origin=0.,Score=0.; };
   TArray<FCandidate> Candidates;
   for(double Scale:{1.,2.,.5})
   {
    if(Cancel.Load()){R.bCancelled=true;return R;}
    const double Period=BasePeriod*Scale;const double Bpm=60./Period;
    if(Bpm<30.||Bpm>300.)continue;
    A.BpmCandidates.Add(Bpm);
    // Fit phase without changing decoded sample coordinates. Rejected fit points
    // remain in every confidence denominator below; regression is not evidence.
    double SumX=0.,SumY=0.,SumXX=0.,SumXY=0.;int32 N=0;
    for(double T:Events){const double X=FMath::RoundToDouble((T-Events[0])/Period);if(FMath::Abs((T-Events[0])-X*Period)>.04)continue;SumX+=X;SumY+=T;SumXX+=X*X;SumXY+=X*T;++N;}
    const double Den=N*SumXX-SumX*SumX;const double P=Den>0?(N*SumXY-SumX*SumY)/Den:Period;
    const double Origin=N>0?(SumY-P*SumX)/N:Events[0];
    if(!FMath::IsFinite(P)||P<=0||FMath::Abs(P-Period)>Period*.02)continue;
    int32 PhaseGood=0,AdjacentGood=0;double Error=0.;int32 QuarterAll[4]={},QuarterGood[4]={};
    const double Span=FMath::Max(.001,Events.Last()-Events[0]);
    for(int32 I=0;I<Events.Num();++I)
    {
     const double X=FMath::RoundToDouble((Events[I]-Origin)/P);
     const double Residual=FMath::Abs(Events[I]-(Origin+X*P));
     const int32 Q=FMath::Clamp(int32((Events[I]-Events[0])/Span*4.),0,3);++QuarterAll[Q];
     if(Residual<=.025){++PhaseGood;++QuarterGood[Q];}
     Error+=FMath::Min(Residual,.1);
     // An adjacent interval outside the proposal remains contradictory evidence.
     // Missing beats lower occupancy rather than silently vanishing from scoring.
     if(I>0&&FMath::Abs((Events[I]-Events[I-1])-P)<=.025)++AdjacentGood;
    }
    double Coverage=0.;for(int32 Q=0;Q<4;++Q)if(QuarterAll[Q]>0)Coverage+=double(QuarterGood[Q])/QuarterAll[Q]*.25;
    const double PhaseSupport=double(PhaseGood)/Events.Num();
    const double AdjacentSupport=double(AdjacentGood)/(Events.Num()-1);
    const double ExpectedBeats=Span/P+1.;
    const double Occupancy=FMath::Clamp(double(PhaseGood)/ExpectedBeats,0.,1.);
    const double Accuracy=FMath::Clamp(1.-Error/Events.Num()/.05,0.,1.);
    FCandidate C;C.Bpm=60./P;C.Origin=FMath::Max(0.,Origin);
    C.Score=PhaseSupport*AdjacentSupport*Coverage*Occupancy*Accuracy;
    Candidates.Add(C);
   }
   Candidates.Sort([](const FCandidate& L,const FCandidate& R){return L.Score>R.Score;});
   if(!Candidates.IsEmpty())
   {
    const FCandidate& Best=Candidates[0];double Confidence=Best.Score;
    // Half/double grids are evaluated, not merely listed. Similar evidence leaves
    // the user in review mode; conservative BPM limits also expose half-time doubt.
    if(Candidates.Num()>1&&Best.Score-Candidates[1].Score<.15)Confidence=FMath::Min(Confidence,.65);
    if(Best.Bpm<80.||Best.Bpm>190.)Confidence=FMath::Min(Confidence,.65);
    A.Confidence=float(FMath::Clamp(Confidence,0.,1.));
    if(A.Confidence>=.80f&&GridValid(Best.Bpm,Best.Origin,A.DurationSeconds))
    {A.Grid={Best.Bpm,Best.Origin};A.Source=EBootGridSource::Estimated;}
   }
  }
 }
 A.SectionStartSeconds.Add(0.);double LastSection=0.;for(int32 I=80;I<Bins-20;++I){const double T=I*.05;if(T-LastSection<4.)continue;double Before=0.,After=0.;for(int32 J=1;J<=20;++J){Before+=A.Energy[I-J];After+=A.Energy[I+J];}if(FMath::Abs(After-Before)>.8){A.SectionStartSeconds.Add(T);LastSection=T;if(A.SectionStartSeconds.Num()>=FBootTrackAnalysis::MaxSections)break;}}
 if(!Validate(A))return Fail(TEXT("Analysis metadata validation failed"));if(Cancel.Load()){R.bCancelled=true;return R;}R.bNeedsReview=A.Source==EBootGridSource::Safe;R.Analysis=MakeShared<const FBootTrackAnalysis,ESPMode::ThreadSafe>(MoveTemp(A));Progress(1.);return R;
}



