# Polygon rasterization regression test

This directory contains the supplied `suite-poly.nds` ROM and a decoder for
its machine-readable result block. The ROM runs 27 CPU/system groups followed
by group 27, **POLY RASTER**; group 27 compares 77 scenes against recorded
pixels (102,942 expected pixels in total).

The test ROM is provided as a prebuilt binary; its source tree is not present
in this repository. SHA-256 of the supplied ROM:

```text
5e6bb0f1141111f37a974b039a5fdc2a91716e599d96b803b2e46e9271e35c1c  suite-poly.nds
```

## Build a test DOL

From the repository root, with devkitPro's Wii toolchain installed:

```sh
make poly-test-dol
```

This creates `NooDS-Wii-poly-test.dol` with the RAM-dump, scripted-input and
fast-test switches enabled (`TEST_JIT=0` uses the interpreter by default;
set `TEST_JIT=1` to build the JIT variant). `TEST_FAST=1` removes the host-side frame limiter
and pins `Settings::frameskip` to zero before every emulated frame; it does not
change guest timing. The normal release target is separate and does not include
the test-only harness.

## Run under headless Dolphin

Create a 128 MiB FAT32 image, copy the ROM to its root, and add this
`autoboot.txt` to its root:

```text
sd:/suite-poly.nds
MAX_FRAMES=5000
CHECKPOINTS=500,1000,2000,3000,4000,5000
INPUT_A_FROM=320
INPUT_A_TO=322
```

The two-frame A pulse starts the suite's run sequence. All test runs must use
frameskip 0. The test DOL forces it to zero and writes the value to each
checkpoint log as `skip=0`.

Example Dolphin command (adjust the executable, profile and image paths for
your system):

```sh
dolphin-emu-nogui --platform=headless --video_backend=Null \
  --user=/tmp/dolphin-profile \
  --exec=NooDS-Wii-poly-test.dol \
  -C Dolphin.Wii.WiiSDCard=True \
  -C Dolphin.Wii.WiiSDCardAllowWrites=True \
  -C Dolphin.General.WiiSDCardPath=/path/to/sd.raw \
  -C Dolphin.Core.EnableAudio=False
```

At `MAX_FRAMES`, the test DOL writes `mem_dump.bin` and `fb_dump.bin` to the
virtual SD root. Extract `mem_dump.bin` and decode it with:

```sh
python3 tests/decode_poly.py mem_dump.bin --scenes
```

A verified full pass requires header state `complete`, 77 passed scenes, zero
failed and errored scenes, 102,942 expected pixels, zero missing/overdraw/
wrong-colour pixels, and group 27 marked `PASS`. A header in state `running`
with pending scene records is only a partial run—not an accuracy result.

## Result layout

For per-frame VRAM inspection, add `DUMP_VRAM_FROM=550` and
`DUMP_VRAM_TO=900` to `autoboot.txt`. The test DOL then appends records to
`vramA_frames.bin`: a four-byte little-endian frame number, the raw 20-word
PLY5 header, then the 128 KiB VRAM-A bank. This optional capture is useful for
correlating a failing scene with the actual display-capture pixels.

The guest result block begins at `0x023F0400`. The full-memory dump has a
`0x25000`-byte prefix, followed by guest RAM, so the corresponding file offset
is `0x415400`. It contains a 20-word header and 77 records of 12 words each.
The existing group table is at `0x023F0100`; each group record is two words
(state and code). The decoder reads the words little-endian, as stored by the
emulated ARM9.
