# Same as vcpkg's arm64-android triplet, except that SDL3 is built as a shared library.
# SDL's Java activity loads libSDL3.so and then libmain.so (OpenTTD); a static SDL3 would
# let the linker drop SDL's JNI entry points from libmain.so.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Android)
set(VCPKG_CMAKE_SYSTEM_VERSION 28)
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-linux-android")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DANDROID_ABI=arm64-v8a)

if(PORT STREQUAL "sdl3")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
