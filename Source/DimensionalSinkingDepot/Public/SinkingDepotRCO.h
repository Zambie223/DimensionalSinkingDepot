#pragma once

#include "CoreMinimal.h"
#include "FGRemoteCallObject.h"
#include "SinkingDepotRCO.generated.h"

class AFGCentralStorageContainer;

/**
 * Per-player channel for the Dimensional Sinking Depot (SML registers one per player controller). Used by the AWESOME Sink
 * tab on the building's panel: a player asks the server to print the available AWESOME Sink coupons into their inventory.
 */
UCLASS(NotBlueprintable)
class DIMENSIONALSINKINGDEPOT_API USinkingDepotRCO : public UFGRemoteCallObject
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Prints every available coupon that fits into the sending player's inventory; the rest stay available. Refused unless
	 * Depot is a Dimensional Sinking Depot near the player. Answers with Client_PrintResult.
	 */
	UFUNCTION(Server, Reliable)
	void Server_PrintCoupons(AFGCentralStorageContainer* Depot);

	/** The server's answer: how many coupons went into the inventory and how many are still available. */
	UFUNCTION(Client, Reliable)
	void Client_PrintResult(int32 Printed, int32 StillAvailable);

private:
	/** Remote call objects need a replicated property (the game's own RCOs carry one too). */
	UPROPERTY(Replicated)
	bool mForceNetField_USinkingDepotRCO = false;
};
