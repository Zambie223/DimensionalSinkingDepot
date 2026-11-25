// Development-only self-check of the AWESOME Sink tab (-SinkingDepotSinkTabCheck), compiled only when
// SINKINGDEPOT_DEV_COMMANDS is 1. Single player / host. Writes "[sinktab] CHECK <name>: PASS|FAIL <detail>" lines, a
// "[sinktab] RESULT <passed>/<total>" line and screenshots SinkingDepotShowcase_sinktab_*.png (the last one, "done",
// is the marker the launcher script waits for).

#include "SinkingDepotDevCommands.h"

#if SINKINGDEPOT_DEV_COMMANDS

#include "SinkingDepotConstants.h"
#include "SinkingDepotLog.h"
#include "SinkingDepotOverflow.h"
#include "SinkingDepotReflection.h"
#include "SinkingDepotSinkTab.h"

#include "Blueprint/WidgetTree.h"
#include "Buildables/FGBuildable.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "FGPlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateUser.h"
#include "Layout/WidgetPath.h"
#include "FGBuildableSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGHUD.h"
#include "FGInventoryComponent.h"
#include "FGResourceSinkSubsystem.h"
#include "Resources/FGItemDescriptor.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

// Named namespace so a unity build merging this with the other dev files can't clash with their helpers.
namespace SinkingDepot::SinkTabCheck
{
namespace
{
	int32 GPassed = 0;
	int32 GTotal = 0;
	TWeakObjectPtr<AFGBuildable> GOurs;
	TWeakObjectPtr<AFGBuildable> GVanilla;
	constexpr int32 CouponsGiven = 7;

	void Check(const TCHAR* Name, const bool bPass, const FString& Detail)
	{
		++GTotal;
		GPassed += bPass ? 1 : 0;
		UE_LOG(LogSinkingDepot, Log, TEXT("[sinktab] CHECK %s: %s %s"), Name, bPass ? TEXT("PASS") : TEXT("FAIL"), *Detail);
	}

