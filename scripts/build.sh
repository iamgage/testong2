#!/bin/sh
set -e
cd "$(dirname "$0")/.."
: "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point at the NDK}"

echo "=== SFQ DIAGNOSTICS ==="
echo "CMakeLists.txt contains the link fix (1 = yes, 0 = no): $(grep -c 'SFQ_EXTERN_LIBS' CMakeLists.txt || true)"
echo "--- extern/libs contents:"
ls -la extern/libs 2>&1 | head -40 || true
echo "--- extern.cmake link-related lines:"
grep -n -i 'link\|imported\|\.so' extern.cmake 2>&1 | head -40 || true
echo "=== END DIAGNOSTICS ==="

cmake -G Ninja -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_static
cmake --build build
