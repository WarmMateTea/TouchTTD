#!/usr/bin/env bash
# Build the TouchTTD Android APK (arm64-v8a).
#
# Usage: os/android/build.sh [debug|release]      (default: release, signed with the debug key)
#
# Steps:
#   1. host tools (strgen, settingsgen, ...) built for the build machine;
#   2. dependencies for Android through vcpkg (os/android/vcpkg.json, overlay triplet in os/android/triplets);
#   3. OpenTTD cross-compiled with the NDK into libmain.so, plus its data (lang, baseset, ...);
#   4. libraries and data staged into the Gradle project (os/android/project);
#   5. Gradle packages the APK.
#
# Environment (defaults match the project server, see docs/mobile/03-android-build-log.md):
#   ANDROID_HOME   /opt/android-sdk
#   ANDROID_NDK_HOME  $ANDROID_HOME/ndk/28.2.13676358
#   VCPKG_ROOT     /opt/vcpkg
#   OPENGFX_TAR    ~/.local/share/openttd/baseset/opengfx-7.1.tar
#   BUILD_ROOT     <repo>/build-android
set -euo pipefail

VARIANT=${1:-release}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export ANDROID_HOME=${ANDROID_HOME:-/opt/android-sdk}
export ANDROID_NDK_HOME=${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk/28.2.13676358}
VCPKG_ROOT=${VCPKG_ROOT:-/opt/vcpkg}
OPENGFX_TAR=${OPENGFX_TAR:-$HOME/.local/share/openttd/baseset/opengfx-7.1.tar}
BUILD_ROOT=${BUILD_ROOT:-$ROOT/build-android}
ABI=arm64-v8a
API=28
JOBS=$(nproc)
PROJECT=$ROOT/os/android/project

echo "== 1. Host tools"
cmake -S "$ROOT" -B "$BUILD_ROOT/host" -DOPTION_TOOLS_ONLY=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_ROOT/host" -j"$JOBS" >/dev/null

echo "== 2+3. OpenTTD for Android (vcpkg installs the dependencies during configure)"
cmake -S "$ROOT" -B "$BUILD_ROOT/$ABI" \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
	-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
	-DVCPKG_TARGET_TRIPLET=arm64-android \
	-DVCPKG_MANIFEST_DIR="$ROOT/os/android" \
	-DVCPKG_OVERLAY_TRIPLETS="$ROOT/os/android/triplets" \
	-DVCPKG_INSTALLED_DIR="$BUILD_ROOT/vcpkg_installed" \
	-DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-$API -DANDROID_STL=c++_shared \
	-DHOST_BINARY_DIR="$BUILD_ROOT/host" >/dev/null
cmake --build "$BUILD_ROOT/$ABI" --target openttd -j"$JOBS"

echo "== 4. Stage libraries and data"
LIBS=$PROJECT/app/libs/$ABI
ASSETS=$PROJECT/app/src/main/assets
rm -rf "$LIBS" "$ASSETS" && mkdir -p "$LIBS" "$ASSETS"
# Ship a stripped libmain.so (~200 MB of debug info otherwise); the unstripped one stays in
# $BUILD_ROOT/$ABI for symbolising crash stack traces (ndk-stack -sym $BUILD_ROOT/$ABI).
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded -o "$LIBS/libmain.so" "$BUILD_ROOT/$ABI/libmain.so"
cp "$BUILD_ROOT/vcpkg_installed/arm64-android/lib/libSDL3.so" "$LIBS/"
cp "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" "$LIBS/"
# Data: same layout as next to the desktop binary. The regression-test AIs are left out.
cp -r "$BUILD_ROOT/$ABI/lang" "$BUILD_ROOT/$ABI/baseset" "$BUILD_ROOT/$ABI/game" "$ASSETS/"
mkdir -p "$ASSETS/ai" && cp "$BUILD_ROOT/$ABI"/ai/*.nut "$ASSETS/ai/"
cp "$OPENGFX_TAR" "$ASSETS/baseset/"

echo "== 5. Gradle ($VARIANT)"
cd "$PROJECT"
VERSION_CODE=$(git -C "$ROOT" rev-list --count HEAD)
VERSION_NAME="$(git -C "$ROOT" describe --always --dirty)"
TASK=assemble${VARIANT^}
./gradlew --no-daemon -q "$TASK" -PtouchttdVersionCode="$VERSION_CODE" -PtouchttdVersionName="$VERSION_NAME"
APK=$(ls "$PROJECT"/app/build/outputs/apk/"$VARIANT"/*.apk | head -1)
mkdir -p "$BUILD_ROOT/out"
cp "$APK" "$BUILD_ROOT/out/TouchTTD-$VERSION_NAME-$ABI.apk"
echo "APK: $BUILD_ROOT/out/TouchTTD-$VERSION_NAME-$ABI.apk"
