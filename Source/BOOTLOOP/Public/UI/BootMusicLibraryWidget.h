#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Music/BootMusicLibrarySubsystem.h"
#include "Containers/Ticker.h"
#include "BootMusicLibraryWidget.generated.h"

class UBootMusicTransportComponent;

struct FBootLibraryHitArea
{
    FSlateRect Rect;
    int32 Id = 0;
    FString Hash;
};

/** Cached, timer-refreshed library view. Grid edits affect future playback leases only. */
UCLASS()
class BOOTLOOP_API UBootMusicLibraryWidget : public UUserWidget
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
    virtual FReply NativeOnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
    virtual FReply NativeOnMouseMove(const FGeometry&, const FPointerEvent&) override;
    virtual FReply NativeOnMouseWheel(const FGeometry&, const FPointerEvent&) override;
    virtual FReply NativeOnKeyDown(const FGeometry&, const FKeyEvent&) override;
    virtual FReply NativeOnKeyChar(const FGeometry&, const FCharacterEvent&) override;
    virtual void NativeOnFocusLost(const FFocusEvent&) override;
private:
    UBootMusicLibrarySubsystem* Library() const;
    UBootMusicTransportComponent* Transport() const;
    const FBootLibraryEntry* Selected() const;
    void Select(int32 Index);
    void Scroll(int32 Delta);
    void Activate(int32 Id, const FString& Hash = FString());
    void BeginEdit(int32 Field);
    bool CommitEdit();
    void ApplyGrid(double Bpm, double FirstBeat);
    void StopPreview();
    FVector2D LogicalPoint(const FGeometry&, FVector2D Absolute) const;
    TArray<FBootLibraryEntry> Items;
    FString SelectedHash, PreviewHash, Message, EditText;
    bool RefreshRealtime(float DeltaSeconds);
    FTSTicker::FDelegateHandle RefreshTicker;
    bool bActive = false, bReplaceEdit = false;
    int32 ScrollRow = 0, FocusId = 1, EditField = 0, IncludedReady = 0;
    mutable TArray<FBootLibraryHitArea> Areas;
    mutable float DrawScale = 1.f;
    mutable FVector2D DrawOrigin;
    FVector2D Mouse = FVector2D(-1,-1);
};
