//interpreter_lookup.cpp (optimized for PowerPC/Wii)
// This file contains the ARM and THUMB opcode dispatch tables.
// These are the hottest data structures in the emulator - they are accessed
// on every single instruction fetch. Optimizations focus on:
//   1. Cache line alignment (32 bytes on Broadway/Gekko PPC)
//   2. Placing table in a dedicated section for linker control
//   3. Keeping the table as compact as possible
//   4. Prefetch hints in the dispatch code (added as inline asm macros here)

#include "core.h"

// Place the dispatch tables in a dedicated read-only section.
// The Wii linker script can be configured to put .rodata.dispatch
// in a fast memory region (e.g. L2-cached MEM1).
// __attribute__((section(".rodata.dispatch"))) tells the linker where to put it.
// __attribute__((aligned(32))) ensures the table starts on a cache line boundary,
// preventing the first access from straddling two cache lines.
//
// On the Broadway CPU (Wii), cache lines are 32 bytes = 8 function pointers
// at 4 bytes each. Aligning to 32 bytes means each group of 8 table entries
// maps exactly to one cache line - optimal for sequential dispatch.

#define DISPATCH_TABLE \
    __attribute__((section(".rodata.dispatch"), aligned(32)))

// ARM instruction dispatch table
// Indexed by: ((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)
// This gives a 12-bit index into a 4096-entry table.
// Each entry is a member function pointer (4 bytes on Wii/PPC32).
// Total table size: 4096 * 4 = 16384 bytes = 512 cache lines.
//
// The table is laid out so that the most common instruction groups
// (data processing with register operands, 0x000-0x1FF) appear first
// and are most likely to be in cache during normal emulation.

