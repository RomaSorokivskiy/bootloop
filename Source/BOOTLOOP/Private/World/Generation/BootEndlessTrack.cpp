#include "World/Generation/BootEndlessTrack.h"
#include "World/Generation/BootEndlessChunk.h"
#include "World/Interaction/BootSurfWindow.h"
#include "Player/BootRunner.h"
#include "Movement/BootMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

namespace
{
double CatalogPeakBaseCmps()
{
    static const double Peak=[]()
    {
        double Value=0.;
        for(const auto& D:FBootChunkCatalog::Build())
        {
            for(const auto& Rail:D.Recipe.Rails)Value=FMath::Max(Value,double(Rail.SpeedCmps));
            for(const auto& Hazard:D.Recipe.Hazards)if(Hazard.bPopup)
                Value=FMath::Max(Value,FBootEndlessReadiness::RailPeakCmps(Hazard.Sweep.GetSize().X-1000.,4.));
        }
        return Value;
    }();
    return Peak;
}
double AlongBaseline(const FBootCommittedChunk& C,FVector WorldPosition)
{
    const FVector P=C.WorldTransform.InverseTransformPosition(WorldPosition);double Best=TNumericLimits<double>::Max(),Travel=0.,Result=0.;
    for(int32 I=1;I<C.Recipe.Baseline.Num();++I)
    {
        const FVector A=C.Recipe.Baseline[I-1],B=C.Recipe.Baseline[I],D=B-A;const double Length=D.Size();
        const double T=Length>.001?FMath::Clamp(FVector::DotProduct(P-A,D)/(Length*Length),0.,1.):0.;
        const double Distance=FVector::DistSquared(P,A+D*T);if(Distance<Best){Best=Distance;Result=Travel+Length*T;}Travel+=Length;
    }
    return Result; // Readiness distance only; never score or gate progress.
}
}
void ABootEndlessTrack::ResetTrack(int32 Seed)
{
    FBootRunToken NewRun;NewRun.RunId=FGuid::NewGuid();NewRun.Generation=Run.Generation+1;
    BeginMusicRun(NewRun,uint64(uint32(Seed)),FBootTraversalPolicy::FromTempo(130.,1));
}
void ABootEndlessTrack::BeginMusicRun(const FBootRunToken& Token,uint64 Seed,const FBootTraversalEnvelope& Initial)
{
    FText Error;if(!Token.RunId.IsValid()||!FBootTraversalPolicy::Validate(Initial,Error))return;
    // Run lifecycle resets its score ledger separately; do not emit old-run retire IDs into a new ledger.
    for(ABootEndlessChunk* Chunk:LiveChunks)if(IsValid(Chunk))Chunk->Destroy();LiveChunks.Empty();ExternalPins.Empty();LastRunner.Reset();
    Run=Token;Envelope=Initial;PendingEnvelope.Reset();Music=FBootMusicContext();ActiveSeconds=0.;Modifiers=FBootRunModifiers();
    Planner.Reset(Seed,Envelope);Cursor.Reset(1);NextId=1;NextConnector=GetActorTransform();PlannedMetres=0.;bObservedPosition=false;bProcessesActive=true;FillHorizon();
}
FBootPlanRequest ABootEndlessTrack::MakeRequest() const
{
    FBootPlanRequest R;R.Run=Run;R.NextInstanceId=NextId;R.Connector=NextConnector;R.CurrentEnvelope=GetConservativeEnvelope(false);R.PendingEnvelope=PendingEnvelope;
    R.ActiveSeconds=ActiveSeconds;R.Tier=5;R.CanonicalStartMetres=PlannedMetres;R.Modifiers=Modifiers;R.MusicContext=Music;
    for(const ABootEndlessChunk* Chunk:LiveChunks)if(IsValid(Chunk)){R.LiveDescriptors.Add(Chunk->GetDescriptor());R.CurrentEnvelope.MaxBaseCmps=FMath::Max(R.CurrentEnvelope.MaxBaseCmps,float(Chunk->GetPeakBaseCmps()));}
    return R;
}
FBootTraversalEnvelope ABootEndlessTrack::GetConservativeEnvelope(bool IncludePending) const
{
    FBootTraversalEnvelope Result=Envelope;
    auto Merge=[&](const FBootTraversalEnvelope& Other)
    {
        FText Error;if(!FBootTraversalPolicy::Validate(Other,Error))return;
        Result.RunCmps=FMath::Max(Result.RunCmps,Other.RunCmps);
        Result.MomentumCmps=FMath::Max(Result.MomentumCmps,Other.MomentumCmps);
        Result.TemporaryCmps=FMath::Max(Result.TemporaryCmps,Other.TemporaryCmps);
        Result.MaxBaseCmps=FMath::Max(Result.MaxBaseCmps,Other.MaxBaseCmps);
    };
    if(LastRunner.IsValid())Merge(LastRunner->GetParkour()->GetTraversalEnvelope());
    if(IncludePending&&PendingEnvelope.IsSet())Merge(PendingEnvelope.GetValue());
    for(const ABootEndlessChunk* Chunk:LiveChunks)if(IsValid(Chunk))Result.MaxBaseCmps=FMath::Max(Result.MaxBaseCmps,float(Chunk->GetPeakBaseCmps()));
    return Result;
}
double ABootEndlessTrack::GetRequiredHorizonCm() const
{
    auto Future=GetConservativeEnvelope();
    Future.TemporaryCmps=FMath::Min(Future.AbsoluteCmps,Future.TemporaryCmps+FBootTraversalPolicy::SurfMomentumCmps);
    return FBootEndlessReadiness::RequiredDistanceCm(Future,CatalogPeakBaseCmps());
}
double ABootEndlessTrack::GetPreparedHorizonCm() const
{
    double Distance=0.;
    for(const ABootEndlessChunk* Chunk:LiveChunks)
    {
        if(!IsValid(Chunk)||Chunk->GetDescriptor().InstanceId<Cursor.CurrentInstanceId)continue;
        if(!Chunk->IsReady())break;
        const auto& C=Chunk->GetDescriptor();Distance+=(C.CanonicalEndMetres-C.CanonicalStartMetres)*100.;
        if(C.InstanceId==Cursor.CurrentInstanceId&&LastRunner.IsValid())Distance-=AlongBaseline(C,LastRunner->GetActorLocation());
    }
    return FMath::Max(0.,Distance);
}
void ABootEndlessTrack::FillHorizon()
{
    if(!Run.RunId.IsValid())return;
    const double Required=GetRequiredHorizonCm();
    for(ABootEndlessChunk* Chunk:LiveChunks)if(!Chunk->IsReady()&&!Chunk->FinishPreparation())return;
    for(int32 Attempts=0;Attempts<FBootEndlessPlanner::MaxLive&&LiveChunks.Num()<FBootEndlessPlanner::MaxLive;++Attempts)
    {
        int32 Ahead=0;for(const ABootEndlessChunk* Chunk:LiveChunks)if(Chunk->GetDescriptor().InstanceId>=Cursor.CurrentInstanceId)++Ahead;
        if(Ahead>=3&&GetPreparedHorizonCm()>=Required)break;
        const auto Result=Planner.PlanNext(MakeRequest());if(!Result.Committed.IsSet())break;
        FActorSpawnParameters Params;Params.Owner=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Chunk=GetWorld()->SpawnActor<ABootEndlessChunk>(Result.Committed->WorldTransform.GetLocation(),Result.Committed->WorldTransform.Rotator(),Params);
        if(!Chunk)break;if(!Chunk->Configure(Result.Committed.GetValue())){Chunk->Destroy();break;}
        Chunk->OnRunThreatContact.AddWeakLambda(this,[this](ACharacter* Character,const FBootOperationKey& Operation)
        {
            if(Operation.Run==Run&&LiveChunks.ContainsByPredicate([&](const auto& C){return C->GetDescriptor().InstanceId==Operation.ChunkId;}))OnRunThreatContact.Broadcast(Character,Operation);
        });
        Chunk->SetProcessesActive(bProcessesActive);LiveChunks.Add(Chunk);NextConnector=Result.Committed->WorldExit;PlannedMetres=Result.Committed->CanonicalEndMetres;++NextId;
        OnChunkCommitted.Broadcast(Chunk->GetDescriptor());if(!Chunk->FinishPreparation())break;
    }
}
void ABootEndlessTrack::UpdateCourse(ABootRunner* Runner,float Dt)
{
    if(!IsValid(Runner)||!Run.RunId.IsValid()||!FMath::IsFinite(Dt)||Dt<0.f)return;
    LastRunner=Runner;const FVector Position=Runner->GetActorLocation();UpdatePresentationOrigin(Position);
    if(!bObservedPosition){PreviousPosition=Position;bObservedPosition=true;}
    if(!bProcessesActive){PreviousPosition=Position;return;}
    TArray<FBootCommittedChunk> Descriptors;for(const ABootEndlessChunk* Chunk:LiveChunks)if(Chunk->IsReady())Descriptors.Add(Chunk->GetDescriptor());
    Cursor.Advance(PreviousPosition,Position,Descriptors);PreviousPosition=Position;
    FBootTraversalEnvelope ApproachEnvelope=GetConservativeEnvelope();
    ApproachEnvelope.TemporaryCmps=FMath::Min(ApproachEnvelope.AbsoluteCmps,ApproachEnvelope.TemporaryCmps+FBootTraversalPolicy::SurfMomentumCmps);
    for(const ABootEndlessChunk* Chunk:LiveChunks)ApproachEnvelope.MaxBaseCmps=FMath::Max(ApproachEnvelope.MaxBaseCmps,float(Chunk->GetPeakBaseCmps()));
    for(ABootEndlessChunk* Chunk:LiveChunks)Chunk->UpdateApproach(Position,ApproachEnvelope);
    RetireReleased();FillHorizon();
}
bool ABootEndlessTrack::PinChunk(int64 Id,EBootChunkPin Pin)
{
    if(!LiveChunks.ContainsByPredicate([&](const auto& C){return C->GetDescriptor().InstanceId==Id;}))return false;
    ExternalPins.FindOrAdd(Id)|=uint8(Pin);return true;
}
void ABootEndlessTrack::UnpinChunk(int64 Id,EBootChunkPin Pin)
{
    if(auto* Bits=ExternalPins.Find(Id)){*Bits&=~uint8(Pin);if(*Bits==0)ExternalPins.Remove(Id);}
}
bool ABootEndlessTrack::IsChunkPinned(int64 Id) const
{
    if(const auto* Bits=ExternalPins.Find(Id))if(*Bits)return true;
    for(const ABootEndlessChunk* Chunk:LiveChunks)if(Chunk->GetDescriptor().InstanceId==Id)
    {
        if(Chunk->HasActiveRide())return true;
        if(LastRunner.IsValid()&&Chunk->OwnsSupport(LastRunner->GetMovementBase()))return true;
    }
    return false;
}
void ABootEndlessTrack::RetireReleased()
{
    // Retire a contiguous prefix. This preserves score's retired watermark even when an older
    // supported/pending destination is pinned; capacity pressure never removes the pinned support.
    while(!LiveChunks.IsEmpty())
    {
        ABootEndlessChunk* Chunk=LiveChunks[0];const int64 Id=Chunk->GetDescriptor().InstanceId;
        if(Id>=Cursor.CurrentInstanceId-2||IsChunkPinned(Id))break;
        LiveChunks.RemoveAt(0);ExternalPins.Remove(Id);OnChunkRetired.Broadcast(Id);Chunk->Destroy();
    }
}
void ABootEndlessTrack::SetDirectorContext(const FBootMusicContext& Context,double Seconds,const FBootRunModifiers& InModifiers)
{
    if(!FMath::IsFinite(Seconds)||Seconds<0.)return;Music=Context;ActiveSeconds=Seconds;Modifiers=InModifiers;
}
bool ABootEndlessTrack::PrepareRevision(const FBootTraversalEnvelope& Revision,FText& Error)
{
    if(Revision.Revision<=Envelope.Revision||!FBootTraversalPolicy::Validate(Revision,Error))return false;
    if(!Planner.CanPrepareEnvelope(MakeRequest(),Revision)){Error=FText::FromString(TEXT("Future continuation cannot safely prepare this revision"));return false;}
    PendingEnvelope=Revision;FillHorizon();Error=FText::GetEmpty();return true;
}
bool ABootEndlessTrack::CanPrepareBuild(const FBootRunModifiers& Candidate,const FBootTraversalEnvelope& Revision) const
{
    return Planner.CanPrepareBuild(MakeRequest(),Candidate,Revision);
}
bool ABootEndlessTrack::IsRecoveryReady(const ABootRunner* Runner) const
{
    if(!IsValid(Runner)||!Run.RunId.IsValid()||!Runner->GetCharacterMovement()->IsMovingOnGround())return false;
    const auto* Base=Runner->GetMovementBase();if(!IsValid(Base)||!Base->IsCollisionEnabled())return false;
    const ABootEndlessChunk* Supported=nullptr;int32 Ready=0;
    for(const ABootEndlessChunk* Chunk:LiveChunks)
    {
        if(!IsValid(Chunk))continue;
        if(Chunk->HasActiveRide())return false;
        if(!Chunk->IsReady())continue;++Ready;
        const auto& C=Chunk->GetDescriptor();
        if(C.Run==Run&&C.InstanceId==Cursor.CurrentInstanceId&&Base->GetOwner()==Chunk)Supported=Chunk;
    }
    if(Ready<3||!Supported)return false;
    const auto& C=Supported->GetDescriptor();
    static const auto Catalog=FBootChunkCatalog::Build();
    const auto* Definition=Catalog.FindByPredicate([&](const auto& D){return D.Id==C.DefinitionId&&D.Version==C.DefinitionVersion;});
    if(!Definition||Definition->Family!=EBootChunkFamily::Recovery||!C.Recipe.Rails.IsEmpty()||!C.Recipe.Hazards.IsEmpty())return false;
    const FVector Feet=C.WorldTransform.InverseTransformPosition(Runner->GetActorLocation()-FVector(0.,0.,Runner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    const float Radius=Runner->GetCapsuleComponent()->GetScaledCapsuleRadius();
    for(const auto& Box:C.Recipe.Boxes)
        if(Box.Role==EBootRecipeRole::Floor&&FMath::Abs(Feet.Z-(Box.Center.Z+Box.HalfSize.Z))<=5.
            &&FMath::Abs(Feet.X-Box.Center.X)+Radius<=Box.HalfSize.X
            &&FMath::Abs(Feet.Y-Box.Center.Y)+Radius<=Box.HalfSize.Y)return true;
    return false;
}
bool ABootEndlessTrack::PeekRevisionBoundary(FBootSafeBoundary& Boundary) const
{
    Boundary=FBootSafeBoundary();if(!PendingEnvelope.IsSet()||!LastRunner.IsValid()||!LastRunner->GetCharacterMovement()->IsMovingOnGround())return false;
    const auto* Base=LastRunner->GetMovementBase();
    for(const ABootEndlessChunk* Chunk:LiveChunks)
    {
        const auto& C=Chunk->GetDescriptor();if(!Chunk->IsReady()||C.InstanceId!=Cursor.CurrentInstanceId||!Chunk->OwnsSupport(Base)||Chunk->HasActiveRide())continue;
        // Revision commits only in the supported connector apron, never midgap/ride/tunnel.
        const FVector P=LastRunner->GetActorLocation();
        const double EntryDistance=FVector::Dist2D(P,C.WorldEntry.GetLocation()),ExitDistance=FVector::Dist2D(P,C.WorldExit.GetLocation());
        if(FMath::Min(EntryDistance,ExitDistance)>650.)continue;
        if(GetPreparedHorizonCm()<GetRequiredHorizonCm())return false;
        Boundary.Run=Run;Boundary.ChunkId=C.InstanceId;Boundary.PreparedRevision=PendingEnvelope->Revision;
        Boundary.bSupported=true;Boundary.bNoActiveCommitment=true;Boundary.bFutureReady=true;
        return true;
    }
    return false;
}
bool ABootEndlessTrack::CommitPreparedRevision(const FBootSafeBoundary& Boundary)
{
    FBootSafeBoundary Current;
    if(!PeekRevisionBoundary(Current)||Boundary.Run!=Current.Run||Boundary.ChunkId!=Current.ChunkId
        ||Boundary.PreparedRevision!=Current.PreparedRevision||!Boundary.bSupported||!Boundary.bNoActiveCommitment||!Boundary.bFutureReady)return false;
    Envelope=PendingEnvelope.GetValue();PendingEnvelope.Reset();return true;
}
bool ABootEndlessTrack::TryCommitRevision(FBootSafeBoundary& Boundary)
{
    return PeekRevisionBoundary(Boundary)&&CommitPreparedRevision(Boundary);
}
void ABootEndlessTrack::SetProcessesActive(bool Active)
{
    bProcessesActive=Active;for(ABootEndlessChunk* Chunk:LiveChunks)Chunk->SetProcessesActive(Active);
}
bool ABootEndlessTrack::CompleteRunOperation(ABootSurfWindow* Window,ABootRunner* Runner,EBootBeatRating Rating)
{
    if(!IsValid(Window)||!IsValid(Runner))return false;const auto* Key=Window->GetRunOperation();if(!Key||Key->Run!=Run)return false;
    for(const ABootEndlessChunk* Chunk:LiveChunks)
        if(Chunk->GetDescriptor().InstanceId==Key->ChunkId&&Chunk->IsPreparedLanding(Window,Runner)&&Window->CommitRide(Runner))
        {OnRunOperation.Broadcast(*Key,Rating);return true;}
    return false;
}
bool ABootEndlessTrack::IsFatal(FVector Center,float Radius,float HalfHeight) const
{
    if(Center.ContainsNaN()||!FMath::IsFinite(Radius)||!FMath::IsFinite(HalfHeight))return true;
    for(const ABootEndlessChunk* Chunk:LiveChunks)if(Chunk->GetDescriptor().InstanceId==Cursor.CurrentInstanceId)return Center.Z+HalfHeight<Chunk->GetDescriptor().WorldEntry.GetLocation().Z-800.;
    return Center.Z+HalfHeight<GetActorLocation().Z-800.;
}
void ABootEndlessTrack::EndPlay(const EEndPlayReason::Type Reason)
{
    for(ABootEndlessChunk* Chunk:LiveChunks)if(IsValid(Chunk))Chunk->Destroy();LiveChunks.Empty();ExternalPins.Empty();Super::EndPlay(Reason);
}





