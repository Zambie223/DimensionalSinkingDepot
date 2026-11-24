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
	 * - -SinkingDepotRegression: self-checking run over the shipping code paths (PASS/FAIL lines in the log).
	 * - -SinkingDepotBeltCheck / -SinkingDepotIconRender: belt build diagnostics and build-gun icon renders.
	 */
	void OnWorldReady(UWorld* World);

	/**
	 * Called by the subsystem's BeginPlay on clients. With -SinkingDepotClientCheck it waits for a Sinking Depot to
	 * replicate, then logs what this client sees (unlock, paint, grinder state) and takes screenshots.
	 */
	void OnClientWorldReady(UWorld* World);

	/**
	 * -SinkingDepotUIProbe (SinkingDepotUIProbe.cpp), single player or host: 35 s in, logs ("[uiprobe]") the layout,
	 * properties and functions of the uploader, AWESOME Sink and station panels and the shared window frame, then
	 * opens a Sinking Depot's and an AWESOME Sink's panel and logs their live trees, with screenshots.
	 */
	void UIProbe(UWorld* World);

	/**
	 * -SinkingDepotSinkTabCheck (SinkingDepotSinkTabCheck.cpp), single player or host: 35 s in, self-checks the AWESOME
	 * Sink tab (two tabs on a Sinking Depot, switching, printing coupons, a reused panel on a vanilla uploader) and logs
	 * "[sinktab] CHECK ..." lines, "[sinktab] RESULT n/m" and screenshots SinkingDepotShowcase_sinktab_*.png.
	 */
	void SinkTabCheck(UWorld* World);

	/** -SinkingDepotTabLab: diagnostic for the window frame's tabs; logs "[tablab] ..." lines, ending with "[tablab] done". */
	void TabLab(UWorld* World);
}
