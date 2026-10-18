#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Profile/BootProfile.h"
#include "Kismet/GameplayStatics.h"
#include <limits>
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootMusicProfileTest,"Bootloop.V04.Flow.VersionedProfile",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootMusicProfileTest::RunTest(const FString&)
{
    auto* P=NewObject<UBootProfile>();P->BestScore=123;P->BestDistance=456;P->Runs=7;P->bTutorialComplete=true;
    TestFalse(TEXT("v03 completion does not bypass new onboarding"),P->HasCurrentOnboarding());
    P->UpdateMusicRunRecord(int64(MAX_int32)+500,2000.125,601.75);
    P->UpdateMusicRunRecord(100,800.5,100.);
    TestEqual(TEXT("wide best score"),P->MusicRunRecord.BestScore,int64(MAX_int32)+500);
    TestEqual(TEXT("distance precision"),P->MusicRunRecord.BestDistanceMetres,2000.125);
    P->UpdateMusicRunRecord(-1,std::numeric_limits<double>::quiet_NaN(),-1.);
    TestEqual(TEXT("invalid record ignored"),P->MusicRunRecord.BestActiveSeconds,601.75);
    P->OnboardingVersion=4;TArray<uint8> Bytes;
    TestTrue(TEXT("serialize memory without touching normal profile"),UGameplayStatics::SaveGameToMemory(P,Bytes));
    auto* Loaded=Cast<UBootProfile>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if(!TestNotNull(TEXT("profile loaded"),Loaded))return false;
    TestTrue(TEXT("onboarding roundtrip"),Loaded->HasCurrentOnboarding());
    TestEqual(TEXT("new record roundtrip"),Loaded->MusicRunRecord.BestScore,int64(MAX_int32)+500);
    TestEqual(TEXT("old record intact"),Loaded->BestScore,123);TestEqual(TEXT("old runs intact"),Loaded->Runs,7);
    TestEqual(TEXT("old distance intact"),Loaded->BestDistance,456.f);
    return true;
}
#endif
