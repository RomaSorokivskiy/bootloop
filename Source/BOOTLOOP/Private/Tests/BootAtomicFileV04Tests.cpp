#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Core/BootAtomicFile.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootAtomicReplacementTest,"Bootloop.V04.Storage.AtomicFailurePreservesPrior",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootAtomicReplacementTest::RunTest(const FString&)
{
    const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("V04Audit/Atomic")/FGuid::NewGuid().ToString());
    IFileManager::Get().MakeDirectory(*Dir,true);const FString Final=Dir/TEXT("state.json"),Temp=Final+TEXT(".tmp");
    FFileHelper::SaveStringToFile(TEXT("prior-valid"),*Final);
    TestFalse(TEXT("missing temp rejected"),FBootAtomicFile::Replace(Temp,Final));
    FString Value;FFileHelper::LoadFileToString(Value,*Final);
    TestEqual(TEXT("failed rename retains prior final"),Value,FString(TEXT("prior-valid")));
    FFileHelper::SaveStringToFile(TEXT("next-valid"),*Temp);
    TestTrue(TEXT("valid replacement"),FBootAtomicFile::Replace(Temp,Final));
    Value.Reset();FFileHelper::LoadFileToString(Value,*Final);TestEqual(TEXT("new final visible"),Value,FString(TEXT("next-valid")));
    TestFalse(TEXT("temp consumed once"),IFileManager::Get().FileExists(*Temp));
    FFileHelper::SaveStringToFile(TEXT("locked-next"),*Temp);
    // Windows read handle denies deletion/rename of this source. A failed
    // replacement must keep the prior valid final, even after temp creation.
    TUniquePtr<IFileHandle> Lock(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Temp));
    TestFalse(TEXT("locked temp rename rejected"),FBootAtomicFile::Replace(Temp,Final));
    Value.Reset();FFileHelper::LoadFileToString(Value,*Final);TestEqual(TEXT("locked replacement retains prior final"),Value,FString(TEXT("next-valid")));
    Lock.Reset();IFileManager::Get().DeleteDirectory(*Dir,false,true);return true;
}
#endif
