# DNG → TIFF batch export: problem notes and automation options

## Context

- **Camera:** Sony A7R V, shooting raw (ARW). The in-camera color space is set to sRGB, but that only affects the camera's JPEG and the preview embedded in the raw file. The raw data itself has no color space.
- **Wrapping:** ARW files converted to DNG with **dnglab**.
- **Platform:** macOS, Adobe Bridge + Photoshop + Adobe Camera Raw (ACR).
- **Goal:** batch-export the DNGs to TIFF so they look the same as they do in Camera Raw with no edits.

## The problem

Running **Bridge → Tools → Photoshop → Image Processor** (Save as TIFF, Include ICC Profile on, Run Action off) gives TIFFs in which a **subtle, dim background fades to black**.

## What was tested

| Test | Result |
|---|---|
| Open the DNG in Camera Raw from Bridge (Cmd+R, Bridge-hosted ACR) | Background visible |
| Open the DNG in Photoshop's Camera Raw (double-click in Bridge), click Open | Background visible |
| Same as above, then Photoshop **File → Save As → TIFF** | **TIFF is fine** |
| Image Processor (TIFF only, "Open first image to apply settings" off, no resize, no action) | **Background goes black** |
| Camera Raw **Save Images** (select all in filmstrip → Save → Format: TIFF) | **TIFF is fine** ✅ |

**Conclusions:**
- The DNG data and dnglab's metadata are fine for Adobe's renderer. ACR renders the background correctly in both Bridge and Photoshop.
- A manual Photoshop Save As TIFF is fine.
- The failure is specific to Image Processor. The exact cause was never identified, even with every relevant setting checked.

## Working workaround (currently in use)

1. In Bridge, select the DNGs and press **Cmd+R** to open them in Camera Raw.
2. In the filmstrip, select all.
3. Click the **Save Images** icon (top right).
4. Set Format to **TIFF**, then choose color space, bit depth (16-bit if editing further), compression and destination.

Save Options are remembered between runs. This route never touches Photoshop.

## Other automation options in the Adobe ecosystem

### Photoshop droplet (no code)
1. Record an action: open a DNG → click Open in ACR → Save As TIFF → close.
2. Go to File → Automate → **Create Droplet**. Check *Override Action "Open" Commands*, *Suppress File Open Options Dialogs* and *Override Action "Save As" Commands*.
3. Drag DNGs onto the droplet app.

This follows the same path as the manual Save As, which is known to work.

### Photoshop Batch
Same recorded action, run through File → Automate → Batch with the same override and suppress options.

### Bridge startup script (ExtendScript)
- Put a `.jsx` file in `~/Library/Application Support/Adobe/Bridge <version>/Startup Scripts/`. Bridge asks you to enable it on the next launch.
- It can add a menu or context-menu item, get the selected files, and send work to Photoshop through **BridgeTalk**.
- **Limitation:** as far as I know, Bridge scripting **cannot call ACR's Save Images**. It can only read and write ACR settings in XMP. The script would therefore have Photoshop open each file (with `DialogModes.NO`, so ACR uses the saved settings), save it with `TiffSaveOptions` (embedColorProfile = true), and close it.
- **Risk:** this is similar to what Image Processor does. A minimal script skips Image Processor's extra steps (bit-depth handling, its JPEG/PSD/TIFF sequencing) and may work, but test it on one file first.

## Non-Adobe / CLI options

No open-source tool reproduces Adobe's default rendering exactly (the Adobe Color profile, tone curve, default sharpening and noise reduction are proprietary).

### Closest to Adobe: `dng_validate` (Adobe DNG SDK)
- The source is available under Adobe's own license, which is free but not OSI open source.
- It renders a DNG to TIFF with the **DNG spec reference rendering**: embedded color matrices/profile, BaselineExposure, white balance and Adobe's default tone curve.
- Example (check the flags by running it with no arguments):
  ```
  dng_validate -16 -cs3 -tif out.tif in.dng
  ```
  `-16` = 16-bit output; `-cs1` / `-cs2` / `-cs3` = sRGB / Adobe RGB / ProPhoto.
- Drawbacks: you have to build it (CMake, Adobe XMP SDK, libjpeg). It won't use Adobe's camera-matching profiles unless one is embedded, and dnglab DNGs probably carry only the basic matrices. Expect output close to ACR but not identical.

### rawpy (Python bindings for LibRaw): easiest to script
```python
import rawpy, tifffile

with rawpy.imread("in.dng") as raw:
    rgb = raw.postprocess(
        use_camera_wb=True,
        output_bps=16,
        no_auto_bright=True,
    )
tifffile.imwrite("out.tif", rgb)
```
- With `no_auto_bright=True`, shadows tend to come out darker and flatter than ACR's. A tone curve may be needed to keep the subtle background.
- Other relevant `postprocess` options: `output_color` (`rawpy.ColorSpace.sRGB`, `Adobe`, `ProPhoto`), `gamma`, `bright`, `exp_shift`, `demosaic_algorithm`.
- Note: rawpy does **not** embed an ICC profile in the TIFF. You'd need to add one (for example with `tifffile`'s extratags or Pillow's `icc_profile`).

### Other CLIs
- **LibRaw `dcraw_emu`** or classic **dcraw**: `-T -6 -w` (TIFF, 16-bit, camera WB).
- **darktable-cli** and **rawtherapee-cli**: complete headless processors, but with no edits they use their own default looks (filmic/sigmoid, different contrast), so shadows will differ noticeably from ACR.

## Possible next steps for a scripted pipeline

1. Try `dng_validate` on one problem file and compare its output with the Camera Raw Save Images TIFF, paying attention to the dim background.
2. If you go with rawpy, tune `exp_shift` / `bright` / `gamma` or apply a tone curve so the shadows match ACR, embed the ICC profile, and loop over a folder.
3. Optional: compare tags with `exiftool -BlackLevel -WhiteLevel -BaselineExposure -UniqueCameraModel -DNGVersion file.dng` on a dnglab DNG and on an Adobe DNG Converter DNG from the same ARW.
