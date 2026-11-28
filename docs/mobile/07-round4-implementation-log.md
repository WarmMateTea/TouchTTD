# Round 4 implementation log

Design input: [`handoffs/08-design-to-impl-round4.md`](handoffs/08-design-to-impl-round4.md): buying a train, depot, train window, orders (part A), and the menus (part B), plus fixes and answers to our round 3 questions.

## Fixes from the design review (§1, §2)

- **Picker labels in the normal font**; orientation buttons "NE-SW" / "NW-SE". The sprite font (what the phone uses) has no en dash, arrows or "✕": it covers Latin-1 only (`src/table/unicode.h` remaps a few codes to game glyphs: `{RIGHT_ARROW}`, `{LEFT_ARROW}`, `{CHECKMARK}`, `{UP_ARROW}`...). Use those control codes instead of Unicode symbols.
- **Station window**: "Nothing waiting yet"; the waiting list resizes **by pixel** (`resize.height = 1`, capacity computed in `OnResize()`), so the window fills the sheet and the footer sits on its bottom edge. The gesture layer's list scrolling now takes its step from the list's height and capacity, so pixel-resizing lists scroll by rows.
- **"Show tools"**: in landscape with a sheet open, no tool and nothing pending, the rail toolbar shows "Tools are hidden while a panel is open" and a Show tools button (`WID_RAT_SEL_CANCEL`, plane 2 of `WID_RAT_SEL_CONFIRM`).
- **Minimap main-view rectangle**: the desktop draws four 4-pixel corner marks, 1 screen pixel thin; the mobile layout draws the full rectangle, 1 GUI pixel thick (`DrawMapIndicators()`).
- **Handles (§2.1)**: the blue outline of a handle tile goes into a second sprite list (`_vd.handle_sprites_to_draw`, drawn after all tile sprites), so neighbouring white outlines can't cover it; a 6×6 blue knob with a white border is drawn at the centre of each handle, after all sprites (`_vd.handle_knobs`).
- **Paused state (§2.5)**: the pill reads "Paused" and its button shows the play sprite; returning to the app paused shows a yellow banner "Paused while you were away", tapping it unpauses (`ShowMobileResumeBanner()`); while paused, a selection in review says "⚠ Paused · tap ▸ to build" and Build stays disabled (`CommandResultCapture::blocked_by_pause`).
- **Static news headline (§2.3)**: the banner draws the ticker text truncated, no scrolling.
- **Touch dropdown (§2.2)** in `src/dropdown.cpp`, for every dropdown in the mobile layout: rows ≥ 40 GUI px (`GetDropDownItemHeight()`), text inset 8, the selected row lowered with a tick. Landscape: anchored to the button, at most 8 rows, at most the visible map's width. Portrait: a full-width sheet along the bottom of the visible map, at most 60% of the screen. Back closes it. Not done: the portrait sheet's title (the dropdown doesn't know its button's label).

## Part A: from depot to running train

New file `src/mobile_vehicle_gui.cpp` (+ `.h`). New windows rather than layouts of the desktop ones (built around drag and drop); they reuse the desktop's window classes so the game's invalidations reach them:

| Window | Class / number | Opened by |
|---|---|---|
| `MobileDepotWindow` | `VehicleDepot` / tile | `ShowDepotWindow()` for trains (tap a depot) |
| `MobileNewTrainWindow` | `BuildVehicle` / tile | "+ New train", "+ Add vehicles" |
| `MobileTrainWindow` | `VehicleView` / vehicle | `ShowVehicleViewWindow()` for trains, depot rows, the train list, "Next: orders" |
| `MobileStopsBarWindow` | `MobileStopsBar` (new) / 0 | adding stops |
| `MobileTrainListWindow` | `TrainList` / as desktop | `ShowVehicleListWindowLocal()` for the company's trains (Vehicles hub) and trains calling at a station |

- **Train being built**: `_mobile_train_target`. Buying uses a new command callback, `CcMobileBuildTrainPart` (appended to `_callback_tuple` in `network/network_command.cpp`): the first engine becomes the target; later vehicles are attached to its end with `MoveRailVehicle`, not relying on the desktop's "exactly one stopped train in the depot" rule.
- **Composition strip** (`DrawTrainStrip()`): vehicle sprites with an "X Sell" button under each; selling the engine of a longer train asks first (`ShowQuery`).
- **Orders**: rows with the station and a flags line; tapping a row opens its ⋯ menu as a touch dropdown (non-stop, load, unload, skip, delete → `ModifyOrder`, `SkipToOrder`, `DeleteOrder`). Long-press reordering is not built yet.
- **Adding stops**: the train window becomes the placement callback (`SetObjectToPlaceWnd(..., HT_RECT, this)`); a tap on a station tile reaches `OnPlaceObject()`, a tap on a station **sign** is caught first in `HandleViewportClicked()` (`GetStationSignAt()` + `MobileVehicleOnStationSignClick()`). Up to three chips suggest the two newest train stations and the one nearest the depot. The stops bar replaces the dock like the rail toolbar (`GetMobileBottomBar()` in the layout pass). Start is enabled from two orders.
- Lists in these windows resize by pixel (`GetRowCapacity()`), as the station window's.

### Findings

