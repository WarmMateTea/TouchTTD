# Backlog

Things to do in a later step, with enough context to start without re-researching. Newest first within each section. Move an item into a round's log when work on it starts.

## Android

### Keep a running game alive in the background (requested by the user, 15 Nov 2026; all three parts approved the same day)

**Problem:** when the user switches away from TouchTTD, Android may kill the process to free memory. Anything since the last save is lost. MIUI/HyperOS (the user's Poco X7 Pro) is especially aggressive about this.

**Plan:**
1. **Persistent notification via a foreground service** while a game is running, e.g. "TouchTTD: game in progress (unsaved)", with a tap action that returns to the game. A foreground service raises the process priority so it's much less likely to be killed.
   - Java: a small `Service` started from `OpenTTDActivity` with `startForegroundService()` and stopped when the game is quit or saved and closed.
   - Manifest: `FOREGROUND_SERVICE`, plus a typed permission. Android 14+ requires a `foregroundServiceType`, and no type fits a game exactly. `specialUse` (with a `PROPERTY_SPECIAL_USE_FGS_SUBTYPE` explanation) is the honest one. It's fine for sideloading; the Play Store asks for a justification. Also `POST_NOTIFICATIONS`, a runtime permission on Android 13+, needed for the notification to show.
   - The native side tells Java whether a game is running, through JNI (SDL's Java class can host a small static method) or an SDL property/event. The useful states are "in a game" and "unsaved changes since the last save".
2. **Autosave when the app goes to the background:** a safety net even if the process is killed anyway, e.g. by a reboot or by the user swiping the app away.
   - SDL sends `SDL_EVENT_WILL_ENTER_BACKGROUND` / `SDL_EVENT_DID_ENTER_BACKGROUND`. Handle them in `src/video/sdl3_v.cpp` and trigger a save to a dedicated slot (e.g. `autosave/background.sav`), the same way the timed autosave works (`DoAutoOrNetsave` in `src/openttd.cpp`).
   - Android gives roughly 5 seconds before the app may be frozen. Saving a large map can take longer, so the save should run on the existing threaded-save path and the foreground service should keep the process alive meanwhile.
   - Offer "Continue last game" on the next start when a background save is newer than the last normal save.
3. **Pause in the background** (optional, to discuss with the user): single-player time keeps running while the app is backgrounded, which may not be what a phone player expects.

**Verify on the device:** start a game, switch to a heavy app, then run `adb shell am kill io.github.warmmatetea.touchttd` (simulates a low-memory kill; it only kills background processes). With the foreground service the process should survive. With autosave only, the game should restore.

### Other Android items
- **Bottom safe area:** with the system bars hidden, `SDL_GetWindowSafeArea()` still leaves out the 153 px gesture strip, so the dock floats above a strip of map. Decide with the designer: dock flush with the edge and only its buttons kept clear?
- **Hover tooltips after taps:** Android leaves the emulated mouse pointer where the finger lifted, so OpenTTD treats it as a hover. Belongs to the gesture layer.
- **Sound:** the message "Only a fallback sound set was found" appears on start. Bundle OpenSFX (and maybe OpenMSX) in the APK.
- **Release signing:** builds are signed with the debug key. Create a release key before sharing builds beyond the user's phone. An update signed with a different key can't install over the old one, so do it early.
- **GitHub Actions build:** option B in `02-android-build-plan.md`, for builds that don't depend on this server.
- **Rotation:** test a live rotation on the device (the layout pass handles it, but it has only been tested from a fresh start).

## Mobile UI
- The gesture layer, the pending-selection build flow and the other items listed in `handoffs/03-impl-to-design-round2.md` §6.
