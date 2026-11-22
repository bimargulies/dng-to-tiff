#!/bin/bash
# Build XMP Core, XMP Files, and libjxl static libraries using their
# pre-configured Xcode projects.  Run this once before cmake --build.
# Output libs land in the paths that CMakeLists.txt expects.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SDK="$PROJECT_ROOT/third-party/dng_sdk"
CONFIG=Release

if [ ! -f "$SDK/dng_sdk/source/dng_negative.cpp" ]; then
    echo "error: DNG SDK not found in $SDK; run scripts/fetch-sdk.sh first" >&2
    exit 1
fi

EXPAT_INCLUDE="/opt/homebrew/opt/expat/include"
EXPAT_LIB="/opt/homebrew/opt/expat/lib"

XMP_OUT="$SDK/xmp/toolkit/public/libraries/macintosh/intel_64_libcpp/$CONFIG"
JXL_OUT="$SDK/libjxl/client_projects/mac/build"

echo "=== Applying SDK patches ==="
for p in "$PROJECT_ROOT"/patches/*.patch; do
    if patch -d "$SDK" -p1 -N --dry-run -s < "$p" >/dev/null 2>&1; then
        patch -d "$SDK" -p1 -N < "$p"
    else
        echo "already applied (or does not apply): $(basename "$p")"
    fi
done

echo "=== Building XMPCore ==="
xcodebuild \
    -project "$SDK/xmp/toolkit/XMPCore/build/CMake64_libcpp_Static/XMPCore64.xcodeproj" \
    -scheme XMPCoreStatic \
    -configuration $CONFIG \
    OTHER_CFLAGS="-I$EXPAT_INCLUDE" \
    OTHER_LDFLAGS="-L$EXPAT_LIB" \
    CONFIGURATION_BUILD_DIR="$XMP_OUT" \
    build 2>&1 | grep -E '(error:|warning:|Build succeeded|FAILED|BUILD)'

echo "=== Building XMPFiles ==="
xcodebuild \
    -project "$SDK/xmp/toolkit/XMPFiles/build/CMake64_libcpp_Static/XMPFiles64.xcodeproj" \
    -scheme XMPFilesStatic \
    -configuration $CONFIG \
    OTHER_CFLAGS="-I$EXPAT_INCLUDE" \
    OTHER_LDFLAGS="-L$EXPAT_LIB" \
    CONFIGURATION_BUILD_DIR="$XMP_OUT" \
    build 2>&1 | grep -E '(error:|warning:|Build succeeded|FAILED|BUILD)'

echo "=== Building libjxl ==="
xcodebuild \
    -project "$SDK/libjxl/client_projects/mac/jxl.xcodeproj" \
    -scheme jxl_release \
    CONFIGURATION_BUILD_DIR="$JXL_OUT" \
    build 2>&1 | grep -E '(error:|warning:|Build succeeded|FAILED|BUILD)'

echo ""
echo "Done. Libraries:"
ls -lh "$XMP_OUT"/*.a 2>/dev/null || echo "  (no XMP .a files yet)"
ls -lh "$JXL_OUT"/*.a  2>/dev/null || echo "  (no jxl .a files yet)"
