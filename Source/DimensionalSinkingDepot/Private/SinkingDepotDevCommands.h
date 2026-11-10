#pragma once

#include "CoreMinimal.h"

// Development aids (SinkingDepot.Dev.*). Set in DimensionalSinkingDepot.Build.cs; 0 for releases.
#ifndef SINKINGDEPOT_DEV_COMMANDS
#define SINKINGDEPOT_DEV_COMMANDS 0
#endif

class UWorld;

namespace SinkingDepot::Dev
{
	/** Logs that development commands are compiled in. Does nothing unless SINKINGDEPOT_DEV_COMMANDS is 1. */
	void Install();

	/**
	 * Called by the subsystem's BeginPlay on the server or host. Runs the command-line test flags:
	 * - -SinkingDepotShowcase: spawns a vanilla uploader and Sinking Depots and takes screenshots.
	 * - -SinkingDepotDevKit: grants the research and fills the depot with building ingredients.
	 * - -SinkingDepotDevBuild: builds one with the real build gun.
	 * - -SinkingDepotMenuCheck: opens the build menu and searches for the building.
	 * - -SinkingDepotServerShowcase: on a dedicated server, spawns the showcase once a player has joined.
	 */
	void OnWorldReady(UWorld* World);

	/**
	 * Called by the subsystem's BeginPlay on clients. With -SinkingDepotClientCheck it waits for a Sinking Depot to
	 * replicate, then logs what this client sees (unlock, paint, grinder state) and takes screenshots.
	 */
	void OnClientWorldReady(UWorld* World);
}
