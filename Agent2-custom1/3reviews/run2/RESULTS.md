# v3 NooDS-Wii: the ARM JIT is faster than the interpreter

**Headline: `NooDS-Wii.dol` (JIT=1) runs rockwrestler.nds at 1.64x–1.67x the speed
of the interpreter-only build, with identical guest state at every checkpoint.**
Shipped binary: `/home/user/v3-jit.dol`, md5 `ba410c11644ba39502ee0374e65a3ee0`.

| frame | interpreter ticks | v3 JIT ticks | speed-up | cycles / RAM hash / FB hash |
|---|---|---|---|---|
| 10  | 191,411 | 114,338 | 1.67x | identical |
| 30  | 619,653 | 378,361 | 1.64x | identical |
| 60  | 1,274,376 | 774,294 | 1.65x | identical |
| 90  | 1,942,061 | 1,170,242 | 1.66x | identical |
| 120 | 2,609,744 | 1,566,267 | 1.67x | identical |
| 140 | 3,054,834 | 1,826,305 | 1.67x | identical |

Measured by `tools/run_ab.sh` (headless Dolphin, fresh SD card, guest writes a
report row every 10 frames).  The interpreter column is `ab-N4int-report.txt`;
the JIT column is `ab-Final150-clean-report.txt`.  All 14 common checkpoints
match on guest cycles **and** main-RAM hash **and** framebuffer hash.

Long run: 670 frames (150 s wall), `flushes=0`, 35,472,373 dispatches, and the
same numbers from two independently built binaries.
Absolute context, stated plainly: 670 frames is 11.2 s of guest time, so the
emulator is still ~13x slower than realtime on this ROM — the result here is
*relative to our own interpreter build*, which is what this task asked for.

## Correctness evidence

* **shadow verifier on the shipped bytes** — re-runs every compiled block through
  the interpreter and compares instruction by instruction:
  **`bad=0` over 11,601,919 blocks** (`ab-D16-verify.txt`; ~11,350 progress lines,
  every one `bad=0`).  Earlier runs on earlier trees: `ab-D11-verify.txt`,
  `ab-D14-verify.txt` (11,935,743 blocks, `bad=0`).
* **differential emitter sweep on the host** (24 operand shapes, register/flag
  corners): **247,412 checked, 0 failed, 1,658 not modelled**
  (`sh tools/host/run_tests_ppc.sh`).
* **on-device self-test** at boot: `programs=4 fail=0`, all four programs matching
  cycles/pc/CPSR (`ab-D16-verify-stage.txt`).
* **rebuild determinism** — the shipped sources rebuild byte-for-byte to
  `ba410c11644ba39502ee0374e65a3ee0` (`v3src-backup/v3-src-1940-final.tar.gz`,
  `tools/restore_v3.sh --verify`), so the evidence above belongs to exactly the
  bytes that were measured.

## What was wrong in the two original implementations

1. **The refusals were a red herring.** `isPsrOp()` (bits 27..23 = `00010`) also
   matches the register-operand form of CMP/CMN/TST and BX, so the gate refused
   *and* the dispatch misrouted them: `cmp r0, r2` emitted an MRS (the CPSR into a
   guest register) and no flags.  Fixed with a narrow `isPlainRegDp()` predicate;
   refusals fell 97,632 → 318.  **This changed no measurable time** — it was a
   correctness fix, and it is what let the sweep go to zero failures.

2. **The real cost was the delegates.** Half of every block's instructions were
   handed to the interpreter inline: each one paid a full register-file writeback,
   an interpreter call and a reload — ~30 register moves around one instruction.
   The guests were `str r3,[r1],#4`, `ldrb r3,[r2],#1`, `ldrh r3,[r2,#4]`,
   `ldr r2,[pc,#0x4E0]`, `stmfd/ldmfd`.

## What was built

* `emitTransfer()` — LDR/STR/LDRB/STRB compile to address arithmetic plus one
  memory-helper call, with the write-back, base and destination kept in the
  block's registers.  Cycle charge is the handler's own formula, computed at
  compile time.
* `emitMiscTransfer()` — LDRH/STRH/LDRSB/LDRSH (2.5M of the 2.8M delegated
  instructions, entry-weighted), including the ARM7 misaligned-read rotation.
* Post-indexed transfers with **P=0,W=0**: the ARM ARM calls it unpredictable,
  NooDS routes it to the same post-with-write-back handler as P=0,W=1 — and the
  guest depends on the write-back.  Refusing it (the "safe" choice) hung the ROM;
  the emitter now mirrors NooDS.
* PC-relative bases (`ldr r2,[pc,#imm]`) compile, because `getReg(15)` already
  reproduces the handler-time `pc + 8`.
* Block chaining was already carrying the dispatch load: at frame 10 there are
  **3,641,796 chain hops against 425,452 dispatcher runs** — ~90 % of block
  transitions never touch the dispatcher (`ab-D15-hits.txt`, `HITS=1`).

## What was tried this round and rejected

