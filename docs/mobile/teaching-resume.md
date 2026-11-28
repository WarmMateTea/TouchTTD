# Resuming the teaching

For the next mentor session (or for you, reading it yourself). The original mentoring brief is [`handoffs/00-mentor-handoff.md`](handoffs/00-mentor-handoff.md). This file records where the learning stands and how the new code can be used to teach.

## Where things stand (14 Nov 2026)

- **Learning path:** you'd had the recap (the three parts of a window: layout list, `WindowDesc`, window struct) and the suggested first experiment on `src/screenshot_gui.cpp`. You hadn't reported trying it yet. Start by asking.
- **In the meantime, round 1 of the mobile UI was built by Claude** (not by you), so there's a working mobile shell in the repo to learn from. It's switched on with the `gui.mobile_layout` setting. See [`01-shell-implementation-log.md`](01-shell-implementation-log.md) for every step and finding.
- **The design session** is working on round 2 (track laying), from [`handoffs/03-impl-to-design-round2.md`](handoffs/03-impl-to-design-round2.md).
- **Android:** plan in [`02-android-build-plan.md`](02-android-build-plan.md); waiting on your answers in its section 5.

## Update, 26 Nov 2026

Since the first version of this file, rounds 2, the touch gesture layer and the device polish were built (see `session-state.md`). New code that makes good teaching material:
- `src/mobile_touch.cpp`: a small state machine turning finger events into mouse events. Good for learning how input reaches windows (`HandleMouseEvents()` → `MouseLoop()`).
- `src/mobile_build.cpp`: shows the command system in practice: posting a command, test-mode estimates, results.
- `os/android/project/.../OpenTTDActivity.java` + `src/os/android/android_bridge.cpp`: how C++ calls into Java (JNI).

The user has not yet reported trying the screenshot window exercise; ask first.

## Update, 26 Nov 2026 (evening)

Round 3 adds good examples of **one window class, two layouts** (the most important technique of this project), each with a small `mobile` flag in the window:
- `src/town_gui.cpp`: `_nested_town_mobile_view_widgets` vs `_nested_town_game_view_widgets`; compare `DrawMobileInfo()` with the desktop `DrawWidget()`.
- `src/station_gui.cpp`: the mobile station window keeps the desktop's controls in a never-shown `NWID_SELECTION`, because the window's code still talks to them. A good puzzle: why does the window break if they're simply left out?
- `src/mobile_app.cpp`: a tiny module that reacts to the app going to the background. It shows how SDL events reach the game (`src/video/sdl3_v.cpp`, `LifecycleEventWatch`).
- Exercise idea: in `TownViewWindow::GetMobileInfoRows()`, add a row "Stations nearby".

## Suggested path from here

The original path still holds. The new code adds examples that are **closer to your goal** than the screenshot window. Same pattern, but now it's mobile UI you care about.

1. **The screenshot window experiment** (unchanged): add `SetMinimalSize(0, 60)` to a button, rebuild, look. That's 10 minutes, and it builds intuition for "the layout list decides the size".
2. **Read `MobileStatusWindow`** in `src/mobile_gui.cpp` (about 70 lines, search for `/* --- Status pill`). It has all three parts, plus two things the screenshot window doesn't show:
   - `IntervalTimer<TimerWindow>`: how a window keeps itself in sync with the game (the Pause button lowering itself).
   - `DrawWidget()` drawing two lines of text, and `UpdateWidgetSize()` making room for them.
   - Exercise: make the money line show "(paused)" while the game is paused.
3. **Compare the two Town directory layouts** in `src/town_gui.cpp`: `_nested_town_directory_widgets` (desktop) and `_nested_town_directory_mobile_widgets` (mobile). Same window code, two layouts. This is *the* technique of this project. Then read `DrawMobileList()` for custom row drawing.
   - Exercise: put the rating icon back into the mobile rows (the desktop list draws it; copy that bit).
4. **The dock and its "icon above label" buttons** (`MobileDockWindow`, `DrawIconAboveLabel()`): a `WWT_PANEL` as a custom button, and lowered state as "this hub is open".
   - Exercise: change Build's colour to blue in portrait only.
5. **Window management:** `OpenSheetContent()` and `RelocateMobileWindows()`. Moving and resizing other windows, and why resizing goes in steps (`FitWindowTo()`).
6. **The command system:** `MobileStatusWindow::OnClick` → `Command<Commands::Pause>::Post(...)`. Then follow it into `src/command.cpp` to see the "estimate only" path the round 2 cost preview will use.
7. **Then** `company_gui.cpp`, as originally planned. It should now read much more easily.

## Good debugging targets

- Breakpoint in `MobileDockWindow::OnClick` and tap World. Step through `ToggleMobileHub()` → `OpenSheetContent()` → `ShowTownDirectory()` → back into `RelocateMobileWindows()`. That one walk covers most of the shell.
- Breakpoint in `RelocateMobileWindows()`, then resize the game window: see who calls it (`GameSizeChanged()` → `RelocateAllWindows()`).

## New examples from round 5

- **A table of rows with function pointers:** `_mobile_options[]` in `mobile_settings_gui.cpp`. Each row is data (name, kind) plus small lambdas for "current value", "build the list", "apply the choice". Compare with a class per row: the table keeps 15 rows in one screen of code.
- **Extracting a shared function from a window:** `ChangeLanguage()`, `ChangeCurrency()`... in `settings_gui.cpp` were bodies of `GameOptionsWindow::OnDropdownSelect()`. Moving them out lets the desktop window and the mobile Settings call the same code; a good small refactoring exercise to read as a diff (`git show` the batch 2 commit, `settings_gui.cpp`).
- **A virtual with a default, overridden where needed:** `Window::GetMobileDropDownTitle()` (returns empty) and its overrides in the settings and train windows; the dropdown asks the parent window and falls back to the button's label.

- **Sentinel values in one int:** the vehicle window's ⋯ menu (`ShowTitleMenu()` / `OnTitleMenu()` in `mobile_vehicle_gui.cpp`) puts Rename, a heading and every group in one dropdown by giving each entry an int: group ids for the groups, and two constants above any group id for Rename and the heading. Compare with the nested-dropdown version described in the batch 10 log, which failed because one tap picked from both lists.
- **A re-entrancy guard:** `_opening_sheet_content` in `mobile_gui.cpp`. `ShowCompanyFinances()` asks the hub to open its Finances tab, and the tab opens the window by calling `ShowCompanyFinances()` again; the flag tells the second call to build the window instead of looping.

## Tools you can use while learning

- `tools/mobile/run_headless.sh` and `shot.sh` (see [`00-toolchain.md`](00-toolchain.md)) if you're working on the server without a screen.
- On a desktop: `./openttd -r 1357x611` with interface size 150% is the phone at half size.
