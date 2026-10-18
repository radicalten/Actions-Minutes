# Agents2-5 CPU correctness and browser-mode JIT build

**Updated:** 2026-10-18. This is the current source-of-truth runbook for the supplied `suite.nds`, Agents2-5, Dust, and Dolphin. Commands assume `/home/user/repo`.

## Current status

The working emulator is **Agents2-5** at `/home/user/repo/Agents2-5-NooDS-Wii`. Do not use `b4-pp2-NooDS-Wii` as a base. The source was restored from `radicalten/Actions-Minutes` commit `0096fa162433e0a1df70fec09c7156e8ca454687`; the local source directory is not a Git checkout.

CPU-correctness changes now reach the requested target:

- Latest frame-650 run: **25/27 groups**, 2 nonpassing, 0 unrun (**92.59%**). The only nonpassing groups are ARM9 and ARM9 Thumb; Dust’s stock run also completed at 25/27, and the accepted exception is **128 combined ARM9/ARM9 Thumb vector failures**.
- Candidate trace: ARM9-family vectors **13,252 pass / 128 fail of 13,380**; ARM7-family vectors **12,056 pass / 0 fail of 12,056**. The 128 ARM9-family failures are within the user’s stated exception; there are no extra ARM7 failures.
- Page 2 (`cp15-page2-test/out/preview.png`) shows groups 0–13: ARM7 and ARM7 Thumb pass, ARM9 and ARM9 Thumb are the only failures, and groups 4–13 pass.
- Page 3 (`cp15-test/out/preview.png`) shows groups 14–26 all passing. Keep both pages and their extra PASS/FAIL checks in scope.
- Comparing the earlier 373-failure run (`stm-wb-test`) to the latest candidate produced **245 fail-to-pass, 0 pass-to-fail** status transitions. ARM7 records did not change; the assertion journal is identical. The UDF exception change accounts for 64 additional passes; the CP15 change accounts for another 181.

CPU changes validated in the suite include the RSC NZCV fix, ARM7/ARM9 MSR F-mask and privilege handling, CPU-specific empty register-list and STM writeback behavior, the existing arithmetic/rotate/Thumb fixes, undefined-instruction exception entry (including ARM/Thumb LR offsets), and CP15 C2/C3/C5/C6 register behavior. CP15 fields are saved/restored; the save-state format version was bumped to 8. Temporary mode/transfer logging was removed from source after the diagnostic runs.

## Paths and hashes

- Verified ROM: `/home/user/repo/suite.nds`, SHA-256 `8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3`. This host copy was extracted from the existing iter1 SD image and verified. Always hash-check after copying.
- Fresh-run SD source: `/home/user/repo/dolphin-test/cpu-correctness-iter1/sd.raw` (**64 MiB**). Clone it for every Dolphin run, overwrite the ROM and `autoboot.txt`, and remove/replace prior captures. Do **not** rely on `dolphin-test/suite-template.raw`; that path does not exist.
- Top-level host config: `/home/user/repo/dolphin-test/autoboot.txt` (120-frame baseline). Iter1 host config: `/home/user/repo/dolphin-test/cpu-correctness-iter1/autoboot.txt` (600-frame baseline). Inspect both before a new run; the SD image also has its own root `autoboot.txt`.
- Page schedules: `cpu-correctness-iter1/autoboot-page2-650.txt` advances once at frames 600–604; `autoboot-page3-650.txt` advances at 600–604 and 620–624; `autoboot-page4-700.txt` adds a third pulse at 640–644. The input parser uses an exclusive end frame (`[FROM,TO)`). The page-2 and page-3 schedules were both exercised on fresh SD copies.
- Latest test run outputs: `/home/user/repo/dolphin-test/cpu-correctness-iter1/cp15-test/out/` and `cp15-page2-test/out/`. The former includes the frame-650 checkpoint, trace diff, screenshot, and memory dump.
- Latest test-only DOL used for those page runs: `/home/user/repo/Agents2-5-NooDS-Wii/NooDS-Wii_jit1_blocks1_browser0_fullram_scriptinput.dol`, SHA-256 `4014fadcae70435fe11b735df0c0c330f1488cb194edff1be189f517ee4c7979`. It is a diagnostic ROM-autoboot build, **not** the release/browser DOL.
- User-facing browser JIT deliverable: `/home/user/repo/build-artifacts/NooDS-Wii-Agents2-5-JIT-browser.dol`, SHA-256 `d9111514574813b24427fa54e6412e7ec101e19b9286afcdb0791afdb5cadbfe`. It was rebuilt from the cleaned source with `JIT=1`, `BLOCKS=1`, `BROWSER=1`, full-RAM dumping off, and scripted-input support off. It starts at the SD browser (`sd:/`); it does not autoboot `suite.nds`.

