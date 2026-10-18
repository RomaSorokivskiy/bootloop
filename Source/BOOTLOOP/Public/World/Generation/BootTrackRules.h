#pragma once
#include "CoreMinimal.h"

struct FBootTrackSequence
{
    static constexpr double Length = 1800.;
    explicit FBootTrackSequence(int32 Seed) : Random(Seed) {}
    static bool IsHeavy(int32 Type) { return Type == 1 || Type == 2 || Type == 5; }
    int32 Next(int32 Tier)
    {
        static const int32 Intro[] = {0, 0, 1, 0, 2, 0, 3, 0, 1, 0, 2, 0};
        int32 Choice = 0;
        if (Index < UE_ARRAY_COUNT(Intro)) Choice = Intro[Index];
        else
        {
            TArray<int32> Eligible;
            for (int32 T = 0; T < 6; ++T)
                if (!(T == Last && Repeats >= 2) && !(IsHeavy(T) && Heavy >= 2)
                    && !(T == 4 && Tier < 1) && !(T == 5 && Tier < 2)) Eligible.Add(T);
            Choice = Eligible.IsEmpty() ? 0 : Eligible[Random.RandRange(0, Eligible.Num() - 1)];
        }
        Repeats = Choice == Last ? Repeats + 1 : 1;
        Heavy = IsHeavy(Choice) ? Heavy + 1 : 0;
        Last = Choice;
        ++Index;
        return Choice;
    }
private:
    FRandomStream Random;
    int32 Index = 0, Last = -1, Repeats = 0, Heavy = 0;
};
