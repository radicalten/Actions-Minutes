# Agent quickstart: Agents2-5, `suite.nds`, Dust, and Dolphin

**Last verified:** 2026-10-18. Commands below assume the Linux workspace at `/home/user/repo` and the supplied ROM. Preserve ROM/build hashes in every comparison; do not treat a similarly named build or a different test ROM as equivalent.

## 0. Ground truth before changing anything

- **Working emulator base:** `/home/user/repo/Agents2-5-NooDS-Wii`. Continue from this tree. **Do not use `b4-pp2-NooDS-Wii` as a base.** Dust is a reference only; its Rust CPU techniques can inform a carefully tested C/C++ change.
- **Test ROM:** `/home/user/repo/suite.nds`, SHA-256 `8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3`. The workspace contains the ROM and handbook, not the ROM-generation source project; for this task, verify and use this binary rather than trying to rebuild it.
- **Existing Agents2-5 fix:** `NooDS-Wii/interpreter_alu.cpp` fixes `Interpreter::rscs` to clear all NZCV bits. The old mask cleared only N/Z and left stale C/V. Full-RAM interpreter/JIT testing on this ROM recorded 59 fail-to-pass transitions per CPU and no regressions. The test-only full-RAM tree has a copied version of this fix; keep it synchronized with future emulator changes.
- **Local group comparison after that fix:** Agents2-5 passes 21/27 groups; fresh stock Dust passes 25/27 on the *same ROM hash*. Agents2-5-only nonpasses are group 0 ARM7, 1 ARM7 Thumb, 15 LDM/STM, and 20 SQRT64. Both builds fail groups 2–3 (ARM9 and ARM9 Thumb). Do not repeat the user’s “Dust passes all” statement as a measured result: it was not reproduced by the fresh stock build in this environment.

| `suite.nds` group | Test | Agents2-5 | Stock Dust | Current note |
|---:|---|---|---|---|
| 0 | ARM7 | Fail | Pass | Candidate 5,263/5,344 pass; first bad ID 3571. Candidate-only; map the first case before patching. |
| 1 | ARM7 Thumb | Fail | Pass | Candidate 6,680/6,712 pass; first bad ID 4551. Candidate-only; map the first case before patching. |
| 2 | ARM9 | Fail | Fail | Candidate 6,268/6,641 pass, first bad 5435; Dust has 64 failures, first bad 5859. Shared failure. |
| 3 | ARM9 Thumb | Fail | Fail | Candidate 6,595/6,739 pass, first bad 5419; Dust has 64 failures, first bad 5891. Shared failure. |
| 4–14 | Other CPU/ALU groups | Pass | Pass | No page-2 discrepancy observed. |
| 15 | LDM/STM | Fail | Pass | Candidate-only; exact subtest/root cause not mapped yet. |
| 16–19 | Other groups | Pass | Pass | No discrepancy observed. |
| 20 | SQRT64 | Fail | Pass | Candidate-only; `sqrtl` precision is a lead, not a proven cause. |
| 21–26 | Other groups | Pass | Pass | No page-3 discrepancy observed. |

Screenshots: `dolphin-test/agents-rscfix-jit-page2-screen.png`, `agents-rscfix-jit-page3-screen.png`, and `dust-run/fresh-review/captures/page-02-fresh.png`, `page-03-fresh.png`. A fresh Dust build with optional ARM9 interlocks compiled after a local upstream typo correction, but its display stayed black and no completed result page was captured; **that run is inconclusive and is not a reference result**. The local-only Dust typo change is in `upstream/dust/core/src/cpu/interpreter/arm9.rs` (`interlock_c.port_ab` → `interlock.port_ab`); do not apply it to Agents2-5.

The exact failing vectors/subtests for groups 0, 1, 15, and 20 are still to be mapped. The assertion journal in `dolphin-test/agents-rscfix-jit-page4-mem.bin` has a parsed header (magic `0x41535235`, version 1, capacity 4096, 234 records; 20-byte records), but event-field meanings and page-4 markers `0000000B` / `00000014` remain unknown. Do not infer a specific failing subtest from those markers. Dust uses an integer square-root loop; this PowerPC toolchain reports `long double` mantissa 53, making NooDS’s `sqrtl` SQRT64 path worth testing—but not a confirmed fix.

## 1. Install/verify the toolchains and host tools

