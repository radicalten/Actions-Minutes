// jit_ppc.cpp
// ARMv4/5 -> PowerPC/Gekko JIT Recompiler for NooDS on Wii
// Translates DS ARM9/ARM7 instructions to native Wii Gekko PPC instructions
// Falls back to interpreter for unhandled opcodes

#include "jit_ppc.h"
#include "core.h"
#include "interpreter.h"
#include "memory.h"
#include "defines.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <malloc.h>      // memalign on Wii/GCC

// ============================================================
// Wii/Gekko memory management for executable code
// ============================================================
extern "C" {
    #include <ogc/cache.h>   // DCFlushRange / ICInvalidateRange
}

// ============================================================
// PowerPC Instruction Encoding Helpers
// ============================================================
// All PPC instructions are 32-bit big-endian words.
// Gekko is a 32-bit PowerPC 750CXe derivative.
//
// Register allocation strategy:
//   r3  - r10  : ARM registers R0-R7  (volatile across calls, save/restore)
//   r14 - r20  : ARM registers R8-R14 (non-volatile, persistent)
//   r21        : ARM R15 (PC) - managed carefully
//   r22        : ARM CPSR
//   r23        : pointer to Interpreter struct (ARM state)
//   r24        : pointer to Core struct
//   r25        : scratch / temp
//   r26        : scratch / temp
//   r27        : ARM carry flag (extracted from CPSR bit 29)
//   r0         : linkage / scratch (PPC ABI)
//   r1         : stack pointer (PPC ABI, do not touch)
//   r2         : reserved (TOC on ELF, unused on Wii)
//   r11, r12   : scratch for codegen
// ============================================================

