# Real-hardware test build

This folder contains the newly rebuilt Nintendo DS ROM and the complete source snapshot used to build it. Use **this `suite.nds`** for the requested DSi/3DS test; it is not the older `e72f…` ROM from the original white-screen report.

## ROM identity

- File: `suite.nds`
- Size: 2,036,233 bytes
- SHA-256: `8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3`
- ARM9 entry/load: `0x02000100`
- ARM7 entry/load: `0x02200000`
- NDS header CRC: `0x6601` — verified by `ndstool`
- NDS logo CRC: `0x9E1A` — verified by `ndstool`

`SHA256SUMS` in this folder records the ROM and package-file checksums.

## Changes included

The targeted hardware-compatibility correction is the `POWCNT1` register access width. The DS ARM9 register at `0x04000304` is 16-bit, but the pinned upstream ARM9 header declared it as `vu32*`. `scripts/build.sh` now patches only its private copied header from `vu32*` to `vu16*`; the pinned vendor source is left unchanged. The rebuilt ARM9 code uses halfword `STRH` writes for this register in both early display initialization and lower-screen setup. The register width is documented by [GBATEK](https://mgba-emu.github.io/gbatek/).

The build also regenerated the checked-in trace-v5 test tables from the project generators (13,380 ARM9 vectors and 12,056 ARM7 vectors). The exact pinned upstream source revisions were:

- `rockwrestler`: `b0067a06cddb9bbc9c46ecf0c60a2d572594e581`
- `mgba-suite-auto`: `2b239cfe63f15ec8ab9cbedb4c09498fc98e7b70`

The full source, build scripts, project docs, lockfile, and these upstream source checkouts are in [`source-code/`](source-code/). See [`source-code/README.md`](source-code/README.md) for rebuilding.

## Build checks performed

- `make build` completed successfully with devkitARM GCC 16.1.0 and `ndstool` 2.3.1.
- The build emitted existing warnings for volatile chained assignments, unpredictable assembly constructs, and the ARM9 ELF's RWX load segment; they did not stop the build and are separate from this register-width fix.
- The copied `source-code/` tree was independently rebuilt and reproduced this exact ROM SHA-256.
- `ndstool -i suite.nds` reported the header and logo CRCs as OK.
- `make host` passed, including all five POWCNT1-width regression tests and all current host test targets.
- ARM9 disassembly confirms `STRH` writes to `0x04000304` in both display setup paths; see [`POWCNT1-DISASSEMBLY.txt`](POWCNT1-DISASSEMBLY.txt).

These are build, structural, disassembly, and host-test checks—not a physical-console result. This new ROM has **not** yet been run in DeSmuME or on a DSi/3DS, so the white-screen issue is not claimed fixed.

## Test it on the console

1. Copy this folder's `suite.nds` to the SD card used by your DSi/3DS.
2. Launch it through the same TWiLight Menu++ / nds-bootstrap path and settings used for the earlier test.
3. Check whether both screens show the suite immediately after launch; note any text, sound, restart, or continued white screen.
4. Report the console model, system firmware, TWiLight Menu++ version, nds-bootstrap version, relevant per-game/global bootstrap settings, and the ROM SHA-256 above. A photo/video of the first few seconds is useful.

Please test this exact hash rather than the older `e72f…` file. If both screens remain white, the width fix alone was insufficient and further early-boot instrumentation will be needed.

## Distribution note

This is a local test bundle, not a release package. Redistribution permission for upstream-derived content and the combined ROM remains unresolved; do not publish it. No Nintendo BIOS or firmware is included.
