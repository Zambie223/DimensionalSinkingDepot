# Dimensional Sinking Depot

A Satisfactory mod that adds the **Dimensional Sinking Depot**: a Dimensional Depot Uploader that sends its overflow into the AWESOME Sink for points instead of backing up the belt.

A normal uploader stops taking items when the Dimensional Depot is full for that item, or when items arrive faster than it can upload. The belt then backs up, and the usual fix is a splitter and an AWESOME Sink next to every uploader. The Dimensional Sinking Depot does both jobs in one building.

## What it does

- **Uploads like a normal uploader.** It uses the same upload speed, research upgrades and Dimensional Depot.
- **Sinks what the Depot is full of.** When the Depot can't hold any more of an item, those items go to the AWESOME Sink. You get the same points and coupons a real AWESOME Sink gives.
- **Optionally sinks what it can't keep up with.** This is on by default. Items arriving faster than the building can upload are sunk too, so the belt never backs up.
  - Turn it off in Mod Options if you'd rather the belt wait until the Depot is full.
- **Leaves unsinkable items alone.** Items the AWESOME Sink won't take wait in the building, just like a normal uploader.
- **Has the AWESOME Sink's coupon screen built in.** Its window has a second tab showing how many points you need for the next coupon on both tracks, and a button that prints your coupons. You don't need to go to an AWESOME Sink.

## Installation

You need Satisfactory 1.2.5.1 (build 520140 or newer) with Satisfactory Mod Loader (SML) 3.12.1 or newer installed.

The easiest way is [Satisfactory Mod Manager](https://smm.ficsit.app/): search for **Dimensional Sinking Depot** and install it. Its page on the Satisfactory Mod Repository is [ficsit.app/mod/DimensionalSinkingDepot](https://ficsit.app/mod/DimensionalSinkingDepot).

To install a release by hand instead:

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

## The AWESOME Sink tab

Open a Dimensional Sinking Depot and switch to the **FICSIT AWESOME Sink** tab at the top of its window. With a gamepad, use the bumpers.

- **Standard Points and DNA Points until the next coupon**: the same two bars the AWESOME Sink shows.
- **Printable Coupons** and a **Print Coupons** button. It prints every available coupon that fits into your inventory. If they don't all fit, the rest stay available for later, and nothing is lost.
- You need to be within 30 m of the building to print.
- Coupons are shared by everyone in the game, as at a real AWESOME Sink.

Normal Dimensional Depot Uploaders don't get the tab.

## Console

On a dedicated server, run these from the Server Manager console:

| Command | Effect |
|---|---|
| `SinkingDepot.Status` | Shows the setting in effect, whether the building is unlocked, and how many items have been sunk since the game was launched. |
| `SinkingDepot.Set SinkWhenUploaderCantKeepUp off` | Changes and saves a setting (`on` or `off`). It applies immediately. |
| `SinkingDepot.Set RequirePower on` | The same for the power setting. |

## Removing the mod

If you just remove the mod, every Dimensional Sinking Depot disappears from your save, along with the items waiting
inside it. To keep them, turn them into normal Dimensional Depot Uploaders first.

**In game (single player, or the host of a multiplayer game):**

1. Type `/sinkingdepot convert` in chat. It tells you how many buildings it will change and what happens to their
   wires. Nothing is changed yet.
2. Type `/sinkingdepot convert confirm backup` to save a backup of your world first and then convert, or
   `/sinkingdepot convert confirm nobackup` to convert without a backup.
3. Save the game, then remove the mod.

**On a dedicated server**, use the Server Manager console: `SinkingDepot.ConvertAll`, then
`SinkingDepot.ConvertAll confirm backup` (or `confirm nobackup`). Run `SinkingDepot.Status` a few seconds later to see
the result.

What happens to each building:
- It becomes a normal uploader in the same place, with the same belt, paint and buffered items.
- Its power wire, if it has one, is removed, because a normal uploader has no power connector.
- Nothing is refunded, including the AWESOME Sink half of the cost.

**Changed your mind?** While the mod is still installed, `/sinkingdepot undo` (console: `SinkingDepot.UndoConvert`)
turns the uploaders from the last conversion back into Dimensional Sinking Depots, for free. This also works after
saving and reloading. Power wires come back where the building at the other end still exists and has a free slot.
Uploaders you built yourself are never touched.

### Already removed the mod, or can't load the game with it?

[`Tools/convert_to_vanilla.py`](Tools/convert_to_vanilla.py) does the same conversion on a save file or blueprint
file, without the game. It needs Python 3.9 or newer and nothing else.

```
python convert_to_vanilla.py "MySave.sav"
```

It writes a new file next to the original (`MySave_vanilla.sav`) and never changes the original. It refuses to write
anything if it can't read the file back exactly, or if the file comes from a game version it hasn't been tested with
(`--force` tries anyway). `--dry-run` shows what it would change without writing anything. Load the new file with the
mod removed, and keep the original until you've checked the result in game.

Saves are in `%LOCALAPPDATA%\FactoryGame\Saved\SaveGames\<your id>\`, and blueprints in the `blueprints` folder next
to it.

## Notes

- By default the building needs no power, like the uploader. Sinking happens on the host or server.
- To remove the mod without losing your Sinking Depots, see [Removing the mod](#removing-the-mod).
- This mod works together with mods that change the Depot's upload speed or stack limit.

## Status

Version 0.3.0 adds the AWESOME Sink tab. On Satisfactory 1.2.5.1 it passed:
- an automated in-game check: switching tabs with the mouse and with the gamepad bumpers, both point bars, printing every coupon, a nearly full inventory (part printed, the rest kept), a full one (nothing printed, nothing lost), and normal uploaders left without the tab
- a hosted game and a Windows and a Linux dedicated server, each with a joined player who prints from the tab: refused 60 m away, every coupon 15 m away, and counted once
- loading a save made with 0.2.0

Version 0.2.0 adds a way to turn every Dimensional Sinking Depot back into a normal uploader before removing the mod,
and to undo that. On Satisfactory 1.2.5.1 it passed:
- an automated in-game check: conversion with the chat and console commands, the backup save, belts, buffered items,
  paint, power wires (including one between two Sinking Depots and one inside a Blueprint Designer), undo before and
  after saving and reloading, and a dismantled uploader being skipped
- a hosted game, where a joined player is refused and the host can convert
- a Windows dedicated server, through the Server Manager console
- loading the converted save, and the offline script's output, in the game without the mod

Version 0.1.2 makes the mod work with Satisfactory 1.2.5.1 and SML 3.12.1. That game update changed the layout of the building this mod extends, so 0.1.1 crashes there when a save contains a Dimensional Sinking Depot (see [CHANGELOG.md](CHANGELOG.md)). On 1.2.5.1, before release, 0.1.2 passed:
- the automated in-game check below, in a hosted game on Windows
- a check on a Windows and on a Linux dedicated server: sinking for exactly the AWESOME Sink's points, with a joined client seeing the buildings, their paint and the running grinder

Version 0.1.1 changed the build menu icon to a render of the building and made small performance improvements. The automated in-game check covers:
- the building's setup when placed: power connector, grinder window and default paint, with a normal uploader left unchanged
- exact AWESOME Sink points when the Depot is full
- the grinder starting and stopping
- the power setting blocking sinking without power, and sinking freely when off
- sinking straight off a belt when the building can't keep up, with nothing lost

The results below are from version 0.1.0, the first release. It was tested on a Windows dedicated server with a large factory (660 uploaders), together with a mod that changes the Depot's upload speed and stack limit. The tests covered:
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
