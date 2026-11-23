#pragma once

#include "CoreMinimal.h"
#include "FGFactoryColoringTypes.h"
#include "Templates/SubclassOf.h"
#include "SinkingDepotConvertTypes.generated.h"

class AFGBuildable;
class AFGBuildableWire;
class UFGRecipe;

/** One power wire the convert command removed, so the undo command can put it back. */
USTRUCT()
struct DIMENSIONALSINKINGDEPOT_API FSinkingDepotWireRecord
{
	GENERATED_BODY()

	/** The building at the wire's other end (usually a power pole), and the name of the connector it used there. */
	UPROPERTY(SaveGame)
	TObjectPtr<AActor> OtherActor;

	UPROPERTY(SaveGame)
	FName OtherConnector;

	/**
	 * True if the other end was another Sinking Depot that the same conversion replaced; OtherActor is then that
	 * building's replacement uploader, and undo connects the wire to whatever that uploader is turned back into.
	 */
	UPROPERTY(SaveGame)
	bool bOtherEndConverted = false;

	UPROPERTY(SaveGame)
	TSubclassOf<AFGBuildableWire> WireClass;

	UPROPERTY(SaveGame)
	TSubclassOf<UFGRecipe> WireRecipe;

	UPROPERTY(SaveGame)
	FFactoryCustomizationData WirePaint;
};

/** One Sinking Depot the convert command turned into a vanilla uploader. */
USTRUCT()
struct DIMENSIONALSINKINGDEPOT_API FSinkingDepotConvertRecord
{
	GENERATED_BODY()

	/** The vanilla uploader that replaced it. Null or destroyed if the player has dismantled it since. */
	UPROPERTY(SaveGame)
	TObjectPtr<AFGBuildable> Uploader;

	/** What the Sinking Depot was built with, so a restored one refunds the same cost when dismantled. */
	UPROPERTY(SaveGame)
	TSubclassOf<UFGRecipe> OriginalRecipe;

	UPROPERTY(SaveGame)
	TArray<FSinkingDepotWireRecord> Wires;
};