Dust reference: `/home/user/repo/upstream/dust`, commit `76d79f25e4ea3ce355c4ac868f5ac9a233e64b89`. Its stock desktop interpreter completed the same ROM at 25/27. Dust’s ARM9 CP15 implementation covers the C2 cache configuration, C3 write-buffer control, C5 access permissions, and C6 protection regions missing from the original NooDS subset; its undefined handler enters vector 4. Use Dust as the practical correctness oracle, but do not claim a fresh Dust run if one was not performed.

## Toolchain setup

The current environment has devkitPro `wii-dev` installed. In a fresh Debian/Ubuntu environment, use the official [Getting Started](https://devkitpro.org/wiki/Getting_Started) and [devkitPro pacman](https://devkitpro.org/wiki/devkitPro_pacman) instructions, then install `wii-dev` (and `nds-dev` only if rebuilding ARM test code).

```sh
source /etc/profile.d/devkit-env.sh
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC="$DEVKITPRO/devkitPPC"
export PATH="$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH"
"$DEVKITPPC/bin/powerpc-eabi-g++" --version
command -v elf2dol /usr/games/dolphin-emu-nogui mcopy mdir
```

The shell may print a harmless warning about a missing old Dust cargo environment at `/home/user/.local/share/dust-cargo/env`; Dust commands succeeded when `RUSTUP_HOME`, `CARGO_HOME`, and `$CARGO_HOME/bin` were set explicitly.

## Build commands

### Normal user-facing JIT/browser DOL

Build from the Agents2-5 tree. Set browser mode explicitly and leave test instrumentation disabled:

```sh
source /etc/profile.d/devkit-env.sh
cd /home/user/repo/Agents2-5-NooDS-Wii
make -j"$(nproc)" JIT=1 BLOCKS=1 BROWSER=1 FULL_RAM_DUMP=0 G2_SCRIPTED_INPUT=0
# Tagged build: NooDS-Wii_jit1_blocks1_browser1.dol
mkdir -p /home/user/repo/build-artifacts
cp -f NooDS-Wii_jit1_blocks1_browser1.dol \
  /home/user/repo/build-artifacts/NooDS-Wii-Agents2-5-JIT-browser.dol
sha256sum /home/user/repo/build-artifacts/NooDS-Wii-Agents2-5-JIT-browser.dol
```

`BROWSER=1` sets `NOODS_FORCE_BROWSER=1`; `main.cpp` starts the browser at `sd:/` and skips the autoboot hook. Do not distribute the diagnostic `BROWSER=0` DOL as the normal build.

### Test-only full-RAM / scripted-input DOL

Use this only for ROM-autoboot regression and capture runs:

```sh
make -j"$(nproc)" JIT=1 BLOCKS=1 BROWSER=0 FULL_RAM_DUMP=1 G2_SCRIPTED_INPUT=1
# Tagged build: NooDS-Wii_jit1_blocks1_browser0_fullram_scriptinput.dol
```

It reads `sd:/autoboot.txt` (or `sd:/noods/autoboot.txt`) when `BROWSER=0`. The first non-comment line names the ROM. `FULL_RAM_DUMP=1` captures 4 MiB of main RAM; `G2_SCRIPTED_INPUT=1` enables the frame-window keys. Keep `BROWSER=0` confined to intentional test runs.

## Reliable Dolphin test workflow

1. Inspect both configs before setting up the run:

   ```sh
   cat /home/user/repo/dolphin-test/autoboot.txt
   cat /home/user/repo/dolphin-test/cpu-correctness-iter1/autoboot.txt
   ```

2. Make a new run directory and clone the existing 64 MiB SD image. If the host ROM copy is missing, extract it from that image first. Overwrite the suite file and run-specific autoboot config in the clone; delete prior outputs from the clone so old captures cannot pass as fresh ones.

   ```sh
   ROOT=/home/user/repo
   RUN="$ROOT/dolphin-test/cpu-correctness-iter1/my-run"
   mkdir -p "$RUN/out"
   cp "$ROOT/dolphin-test/cpu-correctness-iter1/sd.raw" "$RUN/sd.raw"
   # Optional recovery of the supplied ROM from the SD image:
   # mcopy -o -i "$RUN/sd.raw" ::suite.nds "$ROOT/suite.nds"
   sha256sum "$ROOT/suite.nds"
   mcopy -o -i "$RUN/sd.raw" "$ROOT/suite.nds" ::suite.nds
   ```

3. Write the schedule you intend to test to `$RUN/autoboot.txt`, then copy it into the fresh image. Example for page 3 (page 2 uses only the first Right pulse):

   ```text
   sd:/suite.nds
   MAX_FRAMES=650
   CHECKPOINTS=650
   INPUT_RIGHT_FROM=600
   INPUT_RIGHT_TO=605
   INPUT_RIGHT2_FROM=620
   INPUT_RIGHT2_TO=625
   ```

   ```sh
   mcopy -o -i "$RUN/sd.raw" "$RUN/autoboot.txt" ::autoboot.txt
   mdelete -i "$RUN/sd.raw" ::g2_state.log ::mem_dump.bin ::fb_dump.bin \
     ::ldm_user_test.log ::mode_transitions.log 2>/dev/null || true
   ```

4. Copy a known-good Dolphin user profile and edit its SD path. The test runs here use the iter1 profile at `cpu-correctness-iter1/stm-wb-test/dolphin-user`.

   ```sh
   cp -a "$ROOT/dolphin-test/cpu-correctness-iter1/stm-wb-test/dolphin-user" "$RUN/dolphin-user"
   sed -i "s|^WiiSDCardPath = .*|WiiSDCardPath = $RUN/sd.raw|" \
     "$RUN/dolphin-user/Config/Dolphin.ini"
   ```

5. Run the diagnostic DOL. A timeout status of 137 can occur after Dolphin has written valid frame-650 files; do not classify by exit code alone. Check the checkpoint and output sizes/timestamps, then copy captures out of the image.

   ```sh
   timeout --signal=TERM --kill-after=5s 140s \
     env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
     /usr/games/dolphin-emu-nogui -u "$RUN/dolphin-user" \
     -e "$ROOT/Agents2-5-NooDS-Wii/NooDS-Wii_jit1_blocks1_browser0_fullram_scriptinput.dol" \
     -p headless -v Null \
     -C Core.WiiSDCard=True -C Core.WiiSDCardAllowWrites=True \
     -C General.WiiSDCardPath="$RUN/sd.raw"
   mcopy -o -i "$RUN/sd.raw" ::g2_state.log "$RUN/out/g2_state.log"
   mcopy -o -i "$RUN/sd.raw" ::mem_dump.bin "$RUN/out/mem_dump.bin"
   mcopy -o -i "$RUN/sd.raw" ::fb_dump.bin "$RUN/out/fb_dump.bin"
   stat -c '%n %s bytes' "$RUN/out"/*
   ```

   Expected capture sizes: `mem_dump.bin` 4,345,856 bytes and `fb_dump.bin` 393,216 bytes. The log must include `CHECKPOINT FRAME 650` and the intended `SCRIPT_INPUT` schedule.

6. Compare traces, not only pixels or process status:

   ```sh
   python3 "$ROOT/scripts/compare_trace_ram.py" \
     "$ROOT/dolphin-test/cpu-correctness-iter1/stm-wb-test/out/mem_dump.bin" \
     "$RUN/out/mem_dump.bin" --output "$RUN/out/trace-diff.json"
   ```

   The comparator reports vector status transitions and assertion-journal equality. It does not itself determine architectural correctness; review the ROM hash, on-screen group totals, trace records, and assertion journal together. Page-2 and page-3 evidence from the current run is under `cp15-page2-test/out/` and `cp15-test/out/` respectively.

## Common traps and standing scope

- Always clone `/home/user/repo/dolphin-test/cpu-correctness-iter1/sd.raw`; `suite-template.raw` is absent. Overwrite `suite.nds` and `autoboot.txt` in each new clone and delete stale capture files.
- Use the verified Agents2-5 source, not b4. Keep all changes in the Agents2-5 C/C++ tree; Dust is a reference.
- Dust is the suite oracle. The accepted limit is **128 combined ARM9 + ARM9 Thumb failures**; this candidate is at that limit, not above it. Keep pages 2 and 3 and the extra PASS/FAIL tests enabled.
- Normal JIT/browser DOLs must start in the SD browser. Only intentional regression runs use `BROWSER=0`.
- A timeout or wrapper status 137 may follow successful output capture; validate fresh checkpoint and dump contents before repeating a run.
- The optional Dust interlocks run was inconclusive and is not a reference result.
- The local Agents2-5 folder is not a Git repository; record source/build hashes and do not assume `git status` is available.