namespace JitPpc {

// ------------------------------------------------------------------
// PPC instruction word builders
// ------------------------------------------------------------------

// I-form: BL / B
static inline uint32_t ppc_b(int32_t offset, bool aa = false, bool lk = false) {
    return (18u << 26) | ((uint32_t)(offset & 0x3FFFFFC)) | (aa ? 2u : 0u) | (lk ? 1u : 0u);
}

// B-form: conditional branch
static inline uint32_t ppc_bc(uint8_t bo, uint8_t bi, int16_t offset, bool lk = false) {
    return (16u << 26) | ((uint32_t)(bo & 0x1F) << 21) |
           ((uint32_t)(bi & 0x1F) << 16) | ((uint32_t)(offset & 0xFFFC)) | (lk ? 1u : 0u);
}

// XL-form: BCLR / BCCTR
static inline uint32_t ppc_bclr(uint8_t bo, uint8_t bi, bool lk = false) {
    return (19u << 26) | ((uint32_t)(bo & 0x1F) << 21) |
           ((uint32_t)(bi & 0x1F) << 16) | (16u << 1) | (lk ? 1u : 0u);
}
static inline uint32_t ppc_bctr(bool lk = false) {
    return (19u << 26) | (20u << 21) | (528u << 1) | (lk ? 1u : 0u);
}

// D-form: addi, addis, ori, oris, andi., andis., lwz, stw, lbz, stb, lhz, sth, lha, etc.
static inline uint32_t ppc_addi(uint8_t rt, uint8_t ra, int16_t imm) {
    return (14u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) | (uint16_t)imm;
}
static inline uint32_t ppc_addis(uint8_t rt, uint8_t ra, int16_t imm) {
    return (15u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) | (uint16_t)imm;
}
static inline uint32_t ppc_ori(uint8_t ra, uint8_t rs, uint16_t imm) {
    return (24u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | imm;
}
static inline uint32_t ppc_oris(uint8_t ra, uint8_t rs, uint16_t imm) {
    return (25u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | imm;
}
static inline uint32_t ppc_andi(uint8_t ra, uint8_t rs, uint16_t imm) {
    return (28u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | imm;
}
static inline uint32_t ppc_andis(uint8_t ra, uint8_t rs, uint16_t imm) {
    return (29u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | imm;
}
static inline uint32_t ppc_xori(uint8_t ra, uint8_t rs, uint16_t imm) {
    return (26u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | imm;
}
static inline uint32_t ppc_xoris(uint8_t ra, uint8_t rs, uint16_t imm) {
    return (27u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | imm;
}
static inline uint32_t ppc_lwz(uint8_t rt, int16_t d, uint8_t ra) {
    return (32u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) | (uint16_t)d;
}
static inline uint32_t ppc_stw(uint8_t rs, int16_t d, uint8_t ra) {
    return (36u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | (uint16_t)d;
}
static inline uint32_t ppc_lbz(uint8_t rt, int16_t d, uint8_t ra) {
    return (34u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) | (uint16_t)d;
}
static inline uint32_t ppc_stb(uint8_t rs, int16_t d, uint8_t ra) {
    return (38u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | (uint16_t)d;
}
static inline uint32_t ppc_lhz(uint8_t rt, int16_t d, uint8_t ra) {
    return (40u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) | (uint16_t)d;
}
static inline uint32_t ppc_lha(uint8_t rt, int16_t d, uint8_t ra) {
    return (42u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) | (uint16_t)d;
}
static inline uint32_t ppc_sth(uint8_t rs, int16_t d, uint8_t ra) {
    return (44u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | (uint16_t)d;
}
static inline uint32_t ppc_cmpi(uint8_t crD, uint8_t ra, int16_t imm, bool l = false) {
    return (11u << 26) | ((uint32_t)(crD & 7) << 23) | (l ? (1u << 21) : 0u) |
           ((uint32_t)ra << 16) | (uint16_t)imm;
}
static inline uint32_t ppc_cmpli(uint8_t crD, uint8_t ra, uint16_t imm, bool l = false) {
    return (10u << 26) | ((uint32_t)(crD & 7) << 23) | (l ? (1u << 21) : 0u) |
           ((uint32_t)ra << 16) | imm;
}

// subfic: rt = imm - ra  (sets XER[CA])
static inline uint32_t ppc_subfic(uint8_t rt, uint8_t ra, int16_t imm) {
    return (8u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) | (uint16_t)imm;
}

// X-form
static inline uint32_t ppc_Xform(uint32_t op, uint8_t rt, uint8_t ra, uint8_t rb,
                                   uint32_t xop, bool rc = false) {
    return (op << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) |
           ((uint32_t)rb << 11) | (xop << 1) | (rc ? 1u : 0u);
}

// XO-form: add, subf, and, or, xor, mullw, divwu, divw, etc.
static inline uint32_t ppc_XOform(uint32_t op, uint8_t rt, uint8_t ra, uint8_t rb,
                                    bool oe, uint32_t xop, bool rc = false) {
    return (op << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) |
           ((uint32_t)rb << 11) | (oe ? (1u << 10) : 0u) | (xop << 1) | (rc ? 1u : 0u);
}

// Common XO shortcuts
static inline uint32_t ppc_add(uint8_t rt, uint8_t ra, uint8_t rb, bool rc = false) {
    return ppc_XOform(31, rt, ra, rb, false, 266, rc);
}
static inline uint32_t ppc_addo(uint8_t rt, uint8_t ra, uint8_t rb, bool rc = false) {
    return ppc_XOform(31, rt, ra, rb, true, 266, rc);
}
static inline uint32_t ppc_addc(uint8_t rt, uint8_t ra, uint8_t rb, bool rc = false) {
    return ppc_XOform(31, rt, ra, rb, false, 10, rc);
}
static inline uint32_t ppc_adde(uint8_t rt, uint8_t ra, uint8_t rb, bool rc = false) {
    return ppc_XOform(31, rt, ra, rb, false, 138, rc);
}
static inline uint32_t ppc_subf(uint8_t rt, uint8_t ra, uint8_t rb, bool rc = false) {
    // subf rt, ra, rb => rt = rb - ra
    return ppc_XOform(31, rt, ra, rb, false, 40, rc);
}
static inline uint32_t ppc_subfc(uint8_t rt, uint8_t ra, uint8_t rb, bool rc = false) {
    return ppc_XOform(31, rt, ra, rb, false, 8, rc);
}
static inline uint32_t ppc_subfe(uint8_t rt, uint8_t ra, uint8_t rb, bool rc = false) {
    return ppc_XOform(31, rt, ra, rb, false, 136, rc);
}
static inline uint32_t ppc_subfme(uint8_t rt, uint8_t ra, bool rc = false) {
    return ppc_XOform(31, rt, ra, 0, false, 232, rc);
}
static inline uint32_t ppc_neg(uint8_t rt, uint8_t ra, bool rc = false) {
    return ppc_XOform(31, rt, ra, 0, false, 104, rc);
}
static inline uint32_t ppc_mullw(uint8_t rt, uint8_t ra, uint8_t rb, bool rc = false) {
    return ppc_XOform(31, rt, ra, rb, false, 235, rc);
}
static inline uint32_t ppc_mulhw(uint8_t rt, uint8_t ra, uint8_t rb, bool rc = false) {
    return ppc_XOform(31, rt, ra, rb, false, 75, rc);
}
static inline uint32_t ppc_mulhwu(uint8_t rt, uint8_t ra, uint8_t rb, bool rc = false) {
    return ppc_XOform(31, rt, ra, rb, false, 11, rc);
}
static inline uint32_t ppc_divwu(uint8_t rt, uint8_t ra, uint8_t rb, bool rc = false) {
    return ppc_XOform(31, rt, ra, rb, false, 459, rc);
}
static inline uint32_t ppc_divw(uint8_t rt, uint8_t ra, uint8_t rb, bool rc = false) {
    return ppc_XOform(31, rt, ra, rb, false, 491, rc);
}

// X-form logical
static inline uint32_t ppc_and(uint8_t ra, uint8_t rs, uint8_t rb, bool rc = false) {
    return ppc_Xform(31, rs, ra, rb, 28, rc);
}
static inline uint32_t ppc_or(uint8_t ra, uint8_t rs, uint8_t rb, bool rc = false) {
    return ppc_Xform(31, rs, ra, rb, 444, rc);
}
static inline uint32_t ppc_xor(uint8_t ra, uint8_t rs, uint8_t rb, bool rc = false) {
    return ppc_Xform(31, rs, ra, rb, 316, rc);
}
static inline uint32_t ppc_andc(uint8_t ra, uint8_t rs, uint8_t rb, bool rc = false) {
    return ppc_Xform(31, rs, ra, rb, 60, rc);
}
static inline uint32_t ppc_nor(uint8_t ra, uint8_t rs, uint8_t rb, bool rc = false) {
    return ppc_Xform(31, rs, ra, rb, 124, rc);
}
static inline uint32_t ppc_eqv(uint8_t ra, uint8_t rs, uint8_t rb, bool rc = false) {
    return ppc_Xform(31, rs, ra, rb, 284, rc);
}
static inline uint32_t ppc_nand(uint8_t ra, uint8_t rs, uint8_t rb, bool rc = false) {
    return ppc_Xform(31, rs, ra, rb, 476, rc);
}
static inline uint32_t ppc_orc(uint8_t ra, uint8_t rs, uint8_t rb, bool rc = false) {
    return ppc_Xform(31, rs, ra, rb, 412, rc);
}

// MR = OR rs,rs,rs
static inline uint32_t ppc_mr(uint8_t ra, uint8_t rs) {
    return ppc_or(ra, rs, rs);
}

// NOP = ORI r0,r0,0
static inline uint32_t ppc_nop() {
    return ppc_ori(0, 0, 0);
}

// Shift/rotate
static inline uint32_t ppc_slw(uint8_t ra, uint8_t rs, uint8_t rb, bool rc = false) {
    return ppc_Xform(31, rs, ra, rb, 24, rc);
}
static inline uint32_t ppc_srw(uint8_t ra, uint8_t rs, uint8_t rb, bool rc = false) {
    return ppc_Xform(31, rs, ra, rb, 536, rc);
}
static inline uint32_t ppc_sraw(uint8_t ra, uint8_t rs, uint8_t rb, bool rc = false) {
    return ppc_Xform(31, rs, ra, rb, 792, rc);
}

// M-form: rlwinm, rlwimi, rlwnm
static inline uint32_t ppc_rlwinm(uint8_t ra, uint8_t rs, uint8_t sh,
                                    uint8_t mb, uint8_t me, bool rc = false) {
    return (21u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
           ((uint32_t)sh << 11) | ((uint32_t)mb << 6) | ((uint32_t)me << 1) | (rc ? 1u : 0u);
}
static inline uint32_t ppc_rlwimi(uint8_t ra, uint8_t rs, uint8_t sh,
                                    uint8_t mb, uint8_t me, bool rc = false) {
    return (20u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
           ((uint32_t)sh << 11) | ((uint32_t)mb << 6) | ((uint32_t)me << 1) | (rc ? 1u : 0u);
}
static inline uint32_t ppc_rlwnm(uint8_t ra, uint8_t rs, uint8_t rb,
                                   uint8_t mb, uint8_t me, bool rc = false) {
    return (23u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
           ((uint32_t)rb << 11) | ((uint32_t)mb << 6) | ((uint32_t)me << 1) | (rc ? 1u : 0u);
}

// srawi
static inline uint32_t ppc_srawi(uint8_t ra, uint8_t rs, uint8_t sh, bool rc = false) {
    return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) |
           ((uint32_t)sh << 11) | (824u << 1) | (rc ? 1u : 0u);
}

// cntlzw
static inline uint32_t ppc_cntlzw(uint8_t ra, uint8_t rs, bool rc = false) {
    return ppc_Xform(31, rs, ra, 0, 26, rc);
}

// compare
static inline uint32_t ppc_cmp(uint8_t crD, uint8_t ra, uint8_t rb) {
    return (31u << 26) | ((uint32_t)(crD & 7) << 23) | ((uint32_t)ra << 16) |
           ((uint32_t)rb << 11) | (0u << 1);
}
static inline uint32_t ppc_cmpl(uint8_t crD, uint8_t ra, uint8_t rb) {
    return (31u << 26) | ((uint32_t)(crD & 7) << 23) | ((uint32_t)ra << 16) |
           ((uint32_t)rb << 11) | (32u << 1);
}

// mtspr / mfspr
static inline uint32_t ppc_mtspr(uint16_t spr, uint8_t rs) {
    uint8_t sprLo = spr & 0x1F;
    uint8_t sprHi = (spr >> 5) & 0x1F;
    return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)sprLo << 16) |
           ((uint32_t)sprHi << 11) | (467u << 1);
}
static inline uint32_t ppc_mfspr(uint8_t rt, uint16_t spr) {
    uint8_t sprLo = spr & 0x1F;
    uint8_t sprHi = (spr >> 5) & 0x1F;
    return (31u << 26) | ((uint32_t)rt << 21) | ((uint32_t)sprLo << 16) |
           ((uint32_t)sprHi << 11) | (339u << 1);
}

// mtctr / mfctr
static inline uint32_t ppc_mtctr(uint8_t rs) { return ppc_mtspr(9, rs); }
static inline uint32_t ppc_mfctr(uint8_t rt) { return ppc_mfspr(rt, 9); }
static inline uint32_t ppc_mtlr(uint8_t rs)  { return ppc_mtspr(8, rs); }
static inline uint32_t ppc_mflr(uint8_t rt)  { return ppc_mfspr(rt, 8); }

// XER SPR = 1
static inline uint32_t ppc_mtxer(uint8_t rs) { return ppc_mtspr(1, rs); }
static inline uint32_t ppc_mfxer(uint8_t rt) { return ppc_mfspr(rt, 1); }

// mfcr / mtcrf
static inline uint32_t ppc_mfcr(uint8_t rt) {
    return (31u << 26) | ((uint32_t)rt << 21) | (19u << 1);
}
static inline uint32_t ppc_mtcrf(uint8_t fxm, uint8_t rs) {
    return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)(fxm & 0xFF) << 12) | (144u << 1);
}

// Sync/isync
static inline uint32_t ppc_isync() {
    return (19u << 26) | (150u << 1);
}
static inline uint32_t ppc_sync() {
    return (31u << 26) | (598u << 1);
}
static inline uint32_t ppc_eieio() {
    return (31u << 26) | (854u << 1);
}

// lwzx, lbzx, lhzx, lhax, stwx, stbx, sthx
static inline uint32_t ppc_lwzx(uint8_t rt, uint8_t ra, uint8_t rb) {
    return ppc_Xform(31, rt, ra, rb, 23);
}
static inline uint32_t ppc_lbzx(uint8_t rt, uint8_t ra, uint8_t rb) {
    return ppc_Xform(31, rt, ra, rb, 87);
}
static inline uint32_t ppc_lhzx(uint8_t rt, uint8_t ra, uint8_t rb) {
    return ppc_Xform(31, rt, ra, rb, 279);
}
static inline uint32_t ppc_lhax(uint8_t rt, uint8_t ra, uint8_t rb) {
    return ppc_Xform(31, rt, ra, rb, 343);
}
static inline uint32_t ppc_stwx(uint8_t rs, uint8_t ra, uint8_t rb) {
    return ppc_Xform(31, rs, ra, rb, 151);
}
static inline uint32_t ppc_stbx(uint8_t rs, uint8_t ra, uint8_t rb) {
    return ppc_Xform(31, rs, ra, rb, 215);
}
static inline uint32_t ppc_sthx(uint8_t rs, uint8_t ra, uint8_t rb) {
    return ppc_Xform(31, rs, ra, rb, 407);
}

// stwu / stmw / lmw
static inline uint32_t ppc_stwu(uint8_t rs, int16_t d, uint8_t ra) {
    return (37u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | (uint16_t)d;
}
static inline uint32_t ppc_lwzu(uint8_t rt, int16_t d, uint8_t ra) {
    return (33u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) | (uint16_t)d;
}

// blr
static inline uint32_t ppc_blr() { return ppc_bclr(20, 0); }

// extsb / extsh
static inline uint32_t ppc_extsb(uint8_t ra, uint8_t rs, bool rc = false) {
    return ppc_Xform(31, rs, ra, 0, 954, rc);
}
static inline uint32_t ppc_extsh(uint8_t ra, uint8_t rs, bool rc = false) {
    return ppc_Xform(31, rs, ra, 0, 922, rc);
}

// ============================================================
// Load a 32-bit immediate into a register (1 or 2 instructions)
// ============================================================
static int emit_li32(uint32_t* out, uint8_t rt, uint32_t imm) {
    uint16_t lo = (uint16_t)(imm & 0xFFFF);
    uint16_t hi = (uint16_t)(imm >> 16);
    if (hi == 0) {
        out[0] = ppc_addi(rt, 0, (int16_t)lo);
        return 1;
    } else if (lo == 0) {
        out[0] = ppc_addis(rt, 0, (int16_t)hi);
        return 1;
    } else {
        out[0] = ppc_addis(rt, 0, (int16_t)hi);
        out[1] = ppc_ori(rt, rt, lo);
        return 2;
    }
}

// ============================================================
// ARM -> PPC register mapping
// ============================================================
static const uint8_t PPC_ARM_R0  = 3;
static const uint8_t PPC_ARM_R1  = 4;
static const uint8_t PPC_ARM_R2  = 5;
static const uint8_t PPC_ARM_R3  = 6;
static const uint8_t PPC_ARM_R4  = 7;
static const uint8_t PPC_ARM_R5  = 8;
static const uint8_t PPC_ARM_R6  = 9;
static const uint8_t PPC_ARM_R7  = 10;
static const uint8_t PPC_ARM_R8  = 14;
static const uint8_t PPC_ARM_R9  = 15;
static const uint8_t PPC_ARM_R10 = 16;
static const uint8_t PPC_ARM_R11 = 17;
static const uint8_t PPC_ARM_R12 = 18;
static const uint8_t PPC_ARM_R13 = 19;  // SP
static const uint8_t PPC_ARM_R14 = 20;  // LR
static const uint8_t PPC_ARM_R15 = 21;  // PC
static const uint8_t PPC_CPSR    = 22;
static const uint8_t PPC_INTERP  = 23;
static const uint8_t PPC_CORE    = 24;
static const uint8_t PPC_TMP0    = 25;
static const uint8_t PPC_TMP1    = 26;
static const uint8_t PPC_TMP2    = 11;
static const uint8_t PPC_TMP3    = 12;

static const uint8_t ARM_TO_PPC[16] = {
    PPC_ARM_R0,  PPC_ARM_R1,  PPC_ARM_R2,  PPC_ARM_R3,
    PPC_ARM_R4,  PPC_ARM_R5,  PPC_ARM_R6,  PPC_ARM_R7,
    PPC_ARM_R8,  PPC_ARM_R9,  PPC_ARM_R10, PPC_ARM_R11,
    PPC_ARM_R12, PPC_ARM_R13, PPC_ARM_R14, PPC_ARM_R15,
};

// ============================================================
// ARM Condition Code definitions
// ============================================================
#define CPSR_N  (1u << 31)
#define CPSR_Z  (1u << 30)
#define CPSR_C  (1u << 29)
#define CPSR_V  (1u << 28)
#define CPSR_T  (1u << 5)
#define CPSR_I  (1u << 7)
#define CPSR_F  (1u << 6)
#define CPSR_MODE_MASK 0x1Fu

// ============================================================
// JIT Code Buffer
// ============================================================
static const size_t JIT_CODE_SIZE   = 4 * 1024 * 1024; // 4 MB code cache
static const size_t JIT_MAX_INSTRS  = JIT_CODE_SIZE / 4;
static const size_t MAX_BLOCK_SIZE  = 256;  // max ARM instructions per block
static const size_t MAX_PPC_PER_ARM = 64;   // max PPC words per ARM instruction

static uint32_t* codeBuffer    = nullptr;
static size_t    codeBufferPos = 0;

// ============================================================
// Block cache
// ============================================================
struct JitBlock {
    uint32_t  armPC;
    uint32_t* ppcCode;
    uint32_t  ppcWords;
    uint32_t  armInstrs;
    bool      thumb;
    bool      valid;
};

static const size_t BLOCK_CACHE_SIZE = 8192;
static JitBlock blockCache[BLOCK_CACHE_SIZE];
static uint32_t blockCacheGen = 0;

static inline size_t hashPC(uint32_t pc) {
    return (pc >> 1) & (BLOCK_CACHE_SIZE - 1);
}

// ============================================================
// flushJitCache — not static; declared extern in jit_ppc.h
// ============================================================
void flushJitCache() {
    codeBufferPos = 0;
    for (size_t i = 0; i < BLOCK_CACHE_SIZE; i++)
        blockCache[i].valid = false;
    blockCacheGen++;
}

// ============================================================
// Code emitter context
// ============================================================
struct EmitCtx {
    uint32_t* base;
    uint32_t* cur;
    size_t    capacity;

    bool thumb;
    bool arm7;
    uint32_t armPC;

    Interpreter* interp;
    Core*        core;

    void emit(uint32_t word) {
        if ((size_t)(cur - base) < capacity)
            *cur++ = word;
    }
    size_t size() const { return (size_t)(cur - base); }
};

// ============================================================
// Flush caches for newly written code
// ============================================================
static void flushCaches(uint32_t* start, size_t words) {
    DCFlushRange(start, words * 4);
    ICInvalidateRange(start, words * 4);
}

// ============================================================
// Condition check helpers
// ============================================================

// Setup NZCV in CR0 from PPC_CPSR before a conditional instruction.
// After this:
//   CR0[LT]=V (CPSR bit28), CR0[GT]=C (bit29), CR0[EQ]=Z (bit30), CR0[SO]=N (bit31)
// Clobbers PPC_TMP0.
static void emit_setupCondFlags(EmitCtx& ctx) {
    // Isolate CPSR bits 28-31 (VCZN) into TMP0[28:31]
    ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_CPSR, 0, 28, 31));
    // mtcrf 0x80 writes TMP0[28:31] -> CR[28:31] = CR0
    ctx.emit(ppc_mtcrf(0x80, PPC_TMP0));
}

// BO/BI condition mapping after emit_setupCondFlags:
//   CR0[LT]=V (bit 0), CR0[GT]=C (bit 1), CR0[EQ]=Z (bit 2), CR0[SO]=N (bit 3)
struct CondBranch { uint8_t bo; uint8_t bi; bool valid; };

static CondBranch armCondToPpc(uint8_t cond) {
    // BI numbering for CR0: LT=0, GT=1, EQ=2, SO=3
    switch (cond) {
        case 0:  return {12, 2, true};  // EQ: Z=1
        case 1:  return {4,  2, true};  // NE: Z=0
        case 2:  return {12, 1, true};  // CS: C=1 (GT slot)
        case 3:  return {4,  1, true};  // CC: C=0
        case 4:  return {12, 3, true};  // MI: N=1 (SO slot)
        case 5:  return {4,  3, true};  // PL: N=0
        case 6:  return {12, 0, true};  // VS: V=1 (LT slot)
        case 7:  return {4,  0, true};  // VC: V=0
        case 14: return {20, 0, true};  // AL: always (BO=20)
        // HI/LS/GE/LT/GT/LE need compound logic -> fall back
        default: return {0,  0, false};
    }
}

// ============================================================
// Struct offsets (computed once at init)
// ============================================================
static size_t off_registersUsr = 0;
static size_t off_cpsr         = 0;
static size_t off_cycles       = 0;
static size_t off_halted       = 0;
static size_t off_pipeline     = 0;
static size_t off_pcData       = 0;

// ============================================================
// Stack frame constants
// ============================================================
static const int FRAME_SIZE     = 128;
static const int FRAME_LR       = 124;
static const int FRAME_R14      = 8;    // saved r14..r26 at bytes 8..60
static const int FRAME_REGSYNC  = 64;   // 17 words (ARM R0-R15 + CPSR) at 64..131
static const int FRAME_VOLATILE = 68;   // 8 words (ARM R0-R7) at 68..99 — overlaps REGSYNC intentionally

// ============================================================
// Public C-callable helpers (no internal linkage)
// ============================================================
extern "C" {

void JitPpc_syncToInterp(Interpreter* interp, uint32_t* regs) {
    uint32_t* usr    = (uint32_t*)((uint8_t*)interp + off_registersUsr);
    uint32_t* cpsrPtr = (uint32_t*)((uint8_t*)interp + off_cpsr);
    for (int i = 0; i < 16; i++)
        usr[i] = regs[i];
    *cpsrPtr = regs[16];
}

void JitPpc_syncFromInterp(Interpreter* interp, uint32_t* regs) {
    const uint32_t* usr    = (const uint32_t*)((uint8_t*)interp + off_registersUsr);
    const uint32_t* cpsrPtr = (const uint32_t*)((uint8_t*)interp + off_cpsr);
    for (int i = 0; i < 16; i++)
        regs[i] = usr[i];
    regs[16] = *cpsrPtr;
}

int JitPpc_interpFallback(Interpreter* interp) {
    return interp->jitRunOpcode();
}

uint32_t JitPpc_memRead32(Core* core, bool arm7, uint32_t addr) {
    return core->memory.read<uint32_t>(arm7, addr);
}
uint16_t JitPpc_memRead16(Core* core, bool arm7, uint32_t addr) {
    return core->memory.read<uint16_t>(arm7, addr);
}
uint8_t JitPpc_memRead8(Core* core, bool arm7, uint32_t addr) {
    return core->memory.read<uint8_t>(arm7, addr);
}
void JitPpc_memWrite32(Core* core, bool arm7, uint32_t addr, uint32_t val) {
    core->memory.write<uint32_t>(arm7, addr, val);
}
void JitPpc_memWrite16(Core* core, bool arm7, uint32_t addr, uint16_t val) {
    core->memory.write<uint16_t>(arm7, addr, val);
}
void JitPpc_memWrite8(Core* core, bool arm7, uint32_t addr, uint8_t val) {
    core->memory.write<uint8_t>(arm7, addr, val);
}

void JitPpc_addCycles(Interpreter* interp, uint32_t cycles) {
    uint32_t* cyc = (uint32_t*)((uint8_t*)interp + off_cycles);
    *cyc += cycles;
}

} // extern "C"

// ============================================================
// Emit an indirect call to a C function via CTR
// Clobbers r0, r12
// ============================================================
static void emit_call(EmitCtx& ctx, void* fnAddr) {
    uint32_t addr = (uint32_t)(uintptr_t)fnAddr;
    int n = emit_li32(ctx.cur, 12, addr);
    ctx.cur += n;
    ctx.emit(ppc_mtctr(12));
    ctx.emit(ppc_bctr(true));   // bctrl
}

// ============================================================
// Block prologue / epilogue
// ============================================================
static void emit_prologue(EmitCtx& ctx, Interpreter* interp, Core* core) {
    ctx.emit(ppc_stwu(1, -FRAME_SIZE, 1));
    ctx.emit(ppc_mflr(0));
    ctx.emit(ppc_stw(0, FRAME_LR, 1));
    for (int r = 14; r <= 26; r++)
        ctx.emit(ppc_stw(r, FRAME_R14 + (r - 14) * 4, 1));

    int n = emit_li32(ctx.cur, PPC_INTERP, (uint32_t)(uintptr_t)interp);
    ctx.cur += n;
    n = emit_li32(ctx.cur, PPC_CORE, (uint32_t)(uintptr_t)core);
    ctx.cur += n;
}

static void emit_epilogue(EmitCtx& ctx) {
    for (int r = 14; r <= 26; r++)
        ctx.emit(ppc_lwz(r, FRAME_R14 + (r - 14) * 4, 1));
    ctx.emit(ppc_lwz(0, FRAME_LR, 1));
    ctx.emit(ppc_mtlr(0));
    ctx.emit(ppc_addi(1, 1, FRAME_SIZE));
    ctx.emit(ppc_blr());
}

// ============================================================
// Sync helpers emitted inline
// ============================================================
static void emit_syncToInterp(EmitCtx& ctx) {
    // Spill all ARM regs + CPSR into stack frame scratch area
    for (int i = 0; i < 16; i++)
        ctx.emit(ppc_stw(ARM_TO_PPC[i], FRAME_REGSYNC + i * 4, 1));
    ctx.emit(ppc_stw(PPC_CPSR, FRAME_REGSYNC + 16 * 4, 1));

    // Call JitPpc_syncToInterp(interp, &frame[FRAME_REGSYNC])
    ctx.emit(ppc_mr(3, PPC_INTERP));
    ctx.emit(ppc_addi(4, 1, FRAME_REGSYNC));
    emit_call(ctx, (void*)JitPpc_syncToInterp);
}

static void emit_syncFromInterp(EmitCtx& ctx) {
    // Call JitPpc_syncFromInterp(interp, &frame[FRAME_REGSYNC])
    ctx.emit(ppc_mr(3, PPC_INTERP));
    ctx.emit(ppc_addi(4, 1, FRAME_REGSYNC));
    emit_call(ctx, (void*)JitPpc_syncFromInterp);

    // Reload ARM regs + CPSR from frame
    for (int i = 0; i < 16; i++)
        ctx.emit(ppc_lwz(ARM_TO_PPC[i], FRAME_REGSYNC + i * 4, 1));
    ctx.emit(ppc_lwz(PPC_CPSR, FRAME_REGSYNC + 16 * 4, 1));
}

// ============================================================
// CPSR flag update helpers
// ============================================================

// Update N and Z in PPC_CPSR from `result` register.
// IBM bit notation: bit0=MSB. C++ bit31=IBM bit0, C++ bit30=IBM bit1, etc.
// CPSR N = C++ bit31 = IBM bit0
// CPSR Z = C++ bit30 = IBM bit1
static void emit_updateNZ(EmitCtx& ctx, uint8_t result) {
    // Clear N (IBM 0) and Z (IBM 1) = C++ bits 31, 30
    // rlwinm with IBM mb=2,me=31 keeps IBM bits 2..31 = C++ bits 29..0
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 2, 31));

    // Insert N: copy result[IBM0]=C++bit31 into CPSR[IBM0]
    ctx.emit(ppc_rlwimi(PPC_CPSR, result, 0, 0, 0));

    // Compute Z: cmpwi result,0 sets CR0[EQ]
    ctx.emit(ppc_cmpi(0, result, 0));
    ctx.emit(ppc_mfcr(PPC_TMP0));
    // CR0[EQ] = C++ bit29 of mfcr value.
    // We need it at CPSR C++ bit30.
    // Rotate left by 1: C++bit29 -> C++bit30, then mask.
    ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_TMP0, 1, 30, 30));
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP0));
}

// Update C from XER[CA] (set by addc/adde/subfc/subfe)
// XER[CA] = C++ bit29 of XER.
// CPSR C  = C++ bit29 of CPSR.
static void emit_updateC_fromXER(EmitCtx& ctx) {
    ctx.emit(ppc_mfxer(PPC_TMP0));
    // Isolate XER[CA] at C++ bit29
    ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_TMP0, 0, 2, 2));
    // Clear CPSR[C] (IBM bit2 = C++ bit29):
    // rlwinm IBM mb=3,me=1 wraps: keeps IBM 3..31 and 0..1, clears IBM 2
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 3, 1));
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP0));
}

