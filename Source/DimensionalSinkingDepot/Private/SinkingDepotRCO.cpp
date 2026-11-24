#include "SinkingDepotRCO.h"

#include "SinkingDepotLog.h"
#include "SinkingDepotOverflow.h"
#include "SinkingDepotSinkTab.h"

#include "Buildables/FGCentralStorageContainer.h"
#include "FGCharacterPlayer.h"
#include "FGInventoryComponent.h"
#include "FGPlayerController.h"
#include "FGResourceSinkSubsystem.h"
#include "Net/UnrealNetwork.h"

// Named namespace so a unity build merging this file with others can't clash with their helpers.
namespace SinkingDepot::Rco
{
	/** How far the player may be from the building when printing. Generous: the panel stays open while you step back. */
	constexpr double MaxPrintDistance = 3000.0;
}

void USinkingDepotRCO::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USinkingDepotRCO, mForceNetField_USinkingDepotRCO);
}

void USinkingDepotRCO::Server_PrintCoupons_Implementation(AFGCentralStorageContainer* Depot)
{
	AFGPlayerController* Controller = GetOwnerPlayerController();
	AFGCharacterPlayer* Player = Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
	AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(GetWorld());
	UFGInventoryComponent* Inventory = Player ? Player->GetInventory() : nullptr;
	if (!Depot || !SinkingDepot::IsSinkingDepot(Depot) || !Inventory || !Sink || !Sink->GetCouponClass())
	{
		UE_LOG(LogSinkingDepot, Warning, TEXT("Print coupons refused: depot %s, player %s, sink %d"), *GetNameSafe(Depot), *GetNameSafe(Player), Sink != nullptr);
		return;
	}
	if (FVector::Dist(Player->GetActorLocation(), Depot->GetActorLocation()) > SinkingDepot::Rco::MaxPrintDistance)
	{
		UE_LOG(LogSinkingDepot, Warning, TEXT("Print coupons refused: %s is too far from %s"), *GetNameSafe(Player), *GetNameSafe(Depot));
		Client_PrintResult(0, Sink->GetNumCoupons());
		return;
	}

	// Take them all, give what fits, and return the rest (not counted as new coupons: no telemetry).
	const int32 Removed = Sink->RemoveResourceSinkCoupons(Sink->GetNumCoupons());
	const int32 Added = Removed > 0 ? Inventory->AddStack(FInventoryStack(Removed, Sink->GetCouponClass()), true) : 0;
	if (Added < Removed)
	{
		Sink->AddResourceSinkCoupons(Removed - Added, false);
	}
	UE_LOG(LogSinkingDepot, Log, TEXT("Printed %d coupon(s) for %s at %s, %d still available"), Added, *GetNameSafe(Player), *GetNameSafe(Depot), Sink->GetNumCoupons());
	Client_PrintResult(Added, Sink->GetNumCoupons());
}

void USinkingDepotRCO::Client_PrintResult_Implementation(int32 Printed, int32 StillAvailable)
{
	SinkingDepot::SinkTab::OnPrintResult(Printed, StillAvailable);
}