	void Shot(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("SinkingDepotShowcase_sinktab_%s.png"), Name), true, false);
	}

	void Then(UWorld* World, const float Delay, TFunction<void(UWorld*)> Function)
	{
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [WeakWorld, Function = MoveTemp(Function)]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Function(W);
			}
		}), Delay, false);
	}

	UClass* WidgetClassOf(const AFGBuildable* Buildable)
	{
		static const FSoftClassProperty* Property = FindFProperty<FSoftClassProperty>(AFGBuildable::StaticClass(), TEXT("mInteractWidgetSoftClass"));
		return (Property && Buildable) ? Cast<UClass>(Property->GetPropertyValue_InContainer(Buildable).LoadSynchronous()) : nullptr;
	}

	AFGHUD* Hud(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		return Controller ? Cast<AFGHUD>(Controller->GetHUD()) : nullptr;
	}

	UFGInteractWidget* OpenWidgetFor(UWorld* World, const AActor* Building)
	{
		AFGHUD* H = Hud(World);
		UFGGameUI* Ui = H ? H->GetGameUI() : nullptr;
		if (Ui)
		{
			for (UFGInteractWidget* Widget : Ui->GetInteractWidgetStack())
			{
				if (Widget && Widget->mInteractObject == Building)
				{
					return Widget;
				}
			}
		}
		return nullptr;
	}

	void Open(UWorld* World, AFGBuildable* Building)
	{
		if (AFGHUD* H = Hud(World); H && Building)
		{
			H->OpenInteractUI(WidgetClassOf(Building), Building);
		}
	}

	void Close(UWorld* World, const AActor* Building)
	{
		AFGHUD* H = Hud(World);
		UFGInteractWidget* Widget = OpenWidgetFor(World, Building);
		if (H && H->GetGameUI() && Widget)
		{
			H->GetGameUI()->PopWidget(Widget);
		}
	}

	UUserWidget* WindowOf(UFGInteractWidget* Widget)
	{
		return Widget && Widget->WidgetTree ? Cast<UUserWidget>(Widget->WidgetTree->FindWidget(TEXT("Widget_Window_DarkMode"))) : nullptr;
	}

	int32 TabCount(UUserWidget* Window)
	{
		FArrayProperty* Tabs = Window ? FindFProperty<FArrayProperty>(Window->GetClass(), TEXT("mTabs")) : nullptr;
		return Tabs ? FScriptArrayHelper(Tabs, Tabs->ContainerPtrToValuePtr<void>(Window)).Num() : -1;
	}

	USinkingDepotSinkPanel* PanelOf(UUserWidget* Window)
	{
		UOverlay* Overlay = Window ? Cast<UOverlay>(Window->GetContentForSlot(TEXT("WindowBody"))) : nullptr;
		if (Overlay)
		{
			for (UWidget* Child : Overlay->GetAllChildren())
			{
				if (USinkingDepotSinkPanel* Panel = Cast<USinkingDepotSinkPanel>(Child))
				{
					return Panel;
				}
			}
		}
		return nullptr;
	}

	UWidget* VanillaContentOf(UUserWidget* Window)
	{
		UOverlay* Overlay = Window ? Cast<UOverlay>(Window->GetContentForSlot(TEXT("WindowBody"))) : nullptr;
		return Overlay && Overlay->GetChildrenCount() > 0 ? Overlay->GetChildAt(0) : (Window ? Window->GetContentForSlot(TEXT("WindowBody")) : nullptr);
	}

	/** What the gamepad's next/previous tab buttons do (the frame's own handlers). */
	void PressTab(UUserWidget* Window, const bool bNext)
	{
		Reflection::Call(Window, bNext ? TEXT("HandleNextTab") : TEXT("HandlePreviousTab"));
	}

	bool Shown(const UWidget* Widget)
	{
		return Widget && Widget->GetVisibility() != ESlateVisibility::Collapsed && Widget->GetVisibility() != ESlateVisibility::Hidden;
	}

	UUserWidget* TabsContainerOf(UUserWidget* Window)
	{
		return Window && Window->WidgetTree ? Cast<UUserWidget>(Window->WidgetTree->FindWidget(TEXT("Widget_TabsContainer"))) : nullptr;
	}

	/** The tab buttons the frame really made (its container's canvas children). */
	TArray<UWidget*> TabButtons(UUserWidget* Window)
	{
		UUserWidget* Container = TabsContainerOf(Window);
		const UCanvasPanel* Canvas = Container && Container->WidgetTree ? Cast<UCanvasPanel>(Container->WidgetTree->FindWidget(TEXT("mContainer"))) : nullptr;
		return Canvas ? Canvas->GetAllChildren() : TArray<UWidget*>();
	}

	/** How many tab buttons a player could click: visible, with a size, and not on top of each other. */
	int32 UsableTabButtons(UUserWidget* Window)
	{
		TArray<FSlateRect> Rects;
		for (const UWidget* Button : TabButtons(Window))
		{
			const FGeometry& Geometry = Button->GetCachedGeometry();
			const FSlateRect Rect = Geometry.GetLayoutBoundingRect();
			bool bOverlaps = false;
			for (const FSlateRect& Other : Rects)
			{
				bOverlaps |= Rect.Left < Other.Right - 1.0f && Other.Left < Rect.Right - 1.0f;
			}
			if (Shown(Button) && Rect.Right - Rect.Left > 10.0f && !bOverlaps)
			{
				Rects.Add(Rect);
			}
		}
		return Rects.Num();
	}

	/**
	 * A mouse move from From to At, then (bClick) a left click at At. The move needs a real distance: the game only treats
	 * a move with a non-zero cursor delta as the player switching from gamepad to mouse, and a click that arrives while the
	 * game still thinks a gamepad is in use is spent on that switch instead (seen in game, check6).
	 */
	void SendMouse(const FVector2D& From, const FVector2D& At, const bool bClick)
	{
		FSlateApplication& Slate = FSlateApplication::Get();
		const FModifierKeysState NoModifiers;
		const TSet<FKey> NoButtons;
		const TSet<FKey> LeftDown = {EKeys::LeftMouseButton};
		// Slate refuses the mouse capture a button click needs while the application isn't active (FSlateApplication::
		// ProcessReply), and the off-screen test window never is, so that rule is lifted for just these events.
		const bool bOldInactiveInput = Slate.GetHandleDeviceInputWhenApplicationNotActive();
		Slate.SetHandleDeviceInputWhenApplicationNotActive(true);
		Slate.ProcessMouseMoveEvent(FPointerEvent(0, 0, At, From, NoButtons, EKeys::Invalid, 0.0f, NoModifiers));
		if (bClick)
		{
			Slate.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, 0, At, At, LeftDown, EKeys::LeftMouseButton, 0.0f, NoModifiers));
			Slate.ProcessMouseButtonUpEvent(FPointerEvent(0, 0, At, At, NoButtons, EKeys::LeftMouseButton, 0.0f, NoModifiers));
		}
		Slate.SetHandleDeviceInputWhenApplicationNotActive(bOldInactiveInput);
	}

	FVector2D Middle(const UWidget* Widget)
	{
		const FSlateRect Rect = Widget->GetCachedGeometry().GetLayoutBoundingRect();
		return FVector2D((Rect.Left + Rect.Right) * 0.5, (Rect.Top + Rect.Bottom) * 0.5);
	}

	bool UsingGamepad(UWorld* World);
	void DumpTabs(UWorld* World, const TCHAR* Label, const AActor* Building);

	/** UMG names for the Slate widgets of Root's tree, nested user widgets included (shipping has no reflection metadata). */
	void CollectNames(UUserWidget* Root, TMap<const SWidget*, FString>& Names, const int32 Depth = 0)
	{
		if (!Root || !Root->WidgetTree || Depth > 6)
		{
			return;
		}
		if (const TSharedPtr<SWidget> Own = Root->GetCachedWidget())
		{
			Names.Add(Own.Get(), Root->GetName());
		}
		Root->WidgetTree->ForEachWidget([&Names, Depth](UWidget* Widget)
		{
			if (const TSharedPtr<SWidget> Cached = Widget->GetCachedWidget())
			{
				Names.Add(Cached.Get(), Widget->GetName());
			}
			CollectNames(Cast<UUserWidget>(Widget), Names, Depth + 1);
		});
	}

	/** What Slate would hit at At (the innermost widgets), who holds the mouse, and who has focus. Diagnostics only. */
	void LogUnder(UWorld* World, const TCHAR* When, const FVector2D& At)
	{
		FSlateApplication& Slate = FSlateApplication::Get();
		TMap<const SWidget*, FString> Names;
		CollectNames(OpenWidgetFor(World, GOurs.Get()), Names);
		auto Describe = [&Names](const SWidget* Widget) -> FString
		{
			if (!Widget)
			{
				return TEXT("none");
			}
			const FString* Name = Names.Find(Widget);
			return FString::Printf(TEXT("%s%s%s"), *Widget->GetTypeAsString(), Name ? *FString::Printf(TEXT(" '%s'"), **Name) : TEXT(""),
				Widget->IsEnabled() ? TEXT("") : TEXT(" [disabled]"));
		};
		const FWidgetPath Path = Slate.LocateWindowUnderMouse(At, Slate.GetInteractiveTopLevelWindows(), true);
		FString Chain;
		for (int32 Index = FMath::Max(0, Path.Widgets.Num() - 8); Index < Path.Widgets.Num(); ++Index)
		{
			Chain += TEXT(" > ") + Describe(&Path.Widgets[Index].Widget.Get());
		}
		const TSharedPtr<FSlateUser> User = Slate.GetCursorUser();
		const TSharedPtr<SWidget> Captor = User ? User->GetCursorCaptor() : nullptr;
		const TSharedPtr<SWidget> Focus = Slate.GetUserFocusedWidget(0);
		UE_LOG(LogSinkingDepot, Log, TEXT("[sinktab]   %s, under %s: %d deep%s | mouse captor %s | focus %s | gamepad %d"), When, *At.ToString(), Path.Widgets.Num(), *Chain,
			*Describe(Captor.Get()), *Describe(Focus.Get()), UsingGamepad(World));
	}

	/**
	 * Like a player: the pointer moves onto the widget first, then, a moment later (the game may switch from gamepad to mouse
	 * layout on that move), it clicks the widget where it is drawn now. Find is asked again for the click.
	 */
	void ClickWidget(UWorld* World, TFunction<UWidget*(UWorld*)> Find)
	{
		UWidget* Widget = Find(World);
		if (!Widget || !FSlateApplication::IsInitialized())
		{
			UE_LOG(LogSinkingDepot, Log, TEXT("[sinktab] nothing to click (%s)"), *GetNameSafe(Widget));
			return;
		}
		const FVector2D First = Middle(Widget);
		LogUnder(World, TEXT("before the move"), First);
		SendMouse(First - FVector2D(30.0, 20.0), First, false); // the approach, from a little up and to the left
		Then(World, 0.3f, [Find = MoveTemp(Find), First](UWorld* W)
		{
			UWidget* Target = Find(W);
			if (!Target)
			{
				UE_LOG(LogSinkingDepot, Log, TEXT("[sinktab] the widget to click is gone"));
				return;
			}
			const FVector2D At = Middle(Target);
			LogUnder(W, TEXT("before the click"), At);
			SendMouse(First, At, true); // follows the widget if the switch to mouse layout moved it
			UE_LOG(LogSinkingDepot, Log, TEXT("[sinktab] clicked %s at %s (moved onto it at %s; application active %d)"), *GetNameSafe(Target), *At.ToString(), *First.ToString(), FSlateApplication::Get().IsActive());
		});
	}

	void ClickTab(UWorld* World, const int32 Index)
	{
		ClickWidget(World, [Index](UWorld* W) -> UWidget*
		{
			const TArray<UWidget*> Buttons = TabButtons(WindowOf(OpenWidgetFor(W, GOurs.Get())));
			return Buttons.IsValidIndex(Index) ? Buttons[Index] : nullptr;
		});
	}

	UWidget* PanelWidget(UWorld* World, const TCHAR* Name);

	void ClickPrint(UWorld* World)
	{
		ClickWidget(World, [](UWorld* W) { return PanelWidget(W, TEXT("PrintButton")); });
	}

	void PressKey(const FKey& Key)
	{
		if (FSlateApplication::IsInitialized())
		{
			constexpr uint32 LocalUser = 0; // the user-index constructor, like the dev commands' SlateKey
			const FModifierKeysState NoModifiers;
			FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, NoModifiers, LocalUser, false, 0, 0));
			FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, NoModifiers, LocalUser, false, 0, 0));
		}
	}

	bool UsingGamepad(UWorld* World)
	{
		const AFGPlayerController* Controller = Cast<AFGPlayerController>(World->GetFirstPlayerController());
		return Controller && Controller->GetIsUsingGamepad();
	}

	UWidget* PanelWidget(UWorld* World, const TCHAR* Name)
	{
		USinkingDepotSinkPanel* Panel = PanelOf(WindowOf(OpenWidgetFor(World, GOurs.Get())));
		return Panel && Panel->WidgetTree ? Panel->WidgetTree->FindWidget(FName(Name)) : nullptr;
	}

	FString StatusText(UWorld* World)
	{
		const UTextBlock* Status = Cast<UTextBlock>(PanelWidget(World, TEXT("StatusText")));
		return Status ? Status->GetText().ToString() : TEXT("(no status text)");
	}

	/** Which content shows, 0 the uploader's, 1 the sink panel, -1 neither or both. And which tab button is highlighted. */
	void CheckTab(UWorld* World, const TCHAR* Name, const int32 Expected)
	{
		UUserWidget* Window = WindowOf(OpenWidgetFor(World, GOurs.Get()));
		const bool bSink = Shown(PanelOf(Window));
		const bool bVanilla = Shown(VanillaContentOf(Window));
		const int32 Showing = bSink == bVanilla ? -1 : (bSink ? 1 : 0);
		int32 Highlighted = -1;
		const TArray<UWidget*> Buttons = TabButtons(Window);
		for (int32 Index = 0; Index < Buttons.Num(); ++Index)
		{
			const FBoolProperty* Active = FindFProperty<FBoolProperty>(Buttons[Index]->GetClass(), TEXT("IsActive"));
			if (Active && Active->GetPropertyValue_InContainer(Buttons[Index]))
			{
				Highlighted = Index;
			}
		}
		Check(Name, Showing == Expected && Highlighted == Expected, FString::Printf(TEXT("showing %d, highlighted %d, expected %d, gamepad %d"), Showing, Highlighted, Expected, UsingGamepad(World)));
	}

	int32 CouponsInInventory(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		const AFGCharacterPlayer* Player = Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
		AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(World);
		return (Player && Player->GetInventory() && Sink && Sink->GetCouponClass()) ? Player->GetInventory()->GetNumItems(Sink->GetCouponClass()) : -1;
	}

	void Finish(UWorld* World)
	{
		UE_LOG(LogSinkingDepot, Log, TEXT("[sinktab] RESULT %d/%d"), GPassed, GTotal);
		Then(World, 1.0f, [](UWorld*) { Shot(TEXT("done")); });
	}

	void Step5ReopenOurs(UWorld* World)
	{
		Open(World, GOurs.Get());
		Then(World, 3.0f, [](UWorld* W)
		{
			UUserWidget* Window = WindowOf(OpenWidgetFor(W, GOurs.Get()));
			Check(TEXT("5 reopened Sinking Depot has two tabs again"), TabCount(Window) == 2, FString::Printf(TEXT("tabs %d"), TabCount(Window)));
			Shot(TEXT("5_reopen"));
			Then(W, 2.0f, [](UWorld* W2) { Close(W2, GOurs.Get()); Then(W2, 2.0f, [](UWorld* W3) { Finish(W3); }); });
		});
	}

	void Step4Vanilla(UWorld* World)
	{
		Close(World, GOurs.Get());
		Then(World, 2.0f, [](UWorld* W)
		{
			Open(W, GVanilla.Get());
			Then(W, 3.0f, [](UWorld* W2)
			{
				UUserWidget* Window = WindowOf(OpenWidgetFor(W2, GVanilla.Get()));
				USinkingDepotSinkPanel* Panel = PanelOf(Window);
				Check(TEXT("4 vanilla uploader panel has one tab"), Window && TabCount(Window) == 1, FString::Printf(TEXT("window %d, tabs %d, reused panel with our overlay %d"), Window != nullptr, TabCount(Window), Panel != nullptr));
				Check(TEXT("4 vanilla uploader shows its own content"), Shown(VanillaContentOf(Window)) && !Shown(Panel), FString::Printf(TEXT("vanilla shown %d, sink panel shown %d"), Shown(VanillaContentOf(Window)), Shown(Panel)));
				Shot(TEXT("4_vanilla"));
				Then(W2, 2.0f, [](UWorld* W3) { Close(W3, GVanilla.Get()); Then(W3, 2.0f, Step5ReopenOurs); });
			});
		});
	}

	/** 600 coupons into an inventory with no free slot: only what tops up the coupon stack fits, the rest must stay available. */
	void Step3Full(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AFGCharacterPlayer* Player = Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
		AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(World);
		UClass* Plate = LoadClass<UFGItemDescriptor>(nullptr, TEXT("/Game/FactoryGame/Resource/Parts/IronPlate/Desc_IronPlate.Desc_IronPlate_C"));
		if (!Player || !Player->GetInventory() || !Sink || !Plate)
		{
			Check(TEXT("3 full inventory setup"), false, TEXT(""));
			Step4Vanilla(World);
			return;
		}
		Player->GetInventory()->AddStack(FInventoryStack(100000, TSubclassOf<UFGItemDescriptor>(Plate)), true);
		Sink->AddResourceSinkCoupons(600, false);
		const int32 Before = CouponsInInventory(World);
		Then(World, 1.0f, [Before](UWorld* W)
		{
			ClickPrint(W);
			Then(W, 2.0f, [Before](UWorld* W2)
			{
				AFGResourceSinkSubsystem* Sink2 = AFGResourceSinkSubsystem::Get(W2);
				const int32 Added = CouponsInInventory(W2) - Before;
				const int32 Left = Sink2->GetNumCoupons();
				Check(TEXT("3 full inventory: some printed, none lost"), Added > 0 && Left > 0 && Added + Left == 600, FString::Printf(TEXT("added %d, still available %d, status '%s'"), Added, Left, *StatusText(W2)));
				Shot(TEXT("3_full"));
				Then(W2, 1.0f, [Left](UWorld* W3)
				{
					const int32 Before3 = CouponsInInventory(W3);
					ClickPrint(W3);
					Then(W3, 2.0f, [Left, Before3](UWorld* W4)
					{
						AFGResourceSinkSubsystem* Sink4 = AFGResourceSinkSubsystem::Get(W4);
						Check(TEXT("3 no room at all: nothing printed, none lost, and it says so"), CouponsInInventory(W4) == Before3 && Sink4->GetNumCoupons() == Left && StatusText(W4).Contains(TEXT("No room")),
							FString::Printf(TEXT("inventory %d -> %d, available %d -> %d, status '%s'"), Before3, CouponsInInventory(W4), Left, Sink4->GetNumCoupons(), *StatusText(W4)));
						Step4Vanilla(W4);
					});
				});
			});
		});
	}

	void Step3Print(UWorld* World)
	{
		DumpTabs(World, TEXT("P3 after the click after gamepad use"), GOurs.Get());
		CheckTab(World, TEXT("3 the sink tab is showing before Print"), 1);
		const int32 Before = CouponsInInventory(World);
		ClickPrint(World);
		Then(World, 2.0f, [Before](UWorld* W)
		{
			AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(W);
			const int32 After = CouponsInInventory(W);
			const int32 Left = Sink ? Sink->GetNumCoupons() : -1;
			Check(TEXT("3 clicking Print puts every coupon in the inventory"), After - Before == CouponsGiven && Left == 0, FString::Printf(TEXT("inventory %d -> %d, still available %d"), Before, After, Left));
			Check(TEXT("3 status line reports the print"), StatusText(W).Contains(TEXT("7 coupons")), StatusText(W));
			const UWidget* Button = PanelWidget(W, TEXT("PrintButton"));
			Check(TEXT("3 Print is disabled with none left"), Button && !Button->GetIsEnabled(), TEXT(""));
			Shot(TEXT("3_printed"));
			Then(W, 1.0f, Step3Full);
		});
	}

	void Step2Tabs(UWorld* World)
	{
		DumpTabs(World, TEXT("C2 before the first tab click"), GOurs.Get());
		ClickTab(World, 1);
		Then(World, 1.0f, [](UWorld* W)
		{
			CheckTab(W, TEXT("2 clicking the second tab shows the AWESOME Sink tab"), 1);
			if (AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(W))
			{
				const float Standard = Sink->GetProgressionTowardsNextCoupon(EResourceSinkTrack::RST_Default);
				const float Dna = Sink->GetProgressionTowardsNextCoupon(EResourceSinkTrack::RST_Exploration);
				Check(TEXT("2 both tracks have progress to show (see screenshot 2_sinktab)"), Standard > 0.2f && Dna > 0.2f, FString::Printf(TEXT("standard %.2f, DNA %.2f"), Standard, Dna));
			}
			Shot(TEXT("2_sinktab"));
			Then(W, 1.0f, [](UWorld* W2)
			{
				ClickTab(W2, 0);
				Then(W2, 1.0f, [](UWorld* W3)
				{
					CheckTab(W3, TEXT("2 clicking the first tab goes back"), 0);
					ClickTab(W3, 0);
					Then(W3, 1.0f, [](UWorld* W4)
					{
						CheckTab(W4, TEXT("2 clicking the tab that's already open changes nothing"), 0);
						PressKey(EKeys::Gamepad_RightShoulder);
						Then(W4, 1.0f, [](UWorld* W5)
						{
							CheckTab(W5, TEXT("2 gamepad right bumper shows the AWESOME Sink tab"), 1);
							PressKey(EKeys::Gamepad_RightShoulder);
							Then(W5, 1.0f, [](UWorld* W5b)
							{
							CheckTab(W5b, TEXT("2 right bumper on the last tab changes nothing"), 1);
							PressKey(EKeys::Gamepad_LeftShoulder);
							Then(W5b, 1.0f, [](UWorld* W6)
							{
								CheckTab(W6, TEXT("2 gamepad left bumper goes back"), 0);
								PressKey(EKeys::Gamepad_LeftShoulder);
								Then(W6, 1.0f, [](UWorld* W7)
								{
									CheckTab(W7, TEXT("2 left bumper on the first tab changes nothing"), 0);
									DumpTabs(W7, TEXT("G7 after the bumpers"), GOurs.Get());
									ClickTab(W7, 1); // stay on the sink tab for the print steps (a click after gamepad use)
									Then(W7, 1.5f, Step3Print);
								});
							});
							});
						});
					});
				});
			});
		});
	}

	void Step1Open(UWorld* World)
	{
		Open(World, GOurs.Get());
		Then(World, 3.0f, [](UWorld* W)
		{
			UFGInteractWidget* Widget = OpenWidgetFor(W, GOurs.Get());
			UUserWidget* Window = WindowOf(Widget);
			USinkingDepotSinkPanel* Panel = PanelOf(Window);
			Check(TEXT("1 Sinking Depot panel opens"), Widget != nullptr, GetNameSafe(Widget));
			Check(TEXT("1 window has two tabs"), TabCount(Window) == 2, FString::Printf(TEXT("tabs %d"), TabCount(Window)));
			Check(TEXT("1 two tab buttons a player can click"), UsableTabButtons(Window) == 2, FString::Printf(TEXT("usable %d of %d"), UsableTabButtons(Window), TabButtons(Window).Num()));
			Check(TEXT("1 starts on the uploader tab"), Panel && !Shown(Panel) && Shown(VanillaContentOf(Window)), FString::Printf(TEXT("panel %d, panel shown %d, vanilla shown %d"), Panel != nullptr, Shown(Panel), Shown(VanillaContentOf(Window))));
			Shot(TEXT("1_open"));
			if (!Panel)
			{
				Finish(W); // nothing to drive
				return;
			}
			Then(W, 1.0f, Step2Tabs);
		});
	}

	AFGBuildable* Spawn(UWorld* World, const TCHAR* ClassPath, const FVector& Location, const float Yaw)
	{
		AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(World);
		UClass* Class = LoadClass<AFGBuildable>(nullptr, ClassPath);
		if (!Buildables || !Class)
		{
			return nullptr;
		}
		const FTransform Transform(FRotator(0.0f, Yaw, 0.0f), Location);
		AFGBuildable* Buildable = Buildables->BeginSpawnBuildable(Class, Transform);
		if (Buildable)
		{
			Buildable->FinishSpawning(Transform);
		}
		return Buildable;
	}

	/**
	 * Sinks items into both tracks, about 60 % of the way to the next coupon (never a whole coupon), so the panel's track
	 * bars show progress and colour. The item per track is the one with the most points that still fits.
	 */
	void AddTrackProgress(AFGResourceSinkSubsystem* Sink)
	{
		for (const EResourceSinkTrack Track : {EResourceSinkTrack::RST_Default, EResourceSinkTrack::RST_Exploration})
		{
			const int64 Target = Sink->GetNumPointsToNextCoupon(Track) * 6 / 10;
			UClass* Best = nullptr;
			int32 BestPoints = 0;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				int32 Points = 0;
				EResourceSinkTrack ItemTrack = EResourceSinkTrack::RST_Default;
				if (It->IsChildOf(UFGItemDescriptor::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract)
					&& Sink->FindResourceSinkPointsForItem(*It, Points, ItemTrack) && ItemTrack == Track && Points > BestPoints && Points <= Target)
				{
					Best = *It;
					BestPoints = Points;
				}
			}
			const int32 Count = Best ? static_cast<int32>(FMath::Min<int64>(Target / BestPoints, 1000)) : 0;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Sink->AddPoints_ThreadSafe(Best);
			}
			UE_LOG(LogSinkingDepot, Log, TEXT("[sinktab] track %d: sank %d x %s (%d points each) toward %lld points to the next coupon"),
				static_cast<int32>(Track), Count, *GetNameSafe(Best), BestPoints, Sink->GetNumPointsToNextCoupon(Track));
		}
	}

	void Start(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(World);
		if (!Pawn || !Sink)
		{
			Check(TEXT("0 setup"), false, FString::Printf(TEXT("pawn %d, sink subsystem %d"), Pawn != nullptr, Sink != nullptr));
			Finish(World);
			return;
		}
		const float Yaw = Controller->GetControlRotation().Yaw;
		const FVector Forward = FRotator(0.0f, Yaw, 0.0f).Vector();
		const FVector Right = FRotationMatrix(FRotator(0.0f, Yaw, 0.0f)).GetUnitAxis(EAxis::Y);
		const FVector Base = Pawn->GetActorLocation() - FVector(0, 0, 100.0f) + Forward * 1500.0f;
		GOurs = Spawn(World, BuildClassPath, Base - Right * 600.0f, Yaw + 180.0f);
		GVanilla = Spawn(World, VanillaUploaderClassPath, Base + Right * 600.0f, Yaw + 180.0f);
		AddTrackProgress(Sink);
		Sink->AddResourceSinkCoupons(CouponsGiven - Sink->GetNumCoupons(), false);
		LogSinkingDepot.SetVerbosity(ELogVerbosity::Verbose);
		Check(TEXT("0 setup"), GOurs.IsValid() && GVanilla.IsValid() && Sink->GetNumCoupons() == CouponsGiven,
			FString::Printf(TEXT("ours %s, vanilla %s, coupons available %d"), *GetNameSafe(GOurs.Get()), *GetNameSafe(GVanilla.Get()), Sink->GetNumCoupons()));
		Then(World, 3.0f, Step1Open);
	}

	// ---- Multiplayer: -SinkingDepotSinkTabServer (listen host or dedicated server) with -SinkingDepotSinkTabClient (a
	// joined client). The server spawns two Sinking Depots for the first remote player, one 15 m away and one 60 m away
	// (beyond the 30 m print range), and sets 7 coupons. The client prints through the real panel: at the far one it must
	// be refused, at the near one it must get all 7, and both sides check the coupons were counted once.

	constexpr float NetNear = 1500.0f;
	constexpr float NetFar = 6000.0f;
	TWeakObjectPtr<AFGCharacterPlayer> GNetPlayer;

	int32 CouponsOf(UWorld* World, const AFGCharacterPlayer* Player)
	{
		AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(World);
		return (Player && Player->GetInventory() && Sink && Sink->GetCouponClass()) ? Player->GetInventory()->GetNumItems(Sink->GetCouponClass()) : -1;
	}

	void NetServerWatch(UWorld* World, const int32 Polls)
	{
		AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(World);
		const int32 Inventory = CouponsOf(World, GNetPlayer.Get());
		if (Inventory <= 0 && Polls < 300)
		{
			Then(World, 2.0f, [Polls](UWorld* W) { NetServerWatch(W, Polls + 1); });
			return;
		}
		// The client checks again 10 s after its print; so does the server, in case anything arrives twice.
		Then(World, 12.0f, [](UWorld* W)
		{
			AFGResourceSinkSubsystem* Sink2 = AFGResourceSinkSubsystem::Get(W);
			const int32 Now = CouponsOf(W, GNetPlayer.Get());
			const int32 Left = Sink2 ? Sink2->GetNumCoupons() : -1;
			Check(TEXT("S1 the client's print reached its inventory, counted once"), Now == CouponsGiven && Left == 0,
				FString::Printf(TEXT("client inventory %d, still available %d (given %d)"), Now, Left, CouponsGiven));
			Finish(W);
		});
		UE_LOG(LogSinkingDepot, Log, TEXT("[sinktab] server: the client's inventory has %d coupon(s), %d still available"), Inventory, Sink ? Sink->GetNumCoupons() : -1);
	}

	void NetServerSpawn(UWorld* World)
	{
		AFGCharacterPlayer* Player = GNetPlayer.Get();
		AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(World);
		if (!Player || !Sink)
		{
			Check(TEXT("S0 setup"), false, FString::Printf(TEXT("client player %d, sink subsystem %d"), Player != nullptr, Sink != nullptr));
			Finish(World);
			return;
		}
		const float Yaw = Player->GetActorRotation().Yaw;
		const FVector Forward = FRotator(0.0f, Yaw, 0.0f).Vector();
		const FVector Base = Player->GetActorLocation() - FVector(0, 0, 100.0f);
		const AFGBuildable* Near = Spawn(World, BuildClassPath, Base + Forward * NetNear, Yaw + 180.0f);
		const AFGBuildable* Far = Spawn(World, BuildClassPath, Base + Forward * NetFar, Yaw + 180.0f);
		Sink->AddResourceSinkCoupons(CouponsGiven - Sink->GetNumCoupons(), false);
		Check(TEXT("S0 setup"), Near && Far && Sink->GetNumCoupons() == CouponsGiven && CouponsOf(World, Player) == 0,
			FString::Printf(TEXT("for %s: near %s, far %s, coupons available %d, in the client's inventory %d"), *GetNameSafe(Player), *GetNameSafe(Near), *GetNameSafe(Far),
				Sink->GetNumCoupons(), CouponsOf(World, Player)));
		NetServerWatch(World, 0);
	}

	void NetServerWaitForClient(UWorld* World, const int32 Polls)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (Controller && !Controller->IsLocalController() && Cast<AFGCharacterPlayer>(Controller->GetPawn()))
			{
				GNetPlayer = Cast<AFGCharacterPlayer>(Controller->GetPawn());
				UE_LOG(LogSinkingDepot, Log, TEXT("[sinktab] server: %s joined; spawning in 15 s"), *GetNameSafe(GNetPlayer.Get()));
				Then(World, 15.0f, NetServerSpawn); // let the new player land first
				return;
			}
		}
		if (Polls < 300)
		{
			Then(World, 3.0f, [Polls](UWorld* W) { NetServerWaitForClient(W, Polls + 1); });
			return;
		}
		Check(TEXT("S0 a client joined"), false, TEXT("none within 15 min"));
		Finish(World);
	}

	TWeakObjectPtr<AFGBuildable> GNetNear;
	TWeakObjectPtr<AFGBuildable> GNetFar;

	/** Opens Building's panel on this client and shows the sink tab with a real click; Next runs 1.5 s after the click. */
	void NetOpenSinkTab(UWorld* World, AFGBuildable* Building, const TCHAR* Which, TFunction<void(UWorld*)> Next)
	{
		GOurs = Building;
		Open(World, Building);
		Then(World, 3.0f, [Which, Next = MoveTemp(Next)](UWorld* W) mutable
		{
			UUserWidget* Window = WindowOf(OpenWidgetFor(W, GOurs.Get()));
			Check(*FString::Printf(TEXT("C1 the %s building's panel has two tabs on a client"), Which), TabCount(Window) == 2, FString::Printf(TEXT("tabs %d"), TabCount(Window)));
			ClickTab(W, 1);
			Then(W, 1.5f, [Which, Next = MoveTemp(Next)](UWorld* W2)
			{
				CheckTab(W2, *FString::Printf(TEXT("C1 clicking the second tab shows the sink tab (%s building)"), Which), 1);
				const UTextBlock* Coupons = Cast<UTextBlock>(PanelWidget(W2, TEXT("CouponsText")));
				const FString Text = Coupons ? Coupons->GetText().ToString() : TEXT("(no coupons text)");
				Check(*FString::Printf(TEXT("C1 the client sees the server's coupons (%s building)"), Which), Text.EndsWith(TEXT(" 7")), Text);
				Next(W2);
			});
		});
	}

	void NetClientNear(UWorld* World)
	{
		NetOpenSinkTab(World, GNetNear.Get(), TEXT("near"), [](UWorld* W)
		{
			ClickPrint(W);
			Then(W, 3.0f, [](UWorld* W2)
			{
				AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(W2);
				Check(TEXT("C3 Print on a client puts all 7 coupons in its inventory"), CouponsInInventory(W2) == CouponsGiven && Sink && Sink->GetNumCoupons() == 0,
					FString::Printf(TEXT("inventory %d, still available %d, status '%s'"), CouponsInInventory(W2), Sink ? Sink->GetNumCoupons() : -1, *StatusText(W2)));
				Check(TEXT("C3 the client's status line reports the print"), StatusText(W2).Contains(TEXT("7 coupons")), StatusText(W2));
				Shot(TEXT("client_printed"));
				Then(W2, 10.0f, [](UWorld* W3)
				{
					AFGResourceSinkSubsystem* Sink3 = AFGResourceSinkSubsystem::Get(W3);
					Check(TEXT("C4 10 s later still exactly 7 (counted once)"), CouponsInInventory(W3) == CouponsGiven && Sink3 && Sink3->GetNumCoupons() == 0,
						FString::Printf(TEXT("inventory %d, still available %d"), CouponsInInventory(W3), Sink3 ? Sink3->GetNumCoupons() : -1));
					Close(W3, GOurs.Get());
					Finish(W3);
				});
			});
		});
	}

	void NetClientFar(UWorld* World)
	{
		NetOpenSinkTab(World, GNetFar.Get(), TEXT("far"), [](UWorld* W)
		{
			ClickPrint(W);
			Then(W, 3.0f, [](UWorld* W2)
			{
				AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(W2);
				Check(TEXT("C2 Print at a building 60 m away is refused and prints nothing"),
					CouponsInInventory(W2) == 0 && Sink && Sink->GetNumCoupons() == CouponsGiven && StatusText(W2).Contains(TEXT("refused")),
					FString::Printf(TEXT("inventory %d, still available %d, status '%s'"), CouponsInInventory(W2), Sink ? Sink->GetNumCoupons() : -1, *StatusText(W2)));
				Shot(TEXT("client_refused"));
				Close(W2, GOurs.Get());
				Then(W2, 2.0f, NetClientNear);
			});
		});
	}

	void NetClientWait(UWorld* World, const int32 Polls)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(World);
		TArray<AFGBuildable*> Ours;
		for (TActorIterator<AFGBuildable> It(World); It; ++It)
		{
			if (IsSinkingDepot(*It))
			{
				Ours.Add(*It);
			}
		}
		if (!Pawn || !Sink || Ours.Num() < 2 || Sink->GetNumCoupons() != CouponsGiven)
		{
			if (Polls < 300)
			{
				Then(World, 3.0f, [Polls](UWorld* W) { NetClientWait(W, Polls + 1); });
				return;
			}
			Check(TEXT("C0 setup"), false, FString::Printf(TEXT("pawn %d, sink %d, Sinking Depots %d, coupons %d"), Pawn != nullptr, Sink != nullptr, Ours.Num(), Sink ? Sink->GetNumCoupons() : -1));
			Finish(World);
			return;
		}
		Ours.Sort([Pawn](const AFGBuildable& A, const AFGBuildable& B) { return FVector::Dist(A.GetActorLocation(), Pawn->GetActorLocation()) < FVector::Dist(B.GetActorLocation(), Pawn->GetActorLocation()); });
		GNetNear = Ours[0];
		GNetFar = Ours.Last();
		const double NearDist = FVector::Dist(Ours[0]->GetActorLocation(), Pawn->GetActorLocation());
		const double FarDist = FVector::Dist(Ours.Last()->GetActorLocation(), Pawn->GetActorLocation());
		LogSinkingDepot.SetVerbosity(ELogVerbosity::Verbose);
		Check(TEXT("C0 setup"), NearDist < 3000.0 && FarDist > 3000.0 && CouponsInInventory(World) == 0,
			FString::Printf(TEXT("near %s at %.0f, far %s at %.0f, coupons available %d, in the inventory %d"), *GetNameSafe(Ours[0]), NearDist, *GetNameSafe(Ours.Last()), FarDist,
				Sink->GetNumCoupons(), CouponsInInventory(World)));
		Then(World, 2.0f, NetClientFar);
	}
	// ---- Tab lab: tries the ways the frame might be made to draw and switch a second tab, logging the result of each.

	FString Export(UObject* Object, const TCHAR* Name)
	{
		const FProperty* Property = Object ? FindFProperty<FProperty>(Object->GetClass(), Name) : nullptr;
		FString Value(TEXT("(missing)"));
		if (Property)
		{
			Value.Reset();
			Property->ExportTextItem_Direct(Value, Property->ContainerPtrToValuePtr<void>(Object), nullptr, Object, PPF_None);
		}
		return Value.Left(160);
	}

	int32 ArrayNum(UObject* Object, const TCHAR* Name)
	{
		FArrayProperty* Array = Object ? FindFProperty<FArrayProperty>(Object->GetClass(), Name) : nullptr;
		return Array ? FScriptArrayHelper(Array, Array->ContainerPtrToValuePtr<void>(Object)).Num() : -1;
	}

	bool CallBoolOut(UObject* Object, const TCHAR* Function, const TCHAR* OutName)
	{
		bool bValue = false;
		Reflection::Call(Object, Function, [](UFunction*, uint8*) {}, [&bValue, OutName](UFunction* F, uint8* P)
		{
			if (const FBoolProperty* Property = CastField<FBoolProperty>(F->FindPropertyByName(FName(OutName))))
			{
				bValue = Property->GetPropertyValue_InContainer(P);
			}
		});
		return bValue;
	}

	int32 CallIntOut(UObject* Object, const TCHAR* Function, const TCHAR* OutName)
	{
		int32 Value = -1;
		Reflection::Call(Object, Function, [](UFunction*, uint8*) {}, [&Value, OutName](UFunction* F, uint8* P) { Value = Reflection::GetInt(F, P, OutName, -1); });
		return Value;
	}

	void SetBoolProperty(UObject* Object, const TCHAR* Name, const bool bValue)
	{
		if (const FBoolProperty* Property = Object ? FindFProperty<FBoolProperty>(Object->GetClass(), Name) : nullptr)
		{
			Property->SetPropertyValue_InContainer(Object, bValue);
		}
	}

	void DumpTabs(UWorld* World, const TCHAR* Label, const AActor* Building)
	{
		UUserWidget* Window = WindowOf(OpenWidgetFor(World, Building));
		UUserWidget* Container = TabsContainerOf(Window);
		USinkingDepotSinkPanel* Panel = PanelOf(Window);
		UE_LOG(LogSinkingDepot, Log, TEXT("[tablab] %s: window %d, mTabs %d, container %d, mButtons %d, sliding %s, active %d, canSwitch %d, visibleButtons %d, usable %d, overwrite %s/%s, sink panel shown %d"),
			Label, Window != nullptr, TabCount(Window), Container != nullptr, ArrayNum(Container, TEXT("mButtons")), *Export(Container, TEXT("mSlidingTabsWidget")).Right(60),
			CallIntOut(Window, TEXT("GetActiveTabIndex"), TEXT("ActiveTabIndex")), CallBoolOut(Container, TEXT("CanSwitchTabs"), TEXT("Out")),
			CallIntOut(Container, TEXT("NumberOfVisibleButtons"), TEXT("ReturnValue")), UsableTabButtons(Window),
			*Export(Window, TEXT("mOverwriteTabButtonFunctions")), *Export(Container, TEXT("mOverrideButtonFunctions")), Shown(Panel));
		if (Container)
		{
			const FSlateRect Rect = Container->GetCachedGeometry().GetLayoutBoundingRect();
			UE_LOG(LogSinkingDepot, Log, TEXT("[tablab]   container rect x %.0f..%.0f y %.0f..%.0f vis %d, hidden %s"), Rect.Left, Rect.Right, Rect.Top, Rect.Bottom, Shown(Container), *Export(Container, TEXT("mHiddenTabIndexes")));
		}
		for (UWidget* Button : TabButtons(Window))
		{
			const FSlateRect Rect = Button->GetCachedGeometry().GetLayoutBoundingRect();
			const UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Button->Slot);
			UE_LOG(LogSinkingDepot, Log, TEXT("[tablab]   button %s vis %d opacity %.2f rect x %.0f..%.0f y %.0f..%.0f | anchors %.2f..%.2f, offsets L%.0f T%.0f R%.0f B%.0f, autosize %d, z %d | title %s, target %s, active %s, sliding %s, override %s"),
				*GetNameSafe(Button), static_cast<int32>(Button->GetVisibility()), Button->GetRenderOpacity(), Rect.Left, Rect.Right, Rect.Top, Rect.Bottom,
				Slot ? Slot->GetAnchors().Minimum.X : -1.0, Slot ? Slot->GetAnchors().Maximum.X : -1.0, Slot ? Slot->GetOffsets().Left : -1.0f, Slot ? Slot->GetOffsets().Top : -1.0f,
				Slot ? Slot->GetOffsets().Right : -1.0f, Slot ? Slot->GetOffsets().Bottom : -1.0f, Slot && Slot->GetAutoSize(), Slot ? Slot->GetZOrder() : -1,
				*Export(Button, TEXT("mTitle")).Right(70), *Export(Button, TEXT("mTargetIndex")), *Export(Button, TEXT("IsActive")),
				*Export(Button, TEXT("mSlidingTabsWidget")).Right(40), *Export(Button, TEXT("mOverrideButtonFunction")));
		}
		Shot(*FString::Printf(TEXT("lab_%s"), *FString(Label).Left(2)));
	}

	using FLabStep = TFunction<void(UWorld*)>;

	void RunLab(UWorld* World, TSharedRef<TArray<TPair<float, FLabStep>>> Steps, const int32 Index)
	{
		if (!Steps->IsValidIndex(Index))
		{
			return;
		}
		Then(World, (*Steps)[Index].Key, [Steps, Index](UWorld* W)
		{
			(*Steps)[Index].Value(W);
			RunLab(W, Steps, Index + 1);
		});
	}

	void StartLab(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!Pawn)
		{
			UE_LOG(LogSinkingDepot, Log, TEXT("[tablab] done (no player)"));
			return;
		}
		LogSinkingDepot.SetVerbosity(ELogVerbosity::Verbose);
		const float Yaw = Controller->GetControlRotation().Yaw;
		const FVector Forward = FRotator(0.0f, Yaw, 0.0f).Vector();
		const FVector Base = Pawn->GetActorLocation() - FVector(0, 0, 100.0f) + Forward * 1500.0f;
		GOurs = Spawn(World, BuildClassPath, Base, Yaw + 180.0f);
		GVanilla = Spawn(World, TEXT("/Game/FactoryGame/Buildable/Factory/DroneStation/Build_DroneStation.Build_DroneStation_C"), Base + Forward * 4000.0f, Yaw + 180.0f);
		auto Win = [](UWorld* W) { return WindowOf(OpenWidgetFor(W, GOurs.Get())); };
		TSharedRef<TArray<TPair<float, FLabStep>>> Steps = MakeShared<TArray<TPair<float, FLabStep>>>();
		auto Do = [&Steps](const float Delay, FLabStep Step) { Steps->Emplace(Delay, MoveTemp(Step)); };
		Do(3.0f, [](UWorld* W) { Open(W, GOurs.Get()); });
		Do(3.0f, [](UWorld* W) { DumpTabs(W, TEXT("L0 as the hook leaves it"), GOurs.Get()); });
		Do(1.0f, [Win](UWorld* W) { Reflection::Call(TabsContainerOf(Win(W)), TEXT("GenerateButtons")); });
		Do(1.0f, [](UWorld* W) { DumpTabs(W, TEXT("L1 container GenerateButtons"), GOurs.Get()); });
		Do(1.0f, [Win](UWorld* W)
		{
			UUserWidget* Window = Win(W);
			UUserWidget* Container = TabsContainerOf(Window);
			FArrayProperty* Tabs = Window ? FindFProperty<FArrayProperty>(Window->GetClass(), TEXT("mTabs")) : nullptr;
			FArrayProperty* Buttons = Container ? FindFProperty<FArrayProperty>(Container->GetClass(), TEXT("mButtons")) : nullptr;
			const FStructProperty* From = Tabs ? CastField<FStructProperty>(Tabs->Inner) : nullptr;
			const FStructProperty* To = Buttons ? CastField<FStructProperty>(Buttons->Inner) : nullptr;
			if (From && To && From->Struct == To->Struct)
			{
				Buttons->CopyCompleteValue(Buttons->ContainerPtrToValuePtr<void>(Container), Tabs->ContainerPtrToValuePtr<void>(Window));
				Reflection::Call(Container, TEXT("GenerateButtons"));
			}
			else
			{
				UE_LOG(LogSinkingDepot, Log, TEXT("[tablab] L2 skipped: mTabs and mButtons hold different structs"));
			}
		});
		Do(1.0f, [](UWorld* W) { DumpTabs(W, TEXT("L2 mButtons = mTabs, GenerateButtons"), GOurs.Get()); });
		Do(1.0f, [Win](UWorld* W)
		{
			SetBoolProperty(Win(W), TEXT("mOverwriteTabButtonFunctions"), true);
			SetBoolProperty(TabsContainerOf(Win(W)), TEXT("mOverrideButtonFunctions"), true);
			Reflection::Call(Win(W), TEXT("InitTabs"));
		});
		Do(1.0f, [](UWorld* W) { DumpTabs(W, TEXT("L3 override flags, InitTabs"), GOurs.Get()); });
		Do(1.0f, [Win](UWorld* W)
		{
			UE_LOG(LogSinkingDepot, Log, TEXT("[tablab] Activate Next Tab changed: %d"), CallBoolOut(TabsContainerOf(Win(W)), TEXT("Activate Next Tab"), TEXT("DidChange")));
		});
		Do(1.0f, [](UWorld* W) { DumpTabs(W, TEXT("L4 Activate Next Tab"), GOurs.Get()); });
		Do(1.0f, [Win](UWorld* W)
		{
			const TArray<UWidget*> Buttons = TabButtons(Win(W));
			if (Buttons.IsValidIndex(1))
			{
				Reflection::Call(TabsContainerOf(Win(W)), TEXT("IfButtonClicked"), [&Buttons](UFunction* F, uint8* P) { Reflection::SetObject(F, P, TEXT("Instigator"), Buttons[1]); });
			}
		});
		Do(1.0f, [](UWorld* W) { DumpTabs(W, TEXT("L5 second button clicked"), GOurs.Get()); });
		Do(1.0f, [Win](UWorld* W) { PressTab(Win(W), true); });
		Do(1.0f, [](UWorld* W) { DumpTabs(W, TEXT("L6 HandleNextTab"), GOurs.Get()); });
		Do(1.0f, [](UWorld* W) { Close(W, GOurs.Get()); });
		// Reference: a vanilla panel whose tabs work.
		Do(2.0f, [](UWorld* W) { Open(W, GVanilla.Get()); });
		Do(3.0f, [](UWorld* W) { DumpTabs(W, TEXT("R0 drone port"), GVanilla.Get()); });
		Do(1.0f, [](UWorld* W) { PressTab(WindowOf(OpenWidgetFor(W, GVanilla.Get())), true); });
		Do(1.0f, [](UWorld* W) { DumpTabs(W, TEXT("R1 drone port HandleNextTab"), GVanilla.Get()); });
		Do(1.0f, [](UWorld* W) { Close(W, GVanilla.Get()); });
		Do(1.0f, [](UWorld* W) { Shot(TEXT("done")); UE_LOG(LogSinkingDepot, Log, TEXT("[tablab] done")); });
		RunLab(World, Steps, 0);
	}
}
}

