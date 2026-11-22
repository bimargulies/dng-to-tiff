#!/bin/bash
# Check this project's sources with clang-format and clang-tidy, using the
# rules in .clang-format and .clang-tidy.
# Needs a configured build directory (cmake -S . -B build) and both tools
# (brew install llvm).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

CLANG_TIDY="$(command -v clang-tidy || true)"
if [ -z "$CLANG_TIDY" ] && [ -x /opt/homebrew/opt/llvm/bin/clang-tidy ]; then
    CLANG_TIDY=/opt/homebrew/opt/llvm/bin/clang-tidy
fi
if [ -z "$CLANG_TIDY" ]; then
    echo "error: clang-tidy not found; run: brew install llvm" >&2
    exit 1
fi

if [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
    echo "error: no compile database; run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release" >&2
    exit 1
fi

CLANG_FORMAT="$(command -v clang-format || true)"
if [ -z "$CLANG_FORMAT" ] && [ -x /opt/homebrew/opt/llvm/bin/clang-format ]; then
    CLANG_FORMAT=/opt/homebrew/opt/llvm/bin/clang-format
fi
if [ -z "$CLANG_FORMAT" ]; then
    echo "error: clang-format not found; run: brew install llvm" >&2
    exit 1
fi

cd "$PROJECT_ROOT"
"$CLANG_FORMAT" --dry-run --Werror src/*.cpp
"$CLANG_TIDY" -p "$BUILD_DIR" --quiet \
    --extra-arg=-isysroot --extra-arg="$(xcrun --show-sdk-path)" \
    src/*.cpp
echo "Lint passed."
