#include "DimensionalSinkingDepot.h"

#include "SinkingDepotDevCommands.h"
#include "SinkingDepotLog.h"
#include "SinkingDepotOverflow.h"
#include "SinkingDepotWindow.h"

DEFINE_LOG_CATEGORY(LogSinkingDepot);

void FDimensionalSinkingDepotModule::StartupModule()
{
	// Native hooks only exist in the packaged game; the editor never runs factory ticks on real buildings.
	SinkingDepot::Overflow::InstallHook();
	SinkingDepot::Window::InstallHooks();
	SinkingDepot::Dev::Install();
}

void FDimensionalSinkingDepotModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FDimensionalSinkingDepotModule, DimensionalSinkingDepot)
