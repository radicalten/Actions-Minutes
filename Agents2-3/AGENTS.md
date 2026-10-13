# AGENTS.md — NooDS-Wii handoff for a fresh repo-linked session

Read this as a standalone handoff attached to the chat. The user will also provide a link to the GitHub repository `radicalten/Actions-Minutes2` (`https://github.com/radicalten/Actions-Minutes2`). This file is **not assumed to be inside the repository**. In a checkout, the emulator project is `NooDS-Wii/` relative to the Git repo root, and its C++ sources are `NooDS-Wii/NooDS-Wii/`. Locate the actual checkout; do not reuse paths from a previous chat. The user says they will upload the new source files into that nested C++ source directory. They may also attach `NooDS-Wii-session-handoff.zip`: it contains a compact snapshot under `Actions-Minutes2/NooDS-Wii/` with sources, scripts, two small ROMs, selected historical evidence, and DOLs. Compare it with the actual Git checkout rather than blindly overwriting newer files. If only AGENTS.md and a repo link arrive, check what exists in the checkout; do not assume the ZIP's contents are there.

## User goals and communication

- Continue improving Wii emulation performance, especially `.nds` games, **without trading away guest correctness or breaking `.gba` support or the ROM browser**. `rockwrestler.nds` has been the reproducible test ROM, not a reliable stand-in for every game's animated gameplay.
- The user uploads changes themselves. **Do not commit or push** unless explicitly asked. Keep progress updates short while working. Leave a usable build or clear source changes for them.
- The user said an in-game engine counter around 16 FPS appears in both Dolphin and on a real Wii. That is distinct from the emulator's Display/Emu FPS overlay.
- The preceding browser JIT build was reported at **Display 4 / Emu 11 FPS on both Wii and Dolphin**. After the most recent KEYINPUT build, the user said performance was **unchanged**, but wrote “Emu 4fps display 11fps,” reversing those labels. Do **not** infer a real-world speedup or slowdown from that wording; confirm which overlay number belongs to which counter if it matters. In all cases, the user reports **no perceptible FPS improvement from the KEYINPUT change**.

## Project map and build

- GitHub repo: `radicalten/Actions-Minutes2`. Previous checkout started at commit `c79fd20`; uploaded changes may now be at a different HEAD. Inspect current state and `git status` before editing.
- Project root, relative to repo root: `NooDS-Wii/` (contains `Makefile`, possibly `tools/`, `jit.dol`, `nds/`, `gba/`, and prior handoff `.md` files). Source folder: `NooDS-Wii/NooDS-Wii/` (not a second separate repo).
- Important sources: `main.cpp` (browser, overlay, harness), `arm_jit.cpp/.h` (PPC JIT, block classifier and emitter), `jit_bridge.S`, `interpreter.cpp/.h` (ARM scheduling/dispatch), `memory.h/.cpp` (mapped bus/MMIO), `gpu.cpp/.h`, `input.h`, `spu.cpp`, `core.cpp/.h`.
- Standard `Makefile` variants: `make -j2 JIT=0 BLOCKS=0 CONFIG_TAG=jit0_blocks0`, `JIT=1 BLOCKS=0`, `JIT=1 BLOCKS=1`. Set `DEVKITPRO`, `DEVKITPPC`, and `PATH` for devkitPPC if available; inspect local environment first. `Makefile` defaults to `-O2`. The generic `NooDS-Wii.dol` gets overwritten by the last variant built; use named DOLs.
- If present, run `bash tools/build_browser_jit.sh` from `NooDS-Wii/` to rebuild **browser-enabled** `jit.dol` in a separate `build_jit_browser/` directory (`JIT=1 BLOCKS=1`, `NOODS_FORCE_BROWSER=1`). It opens `sd:/` and ignores autoboot/test fallback. Standard regression DOLs intentionally allow autoboot. If the script is missing but the Makefile and source are present, the equivalent command is `CXXFLAGS='-DNOODS_FORCE_BROWSER=1' make -j2 JIT=1 BLOCKS=1 CONFIG_TAG=jit_browser`, then `cp NooDS-Wii_jit_browser.dol jit.dol`; verify the browser startup rather than assuming it works.
- The last workspace `jit.dol` was SHA-256 `70ea0748bbb03435bcba63280ac47f07e54588b31d5995d08cfe85cb9eccf3b2`, 1,359,680 bytes. This identifies the **historical artifact**, not proof that the new checkout contains it. Its browser smoke reached `FILE_BROWSER active dir=sd:/` even with an autoboot file. Manual controller-based ROM selection and post-upload on-Wii behavior still require verification.

