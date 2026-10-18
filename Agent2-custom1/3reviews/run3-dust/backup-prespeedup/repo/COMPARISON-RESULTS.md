# NooDS-Wii ARM/Thumb JIT comparison

## Executive result

I built and compared the interpreter-only baseline, the b4 JIT, the Agents2-5 block JIT, and a new conservative safe-JIT mode using the supplied `suite.nds` under headless Dolphin.

- **Agents2-5 JIT:** its complete ARM9 and ARM7 trace slots, all 25,436 vector records, and the assertion journal are byte-for-byte identical to the interpreter baseline. The 4 MiB RAM image differs in one byte outside those test regions (`0x826c`: baseline `0x64`, Agents `0x54`).
- **b4 JIT:** runtime probe confirms JIT active. The suite finished by actual video frame 300 with no unrun groups, but it has **44 more vector failures** than the interpreter: 12 more on CPU9 and 32 more on CPU7. Raw trace records show both fail-to-pass and pass-to-fail changes; this is not merely a different group-level score.
- **New conservative safe JIT:** its full 4 MiB main-RAM image, both complete trace slots, assertion journal, and framebuffer checkpoints match the interpreter byte-for-byte / hash-for-hash for this ROM. It preserves the interpreter’s existing failures rather than claiming to fix them.

All four builds reported 21/27 groups passed, 6 non-passing, and 0 unrun. The group score alone hides the b4 vector-level regression.

## Per-profile suite results

Values are **pass / fail**. There are 25,436 planned vectors total: 12,056 CPU7 and 13,380 CPU9.

| Build | ARM7 ARM | ARM7 Thumb | ARM9 ARM | ARM9 Thumb | CPU7 total | CPU9 total | Groups / unrun |
|---|---:|---:|---:|---:|---:|---:|---|
| Interpreter baseline | 5,204 / 140 | 6,680 / 32 | 6,209 / 432 | 6,595 / 144 | 11,884 / 172 | 12,804 / 576 | 21/27 / 0 |
| Agents2-5 JIT (`JIT=1 BLOCKS=1`) | 5,204 / 140 | 6,680 / 32 | 6,209 / 432 | 6,595 / 144 | 11,884 / 172 | 12,804 / 576 | 21/27 / 0 |
| Conservative safe JIT (`JIT=1 BLOCKS=0 SAFE=1`) | 5,204 / 140 | 6,680 / 32 | 6,209 / 432 | 6,595 / 144 | 11,884 / 172 | 12,804 / 576 | 21/27 / 0 |
| b4 JIT | 5,182 / 162 | 6,670 / 42 | 6,197 / 444 | 6,595 / 144 | 11,852 / 204 | 12,792 / 588 | 21/27 / 0 |

The b4 run repaired some baseline failures but introduced more: ARM9 has 59 fail-to-pass and 71 pass-to-fail record-status transitions; ARM7 has 60 fail-to-pass and 92 pass-to-fail. Net result: +12 CPU9 failures and +32 CPU7 failures. Its on-screen first-bad IDs shifted from 113 to 3136 for ARM7/ARM9 ARM, and from 4551 to 3492 for ARM7 Thumb; ARM9 Thumb remains 5419.

One concrete b4 regression is trace record index 3135 (the on-screen first-bad ID 3136). For ARM9 at helper PC `0x0205DAA0`, the expected result is `1`; the interpreter produced `1` with CPSR `0x000000DF`, while b4 produced `0` with CPSR `0x400000DF` (the recorded flag comparison mask is `0xF0000000`). ARM7 shows the analogous result/flag mismatch at `0x02257D1C`.

### b4 timing/scheduler lead

