#pragma once

#include "CoreMinimal.h"

#include <atomic>

class AActor;
class AFGCentralStorageSubsystem;
class AFGResourceSinkSubsystem;

namespace SinkingDepot
{
	/** True for our buildable (exact class, checked by name and package so it is cheap and needs no loaded-class lookup). */
	bool IsSinkingDepot(const AActor* Actor);
}

namespace SinkingDepot::Overflow
{
	/**
	 * State shared between the game thread (writes) and factory ticks (reads), which may run off the game thread.
	 * The subsystem pointers are set by ASinkingDepotSubsystem on the server and cleared when its world ends.
	 */
	struct FRuntime
	{
		std::atomic<AFGCentralStorageSubsystem*> Depot{nullptr};
		std::atomic<AFGResourceSinkSubsystem*> Sink{nullptr};
		std::atomic<bool> bSinkWhenUploaderCantKeepUp{true};
		std::atomic<bool> bRequirePower{false};
		/** MW drawn while sinking when power is required (the AWESOME Sink's own value, read at startup). */
		std::atomic<float> PowerMW{30.0f};

		/** Items sunk because the depot was at its limit for them. */
		std::atomic<int64> SunkDepotFull{0};
		/** Items sunk because the uploader had no room for them (depot not full; only with the setting on). */
		std::atomic<int64> SunkCantKeepUp{0};
		/** Items taken off a belt whose sink call then failed: put back into the uploader, or lost if there was no room. */
		std::atomic<int64> ReturnedToBuffer{0};
		std::atomic<int64> Lost{0};
	};

	FRuntime& Runtime();

	/**
	 * Installs the after-hooks on AFGCentralStorageContainer::Factory_Tick (the sinking) and ::BeginPlay (linking the
	 * building's power connector to its power info). Packaged game only.
	 */
	void InstallHook();

	/** Server side, game thread: true if this building is unpowered while power is required (for the status command). */
	bool IsBlockedByPower(const AActor* Building);

	/**
	 * Server side, game thread: true if the building's power connector is wired to a circuit that has power and no
	 * tripped fuse. Asks the circuit, not the building's own power info, because UFGPowerInfoComponent::HasPower
	 * means "has enough of the power it requests" and the building requests nothing while it isn't sinking.
	 */
	bool HasGridPower(AActor* Building);

	/** Server side, game thread: refreshes the powered set the factory-tick hook reads (called twice a second). */
	void RefreshPowered(UWorld* World);

	/**
	 * Server side, game thread: the Sinking Depots that sank something within the last HoldSeconds, for the grinder
	 * window indicator. Also forgets buildings that are gone or have been idle for longer.
	 */
	void CollectActive(double HoldSeconds, TArray<AActor*>& OutActive);
}