DISPATCH_TABLE
int (Interpreter::*Interpreter::armInstrs[])(uint32_t) = {
    // ----------------------------------------------------------------
    // 0x000-0x0FF: Data processing, register shifts, multiplies,
    //              and post-indexed half/signed/double word transfers
    // ----------------------------------------------------------------

    // 0x000-0x00F: AND/MUL group
    &Interpreter::_andLli, &Interpreter::_andLlr,
    &Interpreter::_andLri, &Interpreter::_andLrr,
    &Interpreter::_andAri, &Interpreter::_andArr,
    &Interpreter::_andRri, &Interpreter::_andRrr,
    &Interpreter::_andLli, &Interpreter::mul,
    &Interpreter::_andLri, &Interpreter::strhPtrm,
    &Interpreter::_andAri, &Interpreter::ldrdPtrm,
    &Interpreter::_andRri, &Interpreter::strdPtrm,

    // 0x010-0x01F: ANDS/MULS group
    &Interpreter::andsLli, &Interpreter::andsLlr,
    &Interpreter::andsLri, &Interpreter::andsLrr,
    &Interpreter::andsAri, &Interpreter::andsArr,
    &Interpreter::andsRri, &Interpreter::andsRrr,
    &Interpreter::andsLli, &Interpreter::muls,
    &Interpreter::andsLri, &Interpreter::ldrhPtrm,
    &Interpreter::andsAri, &Interpreter::ldrsbPtrm,
    &Interpreter::andsRri, &Interpreter::ldrshPtrm,

    // 0x020-0x02F: EOR/MLA group
    &Interpreter::eorLli,  &Interpreter::eorLlr,
    &Interpreter::eorLri,  &Interpreter::eorLrr,
    &Interpreter::eorAri,  &Interpreter::eorArr,
    &Interpreter::eorRri,  &Interpreter::eorRrr,
    &Interpreter::eorLli,  &Interpreter::mla,
    &Interpreter::eorLri,  &Interpreter::strhPtrm,
    &Interpreter::eorAri,  &Interpreter::ldrdPtrm,
    &Interpreter::eorRri,  &Interpreter::strdPtrm,

    // 0x030-0x03F: EORS/MLAS group
    &Interpreter::eorsLli, &Interpreter::eorsLlr,
    &Interpreter::eorsLri, &Interpreter::eorsLrr,
    &Interpreter::eorsAri, &Interpreter::eorsArr,
    &Interpreter::eorsRri, &Interpreter::eorsRrr,
    &Interpreter::eorsLli, &Interpreter::mlas,
    &Interpreter::eorsLri, &Interpreter::ldrhPtrm,
    &Interpreter::eorsAri, &Interpreter::ldrsbPtrm,
    &Interpreter::eorsRri, &Interpreter::ldrshPtrm,

    // 0x040-0x04F: SUB group
    &Interpreter::subLli,  &Interpreter::subLlr,
    &Interpreter::subLri,  &Interpreter::subLrr,
    &Interpreter::subAri,  &Interpreter::subArr,
    &Interpreter::subRri,  &Interpreter::subRrr,
    &Interpreter::subLli,  &Interpreter::unkArm,
    &Interpreter::subLri,  &Interpreter::strhPtim,
    &Interpreter::subAri,  &Interpreter::ldrdPtim,
    &Interpreter::subRri,  &Interpreter::strdPtim,

    // 0x050-0x05F: SUBS group
    &Interpreter::subsLli, &Interpreter::subsLlr,
    &Interpreter::subsLri, &Interpreter::subsLrr,
    &Interpreter::subsAri, &Interpreter::subsArr,
    &Interpreter::subsRri, &Interpreter::subsRrr,
    &Interpreter::subsLli, &Interpreter::unkArm,
    &Interpreter::subsLri, &Interpreter::ldrhPtim,
    &Interpreter::subsAri, &Interpreter::ldrsbPtim,
    &Interpreter::subsRri, &Interpreter::ldrshPtim,

    // 0x060-0x06F: RSB group
    &Interpreter::rsbLli,  &Interpreter::rsbLlr,
    &Interpreter::rsbLri,  &Interpreter::rsbLrr,
    &Interpreter::rsbAri,  &Interpreter::rsbArr,
    &Interpreter::rsbRri,  &Interpreter::rsbRrr,
    &Interpreter::rsbLli,  &Interpreter::unkArm,
    &Interpreter::rsbLri,  &Interpreter::strhPtim,
    &Interpreter::rsbAri,  &Interpreter::ldrdPtim,
    &Interpreter::rsbRri,  &Interpreter::strdPtim,

    // 0x070-0x07F: RSBS group
    &Interpreter::rsbsLli, &Interpreter::rsbsLlr,
    &Interpreter::rsbsLri, &Interpreter::rsbsLrr,
    &Interpreter::rsbsAri, &Interpreter::rsbsArr,
    &Interpreter::rsbsRri, &Interpreter::rsbsRrr,
    &Interpreter::rsbsLli, &Interpreter::unkArm,
    &Interpreter::rsbsLri, &Interpreter::ldrhPtim,
    &Interpreter::rsbsAri, &Interpreter::ldrsbPtim,
    &Interpreter::rsbsRri, &Interpreter::ldrshPtim,

    // 0x080-0x08F: ADD/UMULL group
    &Interpreter::addLli,  &Interpreter::addLlr,
    &Interpreter::addLri,  &Interpreter::addLrr,
    &Interpreter::addAri,  &Interpreter::addArr,
    &Interpreter::addRri,  &Interpreter::addRrr,
    &Interpreter::addLli,  &Interpreter::umull,
    &Interpreter::addLri,  &Interpreter::strhPtrp,
    &Interpreter::addAri,  &Interpreter::ldrdPtrp,
    &Interpreter::addRri,  &Interpreter::strdPtrp,

    // 0x090-0x09F: ADDS/UMULLS group
    &Interpreter::addsLli, &Interpreter::addsLlr,
    &Interpreter::addsLri, &Interpreter::addsLrr,
    &Interpreter::addsAri, &Interpreter::addsArr,
    &Interpreter::addsRri, &Interpreter::addsRrr,
    &Interpreter::addsLli, &Interpreter::umulls,
    &Interpreter::addsLri, &Interpreter::ldrhPtrp,
    &Interpreter::addsAri, &Interpreter::ldrsbPtrp,
    &Interpreter::addsRri, &Interpreter::ldrshPtrp,

    // 0x0A0-0x0AF: ADC/UMLAL group
    &Interpreter::adcLli,  &Interpreter::adcLlr,
    &Interpreter::adcLri,  &Interpreter::adcLrr,
    &Interpreter::adcAri,  &Interpreter::adcArr,
    &Interpreter::adcRri,  &Interpreter::adcRrr,
    &Interpreter::adcLli,  &Interpreter::umlal,
    &Interpreter::adcLri,  &Interpreter::strhPtrp,
    &Interpreter::adcAri,  &Interpreter::ldrdPtrp,
    &Interpreter::adcRri,  &Interpreter::strdPtrp,

    // 0x0B0-0x0BF: ADCS/UMLALS group
    &Interpreter::adcsLli, &Interpreter::adcsLlr,
    &Interpreter::adcsLri, &Interpreter::adcsLrr,
    &Interpreter::adcsAri, &Interpreter::adcsArr,
    &Interpreter::adcsRri, &Interpreter::adcsRrr,
    &Interpreter::adcsLli, &Interpreter::umlals,
    &Interpreter::adcsLri, &Interpreter::ldrhPtrp,
    &Interpreter::adcsAri, &Interpreter::ldrsbPtrp,
    &Interpreter::adcsRri, &Interpreter::ldrshPtrp,

    // 0x0C0-0x0CF: SBC/SMULL group
    &Interpreter::sbcLli,  &Interpreter::sbcLlr,
    &Interpreter::sbcLri,  &Interpreter::sbcLrr,
    &Interpreter::sbcAri,  &Interpreter::sbcArr,
    &Interpreter::sbcRri,  &Interpreter::sbcRrr,
    &Interpreter::sbcLli,  &Interpreter::smull,
    &Interpreter::sbcLri,  &Interpreter::strhPtip,
    &Interpreter::sbcAri,  &Interpreter::ldrdPtip,
    &Interpreter::sbcRri,  &Interpreter::strdPtip,

    // 0x0D0-0x0DF: SBCS/SMULLS group
    &Interpreter::sbcsLli, &Interpreter::sbcsLlr,
    &Interpreter::sbcsLri, &Interpreter::sbcsLrr,
    &Interpreter::sbcsAri, &Interpreter::sbcsArr,
    &Interpreter::sbcsRri, &Interpreter::sbcsRrr,
    &Interpreter::sbcsLli, &Interpreter::smulls,
    &Interpreter::sbcsLri, &Interpreter::ldrhPtip,
    &Interpreter::sbcsAri, &Interpreter::ldrsbPtip,
    &Interpreter::sbcsRri, &Interpreter::ldrshPtip,

    // 0x0E0-0x0EF: RSC/SMLAL group
    &Interpreter::rscLli,  &Interpreter::rscLlr,
    &Interpreter::rscLri,  &Interpreter::rscLrr,
    &Interpreter::rscAri,  &Interpreter::rscArr,
    &Interpreter::rscRri,  &Interpreter::rscRrr,
    &Interpreter::rscLli,  &Interpreter::smlal,
    &Interpreter::rscLri,  &Interpreter::strhPtip,
    &Interpreter::rscAri,  &Interpreter::ldrdPtip,
    &Interpreter::rscRri,  &Interpreter::strdPtip,

    // 0x0F0-0x0FF: RSCS/SMLALS group
    &Interpreter::rscsLli, &Interpreter::rscsLlr,
    &Interpreter::rscsLri, &Interpreter::rscsLrr,
    &Interpreter::rscsAri, &Interpreter::rscsArr,
    &Interpreter::rscsRri, &Interpreter::rscsRrr,
    &Interpreter::rscsLli, &Interpreter::smlals,
    &Interpreter::rscsLri, &Interpreter::ldrhPtip,
    &Interpreter::rscsAri, &Interpreter::ldrsbPtip,
    &Interpreter::rscsRri, &Interpreter::ldrshPtip,

    // ----------------------------------------------------------------
    // 0x100-0x1FF: Miscellaneous: MRS, MSR, BX, CLZ, SWP,
    //              SMLA*, SMUL*, TST, TEQ, CMP, CMN, ORR, MOV,
    //              BIC, MVN - register variants + pre-indexed transfers
    // ----------------------------------------------------------------

    // 0x100-0x10F: MRS CPSR, QADD, SMLA, SWP, STRH/LDRD/STRD Ofrm
    &Interpreter::mrsRc,   &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::qadd,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::smlabb,  &Interpreter::swp,
    &Interpreter::smlatb,  &Interpreter::strhOfrm,
    &Interpreter::smlabt,  &Interpreter::ldrdOfrm,
    &Interpreter::smlatt,  &Interpreter::strdOfrm,

    // 0x110-0x11F: TST register group
    &Interpreter::tstLli,  &Interpreter::tstLlr,
    &Interpreter::tstLri,  &Interpreter::tstLrr,
    &Interpreter::tstAri,  &Interpreter::tstArr,
    &Interpreter::tstRri,  &Interpreter::tstRrr,
    &Interpreter::tstLli,  &Interpreter::unkArm,
    &Interpreter::tstLri,  &Interpreter::ldrhOfrm,
    &Interpreter::tstAri,  &Interpreter::ldrsbOfrm,
    &Interpreter::tstRri,  &Interpreter::ldrshOfrm,

    // 0x120-0x12F: MSR CPSR, BX, BLX, QSUB, SMLAW*, STRH/LDRD/STRD Prrm
    &Interpreter::msrRc,   &Interpreter::bx,
    &Interpreter::unkArm,  &Interpreter::blxReg,
    &Interpreter::unkArm,  &Interpreter::qsub,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::smlawb,  &Interpreter::unkArm,
    &Interpreter::smulwb,  &Interpreter::strhPrrm,
    &Interpreter::smlawt,  &Interpreter::ldrdPrrm,
    &Interpreter::smulwt,  &Interpreter::strdPrrm,

    // 0x130-0x13F: TEQ register group
    &Interpreter::teqLli,  &Interpreter::teqLlr,
    &Interpreter::teqLri,  &Interpreter::teqLrr,
    &Interpreter::teqAri,  &Interpreter::teqArr,
    &Interpreter::teqRri,  &Interpreter::teqRrr,
    &Interpreter::teqLli,  &Interpreter::unkArm,
    &Interpreter::teqLri,  &Interpreter::ldrhPrrm,
    &Interpreter::teqAri,  &Interpreter::ldrsbPrrm,
    &Interpreter::teqRri,  &Interpreter::ldrshPrrm,

    // 0x140-0x14F: MRS SPSR, QDADD, SMLAL*, SWPB, STRH/LDRD/STRD Ofim
    &Interpreter::mrsRs,   &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::qdadd,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::smlalbb, &Interpreter::swpb,
    &Interpreter::smlaltb, &Interpreter::strhOfim,
    &Interpreter::smlalbt, &Interpreter::ldrdOfim,
    &Interpreter::smlaltt, &Interpreter::strdOfim,

    // 0x150-0x15F: CMP register group
    &Interpreter::cmpLli,  &Interpreter::cmpLlr,
    &Interpreter::cmpLri,  &Interpreter::cmpLrr,
    &Interpreter::cmpAri,  &Interpreter::cmpArr,
    &Interpreter::cmpRri,  &Interpreter::cmpRrr,
    &Interpreter::cmpLli,  &Interpreter::unkArm,
    &Interpreter::cmpLri,  &Interpreter::ldrhOfim,
    &Interpreter::cmpAri,  &Interpreter::ldrsbOfim,
    &Interpreter::cmpRri,  &Interpreter::ldrshOfim,

    // 0x160-0x16F: MSR SPSR, CLZ, QDSUB, SMUL*, STRH/LDRD/STRD Prim
    &Interpreter::msrRs,   &Interpreter::clz,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::qdsub,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::smulbb,  &Interpreter::unkArm,
    &Interpreter::smultb,  &Interpreter::strhPrim,
    &Interpreter::smulbt,  &Interpreter::ldrdPrim,
    &Interpreter::smultt,  &Interpreter::strdPrim,

    // 0x170-0x17F: CMN register group
    &Interpreter::cmnLli,  &Interpreter::cmnLlr,
    &Interpreter::cmnLri,  &Interpreter::cmnLrr,
    &Interpreter::cmnAri,  &Interpreter::cmnArr,
    &Interpreter::cmnRri,  &Interpreter::cmnRrr,
    &Interpreter::cmnLli,  &Interpreter::unkArm,
    &Interpreter::cmnLri,  &Interpreter::ldrhPrim,
    &Interpreter::cmnAri,  &Interpreter::ldrsbPrim,
    &Interpreter::cmnRri,  &Interpreter::ldrshPrim,

    // 0x180-0x18F: ORR register group + STRH/LDRD/STRD Ofrp
    &Interpreter::orrLli,  &Interpreter::orrLlr,
    &Interpreter::orrLri,  &Interpreter::orrLrr,
    &Interpreter::orrAri,  &Interpreter::orrArr,
    &Interpreter::orrRri,  &Interpreter::orrRrr,
    &Interpreter::orrLli,  &Interpreter::unkArm,
    &Interpreter::orrLri,  &Interpreter::strhOfrp,
    &Interpreter::orrAri,  &Interpreter::ldrdOfrp,
    &Interpreter::orrRri,  &Interpreter::strdOfrp,

    // 0x190-0x19F: ORRS register group
    &Interpreter::orrsLli, &Interpreter::orrsLlr,
    &Interpreter::orrsLri, &Interpreter::orrsLrr,
    &Interpreter::orrsAri, &Interpreter::orrsArr,
    &Interpreter::orrsRri, &Interpreter::orrsRrr,
    &Interpreter::orrsLli, &Interpreter::unkArm,
    &Interpreter::orrsLri, &Interpreter::ldrhOfrp,
    &Interpreter::orrsAri, &Interpreter::ldrsbOfrp,
    &Interpreter::orrsRri, &Interpreter::ldrshOfrp,

    // 0x1A0-0x1AF: MOV register group + STRH/LDRD/STRD Prrp
    &Interpreter::movLli,  &Interpreter::movLlr,
    &Interpreter::movLri,  &Interpreter::movLrr,
    &Interpreter::movAri,  &Interpreter::movArr,
    &Interpreter::movRri,  &Interpreter::movRrr,
    &Interpreter::movLli,  &Interpreter::unkArm,
    &Interpreter::movLri,  &Interpreter::strhPrrp,
    &Interpreter::movAri,  &Interpreter::ldrdPrrp,
    &Interpreter::movRri,  &Interpreter::strdPrrp,

    // 0x1B0-0x1BF: MOVS register group
    &Interpreter::movsLli, &Interpreter::movsLlr,
    &Interpreter::movsLri, &Interpreter::movsLrr,
    &Interpreter::movsAri, &Interpreter::movsArr,
    &Interpreter::movsRri, &Interpreter::movsRrr,
    &Interpreter::movsLli, &Interpreter::unkArm,
    &Interpreter::movsLri, &Interpreter::ldrhPrrp,
    &Interpreter::movsAri, &Interpreter::ldrsbPrrp,
    &Interpreter::movsRri, &Interpreter::ldrshPrrp,

    // 0x1C0-0x1CF: BIC register group + STRH/LDRD/STRD Ofip
    &Interpreter::bicLli,  &Interpreter::bicLlr,
    &Interpreter::bicLri,  &Interpreter::bicLrr,
    &Interpreter::bicAri,  &Interpreter::bicArr,
    &Interpreter::bicRri,  &Interpreter::bicRrr,
    &Interpreter::bicLli,  &Interpreter::unkArm,
    &Interpreter::bicLri,  &Interpreter::strhOfip,
    &Interpreter::bicAri,  &Interpreter::ldrdOfip,
    &Interpreter::bicRri,  &Interpreter::strdOfip,

    // 0x1D0-0x1DF: BICS register group
    &Interpreter::bicsLli, &Interpreter::bicsLlr,
    &Interpreter::bicsLri, &Interpreter::bicsLrr,
    &Interpreter::bicsAri, &Interpreter::bicsArr,
    &Interpreter::bicsRri, &Interpreter::bicsRrr,
    &Interpreter::bicsLli, &Interpreter::unkArm,
    &Interpreter::bicsLri, &Interpreter::ldrhOfip,
    &Interpreter::bicsAri, &Interpreter::ldrsbOfip,
    &Interpreter::bicsRri, &Interpreter::ldrshOfip,

    // 0x1E0-0x1EF: MVN register group + STRH/LDRD/STRD Prip
    &Interpreter::mvnLli,  &Interpreter::mvnLlr,
    &Interpreter::mvnLri,  &Interpreter::mvnLrr,
    &Interpreter::mvnAri,  &Interpreter::mvnArr,
    &Interpreter::mvnRri,  &Interpreter::mvnRrr,
    &Interpreter::mvnLli,  &Interpreter::unkArm,
    &Interpreter::mvnLri,  &Interpreter::strhPrip,
    &Interpreter::mvnAri,  &Interpreter::ldrdPrip,
    &Interpreter::mvnRri,  &Interpreter::strdPrip,

    // 0x1F0-0x1FF: MVNS register group
    &Interpreter::mvnsLli, &Interpreter::mvnsLlr,
    &Interpreter::mvnsLri, &Interpreter::mvnsLrr,
    &Interpreter::mvnsAri, &Interpreter::mvnsArr,
    &Interpreter::mvnsRri, &Interpreter::mvnsRrr,
    &Interpreter::mvnsLli, &Interpreter::unkArm,
    &Interpreter::mvnsLri, &Interpreter::ldrhPrip,
    &Interpreter::mvnsAri, &Interpreter::ldrsbPrip,
    &Interpreter::mvnsRri, &Interpreter::ldrshPrip,

    // ----------------------------------------------------------------
    // 0x200-0x3FF: Data processing immediate operand
    // Each 16-entry block maps to one instruction (all 16 rotation values)
    // These are contiguous and cache-friendly by design.
    // ----------------------------------------------------------------

    // 0x200-0x20F: AND #imm (16 entries, all same)
    &Interpreter::_andImm, &Interpreter::_andImm,
    &Interpreter::_andImm, &Interpreter::_andImm,
    &Interpreter::_andImm, &Interpreter::_andImm,
    &Interpreter::_andImm, &Interpreter::_andImm,
    &Interpreter::_andImm, &Interpreter::_andImm,
    &Interpreter::_andImm, &Interpreter::_andImm,
    &Interpreter::_andImm, &Interpreter::_andImm,
    &Interpreter::_andImm, &Interpreter::_andImm,

    // 0x210-0x21F: ANDS #imm
    &Interpreter::andsImm, &Interpreter::andsImm,
    &Interpreter::andsImm, &Interpreter::andsImm,
    &Interpreter::andsImm, &Interpreter::andsImm,
    &Interpreter::andsImm, &Interpreter::andsImm,
    &Interpreter::andsImm, &Interpreter::andsImm,
    &Interpreter::andsImm, &Interpreter::andsImm,
    &Interpreter::andsImm, &Interpreter::andsImm,
    &Interpreter::andsImm, &Interpreter::andsImm,

    // 0x220-0x22F: EOR #imm
    &Interpreter::eorImm,  &Interpreter::eorImm,
    &Interpreter::eorImm,  &Interpreter::eorImm,
    &Interpreter::eorImm,  &Interpreter::eorImm,
    &Interpreter::eorImm,  &Interpreter::eorImm,
    &Interpreter::eorImm,  &Interpreter::eorImm,
    &Interpreter::eorImm,  &Interpreter::eorImm,
    &Interpreter::eorImm,  &Interpreter::eorImm,
    &Interpreter::eorImm,  &Interpreter::eorImm,

    // 0x230-0x23F: EORS #imm
    &Interpreter::eorsImm, &Interpreter::eorsImm,
    &Interpreter::eorsImm, &Interpreter::eorsImm,
    &Interpreter::eorsImm, &Interpreter::eorsImm,
    &Interpreter::eorsImm, &Interpreter::eorsImm,
    &Interpreter::eorsImm, &Interpreter::eorsImm,
    &Interpreter::eorsImm, &Interpreter::eorsImm,
    &Interpreter::eorsImm, &Interpreter::eorsImm,
    &Interpreter::eorsImm, &Interpreter::eorsImm,

    // 0x240-0x24F: SUB #imm
    &Interpreter::subImm,  &Interpreter::subImm,
    &Interpreter::subImm,  &Interpreter::subImm,
    &Interpreter::subImm,  &Interpreter::subImm,
    &Interpreter::subImm,  &Interpreter::subImm,
    &Interpreter::subImm,  &Interpreter::subImm,
    &Interpreter::subImm,  &Interpreter::subImm,
    &Interpreter::subImm,  &Interpreter::subImm,
    &Interpreter::subImm,  &Interpreter::subImm,

    // 0x250-0x25F: SUBS #imm
    &Interpreter::subsImm, &Interpreter::subsImm,
    &Interpreter::subsImm, &Interpreter::subsImm,
    &Interpreter::subsImm, &Interpreter::subsImm,
    &Interpreter::subsImm, &Interpreter::subsImm,
    &Interpreter::subsImm, &Interpreter::subsImm,
    &Interpreter::subsImm, &Interpreter::subsImm,
    &Interpreter::subsImm, &Interpreter::subsImm,
    &Interpreter::subsImm, &Interpreter::subsImm,

    // 0x260-0x26F: RSB #imm
    &Interpreter::rsbImm,  &Interpreter::rsbImm,
    &Interpreter::rsbImm,  &Interpreter::rsbImm,
    &Interpreter::rsbImm,  &Interpreter::rsbImm,
    &Interpreter::rsbImm,  &Interpreter::rsbImm,
    &Interpreter::rsbImm,  &Interpreter::rsbImm,
    &Interpreter::rsbImm,  &Interpreter::rsbImm,
    &Interpreter::rsbImm,  &Interpreter::rsbImm,
    &Interpreter::rsbImm,  &Interpreter::rsbImm,

    // 0x270-0x27F: RSBS #imm
    &Interpreter::rsbsImm, &Interpreter::rsbsImm,
    &Interpreter::rsbsImm, &Interpreter::rsbsImm,
    &Interpreter::rsbsImm, &Interpreter::rsbsImm,
    &Interpreter::rsbsImm, &Interpreter::rsbsImm,
    &Interpreter::rsbsImm, &Interpreter::rsbsImm,
    &Interpreter::rsbsImm, &Interpreter::rsbsImm,
    &Interpreter::rsbsImm, &Interpreter::rsbsImm,
    &Interpreter::rsbsImm, &Interpreter::rsbsImm,

    // 0x280-0x28F: ADD #imm
    &Interpreter::addImm,  &Interpreter::addImm,
    &Interpreter::addImm,  &Interpreter::addImm,
    &Interpreter::addImm,  &Interpreter::addImm,
    &Interpreter::addImm,  &Interpreter::addImm,
    &Interpreter::addImm,  &Interpreter::addImm,
    &Interpreter::addImm,  &Interpreter::addImm,
    &Interpreter::addImm,  &Interpreter::addImm,
    &Interpreter::addImm,  &Interpreter::addImm,

    // 0x290-0x29F: ADDS #imm
    &Interpreter::addsImm, &Interpreter::addsImm,
    &Interpreter::addsImm, &Interpreter::addsImm,
    &Interpreter::addsImm, &Interpreter::addsImm,
    &Interpreter::addsImm, &Interpreter::addsImm,
    &Interpreter::addsImm, &Interpreter::addsImm,
    &Interpreter::addsImm, &Interpreter::addsImm,
    &Interpreter::addsImm, &Interpreter::addsImm,
    &Interpreter::addsImm, &Interpreter::addsImm,

    // 0x2A0-0x2AF: ADC #imm
    &Interpreter::adcImm,  &Interpreter::adcImm,
    &Interpreter::adcImm,  &Interpreter::adcImm,
    &Interpreter::adcImm,  &Interpreter::adcImm,
    &Interpreter::adcImm,  &Interpreter::adcImm,
    &Interpreter::adcImm,  &Interpreter::adcImm,
    &Interpreter::adcImm,  &Interpreter::adcImm,
    &Interpreter::adcImm,  &Interpreter::adcImm,
    &Interpreter::adcImm,  &Interpreter::adcImm,

    // 0x2B0-0x2BF: ADCS #imm
    &Interpreter::adcsImm, &Interpreter::adcsImm,
    &Interpreter::adcsImm, &Interpreter::adcsImm,
    &Interpreter::adcsImm, &Interpreter::adcsImm,
    &Interpreter::adcsImm, &Interpreter::adcsImm,
    &Interpreter::adcsImm, &Interpreter::adcsImm,
    &Interpreter::adcsImm, &Interpreter::adcsImm,
    &Interpreter::adcsImm, &Interpreter::adcsImm,
    &Interpreter::adcsImm, &Interpreter::adcsImm,

    // 0x2C0-0x2CF: SBC #imm
    &Interpreter::sbcImm,  &Interpreter::sbcImm,
    &Interpreter::sbcImm,  &Interpreter::sbcImm,
    &Interpreter::sbcImm,  &Interpreter::sbcImm,
    &Interpreter::sbcImm,  &Interpreter::sbcImm,
    &Interpreter::sbcImm,  &Interpreter::sbcImm,
    &Interpreter::sbcImm,  &Interpreter::sbcImm,
    &Interpreter::sbcImm,  &Interpreter::sbcImm,
    &Interpreter::sbcImm,  &Interpreter::sbcImm,

    // 0x2D0-0x2DF: SBCS #imm
    &Interpreter::sbcsImm, &Interpreter::sbcsImm,
    &Interpreter::sbcsImm, &Interpreter::sbcsImm,
    &Interpreter::sbcsImm, &Interpreter::sbcsImm,
    &Interpreter::sbcsImm, &Interpreter::sbcsImm,
    &Interpreter::sbcsImm, &Interpreter::sbcsImm,
    &Interpreter::sbcsImm, &Interpreter::sbcsImm,
    &Interpreter::sbcsImm, &Interpreter::sbcsImm,
    &Interpreter::sbcsImm, &Interpreter::sbcsImm,

    // 0x2E0-0x2EF: RSC #imm
    &Interpreter::rscImm,  &Interpreter::rscImm,
    &Interpreter::rscImm,  &Interpreter::rscImm,
    &Interpreter::rscImm,  &Interpreter::rscImm,
    &Interpreter::rscImm,  &Interpreter::rscImm,
    &Interpreter::rscImm,  &Interpreter::rscImm,
    &Interpreter::rscImm,  &Interpreter::rscImm,
    &Interpreter::rscImm,  &Interpreter::rscImm,
    &Interpreter::rscImm,  &Interpreter::rscImm,

    // 0x2F0-0x2FF: RSCS #imm
    &Interpreter::rscsImm, &Interpreter::rscsImm,
    &Interpreter::rscsImm, &Interpreter::rscsImm,
    &Interpreter::rscsImm, &Interpreter::rscsImm,
    &Interpreter::rscsImm, &Interpreter::rscsImm,
    &Interpreter::rscsImm, &Interpreter::rscsImm,
    &Interpreter::rscsImm, &Interpreter::rscsImm,
    &Interpreter::rscsImm, &Interpreter::rscsImm,
    &Interpreter::rscsImm, &Interpreter::rscsImm,

    // 0x300-0x30F: undefined (MSR/MRS immediate forms reserved here)
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,

    // 0x310-0x31F: TST #imm
    &Interpreter::tstImm,  &Interpreter::tstImm,
    &Interpreter::tstImm,  &Interpreter::tstImm,
    &Interpreter::tstImm,  &Interpreter::tstImm,
    &Interpreter::tstImm,  &Interpreter::tstImm,
    &Interpreter::tstImm,  &Interpreter::tstImm,
    &Interpreter::tstImm,  &Interpreter::tstImm,
    &Interpreter::tstImm,  &Interpreter::tstImm,
    &Interpreter::tstImm,  &Interpreter::tstImm,

    // 0x320-0x32F: MSR CPSR, #imm
    &Interpreter::msrIc,   &Interpreter::msrIc,
    &Interpreter::msrIc,   &Interpreter::msrIc,
    &Interpreter::msrIc,   &Interpreter::msrIc,
    &Interpreter::msrIc,   &Interpreter::msrIc,
    &Interpreter::msrIc,   &Interpreter::msrIc,
    &Interpreter::msrIc,   &Interpreter::msrIc,
    &Interpreter::msrIc,   &Interpreter::msrIc,
    &Interpreter::msrIc,   &Interpreter::msrIc,

    // 0x330-0x33F: TEQ #imm
    &Interpreter::teqImm,  &Interpreter::teqImm,
    &Interpreter::teqImm,  &Interpreter::teqImm,
    &Interpreter::teqImm,  &Interpreter::teqImm,
    &Interpreter::teqImm,  &Interpreter::teqImm,
    &Interpreter::teqImm,  &Interpreter::teqImm,
    &Interpreter::teqImm,  &Interpreter::teqImm,
    &Interpreter::teqImm,  &Interpreter::teqImm,
    &Interpreter::teqImm,  &Interpreter::teqImm,

    // 0x340-0x34F: undefined
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,

    // 0x350-0x35F: CMP #imm
    &Interpreter::cmpImm,  &Interpreter::cmpImm,
    &Interpreter::cmpImm,  &Interpreter::cmpImm,
    &Interpreter::cmpImm,  &Interpreter::cmpImm,
    &Interpreter::cmpImm,  &Interpreter::cmpImm,
    &Interpreter::cmpImm,  &Interpreter::cmpImm,
    &Interpreter::cmpImm,  &Interpreter::cmpImm,
    &Interpreter::cmpImm,  &Interpreter::cmpImm,
    &Interpreter::cmpImm,  &Interpreter::cmpImm,

    // 0x360-0x36F: MSR SPSR, #imm
    &Interpreter::msrIs,   &Interpreter::msrIs,
    &Interpreter::msrIs,   &Interpreter::msrIs,
    &Interpreter::msrIs,   &Interpreter::msrIs,
    &Interpreter::msrIs,   &Interpreter::msrIs,
    &Interpreter::msrIs,   &Interpreter::msrIs,
    &Interpreter::msrIs,   &Interpreter::msrIs,
    &Interpreter::msrIs,   &Interpreter::msrIs,
    &Interpreter::msrIs,   &Interpreter::msrIs,

    // 0x370-0x37F: CMN #imm
    &Interpreter::cmnImm,  &Interpreter::cmnImm,
    &Interpreter::cmnImm,  &Interpreter::cmnImm,
    &Interpreter::cmnImm,  &Interpreter::cmnImm,
    &Interpreter::cmnImm,  &Interpreter::cmnImm,
    &Interpreter::cmnImm,  &Interpreter::cmnImm,
    &Interpreter::cmnImm,  &Interpreter::cmnImm,
    &Interpreter::cmnImm,  &Interpreter::cmnImm,
    &Interpreter::cmnImm,  &Interpreter::cmnImm,

    // 0x380-0x38F: ORR #imm
    &Interpreter::orrImm,  &Interpreter::orrImm,
    &Interpreter::orrImm,  &Interpreter::orrImm,
    &Interpreter::orrImm,  &Interpreter::orrImm,
    &Interpreter::orrImm,  &Interpreter::orrImm,
    &Interpreter::orrImm,  &Interpreter::orrImm,
    &Interpreter::orrImm,  &Interpreter::orrImm,
    &Interpreter::orrImm,  &Interpreter::orrImm,
    &Interpreter::orrImm,  &Interpreter::orrImm,

    // 0x390-0x39F: ORRS #imm
    &Interpreter::orrsImm, &Interpreter::orrsImm,
    &Interpreter::orrsImm, &Interpreter::orrsImm,
    &Interpreter::orrsImm, &Interpreter::orrsImm,
    &Interpreter::orrsImm, &Interpreter::orrsImm,
    &Interpreter::orrsImm, &Interpreter::orrsImm,
    &Interpreter::orrsImm, &Interpreter::orrsImm,
    &Interpreter::orrsImm, &Interpreter::orrsImm,
    &Interpreter::orrsImm, &Interpreter::orrsImm,

    // 0x3A0-0x3AF: MOV #imm
    &Interpreter::movImm,  &Interpreter::movImm,
    &Interpreter::movImm,  &Interpreter::movImm,
    &Interpreter::movImm,  &Interpreter::movImm,
    &Interpreter::movImm,  &Interpreter::movImm,
    &Interpreter::movImm,  &Interpreter::movImm,
    &Interpreter::movImm,  &Interpreter::movImm,
    &Interpreter::movImm,  &Interpreter::movImm,
    &Interpreter::movImm,  &Interpreter::movImm,

    // 0x3B0-0x3BF: MOVS #imm
    &Interpreter::movsImm, &Interpreter::movsImm,
    &Interpreter::movsImm, &Interpreter::movsImm,
    &Interpreter::movsImm, &Interpreter::movsImm,
    &Interpreter::movsImm, &Interpreter::movsImm,
    &Interpreter::movsImm, &Interpreter::movsImm,
    &Interpreter::movsImm, &Interpreter::movsImm,
    &Interpreter::movsImm, &Interpreter::movsImm,
    &Interpreter::movsImm, &Interpreter::movsImm,

    // 0x3C0-0x3CF: BIC #imm
    &Interpreter::bicImm,  &Interpreter::bicImm,
    &Interpreter::bicImm,  &Interpreter::bicImm,
    &Interpreter::bicImm,  &Interpreter::bicImm,
    &Interpreter::bicImm,  &Interpreter::bicImm,
    &Interpreter::bicImm,  &Interpreter::bicImm,
    &Interpreter::bicImm,  &Interpreter::bicImm,
    &Interpreter::bicImm,  &Interpreter::bicImm,
    &Interpreter::bicImm,  &Interpreter::bicImm,

    // 0x3D0-0x3DF: BICS #imm
    &Interpreter::bicsImm, &Interpreter::bicsImm,
    &Interpreter::bicsImm, &Interpreter::bicsImm,
    &Interpreter::bicsImm, &Interpreter::bicsImm,
    &Interpreter::bicsImm, &Interpreter::bicsImm,
    &Interpreter::bicsImm, &Interpreter::bicsImm,
    &Interpreter::bicsImm, &Interpreter::bicsImm,
    &Interpreter::bicsImm, &Interpreter::bicsImm,
    &Interpreter::bicsImm, &Interpreter::bicsImm,

    // 0x3E0-0x3EF: MVN #imm
    &Interpreter::mvnImm,  &Interpreter::mvnImm,
    &Interpreter::mvnImm,  &Interpreter::mvnImm,
    &Interpreter::mvnImm,  &Interpreter::mvnImm,
    &Interpreter::mvnImm,  &Interpreter::mvnImm,
    &Interpreter::mvnImm,  &Interpreter::mvnImm,
    &Interpreter::mvnImm,  &Interpreter::mvnImm,
    &Interpreter::mvnImm,  &Interpreter::mvnImm,
    &Interpreter::mvnImm,  &Interpreter::mvnImm,

    // 0x3F0-0x3FF: MVNS #imm
    &Interpreter::mvnsImm, &Interpreter::mvnsImm,
    &Interpreter::mvnsImm, &Interpreter::mvnsImm,
    &Interpreter::mvnsImm, &Interpreter::mvnsImm,
    &Interpreter::mvnsImm, &Interpreter::mvnsImm,
    &Interpreter::mvnsImm, &Interpreter::mvnsImm,
    &Interpreter::mvnsImm, &Interpreter::mvnsImm,
    &Interpreter::mvnsImm, &Interpreter::mvnsImm,
    &Interpreter::mvnsImm, &Interpreter::mvnsImm,

    // ----------------------------------------------------------------
    // 0x400-0x4FF: LDR/STR immediate offset - post-indexed
    // ----------------------------------------------------------------

    // 0x400-0x40F: STR Rd,[Rn],-#imm (post-decrement immediate)
    &Interpreter::strPtim,  &Interpreter::strPtim,
    &Interpreter::strPtim,  &Interpreter::strPtim,
    &Interpreter::strPtim,  &Interpreter::strPtim,
    &Interpreter::strPtim,  &Interpreter::strPtim,
    &Interpreter::strPtim,  &Interpreter::strPtim,
    &Interpreter::strPtim,  &Interpreter::strPtim,
    &Interpreter::strPtim,  &Interpreter::strPtim,
    &Interpreter::strPtim,  &Interpreter::strPtim,

    // 0x410-0x41F: LDR Rd,[Rn],-#imm
    &Interpreter::ldrPtim,  &Interpreter::ldrPtim,
    &Interpreter::ldrPtim,  &Interpreter::ldrPtim,
    &Interpreter::ldrPtim,  &Interpreter::ldrPtim,
    &Interpreter::ldrPtim,  &Interpreter::ldrPtim,
    &Interpreter::ldrPtim,  &Interpreter::ldrPtim,
    &Interpreter::ldrPtim,  &Interpreter::ldrPtim,
    &Interpreter::ldrPtim,  &Interpreter::ldrPtim,
    &Interpreter::ldrPtim,  &Interpreter::ldrPtim,

    // 0x420-0x43F: undefined
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,

    // 0x440-0x44F: STRB Rd,[Rn],-#imm
    &Interpreter::strbPtim, &Interpreter::strbPtim,
    &Interpreter::strbPtim, &Interpreter::strbPtim,
    &Interpreter::strbPtim, &Interpreter::strbPtim,
    &Interpreter::strbPtim, &Interpreter::strbPtim,
    &Interpreter::strbPtim, &Interpreter::strbPtim,
    &Interpreter::strbPtim, &Interpreter::strbPtim,
    &Interpreter::strbPtim, &Interpreter::strbPtim,
    &Interpreter::strbPtim, &Interpreter::strbPtim,

    // 0x450-0x45F: LDRB Rd,[Rn],-#imm
    &Interpreter::ldrbPtim, &Interpreter::ldrbPtim,
    &Interpreter::ldrbPtim, &Interpreter::ldrbPtim,
    &Interpreter::ldrbPtim, &Interpreter::ldrbPtim,
    &Interpreter::ldrbPtim, &Interpreter::ldrbPtim,
    &Interpreter::ldrbPtim, &Interpreter::ldrbPtim,
    &Interpreter::ldrbPtim, &Interpreter::ldrbPtim,
    &Interpreter::ldrbPtim, &Interpreter::ldrbPtim,
    &Interpreter::ldrbPtim, &Interpreter::ldrbPtim,

    // 0x460-0x47F: undefined
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,

    // 0x480-0x48F: STR Rd,[Rn],+#imm
    &Interpreter::strPtip,  &Interpreter::strPtip,
    &Interpreter::strPtip,  &Interpreter::strPtip,
    &Interpreter::strPtip,  &Interpreter::strPtip,
    &Interpreter::strPtip,  &Interpreter::strPtip,
    &Interpreter::strPtip,  &Interpreter::strPtip,
    &Interpreter::strPtip,  &Interpreter::strPtip,
    &Interpreter::strPtip,  &Interpreter::strPtip,
    &Interpreter::strPtip,  &Interpreter::strPtip,

    // 0x490-0x49F: LDR Rd,[Rn],+#imm
    &Interpreter::ldrPtip,  &Interpreter::ldrPtip,
    &Interpreter::ldrPtip,  &Interpreter::ldrPtip,
    &Interpreter::ldrPtip,  &Interpreter::ldrPtip,
    &Interpreter::ldrPtip,  &Interpreter::ldrPtip,
    &Interpreter::ldrPtip,  &Interpreter::ldrPtip,
    &Interpreter::ldrPtip,  &Interpreter::ldrPtip,
    &Interpreter::ldrPtip,  &Interpreter::ldrPtip,
    &Interpreter::ldrPtip,  &Interpreter::ldrPtip,

    // 0x4A0-0x4BF: undefined
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,

    // 0x4C0-0x4CF: STRB Rd,[Rn],+#imm
    &Interpreter::strbPtip, &Interpreter::strbPtip,
    &Interpreter::strbPtip, &Interpreter::strbPtip,
    &Interpreter::strbPtip, &Interpreter::strbPtip,
    &Interpreter::strbPtip, &Interpreter::strbPtip,
    &Interpreter::strbPtip, &Interpreter::strbPtip,
    &Interpreter::strbPtip, &Interpreter::strbPtip,
    &Interpreter::strbPtip, &Interpreter::strbPtip,
    &Interpreter::strbPtip, &Interpreter::strbPtip,

    // 0x4D0-0x4DF: LDRB Rd,[Rn],+#imm
    &Interpreter::ldrbPtip, &Interpreter::ldrbPtip,
    &Interpreter::ldrbPtip, &Interpreter::ldrbPtip,
    &Interpreter::ldrbPtip, &Interpreter::ldrbPtip,
    &Interpreter::ldrbPtip, &Interpreter::ldrbPtip,
    &Interpreter::ldrbPtip, &Interpreter::ldrbPtip,
    &Interpreter::ldrbPtip, &Interpreter::ldrbPtip,
    &Interpreter::ldrbPtip, &Interpreter::ldrbPtip,
    &Interpreter::ldrbPtip, &Interpreter::ldrbPtip,

    // 0x4E0-0x4FF: undefined
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,

    // ----------------------------------------------------------------
    // 0x500-0x5FF: LDR/STR immediate offset - pre-indexed
    // ----------------------------------------------------------------

    // 0x500-0x50F: STR Rd,[Rn-#imm]
    &Interpreter::strOfim,  &Interpreter::strOfim,
    &Interpreter::strOfim,  &Interpreter::strOfim,
    &Interpreter::strOfim,  &Interpreter::strOfim,
    &Interpreter::strOfim,  &Interpreter::strOfim,
    &Interpreter::strOfim,  &Interpreter::strOfim,
    &Interpreter::strOfim,  &Interpreter::strOfim,
    &Interpreter::strOfim,  &Interpreter::strOfim,
    &Interpreter::strOfim,  &Interpreter::strOfim,

    // 0x510-0x51F: LDR Rd,[Rn-#imm]
    &Interpreter::ldrOfim,  &Interpreter::ldrOfim,
    &Interpreter::ldrOfim,  &Interpreter::ldrOfim,
    &Interpreter::ldrOfim,  &Interpreter::ldrOfim,
    &Interpreter::ldrOfim,  &Interpreter::ldrOfim,
    &Interpreter::ldrOfim,  &Interpreter::ldrOfim,
    &Interpreter::ldrOfim,  &Interpreter::ldrOfim,
    &Interpreter::ldrOfim,  &Interpreter::ldrOfim,
    &Interpreter::ldrOfim,  &Interpreter::ldrOfim,

    // 0x520-0x52F: STR Rd,[Rn-#imm]! (pre-decrement with writeback)
    &Interpreter::strPrim,  &Interpreter::strPrim,
    &Interpreter::strPrim,  &Interpreter::strPrim,
    &Interpreter::strPrim,  &Interpreter::strPrim,
    &Interpreter::strPrim,  &Interpreter::strPrim,
    &Interpreter::strPrim,  &Interpreter::strPrim,
    &Interpreter::strPrim,  &Interpreter::strPrim,
    &Interpreter::strPrim,  &Interpreter::strPrim,
    &Interpreter::strPrim,  &Interpreter::strPrim,

    // 0x530-0x53F: LDR Rd,[Rn-#imm]!
    &Interpreter::ldrPrim,  &Interpreter::ldrPrim,
    &Interpreter::ldrPrim,  &Interpreter::ldrPrim,
    &Interpreter::ldrPrim,  &Interpreter::ldrPrim,
    &Interpreter::ldrPrim,  &Interpreter::ldrPrim,
    &Interpreter::ldrPrim,  &Interpreter::ldrPrim,
    &Interpreter::ldrPrim,  &Interpreter::ldrPrim,
    &Interpreter::ldrPrim,  &Interpreter::ldrPrim,
    &Interpreter::ldrPrim,  &Interpreter::ldrPrim,

    // 0x540-0x54F: STRB Rd,[Rn-#imm]
    &Interpreter::strbOfim, &Interpreter::strbOfim,
    &Interpreter::strbOfim, &Interpreter::strbOfim,
    &Interpreter::strbOfim, &Interpreter::strbOfim,
    &Interpreter::strbOfim, &Interpreter::strbOfim,
    &Interpreter::strbOfim, &Interpreter::strbOfim,
    &Interpreter::strbOfim, &Interpreter::strbOfim,
    &Interpreter::strbOfim, &Interpreter::strbOfim,
    &Interpreter::strbOfim, &Interpreter::strbOfim,

    // 0x550-0x55F: LDRB Rd,[Rn-#imm]
    &Interpreter::ldrbOfim, &Interpreter::ldrbOfim,
    &Interpreter::ldrbOfim, &Interpreter::ldrbOfim,
    &Interpreter::ldrbOfim, &Interpreter::ldrbOfim,
    &Interpreter::ldrbOfim, &Interpreter::ldrbOfim,
    &Interpreter::ldrbOfim, &Interpreter::ldrbOfim,
    &Interpreter::ldrbOfim, &Interpreter::ldrbOfim,
    &Interpreter::ldrbOfim, &Interpreter::ldrbOfim,
    &Interpreter::ldrbOfim, &Interpreter::ldrbOfim,

    // 0x560-0x56F: STRB Rd,[Rn-#imm]!
    &Interpreter::strbPrim, &Interpreter::strbPrim,
    &Interpreter::strbPrim, &Interpreter::strbPrim,
    &Interpreter::strbPrim, &Interpreter::strbPrim,
    &Interpreter::strbPrim, &Interpreter::strbPrim,
    &Interpreter::strbPrim, &Interpreter::strbPrim,
    &Interpreter::strbPrim, &Interpreter::strbPrim,
    &Interpreter::strbPrim, &Interpreter::strbPrim,
    &Interpreter::strbPrim, &Interpreter::strbPrim,

    // 0x570-0x57F: LDRB Rd,[Rn-#imm]!
    &Interpreter::ldrbPrim, &Interpreter::ldrbPrim,
    &Interpreter::ldrbPrim, &Interpreter::ldrbPrim,
    &Interpreter::ldrbPrim, &Interpreter::ldrbPrim,
    &Interpreter::ldrbPrim, &Interpreter::ldrbPrim,
    &Interpreter::ldrbPrim, &Interpreter::ldrbPrim,
    &Interpreter::ldrbPrim, &Interpreter::ldrbPrim,
    &Interpreter::ldrbPrim, &Interpreter::ldrbPrim,
    &Interpreter::ldrbPrim, &Interpreter::ldrbPrim,

    // 0x580-0x58F: STR Rd,[Rn+#imm]
    &Interpreter::strOfip,  &Interpreter::strOfip,
    &Interpreter::strOfip,  &Interpreter::strOfip,
    &Interpreter::strOfip,  &Interpreter::strOfip,
    &Interpreter::strOfip,  &Interpreter::strOfip,
    &Interpreter::strOfip,  &Interpreter::strOfip,
    &Interpreter::strOfip,  &Interpreter::strOfip,
    &Interpreter::strOfip,  &Interpreter::strOfip,
    &Interpreter::strOfip,  &Interpreter::strOfip,

    // 0x590-0x59F: LDR Rd,[Rn+#imm]
    &Interpreter::ldrOfip,  &Interpreter::ldrOfip,
    &Interpreter::ldrOfip,  &Interpreter::ldrOfip,
    &Interpreter::ldrOfip,  &Interpreter::ldrOfip,
    &Interpreter::ldrOfip,  &Interpreter::ldrOfip,
    &Interpreter::ldrOfip,  &Interpreter::ldrOfip,
    &Interpreter::ldrOfip,  &Interpreter::ldrOfip,
    &Interpreter::ldrOfip,  &Interpreter::ldrOfip,
    &Interpreter::ldrOfip,  &Interpreter::ldrOfip,

    // 0x5A0-0x5AF: STR Rd,[Rn+#imm]!
    &Interpreter::strPrip,  &Interpreter::strPrip,
    &Interpreter::strPrip,  &Interpreter::strPrip,
    &Interpreter::strPrip,  &Interpreter::strPrip,
    &Interpreter::strPrip,  &Interpreter::strPrip,
    &Interpreter::strPrip,  &Interpreter::strPrip,
    &Interpreter::strPrip,  &Interpreter::strPrip,
    &Interpreter::strPrip,  &Interpreter::strPrip,
    &Interpreter::strPrip,  &Interpreter::strPrip,

    // 0x5B0-0x5BF: LDR Rd,[Rn+#imm]!
    &Interpreter::ldrPrip,  &Interpreter::ldrPrip,
    &Interpreter::ldrPrip,  &Interpreter::ldrPrip,
    &Interpreter::ldrPrip,  &Interpreter::ldrPrip,
    &Interpreter::ldrPrip,  &Interpreter::ldrPrip,
    &Interpreter::ldrPrip,  &Interpreter::ldrPrip,
    &Interpreter::ldrPrip,  &Interpreter::ldrPrip,
    &Interpreter::ldrPrip,  &Interpreter::ldrPrip,
    &Interpreter::ldrPrip,  &Interpreter::ldrPrip,

    // 0x5C0-0x5CF: STRB Rd,[Rn+#imm]
    &Interpreter::strbOfip, &Interpreter::strbOfip,
    &Interpreter::strbOfip, &Interpreter::strbOfip,
    &Interpreter::strbOfip, &Interpreter::strbOfip,
    &Interpreter::strbOfip, &Interpreter::strbOfip,
    &Interpreter::strbOfip, &Interpreter::strbOfip,
    &Interpreter::strbOfip, &Interpreter::strbOfip,
    &Interpreter::strbOfip, &Interpreter::strbOfip,
    &Interpreter::strbOfip, &Interpreter::strbOfip,

    // 0x5D0-0x5DF: LDRB Rd,[Rn+#imm]
    &Interpreter::ldrbOfip, &Interpreter::ldrbOfip,
    &Interpreter::ldrbOfip, &Interpreter::ldrbOfip,
    &Interpreter::ldrbOfip, &Interpreter::ldrbOfip,
    &Interpreter::ldrbOfip, &Interpreter::ldrbOfip,
    &Interpreter::ldrbOfip, &Interpreter::ldrbOfip,
    &Interpreter::ldrbOfip, &Interpreter::ldrbOfip,
    &Interpreter::ldrbOfip, &Interpreter::ldrbOfip,
    &Interpreter::ldrbOfip, &Interpreter::ldrbOfip,

    // 0x5E0-0x5EF: STRB Rd,[Rn+#imm]!
    &Interpreter::strbPrip, &Interpreter::strbPrip,
    &Interpreter::strbPrip, &Interpreter::strbPrip,
    &Interpreter::strbPrip, &Interpreter::strbPrip,
    &Interpreter::strbPrip, &Interpreter::strbPrip,
    &Interpreter::strbPrip, &Interpreter::strbPrip,
    &Interpreter::strbPrip, &Interpreter::strbPrip,
    &Interpreter::strbPrip, &Interpreter::strbPrip,
    &Interpreter::strbPrip, &Interpreter::strbPrip,

    // 0x5F0-0x5FF: LDRB Rd,[Rn+#imm]!
    &Interpreter::ldrbPrip, &Interpreter::ldrbPrip,
    &Interpreter::ldrbPrip, &Interpreter::ldrbPrip,
    &Interpreter::ldrbPrip, &Interpreter::ldrbPrip,
    &Interpreter::ldrbPrip, &Interpreter::ldrbPrip,
    &Interpreter::ldrbPrip, &Interpreter::ldrbPrip,
    &Interpreter::ldrbPrip, &Interpreter::ldrbPrip,
    &Interpreter::ldrbPrip, &Interpreter::ldrbPrip,
    &Interpreter::ldrbPrip, &Interpreter::ldrbPrip,

    // ----------------------------------------------------------------
    // 0x600-0x6FF: LDR/STR register offset - post-indexed
    // Odd indices are undefined (bit 4 set in register-shifted operands
    // is illegal in this encoding space)
    // ----------------------------------------------------------------

    // 0x600-0x60F: STR Rd,[Rn],-Rm,LSL/LSR/ASR/ROR #i (post-decrement reg)
    &Interpreter::strPtrmll, &Interpreter::unkArm,
    &Interpreter::strPtrmlr, &Interpreter::unkArm,
    &Interpreter::strPtrmar, &Interpreter::unkArm,
    &Interpreter::strPtrmrr, &Interpreter::unkArm,
    &Interpreter::strPtrmll, &Interpreter::unkArm,
    &Interpreter::strPtrmlr, &Interpreter::unkArm,
    &Interpreter::strPtrmar, &Interpreter::unkArm,
    &Interpreter::strPtrmrr, &Interpreter::unkArm,

    // 0x610-0x61F: LDR Rd,[Rn],-Rm,shift (post-decrement reg)
    &Interpreter::ldrPtrmll, &Interpreter::unkArm,
    &Interpreter::ldrPtrmlr, &Interpreter::unkArm,
    &Interpreter::ldrPtrmar, &Interpreter::unkArm,
    &Interpreter::ldrPtrmrr, &Interpreter::unkArm,
    &Interpreter::ldrPtrmll, &Interpreter::unkArm,
    &Interpreter::ldrPtrmlr, &Interpreter::unkArm,
    &Interpreter::ldrPtrmar, &Interpreter::unkArm,
    &Interpreter::ldrPtrmrr, &Interpreter::unkArm,

    // 0x620-0x63F: undefined
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,

    // 0x640-0x64F: STRB Rd,[Rn],-Rm,shift
    &Interpreter::strbPtrmll, &Interpreter::unkArm,
    &Interpreter::strbPtrmlr, &Interpreter::unkArm,
    &Interpreter::strbPtrmar, &Interpreter::unkArm,
    &Interpreter::strbPtrmrr, &Interpreter::unkArm,
    &Interpreter::strbPtrmll, &Interpreter::unkArm,
    &Interpreter::strbPtrmlr, &Interpreter::unkArm,
    &Interpreter::strbPtrmar, &Interpreter::unkArm,
    &Interpreter::strbPtrmrr, &Interpreter::unkArm,

    // 0x650-0x65F: LDRB Rd,[Rn],-Rm,shift
    &Interpreter::ldrbPtrmll, &Interpreter::unkArm,
    &Interpreter::ldrbPtrmlr, &Interpreter::unkArm,
    &Interpreter::ldrbPtrmar, &Interpreter::unkArm,
    &Interpreter::ldrbPtrmrr, &Interpreter::unkArm,
    &Interpreter::ldrbPtrmll, &Interpreter::unkArm,
    &Interpreter::ldrbPtrmlr, &Interpreter::unkArm,
    &Interpreter::ldrbPtrmar, &Interpreter::unkArm,
    &Interpreter::ldrbPtrmrr, &Interpreter::unkArm,

    // 0x660-0x67F: undefined
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,

    // 0x680-0x68F: STR Rd,[Rn],+Rm,shift
    &Interpreter::strPtrpll, &Interpreter::unkArm,
    &Interpreter::strPtrplr, &Interpreter::unkArm,
    &Interpreter::strPtrpar, &Interpreter::unkArm,
    &Interpreter::strPtrprr, &Interpreter::unkArm,
    &Interpreter::strPtrpll, &Interpreter::unkArm,
    &Interpreter::strPtrplr, &Interpreter::unkArm,
    &Interpreter::strPtrpar, &Interpreter::unkArm,
    &Interpreter::strPtrprr, &Interpreter::unkArm,

    // 0x690-0x69F: LDR Rd,[Rn],+Rm,shift
    &Interpreter::ldrPtrpll, &Interpreter::unkArm,
    &Interpreter::ldrPtrplr, &Interpreter::unkArm,
    &Interpreter::ldrPtrpar, &Interpreter::unkArm,
    &Interpreter::ldrPtrprr, &Interpreter::unkArm,
    &Interpreter::ldrPtrpll, &Interpreter::unkArm,
    &Interpreter::ldrPtrplr, &Interpreter::unkArm,
    &Interpreter::ldrPtrpar, &Interpreter::unkArm,
    &Interpreter::ldrPtrprr, &Interpreter::unkArm,

    // 0x6A0-0x6BF: undefined
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,

    // 0x6C0-0x6CF: STRB Rd,[Rn],+Rm,shift
    &Interpreter::strbPtrpll, &Interpreter::unkArm,
    &Interpreter::strbPtrplr, &Interpreter::unkArm,
    &Interpreter::strbPtrpar, &Interpreter::unkArm,
    &Interpreter::strbPtrprr, &Interpreter::unkArm,
    &Interpreter::strbPtrpll, &Interpreter::unkArm,
    &Interpreter::strbPtrplr, &Interpreter::unkArm,
    &Interpreter::strbPtrpar, &Interpreter::unkArm,
    &Interpreter::strbPtrprr, &Interpreter::unkArm,

    // 0x6D0-0x6DF: LDRB Rd,[Rn],+Rm,shift
    &Interpreter::ldrbPtrpll, &Interpreter::unkArm,
    &Interpreter::ldrbPtrplr, &Interpreter::unkArm,
    &Interpreter::ldrbPtrpar, &Interpreter::unkArm,
    &Interpreter::ldrbPtrprr, &Interpreter::unkArm,
    &Interpreter::ldrbPtrpll, &Interpreter::unkArm,
    &Interpreter::ldrbPtrplr, &Interpreter::unkArm,
    &Interpreter::ldrbPtrpar, &Interpreter::unkArm,
    &Interpreter::ldrbPtrprr, &Interpreter::unkArm,

    // 0x6E0-0x6FF: undefined
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,
    &Interpreter::unkArm,  &Interpreter::unkArm,

    // ----------------------------------------------------------------
    // 0x700-0x7FF: LDR/STR register offset - pre-indexed
    // ----------------------------------------------------------------

    // 0x700-0x70F: STR Rd,[Rn-Rm,shift]
    &Interpreter::strOfrmll, &Interpreter::unkArm,
    &Interpreter::strOfrmlr, &Interpreter::unkArm,
    &Interpreter::strOfrmar, &Interpreter::unkArm,
    &Interpreter::strOfrmrr, &Interpreter::unkArm,
    &Interpreter::strOfrmll, &Interpreter::unkArm,
    &Interpreter::strOfrmlr, &Interpreter::unkArm,
    &Interpreter::strOfrmar, &Interpreter::unkArm,
    &Interpreter::strOfrmrr, &Interpreter::unkArm,

    // 0x710-0x71F: LDR Rd,[Rn-Rm,shift]
    &Interpreter::ldrOfrmll, &Interpreter::unkArm,
    &Interpreter::ldrOfrmlr, &Interpreter::unkArm,
    &Interpreter::ldrOfrmar, &Interpreter::unkArm,
    &Interpreter::ldrOfrmrr, &Interpreter::unkArm,
    &Interpreter::ldrOfrmll, &Interpreter::unkArm,
    &Interpreter::ldrOfrmlr, &Interpreter::unkArm,
    &Interpreter::ldrOfrmar, &Interpreter::unkArm,
    &Interpreter::ldrOfrmrr, &Interpreter::unkArm,

    // 0x720-0x72F: STR Rd,[Rn-Rm,shift]! (pre-decrement with writeback)
    &Interpreter::strPrrmll, &Interpreter::unkArm,
    &Interpreter::strPrrmlr, &Interpreter::unkArm,
    &Interpreter::strPrrmar, &Interpreter::unkArm,
    &Interpreter::strPrrmrr, &Interpreter::unkArm,
    &Interpreter::strPrrmll, &Interpreter::unkArm,
    &Interpreter::strPrrmlr, &Interpreter::unkArm,
    &Interpreter::strPrrmar, &Interpreter::unkArm,
    &Interpreter::strPrrmrr, &Interpreter::unkArm,

    // 0x730-0x73F: LDR Rd,[Rn-Rm,shift]!
    &Interpreter::ldrPrrmll, &Interpreter::unkArm,
    &Interpreter::ldrPrrmlr, &Interpreter::unkArm,
    &Interpreter::ldrPrrmar, &Interpreter::unkArm,
    &Interpreter::ldrPrrmrr, &Interpreter::unkArm,
    &Interpreter::ldrPrrmll, &Interpreter::unkArm,
    &Interpreter::ldrPrrmlr, &Interpreter::unkArm,
    &Interpreter::ldrPrrmar, &Interpreter::unkArm,
    &Interpreter::ldrPrrmrr, &Interpreter::unkArm,

    // 0x740-0x74F: STRB Rd,[Rn-Rm,shift]
    &Interpreter::strbOfrmll, &Interpreter::unkArm,
    &Interpreter::strbOfrmlr, &Interpreter::unkArm,
    &Interpreter::strbOfrmar, &Interpreter::unkArm,
    &Interpreter::strbOfrmrr, &Interpreter::unkArm,
    &Interpreter::strbOfrmll, &Interpreter::unkArm,
    &Interpreter::strbOfrmlr, &Interpreter::unkArm,
    &Interpreter::strbOfrmar, &Interpreter::unkArm,
    &Interpreter::strbOfrmrr, &Interpreter::unkArm,

    // 0x750-0x75F: LDRB Rd,[Rn-Rm,shift]
    &Interpreter::ldrbOfrmll, &Interpreter::unkArm,
    &Interpreter::ldrbOfrmlr, &Interpreter::unkArm,
    &Interpreter::ldrbOfrmar, &Interpreter::unkArm,
    &Interpreter::ldrbOfrmrr, &Interpreter::unkArm,
    &Interpreter::ldrbOfrmll, &Interpreter::unkArm,
    &Interpreter::ldrbOfrmlr, &Interpreter::unkArm,
    &Interpreter::ldrbOfrmar, &Interpreter::unkArm,
    &Interpreter::ldrbOfrmrr, &Interpreter::unkArm,

    // 0x760-0x76F: STRB Rd,[Rn-Rm,shift]!
    &Interpreter::strbPrrmll, &Interpreter::unkArm,
    &Interpreter::strbPrrmlr, &Interpreter::unkArm,
    &Interpreter::strbPrrmar, &Interpreter::unkArm,
    &Interpreter::strbPrrmrr, &Interpreter::unkArm,
    &Interpreter::strbPrrmll, &Interpreter::unkArm,
    &Interpreter::strbPrrmlr, &Interpreter::unkArm,
    &Interpreter::strbPrrmar, &Interpreter::unkArm,
    &Interpreter::strbPrrmrr, &Interpreter::unkArm,

    // 0x770-0x77F: LDRB Rd,[Rn-Rm,shift]!
    &Interpreter::ldrbPrrmll, &Interpreter::unkArm,
    &Interpreter::ldrbPrrmlr, &Interpreter::unkArm,
    &Interpreter::ldrbPrrmar, &Interpreter::unkArm,
    &Interpreter::ldrbPrrmrr, &Interpreter::unkArm,
    &Interpreter::ldrbPrrmll, &Interpreter::unkArm,
    &Interpreter::ldrbPrrmlr, &Interpreter::unkArm,
    &Interpreter::ldrbPrrmar, &Interpreter::unkArm,
    &Interpreter::ldrbPrrmrr, &Interpreter::unkArm,

    // 0x780-0x78F: STR Rd,[Rn+Rm,shift]
    &Interpreter::strOfrpll, &Interpreter::unkArm,
    &Interpreter::strOfrplr, &Interpreter::unkArm,
    &Interpreter::strOfrpar, &Interpreter::unkArm,
    &Interpreter::strOfrprr, &Interpreter::unkArm,
    &Interpreter::strOfrpll, &Interpreter::unkArm,
    &Interpreter::strOfrplr, &Interpreter::unkArm,
    &Interpreter::strOfrpar, &Interpreter::unkArm,
    &Interpreter::strOfrprr, &Interpreter::unkArm,

    // 0x790-0x79F: LDR Rd,[Rn+Rm,shift]
    &Interpreter::ldrOfrpll, &Interpreter::unkArm,
    &Interpreter::ldrOfrplr, &Interpreter::unkArm,
    &Interpreter::ldrOfrpar, &Interpreter::unkArm,
    &Interpreter::ldrOfrprr, &Interpreter::unkArm,
    &Interpreter::ldrOfrpll, &Interpreter::unkArm,
    &Interpreter::ldrOfrplr, &Interpreter::unkArm,
    &Interpreter::ldrOfrpar, &Interpreter::unkArm,
    &Interpreter::ldrOfrprr, &Interpreter::unkArm,

    // 0x7A0-0x7AF: STR Rd,[Rn+Rm,shift]!
    &Interpreter::strPrrpll, &Interpreter::unkArm,
    &Interpreter::strPrrplr, &Interpreter::unkArm,
    &Interpreter::strPrrpar, &Interpreter::unkArm,
    &Interpreter::strPrrprr, &Interpreter::unkArm,
    &Interpreter::strPrrpll, &Interpreter::unkArm,
    &Interpreter::strPrrplr, &Interpreter::unkArm,
    &Interpreter::strPrrpar, &Interpreter::unkArm,
    &Interpreter::strPrrprr, &Interpreter::unkArm,

    // 0x7B0-0x7BF: LDR Rd,[Rn+Rm,shift]!
    &Interpreter::ldrPrrpll, &Interpreter::unkArm,
    &Interpreter::ldrPrrplr, &Interpreter::unkArm,
    &Interpreter::ldrPrrpar, &Interpreter::unkArm,
    &Interpreter::ldrPrrprr, &Interpreter::unkArm,
    &Interpreter::ldrPrrpll, &Interpreter::unkArm,
    &Interpreter::ldrPrrplr, &Interpreter::unkArm,
    &Interpreter::ldrPrrpar, &Interpreter::unkArm,
    &Interpreter::ldrPrrprr, &Interpreter::unkArm,

    // 0x7C0-0x7CF: STRB Rd,[Rn+Rm,shift]
    &Interpreter::strbOfrpll, &Interpreter::unkArm,
    &Interpreter::strbOfrplr, &Interpreter::unkArm,
    &Interpreter::strbOfrpar, &Interpreter::unkArm,
    &Interpreter::strbOfrprr, &Interpreter::unkArm,
    &Interpreter::strbOfrpll, &Interpreter::unkArm,
    &Interpreter::strbOfrplr, &Interpreter::unkArm,
    &Interpreter::strbOfrpar, &Interpreter::unkArm,
    &Interpreter::strbOfrprr, &Interpreter::unkArm,

    // 0x7D0-0x7DF: LDRB Rd,[Rn+Rm,shift]
    &Interpreter::ldrbOfrpll, &Interpreter::unkArm,
    &Interpreter::ldrbOfrplr, &Interpreter::unkArm,
    &Interpreter::ldrbOfrpar, &Interpreter::unkArm,
    &Interpreter::ldrbOfrprr, &Interpreter::unkArm,
    &Interpreter::ldrbOfrpll, &Interpreter::unkArm,
    &Interpreter::ldrbOfrplr, &Interpreter::unkArm,
    &Interpreter::ldrbOfrpar, &Interpreter::unkArm,
    &Interpreter::ldrbOfrprr, &Interpreter::unkArm,

    // 0x7E0-0x7EF: STRB Rd,[Rn+Rm,shift]!
    &Interpreter::strbPrrpll, &Interpreter::unkArm,
    &Interpreter::strbPrrplr, &Interpreter::unkArm,
    &Interpreter::strbPrrpar, &Interpreter::unkArm,
    &Interpreter::strbPrrprr, &Interpreter::unkArm,
    &Interpreter::strbPrrpll, &Interpreter::unkArm,
    &Interpreter::strbPrrplr, &Interpreter::unkArm,
    &Interpreter::strbPrrpar, &Interpreter::unkArm,
    &Interpreter::strbPrrprr, &Interpreter::unkArm,

    // 0x7F0-0x7FF: LDRB Rd,[Rn+Rm,shift]!
    &Interpreter::ldrbPrrpll, &Interpreter::unkArm,
    &Interpreter::ldrbPrrplr, &Interpreter::unkArm,
    &Interpreter::ldrbPrrpar, &Interpreter::unkArm,
    &Interpreter::ldrbPrrprr, &Interpreter::unkArm,
    &Interpreter::ldrbPrrpll, &Interpreter::unkArm,
    &Interpreter::ldrbPrrplr, &Interpreter::unkArm,
    &Interpreter::ldrbPrrpar, &Interpreter::unkArm,
    &Interpreter::ldrbPrrprr, &Interpreter::unkArm,

    // ----------------------------------------------------------------
    // 0x800-0x9FF: LDM/STM (block data transfer)
    // All 256 entries in each 256-entry sub-block map to the same handler
    // because the register list is encoded in bits 15-0 of the opcode,
    // not in the index bits we use for dispatch.
    // ----------------------------------------------------------------

    // 0x800-0x80F: STMDA Rn,<Rlist> (store multiple, decrement after)
    &Interpreter::stmda, &Interpreter::stmda,
    &Interpreter::stmda, &Interpreter::stmda,
    &Interpreter::stmda, &Interpreter::stmda,
    &Interpreter::stmda, &Interpreter::stmda,
    &Interpreter::stmda, &Interpreter::stmda,
    &Interpreter::stmda, &Interpreter::stmda,
    &Interpreter::stmda, &Interpreter::stmda,
    &Interpreter::stmda, &Interpreter::stmda,

    // 0x810-0x81F: LDMDA Rn,<Rlist>
    &Interpreter::ldmda, &Interpreter::ldmda,
    &Interpreter::ldmda, &Interpreter::ldmda,
    &Interpreter::ldmda, &Interpreter::ldmda,
    &Interpreter::ldmda, &Interpreter::ldmda,
    &Interpreter::ldmda, &Interpreter::ldmda,
    &Interpreter::ldmda, &Interpreter::ldmda,
    &Interpreter::ldmda, &Interpreter::ldmda,
    &Interpreter::ldmda, &Interpreter::ldmda,

    // 0x820-0x82F: STMDA Rn!,<Rlist>
    &Interpreter::stmdaW, &Interpreter::stmdaW,
    &Interpreter::stmdaW, &Interpreter::stmdaW,
    &Interpreter::stmdaW, &Interpreter::stmdaW,
    &Interpreter::stmdaW, &Interpreter::stmdaW,
    &Interpreter::stmdaW, &Interpreter::stmdaW,
    &Interpreter::stmdaW, &Interpreter::stmdaW,
    &Interpreter::stmdaW, &Interpreter::stmdaW,
    &Interpreter::stmdaW, &Interpreter::stmdaW,

    // 0x830-0x83F: LDMDA Rn!,<Rlist>
    &Interpreter::ldmdaW, &Interpreter::ldmdaW,
    &Interpreter::ldmdaW, &Interpreter::ldmdaW,
    &Interpreter::ldmdaW, &Interpreter::ldmdaW,
    &Interpreter::ldmdaW, &Interpreter::ldmdaW,
    &Interpreter::ldmdaW, &Interpreter::ldmdaW,
    &Interpreter::ldmdaW, &Interpreter::ldmdaW,
    &Interpreter::ldmdaW, &Interpreter::ldmdaW,
    &Interpreter::ldmdaW, &Interpreter::ldmdaW,

    // 0x840-0x84F: STMDA Rn,<Rlist>^ (user mode registers)
    &Interpreter::stmdaU, &Interpreter::stmdaU,
    &Interpreter::stmdaU, &Interpreter::stmdaU,
    &Interpreter::stmdaU, &Interpreter::stmdaU,
    &Interpreter::stmdaU, &Interpreter::stmdaU,
    &Interpreter::stmdaU, &Interpreter::stmdaU,
    &Interpreter::stmdaU, &Interpreter::stmdaU,
    &Interpreter::stmdaU, &Interpreter::stmdaU,
    &Interpreter::stmdaU, &Interpreter::stmdaU,

    // 0x850-0x85F: LDMDA Rn,<Rlist>^
    &Interpreter::ldmdaU, &Interpreter::ldmdaU,
    &Interpreter::ldmdaU, &Interpreter::ldmdaU,
    &Interpreter::ldmdaU, &Interpreter::ldmdaU,
    &Interpreter::ldmdaU, &Interpreter::ldmdaU,
    &Interpreter::ldmdaU, &Interpreter::ldmdaU,
    &Interpreter::ldmdaU, &Interpreter::ldmdaU,
    &Interpreter::ldmdaU, &Interpreter::ldmdaU,
    &Interpreter::ldmdaU, &Interpreter::ldmdaU,

    // 0x860-0x86F: STMDA Rn!,<Rlist>^
    &Interpreter::stmdaUW, &Interpreter::stmdaUW,
    &Interpreter::stmdaUW, &Interpreter::stmdaUW,
    &Interpreter::stmdaUW, &Interpreter::stmdaUW,
    &Interpreter::stmdaUW, &Interpreter::stmdaUW,
    &Interpreter::stmdaUW, &Interpreter::stmdaUW,
    &Interpreter::stmdaUW, &Interpreter::stmdaUW,
    &Interpreter::stmdaUW, &Interpreter::stmdaUW,
    &Interpreter::stmdaUW, &Interpreter::stmdaUW,

    // 0x870-0x87F: LDMDA Rn!,<Rlist>^
    &Interpreter::ldmdaUW, &Interpreter::ldmdaUW,
    &Interpreter::ldmdaUW, &Interpreter::ldmdaUW,
    &Interpreter::ldmdaUW, &Interpreter::ldmdaUW,
    &Interpreter::ldmdaUW, &Interpreter::ldmdaUW,
    &Interpreter::ldmdaUW, &Interpreter::ldmdaUW,
    &Interpreter::ldmdaUW, &Interpreter::ldmdaUW,
    &Interpreter::ldmdaUW, &Interpreter::ldmdaUW,
    &Interpreter::ldmdaUW, &Interpreter::ldmdaUW,

    // 0x880-0x88F: STMIA Rn,<Rlist>
    &Interpreter::stmia, &Interpreter::stmia,
    &Interpreter::stmia, &Interpreter::stmia,
    &Interpreter::stmia, &Interpreter::stmia,
    &Interpreter::stmia, &Interpreter::stmia,
    &Interpreter::stmia, &Interpreter::stmia,
    &Interpreter::stmia, &Interpreter::stmia,
    &Interpreter::stmia, &Interpreter::stmia,
    &Interpreter::stmia, &Interpreter::stmia,

    // 0x890-0x89F: LDMIA Rn,<Rlist>
    &Interpreter::ldmia, &Interpreter::ldmia,
    &Interpreter::ldmia, &Interpreter::ldmia,
    &Interpreter::ldmia, &Interpreter::ldmia,
    &Interpreter::ldmia, &Interpreter::ldmia,
    &Interpreter::ldmia, &Interpreter::ldmia,
    &Interpreter::ldmia, &Interpreter::ldmia,
    &Interpreter::ldmia, &Interpreter::ldmia,
    &Interpreter::ldmia, &Interpreter::ldmia,

    // 0x8A0-0x8AF: STMIA Rn!,<Rlist>
    &Interpreter::stmiaW, &Interpreter::stmiaW,
    &Interpreter::stmiaW, &Interpreter::stmiaW,
    &Interpreter::stmiaW, &Interpreter::stmiaW,
    &Interpreter::stmiaW, &Interpreter::stmiaW,
    &Interpreter::stmiaW, &Interpreter::stmiaW,
    &Interpreter::stmiaW, &Interpreter::stmiaW,
    &Interpreter::stmiaW, &Interpreter::stmiaW,
    &Interpreter::stmiaW, &Interpreter::stmiaW,

    // 0x8B0-0x8BF: LDMIA Rn!,<Rlist>
    &Interpreter::ldmiaW, &Interpreter::ldmiaW,
    &Interpreter::ldmiaW, &Interpreter::ldmiaW,
    &Interpreter::ldmiaW, &Interpreter::ldmiaW,
    &Interpreter::ldmiaW, &Interpreter::ldmiaW,
    &Interpreter::ldmiaW, &Interpreter::ldmiaW,
    &Interpreter::ldmiaW, &Interpreter::ldmiaW,
    &Interpreter::ldmiaW, &Interpreter::ldmiaW,
    &Interpreter::ldmiaW, &Interpreter::ldmiaW,

    // 0x8C0-0x8CF: STMIA Rn,<Rlist>^
    &Interpreter::stmiaU, &Interpreter::stmiaU,
    &Interpreter::stmiaU, &Interpreter::stmiaU,
    &Interpreter::stmiaU, &Interpreter::stmiaU,
    &Interpreter::stmiaU, &Interpreter::stmiaU,
    &Interpreter::stmiaU, &Interpreter::stmiaU,
    &Interpreter::stmiaU, &Interpreter::stmiaU,
    &Interpreter::stmiaU, &Interpreter::stmiaU,
    &Interpreter::stmiaU, &Interpreter::stmiaU,

    // 0x8D0-0x8DF: LDMIA Rn,<Rlist>^
    &Interpreter::ldmiaU, &Interpreter::ldmiaU,
    &Interpreter::ldmiaU, &Interpreter::ldmiaU,
    &Interpreter::ldmiaU, &Interpreter::ldmiaU,
    &Interpreter::ldmiaU, &Interpreter::ldmiaU,
    &Interpreter::ldmiaU, &Interpreter::ldmiaU,
    &Interpreter::ldmiaU, &Interpreter::ldmiaU,
    &Interpreter::ldmiaU, &Interpreter::ldmiaU,
    &Interpreter::ldmiaU, &Interpreter::ldmiaU,

    // 0x8E0-0x8EF: STMIA Rn!,<Rlist>^
    &Interpreter::stmiaUW, &Interpreter::stmiaUW,
    &Interpreter::stmiaUW, &Interpreter::stmiaUW,
    &Interpreter::stmiaUW, &Interpreter::stmiaUW,
    &Interpreter::stmiaUW, &Interpreter::stmiaUW,
    &Interpreter::stmiaUW, &Interpreter::stmiaUW,
    &Interpreter::stmiaUW, &Interpreter::stmiaUW,
    &Interpreter::stmiaUW, &Interpreter::stmiaUW,
    &Interpreter::stmiaUW, &Interpreter::stmiaUW,

    // 0x8F0-0x8FF: LDMIA Rn!,<Rlist>^
    &Interpreter::ldmiaUW, &Interpreter::ldmiaUW,
    &Interpreter::ldmiaUW, &Interpreter::ldmiaUW,
    &Interpreter::ldmiaUW, &Interpreter::ldmiaUW,
    &Interpreter::ldmiaUW, &Interpreter::ldmiaUW,
    &Interpreter::ldmiaUW, &Interpreter::ldmiaUW,
    &Interpreter::ldmiaUW, &Interpreter::ldmiaUW,
    &Interpreter::ldmiaUW, &Interpreter::ldmiaUW,
    &Interpreter::ldmiaUW, &Interpreter::ldmiaUW,

    // 0x900-0x90F: STMDB Rn,<Rlist>
    &Interpreter::stmdb, &Interpreter::stmdb,
    &Interpreter::stmdb, &Interpreter::stmdb,
    &Interpreter::stmdb, &Interpreter::stmdb,
    &Interpreter::stmdb, &Interpreter::stmdb,
    &Interpreter::stmdb, &Interpreter::stmdb,
    &Interpreter::stmdb, &Interpreter::stmdb,
    &Interpreter::stmdb, &Interpreter::stmdb,
    &Interpreter::stmdb, &Interpreter::stmdb,

    // 0x910-0x91F: LDMDB Rn,<Rlist>
    &Interpreter::ldmdb, &Interpreter::ldmdb,
    &Interpreter::ldmdb, &Interpreter::ldmdb,
    &Interpreter::ldmdb, &Interpreter::ldmdb,
    &Interpreter::ldmdb, &Interpreter::ldmdb,
    &Interpreter::ldmdb, &Interpreter::ldmdb,
    &Interpreter::ldmdb, &Interpreter::ldmdb,
    &Interpreter::ldmdb, &Interpreter::ldmdb,
    &Interpreter::ldmdb, &Interpreter::ldmdb,

    // 0x920-0x92F: STMDB Rn!,<Rlist>
    &Interpreter::stmdbW, &Interpreter::stmdbW,
    &Interpreter::stmdbW, &Interpreter::stmdbW,
    &Interpreter::stmdbW, &Interpreter::stmdbW,
    &Interpreter::stmdbW, &Interpreter::stmdbW,
    &Interpreter::stmdbW, &Interpreter::stmdbW,
    &Interpreter::stmdbW, &Interpreter::stmdbW,
    &Interpreter::stmdbW, &Interpreter::stmdbW,
    &Interpreter::stmdbW, &Interpreter::stmdbW,

    // 0x930-0x93F: LDMDB Rn!,<Rlist>
    &Interpreter::ldmdbW, &Interpreter::ldmdbW,
    &Interpreter::ldmdbW, &Interpreter::ldmdbW,
    &Interpreter::ldmdbW, &Interpreter::ldmdbW,
    &Interpreter::ldmdbW, &Interpreter::ldmdbW,
    &Interpreter::ldmdbW, &Interpreter::ldmdbW,
    &Interpreter::ldmdbW, &Interpreter::ldmdbW,
    &Interpreter::ldmdbW, &Interpreter::ldmdbW,
    &Interpreter::ldmdbW, &Interpreter::ldmdbW,

    // 0x940-0x94F: STMDB Rn,<Rlist>^
    &Interpreter::stmdbU, &Interpreter::stmdbU,
    &Interpreter::stmdbU, &Interpreter::stmdbU,
    &Interpreter::stmdbU, &Interpreter::stmdbU,
    &Interpreter::stmdbU, &Interpreter::stmdbU,
    &Interpreter::stmdbU, &Interpreter::stmdbU,
    &Interpreter::stmdbU, &Interpreter::stmdbU,
    &Interpreter::stmdbU, &Interpreter::stmdbU,
    &Interpreter::stmdbU, &Interpreter::stmdbU,

    // 0x950-0x95F: LDMDB Rn,<Rlist>^
    &Interpreter::ldmdbU, &Interpreter::ldmdbU,
    &Interpreter::ldmdbU, &Interpreter::ldmdbU,
    &Interpreter::ldmdbU, &Interpreter::ldmdbU,
    &Interpreter::ldmdbU, &Interpreter::ldmdbU,
    &Interpreter::ldmdbU, &Interpreter::ldmdbU,
    &Interpreter::ldmdbU, &Interpreter::ldmdbU,
    &Interpreter::ldmdbU, &Interpreter::ldmdbU,
    &Interpreter::ldmdbU, &Interpreter::ldmdbU,

    // 0x960-0x96F: STMDB Rn!,<Rlist>^
    &Interpreter::stmdbUW, &Interpreter::stmdbUW,
    &Interpreter::stmdbUW, &Interpreter::stmdbUW,
    &Interpreter::stmdbUW, &Interpreter::stmdbUW,
    &Interpreter::stmdbUW, &Interpreter::stmdbUW,
    &Interpreter::stmdbUW, &Interpreter::stmdbUW,
    &Interpreter::stmdbUW, &Interpreter::stmdbUW,
    &Interpreter::stmdbUW, &Interpreter::stmdbUW,
    &Interpreter::stmdbUW, &Interpreter::stmdbUW,

    // 0x970-0x97F: LDMDB Rn!,<Rlist>^
    &Interpreter::ldmdbUW, &Interpreter::ldmdbUW,
    &Interpreter::ldmdbUW, &Interpreter::ldmdbUW,
    &Interpreter::ldmdbUW, &Interpreter::ldmdbUW,
    &Interpreter::ldmdbUW, &Interpreter::ldmdbUW,
    &Interpreter::ldmdbUW, &Interpreter::ldmdbUW,
    &Interpreter::ldmdbUW, &Interpreter::ldmdbUW,
    &Interpreter::ldmdbUW, &Interpreter::ldmdbUW,
    &Interpreter::ldmdbUW, &Interpreter::ldmdbUW,

    // 0x980-0x98F: STMIB Rn,<Rlist>
    &Interpreter::stmib, &Interpreter::stmib,
    &Interpreter::stmib, &Interpreter::stmib,
    &Interpreter::stmib, &Interpreter::stmib,
    &Interpreter::stmib, &Interpreter::stmib,
    &Interpreter::stmib, &Interpreter::stmib,
    &Interpreter::stmib, &Interpreter::stmib,
    &Interpreter::stmib, &Interpreter::stmib,
    &Interpreter::stmib, &Interpreter::stmib,

    // 0x990-0x99F: LDMIB Rn,<Rlist>
    &Interpreter::ldmib, &Interpreter::ldmib,
    &Interpreter::ldmib, &Interpreter::ldmib,
    &Interpreter::ldmib, &Interpreter::ldmib,
    &Interpreter::ldmib, &Interpreter::ldmib,
    &Interpreter::ldmib, &Interpreter::ldmib,
    &Interpreter::ldmib, &Interpreter::ldmib,
    &Interpreter::ldmib, &Interpreter::ldmib,
    &Interpreter::ldmib, &Interpreter::ldmib,

    // 0x9A0-0x9AF: STMIB Rn!,<Rlist>
    &Interpreter::stmibW, &Interpreter::stmibW,
    &Interpreter::stmibW, &Interpreter::stmibW,
    &Interpreter::stmibW, &Interpreter::stmibW,
    &Interpreter::stmibW, &Interpreter::stmibW,
    &Interpreter::stmibW, &Interpreter::stmibW,
    &Interpreter::stmibW, &Interpreter::stmibW,
    &Interpreter::stmibW, &Interpreter::stmibW,
    &Interpreter::stmibW, &Interpreter::stmibW,

    // 0x9B0-0x9BF: LDMIB Rn!,<Rlist>
    &Interpreter::ldmibW, &Interpreter::ldmibW,
    &Interpreter::ldmibW, &Interpreter::ldmibW,
    &Interpreter::ldmibW, &Interpreter::ldmibW,
    &Interpreter::ldmibW, &Interpreter::ldmibW,
    &Interpreter::ldmibW, &Interpreter::ldmibW,
    &Interpreter::ldmibW, &Interpreter::ldmibW,
    &Interpreter::ldmibW, &Interpreter::ldmibW,
    &Interpreter::ldmibW, &Interpreter::ldmibW,

    // 0x9C0-0x9CF: STMIB Rn,<Rlist>^
    &Interpreter::stmibU, &Interpreter::stmibU,
    &Interpreter::stmibU, &Interpreter::stmibU,
    &Interpreter::stmibU, &Interpreter::stmibU,
    &Interpreter::stmibU, &Interpreter::stmibU,
    &Interpreter::stmibU, &Interpreter::stmibU,
    &Interpreter::stmibU, &Interpreter::stmibU,
    &Interpreter::stmibU, &Interpreter::stmibU,
    &Interpreter::stmibU, &Interpreter::stmibU,

    // 0x9D0-0x9DF: LDMIB Rn,<Rlist>^
    &Interpreter::ldmibU, &Interpreter::ldmibU,
    &Interpreter::ldmibU, &Interpreter::ldmibU,
    &Interpreter::ldmibU, &Interpreter::ldmibU,
    &Interpreter::ldmibU, &Interpreter::ldmibU,
    &Interpreter::ldmibU, &Interpreter::ldmibU,
    &Interpreter::ldmibU, &Interpreter::ldmibU,
    &Interpreter::ldmibU, &Interpreter::ldmibU,
    &Interpreter::ldmibU, &Interpreter::ldmibU,

    // 0x9E0-0x9EF: STMIB Rn!,<Rlist>^
    &Interpreter::stmibUW, &Interpreter::stmibUW,
    &Interpreter::stmibUW, &Interpreter::stmibUW,
    &Interpreter::stmibUW, &Interpreter::stmibUW,
    &Interpreter::stmibUW, &Interpreter::stmibUW,
    &Interpreter::stmibUW, &Interpreter::stmibUW,
    &Interpreter::stmibUW, &Interpreter::stmibUW,
    &Interpreter::stmibUW, &Interpreter::stmibUW,
    &Interpreter::stmibUW, &Interpreter::stmibUW,

    // 0x9F0-0x9FF: LDMIB Rn!,<Rlist>^
    &Interpreter::ldmibUW, &Interpreter::ldmibUW,
    &Interpreter::ldmibUW, &Interpreter::ldmibUW,
    &Interpreter::ldmibUW, &Interpreter::ldmibUW,
    &Interpreter::ldmibUW, &Interpreter::ldmibUW,
    &Interpreter::ldmibUW, &Interpreter::ldmibUW,
    &Interpreter::ldmibUW, &Interpreter::ldmibUW,
    &Interpreter::ldmibUW, &Interpreter::ldmibUW,
    &Interpreter::ldmibUW, &Interpreter::ldmibUW,

    // ----------------------------------------------------------------
    // 0xA00-0xAFF: B (branch) - 256 entries, all same handler
    // ----------------------------------------------------------------
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,
    &Interpreter::b, &Interpreter::b, &Interpreter::b, &Interpreter::b,

    // ----------------------------------------------------------------
    // 0xB00-0xBFF: BL (branch with link) - 256 entries
    // ----------------------------------------------------------------
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,
    &Interpreter::bl, &Interpreter::bl, &Interpreter::bl, &Interpreter::bl,

    // ----------------------------------------------------------------
    // 0xC00-0xCFF: Coprocessor data transfer (unused on NDS, all unkArm)
    // ----------------------------------------------------------------
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,

    // ----------------------------------------------------------------
    // 0xD00-0xDFF: Coprocessor data transfer (unused, all unkArm)
    // ----------------------------------------------------------------
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,
    &Interpreter::unkArm, &Interpreter::unkArm,

    // ----------------------------------------------------------------
    // 0xE00-0xEFF: Coprocessor data operations and MRC/MCR
    // Odd indices in 0xE00-0xEFF are MRC/MCR; even are CDP (unused)
    // ----------------------------------------------------------------
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mrc,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,
    &Interpreter::unkArm, &Interpreter::mcr,

    // ----------------------------------------------------------------
    // 0xF00-0xFFF: SWI (software interrupt) - 256 entries
    // ----------------------------------------------------------------
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
    &Interpreter::swi, &Interpreter::swi, &Interpreter::swi, &Interpreter::swi,
};

