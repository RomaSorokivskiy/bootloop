#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Run/BootRunCoordinator.h"
#include "BootRunCardsWidget.generated.h"

/** Presentation only: never captures input, pauses, changes pages, or chooses a card. */
UCLASS()
class BOOTLOOP_API UBootRunCardsWidget : public UUserWidget
{
    GENERATED_BODY()
    friend struct FBootV04UiTestAccess;
public:
    void SetActive(bool bActive);
    void Refresh();
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeDestruct() override;
    virtual int32 NativePaint(const FPaintArgs&, const FGeometry&, const FSlateRect&, FSlateWindowElementList&, int32, const FWidgetStyle&, bool) const override;
private:
    FBootRunSnapshot View;
    TArray<FString> KeyLabels;
    FTimerHandle RefreshTimer;
    bool bActive = false;
    bool bTrial = false;
};
