# Mobile UI project docs

Work notes for the touch-first mobile layout of this OpenTTD fork. **Resuming work? Start with [`session-state.md`](session-state.md).** The project rule (see `/CLAUDE.md`) is that every step is documented here as it's done.

| File | What's in it |
|---|---|
| [`00-toolchain.md`](00-toolchain.md) | Build, run, headless screenshots; tips |
| [`01-shell-implementation-log.md`](01-shell-implementation-log.md) | Round 1 (app shell): steps, findings, bugs, verification |
| [`02-android-build-plan.md`](02-android-build-plan.md) | Android: what exists, how the build works, delivery options, what's needed from the user |
| [`03-android-build-log.md`](03-android-build-log.md) | Android: toolchain install, upstream merge, vcpkg, Gradle project, first APK, publishing |
| [`04-round2-implementation-log.md`](04-round2-implementation-log.md) | Round 2 (laying track): steps, findings, verification |
| [`05-device-polish-log.md`](05-device-polish-log.md) | Navigation bar, cancelling tools, sliding layout (user requests of 25 Nov) |
| [`06-round3-implementation-log.md`](06-round3-implementation-log.md) | Background survival and auto-pause on Android (part A); round 3: stations, depots, waypoints, info windows in the sheet, design fixes (part B) |
| [`07-round4-implementation-log.md`](07-round4-implementation-log.md) | Round 4: design fixes, touch dropdown, depot → train → orders (part A), the menus (part B) |
| [`backlog.md`](backlog.md) | Things for later steps, with context |
| [`teaching-resume.md`](teaching-resume.md) | Where the learning stands and how to continue it with the new code |
| `handoffs/` | Documents passed between sessions (mentor, design, implementation), numbered in order |
| `screenshots/round1/` | The built shell (L = landscape, P = portrait, D = desktop) |
| `screenshots/round2/` | The built round 2 (L = landscape, P = portrait) |
| `screenshots/android/` | From the user's phone |
| `screenshots/round3/` | Round 3 (L = landscape, P = portrait headless; A = phone crops) |
| `screenshots/round4/` | Round 4, headless (L = landscape, P = portrait) |
| `screenshots/round2-reference/` | Desktop reference shots the designer asked for (rounds 2–4) |

## Handoffs, in order

1. `00-mentor-handoff.md`: the original mentoring brief (goal, architecture, learning path)
2. `01-impl-to-design-initial-brief.md`: brief that started the design session
3. `02-design-to-impl-round1.md`: design round 1 (app shell), the spec that was built
4. `03-impl-to-design-round2.md`: round 1 results, answers to the feasibility questions, input for round 2
5. `04-design-to-impl-round2.md`: design round 2 (laying track): rail toolbar, review before building, bridges, tunnels, undo
6. `05-impl-to-design-round3.md`: round 2 results, answers to the round 3 questions, device findings
7. `06-design-to-impl-round3.md`: design round 3 (stations, depots, info windows, inset and polish fixes)
8. `07-impl-to-design-round4.md`: round 3 results, open questions, round 4 feasibility answers; asks for trains/orders **and the menus**
9. `08-design-to-impl-round4.md`: design round 4: buying a train, depot, train window, orders; the menus (main menu, new game, settings, save/load, confirmations)
10. `09-impl-to-design-round5.md`: round 4 results, user requests (title and Continue fonts), round 5 feasibility answers
11. (next) `10-design-to-impl-round5.md`: expected from the design session

## Code map

- `src/mobile_gui.cpp`, `src/mobile_gui.h`, `src/widgets/mobile_widget.h`: the shell
- `src/town_gui.cpp`, `src/station_gui.cpp`, `src/smallmap_gui.cpp`: mobile layouts of the town directory and window, station window, minimap
- `src/mobile_vehicle_gui.cpp`: depot sheet, New train, train window with orders, adding stops, train list
- `src/mobile_saveload_gui.cpp`, `src/mobile_newgame_gui.cpp`, `src/mobile_settings_gui.cpp`: save/load, New game, Settings; the main menu is a layout in `src/intro_gui.cpp`
- `src/mobile_app.cpp`, `src/mobile_app.h`: app life cycle on phones (pause and save when leaving, resume after a kill)
- `src/mobile_touch.cpp`: the touch gesture layer
- `src/mobile_build.cpp`, `src/mobile_build.h`: review before building (pending selection, estimates, undo)
- `src/rail_gui.cpp`, `src/bridge_gui.cpp`: mobile layouts of the rail toolbar and bridge list
- `tools/mobile/`: headless run and screenshot scripts
- `src/os/android/android_bridge.cpp`: calls from the game into the Android activity (JNI)
- `os/android/`: Android build (`build.sh`, vcpkg manifest and triplet, Gradle project from SDL3's template)
