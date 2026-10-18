#include "Music/BootBeatJudge.h"
namespace
{
 bool Valid(const FBootTransportSnapshot& S)
 { return S.bValid && !S.bPaused && !S.bUnderflow && FMath::IsFinite(S.LocalSeconds) && FMath::IsFinite(S.Grid.Bpm) && S.Grid.Bpm>0. && FMath::IsFinite(S.Grid.FirstBeatSeconds); }
}
EBootBeatRating FBootBeatJudge::Rate(const FBootTransportSnapshot& S, double OffsetMs)
{
 if(!Valid(S)||!FMath::IsFinite(OffsetMs))return EBootBeatRating::OffBeat;
 const double Period=60./S.Grid.Bpm;
 const double Relative=S.LocalSeconds-S.Grid.FirstBeatSeconds-FMath::Clamp(OffsetMs,-250.,250.)*.001;
 if(Relative<-.150)return EBootBeatRating::OffBeat;
 const double Nearest=FMath::Max(0.,FMath::RoundToDouble(Relative/Period));
 const double Delta=FMath::Abs(Relative-Nearest*Period);
 if(Delta<=.080+1.e-9)return EBootBeatRating::Perfect;
 if(Delta<=.150+1.e-9)return EBootBeatRating::Good;
 return EBootBeatRating::OffBeat;
}
float FBootBeatJudge::Pulse(const FBootTransportSnapshot& S)
{
 if(!S.bValid || !FMath::IsFinite(S.LocalSeconds) || !FMath::IsFinite(S.Grid.Bpm) || S.Grid.Bpm<=0. || !FMath::IsFinite(S.Grid.FirstBeatSeconds))return 0.f;
 const double Relative=S.LocalSeconds-S.Grid.FirstBeatSeconds;
 if(Relative<0.)return 0.f;
 const double Phase=FMath::Fmod(Relative,60./S.Grid.Bpm);
 return static_cast<float>(FMath::Exp(-Phase*14.));
}
