// Development-only console commands, compiled only when SINKINGDEPOT_DEV_COMMANDS is 1 (set in
// DimensionalSinkingDepot.Build.cs). They set up test situations and tune the look. Set the define to 0 for release
// builds; nothing else depends on this file.

#include "SinkingDepotDevCommands.h"

#if SINKINGDEPOT_DEV_COMMANDS

#include "SinkingDepotConstants.h"
#include "SinkingDepotLog.h"
#include "SinkingDepotOverflow.h"
#include "SinkingDepotSubsystem.h"

#include "Buildables/FGCentralStorageContainer.h"
#include "Buildables/FGBuildableStorage.h"
#include "Camera/CameraActor.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FGBuildableSubsystem.h"
#include "FGCentralStorageSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGCharacterPlayer.h"
#include "FGColoredInstanceMeshProxy.h"
#include "FGConstructDisqualifier.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"
#include "UObject/UObjectIterator.h"
#include "FGFactoryColoringTypes.h"
#include "Hologram/FGHologram.h"
#include "Hologram/FGSplineHologram.h"
#include "Buildables/FGBuildablePowerPole.h"
#include "Buildables/FGBuildableWire.h"
#include "FGCircuitSubsystem.h"
#include "FGPowerCircuit.h"
#include "FGPowerConnectionComponent.h"
#include "FGPowerInfoComponent.h"
#include "FGFactoryConnectionComponent.h"
#include "FGInventoryComponent.h"
#include "FGRecipe.h"
#include "FGResourceSinkSubsystem.h"
#include "FGSchematic.h"
#include "FGSchematicManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Resources/FGItemDescriptor.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FGBuildEffectActor.h"
#include "FGGameState.h"
#include "FGRecipeManager.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TextureResource.h"
#include "Async/Async.h"
#include "SinkingDepotConvert.h"
#include "SinkingDepotConvertTypes.h"
#include "Buildables/FGBuildableBlueprintDesigner.h"
#include "FGBlueprintSubsystem.h"
#include "FGFactoryBlueprintTypes.h"
#include "FGPlayerController.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "GameFramework/PlayerStart.h"
#include "Player/SMLRemoteCallObject.h"

namespace
{
	constexpr const TCHAR* BuildClassPath = TEXT("/DimensionalSinkingDepot/Buildable/Build_SinkingDepot.Build_SinkingDepot_C");
	constexpr const TCHAR* RecipeClassPath = TEXT("/DimensionalSinkingDepot/Buildable/Recipe_SinkingDepot.Recipe_SinkingDepot_C");

	/** Logs to the output device and to LogSinkingDepot, so results also land in the server log. */
	void Report(FOutputDevice& Ar, const FString& Line)
	{
		Ar.Log(Line);
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] %s"), *Line);
	}

	bool IsServer(const UWorld* World, FOutputDevice& Ar)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			Ar.Log(TEXT("Server, host or single player only."));
			return false;
		}
		return true;
	}

	UClass* ResolveItemClass(const FString& Arg, FOutputDevice& Ar)
	{
		UClass* Class = Arg.Contains(TEXT("/"))
			? LoadClass<UFGItemDescriptor>(nullptr, *Arg)
			: FindFirstObject<UClass>(*Arg, EFindFirstObjectOptions::None, ELogVerbosity::Warning, TEXT("SinkingDepot dev command"));
		if (!Class || !Class->IsChildOf(UFGItemDescriptor::StaticClass()))
		{
			Ar.Logf(TEXT("'%s' is not a loaded item descriptor class (try Desc_IronPlate_C, or a full /Game/... path)."), *Arg);
			return nullptr;
		}
		return Class;
	}

	UFGFactoryConnectionComponent* FindInput(AActor* Actor)
	{
		TInlineComponentArray<UFGFactoryConnectionComponent*> Connections(Actor);
		for (UFGFactoryConnectionComponent* Connection : Connections)
		{
			if (Connection && Connection->GetDirection() == EFactoryConnectionDirection::FCD_INPUT)
			{
				return Connection;
			}
		}
		return nullptr;
	}

	/** Uploaders sorted by name, so repeated runs pick the same ones. */
	TArray<AFGCentralStorageContainer*> GetUploaders(UWorld* World, const bool bSinkingDepots)
	{
		TArray<AFGCentralStorageContainer*> Result;
		for (TActorIterator<AFGCentralStorageContainer> It(World); It; ++It)
		{
			if (It->HasActorBegunPlay() && !It->IsActorBeingDestroyed() && SinkingDepot::IsSinkingDepot(*It) == bSinkingDepots)
			{
				Result.Add(*It);
			}
		}
		Result.Sort([](const AFGCentralStorageContainer& A, const AFGCentralStorageContainer& B) { return A.GetName() < B.GetName(); });
		return Result;
	}

	/** Logs an actor's components, their materials, and every vector parameter with its current value. */
	void DumpMaterials(AActor* Actor, FOutputDevice& Ar)
	{
		Report(Ar, FString::Printf(TEXT("Materials of %s (%s)"), *Actor->GetName(), *Actor->GetClass()->GetPathName()));
		TInlineComponentArray<UActorComponent*> Components(Actor);
		for (UActorComponent* Component : Components)
		{
			UMeshComponent* Mesh = Cast<UMeshComponent>(Component);
			Report(Ar, FString::Printf(TEXT("  %s [%s]%s"), *Component->GetName(), *Component->GetClass()->GetName(),
				Mesh ? *FString::Printf(TEXT(" visible=%d"), Mesh->IsVisible()) : TEXT("")));
			if (!Mesh)
			{
				continue;
			}
			for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
			{
				const UMaterialInterface* Material = Mesh->GetMaterial(Slot);
				if (!Material)
				{
					continue;
				}
				TArray<FMaterialParameterInfo> Infos;
				TArray<FGuid> Ids;
				Material->GetAllVectorParameterInfo(Infos, Ids);
				FString Params;
				for (const FMaterialParameterInfo& Info : Infos)
				{
					FLinearColor Value;
					Material->GetVectorParameterValue(FHashedMaterialParameterInfo(Info), Value);
					Params += FString::Printf(TEXT(" '%s'=(%.2f,%.2f,%.2f,%.2f)"), *Info.Name.ToString(), Value.R, Value.G, Value.B, Value.A);
				}
				Report(Ar, FString::Printf(TEXT("    slot %d: %s [%s]%s"), Slot, *Material->GetPathName(), *Material->GetClass()->GetName(), *Params));
			}
		}
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GMaterialsCommand(
		TEXT("SinkingDepot.Dev.Materials"),
		TEXT("SinkingDepot.Dev.Materials [vanilla] - logs the mesh components, materials and vector parameters of one uploader (development aid)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			const bool bVanilla = Args.Num() > 0 && Args[0].Equals(TEXT("vanilla"), ESearchCase::IgnoreCase);
			const TArray<AFGCentralStorageContainer*> Uploaders = World ? GetUploaders(World, !bVanilla) : TArray<AFGCentralStorageContainer*>();
			if (Uploaders.Num() == 0)
			{
				Ar.Log(TEXT("No such uploader in this world."));
				return;
			}
			DumpMaterials(Uploaders[0], Ar);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GCountCommand(
		TEXT("SinkingDepot.Dev.Count"),
		TEXT("SinkingDepot.Dev.Count <Item> - shows the depot amount and limit for an item and whether it can be sunk (development aid)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (Args.Num() != 1 || !IsServer(World, Ar))
			{
				Ar.Log(TEXT("Usage: SinkingDepot.Dev.Count <Item>"));
				return;
			}
			UClass* ItemClass = ResolveItemClass(Args[0], Ar);
			AFGCentralStorageSubsystem* Depot = AFGCentralStorageSubsystem::Get(World);
			AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(World);
			if (!ItemClass || !Depot || !Sink)
			{
				return;
			}
			const TSubclassOf<UFGItemDescriptor> Item(ItemClass);
			int32 Points = 0;
			EResourceSinkTrack Track = EResourceSinkTrack::RST_Default;
			const bool bFound = Sink->FindResourceSinkPointsForItem(Item, Points, Track);
			Report(Ar, FString::Printf(TEXT("Count %s: depot %d of limit %d; discardable=%d, sink points=%d (%s, track %d); total default-track points %lld"),
				*ItemClass->GetName(), Depot->GetNumItemsFromCentralStorage(Item), Depot->GetCentralStorageItemLimit(Item),
				UFGItemDescriptor::CanBeDiscarded(Item), Points, bFound ? TEXT("listed") : TEXT("not listed"), static_cast<int32>(Track),
				Sink->GetNumTotalPoints(EResourceSinkTrack::RST_Default)));
		}));

	/** Uploads up to Wanted items straight into the depot through a throwaway one-slot inventory. Returns how many went in. */
	int32 FillDepot(UWorld* World, const TSubclassOf<UFGItemDescriptor> Item, const int32 Wanted)
	{
		AFGCentralStorageSubsystem* Depot = AFGCentralStorageSubsystem::Get(World);
		ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World);
		if (!Depot || !Subsystem || !Item || Wanted <= 0)
		{
			return 0;
		}
		// The depot only accepts uploads from an inventory; the subsystem actor owns this one so it has authority.
		UFGInventoryComponent* Temp = NewObject<UFGInventoryComponent>(Subsystem, NAME_None, RF_Transient);
		Temp->RegisterComponent();
		Temp->Resize(1);
		int32 Uploaded = 0;
		while (Uploaded < Wanted)
		{
			if (Temp->IsIndexEmpty(0) && Temp->AddStack(FInventoryStack(FMath::Min(Wanted - Uploaded, UFGItemDescriptor::GetStackSize(Item)), Item), true) <= 0)
			{
				break;
			}
			if (!Depot->UploadItemFromInventoryToCentralStorage(Temp, 0))
			{
				break;
			}
			++Uploaded;
		}
		Temp->Empty();
		Temp->DestroyComponent();
		return Uploaded;
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GFillDepotCommand(
		TEXT("SinkingDepot.Dev.FillDepot"),
		TEXT("SinkingDepot.Dev.FillDepot <Item> [Count] - uploads items straight into the depot, up to its limit by default (development aid)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (Args.Num() < 1 || Args.Num() > 2 || (Args.Num() == 2 && !Args[1].IsNumeric()) || !IsServer(World, Ar))
			{
				Ar.Log(TEXT("Usage: SinkingDepot.Dev.FillDepot <Item> [Count]"));
				return;
			}
			UClass* ItemClass = ResolveItemClass(Args[0], Ar);
			AFGCentralStorageSubsystem* Depot = AFGCentralStorageSubsystem::Get(World);
			ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World);
			if (!ItemClass || !Depot || !Subsystem)
			{
				return;
			}
			const TSubclassOf<UFGItemDescriptor> Item(ItemClass);
			const int32 Space = Depot->GetCentralStorageItemLimit(Item) - Depot->GetNumItemsFromCentralStorage(Item);
			const int32 Wanted = Args.Num() == 2 ? FMath::Min(FCString::Atoi(*Args[1]), Space) : Space;
			if (Wanted <= 0)
			{
				Report(Ar, FString::Printf(TEXT("FillDepot %s: depot already full (%d)"), *ItemClass->GetName(), Depot->GetNumItemsFromCentralStorage(Item)));
				return;
			}
			const int32 Uploaded = FillDepot(World, Item, Wanted);
			Report(Ar, FString::Printf(TEXT("FillDepot %s: uploaded %d; depot now %d of %d"), *ItemClass->GetName(), Uploaded,
				Depot->GetNumItemsFromCentralStorage(Item), Depot->GetCentralStorageItemLimit(Item)));
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GFeedCommand(
		TEXT("SinkingDepot.Dev.Feed"),
		TEXT("SinkingDepot.Dev.Feed <Item> <Count> - puts items into every Dimensional Sinking Depot's own inventory (development aid)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (Args.Num() != 2 || !Args[1].IsNumeric() || !IsServer(World, Ar))
			{
				Ar.Log(TEXT("Usage: SinkingDepot.Dev.Feed <Item> <Count>"));
				return;
			}
			UClass* ItemClass = ResolveItemClass(Args[0], Ar);
			if (!ItemClass)
			{
				return;
			}
			for (AFGCentralStorageContainer* Uploader : GetUploaders(World, true))
			{
				UFGInventoryComponent* Inventory = Uploader->GetStorageInventory();
				const int32 Added = Inventory ? Inventory->AddStack(FInventoryStack(FCString::Atoi(*Args[1]), TSubclassOf<UFGItemDescriptor>(ItemClass)), true) : 0;
				Report(Ar, FString::Printf(TEXT("Feed %s: added %d %s (inventory size %d)"), *Uploader->GetName(), Added, *ItemClass->GetName(),
					Inventory ? Inventory->GetSizeLinear() : 0));
			}
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GGrantResearchCommand(
		TEXT("SinkingDepot.Dev.GrantResearch"),
		TEXT("SinkingDepot.Dev.GrantResearch - completes the vanilla Dimensional Depot research, to test the automatic unlock (development aid)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			AFGSchematicManager* Schematics = IsServer(World, Ar) ? AFGSchematicManager::Get(World) : nullptr;
			const ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World);
			if (!Schematics || !Subsystem || !Subsystem->GetDepotResearch())
			{
				Ar.Log(TEXT("Schematic manager, subsystem or research class missing."));
				return;
			}
			Schematics->GiveAccessToSchematic(Subsystem->GetDepotResearch(), nullptr, ESchematicUnlockFlags::BlockTelemetry);
			Report(Ar, TEXT("Granted the Dimensional Depot research; the Sinking Depot should unlock on the next tick (see SinkingDepot.Status)."));
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GPowerCommand(
		TEXT("SinkingDepot.Dev.Power"),
		TEXT("SinkingDepot.Dev.Power - lists each Dimensional Sinking Depot's wiring, circuit and power state (development aid)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (!IsServer(World, Ar))
			{
				return;
			}
			AFGCircuitSubsystem* Circuits = AFGCircuitSubsystem::Get(World);
			for (AFGCentralStorageContainer* Depot : GetUploaders(World, true))
			{
				const UFGPowerConnectionComponent* Connection = Depot->FindComponentByClass<UFGPowerConnectionComponent>();
				const UFGPowerInfoComponent* Info = Depot->GetPowerInfo();
				UFGPowerCircuit* Circuit = (Connection && Circuits) ? Circuits->FindCircuit<UFGPowerCircuit>(Connection->GetCircuitID()) : nullptr;
				FPowerCircuitStats Stats;
				if (Circuit)
				{
					Circuit->GetStats(Stats);
				}
				Report(Ar, FString::Printf(TEXT("Power %s: connector=%s wires=%d linkedInfo=%s circuit=%d(%s) circuitHasPower=%d fuse=%d produced=%.1f capacity=%.1f consumed=%.1f | info HasPower=%d target=%.1f actual=%.1f | grid=%d blocked=%d"),
					*Depot->GetName(), Connection ? TEXT("yes") : TEXT("NO"), Connection ? Connection->GetNumConnections() : 0,
					Connection && Connection->GetPowerInfo() ? (Connection->GetPowerInfo() == Info ? TEXT("own") : TEXT("OTHER")) : TEXT("none"),
					Connection ? Connection->GetCircuitID() : -1, Circuit ? TEXT("found") : TEXT("missing"),
					Circuit ? Circuit->HasPower() : 0, Circuit ? Circuit->IsFuseTriggered() : 0,
					Stats.PowerProduced, Stats.PowerProductionCapacity, Stats.PowerConsumed,
					Info ? Info->HasPower() : 0, Info ? Info->GetTargetConsumption() : 0.0f, Info ? Info->GetActualConsumption() : 0.0f,
					SinkingDepot::Overflow::HasGridPower(Depot), SinkingDepot::Overflow::IsBlockedByPower(Depot)));
			}
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GWireCommand(
		TEXT("SinkingDepot.Dev.WireToPole"),
		TEXT("SinkingDepot.Dev.WireToPole - wires each unwired Dimensional Sinking Depot to the nearest power pole with a free slot within 100 m (development aid)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (!IsServer(World, Ar))
			{
				return;
			}
			AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(World);
			UClass* WireClass = LoadClass<AFGBuildableWire>(nullptr, TEXT("/Game/FactoryGame/Buildable/Factory/PowerLine/Build_PowerLine.Build_PowerLine_C"));
			if (!Buildables || !WireClass)
			{
				Ar.Log(TEXT("Buildable subsystem or Build_PowerLine class missing."));
				return;
			}
			int32 Wired = 0;
			for (AFGCentralStorageContainer* Depot : GetUploaders(World, true))
			{
				UFGPowerConnectionComponent* Ours = Depot->FindComponentByClass<UFGPowerConnectionComponent>();
				if (!Ours || Ours->GetNumConnections() > 0)
				{
					continue;
				}
				const FVector From = Ours->GetComponentLocation();
				UFGPowerConnectionComponent* Best = nullptr;
				double BestDistance = 10000.0;
				for (TActorIterator<AFGBuildablePowerPole> It(World); It; ++It)
				{
					for (UFGPowerConnectionComponent* Pole : It->GetPowerConnections())
					{
						const double Distance = Pole ? FVector::Dist(From, Pole->GetComponentLocation()) : BIG_NUMBER;
						if (Pole && Pole->GetNumFreeConnections() > 0 && Distance < BestDistance)
						{
							Best = Pole;
							BestDistance = Distance;
						}
					}
				}
				if (!Best)
				{
					Report(Ar, FString::Printf(TEXT("WireToPole: no pole with a free slot within 100 m of %s"), *Depot->GetName()));
					continue;
				}
				const FTransform Transform(From);
				AFGBuildableWire* Wire = Cast<AFGBuildableWire>(Buildables->BeginSpawnBuildable(WireClass, Transform));
				if (!Wire)
				{
					Report(Ar, TEXT("WireToPole: spawning the wire failed"));
					break;
				}
				const bool bConnected = Wire->Connect(Ours, Best);
				Wire->FinishSpawning(Transform);
				Report(Ar, FString::Printf(TEXT("WireToPole: %s -> %s (%.0f m): %s"), *Depot->GetName(), *Best->GetOwner()->GetName(), BestDistance / 100.0,
					bConnected ? TEXT("connected") : TEXT("FAILED")));
				Wired += bConnected ? 1 : 0;
			}
			Report(Ar, FString::Printf(TEXT("WireToPole done: %d wired"), Wired));
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GReplaceCommand(
		TEXT("SinkingDepot.Dev.Replace"),
		TEXT("SinkingDepot.Dev.Replace <Count> - replaces vanilla uploaders that have a belt attached with Dimensional Sinking Depots, keeping the belt (development aid; their buffered items are lost)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (Args.Num() != 1 || !Args[0].IsNumeric() || FCString::Atoi(*Args[0]) <= 0 || !IsServer(World, Ar))
			{
				Ar.Log(TEXT("Usage: SinkingDepot.Dev.Replace <Count>"));
				return;
			}
			AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(World);
			UClass* BuildClass = LoadClass<AFGBuildable>(nullptr, BuildClassPath);
			UClass* RecipeClass = LoadClass<UFGRecipe>(nullptr, RecipeClassPath);
			if (!Buildables || !BuildClass)
			{
				Ar.Log(TEXT("Buildable subsystem or Build_SinkingDepot class missing."));
				return;
			}
			const int32 Wanted = FCString::Atoi(*Args[0]);
			int32 Replaced = 0;
			for (AFGCentralStorageContainer* Old : GetUploaders(World, false))
			{
				if (Replaced >= Wanted)
				{
					break;
				}
				UFGFactoryConnectionComponent* OldInput = FindInput(Old);
				UFGFactoryConnectionComponent* Belt = OldInput ? OldInput->GetConnection() : nullptr;
				if (!Belt)
				{
					continue;
				}
				const FTransform Transform = Old->GetActorTransform();
				const FString OldName = Old->GetName();
				OldInput->ClearConnection();
				Old->Destroy();

				AFGBuildable* New = Buildables->BeginSpawnBuildable(BuildClass, Transform);
				if (!New)
				{
					Report(Ar, FString::Printf(TEXT("Replace: spawning failed at %s"), *OldName));
					break;
				}
				if (RecipeClass)
				{
					New->SetBuiltWithRecipe(RecipeClass);
				}
				New->FinishSpawning(Transform);
				UFGFactoryConnectionComponent* NewInput = FindInput(New);
				if (NewInput)
				{
					NewInput->SetConnection(Belt);
				}
				Report(Ar, FString::Printf(TEXT("Replace: %s -> %s, belt %s"), *OldName, *New->GetName(),
					NewInput && NewInput->IsConnected() ? TEXT("connected") : TEXT("NOT connected")));
				++Replaced;
			}
			Report(Ar, FString::Printf(TEXT("Replace done: %d uploader(s)"), Replaced));
		}));
}

namespace
{
	constexpr const TCHAR* VanillaBuildClassPath = TEXT("/Game/FactoryGame/Buildable/Factory/CentralStorage/Build_CentralStorage.Build_CentralStorage_C");

	/** Finds the ground straight below (or above) Location, ignoring pawns; returns Location if nothing is hit. */
	FVector FindGround(UWorld* World, const FVector& Location, const float Reach)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("SinkingDepotShowcase"), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		if (World->LineTraceSingleByChannel(Hit, Location + FVector(0, 0, Reach), Location - FVector(0, 0, Reach), ECC_WorldStatic, Params))
		{
			return Hit.ImpactPoint;
		}
		return Location;
	}

	/** Spawns a buildable standing on the ground near Location, facing Yaw. */
	AFGBuildable* SpawnOnGround(UWorld* World, UClass* Class, const FVector& Location, const float Yaw)
	{
		AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(World);
		if (!Buildables || !Class)
		{
			return nullptr;
		}
		const FTransform Transform(FRotator(0.0f, Yaw, 0.0f), FindGround(World, Location, 5000.0f));
		AFGBuildable* Buildable = Buildables->BeginSpawnBuildable(Class, Transform);
		if (Buildable)
		{
			Buildable->FinishSpawning(Transform);
		}
		return Buildable;
	}

	/** Our own cameras on the showcase, so the shots work even while the drop-pod intro owns the view. */
	struct FShowcaseShot
	{
		FString Name;
		TWeakObjectPtr<ACameraActor> Camera;
	};
	TArray<FShowcaseShot> GShowcaseShots;

	void ViewCamera(UWorld* World, ACameraActor* Camera)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		if (Controller && Camera)
		{
			Controller->SetViewTargetWithBlend(Camera, 0.0f);
		}
	}

	void AddShot(UWorld* World, const FString& Name, const FVector& Target, const FVector& CameraLocation)
	{
		ACameraActor* Camera = World->SpawnActor<ACameraActor>(CameraLocation, FRotationMatrix::MakeFromX(Target - CameraLocation).Rotator());
		GShowcaseShots.Add({Name, Camera});
	}

	/**
	 * Exact sink accounting in a world with no AWESOME Sinks: fill the depot with Iron Plate, put 50 more into the
	 * Sinking Depot (no belt attached), and compare the default-track points a few seconds later with 50 x the
	 * item's points. The sink subsystem drains its queue on a 1 s tick.
	 */
	void StartPointsCheck(UWorld* World, AFGCentralStorageContainer* Ours, FOutputDevice& Ar)
	{
		AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(World);
		UClass* PlateClass = LoadClass<UFGItemDescriptor>(nullptr, TEXT("/Game/FactoryGame/Resource/Parts/IronPlate/Desc_IronPlate.Desc_IronPlate_C"));
		if (!Sink || !Ours || !PlateClass || !Ours->GetStorageInventory())
		{
			Report(Ar, TEXT("Points check: missing sink, building or item class; skipped"));
			return;
		}
		const TSubclassOf<UFGItemDescriptor> Plate(PlateClass);
		AFGCentralStorageSubsystem* Depot = AFGCentralStorageSubsystem::Get(World);
		const int32 Space = Depot ? Depot->GetCentralStorageItemLimit(Plate) - Depot->GetNumItemsFromCentralStorage(Plate) : 0;
		FillDepot(World, Plate, Space);
		int32 Points = 0;
		EResourceSinkTrack Track = EResourceSinkTrack::RST_Default;
		Sink->FindResourceSinkPointsForItem(Plate, Points, Track);
		const int64 PointsBefore = Sink->GetNumTotalPoints(EResourceSinkTrack::RST_Default);
		const int64 SunkBefore = SinkingDepot::Overflow::Runtime().SunkDepotFull.load();
		constexpr int32 Fed = 50;
		const int32 Added = Ours->GetStorageInventory()->AddStack(FInventoryStack(Fed, Plate), true);
		Report(Ar, FString::Printf(TEXT("Points check: depot %d of %d Iron Plate; fed %d into the Sinking Depot; %d points each; points before %lld"),
			Depot ? Depot->GetNumItemsFromCentralStorage(Plate) : -1, Depot ? Depot->GetCentralStorageItemLimit(Plate) : -1, Added, Points, PointsBefore));

		const TWeakObjectPtr<AFGResourceSinkSubsystem> WeakSink(Sink);
		const TWeakObjectPtr<AFGCentralStorageContainer> WeakOurs(Ours);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [WeakSink, WeakOurs, Plate, Added, Points, PointsBefore, SunkBefore]()
		{
			AFGResourceSinkSubsystem* S = WeakSink.Get();
			if (!S)
			{
				return;
			}
			const int64 Gained = S->GetNumTotalPoints(EResourceSinkTrack::RST_Default) - PointsBefore;
			const int64 Sunk = SinkingDepot::Overflow::Runtime().SunkDepotFull.load() - SunkBefore;
			const int32 Left = WeakOurs.IsValid() && WeakOurs->GetStorageInventory() ? WeakOurs->GetStorageInventory()->GetNumItems(Plate) : -1;
			const int64 Expected = static_cast<int64>(Added) * Points;
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] Points check: sunk %lld of %d, %d left in the building; points gained %lld, expected %lld -> %s"),
				Sunk, Added, Left, Gained, Expected, (Gained == Expected && Sunk == Added && Left == 0) ? TEXT("PASS") : TEXT("FAIL"));
		}), 5.0f, false);
	}

	void Showcase(UWorld* World, FOutputDevice& Ar)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!Pawn)
		{
			Ar.Log(TEXT("Showcase: no local player pawn yet."));
			return;
		}
		if (ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World))
		{
			if (AFGSchematicManager* Schematics = AFGSchematicManager::Get(World))
			{
				Schematics->GiveAccessToSchematic(Subsystem->GetDepotResearch(), nullptr, ESchematicUnlockFlags::BlockTelemetry);
			}
		}
		// During the drop-pod intro the pawn is still in the sky, so build on the ground below it.
		const FRotator View(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
		const FVector Forward = View.Vector();
		const FVector Right = FRotationMatrix(View).GetUnitAxis(EAxis::Y);
		const FVector Anchor = FindGround(World, Pawn->GetActorLocation(), 200000.0f);
		const FVector RowCentre = Anchor + Forward * 2500.0f;
		const float FacePlayer = View.Yaw + 180.0f;

		// One row: vanilla; ours idle (grinder window still), also shot from behind for the power connector; ours forced
		// active (drum turning, glow), shot twice a few seconds apart to show the drum moving. Sinking Depots spawned
		// here count as newly placed, so they get the default white/FICSIT paint. Each gets its own close-up.
		struct FVariant
		{
			const TCHAR* Name;
			bool bVanilla;
			bool bForceActive;
			bool bBackShot;
		};
		const FVariant Variants[] = {
			{TEXT("01_vanilla"), true, false, false},
			{TEXT("02_grinder_idle"), false, false, true},
			{TEXT("03_grinder_active"), false, true, false},
		};
		GShowcaseShots.Reset();
		AFGBuildable* Plain = nullptr;
		AFGBuildable* Vanilla = nullptr;
		ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World);
		const int32 Count = UE_ARRAY_COUNT(Variants);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVariant& Variant = Variants[Index];
			const FVector Location = RowCentre + Right * ((Index - (Count - 1) * 0.5f) * 1600.0f);
			AFGBuildable* Building = SpawnOnGround(World, LoadClass<AFGBuildable>(nullptr, Variant.bVanilla ? VanillaBuildClassPath : BuildClassPath), Location, FacePlayer);
			if (!Building)
			{
				Report(Ar, FString::Printf(TEXT("Showcase: spawning %s failed"), Variant.Name));
				continue;
			}
			if (Variant.bForceActive && Subsystem)
			{
				Subsystem->SetForcedActive(Building, true);
			}
			Plain = (!Plain && !Variant.bVanilla && !Variant.bForceActive) ? Building : Plain;
			Vanilla = (!Vanilla && Variant.bVanilla) ? Building : Vanilla;
			const FVector Centre = Building->GetComponentsBoundingBox(true).GetCenter();
			const FVector CameraLocation = Centre - Forward * 1500.0f + Right * 700.0f + FVector(0, 0, 450.0f);
			AddShot(World, Variant.Name, Centre, CameraLocation);
			if (Variant.bForceActive)
			{
				AddShot(World, FString(Variant.Name) + TEXT("_later"), Centre, CameraLocation);
			}
			if (Variant.bBackShot)
			{
				// From behind and to its right (the building faces the player), where the power connector is.
				AddShot(World, FString(Variant.Name) + TEXT("_back"), Centre, Centre + Forward * 1400.0f + Right * 900.0f + FVector(0, 0, 600.0f));
				// Closer, aimed at the top of that same corner, for the power stub.
				const FVector Corner = Centre + Forward * 150.0f + Right * 380.0f + FVector(0, 0, 180.0f);
				AddShot(World, FString(Variant.Name) + TEXT("_back_close"), Corner, Corner + Forward * 650.0f + Right * 450.0f + FVector(0, 0, 250.0f));
			}
		}
		const FVector RowTarget = FindGround(World, RowCentre, 5000.0f) + FVector(0, 0, 300.0f);
		AddShot(World, TEXT("0_overview"), RowTarget, RowTarget - Forward * 5200.0f + FVector(0, 0, 1400.0f));
		if (GShowcaseShots.Num() > 0)
		{
			ViewCamera(World, GShowcaseShots.Last().Camera.Get());
		}
		Report(Ar, FString::Printf(TEXT("Showcase: %d buildings; ground at %s (pawn at %s)"), Count, *Anchor.ToCompactString(), *Pawn->GetActorLocation().ToCompactString()));
		if (Plain)
		{
			DumpMaterials(Plain, Ar);
			// Where the connector post really ends up versus the uploader body's bounds (the editor only has stand-in meshes).
			FBox Whole(ForceInit);
			for (UActorComponent* Component : Plain->GetComponents())
			{
				if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component); Primitive && Component->GetName() == TEXT("MainMesh"))
				{
					Whole = Primitive->Bounds.GetBox();
				}
				// The real mesh's bounds in building space (the editor only has stand-ins), for placing the connector post.
				if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component); Mesh && Mesh->GetStaticMesh())
				{
					const FBox Local = Mesh->GetStaticMesh()->GetBoundingBox().TransformBy(Mesh->GetRelativeTransform());
					Report(Ar, FString::Printf(TEXT("Bounds %s (%s) in building space: X %.0f..%.0f, Y %.0f..%.0f, Z %.0f..%.0f"),
						*Component->GetName(), *Mesh->GetStaticMesh()->GetName(), Local.Min.X, Local.Max.X, Local.Min.Y, Local.Max.Y, Local.Min.Z, Local.Max.Z));
				}
			}
			for (UActorComponent* Component : Plain->GetComponents())
			{
				if (const USceneComponent* Scene = Cast<USceneComponent>(Component); Scene && Component->GetName().StartsWith(TEXT("Power")))
				{
					const FTransform ToLocal = Plain->GetActorTransform().Inverse();
					const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Scene);
					Report(Ar, FString::Printf(TEXT("Connector %s: local location %s%s; uploader body (MainMesh) %s"), *Component->GetName(),
						*ToLocal.TransformPosition(Scene->GetComponentLocation()).ToCompactString(),
						Primitive ? *FString::Printf(TEXT(", bounds Z %.0f..%.0f (world)"), Primitive->Bounds.GetBox().Min.Z, Primitive->Bounds.GetBox().Max.Z) : TEXT(""),
						*FString::Printf(TEXT("Z %.0f..%.0f"), Whole.Min.Z, Whole.Max.Z)));
				}
			}
		}
		if (Vanilla)
		{
			DumpMaterials(Vanilla, Ar);
		}
		StartPointsCheck(World, Cast<AFGCentralStorageContainer>(Plain), Ar);
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice GShowcaseCommand(
		TEXT("SinkingDepot.Dev.Showcase"),
		TEXT("SinkingDepot.Dev.Showcase - grants the depot research and spawns a Sinking Depot and a vanilla uploader in front of you (development aid)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (IsServer(World, Ar))
			{
				Showcase(World, Ar);
			}
		}));
}

