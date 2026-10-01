# AGENTS.md — NooDS-Wii ARMv4/v5 → PowerPC JIT

Clean-start/resume guide from the 2026-10-01 bring-up. Read before editing.
The prompt defines the task; historical results are NOT fresh verification.

## 0. Task, scope and non-negotiable rules

- Add a real ARM/THUMB dynamic recompiler for GBA ARM7 and NDS ARM9/ARM7.
  Emit PowerPC instructions and execute them; do not rename an interpreter as a JIT.
- JIT must be the default for both active CPUs. Interpreter execution is fallback
  for unsupported operations/reference quirks, not an undisclosed whole-CPU bypass.
- Configure reproducible devkitPPC builds, assembly `.S` support and headless Dolphin.
- Test the pinned `rockwrestler.nds` and `suite.gba`; compare against a fresh interpreter.
- Deliver `/home/user/deliverables/jit.dol`, `handoff.md` and hashed evidence/GBA CSV.
- Compilation, booting, native counters and an encoder pass are NOT accuracy proof.
- Report PASS/FAIL/HANG/NOT_RUN honestly. Keep failed attempts; never invent a pass.
- Use a feature branch. Do not push main/production/release. Preserve existing work.
- No global package/tool installations without approval except the bootstrap below.
- No interactive REPL/PTY/stdin. Use noninteractive commands. Start long jobs with
  process tools and monitor with a blocking wait, not `ps`/sleep/curl polling loops.
- Run Dolphin serially: approximately 2 vCPU, 1.9 GiB RAM, no swap in the original VM.
  Each run must stay below about 25 minutes; short isolated cases are preferable.
- No unrequested roadmap implementation: lazy flags, block linking, idle detection,
  new native shift/multiply timing work and instruction-level oracles belong in handoff.
- Backend ISA compatibility is not host-port verification. The demonstrated DOL is
  Wii (`-mrvl`); do not claim a GameCube build or physical-console success without tests.

## 1. Establish what actually exists

First inspect workspace files, Git status, source revision and installed tools.
Do not assume a JIT, helper script, ROM, prior binary or `/opt` installation exists.
Older documentation described a v07 tree that was NOT in the starting workspace.

If a working checkout/bundle is supplied, preserve it and inspect before rebuilding.
If only this file and the initial prompt are supplied, use PUBLIC UPSTREAM; the user
explicitly authorized that route. Do not wait for an unavailable prior-tree upload.

```bash
cd /home/user
# Only when no checkout exists:
git clone https://github.com/radicalten/NooDS-Wii.git NooDS-Wii
cd NooDS-Wii
git checkout 1c995b48c37ebf3645646968c416958f79264137
git switch -c feature/arm-ppc-jit
```

The pinned public base has no new JIT or deterministic rig. Tools named later in this
file are interfaces to implement if absent, NOT commands to pretend already exist.
Inspect `interpreter.h/.cpp`, `interpreter_alu.cpp`, `interpreter_branch.cpp`,
`interpreter_transfer.cpp`, `interpreter_lookup.cpp`, `core.*`, `memory.*`, `main.cpp`,
`save_states.cpp`, `cp15.cpp`, scheduler callbacks and Wii/MEM2 allocation first.

Optional resume artifacts from the completed bring-up, IF actually supplied:
- Local source/tooling revision: `f88b131e916a199cb1de79c5e800dc5a3ceb09d3`.
- Binary build revision: `ad234f9cf9c41f4bc0758755d0da2202f2a93a92`.
- These are local feature commits, not revisions to request from public upstream.
- `deliverables/noods-wii-jit.bundle` restores exact branch/history offline:
  `git clone -b feature/arm-ppc-jit BUNDLE NEW_CHECKOUT` (the `-b` is required).
- Source ZIP/patch and `deliverables/evidence/runs.zip` may also be present.
  Never overwrite an existing checkout; never infer their presence from this list.

## 2. Reproducible devkitPPC environment

### 2.1 Approved system bootstrap and persistence

```bash
sudo apt-get update
sudo apt-get install -y zstd dolphin-emu xvfb imagemagick mtools libgl1-mesa-dri
```

Bash, make, git, curl, host C++ and Python were preinstalled. If another tool is absent,
ask before installing it; cmake, dkp-pacman and an extra ARM toolchain are unnecessary.
`/opt` and apt packages may disappear across sandbox resets. Verify, then bootstrap.
Only files under `/home/user` persist; dependency/build/cache folders may be excluded.
Keep source, scripts, manifests and evidence.

### 2.2 Sanctioned package mirror, versions and hashes

Use ONLY `https://wii.leseratte10.de/devkitPro/`. Do not use dkp-pacman or
`pkg.devkitpro.org` (403). Curl with URL-encoded spaces worked; urllib returned 403.
GCC alone is insufficient: rules, crtls and newlib are required for specs/link scripts.
Missing `rvl.ld` was fixed by installing rules/crtls, not by inventing a linker script.