// Update V from XER[OV] (set by addo/subfo)
// XER[OV] = C++ bit30 of XER.
// CPSR V  = C++ bit28.
static void emit_updateV_fromXER(EmitCtx& ctx) {
    ctx.emit(ppc_mfxer(PPC_TMP0));
    // XER[OV] at C++ bit30 -> shift right 2 -> C++ bit28
    ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_TMP0, 30, 29, 29));
    // Clear CPSR[V] (IBM bit3 = C++ bit28):
    // rlwinm IBM mb=4,me=2 wraps: keeps IBM 4..31,0..2, clears IBM 3
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 4, 2));
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP0));
}

// Full NZCV update after an add with carry/overflow tracking
// Requires: operation used addc/adde (sets XER[CA]) and addo (sets XER[OV])
static void emit_updateNZCV_add(EmitCtx& ctx, uint8_t result) {
    emit_updateNZ(ctx, result);
    emit_updateC_fromXER(ctx);
    emit_updateV_fromXER(ctx);
}

// Full NZCV update after a subtract
// ARM SUB C = NOT borrow. PPC subfc sets XER[CA] = NOT borrow. ✓
static void emit_updateNZCV_sub(EmitCtx& ctx, uint8_t result) {
    emit_updateNZ(ctx, result);
    emit_updateC_fromXER(ctx);
    emit_updateV_fromXER(ctx);
}

