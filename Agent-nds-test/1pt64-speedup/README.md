# NooDS-Wii v1.64 — Broadway PowerPC JIT & Graphics/System Performance Release

`NooDS-Wii v1.64` builds directly on every hardware accuracy fix in `NooDS-Wii v1.63` and delivers major frame-rate and frame-time improvements across **all three verification benchmarks** (`suite.gba`, `nds-2d-agent-suite.nds`, and `suite-poly.nds`) with **`frameskip = 0`** and **`0` correctness regressions across all `7,111` test assertions**.

---

## Head-to-Head Performance Summary (`frameskip = 0`)

All measurements compare `/home/user/NooDS-Wii-v1pt63/NooDS-Wii.dol` against `/home/user/NooDS-Wii-v1pt64/NooDS-Wii.dol` under identical settings (`frameskip = 0`, `threaded2D = 0`, `threaded3D = 0`) using the Wii hardware timebase (`PPCGetTickCount()`).

| Test Suite (`frameskip = 0`) | Metric | `NooDS-Wii v1.63` | `NooDS-Wii v1.64` | Improvement |
| --- | --- | ---: | ---: | ---: |
| **`suite.gba`** (`2,050` iters / `561` GBA frames) | **Live On-Screen `Core::fps`** | `47 FPS` | **`56 FPS`** | **`+19.1%` (`1.19x`)** |
| | Total Display Elapsed (`DISPLAY elapsed_us`) | `11.372 s` (`11,371,959 us`) | **`9.409 s` (`9,408,756 us`)** | **`1.21x` faster (`-1.96 s`)** |
| | Core Emulation Time (`PERF accum_us`) | `9.746 s` (`9,745,917 us`) | **`9.165 s` (`9,165,240 us`)** | **`+6.3%` faster** |
| | GX Render + Flush (`render_us + flush_us`) | `1,086.0 ms` (`615.9 ms` flush) | **`316.3 ms` (`0.155 ms` flush)** | **`3.43x` faster** |
| **`nds-2d-agent-suite.nds`** (`6,000` iters / `445` active frames) | **Live On-Screen `Core::fps`** | `5 FPS` | **`17 FPS`** | **`3.40x` higher (`+240%`)** |
| | Total Display Elapsed (`DISPLAY elapsed_us`) | `104.364 s` (`104,364,155 us`) | **`24.750 s` (`24,750,384 us`)** | **`4.22x` faster (`-79.61 s`)** |
| | Core Emulation Time (`PERF accum_us`) | `100.999 s` (`100,998,548 us`) | **`23.787 s` (`23,786,753 us`)** | **`4.25x` faster (`-77.21 s`)** |
| | GX Render + Flush (`render_us + flush_us`) | `55,542.0 ms` (`46.71 s` flush) | **`650.6 ms` (`0.147 ms` flush)** | **`85.4x` faster** |
| **`suite-poly.nds`** (`1,300` frames, 28 CPU + 77 3D) | **Live On-Screen `Core::fps`** | `10 FPS` | **`33 FPS`** | **`3.30x` higher (`+230%`)** |
| | Total Display Elapsed (`DISPLAY elapsed_us`) | `143.560 s` (`143,560,147 us`) | **`66.011 s` (`66,010,594 us`)** | **`2.17x` faster (`-77.55 s`)** |
| | Core Emulation Time (`PERF accum_us`) | `134.061 s` (`134,060,819 us`) | **`63.376 s` (`63,376,021 us`)** | **`2.12x` faster (`-70.68 s`)** |
| | GX Render + Flush (`render_us + flush_us`) | `72,758.4 ms` (`60.66 s` flush) | **`1,647.7 ms` (`0.417 ms` flush)** | **`44.2x` faster** |

---

## Zero-Regression Accuracy Verification (`frameskip = 0`)

Every single test assertion across all three suites matches `NooDS-Wii v1.63` with **zero regressions**:

| Suite | Sub-Tests / Verification | `v1.63` Score | `v1.64` Score | Determinism / Checksum Verification |
| --- | --- | :---: | :---: | --- |
| **`suite.gba`** (`mattrbeck/mgba-suite-auto` `r101`) | Memory `1552/1552`, I/O `130/130`, Timing `2020/2020`, Timers `936/936`, Timer IRQ `90/90`, Shifter `140/140`, Carry `93/93`, MulLong `72/72`, BIOS Math `615/615`, DMA `1244/1244`, SIO R/W `90/90`, SIO Timing `4/4`, Misc `11/12` | `6,997 / 6,998` (`99.99%`) | **`6,997 / 6,998` (`99.99%`)** | Exact cycle & state match: `CYCLES = 157711793`, `MEM_HASH = C7041162B0CEE898`, `FB_HASH = CDA7DD02258E8C35` |
| **`nds-2d-agent-suite.nds`** | Cards `01`–`08` (Text BG flips, scroll wrap, alpha blend, affine tile wrap, 16-bit bitmap, affine bitmap, window + 4x4 mosaic, OBJ mapping/flips/priority) | `8 / 8` (`100.00%`) | **`8 / 8` (`100.00%`)** | **100% byte-for-byte identical dual-screen framebuffer** vs `v1.63`: `FB_HASH = 3A0E82641E2AA7F5` |
| **`suite-poly.nds`** | **28 ARM9/ARM7 CPU & hardware groups** (including CP15 MPU prefetch/data aborts, self-modifying code, timers, DMA, Div/Sqrt, IPC) + **77 3D polygon scenes** | `28 / 28` CPU + `77 / 77` 3D (`100.00%`) | **`28 / 28` CPU + `77 / 77` 3D (`100.00%`)** | Exact pixel & memory match: `102,942` expected px (`0` missing, `0` overdraw, `0` wrong-color), `CYCLES = 728095770`, `MEM_HASH = 1CF4E24E52C1C0F0`, `FB_HASH = 513C66AF6C80456F` |

---

## Key Architectural Optimizations in `v1.64`

### 1. Broadway / PowerPC 750CL Multi-Instruction & Loop Dynamic Recompiler (`NooDS-Wii/arm_jit.h`, `NooDS-Wii/arm_jit.cpp`, `NooDS-Wii/jit_bridge.S`)
- **8 MiB MEM2 Executable Code Pool (`main.cpp`):** Reserves an aligned 8 MiB arena in Wii MEM2 (`SYS_GetArena2Lo` / `SYS_SetArena2Lo`) before `fatInitDefault()`, eliminating code-pool thrashing while preserving full MEM1 headroom.
- **Native ARM & Thumb Basic Blocks + Bounded Hardware Loop Execution:** Compiles up to 24 guest instructions per block into native big-endian PowerPC 750CL machine code with dead-flag elimination (`emitFlags`), inline byte-reversed (`lwbrx`/`lhbrx`/`stwbrx`/`sthbrx`) RAM loads/stores through `readMap`/`writeMap`, native `PUSH`/`POP` stack bursts, guarded MMIO fast polling (`DISPSTAT`/`VCOUNT`/`KEYINPUT`), inline direct/indirect branches (`B`/`BL`/`BX`/`LDR PC`), and in-block multi-iteration execution up to the exact next scheduled hardware event timestamp.
- **Slow-Path `r15` + `pcData` Preservation (`emitHelperR15Sync`):** Saves and restores both `registersUsr[15]` and `cpu->pcData` inside `jit_enter`'s stack frame (`8(r1)` / `12(r1)`) across C++ MMIO store helpers so mid-block `VRAMCNT`/`WRAMCNT`/`CP15` remaps calling `Memory::updateMap9() -> getOpcode16()` never shift block-epilogue `pcData` deltas or clobber writeback scratch registers.
- **Tiered Hotness Hysteresis & Sticky Negative Cache (`s_candPc` / `s_candHits`):** Compiles blocks only on their 3rd visit and negatively caches ineligible sites and single-instruction non-loop branches (`firstIsTerm && !selfLoop`), eliminating ~75–95% of cold unit-test compilations, avoiding cache evictions of hot blocks, and bypassing 1-instruction `stmw`/`lmw` wrapper stubs directly to inline C++ handlers.
- **64-Byte Granular Self-Modifying Code & Region-Scoped Map Invalidation:** Tracks compiled code lines at 64-byte granularity (`codeLineBits`) with 4 KB `pageEpoch` counters + tail FNV-1a hashes so writes to non-code RAM and non-code `VRAMCNT` remaps incur zero JIT invalidations.