## Practical Linux setup (run inside the actual checkout)

These are instructions, **not claims that a fresh environment has been provisioned**. First locate the project, for example `cd /path/to/Actions-Minutes2/NooDS-Wii`, then inspect `pwd`, `ls tools nds gba`, `git status` (if in a Git checkout) and `evidence/toolchain_bootstrap_result.txt` if present. The ZIP snapshot has no `.git` history. The historical sandbox had devkitPro under `/opt/devkitpro`, but that installation is not part of the upload. This is a **Wii PowerPC** build: devkitPPC, Wii `wii_rules`, libogc, libfat and `elf2dol` are needed; **devkitARM is not required** to compile the DS-guest emulator.

- Linux host utilities: `make`, host `g++`, Python 3, `curl`, `tar`, `unzstd` (package `zstd`), `sha256sum`; for test images, `dd`, `mkfs.vfat` (`dosfstools`), `mcopy`/`mmd`/`mdir` (`mtools`), `flock`, `realpath`, and `dolphin-emu-nogui`. On Debian-like distributions, host packages typically include `build-essential python3 curl zstd mtools dosfstools util-linux coreutils dolphin-emu`; package names and Dolphin availability vary. A GUI-only Dolphin install will not satisfy `tools/run_dolphin.sh`. Headless Dolphin must support the script's `-p headless`, `-v Null`, `-u`, `-C`, and `-e` options.
- Prefer an existing supported devkitPro Wii installation. Otherwise, **if `tools/bootstrap_toolchain.sh` is supplied and this is Linux x86_64**, a fresh unprivileged prefix can be installed with `export DEVKITPRO="$HOME/devkitpro"; bash tools/bootstrap_toolchain.sh` from the project root. That script fetches pinned May-2026 devkitPPC GCC/binutils, newlib, Wii rules/crtls, libogc, libfat and `elf2dol` packages from its stated community mirror, verifies their hardcoded SHA-256 hashes, extracts them, compiles a tiny Wii smoke program and writes `evidence/toolchain_bootstrap_result.txt` (`STATUS=PASS` on success). It needs `curl`, `tar` with `--use-compress-program`, and `unzstd`; do **not** bypass a failed checksum or assume a mirror will always stay up. On another OS, or if that mirror is unavailable, use devkitPro's official installation instructions to obtain equivalent Wii components instead. Avoid running the bootstrap into an unwritable `/opt/devkitpro` path.
- After either route, in **each new shell**: 

  ```bash
  export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"  # use the same prefix as the installation
  export DEVKITPPC="$DEVKITPRO/devkitPPC"
  export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:/usr/sbin:/usr/games:$PATH"
  test -f "$DEVKITPPC/wii_rules" || { echo 'Missing Wii build rules'; exit 1; }
  command -v powerpc-eabi-g++ elf2dol dolphin-emu-nogui mkfs.vfat mcopy mmd mdir flock python3 g++
  powerpc-eabi-g++ --version
  ```

  Check that `wii_rules` exists and each `command -v` succeeds. If Dolphin is unavailable but the cross-compiler works, the build and host-side oracle can still run; mark Dolphin checks NOT_RUN. `Makefile` links Wii libogc libraries `asnd`, `wiikeyboard`, `fat`, `wiiuse`, `bte`, `ogc`, and `m`. If build errors mention `wii_rules`, Wii headers or missing libraries, fix the toolchain/prefix rather than changing emulator source.