void SinkingDepot::Dev::TabLab(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotTabLab: starts in 35 s"));
	SinkingDepot::SinkTabCheck::Then(World, 35.0f, [](UWorld* W) { SinkingDepot::SinkTabCheck::StartLab(W); });
}

void SinkingDepot::Dev::SinkTabNetServer(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Standalone || World->GetNetMode() == NM_Client)
	{
		return;
	}
	UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotSinkTabServer: waiting for a client"));
	SinkingDepot::SinkTabCheck::NetServerWaitForClient(World, 0);
}

void SinkingDepot::Dev::SinkTabNetClient(UWorld* World)
{
	if (!World || World->GetNetMode() != NM_Client)
	{
		return;
	}
	UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotSinkTabClient: waiting for the server's Sinking Depots"));
	SinkingDepot::SinkTabCheck::NetClientWait(World, 0);
}

void SinkingDepot::Dev::SinkTabCheck(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotSinkTabCheck: starts in 35 s"));
	SinkingDepot::SinkTabCheck::Then(World, 35.0f, [](UWorld* W) { SinkingDepot::SinkTabCheck::Start(W); });
}

#else

void SinkingDepot::Dev::SinkTabNetServer(UWorld* World)
{
}

void SinkingDepot::Dev::SinkTabNetClient(UWorld* World)
{
}

void SinkingDepot::Dev::SinkTabCheck(UWorld* World)
{
}

void SinkingDepot::Dev::TabLab(UWorld* World)
{
}

#endif
