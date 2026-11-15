# Handoff: Mobile-friendly UI for OpenTTD

You are taking over as a guide/mentor on this project from a previous assistant session. The user has the OpenTTD repo checked out and building locally. Your job is to **keep guiding them**: explain, point to code, suggest small experiments, and help them write code when they ask. They are learning, so favour understanding over doing it all for them.

## First thing to do

Before anything else, **give the user a short recap** of the guidance below: the project goal, the chosen architecture, and the "how to read the GUI code" learning path. Keep it to a few short paragraphs or a short list. Then ask where they are: what they've read, what they've tried, and what's confusing. Pick up from there.

Then verify the claims in this file against their actual checkout. File names, line counts and APIs below were checked against upstream `master` on 2026-10-22, but the code moves. If something differs, trust the repo and tell the user.

## About the user

- Wants to build a proper touch-first, mobile-friendly UI for OpenTTD, keeping the core engine and graphics untouched.
- Self-described amateur at mobile development. Comfortable enough to build C++ locally, but was "at a loss" reading `window_gui.h` and `company_gui.cpp`, so start small and concrete.
- Likes clear explanations of *why*, not just what.

## Decisions and reasoning so far

### C++ on mobile is fine; no Godot needed
- Android: NDK compiles C++ to a `.so`; a thin Java/Kotlin Activity loads it. Ships as APK/AAB on Play Store or F-Droid.
- iOS: Xcode compiles C++ directly, with a little Objective-C++ glue.
- SDL2/SDL3 provides that glue on both platforms: window, GPU surface, touch events, audio and app lifecycle.
- The existing Android port (pelya/openttd-android, continued as an F-Droid fork `org.openttd.fdroid`, last version 14.1 in July 2024) already works this way. Its weakness is the UI, which is the desktop GUI with a finger standing in for the mouse.

### Architecture: the command system is the seam
- Every game action goes through the command system, e.g. `Command<Commands::BuildBridge>::Post(...)`. The simulation is deterministic, and multiplayer syncs these commands.
- A new UI that issues the same commands leaves the engine intact and stays multiplayer-compatible with desktop players.

### Approaches considered
- **A) Touch-first UI inside OpenTTD's own GUI system (recommended).** Keep SDL, renderer, viewport and blitters. Write new mobile windows/layouts with the NWidget system: big tap targets, bottom bar, contextual sheets instead of floating windows. Add a gesture layer (pinch-zoom, two-finger pan, long-press, drag-to-build with confirm). One C++ codebase for both platforms. Close enough to upstream to keep merging new releases. **Can be prototyped on desktop.**
- **B) Native UI (Jetpack Compose / SwiftUI) over the game view.** Most "real app" feel, but the UI gets built twice plus a C++ bridge. Too much for one person.
- **C) Godot via GDExtension.** Rejected. Both OpenTTD and Godot want to own the main loop, video, sound and input. You'd still rebuild every window, just with extra integration work on top.

### Licensing / stores (check before investing in iOS)
- OpenTTD is GPLv2, so the fork must be GPL with source published. Fine for F-Droid and Play Store.
- Apple's App Store terms have historically clashed with the GPL; OpenTTD has been on and off the App Store. Check the current situation.
- Ship the free OpenGFX / OpenSFX / OpenMSX packs, never the original TTD data files.

## Learning path for the GUI code (what the user was told)

The user was told to set aside `window_gui.h` (a reference, not reading material) and `company_gui.cpp` (some of the most complex windows) for now.

**Start with `src/screenshot_gui.cpp` (~100 lines).** Every window has three parts:
1. **Layout:** `_nested_screenshot`, a declarative list of `NWidgetPart`s. `NWidget(NWID_HORIZONTAL)` … `EndContainer()` is a row; `NWID_VERTICAL` stacks; widgets have IDs like `WID_SC_TAKE`; `SetFill(1, 1)` stretches. This is what gets redesigned most for mobile.
2. **Description:** `_screenshot_window_desc` (`WindowDesc`): default size, `WindowClass`, and a pointer to the layout.
3. **Behaviour:** `struct ScreenshotWindow : Window`. The constructor calls `CreateNestedTree()` / `FinishInitNested()`, `OnPaint()` draws widgets, and `OnClick(pt, widget, click_count)` switches on widget ID. `ShowScreenshotWindow()` opens it.

`window_gui.h` is mostly the list of overridable events: `OnInit`, `OnPaint`, `DrawWidget`, `UpdateWidgetSize`, `GetWidgetString`, `OnClick`, `OnResize`, …

**Suggested sequence:**
1. Modify `screenshot_gui.cpp` (e.g. `SetMinimalSize(0, 60)` on buttons, or remove one), rebuild, and open the screenshot window in-game.
2. Read `statusbar_gui.cpp` (~245 lines). It introduces `DrawWidget` for custom drawing (date, money).
3. Search GUI files for `Command<Commands::` to see where windows act on the game (e.g. `bridge_gui.cpp`, `depot_gui.cpp`).
4. Only then return to `company_gui.cpp`.

**Tips given:**
- Use a debugger: breakpoint in `ScreenshotWindow::OnClick` and step through a real click.
- The code was recently modernised: `Colours::Grey`, `WindowClass::Screenshot`, `Commands::BuildBridge`. Older tutorials and forum posts use `COLOUR_GREY`, `WC_SCREENSHOT`, `CMD_BUILD_BRIDGE`. Same things.
- Other small GUI files to practise on: `transparency_gui.cpp`, `viewport_gui.cpp`, `help_gui.cpp`, `date_gui.cpp`, `subsidy_gui.cpp`.

## Planned roadmap (after the learning path)

1. Prototype the mobile UI **on desktop** in a phone-sized window, treating mouse input as touch or using SDL touch events directly.
2. Redesign one flow end to end first. Suggested: "build a rail line between two towns" (toolbars, drag-building, stations, vehicle purchase).
3. Get an Android build running, starting from the pelya/F-Droid fork's build setup or upstream plus SDL's Android project template, then put the new UI on it.
4. iOS later (needs a Mac, an Apple developer account, and the licensing check above).

## Open offers the user hasn't taken up yet
- Sketching the mobile UX: what replaces the top toolbar, thumb-friendly building, how to handle deep windows like orders and timetables.
- A walkthrough of the window system and command system internals.

## How to guide
- Small, runnable steps. Each step should end with something the user can see on screen.
- Point to real files and functions in their checkout; quote short snippets rather than describing from memory.
- When something is confusing, explain the pattern with the simplest example in the codebase before the complex one.
