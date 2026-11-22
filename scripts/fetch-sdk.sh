#!/bin/bash
# Install the Adobe DNG SDK into third-party/dng_sdk.
#
#   scripts/fetch-sdk.sh path/to/dng_sdk.zip   use an archive you downloaded
#   scripts/fetch-sdk.sh path/to/dng_sdk_dir   use a folder (browsers may auto-unpack the zip)
#   scripts/fetch-sdk.sh                       download it, if SDK_URL is pinned below
#
# The SDK is not redistributed here. It is covered by Adobe's own license,
# which is included in the SDK as LICENSE.txt.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
DEST="${SDK_DEST:-$PROJECT_ROOT/third-party/dng_sdk}"

SDK_PAGE="https://www.adobe.com/support/downloads/dng/dng_sdk.html"
# The SDK build this project was tested with (kDNGSDK_BuildVersion in dng_flags.h).
TESTED_BUILD=2802
# Optional pin for a specific zip. A download is never attempted without both.
SDK_URL="${SDK_URL:-}"
SDK_SHA256="${SDK_SHA256:-}"

die() {
    echo "error: $*" >&2
    exit 1
}

manual_help() {
    cat >&2 << EOF

Download the DNG SDK in a browser from:
  $SDK_PAGE
Then run:
  scripts/fetch-sdk.sh path/to/the-downloaded.zip
or, if your browser unpacked it:
  scripts/fetch-sdk.sh path/to/the-unpacked-folder
EOF
}

[ $# -le 1 ] || die "usage: $0 [archive.zip | sdk-folder]"
[ ! -e "$DEST" ] || die "$DEST already exists; move it aside to reinstall"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/dng-sdk.XXXXXX")"
trap 'rm -rf "$WORK" "$DEST.partial"' EXIT

if [ $# -eq 1 ] && [ -d "$1" ]; then
    # Resolve symlinks so we copy the SDK rather than a link to someone's original.
    ROOT="$(cd "$1" && pwd -P)"
    [ -f "$ROOT/dng_sdk/source/dng_negative.cpp" ] || die "$ROOT does not look like the DNG SDK"
else
    if [ $# -eq 1 ]; then
        ARCHIVE="$1"
        [ -f "$ARCHIVE" ] || die "no such file: $ARCHIVE"
    else
        if [ -z "$SDK_URL" ] || [ -z "$SDK_SHA256" ]; then
            echo "No download URL is pinned." >&2
            manual_help
            exit 1
        fi
        ARCHIVE="$WORK/dng_sdk.zip"
        echo "Downloading $SDK_URL"
        if ! curl -fL --retry 2 -o "$ARCHIVE" "$SDK_URL"; then
            echo "Download failed." >&2
            manual_help
            exit 1
        fi
    fi

    if [ -n "$SDK_SHA256" ]; then
        ACTUAL="$(shasum -a 256 "$ARCHIVE" | cut -d' ' -f1)"
        if [ "$ACTUAL" != "$SDK_SHA256" ]; then
            echo "error: SHA-256 mismatch for $ARCHIVE" >&2
            echo "  expected $SDK_SHA256" >&2
            echo "  actual   $ACTUAL" >&2
            exit 1
        fi
        echo "SHA-256 verified."
    fi

    mkdir "$WORK/x"
    unzip -q "$ARCHIVE" -d "$WORK/x" || die "could not unpack $ARCHIVE (expected a zip)"

    # The archive may or may not have a top-level folder; find the SDK root by a known file.
    MARKER="$(find "$WORK/x" -path '*/dng_sdk/source/dng_negative.cpp' -not -path '*/__MACOSX/*' | head -n 1)"
    [ -n "$MARKER" ] || die "$ARCHIVE does not look like the DNG SDK"
    ROOT="$(dirname "$(dirname "$(dirname "$MARKER")")")"
fi

BUILD="$(sed -n 's/^#define kDNGSDK_BuildVersion[[:space:]]*\([0-9]*\).*/\1/p' \
    "$ROOT/dng_sdk/source/dng_flags.h" 2> /dev/null | head -n 1 || true)"
if [ "$BUILD" = "$TESTED_BUILD" ]; then
    echo "DNG SDK build $BUILD (the tested build)."
else
    echo "warning: DNG SDK build ${BUILD:-unknown}; this project was tested with build $TESTED_BUILD." >&2
fi

mkdir -p "$(dirname "$DEST")"
cp -R "$ROOT" "$DEST.partial"
mv "$DEST.partial" "$DEST"
echo "Installed the DNG SDK in $DEST"
echo "Next: bash scripts/build-deps.sh"
