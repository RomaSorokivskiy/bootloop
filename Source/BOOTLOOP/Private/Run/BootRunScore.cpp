#include "Run/BootRunScore.h"
namespace
{
void AddSaturated(int64& Total,double Points)
{
    if(!FMath::IsFinite(Points)||Points<=0.)return;
    const double Whole=FMath::FloorToDouble(Points+1.e-7);
    if(Whole>=double(MAX_int64-Total)){Total=MAX_int64;return;}
    Total+=int64(Whole);
}
}
void FBootRunScore::Reset(FBootRunToken InRun)
{
    Run=InRun;LiveOperations.Reset();Score=LastPoints=0;RetiredWatermark=-1;
    Combo=0;Distance=DistancePoints=0.;LastRating=EBootBeatRating::OffBeat;
}
void FBootRunScore::AdvanceDistance(double NewMetres,double Multiplier)
{
    if(!FMath::IsFinite(NewMetres)||NewMetres<=Distance||!FMath::IsFinite(Multiplier))return;
    const double Before=FMath::FloorToDouble(DistancePoints+1.e-7);
    DistancePoints+=(NewMetres-Distance)*10.*FMath::Clamp(Multiplier,1.,3.);
    Distance=NewMetres;
    AddSaturated(Score,FMath::FloorToDouble(DistancePoints+1.e-7)-Before);
}
bool FBootRunScore::AwardOperation(const FBootOperationKey& Key,EBootBeatRating Rating,double Multiplier)
{
    if(Key.Run!=Run||!Run.RunId.IsValid()||!FMath::IsFinite(Multiplier)||uint8(Rating)>uint8(EBootBeatRating::Perfect))return false;
    bool* Completed=LiveOperations.Find(Key);
    if(!Completed||*Completed)return false;
    *Completed=true;LastRating=Rating;
    if(Rating==EBootBeatRating::OffBeat)Combo=0;
    const int32 Base=Rating==EBootBeatRating::Perfect?300:Rating==EBootBeatRating::Good?150:75;
    LastPoints=int64(FMath::FloorToDouble((Base*(10+FMath::Min(Combo,20))/10)*FMath::Clamp(Multiplier,1.,3.)));
    AddSaturated(Score,double(LastPoints));
    if(Rating!=EBootBeatRating::OffBeat)Combo=FMath::Min(Combo+1,MAX_int32-1);
    return true;
}
void FBootRunScore::RegisterChunk(int64 Id,const TArray<FBootOperationKey>& Keys)
{
    if(Id<=RetiredWatermark||Id<=0||Keys.Num()>8)return;
    for(const auto& Key:Keys)
        if(Key.Run==Run&&Key.ChunkId==Id&&Key.LocalId>=0&&!LiveOperations.Contains(Key)&&LiveOperations.Num()<320)
            LiveOperations.Add(Key,false);
}
void FBootRunScore::RetireChunk(int64 Id)
{
    if(Id<=0)return;
    for(auto It=LiveOperations.CreateIterator();It;++It)if(It.Key().ChunkId==Id)It.RemoveCurrent();
    RetiredWatermark=FMath::Max(RetiredWatermark,Id);
}
