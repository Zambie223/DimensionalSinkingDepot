// Development-only UI probe (-SinkingDepotUIProbe), compiled only when SINKINGDEPOT_DEV_COMMANDS is 1. It answers the
// questions the AWESOME Sink tab plan needs before any shipping code is written: how the uploader panel and the
// AWESOME Sink panel are laid out, how the shared window frame (Widget_Window_DarkMode) does tabs, and how the
// stations that already use tabs configure them. Everything goes to the log as "[uiprobe]" lines, plus screenshots.

#include "SinkingDepotDevCommands.h"

#if SINKINGDEPOT_DEV_COMMANDS

#include "SinkingDepotConstants.h"
#include "SinkingDepotLog.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Buildables/FGBuildable.h"
#include "Components/NamedSlotInterface.h"
#include "Components/PanelWidget.h"
#include "Engine/World.h"
#include "FGBuildableSubsystem.h"
#include "FGHUD.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

// Named namespace (not the global anonymous one) so a unity build that merges this file with the other dev file can't
// clash with its helpers of the same name.
namespace SinkingDepot::UIProbe
{
namespace
{
	int32 GLines = 0;
	constexpr int32 MaxLines = 12000;

	void Line(const FString& Text)
	{
		if (GLines++ < MaxLines)
		{
			UE_LOG(LogSinkingDepot, Log, TEXT("[uiprobe] %s"), *Text);
		}
	}

	void Later(UWorld* World, const float Delay, TFunction<void(UWorld*)> Function)
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

	FString Visibility(const UWidget* Widget)
	{
		return StaticEnum<ESlateVisibility>()->GetNameStringByValue(static_cast<int64>(Widget->GetVisibility()));
	}

	/** Blueprint-declared properties of Object (stops at the first native class), with their current values. */
	void DumpProperties(UObject* Object, const FString& Indent)
	{
		for (UClass* Class = Object->GetClass(); Class && !Class->HasAnyClassFlags(CLASS_Native); Class = Class->GetSuperClass())
		{
			for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				const FProperty* Property = *It;
				if (Property->GetName() == TEXT("UberGraphFrame"))
				{
					continue;
				}
				FString Value;
				Property->ExportTextItem_Direct(Value, Property->ContainerPtrToValuePtr<void>(Object), nullptr, Object, PPF_None);
				Line(FString::Printf(TEXT("%s  prop %s.%s : %s = %s"), *Indent, *Class->GetName(), *Property->GetName(), *Property->GetCPPType(), *Value.Left(800)));
			}
		}
	}

