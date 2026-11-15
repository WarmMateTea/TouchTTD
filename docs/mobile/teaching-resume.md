# Resuming the teaching

For the next mentor session (or for you, reading it yourself). The original mentoring brief is [`handoffs/00-mentor-handoff.md`](handoffs/00-mentor-handoff.md). This file records where the learning stands and how the new code can be used to teach.

## Where things stand (14 Nov 2026)

- **Learning path:** you'd had the recap (the three parts of a window: layout list, `WindowDesc`, window struct) and the suggested first experiment on `src/screenshot_gui.cpp`. You hadn't reported trying it yet. Start by asking.
- **In the meantime, round 1 of the mobile UI was built by Claude** (not by you), so there's a working mobile shell in the repo to learn from. It's switched on with the `gui.mobile_layout` setting. See [`01-shell-implementation-log.md`](01-shell-implementation-log.md) for every step and finding.
- **The design session** is working on round 2 (track laying), from [`handoffs/03-impl-to-design-round2.md`](handoffs/03-impl-to-design-round2.md).
- **Android:** plan in [`02-android-build-plan.md`](02-android-build-plan.md); waiting on your answers in its section 5.

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

## Tools you can use while learning

- `tools/mobile/run_headless.sh` and `shot.sh` (see [`00-toolchain.md`](00-toolchain.md)) if you're working on the server without a screen.
- On a desktop: `./openttd -r 1357x611` with interface size 150% is the phone at half size.
