# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Purpose

A CLI tool (`dng_to_tiff`) that converts DNG files (specifically dnglab-wrapped Sony ARW) to 16-bit TIFF using the Adobe DNG SDK's reference renderer.  Intended to replace the flaky Photoshop Image Processor step in the `bridge-zerene-stack` workflow next door.

## Build — two phases

### Phase 1: build XMP and libjxl static libs (once)

```bash
bash scripts/build-deps.sh
```

This runs `xcodebuild` on the pre-configured Xcode projects bundled inside the Adobe DNG SDK 1.7.1:
- XMPCore and XMPFiles → `third-party/dng_sdk/xmp/toolkit/public/libraries/macintosh/intel_64_libcpp/Release/`
- libjxl (client build) → `third-party/dng_sdk/libjxl/client_projects/mac/build/`

Requires: Xcode, `brew install expat` (keg-only; headers at `/opt/homebrew/opt/expat/include`).

### Phase 2: CMake build (every change)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Outputs `build/dng_to_tiff`.

## Usage

```bash
dng_to_tiff [-cs srgb|adobe|prophoto|p3] [-out <dir>] [-ev <stops>] [-shadows <n>] [-dcp <profile.dcp>] file.dng ...
```

Matching Photoshop: dnglab DNGs have no profile/tone curve, so the SDK default (shadows=5) crushes shadows; we default to `-shadows 0`. Best match found: `-dcp "/Library/Application Support/Adobe/CameraRaw/CameraProfiles/Adobe Standard/Sony ILCE-7RM5 Adobe Standard.dcp"` (adding `-ev` made it worse).

Defaults: 16-bit, sRGB, output beside input file.  For Zerene Stacker, sRGB is the right choice.

## Architecture

```
third-party/dng_sdk/     → copy of Adobe DNG SDK 1.7.1 (gitignored; patches in patches/)
  dng_sdk/source/   → all DNG SDK .cpp/.h files
  libjpeg/          → bundled libjpeg (compiled by CMakeLists.txt)
  libjxl/libjxl/    → bundled libjxl source (pre-built by build-deps.sh via Xcode)
  xmp/              → bundled XMP SDK (pre-built by build-deps.sh via Xcode)
src/dng_to_tiff.cpp → our CLI wrapper
CMakeLists.txt      → compiles libjpeg + all DNG SDK sources into a static lib,
                      links against pre-built XMP/JXL, builds dng_to_tiff
```

### Key compile-time flags

| Flag | Value | Why |
|---|---|---|
| `qDNGUseXMP` | 1 | XMP cannot be disabled: `dng_jxl.cpp` calls `GetXMP()` unconditionally |
| `qDNGXMPDocOps` | 0 | DocOps not needed for rendering |
| `qDNGUseLibJPEG` | 1 | Required for lossless-JPEG compressed raw tiles |
| `HWY_DISABLED_TARGETS` | 25952256 | Matches the libjxl Xcode client project |

### Rendering pipeline (in `dng_to_tiff.cpp`)

1. `dng_info::Parse` / `PostParse` — read TIFF/DNG tags
2. `dng_negative::Parse` / `PostParse` — build the negative object
3. `ReadStage1Image` — read raw tiles
4. `SynchronizeMetadata`
5. `BuildStage2Image` — linearise (black subtract, linearisation table)
6. `BuildStage3Image` — demosaic Bayer → full-colour
7. `dng_render::Render` — colour matrix + white balance + tone curve → final image
8. `dng_image_writer::WriteTIFF` — 16-bit uncompressed TIFF with embedded ICC profile

## Notes

- `dng_validate.cpp` (in `third-party/dng_sdk/dng_sdk/source/`) is the SDK's own CLI tool.  It has the same rendering pipeline but is primarily a validator.  Our tool is a simplified render-only version.
- The XMP SDK Xcode projects expect a **specific output path** (`public/libraries/macintosh/intel_64_libcpp/Release/`); `build-deps.sh` redirects there via `CONFIGURATION_BUILD_DIR`.
- libjxl is included because core DNG SDK files (`dng_negative.cpp`, `dng_image_writer.cpp`, etc.) unconditionally include `dng_jxl.h`, which in turn includes `jxl/color_encoding.h`.  The JXL path is never actually exercised for Sony ARW DNGs (no JXL-compressed tiles), but the headers and lib are required at compile/link time.
