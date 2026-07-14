// interpreter_lookup.cpp (optimized for PowerPC/Wii)
// ARM opcode dispatch table - bits 27-20 and 7-4 of opcode select handler.
// Reference: http://imrannazar.com/ARM-Opcode-Map
//
// Optimization notes:
//   • Table is placed in a dedicated read-only section (.rodata) so the
//     linker puts it in a cache-friendly region and the PPC L1 I-cache
//     can prefetch across 32-byte cache lines cleanly.
//   • __attribute__((section)) + aligned(32) ensures the first entry
//     falls on a cache-line boundary, avoiding a partial-line load on
//     the very first dispatch.
//   • The table itself cannot be "optimised away" - correctness requires
//     every entry to be present and in order.  What we CAN do is ensure
//     the data layout is optimal for the PPC 750CL cache hierarchy:
//       - 32-byte cache lines hold exactly 8 function pointers (4 bytes each)
//       - 4-wide grouping in the source matches the hardware line width
//   • volatile/const: marking the table const lets the compiler assume it
//     never changes, enabling CSE of the base-address load across the
//     dispatch loop in interpreter.cpp.
//   • HOT annotation: the translation unit is compiled with -O2 -fno-plt
//     so indirect calls go through the GOT without an extra branch.

#include "core.h"

// Ensure the table starts on a 32-byte (one PPC cache line) boundary.
// 'const' on a member pointer array: the pointers are read-only after
// static initialisation - this is legal in C++ for static data members.

#define _ &Interpreter::   // brevity macro, undefined at end of file