// Update C from PPC_TMP1 bit0 (carry out of shift)
static void emit_updateC_fromTMP1(EmitCtx& ctx) {
    // PPC_TMP1 bit0 -> CPSR C++ bit29
    ctx.emit(ppc_rlwinm(PPC_TMP2, PPC_TMP1, 29, 29, 29));
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 3, 1));  // clear CPSR[C]
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP2));
}

// ============================================================
// ARM Immediate operand decoder
// ============================================================
static uint32_t armImm(uint32_t opcode) {
    uint32_t imm = opcode & 0xFF;
    uint32_t rot = ((opcode >> 8) & 0xF) * 2;
    if (rot == 0) return imm;
    return (imm >> rot) | (imm << (32 - rot));
}

// ============================================================
// Shift / rotate helpers
// ============================================================

static void emit_lsl_imm(EmitCtx& ctx, uint8_t dst, uint8_t src, uint8_t imm,
                          bool setCarry = false) {
    if (imm == 0) {
        if (dst != src) ctx.emit(ppc_mr(dst, src));
        if (setCarry) {
            // carry unchanged; extract C from CPSR
            ctx.emit(ppc_rlwinm(PPC_TMP1, PPC_CPSR, 3, 31, 31));
        }
    } else if (imm < 32) {
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, imm, 31, 31));
        ctx.emit(ppc_rlwinm(dst, src, imm, 0, 31 - imm));
    } else if (imm == 32) {
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, 0, 31, 31));
        ctx.emit(ppc_addi(dst, 0, 0));
    } else {
        if (setCarry) ctx.emit(ppc_addi(PPC_TMP1, 0, 0));
        ctx.emit(ppc_addi(dst, 0, 0));
    }
}

static void emit_lsr_imm(EmitCtx& ctx, uint8_t dst, uint8_t src, uint8_t imm,
                          bool setCarry = false) {
    if (imm == 0 || imm == 32) {
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, 1, 31, 31));
        ctx.emit(ppc_addi(dst, 0, 0));
    } else if (imm < 32) {
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, 33 - imm, 31, 31));
        ctx.emit(ppc_rlwinm(dst, src, 32 - imm, imm, 31));
    } else {
        if (setCarry) ctx.emit(ppc_addi(PPC_TMP1, 0, 0));
        ctx.emit(ppc_addi(dst, 0, 0));
    }
}

static void emit_asr_imm(EmitCtx& ctx, uint8_t dst, uint8_t src, uint8_t imm,
                          bool setCarry = false) {
    if (imm == 0 || imm >= 32) {
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, 1, 31, 31));
        ctx.emit(ppc_srawi(dst, src, 31));
    } else {
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, 33 - imm, 31, 31));
        ctx.emit(ppc_srawi(dst, src, imm));
    }
}

static void emit_ror_imm(EmitCtx& ctx, uint8_t dst, uint8_t src, uint8_t imm,
                          bool setCarry = false) {
    if (imm == 0) {
        // RRX: rotate right 1 through carry
        ctx.emit(ppc_rlwinm(PPC_TMP1, PPC_CPSR, 3, 31, 31)); // old carry -> TMP1 bit0
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP0, src, 0, 31, 31));   // new carry = bit0 of src
        ctx.emit(ppc_rlwinm(dst, src, 31, 1, 31));             // dst = src >> 1
        ctx.emit(ppc_rlwimi(dst, PPC_TMP1, 31, 0, 0));         // dst |= carry << 31
        if (setCarry)
            ctx.emit(ppc_mr(PPC_TMP1, PPC_TMP0));
    } else {
        imm &= 31;
        if (imm == 0) imm = 32; // ror by 32 = identity but carry = bit31
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, 33 - imm, 31, 31));
        ctx.emit(ppc_rlwinm(dst, src, 32 - imm, 0, 31));
    }
}

static void emit_lsl_reg(EmitCtx& ctx, uint8_t dst, uint8_t src, uint8_t shiftReg,
                          bool setCarry = false) {
    if (setCarry) ctx.emit(ppc_addi(PPC_TMP1, 0, 0)); // simplified: carry=0
    ctx.emit(ppc_slw(dst, src, shiftReg));
}

static void emit_lsr_reg(EmitCtx& ctx, uint8_t dst, uint8_t src, uint8_t shiftReg,
                          bool setCarry = false) {
    if (setCarry) ctx.emit(ppc_addi(PPC_TMP1, 0, 0));
    ctx.emit(ppc_srw(dst, src, shiftReg));
}

static void emit_asr_reg(EmitCtx& ctx, uint8_t dst, uint8_t src, uint8_t shiftReg,
                          bool setCarry = false) {
    if (setCarry) ctx.emit(ppc_addi(PPC_TMP1, 0, 0));
    ctx.emit(ppc_sraw(dst, src, shiftReg));
}

static void emit_ror_reg(EmitCtx& ctx, uint8_t dst, uint8_t src, uint8_t shiftReg,
                          bool setCarry = false) {
    // ror dst,src,shiftReg  =  rlwnm dst,src,(32-shiftReg),0,31
    // Compute (32 - shiftReg) using subfic (rt = 32 - ra)
    if (setCarry) ctx.emit(ppc_addi(PPC_TMP1, 0, 0));
    ctx.emit(ppc_subfic(PPC_TMP0, shiftReg, 32));   // TMP0 = 32 - shiftReg
    ctx.emit(ppc_rlwnm(dst, src, PPC_TMP0, 0, 31));
}

