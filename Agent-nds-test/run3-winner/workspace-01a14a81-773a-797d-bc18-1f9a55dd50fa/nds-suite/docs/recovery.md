# Failure continuation and checkpointed recovery — milestone 10

## What changed

Earlier ROMs deliberately halted on hardware-group assertions. The ROM now treats an ordinary returned assertion or ROM-detected timeout as a **group outcome**, restores the framework context, rebuilds a baseline, and advances to the next selected group. A group remains internally fail-fast: assertions after its first failure are not executed or counted as passing.

There are two recovery mechanisms, and they must not be confused:

1. **In-ROM recovery:** for control flow reaching `fail_test`, `timeout_test`, or `timeout_rw`.
2. **Host recovery:** for a hard CPU hang or native emulator process crash. No ROM code can execute after its host process has died.

The second mechanism requires the supplied DeSmuME-based runner. Opening the ROM directly in an arbitrary emulator does not automatically give that emulator a restart supervisor. The unmodified ROM still encounters SIGFPE in the bundled old DeSmuME division implementation; these tests are not removed or weakened.

## In-ROM baseline restoration

`src/fail.s` saves the framework's registers, SP, LR and CPSR control state before each group. On a returned failure it disables ARM9 IME, restores the normal TCM mapping/control configuration, then unwinds to that saved frame. `in_test` prevents configuration errors outside a valid frame from attempting to unwind.

`src/reset9.cpp` establishes a controlled baseline before/after groups:

- masks interrupts, disables IME/IE and acknowledges IF;
- stops the four DMA channels and timers;
- clears IPCSYNC, FIFO/error state;
- restores default shared-WRAM/VRAM assignments;
- restores the standard ARM9 control, TCM and display setup.

This is an audited reset of the facilities used by the current groups, **not a universal machine reset or exception handler**. Future tests must audit additional state they modify. A CPU executing invalid control flow, an unhandled exception, or a broken memory subsystem may never reach this code.

### Cooperative ARM7 reset

ARM9 and ARM7 use a dedicated main-RAM handshake at `0x023ee000`, independent of the IPC registers under test:

| Word | Owner | Meaning |
|---|---|---|
| 0 | ARM9 | requested reset epoch |
| 1 | ARM7 | acknowledged/quiesced epoch |
| 2 | ARM9 | release epoch |
| 3 | ARM7 | command-loop-ready epoch |

ARM7 polls the request in its command loop, RW service loops and relevant wait loops. On a new epoch it masks interrupts, clears IPC state, acknowledges, waits for release, resets its stacks, and re-enters the command loop. It does not rerun the CPU suite or overwrite the startup snapshots on a cooperative restart.

Before groups requiring ARM7 (IPC, WRAM, VRAM), ARM9 waits for this handshake with bounded loop budgets. A failed handshake is recorded as a ROM timeout with code `0xfffffff0`, and the suite continues to later groups. It is not a pass, and a later ARM7-dependent group may also time out if the ARM7 remains unreachable.

The original assertion expressions are retained. Poll insertion and baseline resets do change scheduling; this is not cycle-exact equivalence to the interactive upstream ROM and is not hardware-certified.

## Host checkpoint/restart workflow

Recommended command:

```sh
python3 scripts/run_desmume.py --sequential \
  --output reports/sequential.json --screenshot reports/score-screen.png
```

`--sequential` now delegates to `scripts/run_resilient.py` by default. `--no-restart` explicitly requests one unsupervised boot. The isolated-per-group runner remains available by omitting `--sequential`.

The ROM publishes a RUN/BEGIN ledger and gives frame-based hosts a busy-delay observation window before executing a group. The worker writes an atomic checkpoint only when:

- the score protocol is valid;
- exactly the current group is RUN;
- all preceding groups have a classified outcome;
- later groups remain unrun;
- ledger and score counters agree.

If a native signal or wall/frame timeout occurs, the supervisor reads the last valid BEGIN checkpoint. It records the current group as CRSH or HTIM, starts a fresh emulator process, and resumes at the **next** group with the previously observed ledger. It never retries a crash indefinitely, credits it as PASS, or silently skips a group before attempting it.

If there is no valid checkpoint, progress is inconsistent, the worker has a Python/harness error, or the recovery limit is exhausted, the supervisor stops with an infrastructure error rather than inventing an attributable result. Native signals and hard timeouts are distinct from ROM assertions.

