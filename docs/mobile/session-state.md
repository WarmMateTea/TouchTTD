# Session state (for resuming after a context reset)

Last updated: 28 Nov 2026 (night, round 5). Read this first, then `README.md` (the index) and `CLAUDE.md` (the project rules).

## Where the project is

- **Repo:** `/etc/easypanel/projects/touch-ttd` (the same checkout is also at `/root/projects/touch-ttd`). Branch **`mobile-shell`** on `github.com/WarmMateTea/TouchTTD`, a fork of OpenTTD. Push over HTTPS (`git push https://github.com/WarmMateTea/TouchTTD.git mobile-shell`); the server's SSH key isn't on the account.
- **Upstream:** merged up to `de9df409a7` (15 Nov 2026), which includes the SDL3 migration. Remote `upstream` = `github.com/OpenTTD/OpenTTD`.
- **Built and verified:**
  - **Round 1 (app shell):** status pill, hub dock, flyouts, hub sheets, mobile Town directory, news banner. Log: `01-shell-implementation-log.md`.
  - **Round 2 (track laying with review before building):** rail toolbar, pending selection with handles, live estimate, bridges, tunnels, undo, error routing. Log: `04-round2-implementation-log.md`.
  - **Touch gesture layer** (`src/mobile_touch.cpp`): tap on lift, pan, list scroll, long-press tooltips and land info, pinch and two-finger zoom. Verified on the phone; pinch tested by the user.
  - **Device polish (user requests of 25 Nov):** navigation bar setting, tool Cancel (✕ and the floating red Cancel), the sliding safe area, and a swiped-in bar moving the dock. Log: `05-device-polish-log.md`. The user confirmed it's "perfect".
  - **Background survival (26 Nov):** foreground service + notification while a game runs, pause and save when leaving the app, resume after a kill. Verified on the phone. Log: `06-round3-implementation-log.md` part A.
  - **Round 3 (26 Nov):** stations (tap/drag footprint, picker in the sheet, join, move, Undo), depots (auto-facing, Turn), waypoints, mobile town/station windows and minimap, edge-to-edge chrome, design fixes. Verified headless and on the phone (pinch on the minimap untested). Log: `06-round3-implementation-log.md` part B.
  - **Round 4 (27–28 Nov):** design fixes, touch dropdown, depot → New train → orders → Start, train list, main menu with Continue, New game with presets, confirmations, save/load, settings with search, top chrome against the cutout. Verified headless and partly on the phone. Log: `07-round4-implementation-log.md`.
  - **Android build:** `os/android/build.sh`, APK published as GitHub pre-releases (`android-<commit count>`); latest: `android-33331` (commit `d5986b7e62`, 28 Nov, round 4). Logs: `02-android-build-plan.md`, `03-android-build-log.md`.

  - **Round 5 (28 Nov, in progress):** `handoffs/10-design-to-impl-round5.md`, built batch by batch in its order; progress table at the top of `08-round5-implementation-log.md`. Done: batch 1 (main menu logo/Continue, cells, settings fixes, dropdown titles), batch 2 (Settings with Game Options), batch 3 (signals), batch 4 (road and tram), batch 5 (other vehicles, refit), batch 6 (finances), batch 7 (industries), batch 8 (towns, subsidies, goals, news, land info), batch 9 (landscape, water, air), batch 10 (vehicle details, timetable, autoreplace, groups). Next: batch 11, company, league, graphs (handoff §13).

## Waiting on

- **Phone tests for round 5**: order long-press drag (touch only), everything of round 4 not yet tried on the phone.
- **The user asked to switch the phone back to the Claude app** after device testing (`adb shell monkey -p com.anthropic.claude -c android.intent.category.LAUNCHER 1`).
- **User's request for later:** the top chrome (status pill, minimap button) should slide with the safe area like the dock and maybe sit closer to the top edge (`backlog.md`).

## Working with the user's phone (Poco X7 Pro, Android 16)

- Reached over Tailscale: the phone is `100.65.145.31`, the server `100.91.245.74`. MIUI often puts Tailscale to sleep, so the phone shows "offline". Ask the user to open Tailscale; `tailscale ping 100.65.145.31` confirms the link.
- **Use short timeouts** on adb commands (`timeout 10 adb ...`) and say "LINK-DOWN" fast when Tailscale drops (the user asked). When adb says "protocol fault": `adb kill-server`, start it, rescan the connect port.
- **Floating windows of other apps** (Tailscale's bubble) take the taps; ask the user to close them. **Crop screenshots below the status bar**; delete any that show notifications or other apps.
- **Pairing:** the user sends a pairing port and code → `adb pair IP:PORT CODE`. The connect port differs: find it with a TCP scan of ports 30000–50000 (Python asyncio), then `adb connect IP:PORT`. Restart the adb server on "protocol fault". The connect port has been 40389 recently.
- **Install:** `adb push app.apk /data/local/tmp/x.apk && adb shell pm install -r /data/local/tmp/x.apk`. Plain `adb install` stalls over this link. Check with `dumpsys package io.github.warmmatetea.touchttd | grep lastUpdateTime` (phone clock is UTC−3).
- **Drive it:** `adb shell input tap/swipe`, `adb exec-out screencap -p`. Console: `input keyevent 68`, `input text "setting%sNAME%sVALUE"`, `keyevent 66`. Portrait coordinates: the dock is at y≈2510 (bar shown) or ≈2650 (hidden); New Game at (400,1000); Generate at (610,1660).
- **Privacy:** read only `adb logcat -s TouchTTD:* SDL:*`. Never grep the whole log (it shows other apps' data). Don't keep screenshots showing the user's notifications.

## Standing preferences

- Document every step in `docs/mobile/` as it's done (the `CLAUDE.md` rule).
- Send screenshots with a .zip of them (also in memory).
- The user is learning C++ GUI work. `teaching-resume.md` has the learning path to pick back up.
- Commit and push when asked, with the attribution trailer from the system reminder.

## Gotchas learned the hard way

- Never wait on or kill processes with `pgrep -f`/`pkill -f` using text from the waiting command's own command line: it matches itself.
- In a vertical NWidget container, hide with `SZSP_HORIZONTAL`, not `SZSP_NONE`, or the window can't widen.
- `CommandResultCapture` can't nest (an assert); close its scope before calling code that may capture.
- Unsigned widget sizes (`current_x`) underflow in position math; cast to `int`.
- `MakeDropDownListStringItem`'s third argument masks (greys) the entry; the tick is `ShowDropDownList`'s `selected`.
- A vertical NWidget container with a non-resizable child can't resize horizontally (the LCM of resize steps is 0).
