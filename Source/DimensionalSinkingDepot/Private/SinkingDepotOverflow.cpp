#include "SinkingDepotOverflow.h"

#include "SinkingDepotConstants.h"
#include "SinkingDepotLog.h"

#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "FGCentralStorageSubsystem.h"
#include "FGFactoryConnectionComponent.h"
#include "FGInventoryComponent.h"
#include "EngineUtils.h"
#include "FGCircuitSubsystem.h"
#include "FGPowerCircuit.h"
#include "FGPowerConnectionComponent.h"
#include "FGPowerInfoComponent.h"
#include "FGResourceSinkSubsystem.h"
#include "GameFramework/Actor.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "Patching/NativeHookManager.h"
#include "Resources/FGItemDescriptor.h"
#include "UObject/Package.h"

bool SinkingDepot::IsSinkingDepot(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}
	static const FName ClassName(BuildClassName);
	static const FName PackageName(BuildPackageName);
	const UClass* Class = Actor->GetClass();
	return Class->GetFName() == ClassName && Class->GetPackage()->GetFName() == PackageName;
}

namespace SinkingDepot::Overflow
{
	namespace
	{
		FRuntime GRuntime;

		/** When each Sinking Depot last sank something (FPlatformTime seconds). Written from factory ticks. */
		FCriticalSection GActivityLock;
		TMap<TWeakObjectPtr<AActor>, double> GLastSunk;

		/** Sinking Depots whose grid has power, refreshed on the game thread; read by factory ticks. */
		FCriticalSection GPoweredLock;
		TSet<TWeakObjectPtr<AActor>> GPowered;

		bool IsPoweredCached(AActor* Building)
		{
			FScopeLock Lock(&GPoweredLock);
			return GPowered.Contains(Building);
		}

		void RecordSinking(AActor* Building)
		{
			const double Now = FPlatformTime::Seconds();
			FScopeLock Lock(&GActivityLock);
			GLastSunk.FindOrAdd(Building) = Now;
		}

		bool SankWithin(AActor* Building, const double Seconds)
		{
			const double Now = FPlatformTime::Seconds();
			FScopeLock Lock(&GActivityLock);
			const double* Last = GLastSunk.Find(Building);
			return Last && Now - *Last <= Seconds;
		}

		/** Requests MW from the circuit, only touching the power info when the value changes. */
		void SetDraw(UFGPowerInfoComponent* Power, const float MW)
		{
			if (Power && Power->GetTargetConsumption() != MW)
			{
				Power->SetTargetConsumption(MW);
			}
		}

		/**
		 * Our building adds a power connector the vanilla uploader doesn't have. Link it to the building's existing
		 * power info (every factory has one) if nothing else did, so wires attach to this building's consumption.
		 */
		void LinkPowerConnections(AFGCentralStorageContainer* Self)
		{
			if (!SinkingDepot::IsSinkingDepot(Self))
			{
				return;
			}
			UFGPowerInfoComponent* Power = Self->GetPowerInfo();
			TInlineComponentArray<UFGPowerConnectionComponent*> Connections(Self);
			for (UFGPowerConnectionComponent* Connection : Connections)
			{
				if (Connection && Power && !Connection->GetPowerInfo())
				{
					Connection->SetPowerInfo(Power);
				}
			}
			if (!Power && Connections.Num() > 0)
			{
				UE_LOG(LogSinkingDepot, Warning, TEXT("%s has a power connector but no power info; it can't use power"), *Self->GetName());
			}
		}

		/**
		 * The AWESOME Sink only takes items that can be discarded and are worth at least 1 point (per AddPoints_ThreadSafe's
		 * documentation). Checking first means unsinkable items are never offered, so the game's "could not sink" notice
		 * never fires and they simply stay in the uploader like vanilla.
		 */
		bool IsSinkable(AFGResourceSinkSubsystem* Sink, const TSubclassOf<UFGItemDescriptor> Item)
		{
			if (!Item || !UFGItemDescriptor::CanBeDiscarded(Item))
			{
				return false;
			}
			int32 Points = 0;
			EResourceSinkTrack Track = EResourceSinkTrack::RST_Default;
			return Sink->FindResourceSinkPointsForItem(Item, Points, Track) && Points >= 1;
		}

		bool IsDepotFull(const AFGCentralStorageSubsystem* Depot, const TSubclassOf<UFGItemDescriptor> Item)
		{
			return Depot->GetNumItemsFromCentralStorage(Item) >= Depot->GetCentralStorageItemLimit(Item);
		}

