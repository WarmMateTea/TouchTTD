# Android build log

Running journal of getting the first APK onto the user's phone. The plan and options are in [`02-android-build-plan.md`](02-android-build-plan.md).

**Decisions (from the user, 15 Nov 2026)**
- **Phone:** Poco X7 Pro, Android 16 (build BP2A), so API level 36 and arm64-v8a. The screen is 1220×2712; the reference size in the design docs (2714×1222) is close enough.
- **Android SDK licences:** accepted by the user, so `sdkmanager --licenses` was run on their behalf.
- **Delivery:** APKs are published on GitHub (repo `WarmMateTea/TouchTTD`). Wireless `adb` over Tailscale comes later, for gesture work.
- **Application ID:** `io.github.warmmatetea.touchttd`. It differs from the official and F-Droid IDs, so both can be installed side by side.

---

## Step 1: Commit and push round 1

- Branch `mobile-shell`, commit `b6022497b3`.
- **Finding:** `git push` over SSH failed. This server's `~/.ssh/id_ed25519` isn't registered with the GitHub account, but `gh` is logged in with a token.
- **Fix:** `gh auth setup-git`, then push over HTTPS (`git push https://github.com/WarmMateTea/TouchTTD.git mobile-shell`).

## Step 2: Install the Android toolchain on the server

```bash
apt-get install -y openjdk-17-jdk-headless zip unzip
export ANDROID_HOME=/opt/android-sdk
mkdir -p $ANDROID_HOME/cmdline-tools && cd /tmp
curl -sSfL -o clt.zip https://dl.google.com/android/repository/commandlinetools-linux-13114758_latest.zip
python3 -m zipfile -e clt.zip $ANDROID_HOME/cmdline-tools/
mv $ANDROID_HOME/cmdline-tools/cmdline-tools $ANDROID_HOME/cmdline-tools/latest
chmod +x $ANDROID_HOME/cmdline-tools/latest/bin/*
yes | $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager --licenses
$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager "platform-tools" "platforms;android-36" \
    "build-tools;36.1.0" "ndk;28.2.13676358" "cmake;3.31.6"
```

**Tips**
- `python3 -m zipfile -e` doesn't preserve the executable bit, hence the `chmod +x`.
- Pick versions with `sdkmanager --list | grep -E "ndk;|build-tools;|platforms;"`.
- NDK r28 is the current LTS line. API 36 is Android 16, which is what the phone runs.

## Step 3: Merge upstream (73 commits)

`git fetch upstream master && git merge upstream/master` brought in the Android include fix (#16111) and three changes that matter here:

| Upstream commit | What it changed | Effect on us |
|---|---|---|
| `f5e71c9450` | **SDL2 → SDL3** | The Android project uses SDL3's template. Linux now needs `apt-get install libsdl3-dev`; without it CMake stops with "SDL3 or Allegro is required for this platform". |
| `58884ae9ec` | Mouse and modifier state stored as bitsets; `_ctrl_pressed` → `IsCtrlPressed()` | One merge conflict in `town_gui.cpp`, right next to the mobile branch. Resolved by keeping both. |
| `00ba2ea94a` | Interface scale is limited by the window size | Check that 300% is still allowed on a phone-sized screen. |

## Step 4: vcpkg for the Android dependencies

```bash
cd /opt && git clone https://github.com/microsoft/vcpkg.git && cd vcpkg && ./bootstrap-vcpkg.sh -disableMetrics
```

**Finding:** upstream's `vcpkg.json` marks `sdl3`, `freetype`, `fontconfig`, `harfbuzz`, `icu` and `curl` as `"platform": "linux"`. vcpkg treats Android as a separate platform, so the root manifest would skip SDL3 and FreeType for an Android triplet. Android gets its own manifest in `os/android/`.

## Step 5: Interface scale on phones

Upstream `00ba2ea94a` caps the interface scale at `min(width/480, height/360)`, so that desktop windows fit. On the user's phone that allows **325% in landscape but only 250% in portrait**, below the design's 300% default and 350% floor.

**Change** (`GetMaximumScale()` in `src/gfx.cpp`): with `gui.mobile_layout` on, the cap is the short screen side divided by 320 GUI px, which is 375% on the phone in both orientations. Desktop is unchanged. It uses the raw setting, not `IsMobileLayout()`, so the scale doesn't jump between the main menu and a game.

## Step 6: Android dependencies with vcpkg

- `os/android/vcpkg.json` lists `freetype`, `liblzma`, `libpng`, `lzo`, `sdl3` and `zlib`, at the same baseline as the root manifest.
- `os/android/triplets/arm64-android.cmake` overrides vcpkg's triplet so **SDL3 is shared** (`libSDL3.so`) and everything else static. That's SDL's standard Android setup: its Java activity loads `libSDL3.so`, then `libmain.so`. A static SDL could lose its JNI entry points at link time, because nothing in OpenTTD references them.
- First install took 2.3 min and produced SDL3 **3.4.18**.

**Tip:** to run vcpkg for Android by hand: `ANDROID_NDK_HOME=… vcpkg install --triplet arm64-android --overlay-triplets=os/android/triplets` from `os/android/`.

## Step 7: Source changes for Android

| File | Change |
|---|---|
| `CMakeLists.txt` | On Android, `openttd` is `add_library(SHARED)` named `main`. The regression tests and install/packaging are skipped (they need to run the binary or install an executable). |
| `src/os/unix/unix_main.cpp` | On Android, include `SDL3/SDL_main.h` (renames `main` to `SDL_main`, which the Java side calls) and `chdir()` to the app's internal storage, where the data is extracted. |
| `src/video/sdl3_v.cpp` | `SDLK_AC_BACK` (Android back) → `WKC_ESC`. The mobile layout already treats Escape as "back". |

## Step 8: The Gradle project (`os/android/project`)

Copied from SDL 3.4.18's `android-project` template (in vcpkg's `buildtrees/sdl3/src/*/android-project`), so the Java glue matches the native SDL exactly. Then changed:
- **`app/build.gradle`:** app ID `io.github.warmmatetea.touchttd`, compile/target SDK 36, min SDK 28, arm64 only. No native build in Gradle; it packages prebuilt `.so` files from `app/libs/`. Version code = commit count, version name = `git describe`. Release builds are signed with the debug key for now.
- **`AndroidManifest.xml`:**
  - activity `.OpenTTDActivity`
  - `screenOrientation="fullUser"`, so both orientations work
  - the `INTERNET` permission, for multiplayer
  - `SDL_ANDROID_TRAP_BACK_BUTTON=1`, so the back key reaches the game
