#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "SinkingDepotSinkTab.generated.h"

class AFGCentralStorageContainer;
class UButton;
class UTextBlock;

namespace SinkingDepot::SinkTab
{
	/**
	 * Game clients and listen hosts (packaged game only): an after-hook on UFGInteractWidget::Init that gives the
	 * uploader panel (BPW_CentralStorage) a second tab, "AWESOME Sink", when it opens for a Dimensional Sinking Depot,
	 * and removes it again if the game reuses that cached panel for a vanilla uploader.
	 */
	void InstallHook();

	/** Called by the RCO on the client that asked to print: shows the result on the open sink tab, if any. */
	void OnPrintResult(int32 Printed, int32 StillAvailable, bool bRefused);
}

/**
 * The AWESOME Sink tab's content, built in code: the game's own track progress widgets (Standard and DNA), the number of
 * coupons available, and a Print button that asks the server to print them all into the player's inventory.
 * It sits in an Overlay next to the uploader's own content; the window frame's tab buttons decide which one shows.
 */
UCLASS(NotBlueprintable)
class USinkingDepotSinkPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Set by the hook each time the panel opens (no building: a vanilla uploader reuses the window). TemplateFont: a font from the vanilla panel, so ours matches it. */
	void Setup(AFGCentralStorageContainer* InDepot, UUserWidget* InWindow, UWidget* InVanillaContent, const FSlateFontInfo& InTemplateFont);

	/** Shows tab 0 (the uploader's own content) or tab 1 (this panel). */
	void ShowTab(int32 Index);

	void ShowPrintResult(int32 Printed, int32 StillAvailable, bool bRefused);

	/** The window frame's OnTabButtonClicked: a tab was clicked, or a gamepad tab button pressed. */
	UFUNCTION()
	void OnTabClicked(int32 ButtonIndex);

	UFUNCTION()
	void OnPrintClicked();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	void BuildTree();
	/** Gives the text blocks the vanilla panel's font (set by Setup), at sizes for this layout. */
	void ApplyFont();
	/** Four times a second while the window is open: refreshes the numbers if the sink tab is showing. */
	void Poll();
	void Refresh();

	UPROPERTY()
	TObjectPtr<UUserWidget> StandardTrack;
	UPROPERTY()
	TObjectPtr<UUserWidget> DnaTrack;
	UPROPERTY()
	TObjectPtr<UTextBlock> CouponsText;
	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;
	UPROPERTY()
	TObjectPtr<UTextBlock> PrintLabel;
	UPROPERTY()
	TObjectPtr<UButton> PrintButton;

	TWeakObjectPtr<AFGCentralStorageContainer> Depot;
	TWeakObjectPtr<UUserWidget> Window;
	TWeakObjectPtr<UWidget> VanillaContent;
	ESlateVisibility VanillaVisibility = ESlateVisibility::SelfHitTestInvisible;
	FSlateFontInfo TemplateFont;
	bool bHasTemplateFont = false;

	FTimerHandle PollTimer;
	int32 ActiveTab = 0;
	int32 LastCoupons = -1;
	int64 LastPoints[2] = {-1, -1};
	float LastProgress[2] = {-1.0f, -1.0f};
	double StatusUntil = 0.0;
};
