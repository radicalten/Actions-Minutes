# Workspace index and quick runbook — Agents2-5 speedup

Full setup/test handoff: `/home/user/repo/Agents2-5-NooDS-Wii-Speedup/HANDOFF.md` (also linked from `/home/user/repo/HANDOFF.md`).

## Project and deliverables

- Active speedup source/build folder: `/home/user/repo/Agents2-5-NooDS-Wii-Speedup/` (`Makefile`, `NooDS-Wii/`).
- User-facing browser DOL: `NooDS-Wii-Agents2-5-Speedup.dol` (O3, JIT blocks, browser mode, stats off).
- Diagnostic test DOL: `NooDS-Wii-Agents2-5-Speedup-Test.dol` (autoboot/full-RAM/script-input mode; not the user-facing DOL).
- Check DOLs using `SHA256SUMS.txt` in the speedup folder.
- Frozen correctness reference: `/home/user/repo/Agents2-5-NooDS-Wii/`. Do not edit it or use b4.
- Test ROM: `/home/user/repo/suite.nds`, SHA-256 `8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3`.

## Setup

On Debian/Ubuntu, install devkitPro pacman using the official [instructions](https://devkitpro.org/wiki/devkitPro_pacman), then run:

```sh
sudo dkp-pacman -Sy
sudo dkp-pacman -S wii-dev
source /etc/profile.d/devkit-env.sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH="$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH"
```

The recorded compiler is devkitPPC r51-1 / GCC 16.2.0. For the headless test command, install a distro package exposing `dolphin-emu-nogui`; the tested Debian setup is `sudo apt-get install dolphin-emu mtools dosfstools` (Dolphin `[master] 2503`).

## Build

```sh
cd /home/user/repo/Agents2-5-NooDS-Wii-Speedup
make clean-all
make -j"$(nproc)" JIT=1 BLOCKS=1 BROWSER=1 FULL_RAM_DUMP=0 G2_SCRIPTED_INPUT=0
cp NooDS-Wii_jit1_blocks1_browser1_o3_nostats.dol NooDS-Wii-Agents2-5-Speedup.dol
make -j"$(nproc)" JIT=1 BLOCKS=1 BROWSER=0 FULL_RAM_DUMP=1 \
  G2_SCRIPTED_INPUT=1 OPT_LEVEL=3 JIT_STATS=0
cp NooDS-Wii_jit1_blocks1_browser0_fullram_scriptinput_o3_nostats.dol \
  NooDS-Wii-Agents2-5-Speedup-Test.dol
sha256sum -c SHA256SUMS.txt
```

## Run and compare tests

Clone `/home/user/repo/dolphin-test/cpu-correctness-iter1/sd.raw` for each run, overwrite `suite.nds` and root `autoboot.txt` with the selected schedule (`autoboot-page2-650.txt` or `autoboot-page3-650.txt`), and remove stale guest captures. Run the diagnostic DOL with `/usr/games/dolphin-emu-nogui -p headless -v Null` and Wii SD enabled; the complete command and extraction steps are in the handoff. Timeout/wrapper status 137 is common after a valid frame-650 capture—check `g2_state.log` and dump sizes, not just exit status.

Acceptance gate: **25/27 groups, 0 unrun, exactly 128 combined ARM9/ARM9 Thumb failures, and ARM7-family 12,056/12,056 passing**. Keep page 2 and page 3 and their extra PASS/FAIL groups in scope. Compare RAM/trace dumps with `python3 /home/user/repo/scripts/compare_trace_ram.py BASELINE.mem CANDIDATE.mem --output diff.json`.

Clean O2/stats-on Dolphin baseline: **95,197 µs/frame** over three runs. Best O3/stats-off candidate: **86,052 µs/frame** over three runs (9.61% lower). These are suite-specific Dolphin results, not real-Wii guarantees. Detailed outcomes and caveats: `OPTIMIZATION-RESULTS.md` and `PERFORMANCE-BASELINE.md` in the speedup folder.
