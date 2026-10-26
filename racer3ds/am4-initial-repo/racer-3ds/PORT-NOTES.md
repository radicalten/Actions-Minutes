# Is a 3DS build target possible? — analysis, and what this tree does about it

Short answer: **a 3DS build target for this repository is possible, and the memory-mode
split you described is real and is implemented here. A playable 3DS port of the game is a
much larger job that this tree starts but does not finish.** This document is specific
about which is which.

## 1. What the repository is

`sp00nznet/racer` is not an N64 emulator port and not source code for the game. It is a
*static recompilation*:

```
 N64 ROM  ──N64Recomp──►  RecompiledFuncs/*.c     (the game, as C, generated locally)
                │
                ├──────►  rsp/aspMain.cpp         (the RSP audio microcode, same deal)
                │
                └─ runtime: N64ModernRuntime
                              ├── librecomp   : RDRAM, PI/DMA, EEPROM, controllers, RSP
                              └── ultramodern : threads, events, timers, VI, audio queue
                                        │
                                    RT64 renderer (D3D12 / Vulkan / Metal, via Plume)
```

Consequences that decide the 3DS question:

* `RecompiledFuncs/` and `rsp/aspMain.cpp` **do not exist in the repository** (they are in
  `.gitignore`). They are produced from the user's ROM with N64Recomp/RSPRecomp. Any
  "compiled game" for the 3DS has to be built by whoever owns a ROM.
* The renderer is RT64. Its backends are Plume D3D12, Vulkan and Metal; it uses GPU compute,
  descriptor sets and shader pipelines. **There is no GL1/PICA200-class backend**, and
  adding one is equivalent to writing a new RDP rasteriser.
* Everything else (`librecomp`, `ultramodern`) is portable C++20: no Windows/Linux-only
  APIs in the code paths a game uses, `mmap`/`VirtualAlloc` only for the RDRAM reservation,
  POSIX semaphores for the audio queue.

The project's own README paints the same picture from the other side: the port needs
RT64's `loadUCodeGBI`/`processDisplayLists` to put pixels on screen, i.e. rendering is
assumed to be RT64's job.

## 2. The 3DS memory modes (what "high memory mode" buys you)

| Mode | Exheader value | Old 3DS application RAM | Notes |
|---|---|---|---|
| Prod (default) | `SystemMode: 64MB` | 64 MB | what hbmenu `.3dsx` homebrew gets |
| **Dev2** | **`SystemMode: 80MB`** | **80 MB** | "extended memory mode": the console reboots into it; browser/Miiverse are unloaded. Used by Smash 3DS and ORAS |
| Dev1 | `SystemMode: 96MB` | 96 MB | dev-unit-ish, available to CIAs but rough on a retail O3DS |
| New 3DS | `SystemModeExt: 124MB` | 124 MB (New 3DS only) | 178 MB additionally kills the HOME menu |

Facts that shape the two artefacts:

* The memory mode is an **exheader** property of an installed title (`.cia`). It cannot be
  requested by a `.3dsx`: hbmenu titles have no exheader of their own. So "`.3dsx` in
  regular memory" is the only option for `.3dsx`, and "`.cia` in 80 MB" is exactly what
  `SystemMode: 80MB` does. This tree implements precisely that pair.
* It is 80 MB on an **Old** 3DS. `96MB` is Dev1 and 124/178 MB are New 3DS values; asking
  an O3DS for more than 80 MB is not a thing retail units do.
* Extended memory mode on an O3DS means a full console reboot on entry and on exit. That is
  normal (Smash, Sun/Moon do the same) but it is worth knowing before testing.

Verified on the artefact in `bin/`:

```
$ ctrtool -t cia -n 0 --exheader=exh.bin bin/racer3ds.cia
$ ctrtool -t exheader exh.bin
System mode:            dev2 (AppMemory: 80MB)
System mode (New3DS):   ctr dev2 (AppMemory: 80MB)
```