// ============================================================
// Shifter operand decoder
// ============================================================
static bool emit_shifterOp(EmitCtx& ctx, uint32_t opcode, uint8_t dstReg,
                            bool setCarry, bool& outCarryValid) {
    outCarryValid = setCarry;
    bool isImm = (opcode >> 25) & 1;

    if (isImm) {
        uint32_t val = armImm(opcode);
        int n = emit_li32(ctx.cur, dstReg, val);
        ctx.cur += n;
        if (setCarry) {
            uint32_t rot = ((opcode >> 8) & 0xF) * 2;
            if (rot != 0) {
                int n2 = emit_li32(ctx.cur, PPC_TMP1, val >> 31);
                ctx.cur += n2;
            } else {
                outCarryValid = false;
            }
        }
        return true;
    }

    uint8_t rm         = opcode & 0xF;
    uint8_t ppcRm      = ARM_TO_PPC[rm];
    bool    isRegShift = (opcode >> 4) & 1;
    uint8_t shiftType  = (opcode >> 5) & 3;

    if (!isRegShift) {
        uint8_t shiftAmt = (opcode >> 7) & 0x1F;
        switch (shiftType) {
            case 0: emit_lsl_imm(ctx, dstReg, ppcRm, shiftAmt, setCarry);                          break;
            case 1: emit_lsr_imm(ctx, dstReg, ppcRm, shiftAmt ? shiftAmt : 32, setCarry);          break;
            case 2: emit_asr_imm(ctx, dstReg, ppcRm, shiftAmt ? shiftAmt : 32, setCarry);          break;
            case 3: emit_ror_imm(ctx, dstReg, ppcRm, shiftAmt, setCarry);                          break;
        }
    } else {
        uint8_t rs    = (opcode >> 8) & 0xF;
        uint8_t ppcRs = ARM_TO_PPC[rs];
        // Use only low 8 bits of Rs
        ctx.emit(ppc_rlwinm(PPC_TMP2, ppcRs, 0, 24, 31));
        ctx.emit(ppc_mr(PPC_TMP3, ppcRm));
        switch (shiftType) {
            case 0: emit_lsl_reg(ctx, dstReg, PPC_TMP3, PPC_TMP2, setCarry); break;
            case 1: emit_lsr_reg(ctx, dstReg, PPC_TMP3, PPC_TMP2, setCarry); break;
            case 2: emit_asr_reg(ctx, dstReg, PPC_TMP3, PPC_TMP2, setCarry); break;
            case 3: emit_ror_reg(ctx, dstReg, PPC_TMP3, PPC_TMP2, setCarry); break;
        }
        outCarryValid = false; // variable-shift carry not fully tracked
    }
    return true;
}

// ============================================================
// Data processing
// ============================================================
enum ArmDpOp {
    DP_AND=0, DP_EOR=1, DP_SUB=2, DP_RSB=3,
    DP_ADD=4, DP_ADC=5, DP_SBC=6, DP_RSC=7,
    DP_TST=8, DP_TEQ=9, DP_CMP=10, DP_CMN=11,
    DP_ORR=12, DP_MOV=13, DP_BIC=14, DP_MVN=15
};

static bool emit_dataProc(EmitCtx& ctx, uint32_t opcode) {
    uint8_t cond  = (opcode >> 28) & 0xF;
    uint8_t dpOp  = (opcode >> 21) & 0xF;
    bool    setCC = (opcode >> 20) & 1;
    uint8_t rn    = (opcode >> 16) & 0xF;
    uint8_t rd    = (opcode >> 12) & 0xF;

    if (rd == 15 && setCC) return false; // CPSR restore
    if (rd == 15)          return false; // PC write - end block, complex

    uint8_t ppcRd = ARM_TO_PPC[rd];
    uint8_t ppcRn = ARM_TO_PPC[rn];

    // Condition gating
    size_t condBranchIdx = 0;
    bool   hasCondBranch = false;
    if (cond != 14) {
        emit_setupCondFlags(ctx);
        CondBranch cb = armCondToPpc(cond);
        if (!cb.valid) return false;
        if (cb.bo != 20) {
            condBranchIdx = ctx.size();
            hasCondBranch = true;
            uint8_t skipBO = (cb.bo == 12) ? 4 : 12;
            ctx.emit(ppc_bc(skipBO, cb.bi, 0));
        }
    }

    bool needCarry     = (dpOp == DP_ADC || dpOp == DP_SBC || dpOp == DP_RSC);
    bool needShiftCarry= setCC && (dpOp == DP_AND || dpOp == DP_EOR || dpOp == DP_TST ||
                                   dpOp == DP_TEQ || dpOp == DP_ORR || dpOp == DP_MOV ||
                                   dpOp == DP_BIC || dpOp == DP_MVN);
    bool carryValid = false;
    if (!emit_shifterOp(ctx, opcode, PPC_TMP0, needShiftCarry, carryValid))
        return false;

    // Carry into XER for ADC/SBC/RSC
    if (needCarry) {
        ctx.emit(ppc_rlwinm(PPC_TMP1, PPC_CPSR, 0, 2, 2)); // isolate CPSR[C] at C++ bit29
        ctx.emit(ppc_mtxer(PPC_TMP1));
    }

    bool testOnly  = (dpOp == DP_TST || dpOp == DP_TEQ ||
                      dpOp == DP_CMP || dpOp == DP_CMN);
    uint8_t resReg = testOnly ? PPC_TMP1 : ppcRd;

    switch ((ArmDpOp)dpOp) {
        case DP_AND: case DP_TST:
            ctx.emit(ppc_and(resReg, ppcRn, PPC_TMP0));
            break;
        case DP_EOR: case DP_TEQ:
            ctx.emit(ppc_xor(resReg, ppcRn, PPC_TMP0));
            break;
        case DP_SUB: case DP_CMP:
            // subfc: resReg = ppcRn - PPC_TMP0, sets XER[CA]=NOT borrow
            ctx.emit(ppc_subfc(resReg, PPC_TMP0, ppcRn));
            break;
        case DP_RSB:
            ctx.emit(ppc_subfc(resReg, ppcRn, PPC_TMP0));
            break;
        case DP_ADD: case DP_CMN:
            ctx.emit(ppc_addc(resReg, ppcRn, PPC_TMP0));
            break;
        case DP_ADC:
            ctx.emit(ppc_adde(resReg, ppcRn, PPC_TMP0));
            break;
        case DP_SBC:
            // ARM SBC: rd = rn - op2 - NOT(C)  =>  subfe rd, op2, rn  (uses XER[CA]=C)
            ctx.emit(ppc_subfe(resReg, PPC_TMP0, ppcRn));
            break;
        case DP_RSC:
            ctx.emit(ppc_subfe(resReg, ppcRn, PPC_TMP0));
            break;
        case DP_ORR:
            ctx.emit(ppc_or(resReg, ppcRn, PPC_TMP0));
            break;
        case DP_MOV:
            if (resReg != PPC_TMP0)
                ctx.emit(ppc_mr(resReg, PPC_TMP0));
            break;
        case DP_BIC:
            ctx.emit(ppc_andc(resReg, ppcRn, PPC_TMP0));
            break;
        case DP_MVN:
            ctx.emit(ppc_nor(resReg, PPC_TMP0, PPC_TMP0));
            break;
    }

    if (setCC) {
        switch ((ArmDpOp)dpOp) {
            case DP_ADD: case DP_CMN:
            case DP_ADC:
                emit_updateNZCV_add(ctx, resReg);
                break;
            case DP_SUB: case DP_CMP:
            case DP_RSB: case DP_SBC: case DP_RSC:
                emit_updateNZCV_sub(ctx, resReg);
                break;
            default:
                emit_updateNZ(ctx, resReg);
                if (carryValid)
                    emit_updateC_fromTMP1(ctx);
                break;
        }
    }

    if (hasCondBranch) {
        size_t endIdx    = ctx.size();
        int32_t off      = (int32_t)(endIdx - condBranchIdx) * 4;
        ctx.base[condBranchIdx] = (ctx.base[condBranchIdx] & ~0xFFFC) | (uint32_t)(off & 0xFFFC);
    }
    return true;
}

// ============================================================
// Branch instructions
// ============================================================
static bool emit_branch(EmitCtx& ctx, uint32_t opcode, uint32_t pc) {
    uint8_t cond = (opcode >> 28) & 0xF;

    // BX Rm
    if ((opcode & 0x0FFFFFF0) == 0x012FFF10) {
        uint8_t rm    = opcode & 0xF;
        if (rm == 15) return false;
        uint8_t ppcRm = ARM_TO_PPC[rm];

        size_t condBranchIdx = 0;
        bool   hasCondBranch = false;
        if (cond != 14) {
            emit_setupCondFlags(ctx);
            CondBranch cb = armCondToPpc(cond);
            if (!cb.valid) return false;
            if (cb.bo != 20) {
                condBranchIdx = ctx.size();
                hasCondBranch = true;
                ctx.emit(ppc_bc((cb.bo == 12) ? 4 : 12, cb.bi, 0));
            }
        }

        // Update CPSR T bit from Rm[0]
        ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 27, 25)); // clear T (IBM bit26=C++bit5)
        ctx.emit(ppc_rlwinm(PPC_TMP0, ppcRm, 5, 26, 26));     // Rm[0] -> bit5
        ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP0));
        ctx.emit(ppc_rlwinm(PPC_ARM_R15, ppcRm, 0, 0, 30));   // PC = Rm & ~1

        emit_syncToInterp(ctx);

        if (hasCondBranch) {
            size_t endIdx = ctx.size();
            int32_t off   = (int32_t)(endIdx - condBranchIdx) * 4;
            ctx.base[condBranchIdx] = (ctx.base[condBranchIdx] & ~0xFFFC) | (uint32_t)(off & 0xFFFC);
        }
        return true;
    }

    // B / BL
    if ((opcode & 0x0E000000) == 0x0A000000) {
        bool    isLink = (opcode >> 24) & 1;
        int32_t offset = (int32_t)(opcode << 8) >> 6;
        uint32_t target = pc + 8 + offset;

        size_t condBranchIdx = 0;
        bool   hasCondBranch = false;
        if (cond != 14) {
            emit_setupCondFlags(ctx);
            CondBranch cb = armCondToPpc(cond);
            if (!cb.valid) return false;
            if (cb.bo != 20) {
                condBranchIdx = ctx.size();
                hasCondBranch = true;
                ctx.emit(ppc_bc((cb.bo == 12) ? 4 : 12, cb.bi, 0));
            }
        }

        if (isLink) {
            int n = emit_li32(ctx.cur, PPC_ARM_R14, pc + 4);
            ctx.cur += n;
        }
        int n = emit_li32(ctx.cur, PPC_ARM_R15, target);
        ctx.cur += n;

        emit_syncToInterp(ctx);

        if (hasCondBranch) {
            size_t endIdx = ctx.size();
            int32_t off   = (int32_t)(endIdx - condBranchIdx) * 4;
            ctx.base[condBranchIdx] = (ctx.base[condBranchIdx] & ~0xFFFC) | (uint32_t)(off & 0xFFFC);
        }
        return true;
    }

    return false;
}

