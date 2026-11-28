# Backlog

Things to do in a later step, with enough context to start without re-researching. Newest first within each section. Move an item into a round's log when work on it starts.

## Android

### Keep a running game alive in the background: done (26 Nov), see `06-round3-implementation-log.md` part A

Left over: a "Continue" entry on the main menu (the menus are in design round 4); keeping single-player time running in the background would need `SDL_HINT_ANDROID_BLOCK_ON_PAUSE=0` and is not wanted for now.

### Other Android items
- **Minimap: zoom in further** (user, 27 Nov: "it could zoom in a bit more"). The smallmap's most detailed zoom is 1 tile per 4×2 screen pixels (`SetZoomLevel()`, `zoomlevels[]`); more needs `DrawSmallMapColumn()` to write 2×2 pixel blocks (or drawing into a buffer and scaling it up).
- **Top edge follows the safe area too** (user, 26 Nov; first part done in round 4: the chrome sits against the cutout's rectangles): the status pill, the minimap button and the banners sit some distance below the real top (camera cutout inset, maybe the rounded corners). They should slide with the safe area like the bottom dock does, and an option to sit closer to the edge would be nice. Start: `RelocateMobileWindows()` places them at `safe.top + margin`; check what `OpenTTDSurface` reports at the top (cutout vs status bar) on the Poco.
- **Pinch on the minimap** is untested on the device (adb can't do two fingers): ask the user.
- **Hover tooltips after taps:** Android leaves the emulated mouse pointer where the finger lifted, so OpenTTD treats it as a hover. Belongs to the gesture layer.
- **Sound:** the message "Only a fallback sound set was found" appears on start. Bundle OpenSFX (and maybe OpenMSX) in the APK.
- **Release signing:** builds are signed with the debug key. Create a release key before sharing builds beyond the user's phone. An update signed with a different key can't install over the old one, so do it early.
- **GitHub Actions build:** option B in `02-android-build-plan.md`, for builds that don't depend on this server.
- **Rotation:** test a live rotation on the device (the layout pass handles it, but it has only been tested from a fresh start).

## Mobile UI
- **Corner handles of a station footprint** don't show inside the white rectangle (neighbouring outlines cover them). Needs a marker sprite; asked the designer (handoff 07).
- **Ghost signals** (round 5 design §5): pending signals show only as the highlighted stretch and a count. Drawing them needs the signal sprites added to the viewport's tile drawing at the pending positions (as `_vd.handle_sprites_to_draw` does for handles), computing the positions the way `CmdBuildSignalTrack` walks the track.
- **Fund new industry: footprint highlight** (round 5 §9.3): the pending industry shows only the tapped tile. `IndustrySpec::layouts[layout]` gives the tiles; highlight their bounding box with `SetTileSelectSize()` after the tap.
- **Story book in the mobile layout** (round 5 §10.3): title row ‹ · page dropdown · ›, the desktop page renderer filling the area with drag-scroll, story buttons at least 40 tall. Today the desktop `StoryBookWindow` sits in the World hub's tab.
- **Newspaper picture** (round 5 §10.5): the mobile newspaper shows the headline only; the desktop `NewsWindow` draws vehicle and company pictures per `NewsStyle`.
- **Docks and airports: catchment, acceptance and join** (round 5 §11.2, §11.3): a pending dock or airport shows no catchment or Accepts/Supplies and joins without a choice. The rail station's footprint code (`PendingSelection::station`, `UpdateJoinCandidates()`) would need a per-kind catchment radius and footprint size.
- **Mobile layouts of the airport and tree pickers** (round 5 §11.1, §11.3): both are the desktop windows in the sheet.
- **Rail and road toolbars on MobileToolbarWindow**: they predate the base class and keep their own copies of the status, Cancel and Build logic.
- **Steppers: hold to repeat** (round 5 design §4): Settings' − value + steps once per tap. A held finger is a long press today (tooltip); repeating needs the gesture layer to send repeated clicks while a stepper button is held.
- **Station picker Type row** for NewGRF station classes/types (design round 3 §3.2) isn't built; base game unaffected.
- **"Building…" state** for multiplayer (design round 3 §1.2): planned with multiplayer in round 5, batch 13.
- **Depot entrance arrow** is the white one-way road arrow; `PALETTE_TO_YELLOW` doesn't recolour it. A real yellow arrow needs a sprite.
