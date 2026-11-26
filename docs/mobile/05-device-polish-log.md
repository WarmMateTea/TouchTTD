# Device polish log: system bars, cancelling tools, sliding layout

Requests from the user (25 Nov 2026), worked on while the design session does round 3:
- a. keep Android's bottom (navigation) bar open;
- b. a quick way out of a placement/demolish tool, especially where there is no back button;
- c. move the UI as the safe areas change: teleporting first, then animated;
- d. send a .zip with every set of screenshots (saved as a standing rule in memory).

## (a) Navigation bar

**Finding:** SDL's Android surface (`SDLSurface.onApplyWindowInsets`) reports as unsafe the union of system bars, **system gesture areas**, tappable elements and the cutout. Gesture areas stay reserved while the bars are hidden. That was the 153 px strip below the dock in round 1 (`A02`).

**Changes:**
- `OpenTTDSurface` (Java) subclasses `SDLSurface` and reports only **visible system bars + display cutout**. `OpenTTDActivity.createSDLSurface()` returns it.
- The game no longer asks SDL for a full-screen window on Android. `OpenTTDActivity.applySystemBars(bool)` hides the status bar and shows or hides the navigation bar (`WindowInsetsController`, swipe-to-reveal behaviour). It's re-applied on focus and 300 ms later, because SDL's own window-style command runs asynchronously and resets the flags.
- `src/os/android/android_bridge.cpp` calls `applySystemBars` over JNI (`SDL_GetAndroidJNIEnv`/`SDL_GetAndroidActivity`): once when the window is created, and when the new setting **"Android: keep the navigation bar on screen"** (`gui.mobile_show_navigation_bar`, default on) changes.

## (b) Cancelling a tool

- **Rail toolbar ✕:** cancels the selection in review; otherwise it now drops the selected tool. It's enabled whenever a tool is selected (`B01` → `B02`). The confirm button returns to a grey "Build" once no tool is selected.
- **Floating Cancel** (`MobileToolCancelWindow`): a red "X Cancel" button at the bottom right of the visible map. It appears whenever a placement tool from a toolbar without its own Cancel is active (road, tram, water, airport, landscaping); the dock's 30 ms refresh creates and closes it (`B03`). It drops the tool and keeps the toolbar (`B04`).
- **No leftover square:** with touch input, once the last finger lifts, `UpdateTileSelection()` no longer highlights the tile under the (now absent) pointer.

## (c) Sliding layout

- `GetSafeArea()` in `mobile_gui.cpp` returns a *shown* safe area. `UpdateSafeAreaTarget()` (start of every layout pass) compares it with the system's. The first layout and any screen-size change (rotation) **teleport**; any other change starts a **150 ms ease-out slide**, advanced by a 15 ms timer that re-runs the layout pass.
- `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED` was already re-running the layout pass (round 1 fix), so a transient bar swiped in or out starts a slide.

**Status:** (b) verified on the server (`B01`–`B04`). (a) and (c) verified on the phone (`A10`–`A12`, 26 Nov):
- the navigation bar stays visible with the dock above it;
- `setting gui.mobile_show_navigation_bar false/true` (typed into the game console over adb) hides and shows it at runtime, and the dock follows.

The 150 ms slide is too quick for `adb exec-out screencap` (about 300 ms per frame) to catch mid-way; the user checks it by eye.

### Two device bugs found on the way (a)
1. **SDL re-hides the bars.** SDL treats every Android window as full screen (`mFullscreenModeActive`), and its `onSystemUiVisibilityChange` re-hides any bar that appears, 2 s later. `OpenTTDActivity` clears that flag when it applies the bars, and before SDL's listener runs.
2. **The setting was read too early.** The bridge was called when the window was created, before `LoadFromConfig(false)` had loaded the GUI settings, so it always asked for `false`. It's now also applied in `SetupColoursAndInitialWindow()`, which runs after the settings are loaded and on every menu/game switch.

### Tips
- **Typing into the game over adb:** `adb shell input keyevent 68` (the backquote key) opens the console; `adb shell input text "setting%sNAME%sVALUE"` (`%s` = space); `keyevent 66` (Enter).
- **Read only the app's own log:** `adb logcat -d -s TouchTTD:* SDL:*`. A broad `grep` over the whole log picks up other apps' data from the user's phone.
- **Native logging on Android:** `__android_log_print(ANDROID_LOG_INFO, "TouchTTD", ...)` (link `log`); OpenTTD's `Debug()` goes to stderr, which Android drops.
- **Restart the adb server** (`adb kill-server`) when `adb connect` fails with "protocol fault" right after pairing.
- **An offline phone on Tailscale:** MIUI puts Tailscale to sleep; set its battery saver to "No restrictions". `tailscale ping` sometimes wakes the path.

## Mistake worth remembering

A `pgrep -f "cmake --build"` wait loop matched its own shell and waited forever; then `pkill -f` killed itself. See the toolchain tips.
