#include "SinkingDepotSinkTab.h"

#include "SinkingDepotLog.h"
#include "SinkingDepotOverflow.h"
#include "SinkingDepotRCO.h"
#include "SinkingDepotReflection.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "FGPlayerController.h"
#include "FGResourceSinkSubsystem.h"
#include "Patching/NativeHookManager.h"
#include "TimerManager.h"
#include "UI/FGInteractWidget.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DimensionalSinkingDepot"

// Everything file-local lives in SinkingDepot::SinkTab (not the global anonymous namespace), so a unity build that merges
// this file with others can't clash with their helpers.
namespace SinkingDepot::SinkTab
{
namespace
{
	constexpr const TCHAR* UploaderWidgetClassPath = TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/BPW_CentralStorage.BPW_CentralStorage_C");
	constexpr const TCHAR* TrackProgressClassPath = TEXT("/Game/FactoryGame/Buildable/Factory/ResourceSink/UI/BPW_ResourceSink_TrackProgress.BPW_ResourceSink_TrackProgress_C");
	constexpr const TCHAR* CouponIconPath = TEXT("/Game/FactoryGame/Buildable/Factory/TradingPost/UI/SchematicIcons/TXUI_SIcon_Coupon.TXUI_SIcon_Coupon");
	constexpr const TCHAR* DnaIconPath = TEXT("/Game/FactoryGame/Resource/Parts/AlienDNACapsule/UI/IconDesc_AlienDNA_256.IconDesc_AlienDNA_256");
	const FName WindowName(TEXT("Widget_Window_DarkMode"));
	const FName WindowBodySlot(TEXT("WindowBody"));
	const FName TabsContainerName(TEXT("Widget_TabsContainer"));
	const FName TabButtonsName(TEXT("mContainer"));
	const FName OverlayName(TEXT("SinkingDepotTabOverlay"));
	const FName PanelName(TEXT("SinkingDepotSinkPanel"));
	const FName FontSourceName(TEXT("mUploadRateText"));
	const FName AwesomeTable(TEXT("AWESOME_UI"));
	/** The DNA track's colour, matched to the AWESOME Sink panel's "DNA Points" swatch. */
	const FLinearColor DnaColor(0.386f, 0.191f, 0.386f, 1.0f);
	const FLinearColor ButtonColor(0.783538f, 0.291771f, 0.057805f, 1.0f); // the AWESOME Sink's own orange

	/** The open panel that asked to print, for the server's answer. */
	TWeakObjectPtr<USinkingDepotSinkPanel> GPrintingPanel;

	template <typename T>
	T* LoadCached(const TCHAR* Path, TWeakObjectPtr<T>& Cache)
	{
		if (!Cache.IsValid())
		{
			Cache = LoadObject<T>(nullptr, Path);
		}
		return Cache.Get();
	}

	UClass* UploaderWidgetClass()
	{
		static TWeakObjectPtr<UClass> Cache;
		if (!Cache.IsValid())
		{
			Cache = LoadClass<UUserWidget>(nullptr, UploaderWidgetClassPath);
		}
		return Cache.Get();
	}

	UClass* TrackProgressClass()
	{
		static TWeakObjectPtr<UClass> Cache;
		if (!Cache.IsValid())
		{
			Cache = LoadClass<UUserWidget>(nullptr, TrackProgressClassPath);
		}
		return Cache.Get();
	}

	UUserWidget* TabsContainer(UUserWidget* Window)
	{
		return Window && Window->WidgetTree ? Cast<UUserWidget>(Window->WidgetTree->FindWidget(TabsContainerName)) : nullptr;
	}

	/** The tab buttons the frame made, in order. */
	TArray<UWidget*> TabButtons(UUserWidget* Window)
	{
		UUserWidget* Container = TabsContainer(Window);
		const UCanvasPanel* Canvas = Container && Container->WidgetTree ? Cast<UCanvasPanel>(Container->WidgetTree->FindWidget(TabButtonsName)) : nullptr;
		return Canvas ? Canvas->GetAllChildren() : TArray<UWidget*>();
	}

	const FBoolProperty* FindBool(const UObject* Object, const TCHAR* Name)
	{
		return Object ? FindFProperty<FBoolProperty>(Object->GetClass(), Name) : nullptr;
	}

