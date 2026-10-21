# NooDS-Wii v1.63 (`NooDS-Wii-v1pt63`)

NooDS-Wii is a Nintendo DS & Game Boy Advance emulator for the Nintendo Wii by radicalten (ported from Hydr8gon's NooDS).

## Accuracy & Stability Improvements in v1.63

Verified headlessly under Dolphin with `frameskip = 0` across all three automated hardware accuracy suites:

| Test Suite | Target Subsystem | `default-NooDS-Wii-v1pt5` Baseline | **`NooDS-Wii-v1pt63`** |
| --- | --- | --- | --- |
| **`suite.gba`** ([mGBA Suite Auto](https://github.com/mattrbeck/mgba-suite-auto)) | GBA CPU, Memory, I/O, Timing, Prefetch, Timers, IRQ, Math, DMA, SIO | `3,498 / 6,998` (**49.99%**) *(OOM-crashed at Frame 24 before heap fix)* | **`6,997 / 6,998` (99.99%)** — **12 / 13 sub-suites at 100.00%!** |
| **`nds-2d-agent-suite.nds`** | NDS 2D BG/OBJ/Blend/Affine/Window/Mosaic | `8 / 8` cards (**100.00%**) | **`8 / 8` cards (100.00%, exact framebuffer match)** |
| **`suite-poly.nds`** | NDS ARM9/ARM7 CPU + 3D Polygon Rasterizer | `26 / 28` groups (**92.86%**), `77 / 77` 3D scenes (**100.00%**) | **`28 / 28` groups (100.00%)**, **`77 / 77` 3D scenes (100.00%, 0 pixel errors)** |

### Summary of Key Fixes
- **Cycle-Accurate GBA Memory Controller & 8-Halfword GamePak Prefetch Buffer (`NooDS-Wii/gba_timing.h`):** Full `WAITCNT` (`0x04000204`) non-sequential (`N`) and sequential (`S`) waitstates across WS0/WS1/WS2/SRAM/EWRAM/VRAM, 32-bit buffered ARM prefetch pops, partial in-flight prefetch stalls, and CPU/DMA pipeline phase modeling (`Timing tests`: `365/2020 -> 2020/2020 = 100.00%`).
- **GBA Bus Open Bus, SRAM, Palette/VRAM/OAM 8-bit Stores, & I/O Read Decoder (`NooDS-Wii/memory.cpp`):** Multiplexed GamePak OOB `AD[15:0]` reads, BIOS open-bus state latching (`Reset`/`SWI`/`IRQ`), 8-bit SRAM lane mirroring across `0x0E000000..0x0FFFFFFF`, 8-bit BG VRAM/Palette duplication vs OBJ VRAM/OAM ignore, and 32-bit I/O word open-bus decoding (`Memory tests`: `1215/1552 -> 1552/1552 = 100.00%`, `I/O read tests`: `48/130 -> 130/130 = 100.00%`).
- **GBA DMA Engine (`NooDS-Wii/dma.cpp`):** Per-channel 32-bit `dmaLatch[4]`, exact channel address/count masks (`DMA3` DRQ bit `0x08000000` & `0x4000`/`0x10000` zero-count reloads), forced source increment in GamePak ROM space, 2-cycle startup delay, and separate dot-240 (`960`) HBlank DMA vs dot-251.5 (`1006`) `DISPSTAT` HBlank flag/IRQ (`DMA tests`: `984/1244 -> 1244/1244 = 100.00%`).
- **GBA Global Prescaler Timers & IRQ Synchronization (`NooDS-Wii/timers.cpp`, `NooDS-Wii/interpreter.cpp`):** Shared 10-bit system clock phase alignment for `f/64`, `f/256`, and `f/1024`, 2-cycle timer enable latency, 1-cycle `TMxCNT_L` reload shadow latching, and non-drifting event dispatch (`Timer count-up tests`: `128/936 -> 936/936 = 100.00%`, `Timer IRQ tests`: `0/90 -> 90/90 = 100.00%`).
- **ARM7TDMI Multiplier Carry & HLE BIOS Math/Copy (`NooDS-Wii/arm7tdmi_mul.h`, `NooDS-Wii/hle_bios.cpp`):** Radix-4 Booth multiplier carry flag (adapted under zlib license from `zaydlang/multiplication-algorithm`), fixed unsigned `UMULL`/`UMLAL` early-termination ticks, exact integer `Div`/`DivArm`/`Sqrt`/`ArcTan`/`ArcTan2` & `CpuSet`/`CpuFastSet` behavior (`Multiply long`: `52/72 -> 72/72 = 100.00%`, `BIOS math`: `404/615 -> 615/615 = 100.00%`).
- **GBA Serial I/O (`SIO`) & Transfer Timing (`NooDS-Wii/memory.cpp`):** Complete Normal-8, Normal-32, Multi-player, UART, GPIO, and JOY Bus register handling + internal-clock transfer scheduler (`SIO R/W`: `65/90 -> 90/90 = 100.00%`, `SIO timing`: `0/4 -> 4/4 = 100.00%`).
- **ARM946E-S CP15 Memory Protection Unit (`NooDS-Wii/cp15.cpp`):** Full 8-region priority matching, 4-bit extended data/code AP permissions, and exact ARM/Thumb Prefetch Abort (`0x0C`) & Data Abort (`0x10`) exceptions (`suite-poly.nds`: `26/28 -> 28/28 = 100.00%`).
- **Wii `libogc 3.x` Heap & Audio Limiter Stability (`NooDS-Wii/main.cpp`, `NooDS-Wii/spu.cpp`):** Eliminated non-freeing `Noods_MEM2_Alloc` / `IsMem2Ptr` memory leak (which exhausted all 76 MB of Wii RAM within 24 frames) and bounded SPU `fpsLimiter` waits to prevent audio-backend deadlocks.

## Building

```sh
make clean && make -j2
```

Produces `NooDS-Wii.dol` (with `Settings::frameskip = 0`).

## Wii SD Layout

Optional original DS / GBA BIOS files can be placed at:
- `sd:/noods/bios/bios9.bin`
- `sd:/noods/bios/bios7.bin`
- `sd:/noods/bios/firmware.bin`
- `sd:/noods/bios/gba_bios.bin`

ROMs can be placed in `sd:/noods/roms/`, `sd:/noods/nds/`, or `sd:/noods/gba/`.
