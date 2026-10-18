#include "Music/BootMusicQueue.h"
FString FBootMusicQueue::PeekPendingTrackHash(const TArray<FBootPendingPlaybackIdentity>& Pending,const TArray<FBootLibraryEntry>& Library)const
{
 const FBootPendingPlaybackIdentity* Next=nullptr;
 for(const auto& Request:Pending)
 {
  if(!Request.bOccupied||!Request.bAllowed||Request.bFailed||Request.bCancelled||Request.bPlaying||Request.bRetired||Request.RequestId==0||Request.Hash.IsEmpty())continue;
  const auto* Entry=Library.FindByPredicate([&](const FBootLibraryEntry& E){return E.Hash==Request.Hash;});
  if(!Entry||!Entry->bIncluded||Entry->State!=EBootLibraryState::Ready||Entry->Revision!=Request.LibraryRevision)continue;
  if(!Next||Request.RequestId<Next->RequestId)Next=&Request;
 }
 return Next?Next->Hash:FString();
}
TOptional<FString> FBootMusicQueue::ChooseNext(const TArray<FBootLibraryEntry>& Entries, const FString& Current, FRandomStream& Random)
{
 TArray<FString> Selected;
 for(const auto& E:Entries)
  if(E.bIncluded && E.State==EBootLibraryState::Ready && !E.Hash.IsEmpty()) Selected.AddUnique(E.Hash);
 Selected.Sort();
 Bag.RemoveAll([&Selected](const FString& H){return !Selected.Contains(H);});
 if(Selected.IsEmpty()) { Bag.Reset(); return {}; }
 if(Bag.IsEmpty())
 {
  Bag=Selected;
  for(int32 I=Bag.Num()-1;I>0;--I) Bag.Swap(I,Random.RandRange(0,I));
 }
 int32 Pick=Bag.Num()-1;
 if(Selected.Num()>1 && Bag[Pick]==Current)
 {
  int32 Other=INDEX_NONE;
  for(int32 I=Bag.Num()-1;I>=0;--I) if(Bag[I]!=Current) { Other=I; break; }
  if(Other!=INDEX_NONE) Pick=Other;
  else
  {
   // A selection edit can leave only the current song in an old bag.
   Bag=Selected;
   for(int32 I=Bag.Num()-1;I>0;--I) Bag.Swap(I,Random.RandRange(0,I));
   Pick=Bag.Num()-1;
   if(Bag[Pick]==Current) Pick=0;
  }
 }
 FString Result=Bag[Pick]; Bag.RemoveAt(Pick); return Result;
}
void FBootMusicQueue::Reset() { Bag.Reset(); }