- Rebuild regression binaries **from the current source** (the ZIP's DOLs are snapshots, not proof of a subsequent edit):

  ```bash
  make -j2 JIT=0 BLOCKS=0 CONFIG_TAG=jit0_blocks0
  make -j2 JIT=1 BLOCKS=1 CONFIG_TAG=jit1_blocks1
  bash tools/build_browser_jit.sh
  sha256sum jit.dol
  ```

  The first two produce `NooDS-Wii_jit0_blocks0.dol` (interpreter baseline) and `NooDS-Wii_jit1_blocks1.dol` (autoboot-capable block JIT); the last builds the **browser-only** `jit.dol` in a separate object directory. `Makefile` appends required flags to `CXXFLAGS`, and each variant needs its own `CONFIG_TAG` to avoid mixing objects. **Do not use `make clean-all` casually:** it deletes named regression DOLs as well as build products. The browser build intentionally ignores test autoboot files, so do not use `jit.dol` for automated G2/G3 autoboot comparisons.

## Practical ROM tests and reading the results

The handoff snapshot includes `nds/rockwrestler.nds` (SHA-256 `f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a`) and `gba/suite.gba` (SHA-256 `63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495`). `tools/build_sd_image.sh` checks these exact hashes, makes a **128 MiB FAT16** `sd.img`, and copies the ROMs into several expected `sd:/` paths; it refuses to overwrite an existing image. If the files are absent in a fresh checkout, retrieve them from the user's upload rather than substituting arbitrary ROMs. No commercial BIOS files are in the handoff; the homebrew test ROMs ran in the prior Dolphin harness without such files. Normal game use may need the user's legally obtained original-DS BIOS/firmware (`sd:/noods/bios/bios9.bin`, `bios7.bin`, `firmware.bin`, and GBA `gba_bios.bin`; see project `README.md`).

From `NooDS-Wii/`, after building the two **autoboot-capable** DOLs above:

```bash
bash tools/check_g1_encoder.sh
python3 tools/test_branch_arm.py
bash tools/run_g2_case.sh NooDS-Wii_jit0_blocks0.dol fresh_rw_interp sd:/nds/rockwrestler.nds 15,30,45 45 90
bash tools/run_g2_case.sh NooDS-Wii_jit1_blocks1.dol fresh_rw_jit sd:/nds/rockwrestler.nds 15,30,45 45 90
python3 tools/compare_runs.py --mode checkpoints --ref evidence/g2_fresh_rw_interp --cand evidence/g2_fresh_rw_jit --out evidence/fresh_rw_compare.json
# Force JIT code-pool wrapping (and verify JIT_STATS poolWraps > 0):
bash tools/run_g2_case.sh NooDS-Wii_jit1_blocks1.dol fresh_rw_wrap sd:/nds/rockwrestler.nds 15,30,45 45 90 8192
python3 tools/compare_runs.py --mode checkpoints --ref evidence/g2_fresh_rw_interp --cand evidence/g2_fresh_rw_wrap --out evidence/fresh_rw_wrap_compare.json
```

`tools/run_g2_case.sh` creates `evidence/g2_<LABEL>/` and **deletes/replaces that label's directory**: choose fresh labels, never historical evidence names. It writes an SD autoboot config, runs isolated headless Dolphin, extracts `g2_state.log`, `mem_dump.bin` and `fb_dump.bin`, and removes its temporary 128 MiB image. A comparable GBA checkpoint test uses `sd:/gba/suite.gba`, checkpoints `50,60,120`, `MAX_FRAMES=120`, e.g. run the same two G2 commands with new labels and `... 50,60,120 120 180` (append `8192` on the wrap candidate). Run `compare_runs.py --mode checkpoints` against **both freshly produced complete run directories**; archived comparison JSON and checkpoint logs alone cannot be re-compared because the handoff intentionally omits the old binary dumps. Require `STATUS=PASS`, `SENTINEL_HIT=1`, same guest state/hashes, and zero mismatches. `RC=143` **after** a detected sentinel is the runner terminating Dolphin normally; an absent sentinel or timeout is not a pass.

For the full GBA suite (`G3`, potentially longer than G2), run:

```bash
bash tools/run_suite_gba.sh NooDS-Wii_jit0_blocks0.dol fresh_interp evidence/fresh_g3 180
bash tools/run_suite_gba.sh NooDS-Wii_jit1_blocks1.dol fresh_jit evidence/fresh_g3 180
python3 tools/compare_runs.py --mode suites --ref evidence/fresh_g3/mgba_summary_fresh_interp.json --cand evidence/fresh_g3/mgba_summary_fresh_jit.json --out evidence/fresh_g3/compare.json
```

Check `ALL DONE`, 13 suites, one intentional skip, byte-identical output and the suite comparator's zero differences. `STATUS=PASS` means the log is structurally complete, **not** that every test assertion in the suite passed. Run interactive rockwrestler only with **separate** builds compiled with `NOODS_G2_SCRIPTED_INPUT=1` (e.g. `CXXFLAGS=-DNOODS_G2_SCRIPTED_INPUT=1 make -j2 JIT=1 BLOCKS=1 CONFIG_TAG=scripted_jit`); then the four last `run_g2_case.sh` arguments, after `POOL_LIMIT_WORDS`, may be `60 65 135 140` for two A-press windows. Never mix scripted objects with production/browser objects. The resulting menu/result/B-wait timing is not gameplay FPS.

To smoke-test browser startup without selecting a ROM, build a separate SD image **with** an autoboot file and confirm the browser ignores it:

```bash
bash tools/build_sd_image.sh evidence/fresh_browser_sd.img --autoboot sd:/nds/rockwrestler.nds
bash tools/run_dolphin.sh jit.dol evidence/fresh_browser_sd.img 30 evidence/fresh_browser_smoke 'FILE_BROWSER active dir=sd:/'
# Inspect evidence/fresh_browser_smoke/result.txt: STATUS=PASS, SENTINEL_HIT=1.
# Also confirm dolphin.log has no '[NOODS] AUTOBOOT path=' line.
rm -f evidence/fresh_browser_sd.img
```

This is **only** a startup smoke. For manual Wii use, place the browser DOL as `sd:/apps/<your-app>/boot.dol` and ROMs on a FAT SD card; browse from `sd:/` with A to open/select and B to go up. Verify controller selection, actual gameplay, overlays and Wii speed separately. Scripts use Linux paths and headless Dolphin; on another host port them deliberately rather than claiming they ran. Keep generated SD images and proprietary BIOS/firmware out of uploads.

## What changed and why

- DS frameskip defaults to **2** (one image displayed per roughly three emulated frames); GBA default skip is **0**. Display FPS counts presented images; Emu FPS counts completed guest video frames. Changing frameskip alone is **not** an Emu FPS improvement and can increase rendering work.
- Bus work aligned mapped storage and added guarded PPC byte-reversed mapped reads/writes with unaligned/MMIO fallbacks (`memory.h`). Preserve bus behavior, endianness, unaligned reads, and code invalidations. Prior JIT/branch improvements and the optional FPS diagnostics are described in the project's handoff docs if uploaded.
- ARM9 `LDRH` blocks frequently poll DISPSTAT (`0x04000004`); guarded live-field read in `arm_jit.cpp` is on by default (`NOODS_JIT_DISPSTAT_FAST=1`). ARM9 KEYINPUT (`0x04000130`) now has a similarly guarded live-field read (`NOODS_JIT_KEYINPUT_FAST=1`). They retain the original mapped-memory/interpreter path for other addresses; ARM7 remains unchanged. Generic native halfword loads are separately opt-in and default off (`NOODS_JIT_NATIVE_HALF_LOADS=0`). The block reservation is 4,096 words; code-pool-wrap testing uses at least 8,192 words.
- `NOODS_G2_SCRIPTED_INPUT=1` is an **opt-in test build only**, not enabled in the browser DOL. When available, `tools/run_g2_case.sh` accepts optional `INPUT_A_FROM`, `INPUT_A_TO`, `INPUT_A2_FROM`, `INPUT_A2_TO` as its last four arguments after `POOL_LIMIT_WORDS`. Pressing A over completed frames 60–65 and 135–140 moves the small rockwrestler ROM from menu → ARMv4 CONDITION CODES test → “OK” screen waiting for B. No-input runs stayed on the same menu image through frame 300; even the scripted test is not representative animated-gameplay footage.

## Measured results and their limits

- On the **Dolphin test harness**, before the DISPSTAT path the no-input rockwrestler run was ~101.9 ms/emulated frame; afterward ~88.0 ms/frame. The user subsequently observed Display 4 / Emu 11 with that earlier build on Wii and Dolphin.
- With the two scripted A presses, old (DISPSTAT-only) JIT was **109.116 ms/frame cumulative** at frame 300 and the new DISPSTAT+KEYINPUT JIT was **89.662 ms/frame**. Both matched the interpreter at frames 45/120/180/300. The later interval spends time waiting for B on a KEYINPUT poll. **The user did not observe a real-world FPS change with the new build**. Do not present the harness improvement as a game-wide or on-Wii FPS gain.
- Rockwrestler idle and interactive G2 normal/wrap checks, GBA `suite.gba` normal/wrap checks, and single-instruction JIT comparison had **zero guest CPU/memory/framebuffer mismatches** against the interpreter in prior tests. Encoder oracle 52/52 and branch helper passed; browser startup smoke passed.
- GBA suite output matched the interpreter byte-for-byte (SHA-256 `97b08848fc0bd0e387705042adeabe8d712fbede4f1929eefc9a1e8c002c40ff`). The suite harness exercised 6,998 assertions across 13 suites with 1 intentional skip, but the baseline itself recorded **3,506 passing / 3,492 failing assertions**. “Suite parity PASS” is **not** “all 6,998 assertions pass.” Rerun in the new checkout if the user asks for validation there.

## Dead ends: do not blindly retry

- Enabling generic native halfword loads alone passed short parity but was slightly slower because hot MMIO is unmapped. The ARM7 IPCSYNC read shortcut, an ARM7 guarded JIT IPCSYNC load, and a no-channel SPU shortcut yielded no meaningful improvement and were reverted. Global `-O3` was slightly slower than `-O2` in the Dolphin probe. A purported first `-O3` build was actually overridden by Makefile's later `-O2`; inspect real compile commands when testing flags.
- Existing `NOODS_JIT_POLL_SKIP=1` caused a large regression (~153 versus ~88 ms/frame) and skipped **zero** iterations. Do not make the ARM7 loop “pure” just to force a skip: diagnostic traces showed repeated ARM7 `STRH` writes to IPCSYNC and a conditional `LDR PC`, which can affect cross-CPU state. Event boundaries, MMIO side effects, SMC and pipeline/flag state must remain exact.
- Under the rockwrestler interactive result screen, accelerating the KEYINPUT poll reduced harness CPU work but **not the user's observed FPS**. Prioritize profiling a reproducible real gameplay bottleneck instead of adding more ROM-specific poll shortcuts.
- `rg` was unavailable in the previous workspace (use `grep` if necessary). Full Wii cross-builds emitted a pre-existing `.rodata` section-attribute warning from untouched `interpreter_lookup.cpp`; distinguish it from new warnings. `functions.read_file` once could not preview an `AGENTS.md`; shell `sed` works.

## Validation workflow for future changes

1. Ask which title, scene, input sequence, device and **actual Display vs Emu counter order** is the target. Obtain a ROM/save/input trace if possible; do not rely on a static boot/menu checkpoint to predict gameplay performance. Profile CPU/JIT, scheduled tasks (GPU/SPU), and presentation time separately before optimizing.
2. Compare candidate vs same-scene baseline under the same build flags and hardware/emulator settings. One-shot Dolphin timing is diagnostic; seek repeated controlled runs and real-Wii confirmation. Keep correctness higher priority than speculative speed.
3. If the helper scripts/ROMs/evidence were uploaded, run from project root: `bash tools/check_g1_encoder.sh`; `python3 tools/test_branch_arm.py`; `bash tools/run_g2_case.sh DOL LABEL ROM_PATH CHECKPOINTS MAX_FRAMES TIMEOUT_SECS [POOL_LIMIT_WORDS] [INPUT_A_FROM] [INPUT_A_TO] [INPUT_A2_FROM] [INPUT_A2_TO]`; `python3 tools/compare_runs.py --mode checkpoints --ref REF_DIR --cand CAND_DIR --out RESULT.json`; `bash tools/run_suite_gba.sh DOL LABEL OUT_DIR [TIMEOUT_SECS]`; compare suite JSON with `tools/compare_runs.py --mode suites ...`. A special `NOODS_G2_SCRIPTED_INPUT=1` build is needed for the extra input arguments to have an effect. G2 runner's Dolphin RC=143 after its sentinel is expected; check `STATUS=PASS` and `SENTINEL_HIT=1`.
4. Run normal and constrained-pool NDS/GBA parity, encoder, GBA suite and browser smoke for any JIT/bus change; report skipped tests explicitly. Avoid introducing frontend localhost dependencies or autoplay behavior into the browser build. Rebuild `jit.dol` only when a change measurably helps and is validated; do not silently replace a working binary with a slower experiment.
5. Use repo-relative paths in notes. If uploaded, further detailed historical evidence lives at `NooDS-Wii/BROWSER_JIT.md`, `NooDS-Wii/NDS_DISPSTAT_PROGRESS.md`, `NooDS-Wii/NDS_ROUND2_PROGRESS.md`, `NooDS-Wii/NDS_KEYINPUT_PROGRESS.md`, `NooDS-Wii/BUS_PROGRESS.md`, and `NooDS-Wii/FPS_PROGRESS.md`. These documents may not exist in the new checkout; this AGENTS.md is intentionally self-contained.