void SinkingDepot::Dev::Install()
{
	UE_LOG(LogSinkingDepot, Warning, TEXT("Development commands are compiled in (SinkingDepot.Dev.*). Not for release builds."));
}

namespace
{
	void ReportAndShoot(UWorld* World, AFGBuildable* Buildable);

	/** If set, -SinkingDepotDevBuild hands the new building to this instead of taking the usual screenshots. */
	TFunction<void(UWorld*, AFGBuildable*)> GOnDevBuilt;

	/** Names of the hologram's current construct disqualifiers, comma-separated ("" if none or no hologram). */
	FString DescribeDisqualifiers(AFGHologram* Hologram)
	{
		FString Reasons;
		if (Hologram)
		{
			TArray<TSubclassOf<UFGConstructDisqualifier>> Disqualifiers;
			Hologram->GetConstructDisqualifiers(Disqualifiers);
			for (const TSubclassOf<UFGConstructDisqualifier>& Disqualifier : Disqualifiers)
			{
				Reasons += (Reasons.IsEmpty() ? TEXT("") : TEXT(", ")) + GetNameSafe(Disqualifier.Get());
			}
			// Child holograms (a belt's auto-placed poles) can be what's invalid.
			for (AFGHologram* Child : Hologram->GetHologramChildren())
			{
				const FString ChildReasons = DescribeDisqualifiers(Child);
				Reasons += FString::Printf(TEXT("%s%s: {%s}"), Reasons.IsEmpty() ? TEXT("") : TEXT(", "), *GetNameSafe(Child), *ChildReasons);
			}
		}
		return Reasons;
	}