// ============================================================
// Load / Store (LDR, STR, LDRB, STRB)
// ============================================================
static bool emit_loadStore(EmitCtx& ctx, uint32_t opcode, uint32_t pc) {
    uint8_t cond    = (opcode >> 28) & 0xF;
    bool    isLoad  = (opcode >> 20) & 1;
    bool    isByte  = (opcode >> 22) & 1;
    bool    isUp    = (opcode >> 23) & 1;
    bool    preIdx  = (opcode >> 24) & 1;
    bool    wback   = (opcode >> 21) & 1;
    bool    immOff  = !((opcode >> 25) & 1);
    uint8_t rn      = (opcode >> 16) & 0xF;
    uint8_t rd      = (opcode >> 12) & 0xF;

    if (rd == 15 || rn == 15) return false;

    uint8_t ppcRn = ARM_TO_PPC[rn];
    uint8_t ppcRd = ARM_TO_PPC[rd];

    size_t condBranchIdx = 0;
    bool   hasCondBranch = false;
    if (cond != 14) {
        emit_setupCondFlags(ctx);
        CondBranch cb = armCondToPpc(cond);
        if (!cb.valid) return false;
        if (cb.bo != 20) {
            condBranchIdx = ctx.size();
            hasCondBranch = true;
            ctx.emit(ppc_bc((cb.bo == 12) ? 4 : 12, cb.bi, 0));
        }
    }

    // Compute offset -> PPC_TMP0
    if (immOff) {
        uint32_t offset = opcode & 0xFFF;
        int n = emit_li32(ctx.cur, PPC_TMP0, offset);
        ctx.cur += n;
    } else {
        uint8_t rm    = opcode & 0xF;
        uint8_t ppcRm = ARM_TO_PPC[rm];
        // Simple register offset (ignore shift encoding for now)
        ctx.emit(ppc_mr(PPC_TMP0, ppcRm));
    }

    // Effective address -> PPC_TMP1
    if (preIdx) {
        if (isUp)
            ctx.emit(ppc_add(PPC_TMP1, ppcRn, PPC_TMP0));
        else
            ctx.emit(ppc_subf(PPC_TMP1, PPC_TMP0, ppcRn));
    } else {
        ctx.emit(ppc_mr(PPC_TMP1, ppcRn));
    }

    // Save volatile ARM regs (PPC r3-r10) around the C call
    static const int VSAVE = FRAME_REGSYNC; // reuse sync area (safe: not active here)
    for (int i = 0; i < 8; i++)
        ctx.emit(ppc_stw(3 + i, VSAVE + i * 4, 1));

    ctx.emit(ppc_mr(3, PPC_CORE));
    ctx.emit(ppc_addi(4, 0, ctx.arm7 ? 1 : 0));
    ctx.emit(ppc_mr(5, PPC_TMP1));

    if (!isLoad)
        ctx.emit(ppc_mr(6, ppcRd));

    void* memFn;
    if (isLoad)
        memFn = isByte ? (void*)JitPpc_memRead8 : (void*)JitPpc_memRead32;
    else
        memFn = isByte ? (void*)JitPpc_memWrite8 : (void*)JitPpc_memWrite32;

    emit_call(ctx, memFn);

    // Stash return value before restoring volatile regs
    if (isLoad)
        ctx.emit(ppc_stw(3, VSAVE + 0 * 4, 1)); // overwrite slot 0 with result

    for (int i = 0; i < 8; i++)
        ctx.emit(ppc_lwz(3 + i, VSAVE + i * 4, 1));

    if (isLoad) {
        // Result was written to VSAVE+0; now it's in r3 after reload.
        // If ppcRd != r3 copy it across.
        if (ppcRd != 3)
            ctx.emit(ppc_mr(ppcRd, 3));
    }

    // Write-back
    if (!preIdx) {
        if (isUp)
            ctx.emit(ppc_add(ppcRn, ppcRn, PPC_TMP0));
        else
            ctx.emit(ppc_subf(ppcRn, PPC_TMP0, ppcRn));
    } else if (wback && rn != rd) {
        ctx.emit(ppc_mr(ppcRn, PPC_TMP1));
    }

    if (hasCondBranch) {
        size_t endIdx = ctx.size();
        int32_t off   = (int32_t)(endIdx - condBranchIdx) * 4;
        ctx.base[condBranchIdx] = (ctx.base[condBranchIdx] & ~0xFFFC) | (uint32_t)(off & 0xFFFC);
    }
    return true;
}

// ============================================================
// Multiply
// ============================================================
static bool emit_multiply(EmitCtx& ctx, uint32_t opcode) {
    bool    setCC = (opcode >> 20) & 1;
    uint8_t rd    = (opcode >> 16) & 0xF;
    uint8_t rn    = (opcode >> 12) & 0xF;
    uint8_t rs    = (opcode >> 8)  & 0xF;
    uint8_t rm    = (opcode >> 0)  & 0xF;
    bool    accum = (opcode >> 21) & 1;
    bool    isLong= (opcode >> 23) & 1;

    if (rd == 15 || rm == 15 || rs == 15) return false;
    if (isLong) return false; // UMULL/SMULL/UMLAL/SMLAL - fall back

    uint8_t ppcRd = ARM_TO_PPC[rd];
    uint8_t ppcRn = ARM_TO_PPC[rn];
    uint8_t ppcRs = ARM_TO_PPC[rs];
    uint8_t ppcRm = ARM_TO_PPC[rm];

    if (accum) {
        ctx.emit(ppc_mullw(PPC_TMP0, ppcRm, ppcRs));
        ctx.emit(ppc_add(ppcRd, PPC_TMP0, ppcRn));
    } else {
        ctx.emit(ppc_mullw(ppcRd, ppcRm, ppcRs));
    }

    if (setCC)
        emit_updateNZ(ctx, ppcRd);

    return true;
}

// ============================================================
// Interpreter fallback for one instruction
// ============================================================
static void emit_interpFallback(EmitCtx& ctx) {
    emit_syncToInterp(ctx);

    ctx.emit(ppc_mr(3, PPC_INTERP));
    emit_call(ctx, (void*)JitPpc_interpFallback);

    emit_syncFromInterp(ctx);
}

// ============================================================
// Thumb emitters
// ============================================================
static bool emit_thumb_lslImm(EmitCtx& ctx, uint16_t opcode) {
    uint8_t rd  = (opcode >> 0) & 7;
    uint8_t rs  = (opcode >> 3) & 7;
    uint8_t imm = (opcode >> 6) & 0x1F;
    emit_lsl_imm(ctx, ARM_TO_PPC[rd], ARM_TO_PPC[rs], imm, true);
    emit_updateNZ(ctx, ARM_TO_PPC[rd]);
    emit_updateC_fromTMP1(ctx);
    return true;
}

static bool emit_thumb_lsrImm(EmitCtx& ctx, uint16_t opcode) {
    uint8_t rd  = (opcode >> 0) & 7;
    uint8_t rs  = (opcode >> 3) & 7;
    uint8_t imm = (opcode >> 6) & 0x1F;
    emit_lsr_imm(ctx, ARM_TO_PPC[rd], ARM_TO_PPC[rs], imm ? imm : 32, true);
    emit_updateNZ(ctx, ARM_TO_PPC[rd]);
    emit_updateC_fromTMP1(ctx);
    return true;
}

static bool emit_thumb_asrImm(EmitCtx& ctx, uint16_t opcode) {
    uint8_t rd  = (opcode >> 0) & 7;
    uint8_t rs  = (opcode >> 3) & 7;
    uint8_t imm = (opcode >> 6) & 0x1F;
    emit_asr_imm(ctx, ARM_TO_PPC[rd], ARM_TO_PPC[rs], imm ? imm : 32, true);
    emit_updateNZ(ctx, ARM_TO_PPC[rd]);
    emit_updateC_fromTMP1(ctx);
    return true;
}

static bool emit_thumb_addSubReg(EmitCtx& ctx, uint16_t opcode) {
    uint8_t rd  = (opcode >> 0) & 7;
    uint8_t rs  = (opcode >> 3) & 7;
    bool    sub = (opcode >> 9) & 1;
    bool    imm = (opcode >> 10) & 1;
    uint8_t ppcRd = ARM_TO_PPC[rd];
    uint8_t ppcRs = ARM_TO_PPC[rs];

    if (imm) {
        uint32_t immVal = (opcode >> 6) & 7;
        int n = emit_li32(ctx.cur, PPC_TMP0, immVal);
        ctx.cur += n;
    } else {
        uint8_t rn = (opcode >> 6) & 7;
        ctx.emit(ppc_mr(PPC_TMP0, ARM_TO_PPC[rn]));
    }

    if (sub) {
        ctx.emit(ppc_subfc(ppcRd, PPC_TMP0, ppcRs));
        emit_updateNZCV_sub(ctx, ppcRd);
    } else {
        ctx.emit(ppc_addc(ppcRd, ppcRs, PPC_TMP0));
        emit_updateNZCV_add(ctx, ppcRd);
    }
    return true;
}

static bool emit_thumb_movImm(EmitCtx& ctx, uint16_t opcode) {
    uint8_t rd = (opcode >> 8) & 7;
    uint32_t imm = opcode & 0xFF;
    int n = emit_li32(ctx.cur, ARM_TO_PPC[rd], imm);
    ctx.cur += n;
    emit_updateNZ(ctx, ARM_TO_PPC[rd]);
    return true;
}

static bool emit_thumb_cmpImm(EmitCtx& ctx, uint16_t opcode) {
    uint8_t rs  = (opcode >> 8) & 7;
    uint32_t imm = opcode & 0xFF;
    int n = emit_li32(ctx.cur, PPC_TMP0, imm);
    ctx.cur += n;
    ctx.emit(ppc_subfc(PPC_TMP1, PPC_TMP0, ARM_TO_PPC[rs]));
    emit_updateNZCV_sub(ctx, PPC_TMP1);
    return true;
}

static bool emit_thumb_addImm8(EmitCtx& ctx, uint16_t opcode) {
    uint8_t  rd  = (opcode >> 8) & 7;
    uint32_t imm = opcode & 0xFF;
    int n = emit_li32(ctx.cur, PPC_TMP0, imm);
    ctx.cur += n;
    ctx.emit(ppc_addc(ARM_TO_PPC[rd], ARM_TO_PPC[rd], PPC_TMP0));
    emit_updateNZCV_add(ctx, ARM_TO_PPC[rd]);
    return true;
}