**Inlining the page-map lookup (a "fast memory access" path) into the emitted
code — built, verified, and then reverted.**  The emitted code looked up
`readMap9A/readMap7[addr>>12]`, null-checked, folded the 12-bit page offset into
the address with `rlwinm`, and used `lwbrx/lhbrx/stwbrx/sthbrx`; the helper call
became the cold path.  It was host-verified byte-identical against the helper
form (`tools/host/xfer_probe.cpp`, `fast` vs `nofast` diff empty, including
bit-12 addresses) and `VERIFY=1` clean — **and it was 1 % slower**
(`ab-D7`: 115,514 ms vs 114,338 ms at frame 10; 775,359 vs 774,294 at frame 60).
Conclusion: the helper `bctrl` is *not* the bottleneck, so this avenue is closed.
Two real bugs were found and fixed on the way, both worth remembering:

* the `rlwinm` offset mask must keep **MB = 20** for every access size (only ME
  moves: 31/30/29).  A per-size MB keeps bit 12, so a 16-bit access at
  `addr&0xFFF == 0x804` reads past the page just looked up → silent hang
  (`ab-D5`, empty report).
* a hand-reversion that leaves the transfer emitters reading the loaded value
  from the wrong register corrupts guest registers (the helpers return the value
  in **r3**) → heap corruption inside newlib `_free_r`, `PC = 0x800f73a8`.

Two other clean-ups landed in this round: the `pad[12]` chain-stride experiment
was removed from `jit3.cpp` (timing-neutral), and the dead `Cfg::offReadMap` /
`offWriteMap` fields left over from the fast path were deleted (the rebuild was
re-verified and re-timed — identical numbers to the byte).

## Where the remaining time goes

Blocks still average ~2 instructions, so the block boundary is paid per two
instructions; and ~90 % of transitions already chain, so the dispatcher is not
where the time is.  The remaining delegates are STM/LDM (function prologues and
epilogues, including the `ldmfd sp!,{...,pc}` returns) and SMLAL — the same shape
of work as the transfer slice that produced the 1.65x, and the next slice.

## Known limitation (not exercised by this ROM)

Self-modifying code is not protected in this build: there is no store-path hook and
no per-dispatch word checksum, so a guest that writes to the memory it is executing
keeps running the stale compiled block until a map change or VRAM remap flushes the
cache (`invalidateAll()` is called from `Memory::updateMap9/7` and `updateVram`
only).  `rockwrestler.nds` never does this — 11.6M verified blocks, `bad=0` — but a
title that patches its own code is not safe here.  Fix candidates and their cost are
listed in `notes-jit3.md` §1.7; any of them changes the binary, so it needs a fresh
`VERIFY=1` run and re-timing.

## Files

* `/home/user/v3-jit.dol` — the deliverable (`ba410c11644ba39502ee0374e65a3ee0`)
* `notes-jit3.md` — how the JIT works, how it compares with the two predecessors,
  and how to recreate this environment from scratch
* `NooDS-Wii.dol` — the same file, freshly built in this tree
* `NOTES.md` — full working notes: ABI traps, harness recipe, ledger caveats,
  and the dated log of every experiment
* `tools/host/xfer_probe.cpp` — compiles a single load/store, prints the emitted
  PowerPC and runs it against stub memory (the differential sweep cannot see
  memory instructions, so this is the instrument that does)
* `/home/user/v3src-backup/v3-src-1940-final.tar.gz` — source snapshot of exactly
  this build
* `/home/user/archive/fastmem-keep-armjit.cpp` — the reverted fast-memory
  emitter, kept for reference

## 2026-10-16 — ARM oracle slice (build `39b8fe054c1f7d95cf3ce4efaad39116`)

| check | result |
| --- | --- |
| mGBA suite, ARM (207 vectors) | jit vs suite FAIL=0, interpreter vs suite FAIL=0, shipped-on-own-core FAIL=0, skipped=0 |
| mGBA suite, Thumb (95 vectors) | jit vs suite FAIL=0, NooDS-model vs suite FAIL=0, jit-vs-model=0 |
| host sweeps, ARM/PPC | OK: 247411 checked, 0 failed, 1659 not modelled |
| host sweeps, Thumb | OK: 538440 checked, 0 failed, 49152 not modelled |
| self-test (Wii) | programs=10 fail=0 at 1 and 4 instructions per dispatch |
| VERIFY=1 (Wii) | blocks=29,576,191 bad=0 |
| A/B 150 s vs interpreter | cycles identical at f10/f30/f60/f120, 1.63–1.66x, flushes=0 |
| delegation census | unchanged (dp=6 ldrstr=3 ldmstm=24 branch=0 coproc=0 other=21) |

Fixed: the ARM7TDMI multiply C flag on ARM7 (26/72 suite values were wrong) and
RSCS not being able to clear C/V (6 carry rows, in the interpreter *and* in
jit3's emitter, which had mirrored it). Details in `NOTES.md` and
`notes-jit3.md` Part 4c.