		/**
		 * Sinks every buffered stack whose item the depot can't take any more of. Runs after vanilla has uploaded this
		 * tick. Returns how many items were sunk.
		 */
		int64 SinkFullItemsFromInventory(UFGInventoryComponent* Inventory, const AFGCentralStorageSubsystem* Depot, AFGResourceSinkSubsystem* Sink)
		{
			int64 Total = 0;
			const int32 Size = Inventory->GetSizeLinear();
			for (int32 Index = 0; Index < Size; ++Index)
			{
				FInventoryStack Stack;
				if (!Inventory->GetStackFromIndex(Index, Stack) || !Stack.HasItems())
				{
					continue;
				}
				const TSubclassOf<UFGItemDescriptor> Item = Stack.Item.GetItemClass();
				if (!IsDepotFull(Depot, Item) || !IsSinkable(Sink, Item))
				{
					continue;
				}
				int32 Sunk = 0;
				while (Sunk < Stack.NumItems && Sink->AddPoints_ThreadSafe(Item))
				{
					++Sunk;
				}
				if (Sunk > 0)
				{
					Inventory->RemoveFromIndex(Index, Sunk);
					GRuntime.SunkDepotFull.fetch_add(Sunk, std::memory_order_relaxed);
					Total += Sunk;
				}
			}
			return Total;
		}

		/**
		 * Takes items straight off the input belt into the sink when they would otherwise wait: the depot is full for
		 * them (always), or the uploader has no room for them (only with the setting on). Going belt-side keeps the
		 * uploader's own buffer and upload order untouched, and works for mixed belts. Returns how many items were sunk.
		 */
		int64 SinkFromBelts(AFGCentralStorageContainer* Self, UFGInventoryComponent* Inventory, const AFGCentralStorageSubsystem* Depot,
			AFGResourceSinkSubsystem* Sink, const bool bSinkWhenCantKeepUp)
		{
			int64 Total = 0;
			TInlineComponentArray<UFGFactoryConnectionComponent*> Connections(Self);
			TArray<FInventoryItem> Waiting;
			for (UFGFactoryConnectionComponent* Input : Connections)
			{
				if (!Input || Input->GetDirection() != EFactoryConnectionDirection::FCD_INPUT || !Input->IsConnected())
				{
					continue;
				}
				for (int32 Grab = 0; Grab < MaxBeltGrabsPerTick; ++Grab)
				{
					Waiting.Reset();
					if (!Input->Factory_PeekOutput(Waiting) || Waiting.Num() == 0)
					{
						break;
					}
					const FInventoryItem& Next = Waiting[0];
					const TSubclassOf<UFGItemDescriptor> Item = Next.GetItemClass();
					if (!IsSinkable(Sink, Item))
					{
						break; // Waits like vanilla.
					}
					const bool bDepotFull = IsDepotFull(Depot, Item);
					if (!bDepotFull && !(bSinkWhenCantKeepUp && !Inventory->HasEnoughSpaceForItem(Next)))
					{
						break; // Vanilla will collect and upload it.
					}
					FInventoryItem Taken;
					float OffsetBeyond = 0.0f;
					if (!Input->Factory_GrabOutput(Taken, OffsetBeyond, Item))
					{
						break;
					}
					if (Sink->AddPoints_ThreadSafe(Taken.GetItemClass()))
					{
						(bDepotFull ? GRuntime.SunkDepotFull : GRuntime.SunkCantKeepUp).fetch_add(1, std::memory_order_relaxed);
						++Total;
						continue;
					}
					// Not expected, since sinkability was checked above. Keep the item if there is room.
					if (Inventory->AddStack(FInventoryStack(1, Taken.GetItemClass()), true) == 1)
					{
						GRuntime.ReturnedToBuffer.fetch_add(1, std::memory_order_relaxed);
					}
					else
					{
						GRuntime.Lost.fetch_add(1, std::memory_order_relaxed);
					}
					break;
				}
			}
			return Total;
		}

