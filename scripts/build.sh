#!/bin/sh
set -e
cd "$(dirname "$0")/.."
: "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at the NDK}"
cmake -G Ninja -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_static
cmake --build build
