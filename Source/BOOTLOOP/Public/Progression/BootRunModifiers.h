#pragma once
#include "CoreMinimal.h"
struct FBootRunModifiers
{
    TMap<FName, int32> Stacks;
    float WidthScale = 1.f;
    float RiskySurfWeightScale = 1.f;
    int32 ExtraProcessBudget = 0;
    int32 ExtraCombinationBudget = 0;
    double DistanceMultiplier = 1.;
    double OperationMultiplier = 1.;
};
