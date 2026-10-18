#include "World/Generation/BootEndlessPlanner.h"

namespace
{
uint64 Random(uint64& State)
{
    uint64 Z = (State += 0x9e3779b97f4a7c15ULL);
    Z = (Z ^ (Z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    Z = (Z ^ (Z >> 27)) * 0x94d049bb133111ebULL;
    return Z ^ (Z >> 31);
}
FBox Bounds(const FBootOrientedVolume& V)
{
    return FBox(-V.HalfSize,V.HalfSize).TransformBy(V.Frame);
}
bool StrictBoxOverlap(const FBox& A,const FBox& B)
{
    return A.IsValid && B.IsValid && A.Min.X < B.Max.X-.05 && A.Max.X > B.Min.X+.05
        && A.Min.Y < B.Max.Y-.05 && A.Max.Y > B.Min.Y+.05 && A.Min.Z < B.Max.Z-.05 && A.Max.Z > B.Min.Z+.05;
}
// Separating axis test: six face normals and all nine edge cross products.
// Face contact is legal at a connector; positive penetration over .05cm is not.
bool Overlap(const FBootOrientedVolume& A,const FBootOrientedVolume& B)
{
    if (!StrictBoxOverlap(Bounds(A),Bounds(B))) return false;
    const FVector AX[3]={A.Frame.GetUnitAxis(EAxis::X),A.Frame.GetUnitAxis(EAxis::Y),A.Frame.GetUnitAxis(EAxis::Z)};
    const FVector BX[3]={B.Frame.GetUnitAxis(EAxis::X),B.Frame.GetUnitAxis(EAxis::Y),B.Frame.GetUnitAxis(EAxis::Z)};
    const FVector Delta=B.Frame.GetLocation()-A.Frame.GetLocation();
    auto Separates=[&](FVector Axis)
    {
        const double Size=Axis.Size(); if (Size<1.e-8) return false; Axis/=Size;
        double RadiusA=0.,RadiusB=0.;
        for(int32 I=0;I<3;++I){RadiusA+=A.HalfSize[I]*FMath::Abs(FVector::DotProduct(AX[I],Axis));RadiusB+=B.HalfSize[I]*FMath::Abs(FVector::DotProduct(BX[I],Axis));}
        return FMath::Abs(FVector::DotProduct(Delta,Axis)) >= RadiusA+RadiusB-.05;
    };
    for(int32 I=0;I<3;++I) if(Separates(AX[I])||Separates(BX[I])) return false;
    for(int32 I=0;I<3;++I) for(int32 J=0;J<3;++J) if(Separates(FVector::CrossProduct(AX[I],BX[J]))) return false;
    return true;
}
TArray<FBootOrientedVolume> Volumes(const FBootCommittedChunk& C)
{
    if (!C.WorldVolumes.IsEmpty()) return C.WorldVolumes;
    TArray<FBootOrientedVolume> Result;
    // Also support diagnostic descriptors from external audits and hand-authored tests.
    for (const auto* Boxes : {&C.WorldOccupied,&C.WorldBodyClearance,&C.WorldRailSweeps,&C.WorldHazardSweeps})
        for(const auto& B:*Boxes) if(B.IsValid) {FBootOrientedVolume V;V.Frame=FTransform(B.GetCenter());V.HalfSize=B.GetExtent();Result.Add(V);}
    return Result;
}
FBox UnionBounds(const TArray<FBootOrientedVolume>& Vs)
{
    FBox B(ForceInit);for(const auto& V:Vs)B+=Bounds(V);return B;
}
void AddVolumes(FBootCommittedChunk& C,const TArray<FBox>& Local,EBootVolumeKind Kind,TArray<FBox>& World)
{
    for(const auto& B:Local)
    {
        FBootOrientedVolume V;V.Frame=FTransform(B.GetCenter())*C.WorldTransform;V.HalfSize=B.GetExtent();V.Kind=Kind;
        C.WorldVolumes.Add(V);World.Add(Bounds(V));
    }
}
bool Fits(const FBootChunkDefinition& D,const FBootTraversalEnvelope& E,const FBootRunModifiers& M)
{
    FText Error;
    if(D.CombinationCost>FMath::Clamp(M.ExtraCombinationBudget,0,2)||D.ProcessCost>1+FMath::Clamp(M.ExtraProcessBudget,0,2))return false;
    const float PotentialSpeed=FMath::Min(E.AbsoluteCmps,E.TemporaryCmps+FBootTraversalPolicy::SurfMomentumCmps);
    if(!FBootTraversalPolicy::Validate(E,Error)||E.RunCmps<D.MinSpeedCmps||PotentialSpeed>D.MaxSpeedCmps
        ||!FMath::IsFinite(M.WidthScale)||M.WidthScale<D.MinWidthScale||M.WidthScale>1.f) return false;
    // A narrowed elbow would move its exit floor away from the frozen connector.
    // Keep these authored connector geometries intact and choose a compatible next module.
    if(M.WidthScale<1.f && (FMath::Abs(D.Exit.GetLocation().Y)>.01 || !D.Exit.GetRotation().Equals(FQuat::Identity,.001)))return false;
    // Authored 180cm mandatory gaps include capsule edge allowance and a margin at minimum run speed.
    for(const auto& Move:D.Recipe.BaselineMoves)
        if(Move.Kind==EBootBaselineMove::Jump && 2.*E.JumpZCmps/(980.*E.GravityScale)*E.RunCmps < 280.) return false;
    for(const auto& H:D.Recipe.Hazards)
        if((H.Sweep.Min.X-100.)/(PotentialSpeed+E.MaxBaseCmps) < H.TelegraphSeconds) return false;
    return true;
}
bool ValidRequest(const FBootPlanRequest& R)
{
    FText Error;
    return R.Run.RunId.IsValid() && R.NextInstanceId>0 && R.NextInstanceId<MAX_int64
        && !R.Connector.ContainsNaN() && R.Connector.GetScale3D().Equals(FVector::OneVector)
        && FMath::IsFinite(R.ActiveSeconds) && R.ActiveSeconds>=0. && FMath::IsFinite(R.CanonicalStartMetres) && R.CanonicalStartMetres>=0.
        && FBootTraversalPolicy::Validate(R.CurrentEnvelope,Error)
        && (!R.PendingEnvelope.IsSet()||FBootTraversalPolicy::Validate(R.PendingEnvelope.GetValue(),Error));
}
bool ValidMusic(const FBootMusicContext& M)
{
    return FMath::IsFinite(M.LocalSeconds)&&FMath::IsFinite(M.EffectiveBpm)&&M.EffectiveBpm>0.
        &&FMath::IsFinite(M.Grid.Bpm)&&M.Grid.Bpm>0.&&FMath::IsFinite(M.Grid.FirstBeatSeconds)
        &&FMath::IsFinite(M.Energy)&&FMath::IsFinite(M.OnsetDensity);
}
FBootCommittedChunk Commit(const FBootChunkDefinition& D,const FBootPlanRequest& R)
{
    FBootCommittedChunk C;C.Run=R.Run;C.InstanceId=R.NextInstanceId;C.DefinitionId=D.Id;C.DefinitionVersion=D.Version;
    C.WorldTransform=D.Entry.Inverse()*R.Connector;C.WorldEntry=D.Entry*C.WorldTransform;C.WorldExit=D.Exit*C.WorldTransform;
    C.CanonicalStartMetres=R.CanonicalStartMetres;double Length=0.;
    for(int32 I=1;I<D.Recipe.Baseline.Num();++I)Length+=FVector::Distance(D.Recipe.Baseline[I-1],D.Recipe.Baseline[I]);
    C.CanonicalEndMetres=R.CanonicalStartMetres+Length*.01;
    C.MusicContext=R.ReplayDecision.IsSet()?R.ReplayDecision->MusicContext:R.MusicContext;
    C.EnvelopeRevision=R.CurrentEnvelope.Revision;C.Recipe=D.Recipe;
    C.Recipe.Variant=R.ReplayDecision.IsSet()?R.ReplayDecision->Variant:uint32(FMath::Clamp(C.MusicContext.Energy,0.f,1.f)*3.f) + (uint32(FMath::Max(C.MusicContext.Section,0))%4u)*4u;
    // Width cards select a fixed authored-safe scale, applied to solids/path envelopes together;
    // X gap/rail distances never change. Native rails retain their validated full dimensions.
    const double Width=R.Modifiers.WidthScale;
    if(Width<1.)
    {
        for(auto& B:C.Recipe.Boxes){B.Center.Y*=Width;B.HalfSize.Y*=Width;}
        for(auto& P:C.Recipe.Baseline)P.Y*=Width;
        for(auto& P:C.Recipe.OptionalBranch)P.Y*=Width;
        for(auto& Move:C.Recipe.BaselineMoves){Move.Start.Y*=Width;Move.End.Y*=Width;}
        // Rails/hazards keep their complete original sweep; stricter eligibility below protects bypasses.
    }
    auto Scaled=[&](const TArray<FBox>& In){TArray<FBox> Out=In;for(auto& B:Out){B.Min.Y*=Width;B.Max.Y*=Width;}return Out;};
    AddVolumes(C,Scaled(D.Occupied),EBootVolumeKind::Occupied,C.WorldOccupied);
    AddVolumes(C,Scaled(D.BodyClearance),EBootVolumeKind::BodyClearance,C.WorldBodyClearance);
    AddVolumes(C,D.RailSweeps,EBootVolumeKind::Rail,C.WorldRailSweeps);
    AddVolumes(C,D.HazardSweeps,EBootVolumeKind::Hazard,C.WorldHazardSweeps);
    for(const auto& Rail:D.Recipe.Rails){FBootOperationKey K;K.Run=R.Run;K.ChunkId=C.InstanceId;K.LocalId=Rail.OperationId;C.OperationKeys.Add(K);}
    for(const auto& H:D.Recipe.Hazards){FBootOperationKey K;K.Run=R.Run;K.ChunkId=C.InstanceId;K.LocalId=H.OperationId;C.OperationKeys.Add(K);}
    if(!C.Recipe.OptionalBranch.IsEmpty()){C.BranchId=C.InstanceId;C.MergeId=C.InstanceId;C.BranchMergeMetres=C.CanonicalEndMetres;}
    C.ReservedVolume.Frame=FTransform(FVector(3000.,0.,210.))*C.WorldExit;
    C.ReservedVolume.HalfSize=FVector(3000.,600.,290.);C.ReservedVolume.Kind=EBootVolumeKind::BodyClearance;
    C.ReservedContinuation=Bounds(C.ReservedVolume);return C;
}
bool ReservationClear(const FBootCommittedChunk& C,const TArray<FBootCommittedChunk>& Live)
{
    for(const auto& V:C.WorldVolumes)if(Overlap(C.ReservedVolume,V))return false;
    for(const auto& Other:Live)for(const auto& V:Volumes(Other))if(Overlap(C.ReservedVolume,V))return false;
    return true;
}
bool RespectsFrontier(const FBootChunkDefinition& D,const FBootPlanRequest& R,const FVector& Axis)
{
    const FTransform World=D.Entry.Inverse()*R.Connector;
    // Descents return toward desktop elevation, keeping normal generated floors
    // above the existing global emergency fall plane used by legacy movement.
    if(D.Id==TEXT("flow.steps.down")&&(D.Exit*World).GetLocation().Z<0.)return false;
    if(FVector::DotProduct((D.Exit*World).GetUnitAxis(EAxis::X),Axis)<-.001)return false;
    for(int32 I=1;I<D.Recipe.Baseline.Num();++I)
        if(FVector::DotProduct(World.TransformVectorNoScale(D.Recipe.Baseline[I]-D.Recipe.Baseline[I-1]),Axis)<-.01)return false;
    return true;
}
}

void FBootEndlessPlanner::Reset(uint64 Seed,const FBootTraversalEnvelope& Envelope)
{
    WorldState=Seed^0x776f726c640001ULL;CardState=Seed^0x63617264730001ULL;CosmeticState=Seed^0x636f736d65746963ULL;
    InitialEnvelope=Envelope;bHasGrowthAxis=false;GrowthAxis=FVector::ForwardVector;SetCatalog(FBootChunkCatalog::Build());
}
void FBootEndlessPlanner::SetCatalog(TArray<FBootChunkDefinition> Definitions)
{
    Catalog.Empty();TSet<FName> Seen;
    Definitions.Sort([](const auto& A,const auto& B){return A.Id.ToString()<B.Id.ToString();});
    for(auto& D:Definitions){FString Error;if(FBootChunkCatalog::Validate(D,Error)&&!Seen.Contains(D.Id)){Seen.Add(D.Id);Catalog.Add(MoveTemp(D));}}
}
float FBootEndlessPlanner::DifficultyBudget(double ActiveSeconds,int32 Tier)
{
    if(!FMath::IsFinite(ActiveSeconds))return 0.f;
    return float(FMath::Min(FMath::Clamp(1.+ActiveSeconds/25.,1.,5.),double(FMath::Clamp(Tier,0,5))));
}
uint32 FBootEndlessPlanner::NextCosmeticRandom(){return uint32(Random(CosmeticState)>>32);}
uint32 FBootEndlessPlanner::NextCardRandom(){return uint32(Random(CardState)>>32);}
bool FBootEndlessPlanner::HasUnrelatedOverlap(const FBootCommittedChunk& Candidate,const TArray<FBootCommittedChunk>& Live)
{
    const auto CV=Volumes(Candidate);const FBox CB=UnionBounds(CV);
    for(const auto& Other:Live)
    {
        const auto OV=Volumes(Other);if(!StrictBoxOverlap(CB,UnionBounds(OV)))continue;
        for(const auto& A:CV)for(const auto& B:OV)if(Overlap(A,B))return true;
    }
    return false;
}
bool FBootEndlessPlanner::CanPrepareEnvelope(const FBootPlanRequest& Request,const FBootTraversalEnvelope& Pending) const
{
    FBootPlanRequest Copy=Request;Copy.PendingEnvelope=Pending;
    if(!ValidRequest(Copy)||Copy.LiveDescriptors.Num()>=MaxLive)return false;
    for(const auto& D:Catalog)
    {
        if(!D.bSafeContinuation||!Fits(D,Copy.CurrentEnvelope,Copy.Modifiers)||!Fits(D,Pending,Copy.Modifiers))continue;
        if(!RespectsFrontier(D,Copy,bHasGrowthAxis?GrowthAxis:Copy.Connector.GetUnitAxis(EAxis::X)))continue;
        const auto C=Commit(D,Copy);
        if(!HasUnrelatedOverlap(C,Copy.LiveDescriptors)&&ReservationClear(C,Copy.LiveDescriptors))return true;
    }
    return false;
}
FBootPlanResult FBootEndlessPlanner::PlanNext(const FBootPlanRequest& R)
{
    FBootPlanResult Result;
    auto Fail=[&](EBootPlanFailure Failure,const TCHAR* Message){Result.Failure=Failure;Result.Diagnostics=Message;return Result;};
    if(R.LiveDescriptors.Num()>=MaxLive)return Fail(EBootPlanFailure::LiveBudget,TEXT("40 live instances: retire only released dependencies before planning"));
    if(!ValidRequest(R)||!ValidMusic(R.ReplayDecision.IsSet()?R.ReplayDecision->MusicContext:R.MusicContext))return Fail(EBootPlanFailure::InvalidRequest,TEXT("Invalid run, transform, envelope, time or music context"));
    for(const auto& C:R.LiveDescriptors)
        if(C.InstanceId>=R.NextInstanceId||C.Run!=R.Run)return Fail(EBootPlanFailure::InvalidRequest,TEXT("Live descriptor identity is stale or nonmonotonic"));
    if(!R.LiveDescriptors.IsEmpty())
    {
        const FBootCommittedChunk* Last=&R.LiveDescriptors[0];for(const auto& C:R.LiveDescriptors)if(C.InstanceId>Last->InstanceId)Last=&C;
        if(!Last->WorldExit.Equals(R.Connector,.1)||!FMath::IsNearlyEqual(Last->CanonicalEndMetres,R.CanonicalStartMetres,.001))
            return Fail(EBootPlanFailure::InvalidRequest,TEXT("Continuation must align with latest committed connector/progress"));
    }
    if(!bHasGrowthAxis){GrowthAxis=R.Connector.GetUnitAxis(EAxis::X);bHasGrowthAxis=true;}
    const float Budget=DifficultyBudget(R.ActiveSeconds,R.Tier);
    const bool bScheduledRecovery=!R.ReplayDecision.IsSet()&&R.NextInstanceId%4==0;
    const bool bOpening=!R.ReplayDecision.IsSet()&&R.NextInstanceId==1&&R.ActiveSeconds<1.&&R.Modifiers.WidthScale>=1.f
        &&Catalog.ContainsByPredicate([](const auto& D){return D.Id.ToString().StartsWith(TEXT("intro."));});
    const FBootCommittedChunk* Last=nullptr;const FBootCommittedChunk* Previous=nullptr;
    for(const auto& C:R.LiveDescriptors)
        if(!Last||C.InstanceId>Last->InstanceId){Previous=Last;Last=&C;}
        else if(!Previous||C.InstanceId>Previous->InstanceId)Previous=&C;
    const auto* LastDefinition=Last?Catalog.FindByPredicate([&](const auto& D){return D.Id==Last->DefinitionId;}):nullptr;
    const auto* PriorDefinition=Previous?Catalog.FindByPredicate([&](const auto& D){return D.Id==Previous->DefinitionId;}):nullptr;
    TArray<const FBootChunkDefinition*> Choices,Safe;
    for(const auto& D:Catalog)
    {
        if(!Fits(D,R.CurrentEnvelope,R.Modifiers)||(R.PendingEnvelope.IsSet()&&!Fits(D,R.PendingEnvelope.GetValue(),R.Modifiers)))continue;
        if(!RespectsFrontier(D,R,GrowthAxis))continue;
        // Native moving rails are fixed width; width cards do not alter these commitments.
        if(R.Modifiers.WidthScale<1.f&&(!D.Recipe.Rails.IsEmpty()||!D.Recipe.Hazards.IsEmpty()))continue;
        if(R.ReplayDecision.IsSet())
        {if(D.Id==R.ReplayDecision->DefinitionId&&D.Version==R.ReplayDecision->DefinitionVersion)Choices.Add(&D);continue;}
        const bool bIntro=D.Id.ToString().StartsWith(TEXT("intro."));
        if(bOpening){if(bIntro)Choices.Add(&D);continue;}
        if(bIntro||D.Id==TEXT("surf.crossing")||D.Id==TEXT("surf.branch"))continue;
        if(D.bSafeContinuation)Safe.Add(&D);
        else if(!bScheduledRecovery&&D.DifficultyCost<=Budget
            &&(!Last||D.Id!=Last->DefinitionId)
            &&(!LastDefinition||!PriorDefinition||LastDefinition->Family!=PriorDefinition->Family||D.Family!=LastDefinition->Family))Choices.Add(&D);
    }
    if(!R.ReplayDecision.IsSet()&&!bOpening&&!bScheduledRecovery&&R.NextInstanceId<=12)
    {
        TSet<EBootChunkFamily> Seen;
        for(const auto& C:R.LiveDescriptors)
            if(const auto* D=Catalog.FindByPredicate([&](const auto& V){return V.Id==C.DefinitionId;}))Seen.Add(D->Family);
        TArray<const FBootChunkDefinition*> Fresh;
        for(const auto* D:Choices)if(!Seen.Contains(D->Family))Fresh.Add(D);
        // Show the runnable-wall language early, before random family order can
        // postpone it. Keep all speed, overlap and safe-continuation checks.
        if(R.NextInstanceId==3&&!Seen.Contains(EBootChunkFamily::Wall))
        {
            auto Walls=Fresh.FilterByPredicate([](const auto* D){return D->Family==EBootChunkFamily::Wall;});
            if(!Walls.IsEmpty())Fresh=MoveTemp(Walls);
        }
        if(!Fresh.IsEmpty())Choices=MoveTemp(Fresh);
    }
    auto Try=[&](const FBootChunkDefinition& D,bool bFallback)
    {
        ++Result.AttemptCount;auto C=Commit(D,R);
        if(HasUnrelatedOverlap(C,R.LiveDescriptors)||!ReservationClear(C,R.LiveDescriptors))return false;
        Result.bUsedFallback=bFallback;Result.ReservedContinuation=C.ReservedContinuation;Result.Committed=MoveTemp(C);return true;
    };
    while(!Choices.IsEmpty()&&Result.AttemptCount<MaxAttempts-2)
    {
        int32 Index=0;
        if(!R.ReplayDecision.IsSet())
        {
            TArray<double> Weights;double Sum=0.;
            for(const auto* D:Choices)
            {
                const double Weight=SelectionWeight(*D,R.MusicContext,R.Modifiers);
                Weights.Add(Weight);Sum+=Weight;
            }
            double Pick=double(Random(WorldState)>>11)*(1./9007199254740992.)*Sum;
            for(;Index<Choices.Num()-1;++Index){Pick-=Weights[Index];if(Pick<=0.)break;}
        }
        const auto* D=Choices[Index];Choices.RemoveAt(Index);
        if(Try(*D,false))return Result;
    }
    if(R.ReplayDecision.IsSet())return Fail(EBootPlanFailure::ReplayMismatch,TEXT("Recorded definition/version/context no longer has a legal placement"));
    for(const auto* D:Safe)if(Result.AttemptCount<MaxAttempts&&Try(*D,!bScheduledRecovery))return Result;
    return Fail(EBootPlanFailure::NoLegalContinuation,TEXT("Bounded candidates and reserved recovery corridor exhausted; preserve supported approach"));
}

uint64 FBootEndlessPlanner::DescriptorHash(const FBootCommittedChunk& C)
{
    uint64 H=1469598103934665603ULL;
    auto Text=[&](const FString& S){FTCHARToUTF8 U(*S);for(int32 I=0;I<U.Length();++I){H^=uint8(U.Get()[I]);H*=1099511628211ULL;}H^=0xff;H*=1099511628211ULL;};
    auto Number=[&](double V){Text(FString::Printf(TEXT("%.9f"),V));};
    Text(C.DefinitionId.ToString());Text(LexToString(C.DefinitionVersion));Text(LexToString(C.InstanceId));Text(C.Run.RunId.ToString());Text(LexToString(C.Run.Generation));
    Text(C.WorldTransform.ToString());Number(C.CanonicalStartMetres);Number(C.CanonicalEndMetres);Text(LexToString(C.EnvelopeRevision));
    Text(C.MusicContext.TrackHash);Text(LexToString(C.MusicContext.TrackEpoch));Number(C.MusicContext.LocalSeconds);Number(C.MusicContext.Grid.Bpm);Number(C.MusicContext.Grid.FirstBeatSeconds);
    Number(C.MusicContext.EffectiveBpm);Number(C.MusicContext.Energy);Number(C.MusicContext.OnsetDensity);Text(LexToString(C.MusicContext.Section));Text(C.MusicContext.bSafeGrid?TEXT("safe"):TEXT("confident"));
    Text(LexToString(C.Recipe.Variant));Text(LexToString(C.BranchId));Text(LexToString(C.MergeId));Number(C.BranchMergeMetres);
    for(const auto& V:C.WorldVolumes){Text(V.Frame.ToString());Text(V.HalfSize.ToString());Text(LexToString(uint8(V.Kind)));}
    return H;
}
FString FBootEndlessPlanner::BoundsJson(const FBootCommittedChunk& C)
{
    FString Json=FString::Printf(TEXT("{\"instance\":%lld,\"definition\":\"%s\",\"hash\":\"%llu\",\"bounds\":["),C.InstanceId,*C.DefinitionId.ToString(),DescriptorHash(C));
    bool First=true;
    for(const auto& V:Volumes(C))
    {
        const FBox B=Bounds(V);if(!First)Json+=TEXT(",");First=false;
        Json+=FString::Printf(TEXT("{\"kind\":%d,\"min\":[%.6f,%.6f,%.6f],\"max\":[%.6f,%.6f,%.6f]}"),int32(V.Kind),B.Min.X,B.Min.Y,B.Min.Z,B.Max.X,B.Max.Y,B.Max.Z);
    }
    return Json+TEXT("]}");
}

double FBootEndlessPlanner::SelectionWeight(const FBootChunkDefinition& D,const FBootMusicContext& Music,const FBootRunModifiers& Modifiers)
{
    double Weight=1.+FMath::Clamp(double(Music.Energy),0.,1.)*D.DifficultyCost;
    if((D.MusicalTags&(1u<<(uint32(FMath::Max(Music.Section,0))%6)))!=0)Weight*=2.;
    if(D.Family==EBootChunkFamily::Slide)Weight*=1.+FMath::Clamp(double(Music.OnsetDensity),0.,1.);
    if(!D.Recipe.OptionalBranch.IsEmpty()&&D.Family==EBootChunkFamily::Surf)Weight*=FMath::Clamp(double(Modifiers.RiskySurfWeightScale),.1,4.);
    if(Music.bSafeGrid)Weight/=1.+D.DifficultyCost;
    return Weight;
}



bool FBootEndlessPlanner::CanPrepareBuild(const FBootPlanRequest& Request,const FBootRunModifiers& Candidate,const FBootTraversalEnvelope& Pending) const
{
    if(!FMath::IsFinite(Candidate.RiskySurfWeightScale))return false;
    // Native rails/processes retain their authored width. Neither selection order may
    // remove the very interactions whose risk reward the player already accepted.
    if(Candidate.WidthScale<1.f&&(Candidate.ExtraProcessBudget>0||Candidate.RiskySurfWeightScale>1.f))return false;
    auto Copy=Request;Copy.Modifiers=Candidate;Copy.PendingEnvelope=Pending;
    if(!CanPrepareEnvelope(Copy,Pending))return false;
    const int32 NewCombo=FMath::Clamp(Candidate.ExtraCombinationBudget,0,2);
    const int32 OldCombo=FMath::Clamp(Request.Modifiers.ExtraCombinationBudget,0,2);
    const int32 NewProcess=1+FMath::Clamp(Candidate.ExtraProcessBudget,0,2);
    const int32 OldProcess=1+FMath::Clamp(Request.Modifiers.ExtraProcessBudget,0,2);
    const bool NeedsCombo=NewCombo>OldCombo,NeedsProcess=NewProcess>OldProcess;
    const bool NeedsSurf=Candidate.RiskySurfWeightScale>Request.Modifiers.RiskySurfWeightScale;
    bool HasCombo=!NeedsCombo,HasProcess=!NeedsProcess,HasSurf=!NeedsSurf;
    if(HasCombo&&HasProcess&&HasSurf)return true;
    const FVector Frontier=bHasGrowthAxis?GrowthAxis:Copy.Connector.GetUnitAxis(EAxis::X);
    const float Budget=DifficultyBudget(Copy.ActiveSeconds,Copy.Tier);
    for(const auto& D:Catalog)
    {
        const bool AddsCombo=NeedsCombo&&D.CombinationCost==NewCombo;
        const bool AddsProcess=NeedsProcess&&D.ProcessCost==NewProcess;
        const bool AddsSurf=NeedsSurf&&D.Family==EBootChunkFamily::Surf&&!D.Recipe.OptionalBranch.IsEmpty();
        if(!(AddsCombo||AddsProcess||AddsSurf)||D.DifficultyCost>Budget)continue;
        if(!Fits(D,Copy.CurrentEnvelope,Candidate)||!Fits(D,Pending,Candidate)||!RespectsFrontier(D,Copy,Frontier))continue;
        if(Candidate.WidthScale<1.f&&(!D.Recipe.Rails.IsEmpty()||!D.Recipe.Hazards.IsEmpty()))continue;
        const auto Placement=Commit(D,Copy);
        if(HasUnrelatedOverlap(Placement,Copy.LiveDescriptors)||!ReservationClear(Placement,Copy.LiveDescriptors))continue;
        HasCombo|=AddsCombo;HasProcess|=AddsProcess;HasSurf|=AddsSurf;
        if(HasCombo&&HasProcess&&HasSurf)return true;
    }
    return false;
}


