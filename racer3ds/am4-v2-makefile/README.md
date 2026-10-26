# Racer 3DS — Star Wars Episode I: Racer for the Nintendo 3DS

A complete Old 3DS build target for the N64 static recompilation of
*Star Wars Episode I: Racer* (`sp00nznet/racer`). The original MIPS binary is
translated to C by [N64Recomp](https://github.com/N64Recomp/N64Recomp), the N64
hardware is emulated by N64ModernRuntime (`librecomp` + `ultramodern`), and the
RDP display lists are drawn by **rt64-3ds**, an RT64-equivalent renderer that
interprets F3DEX2 display lists and emits PICA200 command buffers through
libctru/citro3d.

The output is an installable **`Racer3DS.cia`** (and a `.3dsx` when `3dsxtool`
is available) for the **original/Old 3DS**.

## Bring your own ROM

The recompiled game code is generated from **your own, legally-owned** US ROM
(`NEPE`, 32 MiB, big-endian `.z64`, CRC1 `0x72F70398`). No ROM-derived data —
recompiled functions, microcode, textures or audio — is committed to this
repository or redistributed in any form. `platform/3ds/generated/` is
gitignored and is created on your machine by `tools/3ds/recompile.sh`.

At boot the runtime hashes the ROM with XXH3 and refuses any mismatch:

| Item | Value |
|---|---|
| ROM path on the SD card | `sdmc:/3ds/racer/baserom.z64` |
| Expected XXH3-64 hash | `0x9B89D0F9EBC12D32` |
| Saves / settings | the same folder: `saves/` subfolder and `settings.ini` |

## Repository layout

Everything the build needs is vendored in this repository. There are **no
`.patch` files and no network fetches at build time**: the formerly
patch-applied runtime and renderer are committed already-patched.

```
platform/3ds/            the game target
  main_3ds.cpp           main(): the full rt64-3ds GameDesc (renderer, audio,
                         RSP microcode selection, ROM hash gate, save type)
  recomp_api_3ds.cpp     host functions for the generated C (boot state, SI DMA,
                         osGetCount, section registration)
  racer_host_protos.h    force-included prototypes for the generated C
  CMakeLists.txt         ARM cross build + .3dsx / .cia packaging
  icon.png               HOME Menu icon (48x48)
  generated/             ROM-derived output of recompile.sh (gitignored)
lib/N64ModernRuntime/    vendored, fully 3DS-patched N64ModernRuntime fork,
                         including its N64Recomp submodule (GPLv3 / MIT)
lib/rt64-3ds/            vendored PICA200 renderer + 3DS platform runtime,
                         CMake modules, RSF template and packaging functions
recomp/                  ROM-independent recompiler config and fix-up tools
  recomp.toml            N64Recomp function/section config for Racer
  symbols.toml           the 906-function symbol map
  rsp/                   RSPRecomp config for the aspMain audio microcode
  tools/                 fix_fallthroughs.py, post_recomp_patches.py,
                         gen_lookup_table.py
Makefile                 convenience wrapper: setup / recompile / cia / 3dsx
tools/3ds/
  setup.sh               builds the two HOST recompilers from vendored source
  recompile.sh           validates the ROM and runs the full ROM -> C pipeline
docs/RT64-3DS-PORT.md    architecture and every porting decision
BUILDING.md              complete build instructions
NOTICE                   licenses and attribution
```

## Quick start

With the devkitPro environment sourced (`source /etc/profile.d/devkit-env.sh`):

```sh
# 1. One-time: build the host recompilers (N64Recomp, RSPRecomp)
make setup

# 2. Generate the game code from your ROM (output lands in platform/3ds/generated/)
make recompile ROM=/path/to/your/baserom.z64

# 3. Cross-compile and package -> build-3ds/Racer3DS.cia (+ Racer3DS.3dsx)
make
```

Useful targets: `make cia` (CIA only), `make 3dsx` (hbmenu build only),
`make clean` (remove the build tree), `make distclean` (also delete the
ROM-derived generated code), `make help`. The Makefile is a thin wrapper over
the CMake commands below, which do the same thing:

```sh
./tools/3ds/setup.sh
./tools/3ds/recompile.sh --rom /path/to/your/baserom.z64
cmake -S platform/3ds -B build-3ds -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/lib/rt64-3ds/cmake/devkitARM-3ds.cmake
cmake --build build-3ds
cmake --build build-3ds --target Racer3DS_cia
```

Full prerequisites and options (GPR32, null renderer, non-pacman toolchains)
are in [BUILDING.md](BUILDING.md).

## Installing and playing

1. Copy `build-3ds/Racer3DS.cia` to your SD card and install it with FBI
   (or GodMode9).
2. Place your ROM at `sdmc:/3ds/racer/baserom.z64`.
3. Launch **Star Wars Racer 3DS** from the HOME Menu.

If the ROM is missing or its hash does not match, the runtime shows the
expected path and hash instead of starting the game. Saves (16 Kbit EEPROM,
256 blocks) and `settings.ini` live next to the ROM; tunables such as audio
RSP/HLE, frame cap, stereo and button deadzones are editable there and in the
touch-screen settings menu.

The CIA exheader requests 64 MB SystemMode on Old 3DS (124 MB SystemModeExt
with the 804 MHz/L2 profile on New 3DS), title UniqueId `0xF9048`. The 32 MB
ROM is streamed from SD with a cache rather than held in memory, so it fits
alongside the runtime's 16 MB RDRAM model.

## Controls

Library default mapping, fully playable on an Old 3DS without a Circle Pad Pro:

| N64 | 3DS |
|---|---|
| A / B | A / B |
| Z | Y or ZL |
| R trigger | R or ZR |
| C-Up | X |
| C buttons | C-Stick (New 3DS) |
| Start, D-pad, stick | Start, D-pad, Circle Pad |

## Honest scope and limits

- **Target is the Old 3DS.** The two ARM11 MPCore cores run at 268 MHz with
  VFP but no NEON; the RSP vector unit is scalar-emulated on such a CPU. The
  game layout keeps the recompiled game on core 0 and the renderer/audio on
  core 1, with O3DS-specific GPU savings (16-bit eye depth, I/IA textures as
  LA8, leading fill-rectangle as eye clear). Frame-rate tuning is expected to
  be an on-device effort; every relevant knob is reachable from `settings.ini`
  without rebuilding.
- **Audio is HLE by default.** The stock ABI 1 `aspMain` microcode is
  interpreted on the CPU (the same interpreter the sibling 3DS ports use),
  which is the faster path on Old 3DS. `dbg_audio_rsp=1` in `settings.ini`
  switches to the recompiled microcode for A/B comparison.
- **No mods.** Racer has no RecompiledPatches and the native mod loader
  (`dlopen`) does not exist on 3DS, so the mod system is compiled out.
- **Build verification, not a hardware claim.** The full pipeline — host
  recompilers, ROM -> C generation, ARM cross-compile, link and makerom
  packaging — is verified to run cleanly end-to-end and produce a structured
  CIA. The first-hardware validation checklist is section 8 of
  [docs/RT64-3DS-PORT.md](docs/RT64-3DS-PORT.md).

## Licenses

See [NOTICE](NOTICE). In short: the new 3DS target code and rt64-3ds are MIT;
N64ModernRuntime is GPLv3; N64Recomp and the vendored third-party components
carry their own licenses in their source trees.
