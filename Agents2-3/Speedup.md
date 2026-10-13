# AGENTS.md — Performance Review of NooDS-Wii

Repository reviewed: `radicalten/Actions-Minutes2` → `NooDS-Wii/NooDS-Wii` (a Wii port of
Hydr8gon's **NooDS** Nintendo DS/GBA emulator, built with devkitPPC for the Wii's
PowerPC "Broadway" CPU).

This document is written as an `AGENTS.md` so that a coding agent (or a human
contributor) picking up this repository next has a concrete, prioritized backlog
of speedup work, including *why* each change matters and *where* to make it.

> Scope note: this review is based on reading the source on GitHub
> (`arm_jit.cpp/.h`, `interpreter.cpp`, `memory.cpp/.h`, `gpu_2d.cpp`,
> `gpu_3d_renderer.cpp`, `wii_video.cpp`, `core.cpp/.h`, `defines.h`, `Makefile`,
> `README.md`). It is a static/manual review, not a profiled one — recommendation
> #1 below is to get real `gprof`/cycle-counter data before micro-optimizing
> further, but several issues are obvious from the code and build config alone.

---

## TL;DR — Highest-impact items first

1. **The custom ARM→PowerPC JIT exists but is compiled out by default.** This is
   by far the single biggest win available: `JIT ?= 0` and `BLOCKS ?= 0` in the
   `Makefile` mean the shipped/default build runs the **pure C++ interpreter**
   for both the ARM9 and ARM7 cores, even though a full dynarec (`arm_jit.cpp`,
   ~1500+ lines of hand-rolled PowerPC code emission, block compilation, SMC
   invalidation, epoch-based cache management) is already implemented.
2. **Poll-loop skipping (`NOODS_JIT_POLL_SKIP`) is also off by default.** This
   feature fast-forwards through idle spin-loops (the classic
   `while (!flag);` wait patterns DS games use for VBlank/IPC/DMA sync) and is
   one of the cheapest, safest wins in any interpreter-based emulator.
3. **README confirms the symptom**: "DS games run at 3-15 fps, GBA games at
   10-35 fps" — i.e., the emulator is currently far from full speed, consistent
   with running the interpreter-only configuration.
4. Several hot loops (2D compositing, 3D rasterizer, frame presentation) do
   more per-pixel work than necessary (generic memory dispatch calls,
   PowerPC integer division in inner loops, and a full-frame CPU-side pixel
   shuffle every frame). These are secondary to #1-#2 but still meaningful
   once the CPU interpretation bottleneck is addressed.

---

## 1. Build configuration: ship the JIT

**File:** `NooDS-Wii/Makefile`

```make
JIT    ?= 0
BLOCKS ?= 0
...
JIT_DEFS := -DNOODS_JIT=$(JIT) -DNOODS_JIT_BLOCKS=$(BLOCKS)
```

**Problem:** `arm_jit.cpp`/`arm_jit.h` implement a genuine ahead-of-execution
translator from ARM/Thumb opcodes to native PowerPC 750CL instructions,
including:
- A stub table and block table with epoch-based invalidation for self-modifying
  code (`invalidateCpu`, `invalidateSharedAddr`, `invalidateRange`).
- Native code emission for common ALU/shift/branch/load/store instructions
  (`emitNativeArm`, `emitNativeThumb`, `emitNativeLoadArm`, `emitNativeStoreArm`).
- Multi-instruction basic-block compilation (`compileBlock`) that chains
  several guest instructions into one native PPC routine before returning to
  the scheduler, amortizing dispatch overhead.
- A profiling/stat facility (`ArmPpcJit::stats`, `dumpStats`) already built in
  to measure dispatch counts, fallback reasons, and pool wraps.

None of this runs unless the build sets `JIT=1 BLOCKS=1`. Interpreting ARM9 at
~67 MHz + ARM7 at ~33 MHz entirely in C++ on a 729 MHz in-order PPC750CL (which
itself needs ~5-15+ host instructions per guest instruction for a naive
switch-dispatch interpreter) is the single largest reason the project runs at
3-15 fps on DS content.

**Action items:**
- [ ] Change the default build to `JIT ?= 1` and `BLOCKS ?= 1` (or at minimum
      change the release/distributed `.dol` build target to use these flags).
      The code already supports toggling them independently for bisection if a
      regression appears.