- **Text buttons size themselves to their text**: long station names on the chips widened the whole window past the sheet. Use plain push buttons and draw the text, truncated, in `DrawWidget()`.
- **A placement mode shows the floating red Cancel** (`WantToolCancel()`); the train window is excluded, the stops bar has Done.
- `RelocateMobileWindows()` must not call a window's full `InvalidateData()`: it runs during the game loop too (news), when `_current_company` can be invalid, and the rail toolbar's `OnInvalidateData()` checks rail types against it (assertion). `UpdateMobileRailToolbarPlanes()` only re-plans the mobile widgets.

### Tips

- `…/scratchpad/r4/flow.sh` (session scratch, not in the repo) replays a headless game to a running train; the steps are: Build › Rail, station at (1300,760) and (1300,300), depot at (1800,800), Escape twice, tap the depot, New train, buy, Next, tap a station sign, a chip, Start.

## Part B: the menus

`IsMobileMenus()` (new, `mobile_gui.cpp`) is the mobile setting at the main menu as well as in a game; `IsMobileLayout()` stays in-game only. Menus, dropdowns, confirmations, settings and save/load use the former.

- **Main menu** (`intro_gui.cpp`, `_nested_mobile_select_game_{landscape,portrait}_widgets`): a panel 380 wide on the left (landscape) or across the top (portrait); **Continue** (green, large) when a save exists: the background save, else the newest save (`GetMobileContinueSave()`), loaded paused (`MobileContinueGame()`; the pause is applied by `MobileAppTick()`, Android only). `RelocateMobileMainMenu()` follows rotations.
- **New game** (`src/mobile_newgame_gui.cpp`): full screen; presets Classic / Relaxed / Sandbox (designer's values, confirmed by the user on 28 Nov), climate, map size, towns, industries, terrain, start year (±10), "Advanced options" opens the desktop window. Start game → `StartNewGameWithoutGUI()`.
- **In-game Menu flyout**: Save, Load, Map tools, Transparency | Settings, Messages, Help, Exit to main menu, Quit game (§7).
- **Confirmation** (`misc_gui.cpp`): `ShowQuery()` takes a verb and whether it's destructive; the mobile layout shows the question as a coloured title, the consequence, Cancel and the verb on the right; landscape centred in the visible map, portrait along its bottom. A tap outside or Back cancels.
- **Save and load** (`src/mobile_saveload_gui.cpp`): list rows with name, "company · date · money" read from the save's header (`SaveOrLoad(..., SaveLoadOperation::Check)`, cached per row), "Saved 5 minutes ago"; the background save pinned first as "Your last game · LEFT THE APP"; an "Autosaves (n)" row; Load rows expand to Load and Delete; Save has a name field (company name) and asks before overwriting. In a game it's in the sheet; at the main menu full screen.
- **Settings** (`src/mobile_settings_gui.cpp`, new class `WindowClass::MobileSettings`): sections Phone, Building and vehicles, Economy/towns/competitors (curated lists of setting names) and links to the desktop windows for display/sound/language/saving (Game options), NewGRFs, scripts and All settings; rows of 56 with name, help (2 lines) and On/Off, a touch dropdown, or − value +; **search** across every setting of the desktop tree (`GetSettingsTree()`), with the page shown above each hit.
- **Top chrome against the cutout (§2.4)**: `OpenTTDSurface` sends the cutout's bounding rectangles and the visible status bar's height to the game (`nativeSetTopInsets`, JNI into `android_bridge.cpp`); the status pill and map button go 4 GUI px below the top unless they overlap a cutout rectangle, and 8 from the rounded corners (`GetTopChromeY()`).

### Findings (part B)

- **`IsMobileLayout()` is false at the main menu** (`_game_mode == Menu`): menu-level mobile UI needs its own check.
- **The setting titles take the value as a parameter** (`...: {STRING2}`); to get the bare name format with `STR_EMPTY, 0, 0` and strip the trailing ": ".
- **`SaveOrLoad(..., SaveLoadOperation::Check)`** fills `_load_check_data` (companies with name and money, current date) without loading the game; fast enough per row for a handful of saves.
- **JNI from Java into the game** works with a plain `extern "C" JNIEXPORT` function named `Java_<package>_<class>_<method>`, since the game library is loaded by `System.loadLibrary`; a call made before it is loaded throws `UnsatisfiedLinkError`, caught in Java.
- **Minimap zoom**: zoom level 1 (one tile per 4×2 pixels) is the most detailed the minimap can draw; zooming further needs its drawing to scale up (backlog).

## On the phone (28 Nov)

Verified: resuming paused with the yellow banner (tap continues), the top chrome against the punch-hole (`A01`), the new Menu flyout (`A02`), the portrait confirmation along the bottom of the map (`A03`), Exit to the main menu with Continue (`A04`), Settings › Phone and the Back key (`A05`), New game (`A06`).

Fixed during the test:
- **Settings opened under the in-game shell** (news banner over the first section, dock over the bottom): `WindowClass::MobileSettings` now has the z-priority of the on-screen keyboard, above the shell windows.
- **Back** now goes back a level in Settings and closes New game and save/load at the main menu (`MobileHandleBack()`).
- **Continue** said "background" (the file name); it now says "Your last game" (`GetMobileBackgroundSavePath()`).

Not tested on the phone yet: the depot-to-train flow, save/load lists, touch dropdowns (all verified headless).
