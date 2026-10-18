#include "Misc/AutomationTest.h"
#include "Music/BootMusicQueue.h"
#include "Music/BootMusicTransportComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
// Catches NEXT identifying the playing/excluded/cancelled/stale source or choosing an unrelated bag entry.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootTransportNextIdentityTest,"Bootloop.V04.Transport.NextPendingIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootTransportNextIdentityTest::RunTest(const FString&)
{
 FBootMusicQueue Q;
 auto Entry=[](const TCHAR* Hash,uint64 Revision){FBootLibraryEntry E;E.Hash=Hash;E.State=EBootLibraryState::Ready;E.bIncluded=true;E.Revision=Revision;return E;};
 TArray<FBootLibraryEntry> Library{Entry(TEXT("playing"),3),Entry(TEXT("next"),5),Entry(TEXT("later"),7)};
 auto Request=[](const TCHAR* Hash,uint64 Id,uint64 Revision){FBootPendingPlaybackIdentity P;P.Hash=Hash;P.RequestId=Id;P.LibraryRevision=Revision;P.bOccupied=P.bAllowed=true;return P;};
 auto A=Request(TEXT("playing"),1,3);A.bPlaying=true;
 auto B=Request(TEXT("next"),2,5);auto C=Request(TEXT("later"),3,7);
 TArray<FBootPendingPlaybackIdentity> Pending{A,C,B};
 TestEqual(TEXT("NEXT is actual earliest pending request"),Q.PeekPendingTrackHash(Pending,Library),FString(TEXT("next")));
 Pending[2].bCancelled=true;TestEqual(TEXT("cancelled request is hidden immediately"),Q.PeekPendingTrackHash(Pending,Library),FString(TEXT("later")));
 Pending[2]=B;Library[1].bIncluded=false;TestEqual(TEXT("excluded prefetch hidden before Pump"),Q.PeekPendingTrackHash(Pending,Library),FString(TEXT("later")));
 Library[1].bIncluded=true;Library[1].Revision=6;TestEqual(TEXT("old grid revision is not presented"),Q.PeekPendingTrackHash(Pending,Library),FString(TEXT("later")));
 Library[1].Revision=5;Pending[2].bFailed=true;TestEqual(TEXT("failed source is hidden"),Q.PeekPendingTrackHash(Pending,Library),FString(TEXT("later")));
 Pending[2]=B;Pending[2].bRetired=true;Pending[1].bOccupied=false;
 TestTrue(TEXT("no pending means truthful preparing or fallback"),Q.PeekPendingTrackHash(Pending,Library).IsEmpty());
 Pending={A,B};FRandomStream Random(923),BaselineRandom(923);FBootMusicQueue Baseline;
 Q.ChooseNext(Library,TEXT(""),Random);Baseline.ChooseNext(Library,TEXT(""),BaselineRandom);
 for(int32 I=0;I<20;++I)TestEqual(TEXT("repeated peek preserves actual NEXT"),Q.PeekPendingTrackHash(Pending,Library),FString(TEXT("next")));
 TestTrue(TEXT("peek does not advance shuffle bag"),Q.ChooseNext(Library,TEXT("playing"),Random)==Baseline.ChooseNext(Library,TEXT("playing"),BaselineRandom));
 auto* Stopped=NewObject<UBootMusicTransportComponent>();
 TestTrue(TEXT("stopped component has no next song"),Stopped->GetNextTrackHash().IsEmpty());
 return true;
}
#endif
