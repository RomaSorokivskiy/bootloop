#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BootPrototypeHUD.generated.h"

UCLASS()
class BOOTLOOP_API ABootPrototypeHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
