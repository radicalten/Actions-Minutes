# ARM7TDMI execution audit — milestone 6 baseline retained in milestone 7

**Historical stage:** current milestone 10 totals are ARM9 6,498 (6,178 PASS / 320 FAIL), ARM7 5,174 (5,151 PASS / 23 FAIL). ARM9 now has separately named privileged/IRQ helpers too. See [current expansion](arm9-milestone8.md); stage-specific counts below are historical.


**Historical baseline below:** milestone 7 appends 1,052 passing cases, taking ARM7 to 4,630 (4,607 PASS / the same 23 mismatches). The original 188 cases and IDs are unchanged. See [current privileged/IRQ coverage](arm7-privileged.md). The counts below describe the original milestone-6 baseline.

## What was missing

The former 3,390-case group **did execute on the DS ARM7**, using an ARM7TDMI-compiled image, but it used the same expectations as ARM9. That was useful shared-instruction coverage, not sufficient evidence of ARM7TDMI-specific correctness. ARM7TDMI implements ARMv4T; the gap is not primarily a list of unique ARM7 opcodes. It includes processor-specific behavior of otherwise shared instructions.

This update adds a separate **188-case / 23-helper ARM7 execution profile**, taking ARM7 to **3,578 vectors across 283 families**. ARM9 remains at 3,486 vectors. Aligned/control cases are intentionally included alongside distinguishing cases; not every one of the 188 cases has different ARM9 semantics.

## Added checks

| Area | Cases | Checks and boundaries |
|---|---:|---|
| ARM and Thumb LDR/LDRH/LDRSH | 96 | Four input words × offsets 0–3 × three instructions × two states. Word reads rotate; odd LDRH rotates the aligned halfword in 32 bits; odd LDRSH sign-extends the addressed **byte**. An adjacent guard word is checked. |
| ARM and Thumb STR/STRH | 64 | Four input words × offsets 0–3 × two instructions × two states. Forced-aligned stores; halfword preservation of the other half; adjacent guard. |
| ARM SWP | 16 | Four input words × offsets 0–3. Rotated read result, unrotated forced-aligned write of the inverted input word. |
| ARM PC/pipeline values | 5 | MOV PC (+8), register-shifted PC as Rm and Rn (+12), STR PC (+12), STM PC (+12). Compares address differences, not fixed link addresses. |
| Thumb PC values | 4 | High-register MOV/ADD with PC at both halfword positions in an aligned word. Checks PC+4 without incorrectly rounding it down. |
| ARMv4T PC loads / interworking | 3 | ARM LDR PC and LDM PC keep ARM state with an odd target; Thumb POP PC keeps Thumb state with an even target. Also checks T or restored SP as appropriate. These distinguish ARM7 behavior from ARM9 load-to-PC interworking. |
| **Total** | **188** | All execute on ARM7; no ARMv5 instructions are introduced. |

Memory operands currently use aligned scratch storage on the ARM7 private-WRAM stack, then deliberately offset addresses. This is not an exhaustive bus/region, addressing-mode, register-alias, alignment or timing matrix.

## Evidence that this really tests ARM7

`test_arm7_specific.py` verifies all of the following against the actual ROM:

1. All 23 new assembly helper symbols exist in `artifacts/arm7.elf` and **do not exist** in `artifacts/arm9.elf`.
2. Every recorded ARM7 helper PC matches its symbol in the ARM7 ELF and lies within the reserved ARM7 image range `0x02200000..0x022fffff`.
3. All 188 specific records finish, not merely appear in a static manifest.
4. Corrupting the expectation of new case 3,391 **inside the ARM7 ROM payload** adds exactly one failure: baseline 23 → injected 24. Expectations in the distributed ROM are unchanged.
5. Replacing only the first new ARM7 helper with an infinite loop leaves the 3,390-case prefix PASS, case 3,391 INTERRUPTED, and the remaining 187 cases NOT_RUN. It is never scored as a pass.
6. The real lower-screen ARM7 explorer jumps to the actual first failure, including its ARM7 helper PC.

ARM9 links metadata-only code for displaying ARM7 case descriptions. It does not link or execute the new ARM7 assembly helpers. The seven-word vector-table ABI and ten-word execution-trace ABI remain distinct.

ARM7 runs its CPU checker during its own startup, even when the host selects another group. Group 1 consumes ARM7's completion mailbox; it does not execute ARM7 instructions on ARM9. Thus an isolated ARM9 run may still contain real ARM7 background trace evidence, while its group score excludes ARM7.

