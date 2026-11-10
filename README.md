# Dimensional Sinking Depot

A Satisfactory mod that adds the **Dimensional Sinking Depot**: a Dimensional Depot Uploader that sends its overflow into the AWESOME Sink for points instead of backing up the belt.

A normal uploader stops taking items when the Dimensional Depot is full for that item, or when items arrive faster than it can upload. The belt then backs up, and the usual fix is a splitter and an AWESOME Sink next to every uploader. The Dimensional Sinking Depot does both jobs in one building.

## What it does

- **Uploads like a normal uploader.** It uses the same upload speed, research upgrades and Dimensional Depot.
- **Sinks what the Depot is full of.** When the Depot can't hold any more of an item, those items go to the AWESOME Sink. You get the same points and coupons a real AWESOME Sink gives.
- **Optionally sinks what it can't keep up with.** This is on by default. Items arriving faster than the building can upload are sunk too, so the belt never backs up.
  - Turn it off in Mod Options if you'd rather the belt wait until the Depot is full.
- **Leaves unsinkable items alone.** Items the AWESOME Sink won't take wait in the building, just like a normal uploader.

## Installation

You need Satisfactory 1.2 (build 517554 or newer) with Satisfactory Mod Loader (SML) 3.12 or newer installed.

The mod isn't on the Satisfactory Mod Repository yet, so Satisfactory Mod Manager can't install it. Install a release from this repository by hand:

1. From the Releases page, download the zip for where it will run:
   - `DimensionalSinkingDepot-Windows.zip` for your game
   - `DimensionalSinkingDepot-WindowsServer.zip` or `DimensionalSinkingDepot-LinuxServer.zip` for a dedicated server
2. Extract it into `FactoryGame/Mods/DimensionalSinkingDepot/` inside the game or server folder, so that `DimensionalSinkingDepot.uplugin` sits directly in that folder.
3. Start the game or server. The mod appears under **Mods** in the main menu.

In multiplayer, every player and the host or server need the mod, at the same version.

## Getting it in game

- **Unlock:** it unlocks with the Dimensional Depot research in the MAM, including saves where that research is already done.
- **Build menu:** Organization > Storage, next to the Dimensional Depot Uploader.
- **Cost:** a Dimensional Depot Uploader plus an AWESOME Sink. That is 1 Mercer Sphere, 10 SAM Fluctuators, 10 Modular Frames, 100 Wire, 15 Reinforced Iron Plates, 30 Cable and 45 Concrete.
- **Looks:** it uses the uploader's model, painted white with FICSIT orange trim, and its window holds a grinder drum instead of the glowing pane.
  - While it's sinking, the drum turns and glows AWESOME orange, so you can see at a glance which buildings are dumping overflow.
  - A small power connector with an insulator head sticks out of the back of the machine, inside the cage, like the one on a Constructor.
  - It fits anywhere a Dimensional Depot Uploader fits.
  - The white paint is only the default for newly built ones. If you've set your own paint for new buildings, that's used instead, and you can repaint it like any building.

## Settings

Main menu or pause menu > Mods > Dimensional Sinking Depot:

| Setting | Default | Effect |
|---|---|---|
| Sink overflow even if the Depot has free space | On | Sinks items that arrive faster than the building can upload. Off: the belt waits until the Depot is full for that item. |
| Require power to sink (like an AWESOME Sink) | Off | Off: the built-in sink is free. On: it only sinks while it has power, and draws what an AWESOME Sink draws (30 MW) while sinking. Without power it still uploads and the overflow backs up. |

In multiplayer the host's or dedicated server's setting is used.

## Console

On a dedicated server, run these from the Server Manager console:

| Command | Effect |
|---|---|
| `SinkingDepot.Status` | Shows the setting in effect, whether the building is unlocked, and how many items have been sunk since the game was launched. |
| `SinkingDepot.Set SinkWhenUploaderCantKeepUp off` | Changes and saves a setting (`on` or `off`). It applies immediately. |
| `SinkingDepot.Set RequirePower on` | The same for the power setting. |

## Notes

- By default the building needs no power, like the uploader. Sinking happens on the host or server.
- Removing the mod removes the buildings from your save, along with any items waiting inside them. The Dimensional Depot's contents are not affected.
- This mod works together with mods that change the Depot's upload speed or stack limit.

## Status

Version 0.1.0 is the first release. It has been tested on a Windows dedicated server with a large factory (660 uploaders), together with a mod that changes the Depot's upload speed and stack limit. The tests covered:
- sinking when the Depot is full, and when the building can't keep up
- the setting switched on and off at runtime
- unsinkable items being left alone
- save and reload
- the automatic unlock on a save that already had the research

The power setting was also tested on the Windows dedicated server:
- without power, the building doesn't sink
- once wired to a powered grid, it draws 30 MW while sinking
- with the setting off, it sinks for free

In a hosted game on Windows, it was confirmed that:
- each sunk item earns exactly the AWESOME Sink's points
- the building unlocks the moment the Dimensional Depot research completes
- newly built ones get the white and orange paint, and can be repainted
- placing it with the build gun works, and the cost is taken from the Dimensional Depot
- it appears in the build menu under Organization > Storage, right after the Dimensional Depot Uploader, with its icon, description and cost
- a second player joined to the host sees it unlocked, painted, and with its grinder running while it sinks

On a Linux dedicated server it was confirmed that the mod loads, unlocks with the research, and sinks overflow for exactly the AWESOME Sink's points. A client joined that server, but its view of the building wasn't recorded there. The same client check passed against a Windows host.

## License

GPL-3.0. See [LICENSE](LICENSE).

## Responsible AI use disclosure

Most of the code, tooling, artwork and documentation in this mod were written by an AI coding assistant (Claude, made by Anthropic, used through Claude Code), working under the maintainer's direction. The maintainer set the requirements and design decisions. AI-written code can contain mistakes: review it before relying on it, and back up your saves.