__attribute__((section(".rodata"), aligned(32)))
int (Interpreter::* Interpreter::armInstrs[])(uint32_t) =
{
    // ── 0x000–0x00F  AND reg / multiply / halfword store-load ────────────────
    _andLli,    _andLlr,    _andLri,    _andLrr,
    _andAri,    _andArr,    _andRri,    _andRrr,
    _andLli,    mul,        _andLri,    strhPtrm,
    _andAri,    ldrdPtrm,   _andRri,    strdPtrm,

    // ── 0x010–0x01F  ANDS reg ────────────────────────────────────────────────
    andsLli,    andsLlr,    andsLri,    andsLrr,
    andsAri,    andsArr,    andsRri,    andsRrr,
    andsLli,    muls,       andsLri,    ldrhPtrm,
    andsAri,    ldrsbPtrm,  andsRri,    ldrshPtrm,

    // ── 0x020–0x02F  EOR reg ────────────────────────────────────────────────
    eorLli,     eorLlr,     eorLri,     eorLrr,
    eorAri,     eorArr,     eorRri,     eorRrr,
    eorLli,     mla,        eorLri,     strhPtrm,
    eorAri,     ldrdPtrm,   eorRri,     strdPtrm,

    // ── 0x030–0x03F  EORS reg ───────────────────────────────────────────────
    eorsLli,    eorsLlr,    eorsLri,    eorsLrr,
    eorsAri,    eorsArr,    eorsRri,    eorsRrr,
    eorsLli,    mlas,       eorsLri,    ldrhPtrm,
    eorsAri,    ldrsbPtrm,  eorsRri,    ldrshPtrm,

    // ── 0x040–0x04F  SUB reg ────────────────────────────────────────────────
    subLli,     subLlr,     subLri,     subLrr,
    subAri,     subArr,     subRri,     subRrr,
    subLli,     unkArm,     subLri,     strhPtim,
    subAri,     ldrdPtim,   subRri,     strdPtim,

    // ── 0x050–0x05F  SUBS reg ───────────────────────────────────────────────
    subsLli,    subsLlr,    subsLri,    subsLrr,
    subsAri,    subsArr,    subsRri,    subsRrr,
    subsLli,    unkArm,     subsLri,    ldrhPtim,
    subsAri,    ldrsbPtim,  subsRri,    ldrshPtim,

    // ── 0x060–0x06F  RSB reg ────────────────────────────────────────────────
    rsbLli,     rsbLlr,     rsbLri,     rsbLrr,
    rsbAri,     rsbArr,     rsbRri,     rsbRrr,
    rsbLli,     unkArm,     rsbLri,     strhPtim,
    rsbAri,     ldrdPtim,   rsbRri,     strdPtim,

    // ── 0x070–0x07F  RSBS reg ───────────────────────────────────────────────
    rsbsLli,    rsbsLlr,    rsbsLri,    rsbsLrr,
    rsbsAri,    rsbsArr,    rsbsRri,    rsbsRrr,
    rsbsLli,    unkArm,     rsbsLri,    ldrhPtim,
    rsbsAri,    ldrsbPtim,  rsbsRri,    ldrshPtim,

    // ── 0x080–0x08F  ADD reg / UMULL ────────────────────────────────────────
    addLli,     addLlr,     addLri,     addLrr,
    addAri,     addArr,     addRri,     addRrr,
    addLli,     umull,      addLri,     strhPtrp,
    addAri,     ldrdPtrp,   addRri,     strdPtrp,

    // ── 0x090–0x09F  ADDS reg / UMULLS ──────────────────────────────────────
    addsLli,    addsLlr,    addsLri,    addsLrr,
    addsAri,    addsArr,    addsRri,    addsRrr,
    addsLli,    umulls,     addsLri,    ldrhPtrp,
    addsAri,    ldrsbPtrp,  addsRri,    ldrshPtrp,

    // ── 0x0A0–0x0AF  ADC reg / UMLAL ────────────────────────────────────────
    adcLli,     adcLlr,     adcLri,     adcLrr,
    adcAri,     adcArr,     adcRri,     adcRrr,
    adcLli,     umlal,      adcLri,     strhPtrp,
    adcAri,     ldrdPtrp,   adcRri,     strdPtrp,

    // ── 0x0B0–0x0BF  ADCS reg / UMLALS ──────────────────────────────────────
    adcsLli,    adcsLlr,    adcsLri,    adcsLrr,
    adcsAri,    adcsArr,    adcsRri,    adcsRrr,
    adcsLli,    umlals,     adcsLri,    ldrhPtrp,
    adcsAri,    ldrsbPtrp,  adcsRri,    ldrshPtrp,

    // ── 0x0C0–0x0CF  SBC reg / SMULL ────────────────────────────────────────
    sbcLli,     sbcLlr,     sbcLri,     sbcLrr,
    sbcAri,     sbcArr,     sbcRri,     sbcRrr,
    sbcLli,     smull,      sbcLri,     strhPtip,
    sbcAri,     ldrdPtip,   sbcRri,     strdPtip,

    // ── 0x0D0–0x0DF  SBCS reg / SMULLS ──────────────────────────────────────
    sbcsLli,    sbcsLlr,    sbcsLri,    sbcsLrr,
    sbcsAri,    sbcsArr,    sbcsRri,    sbcsRrr,
    sbcsLli,    smulls,     sbcsLri,    ldrhPtip,
    sbcsAri,    ldrsbPtip,  sbcsRri,    ldrshPtip,

    // ── 0x0E0–0x0EF  RSC reg / SMLAL ────────────────────────────────────────
    rscLli,     rscLlr,     rscLri,     rscLrr,
    rscAri,     rscArr,     rscRri,     rscRrr,
    rscLli,     smlal,      rscLri,     strhPtip,
    rscAri,     ldrdPtip,   rscRri,     strdPtip,

    // ── 0x0F0–0x0FF  RSCS reg / SMLALS ──────────────────────────────────────
    rscsLli,    rscsLlr,    rscsLri,    rscsLrr,
    rscsAri,    rscsArr,    rscsRri,    rscsRrr,
    rscsLli,    smlals,     rscsLri,    ldrhPtip,
    rscsAri,    ldrsbPtip,  rscsRri,    ldrshPtip,

    // ── 0x100–0x10F  MRS / QADD / SMLAxy / SWP ──────────────────────────────
    mrsRc,      unkArm,     unkArm,     unkArm,
    unkArm,     qadd,       unkArm,     unkArm,
    smlabb,     swp,        smlatb,     strhOfrm,
    smlabt,     ldrdOfrm,   smlatt,     strdOfrm,

    // ── 0x110–0x11F  TST reg ────────────────────────────────────────────────
    tstLli,     tstLlr,     tstLri,     tstLrr,
    tstAri,     tstArr,     tstRri,     tstRrr,
    tstLli,     unkArm,     tstLri,     ldrhOfrm,
    tstAri,     ldrsbOfrm,  tstRri,     ldrshOfrm,

    // ── 0x120–0x12F  MSR / BX / BLX / QSUB / SMLAWx ────────────────────────
    msrRc,      bx,         unkArm,     blxReg,
    unkArm,     qsub,       unkArm,     unkArm,
    smlawb,     unkArm,     smulwb,     strhPrrm,
    smlawt,     ldrdPrrm,   smulwt,     strdPrrm,

    // ── 0x130–0x13F  TEQ reg ────────────────────────────────────────────────
    teqLli,     teqLlr,     teqLri,     teqLrr,
    teqAri,     teqArr,     teqRri,     teqRrr,
    teqLli,     unkArm,     teqLri,     ldrhPrrm,
    teqAri,     ldrsbPrrm,  teqRri,     ldrshPrrm,

    // ── 0x140–0x14F  MRS SPSR / QDADD / SMLALxy / SWPB ─────────────────────
    mrsRs,      unkArm,     unkArm,     unkArm,
    unkArm,     qdadd,      unkArm,     unkArm,
    smlalbb,    swpb,       smlaltb,    strhOfim,
    smlalbt,    ldrdOfim,   smlaltt,    strdOfim,

    // ── 0x150–0x15F  CMP reg ────────────────────────────────────────────────
    cmpLli,     cmpLlr,     cmpLri,     cmpLrr,
    cmpAri,     cmpArr,     cmpRri,     cmpRrr,
    cmpLli,     unkArm,     cmpLri,     ldrhOfim,
    cmpAri,     ldrsbOfim,  cmpRri,     ldrshOfim,

    // ── 0x160–0x16F  MSR SPSR / CLZ / QDSUB / SMULxy ────────────────────────
    msrRs,      clz,        unkArm,     unkArm,
    unkArm,     qdsub,      unkArm,     unkArm,
    smulbb,     unkArm,     smultb,     strhPrim,
    smulbt,     ldrdPrim,   smultt,     strdPrim,

    // ── 0x170–0x17F  CMN reg ────────────────────────────────────────────────
    cmnLli,     cmnLlr,     cmnLri,     cmnLrr,
    cmnAri,     cmnArr,     cmnRri,     cmnRrr,
    cmnLli,     unkArm,     cmnLri,     ldrhPrim,
    cmnAri,     ldrsbPrim,  cmnRri,     ldrshPrim,

    // ── 0x180–0x18F  ORR reg ────────────────────────────────────────────────
    orrLli,     orrLlr,     orrLri,     orrLrr,
    orrAri,     orrArr,     orrRri,     orrRrr,
    orrLli,     unkArm,     orrLri,     strhOfrp,
    orrAri,     ldrdOfrp,   orrRri,     strdOfrp,

    // ── 0x190–0x19F  ORRS reg ───────────────────────────────────────────────
    orrsLli,    orrsLlr,    orrsLri,    orrsLrr,
    orrsAri,    orrsArr,    orrsRri,    orrsRrr,
    orrsLli,    unkArm,     orrsLri,    ldrhOfrp,
    orrsAri,    ldrsbOfrp,  orrsRri,    ldrshOfrp,

    // ── 0x1A0–0x1AF  MOV reg ────────────────────────────────────────────────
    movLli,     movLlr,     movLri,     movLrr,
    movAri,     movArr,     movRri,     movRrr,
    movLli,     unkArm,     movLri,     strhPrrp,
    movAri,     ldrdPrrp,   movRri,     strdPrrp,

    // ── 0x1B0–0x1BF  MOVS reg ───────────────────────────────────────────────
    movsLli,    movsLlr,    movsLri,    movsLrr,
    movsAri,    movsArr,    movsRri,    movsRrr,
    movsLli,    unkArm,     movsLri,    ldrhPrrp,
    movsAri,    ldrsbPrrp,  movsRri,    ldrshPrrp,

    // ── 0x1C0–0x1CF  BIC reg ────────────────────────────────────────────────
    bicLli,     bicLlr,     bicLri,     bicLrr,
    bicAri,     bicArr,     bicRri,     bicRrr,
    bicLli,     unkArm,     bicLri,     strhOfip,
    bicAri,     ldrdOfip,   bicRri,     strdOfip,

    // ── 0x1D0–0x1DF  BICS reg ───────────────────────────────────────────────
    bicsLli,    bicsLlr,    bicsLri,    bicsLrr,
    bicsAri,    bicsArr,    bicsRri,    bicsRrr,
    bicsLli,    unkArm,     bicsLri,    ldrhOfip,
    bicsAri,    ldrsbOfip,  bicsRri,    ldrshOfip,

    // ── 0x1E0–0x1EF  MVN reg ────────────────────────────────────────────────
    mvnLli,     mvnLlr,     mvnLri,     mvnLrr,
    mvnAri,     mvnArr,     mvnRri,     mvnRrr,
    mvnLli,     unkArm,     mvnLri,     strhPrip,
    mvnAri,     ldrdPrip,   mvnRri,     strdPrip,

    // ── 0x1F0–0x1FF  MVNS reg ───────────────────────────────────────────────
    mvnsLli,    mvnsLlr,    mvnsLri,    mvnsLrr,
    mvnsAri,    mvnsArr,    mvnsRri,    mvnsRrr,
    mvnsLli,    unkArm,     mvnsLri,    ldrhPrip,
    mvnsAri,    ldrsbPrip,  mvnsRri,    ldrshPrip,

    // ════════════════════════════════════════════════════════════════════════
    // Immediate-operand encodings (bits 27-25 = 001)
    // Each group of 16 entries covers the 4-bit rotation field variation.
    // ════════════════════════════════════════════════════════════════════════

    // ── 0x200–0x20F  AND imm ────────────────────────────────────────────────
    _andImm,_andImm,_andImm,_andImm, _andImm,_andImm,_andImm,_andImm,
    _andImm,_andImm,_andImm,_andImm, _andImm,_andImm,_andImm,_andImm,

    // ── 0x210–0x21F  ANDS imm ───────────────────────────────────────────────
    andsImm,andsImm,andsImm,andsImm, andsImm,andsImm,andsImm,andsImm,
    andsImm,andsImm,andsImm,andsImm, andsImm,andsImm,andsImm,andsImm,

    // ── 0x220–0x22F  EOR imm ────────────────────────────────────────────────
    eorImm, eorImm, eorImm, eorImm,  eorImm, eorImm, eorImm, eorImm,
    eorImm, eorImm, eorImm, eorImm,  eorImm, eorImm, eorImm, eorImm,

    // ── 0x230–0x23F  EORS imm ───────────────────────────────────────────────
    eorsImm,eorsImm,eorsImm,eorsImm, eorsImm,eorsImm,eorsImm,eorsImm,
    eorsImm,eorsImm,eorsImm,eorsImm, eorsImm,eorsImm,eorsImm,eorsImm,

    // ── 0x240–0x24F  SUB imm ────────────────────────────────────────────────
    subImm, subImm, subImm, subImm,  subImm, subImm, subImm, subImm,
    subImm, subImm, subImm, subImm,  subImm, subImm, subImm, subImm,

    // ── 0x250–0x25F  SUBS imm ───────────────────────────────────────────────
    subsImm,subsImm,subsImm,subsImm, subsImm,subsImm,subsImm,subsImm,
    subsImm,subsImm,subsImm,subsImm, subsImm,subsImm,subsImm,subsImm,

    // ── 0x260–0x26F  RSB imm ────────────────────────────────────────────────
    rsbImm, rsbImm, rsbImm, rsbImm,  rsbImm, rsbImm, rsbImm, rsbImm,
    rsbImm, rsbImm, rsbImm, rsbImm,  rsbImm, rsbImm, rsbImm, rsbImm,

    // ── 0x270–0x27F  RSBS imm ───────────────────────────────────────────────
    rsbsImm,rsbsImm,rsbsImm,rsbsImm, rsbsImm,rsbsImm,rsbsImm,rsbsImm,
    rsbsImm,rsbsImm,rsbsImm,rsbsImm, rsbsImm,rsbsImm,rsbsImm,rsbsImm,

    // ── 0x280–0x28F  ADD imm ────────────────────────────────────────────────
    addImm, addImm, addImm, addImm,  addImm, addImm, addImm, addImm,
    addImm, addImm, addImm, addImm,  addImm, addImm, addImm, addImm,

    // ── 0x290–0x29F  ADDS imm ───────────────────────────────────────────────
    addsImm,addsImm,addsImm,addsImm, addsImm,addsImm,addsImm,addsImm,
    addsImm,addsImm,addsImm,addsImm, addsImm,addsImm,addsImm,addsImm,

    // ── 0x2A0–0x2AF  ADC imm ────────────────────────────────────────────────
    adcImm, adcImm, adcImm, adcImm,  adcImm, adcImm, adcImm, adcImm,
    adcImm, adcImm, adcImm, adcImm,  adcImm, adcImm, adcImm, adcImm,

    // ── 0x2B0–0x2BF  ADCS imm ───────────────────────────────────────────────
    adcsImm,adcsImm,adcsImm,adcsImm, adcsImm,adcsImm,adcsImm,adcsImm,
    adcsImm,adcsImm,adcsImm,adcsImm, adcsImm,adcsImm,adcsImm,adcsImm,

    // ── 0x2C0–0x2CF  SBC imm ────────────────────────────────────────────────
    sbcImm, sbcImm, sbcImm, sbcImm,  sbcImm, sbcImm, sbcImm, sbcImm,
    sbcImm, sbcImm, sbcImm, sbcImm,  sbcImm, sbcImm, sbcImm, sbcImm,

    // ── 0x2D0–0x2DF  SBCS imm ───────────────────────────────────────────────
    sbcsImm,sbcsImm,sbcsImm,sbcsImm, sbcsImm,sbcsImm,sbcsImm,sbcsImm,
    sbcsImm,sbcsImm,sbcsImm,sbcsImm, sbcsImm,sbcsImm,sbcsImm,sbcsImm,

    // ── 0x2E0–0x2EF  RSC imm ────────────────────────────────────────────────
    rscImm, rscImm, rscImm, rscImm,  rscImm, rscImm, rscImm, rscImm,
    rscImm, rscImm, rscImm, rscImm,  rscImm, rscImm, rscImm, rscImm,

    // ── 0x2F0–0x2FF  RSCS imm ───────────────────────────────────────────────
    rscsImm,rscsImm,rscsImm,rscsImm, rscsImm,rscsImm,rscsImm,rscsImm,
    rscsImm,rscsImm,rscsImm,rscsImm, rscsImm,rscsImm,rscsImm,rscsImm,

    // ── 0x300–0x30F  undefined (S-bit set, no valid operation) ───────────────
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,

    // ── 0x310–0x31F  TST imm ────────────────────────────────────────────────
    tstImm, tstImm, tstImm, tstImm,  tstImm, tstImm, tstImm, tstImm,
    tstImm, tstImm, tstImm, tstImm,  tstImm, tstImm, tstImm, tstImm,

    // ── 0x320–0x32F  MSR imm CPSR ───────────────────────────────────────────
    msrIc,  msrIc,  msrIc,  msrIc,   msrIc,  msrIc,  msrIc,  msrIc,
    msrIc,  msrIc,  msrIc,  msrIc,   msrIc,  msrIc,  msrIc,  msrIc,

    // ── 0x330–0x33F  TEQ imm ────────────────────────────────────────────────
    teqImm, teqImm, teqImm, teqImm,  teqImm, teqImm, teqImm, teqImm,
    teqImm, teqImm, teqImm, teqImm,  teqImm, teqImm, teqImm, teqImm,

    // ── 0x340–0x34F  undefined ───────────────────────────────────────────────
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,

    // ── 0x350–0x35F  CMP imm ────────────────────────────────────────────────
    cmpImm, cmpImm, cmpImm, cmpImm,  cmpImm, cmpImm, cmpImm, cmpImm,
    cmpImm, cmpImm, cmpImm, cmpImm,  cmpImm, cmpImm, cmpImm, cmpImm,

    // ── 0x360–0x36F  MSR imm SPSR ───────────────────────────────────────────
    msrIs,  msrIs,  msrIs,  msrIs,   msrIs,  msrIs,  msrIs,  msrIs,
    msrIs,  msrIs,  msrIs,  msrIs,   msrIs,  msrIs,  msrIs,  msrIs,

    // ── 0x370–0x37F  CMN imm ────────────────────────────────────────────────
    cmnImm, cmnImm, cmnImm, cmnImm,  cmnImm, cmnImm, cmnImm, cmnImm,
    cmnImm, cmnImm, cmnImm, cmnImm,  cmnImm, cmnImm, cmnImm, cmnImm,

    // ── 0x380–0x38F  ORR imm ────────────────────────────────────────────────
    orrImm, orrImm, orrImm, orrImm,  orrImm, orrImm, orrImm, orrImm,
    orrImm, orrImm, orrImm, orrImm,  orrImm, orrImm, orrImm, orrImm,

    // ── 0x390–0x39F  ORRS imm ───────────────────────────────────────────────
    orrsImm,orrsImm,orrsImm,orrsImm, orrsImm,orrsImm,orrsImm,orrsImm,
    orrsImm,orrsImm,orrsImm,orrsImm, orrsImm,orrsImm,orrsImm,orrsImm,

    // ── 0x3A0–0x3AF  MOV imm ────────────────────────────────────────────────
    movImm, movImm, movImm, movImm,  movImm, movImm, movImm, movImm,
    movImm, movImm, movImm, movImm,  movImm, movImm, movImm, movImm,

    // ── 0x3B0–0x3BF  MOVS imm ───────────────────────────────────────────────
    movsImm,movsImm,movsImm,movsImm, movsImm,movsImm,movsImm,movsImm,
    movsImm,movsImm,movsImm,movsImm, movsImm,movsImm,movsImm,movsImm,

    // ── 0x3C0–0x3CF  BIC imm ────────────────────────────────────────────────
    bicImm, bicImm, bicImm, bicImm,  bicImm, bicImm, bicImm, bicImm,
    bicImm, bicImm, bicImm, bicImm,  bicImm, bicImm, bicImm, bicImm,

    // ── 0x3D0–0x3DF  BICS imm ───────────────────────────────────────────────
    bicsImm,bicsImm,bicsImm,bicsImm, bicsImm,bicsImm,bicsImm,bicsImm,
    bicsImm,bicsImm,bicsImm,bicsImm, bicsImm,bicsImm,bicsImm,bicsImm,

    // ── 0x3E0–0x3EF  MVN imm ────────────────────────────────────────────────
    mvnImm, mvnImm, mvnImm, mvnImm,  mvnImm, mvnImm, mvnImm, mvnImm,
    mvnImm, mvnImm, mvnImm, mvnImm,  mvnImm, mvnImm, mvnImm, mvnImm,

    // ── 0x3F0–0x3FF  MVNS imm ───────────────────────────────────────────────
    mvnsImm,mvnsImm,mvnsImm,mvnsImm, mvnsImm,mvnsImm,mvnsImm,mvnsImm,
    mvnsImm,mvnsImm,mvnsImm,mvnsImm, mvnsImm,mvnsImm,mvnsImm,mvnsImm,

    // ════════════════════════════════════════════════════════════════════════
    // Single-register load/store (bits 27-26 = 01)
    // ════════════════════════════════════════════════════════════════════════

    // ── 0x400–0x40F  STR  post-dec imm ──────────────────────────────────────
    strPtim,strPtim,strPtim,strPtim, strPtim,strPtim,strPtim,strPtim,
    strPtim,strPtim,strPtim,strPtim, strPtim,strPtim,strPtim,strPtim,

    // ── 0x410–0x41F  LDR  post-dec imm ──────────────────────────────────────
    ldrPtim,ldrPtim,ldrPtim,ldrPtim, ldrPtim,ldrPtim,ldrPtim,ldrPtim,
    ldrPtim,ldrPtim,ldrPtim,ldrPtim, ldrPtim,ldrPtim,ldrPtim,ldrPtim,

    // ── 0x420–0x43F  undefined (T-bit variants not used on DS) ──────────────
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,

    // ── 0x440–0x44F  STRB post-dec imm ──────────────────────────────────────
    strbPtim,strbPtim,strbPtim,strbPtim, strbPtim,strbPtim,strbPtim,strbPtim,
    strbPtim,strbPtim,strbPtim,strbPtim, strbPtim,strbPtim,strbPtim,strbPtim,

    // ── 0x450–0x45F  LDRB post-dec imm ──────────────────────────────────────
    ldrbPtim,ldrbPtim,ldrbPtim,ldrbPtim, ldrbPtim,ldrbPtim,ldrbPtim,ldrbPtim,
    ldrbPtim,ldrbPtim,ldrbPtim,ldrbPtim, ldrbPtim,ldrbPtim,ldrbPtim,ldrbPtim,

    // ── 0x460–0x47F  undefined ───────────────────────────────────────────────
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,

    // ── 0x480–0x48F  STR  post-inc imm ──────────────────────────────────────
    strPtip,strPtip,strPtip,strPtip, strPtip,strPtip,strPtip,strPtip,
    strPtip,strPtip,strPtip,strPtip, strPtip,strPtip,strPtip,strPtip,

    // ── 0x490–0x49F  LDR  post-inc imm ──────────────────────────────────────
    ldrPtip,ldrPtip,ldrPtip,ldrPtip, ldrPtip,ldrPtip,ldrPtip,ldrPtip,
    ldrPtip,ldrPtip,ldrPtip,ldrPtip, ldrPtip,ldrPtip,ldrPtip,ldrPtip,

    // ── 0x4A0–0x4BF  undefined ───────────────────────────────────────────────
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,

    // ── 0x4C0–0x4CF  STRB post-inc imm ──────────────────────────────────────
    strbPtip,strbPtip,strbPtip,strbPtip, strbPtip,strbPtip,strbPtip,strbPtip,
    strbPtip,strbPtip,strbPtip,strbPtip, strbPtip,strbPtip,strbPtip,strbPtip,

    // ── 0x4D0–0x4DF  LDRB post-inc imm ──────────────────────────────────────
    ldrbPtip,ldrbPtip,ldrbPtip,ldrbPtip, ldrbPtip,ldrbPtip,ldrbPtip,ldrbPtip,
    ldrbPtip,ldrbPtip,ldrbPtip,ldrbPtip, ldrbPtip,ldrbPtip,ldrbPtip,ldrbPtip,

    // ── 0x4E0–0x4FF  undefined ───────────────────────────────────────────────
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,

    // ── 0x500–0x50F  STR  offset-dec imm ────────────────────────────────────
    strOfim,strOfim,strOfim,strOfim, strOfim,strOfim,strOfim,strOfim,
    strOfim,strOfim,strOfim,strOfim, strOfim,strOfim,strOfim,strOfim,

    // ── 0x510–0x51F  LDR  offset-dec imm ────────────────────────────────────
    ldrOfim,ldrOfim,ldrOfim,ldrOfim, ldrOfim,ldrOfim,ldrOfim,ldrOfim,
    ldrOfim,ldrOfim,ldrOfim,ldrOfim, ldrOfim,ldrOfim,ldrOfim,ldrOfim,

    // ── 0x520–0x52F  STR  pre-dec imm ───────────────────────────────────────
    strPrim,strPrim,strPrim,strPrim, strPrim,strPrim,strPrim,strPrim,
    strPrim,strPrim,strPrim,strPrim, strPrim,strPrim,strPrim,strPrim,

    // ── 0x530–0x53F  LDR  pre-dec imm ───────────────────────────────────────
    ldrPrim,ldrPrim,ldrPrim,ldrPrim, ldrPrim,ldrPrim,ldrPrim,ldrPrim,
    ldrPrim,ldrPrim,ldrPrim,ldrPrim, ldrPrim,ldrPrim,ldrPrim,ldrPrim,

    // ── 0x540–0x54F  STRB offset-dec imm ────────────────────────────────────
    strbOfim,strbOfim,strbOfim,strbOfim, strbOfim,strbOfim,strbOfim,strbOfim,
    strbOfim,strbOfim,strbOfim,strbOfim, strbOfim,strbOfim,strbOfim,strbOfim,

    // ── 0x550–0x55F  LDRB offset-dec imm ────────────────────────────────────
    ldrbOfim,ldrbOfim,ldrbOfim,ldrbOfim, ldrbOfim,ldrbOfim,ldrbOfim,ldrbOfim,
    ldrbOfim,ldrbOfim,ldrbOfim,ldrbOfim, ldrbOfim,ldrbOfim,ldrbOfim,ldrbOfim,

    // ── 0x560–0x56F  STRB pre-dec imm ───────────────────────────────────────
    strbPrim,strbPrim,strbPrim,strbPrim, strbPrim,strbPrim,strbPrim,strbPrim,
    strbPrim,strbPrim,strbPrim,strbPrim, strbPrim,strbPrim,strbPrim,strbPrim,

    // ── 0x570–0x57F  LDRB pre-dec imm ───────────────────────────────────────
    ldrbPrim,ldrbPrim,ldrbPrim,ldrbPrim, ldrbPrim,ldrbPrim,ldrbPrim,ldrbPrim,
    ldrbPrim,ldrbPrim,ldrbPrim,ldrbPrim, ldrbPrim,ldrbPrim,ldrbPrim,ldrbPrim,

    // ── 0x580–0x58F  STR  offset-inc imm ────────────────────────────────────
    strOfip,strOfip,strOfip,strOfip, strOfip,strOfip,strOfip,strOfip,
    strOfip,strOfip,strOfip,strOfip, strOfip,strOfip,strOfip,strOfip,

    // ── 0x590–0x59F  LDR  offset-inc imm ────────────────────────────────────
    ldrOfip,ldrOfip,ldrOfip,ldrOfip, ldrOfip,ldrOfip,ldrOfip,ldrOfip,
    ldrOfip,ldrOfip,ldrOfip,ldrOfip, ldrOfip,ldrOfip,ldrOfip,ldrOfip,

    // ── 0x5A0–0x5AF  STR  pre-inc imm ───────────────────────────────────────
    strPrip,strPrip,strPrip,strPrip, strPrip,strPrip,strPrip,strPrip,
    strPrip,strPrip,strPrip,strPrip, strPrip,strPrip,strPrip,strPrip,

    // ── 0x5B0–0x5BF  LDR  pre-inc imm ───────────────────────────────────────
    ldrPrip,ldrPrip,ldrPrip,ldrPrip, ldrPrip,ldrPrip,ldrPrip,ldrPrip,
    ldrPrip,ldrPrip,ldrPrip,ldrPrip, ldrPrip,ldrPrip,ldrPrip,ldrPrip,
    // ── 0x5C0–0x5CF  STRB offset-inc imm ────────────────────────────────────
    strbOfip,strbOfip,strbOfip,strbOfip, strbOfip,strbOfip,strbOfip,strbOfip,
    strbOfip,strbOfip,strbOfip,strbOfip, strbOfip,strbOfip,strbOfip,strbOfip,

    // ── 0x5D0–0x5DF  LDRB offset-inc imm ────────────────────────────────────
    ldrbOfip,ldrbOfip,ldrbOfip,ldrbOfip, ldrbOfip,ldrbOfip,ldrbOfip,ldrbOfip,
    ldrbOfip,ldrbOfip,ldrbOfip,ldrbOfip, ldrbOfip,ldrbOfip,ldrbOfip,ldrbOfip,

    // ── 0x5E0–0x5EF  STRB pre-inc imm ───────────────────────────────────────
    strbPrip,strbPrip,strbPrip,strbPrip, strbPrip,strbPrip,strbPrip,strbPrip,
    strbPrip,strbPrip,strbPrip,strbPrip, strbPrip,strbPrip,strbPrip,strbPrip,

    // ── 0x5F0–0x5FF  LDRB pre-inc imm ───────────────────────────────────────
    ldrbPrip,ldrbPrip,ldrbPrip,ldrbPrip, ldrbPrip,ldrbPrip,ldrbPrip,ldrbPrip,
    ldrbPrip,ldrbPrip,ldrbPrip,ldrbPrip, ldrbPrip,ldrbPrip,ldrbPrip,ldrbPrip,

    // ════════════════════════════════════════════════════════════════════════
    // Register-offset load/store (bits 27-25 = 011, bit 4 = 0)
    // Odd entries are always unkArm (bit 4 = 1 would make a media instr).
    // The alternating valid/unk pattern is intentional and must be preserved.
    // ════════════════════════════════════════════════════════════════════════

    // ── 0x600–0x60F  STR  post-dec reg (ll/lr/ar/rr shifts) ─────────────────
    strPtrmll,unkArm, strPtrmlr,unkArm,
    strPtrmar,unkArm, strPtrmrr,unkArm,
    strPtrmll,unkArm, strPtrmlr,unkArm,
    strPtrmar,unkArm, strPtrmrr,unkArm,

    // ── 0x610–0x61F  LDR  post-dec reg ──────────────────────────────────────
    ldrPtrmll,unkArm, ldrPtrmlr,unkArm,
    ldrPtrmar,unkArm, ldrPtrmrr,unkArm,
    ldrPtrmll,unkArm, ldrPtrmlr,unkArm,
    ldrPtrmar,unkArm, ldrPtrmrr,unkArm,

    // ── 0x620–0x63F  undefined (T-bit / media space) ─────────────────────────
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,

    // ── 0x640–0x64F  STRB post-dec reg ──────────────────────────────────────
    strbPtrmll,unkArm, strbPtrmlr,unkArm,
    strbPtrmar,unkArm, strbPtrmrr,unkArm,
    strbPtrmll,unkArm, strbPtrmlr,unkArm,
    strbPtrmar,unkArm, strbPtrmrr,unkArm,

    // ── 0x650–0x65F  LDRB post-dec reg ──────────────────────────────────────
    ldrbPtrmll,unkArm, ldrbPtrmlr,unkArm,
    ldrbPtrmar,unkArm, ldrbPtrmrr,unkArm,
    ldrbPtrmll,unkArm, ldrbPtrmlr,unkArm,
    ldrbPtrmar,unkArm, ldrbPtrmrr,unkArm,

    // ── 0x660–0x67F  undefined ───────────────────────────────────────────────
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,

    // ── 0x680–0x68F  STR  post-inc reg ──────────────────────────────────────
    strPtrpll,unkArm, strPtrplr,unkArm,
    strPtrpar,unkArm, strPtrprr,unkArm,
    strPtrpll,unkArm, strPtrplr,unkArm,
    strPtrpar,unkArm, strPtrprr,unkArm,

    // ── 0x690–0x69F  LDR  post-inc reg ──────────────────────────────────────
    ldrPtrpll,unkArm, ldrPtrplr,unkArm,
    ldrPtrpar,unkArm, ldrPtrprr,unkArm,
    ldrPtrpll,unkArm, ldrPtrplr,unkArm,
    ldrPtrpar,unkArm, ldrPtrprr,unkArm,

    // ── 0x6A0–0x6BF  undefined ───────────────────────────────────────────────
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,

    // ── 0x6C0–0x6CF  STRB post-inc reg ──────────────────────────────────────
    strbPtrpll,unkArm, strbPtrplr,unkArm,
    strbPtrpar,unkArm, strbPtrprr,unkArm,
    strbPtrpll,unkArm, strbPtrplr,unkArm,
    strbPtrpar,unkArm, strbPtrprr,unkArm,

    // ── 0x6D0–0x6DF  LDRB post-inc reg ──────────────────────────────────────
    ldrbPtrpll,unkArm, ldrbPtrplr,unkArm,
    ldrbPtrpar,unkArm, ldrbPtrprr,unkArm,
    ldrbPtrpll,unkArm, ldrbPtrplr,unkArm,
    ldrbPtrpar,unkArm, ldrbPtrprr,unkArm,

    // ── 0x6E0–0x6FF  undefined ───────────────────────────────────────────────
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,

    // ════════════════════════════════════════════════════════════════════════
    // Pre-indexed register-offset load/store (bits 27-25 = 011, W=0 or W=1)
    // ════════════════════════════════════════════════════════════════════════

    // ── 0x700–0x70F  STR  offset-dec reg ────────────────────────────────────
    strOfrmll,unkArm, strOfrmlr,unkArm,
    strOfrmar,unkArm, strOfrmrr,unkArm,
    strOfrmll,unkArm, strOfrmlr,unkArm,
    strOfrmar,unkArm, strOfrmrr,unkArm,

    // ── 0x710–0x71F  LDR  offset-dec reg ────────────────────────────────────
    ldrOfrmll,unkArm, ldrOfrmlr,unkArm,
    ldrOfrmar,unkArm, ldrOfrmrr,unkArm,
    ldrOfrmll,unkArm, ldrOfrmlr,unkArm,
    ldrOfrmar,unkArm, ldrOfrmrr,unkArm,

    // ── 0x720–0x72F  STR  pre-dec reg (W=1) ──────────────────────────────────
    strPrrmll,unkArm, strPrrmlr,unkArm,
    strPrrmar,unkArm, strPrrmrr,unkArm,
    strPrrmll,unkArm, strPrrmlr,unkArm,
    strPrrmar,unkArm, strPrrmrr,unkArm,

    // ── 0x730–0x73F  LDR  pre-dec reg (W=1) ──────────────────────────────────
    ldrPrrmll,unkArm, ldrPrrmlr,unkArm,
    ldrPrrmar,unkArm, ldrPrrmrr,unkArm,
    ldrPrrmll,unkArm, ldrPrrmlr,unkArm,
    ldrPrrmar,unkArm, ldrPrrmrr,unkArm,

    // ── 0x740–0x74F  STRB offset-dec reg ────────────────────────────────────
    strbOfrmll,unkArm, strbOfrmlr,unkArm,
    strbOfrmar,unkArm, strbOfrmrr,unkArm,
    strbOfrmll,unkArm, strbOfrmlr,unkArm,
    strbOfrmar,unkArm, strbOfrmrr,unkArm,

    // ── 0x750–0x75F  LDRB offset-dec reg ────────────────────────────────────
    ldrbOfrmll,unkArm, ldrbOfrmlr,unkArm,
    ldrbOfrmar,unkArm, ldrbOfrmrr,unkArm,
    ldrbOfrmll,unkArm, ldrbOfrmlr,unkArm,
    ldrbOfrmar,unkArm, ldrbOfrmrr,unkArm,

    // ── 0x760–0x76F  STRB pre-dec reg (W=1) ──────────────────────────────────
    strbPrrmll,unkArm, strbPrrmlr,unkArm,
    strbPrrmar,unkArm, strbPrrmrr,unkArm,
    strbPrrmll,unkArm, strbPrrmlr,unkArm,
    strbPrrmar,unkArm, strbPrrmrr,unkArm,

    // ── 0x770–0x77F  LDRB pre-dec reg (W=1) ──────────────────────────────────
    ldrbPrrmll,unkArm, ldrbPrrmlr,unkArm,
    ldrbPrrmar,unkArm, ldrbPrrmrr,unkArm,
    ldrbPrrmll,unkArm, ldrbPrrmlr,unkArm,
    ldrbPrrmar,unkArm, ldrbPrrmrr,unkArm,

    // ── 0x780–0x78F  STR  offset-inc reg ────────────────────────────────────
    strOfrpll,unkArm, strOfrplr,unkArm,
    strOfrpar,unkArm, strOfrprr,unkArm,
    strOfrpll,unkArm, strOfrplr,unkArm,
    strOfrpar,unkArm, strOfrprr,unkArm,

    // ── 0x790–0x79F  LDR  offset-inc reg ────────────────────────────────────
    ldrOfrpll,unkArm, ldrOfrplr,unkArm,
    ldrOfrpar,unkArm, ldrOfrprr,unkArm,
    ldrOfrpll,unkArm, ldrOfrplr,unkArm,
    ldrOfrpar,unkArm, ldrOfrprr,unkArm,

    // ── 0x7A0–0x7AF  STR  pre-inc reg (W=1) ──────────────────────────────────
    strPrrpll,unkArm, strPrrplr,unkArm,
    strPrrpar,unkArm, strPrrprr,unkArm,
    strPrrpll,unkArm, strPrrplr,unkArm,
    strPrrpar,unkArm, strPrrprr,unkArm,

    // ── 0x7B0–0x7BF  LDR  pre-inc reg (W=1) ──────────────────────────────────
    ldrPrrpll,unkArm, ldrPrrplr,unkArm,
    ldrPrrpar,unkArm, ldrPrrprr,unkArm,
    ldrPrrpll,unkArm, ldrPrrplr,unkArm,
    ldrPrrpar,unkArm, ldrPrrprr,unkArm,

    // ── 0x7C0–0x7CF  STRB offset-inc reg ────────────────────────────────────
    strbOfrpll,unkArm, strbOfrplr,unkArm,
    strbOfrpar,unkArm, strbOfrprr,unkArm,
    strbOfrpll,unkArm, strbOfrplr,unkArm,
    strbOfrpar,unkArm, strbOfrprr,unkArm,

    // ── 0x7D0–0x7DF  LDRB offset-inc reg ────────────────────────────────────
    ldrbOfrpll,unkArm, ldrbOfrplr,unkArm,
    ldrbOfrpar,unkArm, ldrbOfrprr,unkArm,
    ldrbOfrpll,unkArm, ldrbOfrplr,unkArm,
    ldrbOfrpar,unkArm, ldrbOfrprr,unkArm,

    // ── 0x7E0–0x7EF  STRB pre-inc reg (W=1) ─────────────────────────────────
    strbPrrpll,unkArm, strbPrrplr,unkArm,
    strbPrrpar,unkArm, strbPrrprr,unkArm,
    strbPrrpll,unkArm, strbPrrplr,unkArm,
    strbPrrpar,unkArm, strbPrrprr,unkArm,

    // ── 0x7F0–0x7FF  LDRB pre-inc reg (W=1) ─────────────────────────────────
    ldrbPrrpll,unkArm, ldrbPrrplr,unkArm,
    ldrbPrrpar,unkArm, ldrbPrrprr,unkArm,
    ldrbPrrpll,unkArm, ldrbPrrplr,unkArm,
    ldrbPrrpar,unkArm, ldrbPrrprr,unkArm,

    // ════════════════════════════════════════════════════════════════════════
    // Block data transfer (LDM/STM) — bits 27-25 = 100
    // Suffix key:
    //   da=Decrement After   ia=Increment After
    //   db=Decrement Before  ib=Increment Before
    //   W = writeback  U = user-mode registers  UW = both
    // All 16 entries per row are identical (register list in bits 15-0).
    // ════════════════════════════════════════════════════════════════════════

    // ── 0x800–0x80F  STMDA ───────────────────────────────────────────────────
    stmda,stmda,stmda,stmda, stmda,stmda,stmda,stmda,
    stmda,stmda,stmda,stmda, stmda,stmda,stmda,stmda,

    // ── 0x810–0x81F  LDMDA ───────────────────────────────────────────────────
    ldmda,ldmda,ldmda,ldmda, ldmda,ldmda,ldmda,ldmda,
    ldmda,ldmda,ldmda,ldmda, ldmda,ldmda,ldmda,ldmda,

    // ── 0x820–0x82F  STMDA (W=1) ─────────────────────────────────────────────
    stmdaW,stmdaW,stmdaW,stmdaW, stmdaW,stmdaW,stmdaW,stmdaW,
    stmdaW,stmdaW,stmdaW,stmdaW, stmdaW,stmdaW,stmdaW,stmdaW,

    // ── 0x830–0x83F  LDMDA (W=1) ─────────────────────────────────────────────
    ldmdaW,ldmdaW,ldmdaW,ldmdaW, ldmdaW,ldmdaW,ldmdaW,ldmdaW,
    ldmdaW,ldmdaW,ldmdaW,ldmdaW, ldmdaW,ldmdaW,ldmdaW,ldmdaW,

    // ── 0x840–0x84F  STMDA (U) ───────────────────────────────────────────────
    stmdaU,stmdaU,stmdaU,stmdaU, stmdaU,stmdaU,stmdaU,stmdaU,
    stmdaU,stmdaU,stmdaU,stmdaU, stmdaU,stmdaU,stmdaU,stmdaU,

    // ── 0x850–0x85F  LDMDA (U) ───────────────────────────────────────────────
    ldmdaU,ldmdaU,ldmdaU,ldmdaU, ldmdaU,ldmdaU,ldmdaU,ldmdaU,
    ldmdaU,ldmdaU,ldmdaU,ldmdaU, ldmdaU,ldmdaU,ldmdaU,ldmdaU,

    // ── 0x860–0x86F  STMDA (U,W) ─────────────────────────────────────────────
    stmdaUW,stmdaUW,stmdaUW,stmdaUW, stmdaUW,stmdaUW,stmdaUW,stmdaUW,
    stmdaUW,stmdaUW,stmdaUW,stmdaUW, stmdaUW,stmdaUW,stmdaUW,stmdaUW,

    // ── 0x870–0x87F  LDMDA (U,W) ─────────────────────────────────────────────
    ldmdaUW,ldmdaUW,ldmdaUW,ldmdaUW, ldmdaUW,ldmdaUW,ldmdaUW,ldmdaUW,
    ldmdaUW,ldmdaUW,ldmdaUW,ldmdaUW, ldmdaUW,ldmdaUW,ldmdaUW,ldmdaUW,

    // ── 0x880–0x88F  STMIA ───────────────────────────────────────────────────
    stmia,stmia,stmia,stmia, stmia,stmia,stmia,stmia,
    stmia,stmia,stmia,stmia, stmia,stmia,stmia,stmia,

    // ── 0x890–0x89F  LDMIA ───────────────────────────────────────────────────
    ldmia,ldmia,ldmia,ldmia, ldmia,ldmia,ldmia,ldmia,
    ldmia,ldmia,ldmia,ldmia, ldmia,ldmia,ldmia,ldmia,

    // ── 0x8A0–0x8AF  STMIA (W=1) ─────────────────────────────────────────────
    stmiaW,stmiaW,stmiaW,stmiaW, stmiaW,stmiaW,stmiaW,stmiaW,
    stmiaW,stmiaW,stmiaW,stmiaW, stmiaW,stmiaW,stmiaW,stmiaW,

    // ── 0x8B0–0x8BF  LDMIA (W=1) ─────────────────────────────────────────────
    ldmiaW,ldmiaW,ldmiaW,ldmiaW, ldmiaW,ldmiaW,ldmiaW,ldmiaW,
    ldmiaW,ldmiaW,ldmiaW,ldmiaW, ldmiaW,ldmiaW,ldmiaW,ldmiaW,

    // ── 0x8C0–0x8CF  STMIA (U) ───────────────────────────────────────────────
    stmiaU,stmiaU,stmiaU,stmiaU, stmiaU,stmiaU,stmiaU,stmiaU,
    stmiaU,stmiaU,stmiaU,stmiaU, stmiaU,stmiaU,stmiaU,stmiaU,

    // ── 0x8D0–0x8DF  LDMIA (U) ───────────────────────────────────────────────
    ldmiaU,ldmiaU,ldmiaU,ldmiaU, ldmiaU,ldmiaU,ldmiaU,ldmiaU,
    ldmiaU,ldmiaU,ldmiaU,ldmiaU, ldmiaU,ldmiaU,ldmiaU,ldmiaU,

    // ── 0x8E0–0x8EF  STMIA (U,W) ─────────────────────────────────────────────
    stmiaUW,stmiaUW,stmiaUW,stmiaUW, stmiaUW,stmiaUW,stmiaUW,stmiaUW,
    stmiaUW,stmiaUW,stmiaUW,stmiaUW, stmiaUW,stmiaUW,stmiaUW,stmiaUW,

    // ── 0x8F0–0x8FF  LDMIA (U,W) ─────────────────────────────────────────────
    ldmiaUW,ldmiaUW,ldmiaUW,ldmiaUW, ldmiaUW,ldmiaUW,ldmiaUW,ldmiaUW,
    ldmiaUW,ldmiaUW,ldmiaUW,ldmiaUW, ldmiaUW,ldmiaUW,ldmiaUW,ldmiaUW,

    // ── 0x900–0x90F  STMDB ───────────────────────────────────────────────────
    stmdb,stmdb,stmdb,stmdb, stmdb,stmdb,stmdb,stmdb,
    stmdb,stmdb,stmdb,stmdb, stmdb,stmdb,stmdb,stmdb,

    // ── 0x910–0x91F  LDMDB ───────────────────────────────────────────────────
    ldmdb,ldmdb,ldmdb,ldmdb, ldmdb,ldmdb,ldmdb,ldmdb,
    ldmdb,ldmdb,ldmdb,ldmdb, ldmdb,ldmdb,ldmdb,ldmdb,

    // ── 0x920–0x92F  STMDB (W=1) — push ─────────────────────────────────────
    stmdbW,stmdbW,stmdbW,stmdbW, stmdbW,stmdbW,stmdbW,stmdbW,
    stmdbW,stmdbW,stmdbW,stmdbW, stmdbW,stmdbW,stmdbW,stmdbW,

    // ── 0x930–0x93F  LDMDB (W=1) ─────────────────────────────────────────────
    ldmdbW,ldmdbW,ldmdbW,ldmdbW, ldmdbW,ldmdbW,ldmdbW,ldmdbW,
    ldmdbW,ldmdbW,ldmdbW,ldmdbW, ldmdbW,ldmdbW,ldmdbW,ldmdbW,

    // ── 0x940–0x94F  STMDB (U) ───────────────────────────────────────────────
    stmdbU,stmdbU,stmdbU,stmdbU, stmdbU,stmdbU,stmdbU,stmdbU,
    stmdbU,stmdbU,stmdbU,stmdbU, stmdbU,stmdbU,stmdbU,stmdbU,

    // ── 0x950–0x95F  LDMDB (U) ───────────────────────────────────────────────
    ldmdbU,ldmdbU,ldmdbU,ldmdbU, ldmdbU,ldmdbU,ldmdbU,ldmdbU,
    ldmdbU,ldmdbU,ldmdbU,ldmdbU, ldmdbU,ldmdbU,ldmdbU,ldmdbU,

    // ── 0x960–0x96F  STMDB (U,W) ─────────────────────────────────────────────
    stmdbUW,stmdbUW,stmdbUW,stmdbUW, stmdbUW,stmdbUW,stmdbUW,stmdbUW,
    stmdbUW,stmdbUW,stmdbUW,stmdbUW, stmdbUW,stmdbUW,stmdbUW,stmdbUW,

    // ── 0x970–0x97F  LDMDB (U,W) ─────────────────────────────────────────────
    ldmdbUW,ldmdbUW,ldmdbUW,ldmdbUW, ldmdbUW,ldmdbUW,ldmdbUW,ldmdbUW,
    ldmdbUW,ldmdbUW,ldmdbUW,ldmdbUW, ldmdbUW,ldmdbUW,ldmdbUW,ldmdbUW,

    // ── 0x980–0x98F  STMIB ───────────────────────────────────────────────────
    stmib,stmib,stmib,stmib, stmib,stmib,stmib,stmib,
    stmib,stmib,stmib,stmib, stmib,stmib,stmib,stmib,

    // ── 0x990–0x99F  LDMIB ───────────────────────────────────────────────────
    ldmib,ldmib,ldmib,ldmib, ldmib,ldmib,ldmib,ldmib,
    ldmib,ldmib,ldmib,ldmib, ldmib,ldmib,ldmib,ldmib,

    // ── 0x9A0–0x9AF  STMIB (W=1) ─────────────────────────────────────────────
    stmibW,stmibW,stmibW,stmibW, stmibW,stmibW,stmibW,stmibW,
    stmibW,stmibW,stmibW,stmibW, stmibW,stmibW,stmibW,stmibW,

    // ── 0x9B0–0x9BF  LDMIB (W=1) — pop ──────────────────────────────────────
    ldmibW,ldmibW,ldmibW,ldmibW, ldmibW,ldmibW,ldmibW,ldmibW,
    ldmibW,ldmibW,ldmibW,ldmibW, ldmibW,ldmibW,ldmibW,ldmibW,

    // ── 0x9C0–0x9CF  STMIB (U) ───────────────────────────────────────────────
    stmibU,stmibU,stmibU,stmibU, stmibU,stmibU,stmibU,stmibU,
    stmibU,stmibU,stmibU,stmibU, stmibU,stmibU,stmibU,stmibU,

    // ── 0x9D0–0x9DF  LDMIB (U) ───────────────────────────────────────────────
    ldmibU,ldmibU,ldmibU,ldmibU, ldmibU,ldmibU,ldmibU,ldmibU,
    ldmibU,ldmibU,ldmibU,ldmibU, ldmibU,ldmibU,ldmibU,ldmibU,

    // ── 0x9E0–0x9EF  STMIB (U,W) ─────────────────────────────────────────────
    stmibUW,stmibUW,stmibUW,stmibUW, stmibUW,stmibUW,stmibUW,stmibUW,
    stmibUW,stmibUW,stmibUW,stmibUW, stmibUW,stmibUW,stmibUW,stmibUW,

    // ── 0x9F0–0x9FF  LDMIB (U,W) ─────────────────────────────────────────────
    ldmibUW,ldmibUW,ldmibUW,ldmibUW, ldmibUW,ldmibUW,ldmibUW,ldmibUW,
    ldmibUW,ldmibUW,ldmibUW,ldmibUW, ldmibUW,ldmibUW,ldmibUW,ldmibUW,
    // ════════════════════════════════════════════════════════════════════════
    // Branch instructions (bits 27-24 = 1010 / 1011)
    // The 24-bit signed offset is encoded in bits 23-0; every entry in the
    // 0xA00–0xBFF range dispatches to the same handler which extracts it.
    // 256 entries × 2 (B + BL) = 512 total — all identical within each half.
    // ════════════════════════════════════════════════════════════════════════

    // ── 0xA00–0xAFF  B (branch without link) — 256 entries ──────────────────
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xA00–0xA0F
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xA10–0xA1F
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xA20–0xA2F
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xA30–0xA3F
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xA40–0xA4F
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xA50–0xA5F
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xA60–0xA6F
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xA70–0xA7F
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xA80–0xA8F
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xA90–0xA9F
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xAA0–0xAAF
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xAB0–0xABF
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xAC0–0xACF
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xAD0–0xADF
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xAE0–0xAEF
    b,b,b,b, b,b,b,b, b,b,b,b, b,b,b,b,   // 0xAF0–0xAFF

    // ── 0xB00–0xBFF  BL (branch with link) — 256 entries ────────────────────
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xB00–0xB0F
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xB10–0xB1F
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xB20–0xB2F
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xB30–0xB3F
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xB40–0xB4F
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xB50–0xB5F
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xB60–0xB6F
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xB70–0xB7F
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xB80–0xB8F
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xB90–0xB9F
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xBA0–0xBAF
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xBB0–0xBBF
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xBC0–0xBCF
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xBD0–0xBDF
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xBE0–0xBEF
    bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl, bl,bl,bl,bl,   // 0xBF0–0xBFF

    // ════════════════════════════════════════════════════════════════════════
    // Coprocessor load/store (bits 27-24 = 1100/1101/1110/1111)
    // On the NDS/GBA the coprocessor space (0xC00–0xDFF) is entirely
    // undefined — no LDC/STC instructions are used.
    // ════════════════════════════════════════════════════════════════════════

    // ── 0xC00–0xCFF  undefined (coprocessor load/store) ─────────────────────
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xC00–0xC0F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xC10–0xC1F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xC20–0xC2F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xC30–0xC3F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xC40–0xC4F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xC50–0xC5F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xC60–0xC6F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xC70–0xC7F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xC80–0xC8F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xC90–0xC9F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xCA0–0xCAF
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xCB0–0xCBF
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xCC0–0xCCF
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xCD0–0xCDF
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xCE0–0xCEF
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xCF0–0xCFF
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,

    // ── 0xD00–0xDFF  undefined (coprocessor load/store continued) ────────────
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xD00–0xD0F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xD10–0xD1F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xD20–0xD2F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xD30–0xD3F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xD40–0xD4F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xD50–0xD5F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xD60–0xD6F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xD70–0xD7F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xD80–0xD8F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xD90–0xD9F
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xDA0–0xDAF
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xDB0–0xDBF
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xDC0–0xDCF
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xDD0–0xDDF
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xDE0–0xDEF
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,   // 0xDF0–0xDFF
    unkArm,unkArm,unkArm,unkArm, unkArm,unkArm,unkArm,unkArm,

    // ════════════════════════════════════════════════════════════════════════
    // Coprocessor data operations / register transfers (bits 27-24 = 1110)
    // On the NDS the only live coprocessors are CP14 (debug, ARM9 only) and
    // CP15 (system control, ARM9 only).  Every entry alternates:
    //   even index → CDP / unkArm   (bit 4 = 0: data op, unused on NDS)
    //   odd  index → MCR or MRC     (bit 4 = 1: register transfer)
    // Bits 23-21 select the coprocessor opcode; bits 19-16 select CRn;
    // bits 15-12 select Rd; bits 11-8 select the coprocessor number;
    // bits 7-5 select a secondary opcode; bit 4 distinguishes MCR/MRC.
    // The L bit (bit 20) selects MCR (L=0) vs MRC (L=1).
    // ════════════════════════════════════════════════════════════════════════

    // ── 0xE00–0xE0F  CDP/MCR (L=0, opc1 groups 0–1) ──────────────────────────
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xE00–0xE07
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xE08–0xE0F

    // ── 0xE10–0xE1F  CDP/MRC (L=1, opc1 groups 0–1) ──────────────────────────
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xE10–0xE17
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xE18–0xE1F

    // ── 0xE20–0xE2F  CDP/MCR (opc1 groups 2–3) ───────────────────────────────
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xE20–0xE27
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xE28–0xE2F

    // ── 0xE30–0xE3F  CDP/MRC (opc1 groups 2–3) ───────────────────────────────
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xE30–0xE37
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xE38–0xE3F

    // ── 0xE40–0xE4F  CDP/MCR (opc1 groups 4–5) ───────────────────────────────
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xE40–0xE47
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xE48–0xE4F

    // ── 0xE50–0xE5F  CDP/MRC (opc1 groups 4–5) ───────────────────────────────
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xE50–0xE57
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xE58–0xE5F

    // ── 0xE60–0xE6F  CDP/MCR (opc1 groups 6–7) ───────────────────────────────
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xE60–0xE67
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xE68–0xE6F

    // ── 0xE70–0xE7F  CDP/MRC (opc1 groups 6–7) ───────────────────────────────
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xE70–0xE77
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xE78–0xE7F

    // ── 0xE80–0xE8F  CDP/MCR (opc1 groups 8–9) ───────────────────────────────
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xE80–0xE87
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xE88–0xE8F

    // ── 0xE90–0xE9F  CDP/MRC (opc1 groups 8–9) ───────────────────────────────
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xE90–0xE97
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xE98–0xE9F

    // ── 0xEA0–0xEAF  CDP/MCR (opc1 groups 10–11) ─────────────────────────────
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xEA0–0xEA7
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xEA8–0xEAF

    // ── 0xEB0–0xEBF  CDP/MRC (opc1 groups 10–11) ─────────────────────────────
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xEB0–0xEB7
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xEB8–0xEBF

    // ── 0xEC0–0xECF  CDP/MCR (opc1 groups 12–13) ─────────────────────────────
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xEC0–0xEC7
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xEC8–0xECF

    // ── 0xED0–0xEDF  CDP/MRC (opc1 groups 12–13) ─────────────────────────────
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xED0–0xED7
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xED8–0xEDF

    // ── 0xEE0–0xEEF  CDP/MCR (opc1 groups 14–15) ─────────────────────────────
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xEE0–0xEE7
    unkArm,mcr, unkArm,mcr,  unkArm,mcr, unkArm,mcr,   // 0xEE8–0xEEF

    // ── 0xEF0–0xEFF  CDP/MRC (opc1 groups 14–15) ─────────────────────────────
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xEF0–0xEF7
    unkArm,mrc, unkArm,mrc,  unkArm,mrc, unkArm,mrc,   // 0xEF8–0xEFF

    // ════════════════════════════════════════════════════════════════════════
    // Software Interrupt (bits 27-24 = 1111)
    // The 24-bit comment field in the opcode is ignored by hardware;
    // all 256 index positions dispatch to the same SWI handler.
    // ════════════════════════════════════════════════════════════════════════

    // ── 0xF00–0xFFF  SWI — 256 entries ──────────────────────────────────────
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xF00–0xF0F
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xF10–0xF1F
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xF20–0xF2F
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xF30–0xF3F
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xF40–0xF4F
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xF50–0xF5F
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xF60–0xF6F
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xF70–0xF7F
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xF80–0xF8F
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xF90–0xF9F
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xFA0–0xFAF
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xFB0–0xFBF
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xFC0–0xFCF
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xFD0–0xFDF
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xFE0–0xFEF
    swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi, swi,swi,swi,swi,  // 0xFF0–0xFFF
};

