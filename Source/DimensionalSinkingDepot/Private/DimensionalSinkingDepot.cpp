#include "DimensionalSinkingDepot.h"

#include "SinkingDepotDevCommands.h"
#include "SinkingDepotLog.h"
#include "SinkingDepotOverflow.h"
#include "SinkingDepotSinkTab.h"
#include "SinkingDepotWindow.h"

#include "Buildables/FGCentralStorageContainer.h"
#include "Patching/NativeHookManager.h"

DEFINE_LOG_CATEGORY(LogSinkingDepot);

void FDimensionalSinkingDepotModule::StartupModule()
{
	// Native hooks only exist in the packaged game; the editor never runs factory ticks on real buildings.
	SinkingDepot::Overflow::InstallHook();
	SinkingDepot::Window::InstallHooks();
	SinkingDepot::SinkTab::InstallHook(); // clients and listen hosts only
#if !WITH_EDITOR
	// One BeginPlay hook for everything our building needs, so vanilla uploaders pay a single class check.
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCentralStorageContainer::BeginPlay, GetMutableDefault<AFGCentralStorageContainer>(),
		[](AFGCentralStorageContainer* Self)
		{
			if (!SinkingDepot::IsSinkingDepot(Self))
			{
				return;
			}
			SinkingDepot::Overflow::OnBuildingBeginPlay(Self); // Power connector first, as before.
			SinkingDepot::Window::OnBuildingBeginPlay(Self);
		});
	UE_LOG(LogSinkingDepot, Log, TEXT("Installed the uploader BeginPlay hook"));
#endif
	SinkingDepot::Dev::Install();
}

void FDimensionalSinkingDepotModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FDimensionalSinkingDepotModule, DimensionalSinkingDepot)
