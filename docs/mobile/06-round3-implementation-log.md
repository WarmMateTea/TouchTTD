# Round 3 implementation log

Design input: [`handoffs/06-design-to-impl-round3.md`](handoffs/06-design-to-impl-round3.md) (stations, depots, info windows in the sheet slot, inset fixes). The user asked to **start the round with keeping the game alive in the background and pausing automatically** (the backlog item approved on 15 Nov), and to have the next design round also cover the menus.

## Part A: Background survival and auto-pause

### What it does

1. **Leaving the app during a game** (home, app switch, screen off):
   - the game is **paused** (new setting `gui.mobile_pause_in_background`, default on; not for multiplayer clients, who can't pause);
   - it's **saved** to `save/autosave/background.sav` (a threaded save);
   - the activity is told that this save **resumes the game** if Android kills the app.
2. **While a game runs**, a **foreground service** (`GameService`) shows a quiet notification, "Game in progress". That raises the app's priority, so Android (and MIUI) is much less likely to kill it in the background. Tapping the notification returns to the game.
3. **Starting the app after Android killed it**: the activity passes `-g <background.sav>` and the game continues where the player left it, paused.
4. **Leaving the game on purpose** (back to the main menu, or Quit) clears the resume save, so the next start shows the main menu as usual. Swiping the app away from recent apps doesn't count as on purpose: the next start resumes.

### Steps

1. **`src/mobile_app.cpp/.h`** (new): `MobileAppWillEnterBackground()` (pause, save, set the resume save), `MobileAppDidEnterForeground()`, `MobileAppTick()` (tells the platform when a game starts or ends; clears the resume save when the game is left while the app is in front).
2. **`src/video/sdl3_v.cpp`**: an **SDL event watch** (`LifecycleEventWatch`, added after the window is created) for `SDL_EVENT_WILL_ENTER_BACKGROUND` / `SDL_EVENT_DID_ENTER_FOREGROUND`. `PollEvent()` sets `_sdl_polling` around `SDL_PollEvent()`, and the watch only saves while it's set (see the findings). `MobileAppTick()` runs in `InputLoop()` on Android.
3. **`src/os/android/android_bridge.cpp/.h`**: `AndroidSetGameRunning(bool)` and `AndroidSetResumeSave(path)`, JNI calls to the activity's `setGameRunning(Z)V` and `setResumeSave(Ljava/lang/String;)V`.
4. **Setting** `gui.mobile_pause_in_background`: `settings_type.h`, `table/settings/gui_settings.ini`, `settingentry_gui.cpp` (Interface, with the other mobile settings), `lang/english.txt`.
5. **Java** (`os/android/project/app/src/main/`):
   - `GameService.java` (new): foreground service of type `specialUse`, notification channel "Game in progress" (low importance, no sound), tap opens the activity, `START_NOT_STICKY`.
   - `OpenTTDActivity.java`: `setGameRunning()` starts/stops the service and asks once for `POST_NOTIFICATIONS` (Android 13+); `setResumeSave()` stores the path in SharedPreferences with `commit()`; `getArguments()` adds `-g <path>` once if a resume save is stored; `onDestroy()` stops the service.
   - `AndroidManifest.xml`: `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_SPECIAL_USE`, `POST_NOTIFICATIONS`; the `<service>` with `foregroundServiceType="specialUse"`, the `PROPERTY_SPECIAL_USE_FGS_SUBTYPE` explanation and `stopWithTask="true"`.
   - `res/drawable/ic_notification.xml`: a monochrome locomotive for the status bar.

### Findings

- **SDL 3.4 on Android blocks the main loop while the app is in the background** (`SDL_HINT_ANDROID_BLOCK_ON_PAUSE`, default on). So game time doesn't pass in the background anyway; the pause setting decides whether the game is paused **when the player comes back** (and in the save that resumes it).
- **The "entering background" events arrive too late through `SDL_PollEvent()`.** In `Android_PumpEvents()` (`src/video/android/SDL_androidevents.c`), the pause handler queues `WILL_ENTER_BACKGROUND`/`DID_ENTER_BACKGROUND` and sets `Android_Paused`, and the same loop then waits with an infinite timeout (`GetLifecycleEventTimeout()` checks the global, not its `paused` argument). The events are only returned after the app comes back. SDL's own advice is to handle life cycle events in an **event watch** (`SDL_AddEventWatch`), which runs as the event is queued, on the main thread, inside our `SDL_PollEvent()`.
- That's inside `VideoDriver::Tick()`'s `game_state_mutex` lock, so pausing (a command) and starting a save from the watch is as safe as handling a click. The watch checks `_sdl_polling` in case SDL pumps life cycle events from somewhere else (`Android_WaitActiveAndLockActivity()`).
- **The save is threaded** (`SaveOrLoad(..., threaded = true)`): the main thread then blocks in SDL's pump, and the save thread carries on. The foreground service keeps Android from freezing the process meanwhile.
- **`SaveOrLoad()` with `Subdirectory::None` takes a full path.** The path is `FioFindDirectory(Subdirectory::Autosave) + "background.sav"` (the directory comes with a trailing separator). `-g` accepts the same full path.
- **Foreground service types:** Android 14+ requires one, and none fits a game. `specialUse` is the honest choice; it needs `FOREGROUND_SERVICE_SPECIAL_USE` and a subtype property. Fine for sideloading; the Play Store asks for a justification.
- `Notification.Builder.setForegroundServiceBehavior()` is API 31; without it, Android may delay showing the notification by 10 s.

- **Building while auto-paused** first looked like success ("Built station · £0"): a command refused because the game is paused never reaches `InternalPostResult()`, so the capture saw nothing. `InternalPostBefore()` now records "Not allowed while paused" in an active `CommandResultCapture` (and shows no popup). `MobileBuildReestimate()` runs when the pause state changes, so Build comes back after unpausing.

### Verified on the phone (26 Nov)

- Starting a game asks once for notifications; `dumpsys activity services` shows `GameService` with `isForeground=true types=0x40000000` (specialUse).
- Home → log shows `setResumeSave(.../save/autosave/background.sav)`.
- `adb shell am kill …` while in the background: **the process survived** (same pid).
- `adb shell am force-stop …`, then launching: the activity logged "Resuming the game from …background.sav" and the game opened paused at the date it was left (`A23`).
- Home and back without a kill: same process, game paused.
- Installing an update over a running game also resumes it (the install kills the process; the resume save was set when it left the screen).

### Tips

- **Simulate Android killing the app:** `adb shell am kill io.github.warmmatetea.touchttd` only kills cached background processes, so with the service running it does nothing (that's the point). `adb shell am force-stop …` kills it outright; SharedPreferences survive, so the next start resumes. `run-as` doesn't work: release builds aren't debuggable.
- Watch the bridge calls: `adb logcat -s TouchTTD:*`.

## Part B: Round 3 (stations, depots, info windows, fixes)

### Stations (`src/mobile_build.cpp`, `src/rail_gui.cpp`)

1. **The station tool always places by sizing in the mobile layout** (`PlaceRail_Station()`: `VpStartPlaceSizing(..., VPM_X_AND_Y_LIMITED, DDSP_BUILD_STATION)`), and `MobileBuildReviewWanted()` reviews `DDSP_BUILD_STATION`.
2. **`MobileBuildOnRelease()`**: a release without movement is a tap → `PlaceStationFootprint()` makes a footprint of the picker's size **centred on the tapped tile** (`_mb.station_anchor`, `station_centred`). A dragged area keeps its rectangle and `SetRailStationSizeFromFootprint()` updates the picker (only when both sides are ≤ 7; the picker label then says "Platform length: 9").
3. **`ShowFrozen()`** freezes any selection on the map (it replaced the "back to review" code in `MobileBuildConfirm()`), and sets the catchment outline (`SetTileSelectBigSize()`) for stations when "Show catchment area" is on.
4. **Picker changes resize in place**: `BuildRailStationWindow::OnMobileClick()` → `MobileBuildStationResize(x, y)`.
5. **Handles**: four corners for stations (`GetHandles()`, `GetOppositeHandle()`); dragging one resizes as an area (with the station spread limit).
6. **Move by dragging inside** (stations and depots): `MobileBuildOnViewportClick()` starts a sizing drag with `_mb.moving`; `MovedSelection()` shifts the original rectangle by the finger's tile delta, clamped to the map; `MobileBuildOnSizingUpdate()` writes it into `_thd` each frame.
7. **Join**: `FindJoinableRailStations()` (new, `station_gui.cpp`, wraps `FindStationsNearby<StationTypeFilter>()`) gives the candidates; the default is what the desktop does without Ctrl (join the only adjacent station). An explicit choice is passed by `HandleStationPlacement()` through `GetMobileStationJoin()` as `(station or NEW_STATION, adjacent = true)`, so the desktop join window never opens.
8. **Undo**: `RemoveFromRailStation` over the footprint, keeping track that was there before; not offered when the footprint overlapped an existing station.
9. The tool and picker stay after Build (`CcStation()`/`CcRailDepot()` don't reset the tool in the mobile layout).

### Station picker, mobile layout

`_nested_mobile_station_builder_widgets` + `_mobile_station_builder_desc`, shown with `MobileShowInSheet()`. New IDs `WID_BRAS_TITLE/CLOSE/COVERAGE_TOGGLE/SEL_JOIN/JOIN/TRACKS_LABEL/LENGTH_LABEL`. The window gets a `mobile` flag; mobile paths in `OnInit()` (no badge filters, see findings), `OnPaint()`, `DrawWidget()`, `GetWidgetString()`, `OnDropdownSelect()`, `OnClick()`. NewGRF station classes/types: **not in the mobile picker yet** (the design's Type row).

### Depots

`MobileBuildDepotTap()` makes a single-tile pending selection; `GetAutoDepotDirection()` faces the one neighbouring track end (`DiagdirReachesTracks()`), else the last direction. **Turn** (`WID_RAT_ROTATE` in `WID_RAT_SEL_ROTATE`, a text button: there is no rotate sprite) turns it clockwise. The viewport draws the entrance with the one-way road arrow (`DrawMobileDepotArrow()`). Build replays `PostRailDepot()`; Undo demolishes the tile. No desktop depot picker in the mobile layout.

### Waypoints

Reviewed like track (`DDSP_BUILD_STATION` with `VPM_X/Y_LIMITED`); own status texts; Undo removes the waypoint and keeps the rail.

### Toolbar

- Picker plane (status · ✕ · Build) while the station picker is open, and in landscape while any sheet is open (the tools don't fit beside it). `RelocateMobileWindows()` calls `toolbar->InvalidateData()` so it re-plans before being placed.
- Disabled Build is grey. Errors are capitalised (`CapitaliseError()`) and drawn after `SPR_WARNING_SIGN`.

### Info windows in the sheet slot

- **`MobileShowInfoWindow()`**: over a hub's list when a hub is open, else as the sheet content.
- **Town** (`_nested_town_mobile_view_widgets`): no viewport; label/value rows (`GetMobileInfoRows()`), Show on map · Local authority · Cargo graph. `TownAuthorityWindow::GetTownRatingString()` shared.
- **Station** (`_nested_station_view_mobile_widgets`): Waiting/Accepts/Ratings tabs, Show on map · Trains · Catchment; the desktop's sort controls stay in a never-shown selection (`WID_SV_SEL_HIDDEN`) because the window's code uses them.
- **Minimap** (`_nested_smallmap_mobile_widgets`): mode dropdown, Names and Legend toggles, map or legend, gesture hint; the desktop buttons are in a hidden selection. Gestures in `mobile_touch.cpp`: `TouchMode::MapPan` (one finger), two fingers pan/pinch the minimap when they start on it (`PanMinimap()` → `OnScroll()`, `ZoomMinimap()` → `OnMouseWheel()`).

### Other design fixes

Text inset `MOBILE_TEXT_INSET` (titles via `DrawMobileTitle()`, town rows); normal font for portrait dock labels; More flyout cells 56×48 fill 0, anchored above the whole toolbar; bridge speed line black; rail type flyout as wide as its longest row (capped at the visible map width − 16); **flush chrome**: the dock and rail toolbars reach the screen edges, with spacer widgets (`WID_MD_GAP_*`, `WID_RAT_GAP_BOTTOM`) sized by `GetMobileEdgeGaps()`; Android reports the **tappable-element** inset of the navigation bar, not its full height; **reveal the pending selection** (`MobileBuildRevealPending()` from the layout pass).

### Findings

- **`PickerWindow::OnInit()` adds badge filter dropdowns** into `WID_PW_BADGE_FILTER`; a layout without the type list crashes there (`NWidgetContainer::Clear` on null). Skip it in a picker layout without class/type lists.
- **Hidden widgets in a vertical container: use `SZSP_HORIZONTAL`, not `SZSP_NONE`.** `SZSP_HORIZONTAL` is zero-sized but horizontally resizable; `SZSP_NONE` has resize step 0, and the container's step (the LCM) becomes 0, so the window can't widen. Rows of fixed buttons need resizable spacers for the same reason.
- **A tile outline can't mark one tile inside a selected rectangle**: neighbouring tiles' white outlines are drawn over its edges. The corner handles of a station are therefore hard to see; open question for the design.
- **`CommandResultCapture::first_only`**: building a depot also posts track to connect it (`CcRailDepot()`); that follow-up's "Already built" error made the build look failed.
- **HyperOS draws three-button navigation on opaque black** even for an edge-to-edge window; the theme's `android:navigationBarColor` transparent + `enforceNavigationBarContrast` false fixes it (the runtime `Window.setNavigationBarColor()` alone didn't).
- **The phone's game font has no ↗/↘** (U+2197/2198); headless Linux had a TrueType fallback. The orientation buttons say "Along NE-SW / NW-SE".
- **A widget in a hidden selection plane keeps its old position**: a widget in a hidden selection plane keeps its old position, so code that draws into it by rectangle (the station view's `OnPaint()`) must skip it.

### Tips

- Headless: if Xvfb says "Server is already active for display N" with no lock file, the previous one is still shutting down; use another `DISPLAY_NUM`.
- The 8 s Undo offer expires between slow manual steps; script Build and Undo in one command.
- adb taps go to the topmost window: a floating window of another app (Tailscale's) swallowed them.
- Privacy: crop device screenshots below the status bar; a notification can drop over the game at any time (it happened once; the screenshot was deleted unread).
