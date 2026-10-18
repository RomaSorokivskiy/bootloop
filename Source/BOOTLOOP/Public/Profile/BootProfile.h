#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Input/BootInputPreferences.h"
#include "BootProfile.generated.h"

USTRUCT()
struct FBootOptions
{
    GENERATED_BODY()
    UPROPERTY() float Music = .55f;
    UPROPERTY() float Sfx = .65f;
    UPROPERTY() float Sensitivity = .12f;
    UPROPERTY() float Fov = 95.f;
    UPROPERTY() float InputOffsetMs = 0.f;
    UPROPERTY() float UiScale = 1.f;
    UPROPERTY() float EffectsIntensity = 1.f;
    UPROPERTY() bool bPulse = true;
    UPROPERTY() bool bBody = true;
    UPROPERTY() bool bMotion = true;
    UPROPERTY() int32 PixelQuality = 0;
    UPROPERTY() int32 Resolution = 0;
    UPROPERTY() int32 WindowMode = 0;
    UPROPERTY() int32 FrameLimit = 2;
    void Clamp();
};

USTRUCT()
struct FBootCourseRecord
{
    GENERATED_BODY()
    UPROPERTY() int32 BestScore = 0;
    UPROPERTY() float BestTimeSeconds = 0.f;
    UPROPERTY() float BestDistanceMetres = 0.f;
};

USTRUCT()
struct FBootMusicRunRecord
{
    GENERATED_BODY()
    UPROPERTY() int64 BestScore=0;
    UPROPERTY() double BestDistanceMetres=0.;
    UPROPERTY() double BestActiveSeconds=0.;
};

UCLASS()
class BOOTLOOP_API UBootProfile : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() FBootOptions Options;
    UPROPERTY() int32 BestScore = 0;
    UPROPERTY() float BestDistance = 0.f;
    UPROPERTY() int32 Runs = 0;
    UPROPERTY() FBootInputPreferences Inputs;
    UPROPERTY() TMap<FName,FBootCourseRecord> CourseRecords;
    UPROPERTY() bool bTutorialComplete = false;
    UPROPERTY() TArray<int32> TutorialOutcomes;
    UPROPERTY() int32 OnboardingVersion=0;
    UPROPERTY() FBootMusicRunRecord MusicRunRecord;
    bool HasCurrentOnboarding() const { return OnboardingVersion>=4; }
    void UpdateMusicRunRecord(int64 Score,double Distance,double ActiveSeconds);
    void UpdateBest(int32 Score, float Distance);
    void UpdateCourseRecord(FName Course,int32 Score,float Distance,float Time,bool bFinished);
};
