# RT64-equivalent renderer for the 3DS — Racer port design

This document records how the playable 3DS port of Star Wars Episode I: Racer
is assembled on top of the [rt64-3ds](https://github.com/oneup03/rt64-3ds)
renderer and the [N64ModernRuntime](https://github.com/sp00nznet/N64ModernRuntime)
fork. It is the map of the code under `platform/3ds/`, `lib/rt64-3ds/` and
`lib/N64ModernRuntime/`. Both libraries are vendored with all 3DS changes
already applied — the repository contains no patch files and performs no
network fetches.

## 1. Why rt64-3ds, and what it replaces

The desktop Racer port draws through an `RT64Context` (frontend
`rt64_context.cpp`) built on the full RT64 hardware renderer. RT64 itself has
no 3DS backend and cannot run on the ARM11/PICA200, so that context is not
ported. Instead the game is expressed as an rt64-3ds **GameDesc**:

- rt64-3ds is an RT64-equivalent renderer that interprets the same F3DEX2
  display lists and emits PICA200 command buffers, behind the exact
  `ultramodern::renderer::RendererContext` interface N64ModernRuntime expects.
- The runtime (`recomp3ds::run`) owns console bring-up, ROM loading and
  validation, the two-core O3DS thread layout, VI scanout, audio output, HID,
  saves, and the settings menu. The game supplies configuration and a handful
  of host functions; everything else is shared library code.

The other candidate references were ruled out earlier: StarHaven and
mario-kart-64-3ds are decomp native ports with no RendererContext seam,
DK64-Recompiled is desktop-only, the Plume RT64 backends are D3D12/Vulkan/Metal,
and a from-scratch PICA RDP implementation is out of scope when an
F3DEX2-interpreting RendererContext already exists in rt64-3ds.

## 2. Repository layout

| Path | Role |
|---|---|
| `lib/rt64-3ds/` | Vendored renderer + platform runtime: PICA200 F3DEX2 interpreter, citro3d layer, CMake modules (`cmake/`), shader sources and the CIA RSF template (`cia/`) |
| `lib/N64ModernRuntime/` | Vendored sp00nznet fork with every 3DS runtime change already applied to `librecomp/` and `ultramodern/`; includes the N64Recomp submodule that builds the host tools |
| `platform/3ds/main_3ds.cpp` | `main()`, the complete `GameDesc`, RSP task selection |
| `platform/3ds/recomp_api_3ds.cpp` | Host functions: boot state, SI DMA, osGetCount, section registration |
| `platform/3ds/racer_host_protos.h` | Force-included prototypes for the generated C |
| `platform/3ds/CMakeLists.txt` | The ARM game build, `.3dsx` and `.cia` packaging |
| `platform/3ds/generated/` | ROM-derived output of recompile.sh (gitignored, never redistributed) |
| `recomp/` | ROM-independent recompiler configs (`recomp.toml`, `symbols.toml`, RSP config) and the Python fix-up tools |
| `tools/3ds/setup.sh` | Builds the host N64Recomp/RSPRecomp binaries from the vendored tree (no cloning, no patching) |
| `tools/3ds/recompile.sh` | Full ROM → generated C/C++ pipeline |

## 3. Build pipeline

```
baserom.z64 (US NEPE, CRC1 0x72F70398, user-supplied)
   │  tools/3ds/recompile.sh
   ├─ N64Recomp            → generated/RecompiledFuncs/*.c + lookup.cpp
   ├─ fix_fallthroughs.py  → split-function continuations repaired
   ├─ post_recomp_patches  → osPiStartDma_recomp, racer_get_count redirects
   ├─ RSPRecomp            → generated/rsp/aspMain.cpp
   └─ gen_lookup_table.py  → generated/section_table.cpp
                                              │
              devkitARM + libctru + citro3d  ▼
                       Racer3DS.elf → .3dsx / .cia
```

The two Python fix-up steps (3 and 4) are mandatory parts of the pipeline:
without them the game crashes in the controller parser and framebuffer
allocator (broken fallthroughs) and keeps its dead PI manager and
unredirected clock.

## 4. GameDesc decisions (`main_3ds.cpp`)

| Field | Value | Why |
|---|---|---|
| `game_id` / `sd_dir` | `swe1racer` / `racer` | Save/settings identity; the ROM lives at `sdmc:/3ds/racer/baserom.z64` (via `entry.rom_filename_override`), XXH3-gated to `0x9B89D0F9EBC12D32` |
| `save_type` | `Eep16k` | Game addresses 256 EEPROM blocks (saves at 0 and 0x400) |
| entrypoint | `recomp_entrypoint` @ `0x80000400` | Matches the entrypoint in `recomp/recomp.toml` |
| `on_init_callback` | `racer_on_game_init` | Reconstructs stubbed COP0 boot state (§5) |
| RSP | audio → `aspMain`, else `nullptr` | GFX tasks are routed to the renderer before the RSP callback; HLE swaps aspMain for the CPU interpreter |
| `render.gbi` | `Gbi::F3DEX2` | Racer's GSP ucode is F3DEX_GBI_2 (confirmed from the game's display-list setup) |
| `render.vi_width/height` | 320 × 240 | Standard N64 frame; width is also read live from `VI_WIDTH_REG` inside a task |
| `force_lod_branch` / `cull_dl` | true / true | Parity with the desktop's force-near LOD; honour `gSPCullDisplayList` |
| `frame_interpolation` | false | Racer has no gEXMatrixGroup transform tagging; in-betweens would be unsound |
| `eye_depth16`, `intensity_textures16`, `fill_clear` | true | O3DS GPU savings, picture-identical on the sibling ports |
| `audio_hle` / family | true / `Aspmain` | Stock libultra ABI 1 aspMain; the same interpreter Goemon uses. `dbg_audio_rsp=1` in settings.ini selects the recompiled microcode instead |
| `audio_async` / margin | true / 4 | SDK-style audio manager: report task completion immediately, keep four frames of reserve against DSP underruns |
| `two_cores_game_on_core0` | true | O3DS layout: game owns core 0, renderer/audio share core 1 at 80% |
| `rom_old3ds` / `rom_new3ds` | Stream / Auto | The 32 MB cartridge cannot sit beside the 16 MB RDRAM model in the O3DS application heap |
| `old3ds_frame_cap` | 0 | No interpolation: run at the game's own rate |
| button map | library default | A/B, Y or ZL = Z, R or ZR, X = C-Up, Start, D-pad, Circle Pad, C-Stick C buttons — fully playable on O3DS |

The CIA uses the library's `cia/template.rsf`: 64 MB SystemMode for Old 3DS,
124 MB SystemModeExt plus 804 MHz/L2 on New 3DS, 512 KB stack, core affinity
0/1, UniqueId `0xF9048`.

## 5. Host functions and reconstructed boot state (`recomp_api_3ds.cpp`)

The desktop `src/main.cpp` logic, minus Windows-only crash handling:

- **PI handle at `0x80800000`** (`recomp::cart_handle`): zeroed
  `OSPiHandle`, domain 0, base `0xB0000000`; its pointer is written to
  `0x800A7BC0` (__osCartRomHandle) and `0x800A7FC0` (__osPiTable), and the
  handle's next pointer self-links.
- **VI scheduler state**: sink message queue at `0x800A7E80` (buffer
  `0x800A7E60`, 8 messages), fake VI mode struct at `0x800A7F00` (retrace
  count 1, response queue = sink), pointer published at `0x800A7F50`.
- **`osMemSize` at `0x80000318` = 0x00800000** (Expansion Pak, 8 MB). Below the
  BSS clear range so it survives; the framebuffer table at `0x80114530` is
  deliberately left for the game's own allocator.
- **`__osSiRawStartDma_recomp`**: on a PIF→DRAM read, synthesizes the
  four-channel controller block (channel 0 standard pad, 1–3 absent) and calls
  `ultramodern::send_si_message()`. Writes need no response.
- **`racer_get_count`**: returns the runtime `osGetCount()` sign-extended into
  v0; the redirect is inserted by `post_recomp_patches.py`.
- **Sections**: one static section, empty overlay index table, registered
  through `recomp::overlays::register_overlays`.

## 6. Renderer contract: desktop RT64Context → rt64-3ds

The desktop context's per-task behavior is covered by library defaults plus
the RenderDesc above:

- ucode/ucode_data/data pointers are masked to physical RDRAM (`& 0x3FFFFFF`)
  inside the runtime before any display list work;
- `loadUCodeGBI(..., true)` precedes `processDisplayLists(rdram, dl, 0, true)`,
  matching the F3DEX2 load-then-process ordering;
- the screen update applies the stored config and then `updateScreen`;
- the VI width is taken from the live VI registers, never hard-coded past the
  RenderDesc default (Racer's frontend itself contains no fixed 320/240 frame
  assumption).

## 7. Memory map and known risks

- **RDRAM model**: 16 MB (`RECOMP_MEM_SIZE_MB` default), heap start
  `0x80A00000`. Racer has no patch RAM, so the defaults stand. If the game
  ever needs more guest RAM, raise `-DRECOMP_MEM_SIZE_MB` through the rt64-3ds
  CMake knob and re-check the O3DS heap budget in the boot log.
- **Raw MMIO pokes (`0xA4000000`)**: the desktop allocates a 640 MB buffer as
  a safety net for direct VI/AI/PI register access. On 3DS those accesses live
  in the libultra OS region, which is fully stubbed/reimplemented
  (`recomp.toml` stub list + the runtime's `*_recomp` family), so the raw
  instructions never execute. If hardware testing shows a surviving high
  address access, the fix is a targeted host reimplementation, not a larger
  RDRAM allocation.
- **RECOMP_GPR32**: off by default. It halves register-op cost but is only
  legal with no 64-bit register instructions; `RACER_GPR32=ON` runs the
  library's source audit and fails configure with the offending line.
- **Audio HLE compatibility**: the interpreter validates the ucode's DMEM
  constants on first use. A mismatch is logged and the task falls back to the
  recompiled microcode; if Racer's aspMain revision ever trips that, ship with
  `dbg_audio_rsp=1` and capture the task for the interpreter diff tool.

## 8. First-hardware validation checklist

1. Boot with no ROM: the library's "place ROM at sdmc:/3ds/racer/baserom.z64"
   screen must appear (proves CIA install/exheader).
2. Boot with a wrong-hash ROM: hash rejection screen.
3. Correct ROM: `racer_on_game_init` log line, attract demo, a race, and an
   EEPROM save round-trip (power cycle).
4. Check the boot/statistics log for the stall-watch counters and the audio
   path (HLE confirmed, no constant crackle; try the DSP/RSP toggle if so).
5. O3DS frame-rate observations drive any desc tuning (cache size, core
   layout, quality flags); every knob is reachable from settings.ini without a
   rebuild.
