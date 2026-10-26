# Building Racer 3DS

The build has three stages:

1. **host tools** — compile N64Recomp and RSPRecomp for the build machine
   (`tools/3ds/setup.sh`);
2. **recompilation** — turn your own ROM into C/C++ under the gitignored
   `platform/3ds/generated/` (`tools/3ds/recompile.sh`);
3. **cross build** — devkitARM compiles everything for ARMv6K and makerom
   packages `Racer3DS.cia` (CMake/Ninja).

Nothing in stages 1–2 needs devkitPro. Nothing in the whole process touches the
network: every dependency is vendored in this tree.

---

## 0. Prerequisites

### Host (stages 1–2)

- C/C++ compiler, CMake 3.20+, Ninja (make also works), Python 3, git.
- Debian/Ubuntu: `sudo apt install build-essential cmake ninja-build python3 git`

### Target (stage 3)

A standard [devkitPro](https://devkitpro.org/wiki/Getting_Started) 3DS
installation:

```sh
# Debian/Ubuntu
wget https://apt.devkitpro.org/install-devkitpro-pacman
sudo bash install-devkitpro-pacman
sudo dkp-pacman -Sy 3ds-dev
source /etc/profile.d/devkit-env.sh     # exports DEVKITPRO / DEVKITARM
```

`3ds-dev` provides devkitARM (newlib for `arm-none-eabi`, including the
pthread/gloss glue), libctru, citro3d, and the `picasso`, `bin2s`,
`bannertool`, `smdhtool`, `3dsxtool` and `makerom` tools under
`$DEVKITPRO/tools/bin`. The build uses:

| Tool | Needed for | Required? |
|---|---|---|
| `arm-none-eabi-gcc/g++` | compiling the game, runtime and renderer | yes |
| libctru / citro3d | system and GPU libraries | yes |
| `picasso` | compiling `rt64_3ds.v.pica` into a shader object | yes |
| `bin2s` | embedding shader/data blobs | yes |
| `makerom` | building the `.cia` | yes for the CIA target |
| `bannertool` | building the SMDH (icon/metadata) the CIA embeds | yes for the CIA target |
| `3dsxtool` | building the optional `.3dsx` | optional — the target is skipped with a status message when absent |
| `smdhtool` | legacy SMDH fallback | optional — `bannertool makesmdh` is preferred |

The toolchain file reads `DEVKITPRO` (and `DEVKITARM`, defaulting to
`$DEVKITPRO/devkitARM`) from the environment or from `-D` cache variables.

## 1. Build the host recompilers

```sh
make setup                      # or: ./tools/3ds/setup.sh (--jobs N optional)
```

This configures and builds the vendored N64Recomp tree with a normal host
compiler and produces:

- `lib/N64ModernRuntime/N64Recomp/build/N64Recomp` — ROM → recompiled C;
- `lib/N64ModernRuntime/N64Recomp/build/RSPRecomp` — audio microcode → C++.

Re-runnable and ROM-free. The script only warns if devkitPro is missing; the
ARM build is the next stage.

## 2. Generate game code from your ROM

Bring your own legally-owned US ROM: `NEPE`, 32 MiB, big-endian `.z64`,
CRC1 `0x72F70398`. Nothing ROM-derived is committed or redistributed.

```sh
make recompile ROM=/path/to/your/baserom.z64
# equivalent: ./tools/3ds/recompile.sh --rom /path/to/your/baserom.z64
```

The script, in order:

1. validates the ROM (magic `0x80371240`, size 33,554,432 bytes, CRC1);
2. runs `N64Recomp` with `recomp/recomp.toml`
   → `platform/3ds/generated/RecompiledFuncs/*.c` (+ `funcs.h`, `lookup.cpp`);
3. `recomp/tools/fix_fallthroughs.py` — repairs split-function continuations;
4. `recomp/tools/post_recomp_patches.py` — inserts the `osPiStartDma_recomp`
   and `racer_get_count` redirects;
5. runs `RSPRecomp` with `recomp/rsp/aspMain.toml`
   → `generated/rsp/aspMain.cpp` (stock ABI 1 audio microcode);
6. `recomp/tools/gen_lookup_table.py`
   → `generated/section_table.cpp`.

All outputs land in `platform/3ds/generated/` (gitignored). Delete that folder
to force a full regeneration.

## 3. Cross-compile and package

The root `Makefile` wraps every command in this section (it picks Ninja when
available, otherwise the Unix Makefiles generator, and checks that
`DEVKITPRO` is set):

```sh
make                                  # configure + build + .cia and .3dsx
make cia                              # Racer3DS.cia only
make 3dsx                             # Racer3DS.3dsx only
make BUILD_DIR=out cia                # use a different build directory
make clean                            # delete the build tree
make distclean                        # also delete platform/3ds/generated/
make recompile ROM=/path/baserom.z64 # stages 2 and 3 in one go
```

The underlying CMake commands, which the Makefile runs for you:

```sh
cmake -S platform/3ds -B build-3ds -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/lib/rt64-3ds/cmake/devkitARM-3ds.cmake
cmake --build build-3ds -j
cmake --build build-3ds --target Racer3DS_cia
```

Outputs in `build-3ds/`:

| File | What it is |
|---|---|
| `Racer3DS` | the linked ARM ELF (hard-float, entry `0x100000`) |
| `Racer3DS.cia.smdh` | HOME Menu icon/metadata, made by `bannertool makesmdh` |
| `Racer3DS.cia` | installable CIA (NCCH content + SMDH meta), made by `makerom` |
| `Racer3DS.3dsx` (+ `.smdh`) | hbmenu build, only when `3dsxtool` is on `PATH` |
| `Racer3DS.rsf` | the RSF generated from `lib/rt64-3ds/cia/template.rsf` |

The CIA title is **Star Wars Racer 3DS**, product code `RACER-3DS`, UniqueId
`0xF9048`. Its exheader requests 64 MB SystemMode (Old 3DS) and 124 MB
SystemModeExt with the 804 MHz/L2-cache profile when run on a New 3DS.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `RACER_NULL_RENDERER=ON` | OFF | Parse display lists but draw nothing (headless logic/audio bring-up) |
| `RACER_GPR32=ON` | OFF | Compile with `RECOMP_GPR32` (one ARM instruction per N64 register op). Only legal when the generated code contains no 64-bit register instruction; the configure-time source audit fails and names the offending line if it is ineligible |

### Verifying the artefact

```sh
arm-none-eabi-readelf -h build-3ds/Racer3DS     # ARM, Version5 EABI, hard-float ABI
```

A successful makerom run ends with `Racer3DS.cia` containing an `NCCH`
partition and the embedded `SMDH` metadata; the file is deterministic for a
given input tree and ROM.

## 4. Install

1. Copy `build-3ds/Racer3DS.cia` to the SD card and install with FBI or
   GodMode9.
2. Put the ROM at `sdmc:/3ds/racer/baserom.z64`. The runtime XXH3-hashes it
   (`0x9B89D0F9EBC12D32`) and refuses a mismatch; a missing ROM shows the
   expected path instead of starting the game. The on-SD name is set by
   `GameEntry::rom_filename_override` in `platform/3ds/main_3ds.cpp`; without
   an override the runtime looks for `<game_id>.z64`.
3. Launch **Star Wars Racer 3DS** from the HOME Menu. Saves and
   `settings.ini` are written next to the ROM.

---

## Appendix A: building without dkp-pacman

The supported path is the pacman `3ds-dev` group above. The build has also
been verified against a hand-assembled prefix where the distro
(`gcc-arm-none-eabi` 14.x) compiler is combined with devkitPro's own newlib
fork. If you go that route, every one of the following is required; a plain
distro newlib is not enough because libctru depends on devkitPro's reentrant
and pthread extensions.

1. **devkitPro newlib, hard-float.** Build the
   [devkitPro/newlib](https://github.com/devkitPro/newlib) `devkitPro` branch
   for `arm-none-eabi` with
   `CFLAGS_FOR_TARGET="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfp -mtp=soft"`
   and `--disable-newlib-supplied-syscalls --enable-newlib-mb
   --disable-newlib-wide-orient --enable-newlib-register-fini
   --disable-multilib`. Soft-float libraries will not link against the VFP
   objects. The install provides `libc.a`, `libsysbase.a` (the libgloss glue
   with `__getreent`, the `_*_r` syscall stubs, `pthread_create`, `sleep`,
   `usleep`) and `sysbase.specs`.
2. **Link with `sysbase.specs`.** It appends
   `--start-group -lc -lsysbase --end-group` after the driver's own sequence,
   which is the only ordering in which the gloss glue resolves. Pass
   `-specs=<prefix>/arm-none-eabi/lib/sysbase.specs` from compiler wrapper
   scripts alongside the usual `-specs=3dsx.specs`.
3. **Threads and C11 headers.** Distro libstdc++ is configured
   single-threaded for bare metal: switch its multilib `bits/gthr-default.h`
   to `#include "gthr-posix.h"`, expose the timed pthread prototypes with
   `-D_POSIX_TIMEOUTS=200112L`, and restore the two libstdc++ feature macros
   the distro configure left unset: `-D_GLIBCXX_HAS_GTHREADS=1
   -D_GLIBCXX_HAVE_ALIGNED_ALLOC=1`.
4. **libctru, citro3d and the packaging tools**, built/installed into the same
   prefix: libctru (its assembly needs `-x assembler-with-cpp`, and the
   `bin2o` header rule must emit `$(*)_bin.h`), citro3d, plus `picasso`,
   `bannertool` and a `makerom` binary (for example the Project_CTR Ubuntu
   release).
5. Point `DEVKITPRO` at the prefix and use the same CMake commands as the
   standard flow.

## Appendix B: cleaning

```sh
rm -rf build-3ds                                              # cross build only
rm -rf platform/3ds/generated build/recomp-work               # ROM-derived outputs
rm -rf lib/N64ModernRuntime/N64Recomp/build                   # host recompilers
```

---

## Appendix C: what this build-only distribution leaves out

This repository is trimmed to the files the three-stage build actually
touches; every kept component retains its license file (header-only
components carry their SPDX terms in the header itself). The full upstream
trees remain available from their original repositories.

Removed because no target in `Makefile` / `setup.sh` builds them:

- N64Recomp's `RecompModTool`, `RecompModMerger`, `OfflineModRecomp` and
  `LiveRecomp`/`LiveRecompTest` executables and their `lib/sljit` dependency
  (the corresponding `add_executable` blocks are removed from
  `N64Recomp/CMakeLists.txt`, with an explanatory comment);
- test suites, documentation, examples and CI metadata from the vendored
  dependencies (rabbitizer, fmt, tomlplusplus, ELFIO, miniz, o1heap, xxHash),
  plus rabbitizer's Python/Rust bindings;
- rt64-3ds's developer `tools/` and `tests/` directories;
- all build trees, object files and the ROM-derived `platform/3ds/generated/`
  (regenerated by `make recompile`).

Nothing used by `make setup`, `make recompile`, `make`, `make cia` or
`make 3dsx` is removed: a clean clone builds both `Racer3DS.cia` and
`Racer3DS.3dsx` with no network access.