	/** Blueprint functions of Class and its Blueprint parents, with parameters. */
	void DumpFunctions(UClass* Class)
	{
		for (; Class && !Class->HasAnyClassFlags(CLASS_Native); Class = Class->GetSuperClass())
		{
			for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				const UFunction* Function = *It;
				if (Function->GetName().StartsWith(TEXT("ExecuteUbergraph")))
				{
					continue;
				}
				FString Params;
				for (TFieldIterator<FProperty> P(Function); P && P->HasAnyPropertyFlags(CPF_Parm); ++P)
				{
					Params += FString::Printf(TEXT("%s%s%s %s"), Params.IsEmpty() ? TEXT("") : TEXT(", "),
						P->HasAnyPropertyFlags(CPF_ReturnParm) ? TEXT("return ") : (P->HasAnyPropertyFlags(CPF_OutParm) ? TEXT("out ") : TEXT("")),
						*P->GetCPPType(), *P->GetName());
				}
				Line(FString::Printf(TEXT("  func %s::%s(%s) flags=0x%08x"), *Class->GetName(), *Function->GetName(), *Params, static_cast<uint32>(Function->FunctionFlags)));
			}
		}
	}

	/**
	 * One widget and everything under it: panel children, named-slot content and, for user widgets, their own tree.
	 * User widgets from the window/tab family also get their property values.
	 */
	void DumpWidget(UWidget* Widget, const int32 Depth, const bool bProperties)
	{
		if (!Widget || Depth > 30)
		{
			return;
		}
		const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
		Line(FString::Printf(TEXT("%s%s [%s] vis=%s"), *Indent, *Widget->GetName(), *GetNameSafe(Widget->GetClass()), *Visibility(Widget)));
		if (UUserWidget* User = Cast<UUserWidget>(Widget))
		{
			const FString ClassName = User->GetClass()->GetName();
			if (bProperties && (ClassName.Contains(TEXT("Window")) || ClassName.Contains(TEXT("Tab"))))
			{
				DumpProperties(User, Indent);
			}
		}
		if (INamedSlotInterface* Slots = Cast<INamedSlotInterface>(Widget))
		{
			TArray<FName> Names;
			Slots->GetSlotNames(Names);
			for (const FName& Name : Names)
			{
				if (UWidget* Content = Slots->GetContentForSlot(Name))
				{
					Line(FString::Printf(TEXT("%s  <slot %s>"), *Indent, *Name.ToString()));
					DumpWidget(Content, Depth + 2, bProperties);
				}
			}
		}
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				DumpWidget(Panel->GetChildAt(Index), Depth + 1, bProperties);
			}
		}
		if (UUserWidget* User = Cast<UUserWidget>(Widget); User && User->WidgetTree && Depth < 30)
		{
			DumpWidget(User->WidgetTree->RootWidget, Depth + 1, bProperties);
		}
	}

	/** The designer layout of a widget class (no instance needed), its parents, properties and functions. */
	void DumpWidgetClass(UClass* Class, const TCHAR* Why)
	{
		if (!Class)
		{
			return;
		}
		FString Chain;
		for (UClass* C = Class; C; C = C->GetSuperClass())
		{
			Chain += (Chain.IsEmpty() ? TEXT("") : TEXT(" < ")) + C->GetName();
			if (C->HasAnyClassFlags(CLASS_Native))
			{
				break;
			}
		}
		Line(FString::Printf(TEXT("=== class %s (%s): %s"), *Class->GetPathName(), Why, *Chain));
		DumpFunctions(Class);
		DumpProperties(Class->GetDefaultObject(), TEXT(""));
		if (UWidgetBlueprintGeneratedClass* Generated = Cast<UWidgetBlueprintGeneratedClass>(Class))
		{
			if (UWidgetTree* Tree = Generated->GetWidgetTreeArchetype())
			{
				Line(TEXT("  designer tree:"));
				DumpWidget(Tree->RootWidget, 2, true);
			}
		}
	}

	/**
	 * The building's interact widget class. AFGBuildable::GetInteractWidgetClass is protected, so this reads the
	 * mInteractWidgetSoftClass UPROPERTY through reflection (no access transformer needed for a dev probe).
	 */
	UClass* InteractWidgetClass(const AFGBuildable* Buildable)
	{
		static const FSoftClassProperty* Property = FindFProperty<FSoftClassProperty>(AFGBuildable::StaticClass(), TEXT("mInteractWidgetSoftClass"));
		if (!Property || !Buildable)
		{
			Line(FString::Printf(TEXT("interact widget lookup: property found %d, buildable %d"), Property != nullptr, Buildable != nullptr));
			return nullptr;
		}
		return Cast<UClass>(Property->GetPropertyValue_InContainer(Buildable).LoadSynchronous());
	}

	UClass* InteractWidgetOf(const TCHAR* ClassPath)
	{
		UClass* Build = LoadClass<AFGBuildable>(nullptr, ClassPath);
		UClass* Widget = InteractWidgetClass(Build ? Build->GetDefaultObject<AFGBuildable>() : nullptr);
		Line(FString::Printf(TEXT("interact widget of %s: %s"), ClassPath, Widget ? *Widget->GetPathName() : TEXT("(none)")));
		return Widget;
	}

	/** Part 1: class layouts only. Stations are included because they already use the window frame's tabs. */
	void StaticPart()
	{
		TSet<UClass*> Seen;
		const TCHAR* Buildings[] = {
			BuildClassPath,
			VanillaUploaderClassPath,
			ResourceSinkBuildClassPath,
			TEXT("/Game/FactoryGame/Buildable/Factory/TruckStation/Build_TruckStation.Build_TruckStation_C"),
			TEXT("/Game/FactoryGame/Buildable/Factory/Train/Station/Build_TrainStation.Build_TrainStation_C"),
			TEXT("/Game/FactoryGame/Buildable/Factory/Train/Station/Build_TrainDockingStation.Build_TrainDockingStation_C"),
			TEXT("/Game/FactoryGame/Buildable/Factory/DroneStation/Build_DroneStation.Build_DroneStation_C"),
		};
		for (const TCHAR* Path : Buildings)
		{
			if (UClass* Widget = InteractWidgetOf(Path); Widget && !Seen.Contains(Widget))
			{
				Seen.Add(Widget);
				DumpWidgetClass(Widget, Path);
			}
		}
		const TCHAR* Extra[] = {
			TEXT("/Game/FactoryGame/Interface/UI/Widget_Window_DarkMode.Widget_Window_DarkMode_C"),
			TEXT("/Game/FactoryGame/Buildable/Factory/ResourceSink/UI/BPW_ResourceSink_TrackProgress.BPW_ResourceSink_TrackProgress_C"),
			TEXT("/Game/FactoryGame/Buildable/Factory/ResourceSink/UI/BPW_ResourceSink_ScrollingCounter.BPW_ResourceSink_ScrollingCounter_C"),
			TEXT("/Game/FactoryGame/Buildable/Factory/ResourceSink/UI/BPW_ResourceSinkCoupon.BPW_ResourceSinkCoupon_C"),
		};
		for (const TCHAR* Path : Extra)
		{
			UClass* Class = LoadClass<UUserWidget>(nullptr, Path);
			if (!Class)
			{
				Line(FString::Printf(TEXT("could not load %s"), Path));
			}
			else if (!Seen.Contains(Class))
			{
				Seen.Add(Class);
				DumpWidgetClass(Class, TEXT("extra"));
			}
		}
		// The tab container's class is only known from the window frame's tree; dump any loaded class named like it.
		for (TObjectIterator<UWidgetBlueprintGeneratedClass> It; It; ++It)
		{
			const FString Name = It->GetName();
			if (!Seen.Contains(*It) && (Name.Contains(TEXT("TabsContainer")) || Name.StartsWith(TEXT("Widget_Tab")) || Name.StartsWith(TEXT("BPW_Tab"))) && !Name.StartsWith(TEXT("SKEL_")) && !Name.StartsWith(TEXT("REINST_")))
			{
				Seen.Add(*It);
				DumpWidgetClass(*It, TEXT("tab family"));
			}
		}
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

	UFGGameUI* GameUI(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AFGHUD* Hud = Controller ? Cast<AFGHUD>(Controller->GetHUD()) : nullptr;
		return Hud ? Hud->GetGameUI() : nullptr;
	}

	/** Opens Building's own panel the way using it does, dumps the live tree and takes a screenshot, then closes it. */
	void OpenDumpClose(UWorld* World, AFGBuildable* Building, const FString& Name, TFunction<void(UWorld*)> Then)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AFGHUD* Hud = Controller ? Cast<AFGHUD>(Controller->GetHUD()) : nullptr;
		if (!Hud || !Building)
		{
			Line(FString::Printf(TEXT("open %s: no HUD (%d) or building (%d)"), *Name, Hud != nullptr, Building != nullptr));
			Then(World);
			return;
		}
		Hud->OpenInteractUI(InteractWidgetClass(Building), Building);
		Later(World, 3.0f, [WeakBuilding = TWeakObjectPtr<AFGBuildable>(Building), Name, Then = MoveTemp(Then)](UWorld* W) mutable
		{
			UFGGameUI* Ui = GameUI(W);
			UFGInteractWidget* Open = nullptr;
			if (Ui)
			{
				for (UFGInteractWidget* Widget : Ui->GetInteractWidgetStack())
				{
					if (Widget && Widget->mInteractObject == WeakBuilding.Get())
					{
						Open = Widget;
					}
				}
			}
			Line(FString::Printf(TEXT("=== live %s: %s"), *Name, Open ? *Open->GetClass()->GetPathName() : TEXT("(not open)")));
			if (Open)
			{
				DumpWidget(Open, 1, true);
			}
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("SinkingDepotShowcase_uiprobe_%s.png"), *Name), true, false);
			Later(W, 2.0f, [Open = TWeakObjectPtr<UFGInteractWidget>(Open), Then = MoveTemp(Then)](UWorld* W2)
			{
				if (UFGGameUI* Ui2 = GameUI(W2); Ui2 && Open.IsValid())
				{
					Ui2->PopWidget(Open.Get());
				}
				Later(W2, 2.0f, Then);
			});
		});
	}

	void LivePart(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!Pawn)
		{
			Line(TEXT("no local player; live part skipped"));
			FScreenshotRequest::RequestScreenshot(TEXT("SinkingDepotShowcase_uiprobe_done.png"), true, false);
			return;
		}
		const float Yaw = Controller->GetControlRotation().Yaw;
		const FVector Forward = FRotator(0.0f, Yaw, 0.0f).Vector();
		const FVector Right = FRotationMatrix(FRotator(0.0f, Yaw, 0.0f)).GetUnitAxis(EAxis::Y);
		const FVector Base = Pawn->GetActorLocation() - FVector(0, 0, 100.0f) + Forward * 2500.0f;
		AFGBuildable* Ours = Spawn(World, BuildClassPath, Base - Right * 800.0f, Yaw + 180.0f);
		AFGBuildable* Sink = Spawn(World, ResourceSinkBuildClassPath, Base + Right * 1200.0f, Yaw + 180.0f);
		Line(FString::Printf(TEXT("spawned %s and %s"), *GetNameSafe(Ours), *GetNameSafe(Sink)));
		Later(World, 3.0f, [WeakOurs = TWeakObjectPtr<AFGBuildable>(Ours), WeakSink = TWeakObjectPtr<AFGBuildable>(Sink)](UWorld* W)
		{
			OpenDumpClose(W, WeakOurs.Get(), TEXT("depot"), [WeakSink](UWorld* W2)
			{
				OpenDumpClose(W2, WeakSink.Get(), TEXT("sink"), [](UWorld* W3)
				{
					Line(FString::Printf(TEXT("done, %d lines"), GLines));
					FScreenshotRequest::RequestScreenshot(TEXT("SinkingDepotShowcase_uiprobe_done.png"), true, false);
				});
			});
		});
	}
}
}

void SinkingDepot::Dev::UIProbe(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotUIProbe: starts in 35 s"));
	SinkingDepot::UIProbe::Later(World, 35.0f, [](UWorld* W)
	{
		SinkingDepot::UIProbe::StaticPart();
		SinkingDepot::UIProbe::LivePart(W);
	});
}

#else

void SinkingDepot::Dev::UIProbe(UWorld* World)
{
}

#endif