static bool emit_thumb_subImm8(EmitCtx& ctx, uint16_t opcode) {
    uint8_t  rd  = (opcode >> 8) & 7;
    uint32_t imm = opcode & 0xFF;
    int n = emit_li32(ctx.cur, PPC_TMP0, imm);
    ctx.cur += n;
    ctx.emit(ppc_subfc(ARM_TO_PPC[rd], PPC_TMP0, ARM_TO_PPC[rd]));
    emit_updateNZCV_sub(ctx, ARM_TO_PPC[rd]);
    return true;
}

static bool emit_thumb_aluOp(EmitCtx& ctx, uint16_t opcode) {
    uint8_t rd    = (opcode >> 0) & 7;
    uint8_t rs    = (opcode >> 3) & 7;
    uint8_t op    = (opcode >> 6) & 0xF;
    uint8_t ppcRd = ARM_TO_PPC[rd];
    uint8_t ppcRs = ARM_TO_PPC[rs];

    switch (op) {
        case 0:  ctx.emit(ppc_and(ppcRd, ppcRd, ppcRs)); emit_updateNZ(ctx, ppcRd); break;
        case 1:  ctx.emit(ppc_xor(ppcRd, ppcRd, ppcRs)); emit_updateNZ(ctx, ppcRd); break;
        case 2:  ctx.emit(ppc_slw(ppcRd, ppcRd, ppcRs)); emit_updateNZ(ctx, ppcRd); break;
        case 3:  ctx.emit(ppc_srw(ppcRd, ppcRd, ppcRs)); emit_updateNZ(ctx, ppcRd); break;
        case 4:  ctx.emit(ppc_sraw(ppcRd, ppcRd, ppcRs)); emit_updateNZ(ctx, ppcRd); break;
        case 5: {
            ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_CPSR, 0, 2, 2));
            ctx.emit(ppc_mtxer(PPC_TMP0));
            ctx.emit(ppc_adde(ppcRd, ppcRd, ppcRs));
            emit_updateNZCV_add(ctx, ppcRd);
            break;
        }
        case 6: {
            ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_CPSR, 0, 2, 2));
            ctx.emit(ppc_mtxer(PPC_TMP0));
            ctx.emit(ppc_subfe(ppcRd, ppcRs, ppcRd));
            emit_updateNZCV_sub(ctx, ppcRd);
            break;
        }
        case 7:
            emit_ror_reg(ctx, ppcRd, ppcRd, ppcRs, true);
            emit_updateNZ(ctx, ppcRd);
            break;
        case 8: {
            ctx.emit(ppc_and(PPC_TMP0, ppcRd, ppcRs));
            emit_updateNZ(ctx, PPC_TMP0);
            break;
        }
        case 9: {
            ctx.emit(ppc_addi(PPC_TMP0, 0, 0));
            ctx.emit(ppc_subfc(ppcRd, ppcRd, PPC_TMP0));
            emit_updateNZCV_sub(ctx, ppcRd);
            break;
        }
        case 10: {
            ctx.emit(ppc_subfc(PPC_TMP0, ppcRs, ppcRd));
            emit_updateNZCV_sub(ctx, PPC_TMP0);
            break;
        }
        case 11: {
            ctx.emit(ppc_addc(PPC_TMP0, ppcRd, ppcRs));
            emit_updateNZCV_add(ctx, PPC_TMP0);
            break;
        }
        case 12: ctx.emit(ppc_or(ppcRd, ppcRd, ppcRs));   emit_updateNZ(ctx, ppcRd); break;
        case 13: ctx.emit(ppc_mullw(ppcRd, ppcRd, ppcRs)); emit_updateNZ(ctx, ppcRd); break;
        case 14: ctx.emit(ppc_andc(ppcRd, ppcRd, ppcRs));  emit_updateNZ(ctx, ppcRd); break;
        case 15: ctx.emit(ppc_nor(ppcRd, ppcRs, ppcRs));   emit_updateNZ(ctx, ppcRd); break;
        default: return false;
    }
    return true;
}

static bool emit_thumb_hiRegOp(EmitCtx& ctx, uint16_t opcode) {
    uint8_t op  = (opcode >> 8) & 3;
    uint8_t h1  = (opcode >> 7) & 1;
    uint8_t h2  = (opcode >> 6) & 1;
    uint8_t rs  = ((opcode >> 3) & 7) | (h2 << 3);
    uint8_t rd  = (opcode & 7) | (h1 << 3);

    if (rd == 15 || rs == 15) return false;

    uint8_t ppcRd = ARM_TO_PPC[rd];
    uint8_t ppcRs = ARM_TO_PPC[rs];

    switch (op) {
        case 0: ctx.emit(ppc_add(ppcRd, ppcRd, ppcRs)); break;
        case 1: {
            ctx.emit(ppc_subfc(PPC_TMP0, ppcRs, ppcRd));
            emit_updateNZCV_sub(ctx, PPC_TMP0);
            break;
        }
        case 2: ctx.emit(ppc_mr(ppcRd, ppcRs)); break;
        case 3: {
            ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 27, 25));
            ctx.emit(ppc_rlwinm(PPC_TMP0, ppcRs, 5, 26, 26));
            ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP0));
            ctx.emit(ppc_rlwinm(PPC_ARM_R15, ppcRs, 0, 0, 30));
            emit_syncToInterp(ctx);
            break;
        }
    }
    return true;
}

static bool emit_thumb_ldrPc(EmitCtx& ctx, uint16_t opcode, uint32_t pc) {
    uint8_t  rd   = (opcode >> 8) & 7;
    uint32_t off  = (opcode & 0xFF) << 2;
    uint32_t addr = ((pc + 4) & ~3u) + off;

    int n = emit_li32(ctx.cur, PPC_TMP0, addr);
    ctx.cur += n;

    static const int VSAVE = FRAME_REGSYNC;
    for (int i = 0; i < 8; i++)
        ctx.emit(ppc_stw(3 + i, VSAVE + i * 4, 1));

    ctx.emit(ppc_mr(3, PPC_CORE));
    ctx.emit(ppc_addi(4, 0, ctx.arm7 ? 1 : 0));
    ctx.emit(ppc_mr(5, PPC_TMP0));
    emit_call(ctx, (void*)JitPpc_memRead32);

    ctx.emit(ppc_stw(3, VSAVE + 0 * 4, 1));
    for (int i = 1; i < 8; i++)
        ctx.emit(ppc_lwz(3 + i, VSAVE + i * 4, 1));
    ctx.emit(ppc_lwz(ARM_TO_PPC[rd], VSAVE, 1));
    if (ARM_TO_PPC[rd] != 3)
        ctx.emit(ppc_lwz(3, VSAVE, 1));

    return true;
}

static bool emit_thumb_branch(EmitCtx& ctx, uint16_t opcode, uint32_t pc) {
    uint8_t cond = (opcode >> 8) & 0xF;

    if (cond == 0xE) {
        // Unconditional B
        int32_t offset = (int8_t)(opcode & 0xFF);
        offset <<= 1;
        uint32_t target = pc + 4 + offset;
        int n = emit_li32(ctx.cur, PPC_ARM_R15, target);
        ctx.cur += n;
        emit_syncToInterp(ctx);
        return true;
    }

    if (cond == 0xF) return false; // SWI

    // Conditional branch
    int32_t  offset      = (int8_t)(opcode & 0xFF);
    offset <<= 1;
    uint32_t target      = pc + 4 + offset;
    uint32_t fallthrough = pc + 2;

    emit_setupCondFlags(ctx);
    CondBranch cb = armCondToPpc(cond);
    if (!cb.valid) return false;

    // Skip body if condition NOT met
    size_t condBranchIdx = ctx.size();
    ctx.emit(ppc_bc((cb.bo == 12) ? 4 : 12, cb.bi, 0));

    // Taken path
    int n = emit_li32(ctx.cur, PPC_ARM_R15, target);
    ctx.cur += n;
    emit_syncToInterp(ctx);

    // Jump past fall-through
    size_t skipIdx = ctx.size();
    ctx.emit(ppc_b(0));

    // Patch condition branch to here
    {
        size_t bodyStart = ctx.size();
        int32_t off = (int32_t)(bodyStart - condBranchIdx) * 4;
        ctx.base[condBranchIdx] = (ctx.base[condBranchIdx] & ~0xFFFC) | (uint32_t)(off & 0xFFFC);
    }

    // Fall-through path
    n = emit_li32(ctx.cur, PPC_ARM_R15, fallthrough);
    ctx.cur += n;

    // Patch skip branch
    {
        size_t endIdx = ctx.size();
        int32_t off   = (int32_t)(endIdx - skipIdx) * 4;
        ctx.base[skipIdx] = ppc_b(off);
    }

    return true;
}

static bool emit_thumb_bl(EmitCtx& ctx, uint16_t op1, uint16_t op2, uint32_t pc) {
    int32_t  hiOff  = (int32_t)((op1 & 0x7FF) << 21) >> 9;
    int32_t  loOff  = (op2 & 0x7FF) << 1;
    uint32_t target  = pc + 4 + hiOff + loOff;
    uint32_t retAddr = pc + 4;

    int n = emit_li32(ctx.cur, PPC_ARM_R14, retAddr | 1u);
    ctx.cur += n;
    n = emit_li32(ctx.cur, PPC_ARM_R15, target & ~1u);
    ctx.cur += n;

    // BLX: switch to ARM mode
    uint8_t botBits = (op2 >> 11) & 0x1F;
    if (botBits == 0x1C) {
        ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 27, 25)); // clear T
        ctx.emit(ppc_rlwinm(PPC_ARM_R15, PPC_ARM_R15, 0, 0, 29)); // align to 4
    }

    emit_syncToInterp(ctx);
    return true;
}