	/**
	 * One try of -SinkingDepotDevBuild with the player's real build gun: look down at the given pitch, wait for the
	 * hologram to settle, then press and release primary fire. If no new Sinking Depot appears, try again looking
	 * further down (up to three tries).
	 */
	void DevBuildTry(UWorld* World, int32 Try)
	{
		static const float Pitches[] = { -15.0f, -25.0f, -40.0f };
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		AFGCharacterPlayer* Player = Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
		if (!Player || !Player->GetBuildGun() || Try >= UE_ARRAY_COUNT(Pitches))
		{
			UE_LOG(LogSinkingDepot, Error, TEXT("[dev] DevBuild: giving up (try %d)"), Try);
			return;
		}
		Controller->SetControlRotation(FRotator(Pitches[Try], Controller->GetControlRotation().Yaw, 0.0f));
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle FireHandle;
		World->GetTimerManager().SetTimer(FireHandle, FTimerDelegate::CreateWeakLambda(World, [WeakWorld, Try]()
		{
			UWorld* W = WeakWorld.Get();
			APlayerController* C = W ? W->GetFirstPlayerController() : nullptr;
			AFGCharacterPlayer* P = C ? Cast<AFGCharacterPlayer>(C->GetPawn()) : nullptr;
			AFGBuildGun* Gun = P ? P->GetBuildGun() : nullptr;
			if (!Gun)
			{
				return;
			}
			UFGBuildGunStateBuild* BuildState = Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState());
			AFGHologram* Hologram = BuildState ? BuildState->GetHologram() : nullptr;
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] DevBuild: try %d (pitch %.0f), equipped %d, state %s, hologram %s at %s, aiming at %s, disqualifiers: [%s]"),
				Try, C->GetControlRotation().Pitch, P->IsBuildGunEquipped(), *GetNameSafe(Gun->GetCurrentState()), *GetNameSafe(Hologram),
				Hologram ? *Hologram->GetActorLocation().ToString() : TEXT("-"), *GetNameSafe(Gun->GetHitResult().GetActor()),
				*DescribeDisqualifiers(Hologram));
			TSet<AActor*> Before;
			for (TActorIterator<AFGBuildable> It(W); It; ++It)
			{
				if (SinkingDepot::IsSinkingDepot(*It))
				{
					Before.Add(*It);
				}
			}
			// What the build costs, in the depot and in the player's inventory, to see which one paid.
			TArray<FItemAmount> Cost;
			if (UClass* Recipe = LoadClass<UFGRecipe>(nullptr, RecipeClassPath))
			{
				Cost = UFGRecipe::GetIngredients(W, Recipe);
			}
			auto CountCost = [WeakWorld, Cost](int32& OutDepot, int32& OutPlayer)
			{
				OutDepot = OutPlayer = 0;
				UWorld* W2 = WeakWorld.Get();
				AFGCentralStorageSubsystem* Depot = W2 ? AFGCentralStorageSubsystem::Get(W2) : nullptr;
				APlayerController* C2 = W2 ? W2->GetFirstPlayerController() : nullptr;
				AFGCharacterPlayer* P2 = C2 ? Cast<AFGCharacterPlayer>(C2->GetPawn()) : nullptr;
				for (const FItemAmount& Amount : Cost)
				{
					OutDepot += Depot ? Depot->GetNumItemsFromCentralStorage(Amount.ItemClass) : 0;
					OutPlayer += P2 ? P2->GetInventory()->GetNumItems(Amount.ItemClass) : 0;
				}
			};
			int32 DepotBefore = 0, PlayerBefore = 0;
			CountCost(DepotBefore, PlayerBefore);
			Gun->OnPrimaryFirePressed();
			FTimerHandle ReleaseHandle;
			W->GetTimerManager().SetTimer(ReleaseHandle, FTimerDelegate::CreateWeakLambda(W, [WeakWorld]()
			{
				UWorld* W2 = WeakWorld.Get();
				APlayerController* C2 = W2 ? W2->GetFirstPlayerController() : nullptr;
				AFGCharacterPlayer* P2 = C2 ? Cast<AFGCharacterPlayer>(C2->GetPawn()) : nullptr;
				if (P2 && P2->GetBuildGun())
				{
					P2->GetBuildGun()->OnPrimaryFireReleased();
				}
			}), 0.2f, false);
			FTimerHandle CheckHandle;
			W->GetTimerManager().SetTimer(CheckHandle, FTimerDelegate::CreateWeakLambda(W, [WeakWorld, Try, Before, CountCost, DepotBefore, PlayerBefore]()
			{
				UWorld* W2 = WeakWorld.Get();
				if (!W2)
				{
					return;
				}
				for (TActorIterator<AFGBuildable> It(W2); It; ++It)
				{
					if (SinkingDepot::IsSinkingDepot(*It) && !Before.Contains(*It))
					{
						int32 DepotAfter = 0, PlayerAfter = 0;
						CountCost(DepotAfter, PlayerAfter);
						UE_LOG(LogSinkingDepot, Log, TEXT("[dev] DevBuild: built %s at %s with the build gun; cost items in depot %d -> %d, in player inventory %d -> %d"),
							*It->GetName(), *It->GetActorLocation().ToString(), DepotBefore, DepotAfter, PlayerBefore, PlayerAfter);
						// Put the build gun away so its next-placement hologram isn't in the screenshots.
						APlayerController* C2 = W2->GetFirstPlayerController();
						AFGCharacterPlayer* P2 = C2 ? Cast<AFGCharacterPlayer>(C2->GetPawn()) : nullptr;
						if (P2 && P2->IsBuildGunEquipped())
						{
							P2->ToggleBuildGun();
						}
						if (GOnDevBuilt)
						{
							const TFunction<void(UWorld*, AFGBuildable*)> OnBuilt = MoveTemp(GOnDevBuilt);
							GOnDevBuilt = nullptr;
							OnBuilt(W2, *It);
							return;
						}
						ReportAndShoot(W2, *It);
						return;
					}
				}
				UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] DevBuild: try %d built nothing"), Try);
				DevBuildTry(W2, Try + 1);
			}), 2.0f, false);
		}), 3.0f, false);
	}

	/**
	 * -SinkingDepotDevBuild: builds a Sinking Depot with the player's real build gun (equip, select the recipe, aim,
	 * primary fire), the same path as a player's click, so hologram placement, cost (paid from the depot), default
	 * paint and the build effect are all the game's own. Synthetic keyboard/mouse input doesn't reach gameplay, which
	 * is why this is driven from inside the game. Then frames the result and takes screenshots.
	 */
	void DevBuild(UWorld* World)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		AFGCharacterPlayer* Player = Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
		UClass* Recipe = LoadClass<UFGRecipe>(nullptr, RecipeClassPath);
		if (!Player || !Player->GetBuildGun() || !Recipe)
		{
			UE_LOG(LogSinkingDepot, Error, TEXT("[dev] DevBuild: player, build gun or recipe missing"));
			return;
		}
		if (!Player->IsBuildGunEquipped())
		{
			Player->ToggleBuildGun();
		}
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [WeakWorld, Recipe]()
		{
			UWorld* W = WeakWorld.Get();
			APlayerController* C = W ? W->GetFirstPlayerController() : nullptr;
			AFGCharacterPlayer* P = C ? Cast<AFGCharacterPlayer>(C->GetPawn()) : nullptr;
			if (P && P->GetBuildGun())
			{
				P->GetBuildGun()->GotoBuildState(Recipe);
				DevBuildTry(W, 0);
			}
		}), 1.5f, false);
	}

	/** After the default-paint tick and the build effect: report the paint and take the shots. */
	void ReportAndShoot(UWorld* World, AFGBuildable* Buildable)
	{
		const TWeakObjectPtr<AFGBuildable> Weak(Buildable);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [World, Weak]()
		{
			AFGBuildable* B = Weak.Get();
			if (!B)
			{
				return;
			}
			const FFactoryCustomizationData& Paint = B->GetCustomizationData_Native();
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] DevBuild: %s paint: swatch %s, primary %s, secondary %s; loaded-from-save tag: %d"),
				*B->GetName(), Paint.SwatchDesc ? *Paint.SwatchDesc->GetName() : TEXT("(none)"),
				*Paint.OverrideColorData.PrimaryColor.ToString(), *Paint.OverrideColorData.SecondaryColor.ToString(),
				B->Tags.Contains(SinkingDepot::LoadedFromSaveTag));
			const FTransform Frame = B->GetActorTransform();
			// Where the power stub and its wire point really are, in building space.
			for (UActorComponent* Component : B->GetComponents())
			{
				if (const USceneComponent* Scene = Cast<USceneComponent>(Component); Scene && Component->GetName().StartsWith(TEXT("Power")))
				{
					FString Extent;
					if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component); Mesh && Mesh->GetStaticMesh())
					{
						const FBox Local = Mesh->GetStaticMesh()->GetBoundingBox().TransformBy(Mesh->GetRelativeTransform());
						Extent = FString::Printf(TEXT(", bounds X %.0f..%.0f, Y %.0f..%.0f, Z %.0f..%.0f"),
							Local.Min.X, Local.Max.X, Local.Min.Y, Local.Max.Y, Local.Min.Z, Local.Max.Z);
					}
					UE_LOG(LogSinkingDepot, Log, TEXT("[dev] DevBuild: %s in building space: location %s%s"), *Component->GetName(),
						*Frame.InverseTransformPosition(Scene->GetComponentLocation()).ToCompactString(), *Extent);
				}
			}
			GShowcaseShots.Reset();
			// Building space: the window faces +X (front), the belt input is at +Y, "right" (seen from the front) is -Y.
			// The power stub sticks out of the body's back face (x -140) at y -280, z 270, tip at x -205.
			// How every mesh gets its colour: the stub should be set up like the depot's own MainMesh (v2 rendered dark).
			// Custom primitive data is what AFGBuildable::ApplyMeshPrimitiveData writes for non-instanced meshes.
			for (UActorComponent* Component : B->GetComponents())
			{
				if (const UMeshComponent* Mesh = Cast<UMeshComponent>(Component))
				{
					FString Materials;
					for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
					{
						Materials += (Slot ? TEXT(", ") : TEXT("")) + GetNameSafe(Mesh->GetMaterial(Slot));
					}
					const TArray<float>& Data = Mesh->GetCustomPrimitiveData().Data;
					FString FirstFloats;
					for (int32 Index = 0; Index < FMath::Min(Data.Num(), 8); ++Index)
					{
						FirstFloats += FString::Printf(TEXT("%s%.2f"), Index ? TEXT(" ") : TEXT(""), Data[Index]);
					}
					const UFGColoredInstanceMeshProxy* Proxy = Cast<UFGColoredInstanceMeshProxy>(Mesh);
					UE_LOG(LogSinkingDepot, Log, TEXT("[dev] DevBuild: mesh %s [%s]: primitive data %d floats (%s)%s, materials [%s]"),
						*Mesh->GetName(), *Mesh->GetClass()->GetName(), Data.Num(), *FirstFloats,
						Proxy ? *FString::Printf(TEXT(", proxy custom floats %d, block coloring %d"), Proxy->GetNumCustomDataFloats(), Proxy->mBlockColoring) : TEXT(""),
						*Materials);
				}
			}
			auto Shot = [World, &Frame](const TCHAR* Name, const FVector& Target, const FVector& Camera)
			{
				AddShot(World, Name, Frame.TransformPosition(Target), Frame.TransformPosition(Camera));
			};
			Shot(TEXT("devbuild_1_front"), FVector(0, 0, 200), FVector(1300, 600, 600));
			Shot(TEXT("devbuild_2_front_right"), FVector(0, -100, 200), FVector(1100, -1100, 500));
			Shot(TEXT("devbuild_3_back"), FVector(-100, -100, 220), FVector(-1300, -700, 550));
			Shot(TEXT("devbuild_4_back_left"), FVector(-100, 100, 220), FVector(-1100, 1200, 500));
			Shot(TEXT("devbuild_5_stub_close"), FVector(-185, -280, 270), FVector(-560, -480, 330));
			Shot(TEXT("devbuild_6_stub_profile"), FVector(-185, -280, 270), FVector(-200, -900, 300));
			Shot(TEXT("devbuild_7_top"), FVector(-60, -150, 300), FVector(-250, -350, 1500));
			// The last shots with the grinder running (forced; no items needed), to show the active window.
			const int32 ActiveFrom = GShowcaseShots.Num();
			Shot(TEXT("devbuild_8_grinder_active"), FVector(140, 0, 250), FVector(900, 250, 350));
			for (int32 Index = 0; Index < GShowcaseShots.Num(); ++Index)
			{
				FTimerHandle SwitchHandle;
				World->GetTimerManager().SetTimer(SwitchHandle, FTimerDelegate::CreateWeakLambda(World, [World, Index, ActiveFrom, Weak]()
				{
					if (Index == ActiveFrom - 1 && Weak.IsValid()) // one shot early, so the grinder has spun up
					{
						if (ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World))
						{
							Subsystem->SetForcedActive(Weak.Get(), true);
						}
					}
					ViewCamera(World, GShowcaseShots[Index].Camera.Get());
				}), 1.0f + Index * 3.0f, false);
				FTimerHandle ShotHandle;
				World->GetTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateWeakLambda(World, [Index]()
				{
					FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("SinkingDepotShowcase_%s.png"), *GShowcaseShots[Index].Name), false, false);
				}), 2.5f + Index * 3.0f, false);
			}
		}), 15.0f, false);
	}
}

namespace
{
	constexpr const TCHAR* StorageContainerClassPath = TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk1/Build_StorageContainerMk1.Build_StorageContainerMk1_C");
	constexpr const TCHAR* BeltRecipePath = TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_ConveyorBeltMk1.Recipe_ConveyorBeltMk1_C");

