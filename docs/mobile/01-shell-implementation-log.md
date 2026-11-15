# Round 1 implementation log: the mobile app shell

A running journal of how the round 1 design handoff (status pill, hub dock, flyouts, hub sheets, news banner, minimap button, layout pass) was built. Each step lists what was done, what was found in the code, and tips for anyone doing similar work later.

Design source: `openttd-mobile-handoff-round1.md` (the design session's handoff). The design canvas is linked from there.

---

## Step 0: Environment

**Done**
- Installed `xdotool` and `imagemagick` (`apt-get install -y xdotool imagemagick`) so the game can be driven and screenshotted under `Xvfb` (a virtual X display).
- Downloaded OpenGFX 7.1 into `~/.local/share/openttd/baseset/`. The game was built but had never been run here, and it can't start without base graphics.

**Tips**
- OpenTTD reads base sets straight from `.tar` files, so you don't need to unpack OpenGFX's inner tar.
- `unzip` wasn't installed; `python3 -m zipfile -e file.zip dir/` does the same job.

---

## Step 1: Reading the code before writing any

The questions that had to be answered first, and where the answers are:

| Question | Answer | Where |
|---|---|---|
| Where are the toolbar and status bar created? | `ShowVitalWindows()` calls `AllocateToolbar()` and `ShowStatusBar()`. | `src/main_gui.cpp` |
| What runs when the screen size changes? | `GameSizeChanged()` → `RelocateAllWindows()`. GUI-scale changes go through `ReInitAllWindows()`, which also ends in `RelocateAllWindows()`. | `src/main_gui.cpp`, `src/window.cpp` |
| How does Pause work? | `Command<Commands::Pause>::Post(PauseMode::Normal, _pause_mode.None())`. Fast-forward is local: `ChangeGameSpeed(_game_speed == 100)`. | `src/toolbar_gui.cpp` (`ToolbarPauseClick`) |
| How does the toolbar keep Pause lowered? | It polls every 30 ms with an `IntervalTimer<TimerWindow>` and toggles the lowered state. | `MainToolbarWindow::refresh_interval` |
| How does the news ticker reach the status bar? | `news_gui.cpp` sets `_statusbar_news` and calls `InvalidateWindowData(WindowClass::Statusbar, 0, SBI_SHOW_TICKER)`. The status bar scrolls it on a 15 ms timer. | `src/news_gui.cpp`, `src/statusbar_gui.cpp` |
| What assumes the toolbar/status bar exist? | `GetToolbarAlignedWindowPosition()` **asserts** the toolbar exists. It's used by every construction toolbar. `NewsWindow`, the network chat window and `RelocateAllWindows()` dereference the status bar without a null check. News is skipped completely when there's no status bar. | `grep -rn "FindWindowById(WindowClass::MainToolbar\|FindWindowById(WindowClass::Statusbar"` |
| Can a window's caption be non-draggable? | No. Clicking any `WWT_CAPTION` starts a window drag (`DispatchLeftClickEvent`). The mobile windows use a custom-drawn `WWT_PANEL` as a caption instead. | `src/window.cpp` |
| Do plain panels show a pressed state? | Yes. `WWT_PANEL` draws lowered when `SetWidgetLoweredState` is set, and calls `DrawWidget()` for its content. That makes it the base for the "icon above label" buttons. | `src/widget.cpp` |
| Is `WWT_PUSHBTN` a container? | No. It's a leaf (no `EndContainer()`), even though it's `WWT_PANEL | WWB_PUSHBUTTON`. A `WWT_PANEL` *is* a container and needs `EndContainer()`. | `src/statusbar_gui.cpp` |
| Is resizing free-form? | No. `ResizeWindow()` asserts the size change is a multiple of the window's resize step. Round deltas down to the step first (see `FitWindowTo()` in `mobile_gui.cpp`). | `src/window.cpp` |
| Does the script API care about new window classes or widget headers? | Both are exported to scripts automatically at build time (`script_window.hpp.in` globs `widgets/*_widget.h`). New `WindowClass` values go **at the end** of the enum so existing values don't shift. | `src/script/api/script_window.hpp.in` |

**Tip:** `grep -n "^static CallBackFunction" src/toolbar_gui.cpp` lists every desktop toolbar action in one go. It's the fastest map from "button" to "function that does the work".

---

## Step 2: Architecture of the shell

- One new setting, `gui.mobile_layout` (Settings › Interface). When it's on and a normal game is running, `ShowVitalWindows()` creates the mobile shell **instead of** the main toolbar and status bar. Desktop is untouched when it's off.
- All shell windows live in **`src/mobile_gui.cpp`**, with widget IDs in **`src/widgets/mobile_widget.h`** and the public functions in **`src/mobile_gui.h`**.
- **Layout pass:** `RelocateMobileWindows()`, called at the end of `RelocateAllWindows()` and whenever something in the shell opens or closes. It runs as the design's §4.8 describes.
- **Sheet slot:** the design wants existing windows (Town directory, Industry directory…) to show inside a sheet. The shell opens them with their normal `Show…()` function, finds the frontmost window of the expected `WindowClass`, and then moves and resizes it into the slot. That avoids touching every window's code.
- **News:** in the mobile layout every news item goes to the banner (including "full" newspaper items); tapping the banner opens the newspaper.

Files touched outside the new ones:

| File | Change |
|---|---|
| `window_type.h` | 7 new `WindowClass` values, appended at the end |
| `window.cpp` | z-order for the shell windows; flyout "tap outside" hook in `MouseLoop`; `GetToolbarAlignedWindowPosition` fallback; status bar null check; layout pass call; Escape → `MobileHandleBack()` |
| `main_gui.cpp` | `ShowVitalWindows()` branches to the mobile shell |
| `statusbar_gui.cpp/.h` | `DrawScrollingStatusText` shared (with a colour), `IsNewsTickerShown()` aware of the banner |
| `news_gui.cpp/.h` | banner routing, `HasNewsArea()`, `ShowStatusbarNewsMessage()`, null check |
| `network/network_chat_gui.cpp` | status bar null check |
| `settings_type.h`, `table/settings/gui_settings.ini`, `settingentry_gui.cpp` | the two new settings |
| `settings_gui.cpp`, `gui.h` | `ShowGameSettings()` opens Game options on the Advanced tab |
| `town_gui.cpp` | mobile layout of the Town directory |
| `lang/english.txt` | new strings (`STR_MOBILE_*`, `STR_CONFIG_SETTING_MOBILE_*`) |

**Tip:** a new setting needs four things: the field in `settings_type.h`, the `[SDTC_…]` block in `table/settings/*.ini`, its strings in `english.txt`, and an entry in `settingentry_gui.cpp` to appear in the settings window. `post_cb` can be an inline lambda.

---

## Step 3: Writing the shell

Written in this order, each part on the pattern of the screenshot window (layout list → `WindowDesc` → `struct …Window`):

1. `widgets/mobile_widget.h`: one enum per window.
2. `mobile_gui.cpp`:
   - status pill
   - hub tab window and sheet-slot helpers
   - Build flyout
   - Menu flyout (two `WindowDesc`s sharing the widget IDs: one column for portrait, two for landscape)
   - dock (two `WindowDesc`s, as the design asked)
   - minimap button
   - news banner
   - layout pass
3. Hooks in the existing files (table in step 2).
4. The mobile Town directory: a second nested-widget list and `WindowDesc` in `town_gui.cpp`, plus a `mobile` flag in the window. The window code is shared; only the layout, row drawing and the sort dropdown differ.

**Findings**
- **"Icon above label"** (design custom #10) turned out to be cheap: a `WWT_PANEL` with a widget ID gets `DrawWidget()` calls, and honours the lowered state for toggles. See `DrawIconAboveLabel()`.
- **Tabs with a variable count:** one layout with five tab buttons, the fifth inside an `NWID_SELECTION` that is set to `SZSP_NONE` (zero size) for hubs with four tabs. Colours and labels are set before `FinishInitNested()` via `NWidgetCore::colour` and `SetString()`.
- **Showing a widget only in one orientation:** another `NWID_SELECTION` (the ▲/▼ button exists only in portrait). Changing the plane needs `ReInit()`.
- **Finding the window a `Show…()` function opened:** walk `Window::IterateFromFront()` for the first window of the expected `WindowClass`. Works whether the window was new or already open and brought to the front.
- **`GetWidget<T>()` asserts the type.** Asking for an `NWidgetCore` on an `NWID_SELECTION` crashes in a debug build, so loop over an explicit list of IDs instead of a range.
- **The Town directory row height** comes from `UpdateWidgetSize()` for the list (`resize.height = row height`). Raising it to 40 GUI px there gives 40 px rows *and* 40 px scroll steps.

## Step 4: First build: three errors, and what they teach

| Error | Cause | Fix |
|---|---|---|
| `'WID_MH_BACKGROUND' conflicts with a previous declaration` in `script_window.hpp` | `WID_MH_` is already the message-history prefix (`news_widget.h`). All widget enums end up in one script namespace. | Renamed to `WID_MHUB_`. Check with `grep -rn "WID_XX_" src/widgets`. |
| `'_last_built_railtype' was not declared` | It's declared in `toolbar_gui.h`, not `rail_gui.h`. | Include `toolbar_gui.h`. |
| `'ShowMessageHistory' was not declared` in `news_gui.cpp` | `news_gui.cpp` didn't include its own header. | Include `news_gui.h`. |

Plus one `-Wreorder` warning: members are initialised in *declaration* order, so the constructor's init list should follow that order.

## Step 5: Running it: one bug

**Bug:** at start the top-left showed a stray minimap button and a second status pill. **Cause:** `ShowVitalWindows()` is called more than once during game start (also from `genworld.cpp`), and the shell windows were created unconditionally. **Fix:** create each one only if `FindWindowById()` doesn't find it. The desktop toolbar never hit this because desktop windows are usually allocated with `AllocateWindowDescFront`, which brings an existing window to the front instead of creating another.

## Step 6: Verification (screenshots in `screenshots/round1/`)

Driven with `xdotool` under Xvfb at 2714×1222 and 1222×2714 (the reference phone at 300%). Each of these was checked on screen:

- Landscape idle (`L01`).
- World hub with the mobile Town directory (`L02`).
- Row tap scrolls the map and highlights the row (`L03`).
- (i) opens the town window over the list (`L04`).
- Escape (Android back) closes the overlay (`L05`).
- Vehicles and Company hubs (`L06`, `L07`).
- Minimap in the sheet, its button moved left of the panel (`L08`).
- Build flyout, with Tram disabled because the map has no tram types (`L09`).
- Rail toolbar opens and Build lowers (`L10`); Build again closes the toolbar (`L11`).
- Two-column Menu (`L12`).
- News banner scrolling real news (`L09`–`L12`).
- Portrait: idle, half sheet, full sheet, one-column Menu above the dock, Build flyout centred on Build (`P01`–`P05`).
- Desktop layout unchanged with the setting off (`D01`), and switched on at runtime from the console (`D02`).
- `ctest`: 99/99 passed.

**Not verified:** a live rotation (Xvfb can't rotate; each orientation was tested from a fresh start), and anything touch-specific (no touch device yet).

**Findings worth knowing**
- **Clicks fire on button *press*** in OpenTTD (`MouseLoop` in `window.cpp`), not release. That matters for the gesture layer and long-press.
- **Desktop windows docked into the 300 px side panel overflow it**, because their minimum widths are larger. Docking existing windows works mechanically, but each needs a mobile layout eventually.
- **A docked list can't fill the slot exactly**, because windows resize in whole row steps. `FitWindowTo()` rounds down, so a strip of map up to one row tall can show below the list.
- **Escape cancels a drag in progress** without building (seen while taking the demolish reference shot).

## Step 7: Reference screenshots for the designer (`screenshots/round2-reference/`)

All 15 shots from the design handoff's §9a, desktop layout at 200%, by driving the real UI:
- **Round 2:** rail toolbar, railtype dropdown, autorail mid-drag, Shift-release cost estimate, demolish drag, signal picker, bridge picker, tunnel error.
- **Rounds 3–4:** station picker, station footprint with catchment, depot picker, depot window with a train, build-vehicle list, orders with two orders, vehicle window.

**Tips**
- **Holding a drag for a screenshot:** `xdotool mousemove X Y mousedown 1`, a few `mousemove`s, shoot, then `mouseup 1`. For the cost estimate, `xdotool keydown shift` just before `mouseup`.
- **Two stations next to each other merge into one.** For an orders screenshot with two different stations, build them apart.
- **Order "Go To" needs the orders window focused.** The first attempt clicked the station and opened its window instead; the second worked.

## Step 8: Answers to the designer's feasibility questions

Written up in `handoffs/03-impl-to-design-round2.md` §3. The code locations, for whoever implements them:
- **Cost estimate:** `CommandHelperBase::InternalPostBefore` / `InternalPostResult` in `src/command.cpp` (`estimate_only`).
- **Build-on-release:** `VpHandlePlaceSizingDrag()` in `src/viewport.cpp` → `Window::OnPlaceMouseUp()`.
- **Soft keyboard:** `VideoDriver_SDL_Base::EditBoxGainedFocus()` in `src/video/sdl2_v.cpp` calls `SDL_StartTextInput()`.
- **Click on press:** `MouseLoop()` in `src/window.cpp` sets `MouseClick::Left` when `_left_button_down && !_left_button_clicked`.