Save the following as `tools/toolchain-packages.sha256` before bootstrapping:

```text
2b3c3f4773be827960c0fbb985f83f50916c56c54ab5cfcbe5a4239364e6302c  devkitPPC-r50-1-any.pkg.tar.zst
ec39352d27f668d235de9fa69caeee2d753994b695245075a37c4bfc700547d6  devkitppc-binutils-2.46.0-1-linux_x86_64.pkg.tar.zst
5a1144d515579eee73bb936ca36a8ea739b00d8cf32af3fa4dda21a039881bcd  devkitppc-crtls-2.1.0-1-any.pkg.tar.zst
656f0cabcd99a1c0d1510d0fccf5556abac26c3c813113175476d315c5b3da7a  devkitppc-gcc-16.1.0-1-linux_x86_64.pkg.tar.zst
2c5277c6b07a5558c9fd8e631d958c6d7784860aa55f5c320cb51371bc07e21b  devkitppc-newlib-4.6.0.20260123-4-any.pkg.tar.zst
0c3394da451c9dfb3b428d9d61b044eb1eafb947b2b5090ff88525f55c785278  devkitppc-rules-1.2.1-1-any.pkg.tar.zst
e7dea3d441f3951be336a5b52849bdabf191d7157b765b5b3a603ecf6ba233b4  gamecube-tools-1.0.7-1-linux_x86_64.pkg.tar.zst
9fb965672aaa3a82586715aab15ee4a16d3fc8850cbd2bd8cc4868e1c091d1b9  libfat-ogc-2.1.0-4-any.pkg.tar.zst
7c2dba9f4ef8cc496e424cd6208e0eddc893c08fff080016321d777d84c5c083  libogc-3.1.0-1-any.pkg.tar.zst
```

