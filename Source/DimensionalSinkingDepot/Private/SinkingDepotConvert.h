#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * "Convert to vanilla uploaders" and its undo, shared by the chat command (single player and listen hosts) and the
 * console commands (dedicated servers, from the Server Manager console). Server side, game thread only.
 *
 * Convert turns every Dimensional Sinking Depot into a vanilla Dimensional Depot Uploader so the mod can be removed
 * without losing them: same place, belt, buffered items and paint. Power wires are removed (vanilla uploaders take no
 * power) and nothing is refunded. Undo turns the uploaders from the last conversion back, for free, and puts the
 * wires back where the other end still exists and has a free slot.
 */
namespace SinkingDepot::Convert
{
	/** Where command output goes. Say may be called later (after a backup save), so it must stay valid until then. */
	struct FReporter
	{
		TFunction<void(const FString&)> Say;
		/** False for a console output device, which is only valid while the command runs; later lines then go to the log and to SinkingDepot.Status. */
		bool bCanSayLater = false;
	};

	/** How the commands are spelled for this caller, so the help text names the right ones. */
	struct FCommandNames
	{
		FString Convert;
		FString Undo;
	};

	/** Args are the words after "convert": none (explain), "confirm backup", "confirm nobackup", or a bare "confirm". */
	void RunConvert(UWorld* World, const TArray<FString>& Args, const FCommandNames& Names, const FReporter& Reporter);

	/** Args are the words after "undo": none (explain) or "confirm". */
	void RunUndo(UWorld* World, const TArray<FString>& Args, const FCommandNames& Names, const FReporter& Reporter);
}