	/**
	 * What decides whether a building shows, one entry per part: the actor (hidden, build effect), every primitive
	 * component (visibility, registration, render state, colored-instance state, materials) and how many build
	 * effect actors exist in the world.
	 */
	TMap<FString, FString> DescribeVisualState(AFGBuildable* Building)
	{
		TMap<FString, FString> State;
		State.Add(TEXT("actor"), FString::Printf(TEXT("hidden %d, build effect playing %d, effect actor %s"),
			Building->IsHidden(), Building->IsPlayingBuildEffect(), *GetNameSafe(Building->GetBuildEffectActor())));
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Building);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			if (!Primitive)
			{
				continue;
			}
			FString Line = FString::Printf(TEXT("[%s] visible %d, hidden in game %d, registered %d, render state %d"), *Primitive->GetClass()->GetName(),
				Primitive->IsVisible(), Primitive->bHiddenInGame, Primitive->IsRegistered(), Primitive->IsRenderStateCreated());
			if (const UFGColoredInstanceMeshProxy* Proxy = Cast<UFGColoredInstanceMeshProxy>(Primitive))
			{
				Line += FString::Printf(TEXT(", instanced %d"), Proxy->mInstanceHandle.IsInstanced());
			}
			if (const UMeshComponent* Mesh = Cast<UMeshComponent>(Primitive))
			{
				Line += TEXT(", materials [");
				for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
				{
					Line += (Slot ? TEXT(", ") : TEXT("")) + GetNameSafe(Mesh->GetMaterial(Slot));
				}
				Line += TEXT("]");
			}
			State.Add(Primitive->GetName(), Line);
		}
		if (AFGCentralStorageContainer* Uploader = Cast<AFGCentralStorageContainer>(Building))
		{
			const ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(Building->GetWorld());
			const UFGInventoryComponent* Inventory = Uploader->GetStorageInventory();
			State.Add(TEXT("work"), FString::Printf(TEXT("items in the uploader %d, grinder active %d"),
				Inventory ? Inventory->GetNumItems(nullptr) : -1, Subsystem && Subsystem->IsGrinderActive(Building)));
		}
		int32 Effects = 0;
		for (TActorIterator<AFGBuildEffectActor> It(Building->GetWorld()); It; ++It)
		{
			++Effects;
		}
		State.Add(TEXT("world"), FString::Printf(TEXT("build effect actors: %d"), Effects));
		return State;
	}

	/** Every 0.1 s for Seconds: logs whatever changed in the building's visual state (and frame hitches), then OnDone. */
	void MonitorVisualState(const TWeakObjectPtr<UWorld> WeakWorld, const TWeakObjectPtr<AFGBuildable> Weak, const FString Label, const double Start,
		const double Previous, const TSharedRef<TMap<FString, FString>> Last, const double Seconds, const TFunction<void()> OnDone)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return;
		}
		const double Now = FPlatformTime::Seconds();
		if (Now - Previous > 0.5)
		{
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] BeltCheck %s t=%.2f: hitch, %.2f s since the last poll"), *Label, Now - Start, Now - Previous);
		}
		if (AFGBuildable* Building = Weak.Get())
		{
			TMap<FString, FString> State = DescribeVisualState(Building);
			for (const TPair<FString, FString>& Pair : State)
			{
				const FString* Was = Last->Find(Pair.Key);
				if (!Was || *Was != Pair.Value)
				{
					UE_LOG(LogSinkingDepot, Log, TEXT("[dev] BeltCheck %s t=%.2f %s: %s"), *Label, Now - Start, *Pair.Key, *Pair.Value);
				}
			}
			for (const TPair<FString, FString>& Pair : *Last)
			{
				if (!State.Contains(Pair.Key))
				{
					UE_LOG(LogSinkingDepot, Log, TEXT("[dev] BeltCheck %s t=%.2f %s: gone"), *Label, Now - Start, *Pair.Key);
				}
			}
			*Last = MoveTemp(State);
		}
		else
		{
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] BeltCheck %s t=%.2f: the building no longer exists"), *Label, Now - Start);
		}
		if (Now - Start > Seconds)
		{
			OnDone();
			return;
		}
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [WeakWorld, Weak, Label, Start, Now, Last, Seconds, OnDone]()
		{
			MonitorVisualState(WeakWorld, Weak, Label, Start, Now, Last, Seconds, OnDone);
		}), 0.1f, false);
	}

	/** Runs Function after Delay seconds of game time, if the world still exists. */
	void After(UWorld* World, const float Delay, TFunction<void(UWorld*)> Function)
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

	AFGCharacterPlayer* LocalPlayer(UWorld* World, APlayerController*& OutController)
	{
		OutController = World ? World->GetFirstPlayerController() : nullptr;
		return OutController ? Cast<AFGCharacterPlayer>(OutController->GetPawn()) : nullptr;
	}

	/** Points the player's view at a world location (the build gun aims along the camera). */
	void AimAt(APlayerController* Controller, const FVector& Target)
	{
		const FVector Eye = Controller->PlayerCameraManager ? Controller->PlayerCameraManager->GetCameraLocation() : Controller->GetPawn()->GetActorLocation();
		Controller->SetControlRotation((Target - Eye).Rotation());
	}

	void LogBuildGunState(UWorld* World, const FString& Label, const TCHAR* Step)
	{
		APlayerController* Controller = nullptr;
		AFGCharacterPlayer* Player = LocalPlayer(World, Controller);
		AFGBuildGun* Gun = Player ? Player->GetBuildGun() : nullptr;
		UFGBuildGunStateBuild* BuildState = Gun ? Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState()) : nullptr;
		AFGHologram* Hologram = BuildState ? BuildState->GetHologram() : nullptr;
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] BeltCheck %s %s: gun equipped %d, state %s, hologram %s at %s, aiming at %s (%s), disqualifiers [%s]"),
			*Label, Step, Player && Player->IsBuildGunEquipped(), *GetNameSafe(Gun ? Gun->GetCurrentState() : nullptr), *GetNameSafe(Hologram),
			Hologram ? *Hologram->GetActorLocation().ToCompactString() : TEXT("-"), Gun ? *GetNameSafe(Gun->GetHitResult().GetActor()) : TEXT("-"),
			Gun ? *Gun->GetHitResult().ImpactPoint.ToCompactString() : TEXT("-"), *DescribeDisqualifiers(Hologram));
	}

	void InjectActionsForKey(UWorld* World, const FKey& Key); // defined with the build menu check below
	void WatchBuilding(UWorld* W, const FString& Label, const TWeakObjectPtr<AFGBuildable> WeakTarget, const TWeakObjectPtr<UFGFactoryConnectionComponent> WeakInput,
		const FVector Centre, const FVector Out, const TFunction<void()> OnDone);

	/** What a belt build step changes: the hologram object (a new one after building), its build step, the connection. */
	struct FBeltSnapshot
	{
		TWeakObjectPtr<AFGHologram> Hologram;
		int32 Step = -1;
		bool bCanTakeNextStep = false;
		bool bConnected = false;

		FString Describe() const
		{
			return FString::Printf(TEXT("hologram %s, build step %d, can take next step %d, input connected %d"),
				*GetNameSafe(Hologram.Get()), Step, bCanTakeNextStep, bConnected);
		}
	};

	FBeltSnapshot SnapshotBelt(UWorld* World, const TWeakObjectPtr<UFGFactoryConnectionComponent>& Input)
	{
		FBeltSnapshot Snapshot;
		APlayerController* Controller = nullptr;
		AFGCharacterPlayer* Player = LocalPlayer(World, Controller);
		AFGBuildGun* Gun = Player ? Player->GetBuildGun() : nullptr;
		UFGBuildGunStateBuild* State = Gun ? Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState()) : nullptr;
		AFGHologram* Hologram = State ? State->GetHologram() : nullptr;
		Snapshot.Hologram = Hologram;
		if (AFGSplineHologram* Spline = Cast<AFGSplineHologram>(Hologram))
		{
			Snapshot.Step = static_cast<int32>(Spline->GetCurrentBuildStep());
		}
		Snapshot.bCanTakeNextStep = Hologram && Hologram->CanTakeNextBuildStep();
		Snapshot.bConnected = Input.IsValid() && Input->IsConnected();
		return Snapshot;
	}

	/**
	 * Advances the belt hologram one build step, like one click. Input paths, tried in turn until the hologram reacts
	 * (another hologram object, another build step, or the input got connected):
	 * 0. AFGBuildGun::OnPrimaryFirePressed / Released (what placed the depot itself);
	 * 1. the build state's PrimaryFire / PrimaryFireRelease directly (skips the build gun's own gating);
	 * 2. the Enhanced Input action(s) mapped to the left mouse button (the path a real click takes).
	 * OnDone gets whether it advanced.
	 */
	void AdvanceBelt(UWorld* World, const FString Label, const FString StepName, const int32 Method, const TWeakObjectPtr<UFGFactoryConnectionComponent> Input,
		const TFunction<void(UWorld*, bool)> OnDone)
	{
		static const TCHAR* Methods[] = {
			TEXT("build gun OnPrimaryFirePressed/Released"),
			TEXT("build state PrimaryFire/PrimaryFireRelease"),
			TEXT("Enhanced Input action for the left mouse button"),
		};
		const FBeltSnapshot Before = SnapshotBelt(World, Input);
		LogBuildGunState(World, Label, *FString::Printf(TEXT("%s, method %d (%s), before: %s"), *StepName, Method, Methods[Method], *Before.Describe()));
		APlayerController* Controller = nullptr;
		AFGCharacterPlayer* Player = LocalPlayer(World, Controller);
		AFGBuildGun* Gun = Player ? Player->GetBuildGun() : nullptr;
		if (Gun && Method == 0)
		{
			Gun->OnPrimaryFirePressed();
			After(World, 0.15f, [](UWorld* W)
			{
				APlayerController* C = nullptr;
				if (AFGCharacterPlayer* P = LocalPlayer(W, C); P && P->GetBuildGun())
				{
					P->GetBuildGun()->OnPrimaryFireReleased();
				}
			});
		}
		else if (Gun && Method == 1)
		{
			if (UFGBuildGunState* State = Gun->GetCurrentState())
			{
				State->PrimaryFire();
				After(World, 0.15f, [WeakState = TWeakObjectPtr<UFGBuildGunState>(State)](UWorld*)
				{
					if (UFGBuildGunState* S = WeakState.Get())
					{
						S->PrimaryFireRelease();
					}
				});
			}
		}
		else if (Method == 2)
		{
			InjectActionsForKey(World, EKeys::LeftMouseButton);
		}
		After(World, 1.2f, [Label, StepName, Method, Input, OnDone, Before](UWorld* W)
		{
			const FBeltSnapshot Now = SnapshotBelt(W, Input);
			const bool bChanged = Now.Hologram != Before.Hologram || Now.Step != Before.Step || Now.bConnected != Before.bConnected;
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] BeltCheck %s %s, method %d: %s; now %s"), *Label, *StepName, Method,
				bChanged ? TEXT("ADVANCED") : TEXT("no change"), *Now.Describe());
			if (bChanged || Method >= 2)
			{
				OnDone(W, bChanged);
				return;
			}
			AdvanceBelt(W, Label, StepName, Method + 1, Input, OnDone);
		});
	}

	/**
	 * Builds a Mk1 belt into Target's input with the player's real build gun (first step on the input, second on
	 * the ground 12 m out), the same path as a player's clicks, then watches the building for 20 s: state changes are
	 * logged every 0.1 s and screenshots are taken from a fixed camera. Then OnDone.
	 */
	void BuildBeltInto(UWorld* World, AFGBuildable* Target, const FString& Label, TFunction<void()> OnDone, const bool bFeed = false, UClass* FeedItem = nullptr)
	{
		APlayerController* Controller = nullptr;
		AFGCharacterPlayer* Player = LocalPlayer(World, Controller);
		UFGFactoryConnectionComponent* Input = FindInput(Target);
		UClass* Recipe = LoadClass<UFGRecipe>(nullptr, BeltRecipePath);
		if (!Player || !Input || !Recipe)
		{
			UE_LOG(LogSinkingDepot, Error, TEXT("[dev] BeltCheck %s: player, input or belt recipe missing"), *Label);
			OnDone();
			return;
		}
		if (AFGRecipeManager* Recipes = AFGRecipeManager::Get(World))
		{
			Recipes->AddAvailableRecipe(Recipe);
		}
		if (AFGGameState* State = World->GetGameState<AFGGameState>())
		{
			State->SetCheatNoCost(true);
		}
		// In case the no-cost cheat doesn't cover the build gun: plates for the belt, in the player's inventory.
		if (UClass* Plate = LoadClass<UFGItemDescriptor>(nullptr, TEXT("/Game/FactoryGame/Resource/Parts/IronPlate/Desc_IronPlate.Desc_IronPlate_C")))
		{
			const int32 Added = Player->GetInventory()->AddStack(FInventoryStack(100, Plate), true);
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] BeltCheck %s: %d Iron Plates into the player's inventory"), *Label, Added);
		}
		const FVector Centre = Target->GetComponentsBoundingBox(true).GetCenter();
		const FVector InputLocation = Input->GetComponentLocation();
		FVector Out = InputLocation - Centre;
		Out.Z = 0.0f;
		Out = Out.GetSafeNormal();
		const FVector Stand = FindGround(World, InputLocation + Out * 1900.0f, 3000.0f) + FVector(0, 0, 180.0f);
		Player->TeleportTo(Stand, (-Out).Rotation());
		Controller->SetControlRotation((-Out).Rotation());
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] BeltCheck %s: target %s, input at %s (connected %d), player to %s"),
			*Label, *Target->GetName(), *InputLocation.ToCompactString(), Input->IsConnected(), *Stand.ToCompactString());

		// Empty belt: from the input to the ground 12 m out. Fed belt: from a storage container full of Iron Plates
		// (12 m out and 7 m to the side) into the input, so items arrive as soon as the belt exists.
		FVector StartPoint = InputLocation;
		FVector EndPoint = FindGround(World, InputLocation + Out * 1200.0f, 3000.0f) + FVector(0, 0, 20.0f);
		if (bFeed)
		{
			const FVector Side = FVector::CrossProduct(Out, FVector::UpVector);
			AFGBuildable* Container = SpawnOnGround(World, LoadClass<AFGBuildable>(nullptr, StorageContainerClassPath), InputLocation + Out * 1200.0f + Side * 700.0f, (-Out).Rotation().Yaw);
			AFGBuildableStorage* Storage = Cast<AFGBuildableStorage>(Container);
			UFGFactoryConnectionComponent* Output = nullptr;
			if (Container)
			{
				TInlineComponentArray<UFGFactoryConnectionComponent*> Connections(Container);
				for (UFGFactoryConnectionComponent* Connection : Connections)
				{
					if (Connection && Connection->GetDirection() == EFactoryConnectionDirection::FCD_OUTPUT)
					{
						Output = Connection;
						break;
					}
				}
			}
			UClass* Plate = FeedItem ? FeedItem : LoadClass<UFGItemDescriptor>(nullptr, TEXT("/Game/FactoryGame/Resource/Parts/IronPlate/Desc_IronPlate.Desc_IronPlate_C"));
			const int32 Stocked = Storage && Storage->GetStorageInventory() && Plate ? Storage->GetStorageInventory()->AddStack(FInventoryStack(2000, Plate), true) : 0;
			if (!Output)
			{
				UE_LOG(LogSinkingDepot, Error, TEXT("[dev] BeltCheck %s: storage container or its output missing; feeding skipped"), *Label);
			}
			else
			{
				StartPoint = Output->GetComponentLocation();
				EndPoint = InputLocation;
				UE_LOG(LogSinkingDepot, Log, TEXT("[dev] BeltCheck %s: storage container %s with %d x %s, output at %s"),
					*Label, *Container->GetName(), Stocked, *GetNameSafe(Plate), *StartPoint.ToCompactString());
			}
		}

		const TWeakObjectPtr<AFGBuildable> WeakTarget(Target);
		const TWeakObjectPtr<UFGFactoryConnectionComponent> WeakInput(Input);
		After(World, 1.5f, [Recipe](UWorld* W)
		{
			APlayerController* C = nullptr;
			if (AFGCharacterPlayer* P = LocalPlayer(W, C); P && P->GetBuildGun())
			{
				if (!P->IsBuildGunEquipped())
				{
					P->ToggleBuildGun();
				}
				After(W, 1.0f, [Recipe](UWorld* W2)
				{
					APlayerController* C2 = nullptr;
					if (AFGCharacterPlayer* P2 = LocalPlayer(W2, C2); P2 && P2->GetBuildGun())
					{
						P2->GetBuildGun()->GotoBuildState(Recipe);
					}
				});
			}
		});
		After(World, 5.0f, [StartPoint](UWorld* W)
		{
			APlayerController* C = nullptr;
			if (LocalPlayer(W, C))
			{
				AimAt(C, StartPoint);
			}
		});
		// Aim at the input and advance the belt hologram one build step (start on the input); then aim at the end point
		// and advance again, which builds it. Each advance tries the input paths in turn until the hologram reacts.
		const FVector End = EndPoint;
		After(World, 7.5f, [Label, End, WeakTarget, WeakInput, Centre, Out, OnDone](UWorld* W)
		{
			AdvanceBelt(W, Label, TEXT("start"), 0, WeakInput, [Label, End, WeakTarget, WeakInput, Centre, Out, OnDone](UWorld* W2, bool)
			{
				APlayerController* C = nullptr;
				if (LocalPlayer(W2, C))
				{
					AimAt(C, End);
				}
				After(W2, 2.5f, [Label, WeakTarget, WeakInput, Centre, Out, OnDone](UWorld* W3)
				{
					// The end click may place a pole whose height is set by one more click (build step "adjust pole"),
					// like in normal play; keep advancing until the belt exists (input connected), at most 3 more.
					const TSharedRef<TFunction<void(UWorld*, int32)>> Finish = MakeShared<TFunction<void(UWorld*, int32)>>();
					*Finish = [Label, WeakTarget, WeakInput, Centre, Out, OnDone, Finish](UWorld* W4, const int32 Click)
					{
						if ((WeakInput.IsValid() && WeakInput->IsConnected()) || Click >= 3)
						{
							// The self-reference stays (a few bytes per run of a dev test); clearing it here would destroy
							// the running lambda.
							WatchBuilding(W4, Label, WeakTarget, WeakInput, Centre, Out, OnDone);
							return;
						}
						AdvanceBelt(W4, Label, FString::Printf(TEXT("end click %d"), Click + 1), 0, WeakInput, [Finish, Click](UWorld* W5, bool)
						{
							(*Finish)(W5, Click + 1);
						});
					};
					(*Finish)(W3, 0);
				});
			});
		});
	}

	/** After the belt build: 20 s of state logging and screenshots from a fixed camera, then OnDone. */
	void WatchBuilding(UWorld* W, const FString& Label, const TWeakObjectPtr<AFGBuildable> WeakTarget, const TWeakObjectPtr<UFGFactoryConnectionComponent> WeakInput,
		const FVector Centre, const FVector Out, const TFunction<void()> OnDone)
	{
		const double Start = FPlatformTime::Seconds();
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] BeltCheck %s t=0: watching the building"), *Label);
		// Watch from a fixed camera that sees the building's input side and front.
		AFGBuildable* Target = WeakTarget.Get();
		if (Target)
		{
			const FVector Camera = Centre + Out * 1000.0f + Target->GetActorForwardVector() * 900.0f + FVector(0, 0, 450.0f);
			GShowcaseShots.Reset();
			AddShot(W, TEXT("belt"), Centre, Camera);
			After(W, 0.3f, [](UWorld* W2)
			{
				ViewCamera(W2, GShowcaseShots.Num() ? GShowcaseShots[0].Camera.Get() : nullptr);
			});
		}
		static const float ShotTimes[] = { 0.6f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.5f, 7.0f, 9.0f, 12.0f, 16.0f };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(ShotTimes); ++Index)
		{
			const FString Name = FString::Printf(TEXT("SinkingDepotShowcase_belt_%s_%02d_t%.1f.png"), *Label, Index, ShotTimes[Index]);
			After(W, ShotTimes[Index], [Name](UWorld*) { FScreenshotRequest::RequestScreenshot(Name, false, false); });
		}
		After(W, 2.0f, [Label, WeakInput](UWorld* W2)
		{
			const UFGFactoryConnectionComponent* In = WeakInput.Get();
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] BeltCheck %s: input connected %d (%s)"), *Label, In && In->IsConnected(),
				In && In->GetConnection() ? *GetNameSafe(In->GetConnection()->GetOwner()) : TEXT("-"));
			APlayerController* C = nullptr;
			if (AFGCharacterPlayer* P = LocalPlayer(W2, C); P && P->IsBuildGunEquipped())
			{
				P->ToggleBuildGun();
			}
		});
		MonitorVisualState(W, WeakTarget, Label, Start, Start, MakeShared<TMap<FString, FString>>(), 20.0f, [WeakWorld = TWeakObjectPtr<UWorld>(W), OnDone]()
		{
			if (UWorld* W2 = WeakWorld.Get())
			{
				APlayerController* C = nullptr;
				if (AFGCharacterPlayer* P = LocalPlayer(W2, C))
				{
					C->SetViewTargetWithBlend(P, 0.0f);
				}
			}
			OnDone();
		});
	}

	/** The visible meshes' bounds (not the clearance or connection boxes). */
	FBox VisibleMeshBounds(AActor* Building)
	{
		FBox Box(ForceInit);
		TInlineComponentArray<UMeshComponent*> Meshes(Building);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh->IsVisible() && Mesh->IsRegistered() && !Mesh->IsA<USkeletalMeshComponent>())
			{
				Box += Mesh->Bounds.GetBox();
			}
		}
		return Box;
	}

	/**
	 * Renders only Building (nothing else in the world, no sky or fog) from a direction given in building space, with a
	 * scene capture into a float render target, and writes a 1024 px PNG with a transparent background to the
	 * screenshot folder. The raw HDR colour is tone mapped with a simple exposure + filmic curve. Captured a few
	 * frames in a row first so temporal anti-aliasing settles.
	 */
	void RenderIcon(UWorld* World, AActor* Building, const FString& Name, const FVector& LocalDirection, const float Fov, TFunction<void()> OnDone)
	{
		const FBox Box = VisibleMeshBounds(Building);
		const FVector Direction = Building->GetActorTransform().TransformVectorNoScale(LocalDirection.GetSafeNormal());
		const float Radius = Box.GetExtent().Size();
		const float Distance = Radius / FMath::Sin(FMath::DegreesToRadians(Fov * 0.5f)) * 0.9f;
		const FVector Location = Box.GetCenter() + Direction * Distance;
		ASceneCapture2D* Capture = World->SpawnActor<ASceneCapture2D>(Location, (-Direction).Rotation());
		USceneCaptureComponent2D* Component = Capture ? Capture->GetCaptureComponent2D() : nullptr;
		if (!Component)
		{
			OnDone();
			return;
		}
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Capture);
		Target->RenderTargetFormat = RTF_RGBA16f;
		Target->ClearColor = FLinearColor::Transparent;
		Target->bAutoGenerateMips = false;
		Target->InitAutoFormat(1024, 1024);
		Target->UpdateResourceImmediate(true);
		Component->TextureTarget = Target;
		Component->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
		Component->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Component->ShowOnlyActors.Add(Building);
		Component->FOVAngle = Fov;
		Component->bCaptureEveryFrame = false;
		Component->bCaptureOnMovement = false;
		Component->bAlwaysPersistRenderingState = true;
		Component->ShowFlags.SetAtmosphere(false);
		Component->ShowFlags.SetFog(false);
		Component->ShowFlags.SetCloud(false);
		for (int32 Frame = 0; Frame < 12; ++Frame)
		{
			After(World, 0.05f + Frame * 0.1f, [WeakComponent = TWeakObjectPtr<USceneCaptureComponent2D>(Component)](UWorld*)
			{
				if (USceneCaptureComponent2D* C = WeakComponent.Get())
				{
					C->CaptureScene();
				}
			});
		}
		const TWeakObjectPtr<ASceneCapture2D> WeakCapture(Capture);
		After(World, 1.5f, [WeakCapture, Target, Name, OnDone](UWorld*)
		{
			ASceneCapture2D* Capture = WeakCapture.Get();
			FTextureRenderTargetResource* Resource = Capture ? Target->GameThread_GetRenderTargetResource() : nullptr;
			TArray<FLinearColor> Pixels;
			if (!Resource || !Resource->ReadLinearColorPixels(Pixels) || Pixels.Num() != 1024 * 1024)
			{
				UE_LOG(LogSinkingDepot, Error, TEXT("[dev] Icon %s: reading the render target failed"), *Name);
				OnDone();
				return;
			}
			// Scene colour alpha is 1 - coverage in some engine versions; an empty corner says which.
			const bool bInverted = Pixels[0].A > 0.5f;
			TArray<float> Luminance;
			for (const FLinearColor& Pixel : Pixels)
			{
				const float Coverage = bInverted ? 1.0f - Pixel.A : Pixel.A;
				if (Coverage > 0.95f)
				{
					Luminance.Add(Pixel.GetLuminance() / Coverage);
				}
			}
			Luminance.Sort();
			const float Bright = Luminance.Num() ? FMath::Max(Luminance[FMath::Min(Luminance.Num() - 1, static_cast<int32>(Luminance.Num() * 0.97f))], 1e-4f) : 1.0f;
			const float Exposure = 1.0f / Bright;
			TArray64<FColor> Out;
			Out.SetNumUninitialized(Pixels.Num());
			for (int32 Index = 0; Index < Pixels.Num(); ++Index)
			{
				const FLinearColor& Pixel = Pixels[Index];
				const float Coverage = FMath::Clamp(bInverted ? 1.0f - Pixel.A : Pixel.A, 0.0f, 1.0f);
				if (Coverage < 0.002f)
				{
					Out[Index] = FColor(0, 0, 0, 0);
					continue;
				}
				auto Curve = [Exposure, Coverage](const float Value) { return 1.0f - FMath::Exp(-2.0f * Exposure * Value / Coverage); };
				FLinearColor Mapped(Curve(Pixel.R), Curve(Pixel.G), Curve(Pixel.B), Coverage);
				Out[Index] = Mapped.ToFColor(true);
			}
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(1024, 1024, Out, Png);
			const FString Path = FPaths::ScreenShotDir() / FString::Printf(TEXT("SinkingDepotShowcase_icon_%s.png"), *Name);
			const bool bSaved = FFileHelper::SaveArrayToFile(Png, *Path);
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] Icon %s: corner alpha %.3f, centre alpha %.3f (inverted %d), %d covered pixels, 97th percentile luminance %.4f, saved %d to %s"),
				*Name, Pixels[0].A, Pixels[512 * 1024 + 512].A, bInverted, Luminance.Num(), Bright, bSaved, *Path);
			Capture->Destroy();
			OnDone();
		});
	}

	/** Icon renders: our building idle and with the grinder running, the vanilla uploader for comparison, a few angles. */
	void RenderIcons(UWorld* World, AFGBuildable* Ours, AFGBuildable* Vanilla)
	{
		struct FIconShot
		{
			FString Name;
			TWeakObjectPtr<AFGBuildable> Building;
			FVector Direction;
			bool bActive;
		};
		// Building space: the window faces +X, the belt input is at +Y. Vanilla icons look at the front from above.
		TArray<FIconShot> Shots = {
			{TEXT("vanilla_a"), Vanilla, FVector(1.0f, 0.75f, 0.65f), false},
			{TEXT("ours_a"), Ours, FVector(1.0f, 0.75f, 0.65f), false},
			{TEXT("ours_b"), Ours, FVector(1.0f, -0.75f, 0.65f), false},
			{TEXT("ours_c"), Ours, FVector(1.0f, 0.4f, 0.4f), false},
			{TEXT("ours_a_active"), Ours, FVector(1.0f, 0.75f, 0.65f), true},
			{TEXT("ours_b_active"), Ours, FVector(1.0f, -0.75f, 0.65f), true},
		};
		// Full-resolution textures for the close-up.
		for (AFGBuildable* Building : {Ours, Vanilla})
		{
			if (Building)
			{
				TInlineComponentArray<UMeshComponent*> Meshes(Building);
				for (UMeshComponent* Mesh : Meshes)
				{
					Mesh->PrestreamTextures(60.0f, false);
				}
			}
		}
		const TSharedRef<TFunction<void(int32)>> Next = MakeShared<TFunction<void(int32)>>();
		*Next = [WeakWorld = TWeakObjectPtr<UWorld>(World), Shots, Next](const int32 Index)
		{
			UWorld* W = WeakWorld.Get();
			if (!W)
			{
				return;
			}
			if (Index >= Shots.Num())
			{
				// The launcher waits for this file.
				FScreenshotRequest::RequestScreenshot(TEXT("SinkingDepotShowcase_icons_done.png"), false, false);
				*Next = nullptr; // Break the reference cycle.
				return;
			}
			const FIconShot& Shot = Shots[Index];
			AFGBuildable* Building = Shot.Building.Get();
			if (!Building)
			{
				(*Next)(Index + 1);
				return;
			}
			if (ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(W); Subsystem && SinkingDepot::IsSinkingDepot(Building))
			{
				Subsystem->SetForcedActive(Building, Shot.bActive);
			}
			// Look at it with the player camera too, so texture streaming sees it; then render after the grinder spun up.
			const FVector Centre = VisibleMeshBounds(Building).GetCenter();
			GShowcaseShots.Reset();
			AddShot(W, Shot.Name, Centre, Centre + Building->GetActorTransform().TransformVectorNoScale(Shot.Direction.GetSafeNormal()) * 1500.0f);
			ViewCamera(W, GShowcaseShots[0].Camera.Get());
			After(W, 3.0f, [Shot, Next, Index](UWorld* W2)
			{
				if (AFGBuildable* B = Shot.Building.Get())
				{
					RenderIcon(W2, B, Shot.Name, Shot.Direction, 22.0f, [Next, Index]() { (*Next)(Index + 1); });
				}
			});
		};
		(*Next)(0);
	}

	/**
	 * -SinkingDepotBeltCheck (with -SinkingDepotDevKit): looks into a building flickering when a belt is attached.
	 * 1. builds a Sinking Depot with the build gun (the -SinkingDepotDevBuild path), waits for its build effect, then
	 *    builds a belt into it;
	 * 2. spawns a vanilla uploader and builds a belt into that, for comparison;
	 * 3. spawns a second Sinking Depot (no build effect of its own) and builds a belt into that;
	 * 4. renders the icon candidates (-SinkingDepotIconRender also renders them without the belt steps).
	 */
	void BeltCheck(UWorld* World)
	{
		GOnDevBuilt = [](UWorld* W, AFGBuildable* Built)
		{
			const TWeakObjectPtr<AFGBuildable> WeakBuilt(Built);
			After(W, 12.0f, [WeakBuilt](UWorld* W2)
			{
				AFGBuildable* A = WeakBuilt.Get();
				if (!A)
				{
					return;
				}
				BuildBeltInto(W2, A, TEXT("A_ours_buildgun_fed"), [WeakWorld = TWeakObjectPtr<UWorld>(W2), WeakBuilt]()
				{
					UWorld* W3 = WeakWorld.Get();
					AFGBuildable* A2 = WeakBuilt.Get();
					if (!W3 || !A2)
					{
						return;
					}
					const FVector Forward = A2->GetActorForwardVector();
					AFGBuildable* Vanilla = SpawnOnGround(W3, LoadClass<AFGBuildable>(nullptr, VanillaBuildClassPath), A2->GetActorLocation() + Forward * 3500.0f, A2->GetActorRotation().Yaw);
					AFGBuildable* Ours = SpawnOnGround(W3, LoadClass<AFGBuildable>(nullptr, BuildClassPath), A2->GetActorLocation() - Forward * 3500.0f, A2->GetActorRotation().Yaw);
					const TWeakObjectPtr<AFGBuildable> WeakVanilla(Vanilla), WeakOurs(Ours);
					After(W3, 5.0f, [WeakVanilla, WeakOurs](UWorld* W4)
					{
						if (!WeakVanilla.IsValid())
						{
							return;
						}
						BuildBeltInto(W4, WeakVanilla.Get(), TEXT("B_vanilla_fed"), [WeakWorld = TWeakObjectPtr<UWorld>(W4), WeakVanilla, WeakOurs]()
						{
							UWorld* W5 = WeakWorld.Get();
							if (!W5 || !WeakOurs.IsValid())
							{
								return;
							}
							BuildBeltInto(W5, WeakOurs.Get(), TEXT("C_ours_spawned_fed"), [WeakWorld, WeakVanilla, WeakOurs]()
							{
								if (UWorld* W6 = WeakWorld.Get())
								{
									RenderIcons(W6, WeakOurs.Get(), WeakVanilla.Get());
								}
							}, true);
						}, true);
					});
				}, true);
			});
		};
		DevBuild(World);
	}

	/** -SinkingDepotIconRender without the belt check: spawns one of each and renders the icon candidates. */
	void IconRenderOnly(UWorld* World)
	{
		APlayerController* Controller = nullptr;
		AFGCharacterPlayer* Player = LocalPlayer(World, Controller);
		if (!Player)
		{
			return;
		}
		const FRotator View(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
		const FVector Anchor = FindGround(World, Player->GetActorLocation(), 200000.0f) + View.Vector() * 3000.0f;
		const FVector Right = FRotationMatrix(View).GetUnitAxis(EAxis::Y);
		AFGBuildable* Ours = SpawnOnGround(World, LoadClass<AFGBuildable>(nullptr, BuildClassPath), Anchor - Right * 1500.0f, View.Yaw + 180.0f);
		AFGBuildable* Vanilla = SpawnOnGround(World, LoadClass<AFGBuildable>(nullptr, VanillaBuildClassPath), Anchor + Right * 1500.0f, View.Yaw + 180.0f);
		const TWeakObjectPtr<AFGBuildable> WeakOurs(Ours), WeakVanilla(Vanilla);
		After(World, 5.0f, [WeakOurs, WeakVanilla](UWorld* W) { RenderIcons(W, WeakOurs.Get(), WeakVanilla.Get()); });
	}

	/**
	 * -SinkingDepotRegression: a self-checking run over the shipping code paths, for releases. Every check logs
	 * "[dev] Regression CHECK <name>: PASS|FAIL (<detail>)", and the run ends with "[dev] Regression RESULT: PASS|FAIL"
	 * and the screenshot SinkingDepotShowcase_regression_done.png. A and B are Sinking Depots, V a vanilla uploader,
	 * all spawned as newly placed buildings.
	 * 1. BeginPlay: A's power connector is linked to its power info, its grinder window and default paint are applied;
	 *    V gets neither.
	 * 2. Depot full, from the buffer: exact points for 50 Iron Plate put into A; the grinder runs while it sinks and
	 *    stops afterwards; A draws no power (the power option is off).
	 * 3. Power option on (in memory only; the config file isn't touched) with A unwired: nothing sinks and it draws
	 *    nothing. Off again: it sinks.
	 * 4. Uploader can't keep up, from the belt: B's buffer is filled with Mercer Spheres (the depot is full of them and
	 *    the sink refuses them, so they stay), and a fed belt brings Iron Rods, which must be sunk with none lost.
	 */
	struct FRegression
	{
		int32 Checks = 0;
		int32 Failed = 0;
		void Check(const TCHAR* Name, const bool bPass, const FString& Detail)
		{
			++Checks;
			Failed += bPass ? 0 : 1;
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] Regression CHECK %s: %s (%s)"), Name, bPass ? TEXT("PASS") : TEXT("FAIL"), *Detail);
		}
	};
	FRegression GRegression;

	constexpr const TCHAR* IronPlatePath = TEXT("/Game/FactoryGame/Resource/Parts/IronPlate/Desc_IronPlate.Desc_IronPlate_C");
	constexpr const TCHAR* IronRodPath = TEXT("/Game/FactoryGame/Resource/Parts/IronRod/Desc_IronRod.Desc_IronRod_C");
	constexpr const TCHAR* MercerSpherePath = TEXT("/Game/FactoryGame/Prototype/WAT/Desc_WAT2.Desc_WAT2_C");

	/** Number of grinder window instances on the building; OutSpeed is the first one's Speed parameter (-1 if none). */
	int32 CountGrinders(AActor* Building, float& OutSpeed)
	{
		OutSpeed = -1.0f;
		int32 Count = 0;
		if (!Building)
		{
			return 0;
		}
		TInlineComponentArray<UMeshComponent*> Meshes(Building);
		for (UMeshComponent* Mesh : Meshes)
		{
			for (int32 Slot = 0; Mesh && Slot < Mesh->GetNumMaterials(); ++Slot)
			{
				const UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Slot));
				if (Dynamic && Dynamic->Parent && Dynamic->Parent->GetPathName() == SinkingDepot::GrinderMaterialPath)
				{
					if (Count++ == 0)
					{
						Dynamic->GetScalarParameterValue(FHashedMaterialParameterInfo(FMaterialParameterInfo(FName(TEXT("Speed")))), OutSpeed);
					}
				}
			}
		}
		return Count;
	}

	float TargetDraw(AFGCentralStorageContainer* Building)
	{
		const UFGPowerInfoComponent* Power = Building ? Building->GetPowerInfo() : nullptr;
		return Power ? Power->GetTargetConsumption() : -1.0f;
	}

	FName SwatchName(AFGBuildable* Building)
	{
		const TSubclassOf<UFGFactoryCustomizationDescriptor_Swatch> Swatch = Building ? Building->GetCustomizationData_Native().SwatchDesc : nullptr;
		return Swatch ? Swatch->GetFName() : NAME_None;
	}

	void RegressionFinish(UWorld* World)
	{
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] Regression RESULT: %s (%d of %d checks failed)"), GRegression.Failed == 0 && GRegression.Checks > 0 ? TEXT("PASS") : TEXT("FAIL"),
			GRegression.Failed, GRegression.Checks);
		After(World, 1.0f, [](UWorld*) { FScreenshotRequest::RequestScreenshot(TEXT("SinkingDepotShowcase_regression_done.png"), false, false); });
	}

	/** Step 4: belt-side sinking while the uploader can't keep up (the setting must be on, its default). */
	void RegressionCantKeepUp(UWorld* World, const TWeakObjectPtr<AFGCentralStorageContainer> WeakB)
	{
		AFGCentralStorageContainer* B = WeakB.Get();
		AFGCentralStorageSubsystem* Depot = AFGCentralStorageSubsystem::Get(World);
		UClass* Rod = LoadClass<UFGItemDescriptor>(nullptr, IronRodPath);
		UClass* Mercer = LoadClass<UFGItemDescriptor>(nullptr, MercerSpherePath);
		UFGInventoryComponent* Buffer = B ? B->GetStorageInventory() : nullptr;
		if (!Buffer || !Depot || !Rod || !Mercer)
		{
			GRegression.Check(TEXT("4 setup"), false, TEXT("building B, depot, Iron Rod or Mercer Sphere class missing"));
			RegressionFinish(World);
			return;
		}
		const SinkingDepot::Overflow::FRuntime& Runtime = SinkingDepot::Overflow::Runtime();
		GRegression.Check(TEXT("4 setting on"), Runtime.bSinkWhenUploaderCantKeepUp.load(), TEXT("SinkWhenUploaderCantKeepUp must be on (the default) for this step"));
		const TSubclassOf<UFGItemDescriptor> MercerItem(Mercer);
		const int32 Space = Depot->GetCentralStorageItemLimit(MercerItem) - Depot->GetNumItemsFromCentralStorage(MercerItem);
		const int32 Uploaded = FillDepot(World, MercerItem, Space);
		const int32 Filled = Buffer->AddStack(FInventoryStack(100000, MercerItem), true);
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] Regression 4: %d Mercer Spheres into the depot (now %d of %d), %d into B's buffer"), Uploaded,
			Depot->GetNumItemsFromCentralStorage(MercerItem), Depot->GetCentralStorageItemLimit(MercerItem), Filled);
		After(World, 2.0f, [WeakB, Rod, Mercer](UWorld* W)
		{
			AFGCentralStorageContainer* B2 = WeakB.Get();
			UFGInventoryComponent* Buffer2 = B2 ? B2->GetStorageInventory() : nullptr;
			const bool bBlocked = Buffer2 && !Buffer2->HasEnoughSpaceForItem(FInventoryItem(Rod));
			GRegression.Check(TEXT("4 buffer stays full"), bBlocked, FString::Printf(TEXT("%d Mercer Spheres still in B's buffer"),
				Buffer2 ? Buffer2->GetNumItems(TSubclassOf<UFGItemDescriptor>(Mercer)) : -1));
			if (!bBlocked)
			{
				RegressionFinish(W);
				return;
			}
			const SinkingDepot::Overflow::FRuntime& R = SinkingDepot::Overflow::Runtime();
			const int64 CantBefore = R.SunkCantKeepUp.load();
			const int64 LostBefore = R.Lost.load() + R.ReturnedToBuffer.load();
			BuildBeltInto(W, B2, TEXT("regression"), [WeakB, CantBefore, LostBefore, WeakWorld = TWeakObjectPtr<UWorld>(W)]()
			{
				UWorld* W3 = WeakWorld.Get();
				if (!W3)
				{
					return;
				}
				const SinkingDepot::Overflow::FRuntime& R2 = SinkingDepot::Overflow::Runtime();
				const int64 Sunk = R2.SunkCantKeepUp.load() - CantBefore;
				const int64 Lost = R2.Lost.load() + R2.ReturnedToBuffer.load() - LostBefore;
				const UFGFactoryConnectionComponent* Input = WeakB.IsValid() ? FindInput(WeakB.Get()) : nullptr;
				GRegression.Check(TEXT("4 belt connected"), Input && Input->IsConnected(), TEXT("belt built into B with the build gun"));
				GRegression.Check(TEXT("4 sunk off the belt"), Sunk > 0, FString::Printf(TEXT("%lld Iron Rod sunk because the uploader had no room"), Sunk));
				GRegression.Check(TEXT("4 nothing lost"), Lost == 0, FString::Printf(TEXT("%lld returned or lost after leaving the belt"), Lost));
				RegressionFinish(W3);
			}, true, Rod);
		});
	}

	void Regression(UWorld* World)
	{
		GRegression = FRegression();
		APlayerController* Controller = nullptr;
		AFGCharacterPlayer* Player = LocalPlayer(World, Controller);
		ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World);
		AFGSchematicManager* Schematics = AFGSchematicManager::Get(World);
		if (!Player || !Subsystem || !Schematics)
		{
			GRegression.Check(TEXT("setup"), false, TEXT("player, subsystem or schematic manager missing"));
			RegressionFinish(World);
			return;
		}
		Schematics->GiveAccessToSchematic(Subsystem->GetDepotResearch(), nullptr, ESchematicUnlockFlags::BlockTelemetry);
		const FRotator View(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
		const FVector Anchor = FindGround(World, Player->GetActorLocation(), 200000.0f) + View.Vector() * 3000.0f;
		const FVector Right = FRotationMatrix(View).GetUnitAxis(EAxis::Y);
		UClass* Ours = LoadClass<AFGBuildable>(nullptr, BuildClassPath);
		AFGCentralStorageContainer* A = Cast<AFGCentralStorageContainer>(SpawnOnGround(World, Ours, Anchor - Right * 1600.0f, View.Yaw + 180.0f));
		AFGBuildable* V = SpawnOnGround(World, LoadClass<AFGBuildable>(nullptr, VanillaBuildClassPath), Anchor, View.Yaw + 180.0f);
		AFGCentralStorageContainer* B = Cast<AFGCentralStorageContainer>(SpawnOnGround(World, Ours, Anchor + Right * 3500.0f, View.Yaw + 180.0f));
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] Regression: spawned A %s, V %s, B %s"), *GetNameSafe(A), *GetNameSafe(V), *GetNameSafe(B));
		const TWeakObjectPtr<AFGCentralStorageContainer> WeakA(A), WeakB(B);
		const TWeakObjectPtr<AFGBuildable> WeakV(V);

		// 1. After the build effects (as in the showcase).
		After(World, 12.0f, [WeakA, WeakB, WeakV](UWorld* W)
		{
			AFGCentralStorageContainer* A1 = WeakA.Get();
			AFGBuildable* V1 = WeakV.Get();
			if (!A1 || !V1 || !WeakB.IsValid())
			{
				GRegression.Check(TEXT("1 spawn"), false, TEXT("a test building is missing"));
				RegressionFinish(W);
				return;
			}
			UFGPowerInfoComponent* Info = A1->GetPowerInfo();
			TInlineComponentArray<UFGPowerConnectionComponent*> Connections(A1);
			bool bLinked = Info && Connections.Num() > 0;
			for (const UFGPowerConnectionComponent* Connection : Connections)
			{
				bLinked &= Connection && Connection->GetPowerInfo() == Info;
			}
			GRegression.Check(TEXT("1 power connector linked"), bLinked, FString::Printf(TEXT("%d connector(s), power info %s"), Connections.Num(), Info ? TEXT("present") : TEXT("missing")));
			float Speed = -1.0f;
			const int32 Grinders = CountGrinders(A1, Speed);
			GRegression.Check(TEXT("1 grinder window"), Grinders > 0 && Speed == 0.0f, FString::Printf(TEXT("%d grinder slot(s), Speed %.1f"), Grinders, Speed));
			static const FName Custom(TEXT("SwatchDesc_Custom_C"));
			GRegression.Check(TEXT("1 default paint"), SwatchName(A1) == Custom, SwatchName(A1).ToString());
			float VanillaSpeed = -1.0f;
			GRegression.Check(TEXT("1 vanilla untouched"), CountGrinders(V1, VanillaSpeed) == 0 && SwatchName(V1) != Custom,
				FString::Printf(TEXT("grinder slots %d, swatch %s"), CountGrinders(V1, VanillaSpeed), *SwatchName(V1).ToString()));

			// 2. Depot full, from the buffer.
			AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(W);
			AFGCentralStorageSubsystem* Depot = AFGCentralStorageSubsystem::Get(W);
			UClass* PlateClass = LoadClass<UFGItemDescriptor>(nullptr, IronPlatePath);
			if (!Sink || !Depot || !PlateClass)
			{
				GRegression.Check(TEXT("2 setup"), false, TEXT("sink, depot or Iron Plate class missing"));
				RegressionFinish(W);
				return;
			}
			const TSubclassOf<UFGItemDescriptor> Plate(PlateClass);
			FillDepot(W, Plate, Depot->GetCentralStorageItemLimit(Plate) - Depot->GetNumItemsFromCentralStorage(Plate));
			int32 Points = 0;
			EResourceSinkTrack Track = EResourceSinkTrack::RST_Default;
			Sink->FindResourceSinkPointsForItem(Plate, Points, Track);
			const int64 PointsBefore = Sink->GetNumTotalPoints(EResourceSinkTrack::RST_Default);
			const int64 SunkBefore = SinkingDepot::Overflow::Runtime().SunkDepotFull.load();
			const int32 Added = A1->GetStorageInventory()->AddStack(FInventoryStack(50, Plate), true);
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] Regression 2: depot %d of %d Iron Plate, %d put into A, %d points each"),
				Depot->GetNumItemsFromCentralStorage(Plate), Depot->GetCentralStorageItemLimit(Plate), Added, Points);
			After(W, 0.8f, [WeakA](UWorld* W2)
			{
				AFGCentralStorageContainer* A2 = WeakA.Get();
				const ASinkingDepotSubsystem* S = ASinkingDepotSubsystem::Get(W2);
				float Speed2 = -1.0f;
				CountGrinders(A2, Speed2);
				GRegression.Check(TEXT("2 grinder runs"), S && S->IsGrinderActive(A2) && Speed2 == SinkingDepot::GrinderActiveSpeed,
					FString::Printf(TEXT("active %d, Speed %.1f"), S && S->IsGrinderActive(A2), Speed2));
				GRegression.Check(TEXT("2 no power draw"), TargetDraw(A2) == 0.0f, FString::Printf(TEXT("target %.1f MW with the power option off"), TargetDraw(A2)));
			});
			const TWeakObjectPtr<AFGResourceSinkSubsystem> WeakSink(Sink);
			After(W, 5.0f, [WeakA, WeakB, WeakSink, Plate, Added, Points, PointsBefore, SunkBefore](UWorld* W2)
			{
				AFGCentralStorageContainer* A2 = WeakA.Get();
				AFGResourceSinkSubsystem* S = WeakSink.Get();
				const int64 Gained = S ? S->GetNumTotalPoints(EResourceSinkTrack::RST_Default) - PointsBefore : -1;
				const int64 Sunk = SinkingDepot::Overflow::Runtime().SunkDepotFull.load() - SunkBefore;
				const int32 Left = A2 && A2->GetStorageInventory() ? A2->GetStorageInventory()->GetNumItems(Plate) : -1;
				GRegression.Check(TEXT("2 exact points"), Added == 50 && Sunk == Added && Left == 0 && Gained == static_cast<int64>(Added) * Points,
					FString::Printf(TEXT("sunk %lld of %d, %d left, points %lld, expected %lld"), Sunk, Added, Left, Gained, static_cast<int64>(Added) * Points));
				float Speed2 = -1.0f;
				CountGrinders(A2, Speed2);
				const ASinkingDepotSubsystem* Sub = ASinkingDepotSubsystem::Get(W2);
				GRegression.Check(TEXT("2 grinder stops"), Sub && !Sub->IsGrinderActive(A2) && Speed2 == 0.0f,
					FString::Printf(TEXT("active %d, Speed %.1f"), Sub && Sub->IsGrinderActive(A2), Speed2));

				// 3. Power option on, unwired: blocked. In memory only, restored afterwards.
				SinkingDepot::Overflow::FRuntime& R = SinkingDepot::Overflow::Runtime();
				const bool bOriginal = R.bRequirePower.load();
				R.bRequirePower.store(true);
				After(W2, 1.0f, [WeakA, WeakB, Plate, bOriginal](UWorld* W3)
				{
					AFGCentralStorageContainer* A3 = WeakA.Get();
					const int64 SunkBefore3 = SinkingDepot::Overflow::Runtime().SunkDepotFull.load();
					const int32 Added3 = A3 ? A3->GetStorageInventory()->AddStack(FInventoryStack(50, Plate), true) : 0;
					After(W3, 3.0f, [WeakA, WeakB, Plate, bOriginal, SunkBefore3, Added3](UWorld* W4)
					{
						AFGCentralStorageContainer* A4 = WeakA.Get();
						SinkingDepot::Overflow::FRuntime& R4 = SinkingDepot::Overflow::Runtime();
						const int64 Sunk4 = R4.SunkDepotFull.load() - SunkBefore3;
						const int32 Left4 = A4 ? A4->GetStorageInventory()->GetNumItems(Plate) : -1;
						GRegression.Check(TEXT("3 unpowered blocks"), Added3 == 50 && Sunk4 == 0 && Left4 == 50 && SinkingDepot::Overflow::IsBlockedByPower(A4) && TargetDraw(A4) == 0.0f,
							FString::Printf(TEXT("sunk %lld, %d of %d left, blocked %d, target %.1f MW"), Sunk4, Left4, Added3, SinkingDepot::Overflow::IsBlockedByPower(A4), TargetDraw(A4)));
						R4.bRequirePower.store(false);
						After(W4, 3.0f, [WeakA, WeakB, Plate, bOriginal, SunkBefore3, Added3](UWorld* W5)
						{
							AFGCentralStorageContainer* A5 = WeakA.Get();
							SinkingDepot::Overflow::FRuntime& R5 = SinkingDepot::Overflow::Runtime();
							const int64 Sunk5 = R5.SunkDepotFull.load() - SunkBefore3;
							const int32 Left5 = A5 ? A5->GetStorageInventory()->GetNumItems(Plate) : -1;
							GRegression.Check(TEXT("3 power option off sinks"), Sunk5 == Added3 && Left5 == 0 && TargetDraw(A5) == 0.0f,
								FString::Printf(TEXT("sunk %lld of %d, %d left, target %.1f MW"), Sunk5, Added3, Left5, TargetDraw(A5)));
							R5.bRequirePower.store(bOriginal);
							RegressionCantKeepUp(W5, WeakB);
						});
					});
				});
			});
		});
	}
}

