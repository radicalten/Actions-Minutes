# Handoff — Agents2-5 Wii speedup

**Updated:** 2026-10-18. This is the quick-start and current-state handoff for the next turn/agent. Work only in the speedup folder unless explicitly asked otherwise.

## Start here — files and status

Project folder: `/home/user/repo/Agents2-5-NooDS-Wii-Speedup/`

- **Normal user-facing DOL:** `NooDS-Wii-Agents2-5-Speedup.dol` — SHA-256 `80a6bc9b54d80cd80c8f70c2bb1ab113cff7b4d20360ba90f95a256ac6361d6c` (1,468,960 bytes). Built from this folder's source as JIT + block linking, O3, browser mode, JIT stats off. **This is the easy-to-find main deliverable.** The browser code/string is present, but runtime browser startup has not been verified: the GUI attempt was blocked by Dolphin's first-run telemetry dialog, and a short headless launch did not produce a guest log marker.
- **Diagnostic suite-test DOL:** `NooDS-Wii-Agents2-5-Speedup-Test.dol` — SHA-256 `53e41b481deada0bd102ca63fa1988933e8b6c1db3dd9dcf54f870328ebe4dda` (1,475,968 bytes). This is `BROWSER=0`, full-RAM-dump, scripted-input test mode; **do not use it as the user-facing browser DOL**.
- The sources are in `NooDS-Wii/`; build rules are in `Makefile`. `SHA256SUMS.txt` covers both DOLs and the preserved baseline DOLs.
- `baseline-artifacts/` holds the original O2 browser DOL and the clean O2 diagnostic DOL used for timing comparison.
- `test-evidence/` contains the current candidate's page-2/page-3 screenshots and frame-650 logs. `OPTIMIZATION-RESULTS.md` has the experiment table; `PERFORMANCE-BASELINE.md` has the clean baseline and profiler record.
- `/home/user/repo/Agents2-5-NooDS-Wii/` is the correctness-fixed reference source: **keep it frozen**. The speedup worktree was copied from Agents2-5, not b4. Do not make performance edits in the reference tree or use b4 as a base.

The speedup-source delta is deliberately small: Makefile defaults O3 and selects stats-off for browser builds; `arm_jit.h/.cpp` compile out diagnostic counter updates when `JIT_STATS=0`. These counters do not control emulation semantics. No CPU-correctness handler was changed in this worktree.

## Results and acceptance gate

- Clean O2/stats-on baseline: **95,197 µs/frame** on three frame-650 Dolphin runs.
- O3/stats-off candidate: **86,052 µs/frame** on three runs (**9.61% lower** than baseline). Fresh validation after rebuilding the DOL/image measured 86,056 µs on page 3 and 86,062 µs on page 2; these two sanity runs are not the three-sample benchmark mean.
- Correctness: **25/27 groups**, 0 unrun; exactly **128 combined ARM9/ARM9 Thumb failures** (accepted cap); ARM7 and ARM7 Thumb pass **12,056/12,056**. Page 2 shows groups 0–13, with groups 2/3 the only failures. Page-3 groups 14–26 pass.
- `suite.nds` SHA-256: `8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3`. It is a temporary repeatable correctness workload, not a representative game benchmark. Do not claim real-Wii performance from Dolphin timings.
- O3+LTO was not adopted: it is slower than the candidate and surfaces the existing `gpu.cpp:125` warning about a 786,432-byte `memset` into a 393,216-byte `tempBuffer` declared in `main.cpp:462`. Loop-unrolling off and poll-skip were slower; code-pool experiments failed or fell back. See `OPTIMIZATION-RESULTS.md` before retrying them.

## Set up devkitPPC / devkitPro

