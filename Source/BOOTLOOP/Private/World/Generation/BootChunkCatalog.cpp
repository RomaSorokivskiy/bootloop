#include "World/Generation/BootChunkCatalog.h"

namespace
{
void Box(FBootChunkDefinition& D, FVector Min, FVector Max, EBootRecipeRole Role = EBootRecipeRole::Floor)
{
    FBootRecipeBox B; B.Center = (Min + Max) * .5; B.HalfSize = (Max - Min) * .5; B.Role = Role;
    D.Recipe.Boxes.Add(B); if (Role != EBootRecipeRole::Decoration) D.Occupied.Add(FBox(Min, Max));
}
void Move(FBootChunkDefinition& D, EBootBaselineMove Kind, FVector Start, FVector End)
{
    FBootBaselineMoveRecipe M; M.Kind = Kind; M.Start = Start; M.End = End; D.Recipe.BaselineMoves.Add(M);
}
FBootChunkDefinition Base(const TCHAR* Id, EBootChunkFamily Family, double Length, float Cost)
{
    FBootChunkDefinition D; D.Id = FName(Id); D.Family = Family; D.DifficultyCost = Cost;
    D.Exit = FTransform(FVector(Length, 0., 0.)); D.MusicalTags = 1u << uint8(Family);
    D.Recipe.Baseline = {FVector::ZeroVector, FVector(Length,0.,0.)};
    D.BodyClearance.Add(FBox(FVector(0.,-220.,1.), FVector(Length,220.,400.)));
    return D;
}
void Floor(FBootChunkDefinition& D, double Start, double End, double Y = 0., double HalfWidth = 600.)
{
    Box(D,FVector(Start,Y-HalfWidth,-80.),FVector(End,Y+HalfWidth,0.));
}
void TiltedBox(FBootChunkDefinition& D,FVector Center,FVector Half,FRotator Rotation,EBootRecipeRole Role)
{
    FBootRecipeBox B;B.Center=Center;B.HalfSize=Half;B.Rotation=Rotation;B.Role=Role;D.Recipe.Boxes.Add(B);
    if(Role!=EBootRecipeRole::Decoration)D.Occupied.Add(FBox(-Half,Half).TransformBy(FTransform(Rotation,Center)));
}
void RampStrip(FBootChunkDefinition& D,FVector Start,FVector End,int32 Side,double HalfWidth=540.,double SeamOverlap=0.)
{
    const FVector Delta=End-Start;
    const FRotator Tilt(0.,Delta.Rotation().Yaw,Side*55.);
    const FVector LocalNormal=FRotator(0.,0.,Side*55.).RotateVector(FVector::UpVector);
    const double Y=-FMath::Sign(LocalNormal.Y)*140.;
    // The route centreline stays at deck elevation. Move the slab sideways
    // within that plane instead of enlarging it into a tall corridor wall.
    const double Z=-(LocalNormal.Y*Y+18.)/LocalNormal.Z;
    const FVector Offset=FRotator(0.,Tilt.Yaw,0.).RotateVector(FVector(0.,Y,Z));
    TiltedBox(D,(Start+End)*.5+Offset,FVector(Delta.Size()*.5+SeamOverlap,HalfWidth,18.),Tilt,EBootRecipeRole::SurfRamp);
}
void Rail(FBootChunkDefinition& D, double Y)
{
    FBootRailRecipe R; R.Start = FVector(2500.,Y,0.); R.End = FVector(4000.,Y,0.);
    R.PlatformHalfSize = FVector(500.,375.,40.); R.SpeedCmps = 450.f; R.OperationId = 1;
    D.Recipe.Rails.Add(R);
    D.RailSweeps.Add(FBox(FVector(2000.,Y-375.,-80.),FVector(4500.,Y+375.,0.)));
    D.BodyClearance.Add(FBox(FVector(2000.,Y-220.,1.),FVector(4500.,Y+220.,400.)));
}
bool FiniteBox(const FBox& B)
{
    return B.IsValid && !B.Min.ContainsNaN() && !B.Max.ContainsNaN() && B.GetSize().GetMin() > 0.;
}
double PathLength(const TArray<FVector>& Points)
{
    double Length = 0.; for (int32 I=1; I<Points.Num(); ++I) Length += FVector::Distance(Points[I-1],Points[I]); return Length;
}
}