namespace
{
	/**
	 * -SinkingDepotMenuCheck: opens the build menu and searches for the Sinking Depot, for screenshots of its entry and
	 * icon.
	 * - OS-level synthetic input (SendInput, posted messages) never reached gameplay, so the keys are fed in from
	 *   inside the process.
	 * - First choice: FSlateApplication's key entry points, the same path real keyboard input takes (Slate -> game
	 *   viewport -> player input).
	 * - Fallback: APlayerController::InputKey with a simulated key event.
	 * The visible widgets with "BuildMenu" in their class name are logged after each step, so the log says which way
	 * worked.
	 */
	bool LogBuildMenuWidgets(UWorld* World, const TCHAR* Step)
	{
		FString Found;
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			if (It->GetWorld() == World && It->GetClass()->GetName().Contains(TEXT("BuildMenu")) && It->IsVisible() && It->GetCachedWidget().IsValid())
			{
				Found += (Found.IsEmpty() ? TEXT("") : TEXT(", ")) + It->GetClass()->GetName();
			}
		}
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] MenuCheck %s: visible build menu widgets: [%s]"), Step, *Found);
		return !Found.IsEmpty();
	}

	void SlateKey(const FKey& Key, TCHAR Character = 0)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}
		FSlateApplication& Slate = FSlateApplication::Get();
		// Slate user 0 is the local player (the user-index constructors; the input device mapper lives in
		// ApplicationCore, which this module doesn't link).
		constexpr uint32 LocalUser = 0;
		const FModifierKeysState NoModifiers;
		Slate.ProcessKeyDownEvent(FKeyEvent(Key, NoModifiers, LocalUser, false, Character, 0));
		if (Character)
		{
			Slate.ProcessKeyCharEvent(FCharacterEvent(Character, NoModifiers, LocalUser, false));
		}
		Slate.ProcessKeyUpEvent(FKeyEvent(Key, NoModifiers, LocalUser, false, Character, 0));
	}

	/**
	 * Enhanced Input: injects every input action currently mapped to Key (one frame's worth, a press), as if the key
	 * had been pressed. The game binds its controls through Enhanced Input, so this reaches the action itself rather
	 * than depending on key routing. Logs the actions it found.
	 */
	void InjectActionsForKey(UWorld* World, const FKey& Key)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
		UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);
		const UEnhancedPlayerInput* PlayerInput = Controller ? Cast<UEnhancedPlayerInput>(Controller->PlayerInput) : nullptr;
		if (!Input || !PlayerInput)
		{
			UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] MenuCheck: no Enhanced Input subsystem or player input"));
			return;
		}
		TSet<const UInputAction*> Injected;
		for (const FEnhancedActionKeyMapping& Mapping : PlayerInput->GetEnhancedActionMappings())
		{
			if (Mapping.Key == Key && Mapping.Action && !Injected.Contains(Mapping.Action))
			{
				Injected.Add(Mapping.Action);
				UE_LOG(LogSinkingDepot, Log, TEXT("[dev] MenuCheck: injecting %s (mapped to %s)"), *Mapping.Action->GetName(), *Key.ToString());
				Input->InjectInputForAction(Mapping.Action, FInputActionValue(true), {}, {});
			}
		}
		if (Injected.IsEmpty())
		{
			UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] MenuCheck: no input action is mapped to %s"), *Key.ToString());
		}
	}

	/**
	 * A mouse move (and optionally a left click) fed into Slate at a pixel of the 1280x720 game viewport, the same
	 * coordinates as the screenshots. Slate hit-tests the event's own position, so the real OS cursor isn't moved.
	 */
	void SlateMouse(const FVector2D& Pixel, bool bClick)
	{
		const TSharedPtr<SViewport> Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameViewportWidget() : nullptr;
		if (!Viewport.IsValid() || !FSlateApplication::IsInitialized())
		{
			return;
		}
		const FGeometry& Geometry = Viewport->GetCachedGeometry();
		const FVector2D Absolute = FVector2D(Geometry.GetAbsolutePosition()) + FVector2D(Geometry.GetAbsoluteSize()) * (Pixel / FVector2D(1280.0, 720.0));
		FSlateApplication& Slate = FSlateApplication::Get();
		const FModifierKeysState NoModifiers;
		const TSet<FKey> NoButtons;
		const TSet<FKey> LeftDown = {EKeys::LeftMouseButton};
		Slate.ProcessMouseMoveEvent(FPointerEvent(0, 0, Absolute, Absolute, NoButtons, EKeys::Invalid, 0.0f, NoModifiers));
		if (bClick)
		{
			Slate.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, 0, Absolute, Absolute, LeftDown, EKeys::LeftMouseButton, 0.0f, NoModifiers));
			Slate.ProcessMouseButtonUpEvent(FPointerEvent(0, 0, Absolute, Absolute, NoButtons, EKeys::LeftMouseButton, 0.0f, NoModifiers));
		}
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] MenuCheck: mouse %s at pixel %s (absolute %s)"), bClick ? TEXT("click") : TEXT("move"), *Pixel.ToString(), *Absolute.ToString());
	}

	void MenuShot(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("SinkingDepotShowcase_%s.png"), Name), true, false);
	}

	void MenuCheck(UWorld* World)
	{
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		auto At = [World, WeakWorld](float Seconds, TFunction<void(UWorld*)> Step)
		{
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [WeakWorld, Step]()
			{
				if (UWorld* W = WeakWorld.Get())
				{
					Step(W);
				}
			}), Seconds, false);
		};
		// Shared between the steps: whether the build menu has been seen open.
		TSharedRef<bool> bOpen = MakeShared<bool>(false);
		At(0.1f, [bOpen](UWorld* W)
		{
			if (APlayerController* C = W->GetFirstPlayerController())
			{
				C->SetViewTargetWithBlend(C->GetPawn(), 0.0f); // Back from any showcase camera.
			}
			LogBuildMenuWidgets(W, TEXT("before"));
			// An off-screen, inactive window has no keyboard focus, and Slate routes keys along the focus path. Hand
			// focus to the game viewport first.
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().SetAllUserFocusToGameViewport();
			}
			SlateKey(EKeys::Q);
		});
		At(3.0f, [bOpen](UWorld* W)
		{
			*bOpen = LogBuildMenuWidgets(W, TEXT("after Slate Q"));
			MenuShot(TEXT("menu_1_slate_q"));
		});
		At(4.5f, [bOpen](UWorld* W)
		{
			if (!*bOpen)
			{
				InjectActionsForKey(W, EKeys::Q);
			}
		});
		At(7.0f, [bOpen](UWorld* W)
		{
			*bOpen = LogBuildMenuWidgets(W, TEXT("after injected Q action")) || *bOpen;
			MenuShot(TEXT("menu_2_injected_q"));
		});
		// Pixel positions are from a 1280x720 shot of the open menu: search box at (560, 113), the first
		// result tile where "The HUB" sits at (341, 190), the Organization category at (248, 340).
		At(8.5f, [](UWorld* W)
		{
			SlateMouse(FVector2D(560, 113), true); // focus the search box
		});
		At(9.5f, [](UWorld* W)
		{
			for (const TCHAR Character : FString(TEXT("sinking")))
			{
				SlateKey(FKey(*FString::Chr(FChar::ToUpper(Character))), Character);
			}
		});
		At(12.0f, [](UWorld* W)
		{
			LogBuildMenuWidgets(W, TEXT("after typing"));
			MenuShot(TEXT("menu_3_search"));
		});
		At(13.0f, [](UWorld* W)
		{
			SlateMouse(FVector2D(341, 190), false); // hover the first result for its details
		});
		At(15.0f, [](UWorld* W)
		{
			MenuShot(TEXT("menu_4_hover"));
		});
		At(16.0f, [](UWorld* W)
		{
			SlateMouse(FVector2D(821, 113), true); // clear the search (the X at the right of the search box)
		});
		At(17.0f, [](UWorld* W)
		{
			SlateMouse(FVector2D(248, 340), true); // Organization category
		});
		At(19.5f, [](UWorld* W)
		{
			MenuShot(TEXT("menu_5_organization"));
		});
		UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotMenuCheck: opening the build menu"));
	}
}