## 3. What this tree implements

`platform/3ds` is a platform layer for the runtime plus a self-test application:

| Piece | File | State |
|---|---|---|
| Display: citro2d targets, 320×240 VI texture, 4:3 pillarbox, both screens | `source/screen.cpp` | done |
| VI scanout: RDRAM framebuffer → RGBA5551 surface, 16bpp and 32bpp modes | `source/vi.cpp` | done, used by `PicaRenderer::update_screen()` |
| Status screen (memory mode, ROM, paths, fps) | `source/status.cpp` | done, shared with the preview tool |
| ROM discovery + CRC/size validation on SD | `source/rom.cpp` | done |
| Audio: ndsp channel, 32 kHz stereo, wave-buffer ring | `source/audio_ndsp.cpp` | done (silent without recompiled `aspMain`) |
| Input: buttons + circle pad → N64 pad state | `source/input_hid.cpp` | done |
| Renderer bridge: `RendererContext` impl, display-list walker | `source/renderer_pica.cpp` | interface + walker done, **no rasteriser** |
| `.cia` exheader with 80 MB / Legacy | `cia/app.rsf` | done, verified with ctrtool |
| Build: `.3dsx`, `.cia`, `GAME=1` switch | `Makefile` | done |
| Runtime patches for ARMv6K | `tools/patch_runtime.py`, `runtime-patches/` | done, 43/43 sources compile |

## Audio (ndsp): the handshake is bounded, because libctru's is not

`ndspInit()` is the one libctru call in this port that can take the whole process down on bad
input: it ignores the `is_loaded` out-parameter of `DSP_LoadComponent()`, waits for the DSP
interrupt with `U64_MAX`, and then reads a variable count from DSP pipe 2 and `count*2` bytes
into a 16-entry stack array **without checking the count**. Three crash dumps from the test
console land in that sequence.

The fix is not a probe in front of it - an earlier build tried that, and the dump that
followed showed the probe's own load succeeding while libctru's *second* load of the same
component crashed, because a DSP component is not re-entrant across an unload/reload in one
boot. Instead, `platform/3ds/thirdparty/libctru-ndsp/` is libctru's `ndsp` with the handshake
bounded: `is_loaded` required, 400 ms IRQ timeout, `1 <= count <= 16`, every converted pointer
validated with `svcQueryMemory` (the handshake's first write, `ndspSetCounter`, goes through
one of them), and no `svcBreak` on a failed wait. One load per boot, done safely. Audio stays
on by default with a one-key opt-out (**R**, or `off` in `audio.cfg`).

## 3b. What Banjo: Recompiled's 3DS port changes about this plan

oneup03's [BanjoRecomp3D](https://github.com/oneup03/BanjoRecomp3D) is a shipped N64 static
recompilation running on the same hardware, and it settles the two questions this tree leaves
open — the renderer and the audio — so the roadmap below should follow it rather than replace
it.