	/**
	 * The window frame's mTabs: one {icon, title} entry per tab button. Grows it to two entries (ours second) or shrinks it
	 * back to one, then has the frame rebuild its buttons. The entry fields have generated names (Texture_4_..., Text_5_...),
	 * so they are found by prefix.
	 * The two "override" flags make the frame lay the buttons out side by side and report clicks (OnTabButtonClicked) instead
	 * of sliding the content itself, which needs a Widget_SlidingTabs that the uploader panel doesn't have.
	 * Nothing is changed unless everything needed is there.
	 */
	bool SetSecondTab(UUserWidget* Window, const bool bWanted)
	{
		UUserWidget* Container = TabsContainer(Window);
		const FBoolProperty* WindowFlag = FindBool(Window, TEXT("mOverwriteTabButtonFunctions"));
		const FBoolProperty* ContainerFlag = FindBool(Container, TEXT("mOverrideButtonFunctions"));
		FArrayProperty* TabsProperty = Window ? FindFProperty<FArrayProperty>(Window->GetClass(), TEXT("mTabs")) : nullptr;
		FStructProperty* Inner = TabsProperty ? CastField<FStructProperty>(TabsProperty->Inner) : nullptr;
		if (!WindowFlag || !ContainerFlag || !Inner || !Window->FindFunction(TEXT("InitTabs")))
		{
			return false;
		}
		FScriptArrayHelper Tabs(TabsProperty, TabsProperty->ContainerPtrToValuePtr<void>(Window));
		if (bWanted)
		{
			if (Tabs.Num() < 1 || Tabs.Num() > 2)
			{
				return false; // not the layout this was written for; leave the panel alone
			}
			if (Tabs.Num() == 1)
			{
				static TWeakObjectPtr<UTexture2D> IconCache;
				UTexture2D* Icon = LoadCached(CouponIconPath, IconCache);
				const FText Title = FText::FromStringTable(AwesomeTable, TEXT("AWESOMESink/WindowTitle"));
				uint8* Entry = Tabs.GetRawPtr(Tabs.AddValue());
				for (TFieldIterator<FProperty> It(Inner->Struct); It; ++It)
				{
					const FString Name = It->GetName();
					if (Name.StartsWith(TEXT("Texture_")))
					{
						if (FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(*It)) // hard or soft reference
						{
							Object->SetObjectPropertyValue_InContainer(Entry, Icon);
						}
					}
					else if (Name.StartsWith(TEXT("Text_")))
					{
						if (FTextProperty* Text = CastField<FTextProperty>(*It))
						{
							Text->SetPropertyValue_InContainer(Entry, Title);
						}
					}
				}
			}
		}
		else
		{
			if (Tabs.Num() < 2 && !WindowFlag->GetPropertyValue_InContainer(Window))
			{
				return true; // already as the game made it
			}
			Tabs.Resize(FMath::Min(Tabs.Num(), 1));
		}
		WindowFlag->SetPropertyValue_InContainer(Window, bWanted);
		ContainerFlag->SetPropertyValue_InContainer(Container, bWanted);
		return Reflection::Call(Window, TEXT("InitTabs"));
	}

	/** Finds our panel if this window was given one before (the game caches interact widgets and reuses them). */
	USinkingDepotSinkPanel* FindPanel(UUserWidget* Window)
	{
		UOverlay* Overlay = Window ? Cast<UOverlay>(Window->GetContentForSlot(WindowBodySlot)) : nullptr;
		if (!Overlay || Overlay->GetFName() != OverlayName)
		{
			return nullptr;
		}
		for (UWidget* Child : Overlay->GetAllChildren())
		{
			if (USinkingDepotSinkPanel* Panel = Cast<USinkingDepotSinkPanel>(Child))
			{
				return Panel;
			}
		}
		return nullptr;
	}