For Debian/Ubuntu, use devkitPro’s supported pacman installer and groups. See the official [Getting Started](https://devkitpro.org/wiki/Getting_Started) and [devkitPro pacman](https://devkitpro.org/wiki/devkitPro_pacman) pages.

```sh
sudo apt update
sudo apt install -y wget curl ca-certificates git build-essential python3 python3-venv \
  mtools xvfb xdotool imagemagick dolphin-emu

wget -U "dkp-apt" https://apt.devkitpro.org/install-devkitpro-pacman
chmod +x ./install-devkitpro-pacman
sudo ./install-devkitpro-pacman
sudo dkp-pacman -Syu
sudo dkp-pacman -S nds-dev wii-dev
```

`nds-dev` supplies devkitARM/DS tools; `wii-dev` supplies devkitPPC/libogc/Wii build tools. Load the environment and verify the actual compilers before building:

```sh
source /etc/profile.d/devkit-env.sh   # if installed by devkitPro
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
export DEVKITPPC="$DEVKITPRO/devkitPPC"
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$DEVKITPPC/bin:$PATH"

"$DEVKITPPC/bin/powerpc-eabi-g++" --version
"$DEVKITARM/bin/arm-none-eabi-g++" --version
command -v elf2dol arm-none-eabi-objdump
command -v /usr/games/dolphin-emu-nogui mcopy mdir
```

Do **not** add `/opt/devkitpro/pacman/bin` to `PATH`; on Debian-like systems use `dkp-pacman`. This workspace already has devkitPPC r51-1 / GCC 16.2.0 and `/usr/games/dolphin-emu-nogui`; devkitARM was missing at last check. A system `arm-none-eabi-objdump` is useful for disassembly but is not a substitute for the devkitARM compiler when rebuilding ARM test code.

## 2. Verify and prepare `suite.nds`

Check the supplied ROM before each comparison and after copying it into an SD image:

```sh
cd /home/user/repo
sha256sum suite.nds
```

The expected digest is `8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3`. A known-good 128 MiB FAT16 Dolphin SD image is `dolphin-test/suite-template.raw`. Use a *fresh copy per run* so stale logs/dumps cannot masquerade as new results:

```sh
ROOT=/home/user/repo
RUN="$ROOT/dolphin-test/my-run"
mkdir -p "$RUN"
RAW="$RUN/sd.raw"
cp "$ROOT/dolphin-test/suite-template.raw" "$RAW"
mcopy -o -i "$RAW" "$ROOT/suite.nds" ::suite.nds
mdir -i "$RAW" ::
mcopy -i "$RAW" ::suite.nds "$RUN/rom-from-sd.nds"
sha256sum "$RUN/rom-from-sd.nds" "$ROOT/suite.nds"
```

The template’s `autoboot.txt` contains:

```text
sd:/suite.nds
MAX_FRAMES=600
CHECKPOINTS=120,300,600
```

That file is for an intentional autoboot/test-harness run. A normal browser DOL ignores it because `BROWSER=1` bypasses the autoboot hook. For a clean interactive browser test, remove `autoboot.txt` from the image (`mdel -i "$RAW" ::autoboot.txt`) or leave it in place only when the built DOL is known to force browser mode.

## 3. Build the Agents2-5 DOLs

The Makefile encodes distinct build variants in the output names. JIT browser startup is the default when `JIT=1`; it starts in the SD browser at `sd:/` and does **not** autoboot `suite.nds`.

```sh
source /etc/profile.d/devkit-env.sh
cd /home/user/repo/Agents2-5-NooDS-Wii
make -j"$(nproc)" JIT=1 BLOCKS=1
# Named result: NooDS-Wii_jit1_blocks1_browser1.dol
cp NooDS-Wii_jit1_blocks1_browser1.dol \
   /home/user/repo/build-artifacts/NooDS-Wii-Agents2-5-RSCfix-JIT.dol
sha256sum /home/user/repo/build-artifacts/NooDS-Wii-Agents2-5-RSCfix-JIT.dol
```

The checked browser DOL currently has SHA-256 `b64eeec6c7c73e2b02a987ddbf4d865ccaa828d9c1c7c30aa55b1cfa33d30ca6`. The Makefile also copies the last-built DOL to generic `NooDS-Wii.dol`; use the configuration-tagged output for reproducibility.

For an **intentional autoboot regression build only**, set `BROWSER=0` explicitly:

```sh
make -j"$(nproc)" JIT=1 BLOCKS=1 BROWSER=0
# Named result: NooDS-Wii_jit1_blocks1_browser0.dol
```

With `BROWSER=0`, `tryAutoboot()` reads `sd:/autoboot.txt` (or `sd:/noods/autoboot.txt`) and can load the ROM named on its first non-comment line. Do not ship or present this build as the usual JIT/browser DOL. Verify the effective macro/build name when in doubt:

```sh
make -pn JIT=1 BLOCKS=1 2>/dev/null | grep -E '^(CONFIG_OUT|BROWSER_DEFS) ?[:=]'
make -pn JIT=1 BLOCKS=1 BROWSER=0 2>/dev/null | grep -E '^(CONFIG_OUT|BROWSER_DEFS) ?[:=]'
```

## 4. Download/build/run Dust as a reference

The reference source used here is `kelpsyberry/dust` commit `76d79f25e4ea3ce355c4ac868f5ac9a233e64b89`. For a new checkout:

```sh
cd /home/user/repo/upstream
git clone https://github.com/kelpsyberry/dust.git dust
cd dust
git checkout 76d79f25e4ea3ce355c4ac868f5ac9a233e64b89
```

Install Rust with rustup if needed, then install the known-compatible toolchain. The newer `1.101.0-nightly` failed this checkout’s locked `proc-bitfield 0.5.3`; use the pinned nightly below instead:

```sh
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
. "$HOME/.cargo/env"
rustup toolchain install nightly-2025-12-18
cd /home/user/repo/upstream/dust
cargo +nightly-2025-12-18 build --locked --profile ci -p dust-desktop
```

Dust needs an X display in this headless environment. Start Xvfb on a free display, give Dust isolated XDG directories, and run the same ROM path. Example (run the Xvfb and Dust commands in the same shell/session):

```sh
RUN=/home/user/repo/dust-run/agent-reference
mkdir -p "$RUN"/{home,cache,config,data,runtime,captures}
chmod 700 "$RUN/runtime"
Xvfb :98 -screen 0 1300x800x24 -nolisten tcp >"$RUN/xvfb.log" 2>&1 &
XVFB_PID=$!
export DISPLAY=:98 HOME="$RUN/home"
export XDG_CACHE_HOME="$RUN/cache" XDG_CONFIG_HOME="$RUN/config"
export XDG_DATA_HOME="$RUN/data" XDG_RUNTIME_DIR="$RUN/runtime"
cd /home/user/repo/upstream/dust
cargo +nightly-2025-12-18 run --locked --profile ci -p dust-desktop -- \
  /home/user/repo/suite.nds
# In another shell, capture the screen while the app is open:
# DISPLAY=:98 import -window root "$RUN/captures/page.png"
# DISPLAY=:98 xdotool keydown Right; sleep 0.4; DISPLAY=:98 xdotool keyup Right
# After the run, stop Xvfb with: kill "$XVFB_PID"
```

Capture the completion/results pages and retain the run’s ROM hash, executable/source commit, and screenshots. ALSA “cannot find card/default PCM” messages are nonfatal here. The local stock interpreter run completed at **25/27**, not 27/27: it passed the candidate-only groups 0, 1, 15, and 20 but failed groups 2 and 3. The optional `interp-arm9-interlocks` experiment is not a validated alternative: a local upstream typo fix allowed it to compile, but its window remained black and no completed suite page was obtained.

## 5. Run Dolphin and collect trustworthy artifacts

Use the headless executable for automated dumps. Dolphin’s `-C General.WiiSDCardPath=...` override was unreliable with newly created profiles in this environment; the robust method is to copy a known-good Dolphin user directory and edit its `Config/Dolphin.ini`.

```sh
ROOT=/home/user/repo
RUN="$ROOT/dolphin-test/agent-run"
RAW="$RUN/sd.raw"
DOLPHIN_USER="$RUN/dolphin-user"
OUT="$RUN/out"
mkdir -p "$RUN" "$OUT"
cp "$ROOT/dolphin-test/suite-template.raw" "$RAW"
mcopy -o -i "$RAW" "$ROOT/suite.nds" ::suite.nds
# Autoboot file is present in the template; it selects suite.nds and checkpoint 600.
cp -a "$ROOT/dolphin-test/agents-rscfix-jit-600-user" "$DOLPHIN_USER"
sed -i "s|^WiiSDCardPath = .*|WiiSDCardPath = $RAW|" "$DOLPHIN_USER/Config/Dolphin.ini"
grep -E '^(WiiSDCardPath|WiiSDCard|WiiSDCardAllowWrites) =' \
  "$DOLPHIN_USER/Config/Dolphin.ini"
```

For an autoboot/full-RAM test, launch the **test-harness DOL** (not the normal browser DOL) with the `autoboot.txt` above:

```sh
timeout --signal=TERM --kill-after=5s 170s \
  env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  /usr/games/dolphin-emu-nogui -u "$DOLPHIN_USER" \
  -e "$ROOT/build-artifacts/NooDS-Wii-Agents2-5-RSCfix-JIT-fullram.dol" \
  -p headless -v Null \
  -C Core.WiiSDCard=True -C Core.WiiSDCardAllowWrites=True \
  -C General.WiiSDCardPath="$RAW"
```

A timeout/exit 137 can be expected: Dolphin may stay open after the test DOL reaches its frame checkpoint and writes files. Do **not** treat exit 137 alone as failure or success. Check that the raw SD contains fresh `g2_state.log`, `mem_dump.bin`, and `fb_dump.bin`, and that their timestamps/sizes correspond to this run:

```sh
mdir -i "$RAW" ::
mcopy -o -i "$RAW" ::g2_state.log "$OUT/g2_state.log"
mcopy -o -i "$RAW" ::mem_dump.bin "$OUT/mem_dump.bin"
mcopy -o -i "$RAW" ::fb_dump.bin "$OUT/fb_dump.bin"
stat -c '%n %s bytes' "$OUT"/*
```

`mem_dump.bin` is 4,345,856 bytes: a `0x25000`-byte WRAM/TCM/palette/OAM prefix followed by 4 MiB of main RAM. Compare two captures with the workspace comparator:

```sh
python3 "$ROOT/scripts/compare_trace_ram.py" \
  "$ROOT/dolphin-test/reference/mem_dump.bin" \
  "$OUT/mem_dump.bin" \
  --output "$OUT/trace-diff.json"
```

That script verifies/parses the trace-v5 slots and assertion journal and reports byte-level differences. It does **not** decide architectural correctness or replace the on-screen group score. For interactive visual pages, use Dolphin’s GUI binary (`/usr/games/dolphin-emu`, not `-nogui`) with the same copied user profile/SD configuration. With Xvfb running on the chosen display (example `:98`), launch the browser DOL:

```sh
DISPLAY=:98 /usr/games/dolphin-emu -u "$DOLPHIN_USER" \
  -e "$ROOT/build-artifacts/NooDS-Wii-Agents2-5-RSCfix-JIT.dol"
```

Select `suite.nds` from `sd:/`, wait for completion, and capture the Dolphin window/root with `DISPLAY=:98 import -window root "$OUT/page.png"` while the display is active. Use the configured Wii input mapping to advance pages; in the Dust GUI, a brief Right-arrow pulse (about 0.4 s) advanced a results page here, but verify NooDS/Dolphin’s controller mapping before relying on the same key. Keep page-2/page-3 screenshots with the ROM hash and DOL hash.

For automated page capture, the instrumentation tree `/home/user/repo/test-runs/agents-fullram` has `G2_SCRIPTED_INPUT=1`. Its `autoboot.txt` parser supports `INPUT_RIGHT_FROM` / `INPUT_RIGHT_TO`, `INPUT_RIGHT2_FROM` / `INPUT_RIGHT2_TO`, and `INPUT_RIGHT3_FROM` / `INPUT_RIGHT3_TO`; the frame end is exclusive. Use a separate test SD image per page schedule. This is a test-only source snapshot: mirror/verify every emulator logic change before trusting its results, and never use it as the working base.

## 6. Comparison protocol and common traps

1. Use the same `suite.nds` SHA-256, emulator source revision, build options, and fresh SD image for reference and candidate runs.
2. Keep separate output directories, SD images, and Dolphin user profiles per run. Reusing an SD card can leave a prior `mem_dump.bin` or result log behind.
3. For the regular user-facing JIT DOL, leave `BROWSER=1` (the Makefile default for `JIT=1`). `BROWSER=0` is only for intentional ROM-autoboot regression tests.
4. Do not claim Dust passed all groups unless a fresh, hash-matched run visibly demonstrates it. The fresh stock run in this workspace was 25/27; the optional interlocks run was inconclusive.
5. Map each discrepancy to the exact subtest/input and compare output/flags/state before editing CPU semantics. Groups 15 and 20 are not yet mapped to their precise failing cases. Do not infer an SQRT64 fix solely from `long double` precision or an LDM/STM fix from the group label.
6. Compare the generated `mem_dump.bin`/trace artifacts and screenshots—not only Dolphin’s process exit code. External timeout termination after valid files were written is a known behavior.
7. The full-RAM and scripted-input DOLs under `build-artifacts` / `test-runs/agents-fullram` are diagnostic variants, not release/browser binaries. The working code remains Agents2-5.
8. Dust is GPL-3.0 and should remain a reference build; the local optional-interlocks typo patch is reference-only.

Useful artifacts: `COMPARISON-RESULTS.md` (JIT/full-RAM protocol), `scripts/compare_trace_ram.py`, `dolphin-test/agents-rscfix-jit-page2-screen.png`, `page3-screen.png`, `page4-screen.png`, `agents-rscfix-jit-page4-mem.bin`, and `dust-run/fresh-review/`.