- [ ] Add a CI/test matrix entry (or at least a documented manual test pass)
      that boots a handful of known-good ROMs under `JIT=1 BLOCKS=1` and
      compares output/stability against `JIT=0`, since a dynarec is exactly the
      kind of subsystem that can introduce subtle correctness bugs (flag
      computation, PC-relative addressing, self-modifying code, condition code
      edge cases). Use `ArmPpcJit::dumpStats()` (already implemented) to check
      the `fallbackInstrs` / `blockCompileFails` counters on real games — a
      high fallback rate on a given title tells you exactly which opcode
      classes still need native translation (see `fbOpc7` / `fbClassArm` /
      `fbClassThumb` stats, and the `JIT_FBOPC7` / `JIT_LEAD_LOAD` diagnostic
      dumps already wired up).
- [ ] Once `JIT=1` is the default, also turn on
      `NOODS_JIT_POLL_SKIP` (see §2) and keep `NOODS_JIT_NATIVE_HALF_LOADS`
      and the DISPSTAT/KEYINPUT fast paths in mind as the next layer of
      fallback reduction (see §3).

This is a build-flag change plus a validation pass — not a rewrite — and should
be done *first*, before spending time on any of the micro-optimizations below,
since it changes the performance profile of everything else (e.g. GPU draw
time becomes relatively more important once CPU emulation speeds up).

---

## 2. Enable poll-loop / idle-loop skipping

**File:** `arm_jit.h` / `arm_jit.cpp`

```c
#ifndef NOODS_JIT_POLL_SKIP
#define NOODS_JIT_POLL_SKIP 0 // Joint fixed-point poll-loop skipping (ARM9 + ARM7); off by default
#endif
```

The implementation (`pollExecArm`, `pollJointSkip`, `pollSnapshot`,
`PollTrack`) detects when a CPU returns to the same PC with identical register
/ CPSR / pipeline state and no intervening "impure" dispatch (i.e. a classic
spin-wait), measures the loop period once, and then fast-forwards both CPUs by
whole iterations up to the next scheduled hardware event. This is a
well-known, safe technique (similar to "IdleSkip" in other ARM/MIPS emulators)
and is especially valuable on DS titles, which spend a large fraction of time
in `swi WaitForVBlank`/IPC-sync spin loops.

**Action items:**
- [ ] Turn `NOODS_JIT_POLL_SKIP` on by default alongside `JIT`/`BLOCKS`.
- [ ] Validate timing-sensitive titles (anything that polls hardware timers in
      a tight loop for audio/video sync) after enabling it — the code already
      restricts skipping to NDS mode (`core->gbaMode || core->dsiMode` bails
      out) and never crosses a scheduled event boundary, which is the right
      safety invariant, but real-world titles should still be smoke-tested.
- [ ] Consider extending the same idle-detection concept to GBA mode (`pollExecArm`
      currently special-cases out of GBA/DSi), since many GBA titles also spin
      on VBlank/VCount.

---

## 3. Reduce interpreter fallbacks further

**File:** `arm_jit.cpp`

Several native-translation paths are explicitly gated off by default even when
`JIT_BLOCKS=1`:

```c
#ifndef NOODS_JIT_NATIVE_HALF_LOADS
#define NOODS_JIT_NATIVE_HALF_LOADS 0
#endif
#ifndef NOODS_JIT_DISPSTAT_FAST
#define NOODS_JIT_DISPSTAT_FAST 1
#endif
#ifndef NOODS_JIT_KEYINPUT_FAST
#define NOODS_JIT_KEYINPUT_FAST 1
#endif
```

`isNativeLoadArm`/`canTranslateNativeArm` fall back to the interpreter for:
- Any ARM9 halfword load (`LDRH`) unless it's a guarded DISPSTAT/KEYINPUT read
  (`NOODS_JIT_DISPSTAT_FAST`/`NOODS_JIT_KEYINPUT_FAST`, on by default), and
  ARM7 halfword loads are *never* natively translated by default
  (`kNativeHalfLoads = NOODS_JIT_NATIVE_HALF_LOADS != 0` is `false`).
- Register-offset (non-LSL-scaled) addressing modes.
- PC-relative loads/stores (left to the interpreter intentionally; fine).
- CP15 opcodes on ARM9 (`0x0E000010` pattern) are always excluded from blocks.