namespace
{
	/** -SinkingDepotClientCheck: what this client sees of the Sinking Depots the host or server built. */
	void ClientReport(UWorld* World)
	{
		UClass* OwnSchematic = LoadClass<UFGSchematic>(nullptr, SinkingDepot::SchematicClassPath);
		AFGSchematicManager* Schematics = AFGSchematicManager::Get(World);
		ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World);
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ClientCheck: net mode %d, subsystem replicated: %d, schematic purchased on this client: %d"),
			static_cast<int32>(World->GetNetMode()), Subsystem != nullptr,
			Schematics && OwnSchematic ? Schematics->IsSchematicPurchased(OwnSchematic) : -1);
		GShowcaseShots.Reset();
		int32 Index = 0;
		for (TActorIterator<AFGBuildable> It(World); It; ++It)
		{
			if (!SinkingDepot::IsSinkingDepot(*It))
			{
				continue;
			}
			const FFactoryCustomizationData& Paint = It->GetCustomizationData_Native();
			FString Materials;
			for (UActorComponent* Component : It->GetComponents())
			{
				if (const UMeshComponent* Mesh = Cast<UMeshComponent>(Component); Mesh && Component->GetName() == TEXT("MainMesh"))
				{
					for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
					{
						Materials += (Slot ? TEXT(", ") : TEXT("")) + GetNameSafe(Mesh->GetMaterial(Slot));
					}
				}
			}
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ClientCheck: %s: grinder active %d; paint swatch %s, primary %s, secondary %s; MainMesh materials [%s]"),
				*It->GetName(), Subsystem ? Subsystem->IsGrinderActive(*It) : -1, Paint.SwatchDesc ? *Paint.SwatchDesc->GetName() : TEXT("(none)"),
				*Paint.OverrideColorData.PrimaryColor.ToString(), *Paint.OverrideColorData.SecondaryColor.ToString(), *Materials);
			if (Index < 4)
			{
				// Front (window) and back (power stub), in building space: window faces +X, stub at (-205, -280, 270).
				const FTransform Frame = It->GetActorTransform();
				AddShot(World, FString::Printf(TEXT("client_%d_front"), Index), Frame.TransformPosition(FVector(140, 0, 250)), Frame.TransformPosition(FVector(1000, 300, 450)));
				AddShot(World, FString::Printf(TEXT("client_%d_back"), Index), Frame.TransformPosition(FVector(-150, -200, 260)), Frame.TransformPosition(FVector(-900, -700, 450)));
			}
			++Index;
		}
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ClientCheck: %d Sinking Depot(s) replicated to this client"), Index);
		for (int32 Shot = 0; Shot < GShowcaseShots.Num(); ++Shot)
		{
			FTimerHandle SwitchHandle;
			World->GetTimerManager().SetTimer(SwitchHandle, FTimerDelegate::CreateWeakLambda(World, [World, Shot]()
			{
				ViewCamera(World, GShowcaseShots[Shot].Camera.Get());
			}), 1.0f + Shot * 3.0f, false);
			FTimerHandle ShotHandle;
			World->GetTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateWeakLambda(World, [Shot, Last = Shot == GShowcaseShots.Num() - 1]()
			{
				FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("SinkingDepotShowcase_%s.png"), *GShowcaseShots[Shot].Name), false, false);
				if (Last)
				{
					UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ClientCheck: done"));
				}
			}), 2.5f + Shot * 3.0f, false);
		}
	}

	/**
	 * Waits (polling every 3 s, up to 15 min) until a Sinking Depot has replicated, then waits for the host's showcase
	 * to finish (forced-active grinder, build effects) and reports.
	 */
	void ClientCheckPoll(TWeakObjectPtr<UWorld> WeakWorld, int32 Polls)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return;
		}
		bool bFound = false;
		for (TActorIterator<AFGBuildable> It(World); It && !bFound; ++It)
		{
			bFound = SinkingDepot::IsSinkingDepot(*It);
		}
		FTimerHandle Handle;
		if (bFound)
		{
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ClientCheck: a Sinking Depot has replicated; reporting in 40 s"));
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [WeakWorld]()
			{
				if (UWorld* W = WeakWorld.Get())
				{
					ClientReport(W);
				}
			}), 40.0f, false);
		}
		else if (Polls < 300)
		{
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [WeakWorld, Polls]()
			{
				ClientCheckPoll(WeakWorld, Polls + 1);
			}), 3.0f, false);
		}
		else
		{
			UE_LOG(LogSinkingDepot, Error, TEXT("[dev] ClientCheck: no Sinking Depot replicated within 15 min"));
		}
	}

	/**
	 * -SinkingDepotServerShowcase (dedicated server): no local player, so wait (every 3 s, up to 30 min) for a joined
	 * player's pawn, then spawn the showcase row near it. Screenshots are skipped, since the server doesn't render; the
	 * client takes them with -SinkingDepotClientCheck. The showcase's exact points check still runs and logs PASS/FAIL.
	 */
	void ServerShowcasePoll(TWeakObjectPtr<UWorld> WeakWorld, int32 Polls)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return;
		}
		APlayerController* Controller = World->GetFirstPlayerController();
		if (Controller && Controller->GetPawn())
		{
			UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotServerShowcase: player %s joined; spawning the showcase"), *GetNameSafe(Controller->GetPawn()));
			Showcase(World, *GLog);
			GShowcaseShots.Reset(); // The cameras stay as idle actors; the server doesn't take screenshots.
			return;
		}
		if (Polls < 600)
		{
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [WeakWorld, Polls]()
			{
				ServerShowcasePoll(WeakWorld, Polls + 1);
			}), 3.0f, false);
		}
	}
}

namespace
{
	/**
	 * Checks for "convert to vanilla uploaders" and its undo (SinkingDepotConvert.cpp). Every check logs
	 * "[dev] ConvertCheck CHECK <name>: PASS|FAIL (<detail>)"; a run ends with "[dev] ConvertCheck RESULT: PASS|FAIL" and
	 * the screenshot SinkingDepotShowcase_<marker>.png, which the launcher scripts wait for.
	 *
	 * -SinkingDepotConvertCheck (single player or listen host), in two parts with a save and reload in between:
	 *  Part 1: five Sinking Depots: C1 fed directly by a storage container of Iron Plate, C2 wired to a power pole and
	 *  to C1, C3 painted with swatch slot 5, C4 holding 20 Mercer Spheres (the depot is full of them and the sink refuses
	 *  them, so they stay put), C5 inside a blueprint designer. Saves SinkingDepot_ConvFixture, then converts through the
	 *  chat command as the host, undoes through the console command, checks that "convert" without a choice changes
	 *  nothing, converts with a backup save, saves SinkingDepot_ConvCheck_s1 and loads it.
	 *  Part 2 (after the reload): the undo record survived; undo through the chat command; convert, dismantle one, undo
	 *  (skipped); converts again and saves SinkingDepot_ConvFinal for the "load without the mod" test.
	 * -SinkingDepotConvertHost (listen host) with -SinkingDepotClientConvertTry (joined client): the client's chat
	 *  command must be refused; then the host's works.
	 * -SinkingDepotConvertDS (dedicated server): spawns two wired Sinking Depots; the test script drives the console
	 *  commands through the server API.
	 */
	struct FConvertCheck
	{
		int32 Checks = 0;
		int32 Failed = 0;
		void Check(const TCHAR* Name, const bool bPass, const FString& Detail)
		{
			++Checks;
			Failed += bPass ? 0 : 1;
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ConvertCheck CHECK %s: %s (%s)"), Name, bPass ? TEXT("PASS") : TEXT("FAIL"), *Detail);
		}
	};
	FConvertCheck GConvert;

	/** Survives the save/load travel (module memory): 0 = fresh world, 1 = after the reload. */
	int32 GConvertPhase = 0;
	/** Where the test buildings stood, to find whatever stands there after a swap or a reload. */
	FVector GConvertLoc[5];
	FVector GConvertPoleLoc;
	FVector GConvertStorageLoc;
	FVector GConvertDesignerLoc;
	FVector GConvertPole2Loc;
	TArray<FString> GConvertLines;

	constexpr const TCHAR* StorageContainerPath = TEXT("/Game/FactoryGame/Buildable/Factory/StorageContainerMk1/Build_StorageContainerMk1.Build_StorageContainerMk1_C");
	constexpr const TCHAR* PowerPolePath = TEXT("/Game/FactoryGame/Buildable/Factory/PowerPoleMk1/Build_PowerPoleMk1.Build_PowerPoleMk1_C");
	constexpr const TCHAR* Slot5SwatchPath = TEXT("/Game/FactoryGame/Buildable/-Shared/Customization/Swatches/SwatchDesc_Slot5.SwatchDesc_Slot5_C");
	constexpr const TCHAR* DesignerPath = TEXT("/Game/FactoryGame/Buildable/Factory/BlueprintDesigner/Build_BlueprintDesigner.Build_BlueprintDesigner_C");
	constexpr int32 ConvertMercerCount = 20;

	const SinkingDepot::Convert::FCommandNames ConvertTestNames{TEXT("SinkingDepot.ConvertAll"), TEXT("SinkingDepot.UndoConvert")};

	SinkingDepot::Convert::FReporter CaptureReporter()
	{
		SinkingDepot::Convert::FReporter Reporter;
		Reporter.bCanSayLater = true;
		Reporter.Say = [](const FString& Line) { GConvertLines.Add(Line); };
		return Reporter;
	}

	bool LinesContain(const TCHAR* Text)
	{
		return GConvertLines.ContainsByPredicate([Text](const FString& Line) { return Line.Contains(Text); });
	}