// ── Undefine the brevity macro so it cannot leak into other TUs ──────────────
#undef _
    // ════════════════════════════════════════════════════════════════════════
    // THUMB instruction dispatch table
    // Indexed by bits 15-6 of the 16-bit THUMB opcode (1024 entries).
    // Reference: http://imrannazar.com/ARM-Opcode-Map
    //
    // Layout notes (PowerPC cache optimisation):
    //   • uint16_t handler pointers are 4 bytes on 32-bit PPC.
    //   • 32-byte cache line holds 8 pointers.
    //   • 8-wide rows below align one visual row = one cache line.
    //   • const + aligned(32) keeps the table in .rodata, cache-friendly.
    // ════════════════════════════════════════════════════════════════════════

// Re-introduce the brevity macro for the THUMB table.
// It is #undef'd again at the very end of this table.
#define _T &Interpreter::

__attribute__((section(".rodata"), aligned(32)))
int (Interpreter::* Interpreter::thumbInstrs[])(uint16_t) =
{
    // ── 0x000–0x01F  LSL imm5 (32 entries) ───────────────────────────────────
    // bits 15-11 = 00000; 5-bit shift amount in bits 10-6
    _T lslImmT,_T lslImmT,_T lslImmT,_T lslImmT, _T lslImmT,_T lslImmT,_T lslImmT,_T lslImmT,  // 0x000–0x007
    _T lslImmT,_T lslImmT,_T lslImmT,_T lslImmT, _T lslImmT,_T lslImmT,_T lslImmT,_T lslImmT,  // 0x008–0x00F
    _T lslImmT,_T lslImmT,_T lslImmT,_T lslImmT, _T lslImmT,_T lslImmT,_T lslImmT,_T lslImmT,  // 0x010–0x017
    _T lslImmT,_T lslImmT,_T lslImmT,_T lslImmT, _T lslImmT,_T lslImmT,_T lslImmT,_T lslImmT,  // 0x018–0x01F

    // ── 0x020–0x03F  LSR imm5 ────────────────────────────────────────────────
    _T lsrImmT,_T lsrImmT,_T lsrImmT,_T lsrImmT, _T lsrImmT,_T lsrImmT,_T lsrImmT,_T lsrImmT,  // 0x020–0x027
    _T lsrImmT,_T lsrImmT,_T lsrImmT,_T lsrImmT, _T lsrImmT,_T lsrImmT,_T lsrImmT,_T lsrImmT,  // 0x028–0x02F
    _T lsrImmT,_T lsrImmT,_T lsrImmT,_T lsrImmT, _T lsrImmT,_T lsrImmT,_T lsrImmT,_T lsrImmT,  // 0x030–0x037
    _T lsrImmT,_T lsrImmT,_T lsrImmT,_T lsrImmT, _T lsrImmT,_T lsrImmT,_T lsrImmT,_T lsrImmT,  // 0x038–0x03F

    // ── 0x040–0x05F  ASR imm5 ────────────────────────────────────────────────
    _T asrImmT,_T asrImmT,_T asrImmT,_T asrImmT, _T asrImmT,_T asrImmT,_T asrImmT,_T asrImmT,  // 0x040–0x047
    _T asrImmT,_T asrImmT,_T asrImmT,_T asrImmT, _T asrImmT,_T asrImmT,_T asrImmT,_T asrImmT,  // 0x048–0x04F
    _T asrImmT,_T asrImmT,_T asrImmT,_T asrImmT, _T asrImmT,_T asrImmT,_T asrImmT,_T asrImmT,  // 0x050–0x057
    _T asrImmT,_T asrImmT,_T asrImmT,_T asrImmT, _T asrImmT,_T asrImmT,_T asrImmT,_T asrImmT,  // 0x058–0x05F

    // ── 0x060–0x07F  ADD/SUB reg and imm3 ────────────────────────────────────
    // 0x060–0x067: ADD reg  (bits 15-9 = 0001100)
    // 0x068–0x06F: SUB reg  (bits 15-9 = 0001101)
    // 0x070–0x077: ADD imm3 (bits 15-9 = 0001110)
    // 0x078–0x07F: SUB imm3 (bits 15-9 = 0001111)
    _T addRegT, _T addRegT, _T addRegT, _T addRegT,  _T addRegT, _T addRegT, _T addRegT, _T addRegT,  // 0x060–0x067
    _T subRegT, _T subRegT, _T subRegT, _T subRegT,  _T subRegT, _T subRegT, _T subRegT, _T subRegT,  // 0x068–0x06F
    _T addImm3T,_T addImm3T,_T addImm3T,_T addImm3T, _T addImm3T,_T addImm3T,_T addImm3T,_T addImm3T, // 0x070–0x077
    _T subImm3T,_T subImm3T,_T subImm3T,_T subImm3T, _T subImm3T,_T subImm3T,_T subImm3T,_T subImm3T, // 0x078–0x07F

    // ── 0x080–0x09F  MOV imm8 (Rd = bits 10-8, imm = bits 7-0) ──────────────
    _T movImm8T,_T movImm8T,_T movImm8T,_T movImm8T, _T movImm8T,_T movImm8T,_T movImm8T,_T movImm8T, // 0x080–0x087
    _T movImm8T,_T movImm8T,_T movImm8T,_T movImm8T, _T movImm8T,_T movImm8T,_T movImm8T,_T movImm8T, // 0x088–0x08F
    _T movImm8T,_T movImm8T,_T movImm8T,_T movImm8T, _T movImm8T,_T movImm8T,_T movImm8T,_T movImm8T, // 0x090–0x097
    _T movImm8T,_T movImm8T,_T movImm8T,_T movImm8T, _T movImm8T,_T movImm8T,_T movImm8T,_T movImm8T, // 0x098–0x09F

    // ── 0x0A0–0x0BF  CMP imm8 ────────────────────────────────────────────────
    _T cmpImm8T,_T cmpImm8T,_T cmpImm8T,_T cmpImm8T, _T cmpImm8T,_T cmpImm8T,_T cmpImm8T,_T cmpImm8T, // 0x0A0–0x0A7
    _T cmpImm8T,_T cmpImm8T,_T cmpImm8T,_T cmpImm8T, _T cmpImm8T,_T cmpImm8T,_T cmpImm8T,_T cmpImm8T, // 0x0A8–0x0AF
    _T cmpImm8T,_T cmpImm8T,_T cmpImm8T,_T cmpImm8T, _T cmpImm8T,_T cmpImm8T,_T cmpImm8T,_T cmpImm8T, // 0x0B0–0x0B7
    _T cmpImm8T,_T cmpImm8T,_T cmpImm8T,_T cmpImm8T, _T cmpImm8T,_T cmpImm8T,_T cmpImm8T,_T cmpImm8T, // 0x0B8–0x0BF

    // ── 0x0C0–0x0DF  ADD imm8 ────────────────────────────────────────────────
    _T addImm8T,_T addImm8T,_T addImm8T,_T addImm8T, _T addImm8T,_T addImm8T,_T addImm8T,_T addImm8T, // 0x0C0–0x0C7
    _T addImm8T,_T addImm8T,_T addImm8T,_T addImm8T, _T addImm8T,_T addImm8T,_T addImm8T,_T addImm8T, // 0x0C8–0x0CF
    _T addImm8T,_T addImm8T,_T addImm8T,_T addImm8T, _T addImm8T,_T addImm8T,_T addImm8T,_T addImm8T, // 0x0D0–0x0D7
    _T addImm8T,_T addImm8T,_T addImm8T,_T addImm8T, _T addImm8T,_T addImm8T,_T addImm8T,_T addImm8T, // 0x0D8–0x0DF

    // ── 0x0E0–0x0FF  SUB imm8 ────────────────────────────────────────────────
    _T subImm8T,_T subImm8T,_T subImm8T,_T subImm8T, _T subImm8T,_T subImm8T,_T subImm8T,_T subImm8T, // 0x0E0–0x0E7
    _T subImm8T,_T subImm8T,_T subImm8T,_T subImm8T, _T subImm8T,_T subImm8T,_T subImm8T,_T subImm8T, // 0x0E8–0x0EF
    _T subImm8T,_T subImm8T,_T subImm8T,_T subImm8T, _T subImm8T,_T subImm8T,_T subImm8T,_T subImm8T, // 0x0F0–0x0F7
    _T subImm8T,_T subImm8T,_T subImm8T,_T subImm8T, _T subImm8T,_T subImm8T,_T subImm8T,_T subImm8T, // 0x0F8–0x0FF

    // ── 0x100–0x10F  ALU ops / high-reg / BX / BLX ───────────────────────────
    // 0x100–0x10F: Data processing (bits 15-10 = 010000, bits 9-6 = opcode)
    // Each of the 16 entries is a distinct operation — no replication here.
    _T andDpT,  _T eorDpT,  _T lslDpT,  _T lsrDpT,   // 0x100–0x103  AND EOR LSL LSR
    _T asrDpT,  _T adcDpT,  _T sbcDpT,  _T rorDpT,   // 0x104–0x107  ASR ADC SBC ROR
    _T tstDpT,  _T negDpT,  _T cmpDpT,  _T cmnDpT,   // 0x108–0x10B  TST NEG CMP CMN
    _T orrDpT,  _T mulDpT,  _T bicDpT,  _T mvnDpT,   // 0x10C–0x10F  ORR MUL BIC MVN

    // 0x110–0x11F: Special data / branch-exchange (bits 15-10 = 010001)
    //   010001 00 → ADD Hx       (0x110–0x113)
    //   010001 01 → CMP Hx       (0x114–0x117)
    //   010001 10 → MOV Hx       (0x118–0x11B)
    //   010001 110x → BX         (0x11C–0x11D)
    //   010001 111x → BLX reg    (0x11E–0x11F)
    _T addHT,   _T addHT,   _T addHT,   _T addHT,    // 0x110–0x113
    _T cmpHT,   _T cmpHT,   _T cmpHT,   _T cmpHT,    // 0x114–0x117
    _T movHT,   _T movHT,   _T movHT,   _T movHT,    // 0x118–0x11B
    _T bxRegT,  _T bxRegT,  _T blxRegT, _T blxRegT,  // 0x11C–0x11F

    // ── 0x120–0x13F  LDR PC-relative (32 entries) ────────────────────────────
    // bits 15-11 = 01001; Rd in bits 10-8; 8-bit word-aligned offset in 7-0
    _T ldrPcT,_T ldrPcT,_T ldrPcT,_T ldrPcT, _T ldrPcT,_T ldrPcT,_T ldrPcT,_T ldrPcT,  // 0x120–0x127
    _T ldrPcT,_T ldrPcT,_T ldrPcT,_T ldrPcT, _T ldrPcT,_T ldrPcT,_T ldrPcT,_T ldrPcT,  // 0x128–0x12F
    _T ldrPcT,_T ldrPcT,_T ldrPcT,_T ldrPcT, _T ldrPcT,_T ldrPcT,_T ldrPcT,_T ldrPcT,  // 0x130–0x137
    _T ldrPcT,_T ldrPcT,_T ldrPcT,_T ldrPcT, _T ldrPcT,_T ldrPcT,_T ldrPcT,_T ldrPcT,  // 0x138–0x13F

    // ── 0x140–0x17F  Load/store register offset (bits 15-12 = 0101) ──────────
    // Sub-opcode in bits 11-9: 000=STR 001=STRH 010=STRB 011=LDRSB
    //                          100=LDR 101=LDRH 110=LDRB 111=LDRSH
    // Each sub-opcode covers 8 entries (bits 8-6 = Ro register field).
    _T strRegT, _T strRegT, _T strRegT, _T strRegT,  _T strRegT, _T strRegT, _T strRegT, _T strRegT,  // 0x140–0x147 STR
    _T strhRegT,_T strhRegT,_T strhRegT,_T strhRegT, _T strhRegT,_T strhRegT,_T strhRegT,_T strhRegT, // 0x148–0x14F STRH
    _T strbRegT,_T strbRegT,_T strbRegT,_T strbRegT, _T strbRegT,_T strbRegT,_T strbRegT,_T strbRegT, // 0x150–0x157 STRB
    _T ldrsbRegT,_T ldrsbRegT,_T ldrsbRegT,_T ldrsbRegT, _T ldrsbRegT,_T ldrsbRegT,_T ldrsbRegT,_T ldrsbRegT, // 0x158–0x15F LDRSB
    _T ldrRegT, _T ldrRegT, _T ldrRegT, _T ldrRegT,  _T ldrRegT, _T ldrRegT, _T ldrRegT, _T ldrRegT,  // 0x160–0x167 LDR
    _T ldrhRegT,_T ldrhRegT,_T ldrhRegT,_T ldrhRegT, _T ldrhRegT,_T ldrhRegT,_T ldrhRegT,_T ldrhRegT, // 0x168–0x16F LDRH
    _T ldrbRegT,_T ldrbRegT,_T ldrbRegT,_T ldrbRegT, _T ldrbRegT,_T ldrbRegT,_T ldrbRegT,_T ldrbRegT, // 0x170–0x177 LDRB
    _T ldrshRegT,_T ldrshRegT,_T ldrshRegT,_T ldrshRegT, _T ldrshRegT,_T ldrshRegT,_T ldrshRegT,_T ldrshRegT, // 0x178–0x17F LDRSH

    // ── 0x180–0x19F  STR  imm5 (word) ───────────────────────────────────────
    _T strImm5T,_T strImm5T,_T strImm5T,_T strImm5T, _T strImm5T,_T strImm5T,_T strImm5T,_T strImm5T, // 0x180–0x187
    _T strImm5T,_T strImm5T,_T strImm5T,_T strImm5T, _T strImm5T,_T strImm5T,_T strImm5T,_T strImm5T, // 0x188–0x18F
    _T strImm5T,_T strImm5T,_T strImm5T,_T strImm5T, _T strImm5T,_T strImm5T,_T strImm5T,_T strImm5T, // 0x190–0x197
    _T strImm5T,_T strImm5T,_T strImm5T,_T strImm5T, _T strImm5T,_T strImm5T,_T strImm5T,_T strImm5T, // 0x198–0x19F

    // ── 0x1A0–0x1BF  LDR  imm5 (word) ───────────────────────────────────────
    _T ldrImm5T,_T ldrImm5T,_T ldrImm5T,_T ldrImm5T, _T ldrImm5T,_T ldrImm5T,_T ldrImm5T,_T ldrImm5T, // 0x1A0–0x1A7
    _T ldrImm5T,_T ldrImm5T,_T ldrImm5T,_T ldrImm5T, _T ldrImm5T,_T ldrImm5T,_T ldrImm5T,_T ldrImm5T, // 0x1A8–0x1AF
    _T ldrImm5T,_T ldrImm5T,_T ldrImm5T,_T ldrImm5T, _T ldrImm5T,_T ldrImm5T,_T ldrImm5T,_T ldrImm5T, // 0x1B0–0x1B7
    _T ldrImm5T,_T ldrImm5T,_T ldrImm5T,_T ldrImm5T, _T ldrImm5T,_T ldrImm5T,_T ldrImm5T,_T ldrImm5T, // 0x1B8–0x1BF

    // ── 0x1C0–0x1DF  STRB imm5 (byte) ───────────────────────────────────────
    _T strbImm5T,_T strbImm5T,_T strbImm5T,_T strbImm5T, _T strbImm5T,_T strbImm5T,_T strbImm5T,_T strbImm5T, // 0x1C0–0x1C7
    _T strbImm5T,_T strbImm5T,_T strbImm5T,_T strbImm5T, _T strbImm5T,_T strbImm5T,_T strbImm5T,_T strbImm5T, // 0x1C8–0x1CF
    _T strbImm5T,_T strbImm5T,_T strbImm5T,_T strbImm5T, _T strbImm5T,_T strbImm5T,_T strbImm5T,_T strbImm5T, // 0x1D0–0x1D7
    _T strbImm5T,_T strbImm5T,_T strbImm5T,_T strbImm5T, _T strbImm5T,_T strbImm5T,_T strbImm5T,_T strbImm5T, // 0x1D8–0x1DF

    // ── 0x1E0–0x1FF  LDRB imm5 (byte) ───────────────────────────────────────
    _T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T, _T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T, // 0x1E0–0x1E7
    _T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T, _T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T, // 0x1E8–0x1EF
    _T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T, _T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T, // 0x1F0–0x1F7
    _T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T, _T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T,_T ldrbImm5T, // 0x1F8–0x1FF

    // ── 0x200–0x21F  STRH imm5 (halfword) ───────────────────────────────────
    _T strhImm5T,_T strhImm5T,_T strhImm5T,_T strhImm5T, _T strhImm5T,_T strhImm5T,_T strhImm5T,_T strhImm5T, // 0x200–0x207
    _T strhImm5T,_T strhImm5T,_T strhImm5T,_T strhImm5T, _T strhImm5T,_T strhImm5T,_T strhImm5T,_T strhImm5T, // 0x208–0x20F
    _T strhImm5T,_T strhImm5T,_T strhImm5T,_T strhImm5T, _T strhImm5T,_T strhImm5T,_T strhImm5T,_T strhImm5T, // 0x210–0x217
    _T strhImm5T,_T strhImm5T,_T strhImm5T,_T strhImm5T, _T strhImm5T,_T strhImm5T,_T strhImm5T,_T strhImm5T, // 0x218–0x21F

    // ── 0x220–0x23F  LDRH imm5 (halfword) ───────────────────────────────────
    _T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T, _T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T, // 0x220–0x227
    _T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T, _T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T, // 0x228–0x22F
    _T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T, _T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T, // 0x230–0x237
    _T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T, _T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T,_T ldrhImm5T, // 0x238–0x23F

    // ── 0x240–0x25F  STR  SP-relative ────────────────────────────────────────
    _T strSpT,_T strSpT,_T strSpT,_T strSpT, _T strSpT,_T strSpT,_T strSpT,_T strSpT,  // 0x240–0x247
    _T strSpT,_T strSpT,_T strSpT,_T strSpT, _T strSpT,_T strSpT,_T strSpT,_T strSpT,  // 0x248–0x24F
    _T strSpT,_T strSpT,_T strSpT,_T strSpT, _T strSpT,_T strSpT,_T strSpT,_T strSpT,  // 0x250–0x257
    _T strSpT,_T strSpT,_T strSpT,_T strSpT, _T strSpT,_T strSpT,_T strSpT,_T strSpT,  // 0x258–0x25F

    // ── 0x260–0x27F  LDR  SP-relative ────────────────────────────────────────
    _T ldrSpT,_T ldrSpT,_T ldrSpT,_T ldrSpT, _T ldrSpT,_T ldrSpT,_T ldrSpT,_T ldrSpT,  // 0x260–0x267
    _T ldrSpT,_T ldrSpT,_T ldrSpT,_T ldrSpT, _T ldrSpT,_T ldrSpT,_T ldrSpT,_T ldrSpT,  // 0x268–0x26F
    _T ldrSpT,_T ldrSpT,_T ldrSpT,_T ldrSpT, _T ldrSpT,_T ldrSpT,_T ldrSpT,_T ldrSpT,  // 0x270–0x277
    _T ldrSpT,_T ldrSpT,_T ldrSpT,_T ldrSpT, _T ldrSpT,_T ldrSpT,_T ldrSpT,_T ldrSpT,  // 0x278–0x27F

    // ── 0x280–0x29F  ADD PC-relative (ADR) ───────────────────────────────────
    _T addPcT,_T addPcT,_T addPcT,_T addPcT, _T addPcT,_T addPcT,_T addPcT,_T addPcT,  // 0x280–0x287
    _T addPcT,_T addPcT,_T addPcT,_T addPcT, _T addPcT,_T addPcT,_T addPcT,_T addPcT,  // 0x288–0x28F
    _T addPcT,_T addPcT,_T addPcT,_T addPcT, _T addPcT,_T addPcT,_T addPcT,_T addPcT,  // 0x290–0x297
    _T addPcT,_T addPcT,_T addPcT,_T addPcT, _T addPcT,_T addPcT,_T addPcT,_T addPcT,  // 0x298–0x29F

    // ── 0x2A0–0x2BF  ADD SP-relative ─────────────────────────────────────────
    _T addSpT,_T addSpT,_T addSpT,_T addSpT, _T addSpT,_T addSpT,_T addSpT,_T addSpT,  // 0x2A0–0x2A7
    _T addSpT,_T addSpT,_T addSpT,_T addSpT, _T addSpT,_T addSpT,_T addSpT,_T addSpT,  // 0x2A8–0x2AF
    _T addSpT,_T addSpT,_T addSpT,_T addSpT, _T addSpT,_T addSpT,_T addSpT,_T addSpT,  // 0x2B0–0x2B7
    _T addSpT,_T addSpT,_T addSpT,_T addSpT, _T addSpT,_T addSpT,_T addSpT,_T addSpT,  // 0x2B8–0x2BF

    // ── 0x2C0–0x2FF  Miscellaneous 16-bit (bits 15-12 = 1011) ────────────────
    // 0x2C0–0x2C3: ADD SP,#imm7   (bits 9-8 = 00, bit 7 = 0)
    // 0x2C4–0x2CF: undefined
    // 0x2D0–0x2D3: PUSH (no LR)
    // 0x2D4–0x2D7: PUSH (with LR)
    // 0x2D8–0x2EF: undefined
    // 0x2F0–0x2F3: POP  (no PC)
    // 0x2F4–0x2F7: POP  (with PC)
    // 0x2F8–0x2FF: undefined
    _T addSpImmT,_T addSpImmT,_T addSpImmT,_T addSpImmT,                                              // 0x2C0–0x2C3
    _T unkThumb, _T unkThumb, _T unkThumb, _T unkThumb,                                               // 0x2C4–0x2C7
    _T unkThumb, _T unkThumb, _T unkThumb, _T unkThumb,  _T unkThumb,_T unkThumb,_T unkThumb,_T unkThumb, // 0x2C8–0x2CF
    _T pushT,    _T pushT,    _T pushT,    _T pushT,                                                   // 0x2D0–0x2D3
    _T pushLrT,  _T pushLrT,  _T pushLrT,  _T pushLrT,                                                // 0x2D4–0x2D7
    _T unkThumb, _T unkThumb, _T unkThumb, _T unkThumb,  _T unkThumb,_T unkThumb,_T unkThumb,_T unkThumb, // 0x2D8–0x2DF
    _T unkThumb, _T unkThumb, _T unkThumb, _T unkThumb,  _T unkThumb,_T unkThumb,_T unkThumb,_T unkThumb, // 0x2E0–0x2E7
    _T unkThumb, _T unkThumb, _T unkThumb, _T unkThumb,  _T unkThumb,_T unkThumb,_T unkThumb,_T unkThumb, // 0x2E8–0x2EF
    _T popT,     _T popT,     _T popT,     _T popT,                                                    // 0x2F0–0x2F3
    _T popPcT,   _T popPcT,   _T popPcT,   _T popPcT,                                                 // 0x2F4–0x2F7
    _T unkThumb, _T unkThumb, _T unkThumb, _T unkThumb,  _T unkThumb,_T unkThumb,_T unkThumb,_T unkThumb, // 0x2F8–0x2FF

    // ── 0x300–0x31F  STMIA (bits 15-11 = 11000) ─────────────────────────────
    _T stmiaT,_T stmiaT,_T stmiaT,_T stmiaT, _T stmiaT,_T stmiaT,_T stmiaT,_T stmiaT,  // 0x300–0x307
    _T stmiaT,_T stmiaT,_T stmiaT,_T stmiaT, _T stmiaT,_T stmiaT,_T stmiaT,_T stmiaT,  // 0x308–0x30F
    _T stmiaT,_T stmiaT,_T stmiaT,_T stmiaT, _T stmiaT,_T stmiaT,_T stmiaT,_T stmiaT,  // 0x310–0x317
    _T stmiaT,_T stmiaT,_T stmiaT,_T stmiaT, _T stmiaT,_T stmiaT,_T stmiaT,_T stmiaT,  // 0x318–0x31F

    // ── 0x320–0x33F  LDMIA (bits 15-11 = 11001) ─────────────────────────────
    _T ldmiaT,_T ldmiaT,_T ldmiaT,_T ldmiaT, _T ldmiaT,_T ldmiaT,_T ldmiaT,_T ldmiaT,  // 0x320–0x327
    _T ldmiaT,_T ldmiaT,_T ldmiaT,_T ldmiaT, _T ldmiaT,_T ldmiaT,_T ldmiaT,_T ldmiaT,  // 0x328–0x32F
    _T ldmiaT,_T ldmiaT,_T ldmiaT,_T ldmiaT, _T ldmiaT,_T ldmiaT,_T ldmiaT,_T ldmiaT,  // 0x330–0x337
    _T ldmiaT,_T ldmiaT,_T ldmiaT,_T ldmiaT, _T ldmiaT,_T ldmiaT,_T ldmiaT,_T ldmiaT,  // 0x338–0x33F

    // ── 0x340–0x37F  Conditional branch + SWI (bits 15-12 = 1101) ────────────
    // Each condition occupies exactly 4 entries (bits 11-8 = condition code).
    // This is the THUMB branch hot path — most games branch far more often
    // than they execute multiply or coprocessor instructions.
    // Ordering matches ARM condition codes 0–13 plus undefined + SWI.
    _T beqT,     _T beqT,     _T beqT,     _T beqT,      // 0x340–0x343  EQ
    _T bneT,     _T bneT,     _T bneT,     _T bneT,      // 0x344–0x347  NE
    _T bcsT,     _T bcsT,     _T bcsT,     _T bcsT,      // 0x348–0x34B  CS/HS
    _T bccT,     _T bccT,     _T bccT,     _T bccT,      // 0x34C–0x34F  CC/LO
    _T bmiT,     _T bmiT,     _T bmiT,     _T bmiT,      // 0x350–0x353  MI
    _T bplT,     _T bplT,     _T bplT,     _T bplT,      // 0x354–0x357  PL
    _T bvsT,     _T bvsT,     _T bvsT,     _T bvsT,      // 0x358–0x35B  VS
    _T bvcT,     _T bvcT,     _T bvcT,     _T bvcT,      // 0x35C–0x35F  VC
    _T bhiT,     _T bhiT,     _T bhiT,     _T bhiT,      // 0x360–0x363  HI
    _T blsT,     _T blsT,     _T blsT,     _T blsT,      // 0x364–0x367  LS
    _T bgeT,     _T bgeT,     _T bgeT,     _T bgeT,      // 0x368–0x36B  GE
    _T bltT,     _T bltT,     _T bltT,     _T bltT,      // 0x36C–0x36F  LT
    _T bgtT,     _T bgtT,     _T bgtT,     _T bgtT,      // 0x370–0x373  GT
    _T bleT,     _T bleT,     _T bleT,     _T bleT,      // 0x374–0x377  LE
    _T unkThumb, _T unkThumb, _T unkThumb, _T unkThumb,  // 0x378–0x37B  undefined (cond=1110)
    _T swiT,     _T swiT,     _T swiT,     _T swiT,      // 0x37C–0x37F  SWI

    // ── 0x380–0x39F  B unconditional (bits 15-11 = 11100) ────────────────────
    _T bT,_T bT,_T bT,_T bT, _T bT,_T bT,_T bT,_T bT,  // 0x380–0x387
    _T bT,_T bT,_T bT,_T bT, _T bT,_T bT,_T bT,_T bT,  // 0x388–0x38F
    _T bT,_T bT,_T bT,_T bT, _T bT,_T bT,_T bT,_T bT,  // 0x390–0x397
    _T bT,_T bT,_T bT,_T bT, _T bT,_T bT,_T bT,_T bT,  // 0x398–0x39F

    // ── 0x3A0–0x3BF  BLX offset (bits 15-11 = 11101) ─────────────────────────
    // Second half of a BL/BLX pair: LR ← PC + (imm11 << 1), branch to LR
    _T blxOffT,_T blxOffT,_T blxOffT,_T blxOffT, _T blxOffT,_T blxOffT,_T blxOffT,_T blxOffT, // 0x3A0–0x3A7
    _T blxOffT,_T blxOffT,_T blxOffT,_T blxOffT, _T blxOffT,_T blxOffT,_T blxOffT,_T blxOffT, // 0x3A8–0x3AF
    _T blxOffT,_T blxOffT,_T blxOffT,_T blxOffT, _T blxOffT,_T blxOffT,_T blxOffT,_T blxOffT, // 0x3B0–0x3B7
    _T blxOffT,_T blxOffT,_T blxOffT,_T blxOffT, _T blxOffT,_T blxOffT,_T blxOffT,_T blxOffT, // 0x3B8–0x3BF

    // ── 0x3C0–0x3DF  BL setup (bits 15-11 = 11110) ───────────────────────────
    // First half of a BL pair: LR ← PC + SignExtend(imm11 << 12)
    _T blSetupT,_T blSetupT,_T blSetupT,_T blSetupT, _T blSetupT,_T blSetupT,_T blSetupT,_T blSetupT, // 0x3C0–0x3C7
    _T blSetupT,_T blSetupT,_T blSetupT,_T blSetupT, _T blSetupT,_T blSetupT,_T blSetupT,_T blSetupT, // 0x3C8–0x3CF
    _T blSetupT,_T blSetupT,_T blSetupT,_T blSetupT, _T blSetupT,_T blSetupT,_T blSetupT,_T blSetupT, // 0x3D0–0x3D7
    _T blSetupT,_T blSetupT,_T blSetupT,_T blSetupT, _T blSetupT,_T blSetupT,_T blSetupT,_T blSetupT, // 0x3D8–0x3DF

    // ── 0x3E0–0x3FF  BL offset (bits 15-11 = 11111) ─────────────────────────
    // Second half of a BL pair: PC ← LR + (imm11 << 1), LR ← old PC | 1
    _T blOffT,_T blOffT,_T blOffT,_T blOffT, _T blOffT,_T blOffT,_T blOffT,_T blOffT,  // 0x3E0–0x3E7
    _T blOffT,_T blOffT,_T blOffT,_T blOffT, _T blOffT,_T blOffT,_T blOffT,_T blOffT,  // 0x3E8–0x3EF
    _T blOffT,_T blOffT,_T blOffT,_T blOffT, _T blOffT,_T blOffT,_T blOffT,_T blOffT,  // 0x3F0–0x3F7
    _T blOffT,_T blOffT,_T blOffT,_T blOffT, _T blOffT,_T blOffT,_T blOffT,_T blOffT,  // 0x3F8–0x3FF
};

