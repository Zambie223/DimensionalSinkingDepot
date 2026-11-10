#pragma once

#include "CoreMinimal.h"

class AActor;
class AFGBuildable;

namespace SinkingDepot::Window
{
	/**
	 * Installs after-hooks on AFGCentralStorageContainer::BeginPlay and AFGBuildableFactory::OnBuildEffectFinished
	 * (packaged game only) that give our building its grinder window. The second hook matters because the build
	 * effect swaps materials while it plays and may restore the originals when it finishes.
	 */
	void InstallHooks();

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