The proven bootstrap algorithm below can be saved as `tools/setup-devkitppc.sh`.
It installs only approved SDK archives; signatures were not independently established.
Hashes detect corruption/version drift.

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT=${ROOT:-/home/user}; CACHE="$ROOT/tmp/dkp"
BASE=https://wii.leseratte10.de/devkitPro
mkdir -p "$CACHE" "$ROOT/evidence"
packages=(
 'devkitPPC/r50%20%282026-05-03%29/devkitPPC-r50-1-any.pkg.tar.zst'
 'devkitPPC/r50%20%282026-05-03%29/devkitppc-binutils-2.46.0-1-linux_x86_64.pkg.tar.zst'
 'devkitPPC/r50%20%282026-05-03%29/devkitppc-gcc-16.1.0-1-linux_x86_64.pkg.tar.zst'
 'file.php/devkitppc-newlib-4.6.0.20260123-4-any.pkg.tar.zst'
 'file.php/devkitppc-rules-1.2.1-1-any.pkg.tar.zst'
 'file.php/devkitppc-crtls-2.1.0-1-any.pkg.tar.zst'
 'libogc/libogc_3.1%20%282026-05-03%29/libogc-3.1.0-1-any.pkg.tar.zst'
 'libfat/libfat_2.1.0/libfat-ogc-2.1.0-4-any.pkg.tar.zst'
 'other-stuff/gamecube-tools/gamecube-tools-1.0.7-1-linux_x86_64.pkg.tar.zst'
)
for rel in "${packages[@]}"; do
 name=${rel##*/}
 if [[ ! -s "$CACHE/$name" ]]; then
  curl -fL --retry 3 "$BASE/$rel" -o "$CACHE/$name.part"
  mv "$CACHE/$name.part" "$CACHE/$name"
 fi
 expected=$(awk -v name="$name" '$2==name {print $1}' "$(dirname "$0")/toolchain-packages.sha256")
 [[ ${#expected} = 64 ]] || exit 2
 printf '%s  %s\n' "$expected" "$CACHE/$name" | sha256sum -c -
 # devkitPPC-r50 itself is metadata-only; skip archives with no opt/ payload.
 # Do NOT use grep -q here: pipefail + early SIGPIPE can falsely reject an archive.
 if tar --zstd -tf "$CACHE/$name" | grep '^opt/' >/dev/null; then
  sudo tar --zstd -xf "$CACHE/$name" -C / --wildcards 'opt/*'
 fi
done
(cd "$CACHE" && sha256sum *.pkg.tar.zst) > "$ROOT/evidence/toolchain-packages.sha256"
```

```bash
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH=$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH
powerpc-eabi-gcc --version       # GCC 16.1.0 in the pinned environment
command -v elf2dol              # /opt/devkitpro/tools/bin/elf2dol, from gamecube-tools
```

### 2.3 Makefile requirements

- Default `JIT=1` → `jit.elf`/`jit.dol`; `JIT=0` → `NooDS-Wii-interp.elf/.dol`.
- Isolate `obj-jit0` and `obj-jit1`; use `-MMD -MP` and a compiler/flags stamp.
  Switching variants/flags must not silently reuse stale objects.
- Compile `.S` with cross GCC, `-x assembler-with-cpp`; include assembly objects in link.
- Link with cross G++, then run explicit `elf2dol`. Keep linker maps for both variants.
- Proven machine flags: `-mrvl -mcpu=750 -meabi -mhard-float`.
- CPP: `-DGEKKO -DENDIAN_BIG -DNOODS_JIT=0|1`, libogc + project include directories.
- C++: `-O2 -std=gnu++17 -fsigned-char -ffast-math -ffunction-sections -fdata-sections`.
- Link: `--gc-sections`, `/opt/devkitpro/libogc/lib/wii`,
  `-lasnd -lfat -lwiiuse -lbte -logc -lm`. No unused wiikeyboard dependency was needed.
- devkitPPC defines **`PPC=1`**. Use namespace `JitPpc`, NOT `namespace PPC`.
  Host compilation will not expose this target macro collision.
- Record revision, dirty patch, compiler/flags and DOL hashes before launching tests.

## 3. Execution contract — read the actual source, not a lost-tree convention

### 3.1 Scheduler and fallback

The original NDS scheduler advances globalCycles per guest instruction and interleaves
ARM9/ARM7 and events. A block must not let one CPU run ahead of the other CPU or IRQs.
Start with **one guest instruction per native stub**: proven scheduling equivalence
without multi-instruction deadlines. Optimize only after the gates.

Keep `runOpcode()` as the untouched reference. Share/copy its precise fetch/condition
prologue into `stepOpcode()` and call the original decoded handler on fallback, without
fetching/advancing twice. In the reconstructed tree this method is `runDecoded(opcode)`.
Those `interpretOne()`/`jitLimit`/`schedCheck` names are NOT public-upstream APIs.
Fallback must return the handler's ACTUAL cycle cost, never a flat guessed 3.
NDS ARM7 costs are shifted left once by the original scheduler; GBA is different.
Conditions not executed and reserved/HLE common paths may bypass native dispatch stats.

### 3.2 PC, prefetch, banking and memory

For the pinned upstream, with `size=4` ARM / `size=2` THUMB:
- After the common fetch, handler-visible r15 = instruction address + 2×size.
- Between instructions, r15 = next instruction address + size.
- Thus native cache PC = handler-visible r15 − 2×size; do not subtract a size twice.
- `flushPipeline()` aligns the raw target, adds one size, fetches target and next opcode.
- ARM BL LR = instruction+4; THUMB link LR uses the correct next address with bit 0 set.
- Branch/BX/interworking must refill correctly; a mode change cannot reuse wrong-width fetch.

Preserve the SAME two-opcode pipeline in both paths. A store to prefetched code should
not make the JIT reread an instruction that the reference already prefetched.
Use active register-pointer tables for banked registers; do not hardcode all writes to
`registersUsr`. Keep `ArmJit` last in Interpreter; derive target `offsetof`, check signed
D-form range (<32 KiB), and inspect warnings/layout. Never copy stale magic offsets.
Helpers requiring private members must be Interpreter/ArmJit members or authorized friends.

Delegate MMIO, endian handling, alignment and DMA effects to existing Memory methods.
Word LDR rotation is `(address & 3)*8`, with shift-zero guarded (no shift-by-32 UB).
Stored ARM r15 is handler PC +4, i.e. instruction+12 in the ordinary ARM path.
PC loads, SPSR restoration, writeback aliasing and complex transfers should fall back
until their exact reference behavior is implemented. Guest addresses are not host targets.
ARM9 high BIOS addresses are legitimate; a >=0x80000000 guard may deopt, not declare all
such PCs corrupt. Generated branches/calls must only target trusted host code/stubs.

### 3.3 Decode discipline and conservative native inventory

For ARM B/BL require `(opcode & 0x0e000000) == 0x0a000000` (bits 27–25 = 101).
LDM/STM (100) are NOT branches. Broad top-bit matches cause catastrophic PC/LR errors.
Keep MRS/MSR, multiply/extra-transfer and DP sub-encodings distinct; reserved conditions
must pass through the exact original dispatch behavior, not guessed generic semantics.

A proven initial scope: ARM/THUMB ALU + immediate shifts, simple word/byte transfers,
B/BL/BX, supported BLX forms and ARM9 MUL/MLA. Complex v5 operations may remain fallback.
Native ARM memory was restricted to immediate, pre-indexed, NO-writeback transfers.
ARM7 multiply has operand-dependent cycles; leave it fallback. ARM9 MUL/MLA costs
were 2 non-S / 4 S in this reference. THUMB NEG and ARM RSC-S had reference quirks;
leave them fallback rather than make only the JIT “more correct” than the interpreter.
Costs/quirks come from current handlers.

### 3.4 Correct PPC NZCV — the old rotate examples were wrong

PPC `rlwinm` rotates LEFT. The table uses integer LSB bit positions for source/destination
and PPC MSB-numbered MB/ME masks. Do not carry forward old SH=31 for Z or SH=2 for V.

| Guest bit | Source | Integer source → destination | SH | MB=ME |
|---|---|---|---:|---:|
| N (31) | CR0 LT | 31 → 31 | 0 | 0 |
| Z (30) | CR0 EQ | 29 → 30 | **1** | 1 |
| C (29) | XER CA | 29 → 29 | 0 | 2 |
| V (28) | XER OV | 30 → 28 | **30** | 3 |

- Clear sticky XER SO/OV before arithmetic; initialize CA from old guest C for ADC/SBC/RSC.
- PPC `subfc RT,RA,RB` computes RB−RA; `subfe` computes RB+~RA+CA. C is NOT-borrow.
- Harvest `mfcr`/`mfxer` IMMEDIATELY after the arithmetic; no intervening compare.
- Logical NZ may use `cmpwi result,0`; do not compare against scratch r0 as “zero”.
  Preserve guest C/V when required and preserve low CPSR bits, including Q/control/state.
- For condition tests, `mtcrf 0x80` maps guest N/Z/C/V to CR0 LT/GT/EQ/SO respectively.
  This is NOT the normal compare's CR0 EQ→Z mapping; keep these two uses separate.
- Shifter rules matter: LSL #0 preserves C; LSR/ASR #0 means 32; RRX reads OLD C first.
An assembler-byte match proves encoding, not these semantics; runtime diffs are mandatory.

### 3.5 Native ABI, pools and coherency

A sufficient initial bridge uses 32 bytes: `jit_enter(code, Interpreter*)` receives r3/r4,
saves caller LR at 4(old SP), allocates 32 bytes, saves r31 at 28(new SP), sets r31=CPU,
and `bctr`s into the stub. Stub tail-exits to `jit_return`, which restores SP/r31/LR.
A helper's `bctrl` overwrites LR; NEVER return directly through that volatile helper LR.
Emitted scratch: r3–r12, CR0/1, XER/CTR/LR; r0 only in controlled assembler saves.
Do not use r0 as D-form base/index (RA=0 is special); do not touch r2/r13 or r14–r30.
Preserve ABI stack alignment and audit every helper-call signature/outgoing argument area.

- Keep large code/metadata pools POD/zero-initialized `.bss`, not initialized `.data`.
- Proven bounds: shared 4 MiB code pool, 8192 direct-mapped entries per CPU,
  maximum 160 PPC words/stub. These are reference choices, not fixed performance goals.
- Cache key: CPU/PC/actual prefetched opcode/T state. Start serial nonzero so initial
  PC=0/opcode=0 cannot hit a false zero-filled entry. On pool reuse invalidate all tags.
- One-instruction opcode validation avoids stale sequences after stores, DMA, aliases,
  CP15/WRAM/VRAM remaps. If baking more state or longer blocks, this proof no longer suffices.
- Reset/validate cache on new Core and enable/toggle. Review save/load pointer/bank/pipeline
  restoration; do NOT serialize host code pointers or claim parity without a runtime test.
- Before every native execution of new code, flush ALL emitted 32-byte cache lines:
  align start down; `dcbst`, `sync`, `icbi` for each line; final `sync; isync`.
- Check DOL size and codePool `b` in nm.

## 4. ROMs and deterministic rig

### 4.1 Pin ROM bytes before every run

```bash
cd /home/user; mkdir -p nds gba evidence tmp
curl -fL https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds -o nds/rockwrestler.nds
curl -fL https://raw.githubusercontent.com/radicalten/gba-test-suite-mgba-emu/e05e71367964d75d65d2b9dded7240608a097a10/suite.gba -o gba/suite.gba
printf '%s\n' 'f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a  nds/rockwrestler.nds' '8cf68cd31c5468a70aca8a1c5b63dc372fe755c40b446cf6aa6d0fc6e3a1f035  gba/suite.gba' | sha256sum -c -
```

Rockwrestler: 39,433 bytes; MD5 `dfd1770daba69955031c0699d33b1dc6`.
Suite: 524,288 bytes. Source repositories are RockPolish/rockwrestler and
radicalten/gba-test-suite-mgba-emu. Inspect their source, but the pinned ROM is the
identity authority; a newer source HEAD is not automatically the ROM's build revision.

### 4.2 SD, settings and true frame boundaries

Before EACH run recreate SD AND Dolphin user/config. No old SRAM, save, config or dump.
SD: 128 MiB raw, MBR partition 1 at 1 MiB, type 0x0E, FAT16, files in `::/noods/`.
Use Python/standard tools to write MBR; `mformat -i IMAGE@@1M` chooses FAT16 at this size.
Do not use `mformat -F` (forces FAT32); do not install/mount extra disk tools unnecessarily.
Mtools address is `IMAGE@@1M`; stage ROM, autoboot.txt and optional input.txt there.

Test config example: `path=/noods/rockwrestler.nds`, `jit=1`, `dump=45` on separate lines.
For GBA suite navigation use NO `dump=` cutoff; self-exit only after actual results.
Without autoboot, preserve normal UI and JIT default.
Both variants: direct boot, DSi off, limiter/audio/frame skip/threaded rendering off,
ROM in RAM, ARM7 HLE off. HLE BIOS/no proprietary BIOS was the tested configuration;
record it and do not attribute every baseline failure to the JIT.

**`runCore()` is NOT necessarily one frame.** HALT/updateRun can return early. Add an
observer-only counter incremented in `Core::endFrame()`; run until it changes before
counting a frame, applying the next frame-indexed input or ending a measured frame.
Observed increments: NDS first 408960, then 560190; GBA first 197120, then 280896.
The old claim “408960 every frame” is not valid for this public upstream.

### 4.3 Dolphin launcher specification

- Unique `RUN_ID`; refuse overwrite; flock serialization; close lock FD 9 in child
  launch (`9>&-`) so timeout/Xvfb children cannot leak the lock into the next run.
- Isolated `tmp/dolruns/RUN_ID/user/Config` and `user/Load/WiiSD.raw`.
- Copy exact DUT; hash it, ROM and settings; record source commit/dirty patch BEFORE run.
- Requested Dolphin.ini: CPUCore=1, CPUThread=False, EmulationSpeed=0.0, DSPHLE=True,
  SyncGPU=True, DeterministicGPUThread=True, cheats off, writable SD at that run path.
  Retain exact INIs/version; check recognized options if using another Dolphin version.
- GFX: OGL, resolution 1, MSAA 1, VSync/force filtering off. Software Mesa; small Xvfb.
- Proven command (ensure every shell continuation backslash is present):

```bash
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a -s '-screen 0 640x480x24' \
 timeout --signal=TERM --kill-after=10s 180s /usr/games/dolphin-emu-nogui \
 -u "$run/user" -p x11 -v OGL -a HLE -e "$run/dut.dol" 9>&- > "$run/dolphin.log" 2>&1
```

Check `--version`/`--help` first. Capture exit code, timeout and wall seconds.
ALSA no-device warnings occurred despite Null configuration; they did not prove failure.
On non-launch/lock rejection create explicit NOT_RUN bookkeeping, not a missing-directory
exception or a reused old result. On timeout/abort preserve whatever the DUT wrote.
Always pull from the DUT's **user/Load** SD, NOT the staging image.

## 5. GBA suite automation — accurate IDs, not just booting

Drive real emulated keys through Input with frame-indexed press/release events, or a
working interactive controller. DSU is optional, not a prerequisite/missing-tool excuse.
Never patch the ROM/result arrays or infer test success from reaching the main menu.
The suite runs the whole selected group before its result list appears. Wait until
`Testing...` disappears and the correct group's selectable result list is present.

Pinned counts by menu order (1,419 total; menu tests may aggregate sub-checks):

| Index | Group / stable slug | Tests |
|---:|---|---:|
| 0 | memory | 36 |
| 1 | io-read | 130 |
| 2 | timing | 132 |
| 3 | timers | 26 |
| 4 | timer-irq | 9 |
| 5 | shifter | 70 |
| 6 | carry | 31 |
| 7 | multiply-long | 36 |
| 8 | bios-math | 123 |
| 9 | dma | **718** |
| 10 | sio-read | 90 |
| 11 | sio-timing | 8 |
| 12 | misc-edge | 3 |
| 13 | video | 7 |

Do not cap DMA at 160: 160 is the result-name buffer capacity, not its test count.
Discover/validate TestSuite metadata in pinned ROM if possible: seven little-endian
32-bit fields (name/run/list/show/nTests/passes/total); ROM pointers start at 0x08000000.
Text grid: VRAM 0x06000800, stride 32 tiles, rows 3–18 (16 results), 30 visible columns.
Tile ASCII = (tile & 1023)+32; palette 0 pass / 1 fail. Read every displayed row/status.
Include index in stable ID: labels truncate and can duplicate. SRAM logs are bounded
at 32 KiB; keep them, but they may not contain every failed sub-check.

### 5.1 Paging exactness and resource limits

Use result pages, not thousands of row-by-row frames. An initial DMA JIT run timed out
at 2614 matching frames; an extended attempt exhausted VM memory near 2647 frames.
Guest execution was still progressing; no guest hang was established. Simply extending
the timeout is not a fix. Preserve attempt as HANG/resource abort with explanatory notes.

**RIGHT does NOT always mean +16.** Follow this ROM's source exactly (`VIEW_SIZE=16`):

```text
if testIndex == nTests-1:              next = 0
else if testIndex+16 >= nTests:       next = nTests-1
else if viewIndex+15 == testIndex:    next = testIndex+16
else:                               next = viewIndex+15
if next < viewIndex:                 viewIndex = next
else if next >= viewIndex+16:        viewIndex = next-15
```

Starting at index 0, first RIGHT selects index 15; second selects 31 and scrolls.
Capture initial page, UP-wrap to the tail, DOWN-wrap back to zero, then page RIGHT;
this also covers a partial final page. Maintain seen-index bounds/count and acknowledge
selection changes before assigning indices. Use press ~2 real frames, release ~4.
A wrong pager mislabelled **both** matching CSVs.
Parity alone is insufficient: independently validate ALL labels/orders (source/ROM,
known one-row traversal, or a separately checked manifest). Mark invalid IDs NOT_RUN,
preserve raw captures/original rows, correct the driver and rerun fresh.

### 5.2 Video is a separate oracle

The video list hardcodes passed=true and counters=0. NEVER infer PASS from that list.
Invoke each video test (A); capture Actual; select Expected with RIGHT; capture it.
START hides the Actual/Expected navigation sprite; verify hiding before pixel comparison.
In this ROM OAM0 char index is 0x220 Actual / 0x200 Expected; OBJ disable is attr0 bit 9.
Actual drawing also sets DISPCNT OBJ-on (0x1000). Wait for drawing/toggles to take effect.
Compare Actual vs ROM Expected for per-test status AND JIT vs reference for both images.
If no trustworthy visual/capture oracle exists, record NOT_RUN; do not reuse menu true.
Keep raw pixels, inputs and stage/frame evidence.

## 6. Gates — fresh, on the exact delivered binary

After any translation change run all gates. Missing/empty/invalid dumps are NOT_RUN,
not “identical.” Interpreter must be built from the SAME source/flags except JIT=0.

**G0 Build/provenance:** target and reference compile/link; `.S` included; SHA-256 recorded.
**G1 Encoder:** independent GNU assembler bytes + objdump for every used PPC form.
Host C++ emitter harness alone cannot validate target macros, ABI or ARM semantics.
**G2 Accuracy:** same ROM/settings/input/real frame count; 10-frame sanity then 45-frame
CPU/register banks/CPSR/SPSR/halt/cycles/pipeline/event/framebuffer/dumped-RAM comparison.
Require all fields, valid sizes and complete frame-boundary trajectories. Compare frames
and pipelines in this implementation; ignore only known jit_/trace_/fps/runTicks diagnostics.
Call it “dumped RAM,” not all guest memory: DS dump was 4 MiB main +64 KiB ARM7 WRAM;
GBA 256 KiB EWRAM +32 KiB IWRAM. FB was DS 256×384 / GBA 240×160, 4-byte pixels.
**G2b Bisect:** stop at first differing frame/state, inspect guest PC/opcode/banks and
emitted code. Do not run the long suite on a known-divergent build. No periodic-PC
sample is evidence of a hang; tight loops alias sampling. Existing full frame snapshots
are preferable to noisy per-dispatch logs. Do not add an unrequested instruction oracle.
**G3 GBA:** every group and every menu-listed test in both variants; stable audited IDs,
0 unexplained regressions, explicit baseline failures/improvements and incomplete cases.
Compare group state/FB/RAM, displayed rows/SRAM and all video captures too.
**G4 Performance:** fresh same-ROM/count/settings pairs, actual `frame:` logs and native
statistics. Honest slow results are valid; compilation is not a performance result.

CSV (one row/test/variant/run; unique run_id; path/order ID; displayed label retained):
`run_id,commit,variant,dol_sha256,rom_sha256,test_id,test_name,status,notes,evidence`
Allowed statuses: PASS, FAIL, SKIP, HANG, NOT_RUN. Never substitute a group-boot row.
Do not mix superseded/bad-index attempts into the accepted comparison; keep attempts
separately, with raw evidence. A baseline FAIL is not a new JIT regression or a PASS.
“Regression-verified” means recorded interpreter equivalence, NOT hardware-wide accuracy.

Additional NDS CPU coverage is valuable: main ARMv4 menu condition test, and all 11
ARMv5 entries (CLZ, saturating ops, signed multiplies, BLX, PC interworking, LDM/STM).
Press/release A to enter then invoke; DOWN selects; wait for actual OK/FAIL/TIMEOUT output.
The fixed ROM's font is 8192 bytes at file offset 0xDCC, 64 bytes/glyph; its SHA-256 is
`9c9bee00adcdecc27ddc557605a8a2c21351c71dd0aa2dd078dbdc1166ec8b1c`.
Pixel decoding can prove output; don't interpret the ROM's main menu as CPU-test success.

## 7. Measurement and debug hygiene

Log `frame: n=%u runTicks=%llu ndsCycles=%u fps=%d` once per REAL frame.
Measure all `runCore()` calls needed to reach endFrame; exclude SD writes, framebuffer
extraction and menu-driver work from the timed region. Include translation/cache upkeep
and enabled counters honestly. Software GPU work inside runCore is part of that cost.
Use same warmup (proven: exclude 0–4, mean/median 5–44 of 45) and report units/formula:
JIT cost ratio = JIT mean ticks / reference mean ticks; throughput ratio is its reciprocal.
PPC ticks are Dolphin observations, not physical Wii/GCN speed; do not infer hardware FPS.
A short GBA benchmark can finish before FPS updates (fps=0) while all 45 frames completed.

Never SD-log per opcode/native dispatch. Bounded buffers/first-hit probes only, flushed
on cold paths; keep normal UI free of autoboot logging. Record instrumentation cost.
If wall time is unexpectedly >~5× the anticipated short-run budget, inspect progress,
latest written data and memory before changing code. Do not blindly increase timeout.
Treat memory pressure as a resource failure until diagnosed, not an emulation divergence.

## 8. Tool interfaces and reproduction checklist

Implement these tools if absent; the optional completed tree provides them:
- `setup-devkitppc.sh`, pinned hash manifest; `jitdis/test_encoder.py` (asm oracle).
- `mksd.sh IMAGE ROM CONFIG [INPUT]`; `run-dolphin.sh DOL SECONDS IMAGE [FRAMES]`.
- `test-case.sh ID jit|interp ROM FRAMES SECONDS [INPUT] [suite_screen 0|1]`.
- `autodump.sh SD OUTDIR`, or `--diff REF_DIR JIT_DIR`; strict Python comparator.
- `suite-case.py ID VARIANT GROUP [SECONDS]`; serial `suite-regression.sh SESSION 240`.
- `rock-cpu-regression.py SESSION 180`; `perf-report.py REF_ID JIT_ID [WARMUP]`.
- Optional framebuffer PNG conversion; raw-byte gates must not depend on Pillow/CDNs.

Once those interfaces exist (ROOT defaults /home/user), exact use:

```bash
export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:$PATH
cd /home/user/NooDS-Wii
make -B -j4 JIT=1 && make -B -j4 JIT=0
cd /home/user
# /home/user/tools may be a symlink to NooDS-Wii/tools; create only if absent.
python3 tools/jitdis/test_encoder.py evidence/encoder-UNIQUE
bash tools/test-case.sh UNIQUE-rock-jit jit nds/rockwrestler.nds 45 180
bash tools/test-case.sh UNIQUE-rock-ref interp nds/rockwrestler.nds 45 180
bash tools/autodump.sh --diff tmp/dolruns/UNIQUE-rock-ref/artifacts tmp/dolruns/UNIQUE-rock-jit/artifacts
python3 tools/perf-report.py UNIQUE-rock-ref UNIQUE-rock-jit > evidence/UNIQUE-rock-perf.json
bash tools/test-case.sh UNIQUE-gba-jit jit gba/suite.gba 45 180 '' 1
bash tools/test-case.sh UNIQUE-gba-ref interp gba/suite.gba 45 180 '' 1
bash tools/autodump.sh --diff tmp/dolruns/UNIQUE-gba-ref/artifacts tmp/dolruns/UNIQUE-gba-jit/artifacts
python3 tools/perf-report.py UNIQUE-gba-ref UNIQUE-gba-jit > evidence/UNIQUE-gba-perf.json
bash tools/suite-regression.sh UNIQUE-gba-suite 240
python3 tools/rock-cpu-regression.py UNIQUE-nds-cpu 180
# Manual pull, always from this DUT's image:
bash tools/autodump.sh tmp/dolruns/UNIQUE-rock-jit/user/Load/WiiSD.raw tmp/pulled
```

## 9. Trap catalogue (diagnose before broad changes)

| ID | Symptom | Likely cause / smallest fix |
|---|---|---|
| T1 | cmp/bne never terminates, guest PC drifts into data | Wrong Z/V rotates or CR0 clobber; §3.4, runtime first-diff |
| T2 | NZ/conditions depend on scratch state | cmp against r0, not zero; use cmpwi, no compare in harvest live range |
| T3 | LR/PC jump wildly, register-list bits look like target | LDM/STM decoded as branch; require 101 mask |
| T4 | Excess compiles/replacements/words | Inspect keys/serial/init and hash collisions; high words alone do not prove SMC bug |
| T5 | SIGILL, stale code, intermittent native crash | Exact-range D/I cache maintenance and trusted exit targets |
| T6 | Registers corrupt after helper | LR/stack/r31 mishandling or r2/r13/CR2–4 touched; audit EABI |
| T7 | Emission falls through/missing exits | Word/patch-site overflow; guards and one-instruction exits |
| T8 | One CPU reaches poll/IRQ “early” | Retiring more than scheduler's one instruction; keep scheduler granularity |
| T9 | Small cycle/state drift after deopt | Double fetch/PC advance or guessed fallback cost; return original cost |
| T10 | ARM7 speed/budget wrong | NDS <<1 applied twice/not at all; ARM7 MUL simplified incorrectly |
| T11 | Huge wall time/memory despite guest progress | Hot SD I/O or excessive menu traversal; cold logs, bounded paged cases |
| T12 | Ring/probe PCs nonsense | Index underflow or wrong record unit; clamp and label units |
| T13 | no such dol | Relative DUT resolves under /home/user/NooDS-Wii; use basename or absolute path |
| T14 | Artifact missing/stale | Wrong SD, empty dump, non-launch; pull user/Load and reject missing data |
| T15 | “stuck at PC” from periodic samples | Sampling alias; inspect changing counters/full snapshots, not a lone PC |
| T16 | Private-member compile errors | Explicit accessors/member helpers/authorized friend; no offset guessing |
| T17 | Host oracle green, target build red | PPC macro collision, target ABI/specs; crosscompile too |
| T18 | Underflow/wrap in deadlines/serials | Clamp budget subtraction; nonzero epoch and wrap invalidation |
| T19 | PC ±2/±4 or pipeline mismatch | Wrong phase convention/double advance; pinned upstream contract §3.2 |
| T20 | Tools vanished or next run lock rejected | Re-bootstrap /opt; close inherited FD; explicit NOT_RUN, fresh config |
| T21 | GBA “45 frames” has partial cycle bursts | runCore return counted as frame; observe endFrame instead |
| T22 | Equal CSVs but wrong test labels/counts | RIGHT page-end semantics/truncated duplicates; independent ID audit |
| T23 | All video rows magically PASS | Menu hardcodes true; execute and compare Actual/Expected pixels |

## 10. Deliverables, evidence and snapshot discipline

Required `deliverables/` contents:
1. `jit.dol` — exact tested JIT bytes; record SHA-256. Copy only after gates permit the
   stated verification claim. If incomplete, label candidate/unverified, never certified.
2. `handoff.md` — status/verified scope; exact build/SD/run/pull/gate commands; hashed
   evidence index; Dolphin-only performance/statistics table; known failures + smallest
   next steps with trap IDs. Distinguish baseline failures from native regressions.
3. `evidence/gba-suite-results.csv` — accepted runs with mandated schema; separate
   attempts CSV for HANG/NOT_RUN/invalid-index history. Include NDS CPU results if run.
4. Exact DUT/reference, ROM/settings/source identities, configs/input plans, stdout,
   debug.log, CPU/frame trajectories, raw FB/dumped RAM, SRAM/video captures,
   encoder source/emitted/oracle bytes/disassembly, compiler/package hashes and maps.
5. Optional source ZIP/patch/Git bundle; preserve GPL source/license with distributed DOL.

A 128 MiB SD file counts as 128 MiB toward snapshot limits even if sparse. Repeated SDs,
SDK archives (~89 MiB) and unpacked traces can silently lose the useful workspace.
Before ending: validate a compressed archive with per-member hashes; deduplicate exact
DUTs by hash with explicit references; keep accepted CSVs/manifests directly accessible.
Only then remove regenerable images/caches/objects or compact archived raw runs.
Never delete the only copy of an artifact. Explain archive paths/restoration commands.
Preserve source/branch; no secrets/global git config.

## 11. Historical ledger — reference only, re-run before claiming success

At the completed 2026-10-01 run, all requested regression gates were exercised on:
- JIT: 1,285,440 bytes, SHA `4b84feafa9a23e840de32bdd0c59e03c96480c918e6cfd282542c3efa26610d3`.
- Reference: 1,264,640 bytes, SHA `ee148c2235c6f14d1d39b0740598e246048b56903a02f4dd382c996cec06ce2a`.
- G1: 74 cases /84 PPC words byte-identical to assembler (not old v07's 113 forms).
- G2: 45 REAL frames NDS and GBA, identical dumped state/FB/RAM and trajectories.
- G3: all 1419 GBA menu tests/variant, **722 PASS /697 FAIL in BOTH**, 0 regressions/
  improvements; every group dump/SRAM/display matched. Independently audited IDs.
- Video: 3 Actual=Expected, 4 mismatches in both; all cross-variant images identical.
- NDS CPU menus: 11 PASS /1 shared FAIL (`LDM / STM`, `FAIL 00B`), states/pixels identical.
- G4 warm mean ticks (frames 5–44): NDS ref 6263222.125 vs JIT 9776719.325 (**1.561× cost**);
  GBA ref 321559.525 vs JIT 592876.250 (**1.844× cost**). This prototype was SLOWER.
- GBA boot stats: 1479198 dispatch, 1274762 native, 204436 fallback, 45160 compiled,
  6511 unsupported compilations, 50253 replacements, 647647 pool words, 0 resets.
  Counters exclude skipped conditions/reserved common paths; not total guest instructions.
- First JIT flag attempt failed real state/FB comparison; corrected Z/V rotates fixed it.
- Row-by-row DMA timeout/resource failures and one SIO non-launch were preserved.
  A wrong pager's invalid IDs were marked NOT_RUN, fixed, fully audited and rerun.
- No physical Wii/GCN validation, GameCube host build, proprietary BIOS boot or save/load
  runtime parity was performed. Other NDS peripheral/memory menus remain NOT_RUN.

Resume priorities: verify available artifacts/source first, reproduce a short pair, then
profile measured overhead/cache collisions without changing correctness contracts. Fix
an interpreter baseline failure only as a separate scoped change, never JIT-only parity
breakage. Do not relabel this correctness-first prototype as a performance-ready release.
