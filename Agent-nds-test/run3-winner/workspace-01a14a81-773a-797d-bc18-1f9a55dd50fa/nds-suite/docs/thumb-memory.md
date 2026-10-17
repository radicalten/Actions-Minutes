# Thumb aligned memory — development, not exhaustive

Added **1248 cases and 78 helpers per CPU**, all passing in the recorded DeSmuME
run. ARM7 IDs6487–7734; ARM9 IDs7811–9058. Existing CPU-qualified IDs are stable.
Source/model: `scripts/thumb_memory.py`; reference:
https://mgba-emu.github.io/gbatek/#thumbopcodesmemoryloadstoreldrstr

## Finite matrix

78 forms × four data patterns × four incoming NZCV states:

- Immediate word/byte/halfword LDR/STR: encoded offsets0,1,max, correctly scaled.
- SP-relative LDR/STR: byte offsets0,4,1020.
- Register-offset LDR/LDRB/LDRH/LDRSB/LDRSH: three offsets, normal destination,
  destination=base and destination=offset aliases.
- Register-offset STR/STRB/STRH: three offsets.

Patterns:80017f80,ff007fff,12345678,80008000. NZCV:0,20000000,90000000,f0000000.
Primary load results include signed extension. Store results read the entire word
so untouched byte/halfword lanes are checked. Ordinary load destinations are
poisoned with the complement before execution; aliases retain their live address
input. Adjacent words contain a5a5a5a5 guards.

Auxiliary mask f000000f checks NZCV plus error bits:0 adjacent-word guards,
1 base preservation,2 offset-register preservation,3 SP preservation. A register
that is the load destination is exempt from its preservation check. SP is checked,
then restored from protected r7 so a detected SP-clobber fault can safely return.

Manifest metadata includes `verification_scope=thumb_memory`, operation,
addressing, offset_bytes, access_bytes, destination_register, alias,
expected_thumb_opcode, load_destination_poisoned and guard_policy. JSON/CSV retain
CPU/group/ISA, inputs, expected and observed primary/auxiliary values, masks, XOR
and masked-XOR differences. No OCR or interactive input is required.

## Executed verification

`reports/thumb-memory-tests.json`:24 PASS scenarios,12 per CPU. Baselines verify
all1248 results and all78 target encodings. Mutations test primary expectations,
NZCV expectations, missing loads, wrong store width, scaling, sign extension,
alias destination, adjacent-word corruption, and SP/base/offset clobbers. The last
three retain a correct primary result while failing the preservation check.

`reports/qemu-thumb-memory.json`:1248 PASS for ARM9 profile on ARM946, and ARM7
profile on ARM946 and ti925t. ti925t is only an ARMv4T proxy; no exact ARM7TDMI or
DS bus/timing/hardware claim. QEMU executes the generated helper code, not merely
its Python expectation model.

## Resume correctness — trace protocol4

Record state uses low byte0–3 and historical bit0x100. Restore marks existing
nonempty records historical; fresh begin/end clears that bit. Header word11 means
imported history exists, **not** that every record is historical. ARM9 reattaches
its runtime writer to restored memory before executing the next ISA group.

`test_trace_provenance.py` seeds observed prior ARM results, executes fresh Thumb,
and checks all9058 ARM9 records, per-record provenance and unchanged320 failures.
It caught and now guards against a null runtime writer after partial restore.
The fixture is explicitly synthetic; it is not an uninterrupted baseline run.
Registry/score/manifest remain3; older trace versions are rejected.

## Still open

This is aligned scratch-memory coverage, not the complete load/store space.
PC-relative literal loads, broader register/offset combinations, stack/multiple
transfer edges, misalignment across bus regions, abort behavior, high-register
PC/SP forms and branch-range matrices remain priorities. Existing343 CPU
mismatches are unchanged. Full release gauntlet and packaging remain pending.
