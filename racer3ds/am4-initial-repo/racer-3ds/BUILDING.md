# Building the 3DS target

## 0. Toolchain

devkitPro's devkitARM with libctru, citro3d and citro2d:

```sh
# Debian/Ubuntu
wget https://apt.devkitpro.org/install-devkitpro-pacman
sudo bash install-devkitpro-pacman
sudo dkp-pacman -Sy 3ds-dev
source /etc/profile.d/devkit-env.sh
```

`make` builds the `.3dsx`; `make cia` additionally needs **makerom** (not shipped by
devkitPro). Either use the `makerom` binary from
[Project_CTR releases](https://github.com/3DSGuy/Project_CTR/releases) and pass its path:

```sh
make cia MAKEROM=/path/to/makerom
```

or let devkitPro's `pacman` fetch a prebuilt one from a community repo — the path in
`MAKEFILE`'s `MAKEROM ?= makerom` is a plain variable, so any location works.

## 1. The bring-up build (what is in `bin/`)

```sh
cd platform/3ds
make                 # racer3ds.3dsx, racer3ds.smdh
make cia             # racer3ds.cia      (80MB, O3DS extended memory)
make cia-64          # racer3ds-64.cia   (64MB, same binary, different title id)
```

Outputs land next to the Makefile. `make clean` removes them.

What gets compiled:
`source/{main,screen,bootlog,vi,rom,audio_ndsp,input_hid,status,renderer_pica}.cpp`.
No ROM-derived file is involved, which is why this build works without one.

## 2. The runtime cross-build

The N64 recompilation runtime has to compile for ARMv6K before a game build can link.
`tools/build-runtime.sh` copies `deps/N64ModernRuntime` and `deps/N64Recomp` to a temp
directory, applies `tools/patch_runtime.py` (8 small edits, listed in
`../runtime-patches/README.md`), and cross-compiles every source:

```sh
export R3DS_DEPS=/path/to/deps          # folder containing N64ModernRuntime/ and N64Recomp/
cd platform/3ds
./tools/build-runtime.sh                # -> tools/build-runtime/*.o
```

Expected result (verified with devkitARM 16.1.0 / libctru 2.7.0):

```
skipping mods.cpp (native mod loader, dlopen)
cross-compiling 43 runtime sources for ARMv6K, 2 at a time
compiled: 43   failed: 0
```

The 43 objects total ~0.5 MB of `.text` — comfortably inside a 3DS binary.

`mods.cpp` is excluded deliberately: it loads mods as native shared libraries via
`dlopen()`, which the 3DS does not have. A 3DS port links mods statically (or not at all).

## 3. Getting the dependencies

```sh
mkdir -p deps && cd deps
git clone --recurse-submodules https://github.com/sp00nznet/N64ModernRuntime
git clone https://github.com/N64Recomp/N64Recomp
cd N64Recomp && git submodule update --init        # rabbitizer, fmt, ELFIO, tomlplusplus
```

(The submodules matter: `librecomp` includes `miniz`, `xxHash`, `o1heap`, `concurrentqueue`
and `sse2neon` from `N64ModernRuntime/thirdparty`, and `recomp.cpp` includes headers that
live in `N64Recomp/lib`.)

## 4. The full build (`GAME=1`): adding the game itself

The recompiled game is **generated from your ROM** and cannot be distributed. Once you have
`baserom.z64` (USA, CRC1 `0x72F70398`):

```sh
# in the racer repository root
cp /path/to/your.z64 baserom.z64

# 1. recompiled game functions
N64Recomp recomp.toml                    # -> RecompiledFuncs/*.c
python3 tools/fix_fallthroughs.py        # as the project's README documents
python3 tools/post_recomp_patches.py
python3 tools/gen_lookup_table.py

# 2. recompiled audio microcode
RSPRecomp rsp/aspMain.toml               # -> rsp/aspMain.cpp

# 3. build the 3DS target with the game code
cd platform/3ds
make GAME=1
```

`GAME=1` adds `-DR3DS_WITH_GAME -DR3DS_LINK_RUNTIME` and the runtime include paths, so
`main.cpp` compiles the part that registers `ultramodern`'s callbacks and calls
`recomp::start()` with the recompiled entrypoint instead of the self-test, and
`renderer_pica.cpp` compiles the real `RendererContext` implementation.

**Status of that path.** It is written but untested end-to-end, because it needs a ROM. The
pieces are in place and independently verified:

* the runtime compiles for ARMv6K (step 2, 43/43);
* `r3ds::n64_to_offset()` implements the guest→physical address mapping the runtime expects,
  without the 4 GiB `mmap` reservation the PC build uses (patch #5 in
  `runtime-patches/README.md`);
* the `RendererContext` subclass (`PicaRenderer`) satisfies the interface
  (`send_dl`, `update_screen`, `update_config`, …) and does the VI scanout in
  `update_screen()`;
* `RecompiledFuncs/*.c` is plain C compiled from the recompiler's output; the 3DS
  Makefile needs `RecompiledFuncs` added to `SOURCES` once it exists (see the comment at the
  top of the Makefile).

What it will **not** do yet is draw RDP geometry — `send_dl()` walks the display list and
counts opcodes (`r3ds::dl_walk`), it does not rasterise it. That is the next milestone; see
`PORT-NOTES.md` §5.

## 5. Testing

* **Hardware**: copy `bin/racer3ds.3dsx` to `sdmc:/3ds/` and launch from hbmenu, or install
  `bin/racer3ds.cia` with FBI. The lower screen tells you which memory mode you got.
* `platform/3ds/thirdparty/libctru-ndsp/` holds libctru's `ndsp` with the DSP handshake
  bounds-checked (see `TROUBLESHOOTING.md`). It is compiled instead of libctru's `ndsp.o`
  automatically — nothing to configure; keep the two in sync if you update libctru.
* **Audio** is on by default; hold **R** while launching to boot without it, or write `off`
  into `sdmc:/3ds/racer/audio.cfg` to make that permanent. The DSP firmware
  (`sdmc:/3ds/dspfirm.cdc`, dumped from the same console) is validated before `ndspInit()`
  runs, because a wrong-region dump makes libctru's DSP handshake follow garbage. No extra
  library is needed (`-lctru` covers ndsp).
* **Memory profile**: the shipped `.cia` is 80 MB (extended memory mode). Hold **X** at
  launch to run the app with a 64 KiB command buffer and a 4 MB probe (the "as if 64 MB"
  profile); `bin/racer3ds-64.cia` / `make cia-64` is the real 64 MB title.
* **Emulator**: Citra/Lime3DS run both formats, but they do not emulate extended memory
  mode, so the `.cia` will report `REGULAR 64MB` there. The memory-mode split can only be
  checked on real hardware.
* **On a console, when something aborts**: read `sdmc:/3ds/racer/boot.log` — see
  `../TROUBLESHOOTING.md` for the expected sequence and what each stopping point means.
  Safe mode (hold **L** at launch) skips citro3d, citro2d, ndsp and the ROM read.
* **No console at all**: `tools/preview` builds a host program from the same `vi.cpp` and
  `status.cpp` that writes `docs/preview-screens.png`:

  ```sh
  cd platform/3ds
  c++ -O2 -std=c++17 -Iinclude -o /tmp/preview tools/preview/preview.cpp source/vi.cpp source/status.cpp
  /tmp/preview
  ```

## The recompilation pipeline (what runs where)

"Recompile the game" is three steps, and two of them have to happen on a PC — there is no C
compiler on a 3DS and the generated code is hundreds of megabytes of C:

| Step | Runs on | Command | Output |
|---|---|---|---|
| 1. identify + validate the ROM against `game-config/` | **console** | happens at every boot | `sdmc:/3ds/racer/recompile.txt`, plus a `RECOMPILE PLAN` line on the status screen |
| 2. ROM -> C (`N64Recomp`, `RSPRecomp`) | PC | `platform/3ds/tools/recompile.sh --rom <z64>` | `game/RecompiledFuncs/*.c` (906 functions), `game/gen/aspMain.cpp` |
| 3. C -> ARMv6K objects, then link the game binary | PC | same script, then `make -C platform/3ds game` | `game/build/*.o`, then a `.cia` with the game in it |

### Step 1: what the console does now

At every boot, after the ROM check, the app:

* reads the ROM header, checks the byte order (`z64`/`v64`/`n64`). A `v64` or `n64` rip is
  **converted to `sdmc:/3ds/racer/baserom.z64`** on the spot, the same thing a PC ROM swapper
  does, because the recompiler only accepts big-endian;
* compares CRC1/CRC2 with `game-config/recomp.toml`. A mismatch is reported on screen and in
  the report instead of turning into a cryptic recompiler error later;
* walks all **906** function entries from `game-config/symbols.toml` (compiled in as
  `include/recomp_plan.h`, 28 KB) and verifies each one lands inside the ROM's code section
  and starts with a plausible instruction — a progress bar, then:
  `RECOMPILE PLAN / plan: 906/906 OK`;
* writes `sdmc:/3ds/racer/recompile.txt` with the verdict and the exact PC command to run.

This is the same shape as the asset step in other N64 ports (a `.o2r` generated from the
ROM on the device): the console prepares and validates, the PC generates code.

### Step 2 and 3: the PC side

```sh
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
./platform/3ds/tools/recompile.sh --rom "/path/to/Star Wars Episode I - Racer (U) [!].z64"
```

The script clones/builds N64Recomp and N64ModernRuntime if `deps/` is empty, applies the
ARMv6K patches, recompiles, converts `aspMain`, and cross-compiles everything. It reads its
inputs from `game-config/` (see `game-config/README.md`) and writes into
`platform/3ds/game/`, which is gitignored because it is ROM-derived. Verified in
this tree: `N64Recomp` reports **906 functions**, 18 C files compile for ARMv6K with no
errors, and `aspMain.cpp` compiles too.

### The game build: what `make game` does now

```sh
./platform/3ds/tools/recompile.sh --rom baserom.z64     # ROM -> game/build/*.o
./platform/3ds/tools/build-runtime.sh                    # runtime -> runtime-build/*.o
cd platform/3ds && make game                             # -> racer3ds.3dsx (+ racer3ds-game.cia)
```

`make game` compiles `source-game/game.cpp` + `source-game/renderer_game.cpp`, links them with
the 900-odd recompiled functions (`game/build/*.o`), the section table
(`game/section_table.cpp`, generated by `tools/gen_section_table.py`) and the runtime objects
(`runtime-build/*.o`), and produces `racer3ds-game.cia` (title id `000400000F9046`, 80 MB) that
installs **next to** the bring-up build.

**Verified in this tree**: the link completes with no undefined symbols (906 recompiled
functions + 43 runtime objects + the platform layer), `.text` grows by the recompiled code,
and ctrtool reports the exheader as `dev2 (AppMemory: 80MB)`. The ROM used for that check was
a synthetic 32 MiB file, so the code in it is not Racer's — but the link, the section table,
the runtime registration and the packaging are all exercised.

`game.cpp` is the file that makes "install the .cia, put the ROM on the SD card, launch" work.
It registers the game (hash, entrypoint `0x80000400`, 906-function section table, EEPROM
16 kbit saves), points the runtime at `sdmc:/3ds/racer/baserom.z64`, and starts it with the
callbacks the runtime needs:

| Callback | What `game.cpp` does |
|---|---|
| `renderer_callbacks.create_render_context` | **stub renderer** (`renderer_game.cpp`): accepts display lists, counts them, presents the frame |
| `rsp_callbacks.get_rsp_microcode` | returns the recompiled `aspMain` for audio tasks, `nullptr` for graphics (the runtime routes those to the renderer) |
| `input_callbacks.*` | HID → N64 pad (`input_hid.cpp`), rumble is a no-op |
| `audio_callbacks.*` | the ndsp sink from `audio_ndsp.cpp` |
| `events_callbacks.*` | VI counter, "game started" flag |
| `threads/error_handling` | thread names for the log, runtime errors into `boot.log` |

### The one thing left between that and a picture: the renderer

`renderer_game.cpp` is a stub: the game runs — threads, timing, audio, input, ROM access — but
it does not draw, because nothing interprets the RDP command stream yet. The slot is a single
class implementing `ultramodern::renderer::RendererContext`, and
**[rt64-3ds](https://github.com/oneup03/rt64-3ds) already implements it** on citro3d (F3DEX
interpreter, CPU vertex transform, combiner planning, VRAM textures, stereo, frame
interpolation). Wiring it in means replacing `renderer_game.cpp` with a two-line factory that
returns `rt64_3ds`'s context, adding its 116 files to `source-game/`, and building them for
ARMv6K — the same kind of work as the runtime patches, not new architecture.

Until then, `make game` produces a build that boots, validates your ROM, starts the
recompiled game and reports `game: N VI M frames` on the lower screen. That is the difference
between "the .cia launches" and "the game runs"; the remaining gap is pixels.

### Memory budget for the game build

| Item | Size | Notes |
|---|---|---|
| RDRAM | 8 MiB | `memalign` instead of the desktop 4 GiB reservation |
| recompiled code | a few MiB | depends on the ROM; `.text` was +71 KiB for the degenerate synthetic ROM |
| runtime + platform | ~440 KiB | measured |
| audio buffers | 16 KiB | ndsp, 32 kHz stereo |

80 MB is comfortable; 64 MB is not (which is why the O3DS title is the 80 MB one, and why
Banjo's port streams its ROM instead of loading it).
