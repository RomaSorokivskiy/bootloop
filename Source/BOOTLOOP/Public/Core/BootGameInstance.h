#pragma once
#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Profile/BootProfile.h"
#include "BootGameInstance.generated.h"

class USoundBase;
enum class EBootSound : uint8 { Click, Boot, Jump, Slide, Perfect, Good, Error, Fatal, Combo, Step, Land, Snap };

UCLASS()
class BOOTLOOP_API UBootGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    UBootGameInstance();
    virtual void Init() override;
    UBootProfile* GetProfile() const { return Profile; }
    const FBootOptions& GetOptions() const { return Profile->Options; }
    void ApplyOptions(const FBootOptions& Options, bool bDisplay = true);
    bool SaveProfile();
    void Sound(EBootSound Id, float Pitch = 1.f);
    bool HasSaveError() const { return bSaveError; }
    bool ApplyInputPreferences(const FBootInputPreferences& Preferences);
    const FBootInputPreferences& GetInputPreferences() const { return Profile->Inputs; }
private:
    UPROPERTY() TObjectPtr<UBootProfile> Profile;
    UPROPERTY() TArray<TObjectPtr<USoundBase>> Sounds;
    bool bSaveError = false;
};
