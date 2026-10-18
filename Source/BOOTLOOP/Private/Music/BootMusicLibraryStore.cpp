#include "Music/BootMusicLibraryStore.h"
#include "Music/BootMusicAnalysis.h"
#include "Core/BootAtomicFile.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
namespace {
FString Normal(const FString& P){FString N=FPaths::ConvertRelativePathToFull(P);FPaths::NormalizeFilename(N);FPaths::CollapseRelativeDirectories(N);return N;}
bool HashValid(const FString& H){if(H.Len()!=64)return false;for(TCHAR C:H)if(!FChar::IsHexDigit(C))return false;return true;}
FBootDecodeResult DecodeMetadata(const FBootLibraryEntry& E){FBootDecodeResult D;D.ContentHash=E.Hash;D.PcmPath=E.PcmPath;D.Frames=E.Frames;D.Channels=E.Channels;return D;}
}
bool FBootMusicLibraryStore::IsOwnedPath(const FString& Root,const FString& Path)
{
 if(Root.IsEmpty()||Path.IsEmpty()||Path.Contains(TEXT("..")))return false;
 return FPaths::IsUnderDirectory(Normal(Path),Normal(Root))&&IsSafePath(Root)&&IsSafePath(Path);
}
bool FBootMusicLibraryStore::IsSafePath(const FString& Path)
{
#if PLATFORM_WINDOWS
 if(Path.IsEmpty())return false;FString Current=Normal(Path);
 // Deny device paths and alternate data streams as well as directory/file links.
 if(Current.Mid(2).Contains(TEXT(":"))||Current.StartsWith(TEXT("//?"))||Current.StartsWith(TEXT("//.")))return false;
 for(int32 Depth=0;Depth<512;++Depth)
 {
  FString Native=Current;Native.ReplaceInline(TEXT("/"),TEXT("\\"));const uint32 Attributes=GetFileAttributesW(*Native);
  if(Attributes==INVALID_FILE_ATTRIBUTES)
  {
   const uint32 Error=GetLastError();if(Error!=ERROR_FILE_NOT_FOUND&&Error!=ERROR_PATH_NOT_FOUND)return false;
  }
  else if((Attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0)return false;
  if(Current.Len()<=3&&Current.Len()>=2&&Current[1]==TEXT(':'))return Attributes!=INVALID_FILE_ATTRIBUTES;
  const FString Parent=FPaths::GetPath(Current);if(Parent.IsEmpty()||Parent==Current)return Attributes!=INVALID_FILE_ATTRIBUTES;Current=Parent;
 }
#endif
 return false;
}
int64 FBootMusicLibraryStore::ManagedBytes(const FString& Root)
{
 // Never recursively enter an unverified directory: a single unsafe branch fails the reservation.
 if(!IsSafePath(Root))return CacheBudget+1;IPlatformFile& Platform=FPlatformFileManager::Get().GetPlatformFile();if(!Platform.DirectoryExists(*Root))return 0;
 TArray<FString> Pending{Root};int64 Total=0;int32 Visited=0;
 while(!Pending.IsEmpty())
 {
  const FString Directory=Pending.Pop(EAllowShrinking::No);if(!IsSafePath(Directory))return CacheBudget+1;
  const bool Ok=Platform.IterateDirectory(*Directory,[&](const TCHAR* Filename,bool bDirectory)
  {
   const FString P=Filename;if(++Visited>65536||!IsOwnedPath(Root,P))return false;
   if(bDirectory){if(Pending.Num()>=4096)return false;Pending.Add(P);}
   else {const int64 N=Platform.FileSize(Filename);if(N<0||N>CacheBudget||Total>CacheBudget-N)return false;Total+=N;}return true;
  });
  if(!Ok)return CacheBudget+1;
 }
 return Total;
}
bool FBootMusicLibraryStore::CanReserve(const FString& Root,int64 N){return N>=0&&N<=CacheBudget&&ManagedBytes(Root)<=CacheBudget-N;}
void FBootMusicLibraryStore::DeleteOwned(const FString& Root,const FString& Path){if(IsOwnedPath(Root,Path))IFileManager::Get().Delete(*Path,false,true);}
bool FBootMusicLibraryStore::Save(const FString& Root,const TArray<FBootLibraryEntry>& Entries,FText& Error)
{
 const FString Path=Root/TEXT("library.json"),Temp=Path+TEXT(".tmp");
 if(!IsOwnedPath(Root,Path)||!IsOwnedPath(Root,Temp)){Error=FText::FromString(TEXT("Unsafe library storage path"));return false;}
 if(Entries.Num()>512){Error=FText::FromString(TEXT("Library entry limit exceeded"));return false;}
 TSharedRef<FJsonObject> Document=MakeShared<FJsonObject>();Document->SetNumberField(TEXT("schema"),2);TArray<TSharedPtr<FJsonValue>> Rows;TSet<FString> Seen;
 for(const auto& E:Entries)
 {
  if(!HashValid(E.Hash)||Seen.Contains(E.Hash))continue;Seen.Add(E.Hash);
  auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("hash"),E.Hash);R->SetStringField(TEXT("name"),E.DisplayName.Left(256));
  R->SetStringField(TEXT("source"),E.SourcePath);R->SetStringField(TEXT("pcm"),E.PcmPath);R->SetBoolField(TEXT("included"),E.bIncluded);R->SetBoolField(TEXT("builtin"),E.bBuiltIn);
  R->SetBoolField(TEXT("override"),E.bHasGridOverride);R->SetBoolField(TEXT("safe"),E.bSafeGridAccepted);R->SetNumberField(TEXT("bpm"),E.Grid.Bpm);R->SetNumberField(TEXT("first"),E.Grid.FirstBeatSeconds);
  R->SetNumberField(TEXT("frames"),double(E.Frames));R->SetNumberField(TEXT("channels"),E.Channels);R->SetNumberField(TEXT("revision"),double(E.Revision));Rows.Add(MakeShared<FJsonValueObject>(R));
 }
 Document->SetArrayField(TEXT("entries"),Rows);FString Json;auto Writer=TJsonWriterFactory<>::Create(&Json);
 if(!FJsonSerializer::Serialize(Document,Writer)){Error=FText::FromString(TEXT("Library serialization failed"));return false;}
 IFileManager::Get().MakeDirectory(*Root,true);if(!IsOwnedPath(Root,Path)||!IsOwnedPath(Root,Temp)){Error=FText::FromString(TEXT("Unsafe library storage path"));return false;}
 if(!FFileHelper::SaveStringToFile(Json,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)||!FBootAtomicFile::Replace(Temp,Path)){DeleteOwned(Root,Temp);Error=FText::FromString(TEXT("Cannot atomically save library"));return false;}return true;
}
bool FBootMusicLibraryStore::Load(const FString& Root,TArray<FBootLibraryEntry>& Out,FText& Error)
{
 Out.Reset();const FString Path=Root/TEXT("library.json");if(!IsOwnedPath(Root,Path)){Error=FText::FromString(TEXT("Unsafe library manifest path"));return false;}const int64 Size=IFileManager::Get().FileSize(*Path);if(Size<0)return true;
 if(Size<=0||Size>2*1024*1024){Error=FText::FromString(TEXT("Invalid library manifest size"));return false;}
 FString Json;TSharedPtr<FJsonObject> Doc;if(!FFileHelper::LoadFileToString(Json,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Doc)||!Doc){Error=FText::FromString(TEXT("Invalid library manifest"));return false;}
 double Schema=0.;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(!Doc->TryGetNumberField(TEXT("schema"),Schema)||Schema<1||Schema>2||!Doc->TryGetArrayField(TEXT("entries"),Rows)||Rows->Num()>512){Error=FText::FromString(TEXT("Unsupported library schema"));return false;}
 TSet<FString> Seen;for(const auto& V:*Rows)
 {
  const TSharedPtr<FJsonObject> R=V->Type==EJson::Object?V->AsObject():nullptr;if(!R)continue;FBootLibraryEntry E;
  if(!R->TryGetStringField(TEXT("hash"),E.Hash)||!HashValid(E.Hash)||Seen.Contains(E.Hash))continue;Seen.Add(E.Hash);
  R->TryGetStringField(TEXT("name"),E.DisplayName);E.DisplayName=E.DisplayName.Left(256);R->TryGetStringField(TEXT("source"),E.SourcePath);R->TryGetStringField(TEXT("pcm"),E.PcmPath);
  R->TryGetBoolField(TEXT("included"),E.bIncluded);R->TryGetBoolField(TEXT("builtin"),E.bBuiltIn);R->TryGetBoolField(TEXT("override"),E.bHasGridOverride);R->TryGetBoolField(TEXT("safe"),E.bSafeGridAccepted);
  R->TryGetNumberField(TEXT("bpm"),E.Grid.Bpm);R->TryGetNumberField(TEXT("first"),E.Grid.FirstBeatSeconds);double Frames=0.,Channels=0.,Revision=1.;R->TryGetNumberField(TEXT("frames"),Frames);R->TryGetNumberField(TEXT("channels"),Channels);R->TryGetNumberField(TEXT("revision"),Revision);
  if(!FMath::IsFinite(Frames)||Frames<0||Frames>48000.*600||!FMath::IsFinite(Channels)||(Channels!=0&&Channels!=1&&Channels!=2))continue;
  E.Frames=int64(Frames);E.Channels=int32(Channels);E.Revision=FMath::IsFinite(Revision)&&Revision>=1&&Revision<9007199254740991.?uint64(Revision):1;E.DurationSeconds=Frames/48000.;
  if(!FMath::IsFinite(E.Grid.Bpm)||E.Grid.Bpm<30||E.Grid.Bpm>300||!FMath::IsFinite(E.Grid.FirstBeatSeconds)||E.Grid.FirstBeatSeconds<0||E.Grid.FirstBeatSeconds>(E.DurationSeconds>0?E.DurationSeconds:600.)){E.bHasGridOverride=false;E.Grid={130.,0.};}
  // Never trust arbitrary paths in the saved manifest, including built-in paths.
  const FString Starter=Normal(FPaths::ProjectContentDir()/TEXT("Music/Starter"));
  const bool SourceSafe=IsOwnedPath(Root/TEXT("Sources"),E.SourcePath)||(E.bBuiltIn&&IsOwnedPath(Starter,E.SourcePath)&&FPaths::GetPath(Normal(E.SourcePath))==Starter&&FPaths::GetExtension(E.SourcePath)==TEXT("wav"));
  if(!SourceSafe||!IsOwnedPath(Root/TEXT("Cache"),E.PcmPath)||!IsOwnedPath(Root/TEXT("Cache"),E.PcmPath+TEXT(".features"))){E.SourcePath.Reset();E.PcmPath.Reset();E.State=EBootLibraryState::Failed;E.Error=TEXT("Untrusted library file path");Out.Add(MoveTemp(E));continue;}
  auto D=DecodeMetadata(E);FBootTrackAnalysis A;
  if(E.Frames>0&&(E.Channels==1||E.Channels==2)&&IFileManager::Get().FileSize(*E.PcmPath)==E.Frames*E.Channels*2&&FBootMusicAnalyzer::LoadCache(E.PcmPath+TEXT(".features"),D,A))
  {
   if(E.bHasGridOverride)A=FBootMusicGridRules::WithOverride(A,E.Grid.Bpm,E.Grid.FirstBeatSeconds);else E.Grid=A.Grid;
   E.Analysis=MakeShared<const FBootTrackAnalysis,ESPMode::ThreadSafe>(MoveTemp(A));E.State=E.Analysis->Source==EBootGridSource::Safe&&!E.bSafeGridAccepted?EBootLibraryState::NeedsReview:EBootLibraryState::Ready;E.Progress=1.;
  }
  else {E.State=EBootLibraryState::Failed;E.Error=TEXT("Missing or stale PCM/analysis cache; reimport to rebuild");}
  Out.Add(MoveTemp(E));
 }
 return true;
}