		void OnFactoryTick(AFGCentralStorageContainer* Self)
		{
			if (!SinkingDepot::IsSinkingDepot(Self) || !Self->HasAuthority())
			{
				return;
			}
			AFGCentralStorageSubsystem* Depot = GRuntime.Depot.load(std::memory_order_acquire);
			AFGResourceSinkSubsystem* Sink = GRuntime.Sink.load(std::memory_order_acquire);
			if (!Depot || !Sink || Depot->GetWorld() != Self->GetWorld())
			{
				return; // Subsystem not ready yet; behaves like a vanilla uploader meanwhile.
			}
			UFGInventoryComponent* Inventory = Self->GetStorageInventory();
			if (!Inventory)
			{
				return;
			}
			// With "require power" on, an unpowered building only uploads, like the vanilla uploader; overflow waits.
			const bool bRequirePower = GRuntime.bRequirePower.load(std::memory_order_relaxed);
			UFGPowerInfoComponent* Power = Self->GetPowerInfo();
			if (bRequirePower && !IsPoweredCached(Self))
			{
				SetDraw(Power, 0.0f);
				return;
			}
			// Buffer first: freeing room there may let a waiting belt item fit, so it gets uploaded rather than sunk.
			int64 Sunk = SinkFullItemsFromInventory(Inventory, Depot, Sink);
			Sunk += SinkFromBelts(Self, Inventory, Depot, Sink, GRuntime.bSinkWhenUploaderCantKeepUp.load(std::memory_order_relaxed));
			if (Sunk > 0)
			{
				RecordSinking(Self);
			}
			// Like a machine, it draws power while working: while sinking, plus a short hold so gaps don't flicker.
			const bool bWorking = Sunk > 0 || SankWithin(Self, PowerHoldSeconds);
			SetDraw(Power, bRequirePower && bWorking ? GRuntime.PowerMW.load(std::memory_order_relaxed) : 0.0f);
		}
	}

	FRuntime& Runtime()
	{
		return GRuntime;
	}

	bool IsBlockedByPower(const AActor* Building)
	{
		return GRuntime.bRequirePower.load(std::memory_order_relaxed) && !IsPoweredCached(const_cast<AActor*>(Building));
	}

	bool HasGridPower(AActor* Building)
	{
		check(IsInGameThread());
		const UFGPowerConnectionComponent* Connection = Building ? Building->FindComponentByClass<UFGPowerConnectionComponent>() : nullptr;
		if (!Connection || Connection->GetNumConnections() == 0)
		{
			return false;
		}
		AFGCircuitSubsystem* Circuits = AFGCircuitSubsystem::Get(Building->GetWorld());
		UFGPowerCircuit* Circuit = Circuits ? Circuits->FindCircuit<UFGPowerCircuit>(Connection->GetCircuitID()) : nullptr;
		return Circuit && Circuit->HasPower() && !Circuit->IsFuseTriggered();
	}

	void RefreshPowered(UWorld* World)
	{
		check(IsInGameThread());
		TSet<TWeakObjectPtr<AActor>> Powered;
		if (World && GRuntime.bRequirePower.load(std::memory_order_relaxed))
		{
			for (TActorIterator<AFGCentralStorageContainer> It(World); It; ++It)
			{
				if (SinkingDepot::IsSinkingDepot(*It) && HasGridPower(*It))
				{
					Powered.Add(*It);
				}
			}
		}
		FScopeLock Lock(&GPoweredLock);
		GPowered = MoveTemp(Powered);
	}

	void CollectActive(const double HoldSeconds, TArray<AActor*>& OutActive)
	{
		check(IsInGameThread());
		OutActive.Reset();
		const double Now = FPlatformTime::Seconds();
		FScopeLock Lock(&GActivityLock);
		for (auto It = GLastSunk.CreateIterator(); It; ++It)
		{
			AActor* Building = It.Key().Get();
			if (!Building || Now - It.Value() > HoldSeconds)
			{
				It.RemoveCurrent();
				continue;
			}
			OutActive.Add(Building);
		}
	}

	void InstallHook()
	{
#if !WITH_EDITOR
		// Virtual hook: SML resolves the implementation through the sample object's vtable and detours it, so every
		// uploader runs through here. Only our class does any work; vanilla uploaders pay one class-name compare.
		SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCentralStorageContainer::Factory_Tick, GetMutableDefault<AFGCentralStorageContainer>(),
			[](AFGCentralStorageContainer* Self, float DeltaTime)
			{
				OnFactoryTick(Self);
			});
		SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCentralStorageContainer::BeginPlay, GetMutableDefault<AFGCentralStorageContainer>(),
			[](AFGCentralStorageContainer* Self)
			{
				LinkPowerConnections(Self);
			});
		UE_LOG(LogSinkingDepot, Log, TEXT("Installed the uploader factory tick and power hooks"));
#endif
	}
}
