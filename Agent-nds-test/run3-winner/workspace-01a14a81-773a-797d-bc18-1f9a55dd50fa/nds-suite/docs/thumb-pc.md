# Thumb PC-relative address generation and literal loads

Development matrix: **216 cases /54 helpers per CPU**,432 new executions PASS.
ARM9 stable IDs9251–9466; ARM7 IDs7927–8142. Source: `scripts/thumb_pc.py`.

## Defined matrix

- ADD Rd,PC,#imm: instruction address modulo4=0/2, Rd=r0/r1, byte
  immediates0/4/1020, four incoming NZCV patterns:48 cases.
- LDR Rd,[PC,#imm]: same alignment/register/immediate dimensions, static literal
  fixtures0/ffffffff/80000001/12345678, four NZCV patterns:168 cases.
- At alignment2 and offset0, the literal overlaps the next executed halfword.
  Those two forms deliberately use only word46c04728 (BX r5 followed by NOP).
  This is a legal executable fixture, not four arbitrary data words. The matrix
  therefore has42 load helpers, not48. Do not advertise a full Cartesian product.

PC base is `(instruction_address+4)&~3`. ARM wrappers independently calculate
this reference for address-result normalization. ADD reports destination minus
that aligned base; LDR reports the literal word. Destinations are poisoned first.
Auxiliary mask f0000001 checks unchanged NZCV and SP. Literal data is static:
there are no writes to code or assumptions about self-modifying-code coherence.
All helper exits use a register branch around unused literal/padding bytes.

Reference: GBATEK Thumb.6 PC-relative load and Thumb.12 get relative address:
https://mgba-emu.github.io/gbatek/#thumbopcodesmemoryloadstoreldrstr
https://mgba-emu.github.io/gbatek/#thumbopcodesmemoryaddressingaddpcsp

## Agent-facing contract

Scope `thumb_pc`; metadata includes operation, instruction_alignment_mod4,
literal_value, offset_bytes, destination_register, expected_thumb_opcode,
result_interpretation, guard_policy, CPU-qualified ID and scored group/ISA.
Existing runtime expected/observed/XOR/masked-XOR and per-record boot provenance
remain unchanged. Manifest/registry/score3; trace4. No manual interaction needed.

## Evidence

- `thumb-pc-tests.json`:14 scenarios PASS. Each CPU baseline checks216 executed
  results,54 actual target encodings, instruction placement and every literal's
  exact ROM bytes. Mutations independently change54 primary expectations and54
  flag expectations, then replace actual instructions to test missing loads,
  scaled offsets, wrong destinations and PC-versus-SP confusion.
- `qemu-thumb-pc.json`:216 PASS on ARM9-profile ARM946 and ARM7-profile ARM946
  and ti925t. No exact ARM7TDMI or DS bus/timing claim.
- Prior Thumb SP/memory/ALU and ARM operand2 matrices, CPU-group isolation,
  mixed-boot provenance, protocol, UI and recovery regressions refreshed.
- Byte-identical rebuild verified in `development-rebuild.sha256`.

## Limits and next step

This does not finish high-register PC semantics, literal loads across bus regions,
branches or stack transfers. Branch boundary and stack/multiple-transfer matrices
remain next. Trace capacity9828 now leaves362 ARM9/1686 ARM7 slots: plan capacity
before a larger batch, rather than truncate diagnostics or silently omit tests.

Current totals: ARM9 9466=9146 PASS/320 FAIL; ARM7 8142=8119 PASS/23 FAIL.
Score17/27=62.96%,0 unrun with host recovery of two native crashes. Existing343
CPU discrepancies unchanged. Full release gauntlet and upstream licensing
clearance remain blockers; this is not v1.0.