For Debian/Ubuntu, follow the official instructions at [devkitPro Getting Started](https://devkitpro.org/wiki/Getting_Started) and [devkitPro pacman](https://devkitpro.org/wiki/devkitPro_pacman). The tested install sequence is:

```sh
sudo apt-get update
sudo apt-get install -y wget
cd /tmp
wget -U 'dkp-apt' https://apt.devkitpro.org/install-devkitpro-pacman
chmod +x install-devkitpro-pacman
sudo ./install-devkitpro-pacman       # adds the official dkp apt repo and installs pacman
sudo dkp-pacman -Sy
sudo dkp-pacman -S wii-dev
source /etc/profile.d/devkit-env.sh
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC="$DEVKITPRO/devkitPPC"
export PATH="$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH"
powerpc-eabi-g++ --version
command -v elf2dol
```

`wii-dev` provides the Wii/devkitPPC compiler, libogc, Wii rules, and DOL tools. The recorded build environment is devkitPPC r51-1, PowerPC GCC 16.2.0, binutils 2.47, and libogc 3.2.1. In the current environment the compiler is under `/opt/devkitpro/devkitPPC/bin/` and `elf2dol` under `/opt/devkitpro/tools/bin/`. `BASELINE-SOURCES.sha256` records the pre-optimization source-copy provenance; `SHA256SUMS.txt` is the checksum manifest for retained DOL artifacts.

## Set up Dolphin and SD-image tools

For the exact headless workflow used here on Debian 13, install a package that supplies the CLI binary plus mtools:

```sh
sudo apt-get update
sudo apt-get install -y dolphin-emu mtools dosfstools
/usr/games/dolphin-emu-nogui --version
```

The recorded target is Dolphin `[master] 2503`; the Debian package provides `/usr/games/dolphin-emu-nogui`. The Dolphin project also publishes official Linux Flatpak builds at [dolphin-emu.org/download](https://dolphin-emu.org/download/); if using Flatpak or another distro, ensure a headless/no-GUI executable is available for the command below.

The repeatable 64 MiB FAT16 image is `/home/user/repo/dolphin-test/cpu-correctness-iter1/sd.raw` (SHA-256 `b970e17e11421f0a743c92f54cca2f3facd7302ed67c94977e0b2dfc03207df0`). It contains the verified ROM and page-3 schedule. If absent, the user previously authorized rebuilding it from the verified local ROM:

```sh
ROOT=/home/user/repo
TEST="$ROOT/dolphin-test/cpu-correctness-iter1"
mkdir -p "$TEST"
truncate -s 64M "$TEST/sd.raw"
sudo mkfs.fat -F 16 -n NOODS_TEST "$TEST/sd.raw"
mcopy -o -i "$TEST/sd.raw" "$ROOT/suite.nds" ::suite.nds
cp "$TEST/autoboot-page3-650.txt" "$TEST/autoboot.txt"
mcopy -o -i "$TEST/sd.raw" "$TEST/autoboot.txt" ::autoboot.txt
```

`mtools` and `dosfstools` are needed only for image setup/updates. The host schedules are in `dolphin-test/cpu-correctness-iter1/autoboot-page2-650.txt` and `autoboot-page3-650.txt`; the input parser uses an exclusive end frame (`[FROM,TO)`).

## Build the two DOLs

```sh
source /etc/profile.d/devkit-env.sh
cd /home/user/repo/Agents2-5-NooDS-Wii-Speedup

# Normal user-facing JIT/browser build: BROWSER=1, O3, JIT_STATS=0.
make clean-all
make -j"$(nproc)" JIT=1 BLOCKS=1 BROWSER=1 FULL_RAM_DUMP=0 G2_SCRIPTED_INPUT=0
cp -f NooDS-Wii_jit1_blocks1_browser1_o3_nostats.dol \
  NooDS-Wii-Agents2-5-Speedup.dol

# Diagnostic test build: autoboot the suite, dump full RAM, accept scripted keys.
make -j"$(nproc)" JIT=1 BLOCKS=1 BROWSER=0 FULL_RAM_DUMP=1 \
  G2_SCRIPTED_INPUT=1 OPT_LEVEL=3 JIT_STATS=0
cp -f NooDS-Wii_jit1_blocks1_browser0_fullram_scriptinput_o3_nostats.dol \
  NooDS-Wii-Agents2-5-Speedup-Test.dol

sha256sum -c SHA256SUMS.txt
```

To get counters in an autoboot diagnostic build, set `JIT_STATS=1` (the `BROWSER=0` default). For performance-comparable tests explicitly set `JIT_STATS=0`. `make clean-all` removes generated `build_jit*` and tagged `NooDS-Wii_jit*.dol` outputs, but leaves the friendly DOL names and baseline artifacts above.

## Run a fresh frame-650 page test

Each run must clone the base SD image, overwrite the ROM/schedule, and delete old guest captures. Example for page 3 (change `page3` to `page2` for page 2):

```sh
set -e
ROOT=/home/user/repo
TEST="$ROOT/dolphin-test/cpu-correctness-iter1"
PAGE=page3
RUN="$TEST/run-speedup-$PAGE"
rm -rf "$RUN"
mkdir -p "$RUN/out"
cp "$TEST/sd.raw" "$RUN/sd.raw"
mcopy -o -i "$RUN/sd.raw" "$ROOT/suite.nds" ::suite.nds
cp "$TEST/autoboot-$PAGE-650.txt" "$RUN/autoboot.txt"
mcopy -o -i "$RUN/sd.raw" "$RUN/autoboot.txt" ::autoboot.txt
mdelete -i "$RUN/sd.raw" ::g2_state.log ::mem_dump.bin ::fb_dump.bin 2>/dev/null || true
cp -a "$TEST/dolphin-user" "$RUN/dolphin-user"
sed -i "s|^WiiSDCardPath = .*|WiiSDCardPath = $RUN/sd.raw|" \
  "$RUN/dolphin-user/Config/Dolphin.ini"

set +e
timeout --signal=TERM --kill-after=5s 180s \
  env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  /usr/games/dolphin-emu-nogui -u "$RUN/dolphin-user" \
  -e "$ROOT/Agents2-5-NooDS-Wii-Speedup/NooDS-Wii-Agents2-5-Speedup-Test.dol" \
  -p headless -v Null \
  -C Core.WiiSDCard=True -C Core.WiiSDCardAllowWrites=True \
  -C General.WiiSDCardPath="$RUN/sd.raw" > "$RUN/out/dolphin.log" 2>&1
rc=$?
set -e
echo "Dolphin exit status: $rc"
for f in g2_state.log mem_dump.bin fb_dump.bin; do
  mcopy -o -i "$RUN/sd.raw" "::$f" "$RUN/out/$f" 2>/dev/null || true
done
```

A wrapper/timeout status of **137 is expected in this setup even when frame 650 was captured**. Do not infer success from status alone. Check:

```sh
grep -E 'CHECKPOINT FRAME 650|SCRIPT_INPUT|PERF frame=650|JIT_STATS' \
  "$RUN/out/g2_state.log"
stat -c '%n %s bytes' "$RUN/out"/*
```

Require `CHECKPOINT FRAME 650`, the requested schedule, and fresh captures (`mem_dump.bin` is 4,345,856 bytes; `fb_dump.bin` is 393,216 bytes). The log must show the counters state. The expected frame-650 CPU trace totals are ARM9-family 13,252 pass / 128 fail and ARM7-family 12,056 pass / 0 fail; 0 unrun. Page-2/page-3 screenshots and logs from the current DOL are under `test-evidence/`.

To compare a baseline and candidate memory dump:

```sh
python3 "$ROOT/scripts/compare_trace_ram.py" \
  "$BASELINE_RUN/out/mem_dump.bin" "$CANDIDATE_RUN/out/mem_dump.bin" \
  --output "$CANDIDATE_RUN/out/trace-diff.json"
```

The comparator reports trace transitions; it does not independently decide architectural correctness. Review the ROM hash, checkpoint/schedule, group totals, trace results, and assertion journal together. For a clean three-run performance comparison, use fresh profiles/SD clones and compare `avg_frame_us`; do not use `core_fps` or `JIT_PROFILE=1` timings.

## Useful constraints and references

- Keep the 25/27 result, 128 combined ARM9/ARM9 Thumb failure allowance, ARM7 all-pass result, and page-2/page-3 extra checks in scope. Do not regress any passing vector.
- Keep the original correctness-fixed source frozen; do all experiments in `Agents2-5-NooDS-Wii-Speedup/`. Do not use b4.
- The browser DOL has not yet had a successful GUI-startup smoke test. Test that separately before treating it as a hardware-ready release.
- Results are Dolphin/suite-specific, not real-Wii performance claims. The suite ROM is for local testing and must not be bundled or redistributed without authorization.
- Suite vector descriptions and trace schema notes are retained at `/home/user/repo/dolphin-test/suite-readme.md`. Dust is optional; the prior stock Dust run on this ROM was 25/27. If CPU semantics are changed, fetch the oracle from `https://github.com/kelpsyberry/dust` and rerun it rather than treating the suite alone as architectural truth.
- A shell-start warning about `/home/user/.local/share/dust-cargo/env` missing is stale Dust configuration; it is unrelated to devkitPPC, Dolphin, or the build.