### Resume seed

A temporary ROM copy contains an optional 58-word seed block, found by the unique two-word marker `0x52534d34, 0x4e445353`:

1. marker words (2);
2. next group index (1);
3. enabled flag (1);
4. ARM9 passed/failed and ARM7 passed/failed vector counters (4);
5. 25 state/assertion-or-cause pairs (50).

The distributed `suite.nds` has the enabled flag cleared. Host patching affects a temporary payload copy, not the DS header or distributed ROM. The ROM validates the prefix/future ledger state before importing it and recomputes its group counters.

On a resumed run the top screen explicitly says **SUITE FINISHED - RESUMED**. This is a real ROM-rendered screen built from observed checkpoints, not an image composed by the host. Per-boot logs, checkpoints, temporary-ROM hashes and outcomes are retained under the report's same-stem directory.

CPU traces and the executed C++ journal are serialized and restored through the milestone-5 host barrier, with historical CPU records labeled. Old boot/CP15 snapshots are not serialized. Group outcomes and vector totals survive; boot observations shown after a restart belong to the current boot. A resumed execution is not represented as one uninterrupted physical boot.

## Outcome and score semantics

The v2 score mailbox layout is unchanged. Word 8 / JSON `score.failed` is now explicitly the number of **resolved non-pass groups**, and the screen calls it **GROUPS NONPASS**. The ledger distinguishes:

| State | Label | Meaning |
|---|---|---|
| 0 | WAIT | unrun |
| 1 | PASS | group returned successfully |
| 2 | FAIL | returned assertion failure |
| 3 | TIME | ROM timeout, including failed reset handshake |
| 4 | SKIP | outside selected plan |
| 5 | RUN | executing/armed |
| 6 | CRSH | host observed native process signal |
| 7 | HTIM | host observed frame/wall timeout |

The second ledger word is an assertion ID for FAIL/TIME, the positive signal number for CRSH, or timeout cause 1 (frames) / 2 (wall clock) for HTIM. Group-level timeout detail is not a CPU assertion number.

`passed + nonpass + unrun = planned`. A complete score means every selected group has a classified outcome, **not that every internal assertion executed or that every group passed**. The percentage remains passed/planned; crashes and timeouts stay in the denominator.

The isolated runner's aggregate still reports assertion failures, crashes and timeouts in separate fields. The resumed ROM's NONPASS field sums those categories. Compare the per-group ledger, not just two fields named `failed` from different report types.

## Executed validation

On the bundled DeSmuME 0.9.12 core:

- The ROM continues beyond LDM/STM, IPCFIFO and DIV32/32 assertion failures in the first boot.
- IPCFIFO IRQ passes after IPCFIFO fails, without a host restart between them.
- Native division crashes occur at groups 20 and 21 (SIGFPE).
- The supervisor restarts twice and reaches groups 22, 23 and 24.
- Final ledger: **17 PASS, 6 FAIL, 2 CRSH, 0 unrun; score 17/25 = 68.00%**.
- ARM9 executes **6,498: 6,178 PASS / 320 FAIL**; ARM7 executes **5,174: 5,151 PASS / 23 FAIL**.

`scripts/test_recovery.py` additionally executes explicitly temporary injection ROMs:

1. Replaces the two native-crashing division entries with known early assertions. The ROM finishes all 25 groups in **one boot**, demonstrating returned-failure continuation. This is a fault-injection test, not a pass claim for the replaced division tests.
2. Inverts the first WRAM check after ARM7 entered its RW service. The failed WRAM group leaves ARM7 waiting, the cooperative handshake recovers it, and the following VRAM group passes.
3. Injects an infinite ARM9 loop into WRAM. The host records HTIM, restarts, and runs the following groups. The earlier native division crashes are also retained in that run.

The final production ROM contains none of these injections. Failures/hangs can still prevent a bare-ROM score if the emulator cannot execute recovery code. Hardware validation and additional emulator adapters remain outstanding.

## Milestone 5 diagnostic restoration

BEGIN checkpoints now include immutable SHA-256-verified CPU trace and C++ journal references. Score state 6 pauses the resumed boot for restoration; CPU records are marked historical. This supersedes the milestone-4 lack of detailed serialization. See [debugging.md](debugging.md) for the ARM7 race guard, bounded gate, field layouts, and fatal-group evidence limitations. Boot snapshots are still fresh-boot observations.
