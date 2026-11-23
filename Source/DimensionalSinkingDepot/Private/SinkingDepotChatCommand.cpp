#include "SinkingDepotChatCommand.h"

#include "SinkingDepotConvert.h"

#include "Command/CommandSender.h"
#include "Engine/World.h"
#include "FGPlayerController.h"

ASinkingDepotChatCommand::ASinkingDepotChatCommand()
{
	CommandName = TEXT("sinkingdepot");
	Usage = NSLOCTEXT("DimensionalSinkingDepot", "ChatCommand.Usage",
		"/sinkingdepot convert - turn every Dimensional Sinking Depot into a vanilla uploader before removing the mod; /sinkingdepot undo - turn them back (host only)");
	MinNumberOfArguments = 0;
	bOnlyUsableByPlayer = false;
}

EExecutionStatus ASinkingDepotChatCommand::ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label)
{
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_DedicatedServer)
	{
		Sender->SendChatMessage(TEXT("On a dedicated server, run this from the server console (Server Manager): SinkingDepot.ConvertAll, or SinkingDepot.UndoConvert."), FLinearColor::Yellow);
		return EExecutionStatus::INSUFFICIENT_PERMISSIONS;
	}
	// The host's own controller is the local one on the server; joined players' controllers are remote.
	const AFGPlayerController* Player = Sender->IsPlayerSender() ? Sender->GetPlayer() : nullptr;
	if (!Player || !Player->IsLocalController())
	{
		Sender->SendChatMessage(TEXT("Only the host can run this."), FLinearColor::Red);
		return EExecutionStatus::INSUFFICIENT_PERMISSIONS;
	}

	const bool bConvert = Arguments.Num() >= 1 && Arguments[0].Equals(TEXT("convert"), ESearchCase::IgnoreCase);
	const bool bUndo = Arguments.Num() >= 1 && Arguments[0].Equals(TEXT("undo"), ESearchCase::IgnoreCase);
	if (!bConvert && !bUndo)
	{
		PrintCommandUsage(Sender);
		return EExecutionStatus::BAD_ARGUMENTS;
	}

	// SML's player sender lives as long as the player's controller, so a weak pointer still reaches the player when the
	// result arrives a few seconds later (after a backup save).
	const TWeakObjectPtr<UCommandSender> Held(Sender);
	SinkingDepot::Convert::FReporter Reporter;
	Reporter.bCanSayLater = true;
	Reporter.Say = [Held](const FString& Line)
	{
		if (Held.IsValid())
		{
			Held->SendChatMessage(Line);
		}
	};
	const SinkingDepot::Convert::FCommandNames Names{TEXT("/sinkingdepot convert"), TEXT("/sinkingdepot undo")};
	TArray<FString> Rest(Arguments);
	Rest.RemoveAt(0);
	if (bConvert)
	{
		SinkingDepot::Convert::RunConvert(World, Rest, Names, Reporter);
	}
	else
	{
		SinkingDepot::Convert::RunUndo(World, Rest, Names, Reporter);
	}
	return EExecutionStatus::COMPLETED;
}
