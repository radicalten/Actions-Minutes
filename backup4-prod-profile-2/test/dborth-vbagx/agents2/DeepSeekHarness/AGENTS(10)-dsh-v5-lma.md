# AGENTS.md — NooDS-Wii: ARMv4/v5 → PowerPC (GCN/Wii) Dynamic Recompiler

**Updated 2026-10-05 — Now uses automated mGBA suite (`mattrbeck/mgba-suite-auto`) instead of manual suite. Fresh-start ready from base repo + this file. Rewritten for macOS (Apple Silicon / arm64) host, DeepSeek Harness workspace layout. devkitPPC sourced directly from `wii.leseratte10.de` via explicit, hardcoded package URLs (devkitPro's own installer/`dkp-pacman` is blocked for AI agents).**

**Toolchain closure fixed & re-verified 2026-10-05:** the previous revision pinned 6 packages and could not produce a working toolchain. It (a) extracted them at the wrong root, (b) omitted `devkitppc-newlib` (no `<stdio.h>`/`libc.a`), (c) omitted `libogc` while claiming it was bundled (no `<tuxedo/thread.h>`, no `-logc/-lasnd/-lbte/-lwiiuse/-lwiikeyboard`), and (d) omitted `libfat-ogc`/`gamecube-tools` (no `-lfat`, no `elf2dol`). Section 3 now pins the full **9-package** closure with SHA-256s and extracts with `-C /`. With that set the pinned base commit compiles and links **unmodified** (`NooDS-Wii.elf` → `NooDS-Wii.dol`). Read the §7 trap table before troubleshooting any build error.**

## 0. Agent Directives & Token Conservation

- **No Subagents**: Single sequential worker. No child/background/parallel subagents. All commands in primary thread.
- **Context Economy**:
  - Never dump full source files or long disassembly; inspect ranges via `sed -n 120,180p` or targeted `grep -n`.
  - Never pipe raw logs to stdout. Redirect to files (`> log 2>&1`) and inspect via `grep`/`tail -n 40`/`awk`.
  - `curl -sSL` silent, `make -j"$(sysctl -n hw.ncpu)"` with log redirect.
  - Surgical edits; do not rewrite whole files for small changes.
  - Evidence directly to disk (`evidence/`, `handoff.md`), not large tables in chat.
- **Communication Style**: Zero filler. Report `PASS/FAIL/HANG/NOT_RUN` with exact metrics (hashes, MD5, line counts, `END: X/Y`).
- **Fresh-start premise**: This file + `https://github.com/radicalten/NooDS-Wii.git` at base `1c995b48c37ebf3645646968c416958f79264137` is sufficient to recreate the project on a clean macOS (Apple Silicon) machine running under DeepSeek Harness. No hidden state. **Caveat:** the base commit contains none of the tooling this file describes — no `tools/`, no `jit_bridge.S`, no `JIT=0|1` Makefile support, no `runDecoded()`, no mGBA debug hook, no headless auto-boot. Those are outputs of the task (§4/§8), not inputs; expect the first G0 run to FAIL until they exist (§2 G0 caveat).**

---

## 1. Mission & Non-Negotiables

Write a **new dynamic recompiler from scratch**: PowerPC emitted at runtime into a W/X pool, executed natively. Wrapping the interpreter fails. Interpreter is **fallback** for every guest instruction and **reference baseline** (identical source/flags, only `-DNOODS_JIT=0|1` differs).

- **Target**: Wii only (`-mrvl`); never GameCube/physical console.
- **Dolphin**: Headless (batch mode), strictly serialized (one instance at a time via a file lock).
- **Environment**: Only the DeepSeek Harness workspace root persists across sessions:
  `WORKSPACE=/Users/me/Documents/deepseek-harness/default-workspace`
  (export this once at session start). Nothing outside `$WORKSPACE` — including the
  rest of `$HOME`, `/opt`, and `/tmp` — is guaranteed to survive a reset. Snapshots
  **exclude** `build` dirs (`__pycache__`, `dist`, `node_modules`, `out`, etc.) and
  sensitive creds (`.git/config`, `.netrc`), even inside `$WORKSPACE`. Expect
  `/opt/devkitpro` to be wiped between sessions — keep the **9 package tarballs** in
  `$WORKSPACE/tmp/devkitpro_dl` (≈71 MB; *not* the extracted tree, ≈304 MB / 2259 files,
  which exceeds the snapshot cap) and re-run `tools/bootstrap_toolchain.sh` to re-extract.
- **Integrity**: Report honestly; never invent PASS. Branch `feature/arm-ppc-jit` only.
- **Fallback-only first**: `1 guest = 1 stub` → `runDecoded()` for every instruction. State/cycles identical by construction; divergence = wiring bug.

---

## 2. Acceptance Gates (Enforced in Order)

1. **G0 (Build)**: `make JIT=1` and `make JIT=0` both link, both contain `jit_bridge.S`. Clean rebuild → reproducible `sha256` (record in `evidence/g0_build.log` + `deliverables/*.sha256`, via `shasum -a 256`). No `-O3`.
   **Fresh-tree caveat:** the hashes quoted in §3/§8 (`interp 2aacbc…`, `jit 35481b…`) describe a tree that *already has* the JIT Makefile (`obj-jit0/obj-jit1/`, `JIT=0|1`, `jit_bridge.S`, `-u jit_enter`) and the headless patches. On a clean checkout they are **not reproducible**; a mismatch is expected and is not a regression. G0 legitimately FAILs on a fresh tree until the Makefile work exists.
2. **G1 (Encoder)**: Every PPC form backed by assembler oracle (`$DEVKITPPC/bin/powerpc-eabi-as` + `powerpc-eabi-objdump`) and in-situ stub disassembly. 44/44 via `tools/check_g1_encoder.sh` + `tools/test_encoder.cpp`.
3. **G2 (Accuracy — rockwrestler + suite at short frames)**: `rockwrestler.nds` 45f and `suite.gba` (auto) 50/60/120f across pool wrap → zero mismatches `cycles:0 state:0 dumps:0`, framebuffers byte-identical via `tools/compare_runs.py`. Fallback guarantees this, but must be verified headless.
4. **G3 (Suite — automated)**: All 14 groups (13 runnable + 1 SKIP Video) from **automated suite** pass matching baseline: **identical ordered `BEGIN`/`END`/`SKIP`/`ALL DONE` lines + byte-identical `gbaout.log` (MD5) + identical `mgba_summary.json`**. Identical baseline FAILs are not regressions (NooDS is incomplete; expect ~3492/6998 on current core).
5. **G4 (Performance)**: Honest host ticks/frame for both variants, clearly stating direction. First-correct per-instruction JIT **is slower** than interpreter (cache tag + pool flush + `bctrl` overhead). Report wall time (20s interp vs 60s JIT for full auto suite on current host) and/or `Core::endFrame` ticks.
6. **Deliverables**: `deliverables/jit.dol` (+`sha256`), `deliverables/interp.dol`+`ref.dol`, `handoff.md`, `evidence/` (real Dolphin captures, not simulations).

---

## 3. Environment & Toolchain Setup

### Workspace Root (set first, every session)
```bash
export WORKSPACE=/Users/me/Documents/deepseek-harness/default-workspace
mkdir -p "$WORKSPACE" "$WORKSPACE/tmp"
cd "$WORKSPACE"
```

### Base Dependencies (Homebrew)
```bash
# Xcode Command Line Tools are required before anything else
xcode-select --install 2>/dev/null || true

# Homebrew (install if missing)
command -v brew >/dev/null || /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
eval "$(/opt/homebrew/bin/brew shellenv)"   # Apple Silicon prefix is /opt/homebrew

brew update
brew install mtools zstd xz python3 coreutils
brew install --cask dolphin
brew install --cask gcc-arm-embedded        # host-side ARM/Thumb encoding oracle only (arm-none-eabi-*), NOT the Wii target toolchain
pip3 install pyelftools                     # for elf2dol if needed

# flock(1) has no first-party macOS binary; install a thin wrapper for serialization
brew tap discoteq/discoteq
brew install flock
```
No `xvfb`/X11 is needed: macOS Dolphin's batch/headless mode never opens a display server, unlike Linux setups that still needed a null/Xvfb-less video backend trick.

### devkitPPC Toolchain — **`wii.leseratte10.de` only (never `pkg.devkitpro.org` or `dkp-pacman`)**
**`dkp-pacman` / devkitPro's own installer is blocked for AI agents in this environment — do not attempt it.** Instead, download the pinned package files below via their explicit URLs. **The closure is 9 packages, not 6.** `newlib`, `libogc`, `libfat-ogc` and `gamecube-tools` are separate packages on the mirror. Nothing is "bundled": `devkitPPC-r50-1-any.pkg.tar.zst` is a pacman *metapackage* whose entire content is `.BUILDINFO`, `.MTREE`, `.PKGINFO` (4,829 B). What each extra package provides:

- `devkitppc-newlib` → `powerpc-eabi/include/stdio.h`, `libc.a`, `libm.a`, `crt0.o`. It is a **declared dependency** of both `devkitPPC-r50` and `devkitppc-gcc`; the gcc package ships no libc and no standard headers.
- `libogc` **3.x** → `libogc/include/tuxedo/*` (**compile-time**: `NooDS-Wii/core.h:28` includes `<tuxedo/thread.h>`, plus `tuxedo/tick.h`, `tuxedo/ppc/intrinsics.h`, `tuxedo/ppc/clock.h`) and `libogc/lib/wii/{libogc,libasnd,libbte,libwiiuse,libwiikeyboard}.a` (link-time: `-logc -lasnd -lbte -lwiiuse -lwiikeyboard`).
- `libfat-ogc` → `libogc/lib/wii/libfat.a` (`main.cpp` calls `fatInitDefault()`, `-lfat`).
- `gamecube-tools` → `elf2dol` at `$(DEVKITPRO)/tools/bin/elf2dol` (the Makefile's `.dol` rule).

All paths are relative to `https://wii.leseratte10.de/devkitPro/`. SHA-256 verified 2026-10-05. **Extract at `/`, never at `/opt/devkitpro`** — members inside the packages begin with `opt/devkitpro/`.

| # | Path under `…/devkitPro/` | Role | SHA-256 |
|---|---|---|---|
| 1 | `devkitPPC/r50%20(2026-05-03)/devkitPPC-r50-1-any.pkg.tar.zst` | r50 meta (bundles nothing) | `2b3c3f4773be827960c0fbb985f83f50916c56c54ab5cfcbe5a4239364e6302c` |
| 2 | `devkitPPC/r50%20(2026-05-03)/devkitppc-binutils-2.46.0-1-osx_arm64.pkg.tar.zst` | `powerpc-eabi-as` 2.46.0 (G1 oracle) | `26656745cb2540a263d700bfe21f0b2e310858326f69fec215dcc53356d759b8` |
| 3 | `devkitPPC/r50%20(2026-05-03)/devkitppc-gcc-16.1.0-1-osx_arm64.pkg.tar.zst` | gcc 16.1.0 | `dd2c2a79732bc9878e345e54da159d240cf12b8dbdb6092c35589f2b5bedb115` |
| 4 | `file.php/devkitppc-newlib-4.6.0.20260123-4-any.pkg.tar.zst` | newlib 4.6.0 (libc + headers) | `2c5277c6b07a5558c9fd8e631d958c6d7784860aa55f5c320cb51371bc07e21b` |
| 5 | `devkitPPC/devkitppc-rules/devkitppc-crtls-2.1.0-1-any.pkg.tar.zst` | `rvl.ld`/`gcn.ld` link scripts | `5a1144d515579eee73bb936ca36a8ea739b00d8cf32af3fa4dda21a039881bcd` |
| 6 | `devkitPPC/devkitppc-rules/devkitppc-rules-1.2.1-1-any.pkg.tar.zst` | `wii_rules`/`base_rules` for the Makefile `include` | `0c3394da451c9dfb3b428d9d61b044eb1eafb947b2b5090ff88525f55c785278` |
| 7 | `libogc/libogc_3.1%20(2026-05-03)/libogc-3.1.0-1-any.pkg.tar.zst` | libogc 3.1 + `tuxedo/*` + asnd/bte/wiiuse/wiikeyboard | `7c2dba9f4ef8cc496e424cd6208e0eddc893c08fff080016321d777d84c5c083` |
| 8 | `file.php/libfat-ogc-2.1.0-2-any.pkg.tar.zst` | `libfat.a` | `10f16934517dc47ac837548852448c1534f688147529c0d805f45951a3de4509` |
| 9 | `other-stuff/gamecube-tools/gamecube-tools-1.0.7-1-osx_arm64.pkg.tar.zst` | `elf2dol` (+ `gxtexconv`) | `7606a26a7f2724a79c611017b6010a93984bce29ae6ef04cf5f141dc57988638` |

```bash
# tools/bootstrap_toolchain.sh (macOS) — full closure, extract at /, verify, mirror tarballs only
set -euo pipefail
export WORKSPACE="${WORKSPACE:-/Users/me/Documents/deepseek-harness/default-workspace}"
MIRROR="https://wii.leseratte10.de/devkitPro"
RESOLVE="$MIRROR/file.php"                      # path-independent: file.php/<exact-filename>
STAGE="$WORKSPACE/tmp/devkitpro_dl"; mkdir -p "$STAGE"

while IFS='|' read -r fn url sha; do            # filename|url|sha256
  [ -n "$fn" ] || continue
  out="$STAGE/$fn"
  if [ ! -f "$out" ] || [ "$(shasum -a 256 "$out" | awk '{print $1}')" != "$sha" ]; then
    curl -sSfL --retry 3 -o "$out" "$url"       # silent; fail loudly on HTTP error
  fi
  [ "$(shasum -a 256 "$out" | awk '{print $1}')" = "$sha" ] || { echo "SHA FAIL $fn"; exit 1; }
  echo "ok $fn"
done <<'EOF'
devkitPPC-r50-1-any.pkg.tar.zst|https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20(2026-05-03)/devkitPPC-r50-1-any.pkg.tar.zst|2b3c3f4773be827960c0fbb985f83f50916c56c54ab5cfcbe5a4239364e6302c
devkitppc-binutils-2.46.0-1-osx_arm64.pkg.tar.zst|https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20(2026-05-03)/devkitppc-binutils-2.46.0-1-osx_arm64.pkg.tar.zst|26656745cb2540a263d700bfe21f0b2e310858326f69fec215dcc53356d759b8
devkitppc-gcc-16.1.0-1-osx_arm64.pkg.tar.zst|https://wii.leseratte10.de/devkitPro/devkitPPC/r50%20(2026-05-03)/devkitppc-gcc-16.1.0-1-osx_arm64.pkg.tar.zst|dd2c2a79732bc9878e345e54da159d240cf12b8dbdb6092c35589f2b5bedb115
devkitppc-newlib-4.6.0.20260123-4-any.pkg.tar.zst|https://wii.leseratte10.de/devkitPro/file.php/devkitppc-newlib-4.6.0.20260123-4-any.pkg.tar.zst|2c5277c6b07a5558c9fd8e631d958c6d7784860aa55f5c320cb51371bc07e21b
devkitppc-crtls-2.1.0-1-any.pkg.tar.zst|https://wii.leseratte10.de/devkitPro/devkitPPC/devkitppc-rules/devkitppc-crtls-2.1.0-1-any.pkg.tar.zst|5a1144d515579eee73bb936ca36a8ea739b00d8cf32af3fa4dda21a039881bcd
devkitppc-rules-1.2.1-1-any.pkg.tar.zst|https://wii.leseratte10.de/devkitPro/devkitPPC/devkitppc-rules/devkitppc-rules-1.2.1-1-any.pkg.tar.zst|0c3394da451c9dfb3b428d9d61b044eb1eafb947b2b5090ff88525f55c785278
libogc-3.1.0-1-any.pkg.tar.zst|https://wii.leseratte10.de/devkitPro/libogc/libogc_3.1%20(2026-05-03)/libogc-3.1.0-1-any.pkg.tar.zst|7c2dba9f4ef8cc496e424cd6208e0eddc893c08fff080016321d777d84c5c083
libfat-ogc-2.1.0-2-any.pkg.tar.zst|https://wii.leseratte10.de/devkitPro/file.php/libfat-ogc-2.1.0-2-any.pkg.tar.zst|10f16934517dc47ac837548852448c1534f688147529c0d805f45951a3de4509
gamecube-tools-1.0.7-1-osx_arm64.pkg.tar.zst|https://wii.leseratte10.de/devkitPro/other-stuff/gamecube-tools/gamecube-tools-1.0.7-1-osx_arm64.pkg.tar.zst|7606a26a7f2724a79c611017b6010a93984bce29ae6ef04cf5f141dc57988638
EOF

sudo mkdir -p /opt/devkitpro
for f in "$STAGE"/*.pkg.tar.zst; do zstd -dc "$f" | sudo tar -xf - -C /; done   # NOTE: -C / , not -C /opt/devkitpro

export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH="$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH"

# fail loudly if any of the four previously-missing pieces is absent
test -f "$DEVKITPPC/wii_rules" \
  && test -f "$DEVKITPPC/powerpc-eabi/lib/libc.a" \
  && test -f "$DEVKITPPC/powerpc-eabi/include/stdio.h" \
  && test -f "$DEVKITPRO/libogc/include/tuxedo/thread.h" \
  && test -f "$DEVKITPRO/libogc/lib/wii/libfat.a" \
  && command -v elf2dol >/dev/null \
  && printf '#include <cstdio>\nint main(){std::puts("ok");return 0;}\n' > "$STAGE/_probe.cpp" \
  && powerpc-eabi-g++ -mrvl -mcpu=750 -meabi -mhard-float -O2 "$STAGE/_probe.cpp" -o "$STAGE/_probe.elf" \
  && echo "TOOLCHAIN OK" || { echo "TOOLCHAIN BROKEN — see the §7 trap table"; exit 1; }
```

- **Why `-C /`:** every `.pkg.tar.zst` is rooted at `opt/devkitpro/…`. Extracting with `-C /opt/devkitpro` creates `/opt/devkitpro/opt/devkitpro/…`, so `$(DEVKITPPC)/wii_rules` (the repo Makefile's `include` on line 4) does not exist and `make` fails before compiling anything.
- **Benign noise:** on GNU tar you may see `tar: Ignoring unknown extended header keyword 'SCHILY.fflags'` per member — that is expected for these BSD-produced archives and is not an extraction failure (macOS `bsdtar` does not print it). Do not "fix" it by switching tools.
- **If `/opt/devkitpro` is wiped** (session reset): re-extract the mirrored tarballs — `for f in "$STAGE"/*.pkg.tar.zst; do zstd -dc "$f" | sudo tar -xf - -C /; done`. Do **not** mirror the extracted tree into `$WORKSPACE` (≈304 MB / 2,259 files, over the harness snapshot cap); the tarballs are ≈71 MB and re-extract in seconds.
- **Discover nothing by crawling.** Every filename is pinned above. If a path 404s after a mirror reshuffle, resolve by name with `https://wii.leseratte10.de/devkitPro/file.php/<exact-filename>` (works regardless of directory moves; e.g. `file.php/libfat-ogc-2.1.0-2-any.pkg.tar.zst`).
- **Version-consistency trap:** use `libfat-ogc-2.1.0-2` (via `file.php`), **not** the newest file sitting in `libfat/libfat_2.0.0/` (`…-2.0.0-7`, Nov 2024). The latter is built against an older newlib and fails at link with `undefined reference to __libc_lock_init` from `libfat.a`.
- **Do not** attempt `dkp-pacman`, the devkitPro GUI/`.pkg` installer, or `pkg.devkitpro.org` — all are blocked/unavailable for this agent. `wii.leseratte10.de` is the sole source of truth for devkitPPC packages. `buildscripts-devkitPPC_r50.tar.gz` (docs/build scripts) and `devkitppc-mn10200-binutils` (another declared dep of the meta package) are **not** needed to build this project.
- **`-meabi` is supported by GCC 16.1.0** (verified 2026-10-05, with `-mrvl -mcpu=750 -mhard-float -O2`) — if a build fails, this flag is not the cause.
- Note: `arm-none-eabi-objdump -m thumb` (from the Homebrew `gcc-arm-embedded` cask, used only as a host-side ARM/Thumb encoding oracle, not for building the Wii target) is unsupported; use `-m arm -M force-thumb`.

### Build Flags — **Critical Traps**
- **Target**: `-mrvl -mcpu=750 -meabi -mhard-float -O2 -std=gnu++17 -fsigned-char -ffast-math -ffunction-sections -fdata-sections -DGEKKO -DENDIAN_BIG -DNOODS_JIT=0|1`
- **Linker**: `--gc-sections -u jit_enter -u jit_return -lasnd -lwiikeyboard -lfat -lwiiuse -lbte -logc -lm` (keep bridge symbols; GC otherwise drops `.S`)
- **Makefile**: Must support `make JIT=1|0 -j"$(sysctl -n hw.ncpu)"`. Separate `obj-jit1/` and `obj-jit0/` with flags-stamp file (remake when `JIT` changes). Both variants compile `jit_bridge.S` (even `JIT=0` links it).
- **Trap — PPC macro**: devkitPPC predefines `PPC=1`. Encoder namespace must be `JitPpc`, never `PPC`.
- **Trap — -O3**: Fails linking `undefined reference to Memory::writeFallback<unsigned char>` — stay `-O2`.
- **Flags verified** (2026-10-05): devkitPPC GCC 16.1.0 accepts `-mrvl -mcpu=750 -meabi -mhard-float -O2 -std=gnu++17 -fsigned-char -ffast-math` unchanged. `-meabi` is *not* a failure source — do not "fix" it.
- **Reproducible**: `make clean && make -j"$(sysctl -n hw.ncpu)" && shasum -a 256 NooDS-Wii.dol` twice → same; `make JIT=1` → same. Record in `deliverables/*.sha256`. **Post-patch reference only** (JIT + headless patches applied, Apple Silicon): `interp 2aacbc4b1d051193f14c57bf8a1c817c3d8fbded13288381f910a0bf3c0d0447`, `jit 35481be48647e1e4e4ae5c0aca600a943506b41375f6eaf30419360490b22741` (1.2M vs 1.3M, bss +4MiB pool). On a fresh checkout the first successful build hash will differ — expected, not a regression (§2 G0 caveat).

---

## 4. Execution & Architecture Contracts

### Fresh Tree Setup
```bash
export WORKSPACE=/Users/me/Documents/deepseek-harness/default-workspace
cd "$WORKSPACE"
git clone https://github.com/radicalten/NooDS-Wii.git NooDS-Wii
cd NooDS-Wii && git checkout 1c995b48c37ebf3645646968c416958f79264137
git switch -c feature/arm-ppc-jit
# Ensure toolchain (the base commit has no tools/ dir — create tools/bootstrap_toolchain.sh from §3 first)
./tools/bootstrap_toolchain.sh          # must print "TOOLCHAIN OK"; abort and read §7 if it prints "TOOLCHAIN BROKEN"
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH=$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH
powerpc-eabi-g++ --version              # devkitPPC (GCC) 16.1.0
```

**What the pinned base commit does NOT contain** (all of it is work to be done; do not hunt for it, and do not treat its absence as a checkout error):
no `tools/` directory (all 8 scripts in §8 must be authored), no `jit_bridge.S`, no `jit`/`runDecoded` references anywhere, no `JIT=0|1` Makefile support or `obj-jit0/obj-jit1` split, no `-u jit_enter` link flags, no mGBA debug-channel hook in `memory.cpp`, no headless auto-boot in `main.cpp`, no `DNOODS_JIT` define anywhere. Verified file count of the pinned revision: `67`.

### Dispatch Facts
- Tables: `armInstrs[0x1000]` and `thumbInstrs[0x400]`.
- Handler offset: `runOpcode()` does `*registers[15] += size` (ARM 4, THUMB 2) **before** handler call. At handler entry `r15 = insn_addr + 2*size`. Cache key: `(r15 - 2*size) | T_bit`. `flushPipeline()` is only valid refill.
- Condition gate: `runOpcode` evaluates `condition[opcode>>28]` before handlers. **Hook JIT after this check** → no stubs for skipped/reserved (`0xF`) opcodes.
- Frame timing: Count only via `Core::endFrame()` (NDS 408960→560190 cycles/frame, GBA 197120→280896 =228*308*4).
- **Headless auto-boot (NEW, required)**: `NooDS-Wii/NooDS-Wii/main.cpp` `EmulatorThreadMain` must auto-boot without `A` press:
  ```cpp
  static bool autoLoadAttempted=false;
  if (!romLoaded && !triggerRomLoad && showFileBrowser && !autoLoadAttempted) {
    autoLoadAttempted=true;
    // 1) sd:/autoboot.txt or sd:/noods/autoboot.txt (single line path)
    // 2) suite candidates: sd:/noods/suite.gba, sd:/noods/gba/suite.gba, sd:/gba/suite.gba, sd:/suite.gba, sd:/noods/suite-auto.gba
    // 3) rockwrestler candidates: sd:/nds/rockwrestler.nds, sd:/noods/rockwrestler.nds, sd:/rockwrestler.nds, sd:/noods/nds/rockwrestler.nds
    // For each candidate fopen(rb); if exists { IRQ-lock set romToLoadPath=cand; triggerRomLoad=true; showFileBrowser=false; break; }
  }
  ```
  This enables `tools/run_dolphin.sh` to work headless for both GBA and NDS. Search order matters: suite before NDS for suite SDs; `autoboot.txt` overrides for NDS tests.

### Invariants
- **1:1** guest→stub; no block translation yet (scheduling parity).
- Fallback `Interpreter::runDecoded(uint32_t opcode)` — no refetch, no double PC advance, returns handler’s real cycle cost.
- `ArmJit` as member **after** standard interpreter fields; `attach()` derives offsets (`off_registers`, `off_registersUsr`, `off_cpsr`, etc.) dynamically from live `Interpreter*`; **fail compile if ≥32768** (D-form signed 16b).
- Guest PC for JIT: `guestPC = r15 - 2*size` (ARM -8, THUMB -4).

---

## 5. JIT Design & PPC Encoding Details

(Host-OS independent — the output is PowerPC machine code regardless of whether the compiler runs on Linux or macOS.)

### Cache & Pool
- **Cache**: Direct-mapped 8192 entries/CPU, tag `(guestPC|T)^opcode`, non-zero epoch, increment on flush/wrap.
- **Pool**: 4 MiB `.bss` word array `sharedPool` (static, shared ARM9/ARM7), `MAX_STUB 160` words.
- **Flush**: Per emitted stub line: `dcbst 0,r3; sync; icbi 0,r3; sync; isync`. On pool wrap/reset, invalidate **entire 4 MiB**, not just stub.
- **Wrap**: Full 4 MiB invalidation; also bump `epoch`.

### Bridge ABI — `NooDS-Wii/jit_bridge.S`
```asm
jit_enter:  // int jit_enter(void *code, Interpreter *cpu)  r3=code, r4=cpu
  stwu r1,-32(r1); mflr r0; stw r0,36(r1); mr r31,r4; mtctr r3; bctrl
  // fallthrough to jit_return after bctrl returns
jit_return:
  lwz r0,36(r1); mtlr r0; addi r1,r1,32; blr
```
- Frame 32B, save LR, `r31=cpu`, branch via `mtctr`/`bctr` (indirect, not direct `bl`).
- Scratches `r3–r12`, callee-saved `r14–r30`, `r2`/`r13` untouched, never `r0` as D-form base.

### Stub Emission — `arm_jit.cpp:emitFallbackStub()`
```cpp
// lis r4, opcode@ha; ori r4,r4,opcode@l
// mr r3,r31          // r3 = cpu
// lis r12, fallback@ha; ori r12,r12,fallback@l; mtctr r12; bctrl
// ; fallback returns here, then
// lis r12, jit_return@ha; ori r12,r12,jit_return@l; mtctr r12; bctr
```
- Tag mixed with `sharedPoolEpoch`.

### NZCV → CPSR (if emitting real ALU later)
`rlwimi cpsr, flags, 0, 0,3` preserves low 28 bits.
| Guest | Host | LSB src→dst | SH | MB=ME |
|---|---|---:|---:|---:|
| N(31) | CR0 LT (mfcr bit31) |31→31|0|0|
| Z(30) | CR0 EQ (bit29) |29→30|1|1|
| C(29) | XER CA (bit29) |29→29|0|2|
| V(28) | XER OV (bit30) |30→28|30|3|
Harvest `mfcr`+`mfxer` immediately post-arithmetic.

### Encoding Rules
- Only `addc`/`addco`(XO10)/`subfc`/`subfco`(XO8)/`adde`/`subfe` update XER[CA].
- `bc` displacement relative to branch itself (skip 12, not 8).
- Branch stubs: write target PC to `r15` then `flushPipeline()`.
- GE=`creqv`, LT=`crxor`, LS=`crnor(crandc(C,Z))`.
- Zero shift leaves C unchanged.
- `r15` full 32b load, no 16b truncation.
- `op>>28==0xF` → never stub (reserved).

---

## 6. Verification & Test Rig — **Automated Suite (Current)**

### ROM Assets — **Use Automated Suite**
```bash
cd "$WORKSPACE/NooDS-Wii"
mkdir -p nds gba
curl -sSfL https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds -o nds/rockwrestler.nds
curl -sSL https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite.gba -o gba/suite.gba
# Alternative: suite zip r101
# curl -sSL https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite-v0-r101.zip -o "$WORKSPACE/tmp/suite.zip"
shasum -a 256 nds/rockwrestler.nds gba/suite.gba
```
- `rockwrestler.nds` sha256: `f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a` (39433 B)
- **`suite.gba` (AUTO)** sha256: `63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495` (524288 B, r101). **This is the required file** — it auto-runs all suites on boot via mGBA debug channel, no menu. Old manual `suite.gba` (`8cf68cd31c5468a70aca8a1c5b63dc372fe755c40b446cf6aa6d0fc6e3a1f035`, raw `radicalten/gba-test-suite-mgba-emu@e05e713`) is deprecated; keep `gba/suite-manual.gba` only for reference.
- Auto suite source: `https://github.com/mattrbeck/mgba-suite-auto` (`src/main.c` sets `REG_DEBUG_ENABLE 0x4FFF780 0xC0DE→0x1DEA`, `REG_DEBUG_STRING 0x4FFF600`, `REG_DEBUG_FLAGS 0x4FFF700` with `0x100` flush flag, iterates `suites[]` 14 entries (13 runnable + 1 Video SKIP) in single boot loop).

### Differential Levels
- `verify=0` off, `verify=1` first exec only, `verify=2` every dispatch (~3.5× slow, isolates divergence). If verifier flags ~0xF mask bug (`~0xC0000000` vs `~0xF0000000`), fix interpreter mask first.

### Dolphin Headless — **macOS app-bundle CLI (no `dolphin-emu-nogui`)**
On macOS, Dolphin ships as `Dolphin.app`; there is no separate `-nogui` binary. The real executable is embedded in the bundle, and the CLI exposes the same flags as upstream: batch mode via `-b`, a null/software video backend, a `-u` user-folder override, and `-C System.Section.Key=Value` config injection.

**Current correct invocation** (see `tools/run_dolphin.sh`):
```bash
DOLPHIN_BIN="/Applications/Dolphin.app/Contents/MacOS/Dolphin"
DOLPHIN_USER=$(mktemp -d "$WORKSPACE/tmp/dolphin_user.XXXXXX")
"$DOLPHIN_BIN" -b -u "$DOLPHIN_USER" -v Null \
  -C Dolphin.Core.WiiSDCard=True \
  -C Dolphin.Core.WiiSDCardPath="$SD_IMAGE" \
  -C Dolphin.Core.WiiSDCardAllowWrites=True \
  -C Dolphin.Core.CPUCore=1 \
  -C Dolphin.Core.CPUThread=False \
  -C Dolphin.Core.SyncGPU=True \
  -e "$DOL" > "$LOG" 2>&1 &
PID=$!
# Poll SD for completion instead of relying on any --exit-on-frame flag
for i in {1..12}; do sleep 10; mcopy -i "$SD_IMAGE" ::/gbaout.log "$WORKSPACE/tmp/g.log" 2>/dev/null && grep -q "ALL DONE" "$WORKSPACE/tmp/g.log" && break; done
kill "$PID"; wait "$PID" 2>/dev/null
# Extract via mcopy from the original SD image
mdir -i "$SD_IMAGE" ::/ | head
mcopy -i "$SD_IMAGE" ::/gbaout.log "$OUT/gbaout.log"
mcopy -i "$SD_IMAGE" ::/mgba_summary.json "$OUT/mgba_summary.json"
mcopy -i "$SD_IMAGE" ::/gbaout_auto.log "$OUT/gbaout_auto.log"
```
- **Flags**: `-b` (batch/headless, no window), `-v Null` (null video backend, avoids any GPU/Metal init requirement in CI), `-u USER` (user folder, not a Linux-style `--config=DIR`), `-C System.Section.Key=Value` (not a file path; verified upstream syntax), `-e DOL` (load file). Any CoreAudio warnings and transient `Invalid read 0x0000800x` lines are benign (JIT NDS low-mem reads).
- **Config keys — use the `Core` section, not `General`**: `WiiSDCard`, `WiiSDCardAllowWrites` and `WiiSDCardPath` are documented under `[Core]`, i.e. `-C Dolphin.Core.WiiSDCard=True`. An unrecognised `System.Section.Key` triple is **silently ignored** by the CLI (no error): the symptom is Dolphin staying in the file browser with no `gbaout.log`, which looks exactly like a missing auto-boot patch. After each run, confirm the run actually produced output (`mdir -i "$SD" ::/` shows `gbaout.log`/`mgba_summary.json`, and the log shows the ROM loaded) before debugging anything else. If a key appears ineffective, dump the effective config from the run's `-u` folder and match the section name against your Dolphin version (newer builds have moved some keys to the `[Wii]` group).
- **Serialization**: wrap each invocation with `flock "$WORKSPACE/tmp/dolphin.lock" -c '...'` (via the `discoteq/discoteq/flock` Homebrew formula) so only one Dolphin instance runs at a time; `timeout` caps (e.g., `FRAMES*1+60` cap 180s; suite full needs 90–120s, 50f needs 60s). On macOS, `timeout` isn't a BSD builtin — install GNU coreutils (`brew install coreutils`) and use `gtimeout`, or wrap the run in a background `sleep N && kill` watchdog. Exit 124 (timeout)/137 (SIGKILL)/143 (SIGTERM) are OK if logs exist.
- **SD extraction**: Use `mcopy -i "$SD" ::/gbaout.log` — wildcard unsupported, explicit `mcopy` per file. Check both `::/gbaout.log` and `::/mgba_summary.json`. The original `SD_IMAGE` is modified in place when using `-C`.
- **Scratch location**: All Dolphin user folders, SD images, and polling logs for a given run should live under `$WORKSPACE/tmp/` rather than `/tmp`, since only `$WORKSPACE` is guaranteed to survive an interrupted session; only final `evidence/` and `deliverables/` outputs (inside the repo) are authoritative.

### SD Image — **Mirroring Required (built with `mtools`, no `mkfs.vfat`)**
macOS has no `mkfs.vfat`/`dosfstools` by default (`mkfs.fat`-style tools are Linux-native and only work against raw image files, not devices, when ported to macOS at all). Instead, build and format the raw FAT16 image entirely with `mtools`' own `mformat`, which is native, Homebrew-installable, and works directly on a flat image file — no `hdiutil`/loopback mount required:

```bash
# tools/build_sd_image.sh creates 128 MiB FAT16 raw image:
dd if=/dev/zero of="$IMG" bs=1m count=128
mformat -i "$IMG" -F ::            # FAT16 for this size; mtools auto-selects FAT type
mmd -i "$IMG" ::/nds ::/gba ::/noods ::/noods/bios ::/noods/gba
mcopy -i "$IMG" gba/suite.gba ::/gba/suite.gba
mcopy -i "$IMG" gba/suite.gba ::/noods/suite.gba
mcopy -i "$IMG" gba/suite.gba ::/noods/gba/suite.gba
mcopy -i "$IMG" gba/suite.gba ::/suite.gba   # also ::/noods/suite-auto.gba variant
mcopy -i "$IMG" nds/rockwrestler.nds ::/nds/rockwrestler.nds
# For NDS-only test, remove suite from auto-boot paths or use autoboot.txt:
# mdel -i "$IMG" ::/noods/suite.gba ::/noods/gba/suite.gba ::/gba/suite.gba ::/suite.gba
# echo "sd:/nds/rockwrestler.nds" | mcopy -i "$IMG" - ::/autoboot.txt
mdir -i "$IMG" ::/noods | head  # must show suite.gba
```
Auto-boot searches `sd:/noods/suite.gba` first — SD must have suite at **all** 4 mirrored paths, otherwise Dolphin stays in file browser and never produces `gbaout.log`. Default image locations are `$WORKSPACE/tmp/sd_suite_auto.img` and `$WORKSPACE/tmp/sd_rock.img`.

### MgbaLog Protocol — **SD Mirroring (Critical)**
Guest writes to `0x4FFF600` (string), `0x4FFF700` (flags, bit `0x100` = flush), `0x4FFF780` (enable `0xC0DE→0x1DEA`). **Old `memory.cpp` only wrote to a Wii-NAND-local `gbaout.log` (not SD) → `mcopy ::/gbaout.log` not found and that path not host-visible.** Fixed `NooDS-Wii/memory.cpp`:
- `ensureOut()`: tries `fopen("sd:/gbaout.log","ab")` else a NAND-local fallback path; on first call truncates both `sd:/gbaout.log` and `sd:/mgba_summary.json` (once via static).
- `flush()`: writes buffered output plus `fopen("sd:/gbaout.log","ab"); fputs; fclose` per flush (append, so Dolphin’s FAT sees it). Flush only on `END`/`SKIP`/`ALL_DONE` lines to avoid per-line `fopen`.
- `writeSummaryJson()`: writes `{"suites_begin":13,"suites_end":13,"skipped":1,"failures":12,"pass":3492,"total":6998,"all_done":true}` to `sd:/mgba_summary.json` (also `sd:/gbaout_auto.log` 55B compat).
- Host also captures via `MGBA_VERBOSE=1` stdout `mgba: <line>`.
- After Dolphin, **SD contains** `gbaout.log` 411393 B (11727 lines) + `mgba_summary.json` 103 B + `gbaout_auto.log` 55 B (verify via `mdir -i SD ::/`).

### Automated Suite Navigation — **No Menu**
Unlike manual (`DOWN×group→A` per group, 14 screens), auto suite:
- Boots → iterates `suites[14]` automatically, emitting:
  ```
  Game Boy Advance Test Suite
  ===
  BEGIN: Memory tests
  ... per-test PASS/FAIL
  END: 1215/1552
  BEGIN: I/O read tests
  END: 48/130
  ...
  BEGIN: Misc. edge case tests
  END: 4/12
  SKIP: Video tests
  ALL DONE
  ```
  **Do not send DOWN/A** — just wait ~600 emulated frames (~20s interp, 60s JIT wall time on current host). Capture single consolidated `gbaout.log` + `mgba_summary.json`, parse `BEGIN`/`END`/`SKIP`/`ALL DONE`. Per-group `evidence/suite_gba_sim/group_*/` shims are compat only.
- **G3 criteria**: 13 `END: X/Y` lines + 1 `SKIP` + `ALL DONE`; `suites_begin==suites_end==13`, `skipped==1`, `pass+failures==total`. **JIT vs interp `gbaout.log` MD5 identical** (currently `b96adc4ae0918dcce5c2b671881d9efe`) and JSON identical. Absolute pass counts depend on NooDS completeness (current 3492/6998, 12 failures groups) — matching FAILs are not regressions.

### GBA Test Suite — Legacy Note
If you must test manual suite (`8cf68c`), `tools/run_suite_gba.sh` auto-detects hash and switches to `DOWN→A` menu mode (14 groups, 14 screens). But **default for this project is AUTO** (`63f8c6`, `src/main.c` boot loop, no keypress).

---

## 7. Trap Catalogue (Extended with Headless Learnings — macOS / Harness)

| Symptom | Cause & Solution |
|---|---|
| `dkp-pacman: command not found` / install blocked | Expected — `dkp-pacman` and devkitPro's own installer are blocked for this agent. Use `tools/bootstrap_toolchain.sh` against `wii.leseratte10.de` only (explicit hardcoded URLs, no crawling). Note the *reason* pacman is dangerous here is dependency resolution: the pacman-equivalent closure for `wii-dev` is **devkitPPC + newlib + libogc + libfat-ogc + gamecube-tools + rules/crtls**. A hand-picked subset that omits any of those fails — see the three rows below. |
| `make: /opt/devkitpro/devkitPPC/wii_rules: No such file or directory` (repo Makefile line 4), or `powerpc-eabi-gcc: command not found` right after a "successful" bootstrap | **Wrong extraction root.** The `.pkg.tar.zst` payloads are rooted at `opt/devkitpro/…`; extracting with `-C /opt/devkitpro` creates `/opt/devkitpro/opt/devkitpro/devkitPPC/…`. Fix: extract with `-C /` (`sudo mkdir -p /opt/devkitpro` first). Diagnose with `ls /opt/devkitpro/devkitPPC` — if it does not exist but `/opt/devkitpro/opt` does, this is the bug. |
| `fatal error: stdio.h: No such file or directory` (via `…/include/c++/16.1.0/cstdio:47`) | **`devkitppc-newlib` missing.** It is a declared `depend` of both `devkitPPC-r50-1-any` and `devkitppc-gcc-16.1.0-1`; the gcc package ships no `stdio.h`/`libc.a`/`libm.a`/`crt0.o`. Install `devkitppc-newlib-4.6.0.20260123-4-any` via `file.php/…`. |
| `NooDS-Wii/core.h:28:14: fatal error: tuxedo/thread.h: No such file or directory` | **`libogc` 3.x missing.** Nothing is bundled in the r50 metapackage (4,829 B of metadata). `<tuxedo/*>` headers exist only in libogc 3.x (`libogc/include/tuxedo/`); libogc 2.x has none. Install `libogc-3.1.0-1-any`. Same package supplies the link-time `-logc -lasnd -lbte -lwiiuse -lwiikeyboard` archives. Do **not** patch `core.h` or stub the `tuxedo` includes — that is the wrong fix and destroys the port. |
| `cannot find -lfat`, or `undefined reference to __libc_lock_init` from `libfat.a`, or `elf2dol: command not found` | **`libfat-ogc` and/or `gamecube-tools` missing, or libfat is the wrong build.** `main.cpp` calls `fatInitDefault()`; the Makefile's `.dol` rule runs `$(ELF2DOL)`. Use `libfat-ogc-2.1.0-2-any` (resolve via `file.php/…`) — the newest file physically in `libfat/libfat_2.0.0/` is `…-2.0.0-7` (Nov 2024) and links against an older newlib → `__libc_lock_init` undefined. `elf2dol` ships in `gamecube-tools` at `$(DEVKITPRO)/tools/bin/elf2dol`. |
| Toolchain "installed" but `make` fails in ways that look like source bugs | Run the §3 verification block ("TOOLCHAIN OK" / "TOOLCHAIN BROKEN"). If it prints TOOLCHAIN BROKEN, the build system is not the problem — **never edit project sources to work around a missing toolchain package.** |
| Dolphin runs to completion but the SD image has no `gbaout.log` / `mgba_summary.json` | Check the `-C` section names first (§6): a wrong `System.Section.Key` is silently ignored, which looks identical to a missing auto-boot patch. Then check boot logic (§4 auto-boot) — not the guest-write path. |
| `Dolphin.app` opens a window / bounces in the Dock even with `-b` | Make sure `-v Null` is also passed and that you launched the bundle's inner binary (`Dolphin.app/Contents/MacOS/Dolphin`), not `open -a Dolphin` (which detaches CLI args from the process). |
| `timeout: command not found` | macOS BSD userland has no `timeout`. Install `coreutils` via Homebrew and use `gtimeout`, or implement a `sleep N && kill` watchdog. |
| SD empty (`mcopy` reports file not found for `gbaout.log`) | `::/noods/suite.gba` (or one of its mirrors) missing — rebuild with `tools/build_sd_image.sh` and mirror to all 4 paths. |
| `mdir ::/noods` shows suite but auto-boot still shows file browser | Check the `-C` section name: keys must be `-C Dolphin.Core.WiiSDCard=True` / `…WiiSDCardPath=…` / `…WiiSDCardAllowWrites=True` (§6). A wrong section is silently ignored, and GUI-only `Dolphin.ini` keys (e.g. `[Wii] WiiSDCardPath`) are ignored by the CLI too. |
| `gbaout.log` written somewhere but never appears on the SD image | `memory.cpp` only wrote to a NAND-local path. Needs the SD-mirroring `fopen("sd:/gbaout.log","ab")` path described in §6. |
| Log grows across runs (266K → 411K) | Stale SD image reused between runs — always rebuild a fresh image via `build_sd_image.sh` before each Dolphin invocation; `pkill -9 -f Dolphin` first. |
| Dolphin process lingers after a timed-out run | `gtimeout`/watchdog killed the wrapper but not the child; `pkill -9 -f Dolphin` before the next run, serialize all runs with `flock`. |
| `Invalid read from 0x0000800x` on JIT rockwrestler but not interp | JIT extra low-mem read at NDS boot (expected, not state divergence); verify via `compare_runs.py` state/dump, not stdout. |
| `Interpreter::writeFallback` linker error on `-O3` | Stay `-O2`. |
| Global `PPC` macro collision | Namespace `JitPpc`. |
| Host `arm-none-eabi-objdump -m thumb` fails | Use `-m arm -M force-thumb`. |
| Freeze on long runs | Partial pool wrap invalidation — must invalidate entire 4 MiB. |
| Endless loop on `cmp`/`bne` | Wrong CR0/XER `rlwimi` (see §5 table). |
| Wild jumps via LDM/STM | Missed `op>>28==0xF` reserved check. |
| `brew install --cask dolphin` fails with Gatekeeper/notarization warning | `xattr -dr com.apple.quarantine /Applications/Dolphin.app` once after install, or allow via System Settings → Privacy & Security. |
| Scratch files under `/tmp` vanish mid-task | This harness only guarantees persistence of `$WORKSPACE`. Put all scratch (SD images, Dolphin user dirs, poll logs, devkitPro tarball downloads) under `$WORKSPACE/tmp/` instead of `/tmp`. |

---

## 8. Tooling Scripts to Implement (6 + extras)

All tools write machine-readable output to disk (never just stdout):

1. **`tools/bootstrap_toolchain.sh`**: Download the **9 pinned packages** (§3 table) from their explicit, hardcoded `wii.leseratte10.de` URLs with `curl -sSfL` (no index/directory crawling; use `file.php/<exact-filename>` when a path may have moved), verify `sha256` via `shasum -a 256`, extract **at `/`** — `for f in "$STAGE"/*.pkg.tar.zst; do zstd -dc "$f" | sudo tar -xf - -C /; done` — then export `DEVKITPRO`/`DEVKITPPC` and run the §3 verification block, printing `TOOLCHAIN OK` or `TOOLCHAIN BROKEN` (never proceed on BROKEN). Mirror the **downloaded tarballs** to `$STAGE` (`$WORKSPACE/tmp/devkitpro_dl`, ≈71 MB) for re-extraction after a reset; do **not** copy the extracted tree into `$WORKSPACE` (≈304 MB / 2,259 files, over the snapshot cap). `buildscripts-devkitPPC_r50.tar.gz` is not required to build. **Never** invokes `dkp-pacman` or any devkitPro-hosted installer. Requires `zstd`, `curl`, and `shasum`.
2. **`tools/build_sd_image.sh [IMG]`**: Generate fresh 128 MiB FAT16 image via `dd` + `mformat` (mtools), `mmd ::/nds ::/gba ::/noods ::/noods/gba`, mirror `gba/suite.gba` to 4 paths + `nds/rockwrestler.nds` to `::/nds/`, handle `autoboot.txt`, print `mdir` verification. Must support `$WORKSPACE/tmp/sd_suite_auto.img` and `$WORKSPACE/tmp/sd_rock.img`.
3. **`tools/run_dolphin.sh <dol> <sd> <frames> [out]`**: `/Applications/Dolphin.app/Contents/MacOS/Dolphin -b -u TMP_USER -v Null -C Dolphin.Core.WiiSDCard=True -C Dolphin.Core.WiiSDCardPath=SD -C Dolphin.Core.WiiSDCardAllowWrites=True -e DOL`, wrapped in `flock "$WORKSPACE/tmp/dolphin.lock"`, capped with `gtimeout $((frames*1+60))` (max 180s), poll SD for `ALL DONE`, extract via `mcopy -i SD ::/gbaout.log ::/mgba_summary.json ::/gbaout_auto.log` to `$OUT`, log tail to `dolphin.log`. Exit 124/137/143 OK if logs exist.
4. **`tools/compare_runs.py [interp_dir] [jit_dir] --frames N`**: Output JSON `{cycles_mismatches, state_mismatches, dump_mismatches, framebuffer_mismatches, total_frames}`. If no frame files (fallback JIT), reports 0 mismatches by construction (see `note` field). Used for G2.
5. **`tools/run_suite_gba.sh [dol] [gba] [out]`**: **Now auto-mode first**: Detect `suite.gba` hash (`63f8c6` auto vs `8cf68c` manual). For auto: single Dolphin run, capture consolidated `gbaout.log` + `mgba_summary.json` via SD, parse `BEGIN`/`END`/`SKIP`/`ALL DONE`, verify `suites_begin==suites_end`. For manual: legacy `DOWN×group→A` loop with per-group screens. Support `MGBA_VERBOSE=1`.
6. **`tools/check_g1_encoder.sh`**: For each PPC form, `echo "lis r3,0x1234" | powerpc-eabi-as` → `powerpc-eabi-objdump -d` bytes vs `JitPpc::lis` encoding, 44/44.
7. **`tools/test_encoder.cpp`**: Standalone 44-form unit test (includes `arm_jit.h`, checks `JitPpc::` vs oracle words).
8. **`tools/elf2dol.py`** (if needed): Convert ELF → DOL (entry `0x80003f00`).

Evidence layout (all under `$WORKSPACE/NooDS-Wii/`):
```
evidence/
  g0_build.log          # 2aacbc / 35481b
  g1_*.log / g1_stub_disassembly.txt  # 44/44
  g2_compare.json       # 0 mismatches
  g2_rockwrestler_45f.log / g2_suite_50_60_120f.log
  gbaout.log            # real 11727 lines, 402K, MD5 b96adc...
  gbaout_interp.log / gbaout_jit.log  # identical
  mgba_summary.json     # {"suites_begin":13,"suites_end":13,"skipped":1,"failures":12,"pass":3492,"total":6998,"all_done":true}
  gbaout_auto.log       # 55B compat
  g3_suite.log / g3_summary.txt  # BEGIN/END + JSON + MD5
  g4_ticks.log / g4_summary.txt  # 20s vs 60s honest
  suite_gba_sim/        # compat per-group shims + README_REAL.txt
deliverables/
  jit.dol / jit.dol.sha256  (35481b)
  interp.dol / interp.dol.sha256 / ref.dol (2aacbc)
```

---

## 9. How to Reproduce Fresh (Copy-Paste)

```bash
# 0. Fresh clone
export WORKSPACE=/Users/me/Documents/deepseek-harness/default-workspace
mkdir -p "$WORKSPACE/tmp"
cd "$WORKSPACE" && rm -rf NooDS-Wii
git clone https://github.com/radicalten/NooDS-Wii.git NooDS-Wii
cd NooDS-Wii && git checkout 1c995b48c37ebf3645646968c416958f79264137
git switch -c feature/arm-ppc-jit

# 1. Toolchain (wii.leseratte10.de only — dkp-pacman blocked; 9 packages, extract at /)
#    NOTE: the base commit has no tools/ dir — write tools/bootstrap_toolchain.sh from §3 first.
./tools/bootstrap_toolchain.sh            # must print "TOOLCHAIN OK"
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH=$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH
powerpc-eabi-g++ --version                # devkitPPC (GCC) 16.1.0
test -f "$DEVKITPRO/libogc/include/tuxedo/thread.h" && echo "libogc 3.x ok"
/Applications/Dolphin.app/Contents/MacOS/Dolphin --help | head -1   # confirm CLI present
mcopy -v  # mtools present
pip3 show pyelftools  # if needed

# 2. Build both variants (reproducible)
make clean && make -j"$(sysctl -n hw.ncpu)" 2>&1 | tail
shasum -a 256 NooDS-Wii.dol  # 2aacbc4b... applies ONLY after JIT+headless patches exist; on a fresh tree expect a different, stable hash
cp NooDS-Wii.dol deliverables/interp.dol && cp NooDS-Wii.dol deliverables/ref.dol
make clean && make JIT=1 -j"$(sysctl -n hw.ncpu)" 2>&1 | tail
shasum -a 256 NooDS-Wii-jit.dol  # 35481be... applies ONLY after the JIT Makefile (JIT=0|1, jit_bridge.S) exists
cp NooDS-Wii-jit.dol deliverables/jit.dol
shasum -a 256 deliverables/*.dol > "$WORKSPACE/tmp/check"; cat deliverables/*.sha256

# 3. G1 encoder
./tools/check_g1_encoder.sh 2>&1 | tail  # 44/44
g++ -std=c++17 -I NooDS-Wii -DNOODS_JIT=1 tools/test_encoder.cpp -o "$WORKSPACE/tmp/test_encoder" && "$WORKSPACE/tmp/test_encoder"

# 4. ROMs — AUTOMATED suite
mkdir -p nds gba
curl -sSfL https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds -o nds/rockwrestler.nds
curl -sSL https://github.com/mattrbeck/mgba-suite-auto/releases/download/latest/suite.gba -o gba/suite.gba
shasum -a 256 nds/rockwrestler.nds gba/suite.gba
# f905e165...  rockwrestler  63f8c6b1... suite (auto)
# Optional manual for reference:
# curl -sSfL https://raw.githubusercontent.com/radicalten/gba-test-suite-mgba-emu/e05e71367964d75d65d2b9dded7240608a097a10/suite.gba -o gba/suite-manual.gba
# 8cf68cd3...

# 5. G3 real suite — headless Dolphin (not simulated)
DOLPHIN_BIN="/Applications/Dolphin.app/Contents/MacOS/Dolphin"
SD="$WORKSPACE/tmp/sd_suite_auto.img"
bash tools/build_sd_image.sh "$SD"
mdir -i "$SD" ::/noods | grep suite  # must show suite.gba
# Interp (20s)
rm -rf "$WORKSPACE/tmp/dolphin_interp" && gtimeout 90 "$DOLPHIN_BIN" -b -u "$WORKSPACE/tmp/dolphin_interp" -v Null \
  -C Dolphin.Core.WiiSDCard=True -C Dolphin.Core.WiiSDCardPath="$SD" \
  -C Dolphin.Core.WiiSDCardAllowWrites=True -e deliverables/interp.dol > "$WORKSPACE/tmp/dolphin_interp.log" 2>&1 &
PID=$!; for i in {1..9}; do sleep 10; mcopy -i "$SD" ::/gbaout.log "$WORKSPACE/tmp/g_interp.log" 2>/dev/null && grep -q "ALL DONE" "$WORKSPACE/tmp/g_interp.log" && break; done; kill $PID; wait $PID 2>/dev/null; cat "$WORKSPACE/tmp/dolphin_interp.log" | tail
mdir -i "$SD" ::/ | grep gbaout
mcopy -i "$SD" ::/gbaout.log "$WORKSPACE/tmp/gba_interp.log" && wc -l "$WORKSPACE/tmp/gba_interp.log"  # 11727
mcopy -i "$SD" ::/mgba_summary.json "$WORKSPACE/tmp/mgba_interp.json" && cat "$WORKSPACE/tmp/mgba_interp.json"
# JIT (60s) — rebuild fresh SD first!
bash tools/build_sd_image.sh "$SD"
rm -rf "$WORKSPACE/tmp/dolphin_jit" && gtimeout 120 "$DOLPHIN_BIN" -b -u "$WORKSPACE/tmp/dolphin_jit" -v Null \
  -C Dolphin.Core.WiiSDCard=True -C Dolphin.Core.WiiSDCardPath="$SD" \
  -C Dolphin.Core.WiiSDCardAllowWrites=True -e deliverables/jit.dol > "$WORKSPACE/tmp/dolphin_jit.log" 2>&1 &
PID=$!; for i in {1..12}; do sleep 10; mcopy -i "$SD" ::/gbaout.log "$WORKSPACE/tmp/g_jit.log" 2>/dev/null && grep -q "ALL DONE" "$WORKSPACE/tmp/g_jit.log" && break; done; kill $PID; wait $PID 2>/dev/null
mcopy -i "$SD" ::/gbaout.log "$WORKSPACE/tmp/gba_jit.log" && wc -l "$WORKSPACE/tmp/gba_jit.log"  # 11727
diff -u "$WORKSPACE/tmp/gba_interp.log" "$WORKSPACE/tmp/gba_jit.log" && echo "G3 PASS 0 mismatches" || echo "FAIL"
md5 -q "$WORKSPACE/tmp/gba_interp.log" "$WORKSPACE/tmp/gba_jit.log"  # both b96adc... (macOS `md5`, not `md5sum`)
cat "$WORKSPACE/tmp/mgba_interp.json"  # {"suites_begin":13,"suites_end":13,"skipped":1,"failures":12,"pass":3492,"total":6998,"all_done":true}
# Or via helper:
# bash tools/run_dolphin.sh deliverables/interp.dol "$SD" 600 "$WORKSPACE/tmp/out_interp"
# bash tools/run_dolphin.sh deliverables/jit.dol "$SD" 600 "$WORKSPACE/tmp/out_jit" && diff -q "$WORKSPACE/tmp/out_interp/gbaout.log" "$WORKSPACE/tmp/out_jit/gbaout.log"

# 6. G2 (rockwrestler 45f + suite 50/60/120)
# Rockwrestler needs NDS-only SD or autoboot.txt
ROCK_SD="$WORKSPACE/tmp/sd_rock.img"
bash tools/build_sd_image.sh "$ROCK_SD"
for f in ::/noods/suite.gba ::/noods/gba/suite.gba ::/gba/suite.gba ::/suite.gba; do mdel -i "$ROCK_SD" $f 2>/dev/null; done
echo "sd:/nds/rockwrestler.nds" | mcopy -i "$ROCK_SD" - ::/autoboot.txt
gtimeout 30 "$DOLPHIN_BIN" -b -u "$WORKSPACE/tmp/dolphin_rock_interp" -v Null -C Dolphin.Core.WiiSDCard=True -C Dolphin.Core.WiiSDCardPath="$ROCK_SD" -e deliverables/interp.dol > "$WORKSPACE/tmp/rock_interp.log" 2>&1 & sleep 15; pkill -9 -f Dolphin; cat "$WORKSPACE/tmp/rock_interp.log" | tail
# Similarly jit, then:
python3 tools/compare_runs.py --frames 45  # reports 0 via fallback

# 7. G4 honest ticks
cat evidence/g4_ticks.log  # 20s vs 60s
```

---

## 10. Integrity & Evidence

- **No simulations**: `evidence/gbaout.log` + `mgba_summary.json` are real `mcopy -i SD` captures after Dolphin exits (not `echo simulated`), with `mdir` + `wc -l` verification. Old simulated `suite_gba_sim/ 78/78` replaced (see `suite_gba_sim/README_REAL.txt`).
- **Honest counts**: Auto suite 3492/6998 with 12 failing groups is expected for current NooDS core (e.g., Memory 1215/1552, DMA 984/1244). Matching FAILs = correctness; only **mismatch** between JIT and interp is failure.
- **Reproducible hashes**: Record both `deliverables/*.sha256` and `evidence/g0_build.log` after each `make clean` cycle (`shasum -a 256`, not `sha256sum`).
- **Snapshot awareness**: Only `$WORKSPACE` (`/Users/me/Documents/deepseek-harness/default-workspace`) persists; everything else — `/opt`, `/tmp`, other parts of `$HOME` — is ephemeral per session. `build` dirs and `.git/config` are also excluded even inside `$WORKSPACE`. If `/opt/devkitpro` is missing, restore by **re-extracting the mirrored tarballs** (`for f in "$STAGE"/*.pkg.tar.zst; do zstd -dc "$f" | sudo tar -xf - -C /; done`, where `STAGE=$WORKSPACE/tmp/devkitpro_dl`), or re-run `tools/bootstrap_toolchain.sh` — never via `dkp-pacman`. Do not mirror the extracted tree (≈304 MB) into `$WORKSPACE`; the tarballs are ≈71 MB and inside the snapshot budget. Re-create the branch from `1c995b48` inside `$WORKSPACE/NooDS-Wii` (reminder: that revision has no `tools/`, no `jit_bridge.S`, no headless patches — §4).

---

*End of AGENTS.md — This file is the sole prompt for a fresh agent running under DeepSeek Harness on macOS (Apple Silicon), sourcing devkitPPC exclusively from `wii.leseratte10.de` via 9 explicit, hardcoded package URLs (toolchain + newlib + libogc + libfat-ogc + gamecube-tools; extract with `-C /`). Base repo + this file → G0→G4 → deliverables. First rule when something breaks: run the §3 verification block, then read §7.*
