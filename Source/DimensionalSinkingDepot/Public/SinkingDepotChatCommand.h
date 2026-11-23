#pragma once

#include "CoreMinimal.h"
#include "Command/ChatCommandInstance.h"
#include "SinkingDepotChatCommand.generated.h"

/**
 * /sinkingdepot convert [confirm backup|confirm nobackup] and /sinkingdepot undo [confirm].
 * Single player and the host of a hosted game only: SML chat commands have no permissions, so any joined player could
 * otherwise convert the host's factory. On a dedicated server it points to the console commands instead.
 */
UCLASS()
class DIMENSIONALSINKINGDEPOT_API ASinkingDepotChatCommand : public AChatCommandInstance
{
	GENERATED_BODY()

public:
	ASinkingDepotChatCommand();

	virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label) override;
};