TArray<FBootChunkDefinition> FBootChunkCatalog::Build()
{
    TArray<FBootChunkDefinition> Result;
    // The opening has a visible bend after roughly two seconds. Its broad floor
    // gives the player room to learn the direction before the challenge mix starts.
    for(const int32 Sign:{-1,1})
    {
        auto D=Base(Sign>0?TEXT("intro.left"):TEXT("intro.right"),EBootChunkFamily::GapTurn,4400.,0.f);
        D.Exit=FTransform(FRotator(0.,Sign*90.,0.),FVector(2000.,Sign*2400.,0.));
        D.Recipe.Baseline={FVector::ZeroVector,FVector(2000.,0.,0.),D.Exit.GetLocation()};
        Floor(D,0.,2600.);
        Box(D,FVector(1400.,Sign>0?600.:-2400.,-80.),FVector(2600.,Sign>0?2400.:-600.,0.));
        D.BodyClearance.Empty();
        D.BodyClearance.Add(FBox(FVector(0.,-220.,1.),FVector(2000.,220.,400.)));
        D.BodyClearance.Add(FBox(FVector(1780.,Sign>0?0.:-2400.,1.),FVector(2220.,Sign>0?2400.:0.,400.)));
        Result.Add(D);
    }
    {
        auto D=Base(TEXT("recovery.breath"),EBootChunkFamily::Recovery,2400.,0.f);
        D.bSafeContinuation=true;Floor(D,0.,2400.);Result.Add(D);
    }
    for (int32 I=0; I<2; ++I)
    {
        auto D = Base(I == 0 ? TEXT("recovery.clean") : TEXT("recovery.long"), EBootChunkFamily::Recovery, I == 0 ? 6000. : 8000., 0.f);
        D.bSafeContinuation = true; Floor(D,0.,D.Exit.GetLocation().X); Result.Add(D);
    }
    {
        auto D = Base(TEXT("gap.short"),EBootChunkFamily::GapTurn,6000.,1.f);
        Floor(D,0.,2800.); Floor(D,2980.,6000.);
        Move(D,EBootBaselineMove::Jump,FVector(2600.,0.,0.),FVector(3200.,0.,0.)); Result.Add(D);
    }
    {
        auto D = Base(TEXT("turn.dogleg"),EBootChunkFamily::GapTurn,6000.,1.f);
        D.Recipe.Baseline = {FVector::ZeroVector,FVector(1600.,0.,0.),FVector(2400.,700.,0.),FVector(3600.,700.,0.),FVector(4400.,0.,0.),FVector(6000.,0.,0.)};
        Floor(D,0.,1600.); Floor(D,1600.,4400.,350.,950.); Floor(D,4400.,6000.);
        D.BodyClearance.Add(FBox(FVector(1600.,0.,1.),FVector(4400.,920.,400.))); Result.Add(D);
    }
    for(int32 Sign : {-1,1})
    {
        auto D=Base(Sign>0?TEXT("turn.elbow.left"):TEXT("turn.elbow.right"),EBootChunkFamily::GapTurn,8000.,1.f);
        D.Exit=FTransform(FRotator(0.,Sign*90.,0.),FVector(4000.,Sign*4000.,0.));
        D.Recipe.Baseline={FVector::ZeroVector,FVector(4000.,0.,0.),D.Exit.GetLocation()};
        // Two nonoverlapping solids form a real L. No diagonal floor or direct forward continuation.
        Floor(D,0.,4600.);
        Box(D,FVector(3400.,Sign>0?600.:-4000.,-80.),FVector(4600.,Sign>0?4000.:-600.,0.));
        D.BodyClearance.Empty();
        D.BodyClearance.Add(FBox(FVector(0.,-220.,1.),FVector(4000.,220.,400.)));
        D.BodyClearance.Add(FBox(FVector(3780.,Sign>0?0.:-4000.,1.),FVector(4220.,Sign>0?4000.:0.,400.)));
        Result.Add(D);
    }
    {
        auto D=Base(TEXT("recovery.steps.up"),EBootChunkFamily::Recovery,8000.,1.f);
        D.Exit=FTransform(FVector(8000.,0.,300.));
        D.Recipe.Baseline={FVector::ZeroVector};D.BodyClearance.Empty();
        Floor(D,0.,1000.);
        D.BodyClearance.Add(FBox(FVector(0.,-220.,1.),FVector(1000.,220.,400.)));
        // Ten 30cm steps are below CharacterMovement's ordinary step height, with 500cm treads.
        for(int32 I=0;I<10;++I)
        {
            const double X=1000.+500.*I,Z=30.*(I+1);
            Box(D,FVector(X,-600.,Z-80.),FVector(X+500.,600.,Z));
            D.BodyClearance.Add(FBox(FVector(X,-220.,Z+1.),FVector(X+500.,220.,Z+400.)));
            D.Recipe.Baseline.Add(FVector(X,0.,Z));
        }
        Box(D,FVector(6000.,-600.,220.),FVector(8000.,600.,300.));
        D.BodyClearance.Add(FBox(FVector(6000.,-220.,301.),FVector(8000.,220.,700.)));
        D.Recipe.Baseline.Add(FVector(8000.,0.,300.));Result.Add(D);
    }
    for (int32 I=0; I<2; ++I)
    {
        auto D = Base(I == 0 ? TEXT("slide.short") : TEXT("slide.double"),EBootChunkFamily::Slide,6000.,float(I+1));
        Floor(D,0.,6000.); D.BodyClearance.Empty();
        double Prior = 0.;
        for (int32 J=0; J<=I; ++J)
        {
            const double X = 2800. + J*1400.;
            Box(D,FVector(X,-600.,110.),FVector(X+400.,600.,240.),EBootRecipeRole::SlideCeiling);
            D.BodyClearance.Add(FBox(FVector(Prior,-220.,1.),FVector(X,220.,400.)));
            D.BodyClearance.Add(FBox(FVector(X,-220.,1.),FVector(X+400.,220.,101.)));
            Move(D,EBootBaselineMove::Slide,FVector(X-400.,0.,0.),FVector(X+600.,0.,0.)); Prior = X+400.;
        }
        D.BodyClearance.Add(FBox(FVector(Prior,-220.,1.),FVector(6000.,220.,400.))); Result.Add(D);
    }
    for (int32 I=0; I<2; ++I)
    {
        // Basic wall lanes are available in the opening family rotation. The
        // wall-to-turn combinations retain their later difficulty requirement.
        auto D = Base(I == 0 ? TEXT("wall.left") : TEXT("wall.right"),EBootChunkFamily::Wall,6000.,1.f);
        Floor(D,0.,2800.); Floor(D,2980.,6000.);
        const double Y = I == 0 ? -440. : 440.;
        Box(D,FVector(2200.,Y-40.,0.),FVector(3800.,Y+40.,450.),EBootRecipeRole::Wall);
        // Default jump remains valid; wall-run is an authored alternative using the same landing.
        Move(D,EBootBaselineMove::Jump,FVector(2600.,0.,0.),FVector(3200.,0.,0.));
        Move(D,EBootBaselineMove::WallRun,FVector(2400.,Y*.82,0.),FVector(3400.,Y*.82,0.)); Result.Add(D);
    }
    {
        auto D = Base(TEXT("surf.crossing"),EBootChunkFamily::Surf,8000.,2.f);
        Floor(D,0.,2000.); Floor(D,4500.,8000.); Rail(D,0.);
        Move(D,EBootBaselineMove::Surf,FVector(2500.,0.,0.),FVector(4000.,0.,0.)); Result.Add(D);
    }
    {
        auto D = Base(TEXT("surf.branch"),EBootChunkFamily::Surf,8000.,3.f);
        Floor(D,0.,8000.);
        Box(D,FVector(1000.,600.,-80.),FVector(2000.,1700.,0.));
        Box(D,FVector(4500.,600.,-80.),FVector(6500.,1700.,0.)); Rail(D,1200.);
        D.BodyClearance.Add(FBox(FVector(1280.,0.,1.),FVector(1720.,1420.,400.)));
        D.BodyClearance.Add(FBox(FVector(1280.,980.,1.),FVector(5720.,1420.,400.)));
        D.BodyClearance.Add(FBox(FVector(5280.,0.,1.),FVector(5720.,1420.,400.)));
        D.Recipe.OptionalBranch = {FVector::ZeroVector,FVector(1500.,0.,0.),FVector(1500.,1200.,0.),FVector(2500.,1200.,0.),FVector(4000.,1200.,0.),FVector(5500.,1200.,0.),FVector(5500.,0.,0.),FVector(8000.,0.,0.)}; Result.Add(D);
    }
    for (int32 I=0; I<2; ++I)
    {
        auto D = Base(I == 0 ? TEXT("process.scan") : TEXT("process.popup"),EBootChunkFamily::Process,8000.,float(2+I));
        Floor(D,0.,8000.);
        FBootHazardRecipe H; H.Sweep=FBox(FVector(5500.,-100.,0.),FVector(5900.,500.,280.));
        H.TelegraphSeconds=1.2; H.OperationId=1; H.bPopup=I != 0;
        // Popup inherits the native1000x750x80solid. Its top-centre can travel
        // from(5500,400,80) to(6700,400,80), entirely inside this swept reservation.
        if(H.bPopup)H.Sweep=FBox(FVector(5000.,25.,0.),FVector(7200.,775.,280.));
        D.Recipe.Hazards.Add(H); D.HazardSweeps.Add(H.Sweep);
        D.Recipe.Baseline = {FVector::ZeroVector,FVector(4500.,-400.,0.),FVector(H.bPopup?7400.:6200.,-400.,0.),FVector(8000.,0.,0.)};
        D.BodyClearance.Empty();
        D.BodyClearance.Add(FBox(FVector(0.,-550.,1.),FVector(H.Sweep.Min.X,220.,400.)));
        D.BodyClearance.Add(FBox(FVector(H.Sweep.Min.X,-550.,1.),FVector(H.Sweep.Max.X,-200.,400.)));
        D.BodyClearance.Add(FBox(FVector(H.Sweep.Max.X,-550.,1.),FVector(8000.,220.,400.))); Result.Add(D);
    }
    for(int32 Cost=1;Cost<=2;++Cost)
    {
        const double Length=Cost==1?10000.:14000.;
        auto D=Base(Cost==1?TEXT("combination.gap_slide"):TEXT("combination.gap_slide_gap"),EBootChunkFamily::GapTurn,Length,3.f);
        D.CombinationCost=Cost;Floor(D,0.,2800.);
        Floor(D,2980.,Cost==1?Length:10000.);
        if(Cost==2)Floor(D,10180.,Length);
        Move(D,EBootBaselineMove::Jump,FVector(2600.,0.,0.),FVector(3200.,0.,0.));
        Box(D,FVector(5800.,-600.,110.),FVector(6200.,600.,240.),EBootRecipeRole::SlideCeiling);
        Move(D,EBootBaselineMove::Slide,FVector(5400.,0.,0.),FVector(6400.,0.,0.));
        if(Cost==2)Move(D,EBootBaselineMove::Jump,FVector(9800.,0.,0.),FVector(10400.,0.,0.));
        D.BodyClearance.Empty();
        D.BodyClearance.Add(FBox(FVector(0.,-220.,1.),FVector(5800.,220.,400.)));
        D.BodyClearance.Add(FBox(FVector(5800.,-220.,1.),FVector(6200.,220.,101.)));
        D.BodyClearance.Add(FBox(FVector(6200.,-220.,1.),FVector(Length,220.,400.)));
        Result.Add(D);
    }
    for(int32 Count=2;Count<=3;++Count)
    {
        const double LastX=5500.+12000.*(Count-1),Length=LastX+4500.;
        auto D=Base(Count==2?TEXT("process.scan_pair"):TEXT("process.scan_triple"),EBootChunkFamily::Process,Length,3.f);
        D.ProcessCost=Count;Floor(D,0.,Length);D.BodyClearance.Empty();
        D.BodyClearance.Add(FBox(FVector(0.,-550.,1.),FVector(Length,-200.,400.)));
        D.BodyClearance.Add(FBox(FVector(0.,-550.,1.),FVector(4500.,220.,400.)));
        D.BodyClearance.Add(FBox(FVector(LastX+400.,-550.,1.),FVector(Length,220.,400.)));
        D.Recipe.Baseline={FVector::ZeroVector,FVector(4500.,-400.,0.),FVector(LastX+1200.,-400.,0.),FVector(Length,0.,0.)};
        for(int32 I=0;I<Count;++I)
        {
            FBootHazardRecipe H;const double X=5500.+12000.*I;
            H.Sweep=FBox(FVector(X,-100.,0.),FVector(X+400.,500.,280.));H.TelegraphSeconds=1.2;H.OperationId=I+1;
            D.Recipe.Hazards.Add(H);D.HazardSweeps.Add(H.Sweep);
        }
        Result.Add(D);
    }
    // These are basic movement links, available without a risk card. HOT LOOP
    // still adds its separate longer, higher-cost combinations.
    for(const bool GapFirst:{false,true})
    {
        auto D=Base(GapFirst?TEXT("flow.gap_turn_slide"):TEXT("flow.slide_gap"),EBootChunkFamily::GapTurn,8000.,1.5f);
        const double Gap=GapFirst?2400.:4400.,Beam=GapFirst?5200.:2400.;
        Floor(D,0.,Gap);Floor(D,Gap+180.,8000.,GapFirst?350.:0.,GapFirst?1000.:600.);
        D.BodyClearance.Empty();
        D.BodyClearance.Add(FBox(FVector(0.,-220.,1.),FVector(Beam,1200.,400.)));
        D.BodyClearance.Add(FBox(FVector(Beam,-220.,1.),FVector(Beam+400.,1200.,101.)));
        D.BodyClearance.Add(FBox(FVector(Beam+400.,-220.,1.),FVector(8000.,1200.,400.)));
        Box(D,FVector(Beam,-600.,110.),FVector(Beam+400.,GapFirst?1350.:600.,240.),EBootRecipeRole::SlideCeiling);
        Move(D,EBootBaselineMove::Jump,FVector(Gap-200.,0.,0.),FVector(Gap+500.,0.,0.));
        Move(D,EBootBaselineMove::Slide,FVector(Beam-400.,0.,0.),FVector(Beam+1000.,0.,0.));
        if(GapFirst)D.Recipe.Baseline={FVector::ZeroVector,FVector(3200.,0.,0.),FVector(4200.,700.,0.),FVector(6400.,700.,0.),FVector(7400.,0.,0.),FVector(8000.,0.,0.)};
        Result.Add(D);
    }
    for(const int32 Sign:{-1,1})
    {
        auto D=Base(Sign>0?TEXT("flow.wall_exit.left"):TEXT("flow.wall_exit.right"),EBootChunkFamily::Wall,8000.,2.f);
        D.Exit=FTransform(FRotator(0.,Sign*90.,0.),FVector(4000.,Sign*4000.,0.));
        D.Recipe.Baseline={FVector::ZeroVector,FVector(4000.,0.,0.),D.Exit.GetLocation()};
        Floor(D,0.,1000.);Floor(D,1180.,4600.);
        Box(D,FVector(3400.,Sign>0?600.:-4000.,-80.),FVector(4600.,Sign>0?4000.:-600.,0.));
        Box(D,FVector(600.,Sign*440.-40.,0.),FVector(2100.,Sign*440.+40.,450.),EBootRecipeRole::Wall);
        D.BodyClearance.Empty();
        D.BodyClearance.Add(FBox(FVector(0.,-220.,1.),FVector(4000.,220.,400.)));
        D.BodyClearance.Add(FBox(FVector(3780.,Sign>0?0.:-4000.,1.),FVector(4220.,Sign>0?4000.:0.,400.)));
        Move(D,EBootBaselineMove::Jump,FVector(800.,0.,0.),FVector(1800.,0.,0.));
        Move(D,EBootBaselineMove::WallRun,FVector(650.,Sign*360.,0.),FVector(2050.,Sign*360.,0.));Result.Add(D);
    }
    for(const bool Branch:{false,true})
    {
        auto D=Base(Branch?TEXT("surf.flow.branch"):TEXT("surf.flow"),EBootChunkFamily::Surf,7000.,Branch?2.5f:2.f);
        Floor(D,0.,7000.);
        const double Y=Branch?1100.:0.;
        FBootRailRecipe R;R.Start=FVector(2500.,Y,82.);R.End=FVector(4000.,Y,82.);
        R.PlatformHalfSize=FVector(900.,375.,40.);R.SpeedCmps=900.f;R.OperationId=1;R.bFlowRide=true;D.Recipe.Rails.Add(R);
        D.RailSweeps.Add(FBox(FVector(1600.,Y-375.,2.),FVector(4900.,Y+375.,82.)));
        if(Branch)
        {
            Box(D,FVector(1200.,500.,-80.),FVector(5800.,1700.,0.));
            D.BodyClearance.Add(FBox(FVector(1200.,0.,1.),FVector(5800.,1450.,400.)));
            D.Recipe.OptionalBranch={FVector::ZeroVector,FVector(1200.,0.,0.),FVector(1800.,Y,0.),FVector(2500.,Y,0.),FVector(4000.,Y,0.),FVector(5200.,Y,0.),FVector(5800.,0.,0.),FVector(7000.,0.,0.)};
        }
        else
        {
            Move(D,EBootBaselineMove::Surf,FVector(2500.,0.,0.),FVector(4000.,0.,0.));
        }
        Result.Add(D);
    }
    // Descending terraces balance the existing ascent instead of climbing forever.
    {
        auto D=Base(TEXT("flow.steps.down"),EBootChunkFamily::Recovery,6000.,1.f);
        D.Exit=FTransform(FVector(6000.,0.,-240.));D.Recipe.Baseline={FVector::ZeroVector};D.BodyClearance.Empty();
        Floor(D,0.,1000.);D.BodyClearance.Add(FBox(FVector(0.,-220.,1.),FVector(1000.,220.,400.)));
        for(int32 I=0;I<8;++I)
        {
            const double X=1000.+500.*I,Z=-30.*(I+1);
            Box(D,FVector(X,-600.,Z-80.),FVector(X+500.,600.,Z));
            D.BodyClearance.Add(FBox(FVector(X,-220.,Z+1.),FVector(X+500.,220.,Z+400.)));D.Recipe.Baseline.Add(FVector(X,0.,Z));
        }
        Box(D,FVector(5000.,-600.,-320.),FVector(6000.,600.,-240.));D.Recipe.Baseline.Add(D.Exit.GetLocation());
        D.BodyClearance.Add(FBox(FVector(5000.,-220.,-239.),FVector(6000.,220.,160.)));Result.Add(D);
    }
    // Sweeping S-bends use real rotated floor tiles. Straight entry/exit aprons
    // retain exact connector seams, while the route drifts into an open district.
    for(const int32 Sign:{-1,1})for(const bool Wide:{false,true})
    {
        const FString Id=FString::Printf(TEXT("flow.curve.%s%s"),Sign>0?TEXT("left"):TEXT("right"),Wide?TEXT(".wide"):TEXT(""));
        auto D=Base(*Id,EBootChunkFamily::GapTurn,8000.,1.f);D.MinWidthScale=1.f;
        const double Offset=Sign*(Wide?2600.:1500.);
        D.Exit=FTransform(FVector(8000.,Offset,0.));D.BodyClearance.Empty();
        D.Recipe.Baseline={FVector::ZeroVector,FVector(700.,0.,0.)};Floor(D,0.,700.);
        D.BodyClearance.Add(FBox(FVector(0,-220,1),FVector(700,220,400)));
        FVector Previous(700,0,0);
        for(int32 I=1;I<=10;++I)
        {
            const double T=I/10.;const FVector Next(700.+6600.*T,Offset*(.5-.5*FMath::Cos(PI*T)),0.);
            const FVector Center=(Previous+Next)*.5;
            const FRotator Rotation= (Next-Previous).Rotation();
            TiltedBox(D,Center-FVector(0,0,40),FVector(FVector::Distance(Previous,Next)*.5+72.,650.,40.),Rotation,EBootRecipeRole::Floor);
            D.BodyClearance.Add(FBox(FVector(-FVector::Distance(Previous,Next)*.5,-220,1),FVector(FVector::Distance(Previous,Next)*.5,220,400)).TransformBy(FTransform(Rotation,Center)));
            D.Recipe.Baseline.Add(Next);Previous=Next;
        }
        Floor(D,7300.,8000.,Offset,650.);D.BodyClearance.Add(FBox(FVector(7300,Offset-220,1),FVector(8000,Offset+220,400)));
        D.Recipe.Baseline.Add(D.Exit.GetLocation());Result.Add(D);
    }
    // Traversal lanes have no hidden floor under the gap. Their gap length is
    // stretched by the planner beyond the current ordinary ballistic jump.
    for(const int32 Sign:{-1,1})for(const bool Ramp:{false,true})
    {
        const FString Id=FString::Printf(TEXT("%s.required.%s"),Ramp?TEXT("ramp"):TEXT("wall"),Sign>0?TEXT("left"):TEXT("right"));
        auto D=Base(*Id,Ramp?EBootChunkFamily::Surf:EBootChunkFamily::Wall,8000.,Ramp?2.f:1.8f);
        D.MinWidthScale=1.f;D.TechniqueGapStart=2400.;D.TechniqueGapEnd=4600.;
        Floor(D,0.,2400.);Floor(D,4600.,8000.);
        if(Ramp)
        {
            D.Version=2;
            // Keep a real air gap to both aprons: no buried end faces or
            // overlapping platform/ramp collision. The planner scales this
            // straight strip and both clearances with the technique gap.
            RampStrip(D,FVector(2560.,0,0),FVector(4440.,0,0),Sign);
            Move(D,EBootBaselineMove::Surf,FVector(2200,0,0),FVector(4800,0,0));
        }
        else
        {
            Box(D,FVector(2000.,Sign*440.-40.,-300.),FVector(5000.,Sign*440.+40.,650.),EBootRecipeRole::Wall);
            Move(D,EBootBaselineMove::WallRun,FVector(2200.,Sign*350.,0.),FVector(4800.,Sign*350.,0.));
        }
        Result.Add(D);
    }
    // Compact, mirrored surf routes: a short straight, a lateral bend and an
    // S-return. Curves have fixed authored geometry; do not pass yawed slabs
    // through the X-only stretching used by the straight required technique gap.
    for(const int32 Sign:{-1,1})for(int32 Shape=0;Shape<3;++Shape)
    {
        const TCHAR* ShapeName=Shape==0?TEXT("short"):Shape==1?TEXT("bend"):TEXT("s_curve");
        const FString Id=FString::Printf(TEXT("ramp.%s.%s"),ShapeName,Sign>0?TEXT("left"):TEXT("right"));
        const double StartX=2200.,RampLength=Shape==0?1400.:Shape==1?2400.:2800.;
        const double EndX=StartX+RampLength,ExitY=Shape==1?Sign*800.:0.;
        auto D=Base(*Id,EBootChunkFamily::Surf,EndX+2200.,Shape==0?1.5f:Shape==1?2.f:2.5f);
        D.MinWidthScale=1.f;D.Exit=FTransform(FVector(EndX+2200.,ExitY,0.));
        D.BodyClearance.Empty();D.Recipe.Baseline={FVector::ZeroVector};
        const int32 Segments=Shape==0?1:24;
        FVector Previous(StartX,0.,0.);D.Recipe.Baseline.Add(Previous);
        for(int32 I=1;I<=Segments;++I)
        {
            const double T=double(I)/Segments;
            const double Y=Shape==0?0.:Shape==1?ExitY*(.5-.5*FMath::Cos(PI*T))
                :Sign*500.*(.5-.5*FMath::Cos(2.*PI*T));
            const FVector Next(StartX+RampLength*T,Y,0.);
            // Short overlapping facets close the wedge between neighbouring
            // headings while staying below the 32-solid native chunk budget.
            RampStrip(D,Previous,Next,Sign,Shape==0?500.:540.,Shape==0?0.:24.);
            const FVector Center=(Previous+Next)*.5;const FRotator Heading=(Next-Previous).Rotation();
            D.BodyClearance.Add(FBox(FVector(-FVector::Distance(Previous,Next)*.5,-220.,1.),
                FVector(FVector::Distance(Previous,Next)*.5,220.,400.)).TransformBy(FTransform(Heading,Center)));
            D.Recipe.Baseline.Add(Next);Previous=Next;
        }
        // Measure the rotated collision, not its nominal centreline endpoints.
        // This retains at least 160cm of open air even on the curved variants.
        FBox RampBounds(ForceInit);for(const auto& B:D.Occupied)RampBounds+=B;
        const double TakeoffEnd=RampBounds.Min.X-160.,LandingStart=RampBounds.Max.X+160.;
        Floor(D,0.,TakeoffEnd);Floor(D,LandingStart,D.Exit.GetLocation().X,ExitY);
        D.BodyClearance.Add(FBox(FVector(0.,-220.,1.),FVector(TakeoffEnd,220.,400.)));
        D.BodyClearance.Add(FBox(FVector(LandingStart,ExitY-220.,1.),FVector(D.Exit.GetLocation().X,ExitY+220.,400.)));
        D.Recipe.Baseline.Add(D.Exit.GetLocation());
        Move(D,EBootBaselineMove::Surf,FVector(TakeoffEnd-200.,0.,0.),FVector(LandingStart+200.,ExitY,0.));
        Result.Add(D);
    }
    // Red stationary blockers complement the moving processes: alternating
    // side gates, a low vault and a crouch gate with an ordinary clear bypass.
    for(const int32 Sign:{-1,1})
    {
        auto D=Base(Sign>0?TEXT("obstacle.weave.left"):TEXT("obstacle.weave.right"),EBootChunkFamily::Process,9000.,1.5f);
        Floor(D,0.,9000.);D.BodyClearance.Empty();D.Recipe.Baseline={FVector::ZeroVector};
        for(int32 I=0;I<3;++I)
        {
            const double Side=Sign*(I%2==0?1.:-1.),X=2200.+I*2100.;
            Box(D,FVector(X,Side>0?-80.:-600.,0.),FVector(X+120.,Side>0?600.:80.,430.),EBootRecipeRole::Fatal);
            D.Recipe.Baseline.Add(FVector(X-450.,-Side*400.,0.));
            D.Recipe.Baseline.Add(FVector(X+450.,-Side*400.,0.));
        }
        D.Recipe.Baseline.Add(D.Exit.GetLocation());
        for(int32 I=1;I<D.Recipe.Baseline.Num();++I)
        {const auto A=D.Recipe.Baseline[I-1],B=D.Recipe.Baseline[I];D.BodyClearance.Add(FBox(A.ComponentMin(B)+FVector(0,-100,1),A.ComponentMax(B)+FVector(0,100,300)));}
        Result.Add(D);
        auto Vault=Base(Sign>0?TEXT("obstacle.vault.left"):TEXT("obstacle.vault.right"),EBootChunkFamily::GapTurn,6000.,1.f);
        Vault.MinWidthScale=1.f;
        Floor(Vault,0.,6000.);Vault.BodyClearance.Empty();
        Box(Vault,FVector(2600.,Sign>0?-600.:50.,0.),FVector(2800.,Sign>0?-50.:600.,65.),EBootRecipeRole::Fatal);
        Box(Vault,FVector(3800.,Sign>0?50.:-600.,110.),FVector(4100.,Sign>0?600.:-50.,240.),EBootRecipeRole::SlideCeiling);
        Vault.BodyClearance.Add(FBox(FVector(0,-40,1),FVector(6000,40,400)));Result.Add(Vault);
    }
    // The original process recipes all occupied the positive-Y lane. Mirror the
    // complete solids, bypass, warning and operation sweep as one authored unit.
    const int32 BeforeMirrors=Result.Num();
    auto MirrorBounds=[](TArray<FBox>& Boxes){for(auto& B:Boxes){const double Min=B.Min.Y;B.Min.Y=-B.Max.Y;B.Max.Y=-Min;}};
    for(int32 I=0;I<BeforeMirrors;++I)if(!Result[I].Recipe.Hazards.IsEmpty())
    {
        auto D=Result[I];D.Id=FName(*(D.Id.ToString()+TEXT(".mirror")));
        for(auto& B:D.Recipe.Boxes){B.Center.Y=-B.Center.Y;B.Rotation.Yaw=-B.Rotation.Yaw;B.Rotation.Roll=-B.Rotation.Roll;}
        for(auto& P:D.Recipe.Baseline)P.Y=-P.Y;
        for(auto& P:D.Recipe.OptionalBranch)P.Y=-P.Y;
        for(auto& M:D.Recipe.BaselineMoves){M.Start.Y=-M.Start.Y;M.End.Y=-M.End.Y;}
        MirrorBounds(D.Occupied);MirrorBounds(D.BodyClearance);MirrorBounds(D.HazardSweeps);MirrorBounds(D.RailSweeps);
        for(auto& H:D.Recipe.Hazards){const double Min=H.Sweep.Min.Y;H.Sweep.Min.Y=-H.Sweep.Max.Y;H.Sweep.Max.Y=-Min;}
        Result.Add(D);
    }
    for(auto& D:Result)D.ProcessCost=D.Recipe.Hazards.Num();
    Result.Sort([](const auto& A,const auto& B){return A.Id.ToString() < B.Id.ToString();});
    return Result;
}