	/** Wraps the window body's content in an Overlay together with a new panel. Returns the panel, or null. */
	USinkingDepotSinkPanel* AddPanel(UFGInteractWidget* Owner, UUserWidget* Window, UWidget*& OutVanillaContent)
	{
		UWidget* Body = Window->GetContentForSlot(WindowBodySlot);
		if (!Body || !Owner->WidgetTree)
		{
			return nullptr;
		}
		UOverlay* Overlay = Owner->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), OverlayName);
		USinkingDepotSinkPanel* Panel = CreateWidget<USinkingDepotSinkPanel>(Owner, USinkingDepotSinkPanel::StaticClass(), PanelName);
		if (!Overlay || !Panel)
		{
			return nullptr;
		}
		for (UWidget* Child : {Body, static_cast<UWidget*>(Panel)})
		{
			if (UOverlaySlot* NewSlot = Overlay->AddChildToOverlay(Child)) // AddChild takes Body out of the named slot first
			{
				NewSlot->SetHorizontalAlignment(HAlign_Fill);
				NewSlot->SetVerticalAlignment(VAlign_Fill);
			}
		}
		Window->SetContentForSlot(WindowBodySlot, Overlay);
		if (FMulticastDelegateProperty* Clicked = FindFProperty<FMulticastDelegateProperty>(Window->GetClass(), TEXT("OnTabButtonClicked")))
		{
			FScriptDelegate Delegate;
			Delegate.BindUFunction(Panel, GET_FUNCTION_NAME_CHECKED(USinkingDepotSinkPanel, OnTabClicked));
			Clicked->AddDelegate(MoveTemp(Delegate), Window);
		}
		OutVanillaContent = Body;
		return Panel;
	}

	void OnPanelInit(UFGInteractWidget* Self)
	{
		UClass* UploaderWidget = UploaderWidgetClass();
		if (!UploaderWidget)
		{
			static bool bLogged = false;
			if (!bLogged)
			{
				bLogged = true;
				UE_LOG(LogSinkingDepot, Warning, TEXT("Could not load %s; the AWESOME Sink tab is not added"), UploaderWidgetClassPath);
			}
			return;
		}
		if (!Self || !Self->IsA(UploaderWidget))
		{
			return; // some other panel
		}
		UUserWidget* Window = Self->WidgetTree ? Cast<UUserWidget>(Self->WidgetTree->FindWidget(WindowName)) : nullptr;
		if (!Window)
		{
			UE_LOG(LogSinkingDepot, Warning, TEXT("Uploader panel has no %s; the AWESOME Sink tab is not added"), *WindowName.ToString());
			return;
		}
		AFGCentralStorageContainer* Depot = Cast<AFGCentralStorageContainer>(Self->mInteractObject);
		if (!Depot)
		{
			UE_LOG(LogSinkingDepot, Warning, TEXT("Uploader panel opened without a building (%s); no AWESOME Sink tab"), *GetNameSafe(Self->mInteractObject));
		}
		USinkingDepotSinkPanel* Panel = FindPanel(Window);
		if (!SinkingDepot::IsSinkingDepot(Depot))
		{
			// A cached panel we changed before, now opened for a vanilla uploader: back to one tab.
			if (Panel)
			{
				Panel->ShowTab(0);
				Panel->Setup(nullptr, Window, nullptr, FSlateFontInfo()); // stops it following the tabs
				SetSecondTab(Window, false);
			}
			return;
		}
		UWidget* VanillaContent = nullptr;
		if (!Panel)
		{
			Panel = AddPanel(Self, Window, VanillaContent);
			if (!Panel)
			{
				UE_LOG(LogSinkingDepot, Warning, TEXT("Could not add the AWESOME Sink tab to the uploader panel"));
				return;
			}
		}
		FSlateFontInfo Font;
		if (const UTextBlock* FontSource = Cast<UTextBlock>(Self->WidgetTree->FindWidget(FontSourceName)))
		{
			Font = FontSource->GetFont();
		}
		Panel->Setup(Depot, Window, VanillaContent, Font);
		if (SetSecondTab(Window, true))
		{
			UE_LOG(LogSinkingDepot, Verbose, TEXT("AWESOME Sink tab added for %s"), *GetNameSafe(Depot));
		}
		else
		{
			UE_LOG(LogSinkingDepot, Warning, TEXT("The uploader window's tab list has an unexpected layout; the AWESOME Sink tab is not shown"));
		}
		Panel->ShowTab(0);
	}
}
}

void SinkingDepot::SinkTab::InstallHook()
{
#if !WITH_EDITOR
	if (IsRunningDedicatedServer())
	{
		return; // no UI on a dedicated server
	}
	// Not Native_Init: the shipping game inlines it into NativeConstruct, so a hook on it never runs for a panel that opens
	// normally. Both that path and the delayed one (NativeTestAndQueueInit) end in Init.
	SUBSCRIBE_METHOD_AFTER(UFGInteractWidget::Init, [](UFGInteractWidget* Self)
	{
		SinkingDepot::SinkTab::OnPanelInit(Self);
	});
	UE_LOG(LogSinkingDepot, Log, TEXT("Installed the AWESOME Sink tab hook"));
#endif
}