* **Renderer: do not write a PICA200 RDP.** [rt64-3ds](https://github.com/oneup03/rt64-3ds)
  already implements `ultramodern::renderer::RendererContext` on citro3d: an F3DEX/F3DEX2
  display-list **interpreter**, CPU vertex transform and lighting, texture-combiner planning,
  TMEM decoding, textures in VRAM with the game's own mipmaps, per-eye replay for stereoscopic
  3D, and frame interpolation on the spare cores. It is a library a game links, not a fork of
  the runtime. Racer hands the same kind of display lists to it; `source/vi.cpp` is exactly the
  seam it attaches to.
* **Audio: interpret the microcode on the CPU, use ndsp only as the sink.** rt64-3ds's
  `recomp_3ds` ships CPU interpreters of `naudio` and `aspMain`. Racer's task file is
  `rsp/aspMain.toml`, so the same approach applies — and it removes the DSP firmware from the
  critical path except for the final ring of samples.
* **Old 3DS: 64 MB and a ROM streamed from the SD card, not extended memory mode.** The port
  ships a 64 MB CIA on Old 3DS and reads the ROM on demand, because 64 MB cannot hold the ROM
  beside the game. That is the opposite of this tree's default (80 MB ext-mem CIA) and worth
  copying: `bin/racer3ds-64.cia` exists, and a streaming reader would make it the primary
  build.
* **Mods are baked into the executable**, because the console cannot load mod code at runtime
  (`dlopen` does not exist) — the same reason `mods.cpp` is excluded here.
* Performance levers that actually worked for them: keep the game's own culling and low-detail
  models, thin particles per draw call, interpolate frames instead of rendering more of them,
  and classify draws so the second eye replays the first eye's command stream.
* Their 3DS README also documents the `sdmc:/3ds/dspfirm.cdc` requirement (Luma Rosalina >
  Miscellaneous > Dump DSP firmware), which is exactly what this tree checks for.

## 3c. Where the "install and play" chain stands

The recompilation is now a real pipeline in this tree (see `BUILDING.md`): the console
validates and converts the ROM at boot, `tools/recompile.sh` turns it into ARM objects on a
PC, and `make game` links them with the runtime through `source-game/game.cpp` — verified to
link with no undefined symbols and to package as `racer3ds-game.cia`.

Two pieces remain, in order of importance:

1. **A renderer.** `source-game/renderer_game.cpp` is a stub implementing
   `ultramodern::renderer::RendererContext`. [rt64-3ds](https://github.com/oneup03/rt64-3ds)
   implements the same interface on citro3d; swapping it in is file-level work, not design.
2. **Audio production.** The stream reaches ndsp, but the samples come from the recompiled
   `aspMain` microcode, which needs the RSP task path exercised end to end on the console
   (rt64-3ds's `aspmain_hle` is the alternative: interpret the microcode on the CPU).

## 4. What is missing, concretely

### 4.1 The renderer (the big one)

RT64 cannot run on a 3DS: D3D12/Vulkan/Metal do not exist there, PICA200 has no compute
shaders, and VRAM is 6 MB. A 3DS port needs an **RDP interpreter** that turns the game's
F3DEX2 display lists into citro3d draws:

* `G_SETCOMBINE` (the RDP colour combiner) → PICA200 TEV stages. Doable, but the exact
  combiner-to-TEV mapping is the hardest known part of such a port (it is what
  DaedalusX64 etc. spend most of their effort on).
* `G_SETTIMG`/`G_SETTILE`/`G_LOADBLOCK`/`G_LOADTILE`/`G_SETTEX` → PICA200 textures (ETC1/
  RGBA5551/A4, tilings 4×4/8×8/16×16), including the N64's CI4/CI8 paletted formats.
* `G_TRI1`/`G_TRI2`/`G_QUAD`, z-buffer, fill mode, AA → straightforward citro3d work.
* RSP transform (`G_MTX`, `G_VTX` with lighting/fog) → this is the *other* half of the RSP
  microcode; the runtime only recompiles `aspMain` (audio), so the graphics microcode has to
  be reimplemented on the CPU (or, for F3DEX2, largely folded into the RDP interpreter).
  HarvestMoon64Recomp's `n_aspMain` workflow documented in the repo README is audio-only;
  graphics ucode is a separate job.

`source/renderer_pica.cpp` has the seam: `dl_walk()` already counts opcodes so you can see
a real display list's composition before writing the interpreter, and `PicaRenderer` is a
drop-in `RendererContext`.

### 4.2 Performance

| | 3DS (Old/New) | what the recompiled game expects |
|---|---|---|
| CPU | 2× ARM11 268 MHz, VFP, **no NEON** | x86-64/ARM64, out-of-order, ~1 GHz+ |
| SIMD for the RSP VU | none (falls back to `Accuracy::RSP::SISD`) | SSE4.1 / NEON 128-bit |
| GPU | PICA200, fixed-function, 6 MB VRAM, no compute | D3D12/Vulkan class with shaders |
| RAM | 64 / 80 MB app (max 124 on N3DS) | 640 MB RDRAM image in the PC build |

`librecomp/rsp_vu.hpp` compiles a scalar fallback when neither SSE4.1 nor NEON exists, so
the RSP work *runs*, just very slowly; and the GPU-side RDP work moves to the CPU. Expect
this to be the difference between "runs" and "playable": the honest answer is that this
project runs a *static recompilation of a 1999 N64 game* and the 3DS is roughly an order of
magnitude short of what the PC design assumes, mostly in the SIMD and GPU dimensions.

### 4.3 Runtime details a real game build will hit

The 8 patches in `runtime-patches/` are what it takes to *compile* the runtime for ARMv6K.
Running the game adds:

* **RDRAM is 640 MB** in this project (`RECOMP_MEM_SIZE_MB=640`, needed because the game
  pokes VI/AI/PI registers at `0xA4000000`). The heap never goes past 512 MB (`kseg0`).
  An 80 MB budget means deciding what is really touched: N64 RDRAM is 4–8 MB, the rest is
  scratch the game uses for its MMIO pokes. A 3DS build wants `RECOMP_MEM_SIZE_MB` smaller
  (64–80 MB range) and the address mapping to be explicit — see `r3ds::n64_to_offset()`.
* **Guest addresses on a 32-bit host.** The recompiled code does 64-bit address arithmetic
  and the PC runtime subtracts `0xFFFFFFFF80000000` to get a byte offset into a 4 GiB
  reservation. On the 3DS there is no 4 GiB reservation (patch #5 replaces `mmap` with one
  committed allocation); the offset arithmetic still yields 29-bit physical offsets, which
  is why `r3ds::n64_to_offset()` exists as the documented mapping.
* **Threads.** `ultramodern` spawns host threads for VI, audio, events and each game thread.
  libctru's pthreads work, but a 3DS core is 268 MHz; the runtime's `sleep`/`yield` model
  will need tuning (the project already writes about `pause_self`/`run_next_thread_and_wait`
  fixes on the PC side).
* **Mods.** `mods.cpp` is a `dlopen()` shared-library loader — excluded from the
  cross-build. Static mods only, or none.

## 5. Suggested roadmap (if you want to take it further)

1. **Get the game booting without rendering** (`GAME=1`, `send_dl` = no-op, VI scanout
   only). This tells you whether the CPU side of the game runs at all on ARM11, and prints
   its own framebuffer (the game writes some UI into RDRAM). Smallest step that gives
   information.
2. **F3DEX2 → citro3d** for the simple cases: `G_VTX`/`G_MTX` on the CPU, `G_TRI1` +
   `G_SETCOMBINE`-less textured triangles, then combiner/TEV, then paletted textures.
   `dl_walk()`'s opcode histogram tells you which commands the game actually uses.
3. Move RSP graphics microcode to the CPU where the interpreter can't fold it in.
4. Only then worry about the frame rate (lower internal resolution, 30 Hz, skip-frame
   options) and the 80 MB budget (drop the 640 MB scratch, shrink the RSP/audio buffers).

## 6. Licensing / legal

The platform layer in `platform/3ds` is new code for this target (MIT, same spirit as the
repo's other tooling). The 8 runtime patches are small, additive edits to
N64ModernRuntime (`COPYING` in that repo) and N64Recomp (MIT); apply them with
`tools/patch_runtime.py`, or take them upstream. The bitmap font in
`platform/3ds/include/font8x8.h` is rasterised from DejaVu Sans Mono (Bitstream Vera
licence, permissive) — see `NOTICE`. No ROM-derived data is included anywhere in this
folder; the `.3dsx`/`.cia` in `bin/` contain no game code, and the strings on screen
(`STAR WARS EP.I RACER`, `RACER 3DS`, …) are plain text, not extracted assets.
