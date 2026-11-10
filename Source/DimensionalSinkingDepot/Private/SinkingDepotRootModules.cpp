#include "SinkingDepotRootModules.h"

#include "Configuration/ModConfiguration.h"
#include "FGSchematic.h"
#include "SinkingDepotConstants.h"
#include "SinkingDepotLog.h"
#include "SinkingDepotSubsystem.h"

USinkingDepotGameInstanceModule::USinkingDepotGameInstanceModule()
{
	bRootModule = true;
}

void USinkingDepotGameInstanceModule::DispatchLifecycleEvent(const ELifecyclePhase Phase)
{
	// Configurations are registered during INITIALIZATION, so add ours during CONSTRUCTION.
	// Loaded here instead of in the constructor so the mod's content is guaranteed to be mounted.
	if (Phase == ELifecyclePhase::CONSTRUCTION)
	{
		const TSoftClassPtr<UModConfiguration> ConfigClass{FSoftObjectPath(SinkingDepot::ConfigClassPath)};
		if (UClass* Loaded = ConfigClass.LoadSynchronous())
		{
			ModConfigurations.AddUnique(Loaded);
		}
		else
		{
			UE_LOG(LogSinkingDepot, Error, TEXT("Could not load the Mod Configuration %s; the overflow setting stays at its default (on)"), SinkingDepot::ConfigClassPath);
		}
	}
	Super::DispatchLifecycleEvent(Phase);
}

USinkingDepotGameWorldModule::USinkingDepotGameWorldModule()
{
	bRootModule = true;
	ModSubsystems.Add(ASinkingDepotSubsystem::StaticClass());
}

void USinkingDepotGameWorldModule::DispatchLifecycleEvent(const ELifecyclePhase Phase)
{
	// SML registers mSchematics during INITIALIZATION (RegisterDefaultContent), so add ours during CONSTRUCTION.
	if (Phase == ELifecyclePhase::CONSTRUCTION)
	{
		const TSoftClassPtr<UFGSchematic> SchematicClass{FSoftObjectPath(SinkingDepot::SchematicClassPath)};
		if (UClass* Loaded = SchematicClass.LoadSynchronous())
		{
			mSchematics.AddUnique(Loaded);
		}
		else
		{
			UE_LOG(LogSinkingDepot, Error, TEXT("Could not load the schematic %s; the Dimensional Sinking Depot can't be unlocked"), SinkingDepot::SchematicClassPath);
		}
	}
	Super::DispatchLifecycleEvent(Phase);
}