void SinkingDepot::SinkTab::OnPrintResult(const int32 Printed, const int32 StillAvailable, const bool bRefused)
{
	if (USinkingDepotSinkPanel* Panel = GPrintingPanel.Get())
	{
		Panel->ShowPrintResult(Printed, StillAvailable, bRefused);
	}
}

void USinkingDepotSinkPanel::Setup(AFGCentralStorageContainer* InDepot, UUserWidget* InWindow, UWidget* InVanillaContent, const FSlateFontInfo& InTemplateFont)
{
	Depot = InDepot;
	Window = InWindow;
	if (InVanillaContent)
	{
		VanillaContent = InVanillaContent;
		VanillaVisibility = InVanillaContent->GetVisibility();
	}
	if (InTemplateFont.HasValidFont())
	{
		TemplateFont = InTemplateFont;
		bHasTemplateFont = true;
		ApplyFont();
	}
	LastCoupons = -1;
	LastPoints[0] = LastPoints[1] = -1;
	LastProgress[0] = LastProgress[1] = -1.0f;
	StatusUntil = 0.0;
	if (StatusText)
	{
		StatusText->SetText(FText::GetEmpty());
	}
}

void USinkingDepotSinkPanel::ShowTab(const int32 Index)
{
	ActiveTab = Index;
	if (UWidget* Vanilla = VanillaContent.Get())
	{
		// Hidden, not collapsed: it keeps its size, so the window doesn't change shape between the tabs.
		Vanilla->SetVisibility(Index == 0 ? VanillaVisibility : ESlateVisibility::Hidden);
	}
	SetVisibility(Index == 1 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (Index == 1)
	{
		Refresh();
	}
}

void USinkingDepotSinkPanel::OnTabClicked(const int32 ButtonIndex)
{
	const AFGPlayerController* Controller = Cast<AFGPlayerController>(GetOwningPlayer());
	const bool bGamepad = Controller && Controller->GetIsUsingGamepad();
	UE_LOG(LogSinkingDepot, Verbose, TEXT("AWESOME Sink tab: the window reported tab button %d, gamepad %d"), ButtonIndex, bGamepad);
	if (!Depot.IsValid())
	{
		return;
	}
	// The gamepad's previous/next tab buttons always report 0 here, because the frame reads its index from a sliding-tabs
	// widget this panel doesn't have. With two tabs, previous and next both mean the other one.
	const int32 Tab = bGamepad && ButtonIndex == 0 ? 1 - ActiveTab : (ButtonIndex == 1 ? 1 : 0);
	ShowTab(Tab);
	const TArray<UWidget*> Buttons = SinkingDepot::SinkTab::TabButtons(Window.Get());
	if (Buttons.IsValidIndex(Tab))
	{
		// Moves the highlight; a mouse click has already done this, the gamepad buttons haven't.
		SinkingDepot::Reflection::Call(SinkingDepot::SinkTab::TabsContainer(Window.Get()), TEXT("SetActiveButton"),
			[&Buttons, Tab](UFunction* F, uint8* P) { SinkingDepot::Reflection::SetObject(F, P, TEXT("ActiveButton"), Buttons[Tab]); });
	}
}

void USinkingDepotSinkPanel::OnPrintClicked()
{
	AFGPlayerController* Controller = Cast<AFGPlayerController>(GetOwningPlayer());
	USinkingDepotRCO* Rco = Controller ? Controller->GetRemoteCallObjectOfClass<USinkingDepotRCO>() : nullptr;
	if (!Rco || !Depot.IsValid())
	{
		UE_LOG(LogSinkingDepot, Warning, TEXT("Print coupons: no remote call object (%d) or building (%d)"), Rco != nullptr, Depot.IsValid());
		return;
	}
	SinkingDepot::SinkTab::GPrintingPanel = this;
	if (PrintButton)
	{
		PrintButton->SetIsEnabled(false); // until the server answers or the next refresh
	}
	Rco->Server_PrintCoupons(Depot.Get());
}

void USinkingDepotSinkPanel::ShowPrintResult(const int32 Printed, const int32 StillAvailable, const bool bRefused)
{
	if (!StatusText)
	{
		return;
	}
	FText Message;
	if (bRefused)
	{
		Message = LOCTEXT("SinkTabRefused", "The server refused to print. Move closer to the building and try again.");
	}
	else if (Printed > 0)
	{
		Message = FText::Format(LOCTEXT("SinkTabPrinted", "Printed {0} {0}|plural(one=coupon,other=coupons) into your inventory."), FText::AsNumber(Printed));
	}
	else if (StillAvailable > 0)
	{
		Message = LOCTEXT("SinkTabNoRoom", "No room for coupons in your inventory.");
	}
	StatusText->SetText(Message);
	StatusUntil = FPlatformTime::Seconds() + 6.0;
	LastCoupons = -1; // re-enable the button on the next refresh
}

TSharedRef<SWidget> USinkingDepotSinkPanel::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void USinkingDepotSinkPanel::BuildTree()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// A dark backdrop like the uploader's own, so nothing of the hidden content area shows through.
	UImage* Backdrop = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetColorAndOpacity(FLinearColor(0.012f, 0.012f, 0.012f, 1.0f));
	if (UOverlaySlot* NewSlot = Root->AddChildToOverlay(Backdrop))
	{
		NewSlot->SetHorizontalAlignment(HAlign_Fill);
		NewSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	if (UOverlaySlot* NewSlot = Root->AddChildToOverlay(Column))
	{
		NewSlot->SetHorizontalAlignment(HAlign_Fill);
		NewSlot->SetVerticalAlignment(VAlign_Center);
		NewSlot->SetPadding(FMargin(20.0f, 16.0f));
	}
	auto Add = [Column](UWidget* Widget, const FMargin& CellPadding, const EHorizontalAlignment Align)
	{
		if (UVerticalBoxSlot* NewSlot = Column->AddChildToVerticalBox(Widget))
		{
			NewSlot->SetPadding(CellPadding);
			NewSlot->SetHorizontalAlignment(Align);
		}
	};

	// The AWESOME Sink's own track widgets. Standard keeps its defaults; DNA gets its colour, icon and label.
	if (UClass* TrackClass = SinkingDepot::SinkTab::TrackProgressClass())
	{
		StandardTrack = WidgetTree->ConstructWidget<UUserWidget>(TrackClass, TEXT("StandardTrack"));
		DnaTrack = WidgetTree->ConstructWidget<UUserWidget>(TrackClass, TEXT("DnaTrack"));
		Add(StandardTrack, FMargin(0.0f, 0.0f, 0.0f, 12.0f), HAlign_Fill);
		Add(DnaTrack, FMargin(0.0f, 0.0f, 0.0f, 24.0f), HAlign_Fill);
		static TWeakObjectPtr<UTexture2D> DnaIconCache;
		UTexture2D* DnaIcon = SinkingDepot::SinkTab::LoadCached(SinkingDepot::SinkTab::DnaIconPath, DnaIconCache);
		const FText DnaName = FText::FromStringTable(SinkingDepot::SinkTab::AwesomeTable, TEXT("AWESOMESink/PointsUntilNextCoupon/DNA"));
		namespace Reflection = SinkingDepot::Reflection;
		Reflection::Call(DnaTrack, TEXT("SetColor"), [](UFunction* F, uint8* P) { Reflection::SetColor(F, P, TEXT("mColor"), SinkingDepot::SinkTab::DnaColor); });
		if (DnaIcon)
		{
			Reflection::Call(DnaTrack, TEXT("SetIcon"), [DnaIcon](UFunction* F, uint8* P) { Reflection::SetObject(F, P, TEXT("mIcon"), DnaIcon); });
		}
		Reflection::Call(DnaTrack, TEXT("SetCurrentPointsName"), [&DnaName](UFunction* F, uint8* P) { Reflection::SetText(F, P, TEXT("mCurrentPointsName"), DnaName); });
	}
	else
	{
		UE_LOG(LogSinkingDepot, Warning, TEXT("Could not load %s; the AWESOME Sink tab shows no track progress"), SinkingDepot::SinkTab::TrackProgressClassPath);
	}

	CouponsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CouponsText"));
	CouponsText->SetJustification(ETextJustify::Center);
	Add(CouponsText, FMargin(0.0f, 0.0f, 0.0f, 10.0f), HAlign_Center);

	PrintButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("PrintButton"));
	FButtonStyle Style = PrintButton->GetStyle();
	const FLinearColor Orange = SinkingDepot::SinkTab::ButtonColor;
	Style.SetNormal(FSlateColorBrush(Orange));
	Style.SetHovered(FSlateColorBrush(Orange * 1.25f));
	Style.SetPressed(FSlateColorBrush(Orange * 0.75f));
	Style.SetDisabled(FSlateColorBrush(FLinearColor(0.08f, 0.08f, 0.08f, 1.0f)));
	Style.SetNormalPadding(FMargin(24.0f, 8.0f));
	Style.SetPressedPadding(FMargin(24.0f, 9.0f, 24.0f, 7.0f));
	PrintButton->SetStyle(Style);
	PrintButton->OnClicked.AddDynamic(this, &USinkingDepotSinkPanel::OnPrintClicked);
	PrintLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PrintLabel"));
	PrintLabel->SetText(FText::FromStringTable(SinkingDepot::SinkTab::AwesomeTable, TEXT("AWESOMESink/Coupons/Print/Button")));
	PrintLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	PrintLabel->SetJustification(ETextJustify::Center);
	PrintButton->AddChild(PrintLabel);
	Add(PrintButton, FMargin(0.0f, 0.0f, 0.0f, 8.0f), HAlign_Center);

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetJustification(ETextJustify::Center);
	StatusText->SetAutoWrapText(true);
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f)));
	Add(StatusText, FMargin(0.0f), HAlign_Fill);
	ApplyFont();
}

