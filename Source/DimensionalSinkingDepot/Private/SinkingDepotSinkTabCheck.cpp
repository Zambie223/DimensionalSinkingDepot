// Development-only self-check of the AWESOME Sink tab (-SinkingDepotSinkTabCheck), compiled only when
// SINKINGDEPOT_DEV_COMMANDS is 1. Single player / host. Writes "[sinktab] CHECK <name>: PASS|FAIL <detail>" lines, a
// "[sinktab] RESULT <passed>/<total>" line and screenshots SinkingDepotShowcase_sinktab_*.png (the last one, "done",
// is the marker the launcher script waits for).

#include "SinkingDepotDevCommands.h"

#if SINKINGDEPOT_DEV_COMMANDS

#include "SinkingDepotConstants.h"
#include "SinkingDepotLog.h"
#include "SinkingDepotReflection.h"
#include "SinkingDepotSinkTab.h"

#include "Blueprint/WidgetTree.h"
#include "Buildables/FGBuildable.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "FGBuildableSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGHUD.h"
#include "FGInventoryComponent.h"
#include "FGResourceSinkSubsystem.h"
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

	void Step3Print(UWorld* World)
	{
		UUserWidget* Window = WindowOf(OpenWidgetFor(World, GOurs.Get()));
		USinkingDepotSinkPanel* Panel = PanelOf(Window);
		const int32 Before = CouponsInInventory(World);
		Check(TEXT("3 the sink tab is showing when Print is pressed"), Shown(Panel), FString::Printf(TEXT("panel %d, shown %d"), Panel != nullptr, Shown(Panel)));
		if (Panel)
		{
			Panel->OnPrintClicked();
		}
		Then(World, 2.0f, [Before](UWorld* W)
		{
			AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(W);
			const int32 After = CouponsInInventory(W);
			const int32 Left = Sink ? Sink->GetNumCoupons() : -1;
			Check(TEXT("3 print puts every coupon in the inventory"), After - Before == CouponsGiven && Left == 0, FString::Printf(TEXT("inventory %d -> %d, still available %d"), Before, After, Left));
			USinkingDepotSinkPanel* P = PanelOf(WindowOf(OpenWidgetFor(W, GOurs.Get())));
			const UTextBlock* Status = P && P->WidgetTree ? Cast<UTextBlock>(P->WidgetTree->FindWidget(TEXT("StatusText"))) : nullptr;
			Check(TEXT("3 status line reports the print"), Status && !Status->GetText().IsEmpty(), Status ? Status->GetText().ToString() : TEXT("(no status text)"));
			Shot(TEXT("3_printed"));
			Then(W, 2.0f, Step4Vanilla);
		});
	}

	void Step2Tabs(UWorld* World)
	{
		UUserWidget* Window = WindowOf(OpenWidgetFor(World, GOurs.Get()));
		PressTab(Window, true);
		Then(World, 1.0f, [](UWorld* W)
		{
			UUserWidget* Window2 = WindowOf(OpenWidgetFor(W, GOurs.Get()));
			USinkingDepotSinkPanel* Panel = PanelOf(Window2);
			const bool bSinkTabShown = Shown(Panel);
			Check(TEXT("2 next tab shows the AWESOME Sink tab"), Shown(Panel) && !Shown(VanillaContentOf(Window2)), FString::Printf(TEXT("sink panel shown %d, vanilla shown %d"), Shown(Panel), Shown(VanillaContentOf(Window2))));
			Shot(TEXT("2_sinktab"));
			Then(W, 1.0f, [bSinkTabShown](UWorld* W2)
			{
				UUserWidget* Window3 = WindowOf(OpenWidgetFor(W2, GOurs.Get()));
				PressTab(Window3, false);
				Then(W2, 1.0f, [bSinkTabShown](UWorld* W3)
				{
					UUserWidget* Window4 = WindowOf(OpenWidgetFor(W3, GOurs.Get()));
					Check(TEXT("2 previous tab goes back to the uploader"), bSinkTabShown && Shown(VanillaContentOf(Window4)) && !Shown(PanelOf(Window4)), bSinkTabShown ? TEXT("") : TEXT("(the sink tab never showed, so going back proves nothing)"));
					PressTab(Window4, true); // stay on the sink tab for the print step
					Then(W3, 1.5f, Step3Print);
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
		Sink->AddResourceSinkCoupons(CouponsGiven - Sink->GetNumCoupons(), false);
		Check(TEXT("0 setup"), GOurs.IsValid() && GVanilla.IsValid() && Sink->GetNumCoupons() == CouponsGiven,
			FString::Printf(TEXT("ours %s, vanilla %s, coupons available %d"), *GetNameSafe(GOurs.Get()), *GetNameSafe(GVanilla.Get()), Sink->GetNumCoupons()));
		Then(World, 3.0f, Step1Open);
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

void SinkingDepot::Dev::SinkTabCheck(UWorld* World)
{
}

void SinkingDepot::Dev::TabLab(UWorld* World)
{
}

#endif
