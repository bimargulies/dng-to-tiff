# dng-to-tiff

A small macOS command-line tool that renders DNG files to 16-bit TIFFs with the reference renderer in
the [Adobe DNG SDK](https://www.adobe.com/support/downloads/dng/dng_sdk.html). It was written to prepare
focus-stack frames for [Zerene Stacker](https://www.zerenesystems.com/cms/stacker) without going through
Photoshop, and can serve as the converter for
[bridge-zerene-stack](https://github.com/bimargulies/bridge-zerene-stack).

This is an independent hobby project. It is not affiliated with or endorsed by Adobe, Sony or Zerene
Systems.

## What to expect

The render is fixed and repeatable: no per-image adjustments, so every frame in a stack is treated the
same way. It is **not** a Camera Raw clone. Camera Raw's default look (Adobe Color), sharpening and color
noise reduction are not applied, so the output is close to Camera Raw but not identical.

It has been tested only on Sony A7R V ARW files wrapped as DNG by
[dnglab](https://github.com/dnglab/dnglab).

## Requirements

- macOS on Apple silicon, with Xcode
- CMake
- `brew install expat`
- The Adobe DNG SDK, which you download yourself (below)

## Build

1. Download the DNG SDK zip from
   [Adobe's DNG SDK page](https://www.adobe.com/support/downloads/dng/dng_sdk.html), then install it into
   `third-party/dng_sdk`:

   ```bash
   scripts/fetch-sdk.sh ~/Downloads/the-downloaded-sdk.zip
   ```

   If your browser unpacked the zip, pass the folder instead. The script warns if the SDK is not build
   2802, the build this project was tested with.

2. Apply this project's SDK patches and build the SDK's XMP and JPEG XL libraries. This is only needed
   once:

   ```bash
   bash scripts/build-deps.sh
   ```

3. Build the tool, which ends up at `build/dng_to_tiff`:

   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --parallel
   ```

## Usage

```bash
dng_to_tiff [-cs srgb|adobe|prophoto|p3] [-out <dir>] [-ev <stops>] [-shadows <n>] [-dcp <profile.dcp>] file.dng ...
```

| Option | Default | Meaning |
|---|---|---|
| `-cs` | `srgb` | Output color space |
| `-out` | beside the input | Output directory, which must already exist |
| `-ev` | `0` | Exposure adjustment in stops |
| `-shadows` | `0` | The SDK's shadow darkening. The SDK's own default of 5 crushes shadows on DNGs with no profile |
| `-dcp` | none | Camera profile to render with |

Output is an uncompressed 16-bit TIFF with an embedded ICC profile, named `<input name>.tif`. An existing
file of that name is overwritten.

### Camera profiles

DNGs from dnglab carry color matrices but no Adobe profile. For color closer to Camera Raw, pass the
profile for your camera. If you have Camera Raw installed, they are in:

```
/Library/Application Support/Adobe/CameraRaw/CameraProfiles/Adobe Standard/
```

For example:

```bash
dng_to_tiff -dcp "/Library/Application Support/Adobe/CameraRaw/CameraProfiles/Adobe Standard/Sony ILCE-7RM5 Adobe Standard.dcp" -out tiffs *.dng
```

These profiles are part of Camera Raw and are not included here.

## License

This project's own code is under the [MIT license](./LICENSE).

The Adobe DNG SDK is **not** included in this repository and is not covered by that license. It is
distributed by Adobe under its own license (`LICENSE.txt` in the SDK), and it bundles components with
their own licenses. `patches/` holds a small change to the copy of zlib bundled in the SDK. Adobe and DNG
are trademarks of Adobe Inc.

## AI usage

I used Claude while developing this.
