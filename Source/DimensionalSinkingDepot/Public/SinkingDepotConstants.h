#pragma once

#include "CoreMinimal.h"

namespace SinkingDepot
{
	/** Must match the plugin name. SML also uses it as the config file name (Configs/DimensionalSinkingDepot.cfg). */
	inline constexpr const TCHAR* ModReference = TEXT("DimensionalSinkingDepot");

	/** Assets created by Tools/create_assets.py. Renaming any of these breaks existing saves (class paths are saved). */
	inline constexpr const TCHAR* ConfigClassPath = TEXT("/DimensionalSinkingDepot/SinkingDepotConfiguration.SinkingDepotConfiguration_C");
	inline constexpr const TCHAR* SchematicClassPath = TEXT("/DimensionalSinkingDepot/Schematics/Schematic_SinkingDepot.Schematic_SinkingDepot_C");
	inline constexpr const TCHAR* BuildPackageName = TEXT("/DimensionalSinkingDepot/Buildable/Build_SinkingDepot");
	inline constexpr const TCHAR* BuildClassName = TEXT("Build_SinkingDepot_C");

	/** The vanilla MAM research that unlocks the Dimensional Depot; ours is granted alongside it. */
	inline constexpr const TCHAR* DepotResearchClassPath = TEXT("/Game/FactoryGame/Schematics/Research/AlienTech_RS/Research_Alien_CentralStorage.Research_Alien_CentralStorage_C");

	/** Key in the configuration's root section. Changing it renames the value in users' config files. */
	inline constexpr const TCHAR* KeySinkWhenUploaderCantKeepUp = TEXT("SinkWhenUploaderCantKeepUp");
	inline constexpr bool DefaultSinkWhenUploaderCantKeepUp = true;

	/** Off by default: a free sink. On: the building needs power to sink, drawing what an AWESOME Sink draws. */
	inline constexpr const TCHAR* KeyRequirePower = TEXT("RequirePower");
	inline constexpr bool DefaultRequirePower = false;

	/** The AWESOME Sink's power use is read from its own class at runtime; this is only the fallback (game 1.2.5.0 value). */
	inline constexpr const TCHAR* ResourceSinkBuildClassPath = TEXT("/Game/FactoryGame/Buildable/Factory/ResourceSink/Build_ResourceSink.Build_ResourceSink_C");
	inline constexpr float FallbackSinkPowerMW = 30.0f;

	/** How long a building keeps drawing power after its last sink, so short gaps between items don't flicker. */
	inline constexpr double PowerHoldSeconds = 1.5;

	/** The window: the vanilla material we replace, and the mod's grinder material (Tools/create_assets.py). */
	inline constexpr const TCHAR* VanillaWindowMaterialName = TEXT("MM_Window_CC");
	inline constexpr const TCHAR* GrinderMaterialPath = TEXT("/DimensionalSinkingDepot/Materials/M_SinkingDepot_Grinder.M_SinkingDepot_Grinder");
	/** Grinder parameters while sinking: drum surface speed (cm/s) and glow strength between the plates. */
	inline constexpr float GrinderActiveSpeed = 35.0f;
	inline constexpr float GrinderActiveGlow = 6.0f;

	/**
	 * Default paint for a newly placed Sinking Depot: white panels with FICSIT orange trim, through
	 * the game's custom swatch. Only applied when the player's paint for new buildings is the factory default (slot 0);
	 * buildings loaded from a save are never repainted.
	 */
	inline constexpr const TCHAR* CustomSwatchClassPath = TEXT("/Game/FactoryGame/Buildable/-Shared/Customization/Swatches/SwatchDesc_Custom.SwatchDesc_Custom_C");
	inline constexpr const TCHAR* FactoryDefaultSwatchName = TEXT("SwatchDesc_Slot0_C");
	inline const FLinearColor DefaultPrimaryColor(0.85f, 0.85f, 0.85f);    // white
	inline const FLinearColor DefaultSecondaryColor(0.955f, 0.30f, 0.07f); // FICSIT orange, sRGB (250, 149, 73)
	/** Actor tag set on Sinking Depots that came out of a save, so the default paint never touches them. */
	inline const FName LoadedFromSaveTag(TEXT("SinkingDepot.LoadedFromSave"));

	/** Upper bound on items taken straight off the belt per input per factory tick (a Mk.6 belt is 20 items/s). */
	inline constexpr int32 MaxBeltGrabsPerTick = 16;
}
