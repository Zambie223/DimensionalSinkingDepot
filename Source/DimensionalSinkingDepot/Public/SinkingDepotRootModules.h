#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "Module/GameWorldModule.h"
#include "SinkingDepotRootModules.generated.h"

/**
 * Root game instance module (SML discovers native root modules as well as Blueprint ones).
 * Registers the Mod Configuration asset, which has to be a Blueprint because SML's config UI widgets come from
 * its Blueprint property classes.
 */
UCLASS()
class DIMENSIONALSINKINGDEPOT_API USinkingDepotGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	USinkingDepotGameInstanceModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};

/** Root game world module: registers the schematic and spawns the server subsystem in game worlds. */
UCLASS()
class DIMENSIONALSINKINGDEPOT_API USinkingDepotGameWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	USinkingDepotGameWorldModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};
