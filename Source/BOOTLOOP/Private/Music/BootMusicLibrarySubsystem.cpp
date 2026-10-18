#include "Music/BootMusicLibrarySubsystem.h"
#include "Music/BootMusicLibraryStore.h"
#include "Music/BootMusicAnalysis.h"
#include "Developer/BootAuditPaths.h"
#include "Async/Async.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
struct FBootLibraryFileLifetime
{
 FString Root;TArray<FString> Paths;TAtomic<bool> bRemoved{false};
 ~FBootLibraryFileLifetime(){if(!bRemoved.Load())return;const FString R=Root;const auto P=Paths;AsyncTask(ENamedThreads::GameThread,[R,P]{for(const auto& File:P)FBootMusicLibraryStore::DeleteOwned(R,File);});}
};
struct FBootLibraryJob
{
 uint64 Id=0,Serial=0,Generation=0;FString Input,Placeholder,ScratchSource,ScratchPcm,FinalSource,FinalPcm,ExpectedHash;
 FString DisplayName;bool bBuiltIn=false;FBootBeatGrid AuthoredGrid;TArray<double> Sections;
 TAtomic<bool> Cancel{false};
};
namespace {
constexpr int64 SourceLimit=256ll*1024*1024,PcmLimit=128ll*1024*1024;
void CleanupJob(const FString& Root,const TSharedPtr<FBootLibraryJob,ESPMode::ThreadSafe>& J){FBootMusicLibraryStore::DeleteOwned(Root,J->ScratchSource);FBootMusicLibraryStore::DeleteOwned(Root,J->ScratchSource+TEXT(".copy"));FBootMusicLibraryStore::DeleteOwned(Root,J->ScratchPcm);FBootMusicLibraryStore::DeleteOwned(Root,J->ScratchPcm+TEXT(".features"));}
bool JobPathsSafe(const FString& Root,const TSharedPtr<FBootLibraryJob,ESPMode::ThreadSafe>& J)
{
 const FString Paths[]={J->ScratchSource,J->ScratchSource+TEXT(".copy"),J->ScratchPcm,J->ScratchPcm+TEXT(".features"),J->ScratchPcm+TEXT(".features.tmp"),J->FinalSource,J->FinalPcm,J->FinalPcm+TEXT(".features")};
 for(const auto& Path:Paths)if(!FBootMusicLibraryStore::IsOwnedPath(Root,Path))return false;return true;
}
bool ValidGrid(double B,double T,double Duration){return FMath::IsFinite(B)&&B>=30&&B<=300&&FMath::IsFinite(T)&&T>=0&&T<=Duration;}
}
void UBootMusicLibrarySubsystem::Initialize(FSubsystemCollectionBase& C)
{
 Super::Initialize(C);Root=FPaths::ConvertRelativePathToFull(FBootAuditPaths::LibraryRoot(FCommandLine::Get()));bStopping=false;FText Error;FBootMusicLibraryStore::Load(Root,Entries,Error);
 // Importing authored starters is an explicit gameplay operation, never GI::Init side effect.
}
bool UBootMusicLibrarySubsystem::InitializeFixture(const FString& R)
{
 check(IsInGameThread());if(!FBootMusicLibraryStore::IsOwnedPath(FPaths::ProjectSavedDir()/TEXT("V04Audit"),FPaths::ConvertRelativePathToFull(R)))return false;
 for(auto& J:Jobs)J->Cancel.Store(true);++Generation;Jobs.Reset();Entries.Reset();Sources.Reset();FileLifetimes.Reset();bBusy=false;bStopping=false;Root=FPaths::ConvertRelativePathToFull(R);
 FText Error;return FBootMusicLibraryStore::Load(Root,Entries,Error);
}
void UBootMusicLibrarySubsystem::Deinitialize()
{
 check(IsInGameThread());Persist();bStopping=true;++Generation;for(auto& J:Jobs)J->Cancel.Store(true);Jobs.Reset();bBusy=false;Super::Deinitialize();
}
void UBootMusicLibrarySubsystem::Persist(){if(!Root.IsEmpty()){FText Error;FBootMusicLibraryStore::Save(Root,Entries,Error);}}
uint64 UBootMusicLibrarySubsystem::ImportFiles(const TArray<FString>& Files)
{
 check(IsInGameThread());if(bStopping||Files.IsEmpty()||Files.Num()>32||Jobs.Num()+Files.Num()>64||Entries.Num()+Files.Num()>512)return 0;
 if(Root.IsEmpty())Root=FPaths::ConvertRelativePathToFull(FBootAuditPaths::LibraryRoot(FCommandLine::Get()));const uint64 Id=NextJob++;
 for(int32 I=0;I<Files.Num();++I)
 {
  auto J=MakeShared<FBootLibraryJob,ESPMode::ThreadSafe>();J->Id=Id;J->Serial=NextJob++;J->Generation=Generation;J->Input=FPaths::ConvertRelativePathToFull(Files[I]);J->DisplayName=FPaths::GetBaseFilename(Files[I]).Left(256);J->Placeholder=FString::Printf(TEXT("pending-%llu"),J->Serial);
  const FString Token=FGuid::NewGuid().ToString(EGuidFormats::Digits);J->ScratchSource=Root/TEXT("Temp")/(Token+TEXT(".")+FPaths::GetExtension(Files[I]).ToLower());J->ScratchPcm=Root/TEXT("Temp")/(Token+TEXT(".pcm"));J->FinalSource=Root/TEXT("Sources")/(Token+TEXT(".")+FPaths::GetExtension(Files[I]).ToLower());J->FinalPcm=Root/TEXT("Cache")/(Token+TEXT(".pcm"));
  Jobs.Add(J);FBootLibraryEntry E;E.Hash=J->Placeholder;E.DisplayName=J->DisplayName;E.State=EBootLibraryState::Queued;Entries.Add(MoveTemp(E));
 }
 StartNext();return Id;
}
void UBootMusicLibrarySubsystem::CancelImport(uint64 Id)
{
 check(IsInGameThread());for(auto& J:Jobs)if(J->Id==Id){J->Cancel.Store(true);Entries.RemoveAll([&](const FBootLibraryEntry& E){return E.Hash==J->Placeholder;});}
 // Active job retains the serialization slot until worker completion; queued cancelled jobs are skipped.
 Persist();if(!bBusy)StartNext();
}
void UBootMusicLibrarySubsystem::StartNext()
{
 if(bBusy||bStopping)return;Jobs.RemoveAll([](const auto& J){return J->Cancel.Load();});if(Jobs.IsEmpty())return;
 const auto J=Jobs[0];bBusy=true;for(auto& E:Entries)if(E.Hash==J->Placeholder){E.State=EBootLibraryState::Decoding;E.Progress=.01;}
 const FString R=Root;const uint64 G=Generation;TWeakObjectPtr<UBootMusicLibrarySubsystem> Weak(this);
 Async(EAsyncExecution::ThreadPool,[Weak,J,R,G]
 {
  FBootLibraryEntry E;E.Hash=J->Placeholder;E.DisplayName=J->DisplayName;E.bBuiltIn=J->bBuiltIn;E.State=EBootLibraryState::Failed;
  auto Progress=[Weak,J,G](double P,EBootLibraryState State){AsyncTask(ENamedThreads::GameThread,[Weak,J,G,P,State]{if(auto* S=Weak.Get()){if(S->Generation!=G||S->bStopping||J->Cancel.Load())return;for(auto& Entry:S->Entries)if(Entry.Hash==J->Placeholder){Entry.State=State;Entry.Progress=P;}}});};
  const bool SafeStorage=JobPathsSafe(R,J),SafeInput=!J->bBuiltIn||FBootMusicLibraryStore::IsOwnedPath(FPaths::ProjectContentDir()/TEXT("Music/Starter"),J->Input);
  const int64 Bytes=SafeStorage&&SafeInput?IFileManager::Get().FileSize(*J->Input):-1;const FString Ext=FPaths::GetExtension(J->Input).ToLower();
  if(J->Cancel.Load()){}
  else if(!SafeStorage||!SafeInput)E.Error=TEXT("Unsafe music storage or starter path");
  else if(Bytes<=0||Bytes>SourceLimit||(Ext!=TEXT("wav")&&Ext!=TEXT("mp3")))E.Error=TEXT("Only nonempty WAV/MP3 up to 256 MiB are supported");
  else if(!FBootMusicLibraryStore::CanReserve(R,Bytes+PcmLimit+512*1024))E.Error=TEXT("Music cache exceeds the 2 GiB budget; remove unused tracks");
  else
  {
   IFileManager::Get().MakeDirectory(*(R/TEXT("Temp")),true);IFileManager::Get().MakeDirectory(*(R/TEXT("Sources")),true);IFileManager::Get().MakeDirectory(*(R/TEXT("Cache")),true);
   const FString CopyTemp=J->ScratchSource+TEXT(".copy");
   bool Copied=JobPathsSafe(R,J)&&(J->bBuiltIn||IFileManager::Get().Copy(*CopyTemp,*J->Input,true,true)==COPY_OK);
   if(Copied&&!J->bBuiltIn)Copied=IFileManager::Get().Move(*J->ScratchSource,*CopyTemp,true,true,false,true);
   if(!Copied)E.Error=TEXT("Cannot make managed source copy");
   else if(!J->Cancel.Load())
   {
    FBootDecodeRequest Request;Request.JobId=J->Id;Request.SourcePath=J->bBuiltIn?J->Input:J->ScratchSource;Request.TempPcmPath=J->ScratchPcm;
    double Last=-1.;auto Decoder=IBootMusicDecoder::Create();auto D=Decoder->Decode(Request,J->Cancel,[&](double P){if(P-Last>=.05){Last=P;Progress(P*.5,EBootLibraryState::Decoding);}});
    if(!D.Error.IsEmpty())E.Error=D.Error.ToString();
    else if(!J->Cancel.Load()&&!D.bCancelled)
    {
     if(!J->ExpectedHash.IsEmpty()&&D.ContentHash!=J->ExpectedHash)E.Error=TEXT("Starter SHA256 does not match authored manifest");
     else if(D.Frames<=0||D.Frames>48000ll*600||(D.Channels!=1&&D.Channels!=2)||D.Frames*D.Channels*2>PcmLimit)E.Error=TEXT("Decoded PCM budget exceeded");
     else if(!JobPathsSafe(R,J))E.Error=TEXT("Unsafe canonical PCM path");
     else
     {
      Progress(.5,EBootLibraryState::Analyzing);Last=-1.;auto A=FBootMusicAnalyzer::Analyze(D,J->Cancel,[&](double P){if(P-Last>=.05){Last=P;Progress(.5+P*.49,EBootLibraryState::Analyzing);}});
      if(!A.Error.IsEmpty())E.Error=A.Error.ToString();
      else if(A.Analysis&&!J->Cancel.Load())
      {
       E.Hash=D.ContentHash;E.Frames=D.Frames;E.Channels=D.Channels;E.DurationSeconds=double(D.Frames)/48000.;E.Analysis=A.Analysis;
       if(J->bBuiltIn){auto Authored=*A.Analysis;Authored.Grid=J->AuthoredGrid;Authored.Source=EBootGridSource::Authored;Authored.Confidence=1.;Authored.SectionStartSeconds=J->Sections;if(FBootMusicAnalyzer::Validate(Authored))E.Analysis=MakeShared<const FBootTrackAnalysis,ESPMode::ThreadSafe>(MoveTemp(Authored));else E.Error=TEXT("Invalid authored starter grid");}
       E.Grid=E.Analysis->Grid;E.SourcePath=J->bBuiltIn?J->Input:J->FinalSource;E.PcmPath=J->FinalPcm;E.State=E.Analysis->Source==EBootGridSource::Safe?EBootLibraryState::NeedsReview:EBootLibraryState::Ready;E.Progress=1.;
       if(E.Error.IsEmpty()&&(!JobPathsSafe(R,J)||!FBootMusicAnalyzer::SaveCache(J->ScratchPcm+TEXT(".features"),D,*E.Analysis)))E.Error=TEXT("Cannot safely save analysis cache");
      }
     }
    }
   }
  }
  if(!E.Error.IsEmpty())E.State=EBootLibraryState::Failed;
  AsyncTask(ENamedThreads::GameThread,[Weak,J,R,G,E=MoveTemp(E)]()mutable{if(auto* S=Weak.Get()){if(S->Generation==G&&!S->bStopping){S->Publish(J->Serial,G,MoveTemp(E));return;}}CleanupJob(R,J);});
 });
}
void UBootMusicLibrarySubsystem::Publish(uint64 Serial,uint64 G,FBootLibraryEntry E)
{
 check(IsInGameThread());const int32 Index=Jobs.IndexOfByPredicate([&](const auto& J){return J->Serial==Serial;});if(Index==INDEX_NONE||G!=Generation||bStopping)return;const auto J=Jobs[Index];
 if(!J->Cancel.Load())
 {
  const int32 Existing=Entries.IndexOfByPredicate([&](const auto& V){return V.Hash==E.Hash&&V.Hash!=J->Placeholder;});
  if(Existing!=INDEX_NONE&&Entries[Existing].Analysis.IsValid())Entries.RemoveAll([&](const auto& V){return V.Hash==J->Placeholder;});
  else
  {
   if(E.Analysis&&E.Error.IsEmpty())
   {
    // Every successful import owns distinct paths: reimport after remove cannot collide with an older lease.
    bool Moved=JobPathsSafe(Root,J)&&(J->bBuiltIn||IFileManager::Get().Move(*J->FinalSource,*J->ScratchSource,true,true,false,true));
    Moved=Moved&&IFileManager::Get().Move(*J->FinalPcm,*J->ScratchPcm,true,true,false,true)&&IFileManager::Get().Move(*(J->FinalPcm+TEXT(".features")),*(J->ScratchPcm+TEXT(".features")),true,true,false,true);
    if(!Moved){E.State=EBootLibraryState::Failed;E.Error=TEXT("Cannot commit managed music files");E.Analysis.Reset();FBootMusicLibraryStore::DeleteOwned(Root,J->FinalSource);FBootMusicLibraryStore::DeleteOwned(Root,J->FinalPcm);FBootMusicLibraryStore::DeleteOwned(Root,J->FinalPcm+TEXT(".features"));}
   }
   if(Existing!=INDEX_NONE)
   {
    const auto Old=Entries[Existing];E.DisplayName=Old.DisplayName;E.bIncluded=Old.bIncluded;E.bHasGridOverride=Old.bHasGridOverride;E.bSafeGridAccepted=Old.bSafeGridAccepted;E.Revision=Old.Revision+1;
    if(E.Analysis&&Old.PcmPath!=E.PcmPath)
    {
     auto Previous=FileLifetimes.FindRef(Old.PcmPath).Pin();if(Previous)Previous->bRemoved.Store(true);
     else {FBootMusicLibraryStore::DeleteOwned(Root,Old.PcmPath);FBootMusicLibraryStore::DeleteOwned(Root,Old.PcmPath+TEXT(".features"));if(!Old.bBuiltIn)FBootMusicLibraryStore::DeleteOwned(Root,Old.SourcePath);}
     FileLifetimes.Remove(Old.PcmPath);
    }
    if(E.Analysis&&Old.bHasGridOverride&&ValidGrid(Old.Grid.Bpm,Old.Grid.FirstBeatSeconds,E.DurationSeconds)){auto A=FBootMusicGridRules::WithOverride(*E.Analysis,Old.Grid.Bpm,Old.Grid.FirstBeatSeconds);E.Analysis=MakeShared<const FBootTrackAnalysis,ESPMode::ThreadSafe>(MoveTemp(A));E.Grid=Old.Grid;E.State=EBootLibraryState::Ready;}
    else if(E.Analysis&&E.bSafeGridAccepted)E.State=EBootLibraryState::Ready;
    Entries[Existing]=E;Entries.RemoveAll([&](const auto& V){return V.Hash==J->Placeholder;});
   }
   else {const int32 Pending=Entries.IndexOfByPredicate([&](const auto& V){return V.Hash==J->Placeholder;});if(Pending!=INDEX_NONE)Entries[Pending]=MoveTemp(E);}
  }
 }
 CleanupJob(Root,J);Jobs.RemoveAt(Index);bBusy=false;Persist();StartNext();
}
bool UBootMusicLibrarySubsystem::SetIncluded(const FString& H,bool Included){check(IsInGameThread());for(auto& E:Entries)if(E.Hash==H){E.bIncluded=Included;++E.Revision;Persist();return true;}return false;}
bool UBootMusicLibrarySubsystem::SetDisplayName(const FString& H,const FString& Name){check(IsInGameThread());if(Name.TrimStartAndEnd().IsEmpty())return false;for(auto& E:Entries)if(E.Hash==H){E.DisplayName=Name.Left(256);++E.Revision;Persist();return true;}return false;}
bool UBootMusicLibrarySubsystem::SetGridOverride(const FString& H,double B,double T,FText& Error)
{
 check(IsInGameThread());for(auto& E:Entries)if(E.Hash==H){if(!E.Analysis||!ValidGrid(B,T,E.DurationSeconds)){Error=FText::FromString(TEXT("BPM must be 30–300 and first beat within this track"));return false;}auto A=FBootMusicGridRules::WithOverride(*E.Analysis,B,T);E.Analysis=MakeShared<const FBootTrackAnalysis,ESPMode::ThreadSafe>(MoveTemp(A));E.Grid={B,T};E.bHasGridOverride=true;E.State=EBootLibraryState::Ready;++E.Revision;Persist();return true;}return false;
}
bool UBootMusicLibrarySubsystem::AcceptSafeGrid(const FString& H,FText& Error){check(IsInGameThread());for(auto& E:Entries)if(E.Hash==H){if(!E.Analysis){Error=FText::FromString(TEXT("Analysis is not ready"));return false;}E.bSafeGridAccepted=true;E.State=EBootLibraryState::Ready;++E.Revision;Persist();return true;}return false;}
FBootPlaybackLease UBootMusicLibrarySubsystem::Acquire(const FString& H,FText& Error)
{
 check(IsInGameThread());FBootPlaybackLease Lease;for(const auto& E:Entries)if(E.Hash==H)
 {
  if(E.State!=EBootLibraryState::Ready||!E.Analysis||!FBootMusicLibraryStore::IsOwnedPath(Root,E.PcmPath)||IFileManager::Get().FileSize(*E.PcmPath)!=E.Frames*E.Channels*2){Error=FText::FromString(TEXT("Track is not ready for playback"));return Lease;}
  auto Lifetime=FileLifetimes.FindRef(E.PcmPath).Pin();if(!Lifetime){Lifetime=MakeShared<FBootLibraryFileLifetime,ESPMode::ThreadSafe>();Lifetime->Root=Root;Lifetime->Paths={E.PcmPath,E.PcmPath+TEXT(".features")};if(!E.bBuiltIn)Lifetime->Paths.Add(E.SourcePath);FileLifetimes.Add(E.PcmPath,Lifetime);}
  auto Source=MakeShared<FBootPlaybackSource,ESPMode::ThreadSafe>();Source->Hash=H;Source->PcmPath=E.PcmPath;Source->Frames=E.Frames;Source->Channels=E.Channels;Source->Analysis=E.Analysis;Source->Lifetime=Lifetime;Lease.Source=Source;return Lease;
 }Error=FText::FromString(TEXT("Track not found"));return Lease;
}
bool UBootMusicLibrarySubsystem::Remove(const FString& H,FText& Error)
{
 check(IsInGameThread());const int32 I=Entries.IndexOfByPredicate([&](const auto& E){return E.Hash==H;});if(I==INDEX_NONE){Error=FText::FromString(TEXT("Track not found"));return false;}auto E=Entries[I];
 if(E.Hash.StartsWith(TEXT("pending-"))){for(const auto& J:Jobs)if(J->Placeholder==H){CancelImport(J->Id);return true;}}
 if(E.bBuiltIn){Entries[I].bIncluded=false;++Entries[I].Revision;Persist();return true;}
 auto Lifetime=FileLifetimes.FindRef(E.PcmPath).Pin();if(Lifetime)Lifetime->bRemoved.Store(true);else {FBootMusicLibraryStore::DeleteOwned(Root,E.SourcePath);FBootMusicLibraryStore::DeleteOwned(Root,E.PcmPath);FBootMusicLibraryStore::DeleteOwned(Root,E.PcmPath+TEXT(".features"));}
 FileLifetimes.Remove(E.PcmPath);Entries.RemoveAt(I);Persist();return true;
}
void UBootMusicLibrarySubsystem::BootstrapStarterLibrary()
{
 check(IsInGameThread());if(bStopping)return;if(Root.IsEmpty())Root=FPaths::ConvertRelativePathToFull(FBootAuditPaths::LibraryRoot(FCommandLine::Get()));
 if(!FBootMusicLibraryStore::IsSafePath(Root))return;RepairLibrary();const FString Marker=Root/TEXT("starter-seeded-v1");
 const FString Folder=FPaths::ProjectContentDir()/TEXT("Music/Starter"),Manifest=Folder/TEXT("manifest.json");if(!FBootMusicLibraryStore::IsOwnedPath(Folder,Manifest))return;const int64 Size=IFileManager::Get().FileSize(*Manifest);if(Size<=0||Size>1024*1024)return;
 FString Json;TSharedPtr<FJsonObject> Doc;const TArray<TSharedPtr<FJsonValue>>* Tracks=nullptr;if(!FFileHelper::LoadFileToString(Json,*Manifest)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Doc)||!Doc||!Doc->TryGetArrayField(TEXT("tracks"),Tracks)||Tracks->Num()!=10)return;
 // Queue while bBusy prevents a worker reading authored fields before they are assigned.
 const bool WasBusy=bBusy;bBusy=true;int32 Added=0;
 for(const auto& Value:*Tracks)
 {
  if(Value->Type!=EJson::Object)continue;auto T=Value->AsObject();FString File,Hash,Name;double B=0.,Offset=0.,Duration=0.;
  if(!T->TryGetStringField(TEXT("file"),File)||FPaths::GetCleanFilename(File)!=File||FPaths::GetExtension(File)!=TEXT("wav")||!T->TryGetStringField(TEXT("sha256"),Hash)||Hash.Len()!=64||!T->TryGetNumberField(TEXT("bpm"),B)||!T->TryGetNumberField(TEXT("first_beat_seconds"),Offset)||!T->TryGetNumberField(TEXT("duration_seconds"),Duration)||!ValidGrid(B,Offset,Duration))continue;
  if(Entries.ContainsByPredicate([&](const auto& E){return E.Hash==Hash&&E.Analysis.IsValid();})){++Added;continue;}
  if(Jobs.ContainsByPredicate([&](const auto& J){return J->ExpectedHash==Hash&&!J->Cancel.Load();}))continue;
  if(!FBootMusicLibraryStore::IsOwnedPath(Folder,Folder/File))continue;
  T->TryGetStringField(TEXT("display_name"),Name);const uint64 Id=ImportFiles({Folder/File});if(!Id)continue;
  for(auto& J:Jobs)if(J->Id==Id){J->bBuiltIn=true;J->ExpectedHash=Hash;J->AuthoredGrid={B,Offset};J->DisplayName=Name;const TArray<TSharedPtr<FJsonValue>>* Sections=nullptr;if(T->TryGetArrayField(TEXT("sections"),Sections)&&Sections->Num()<=FBootTrackAnalysis::MaxSections)for(const auto& S:*Sections){double Frame=0.;if(S->Type==EJson::Object&&S->AsObject()->TryGetNumberField(TEXT("start_frame"),Frame))J->Sections.Add(Frame/48000.);}}
 }
 bBusy=WasBusy;if(Added==10&&FBootMusicLibraryStore::IsOwnedPath(Root,Marker)){IFileManager::Get().MakeDirectory(*Root,true);if(FBootMusicLibraryStore::IsOwnedPath(Root,Marker))FFileHelper::SaveStringToFile(TEXT("1"),*Marker);}StartNext();
}

void UBootMusicLibrarySubsystem::RepairLibrary()
{
 check(IsInGameThread());if(bStopping)return;
 const auto Stale=Entries;const bool WasBusy=bBusy;bBusy=true;
 for(const auto& E:Stale)
 {
  if(E.bBuiltIn||E.Analysis.IsValid()||!FBootMusicLibraryStore::IsOwnedPath(Root,E.SourcePath)||IFileManager::Get().FileSize(*E.SourcePath)<=0)continue;
  if(Jobs.ContainsByPredicate([&](const auto& J){return J->ExpectedHash==E.Hash&&!J->Cancel.Load();}))continue;
  const uint64 Id=ImportFiles({E.SourcePath});for(auto& J:Jobs)if(J->Id==Id)J->ExpectedHash=E.Hash;
 }
 bBusy=WasBusy;StartNext();
}


