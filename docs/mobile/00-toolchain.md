# Toolchain and getting started

How to build, run and screenshot this fork on a Linux machine, including a headless server with no monitor. Every command here was run on the project server (Ubuntu, 4 cores, GCC, CMake, Unix Makefiles) on 14 Nov 2026. Android is in [`02-android-build-plan.md`](02-android-build-plan.md).

## 1. Prerequisites (Debian/Ubuntu)

Upstream's list is in `COMPILING.md`. The project server already had the build libraries installed; this is the usual set for a full-featured Linux build:

```bash
sudo apt-get install build-essential cmake pkg-config \
    libsdl2-dev zlib1g-dev liblzma-dev libpng-dev libfreetype-dev libfontconfig-dev \
    libharfbuzz-dev libicu-dev libcurl4-openssl-dev
# For headless runs and screenshots (section 4):
sudo apt-get install xvfb xdotool imagemagick
```

## 2. Base graphics (required once per machine)

The game won't start without a base graphics set. Use the free OpenGFX:

```bash
mkdir -p ~/.local/share/openttd/baseset && cd ~/.local/share/openttd/baseset
curl -sSfL -o opengfx.zip https://cdn.openttd.org/opengfx-releases/7.1/opengfx-7.1-all.zip
python3 -m zipfile -e opengfx.zip . && rm opengfx.zip   # leaves opengfx-7.1.tar; OpenTTD reads .tar directly
```

Sound (OpenSFX) and music (OpenMSX) are optional; the headless runner uses null drivers.

## 3. Build

```bash
cd touch-ttd
mkdir -p build && cd build
cmake ..                      # first time only
cmake --build . -j"$(nproc)"  # about 10 min from scratch on 4 cores, under a minute for a GUI-file change
ctest -j"$(nproc)"            # upstream unit tests; 99/99 passed after round 1
```

The binary is `build/openttd`. It runs from the build directory, which has `lang/`, `baseset/` and the other data next to it.

## 4. Run it

### With a desktop

```bash
cd build && ./openttd
```

Turn on the mobile layout in **Game options › Settings › Interface › "Touch-friendly mobile layout"**, or in the console (press the backquote key, `` ` ``):

```
setting gui.mobile_layout true
```

For a phone-like window, start with `./openttd -r 1357x611` and set **Game options › Graphics › Interface size** to 150%. That's the reference phone (2714×1222 at 300%) at half size. Portrait is `-r 611x1357`. Turn on **Settings › Interface › Mouse scroll mode: "Move map with left mouse button"** so a one-finger drag pans the map, as it will on touch.

### Headless (server, CI, screenshots)

`tools/mobile/` has three scripts:

| Script | What it does |
|---|---|
| `run_headless.sh W H [SCALE] [MOBILE] [args]` | Starts `Xvfb :99` at W×H and a new random game (fixed seed `12345`, so the same map every time) with a **private config**; your `openttd.cfg` is never touched. |
| `shot.sh OUT.png [percent]` | Screenshots the virtual display, plus an optional scaled copy. |
| `stop_headless.sh` | Stops both. |

```bash
tools/mobile/run_headless.sh 2714 1222          # reference phone, landscape, 300%, mobile layout
sleep 10                                         # world generation
DISPLAY=:99 xdotool mousemove 70 820 click 1     # tap "World" in the dock
tools/mobile/shot.sh /tmp/world.png 50
tools/mobile/stop_headless.sh
tools/mobile/run_headless.sh 1222 2714          # portrait
tools/mobile/run_headless.sh 1920 1080 200 false # desktop layout at 200%
```

## 5. Tips

- **Coordinates for xdotool** are screen pixels. Take a screenshot, find the button in it, and multiply by the inverse of any scaling you applied. A button at (35, 410) in a 50% copy is at (70, 820).
- **Hover tooltips** show up in screenshots if the pointer stays on a button. Move it to empty map before shooting (`xdotool mousemove 1000 1150`).
- **Keyboard shortcuts** are steadier than coordinates for desktop windows. With the rail toolbar open, `1`–`9` pick tools (`5` = autorail, `6` = demolish, `7` = depot, `9` = station), plus `s` signals, `b` bridge, `t` tunnel. `Escape` cancels a drag. Shift+F1 is **not** the rail toolbar in this build; it opened the train list.
- **The console** (`` ` ``) accepts `setting <name> <value>`, which is the quickest way to flip settings while testing.
- **Screenshots in git:** OpenTTD draws with a 256-colour palette, so `convert in.png PNG8:out.png` shrinks screenshots several-fold with no visible loss.
- **Widget enum names are global.** The script API imports every `src/widgets/*_widget.h` into one namespace, so a new prefix that clashes with an existing one (e.g. `WID_MH_`, already the message history's) fails the build in `script_window.hpp`.
- `unzip` isn't installed on the server; `python3 -m zipfile -e` does the same.