void USinkingDepotSinkPanel::ApplyFont()
{
	if (!bHasTemplateFont)
	{
		return; // keeps the text blocks' default font
	}
	for (UTextBlock* Text : {CouponsText.Get(), StatusText.Get(), PrintLabel.Get()})
	{
		if (Text)
		{
			FSlateFontInfo Font = TemplateFont;
			Font.Size = Text == StatusText.Get() ? 11 : 14;
			Text->SetFont(Font);
		}
	}
}

void USinkingDepotSinkPanel::NativeConstruct()
{
	Super::NativeConstruct();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(PollTimer, FTimerDelegate::CreateUObject(this, &USinkingDepotSinkPanel::Poll), 0.25f, true);
	}
}

void USinkingDepotSinkPanel::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
	Super::NativeDestruct();
}

void USinkingDepotSinkPanel::Poll()
{
	if (ActiveTab == 1 && Depot.IsValid())
	{
		Refresh();
	}
}

void USinkingDepotSinkPanel::Refresh()
{
	AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(this);
	if (!Sink)
	{
		return;
	}
	namespace Reflection = SinkingDepot::Reflection;
	const EResourceSinkTrack Tracks[2] = {EResourceSinkTrack::RST_Default, EResourceSinkTrack::RST_Exploration};
	UUserWidget* Widgets[2] = {StandardTrack.Get(), DnaTrack.Get()};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const int64 Points = Sink->GetNumPointsToNextCoupon(Tracks[Index]);
		const float Progress = Sink->GetProgressionTowardsNextCoupon(Tracks[Index]);
		if (!Widgets[Index] || (Points == LastPoints[Index] && FMath::IsNearlyEqual(Progress, LastProgress[Index])))
		{
			continue; // unchanged: don't retrigger the widget's change animation
		}
		LastPoints[Index] = Points;
		LastProgress[Index] = Progress;
		Reflection::Call(Widgets[Index], TEXT("SetPointsToNext"), [Points](UFunction* F, uint8* P) { Reflection::SetInt64(F, P, TEXT("mPointsToNext"), Points); });
		Reflection::Call(Widgets[Index], TEXT("SetProgression"), [Progress](UFunction* F, uint8* P)
		{
			Reflection::SetDouble(F, P, TEXT("mProgression"), Progress);
			Reflection::SetBool(F, P, TEXT("ForceProgressBarWrap"), false);
		});
	}

	const int32 Coupons = Sink->GetNumCoupons();
	if (Coupons != LastCoupons)
	{
		LastCoupons = Coupons;
		if (CouponsText)
		{
			CouponsText->SetText(FText::Format(LOCTEXT("SinkTabCoupons", "{0}: {1}"),
				FText::FromStringTable(SinkingDepot::SinkTab::AwesomeTable, TEXT("AWESOMESink/Coupons/Printable")), FText::AsNumber(Coupons)));
		}
		if (PrintButton)
		{
			PrintButton->SetIsEnabled(Coupons > 0);
		}
	}
	if (StatusText && StatusUntil > 0.0 && FPlatformTime::Seconds() > StatusUntil)
	{
		StatusUntil = 0.0;
		StatusText->SetText(FText::GetEmpty());
	}
}

#undef LOCTEXT_NAMESPACE
