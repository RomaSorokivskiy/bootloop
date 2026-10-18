#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Run/BootRunScore.h"
#include <limits>
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootScoreV04,"Bootloop.V04.Score.FutureOnlyAndDedup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootScoreV04::RunTest(const FString&)
{
    const FBootRunToken Run{FGuid(1,2,3,4),1},Other{FGuid(1,2,3,4),2};
    FBootRunScore S;S.Reset(Run);S.AdvanceDistance(100.,1.);S.AdvanceDistance(200.,2.);
    TestEqual(TEXT("future distance rewards only"),S.GetScore(),int64(3000));
    S.AdvanceDistance(190.,3.);S.AdvanceDistance(std::numeric_limits<double>::quiet_NaN(),1.);
    TestEqual(TEXT("backtracking nonfinite cannot mint points"),S.GetScore(),int64(3000));
    const int64 Id=int64(MAX_int32)+42;const FBootOperationKey Key{Run,Id,3};
    S.RegisterChunk(Id,{Key});
    TestFalse(TEXT("unknown operation"),S.AwardOperation({Run,Id,4},EBootBeatRating::Perfect,1.));
    TestFalse(TEXT("stale run"),S.AwardOperation({Other,Id,3},EBootBeatRating::Perfect,1.));
    TestTrue(TEXT("registered operation"),S.AwardOperation(Key,EBootBeatRating::Perfect,2.));
    TestEqual(TEXT("operation future multiplier"),S.GetScore(),int64(3600));
    TestFalse(TEXT("duplicate operation"),S.AwardOperation(Key,EBootBeatRating::Perfect,3.));
    const FBootOperationKey Unused{Run,Id,5};S.RegisterChunk(Id,{Unused});S.RetireChunk(Id);
    TestFalse(TEXT("retired reward"),S.AwardOperation(Unused,EBootBeatRating::Perfect,1.));
    S.RegisterChunk(Id,{Unused});TestFalse(TEXT("retired id cannot resurrect"),S.AwardOperation(Unused,EBootBeatRating::Perfect,1.));
    TestEqual(TEXT("retirement bounds dedup"),S.GetTrackedOperationCount(),0);
    FBootRunScore A,B;A.Reset(Run);B.Reset(Run);A.AdvanceDistance(123.456,1.2);
    for(int32 I=1;I<=10000;++I)B.AdvanceDistance(123.456*I/10000.,1.2);
    TestEqual(TEXT("partition independent accrual"),A.GetScore(),B.GetScore());
    S.Reset(Other);TestEqual(TEXT("retry clears score"),S.GetScore(),int64(0));
    S.AdvanceDistance(1.e9,3.);TestTrue(TEXT("score beyond int32"),S.GetScore()>MAX_int32);
    S.Reset(Run);const FBootOperationKey Pinned{Run,1,1},Later{Run,2,1};
    S.RegisterChunk(1,{Pinned});S.RegisterChunk(2,{Later});S.RetireChunk(2);
    TestTrue(TEXT("still live pinned operation survives later retirement"),S.AwardOperation(Pinned,EBootBeatRating::Good,1.));
    for(int64 I=3;I<10003;++I){const FBootOperationKey K{Run,I,1};S.RegisterChunk(I,{K});S.RetireChunk(I);}
    TestEqual(TEXT("long retirement history stays bounded"),S.GetTrackedOperationCount(),1);
    return true;
}
#endif