	void ConvertFinish(UWorld* World, const TCHAR* Marker)
	{
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ConvertCheck RESULT: %s (%d of %d checks failed)"), GConvert.Failed == 0 && GConvert.Checks > 0 ? TEXT("PASS") : TEXT("FAIL"),
			GConvert.Failed, GConvert.Checks);
		const FString Shot = FString::Printf(TEXT("SinkingDepotShowcase_%s.png"), Marker);
		After(World, 1.0f, [Shot](UWorld*) { FScreenshotRequest::RequestScreenshot(Shot, false, false); });
	}

	UFGFactoryConnectionComponent* FindOutput(AActor* Actor)
	{
		TInlineComponentArray<UFGFactoryConnectionComponent*> Connections(Actor);
		for (UFGFactoryConnectionComponent* Connection : Connections)
		{
			if (Connection && Connection->GetDirection() == EFactoryConnectionDirection::FCD_OUTPUT)
			{
				return Connection;
			}
		}
		return nullptr;
	}

	/** The building of this kind standing within 50 cm of Location, or null. */
	template <typename T>
	T* BuildingAt(UWorld* World, const FVector& Location, TFunctionRef<bool(T*)> Filter)
	{
		for (TActorIterator<T> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed() && FVector::Dist(It->GetActorLocation(), Location) < 50.0f && Filter(*It))
			{
				return *It;
			}
		}
		return nullptr;
	}

	AFGCentralStorageContainer* UploaderAt(UWorld* World, const FVector& Location, const bool bSinkingDepot)
	{
		return BuildingAt<AFGCentralStorageContainer>(World, Location, [bSinkingDepot](AFGCentralStorageContainer* C) { return SinkingDepot::IsSinkingDepot(C) == bSinkingDepot; });
	}

	UFGCircuitConnectionComponent* ConvConnector(AActor* Actor)
	{
		if (const AFGBuildablePowerPole* Pole = Cast<AFGBuildablePowerPole>(Actor))
		{
			return Pole->GetPowerConnections().Num() > 0 ? Pole->GetPowerConnections()[0] : nullptr;
		}
		return Actor ? Actor->FindComponentByClass<UFGPowerConnectionComponent>() : nullptr;
	}

	int32 ConvWireCount(AActor* Actor)
	{
		const UFGCircuitConnectionComponent* Found = ConvConnector(Actor);
		return Found ? Found->GetNumConnections() : -1;
	}

	bool ConvWire(UWorld* World, AActor* From, AActor* To, AFGBuildableBlueprintDesigner* Designer = nullptr)
	{
		AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(World);
		UClass* WireClass = LoadClass<AFGBuildableWire>(nullptr, SinkingDepot::PowerLineClassPath);
		UFGCircuitConnectionComponent* A = ConvConnector(From);
		UFGCircuitConnectionComponent* B = ConvConnector(To);
		// Like the build gun: no wire onto a full connector (a Sinking Depot takes one wire, like a machine).
		if (!Buildables || !WireClass || !A || !B || A->GetNumFreeConnections() <= 0 || B->GetNumFreeConnections() <= 0)
		{
			return false;
		}
		const FTransform Transform(A->GetComponentLocation());
		AFGBuildableWire* NewWire = Cast<AFGBuildableWire>(Buildables->BeginSpawnBuildable(WireClass, Transform));
		if (!NewWire)
		{
			return false;
		}
		NewWire->SetBuiltWithRecipe(LoadClass<UFGRecipe>(nullptr, SinkingDepot::PowerLineRecipePath));
		if (Designer)
		{
			NewWire->SetInsideBlueprintDesigner(Designer);
		}
		const bool bConnected = NewWire->Connect(A, B);
		NewWire->FinishSpawning(Transform);
		if (Designer && !Designer->GetBuildablesInBlueprintDesigner().Contains(NewWire))
		{
			Designer->OnBuildableConstructedInsideDesigner(NewWire);
		}
		return bConnected;
	}

	int32 ItemsIn(AFGCentralStorageContainer* Building, UClass* Item)
	{
		UFGInventoryComponent* Inventory = Building ? Building->GetStorageInventory() : nullptr;
		return Inventory ? Inventory->GetNumItems(TSubclassOf<UFGItemDescriptor>(Item)) : -1;
	}

	FName RecipeName(const AFGBuildable* Building)
	{
		return Building && Building->GetBuiltWithRecipe() ? Building->GetBuiltWithRecipe()->GetFName() : NAME_None;
	}

	/** Sends a chat command as the local player, the way SML's chat hook does (server RPC; runs directly on the host). */
	bool SendChatCommand(UWorld* World, const TCHAR* CommandLine)
	{
		AFGPlayerController* Controller = Cast<AFGPlayerController>(World->GetFirstPlayerController());
		USMLRemoteCallObject* Remote = Controller ? Controller->GetRemoteCallObjectOfClass<USMLRemoteCallObject>() : nullptr;
		if (!Remote)
		{
			return false;
		}
		Remote->HandleChatCommand(CommandLine);
		return true;
	}

	/** -SinkingDepotConvertBP: saves the test designer as a blueprint through the game's own save RPC (as the UI does). */
	void SaveTestBlueprint(UWorld* World, const TCHAR* Name)
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotConvertBP")))
		{
			return;
		}
		AFGPlayerController* Controller = Cast<AFGPlayerController>(World->GetFirstPlayerController());
		UFGBlueprintRemoteCallObject* Remote = Controller ? Controller->GetRemoteCallObjectOfClass<UFGBlueprintRemoteCallObject>() : nullptr;
		AFGBuildableBlueprintDesigner* Designer = BuildingAt<AFGBuildableBlueprintDesigner>(World, GConvertDesignerLoc, [](AFGBuildableBlueprintDesigner*) { return true; });
		if (!Remote || !Designer)
		{
			GConvert.Check(TEXT("blueprint save"), false, TEXT("no blueprint remote call object or designer"));
			return;
		}
		FBlueprintRecord Record;
		Record.BlueprintName = Name;
		FBlueprintCategoryRecord Category;
		Category.CategoryName = TEXT("Undefined");
		Category.IsUndefined = true;
		FBlueprintSubCategoryRecord SubCategory;
		SubCategory.SubCategoryName = TEXT("Undefined");
		SubCategory.IsUndefined = true;
		Remote->Server_SaveBlueprintInDesigner(Designer, Controller, Record, Category, SubCategory);
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ConvertCheck: requested blueprint save '%s' (%d building(s) in the designer)"), Name, Designer->GetBuildablesInBlueprintDesigner().Num());
	}

	/** Saves the game under Name and calls Then (on the game thread) once the file is written. */
	void SaveThen(UWorld* World, const FString& Name, TFunction<void(UWorld*, bool)> Then)
	{
		UFGSaveSession* Session = UFGSaveSession::Get(World);
		if (!Session)
		{
			Then(World, false);
			return;
		}
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		Session->SaveGame(Name, FOnSaveGameComplete::CreateLambda([WeakWorld, Then](const bool bSuccess, const FText&, void*)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakWorld, Then, bSuccess]()
			{
				if (UWorld* W = WeakWorld.Get())
				{
					After(W, 1.0f, [Then, bSuccess](UWorld* W2) { Then(W2, bSuccess); });
				}
			});
		}), nullptr);
	}

	/** Facts about the five test spots, for checks after a swap. */
	struct FConvertState
	{
		int32 Ours = 0;
		int32 Vanilla = 0;
		AFGCentralStorageContainer* At[5] = {};
	};

	FConvertState ReadState(UWorld* World, const bool bSinkingDepots)
	{
		FConvertState State;
		State.Ours = GetUploaders(World, true).Num();
		State.Vanilla = GetUploaders(World, false).Num();
		for (int32 Index = 0; Index < 5; ++Index)
		{
			State.At[Index] = UploaderAt(World, GConvertLoc[Index], bSinkingDepots);
		}
		return State;
	}

	/** Shared checks after a swap; Prefix names the step, bSinkingDepots says what should stand at the five spots. */
	void CheckSwapped(UWorld* World, const TCHAR* Prefix, const bool bSinkingDepots, const int32 ExpectedWiresOnPole, const bool bWithDesigner)
	{
		const FConvertState State = ReadState(World, bSinkingDepots);
		int32 Present = 0;
		for (int32 Index = 0; Index < 5; ++Index)
		{
			Present += State.At[Index] ? 1 : 0;
		}
		const int32 Expected = bWithDesigner ? 5 : 4;
		GConvert.Check(*FString::Printf(TEXT("%s all swapped"), Prefix), Present >= Expected && (bSinkingDepots ? State.Ours >= Expected : State.Ours == 0),
			FString::Printf(TEXT("%d of 5 spots hold a %s; %d Sinking Depot(s) and %d vanilla uploader(s) in the world"), Present,
				bSinkingDepots ? TEXT("Sinking Depot") : TEXT("vanilla uploader"), State.Ours, State.Vanilla));

		const FName WantedRecipe = bSinkingDepots ? FName(TEXT("Recipe_SinkingDepot_C")) : FName(TEXT("Recipe_CentralStorage_C"));
		bool bRecipes = true;
		for (int32 Index = 0; Index < 5; ++Index)
		{
			bRecipes &= !State.At[Index] || RecipeName(State.At[Index]) == WantedRecipe;
		}
		GConvert.Check(*FString::Printf(TEXT("%s built-with recipe"), Prefix), bRecipes, WantedRecipe.ToString());

		AFGBuildable* Storage = BuildingAt<AFGBuildable>(World, GConvertStorageLoc, [](AFGBuildable* B) { return B->IsA<AFGBuildableStorage>() && !B->IsA<AFGCentralStorageContainer>(); });
		const UFGFactoryConnectionComponent* Input = State.At[0] ? FindInput(State.At[0]) : nullptr;
		GConvert.Check(*FString::Printf(TEXT("%s belt kept"), Prefix), Input && Storage && Input->GetConnection() == FindOutput(Storage),
			FString::Printf(TEXT("C1 input connected to %s"), Input && Input->GetConnection() ? *GetNameSafe(Input->GetConnection()->GetOwner()) : TEXT("nothing")));

		AFGBuildable* Pole = BuildingAt<AFGBuildable>(World, GConvertPoleLoc, [](AFGBuildable* B) { return B->IsA<AFGBuildablePowerPole>(); });
		GConvert.Check(*FString::Printf(TEXT("%s pole wires"), Prefix), ConvWireCount(Pole) == ExpectedWiresOnPole,
			FString::Printf(TEXT("pole has %d wire(s), expected %d"), ConvWireCount(Pole), ExpectedWiresOnPole));
		if (bSinkingDepots)
		{
			GConvert.Check(*FString::Printf(TEXT("%s depot wires"), Prefix), ConvWireCount(State.At[1]) == 1 && ConvWireCount(State.At[3]) == ExpectedWiresOnPole,
				FString::Printf(TEXT("C2 has %d wire(s) (to C1), C4 has %d (to the pole), expected 1 and %d"), ConvWireCount(State.At[1]), ConvWireCount(State.At[3]), ExpectedWiresOnPole));
		}
		else
		{
			GConvert.Check(*FString::Printf(TEXT("%s no power connector"), Prefix), State.At[1] && !ConvConnector(State.At[1]), TEXT("a vanilla uploader has no power connector"));
		}

		UClass* Mercer = LoadClass<UFGItemDescriptor>(nullptr, MercerSpherePath);
		GConvert.Check(*FString::Printf(TEXT("%s buffer kept"), Prefix), ItemsIn(State.At[3], Mercer) == ConvertMercerCount || (!State.At[3] && !bWithDesigner),
			FString::Printf(TEXT("C4 holds %d Mercer Sphere(s), expected %d"), ItemsIn(State.At[3], Mercer), ConvertMercerCount));

		if (bWithDesigner)
		{
			AFGBuildableBlueprintDesigner* Designer = BuildingAt<AFGBuildableBlueprintDesigner>(World, GConvertDesignerLoc, [](AFGBuildableBlueprintDesigner*) { return true; });
			const bool bInside = Designer && State.At[4] && Designer->GetBuildablesInBlueprintDesigner().Contains(State.At[4]) && State.At[4]->GetBlueprintDesigner() == Designer;
			int32 Stale = 0;
			if (Designer)
			{
				for (const AFGBuildable* Listed : Designer->GetBuildablesInBlueprintDesigner())
				{
					Stale += IsValid(Listed) ? 0 : 1;
				}
			}
			AFGBuildable* Pole2 = BuildingAt<AFGBuildable>(World, GConvertPole2Loc, [](AFGBuildable* B) { return B->IsA<AFGBuildablePowerPole>(); });
			GConvert.Check(*FString::Printf(TEXT("%s designer wire"), Prefix), ConvWireCount(Pole2) == (bSinkingDepots ? 1 : 0),
				FString::Printf(TEXT("the pole inside the designer has %d wire(s)"), ConvWireCount(Pole2)));
			GConvert.Check(*FString::Printf(TEXT("%s designer list"), Prefix), bInside && Stale == 0,
				FString::Printf(TEXT("designer %s, lists %d building(s), %d stale; C5 inside: %d"), Designer ? TEXT("found") : TEXT("MISSING"),
					Designer ? Designer->GetBuildablesInBlueprintDesigner().Num() : -1, Stale, bInside));
		}
	}

	/** Paint is checked a few seconds after a swap, after the "new building" default paint would have run. */
	void CheckPaintNow(UWorld* World, const FString& Name, const bool bSinkingDepots)
	{
		AFGCentralStorageContainer* C3 = UploaderAt(World, GConvertLoc[2], bSinkingDepots);
		GConvert.Check(*Name, SwatchName(C3) == FName(TEXT("SwatchDesc_Slot5_C")), SwatchName(C3).ToString());
	}

	void CheckPaintLater(UWorld* World, const TCHAR* Prefix, const bool bSinkingDepots)
	{
		const FString Name = FString::Printf(TEXT("%s paint kept"), Prefix);
		After(World, 4.0f, [Name, bSinkingDepots](UWorld* W) { CheckPaintNow(W, Name, bSinkingDepots); });
	}

	void ConvertPart2(UWorld* World);

	void ConvertPart1(UWorld* World)
	{
		GConvert = FConvertCheck();
		APlayerController* Controller = nullptr;
		AFGCharacterPlayer* Player = LocalPlayer(World, Controller);
		ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World);
		AFGSchematicManager* Schematics = AFGSchematicManager::Get(World);
		UClass* Ours = LoadClass<AFGBuildable>(nullptr, BuildClassPath);
		UClass* Mercer = LoadClass<UFGItemDescriptor>(nullptr, MercerSpherePath);
		UClass* Plate = LoadClass<UFGItemDescriptor>(nullptr, IronPlatePath);
		if (!Player || !Subsystem || !Schematics || !Ours || !Mercer || !Plate)
		{
			GConvert.Check(TEXT("setup"), false, TEXT("player, subsystem, schematic manager or a class missing"));
			ConvertFinish(World, TEXT("convert_done"));
			return;
		}
		Schematics->GiveAccessToSchematic(Subsystem->GetDepotResearch(), nullptr, ESchematicUnlockFlags::BlockTelemetry);
		const FRotator View(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
		const FVector Forward = View.Vector();
		const FVector Right = FRotationMatrix(View).GetUnitAxis(EAxis::Y);
		const FVector Anchor = FindGround(World, Player->GetActorLocation(), 200000.0f) + Forward * 3000.0f;
		AFGCentralStorageContainer* C[4];
		for (int32 Index = 0; Index < 4; ++Index)
		{
			C[Index] = Cast<AFGCentralStorageContainer>(SpawnOnGround(World, Ours, Anchor + Right * ((Index - 1.5f) * 1500.0f), View.Yaw + 180.0f));
			GConvertLoc[Index] = C[Index] ? C[Index]->GetActorLocation() : FVector::ZeroVector;
		}
		AFGBuildable* Pole = SpawnOnGround(World, LoadClass<AFGBuildable>(nullptr, PowerPolePath), Anchor + Right * (-750.0f) + Forward * 900.0f, View.Yaw);
		AFGBuildable* Storage = SpawnOnGround(World, LoadClass<AFGBuildable>(nullptr, StorageContainerPath), Anchor + Right * (-2250.0f) + Forward * 1200.0f, View.Yaw);
		AFGBuildable* Designer = SpawnOnGround(World, LoadClass<AFGBuildable>(nullptr, DesignerPath), Anchor + Forward * 7000.0f, View.Yaw);
		AFGBuildableBlueprintDesigner* DesignerTyped = Cast<AFGBuildableBlueprintDesigner>(Designer);
		GConvertPoleLoc = Pole ? Pole->GetActorLocation() : FVector::ZeroVector;
		GConvertStorageLoc = Storage ? Storage->GetActorLocation() : FVector::ZeroVector;
		GConvertDesignerLoc = Designer ? Designer->GetActorLocation() : FVector::ZeroVector;

		// C5 inside the designer, the way a building placed in a designer is: flagged before BeginPlay, then listed.
		AFGCentralStorageContainer* C5 = nullptr;
		if (DesignerTyped)
		{
			const FTransform Transform(FRotator(0.0f, View.Yaw, 0.0f), Designer->GetActorLocation() + FVector(0.0f, 0.0f, 150.0f));
			AFGBuildable* Spawned = AFGBuildableSubsystem::Get(World)->BeginSpawnBuildable(Ours, Transform);
			if (Spawned)
			{
				Spawned->SetInsideBlueprintDesigner(DesignerTyped);
				Spawned->FinishSpawning(Transform);
				if (!DesignerTyped->GetBuildablesInBlueprintDesigner().Contains(Spawned))
				{
					DesignerTyped->OnBuildableConstructedInsideDesigner(Spawned);
				}
				C5 = Cast<AFGCentralStorageContainer>(Spawned);
			}
		}
		GConvertLoc[4] = C5 ? C5->GetActorLocation() : FVector::ZeroVector;
		if (DesignerTyped)
		{
			const FTransform PoleTransform(FRotator(0.0f, View.Yaw, 0.0f), Designer->GetActorLocation() + Right * 700.0f + FVector(0.0f, 0.0f, 150.0f));
			AFGBuildable* Pole2 = AFGBuildableSubsystem::Get(World)->BeginSpawnBuildable(LoadClass<AFGBuildable>(nullptr, PowerPolePath), PoleTransform);
			if (Pole2)
			{
				Pole2->SetInsideBlueprintDesigner(DesignerTyped);
				Pole2->FinishSpawning(PoleTransform);
				if (!DesignerTyped->GetBuildablesInBlueprintDesigner().Contains(Pole2))
				{
					DesignerTyped->OnBuildableConstructedInsideDesigner(Pole2);
				}
				GConvertPole2Loc = Pole2->GetActorLocation();
			}
		}
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ConvertCheck: spawned C1-C4 %s %s %s %s, C5 %s in %s, pole %s, storage %s"), *GetNameSafe(C[0]), *GetNameSafe(C[1]),
			*GetNameSafe(C[2]), *GetNameSafe(C[3]), *GetNameSafe(C5), *GetNameSafe(Designer), *GetNameSafe(Pole), *GetNameSafe(Storage));

		// Fill and wire after the default paint has run (it is scheduled a moment after BeginPlay).
		After(World, 4.0f, [Mercer, Plate](UWorld* W)
		{
			AFGCentralStorageContainer* C1 = UploaderAt(W, GConvertLoc[0], true);
			AFGCentralStorageContainer* C2 = UploaderAt(W, GConvertLoc[1], true);
			AFGCentralStorageContainer* C3 = UploaderAt(W, GConvertLoc[2], true);
			AFGCentralStorageContainer* C4 = UploaderAt(W, GConvertLoc[3], true);
			AFGBuildable* Pole1 = BuildingAt<AFGBuildable>(W, GConvertPoleLoc, [](AFGBuildable* B) { return B->IsA<AFGBuildablePowerPole>(); });
			AFGBuildableStorage* Storage1 = Cast<AFGBuildableStorage>(BuildingAt<AFGBuildable>(W, GConvertStorageLoc, [](AFGBuildable* B) { return B->IsA<AFGBuildableStorage>() && !B->IsA<AFGCentralStorageContainer>(); }));
			UClass* Slot5 = LoadClass<UFGFactoryCustomizationDescriptor_Swatch>(nullptr, Slot5SwatchPath);
			if (C3 && Slot5)
			{
				FFactoryCustomizationData Paint = C3->GetCustomizationData_Native();
				Paint.SwatchDesc = Slot5;
				C3->SetCustomizationData_Native(Paint);
				C3->ApplyCustomizationData_Native(Paint);
			}
			// One wire per Sinking Depot, as in the game: pole to C4, and C1 to C2 (a wire between two converted buildings).
			const bool bWirePole = ConvWire(W, Pole1, C4);
			const bool bWireDepots = ConvWire(W, C1, C2);
			AFGBuildableBlueprintDesigner* Designer1 = BuildingAt<AFGBuildableBlueprintDesigner>(W, GConvertDesignerLoc, [](AFGBuildableBlueprintDesigner*) { return true; });
			AFGBuildable* Pole2 = BuildingAt<AFGBuildable>(W, GConvertPole2Loc, [](AFGBuildable* B) { return B->IsA<AFGBuildablePowerPole>(); });
			const bool bWireDesigner = ConvWire(W, UploaderAt(W, GConvertLoc[4], true), Pole2, Designer1);
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ConvertCheck: wire C5-pole inside the designer %d"), bWireDesigner);
			if (Storage1 && C1)
			{
				Storage1->GetStorageInventory()->AddStack(FInventoryStack(200, TSubclassOf<UFGItemDescriptor>(Plate)), true);
				if (UFGFactoryConnectionComponent* Out = FindOutput(Storage1))
				{
					Out->SetConnection(FindInput(C1));
				}
			}
			AFGCentralStorageSubsystem* Depot = AFGCentralStorageSubsystem::Get(W);
			const TSubclassOf<UFGItemDescriptor> MercerItem(Mercer);
			if (Depot)
			{
				FillDepot(W, MercerItem, Depot->GetCentralStorageItemLimit(MercerItem) - Depot->GetNumItemsFromCentralStorage(MercerItem));
			}
			if (C4)
			{
				C4->GetStorageInventory()->AddStack(FInventoryStack(ConvertMercerCount, MercerItem), true);
			}
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ConvertCheck: wires pole-C4 %d, C1-C2 %d; storage fed into C1; %d Mercer Spheres into C4"), bWirePole, bWireDepots, ConvertMercerCount);
		});

		After(World, 9.0f, [](UWorld* W)
		{
			CheckSwapped(W, TEXT("0 setup"), true, 1, true);
			// Checked now: the paint was set 5 s ago, after the default paint, and the conversion starts before a later check would run.
			CheckPaintNow(W, TEXT("0 setup paint kept"), true);
			SaveTestBlueprint(W, TEXT("SinkingDepot_ConvBP_before"));
			SaveThen(W, TEXT("SinkingDepot_ConvFixture"), [](UWorld* W1o, const bool bSaved)
			{
				GConvert.Check(TEXT("0 fixture saved"), bSaved, TEXT("SinkingDepot_ConvFixture"));
				// 1. Convert through the chat command, as the host. Checked a second later in case the RPC is queued.
				const bool bSent = SendChatCommand(W1o, TEXT("sinkingdepot convert confirm nobackup"));
				After(W1o, 1.0f, [bSent](UWorld* W1)
				{
				ASinkingDepotSubsystem* S = ASinkingDepotSubsystem::Get(W1);
				int32 WireEntries = 0;
				int32 Remapped = 0;
				for (const FSinkingDepotConvertRecord& Record : S ? S->GetUndoRecords() : TArray<FSinkingDepotConvertRecord>())
				{
					for (const FSinkingDepotWireRecord& Entry : Record.Wires)
					{
						++WireEntries;
						Remapped += Entry.bOtherEndConverted && IsValid(Entry.OtherActor) && !SinkingDepot::IsSinkingDepot(Entry.OtherActor) ? 1 : 0;
					}
				}
				GConvert.Check(TEXT("1 chat command as host"), bSent && S && S->GetUndoRecords().Num() == 5,
					FString::Printf(TEXT("sent %d, %d undo record(s)"), bSent, S ? S->GetUndoRecords().Num() : -1));
				// pole-C4, C1-C2 (recorded once, on whichever was converted first) and C5-pole inside the designer.
				GConvert.Check(TEXT("1 wires recorded"), WireEntries == 3 && Remapped == 1, FString::Printf(TEXT("%d wire record(s), %d between two converted buildings"), WireEntries, Remapped));
				CheckSwapped(W1, TEXT("1 convert"), false, 0, true);
				CheckPaintLater(W1, TEXT("1 convert"), false);
				SaveTestBlueprint(W1, TEXT("SinkingDepot_ConvBP_after"));
				AFGCentralStorageSubsystem* Depot = AFGCentralStorageSubsystem::Get(W1);
				const TSubclassOf<UFGItemDescriptor> PlateItem(LoadClass<UFGItemDescriptor>(nullptr, IronPlatePath));
				const int32 PlatesBefore = Depot ? Depot->GetNumItemsFromCentralStorage(PlateItem) : -1;
				After(W1, 8.0f, [PlatesBefore, PlateItem](UWorld* W2)
				{
					AFGCentralStorageSubsystem* Depot2 = AFGCentralStorageSubsystem::Get(W2);
					const int32 PlatesAfter = Depot2 ? Depot2->GetNumItemsFromCentralStorage(PlateItem) : -1;
					GConvert.Check(TEXT("1 belt still feeds"), PlatesAfter > PlatesBefore, FString::Printf(TEXT("Iron Plate in the depot %d -> %d"), PlatesBefore, PlatesAfter));

					// 2. Undo through the console command.
					IConsoleManager::Get().ProcessUserConsoleInput(TEXT("SinkingDepot.UndoConvert confirm"), *GLog, W2);
					ASinkingDepotSubsystem* S2 = ASinkingDepotSubsystem::Get(W2);
					GConvert.Check(TEXT("2 undo record cleared"), S2 && S2->GetUndoRecords().IsEmpty(), FString::Printf(TEXT("%d record(s) left"), S2 ? S2->GetUndoRecords().Num() : -1));
					CheckSwapped(W2, TEXT("2 undo"), true, 1, true);
					CheckPaintLater(W2, TEXT("2 undo"), true);

					After(W2, 6.0f, [](UWorld* W3)
					{
						// 3. Without a choice, or with none at all, nothing happens.
						GConvertLines.Reset();
						SinkingDepot::Convert::RunConvert(W3, {TEXT("confirm")}, ConvertTestNames, CaptureReporter());
						SinkingDepot::Convert::RunConvert(W3, {}, ConvertTestNames, CaptureReporter());
						SinkingDepot::Convert::RunUndo(W3, {}, ConvertTestNames, CaptureReporter());
						GConvert.Check(TEXT("3 no choice, no change"), GetUploaders(W3, true).Num() == 5 && LinesContain(TEXT("Nothing was converted yet")) && LinesContain(TEXT("confirm nobackup")),
							FString::Printf(TEXT("%d Sinking Depot(s); %d line(s) of help"), GetUploaders(W3, true).Num(), GConvertLines.Num()));

						// 4. Convert with a backup save.
						GConvertLines.Reset();
						SinkingDepot::Convert::RunConvert(W3, {TEXT("confirm"), TEXT("backup")}, ConvertTestNames, CaptureReporter());
						After(W3, 25.0f, [](UWorld* W4)
						{
							FString BackupName;
							for (const FString& Line : GConvertLines)
							{
								FString Rest;
								if (Line.Split(TEXT("Backup saved as '"), nullptr, &Rest))
								{
									Rest.Split(TEXT("'"), &BackupName, nullptr);
								}
							}
							FString BackupPath;
							const bool bFile = !BackupName.IsEmpty() && UFGSaveSystem::GetAbsolutePathForSaveGame(W4, BackupName, BackupPath) && FPaths::FileExists(BackupPath);
							GConvert.Check(TEXT("4 backup written"), bFile, FString::Printf(TEXT("'%s' -> %s"), *BackupName, *BackupPath));
							ASinkingDepotSubsystem* S4 = ASinkingDepotSubsystem::Get(W4);
							GConvert.Check(TEXT("4 converted after the backup"), GetUploaders(W4, true).Num() == 0 && S4 && S4->GetUndoRecords().Num() == 5,
								FString::Printf(TEXT("%d Sinking Depot(s) left, %d record(s)"), GetUploaders(W4, true).Num(), S4 ? S4->GetUndoRecords().Num() : -1));
							SaveThen(W4, TEXT("SinkingDepot_ConvCheck_s1"), [](UWorld* W5, const bool bSaved5)
							{
								GConvert.Check(TEXT("4 saved for the reload"), bSaved5, TEXT("SinkingDepot_ConvCheck_s1"));
								UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ConvertCheck: part 1 done (%d of %d failed so far); loading SinkingDepot_ConvCheck_s1"), GConvert.Failed, GConvert.Checks);
								GConvertPhase = 1;
								GEngine->Exec(W5, TEXT("open Persistent_Level?loadgame=SinkingDepot_ConvCheck_s1?listen"));
							});
						});
					});
				});
				});
			});
		});
	}

	void ConvertPart2(UWorld* World)
	{
		ASinkingDepotSubsystem* Subsystem = ASinkingDepotSubsystem::Get(World);
		const int32 Loaded = Subsystem ? Subsystem->GetUndoRecords().Num() : -1;
		GConvert.Check(TEXT("5 record survived the reload"), Loaded == 5 && GetUploaders(World, true).Num() == 0,
			FString::Printf(TEXT("%d record(s), %d Sinking Depot(s)"), Loaded, GetUploaders(World, true).Num()));
		CheckSwapped(World, TEXT("5 after reload"), false, 0, true);

		// 6. Undo through the chat command after the reload.
		const bool bSent = SendChatCommand(World, TEXT("sinkingdepot undo confirm"));
		GConvert.Check(TEXT("6 chat undo sent"), bSent, TEXT(""));
		After(World, 1.0f, [](UWorld* W0)
		{
			CheckSwapped(W0, TEXT("6 undo after reload"), true, 1, true);
			CheckPaintLater(W0, TEXT("6 undo after reload"), true);
		});

		After(World, 7.0f, [](UWorld* W)
		{
			// 7. Convert, dismantle one of the uploaders, undo: that one is skipped.
			GConvertLines.Reset();
			SinkingDepot::Convert::RunConvert(W, {TEXT("confirm"), TEXT("nobackup")}, ConvertTestNames, CaptureReporter());
			AFGCentralStorageContainer* C4 = UploaderAt(W, GConvertLoc[3], false);
			if (C4)
			{
				C4->Destroy();
			}
			GConvertLines.Reset();
			SinkingDepot::Convert::RunUndo(W, {TEXT("confirm")}, ConvertTestNames, CaptureReporter());
			ASinkingDepotSubsystem* S = ASinkingDepotSubsystem::Get(W);
			GConvert.Check(TEXT("7 dismantled one is skipped"), C4 && GetUploaders(W, true).Num() == 4 && !UploaderAt(W, GConvertLoc[3], true) && LinesContain(TEXT("had been dismantled"))
				&& S && S->GetUndoRecords().IsEmpty(), FString::Printf(TEXT("%d Sinking Depot(s), %d record(s)"), GetUploaders(W, true).Num(), S ? S->GetUndoRecords().Num() : -1));

			// 8. Convert once more and save, for the "load without the mod" test.
			GConvertLines.Reset();
			SinkingDepot::Convert::RunConvert(W, {TEXT("confirm"), TEXT("nobackup")}, ConvertTestNames, CaptureReporter());
			GConvert.Check(TEXT("8 final convert"), GetUploaders(W, true).Num() == 0, FString::Printf(TEXT("%d Sinking Depot(s) left"), GetUploaders(W, true).Num()));
			SaveThen(W, TEXT("SinkingDepot_ConvFinal"), [](UWorld* W2, const bool bSaved)
			{
				GConvert.Check(TEXT("8 final save"), bSaved, TEXT("SinkingDepot_ConvFinal"));
				GConvertPhase = 2;
				ConvertFinish(W2, TEXT("convert_done"));
			});
		});
	}

	/** -SinkingDepotConvertHost: two Sinking Depots; a joined client's command must be refused, the host's must work. */
	void ConvertHostWaitForClient(TWeakObjectPtr<UWorld> WeakWorld, int32 Polls)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return;
		}
		int32 Players = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			Players += It->IsValid() && (*It)->GetPawn() ? 1 : 0;
		}
		if (Players < 2 && Polls < 400)
		{
			After(World, 3.0f, [WeakWorld, Polls](UWorld*) { ConvertHostWaitForClient(WeakWorld, Polls + 1); });
			return;
		}
		UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ConvertHost: %d player(s); giving the client 60 s to try the command"), Players);
		After(World, 60.0f, [Players](UWorld* W)
		{
			GConvert.Check(TEXT("H1 client joined"), Players >= 2, FString::Printf(TEXT("%d player(s)"), Players));
			GConvert.Check(TEXT("H1 client refused"), GetUploaders(W, true).Num() == 2, FString::Printf(TEXT("%d Sinking Depot(s) still there"), GetUploaders(W, true).Num()));
			const bool bSent = SendChatCommand(W, TEXT("sinkingdepot convert confirm nobackup"));
			After(W, 1.0f, [bSent](UWorld* W2)
			{
				GConvert.Check(TEXT("H2 host allowed"), bSent && GetUploaders(W2, true).Num() == 0, FString::Printf(TEXT("%d Sinking Depot(s) left"), GetUploaders(W2, true).Num()));
				ConvertFinish(W2, TEXT("convert_host_done"));
			});
		});
	}

	void SpawnConvertPair(UWorld* World, const FVector& Near, const float Yaw)
	{
		UClass* Ours = LoadClass<AFGBuildable>(nullptr, BuildClassPath);
		const FVector Right = FRotationMatrix(FRotator(0.0f, Yaw, 0.0f)).GetUnitAxis(EAxis::Y);
		const FVector Forward = FRotator(0.0f, Yaw, 0.0f).Vector();
		AFGBuildable* A = SpawnOnGround(World, Ours, Near + Forward * 3000.0f - Right * 800.0f, Yaw + 180.0f);
		AFGBuildable* B = SpawnOnGround(World, Ours, Near + Forward * 3000.0f + Right * 800.0f, Yaw + 180.0f);
		After(World, 4.0f, [WeakA = TWeakObjectPtr<AActor>(A), WeakB = TWeakObjectPtr<AActor>(B)](UWorld* W)
		{
			const bool bWired = ConvWire(W, WeakA.Get(), WeakB.Get());
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] Convert pair: spawned %s and %s, wired %d"), *GetNameSafe(WeakA.Get()), *GetNameSafe(WeakB.Get()), bWired);
		});
	}

	void ConvertWorldReady(UWorld* World)
	{
		const TCHAR* CommandLine = FCommandLine::Get();
		if (FParse::Param(CommandLine, TEXT("SinkingDepotConvertCheck")) && World->GetNetMode() != NM_DedicatedServer)
		{
			if (GConvertPhase == 0)
			{
				UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotConvertCheck: part 1 starts in 30 s"));
				After(World, 30.0f, [](UWorld* W) { ConvertPart1(W); });
			}
			else if (GConvertPhase == 1)
			{
				UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotConvertCheck: part 2 starts in 25 s"));
				After(World, 25.0f, [](UWorld* W) { ConvertPart2(W); });
			}
		}
		if (FParse::Param(CommandLine, TEXT("SinkingDepotConvertHost")) && World->GetNetMode() == NM_ListenServer)
		{
			After(World, 30.0f, [](UWorld* W)
			{
				APlayerController* Controller = nullptr;
				AFGCharacterPlayer* Player = LocalPlayer(W, Controller);
				if (Player)
				{
					SpawnConvertPair(W, FindGround(W, Player->GetActorLocation(), 200000.0f), Controller->GetControlRotation().Yaw);
				}
				UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotConvertHost: waiting for a client"));
				ConvertHostWaitForClient(W, 0);
			});
		}
		if (FParse::Param(CommandLine, TEXT("SinkingDepotConvertDS")) && World->GetNetMode() == NM_DedicatedServer)
		{
			After(World, 40.0f, [](UWorld* W)
			{
				TActorIterator<APlayerStart> Start(W);
				const FVector Near = Start ? Start->GetActorLocation() : FVector::ZeroVector;
				SpawnConvertPair(W, FindGround(W, Near, 200000.0f), Start ? Start->GetActorRotation().Yaw : 0.0f);
				UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotConvertDS: two Sinking Depots spawned near %s"), *Near.ToString());
			});
		}
	}

	void ConvertClientWorldReady(UWorld* World)
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotClientConvertTry")))
		{
			return;
		}
		// The host spawns its buildings 30 s in; try after that.
		After(World, 45.0f, [](UWorld* W)
		{
			const bool bSent = SendChatCommand(W, TEXT("sinkingdepot convert confirm nobackup"));
			UE_LOG(LogSinkingDepot, Log, TEXT("[dev] ClientConvertTry: sent the convert chat command as a joined client: %d"), bSent);
		});
	}
}