**Action items:**
- [ ] Profile real games with `ArmPpcJit::dumpStats()`'s `JIT_LEAD_LOAD` /
      `JIT_FBOPC7` output (already implemented behind
      `NOODS_JIT_LOAD_HIST`/`NOODS_JIT_PROFILE` compile switches — just enable
      them for a profiling build) to find which fallback classes are actually
      hot in practice before investing in `NOODS_JIT_NATIVE_HALF_LOADS=1`
      generally; halfword loads/stores are common for DS audio and tile-map
      access so this is a reasonable next native-translation target once the
      basic JIT is shipped.
- [ ] `armPurity`/`armLoadAddrOk` (used by poll-skip) already have a notion of
      "safe to treat as side-effect-free" loads limited to a handful of I/O
      addresses (`0x04000004`/`0x04000006`/`0x04000180`). Once more game
      profiles are gathered, consider whitening additional frequently-polled,
      read-only registers (e.g. `KEYINPUT` at `0x04000130`, already fast-pathed
      for direct dispatch per `NOODS_JIT_KEYINPUT_FAST`, but not yet part of
      `armLoadAddrOk`'s poll-skip allow-list) so more titles can benefit from
      §2's idle-skip even inside slightly more complex wait loops.

---

## 4. GPU 3D software rasterizer: replace per-pixel division with reciprocal
   tables / shifts

**File:** `gpu_3d_renderer.cpp`

The scanline rasterizer's inner pixel loop performs multiple **integer
divisions by small constants** per pixel, e.g. (abridged from the texture
blending / modulation code):

```c
uint8_t r = ((((texel >> 0) & 0x3F) + 1) * (((color >> 0) & 0x3F) + 1) - 1) / 64;
...
uint8_t a = (((fog >> 18) & 0x3F) * density + ((framebuffer[layer][i] >> 18) & 0x3F) * (128 - density)) / 128;
...
uint8_t r = (((texel >> 0) & 0x3F) * at + ((color >> 0) & 0x3F) * (63 - at)) / 64;
```

PowerPC 750CL's integer divide (`divw`) is not pipelined and costs on the
order of 20 cycles, far more than a multiply or shift. Every one of these
divisions is by a compile-time-constant power of two (64, 16, 128) or a
near-power-of-two (e.g. `/64` appearing dozens of times across blend modes),
so they should compile down to shifts automatically **only if the compiler can
prove the operands are unsigned and the divisor is a literal power of two**.
A few of these expressions mix signed/unsigned intermediate types or go
through `uint32_t`/`uint8_t` casts that can block this optimization in
practice.

**Action items:**
- [ ] Audit the blend/texture-combine helpers (`drawPolygon`'s mode 0/1/2/3
      branches, `interpolateColor`, `interpolateLinear`, `interpolateFactor`,
      fog blending) and make sure every `/64`, `/16`, `/128`, `/32` divisor is
      applied to an `unsigned` operand with the division written as `>> 6`,
      `>> 4`, `>> 7`, `>> 5` explicitly (or confirm with `-S`/objdump that GCC
      is already emitting shifts — don't assume).
- [ ] The handful of divisions by **non-power-of-two** constants (`/ 64`-style
      "NDS 6-bit blend" math is fine since 64 is POT, but double check mixed
      terms like `... / 16` combined with `std::min(...)` and signed `int`
      temporaries) may need an explicit cast to `uint32_t` before the shift to
      get correct *and* fast code.
- [ ] For non-constant divisors (if any remain, e.g. variable `density`/`factor`
      denominators used via `interpolateFactor`), consider a small reciprocal
      lookup table (denominators are bounded, e.g. `0..0x1FF` from `wShift`
      math) traded for a multiply + shift, following the same pattern already
      used for `fogTable`.
- [ ] The renderer already supports a multi-threaded scanline path
      (`drawThreaded`, `KThreadPrepare`/`KThreadResume`, capped at 4 threads).
      Confirm the thread count cap and per-thread scanline partitioning are
      tuned for the Wii's dual-core-equivalent (Broadway is single-core, but
      NooDS-Wii apparently spreads rendering across OS threads to use idle
      cycles/overlap with CPU emulation) — worth profiling whether thread
      creation/synchronization overhead (`KThreadPrepare` running per *frame*
      the thread set is rebuilt, per the "Clean up previous frame threads"
      comment) outweighs the benefit at the current resolution, versus
      keeping a persistent thread pool that just gets signaled per frame
      instead of recreated.

---

## 5. GPU 2D compositor: avoid generic `Memory::read<T>()` in per-pixel loops

**File:** `gpu_2d.cpp`, `memory.h`

`Memory::read<T>()` is `FORCE_INLINE` and reasonably fast (direct pointer-map
lookup, `readMap9A/B`/`readMap7` indexed by `address >> 12`), which is good.
However, it is still called **once per pixel** (sometimes twice, for tile +
index data) inside the hottest loops in the emulator — `drawText`,
`drawAffine`, `drawExtended`, `drawLarge`, `drawObjects` — each of which
re-derives the VRAM address, re-checks the ARM7/ARM9/GBA mode branch inside
`read<T>`, and re-indexes the page map for every single pixel, even though
consecutive pixels within one tile (or one scanline of a bitmap background)
are contiguous in memory and map to the *same* 4KB page.

**Action items:**
- [ ] In the tile-based background drawers (`drawText`'s 4-bit/8-bit paths
      already do this correctly for the *index* bits by reading a 32/64-bit
      word once per tile and shifting — good), make sure the *tile map* fetch
      (`core->memory.read<uint16_t>(gbaMode, tileAddr)`) and extended-palette
      pointer resolution happen once per tile (8 pixels) rather than being
      re-derived per pixel where the current code already loops per-x. Several
      of the bitmap/affine paths (`drawAffine`, `drawExtended`,
      `drawExtendedGba`, `drawLarge`) call `memory.read<uint8_t/uint16_t>()`
      once per *output pixel* with a fresh address computation and page-map
      probe each time; for the common non-wraparound, in-bounds case consider
      resolving a raw `uint8_t*` into the owning VRAM array once per scanline
      (or amortized across a run of pixels while `x`/`y` stay in the same 4KB
      page) and walking it directly, falling back to `memory.read<T>()` only
      at page boundaries or when overflow/wraparound logic is in play.
- [ ] `drawObjects` performs similar per-pixel `memory.read<uint16_t>()` calls
      for bitmap sprites; same pointer-caching opportunity applies, especially
      for the very common "1D mapping, no rotation/scale" sprite path that
      already computes a linear `dataBase + j * 2` address.
- [ ] These are **secondary** to the JIT work (§1) since they make the
      interpreter-bound build look slightly better without addressing the
      actual bottleneck, but they will matter more once CPU emulation speeds
      up and GPU time becomes a larger fraction of frame time.

---

## 6. Frame presentation: CPU-side pixel shuffle every frame

**File:** `wii_video.cpp` — `TileImageRGBA8_fromABGR()`

Every displayed frame, the emulator's software framebuffer (produced by
`gpu_2d`/`gpu_3d_renderer` in a 32-bit ABGR-ish layout) is converted into the
GX hardware's 4×4-tiled `RGBA8` texture format by a **scalar C loop that reads
each pixel, extracts 4 byte channels with shifts/masks, and re-packs them**,
executed for both the top and bottom screens (and again for `GBA_SCREEN`
content). This is pure data movement/reformatting work done entirely on the
PPC's integer unit with no use of Broadway's "paired-single" (PS) SIMD ALU,
which can process two 32-bit-equivalent lanes at once and is exactly the kind
of local, independent, per-pixel transform PS instructions are good for.

