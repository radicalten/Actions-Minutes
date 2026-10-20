# NDS 2D Raster Score Suite

An unattended Nintendo DS homebrew graphics benchmark inspired by the pixel-comparison workflow in [PolyRasterTest](https://github.com/Jaklyy/polyrastertest) and the testing goal of [mGBA Suite Auto](https://github.com/mattrbeck/mgba-suite-auto). It is NDS-native: it drives the DS 2D BG/OBJ engines and captures the main engine's composed output.

## Automatic scoring

At launch the ROM runs eight cards without input, shows each card's result for 30 VBlanks (~0.5 seconds), then stops on a final results screen on both displays. Press START to run it again. For each card it:

1. Programs the 2D engine with a deterministic scene.
2. Captures the composed main-screen output with the DS display-capture unit.
3. Independently rasterizes the same scene in ARM9 software using the test's tile/map/palette data and expected transforms.
4. Compares all 49,152 RGB555 pixels, allowing at most one 5-bit step per color channel for capture rounding.
5. Replaces the top screen with a difference image and prints the card's pixel-match percentage and matched-pixel count on the bottom screen.

Difference colors follow PolyRasterTest's diagnostic idea: **green** = match, **red** = missing (actual black, expected nonblack), **pink** = extra (actual nonblack, expected black), **blue** = wrong color/position. Each card's difference image and score remain visible for 30 VBlanks (~0.5 seconds). After card 08, both screens switch to the final results table and remain there until START is pressed. Display-capture polling has a generous 4,000,000-iteration budget for slower emulators; frame-boundary polling is limited to 800,000 iterations so a stalled video clock cannot make a card appear stuck for too long. Capture is armed directly after scene setup; the ROM no longer performs a separate VCOUNT wait before the first capture. If the video clock or capture engine still stops responding, the card is marked `FRAME/CAPTURE TIMEOUT`, skipped, and the next test runs; the ROM does not block forever on a wait instruction. Timed-out cards are reported separately and excluded from the pixel-match total.

The total is calculated as matched pixels divided by the pixels in valid captures; timed-out cards are excluded and counted separately. It is not an average of rounded display percentages. Capture and score cover the **main 2D engine**. The **bottom screen is reserved for the status/score console**: test scenes do not write its BG VRAM, palette, layer controls, or blend controls. This isolates the status display from top-screen test failures.

This is a spec-derived software reference renderer, not a golden image recorded from a particular console. It verifies the captured 2D composition against the reference logic and helps find renderer differences. Because the model reads the generated tile/map/palette data, it does not independently validate CPU writes into those inputs; compare the top-screen difference image and bottom-screen totals when diagnosing a result. A score below 100% signals a pixel mismatch, not necessarily an emulator defect until the reference model and any hardware-specific behavior have been checked. This scoring revision has been compile- and ROM-header-validated, but has not yet been run in an emulator or on hardware; verify its initial score behavior against a known-good device/emulator before treating percentages as calibrated benchmark results.

## Test cards

| # | Test | 2D behavior exercised |
|---|---|---|
| 01 | Text BG / palette / tile flips | Mode-0 4bpp text BG, map entries, palette bank and H/V flips. |
| 02 | Scroll offsets / map wrap | Non-tile-aligned horizontal/vertical offsets `(13, 7)` and map wrapping. |
| 03 | BG priority / alpha blend | Two text BGs, priority ordering, alpha source/target masks and 8/16 coefficients. |
| 04 | Affine tile BG / wrap | Mode-2 8bpp affine BG, affine matrix, 16×16 tile map and wrap. |
| 05 | Extended 16-bit bitmap BG | Mode-5 256×256 RGB555 bitmap, opacity bit and edge/checker pattern. |
| 06 | Affine bitmap / repeat edge | Mode-5 affine bitmap matrix and wrapping. |
| 07 | Window mask / BG mosaic | WIN0 geometry, WININ/WINOUT layer masks and 4×4 BG mosaic. |
| 08 | OBJ size / priority / transparency | 1D tiled OBJ mapping, 4bpp sprites, 8/16/32/64-pixel sizes, H/V flips, priority and transparent pixels. |

All eight test scenes are drawn and scored on the main engine. The sub engine remains in Mode 0 with BG0 console enabled and no test pattern drawn behind it, so the bottom screen stays dedicated to the current test and score.

## Agent workflow

1. Start `nds-2d-agent-suite.nds` in an NDS emulator; no buttons are required.
2. Read the bottom screen's test number and score. Capture both screens if you want to inspect the per-pixel difference map.
3. For each card, record the percentage and the colored mismatch pattern. After the eighth card, record `TOTAL` from the summary.
4. Include emulator/version and the card number when reporting a mismatch. A matching score is a pixel comparison against the software reference, not a proof that every untested NDS feature is correct.

Each card's result is held for 30 VBlanks, so the eight-card run is about four seconds plus scene setup and capture work. The final results screen is persistent; press START to rerun. The ROM does not need emulator-specific debug ports or host-side screenshot analysis for its numeric scores.

## Build

The included ROM was built with devkitARM, libnds, and ndstool. On Debian/Ubuntu-like systems, install devkitPro's NDS toolchain (`nds-dev` includes devkitARM, libnds, and ndstool); see [devkitPro's installation guide](https://devkitpro.org/wiki/devkitPro_pacman) for current platform-specific instructions.

```sh
curl -fsSL -A 'dkp-apt' https://apt.devkitpro.org/install-devkitpro-pacman -o /tmp/install-devkitpro-pacman
sudo bash /tmp/install-devkitpro-pacman
sudo dkp-pacman -S nds-dev
source /etc/profile.d/devkit-env.sh
cd nds-2d-agent-suite
make clean && make
```

Build output is `nds-2d-agent-suite.nds`. The Makefile uses devkitARM's `ds_rules` and libnds.

## Source layout

- `source/main.c` — test scenes, software reference rasterizer, capture comparison, difference overlay, score/cycle UI.
- `Makefile` — devkitARM/libnds build.
- `test-manifest.json` — machine-readable test and scoring description.
- `tools/fix_header.py` — assigns the homebrew game title/code and refreshes the ROM header CRC.
- `nds-2d-agent-suite.nds` — built ROM.

## Scope

Current scoring covers main-engine composed pixels for these eight scenes. It does not cover every DS mode, extended palettes, all BG sizes/scroll edge cases, all OBJ shapes, sub-engine pixel capture, scanline timing, or 3D. The total should be read as a score for this test set, not a general compatibility percentage for all NDS graphics.