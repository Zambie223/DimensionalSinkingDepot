# Changelog

Changes made after the first public release are listed here.

## 0.3.0

### Added
- An AWESOME Sink tab in the Dimensional Sinking Depot's window: points until the next coupon on both tracks, the number of coupons ready to print, and a Print Coupons button that prints every coupon that fits into your inventory (the rest stay available). Printing needs you within 30 m of the building. Switch tabs with the mouse or the gamepad bumpers.

## 0.2.0

### Added
- `/sinkingdepot convert` (in chat, single player or host) and `SinkingDepot.ConvertAll` (dedicated server console)
  turn every Dimensional Sinking Depot into a normal Dimensional Depot Uploader, keeping its belt, paint and buffered
  items, so the mod can be removed without losing them. It explains what it will do first, and asks whether to save a
  backup.
- `/sinkingdepot undo` and `SinkingDepot.UndoConvert` turn the uploaders from the last conversion back, for free,
  with their power wires where possible. The undo information is kept in the save.
- `Tools/convert_to_vanilla.py`: the same conversion for a save or blueprint file, without the game.

## 0.1.2

### Fixed
- Works with Satisfactory 1.2.5.1 (build 520140) and SML 3.12.1. Game update 1.2.5.1 changed the memory layout of the building this mod extends, so 0.1.1 read the wrong data on that game version and would very likely crash when loading a save with a Dimensional Sinking Depot. 0.1.2 is built for the new layout. It needs game build 520140 or newer and SML 3.12.1 or newer.

## 0.1.1

### Changed
- The build menu and unlock icon is now a render of the building itself, grinder running, in the same style as the game's own building icons. Before, it was a drawing close to the mod logo.
- Small performance improvements, with no change in behaviour:
  - With the power setting off (the default), a Dimensional Sinking Depot no longer takes a lock shared by all Sinking Depots on every factory tick.
  - Empty slots in the building's buffer are skipped instead of being copied every tick.
  - If the grinder window material ever fails to load, it's tried once instead of on every grinder update.
  - Vanilla uploaders now pay a single class check when they're placed or loaded, instead of three.

### Development
- New development-only test, `-SinkingDepotRegression`. It checks the building's setup, exact sink points, the grinder window, the power setting, and sinking straight off a belt when the building can't keep up. It's compiled out of release builds.