**Action items:**
- [ ] Consider having `gpu_2D`/`gpu_3d_renderer` write directly in the channel
      order GX expects (eliminate the `ABGR → RGBA` shuffle entirely) so
      `TileImageRGBA8_fromABGR` only needs to do the 4×4 tiling rearrangement,
      not a per-pixel channel swap — this removes work rather than
      accelerating it.
  - Note this needs to be weighed against the blending math in `gpu_2d.cpp`,
    which stores extra status bits (e.g. `BIT(24)`, `BIT(26)`) in the high
    byte of each framebuffer word; if that byte is reused as alpha going
    forward, the drawing code needs to stop reusing it as scratch space, or
    the tiling pass needs to mask it out explicitly (it likely already does
    via the `& 0xFF`-style masks visible in the blend code — verify).
- [ ] If a full elimination isn't practical short-term, vectorize
      `TileImageRGBA8_fromABGR`/`DrawCharToStaging` with libogc's paired-single
      intrinsics (`paired_lx`/`paired_stx`/`ps_*` or GCC's
      `__PPC750CL` builtins) or at least unroll the inner 4×4 tile loop and
      hoist the per-row base-address math out of the innermost loop.
- [ ] `DCFlushRange()` is called on the *entire* texture buffer after each
      conversion (`DCFlushRange(wiiVid.texDataTop, bufSize)` /
      `...texDataBottom...`). If only part of the frame actually changed
      (common for DS games with mostly-static UI/HUD elements, similar to the
      dirty-row tracking already implemented for the debug overlay via
      `overlayRowDirty`), apply the same dirty-rectangle idea to the main
      screen textures to shrink both the conversion work and the cache-flush
      range. This is more invasive (needs dirty tracking plumbed from
      `gpu_2d`) but the debug overlay code in the same file already
      demonstrates the pattern (`overlayRowDirty[row]`, flushing only dirty
      byte ranges) — reuse that approach for the main framebuffers.

