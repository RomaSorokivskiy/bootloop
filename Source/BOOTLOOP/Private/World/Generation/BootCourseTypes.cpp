#include "World/Generation/BootCourseTypes.h"
namespace
{
const FBootCourseNode* Find(const TArray<FBootCourseNode>& Nodes,int32 Id)
{ return Nodes.FindByPredicate([Id](const FBootCourseNode& N){return N.Id==Id;}); }

bool CrossGate(const FBootCourseNode& N,const FBox& Gate,FVector A,FVector B,double& T)
{
    A=N.Transform.InverseTransformPosition(A); B=N.Transform.InverseTransformPosition(B);
    const double Plane=Gate.GetCenter().X;
    if(A.X>Plane || B.X<=Plane || B.X-A.X<=UE_SMALL_NUMBER) return false;
    T=(Plane-A.X)/(B.X-A.X);
    const FVector P=FMath::Lerp(A,B,T);
    return P.Y>=Gate.Min.Y && P.Y<=Gate.Max.Y && P.Z>=Gate.Min.Z && P.Z<=Gate.Max.Z;
}
float ProjectProgress(const FBootCourseNode& N,FVector P)
{
    const float Alpha=FMath::Clamp(float(N.Transform.InverseTransformPosition(P).X)/FMath::Max(N.LengthCm,1.f),0.f,1.f);
    return FMath::Lerp(N.ProgressStartMetres,N.ProgressEndMetres,Alpha);
}
}
void FBootCourseState::Initialize(const TArray<FBootCourseNode>& Nodes,int32 StartId,FVector Position)
{
    CurrentNodeId=Find(Nodes,StartId)?StartId:INDEX_NONE;
    GateSequence=0; CrossedAt.Reset(); bFinished=false;
    const auto* N=Find(Nodes,CurrentNodeId);
    ProgressMetres=N?ProjectProgress(*N,Position):0.f;
}
TArray<FBootGateTransition> FBootCourseState::Advance(const TArray<FBootCourseNode>& Nodes,FVector Previous,FVector Current)
{
    TArray<FBootGateTransition> Result;
    if(bFinished || Previous.ContainsNaN() || Current.ContainsNaN()) return Result;
    double LastT=-1.;
    for(int32 Guard=0;Guard<Nodes.Num();++Guard)
    {
        const auto* Active=Find(Nodes,CurrentNodeId); if(!Active) break;
        const FBootCourseNode* Next=nullptr; double FirstT=2.;
        for(int32 Id:Active->SuccessorIds)
        {
            const auto* Candidate=Find(Nodes,Id); double T;
            if(Candidate && CrossGate(*Candidate,Candidate->EntryBounds,Previous,Current,T) && T>LastT && T<FirstT)
            { Next=Candidate; FirstT=T; }
        }
        if(!Next)
        {
            double T;
            if(Active->SuccessorIds.IsEmpty() && CrossGate(*Active,Active->ExitBounds,Previous,Current,T) && T>LastT)
            { bFinished=true; ProgressMetres=FMath::Max(ProgressMetres,Active->ProgressEndMetres); }
            break;
        }
        const int32 From=CurrentNodeId;
        CurrentNodeId=Next->Id; LastT=FirstT;
        CrossedAt.Add(CurrentNodeId,++GateSequence);
        Result.Add({From,CurrentNodeId,GateSequence});
        ProgressMetres=FMath::Max(ProgressMetres,Next->ProgressStartMetres);
    }
    if(const auto* N=Find(Nodes,CurrentNodeId)) ProgressMetres=FMath::Max(ProgressMetres,ProjectProgress(*N,Current));
    return Result;
}
bool FBootCourseState::WasGateCrossedAfter(int32 NodeId,int32 Sequence) const
{ const auto* At=CrossedAt.Find(NodeId); return At && *At>Sequence; }