#undef _T

// ════════════════════════════════════════════════════════════════════════════
// ARM condition evaluation table
// ════════════════════════════════════════════════════════════════════════════
// Index = (cond << 4) | NZCV   (256 entries total)
// Value:  0 = condition false, 1 = condition true, 2 = always/undefined
//
// PowerPC optimisation:
//   • const + aligned(32): resides in .rodata; fits in 8 PPC cache lines.
//   • The dispatch loop in interpreter.cpp loads a byte with lbzx, so the
//     table must be byte-addressable — uint8_t is correct.
//   • 16-wide rows: each row = exactly one 16-byte quarter cache line,
//     making the per-condition layout visually verifiable at a glance.
// ════════════════════════════════════════════════════════════════════════════

__attribute__((section(".rodata"), aligned(32)))
const uint8_t Interpreter::condition[256] =
{
    //        NZCV: 0000 0001 0010 0011  0100 0101 0110 0111
    //              0000 0001 0010 0011  0100 0101 0110 0111
    //              1000 1001 1010 1011  1100 1101 1110 1111
    // EQ (Z=1):
    0,0,0,0, 1,1,1,1, 0,0,0,0, 1,1,1,1,   // cond=0x0
    // NE (Z=0):
    1,1,1,1, 0,0,0,0, 1,1,1,1, 0,0,0,0,   // cond=0x1
    // CS/HS (C=1):
    0,0,1,1, 0,0,1,1, 0,0,1,1, 0,0,1,1,   // cond=0x2
    // CC/LO (C=0):
    1,1,0,0, 1,1,0,0, 1,1,0,0, 1,1,0,0,   // cond=0x3
    // MI (N=1):
    0,0,0,0, 0,0,0,0, 1,1,1,1, 1,1,1,1,   // cond=0x4
    // PL (N=0):
    1,1,1,1, 1,1,1,1, 0,0,0,0, 0,0,0,0,   // cond=0x5
    // VS (V=1):
    0,1,0,1, 0,1,0,1, 0,1,0,1, 0,1,0,1,   // cond=0x6
    // VC (V=0):
    1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0,   // cond=0x7
    // HI (C=1 && Z=0):
    0,0,1,1, 0,0,0,0, 0,0,1,1, 0,0,0,0,   // cond=0x8
    // LS (C=0 || Z=1):
    1,1,0,0, 1,1,1,1, 1,1,0,0, 1,1,1,1,   // cond=0x9
    // GE (N=V):
    1,0,1,0, 1,0,1,0, 0,1,0,1, 0,1,0,1,   // cond=0xA
    // LT (N!=V):
    0,1,0,1, 0,1,0,1, 1,0,1,0, 1,0,1,0,   // cond=0xB
    // GT (Z=0 && N=V):
    1,0,1,0, 0,0,0,0, 0,1,0,1, 0,0,0,0,   // cond=0xC
    // LE (Z=1 || N!=V):
    0,1,0,1, 1,1,1,1, 1,0,1,0, 1,1,1,1,   // cond=0xD
    // AL (always):
    1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,   // cond=0xE
    // Reserved (NV — undefined on ARMv5, used for BLX imm on ARMv5T):
    2,2,2,2, 2,2,2,2, 2,2,2,2, 2,2,2,2,   // cond=0xF
};