### 2. CP15 MPU O(1) Page Permission Cache & `BlocksDS` / `libnds` Fast Gate (`NooDS-Wii/cp15.h`, `NooDS-Wii/cp15.cpp`)
- Added a lazy generation-tagged `1M x 1B` 4 KB page permission cache (`pagePerm` / `pageGen`) replacing repeated 8-region scans per instruction.
- Recognized `BlocksDS` / `libnds` default MPU layouts where `.itcm` (`0x01FF8000`) is mapped privileged read-only (`DataAP = 6`) with a 32 KB-aligned base address (`0x01FF801D`), keeping `mpuFastPriv = true` across all `249M` cycles of `nds-2d-agent-suite.nds` (`disp9 = 58.35M`, `184.47M` native instructions) while preserving full abort-rollback semantics for custom data protection tests in `suite-poly.nds` Group 26.

### 3. Wii GX Presentation & Asynchronous Double-Buffered Display (`NooDS-Wii/wii_video.h`, `NooDS-Wii/wii_video.cpp`, `NooDS-Wii/main.cpp`, `NooDS-Wii/gpu.h`, `NooDS-Wii/gpu.cpp`)
- **Non-Blocking Double-Buffered XFB Copy (`Wii_VideoFlushAsync`):** Replaced blocking `GX_PixModeSync()` + `GX_DrawDone()` stalls (`46.7 s` in `nds-2d-agent-suite.nds` and `60.7 s` in `suite-poly.nds`!) with asynchronous `GX_CopyDisp` across alternating MEM1 XFBs synchronized cleanly by `VIDEO_WaitVSync()`, cutting `flush_us` from **`60.66 s` down to `0.42 ms` (`> 140,000x` reduction)**.
- **Single-Pass Combined `512 x 192` GX `RGB5A3` Tile Swizzler:** Precomputed a 256 KB RGB666->`RGB5A3` lookup table and packed both NDS screens into a single `512 x 192` texture (or `240 x 160` for GBA) without `new[]`/`delete[]` per frame, slashing `render_us` by **`7.3x–13.6x`**.
- **Lock-Free 4-Slot GPU Frame Pool (`Gpu`):** Replaced per-frame `new uint32_t[]` / `delete[]` heap allocations between emulator and display threads with a pre-allocated 4-slot ring buffer.

### 4. GBA Fast Loop, Event Queue, 2D Scanline Fast Paths & SPU Idle Batching (`NooDS-Wii/core.h`, `NooDS-Wii/interpreter.cpp`, `NooDS-Wii/gpu_2d.cpp`, `NooDS-Wii/spu.cpp`)
- **Flat Sorted `EventQueue` (`core.h`):** Replaced doubly-linked `std::list<SchedEvent>` heap node allocations with a contiguous fixed-capacity ring array using binary-search insertion.
- **Inlined GBA CPU + Prefetch Fast Path (`interpreter.cpp`, `gba_timing.h`):** Kept the tight GBA instruction loop completely free of per-opcode probe overhead while preserving cycle-exact gamepak prefetch duty.
- **2D Scanline Composition & LUT Fast Paths (`gpu_2d.cpp`):** Added a 32K `rgb5ToRgb6` LUT, early fast path when non-composed display modes are active without display capture, and an unblended `layers[0]` RGB666 fast path that preserves exact `getRawLine()` output for `DISPCAPCNT`.
- **SPU Silent Batching & Active Audio DMA Check (`spu.cpp`, `main.cpp`):** Coalesces silent SPU ticks into 32-sample batches (`32x` fewer scheduler events when sound is disabled) and avoids 30 Hz back-pressure stalls when no host audio callback is active.

---

## Building & Running Tests

```bash
# Default release build (JIT=1 BLOCKS=1 JIT_STATS=0 -> NooDS-Wii.dol)
make -j4

# Build with detailed JIT runtime telemetry counters enabled in g2_state.log
make -j4 JIT_STATS=1

# Run full 3-ROM automated accuracy verification (frameskip = 0)
/home/user/test/run_all.sh /home/user/NooDS-Wii-v1pt64/NooDS-Wii.dol
```
