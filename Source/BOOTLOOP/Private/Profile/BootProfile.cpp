#include "Profile/BootProfile.h"
void UBootProfile::UpdateMusicRunRecord(int64 Score,double Distance,double ActiveSeconds)
{
    if(Score<0||!FMath::IsFinite(Distance)||Distance<0.||!FMath::IsFinite(ActiveSeconds)||ActiveSeconds<0.)return;
    MusicRunRecord.BestScore=FMath::Max(MusicRunRecord.BestScore,Score);
    MusicRunRecord.BestDistanceMetres=FMath::Max(MusicRunRecord.BestDistanceMetres,Distance);
    MusicRunRecord.BestActiveSeconds=FMath::Max(MusicRunRecord.BestActiveSeconds,ActiveSeconds);
}

void FBootOptions::Clamp()
{
    auto Safe = [](float V, float Default, float Low, float High)
    { return FMath::IsFinite(V) ? FMath::Clamp(V, Low, High) : Default; };
    Music = Safe(Music, .55f, 0.f, 1.f);
    Sfx = Safe(Sfx, .65f, 0.f, 1.f);
    Sensitivity = Safe(Sensitivity, .12f, .03f, .4f);
    Fov = Safe(Fov, 95.f, 80.f, 110.f);
    InputOffsetMs = Safe(InputOffsetMs, 0.f, -250.f, 250.f);
    UiScale = Safe(UiScale, 1.f, 1.f, 1.25f);
    EffectsIntensity = Safe(EffectsIntensity,1.f,0.f,1.f);
    Resolution = FMath::Max(Resolution, 0);
    if(ResolutionSize.X<=0 || ResolutionSize.Y<=0)ResolutionSize=FIntPoint::ZeroValue;
    WindowMode = FMath::Clamp(WindowMode, 0, 2);
    FrameLimit = FMath::Clamp(FrameLimit, 0, 3);
    PixelQuality = FMath::Clamp(PixelQuality, 0, 2);
}

void UBootProfile::UpdateCourseRecord(FName Course,int32 Score,float Distance,float Time,bool bFinished)
{
    if(Course==TEXT("FirstLoginV03")) return;
    auto& Record=CourseRecords.FindOrAdd(Course);
    Record.BestScore=FMath::Max(Record.BestScore,FMath::Max(0,Score));
    if(FMath::IsFinite(Distance)) Record.BestDistanceMetres=FMath::Max(Record.BestDistanceMetres,FMath::Max(0.f,Distance));
    if(bFinished && Time>0.f && FMath::IsFinite(Time) && (Record.BestTimeSeconds<=0.f || Time<Record.BestTimeSeconds)) Record.BestTimeSeconds=Time;
}

void UBootProfile::UpdateBest(int32 Score, float Distance)
{
    BestScore = FMath::Max(BestScore, FMath::Max(Score, 0));
    if (FMath::IsFinite(Distance)) BestDistance = FMath::Max(BestDistance, FMath::Max(Distance, 0.f));
}