// ====================================================================
// THUMB instruction dispatch table
// Indexed by bits 15-6 of the opcode (10 bits = 1024 entries)
// Total size: 1024 * 4 = 4096 bytes = 128 cache lines
// ====================================================================
DISPATCH_TABLE
int (Interpreter::*Interpreter::thumbInstrs[])(uint16_t) = {
    // 0x000-0x01F: LSL Rd,Rs,#i (shift=0..31, 2 entries each)
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,
    &Interpreter::lslImmT, &Interpreter::lslImmT,

    // 0x020-0x03F: LSR Rd,Rs,#i
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,
    &Interpreter::lsrImmT, &Interpreter::lsrImmT,

    // 0x040-0x05F: ASR Rd,Rs,#i
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,
    &Interpreter::asrImmT, &Interpreter::asrImmT,

    // 0x060-0x063: ADD Rd,Rs,Rn
    &Interpreter::addRegT, &Interpreter::addRegT,
    &Interpreter::addRegT, &Interpreter::addRegT,

    // 0x064-0x067: SUB Rd,Rs,Rn
    &Interpreter::subRegT, &Interpreter::subRegT,
    &Interpreter::subRegT, &Interpreter::subRegT,

    // 0x068-0x06B: ADD Rd,Rs,#3
    &Interpreter::addImm3T, &Interpreter::addImm3T,
    &Interpreter::addImm3T, &Interpreter::addImm3T,

    // 0x06C-0x06F: SUB Rd,Rs,#3
    &Interpreter::subImm3T, &Interpreter::subImm3T,
    &Interpreter::subImm3T, &Interpreter::subImm3T,

    // 0x070-0x07F: MOV Rd,#imm8 (Rd in bits 10:8)
    &Interpreter::movImm8T, &Interpreter::movImm8T,
    &Interpreter::movImm8T, &Interpreter::movImm8T,
    &Interpreter::movImm8T, &Interpreter::movImm8T,
    &Interpreter::movImm8T, &Interpreter::movImm8T,
    &Interpreter::movImm8T, &Interpreter::movImm8T,
    &Interpreter::movImm8T, &Interpreter::movImm8T,
    &Interpreter::movImm8T, &Interpreter::movImm8T,
    &Interpreter::movImm8T, &Interpreter::movImm8T,

    // 0x080-0x08F: CMP Rd,#imm8
    &Interpreter::cmpImm8T, &Interpreter::cmpImm8T,
    &Interpreter::cmpImm8T, &Interpreter::cmpImm8T,
    &Interpreter::cmpImm8T, &Interpreter::cmpImm8T,
    &Interpreter::cmpImm8T, &Interpreter::cmpImm8T,
    &Interpreter::cmpImm8T, &Interpreter::cmpImm8T,
    &Interpreter::cmpImm8T, &Interpreter::cmpImm8T,
    &Interpreter::cmpImm8T, &Interpreter::cmpImm8T,
    &Interpreter::cmpImm8T, &Interpreter::cmpImm8T,

    // 0x090-0x09F: ADD Rd,#imm8
    &Interpreter::addImm8T, &Interpreter::addImm8T,
    &Interpreter::addImm8T, &Interpreter::addImm8T,
    &Interpreter::addImm8T, &Interpreter::addImm8T,
    &Interpreter::addImm8T, &Interpreter::addImm8T,
    &Interpreter::addImm8T, &Interpreter::addImm8T,
    &Interpreter::addImm8T, &Interpreter::addImm8T,
    &Interpreter::addImm8T, &Interpreter::addImm8T,
    &Interpreter::addImm8T, &Interpreter::addImm8T,

    // 0x0A0-0x0AF: SUB Rd,#imm8
    &Interpreter::subImm8T, &Interpreter::subImm8T,
    &Interpreter::subImm8T, &Interpreter::subImm8T,
    &Interpreter::subImm8T, &Interpreter::subImm8T,
    &Interpreter::subImm8T, &Interpreter::subImm8T,
    &Interpreter::subImm8T, &Interpreter::subImm8T,
    &Interpreter::subImm8T, &Interpreter::subImm8T,
    &Interpreter::subImm8T, &Interpreter::subImm8T,
    &Interpreter::subImm8T, &Interpreter::subImm8T,

    // 0x0B0-0x0BF: Data processing (ALU ops, 4 entries each)
    // AND, EOR, LSL, LSR, ASR, ADC, SBC, ROR
    // TST, NEG, CMP, CMN, ORR, MUL, BIC, MVN
    &Interpreter::andDpT,  &Interpreter::andDpT,
    &Interpreter::andDpT,  &Interpreter::andDpT,
    &Interpreter::eorDpT,  &Interpreter::eorDpT,
    &Interpreter::eorDpT,  &Interpreter::eorDpT,
    &Interpreter::lslDpT,  &Interpreter::lslDpT,
    &Interpreter::lslDpT,  &Interpreter::lslDpT,
    &Interpreter::lsrDpT,  &Interpreter::lsrDpT,
    &Interpreter::lsrDpT,  &Interpreter::lsrDpT,

    // 0x0C0-0x0CF
    &Interpreter::asrDpT,  &Interpreter::asrDpT,
    &Interpreter::asrDpT,  &Interpreter::asrDpT,
    &Interpreter::adcDpT,  &Interpreter::adcDpT,
    &Interpreter::adcDpT,  &Interpreter::adcDpT,
    &Interpreter::sbcDpT,  &Interpreter::sbcDpT,
    &Interpreter::sbcDpT,  &Interpreter::sbcDpT,
    &Interpreter::rorDpT,  &Interpreter::rorDpT,
    &Interpreter::rorDpT,  &Interpreter::rorDpT,

    // 0x0D0-0x0DF
    &Interpreter::tstDpT,  &Interpreter::tstDpT,
    &Interpreter::tstDpT,  &Interpreter::tstDpT,
    &Interpreter::negDpT,  &Interpreter::negDpT,
    &Interpreter::negDpT,  &Interpreter::negDpT,
    &Interpreter::cmpDpT,  &Interpreter::cmpDpT,
    &Interpreter::cmpDpT,  &Interpreter::cmpDpT,
    &Interpreter::cmnDpT,  &Interpreter::cmnDpT,
    &Interpreter::cmnDpT,  &Interpreter::cmnDpT,

    // 0x0E0-0x0EF
    &Interpreter::orrDpT,  &Interpreter::orrDpT,
    &Interpreter::orrDpT,  &Interpreter::orrDpT,
    &Interpreter::mulDpT,  &Interpreter::mulDpT,
    &Interpreter::mulDpT,  &Interpreter::mulDpT,
    &Interpreter::bicDpT,  &Interpreter::bicDpT,
    &Interpreter::bicDpT,  &Interpreter::bicDpT,
    &Interpreter::mvnDpT,  &Interpreter::mvnDpT,
    &Interpreter::mvnDpT,  &Interpreter::mvnDpT,

    // 0x0F0-0x0FF: Special data instructions (ADD/CMP/MOV Rd,Rs with high regs, BX, BLX)
    // Indexed by bits 9:6: 00=ADD, 01=CMP, 10=MOV, 11=BX/BLX
    &Interpreter::addHT,   &Interpreter::addHT,
    &Interpreter::addHT,   &Interpreter::addHT,
    &Interpreter::cmpHT,   &Interpreter::cmpHT,
    &Interpreter::cmpHT,   &Interpreter::cmpHT,
    &Interpreter::movHT,   &Interpreter::movHT,
    &Interpreter::movHT,   &Interpreter::movHT,
    &Interpreter::bxRegT,  &Interpreter::bxRegT,
    &Interpreter::blxRegT, &Interpreter::blxRegT,

    // 0x100-0x11F: LDR Rd,[PC,#imm8]
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,
    &Interpreter::ldrPcT, &Interpreter::ldrPcT,

    // 0x120-0x121: STR Rd,[Rb,Ro]
    &Interpreter::strRegT, &Interpreter::strRegT,
    // 0x122-0x123: STRH Rd,[Rb,Ro]
    &Interpreter::strhRegT, &Interpreter::strhRegT,
    // 0x124-0x125: STRB Rd,[Rb,Ro]
    &Interpreter::strbRegT, &Interpreter::strbRegT,
    // 0x126-0x127: LDRSB Rd,[Rb,Ro]
    &Interpreter::ldrsbRegT, &Interpreter::ldrsbRegT,
    // 0x128-0x129: LDR Rd,[Rb,Ro]
    &Interpreter::ldrRegT, &Interpreter::ldrRegT,
    // 0x12A-0x12B: LDRH Rd,[Rb,Ro]
    &Interpreter::ldrhRegT, &Interpreter::ldrhRegT,
    // 0x12C-0x12D: LDRB Rd,[Rb,Ro]
    &Interpreter::ldrbRegT, &Interpreter::ldrbRegT,
    // 0x12E-0x12F: LDRSH Rd,[Rb,Ro]
    &Interpreter::ldrshRegT, &Interpreter::ldrshRegT,

    // 0x130-0x13F: STR Rd,[Rb,#imm5] (word)
    &Interpreter::strImm5T, &Interpreter::strImm5T,
    &Interpreter::strImm5T, &Interpreter::strImm5T,
    &Interpreter::strImm5T, &Interpreter::strImm5T,
    &Interpreter::strImm5T, &Interpreter::strImm5T,
    &Interpreter::strImm5T, &Interpreter::strImm5T,
    &Interpreter::strImm5T, &Interpreter::strImm5T,
    &Interpreter::strImm5T, &Interpreter::strImm5T,
    &Interpreter::strImm5T, &Interpreter::strImm5T,

    // 0x140-0x14F: LDR Rd,[Rb,#imm5]
    &Interpreter::ldrImm5T, &Interpreter::ldrImm5T,
    &Interpreter::ldrImm5T, &Interpreter::ldrImm5T,
    &Interpreter::ldrImm5T, &Interpreter::ldrImm5T,
    &Interpreter::ldrImm5T, &Interpreter::ldrImm5T,
    &Interpreter::ldrImm5T, &Interpreter::ldrImm5T,
    &Interpreter::ldrImm5T, &Interpreter::ldrImm5T,
    &Interpreter::ldrImm5T, &Interpreter::ldrImm5T,
    &Interpreter::ldrImm5T, &Interpreter::ldrImm5T,

    // 0x150-0x15F: STRB Rd,[Rb,#imm5]
    &Interpreter::strbImm5T, &Interpreter::strbImm5T,
    &Interpreter::strbImm5T, &Interpreter::strbImm5T,
    &Interpreter::strbImm5T, &Interpreter::strbImm5T,
    &Interpreter::strbImm5T, &Interpreter::strbImm5T,
    &Interpreter::strbImm5T, &Interpreter::strbImm5T,
    &Interpreter::strbImm5T, &Interpreter::strbImm5T,
    &Interpreter::strbImm5T, &Interpreter::strbImm5T,
    &Interpreter::strbImm5T, &Interpreter::strbImm5T,

    // 0x160-0x16F: LDRB Rd,[Rb,#imm5]
    &Interpreter::ldrbImm5T, &Interpreter::ldrbImm5T,
    &Interpreter::ldrbImm5T, &Interpreter::ldrbImm5T,
    &Interpreter::ldrbImm5T, &Interpreter::ldrbImm5T,
    &Interpreter::ldrbImm5T, &Interpreter::ldrbImm5T,
    &Interpreter::ldrbImm5T, &Interpreter::ldrbImm5T,
    &Interpreter::ldrbImm5T, &Interpreter::ldrbImm5T,
    &Interpreter::ldrbImm5T, &Interpreter::ldrbImm5T,
    &Interpreter::ldrbImm5T, &Interpreter::ldrbImm5T,

    // 0x170-0x17F: STRH Rd,[Rb,#imm5]
    &Interpreter::strhImm5T, &Interpreter::strhImm5T,
    &Interpreter::strhImm5T, &Interpreter::strhImm5T,
    &Interpreter::strhImm5T, &Interpreter::strhImm5T,
    &Interpreter::strhImm5T, &Interpreter::strhImm5T,
    &Interpreter::strhImm5T, &Interpreter::strhImm5T,
    &Interpreter::strhImm5T, &Interpreter::strhImm5T,
    &Interpreter::strhImm5T, &Interpreter::strhImm5T,
    &Interpreter::strhImm5T, &Interpreter::strhImm5T,

    // 0x180-0x18F: LDRH Rd,[Rb,#imm5]
    &Interpreter::ldrhImm5T, &Interpreter::ldrhImm5T,
    &Interpreter::ldrhImm5T, &Interpreter::ldrhImm5T,
    &Interpreter::ldrhImm5T, &Interpreter::ldrhImm5T,
    &Interpreter::ldrhImm5T, &Interpreter::ldrhImm5T,
    &Interpreter::ldrhImm5T, &Interpreter::ldrhImm5T,
    &Interpreter::ldrhImm5T, &Interpreter::ldrhImm5T,
    &Interpreter::ldrhImm5T, &Interpreter::ldrhImm5T,
    &Interpreter::ldrhImm5T, &Interpreter::ldrhImm5T,

    // 0x190-0x19F: STR Rd,[SP,#imm8]
    &Interpreter::strSpT, &Interpreter::strSpT,
    &Interpreter::strSpT, &Interpreter::strSpT,
    &Interpreter::strSpT, &Interpreter::strSpT,
    &Interpreter::strSpT, &Interpreter::strSpT,
    &Interpreter::strSpT, &Interpreter::strSpT,
    &Interpreter::strSpT, &Interpreter::strSpT,
    &Interpreter::strSpT, &Interpreter::strSpT,
    &Interpreter::strSpT, &Interpreter::strSpT,

    // 0x1A0-0x1AF: LDR Rd,[SP,#imm8]
    &Interpreter::ldrSpT, &Interpreter::ldrSpT,
    &Interpreter::ldrSpT, &Interpreter::ldrSpT,
    &Interpreter::ldrSpT, &Interpreter::ldrSpT,
    &Interpreter::ldrSpT, &Interpreter::ldrSpT,
    &Interpreter::ldrSpT, &Interpreter::ldrSpT,
    &Interpreter::ldrSpT, &Interpreter::ldrSpT,
    &Interpreter::ldrSpT, &Interpreter::ldrSpT,
    &Interpreter::ldrSpT, &Interpreter::ldrSpT,

    // 0x1B0-0x1BF: ADD Rd,PC,#imm8
    &Interpreter::addPcT, &Interpreter::addPcT,
    &Interpreter::addPcT, &Interpreter::addPcT,
    &Interpreter::addPcT, &Interpreter::addPcT,
    &Interpreter::addPcT, &Interpreter::addPcT,
    &Interpreter::addPcT, &Interpreter::addPcT,
    &Interpreter::addPcT, &Interpreter::addPcT,
    &Interpreter::addPcT, &Interpreter::addPcT,
    &Interpreter::addPcT, &Interpreter::addPcT,

    // 0x1C0-0x1CF: ADD Rd,SP,#imm8
    &Interpreter::addSpT, &Interpreter::addSpT,
    &Interpreter::addSpT, &Interpreter::addSpT,
    &Interpreter::addSpT, &Interpreter::addSpT,
    &Interpreter::addSpT, &Interpreter::addSpT,
    &Interpreter::addSpT, &Interpreter::addSpT,
    &Interpreter::addSpT, &Interpreter::addSpT,
    &Interpreter::addSpT, &Interpreter::addSpT,
    &Interpreter::addSpT, &Interpreter::addSpT,

    // 0x1D0-0x1D1: ADD SP,#imm7 / SUB SP,#imm7
    &Interpreter::addSpImmT, &Interpreter::addSpImmT,
    // 0x1D2-0x1D3: undefined
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    // 0x1D4-0x1D5: PUSH <Rlist>
    &Interpreter::pushT,  &Interpreter::pushT,
    // 0x1D6-0x1D7: PUSH <Rlist>,LR
    &Interpreter::pushLrT, &Interpreter::pushLrT,
    // 0x1D8-0x1DF: undefined
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,

    // 0x1E0-0x1EF: STMIA Rb!,<Rlist>
    &Interpreter::stmiaT, &Interpreter::stmiaT,
    &Interpreter::stmiaT, &Interpreter::stmiaT,
    &Interpreter::stmiaT, &Interpreter::stmiaT,
    &Interpreter::stmiaT, &Interpreter::stmiaT,
    &Interpreter::stmiaT, &Interpreter::stmiaT,
    &Interpreter::stmiaT, &Interpreter::stmiaT,
    &Interpreter::stmiaT, &Interpreter::stmiaT,
    &Interpreter::stmiaT, &Interpreter::stmiaT,

    // 0x1F0-0x1FF: LDMIA Rb!,<Rlist>
    &Interpreter::ldmiaT, &Interpreter::ldmiaT,
    &Interpreter::ldmiaT, &Interpreter::ldmiaT,
    &Interpreter::ldmiaT, &Interpreter::ldmiaT,
    &Interpreter::ldmiaT, &Interpreter::ldmiaT,
    &Interpreter::ldmiaT, &Interpreter::ldmiaT,
    &Interpreter::ldmiaT, &Interpreter::ldmiaT,
    &Interpreter::ldmiaT, &Interpreter::ldmiaT,
    &Interpreter::ldmiaT, &Interpreter::ldmiaT,

    // 0x200-0x21F: Conditional branches (2 entries each for 16 conditions)
    &Interpreter::beqT,    &Interpreter::beqT,
    &Interpreter::bneT,    &Interpreter::bneT,
    &Interpreter::bcsT,    &Interpreter::bcsT,
    &Interpreter::bccT,    &Interpreter::bccT,
    &Interpreter::bmiT,    &Interpreter::bmiT,
    &Interpreter::bplT,    &Interpreter::bplT,
    &Interpreter::bvsT,    &Interpreter::bvsT,
    &Interpreter::bvcT,    &Interpreter::bvcT,
    &Interpreter::bhiT,    &Interpreter::bhiT,
    &Interpreter::blsT,    &Interpreter::blsT,
    &Interpreter::bgeT,    &Interpreter::bgeT,
    &Interpreter::bltT,    &Interpreter::bltT,
    &Interpreter::bgtT,    &Interpreter::bgtT,
    &Interpreter::bleT,    &Interpreter::bleT,
    &Interpreter::unkThumb,&Interpreter::unkThumb, // 0xDE: undefined
    &Interpreter::swiT,    &Interpreter::swiT,      // 0xDF: SWI

    // 0x220-0x23F: B label (unconditional, 11-bit offset)
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,
    &Interpreter::bT, &Interpreter::bT,

    // 0x240-0x25F: undefined (Thumb2 territory, undefined on ARM7/ARM9)
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,

    // 0x260-0x27F: BL/BLX - first half (setup LR with upper 11 bits)
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,
    &Interpreter::blSetupT, &Interpreter::blSetupT,

    // 0x280-0x29F: POP <Rlist>
    &Interpreter::popT, &Interpreter::popT,
    &Interpreter::popT, &Interpreter::popT,
    &Interpreter::popT, &Interpreter::popT,
    &Interpreter::popT, &Interpreter::popT,
    &Interpreter::popT, &Interpreter::popT,
    &Interpreter::popT, &Interpreter::popT,
    &Interpreter::popT, &Interpreter::popT,
    &Interpreter::popT, &Interpreter::popT,
    // 0x290-0x29F: POP <Rlist>,PC
    &Interpreter::popPcT, &Interpreter::popPcT,
    &Interpreter::popPcT, &Interpreter::popPcT,
    &Interpreter::popPcT, &Interpreter::popPcT,
    &Interpreter::popPcT, &Interpreter::popPcT,
    &Interpreter::popPcT, &Interpreter::popPcT,
    &Interpreter::popPcT, &Interpreter::popPcT,
    &Interpreter::popPcT, &Interpreter::popPcT,
    &Interpreter::popPcT, &Interpreter::popPcT,

    // 0x2A0-0x2BF: BLX label (second half, even-indexed only valid on ARM9)
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,
    &Interpreter::blxOffT, &Interpreter::blxOffT,

    // 0x2C0-0x2FF: BL label (second half, odd-indexed in original encoding)
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,
    &Interpreter::blOffT, &Interpreter::blOffT,

    // 0x300-0x3FF: Remaining entries (all undefined/unkThumb for completeness)
    // The THUMB table is 1024 entries; remaining 256 are padding for unused
    // bit patterns that cannot occur with valid THUMB encoding on NDS.
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
    &Interpreter::unkThumb, &Interpreter::unkThumb,
};

