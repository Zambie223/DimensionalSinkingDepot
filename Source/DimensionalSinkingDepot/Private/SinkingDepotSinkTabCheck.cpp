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
			Check(TEXT("2 next tab shows the AWESOME Sink tab"), Shown(Panel) && !Shown(VanillaContentOf(Window2)), FString::Printf(TEXT("sink panel shown %d, vanilla shown %d"), Shown(Panel), Shown(VanillaContentOf(Window2))));
			Shot(TEXT("2_sinktab"));
			Then(W, 1.0f, [](UWorld* W2)
			{
				UUserWidget* Window3 = WindowOf(OpenWidgetFor(W2, GOurs.Get()));
				PressTab(Window3, false);
				Then(W2, 1.0f, [](UWorld* W3)
				{
					UUserWidget* Window4 = WindowOf(OpenWidgetFor(W3, GOurs.Get()));
					Check(TEXT("2 previous tab goes back to the uploader"), Shown(VanillaContentOf(Window4)) && !Shown(PanelOf(Window4)), TEXT(""));
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
}
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

#endif
