#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Profile/BootProfile.h"
#include "UI/BootPlayerController.h"
#include "Input/BootInputCaptureState.h"
#include "Run/BootRunCoordinator.h"
#include "BootShellWidget.generated.h"

class ABootSurfWindow;
class ABootRunner;

struct FBootHitArea
{
    FSlateRect Rect;
    int32 Id = 0;
    bool bSlider = false;
};

UCLASS()
class BOOTLOOP_API UBootShellWidget : public UUserWidget
{
    GENERATED_BODY()
    friend struct FBootShellInputTestAccess;
public:
    void ChangePage(EBootPage NewPage);
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeDestruct() override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
        FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bEnabled) const override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual void NativeOnFocusLost(const FFocusEvent& Event) override;
private:
    FString InteractionPrompt(const ABootSurfWindow* Target,const ABootRunner* Runner,const FBootInputPreferences& Inputs) const;
    void RefreshMusicHud();
    FBootRunSnapshot MusicHud;
    TMap<FString,FString> SongTitles;
    FString CurrentSongTitle;
    FString NextSongTitle;
    FTimerHandle HudTimer;
    void Activate(int32 Id);
    void SetSlider(int32 Id, float Value);
    float GetSlider(int32 Id) const;
    FVector2D LogicalPoint(const FGeometry& Geometry, FVector2D Absolute) const;
    EBootPage Page = EBootPage::Desktop;
    FBootOptions Draft;
    FBootInputCaptureState InputDraft;
    FKey ActivationKey=EKeys::LeftMouseButton;
    FString InputMessage;
    mutable TArray<FBootHitArea> Areas;
    mutable float DrawScale = 1.f;
    mutable FVector2D DrawOrigin;
    FVector2D Mouse = FVector2D(-1,-1);
    int32 Focus = 0;
    int32 InputScrollRow = 0;
    int32 Drag = -1;
    double OpenedAt = 0.;
};
