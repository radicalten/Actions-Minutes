# ARM7 privileged state, returns and real IRQ coverage — milestone 7

**Historical stage:** current milestone 10 totals are ARM9 6,498 (6,178 PASS / 320 FAIL), ARM7 5,174 (5,151 PASS / 23 FAIL). ARM9 now has separately named privileged/IRQ helpers too. See [current expansion](arm9-milestone8.md); stage-specific counts below are historical.


## What this closes—and what it does not

Added **1,052 cases / 137 helpers**, all linked and executed only on ARM7. ARM7 now has **4,630 vectors**, with **4,607 PASS and the same 23 retained reference mismatches** on the tested DeSmuME core. All 1,052 new cases pass. ARM9 remains 3,486/3,486.

This closes the **explicit matrices below**, not every ARM7TDMI case. In particular, software-prepared exception returns are not presented as proof of SWI/undefined/abort/FIQ entry. Real IRQ tests are separate. The machine-readable status ledger is `docs/remaining-cases.json`.

| Matrix | Cases | Exact dimensions |
|---|---:|---|
| Banked SP/LR | 240 | All 30 ordered distinct pairs of SYS/FIQ/IRQ/SVC/ABT/UND × SP/LR × 4 contrasting input pairs |
| FIQ R8–R12 | 20 | Five banked registers × 4 input pairs; SYS versus FIQ |
| Banked SPSR | 80 | All 20 ordered distinct pairs of the five exception modes × 4 PSR pairs |
| SPSR field writes | 40 | Five exception modes × flags-only/control-only writes × 4 PSR pairs; unchanged fields and restoration checked |
| CPSR mode/flag preservation | 96 | Six privileged modes × all 16 NZCV combinations; IRQ/FIQ masked |
| Exception-return instructions | 480 | Five exception modes × MOVS PC,LR / SUBS PC,LR,#4 / LDM {PC}^ × ARM/Thumb SYS target × all 16 NZCV combinations |
| Real IRQ entry from ARM/Thumb | 32 | Two source instruction states × all 16 NZCV combinations |
| User privilege and IRQ escape | 16 | All NZCV combinations: User flag write allowed, CPSR control write ignored, User/System SP shared, IRQ entry from User captured |
| IRQ masks and pending delivery | 48 | CPSR.I / IME / IE gates × all 16 NZCV combinations; no callback before release, exactly one callback afterward |
| **Total** | **1,052** | IDs **ARM7:3579–4630**; all earlier IDs unchanged |

New pure architectural cases occupy IDs 3,579–4,534; real NDS IRQ cases occupy 4,535–4,630. The ARM7 profile now has 1,240 cases beyond the 3,390 shared prefix, across 160 helper families (420 including shared families).

## Software-prepared return tests

`arm7_privileged.py` changes modes with IRQ/FIQ masked, saves original SP/LR/SPSR and FIQ registers as needed, installs contrasting values, and checks bank isolation. All helpers restore caller mode, PSR, stack and saved registers. SYS represents the User/System physical SP/LR bank; the real User-mode cases independently verify that sharing.

For return tests, the helper explicitly prepares the source exception mode and its SPSR, then executes a real exception-return instruction. ARM targets preserve flags. Thumb targets use a non-flag-setting PC read followed by BX back to the ARM observer. The trace's auxiliary value is **CPSR after that BX**, so T is then zero. Execution of the Thumb target establishes the state transition; the report does not falsely claim to read T=1 using an ARM MRS after BX.

These checks validate return instruction semantics and restored mode/NZCV, not hardware exception entry or automatically generated exception LR offsets. Original SPSR/LR values are restored after observation. Only defined PSR bits are compared where needed; no undefined multiplier flag expectation was introduced.

## Real timer IRQ tests

`arm7_irq.py` uses ARM7 timer 3 and the real NDS BIOS IRQ callback at `0x0380fffc`. It does **not** simulate IRQ entry with an MSR.

The callback captures CPSR control and SPSR, stops timer 3, acknowledges its IF bit, and increments a callback counter. Normal ARM/Thumb cases verify the interrupted instruction state, source mode, all NZCV flags, IRQ mode, I/F controls, and exactly one callback. The helper executes a bounded straight-line NOP window rather than a flag-changing polling loop.

For User mode, privileged setup first installs opposite NZCV flags. User mode then writes the requested flags, attempts a forbidden CPSR control write, and samples its SP. Timer IRQ captures the User SPSR. **Only for this deliberate User escape**, the callback rewrites the saved return mode to SYS so the test can clean up. That is a controlled escape, not a claim that ordinary IRQ return changes User to SYS.

The three gating cases let timer 3 become pending while CPSR.I, IME or IE blocks delivery. A pre-release callback count must be zero. After release, exactly one callback must arrive. An early callback sets an explicit auxiliary error bit, even if the eventual SPSR value otherwise looks correct.

Auxiliary result encoding: callback CPSR control byte; callback-count error in bits 16+; User/SP mismatch at bit 17; pre-release callback error at bit 18. Normal expected value is `000000D2` (ARM IRQ mode, I/F masked). A missing callback leaves a sentinel SPSR and count error, never a synthetic PASS.

### Side effects and limits

These cases run during the suite's **quiescent ARM7 startup**, not during another device test. They save/restore the BIOS callback, IE/IME, timer register image and CPU context. Timer-3 IF is acknowledged; an arbitrary pre-existing pending interrupt or running timer phase cannot be reconstructed. They are not safe as a drop-in test during active game peripherals. The timer is a stimulus, **not a cycle-accuracy oracle**.

The callback sees state after BIOS dispatch, not a raw vector-entry trace. No exact IRQ LR-offset, interrupt latency, priority arbitration, nesting or FIQ-line validation is claimed. Failure of the IRQ-based User escape can leave the CPU unable to return to privileged reporting; host timeout handling remains necessary on a broken implementation.

## Independent checks and fault injection

- All 1,052 cases pass on the tested DS ARM7 implementation.
- The **956 portable, software-prepared cases** additionally pass QEMU ARM946 and the explicitly labeled ti925t ARMv4T proxy. They are **not exact ARM7TDMI verification**.
- NDS timer/User/gating cases are intentionally excluded from QEMU's Integrator/CP oracle.
- Fifteen corrupted expectations—covering each new category—produce their exact failing vector IDs.
- Suppressing the callback event counter makes all 96 IRQ cases fail, while the portable cases still pass.
- Releasing CPSR.I too early makes exactly the 16 masked-I cases fail, with the early-delivery error bit set.
- Existing address-attribution tests check every new helper against ARM7 ELF symbols and confirm those symbols are absent from ARM9 ELF.
- Existing crash/restart, journal preservation and all 12 lower-screen pages remain tested.

```sh
python3 scripts/run_qemu_privileged.py
python3 scripts/test_privileged.py
python3 scripts/test_arm7_specific.py
python3 scripts/run_resilient.py --output reports/resilient.json
python3 scripts/export_debug.py reports/resilient.json
```

Reference: GBATEK [register set](https://mgba-emu.github.io/gbatek/#armcpuregisterset), [PSR transfer](https://mgba-emu.github.io/gbatek/#armopcodespsrtransfermrsmsr), and [exceptions](https://mgba-emu.github.io/gbatek/#armcpuexceptions). No physical DS baseline or exact ARM7 QEMU model is available. Earlier 23 alignment/PC reference mismatches remain failures and are described in `arm7-specific.md`.
