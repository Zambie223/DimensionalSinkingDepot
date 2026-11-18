#pragma once

#include "CoreMinimal.h"

class AActor;
class AFGBuildable;
class AFGCentralStorageContainer;

namespace SinkingDepot::Window
{
	/**
	 * Installs after-hooks (packaged game only) on AFGBuildableFactory::OnBuildEffectFinished, which re-applies the
	 * grinder window because the build effect swaps materials while it plays and may restore the originals when it
	 * finishes, and on PostLoadGame, which marks buildings from a save so the default paint skips them.
	 */
	void InstallHooks();

	/**
	 * Everyone, from the module's single BeginPlay hook; Building must be a Sinking Depot. Gives it the grinder window,
	 * and on the server schedules the default paint for a newly placed building.
	 */
	void OnBuildingBeginPlay(AFGCentralStorageContainer* Building);

	/**
	 * Replaces the building's window material (vanilla MM_Window_CC) with a dynamic instance of the mod's grinder
	 * material, running or idle as the replicated ASinkingDepotSubsystem says. Visual only: does nothing on a
	 * dedicated server. Returns the number of material slots replaced or updated.
	 */
	int32 Apply(AActor* Building);

	/** Turns the grinder on (drum turning, orange glow between the plates) or off for one building's windows. */
	void SetActive(AActor* Building, bool bActive);

	/** Server: paints the building white with FICSIT orange trim (the custom swatch). Also used by the showcase. */
	void ApplyDefaultPaint(AFGBuildable* Building);
}
