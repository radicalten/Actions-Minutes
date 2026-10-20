# NooDS-Wii v1pt62 — 2D accuracy work

Fork of `default-NooDS-Wii-v1pt5` with 2D-renderer fixes, validated against
`nds-2d-agent-suite.nds` (main deliverable test) and `suite-poly.nds`
(CPU/3D regression check). All testing done with frameskip = 0.

## Code change

**Horizontal BG mosaic was missing** (`NooDS-Wii/gpu_2d.cpp`, `Gpu2D::drawText`).
Text BGs with BGCNT bit 6 set only had their vertical coordinate snapped to
the mosaic block; the fetched pixel never repeated across horizontal mosaic
blocks. `drawText` now takes a per-pixel sampling path when mosaic is enabled
that snaps the screen X coordinate to the mosaic period before applying the
scroll offset (`srcX = bgHOfs + (x - x % (mosaicH + 1))`, vertical snap
unchanged), matching the DS hardware model (and melonDS's implementation).
The non-mosaic fast path is untouched.

## Measured results (host build of this source, interpreter core, 8000 frames)

| Card | Test | Before | After |
|------|------|-------:|------:|
| 01 | Text BG / palette / flips | 100.00% | 100.00% |
| 02 | Scroll / map wrap | 100.00% | 100.00% |
| 03 | BG priority / alpha blend | 71.08% | 71.08% |
| 04 | Affine tile BG / wrap | 8.34% | 8.34% |
| 05 | Extended bitmap BG | 100.00% | 100.00% |
| 06 | Affine bitmap / repeat | 100.00% | 100.00% |
| 07 | Window / BG mosaic | 62.17% | 60.06% |
| 08 | OBJ size/priority/transparency | 15.30% | 15.30% |

Regression check, `suite-poly.nds` (interpreter): **PASS 26, FAIL 2, 92.85%,
POLY 77/77, 0 errors** — identical to the pre-change JIT baseline, including
the two pre-existing CPU-group failures (groups 02/03, 64 fails each,
first-bad indices 5859/5891 — bit-identical under interpreter and JIT, so not
a JIT defect; left for CPU-side follow-up).

## Why cards 03, 04, 07, 08 still score low: test-reference artifacts

The suite's README warns it was never run on hardware or in an emulator and
its scores are uncalibrated. Per-card forensic fits (re-rendering each scene
in Python from the captured VRAM/OAM/palette and comparing against the
suite's own green/blue diff classification) show the remaining gaps come
from the ROM's software reference modelling something other than the actual
register state, i.e. these cards would score the same on real hardware:

- **Card 03 (alpha blend):** the reference blends BG2 with the *backdrop*
  where BG3 is transparent, although BLDCNT bit 13 (backdrop as 2nd target)
  is clear. GBATek ("…if so — and only if so…"), the official GBA manual,
  melonDS and DeSmuME all require the target bit. A model with backdrop
  blending reproduces the reference at 100.000%; the emulator follows the
  documented hardware rule.
- **Card 04 (affine tile BG):** the ROM programs BG2CNT = 0xF085 (affine
  size 1024×1024) but generates a 16×16-tile map (256 bytes, 16 tiles); the
  reference renders it as 128×128 with wrap. A 128×128 model matches the
  reference at 99.9%+ (residuals are within the suite's own ±1/channel
  tolerance); the emulator implements the documented 1024×1024 fetch, which
  reproduces the captured (failing) image exactly.
- **Cards 07 & 08 (window/mosaic, OBJ):** the reference renders BG1 with
  scroll (0,0), but the ROM never rewrites BG1HOFS/VOFS after card 02 set
  them to (13,7) — verified by tracing every write to those registers over
  the full run (only the boot clear and card 02's write occur). The emulator
  faithfully keeps (13,7), as hardware would. With scroll (0,0) + mosaic,
  card 07's reference is reproduced at 99.4% and card 08's at 99.5%.
  Card 07's small score dip from the mosaic fix (62.17% → 60.06%) is an
  interaction of the correct mosaic sampling with this stale-scroll scene,
  not a regression in mosaic behaviour.

The "freeze" reported with this ROM is not a hang: the suite needs ~8000
frames (its per-pixel software compare is chunked across frames), which at
Wii/Dolphin wall-clock speed looks frozen for a long time. It always
completes; the emulator's PC/cycle traces advance throughout.

## Known remaining 2D gaps (not exercised by the suite)

- Horizontal mosaic for affine/extended/large BGs and for OBJ is not
  implemented (only text-BG mosaic, both axes, is complete).
- Poly-suite CPU groups 02/03 fail 64 tests each in a systematic block
  (identical in interpreter and JIT); needs CPU-instruction-level follow-up.

## Builds in this folder

- `NooDS-Wii.dol` — default build (`make`), frameskip defaults to 0.
- `NooDS-Wii_jit0_blocks0_browser0_fullram_scriptinput.dol` — interpreter
  test harness (full RAM dump + scripted input), smoke-tested in Dolphin.
- `NooDS-Wii_jit1_blocks0_browser0_fullram_scriptinput.dol` — JIT test harness.

## Update: suite-side fixes (cross-validated)

The test-ROM artifacts documented above have since been fixed in the
`radicalten/Actions-Minutes3` repo (`2d-suite/`, commit `24b3242`):
transparency-aware blending in the card-3 reference, `BG_RS_16x16` for the
card-4 scene, and per-scene BG scroll resets in `prepare_text_mode()`.
Validation matrix (total matched pixels):

| ROM | v1pt5 baseline | this build (v1pt62) |
| --- | --- | --- |
| original suite | 69.61% | 69.35% |
| fixed suite | 97.57% (card 07 only — the real mosaic gap) | **100.00% (8/8)** |

The fixed suite on the *unmodified* emulator isolates the one genuine
emulator bug this release fixes (horizontal BG mosaic, card 07), and scores
100% everywhere with it — independent confirmation of both fix sets.

## Performance note (why early v1pt62 builds could feel slower)

The first implementation of the horizontal-mosaic path sampled **per
pixel**: one `x % period` (a hardware divide on PowerPC) plus two
memory-system VRAM reads for every screen pixel, versus one fetch per
8-pixel tile in the original path. That only ever affected text BGs with
the BGCNT mosaic bit set — including the no-op case of mosaic size 0 —
but on such layers it was several times more expensive.

The path now fetches **once per mosaic block** and replicates the pixel
across the block, and layers with mosaic size 0 (period 1, a visual
no-op) take the original fast tile path. Pixel output is unchanged
(verified: fixed suite still 8/8 at 100%, original suite scores
identical, poly suite framebuffer byte-identical). Host timings over
the suite's mosaic-containing segment (3800 frames, medians of 5):
v1pt5 code 3503 ms, per-pixel mosaic 3649 ms, block fetch 3482 ms —
the correctness fix now costs nothing measurable. If a comparison
build feels dramatically slower overall rather than in mosaic scenes,
check the CPU mode first: this repo's default `NooDS-Wii.dol` is the
interpreter build (`JIT=0`, the Makefile default, same as v1pt5);
a JIT build (`NooDS-Wii_jit1_blocks0_browser1.dol`) is included for
apples-to-apples comparison with JIT-enabled v1pt5 builds.