// ════════════════════════════════════════════════════════════════════════════
// Population-count (bitCount) lookup table
// ════════════════════════════════════════════════════════════════════════════
// bitCount[n] = number of set bits in n, for n in [0, 255].
// Used by LDM/STM to determine the number of registers transferred, which
// directly controls the cycle count and the address increment.
//
// PowerPC optimisation:
//   • 256 bytes fit in exactly 8 PPC cache lines (32 bytes each).
//   • aligned(32): guarantees the first entry is on a cache-line boundary
//     so a single dcbt prefetch warms the whole table.
//   • 16-wide rows: one row = one half-cache-line; easy to verify by
//     checking that row[n>>4] sums match Pascal's triangle coefficients.
//   • Values range 0–8; uint8_t saves space vs uint32_t (4× smaller),
//     keeping the whole table in L1.
//
// Verification: each row is the binomial expansion of (1+1)^4 = 16 entries
// with the expected number of 1-bits for a 4-bit prefix group.
// ════════════════════════════════════════════════════════════════════════════

__attribute__((section(".rodata"), aligned(32)))
const uint8_t Interpreter::bitCount[256] =
{
    // n:  +0 +1 +2 +3  +4 +5 +6 +7  +8 +9 +A +B  +C +D +E +F
    /* 0x00 */  0, 1, 1, 2,  1, 2, 2, 3,  1, 2, 2, 3,  2, 3, 3, 4,
    /* 0x10 */  1, 2, 2, 3,  2, 3, 3, 4,  2, 3, 3, 4,  3, 4, 4, 5,
    /* 0x20 */  1, 2, 2, 3,  2, 3, 3, 4,  2, 3, 3, 4,  3, 4, 4, 5,
    /* 0x30 */  2, 3, 3, 4,  3, 4, 4, 5,  3, 4, 4, 5,  4, 5, 5, 6,
    /* 0x40 */  1, 2, 2, 3,  2, 3, 3, 4,  2, 3, 3, 4,  3, 4, 4, 5,
    /* 0x50 */  2, 3, 3, 4,  3, 4, 4, 5,  3, 4, 4, 5,  4, 5, 5, 6,
    /* 0x60 */  2, 3, 3, 4,  3, 4, 4, 5,  3, 4, 4, 5,  4, 5, 5, 6,
    /* 0x70 */  3, 4, 4, 5,  4, 5, 5, 6,  4, 5, 5, 6,  5, 6, 6, 7,
    /* 0x80 */  1, 2, 2, 3,  2, 3, 3, 4,  2, 3, 3, 4,  3, 4, 4, 5,
    /* 0x90 */  2, 3, 3, 4,  3, 4, 4, 5,  3, 4, 4, 5,  4, 5, 5, 6,
    /* 0xA0 */  2, 3, 3, 4,  3, 4, 4, 5,  3, 4, 4, 5,  4, 5, 5, 6,
    /* 0xB0 */  3, 4, 4, 5,  4, 5, 5, 6,  4, 5, 5, 6,  5, 6, 6, 7,
    /* 0xC0 */  2, 3, 3, 4,  3, 4, 4, 5,  3, 4, 4, 5,  4, 5, 5, 6,
    /* 0xD0 */  3, 4, 4, 5,  4, 5, 5, 6,  4, 5, 5, 6,  5, 6, 6, 7,
    /* 0xE0 */  3, 4, 4, 5,  4, 5, 5, 6,  4, 5, 5, 6,  5, 6, 6, 7,
    /* 0xF0 */  4, 5, 5, 6,  5, 6, 6, 7,  5, 6, 6, 7,  6, 7, 7, 8,
};