bool FBootChunkCatalog::Validate(const FBootChunkDefinition& D,FString& Error)
{
    auto Fail=[&](const TCHAR* Message){Error=Message;return false;};
    if (D.Id.IsNone() || D.Version == 0 || D.Entry.ContainsNaN() || D.Exit.ContainsNaN()
        || !D.Entry.GetScale3D().Equals(FVector::OneVector) || !D.Exit.GetScale3D().Equals(FVector::OneVector)) return Fail(TEXT("Invalid connector identity/frame"));
    if(!FMath::IsFinite(D.TechniqueGapStart)||!FMath::IsFinite(D.TechniqueGapEnd)||D.TechniqueGapStart<0.||D.TechniqueGapEnd<D.TechniqueGapStart)return Fail(TEXT("Invalid technique gap interval"));
    if (D.RequiredAbilityMask != 0 || !FMath::IsFinite(D.DifficultyCost) || D.DifficultyCost < 0.f
        || !FMath::IsFinite(D.MinSpeedCmps) || !FMath::IsFinite(D.MaxSpeedCmps)
        || D.MinSpeedCmps < 0.f || D.MaxSpeedCmps > 3600.f || D.MaxSpeedCmps < D.MinSpeedCmps) return Fail(TEXT("Invalid baseline speed/ability/difficulty"));
    if (D.Recipe.Boxes.IsEmpty() || D.Recipe.Baseline.Num()<2 || D.Occupied.IsEmpty() || D.BodyClearance.IsEmpty()) return Fail(TEXT("Missing materializable route"));
    if(D.CombinationCost<0||D.CombinationCost>2||D.ProcessCost!=D.Recipe.Hazards.Num()||D.ProcessCost>3)
        return Fail(TEXT("Invalid explicit combination/process budget"));
    TSet<int32> OperationIds;
    for(const auto& Rail:D.Recipe.Rails){if(OperationIds.Contains(Rail.OperationId))return Fail(TEXT("Duplicate operation ID"));OperationIds.Add(Rail.OperationId);}
    for(const auto& Hazard:D.Recipe.Hazards){if(OperationIds.Contains(Hazard.OperationId))return Fail(TEXT("Duplicate operation ID"));OperationIds.Add(Hazard.OperationId);}
    for (const auto* Volumes : {&D.Occupied,&D.BodyClearance,&D.RailSweeps,&D.HazardSweeps})
        for (const auto& B : *Volumes) if (!FiniteBox(B)) return Fail(TEXT("Invalid swept volume"));
    for (const auto& B : D.Recipe.Boxes)
        if (B.Center.ContainsNaN() || B.HalfSize.ContainsNaN() || B.Rotation.ContainsNaN() || B.HalfSize.GetMin() <= 0.) return Fail(TEXT("Invalid recipe box"));
    auto Covered=[](const FBox& B,const TArray<FBox>& Volumes)
    {
        for(const auto& V:Volumes)if(V.Min.X<=B.Min.X+.01&&V.Min.Y<=B.Min.Y+.01&&V.Min.Z<=B.Min.Z+.01
            &&V.Max.X>=B.Max.X-.01&&V.Max.Y>=B.Max.Y-.01&&V.Max.Z>=B.Max.Z-.01)return true;
        return false;
    };
    for(const auto& B:D.Recipe.Boxes)
        if(B.Role!=EBootRecipeRole::Decoration&&!Covered(FBox(-B.HalfSize,B.HalfSize).TransformBy(FTransform(B.Rotation,B.Center)),D.Occupied))return Fail(TEXT("Solid recipe escapes declared occupied volume"));
    for (const FVector& P : D.Recipe.Baseline) if (P.ContainsNaN()) return Fail(TEXT("Nonfinite path"));
    if (!D.Recipe.Baseline[0].Equals(D.Entry.GetLocation(),.1) || !D.Recipe.Baseline.Last().Equals(D.Exit.GetLocation(),.1)) return Fail(TEXT("Baseline connector mismatch"));
    if (!D.Recipe.OptionalBranch.IsEmpty())
    {
        for (const FVector& P : D.Recipe.OptionalBranch) if (P.ContainsNaN()) return Fail(TEXT("Nonfinite branch"));
        if (D.Recipe.OptionalBranch.Num()<2 || D.Recipe.OptionalBranch.Num()>16
            || !D.Recipe.OptionalBranch[0].Equals(D.Entry.GetLocation(),.1) || !D.Recipe.OptionalBranch.Last().Equals(D.Exit.GetLocation(),.1)
            || PathLength(D.Recipe.OptionalBranch)>2.*PathLength(D.Recipe.Baseline)) return Fail(TEXT("Unbounded or unmerged branch"));
    }
    for (const auto& H : D.Recipe.Hazards)
        if (!FiniteBox(H.Sweep) || !Covered(H.Sweep,D.HazardSweeps) || !FMath::IsFinite(H.TelegraphSeconds) || H.TelegraphSeconds < .9) return Fail(TEXT("Invalid hazard telegraph/sweep"));
    for (const auto& R : D.Recipe.Rails)
    {
        if (R.Start.ContainsNaN() || R.End.ContainsNaN() || R.PlatformHalfSize.ContainsNaN() || R.PlatformHalfSize.GetMin()<=0. || !FMath::IsFinite(R.SpeedCmps) || R.SpeedCmps<=0.f) return Fail(TEXT("Invalid full rail"));
        FBox Sweep(ForceInit);
        for(const auto& Top:{R.Start,R.End})
        {
            const FVector Center=Top-FVector(0.,0.,R.PlatformHalfSize.Z);
            Sweep+=Center-R.PlatformHalfSize;Sweep+=Center+R.PlatformHalfSize;
        }
        if(!Covered(Sweep,D.RailSweeps))return Fail(TEXT("Moving rail escapes declared full sweep"));
    }
    Error.Empty(); return true;
}

