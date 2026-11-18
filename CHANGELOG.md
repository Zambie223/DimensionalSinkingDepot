# Changelog

Changes made after the first public release are listed here.

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