void SinkingDepot::Dev::OnClientWorldReady(UWorld* World)
{
	if (World)
	{
		ConvertClientWorldReady(World);
	}
	if (World && FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotClientCheck")))
	{
		UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotClientCheck: waiting for a Sinking Depot to replicate"));
		ClientCheckPoll(World, 0);
	}
}

void SinkingDepot::Dev::OnWorldReady(UWorld* World)
{
	if (World)
	{
		ConvertWorldReady(World);
	}
	if (World && World->GetNetMode() == NM_DedicatedServer && FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotServerShowcase")))
	{
		UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotServerShowcase: waiting for a player to join"));
		ServerShowcasePoll(World, 0);
	}

	if (World && FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotUIProbe")))
	{
		UIProbe(World);
	}

	if (World && FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotMenuCheck")))
	{
		// After the dev kit (20 s) has unlocked the building.
		const TWeakObjectPtr<UWorld> WeakMenuWorld(World);
		FTimerHandle MenuHandle;
		World->GetTimerManager().SetTimer(MenuHandle, FTimerDelegate::CreateWeakLambda(World, [WeakMenuWorld]()
		{
			if (UWorld* W = WeakMenuWorld.Get())
			{
				MenuCheck(W);
			}
		}), 35.0f, false);
	}

	// After the dev kit (20 s). -SinkingDepotBeltCheck includes the icon renders; -SinkingDepotIconRender does only those.
	if (World && (FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotBeltCheck")) || FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotIconRender"))))
	{
		const bool bBelt = FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotBeltCheck"));
		After(World, 30.0f, [bBelt](UWorld* W)
		{
			if (bBelt)
			{
				BeltCheck(W);
			}
			else
			{
				IconRenderOnly(W);
			}
		});
	}

	// Self-checking regression run for releases (no other flags needed); see Regression().
	if (World && FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotRegression")))
	{
		UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotRegression: starting in 30 s"));
		After(World, 30.0f, [](UWorld* W) { Regression(W); });
	}

	if (World && FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotDevBuild")))
	{
		// After the dev kit (20 s) has filled the depot with the ingredients.
		const TWeakObjectPtr<UWorld> WeakBuildWorld(World);
		FTimerHandle BuildHandle;
		World->GetTimerManager().SetTimer(BuildHandle, FTimerDelegate::CreateWeakLambda(World, [WeakBuildWorld]()
		{
			DevBuild(WeakBuildWorld.Get());
		}), 30.0f, false);
	}

	// -SinkingDepotDevKit: for hands-on build gun tests. Completes the depot research and fills the Dimensional Depot
	// with what one Sinking Depot costs, x3 (the build gun can pay from the depot). No spawning, no camera changes.
	if (World && FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotDevKit")))
	{
		const TWeakObjectPtr<UWorld> WeakKitWorld(World);
		FTimerHandle KitHandle;
		World->GetTimerManager().SetTimer(KitHandle, FTimerDelegate::CreateWeakLambda(World, [WeakKitWorld]()
		{
			UWorld* W = WeakKitWorld.Get();
			ASinkingDepotSubsystem* Subsystem = W ? ASinkingDepotSubsystem::Get(W) : nullptr;
			AFGSchematicManager* Schematics = W ? AFGSchematicManager::Get(W) : nullptr;
			if (!Subsystem || !Schematics)
			{
				return;
			}
			Schematics->GiveAccessToSchematic(Subsystem->GetDepotResearch(), nullptr, ESchematicUnlockFlags::BlockTelemetry);
			// Uploads through a throwaway inventory (FillDepot); the log shows how many of each actually went in.
			UClass* Recipe = LoadClass<UFGRecipe>(nullptr, RecipeClassPath);
			if (const UFGRecipe* RecipeCDO = Recipe ? Recipe->GetDefaultObject<UFGRecipe>() : nullptr)
			{
				for (const FItemAmount& Ingredient : RecipeCDO->GetIngredients())
				{
					const int32 Added = FillDepot(W, Ingredient.ItemClass, Ingredient.Amount * 3);
					UE_LOG(LogSinkingDepot, Log, TEXT("[dev] DevKit: %d x %s into the depot"), Added, *GetNameSafe(Ingredient.ItemClass));
				}
			}
			UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotDevKit: depot research done, ingredients for 3 Sinking Depots in the depot"));
		}), 20.0f, false);
	}

	if (!World || !FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotShowcase")))
	{
		return;
	}
	// Give the player time to spawn and the world time to stream in, then build. After the build effects have finished
	// (15 s), take one screenshot per camera: switch the view, then shoot a moment later so the frame has settled.
	// Screenshots land in <userdir>/Saved/Screenshots; the last one is the overview.
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [WeakWorld]()
	{
		UWorld* W = WeakWorld.Get();
		if (!W)
		{
			return;
		}
		Showcase(W, *GLog);
		for (int32 Index = 0; Index < GShowcaseShots.Num(); ++Index)
		{
			const float SwitchAt = 15.0f + Index * 3.0f;
			FTimerHandle SwitchHandle;
			W->GetTimerManager().SetTimer(SwitchHandle, FTimerDelegate::CreateWeakLambda(W, [W, Index]()
			{
				ViewCamera(W, GShowcaseShots[Index].Camera.Get()); // Also takes the view back from the intro sequence.
			}), SwitchAt, false);
			FTimerHandle ShotHandle;
			W->GetTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateWeakLambda(W, [Index]()
			{
				FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("SinkingDepotShowcase_%s.png"), *GShowcaseShots[Index].Name), false, false);
				UE_LOG(LogSinkingDepot, Log, TEXT("[dev] Showcase screenshot requested: %s"), *GShowcaseShots[Index].Name);
			}), SwitchAt + 1.5f, false);
		}
	}), 25.0f, false);
	UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotShowcase: building the showcase in 25 s"));
}

#else

void SinkingDepot::Dev::Install()
{
}

void SinkingDepot::Dev::OnWorldReady(UWorld* World)
{
}

void SinkingDepot::Dev::OnClientWorldReady(UWorld* World)
{
}

#endif
