#pragma once
#include "CoreMinimal.h"

/** Active gameplay time from a monotonic source; boot and pause never accrue. */
class FBootActiveRunClock
{
public:
    void Start(double Now)
    {
        Accumulated = 0.;
        LastObserved = FMath::IsFinite(Now) ? Now : 0.;
        bStarted = FMath::IsFinite(Now);
        bPaused = false;
    }
    void SetPaused(bool bInPaused, double Now)
    {
        Advance(Now);
        if (bStarted) bPaused = bInPaused;
    }
    double Seconds(double Now) const
    {
        Advance(Now);
        return Accumulated;
    }
private:
    void Advance(double Now) const
    {
        if (!bStarted || !FMath::IsFinite(Now) || Now <= LastObserved) return;
        if (!bPaused) Accumulated += Now - LastObserved;
        LastObserved = Now;
    }
    mutable double Accumulated = 0.;
    mutable double LastObserved = 0.;
    bool bStarted = false;
    bool bPaused = false;
};
