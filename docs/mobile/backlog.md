# Backlog

Things to do in a later step, with enough context to start without re-researching. Newest first within each section. Move an item into a round's log when work on it starts.

## Android

### Keep a running game alive in the background: done (26 Nov), see `06-round3-implementation-log.md` part A

Left over: a "Continue" entry on the main menu (the menus are in design round 4); keeping single-player time running in the background would need `SDL_HINT_ANDROID_BLOCK_ON_PAUSE=0` and is not wanted for now.

### Other Android items
- **Top edge follows the safe area too** (user, 26 Nov): the status pill, the minimap button and the banners sit some distance below the real top (camera cutout inset, maybe the rounded corners). They should slide with the safe area like the bottom dock does, and an option to sit closer to the edge would be nice. Start: `RelocateMobileWindows()` places them at `safe.top + margin`; check what `OpenTTDSurface` reports at the top (cutout vs status bar) on the Poco.
- **Pinch on the minimap** is untested on the device (adb can't do two fingers): ask the user.
- **Hover tooltips after taps:** Android leaves the emulated mouse pointer where the finger lifted, so OpenTTD treats it as a hover. Belongs to the gesture layer.
- **Sound:** the message "Only a fallback sound set was found" appears on start. Bundle OpenSFX (and maybe OpenMSX) in the APK.
- **Release signing:** builds are signed with the debug key. Create a release key before sharing builds beyond the user's phone. An update signed with a different key can't install over the old one, so do it early.
- **GitHub Actions build:** option B in `02-android-build-plan.md`, for builds that don't depend on this server.
- **Rotation:** test a live rotation on the device (the layout pass handles it, but it has only been tested from a fresh start).

## Mobile UI
- **Corner handles of a station footprint** don't show inside the white rectangle (neighbouring outlines cover them). Needs a marker sprite; asked the designer (handoff 07).
- **Dropdown lists** (join station, minimap mode, sort orders) use the desktop's small rows; a touch-sized dropdown is for the design.
- **Station picker Type row** for NewGRF station classes/types (design round 3 §3.2) isn't built; base game unaffected.
- **"Building…" state** for multiplayer (design round 3 §1.2).
- **Depot entrance arrow** is the white one-way road arrow; `PALETTE_TO_YELLOW` doesn't recolour it. A real yellow arrow needs a sprite.
