#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Engine/GameInstance.h"
#include "Music/BootMusicLibraryStore.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootLibraryPersistence,"Bootloop.V04.Library.DedupPersistence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootLibraryPersistence::RunTest(const FString&)
{
 const FString Root=FPaths::ProjectSavedDir()/TEXT("V04Audit/LibraryTests")/FGuid::NewGuid().ToString(); FText Error;
 FBootLibraryEntry E; E.Hash=FString::ChrN(64,TEXT('a')); E.DisplayName=TEXT("renamed"); E.bIncluded=false; E.bHasGridOverride=true; E.Grid={142.,.2}; E.PcmPath=Root/TEXT("track.pcm");
 TArray<FBootLibraryEntry> Entries{E,E}; TestTrue(TEXT("atomic save"),FBootMusicLibraryStore::Save(Root,Entries,Error));
 TArray<FBootLibraryEntry> Loaded; TestTrue(TEXT("restart load"),FBootMusicLibraryStore::Load(Root,Loaded,Error));
 TestEqual(TEXT("dedup"),Loaded.Num(),1); if(Loaded.Num()){TestFalse(TEXT("checkbox persists"),Loaded[0].bIncluded);TestEqual(TEXT("name persists"),Loaded[0].DisplayName,E.DisplayName);TestEqual(TEXT("grid persists"),Loaded[0].Grid.Bpm,142.);}
 IFileManager::Get().DeleteDirectory(*Root,false,true); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootLibraryBudgets,"Bootloop.V04.Library.Budgets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootLibraryBudgets::RunTest(const FString&)
{
 const FString Root=FPaths::ProjectSavedDir()/TEXT("V04Audit/LibraryTests");
 TestFalse(TEXT("over 2GiB rejected before allocation"),FBootMusicLibraryStore::CanReserve(Root,FBootMusicLibraryStore::CacheBudget+1));
 TestFalse(TEXT("negative reservation rejected"),FBootMusicLibraryStore::CanReserve(Root,-1));
 TestFalse(TEXT("sibling is not owned"),FBootMusicLibraryStore::IsOwnedPath(Root,Root+TEXT("Sibling/track.pcm")));
 TestFalse(TEXT("traversal is not owned"),FBootMusicLibraryStore::IsOwnedPath(Root,Root/TEXT("../outside.pcm")));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootLibraryLease,"Bootloop.V04.Library.LeaseAndGrid",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootLibraryLease::RunTest(const FString&)
{
 int32 Cleaned=0; TSharedPtr<FBootPlaybackSource,ESPMode::ThreadSafe> Source=MakeShared<FBootPlaybackSource,ESPMode::ThreadSafe>(); auto Analysis=MakeShared<FBootTrackAnalysis,ESPMode::ThreadSafe>();Analysis->Grid.Bpm=130.;Source->Analysis=Analysis;Source->Cleanup=[&Cleaned]{++Cleaned;};
 FBootPlaybackLease Lease;Lease.Source=Source;Source.Reset();TestTrue(TEXT("lease holds source"),Lease.IsValid());TestEqual(TEXT("not cleaned while leased"),Cleaned,0);TestEqual(TEXT("immutable snapshot grid"),Lease.Source->Analysis->Grid.Bpm,130.);Lease.Source.Reset();TestEqual(TEXT("cleanup on final release"),Cleaned,1);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootLibraryCancel,"Bootloop.V04.Library.CancelAndShutdown",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootLibraryCancel::RunTest(const FString&)
{
 auto* GameInstance=NewObject<UGameInstance>(); auto* Library=NewObject<UBootMusicLibrarySubsystem>(GameInstance); const uint64 Job=Library->ImportFiles({TEXT("not-a-real-track.wav")});TestTrue(TEXT("job allocated"),Job>0);Library->CancelImport(Job);TestEqual(TEXT("cancel cannot resurrect entry"),Library->Snapshot().Num(),0);Library->Deinitialize();return true;
}
// Public-pipeline regression coverage: bounded WAV input, actual import workers, isolated roots.
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/PlatformTime.h"
namespace {
struct FLibraryPipelineFixture
{
 TStrongObjectPtr<UGameInstance> Game{NewObject<UGameInstance>()};
 TStrongObjectPtr<UBootMusicLibrarySubsystem> Library{NewObject<UBootMusicLibrarySubsystem>(Game.Get())};
 FString Parent=FPaths::ProjectSavedDir()/TEXT("V04Audit/LibraryTests")/FGuid::NewGuid().ToString();
 FString Root=Parent/TEXT("Library"), Original=Parent/TEXT("original.wav");
 TArray<uint8> Bytes;
 double Started=FPlatformTime::Seconds();
 FBootPlaybackLease Before,After;
 bool Create()
 {
  const int32 Frames=48000*3; Bytes.SetNumZeroed(44+Frames*2);
  auto Put=[&](int32 At,uint32 V,int32 N){for(int32 I=0;I<N;++I)Bytes[At+I]=uint8(V>>(I*8));};
  FMemory::Memcpy(Bytes.GetData(),"RIFF",4);Put(4,Bytes.Num()-8,4);FMemory::Memcpy(Bytes.GetData()+8,"WAVEfmt ",8);Put(16,16,4);Put(20,1,2);Put(22,1,2);Put(24,48000,4);Put(28,96000,4);Put(32,2,2);Put(34,16,2);FMemory::Memcpy(Bytes.GetData()+36,"data",4);Put(40,Frames*2,4);
  for(int32 I=0;I<Frames;++I){const int16 Sample=I%24000<240?int16(20000*FMath::Sin(I*.2)):0;Put(44+I*2,uint16(Sample),2);}
  IFileManager::Get().MakeDirectory(*Parent,true);return FFileHelper::SaveArrayToFile(Bytes,*Original)&&Library->InitializeFixture(Root);
 }
 bool OriginalUntouched()const{TArray<uint8> Actual;return FFileHelper::LoadFileToArray(Actual,*Original)&&Actual==Bytes;}
 void Cleanup(){Before.Source.Reset();After.Source.Reset();Library->Deinitialize();IFileManager::Get().DeleteDirectory(*Parent,false,true);}
};
class FLibraryPipelineCommand : public IAutomationLatentCommand
{
 FAutomationTestBase* Test; TSharedPtr<FLibraryPipelineFixture> F; int32 Phase=0; bool LeaseTest;
public:
 FLibraryPipelineCommand(FAutomationTestBase* T,bool L):Test(T),F(MakeShared<FLibraryPipelineFixture>()),LeaseTest(L){}
 bool Update()override
 {
  if(Phase==0){if(!F->Create()){Test->AddError(TEXT("isolated library fixture startup"));F->Cleanup();return true;}F->Library->ImportFiles({F->Original,F->Original});Phase=1;return false;}
  if(FPlatformTime::Seconds()-F->Started>20.){Test->AddError(TEXT("library import timed out"));F->Cleanup();return true;}
  auto Entries=F->Library->Snapshot();
  if(Phase==1)
  {
   if(Entries.IsEmpty())return false;
   for(const auto& E:Entries){if(E.State==EBootLibraryState::Queued||E.State==EBootLibraryState::Decoding||E.State==EBootLibraryState::Analyzing)return false;if(E.State==EBootLibraryState::Failed){Test->AddError(E.Error);F->Cleanup();return true;}}
   Test->TestEqual(TEXT("duplicate import one entry"),Entries.Num(),1);if(Entries.Num()!=1){F->Cleanup();return true;}
   const FString Hash=Entries[0].Hash;FText Error;F->Library->AcceptSafeGrid(Hash,Error);
   Test->TestTrue(TEXT("manual grid accepted"),F->Library->SetGridOverride(Hash,140.,.1,Error));
   if(!LeaseTest)
   {
    F->Library->SetIncluded(Hash,false);F->Library->SetDisplayName(Hash,TEXT("persisted user title"));
    auto* Restart=NewObject<UBootMusicLibrarySubsystem>(F->Game.Get());Test->TestTrue(TEXT("restart fixture"),Restart->InitializeFixture(F->Root));const auto Loaded=Restart->Snapshot();Test->TestEqual(TEXT("dedup survives restart"),Loaded.Num(),1);
    if(Loaded.Num()){Test->TestFalse(TEXT("exclude survives restart"),Loaded[0].bIncluded);Test->TestEqual(TEXT("name survives restart"),Loaded[0].DisplayName,FString(TEXT("persisted user title")));Test->TestEqual(TEXT("manual grid survives restart"),Loaded[0].Grid.Bpm,140.);}
    Restart->Deinitialize();Test->TestTrue(TEXT("original untouched"),F->OriginalUntouched());F->Cleanup();return true;
   }
   F->Before=F->Library->Acquire(Hash,Error);Test->TestTrue(TEXT("acquire ready PCM"),F->Before.IsValid());
   Test->TestTrue(TEXT("next playback override accepted"),F->Library->SetGridOverride(Hash,150.,.2,Error));F->After=F->Library->Acquire(Hash,Error);
   if(F->Before.IsValid()&&F->After.IsValid()){Test->TestEqual(TEXT("active lease keeps old grid"),F->Before.Source->Analysis->Grid.Bpm,140.);Test->TestEqual(TEXT("next lease uses new grid"),F->After.Source->Analysis->Grid.Bpm,150.);Test->TestTrue(TEXT("analysis snapshot changes"),F->Before.Source->Analysis!=F->After.Source->Analysis);}
   Test->TestTrue(TEXT("remove library entry"),F->Library->Remove(Hash,Error));Test->TestEqual(TEXT("removed entry absent"),F->Library->Snapshot().Num(),0);
   if(F->Before.IsValid())Test->TestTrue(TEXT("leased PCM retained after remove"),IFileManager::Get().FileExists(*F->Before.Source->PcmPath));
   Phase=2;return false;
  }
  if(Phase==2){if(F->Before.IsValid())F->Root=F->Before.Source->PcmPath;F->Before.Source.Reset();F->After.Source.Reset();Phase=3;return false;}
  Test->TestFalse(TEXT("owned PCM removed after final lease"),IFileManager::Get().FileExists(*F->Root));Test->TestTrue(TEXT("original untouched"),F->OriginalUntouched());F->Cleanup();return true;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootLibraryImportPipeline,"Bootloop.V04.Library.DedupImportPipeline",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootLibraryImportPipeline::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(FLibraryPipelineCommand(this,false));return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootLibraryLeasePipeline,"Bootloop.V04.Library.LeaseGridPipeline",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootLibraryLeasePipeline::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(FLibraryPipelineCommand(this,true));return true;}

class FLibraryCancelPipelineCommand : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;TSharedPtr<FLibraryPipelineFixture> F;int32 Phase=0;double CancelledAt=0.;
public:
 explicit FLibraryCancelPipelineCommand(FAutomationTestBase* T):Test(T),F(MakeShared<FLibraryPipelineFixture>()){}
 bool Update()override
 {
  if(Phase==0){if(!F->Create()){Test->AddError(TEXT("cancel fixture startup"));F->Cleanup();return true;}const uint64 Id=F->Library->ImportFiles({F->Original});Test->TestTrue(TEXT("real decode job allocated"),Id>0);const auto S=F->Library->Snapshot();Test->TestTrue(TEXT("in-flight decoding observed"),S.Num()==1&&S[0].State==EBootLibraryState::Decoding);F->Library->CancelImport(Id);F->Library->Deinitialize();CancelledAt=FPlatformTime::Seconds();Phase=1;return false;}
  if(FPlatformTime::Seconds()-CancelledAt<1.)return false;
  Test->TestEqual(TEXT("shutdown callback cannot resurrect cancelled entry"),F->Library->Snapshot().Num(),0);Test->TestTrue(TEXT("original untouched after cancellation"),F->OriginalUntouched());F->Cleanup();return true;
 }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootLibraryCancelPipeline,"Bootloop.V04.Library.CancelShutdownPipeline",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootLibraryCancelPipeline::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(FLibraryCancelPipelineCommand(this));return true;}

#include "Serialization/MemoryReader.h"
#include "Music/BootMusicAnalysis.h"
class FLibraryCacheRebuildCommand : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;TSharedPtr<FLibraryPipelineFixture> F;int32 Phase=0;FString Hash;
public:
 explicit FLibraryCacheRebuildCommand(FAutomationTestBase* T):Test(T),F(MakeShared<FLibraryPipelineFixture>()){}
 bool Update()override
 {
  if(Phase==0){if(!F->Create()){Test->AddError(TEXT("cache version fixture startup"));F->Cleanup();return true;}F->Library->ImportFiles({F->Original});Phase=1;return false;}
  if(FPlatformTime::Seconds()-F->Started>25.){Test->AddError(TEXT("cache rebuild timeout"));F->Cleanup();return true;}
  const auto S=F->Library->Snapshot();for(const auto& E:S)if(E.State==EBootLibraryState::Queued||E.State==EBootLibraryState::Decoding||E.State==EBootLibraryState::Analyzing)return false;
  if(S.Num()!=1||!S[0].Analysis){Test->AddError(TEXT("cache fixture import/rebuild failed"));F->Cleanup();return true;}
  if(Phase==1)
  {
   Hash=S[0].Hash;FText Error;F->Library->SetGridOverride(Hash,147.,.2,Error);F->Library->SetIncluded(Hash,false);F->Library->SetDisplayName(Hash,TEXT("version retained name"));
   TArray<uint8> Cache;const FString Path=S[0].PcmPath+TEXT(".features");if(!FFileHelper::LoadFileToArray(Cache,*Path)){Test->AddError(TEXT("read feature cache"));F->Cleanup();return true;}
   FMemoryReader Reader(Cache);uint32 Magic=0;FString Key;Reader<<Magic<<Key;const int64 Offset=Reader.Tell();const uint32 Old=FBootMusicAnalyzer::AnalysisVersion-1;if(Offset+4>Cache.Num()){Test->AddError(TEXT("feature cache header bounds"));F->Cleanup();return true;}FMemory::Memcpy(Cache.GetData()+Offset,&Old,4);FFileHelper::SaveArrayToFile(Cache,*Path);
   F->Library->Deinitialize();F->Library.Reset(NewObject<UBootMusicLibrarySubsystem>(F->Game.Get()));Test->TestTrue(TEXT("version restart loads metadata"),F->Library->InitializeFixture(F->Root));const auto Stale=F->Library->Snapshot();Test->TestTrue(TEXT("old analysis rejected"),Stale.Num()==1&&!Stale[0].Analysis);
   F->Library->RepairLibrary();Phase=2;return false;
  }
  Test->TestEqual(TEXT("analysis version rebuilt"),S[0].Analysis->Version,FBootMusicAnalyzer::AnalysisVersion);Test->TestFalse(TEXT("disabled checkbox survives rebuild"),S[0].bIncluded);Test->TestEqual(TEXT("name survives rebuild"),S[0].DisplayName,FString(TEXT("version retained name")));Test->TestEqual(TEXT("manual grid reapplied"),S[0].Analysis->Grid.Bpm,147.);Test->TestTrue(TEXT("source original untouched"),F->OriginalUntouched());F->Cleanup();return true;
 }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootLibraryCacheRebuild,"Bootloop.V04.Library.CacheVersionRebuild",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootLibraryCacheRebuild::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(FLibraryCacheRebuildCommand(this));return true;}
class FLibraryBootstrapRecoveryCommand : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;TSharedPtr<FLibraryPipelineFixture> F;int32 Phase=0;double At=0.;
 void CancelAll(){const auto S=F->Library->Snapshot();FText Error;for(const auto& E:S)F->Library->Remove(E.Hash,Error);F->Library->Deinitialize();}
public:
 explicit FLibraryBootstrapRecoveryCommand(FAutomationTestBase* T):Test(T),F(MakeShared<FLibraryPipelineFixture>()){}
 bool Update()override
 {
  if(Phase==0)
  {
   if(!F->Create()){Test->AddError(TEXT("bootstrap fixture startup"));F->Cleanup();return true;}F->Library->BootstrapStarterLibrary();Test->TestEqual(TEXT("ten explicit starter jobs"),F->Library->Snapshot().Num(),10);F->Library->BootstrapStarterLibrary();Test->TestEqual(TEXT("pending starters not duplicated"),F->Library->Snapshot().Num(),10);Test->TestFalse(TEXT("queued imports do not mark completed seed"),IFileManager::Get().FileExists(*(F->Root/TEXT("starter-seeded-v1"))));CancelAll();At=FPlatformTime::Seconds();Phase=1;return false;
  }
  if(FPlatformTime::Seconds()-At<1.)return false;
  if(Phase==1)
  {
   // Simulate a previous interrupted version that prematurely persisted its marker.
   FFileHelper::SaveStringToFile(TEXT("1"),*(F->Root/TEXT("starter-seeded-v1")));F->Library.Reset(NewObject<UBootMusicLibrarySubsystem>(F->Game.Get()));F->Library->InitializeFixture(F->Root);Test->TestEqual(TEXT("Initialize does not bootstrap"),F->Library->Snapshot().Num(),0);F->Library->BootstrapStarterLibrary();Test->TestEqual(TEXT("interrupted bootstrap repairs despite stale marker"),F->Library->Snapshot().Num(),10);CancelAll();At=FPlatformTime::Seconds();Phase=2;return false;
  }
  Test->TestTrue(TEXT("bootstrap leaves fixture original untouched"),F->OriginalUntouched());F->Cleanup();return true;
 }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootLibraryBootstrapRecovery,"Bootloop.V04.Library.BootstrapInterruptedRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootLibraryBootstrapRecovery::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(FLibraryBootstrapRecoveryCommand(this));return true;}

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <winioctl.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
namespace {
bool MakeAuditJunction(const FString& Link,const FString& Target)
{
 FString Native=FPaths::ConvertRelativePathToFull(Link),Destination=FPaths::ConvertRelativePathToFull(Target);Native.ReplaceInline(TEXT("/"),TEXT("\\"));Destination.ReplaceInline(TEXT("/"),TEXT("\\"));
 if(!CreateDirectoryW(*Native,nullptr))return false;
 HANDLE Handle=CreateFileW(*Native,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);if(Handle==INVALID_HANDLE_VALUE){RemoveDirectoryW(*Native);return false;}
 const FString Substitute=TEXT("\\??\\")+Destination;const uint16 SubBytes=uint16(Substitute.Len()*sizeof(WCHAR)),PrintBytes=uint16(Destination.Len()*sizeof(WCHAR));
 TArray<uint8> Buffer;Buffer.SetNumZeroed(16+SubBytes+2+PrintBytes+2);const DWORD Tag=IO_REPARSE_TAG_MOUNT_POINT;const uint16 DataLength=uint16(Buffer.Num()-8),Zero=0,PrintOffset=SubBytes+2;
 FMemory::Memcpy(Buffer.GetData(),&Tag,4);FMemory::Memcpy(Buffer.GetData()+4,&DataLength,2);FMemory::Memcpy(Buffer.GetData()+8,&Zero,2);FMemory::Memcpy(Buffer.GetData()+10,&SubBytes,2);FMemory::Memcpy(Buffer.GetData()+12,&PrintOffset,2);FMemory::Memcpy(Buffer.GetData()+14,&PrintBytes,2);FMemory::Memcpy(Buffer.GetData()+16,*Substitute,SubBytes);FMemory::Memcpy(Buffer.GetData()+16+PrintOffset,*Destination,PrintBytes);
 DWORD Returned=0;const bool Ok=DeviceIoControl(Handle,FSCTL_SET_REPARSE_POINT,Buffer.GetData(),Buffer.Num(),nullptr,0,&Returned,nullptr)!=0;CloseHandle(Handle);if(!Ok)RemoveDirectoryW(*Native);return Ok;
}
void RemoveAuditJunction(const FString& Link){FString Native=FPaths::ConvertRelativePathToFull(Link);Native.ReplaceInline(TEXT("/"),TEXT("\\"));RemoveDirectoryW(*Native);}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootLibraryJunctionBoundary,"Bootloop.V04.Library.JunctionBoundary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootLibraryJunctionBoundary::RunTest(const FString&)
{
 FLibraryPipelineFixture Fixture;if(!Fixture.Create()){AddError(TEXT("junction fixture startup"));Fixture.Cleanup();return true;}
 const FString Outside=Fixture.Parent/TEXT("Outside"),Sentinel=Outside/TEXT("original.wav"),Link=Fixture.Root/TEXT("Sources/linked"),Escaped=Link/TEXT("original.wav");
 IFileManager::Get().MakeDirectory(*Outside,true);IFileManager::Get().MakeDirectory(*(Fixture.Root/TEXT("Sources")),true);IFileManager::Get().MakeDirectory(*(Fixture.Root/TEXT("Cache")),true);FFileHelper::SaveArrayToFile(Fixture.Bytes,*Sentinel);
 FBootDecodeRequest Request;Request.SourcePath=Sentinel;Request.TempPcmPath=Fixture.Parent/TEXT("probe.pcm");TAtomic<bool> Cancel(false);auto Decoder=IBootMusicDecoder::Create();const auto D=Decoder->Decode(Request,Cancel,[](double){});
 if(D.ContentHash.Len()!=64||!MakeAuditJunction(Link,Outside)){AddError(TEXT("real SHA256 decode / Windows audit junction creation failed"));Fixture.Cleanup();return true;}
 FBootLibraryEntry E;E.Hash=D.ContentHash;E.DisplayName=TEXT("junction sentinel");E.SourcePath=Escaped;E.PcmPath=Fixture.Root/TEXT("Cache/missing.pcm");E.Frames=D.Frames;E.Channels=D.Channels;
 FText Error;TestTrue(TEXT("write current-schema audit manifest"),FBootMusicLibraryStore::Save(Fixture.Root,{E},Error));
 TestFalse(TEXT("junction child is not physically owned"),FBootMusicLibraryStore::IsOwnedPath(Fixture.Root,Escaped));TestFalse(TEXT("junction root itself is rejected"),FBootMusicLibraryStore::IsOwnedPath(Link,Escaped));
 TArray<FBootLibraryEntry> Loaded;TestTrue(TEXT("load manifest safely"),FBootMusicLibraryStore::Load(Fixture.Root,Loaded,Error));TestTrue(TEXT("unsafe source rejected by Load"),Loaded.Num()==1&&Loaded[0].SourcePath.IsEmpty());
 TestTrue(TEXT("subsystem reload audit manifest"),Fixture.Library->InitializeFixture(Fixture.Root));TestTrue(TEXT("remove untrusted entry safely"),Fixture.Library->Remove(D.ContentHash,Error));TArray<uint8> After;TestTrue(TEXT("outside sentinel unchanged after Remove"),FFileHelper::LoadFileToArray(After,*Sentinel)&&After==Fixture.Bytes);
 // Exercise the cleanup guard independently, even if Load already rejected the entry.
 FFileHelper::SaveArrayToFile(Fixture.Bytes,*Sentinel);FBootMusicLibraryStore::DeleteOwned(Fixture.Root,Escaped);After.Reset();TestTrue(TEXT("outside sentinel unchanged after direct cleanup"),FFileHelper::LoadFileToArray(After,*Sentinel)&&After==Fixture.Bytes);
 RemoveAuditJunction(Link);Fixture.Cleanup();return true;
}
#endif

#endif