## Actual current result—not a manufactured all-pass

On the tested py-desmume 0.0.9 / DeSmuME 0.9.12 core:

- Shared ARM7 prefix: **3,390 PASS**.
- New ARM7 profile: **165 PASS, 23 reference mismatches**.
- ARM7 total: **3,555 PASS, 23 FAIL, 0 unrun**.
- ARM9: **3,486 PASS, 0 FAIL**.
- Full supervised suite: **18/25 groups = 72%**; five returned group failures, two native division crashes, zero unrun.

The 23 mismatches are:

- **12 odd LDRH results** (six ARM, six Thumb): the tested core returns the aligned halfword instead of the expected 32-bit rotation.
- **8 odd LDRSH results** (four ARM, four Thumb): it returns a signed halfword instead of a signed byte.
- **3 PC results**: register-shifted Rn=PC, STR PC and STM PC return a +8 delta instead of the reference +12.

Some odd-address inputs produce the same numerical result under both rules, so not all odd-address cases fail. For example, case **ARM7:3412** loads from offset 1 of `FFFFFFFF`: expected **FF0000FF**, observed **0000FFFF**.

These are discrepancies against the stated reference, **not physical-hardware-confirmed emulator bugs**. Expectations are retained for investigation. The overall score drops because the new tests expose previously untested behavior, not because earlier passing checks were weakened.

## Reference and verification limits

Expectations follow the locally retained GBATEK reference and these sections:

- [Memory alignments](https://mgba-emu.github.io/gbatek/#armcpumemoryalignments): ARM7 odd LDRH/LDRSH versus ARM9; LDR/SWP rotation; forced-aligned stores.
- [Data processing](https://mgba-emu.github.io/gbatek/#armopcodesdataprocessingalu): R15 as Rm/Rn, register-shift pipeline values.
- [Single transfer](https://mgba-emu.github.io/gbatek/#armopcodesmemorysingledatatransferldrstrpld) and [block transfer](https://mgba-emu.github.io/gbatek/#armopcodesmemoryblockdatatransferldmstm): stored PC values.
- [Thumb register operations](https://mgba-emu.github.io/gbatek/#thumbopcodesregisteroperationsalubx) and [multiple transfers](https://mgba-emu.github.io/gbatek/#thumbopcodesmemorymultipleloadstorepushpopandldmstm): Thumb PC and POP behavior.

**No exact ARM7TDMI QEMU model is available in the installed QEMU.** The new profile is intentionally excluded from the ti925t proxy oracle: its pass/fail would not establish ARM7-specific correctness. Shared vectors still run on that explicitly labeled proxy and ARM946; the ARM9-specific oracle remains separate. No real-DS execution has been performed.

ARM7 multiplication flags that are undefined/destroyed are not converted into arbitrary exact expectations. Shared multiplication tests retain their defined-bit masks. Exceptions, banked registers, IRQ entry/return, SWI/BIOS contracts, remaining PC/register aliases, operand encodings, region-specific memory behavior and timing still need dedicated coverage. The user selected CPU behavior for this expansion, not exception/banked-state work. This is a targeted expansion, not a claim of complete ARM7TDMI validation.

## Files and commands

```sh
bash scripts/build.sh
python3 scripts/run_desmume.py --group 1 --output reports/cpu7.json
# Expected suite nonzero exit: the 23 reference mismatches are retained.
python3 scripts/test_arm7_specific.py
python3 scripts/run_resilient.py --output reports/resilient.json
python3 scripts/export_debug.py reports/resilient.json
```

Generator: `scripts/generate_arm7.py`; cases: `src/generated/arm7-vectors.json`; actual assembly: `src/generated/arm7.s`; dispatch: `src/cpu7.cpp`; evidence: `reports/arm7-specific-tests.json` and `arm7-specific-screen.png`.

The debug manifest is now **schema 2**: `vectors` is the full ARM9 list, and `arm7_vectors` is the full ARM7 list. Case identity is **(CPU, ID)**. Shared IDs 1–3,390 retain identical meanings; ARM9 IDs 3,391–3,486 and ARM7 IDs 3,391–3,578 have different meanings. Readers and the ROM explorer select metadata by CPU. The combined manifest fingerprint prevents restoring traces against a different case definition.
