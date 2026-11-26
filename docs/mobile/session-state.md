# Session state (for resuming after a context reset)

Last updated: 26 Nov 2026. Read this first, then `README.md` (the index) and `CLAUDE.md` (the project rules).

## Where the project is

- **Repo:** `/etc/easypanel/projects/touch-ttd` (the same checkout is also at `/root/projects/touch-ttd`). Branch **`mobile-shell`** on `github.com/WarmMateTea/TouchTTD`, a fork of OpenTTD. Push over HTTPS (`git push https://github.com/WarmMateTea/TouchTTD.git mobile-shell`); the server's SSH key isn't on the account.
- **Upstream:** merged up to `de9df409a7` (15 Nov 2026), which includes the SDL3 migration. Remote `upstream` = `github.com/OpenTTD/OpenTTD`.
- **Built and verified:**
  - **Round 1 (app shell):** status pill, hub dock, flyouts, hub sheets, mobile Town directory, news banner. Log: `01-shell-implementation-log.md`.
  - **Round 2 (track laying with review before building):** rail toolbar, pending selection with handles, live estimate, bridges, tunnels, undo, error routing. Log: `04-round2-implementation-log.md`.
  - **Touch gesture layer** (`src/mobile_touch.cpp`): tap on lift, pan, list scroll, long-press tooltips and land info, pinch and two-finger zoom. Verified on the phone; pinch tested by the user.
  - **Device polish (user requests of 25 Nov):** navigation bar setting, tool Cancel (✕ and the floating red Cancel), the sliding safe area, and a swiped-in bar moving the dock. Log: `05-device-polish-log.md`. The user confirmed it's "perfect".
  - **Android build:** `os/android/build.sh`, APK published as GitHub pre-releases (`android-<commit count>`); latest: `android-33327` (commit `b3471b1b13`, 26 Nov). Logs: `02-android-build-plan.md`, `03-android-build-log.md`.

## Waiting on

- **Design session, round 3** (stations and depots). Our handoff to it: `handoffs/05-impl-to-design-round3.md`, plus the screenshots in `screenshots/round2/`. Its reply goes in `handoffs/06-…`.
- **Background survival on Android:** persistent notification, autosave when backgrounded, optional pause. Approved by the user; not started. Details: `backlog.md`.

## Working with the user's phone (Poco X7 Pro, Android 16)

- Reached over Tailscale: the phone is `100.65.145.31`, the server `100.91.245.74`. MIUI often puts Tailscale to sleep, so the phone shows "offline". Ask the user to open Tailscale; `tailscale ping 100.65.145.31` confirms the link.
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
- `CommandResultCapture` can't nest (an assert); close its scope before calling code that may capture.
- Unsigned widget sizes (`current_x`) underflow in position math; cast to `int`.
- A vertical NWidget container with a non-resizable child can't resize horizontally (the LCM of resize steps is 0).
