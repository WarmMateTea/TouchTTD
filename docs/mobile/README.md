# Mobile UI project docs

Work notes for the touch-first mobile layout of this OpenTTD fork. The project rule (see `/CLAUDE.md`) is that every step is documented here as it's done.

| File | What's in it |
|---|---|
| [`00-toolchain.md`](00-toolchain.md) | Build, run, headless screenshots; tips |
| [`01-shell-implementation-log.md`](01-shell-implementation-log.md) | Round 1 (app shell): steps, findings, bugs, verification |
| [`02-android-build-plan.md`](02-android-build-plan.md) | Android: what exists, how the build works, delivery options, what's needed from the user |
| [`03-android-build-log.md`](03-android-build-log.md) | Android: toolchain install, upstream merge, vcpkg, Gradle project, first APK, publishing |
| [`backlog.md`](backlog.md) | Things for later steps, with context (e.g. keeping a game alive in the background on Android) |
| [`teaching-resume.md`](teaching-resume.md) | Where the learning stands and how to continue it with the new code |
| `handoffs/` | Documents passed between sessions (mentor, design, implementation), numbered in order |
| `screenshots/round1/` | The built shell (L = landscape, P = portrait, D = desktop) |
| `screenshots/round2-reference/` | Desktop reference shots the designer asked for (rounds 2–4) |

## Handoffs, in order

1. `00-mentor-handoff.md`: the original mentoring brief (goal, architecture, learning path)
2. `01-impl-to-design-initial-brief.md`: brief that started the design session
3. `02-design-to-impl-round1.md`: design round 1 (app shell), the spec that was built
4. `03-impl-to-design-round2.md`: round 1 results, answers to the feasibility questions, input for round 2

## Code map

- `src/mobile_gui.cpp`, `src/mobile_gui.h`, `src/widgets/mobile_widget.h`: the shell
- `src/town_gui.cpp`: the mobile Town directory layout
- `tools/mobile/`: headless run and screenshot scripts
- `os/android/`: Android build (`build.sh`, vcpkg manifest and triplet, Gradle project from SDL3's template)