---

## 7. Misc / lower-priority observations

- **`defines.h` already does several things right** for a PPC target: it
  defines `FORCE_INLINE`, handles the big-endian `U8TO16/U8TO32` macros with
  `__builtin_bswap*`, and the `Makefile` passes `-ffast-math -funroll-loops
  -fauto-inc-dec -finline-functions -O2`. Consider also trying `-O3` and/or
  `-flto` (link-time optimization) for a release build — LTO in particular can
  help across the many small accessor methods (`Gpu2D::readBgCnt`-style
  one-liners scattered through `memory.cpp`'s I/O switch tables) by inlining
  across translation units, which `-O2` alone cannot do.
- **`memory.cpp`'s `ioRead9`/`ioWrite9`/`ioRead7`/`ioWrite7` use large
  `switch` statements built from the `DEF_IO16/DEF_IO32/DEF_IO_8` macros.**
  These compile to jump tables for contiguous ranges, which is fine, but the
  *address-space layout* means a few very hot registers (DISPSTAT, VCOUNT,
  KEYINPUT, IPC sync/fifo, timers) are interspersed with hundreds of
  rarely-touched ones (every individual channel/sound register, every WiFi
  register). This is unlikely to be a real hotspot given jump-table dispatch,
  but if profiling shows otherwise, consider special-casing the handful of
  registers polled every scanline/every sample (DISPSTAT/VCOUNT/KEYINPUT/
  IPCSYNC — which already got a fast path inside the JIT via
  `NOODS_JIT_DISPSTAT_FAST`/`armLoadAddrOk`) ahead of the generic switch.
- **`Core::events` scheduler**: `SchedEvent` defines `operator<` and is stored
  in a `std::vector events` member sorted by `cycles` (per the comment next to
  `events[0].cycles` usage throughout the JIT and interpreter hot paths).
  Confirm `Core::schedule()` inserts in sorted order with something at worst
  O(log n) + O(n) memmove (`std::vector::insert` on a sorted vector) rather
  than doing a full `std::sort` after every push — with typically only a
  handful of pending events this is unlikely to matter, but it's worth a
  quick check since `schedule()` is called extremely frequently (every DMA,
  timer, IRQ, scanline, and sample tick).
- **Threading model**: `Gpu3DRenderer` spins up worker threads
  (`KThreadPrepare`/`KThreadResume`) and tears them down ("Clean up previous
  frame threads") every frame when 3D content needs rendering. Creating OS
  threads per frame is measurably expensive relative to a persistent
  thread-pool-with-semaphore design; see §4's last bullet.
- **General**: once §1/§2 land, re-profile before doing more rasterizer/GPU
  work — with a working JIT, the CPU-emulation bottleneck shrinks
  dramatically and the relative cost of GPU/video-presentation code goes up,
  which may reorder these priorities (e.g. §6's per-frame pixel shuffle could
  become the new top offender on 3D-heavy titles).

---

## Suggested order of work

1. Flip `JIT=1 BLOCKS=1` as the default build config; validate correctness on
   a broad ROM set; use the existing `ArmPpcJit::stats`/`dumpStats()` counters
   to find and fix the costliest remaining interpreter fallbacks. (§1)
2. Enable `NOODS_JIT_POLL_SKIP=1`; validate timing-sensitive titles. (§2)
3. Re-profile. Use the newly-freed CPU budget as the baseline for deciding how
   much further JIT work (§3) vs. GPU/presentation work (§4-§6) is worth it.
4. Address the 3D rasterizer division-by-constant hotspots (§4) and the
   GPU2D per-pixel memory-dispatch overhead (§5) — both are localized,
   low-risk changes.
5. Tackle frame presentation (§6) — higher effort/risk since it may require
   coordinated changes across `gpu_2d.cpp`/`gpu_3d_renderer.cpp` and
   `wii_video.cpp`, but offers a full-frame, every-frame win.
6. Revisit the misc items (§7) opportunistically (`-flto`, scheduler
   insertion complexity, persistent thread pool for the 3D renderer) as time
   allows.