At actual video frame 120, b4 reports `globalCycles=200,679,328`, versus `67,071,570` for the interpreter; at frame 300 the values are `503,319,158` versus `167,905,770`—about 3× at each matched frame checkpoint. The b4 frame-120 framebuffer hash also equals the interpreter’s frame-300 hash. In source, b4’s `runJitNds` advances its scheduler with `runCpu()<<1` for ARM9 and `runCpu()<<2` for ARM7, while the interpreter NDS loop advances ARM9 by `runOpcode()` and ARM7 by `runOpcode()<<1`. This is a strong scheduler/cycle-scale lead to investigate, not a proof that this one difference alone causes every bad vector.

## Raw trace and RAM comparison

The harness dumps a 256 KiB prefix followed by the first 4 MiB of NDS main RAM. `scripts/compare_trace_ram.py` parses the trace-v5 headers/records and assertion journal from that 4 MiB slice. Its three full JSON outputs are in `dolphin-test/`.

| Candidate vs interpreter | ARM9 slot (13,380 records) | ARM7 slot (12,056 records) | Assertion journal (81,984 bytes) | Main RAM |
|---|---|---|---|---|
| Agents2-5 JIT | byte-identical | byte-identical | byte-identical | 1 byte differs outside trace/journal at `0x826c` |
| Conservative safe JIT | byte-identical | byte-identical | byte-identical | byte-identical across all 4 MiB |
| b4 JIT | 189 records changed; 130 status words changed | 209 records changed; 152 status words changed | byte-identical | different, as expected from changed execution/results |

Reference hashes:

- Interpreter main-RAM SHA-256: `e40d55e873d842d4ffd47a829f5570725d49bc20a3af63cc0ea16d4f98fa20a1`.
- ARM9 trace-slot SHA-256: `ff42965393e9b509b66cc065fc513f01562531d4e8e73f83cb07dc470cffc010`.
- ARM7 trace-slot SHA-256: `fea99a0717e4ef945776deccf75e6cd6222b88614cfc591bd5ddb427b5921b3f`.
- Assertion-journal SHA-256: `164b93cee8e77e162b6e645186b5fbdebc5fd5736e35c1d71b4465b179dd524d`.
- The safe full-RAM dump and interpreter full-RAM dump are identical; the full dump file SHA-256 is `48db9a1120a6139ef820472c7ac49e92091f21ffa46ad8fc70c6e45886c03191` for each.

The Agents-only byte at `0x826c` explains its different RAM hash; it is not a vector-record or assertion-journal difference. The b4 assertion journal also matches, even though its CPU trace records do not.

## Conservative safe-JIT implementation

The new mode is opt-in and keeps block compilation off. It emits per-instruction PPC stubs only for a small whitelist: ARM AL data-processing operations that do not write flags, do not touch PC, use immediate or unshifted-register operands, and are not ADC/SBC/RSC; plus Thumb high-register ADD/MOV without PC. All other instructions—including flag-setting arithmetic, memory, branches, and system operations—fall back to the existing interpreter. This favors a narrow, auditable native subset over coverage.

Build from `conservative-jit/`:

```sh
source /etc/profile.d/devkit-env.sh
make clean JIT=1 BLOCKS=0 SAFE=1
make -j4 JIT=1 BLOCKS=0 SAFE=1
```

The build uses devkitPPC Wii toolchain r51-1 (GCC 16.2.0). Dolphin was `2503` (`dolphin-emu-nogui`, headless). The suite ROM is 2,036,233 bytes, SHA-256 `8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3`.

## Headless-Dolphin workflow and evidence

Each run used a FAT virtual SD card containing `suite.nds` and an `autoboot.txt` with the selected `MAX_FRAMES`/checkpoint list, then launched the Wii DOL under `dolphin-emu-nogui -p headless -v Null` with SDL dummy video/audio. The harness writes `g2_state.log`, `fb_dump.bin`, and (for full-RAM captures) `mem_dump.bin` to the card. The generic launch form used was:

```sh
timeout --signal=TERM --kill-after=5s 170s \
  env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  /usr/games/dolphin-emu-nogui -u <user-dir> -e <test-harness.dol> \
  -p headless -v Null \
  -C Core.WiiSDCard=True -C Core.WiiSDCardAllowWrites=True \
  -C General.WiiSDCardPath=<virtual-sd.raw>
```

Dolphin does not always exit after a harness reaches its checkpoint, so the external timeout terminated it after the SD-card files were written (wrapper exit 137). The interpreter, Agents, and safe captures used a 170-second timeout; the slower b4 capture used 200 seconds. Treat the extracted checkpoint data—not that timeout status—as the result. Interpreter, Agents, and safe full-RAM runs reached frame 600. The verified b4 run finished all suite groups by frame 300; `JIT_ACTIVE available=1` is recorded at frames 120 and 300. Its `JIT_STATS enabled=0` line is a harness placeholder in this fork, not the runtime JIT test; use `JIT_ACTIVE` for that check.

Useful evidence:

- Trace comparison JSON: `dolphin-test/trace-diff-interpreter-vs-agents.json`, `...-vs-safe.json`, and `...-vs-b4.json`.
- Logs and full-RAM dumps: `dolphin-test/interpreter-fullram-600.*`, `agents-fullram-600.*`, `safe-fullram-600.*`, and `b4-jit-fullrun-300.*`.
- Suite screenshots: `dolphin-test/safe-suite-frame600-upscaled.png` and `dolphin-test/b4-suite-finished-frame300-upscaled.png`.
- DOL checksums: `SHA256SUMS.txt`.

## DOL artifacts

All DOLs were compiled with devkitPPC. The first four are the direct build artifacts; the `fullram`/`harness` variants include test-only logging or dump-size changes for capture.

| DOL | SHA-256 |
|---|---|
| `build-artifacts/NooDS-Wii-interpreter.dol` | `7182b7d4c1878e2d38f06210086302ff6ad38eb78366a413d718102eabb06f60` |
| `build-artifacts/NooDS-Wii-b4-pp2.dol` | `6304e8a8c8b8924ababbe8915cc65c74d7c5e2c6ff2db80098b2d9406c6a39fb` |
| `build-artifacts/NooDS-Wii-Agents2-5-JIT.dol` | `0fb5d6b48927901f2cd435d849131d0daf6922046e97dc9b76c44c14a7d443aa` |
| `build-artifacts/NooDS-Wii-conservative-safe-jit.dol` | `2fb683f12bfe7a95a02440d53f36bde82562d704dc50a41dfbfa87d4c08b1c5e` |
| `build-artifacts/NooDS-Wii-interpreter-fullram.dol` | `207ab3874c85959ed0fec19943f4fad88b7d9c16fc0b713354ac45aa5167ee70` |
| `build-artifacts/NooDS-Wii-Agents2-5-JIT-fullram.dol` | `9634854744d7e8c5298821edbe1ee26c71b05621beefa0733ee1d0b480265fef` |
| `build-artifacts/NooDS-Wii-b4-harness-fullram.dol` | `0fc54f2145c3e789c3ca6b54cd048bee9cb2efc59bf8f531d53fa1bdbb03b997` |
| `build-artifacts/NooDS-Wii-conservative-safe-jit-fullram.dol` | `f1509841e01502da6255d3450913b1204436dd514b1c27a46ce3502c6aa29173` |

## Scope and limits

This establishes differential parity for the supplied ROM under Dolphin, not architectural correctness on physical DS hardware. The conservative mode matches the interpreter exactly but inherits its 748 failing vectors. It safely avoids introducing the b4-style trace changes on this test, but it does not fix the baseline’s known CPU inaccuracies. Full test artifacts and the source tree are retained in the workspace under `/home/user/repo/`. A bundled copy of the report, source, DOLs, trace JSON, logs, captures, and selected screenshots is `deliverables/NooDS-Wii-comparison-bundle.tar.gz`; its checksum is in `deliverables/NooDS-Wii-comparison-bundle.sha256`.
