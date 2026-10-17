# Verification summary — milestone 1

This is an incomplete baseline; see readme.md and coverage.json.

ROM SHA-256: `08e77fde53c4df129030bf0ea02655958c9a27cb2c28572d218768bf78c7498c`

653 CPU vectors pass on each DS processor in the tested emulator. QEMU arm946 and ti925t proxy comparisons pass; exact QEMU ARM7TDMI verification is unavailable.

| Group | Result | Assertion / process code |
|---|---|---|
| CPU ARM9 ALU | PASS | 0 |
| CPU ARM7 ALU | PASS | 0 |
| CONDITION CODES | PASS | 0 |
| CLZ | PASS | 0 |
| QADD, QSUB | PASS | 0 |
| QDADD, QDSUB | PASS | 0 |
| SMULxy | PASS | 0 |
| SMLAxy | PASS | 0 |
| SMULWy | PASS | 0 |
| SMLAWy | PASS | 0 |
| SMLALxy | PASS | 0 |
| BLX | PASS | 0 |
| LDR r15, POP {r15}, LDM {r15} | PASS | 0 |
| LDM / STM | FAIL | 4 |
| IPCSYNC | PASS | 0 |
| IPCFIFO | FAIL | 37 |
| IPCFIFO IRQ | PASS | 0 |
| SQRT 32 | PASS | 0 |
| SQRT 64 | PASS | 0 |
| DIV 32/32 | FAIL | 21 |
| DIV 64/32 | EMULATOR_CRASH | -8 |
| DIV 64/64 | EMULATOR_CRASH | -8 |
| WRAM CNT | PASS | 0 |
| VRAM CNT | PASS | 0 |
| TCM | FAIL | 0 |

Eight host checks and three negative execution scenarios pass. No real DS hardware run was performed. Failures/crashes are not suppressed.