// ============================================================
// ARM instruction dispatch
// ============================================================
static bool emitARMInstr(EmitCtx& ctx, uint32_t opcode, uint32_t pc) {
    uint8_t cond      = (opcode >> 28) & 0xF;
    if (cond == 15)   return false; // unconditional instructions (complex)

    uint32_t instrType = (opcode >> 25) & 7;

    switch (instrType) {
        case 0:
        case 1: {
            // Multiply: bits[27:24]=0000 and bits[7:4]=1001
            if ((opcode & 0x0FC000F0) == 0x00000090)
                return emit_multiply(ctx, opcode);
            // BX / BLX reg
            if ((opcode & 0x0FFFFFF0) == 0x012FFF10 ||
                (opcode & 0x0FFFFFF0) == 0x012FFF30)
                return emit_branch(ctx, opcode, pc);
            // MSR/MRS/CLZ/etc. — fall back
            if ((opcode & 0x0FB00FF0) == 0x01000000 ||
                (opcode & 0x0FB00000) == 0x03200000 ||
                (opcode & 0x0DB0F000) == 0x010F0000)
                return false;
            return emit_dataProc(ctx, opcode);
        }
        case 2:
        case 3:
            return emit_loadStore(ctx, opcode, pc);
        case 4:
            return false; // LDM/STM - fall back
        case 5:
            return emit_branch(ctx, opcode, pc);
        case 6:
            return false; // coprocessor load/store
        case 7:
            if ((opcode & 0x0F000000) == 0x0F000000) return false; // SWI
            if ((opcode & 0x0F000010) == 0x0E000010) return false; // MRC/MCR
            return false;
    }
    return false;
}

// ============================================================
// Thumb instruction dispatch
// ============================================================
static bool emitThumbInstr(EmitCtx& ctx, uint16_t opcode, uint32_t pc) {
    uint8_t bits1514 = (opcode >> 14) & 3;
    uint8_t bits1311 = (opcode >> 11) & 7;

    switch (bits1514) {
        case 0:
            switch (bits1311) {
                case 0: return emit_thumb_lslImm(ctx, opcode);
                case 1: return emit_thumb_lsrImm(ctx, opcode);
                case 2: return emit_thumb_asrImm(ctx, opcode);
                case 3: return emit_thumb_addSubReg(ctx, opcode);
                case 4: return emit_thumb_movImm(ctx, opcode);
                case 5: return emit_thumb_cmpImm(ctx, opcode);
                case 6: return emit_thumb_addImm8(ctx, opcode);
                case 7: return emit_thumb_subImm8(ctx, opcode);
            }
            break;
        case 1: {
            uint8_t bits10 = (opcode >> 10) & 3;
            if (bits10 == 0) return emit_thumb_aluOp(ctx, opcode);
            if (bits10 == 1) return emit_thumb_hiRegOp(ctx, opcode);
            if (bits1311 == 0x09) return emit_thumb_ldrPc(ctx, opcode, pc);
            return false; // load/store register offset
        }
        case 2:
            return false; // load/store with immediate offset / SP / push/pop
        case 3: {
            uint8_t bits1512 = (opcode >> 12) & 0xF;
            if (bits1512 == 0xD || bits1512 == 0xE)
                return emit_thumb_branch(ctx, opcode, pc);
            return false; // BL halves, SWI
        }
    }
    return false;
}

// ============================================================
// Block compiler
// ============================================================
static JitBlock* compileBlock(Interpreter* interp, Core* core,
                               uint32_t armPC, bool arm7) {
    bool isThumb = interp->isThumb();

    size_t bucket = hashPC(armPC);
    JitBlock& slot = blockCache[bucket];
    if (slot.valid && slot.armPC == armPC && slot.thumb == isThumb)
        return &slot;

    if (codeBufferPos + MAX_BLOCK_SIZE * MAX_PPC_PER_ARM + 256 >= JIT_MAX_INSTRS)
        flushJitCache();

    EmitCtx ctx;
    ctx.base     = codeBuffer + codeBufferPos;
    ctx.cur      = ctx.base;
    ctx.capacity = JIT_MAX_INSTRS - codeBufferPos;
    ctx.thumb    = isThumb;
    ctx.arm7     = arm7;
    ctx.armPC    = armPC;
    ctx.interp   = interp;
    ctx.core     = core;

    emit_prologue(ctx, interp, core);
    emit_syncFromInterp(ctx);

    uint32_t pc         = armPC;
    int      instrCount = 0;
    bool     blockEnded = false;

    for (instrCount = 0;
         instrCount < (int)MAX_BLOCK_SIZE && !blockEnded;
         instrCount++) {

        // Keep live PC up to date
        int n = emit_li32(ctx.cur, PPC_ARM_R15, pc + (isThumb ? 4u : 8u));
        ctx.cur += n;

        if (isThumb) {
            uint16_t top = core->memory.read<uint16_t>(arm7, pc);

            // Detect BL/BLX pair
            uint8_t bits1511 = (top >> 11) & 0x1F;
            if (bits1511 == 0x1E) {
                uint16_t bot     = core->memory.read<uint16_t>(arm7, pc + 2);
                uint8_t  botBits = (bot >> 11) & 0x1F;
                if (botBits == 0x1F || botBits == 0x1C) {
                    if (emit_thumb_bl(ctx, top, bot, pc)) {
                        pc += 4;
                        instrCount++;
                        blockEnded = true;
                        continue;
                    }
                }
            }

            if (!emitThumbInstr(ctx, top, pc))
                emit_interpFallback(ctx);

            pc += 2;

            uint8_t bits1512 = (top >> 12) & 0xF;
            if (bits1512 == 0xD || bits1512 == 0xE)
                blockEnded = true;
        } else {
            uint32_t opcode = core->memory.read<uint32_t>(arm7, pc);

            if (!emitARMInstr(ctx, opcode, pc))
                emit_interpFallback(ctx);

            pc += 4;

            uint32_t itype = (opcode >> 25) & 7;
            if (itype == 5 ||
                (opcode & 0x0FFFFFF0) == 0x012FFF10 ||
                (opcode & 0x0FFFFFF0) == 0x012FFF30)
                blockEnded = true;

            if (itype == 4 && ((opcode >> 20) & 1) && (opcode & (1u << 15)))
                blockEnded = true;
        }
    }

    // End-of-block sync
    emit_syncToInterp(ctx);
    emit_epilogue(ctx);

    slot.armPC     = armPC;
    slot.ppcCode   = ctx.base;
    slot.ppcWords  = (uint32_t)ctx.size();
    slot.armInstrs = (uint32_t)instrCount;
    slot.thumb     = isThumb;
    slot.valid     = true;

    codeBufferPos += ctx.size();

    flushCaches(ctx.base, ctx.size());
    return &slot;
}

// ============================================================
// Execute a compiled block
// ============================================================
static void executeBlock(const JitBlock* block) {
    typedef void (*BlockFn)();
    ((BlockFn)block->ppcCode)();
}

// ============================================================
// JIT public run entry point
// ============================================================
void runJitNds(Core& core) {
    Interpreter& arm9 = core.interpreter[0];
    Interpreter& arm7 = core.interpreter[1];

    if (!arm9.halted) {
        uint32_t pc9 = arm9.getPC();
        JitBlock* b  = compileBlock(&arm9, &core, pc9, false);
        if (b) executeBlock(b);
    }

    if (!arm7.halted) {
        uint32_t pc7 = arm7.getPC();
        JitBlock* b  = compileBlock(&arm7, &core, pc7, true);
        if (b) executeBlock(b);
    }
}

// ============================================================
// Offset computation (called once at startup)
// ============================================================
static void computeOffsets() {
    // Layout of Interpreter (private fields, order matches interpreter.h):
    //
    //  public:
    //    HleBios*  bios        @ 0   (4 B)
    //    uint32_t  entryAddr   @ 4   (4 B)
    //    uint8_t   halted      @ 8   (1 B) + 3 pad
    //  private:
    //    Core*     core        @ 12  (4 B)
    //    bool      arm7        @ 16  (1 B) + 3 pad
    //    uint8_t*  pcData      @ 20  (4 B)
    //    uint32_t  pipeline[2] @ 24  (8 B)
    //    uint32_t* registers[32] @ 32 (128 B)
    //    uint32_t  registersUsr[16] @ 160 (64 B)
    //    uint32_t  registersFiq[7]  @ 224 (28 B)
    //    uint32_t  registersSvc[2]  @ 252 (8  B)
    //    uint32_t  registersAbt[2]  @ 260 (8  B)
    //    uint32_t  registersIrq[2]  @ 268 (8  B)
    //    uint32_t  registersUnd[2]  @ 276 (8  B)
    //    uint32_t  cpsr             @ 284 (4  B)
    //    uint32_t* spsr             @ 288 (4  B)
    //    uint32_t  spsrFiq          @ 292 (4  B)
    //    uint32_t  spsrSvc          @ 296 (4  B)
    //    uint32_t  spsrAbt          @ 300 (4  B)
    //    uint32_t  spsrIrq          @ 304 (4  B)
    //    uint32_t  spsrUnd          @ 308 (4  B)
    //    uint32_t  cycles           @ 312 (4  B)
    //    bool      dsiCycle         @ 316 (1  B) + 3 pad
    //
    // IMPORTANT: verify these offsets against the actual build with:
    //   powerpc-eabi-objdump -d interpreter.o  (or add static_asserts via a helper method)
    off_halted       = 8;
    off_pcData       = 20;
    off_pipeline     = 24;
    off_registersUsr = 160;
    off_cpsr         = 284;
    off_cycles       = 312;
}

// ============================================================
// initJit / shutdownJit / invalidateJitRange
// ============================================================
bool initJit() {
    computeOffsets();

    // memalign is in <malloc.h> on Wii GCC
    codeBuffer = (uint32_t*)memalign(32, JIT_CODE_SIZE);
    if (!codeBuffer) {
        printf("[JIT] Failed to allocate %zu KB code buffer\n",
               JIT_CODE_SIZE / 1024);
        return false;
    }

    codeBufferPos = 0;
    for (size_t i = 0; i < BLOCK_CACHE_SIZE; i++)
        blockCache[i].valid = false;

    printf("[JIT] ARM->PPC JIT ready. Buffer: %p  (%zu KB)\n",
           (void*)codeBuffer, JIT_CODE_SIZE / 1024);
    return true;
}

void shutdownJit() {
    if (codeBuffer) {
        free(codeBuffer);
        codeBuffer = nullptr;
    }
}

void invalidateJitRange(uint32_t start, uint32_t end) {
    for (size_t i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (blockCache[i].valid) {
            uint32_t bpc = blockCache[i].armPC;
            if (bpc >= start && bpc < end)
                blockCache[i].valid = false;
        }
    }
}

} // namespace JitPpc
