#include "Run/BootPrototypeGameMode.h"
#include "UI/BootPrototypeHUD.h"
#include "Player/BootRunner.h"

ABootPrototypeGameMode::ABootPrototypeGameMode()
{
    DefaultPawnClass = ABootRunner::StaticClass();
    HUDClass = ABootPrototypeHUD::StaticClass();
}