- **`OpenTTDActivity.java`:**
  - On install or update, copies `lang/`, `baseset/`, `ai/` and `game/` from the APK's assets into internal storage, tracked by a version marker file.
  - On first start, writes an `openttd.cfg` with `mobile_layout = true`, `gui_scale = 300` and drag-to-pan.
  - Passes `-c <files>/openttd.cfg`.
- **Launcher icon** made from `media/openttd.256.png`.

## Step 9: `os/android/build.sh`

One command does host tools → CMake cross-build with vcpkg chain-loading the NDK toolchain → stage libraries and data → Gradle. The APK lands in `build-android/out/`.

**Why `ANDROID_STL=c++_shared`:** vcpkg's Android triplets use `VCPKG_CRT_LINKAGE dynamic`, which means `c++_shared`. Everything has to use the same C++ runtime, so `libc++_shared.so` is copied from the NDK into the APK.

## Step 10: First APK

`os/android/build.sh` worked on the first run. The C++ cross-compile takes about 15 min from scratch on the 4-core server. Gradle downloaded Gradle 8.12 and AGP 8.7.3, and installed build-tools 34 itself, under the already accepted licence.

**Static checks** (there's no emulator: the server has no `/dev/kvm`):
- `llvm-readelf -d libmain.so` needs only `libSDL3.so`, `libc++_shared.so` and system libraries; all three are in the APK.
- `llvm-nm -D libmain.so | grep SDL_main` shows the entry point exported.
- The APK contains `lib/arm64-v8a/{libmain,libSDL3,libc++_shared}.so` and `assets/{lang,baseset,ai,game}` (with OpenGFX).

**Finding: the first APK was 85 MB.** `libmain.so` was shipped unstripped (200 MB, 57 MB compressed). Gradle only strips native libraries when it knows the NDK version, and this project gives it prebuilt libraries without `ndkVersion`. **Fix:** `build.sh` runs `llvm-strip --strip-unneeded` while staging; the APK is now **31 MB**. The unstripped library stays in `build-android/arm64-v8a/` for `ndk-stack`.

## Step 11: Publishing

The APK goes up as a **GitHub pre-release** on `WarmMateTea/TouchTTD`, tagged `android-<version code>`:

```bash
gh release create android-<code> build-android/out/TouchTTD-*.apk --repo WarmMateTea/TouchTTD \
    --target mobile-shell --prerelease --title "TouchTTD Android test build <code>" --notes-file notes.md
```

## Still to verify on the phone

- [ ] The app starts and the data extraction works (first launch is slower).
- [ ] The interface renders at 300% and the mobile layout is on.
- [ ] Taps work through SDL's touch-to-mouse emulation, and one-finger drag pans.
- [ ] The back button closes flyouts and sheets, and doesn't quit the app.
- [ ] Rotation re-lays out the shell.
- [ ] The soft keyboard appears for the town filter.
- [ ] Sound plays.

If it crashes: `adb logcat -s SDL TouchTTD DEBUG` over Tailscale (see the plan) gives the native backtrace, and `ndk-stack -sym build-android/arm64-v8a` symbolises it.

## Step 12: First run on the phone (15 Nov 2026)

**Report from the user:** the app launches and runs (the main menu works), but "the UI disappears upon loading the map".

**Ruled out so far:**
- **The menu → New Game → Generate path itself.** Reproduced on the server with `START=menu tools/mobile/run_headless.sh 1220 2712` (the phone's portrait size): the game loads with the full shell. So it's Android-specific.
- **The shell being closed on the mode switch.** `CloseNonVitalWindows()` keeps windows flagged `NoClose`, and the dock, status pill and minimap button have that flag.

**Candidates to check on the device** (needs `adb logcat` or a screenshot):
- **The window size at startup.** There is no `resolution` in the first-start config, so OpenTTD opens a 640×480 window, and Android then resizes it to full screen. The new scale cap (step 5) is computed from the screen size *at that moment* (480 short side → 150%), and may not be recomputed after the resize.
- **Presenting frames.** `VideoDriver_SDL_Default::Paint` updates only the dirty rectangle (`SDL_UpdateWindowSurfaceRects`). On Android, SDL backs the window surface with a GLES texture. The intro game redraws the whole screen all the time, but a paused-looking in-game screen only redraws small parts.
- **Safe-area or cutout offsets** pushing the docked windows off-screen.

**Blocked:** wireless `adb` needs the server on the user's tailnet. Installing Tailscale on the server was refused by the permission system (it joins the machine to a private network), so the user installs it: `curl -fsSL https://tailscale.com/install.sh | sh && tailscale up`.

**Tip:** `START=menu` makes `tools/mobile/run_headless.sh` start at the main menu, to test the same path a phone user takes.
