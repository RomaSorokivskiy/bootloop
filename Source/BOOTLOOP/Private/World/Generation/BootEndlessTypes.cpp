#include "World/Generation/BootEndlessTypes.h"

void FBootRouteCursor::Reset(int64 FirstInstanceId)
{
    CurrentInstanceId=FirstInstanceId;Sequence=0;CanonicalMetres=0.;
}
TArray<FBootGateCrossing> FBootRouteCursor::Advance(FVector Previous,FVector Current,const TArray<FBootCommittedChunk>& Live)
{
    TArray<FBootGateCrossing> Result;
    if(Previous.ContainsNaN()||Current.ContainsNaN()||Live.Num()>40)return Result;
    double LastAlpha=-1.;
    // Follow identity order, never nearest/world-X order. A high-speed sweep may cross several gates.
    for(int32 Count=0;Count<Live.Num();++Count)
    {
        const auto* C=Live.FindByPredicate([&](const auto& Item){return Item.InstanceId==CurrentInstanceId;});
        if(!C)break;
        const auto* Next=Live.FindByPredicate([&](const auto& Item){return Item.InstanceId==CurrentInstanceId+1;});
        if(!Next)break; // No progress into unprepared continuation.
        const FVector A=C->WorldExit.InverseTransformPosition(Previous),B=C->WorldExit.InverseTransformPosition(Current);
        if(A.X>0. || B.X<=0. || B.X-A.X<1.e-8)break;
        const double Alpha=-A.X/(B.X-A.X);const FVector Hit=FMath::Lerp(A,B,Alpha);
        if(Alpha<LastAlpha || FMath::Abs(Hit.Y)>600. || Hit.Z < -20. || Hit.Z>450.)break;
        if(C->CanonicalEndMetres<CanonicalMetres)break;
        FBootGateCrossing Crossing;Crossing.FromInstanceId=C->InstanceId;Crossing.ToInstanceId=Next->InstanceId;
        Crossing.Sequence=++Sequence;Crossing.CanonicalMetres=C->CanonicalEndMetres;
        Result.Add(Crossing);CanonicalMetres=Crossing.CanonicalMetres;CurrentInstanceId=Next->InstanceId;LastAlpha=Alpha;
    }
    return Result;
}