// ====================================================================
// Condition code evaluation table
// Indexed by: ((opcode >> 24) & 0xF0) | (cpsr >> 28)
// Upper 4 bits = condition code (0-15), lower 4 bits = NZCV flags
// 0 = condition false (skip), 1 = condition true (execute),
// 2 = reserved (unconditional/BLX)
// ====================================================================
DISPATCH_TABLE
const uint8_t Interpreter::condition[0x100] = {
    // EQ: Z set (0x0n where n = NZCV)
    // Z is bit 30 of CPSR, which maps to bit 1 of the 4-bit flag nibble
    0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1, // EQ: execute if Z=1 (flag bit 1 set)
    1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0, // NE: execute if Z=0
    0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1, // CS: execute if C=1 (flag bit 2 set)
    1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0, // CC: execute if C=0
    0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1, // MI: execute if N=1 (flag bit 3 set)
    1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0, // PL: execute if N=0
    0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1, // VS: execute if V=1 (flag bit 0 set)
    1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0, // VC: execute if V=0
    // HI: C=1 and Z=0 -> flag bits: C=1(bit2), Z=0(bit1) -> execute when C set AND Z clear
    0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,1, // HI
    1,1,1,1,1,0,1,0,1,1,1,1,1,0,1,0, // LS: C=0 or Z=1
    // GE: N=V
    1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1, // GE: N==V
    0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0, // LT: N!=V
    // GT: Z=0 and N=V
    0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1, // GT
    1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0, // LE: Z=1 or N!=V
    // AL: always execute
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // AL
    // NV/reserved: used for BLX (value=2 signals handleReserved)
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // NV/reserved
};

// ====================================================================
// Bit count (popcount) lookup table for register lists
// Used by LDM/STM to count how many registers are in the list
// Indexed by an 8-bit value; returns number of set bits.
// Two lookups per 16-bit register list (high byte and low byte).
// ====================================================================
DISPATCH_TABLE
const uint8_t Interpreter::bitCount[0x100] = {
    0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4, // 0x00-0x0F
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5, // 0x10-0x1F
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5, // 0x20-0x2F
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6, // 0x30-0x3F
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5, // 0x40-0x4F
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6, // 0x50-0x5F
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6, // 0x60-0x6F
    3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7, // 0x70-0x7F
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5, // 0x80-0x8F
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6, // 0x90-0x9F
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6, // 0xA0-0xAF
    3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7, // 0xB0-0xBF
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6, // 0xC0-0xCF
    3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7, // 0xD0-0xDF
    3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7, // 0xE0-0xEF
    4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8, // 0xF0-0xFF
};
