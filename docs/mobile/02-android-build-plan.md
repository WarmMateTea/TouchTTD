# Android build: research and plan

Status: **researched, not set up yet.** Several steps need decisions from the user (section 5). Researched on 14 Nov 2026.

## 1. What exists

| Source | What it is | Useful for |
|---|---|---|
| Upstream OpenTTD | No Android app project. Cross-compile plumbing exists: `-DOPTION_TOOLS_ONLY=ON` builds only the host tools, and `-DHOST_BINARY_DIR=…` points a cross build at them (`CMakeLists.txt`, `cmake/Options.cmake`). The test executable already links `log` on Android. PR [#16111](https://github.com/OpenTTD/OpenTTD/pull/16111) (merged 27 Oct 2026) fixed bionic includes; **our checkout is from 22 Oct and doesn't have it yet.** | The base. Pull upstream first. |
| [pelya/openttd-android](https://github.com/pelya/openttd-android) | The long-running unofficial port (F-Droid `org.openttd.fdroid`). Built with pelya's "commandergenius" SDL wrapper, historically SDL 1.2. Has its own touch tweaks to the desktop UI. | History; asset-extraction ideas. Heavy, unusual build. |
| [XION-HN/openttd-android](https://github.com/XION-HN/openttd-android) | OpenTTD 15.3, arm64, **NDK 27.2 + SDL2 + FreeType + Gradle**, built in GitHub Actions. Patches: builds OpenTTD as `libmain.so` for SDL's `SDLActivity`, bionic header fixes, crash capture, an Android music driver. Bundles OpenGFX/OpenMSX in the APK and extracts them on first launch. | **The closest model for us**: standard SDL2 Android template, no exotic wrapper. |
| [steamytooolz-commits/openttd-android-v16](https://github.com/steamytooolz-commits/openttd-android-v16) | v16 beta port via the commandergenius wrapper, SDL2, CI-built APK. | Shows CI-only APK builds work. |

## 2. How an SDL2 Android build works

```
Gradle project (from SDL2's android-project template)
├── app/src/main/java/org/libsdl/app/SDLActivity.java   ← from SDL, unchanged
├── app/src/main/java/…/OpenTTDActivity.java            ← ~20 lines: extends SDLActivity, names the libs
├── app/src/main/assets/                                ← lang/, baseset/ (OpenGFX), ai/, game/ …
└── app/jni/  (CMake, driven by Gradle's externalNativeBuild)
    ├── SDL2/          → libSDL2.so
    └── openttd/       → libmain.so   (OpenTTD built as a SHARED library; SDL calls its SDL_main)
```

Steps:
1. **Host tools first.** OpenTTD generates sources at build time with its own tools (string compiler, settings generator, …), which have to run on the build machine:
   `cmake -S . -B build-host -DOPTION_TOOLS_ONLY=ON && cmake --build build-host`
2. **Cross-compile** with the NDK toolchain file: `-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DHOST_BINARY_DIR=$PWD/build-host`.
3. **Dependencies for Android**, each cross-built or taken from the NDK:
   - zlib (in the NDK)
   - liblzma, needed for savegames
   - libpng, needed for screenshots
   - FreeType, for scalable fonts at 300%, which matters on a phone
   - SDL2
   - Optional: HarfBuzz and ICU for complex scripts, and libcurl for the in-game content download.
   vcpkg has Android triplets (`arm64-android`), and the repo already ships a `vcpkg.json`, so vcpkg is the least hand-work.
4. **Source changes needed:**
   - CMake: on Android, build `openttd` as a shared library named `main`.
   - Data paths: point OpenTTD's personal/shared directories at the app's storage, and extract the bundled assets there on first run.
   - The keymap: SDL's Android back button reaches us as `SDL_SCANCODE_AC_BACK`; map it to `WKC_ESC`, which the mobile layout already handles as "back".
   - Default settings: `gui.mobile_layout = true`, GUI scale from the screen density, and scroll mode = left-drag.
5. **Package:** `./gradlew assembleDebug` produces an APK signed with a debug key. That's fine for sideloading onto your own phone.

**Sizes:** Android SDK + NDK + build-tools ≈ 4–5 GB on disk. The server has 170 GB free. JDK 17 is needed for Gradle.

## 3. Getting the APK onto the phone

The build server is remote (no USB), so there are three options:

| Option | How it works | Setup | Turnaround |
|---|---|---|---|
| **A. Build on this server, download over HTTPS** | I build here. The APK goes somewhere your phone can download it, such as a small static site on your easypanel or a GitHub release. You tap it and install (Android asks once to allow installs from that browser). | SDK/NDK on this server; somewhere to host the file. | Minutes per build. |
| **B. GitHub Actions** | A workflow builds the APK on every push and attaches it as an artifact or release. | Repo on GitHub (a fork of OpenTTD), Actions enabled. | ~15–25 min per build; nothing heavy on this server. |
| **C. adb over the network** | `adb connect` to the phone's wireless debugging, through a VPN/tunnel such as Tailscale. I install and read logs (`adb logcat`) directly. | Developer options and wireless debugging on the phone, plus a tunnel between the server and the phone. | Seconds, with live logs, which is best for debugging the gesture layer. |

Recommendation: **A first, for the fastest path to a first APK. Add C when we start the gesture layer**, because touch debugging without `logcat` is painful. B is a good later addition, for builds of tagged versions you might share.

## 4. Plan

1. `git pull` upstream (brings in the #16111 Android fixes); rebuild and re-test desktop.
2. Install JDK 17, Android cmdline-tools, NDK 27.x, platform/build-tools on this server *(needs the SDK licence accepted, see section 5)*.
3. Host tools build; vcpkg Android deps; SDL2 Android template under `os/android/`.
4. CMake changes for `libmain.so`, data-path and asset extraction, the back-button mapping, mobile defaults.
5. First APK. Install it (option A); check that it starts, renders at 300%, and that taps work through SDL's mouse emulation.
6. Gesture layer v1 on device (option C recommended).

Each step gets its own section in a `03-android-build-log.md` as it happens (see `CLAUDE.md`).

## 5. What I need from you

1. **Your phone:** model and Android version. I'm assuming arm64 (every phone since about 2017). It matters for the minimum API level and for the safe-area/cutout handling.
2. **The Android SDK licence:** installing the SDK requires accepting Google's Android SDK licence terms. Do you accept them, so I can run `sdkmanager --licenses` for you? (If you'd rather read them first, run `! sdkmanager --licenses` yourself once it's installed.)
3. **Delivery:** option A, B or C (or A now and C later, as recommended). For A, where should the APK be published? An easypanel static service, a GitHub release, or something else. For B, the GitHub repo to use. For C, whether you can enable wireless debugging and use Tailscale (or similar) on both machines.
4. **App identity:** an application ID, e.g. `io.github.<you>.touchttd`. It must differ from the official and F-Droid OpenTTD IDs so they can be installed side by side. Debug-signed is fine for now.
5. **Licensing reminder:** sideloading on your own phone has no store issues. Publishing later means GPLv2 (source published), and only the free OpenGFX/OpenSFX/OpenMSX sets in the APK.
