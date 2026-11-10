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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Resources/FGItemDescriptor.h"
#include "TimerManager.h"
#include "UnrealClient.h"

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

void SinkingDepot::Dev::OnClientWorldReady(UWorld* World)
{
	if (World && FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotClientCheck")))
	{
		UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotClientCheck: waiting for a Sinking Depot to replicate"));
		ClientCheckPoll(World, 0);
	}
}

void SinkingDepot::Dev::OnWorldReady(UWorld* World)
{
	if (World && World->GetNetMode() == NM_DedicatedServer && FParse::Param(FCommandLine::Get(), TEXT("SinkingDepotServerShowcase")))
	{
		UE_LOG(LogSinkingDepot, Warning, TEXT("[dev] -SinkingDepotServerShowcase: waiting for a player to join"));
		ServerShowcasePoll(World, 0);
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
