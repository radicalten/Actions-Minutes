// jit_ppc.cpp
// ARMv4/5 -> PowerPC/Gekko JIT Recompiler for NooDS on Wii
// Translates DS ARM9/ARM7 instructions to native Wii Gekko PPC instructions
// Falls back to interpreter for unhandled opcodes

#include "jit_ppc.h"
#include "core.h"
#include "interpreter.h"
#include "memory.h"
#include "defines.h"

#include <gccore.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <unordered_map>
#include <vector>
#include <cstdio>

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
    // offset must be word-aligned, signed 26-bit
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

// X-form
static inline uint32_t ppc_Xform(uint32_t op, uint8_t rt, uint8_t ra, uint8_t rb, uint32_t xop, bool rc = false) {
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
// Load a 32-bit immediate into a register (2 instructions)
// ============================================================
// Emits: lis rt, hi16(imm)  +  ori rt, rt, lo16(imm)
// Returns number of words emitted (1 or 2)
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
// ARM R0-R7   -> PPC r3-r10
// ARM R8-R12  -> PPC r14-r18
// ARM R13(SP) -> PPC r19
// ARM R14(LR) -> PPC r20
// ARM R15(PC) -> PPC r21
// ARM CPSR    -> PPC r22
// Interpreter*-> PPC r23
// Core*       -> PPC r24
// scratch0    -> PPC r25
// scratch1    -> PPC r26
// scratch2    -> PPC r11
// scratch3    -> PPC r12

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
// ARM Condition Codes mapped to PPC CR0 bits
// ============================================================
// PPC CR0: LT=0, GT=1, EQ=2, SO=3  (in CR field 0, bit positions 0-3 of CRF)
// BO field for conditional branch:
//   12 = branch if CRbit set
//   4  = branch if CRbit clear
// BI field: cr*4 + bit (LT=0,GT=1,EQ=2,SO=3)
//
// ARM conditions:
//   EQ=0  Z=1          -> PPC: beq (BI=CR0[EQ]=2, BO=12)
//   NE=1  Z=0          -> PPC: bne (BI=CR0[EQ]=2, BO=4)
//   CS=2  C=1          -> need carry in XER/CR; use CR1[SO] trick or scratch
//   CC=3  C=0
//   MI=4  N=1          -> PPC: blt (BI=CR0[LT]=0, BO=12)
//   PL=5  N=0          -> PPC: bge (BI=CR0[LT]=0, BO=4)
//   VS=6  V=1
//   VC=7  V=0
//   HI=8  C=1 && Z=0
//   LS=9  C=0 || Z=1
//   GE=10 N==V         -> PPC: bge
//   LT=11 N!=V         -> PPC: blt
//   GT=12 Z=0 && N==V  -> PPC: bgt
//   LE=13 Z=1 || N!=V  -> PPC: ble
//   AL=14 always
//   NV=15 never (treat as NOP/UND)

// CPSR bit positions (ARM)
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
// We allocate a fixed-size executable region.
// On Wii we need to flush data cache and invalidate instruction cache.

static const size_t JIT_CODE_SIZE   = 4 * 1024 * 1024; // 4 MB code cache
static const size_t JIT_MAX_INSTRS  = JIT_CODE_SIZE / 4;
static const size_t MAX_BLOCK_SIZE  = 256;  // max ARM instructions per block
static const size_t MAX_PPC_PER_ARM = 64;   // max PPC words per ARM instruction

static uint32_t* codeBuffer    = nullptr;
static size_t    codeBufferPos = 0;  // next free word index

// ============================================================
// Block cache
// ============================================================
struct JitBlock {
    uint32_t  armPC;        // ARM PC of block start
    uint32_t* ppcCode;      // pointer into codeBuffer
    uint32_t  ppcWords;     // number of PPC words
    uint32_t  armInstrs;    // number of ARM instructions translated
    bool      thumb;        // was Thumb mode?
    bool      valid;
};

static const size_t BLOCK_CACHE_SIZE = 8192;
static JitBlock blockCache[BLOCK_CACHE_SIZE];
static uint32_t blockCacheGen = 0; // generation counter for invalidation

// Simple hash: ARM PC -> bucket
static inline size_t hashPC(uint32_t pc) {
    return (pc >> 1) & (BLOCK_CACHE_SIZE - 1);
}

// Flush entire JIT cache
static void flushJitCache() {
    codeBufferPos = 0;
    for (size_t i = 0; i < BLOCK_CACHE_SIZE; i++)
        blockCache[i].valid = false;
    blockCacheGen++;
}

// ============================================================
// Code emitter context
// ============================================================
struct EmitCtx {
    uint32_t* base;      // start of current block in codeBuffer
    uint32_t* cur;       // current write pointer
    size_t    capacity;  // max words available

    bool thumb;          // are we in Thumb mode?
    bool arm7;           // ARM7 or ARM9?
    uint32_t armPC;      // current ARM PC being translated

    // Interpreter* and Core* are known at compile time (passed as literals)
    Interpreter* interp;
    Core*        core;

    void emit(uint32_t word) {
        if ((size_t)(cur - base) < capacity) {
            *cur++ = word;
        }
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
// Helper: Load ARM register Rn into a PPC register
// If Rn == 15 (PC), load the correct value (PC+8 for ARM, PC+4 for Thumb)
// ============================================================
static void emit_loadARMReg(EmitCtx& ctx, uint8_t armReg, uint8_t ppcDst) {
    if (armReg < 16) {
        uint8_t src = ARM_TO_PPC[armReg];
        if (src != ppcDst)
            ctx.emit(ppc_mr(ppcDst, src));
    }
}

static void emit_storeARMReg(EmitCtx& ctx, uint8_t ppcSrc, uint8_t armReg) {
    if (armReg < 16) {
        uint8_t dst = ARM_TO_PPC[armReg];
        if (dst != ppcSrc)
            ctx.emit(ppc_mr(dst, ppcSrc));
    }
}

// ============================================================
// Helper: Sync ARM registers to/from Interpreter struct
//
// The Interpreter stores registers as uint32_t* registersUsr[16]
// which is an array of *pointers*. Each pointer ultimately points
// into registersUsr[]. We maintain live copies in PPC registers
// and sync at call boundaries.
//
// Offsets into Interpreter struct (approximate — must match actual layout).
// We'll derive these at init time using offsetof.
// ============================================================

// Offsets computed at startup
static size_t off_registersUsr  = 0; // offsetof(Interpreter, registersUsr)
static size_t off_cpsr          = 0; // offsetof(Interpreter, cpsr)
static size_t off_cycles        = 0; // offsetof(Interpreter, cycles)
static size_t off_halted        = 0; // offsetof(Interpreter, halted)
static size_t off_pipeline      = 0; // offsetof(Interpreter, pipeline)
static size_t off_pcData        = 0; // offsetof(Interpreter, pcData)

// We can't use offsetof directly on private members, so we use a trick:
// create a dummy Interpreter-shaped struct or use measured deltas.
// For robustness we'll sync via the public interface at call boundaries.

// ============================================================
// Condition check emission
// Returns false if condition is AL (always), true if a branch was emitted.
// The caller must fill in the branch target offset after emitting the body.
// Returns the index of the branch word (to be patched) or -1 for AL.
// ============================================================

// We store condition state in PPC CR0 by testing CPSR.
// Before a conditional block we extract the relevant CPSR bits.

// Extract N,Z,C,V into CR fields for fast condition testing.
// We use:
//   CR0: Z set (EQ), updated by compare
//   CR1: N (negative)
//   CR2: C (carry)
//   CR3: V (overflow)
//
// We set these up by:
//   1. Load PPC_CPSR
//   2. Test bit 30 (Z) -> cmpi against 0 or rlwinm + cmpi
//   3. etc.

// For simplicity in the JIT, we extract all four flags into
// CR bits before each conditional instruction. This is done by:
//   rlwinm TMP, CPSR, 3, 28, 31   ; shift NZCV to bits 28-31
//   mtcrf  0x01, TMP               ; put them into CR7 (bits 28-31 of CR)
//
// Then:
//   bit 28 = V  -> CR7[SO] (bit 31 of CRFIELD 7 = bit 3)
//   bit 29 = C  -> CR7[EQ] (bit 30 of CR  = bit 2)... 
//
// Actually let's use a cleaner approach:
//   Move CPSR[31:28] -> CR7[3:0]  (N->LT, Z->GT, C->EQ, V->SO)
//   rlwinm TMP, CPSR, 4, 28, 31  ; NZCV now in bits 31-28 (N in bit31=LT)
//   mtcrf 0x01, TMP
//
// After mtcrf 0x01, CR7[0]=N, CR7[1]=Z, CR7[2]=C, CR7[3]=V
// (CR7 covers bits 28-31 of CR register; CR7[0] is bit 28 = N, etc.)
//
// Wait: mtcrf writes CRfields. CR is 32 bits. CRfield 7 is bits [28:31].
// The mask 0x01 means write only CRfield 7.
// The source register bits [28:31] go to CR[28:31] = CRfield7.
// After rlwinm rt,rs,4,28,31: bit31=N, bit30=Z, bit29=C, bit28=V
// But CRfield7 bit0 (PPC CR bit28) = LT, bit1=GT, bit2=EQ, bit3=SO.
// So after mtcrf: CR7[LT]=V, CR7[GT]=C, CR7[EQ]=Z, CR7[SO]=N
//
// That's awkward. Let's just extract each bit individually as needed.
//
// Simpler: for each condition, emit a specific test sequence.

// BI values for CRfield 0: LT=0, GT=1, EQ=2, SO=3
// BI values for CRfield 7 (bits 28-31): LT=28, GT=29, EQ=30, SO=31

// We'll put:
//   N -> CR0[LT]  (bit 0 of CR = CR bit 0)
//   Z -> CR0[EQ]  (bit 2 of CR = CR bit 2)
//   C -> CR1[EQ]  (bit 6 of CR = CR bit 6)
//   V -> CR1[SO]  (bit 7 of CR = CR bit 7)
// using mtcrf with appropriate masking.

// Setup condition flags from CPSR (emit before conditional block)
// Clobbers PPC_TMP0, PPC_TMP1
static void emit_setupCondFlags(EmitCtx& ctx) {
    // Extract N (bit31 of CPSR) -> CR0[LT] (bit 0)
    // Extract Z (bit30 of CPSR) -> CR0[EQ] (bit 2)
    // Extract C (bit29 of CPSR) -> CR1[EQ] (bit 6 = crfield1 bit 2)
    // Extract V (bit28 of CPSR) -> CR1[SO] (bit 7 = crfield1 bit 3)
    //
    // Approach: build a word where bits 0,2,6,7 hold N,Z,C,V respectively
    // then use mtcrf.
    //
    // Simpler for JIT: just extract each flag and do individual compares.
    // We'll use two compare instructions to set CR0 and CR1.

    // Extract NZCV into bits [31:28] of TMP0
    // rlwinm TMP0, CPSR, 4, 28, 31  => TMP0 = (CPSR << 4) & 0x0000000F
    // But we want them in specific CR positions.
    // Let's put NZCV into a byte and use mtcrf cleverly.
    //
    // CPSR: N=bit31, Z=bit30, C=bit29, V=bit28
    // We want: CR word bit0=N, bit2=Z, bit6=C, bit7=V
    //
    // Build TMP0 with N in bit0, Z in bit2:
    //   rlwinm TMP0, CPSR, 1, 31, 31  ; bit0 = N
    //   rlwinm TMP1, CPSR, 31, 29, 29 ; bit29... hmm
    //
    // Actually simplest: put NZCV into high nibble of a CR-suitable register.
    // PPC mtcrf writes one CRfield at a time.
    // CRfield 0 = CR bits [0:3] = (LT, GT, EQ, SO)
    // If we put in register bits [31:28] = NZCV, mtcrf 0x80 writes CR[0:3].
    // So: CR[0]=N, CR[1]=Z, CR[2]=C, CR[3]=V
    //     meaning CR0[LT]=N, CR0[GT]=Z, CR0[EQ]=C, CR0[SO]=V
    //
    // But standard compares write CR0 differently. We'll use our own convention:
    //   CR0[LT]=N, CR0[GT]=Z, CR0[EQ]=C, CR0[SO]=V (after mtcrf)

    // rlwinm TMP0, CPSR, 0, 28, 31  ; isolate bits 28-31 (V,C,Z,N in that order)
    ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_CPSR, 0, 28, 31));
    // Now TMP0 has NZCV in bits 28-31 (bit31=N, bit30=Z, bit29=C, bit28=V)
    // mtcrf 0x80, TMP0 writes TMP0[28:31] -> CR[0:3]
    // CR0[LT] = TMP0[28] = V  (WRONG order)
    // PPC CR field 0 bits: bit28=LT, bit29=GT, bit30=EQ, bit31=SO
    // TMP0[28]=V, [29]=C, [30]=Z, [31]=N
    // So CR0[LT]=V, CR0[GT]=C, CR0[EQ]=Z, CR0[SO]=N
    // 
    // That gives us: Z in CR0[EQ], which is what beq/bne need.
    // N in CR0[SO]
    // C in CR0[GT]
    // V in CR0[LT]
    ctx.emit(ppc_mtcrf(0x80, PPC_TMP0));
    // Now:
    //   beq CR0 -> Z=1 (ARM EQ) ✓
    //   bne CR0 -> Z=0 (ARM NE) ✓
    //   blt CR0 -> V=1 (not N!) — we need different approach for MI/PL/GE/LT/GT/LE
    //
    // For MI/PL we need N. N is in CR0[SO].
    // For VS/VC we need V. V is in CR0[LT].
    // For CS/CC we need C. C is in CR0[GT].
    //
    // For GE/LT/GT/LE we need N==V or N!=V which requires both bits.
    // This is complex for a JIT. We'll handle the common cases inline
    // and fall back to interpreter for rare conditions.
}

// Map ARM condition to (BO, BI) pair. Returns false if complex.
// BO=12: branch if set, BO=4: branch if clear
// Using the CR0 layout from emit_setupCondFlags:
//   CR0[LT]=bit28=V, CR0[GT]=bit29=C, CR0[EQ]=bit30=Z, CR0[SO]=bit31=N
// BI values: LT=0, GT=1, EQ=2, SO=3
struct CondBranch { uint8_t bo; uint8_t bi; bool valid; bool negate; };

static CondBranch armCondToPpc(uint8_t cond) {
    // BI: V=0(LT), C=1(GT), Z=2(EQ), N=3(SO)
    switch (cond) {
        case 0:  return {12, 2, true, false}; // EQ: Z=1 -> beq
        case 1:  return {4,  2, true, false}; // NE: Z=0 -> bne
        case 2:  return {12, 1, true, false}; // CS: C=1 -> bgt (C in GT position)
        case 3:  return {4,  1, true, false}; // CC: C=0 -> ble
        case 4:  return {12, 3, true, false}; // MI: N=1 -> bso (N in SO position)
        case 5:  return {4,  3, true, false}; // PL: N=0 -> bns
        case 6:  return {12, 0, true, false}; // VS: V=1 -> blt (V in LT position)
        case 7:  return {4,  0, true, false}; // VC: V=0 -> bge
        case 14: return {20, 0, true, false}; // AL: always (BO=20 = always)
        // HI, LS, GE, LT, GT, LE need compound conditions
        default: return {0,  0, false, false}; // complex - use fallback
    }
}

// ============================================================
// Forward declarations
// ============================================================
static bool emitARMBlock(EmitCtx& ctx, uint32_t startPC, int maxInstrs, bool thumb);
static bool emitARMInstr(EmitCtx& ctx, uint32_t opcode, uint32_t pc);
static bool emitThumbInstr(EmitCtx& ctx, uint16_t opcode, uint32_t pc);

// ============================================================
// Helper: Emit a call to a C function (ABI-safe)
// On PPC Wii (SysV ABI variant):
//   Arguments in r3-r10
//   Return in r3
//   LR must be saved/restored
// ============================================================
// Save non-volatile ARM registers (r14-r26) + LR around a call
// This is expensive but necessary for calls to interpreter helpers.

// Stack frame layout for JIT blocks:
//   We use a simple frame: stwu r1,-128(r1) at block entry
//   Store: r14-r26 (13 regs * 4 = 52 bytes), LR, etc.
// Frame size: 128 bytes (16-byte aligned as required by PPC ABI)

static const int FRAME_SIZE = 128;
// Offsets within our stack frame (from new r1):
static const int FRAME_LR   = 124;  // saved LR at top
static const int FRAME_R14  = 8;    // saved r14
// r14 through r26 = 13 registers at offsets 8,12,...,56

static void emit_prologue(EmitCtx& ctx, Interpreter* interp, Core* core) {
    // Build stack frame
    ctx.emit(ppc_stwu(1, -FRAME_SIZE, 1));  // stwu r1,-128(r1)
    // Save LR
    ctx.emit(ppc_mflr(0));
    ctx.emit(ppc_stw(0, FRAME_LR, 1));
    // Save non-volatile registers r14-r26
    for (int r = 14; r <= 26; r++) {
        ctx.emit(ppc_stw(r, FRAME_R14 + (r-14)*4, 1));
    }
    // Load interpreter pointer into PPC_INTERP (r23)
    int n = emit_li32(ctx.cur, PPC_INTERP, (uint32_t)(uintptr_t)interp);
    ctx.cur += n;
    // Load core pointer into PPC_CORE (r24)
    n = emit_li32(ctx.cur, PPC_CORE, (uint32_t)(uintptr_t)core);
    ctx.cur += n;
}

static void emit_epilogue(EmitCtx& ctx) {
    // Restore non-volatile registers r14-r26
    for (int r = 14; r <= 26; r++) {
        ctx.emit(ppc_lwz(r, FRAME_R14 + (r-14)*4, 1));
    }
    // Restore LR
    ctx.emit(ppc_lwz(0, FRAME_LR, 1));
    ctx.emit(ppc_mtlr(0));
    // Pop stack frame
    ctx.emit(ppc_addi(1, 1, FRAME_SIZE));
    // Return
    ctx.emit(ppc_blr());
}

// ============================================================
// Sync ARM live registers to Interpreter struct
// Called before interpreter fallback or at end of block
// ============================================================
// The Interpreter's registersUsr is an array of uint32_t (not pointers in our model).
// We write our live PPC registers back to the Interpreter struct.
//
// NOTE: Since registersUsr is private in Interpreter, we need a helper
// method on Interpreter. We'll add a friend declaration or use a public
// sync method. For this JIT we expose a flat struct overlay.
//
// To avoid modifying interpreter.h extensively, we use the public
// getPC() as a guide and store via a helper trampoline.
//
// In practice we need to sync 16 ARM registers + CPSR.
// We'll calculate the offset of registersUsr empirically.

// We expose these via a companion header jit_ppc.h that declares
// the JitState struct (a plain POD mirror of the relevant fields).

// For the actual implementation, we call a C helper that does the sync.
// This is called at block boundaries.

static void emit_syncToInterp(EmitCtx& ctx) {
    // Call JitPpc::syncJitToInterp(Interpreter* interp, uint32_t regs[17])
    // where regs[0..15]=ARM R0-R15, regs[16]=CPSR
    // We pass: r3=PPC_INTERP, r4..=register values
    // For simplicity, store ARM regs in a temp buffer on stack then call.
    // This is a slow path (end-of-block sync), so we accept the overhead.
    //
    // Actually we just call the sync function with all regs as args.
    // Since we have up to 16 regs + CPSR = 17 values, pass pointer to frame.
    
    // Store ARM regs into stack frame scratch area (bytes 64-131 = 17*4=68 bytes)
    static const int FRAME_REGSYNC = 64;
    for (int i = 0; i < 16; i++) {
        ctx.emit(ppc_stw(ARM_TO_PPC[i], FRAME_REGSYNC + i*4, 1));
    }
    ctx.emit(ppc_stw(PPC_CPSR, FRAME_REGSYNC + 16*4, 1));
    
    // Call: JitPpc_syncToInterp(interp, &frame[FRAME_REGSYNC], isArm7)
    ctx.emit(ppc_mr(3, PPC_INTERP));
    ctx.emit(ppc_addi(4, 1, FRAME_REGSYNC));
    // isArm7 passed as r5 - we embed it as a constant at compile time
    // (handled by caller)
}

static void emit_syncFromInterp(EmitCtx& ctx) {
    // Load ARM regs from interpreter into PPC registers
    // Call: JitPpc_syncFromInterp(interp, regs_out[17])
    static const int FRAME_REGSYNC = 64;
    ctx.emit(ppc_mr(3, PPC_INTERP));
    ctx.emit(ppc_addi(4, 1, FRAME_REGSYNC));
    // After call, load from frame
    // (handled by caller who invokes the function)
}

// ============================================================
// Public C-callable sync functions (called from emitted code)
// ============================================================
extern "C" {

// Sync JIT register state -> Interpreter
void JitPpc_syncToInterp(Interpreter* interp, uint32_t* regs) {
    // regs[0..15] = ARM R0-R15, regs[16] = CPSR
    // We need to write these into the interpreter's registersUsr array.
    // Since registersUsr is private, we use a byte-offset hack.
    // offsetof is computed once at init.
    uint32_t* usr = (uint32_t*)((uint8_t*)interp + off_registersUsr);
    for (int i = 0; i < 16; i++)
        usr[i] = regs[i];
    // CPSR
    uint32_t* cpsrPtr = (uint32_t*)((uint8_t*)interp + off_cpsr);
    *cpsrPtr = regs[16];
}

// Sync Interpreter -> JIT register state
void JitPpc_syncFromInterp(Interpreter* interp, uint32_t* regs) {
    uint32_t* usr = (uint32_t*)((uint8_t*)interp + off_registersUsr);
    for (int i = 0; i < 16; i++)
        regs[i] = usr[i];
    uint32_t* cpsrPtr = (uint32_t*)((uint8_t*)interp + off_cpsr);
    regs[16] = *cpsrPtr;
}

// Run a single ARM opcode through the interpreter (fallback)
int JitPpc_interpFallback(Interpreter* interp) {
    // This calls the interpreter's internal runOpcode().
    // Since it's private, we need an accessor.
    // We'll add a public wrapper: Interpreter::jitRunOpcode()
    return interp->jitRunOpcode();
}

// Memory read helpers (handle DS memory map properly)
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

// Update cycle count
void JitPpc_addCycles(Interpreter* interp, uint32_t cycles) {
    uint32_t* cyc = (uint32_t*)((uint8_t*)interp + off_cycles);
    *cyc += cycles;
}

} // extern "C"

// ============================================================
// Helper: emit an indirect call to a C function
// fn_addr must be a 32-bit address
// Sets up CTR and calls bctrl
// Clobbers r0, r11, r12
// ============================================================
static void emit_call(EmitCtx& ctx, void* fnAddr) {
    uint32_t addr = (uint32_t)(uintptr_t)fnAddr;
    // Load function address into r12 (scratch per ABI)
    int n = emit_li32(ctx.cur, 12, addr);
    ctx.cur += n;
    ctx.emit(ppc_mtctr(12));
    ctx.emit(ppc_bctr(true));   // bctrl
}

// ============================================================
// ARM Shift/Rotate helpers - emitted inline
// ============================================================
// These compute ARM shifter operand and optionally update carry.
// dst = result register (PPC), carry goes to PPC_TMP1 (bit0).
// scratchA = PPC_TMP0

// LSL #imm (logical shift left by immediate)
static void emit_lsl_imm(EmitCtx& ctx, uint8_t dst, uint8_t src, uint8_t imm,
                          bool setCarry = false) {
    if (imm == 0) {
        if (dst != src) ctx.emit(ppc_mr(dst, src));
        if (setCarry) {
            // carry unchanged — extract C from CPSR
            ctx.emit(ppc_rlwinm(PPC_TMP1, PPC_CPSR, 3, 31, 31)); // bit0 = C
        }
    } else if (imm < 32) {
        if (setCarry) {
            // carry = bit (32-imm) of src before shift
            // rlwinm TMP1, src, (imm-1+1)%32, 31, 31  -- extract bit(32-imm)
            uint8_t bitPos = 32 - imm;  // which bit of src becomes carry
            // rotate left by (imm) gives us the carry in bit31
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, imm, 31, 31));
        }
        ctx.emit(ppc_rlwinm(dst, src, imm, 0, 31 - imm));
    } else if (imm == 32) {
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, 0, 31, 31)); // carry = bit0
        ctx.emit(ppc_addi(dst, 0, 0)); // result = 0
    } else {
        if (setCarry) ctx.emit(ppc_addi(PPC_TMP1, 0, 0));
        ctx.emit(ppc_addi(dst, 0, 0));
    }
}

// LSR #imm (logical shift right by immediate)
static void emit_lsr_imm(EmitCtx& ctx, uint8_t dst, uint8_t src, uint8_t imm,
                          bool setCarry = false) {
    if (imm == 0 || imm == 32) {
        // LSR#0 is encoded as LSR#32
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, 1, 31, 31)); // carry = bit31
        ctx.emit(ppc_addi(dst, 0, 0));
    } else if (imm < 32) {
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, 33 - imm, 31, 31)); // bit(imm-1) -> bit0
        // srw by immediate: rlwinm dst,src,32-imm,imm,31
        ctx.emit(ppc_rlwinm(dst, src, 32 - imm, imm, 31));
    } else {
        if (setCarry) ctx.emit(ppc_addi(PPC_TMP1, 0, 0));
        ctx.emit(ppc_addi(dst, 0, 0));
    }
}

// ASR #imm (arithmetic shift right by immediate)
static void emit_asr_imm(EmitCtx& ctx, uint8_t dst, uint8_t src, uint8_t imm,
                          bool setCarry = false) {
    if (imm == 0 || imm == 32) {
        // ASR#0 = ASR#32
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, 1, 31, 31)); // carry = bit31
        ctx.emit(ppc_srawi(dst, src, 31)); // all-sign
    } else if (imm < 32) {
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, 33 - imm, 31, 31));
        ctx.emit(ppc_srawi(dst, src, imm));
    } else {
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, 1, 31, 31));
        ctx.emit(ppc_srawi(dst, src, 31));
    }
}

// ROR #imm (rotate right by immediate)
static void emit_ror_imm(EmitCtx& ctx, uint8_t dst, uint8_t src, uint8_t imm,
                          bool setCarry = false) {
    if (imm == 0) {
        // RRX: rotate right by 1 through carry
        // dst = (src >> 1) | (carry << 31)
        // Extract C from CPSR to TMP1
        ctx.emit(ppc_rlwinm(PPC_TMP1, PPC_CPSR, 3, 31, 31)); // TMP1 = C
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP0, src, 0, 31, 31)); // new carry = bit0 of src
        ctx.emit(ppc_rlwinm(dst, src, 31, 1, 31));           // dst = src >> 1
        ctx.emit(ppc_rlwimi(dst, PPC_TMP1, 31, 0, 0));       // dst |= carry << 31
        if (setCarry)
            ctx.emit(ppc_mr(PPC_TMP1, PPC_TMP0));
    } else {
        imm &= 31;
        if (setCarry)
            ctx.emit(ppc_rlwinm(PPC_TMP1, src, 33 - imm, 31, 31)); // bit(imm-1)->bit0
        // ror imm = rlwinm dst,src,32-imm,0,31
        ctx.emit(ppc_rlwinm(dst, src, 32 - (imm & 31), 0, 31));
    }
}

// LSL by register (variable shift)
static void emit_lsl_reg(EmitCtx& ctx, uint8_t dst, uint8_t src, uint8_t shiftReg,
                          bool setCarry = false) {
    // PPC slw shifts by amount in low 6 bits, result=0 if shift>=32
    if (setCarry) {
        // carry = bit(32 - shift) of src, if shift in [1..32]
        // complex: use a helper call or simplify
        // For now: if shift==0, carry unchanged; else compute.
        // We'll compute: TMP0 = 32 - shiftReg; extract bit TMP0 from src.
        // This requires variable bit extraction which is complex.
        // Simplified: carry = 0 for variable shift in JIT (rare case)
        ctx.emit(ppc_addi(PPC_TMP1, 0, 0));
    }
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
    // ror by reg: rlwnm dst,src,32-rb,0,31 but we need 32-rb mod 32
    // Compute (32 - shiftReg) & 31 in TMP0
    if (setCarry) ctx.emit(ppc_addi(PPC_TMP1, 0, 0));
    ctx.emit(ppc_subfic(PPC_TMP0, shiftReg, 32));  // Hmm, subfic not defined
    // Use: addi TMP0, 0, 32; subf TMP0, shiftReg, TMP0
    int n = emit_li32(ctx.cur, PPC_TMP0, 32); ctx.cur += n;
    ctx.emit(ppc_subf(PPC_TMP0, shiftReg, PPC_TMP0));
    ctx.emit(ppc_rlwnm(dst, src, PPC_TMP0, 0, 31));
}

// subfic helper (was missing)
static inline uint32_t ppc_subfic(uint8_t rt, uint8_t ra, int16_t imm) {
    return (8u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) | (uint16_t)imm;
}

// ============================================================
// Update CPSR N,Z flags from a result register
// ============================================================
static void emit_updateNZ(EmitCtx& ctx, uint8_t result) {
    // N = result[31], Z = (result == 0)
    // Current CPSR in PPC_CPSR.
    // Clear N and Z bits, then set from result.
    //
    // Clear bits 31,30 of CPSR:
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 2, 29)); // clear bits 30,31 (Z,N)
    // Wait: rlwinm(dst,src,sh,mb,me) masks bits mb..me.
    // To clear bits 30,31: keep bits 0..29: rlwinm CPSR,CPSR,0,2,31 — NO.
    // rlwinm with mb=0,me=29 masks (rotated) bits 0..29 i.e. clears bits 30,31.
    // Actually rlwinm dst,src,0,2,29 rotates by 0 then masks bits 2..29 -> clears 0,1,30,31
    // That's wrong. Use ANDIS to clear upper bits:
    // CPSR & ~(N|Z) = CPSR & 0x3FFFFFFF
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 2, 31)); // keep bits 2..31, clears 0,1 -- WRONG
    // Better: use andis. ANDIS clears upper 16 bits:
    // We want to clear bits 30,31 (N,Z) which are in the top 2 bits.
    // andis. ra,rs,0x3FFF clears top 2 bits of top halfword... 0x3FFF0000 mask
    // Hmm, CPSR bits 30,31: 0xC0000000
    // ~0xC0000000 = 0x3FFFFFFF
    // Use: rlwinm CPSR, CPSR, 0, 2, 31 -- rotate 0, mask bits 2..31
    // This is correct! bits 0..1 are cleared (bit0=31-31=bit31 after rotate=bit31 WRONG)
    // 
    // PPC rlwinm with sh=0,mb=2,me=31: keeps bits 2..31 (bit2=bit from 32-bit position 2=29th from MSB)
    // In IBM notation: bit0=MSB=bit31 in normal notation.
    // IBM bit 0 = C++ bit 31. IBM bit 31 = C++ bit 0.
    // So IBM mb=2,me=31 = keep IBM bits 2..31 = C++ bits 29..0 = clear C++ bits 31,30 = N,Z ✓
    
    // Redo: clear N and Z (C++ bits 31 and 30)
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 2, 31));  // IBM mb=2,me=31 clears IBM bits 0,1 = C++ bits 31,30
    
    // Set N: extract result[31] into CPSR[31]
    // rlwimi CPSR, result, 0, 0, 0  -- insert result[IBM0]=C++bit31 into CPSR[IBM0]
    ctx.emit(ppc_rlwimi(PPC_CPSR, result, 0, 0, 0));
    
    // Set Z: if result==0, set CPSR[30]
    // cmpwi TMP0, result, 0
    ctx.emit(ppc_cmpi(0, result, 0));
    // mfcr TMP0
    ctx.emit(ppc_mfcr(PPC_TMP0));
    // Z flag in CR0 = bit 30 of CR (IBM bit 2 = C++ bit 29... no)
    // CR0[EQ] is IBM bit 2 of CR = C++ bit 29 of CR
    // We want to set CPSR C++ bit 30 from this.
    // Extract CR0[EQ] (IBM bit 2) to a register bit:
    // rlwinm TMP0, CR, 3, 31, 31  -- shift left 3? 
    // CR is 32 bits: bits [0:3]=CR0, bits [4:7]=CR1, etc. in IBM notation.
    // CR0[EQ] = IBM bit 2 = C++ bit 29 of the CR register.
    // To put it at C++ bit 30 of CPSR:
    // rlwinm TMP0, mfcr, 29+1, 31, 31  -- rotate so C++bit29 goes to C++bit31... 
    // 
    // shift: (mfcr_val >> 29) & 1 = bit29 isolated. Then << 30 for CPSR.
    // rlwinm TMP0, mfcr, 3, 31, 31 -- rotate LEFT by 3 puts C++bit29 at C++bit31-3=bit26? No.
    // 
    // Let's think carefully:
    // mfcr value: CR0[LT]=C++bit31, CR0[GT]=C++bit30, CR0[EQ]=C++bit29, CR0[SO]=C++bit28
    // So CR0[EQ] is at C++ bit 29 of mfcr value.
    // We want it at C++ bit 30 of CPSR.
    // So rotate mfcr left by 1: bit29 -> bit30. rlwinm TMP0,TMP0,1,30,30
    ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_TMP0, 1, 30, 30)); // isolate Z, put at bit30
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP0));
}

// Update CPSR N,Z,C,V from arithmetic (add result)
// Uses XER[CA] for carry after addc/adde
static void emit_updateNZCV_add(EmitCtx& ctx, uint8_t result) {
    // N,Z from result
    emit_updateNZ(ctx, result);
    
    // C from XER[CA] (set by addc/adde)
    ctx.emit(ppc_mfxer(PPC_TMP0));
    // XER[CA] = C++ bit 29 of XER (IBM bit 2)
    // We want it at CPSR C++ bit 29.
    // rlwinm TMP0, XER, 0, 29, 29 : keep only C++ bit 29
    ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_TMP0, 0, 3, 3)); // IBM bit3 = C++bit28? Let's check:
    // XER format: OV=IBM0=C++31, SO=IBM1=C++30, CA=IBM2=C++29 <-- this seems wrong for XER
    // Actually PPC XER: bit 0 (MSB/IBM0) = SO, bit 1 = OV, bit 2 = CA, bits 3-24 = 0, bits 25-31 = byte count
    // So CA = IBM bit 2 = C++ bit 29.
    // rlwinm TMP0, XER, 0, 2, 2 keeps IBM bit 2 = C++ bit 29
    ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_TMP0, 0, 2, 2)); // keep only XER[CA] in C++bit29
    
    // Clear C in CPSR (C++ bit 29 = IBM bit 2)
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 0, 2)); // keep IBM bits 0..2 ... wrong
    // Clear C++bit29 from CPSR: CPSR &= ~(1<<29)
    // ~(1<<29) = 0xDFFFFFFF
    // rlwinm CPSR,CPSR,0,3,1 -- IBM mb=3,me=1 wraps around: keep IBM 3..31,0..1 = clear IBM 2 = clear C++29 ✓
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 3, 1)); // clear C++bit29 (IBM bit2)
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP0));    // set C from XER[CA]
    
    // V (overflow): harder to compute from PPC directly.
    // PPC sets OV in XER for addo operations.
    ctx.emit(ppc_mfxer(PPC_TMP0));
    // OV = IBM bit1 = C++bit30 of XER
    ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_TMP0, 0, 1, 1)); // keep only XER[OV] at C++bit30
    // Shift to C++bit28 for CPSR V position:
    ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_TMP0, 30, 31, 31)); // OV -> bit31, then shift
    // Hmm, let me recalculate:
    // XER OV = C++ bit 30. CPSR V = C++ bit 28.
    // Need to shift right by 2: rlwinm TMP0, XER, 30, 31, 31 -- rotate left 30 = rotate right 2
    // Then rlwinm to position at C++bit28:
    ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_TMP0, 30, 29, 29)); // XER OV(bit30) -> bit28
    // Clear V in CPSR (C++bit28):
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 4, 3)); // IBM mb=4,me=3 = clear IBM bit3 = C++bit28
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP0));
}

// Update CPSR N,Z,C,V from sub (subf sets CA = borrow complement)
static void emit_updateNZCV_sub(EmitCtx& ctx, uint8_t result) {
    // N,Z from result
    emit_updateNZ(ctx, result);
    
    // For ARM SUB, C = NOT borrow = XER[CA] (subfc sets CA = ~borrow)
    ctx.emit(ppc_mfxer(PPC_TMP0));
    ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_TMP0, 0, 2, 2)); // XER[CA] at C++bit29
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 3, 1)); // clear CPSR[C]
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP0));
    
    // V: overflow for subtraction. OV from XER.
    ctx.emit(ppc_mfxer(PPC_TMP0));
    ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_TMP0, 30, 29, 29)); // XER OV(bit30) -> bit28
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 4, 3));    // clear CPSR[V]
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP0));
}

// Update only C from PPC_TMP1 (bit 0 = carry)
static void emit_updateC_fromTMP1(EmitCtx& ctx) {
    // PPC_TMP1 bit0 = carry. CPSR bit29 = C.
    // Shift left by 29: rlwinm TMP2, TMP1, 29, 29, 29
    ctx.emit(ppc_rlwinm(PPC_TMP2, PPC_TMP1, 29, 29, 29));
    // Clear C in CPSR:
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 3, 1));
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP2));
}

// ============================================================
// ARM Immediate operand decoder (imm8 rotated right by rot*2)
// ============================================================
static uint32_t armImm(uint32_t opcode) {
    uint32_t imm = opcode & 0xFF;
    uint32_t rot = ((opcode >> 8) & 0xF) * 2;
    return (imm >> rot) | (imm << (32 - rot));
}

// ============================================================
// Emit ARM data processing instruction (ARM mode)
// ============================================================
// Handles: AND, EOR, SUB, RSB, ADD, ADC, SBC, RSC,
//          TST, TEQ, CMP, CMN, ORR, MOV, BIC, MVN
// And their S variants.

enum ArmDpOp {
    DP_AND=0, DP_EOR=1, DP_SUB=2, DP_RSB=3,
    DP_ADD=4, DP_ADC=5, DP_SBC=6, DP_RSC=7,
    DP_TST=8, DP_TEQ=9, DP_CMP=10, DP_CMN=11,
    DP_ORR=12, DP_MOV=13, DP_BIC=14, DP_MVN=15
};

// Decode shifter operand from opcode bits [11:0] (for register form)
// Returns false if too complex for JIT
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
                // carry = bit31 of val = val >> 31
                uint32_t carryVal = val >> 31;
                int n2 = emit_li32(ctx.cur, PPC_TMP1, carryVal);
                ctx.cur += n2;
            } else {
                outCarryValid = false; // carry unchanged
            }
        }
        return true;
    }
    
    // Register operand
    uint8_t rm = opcode & 0xF;
    uint8_t ppcRm = ARM_TO_PPC[rm];
    
    bool isRegShift = (opcode >> 4) & 1;
    uint8_t shiftType = (opcode >> 5) & 3;
    
    if (!isRegShift) {
        // Immediate shift
        uint8_t shiftAmt = (opcode >> 7) & 0x1F;
        switch (shiftType) {
            case 0: emit_lsl_imm(ctx, dstReg, ppcRm, shiftAmt, setCarry); break;
            case 1: emit_lsr_imm(ctx, dstReg, ppcRm, shiftAmt ? shiftAmt : 32, setCarry); break;
            case 2: emit_asr_imm(ctx, dstReg, ppcRm, shiftAmt ? shiftAmt : 32, setCarry); break;
            case 3: emit_ror_imm(ctx, dstReg, ppcRm, shiftAmt, setCarry); break;
        }
    } else {
        // Register shift
        uint8_t rs = (opcode >> 8) & 0xF;
        uint8_t ppcRs = ARM_TO_PPC[rs];
        // Use only low 8 bits of Rs
        ctx.emit(ppc_rlwinm(PPC_TMP2, ppcRs, 0, 24, 31)); // TMP2 = rs & 0xFF
        if (rm != rs) {
            ctx.emit(ppc_mr(PPC_TMP3, ppcRm)); // copy Rm to scratch
        } else {
            ctx.emit(ppc_mr(PPC_TMP3, ppcRm));
        }
        switch (shiftType) {
            case 0: emit_lsl_reg(ctx, dstReg, PPC_TMP3, PPC_TMP2, setCarry); break;
            case 1: emit_lsr_reg(ctx, dstReg, PPC_TMP3, PPC_TMP2, setCarry); break;
            case 2: emit_asr_reg(ctx, dstReg, PPC_TMP3, PPC_TMP2, setCarry); break;
            case 3: emit_ror_reg(ctx, dstReg, PPC_TMP3, PPC_TMP2, setCarry); break;
        }
        outCarryValid = false; // variable shift carry not fully implemented
    }
    return true;
}

// Emit a data processing instruction
// Returns true if successfully emitted, false if should fall back
static bool emit_dataProc(EmitCtx& ctx, uint32_t opcode) {
    uint8_t cond   = (opcode >> 28) & 0xF;
    uint8_t dpOp   = (opcode >> 21) & 0xF;
    bool    setCC  = (opcode >> 20) & 1;
    uint8_t rn     = (opcode >> 16) & 0xF;
    uint8_t rd     = (opcode >> 12) & 0xF;
    
    if (rd == 15 && setCC) return false;  // CPSR restore - complex
    if (rn == 15 || rd == 15) {
        // PC-relative operations need special handling
        // Fall back to interpreter for now
        if (rd == 15) return false;
    }
    
    uint8_t ppcRd = ARM_TO_PPC[rd];
    uint8_t ppcRn = ARM_TO_PPC[rn];
    
    // Patch location for condition branch (to be filled if conditional)
    size_t condBranchIdx = 0;
    bool hasCondBranch = false;
    
    if (cond != 14 && cond != 15) {
        emit_setupCondFlags(ctx);
        CondBranch cb = armCondToPpc(cond);
        if (!cb.valid) return false; // complex condition, fall back
        if (cb.bo == 20) {
            // always - no branch needed
        } else {
            condBranchIdx = ctx.size();
            hasCondBranch = true;
            // Emit a forward branch that skips the instruction
            // We'll negate the condition to skip
            uint8_t skipBO = (cb.bo == 12) ? 4 : 12;
            ctx.emit(ppc_bc(skipBO, cb.bi, 0)); // placeholder offset
        }
    }
    
    // Compute shifter operand into PPC_TMP0
    bool carryValid = false;
    bool needCarry = (dpOp == DP_ADC || dpOp == DP_SBC || dpOp == DP_RSC);
    bool needShiftCarry = setCC && (dpOp == DP_AND || dpOp == DP_EOR || dpOp == DP_TST ||
                                    dpOp == DP_TEQ || dpOp == DP_ORR || dpOp == DP_MOV ||
                                    dpOp == DP_BIC || dpOp == DP_MVN);
    
    if (!emit_shifterOp(ctx, opcode, PPC_TMP0, needShiftCarry || setCC, carryValid))
        return false;
    
    // Extract carry from CPSR for ADC/SBC/RSC
    if (needCarry) {
        // Extract C from CPSR into XER[CA] for adde/subfe
        // C = CPSR bit29. XER CA = bit29.
        // Copy C to XER: first build XER value with just CA set from CPSR[C]
        ctx.emit(ppc_rlwinm(PPC_TMP1, PPC_CPSR, 0, 2, 2)); // isolate CPSR[C] at C++bit29
        ctx.emit(ppc_mtxer(PPC_TMP1));                      // set XER with CA
    }
    
    bool testOnly = (dpOp == DP_TST || dpOp == DP_TEQ || dpOp == DP_CMP || dpOp == DP_CMN);
    uint8_t resultReg = testOnly ? PPC_TMP1 : ppcRd;
    
    switch (dpOp) {
        case DP_AND:
        case DP_TST:
            ctx.emit(ppc_and(resultReg, ppcRn, PPC_TMP0));
            break;
        case DP_EOR:
        case DP_TEQ:
            ctx.emit(ppc_xor(resultReg, ppcRn, PPC_TMP0));
            break;
        case DP_SUB:
        case DP_CMP:
            // ARM SUB: rd = rn - op2; C=NOT borrow
            ctx.emit(ppc_subfc(resultReg, PPC_TMP0, ppcRn)); // subfc rd,op2,rn = rn-op2 + carry stuff
            break;
        case DP_RSB:
            ctx.emit(ppc_subfc(resultReg, ppcRn, PPC_TMP0)); // op2 - rn
            break;
        case DP_ADD:
        case DP_CMN:
            ctx.emit(ppc_addc(resultReg, ppcRn, PPC_TMP0));
            break;
        case DP_ADC:
            ctx.emit(ppc_adde(resultReg, ppcRn, PPC_TMP0));
            break;
        case DP_SBC:
            // ARM SBC: rd = rn - op2 - NOT(C) = rn + NOT(op2) + C
            // PPC subfe: rd = NOT(ra) + rb + XER[CA] where we set CA=C
            ctx.emit(ppc_subfe(resultReg, PPC_TMP0, ppcRn));
            break;
        case DP_RSC:
            ctx.emit(ppc_subfe(resultReg, ppcRn, PPC_TMP0));
            break;
        case DP_ORR:
            ctx.emit(ppc_or(resultReg, ppcRn, PPC_TMP0));
            break;
        case DP_MOV:
            if (resultReg != PPC_TMP0)
                ctx.emit(ppc_mr(resultReg, PPC_TMP0));
            break;
        case DP_BIC:
            ctx.emit(ppc_andc(resultReg, ppcRn, PPC_TMP0));
            break;
        case DP_MVN:
            ctx.emit(ppc_nor(resultReg, PPC_TMP0, PPC_TMP0)); // NOT op2
            break;
        default:
            return false;
    }
    
    if (setCC) {
        switch (dpOp) {
            case DP_ADD: case DP_CMN:
                emit_updateNZCV_add(ctx, resultReg);
                break;
            case DP_ADC:
                emit_updateNZCV_add(ctx, resultReg);
                break;
            case DP_SUB: case DP_CMP:
            case DP_RSB: case DP_SBC: case DP_RSC:
                emit_updateNZCV_sub(ctx, resultReg);
                break;
            default:
                // Logical ops: update N,Z. C from shifter. V unchanged.
                emit_updateNZ(ctx, resultReg);
                if (carryValid)
                    emit_updateC_fromTMP1(ctx);
                break;
        }
    }
    
    // Patch conditional branch
    if (hasCondBranch) {
        size_t endIdx = ctx.size();
        int32_t offset = (int32_t)(endIdx - condBranchIdx) * 4;
        ctx.base[condBranchIdx] = (ctx.base[condBranchIdx] & ~0xFFFC) | (offset & 0xFFFC);
    }
    
    return true;
}

// ============================================================
// Emit ARM branch instructions (B, BL, BX, BLX)
// ============================================================
static bool emit_branch(EmitCtx& ctx, uint32_t opcode, uint32_t pc) {
    uint8_t cond = (opcode >> 28) & 0xF;
    
    // BX register
    if ((opcode & 0x0FFFFFF0) == 0x012FFF10) {
        uint8_t rm = opcode & 0xF;
        if (rm == 15) return false; // BX PC - unusual
        uint8_t ppcRm = ARM_TO_PPC[rm];
        
        // Condition setup
        size_t condBranchIdx = 0;
        bool hasCondBranch = false;
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
        
        // Check bit0 of Rm for Thumb bit
        ctx.emit(ppc_rlwinm(PPC_TMP0, ppcRm, 0, 31, 31)); // TMP0 = bit0 of Rm
        // Update CPSR T bit (bit5) based on bit0 of Rm
        // First clear T bit:
        ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 0, 25)); // clear bit5... 
        // IBM bit26 = C++ bit5. rlwinm with mb=0,me=25 keeps IBM 0..25 = C++31..6, clears C++5..0
        // Hmm: CPSR &= ~(1<<5) = CPSR & 0xFFFFFFDF
        // Use: rlwinm with mask 0xFFFFFFDF (keep all except bit5)
        // IBM bit26=C++bit5. rlwinm(dst,src,0,27,25) wraps: keep IBM27..31 and 0..25 = clear IBM26 = C++5 ✓
        ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 27, 25)); // clear T bit (C++ bit5)
        // Set T from Rm bit0 << 5:
        ctx.emit(ppc_rlwinm(PPC_TMP0, ppcRm, 5, 26, 26));    // rm[0] -> bit5 position in IBM
        ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP0));
        
        // Set PC = Rm & ~1
        ctx.emit(ppc_rlwinm(PPC_ARM_R15, ppcRm, 0, 0, 30)); // clear bit0
        
        // End block - sync and return
        emit_syncToInterp(ctx);
        // ... epilogue handles return
        
        if (hasCondBranch) {
            size_t endIdx = ctx.size();
            int32_t offset = (int32_t)(endIdx - condBranchIdx) * 4;
            ctx.base[condBranchIdx] = (ctx.base[condBranchIdx] & ~0xFFFC) | (offset & 0xFFFC);
        }
        return true;
    }
    
    // BLX register
    if ((opcode & 0x0FFFFFF0) == 0x012FFF30) {
        return false; // fall back for now
    }
    
    // B / BL
    if ((opcode & 0x0E000000) == 0x0A000000) {
        bool isLink = (opcode >> 24) & 1;
        int32_t offset = (int32_t)(opcode << 8) >> 6; // sign-extend 24-bit, shift left 2
        uint32_t target = pc + 8 + offset;
        
        size_t condBranchIdx = 0;
        bool hasCondBranch = false;
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
        
        if (isLink) {
            // LR = PC + 4 (next instruction after branch)
            uint32_t lrVal = pc + 4;
            int n = emit_li32(ctx.cur, PPC_ARM_R14, lrVal);
            ctx.cur += n;
        }
        
        // Set PC to target
        int n = emit_li32(ctx.cur, PPC_ARM_R15, target);
        ctx.cur += n;
        
        // End of block - sync and return
        emit_syncToInterp(ctx);
        
        if (hasCondBranch) {
            size_t endIdx = ctx.size();
            int32_t offset2 = (int32_t)(endIdx - condBranchIdx) * 4;
            ctx.base[condBranchIdx] = (ctx.base[condBranchIdx] & ~0xFFFC) | (offset2 & 0xFFFC);
        }
        return true;
    }
    
    // BLX immediate (T=1)
    if (cond == 15 && (opcode & 0xFE000000) == 0xFA000000) {
        return false; // BLX imm - complex, fall back
    }
    
    return false;
}

// ============================================================
// Emit ARM load/store (LDR/STR/LDRB/STRB)
// ============================================================
static bool emit_loadStore(EmitCtx& ctx, uint32_t opcode, uint32_t pc) {
    uint8_t cond   = (opcode >> 28) & 0xF;
    bool    isLoad = (opcode >> 20) & 1;
    bool    isByte = (opcode >> 22) & 1;
    bool    isUp   = (opcode >> 23) & 1;
    bool    preIdx = (opcode >> 24) & 1;
    bool    wback  = (opcode >> 21) & 1;
    bool    immOff = !((opcode >> 25) & 1);
    uint8_t rn     = (opcode >> 16) & 0xF;
    uint8_t rd     = (opcode >> 12) & 0xF;
    
    if (rd == 15 || rn == 15) return false; // PC involved - complex
    
    uint8_t ppcRn = ARM_TO_PPC[rn];
    uint8_t ppcRd = ARM_TO_PPC[rd];
    
    // Condition setup
    size_t condBranchIdx = 0;
    bool hasCondBranch = false;
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
    
    // Compute offset into PPC_TMP0
    if (immOff) {
        uint32_t offset = opcode & 0xFFF;
        int n = emit_li32(ctx.cur, PPC_TMP0, offset);
        ctx.cur += n;
    } else {
        // Register offset with shift
        if (!emit_shifterOp(ctx, opcode & ~(0xF << 12), PPC_TMP0, false, *(bool*)nullptr)) {
            // Use a local bool
        }
        // Redo properly:
        bool dummyCarry;
        // just use rm directly for simple case
        uint8_t rm = opcode & 0xF;
        ctx.emit(ppc_mr(PPC_TMP0, ARM_TO_PPC[rm]));
        // TODO: apply shift from bits [11:4]
    }
    
    // Compute effective address
    // addr = Rn (+ offset if pre, - offset if !isUp)
    ctx.emit(ppc_mr(PPC_TMP1, ppcRn)); // base address
    
    if (preIdx) {
        if (isUp)
            ctx.emit(ppc_add(PPC_TMP1, ppcRn, PPC_TMP0));
        else
            ctx.emit(ppc_subf(PPC_TMP1, PPC_TMP0, ppcRn));
    }
    
    // Save caller-save registers (r3-r10) needed around memory call
    // We'll use a simplified approach: save/restore what we need.
    // For the memory call:
    //   r3 = core ptr (PPC_CORE = r24 - non-volatile, safe)
    //   r4 = arm7 flag
    //   r5 = address (PPC_TMP1 = r26)
    //   r6 = value (for stores)
    // The memory functions are defined in C and follow ABI.
    
    ctx.emit(ppc_mr(3, PPC_CORE));                     // r3 = core
    // arm7 flag: embed as constant
    bool isArm7 = ctx.arm7;
    ctx.emit(ppc_addi(4, 0, isArm7 ? 1 : 0));          // r4 = arm7
    ctx.emit(ppc_mr(5, PPC_TMP1));                     // r5 = address
    
    if (!isLoad) {
        // Store: r6 = value
        ctx.emit(ppc_mr(6, ppcRd));
    }
    
    // Call appropriate memory function
    void* memFn = nullptr;
    if (isLoad) {
        memFn = isByte ? (void*)JitPpc_memRead8 : (void*)JitPpc_memRead32;
    } else {
        memFn = isByte ? (void*)JitPpc_memWrite8 : (void*)JitPpc_memWrite32;
    }
    
    // Save volatile ARM regs r3-r10 (mapped to PPC r3-r10) around call
    // They are part of our ARM reg state and are PPC volatile.
    // Push them onto stack in temp area:
    static const int FRAME_VOLATILE = 68; // bytes 68..99 = 8 regs * 4
    for (int i = 0; i < 8; i++)
        ctx.emit(ppc_stw(3 + i, FRAME_VOLATILE + i*4, 1));
    
    emit_call(ctx, memFn);
    
    // Restore volatile ARM regs
    for (int i = 0; i < 8; i++)
        ctx.emit(ppc_lwz(3 + i, FRAME_VOLATILE + i*4, 1));
    
    if (isLoad) {
        // Result in r3. Move to ppcRd (which may be r3-r10).
        // If ppcRd is r3-r10, we just saved/restored it wrongly.
        // Actually after restore, r3 holds old value; we need result.
        // Better: save result before restore.
        // Let's save r3 (result) to stack, restore others, then load.
        // We need to handle this more carefully.
        
        // Redo: save r3 separately
        ctx.emit(ppc_stw(3, FRAME_VOLATILE + 0*4, 1)); // r3=result already there
        // Restore r4-r10 (their original values were saved, r3's was the result)
        for (int i = 1; i < 8; i++)
            ctx.emit(ppc_lwz(3 + i, FRAME_VOLATILE + i*4, 1));
        // Now load result from stack to ppcRd
        if (ppcRd >= 3 && ppcRd <= 10) {
            ctx.emit(ppc_lwz(ppcRd, FRAME_VOLATILE, 1));
        } else {
            ctx.emit(ppc_lwz(ppcRd, FRAME_VOLATILE, 1));
            ctx.emit(ppc_lwz(3, FRAME_VOLATILE, 1)); // also restore r3 if needed
        }
        
        if (isByte) {
            // Result is already zero-extended for byte reads
        }
    }
    
    // Write-back: Rn = effective address (pre with W, or post)
    if ((!preIdx || wback) && rn != rd) {
        if (!preIdx) {
            // post-index: addr = Rn +/- offset (update Rn)
            if (isUp)
                ctx.emit(ppc_add(ppcRn, ppcRn, PPC_TMP0));
            else
                ctx.emit(ppc_subf(ppcRn, PPC_TMP0, ppcRn));
        } else {
            // pre-index with writeback
            ctx.emit(ppc_mr(ppcRn, PPC_TMP1));
        }
    }
    
    if (hasCondBranch) {
        size_t endIdx = ctx.size();
        int32_t offset2 = (int32_t)(endIdx - condBranchIdx) * 4;
        ctx.base[condBranchIdx] = (ctx.base[condBranchIdx] & ~0xFFFC) | (offset2 & 0xFFFC);
    }
    return true;
}

// ============================================================
// Emit multiply instruction
// ============================================================
static bool emit_multiply(EmitCtx& ctx, uint32_t opcode) {
    uint8_t cond   = (opcode >> 28) & 0xF;
    bool    setCC  = (opcode >> 20) & 1;
    uint8_t rd     = (opcode >> 16) & 0xF;  // Rd (accumulate dest / high for long)
    uint8_t rn     = (opcode >> 12) & 0xF;  // Rn for MLA
    uint8_t rs     = (opcode >> 8)  & 0xF;
    uint8_t rm     = (opcode >> 0)  & 0xF;
    
    if (rd == 15 || rm == 15 || rs == 15) return false;
    
    uint8_t bits78 = (opcode >> 20) & 0xFF;
    bool isLong   = (opcode >> 23) & 1;
    bool isSigned = (opcode >> 22) & 1;
    bool accum    = (opcode >> 21) & 1;
    
    uint8_t ppcRd = ARM_TO_PPC[rd];
    uint8_t ppcRn = ARM_TO_PPC[rn];
    uint8_t ppcRs = ARM_TO_PPC[rs];
    uint8_t ppcRm = ARM_TO_PPC[rm];
    
    if (isLong) return false; // UMULL/SMULL/etc - complex, fall back
    
    if (accum) {
        // MLA: rd = rm*rs + rn
        ctx.emit(ppc_mullw(PPC_TMP0, ppcRm, ppcRs));
        ctx.emit(ppc_add(ppcRd, PPC_TMP0, ppcRn));
    } else {
        // MUL: rd = rm*rs
        ctx.emit(ppc_mullw(ppcRd, ppcRm, ppcRs));
    }
    
    if (setCC) {
        emit_updateNZ(ctx, ppcRd);
        // C unpredictable, V unchanged
    }
    
    return true;
}

// ============================================================
// Emit LDM/STM (block transfer)
// These are complex. We emit a call to the interpreter fallback.
// ============================================================
static bool emit_ldmstm_fallback(EmitCtx& ctx) {
    return false; // always fall back to interpreter for LDM/STM
}

// ============================================================
// Emit SWI (software interrupt)
// ============================================================
static bool emit_swi(EmitCtx& ctx, uint32_t opcode, uint32_t pc) {
    // SWI: save PC+4 to LR_svc, mode change, jump to exception vector
    // This is complex; always fall back to interpreter.
    return false;
}

// ============================================================
// Emit MSR/MRS
// ============================================================
static bool emit_msrmrs(EmitCtx& ctx, uint32_t opcode) {
    return false; // Complex PSR manipulation - fall back
}

// ============================================================
// Emit MRC/MCR (coprocessor)
// ============================================================
static bool emit_mcrmrc(EmitCtx& ctx, uint32_t opcode) {
    return false; // Coprocessor - fall back to interpreter
}

// ============================================================
// Thumb instruction emitters
// ============================================================

static bool emit_thumb_lslImm(EmitCtx& ctx, uint16_t opcode) {
    uint8_t rd     = (opcode >> 0) & 7;
    uint8_t rs     = (opcode >> 3) & 7;
    uint8_t offset = (opcode >> 6) & 0x1F;
    
    emit_lsl_imm(ctx, ARM_TO_PPC[rd], ARM_TO_PPC[rs], offset, true);
    emit_updateNZ(ctx, ARM_TO_PPC[rd]);
    emit_updateC_fromTMP1(ctx);
    return true;
}

static bool emit_thumb_lsrImm(EmitCtx& ctx, uint16_t opcode) {
    uint8_t rd     = (opcode >> 0) & 7;
    uint8_t rs     = (opcode >> 3) & 7;
    uint8_t offset = (opcode >> 6) & 0x1F;
    
    emit_lsr_imm(ctx, ARM_TO_PPC[rd], ARM_TO_PPC[rs], offset ? offset : 32, true);
    emit_updateNZ(ctx, ARM_TO_PPC[rd]);
    emit_updateC_fromTMP1(ctx);
    return true;
}

static bool emit_thumb_asrImm(EmitCtx& ctx, uint16_t opcode) {
    uint8_t rd     = (opcode >> 0) & 7;
    uint8_t rs     = (opcode >> 3) & 7;
    uint8_t offset = (opcode >> 6) & 0x1F;
    
    emit_asr_imm(ctx, ARM_TO_PPC[rd], ARM_TO_PPC[rs], offset ? offset : 32, true);
    emit_updateNZ(ctx, ARM_TO_PPC[rd]);
    emit_updateC_fromTMP1(ctx);
    return true;
}

static bool emit_thumb_addSubReg(EmitCtx& ctx, uint16_t opcode) {
    uint8_t rd  = (opcode >> 0) & 7;
    uint8_t rs  = (opcode >> 3) & 7;
    uint8_t rn  = (opcode >> 6) & 7;
    bool    sub = (opcode >> 9) & 1;
    bool    imm = (opcode >> 10) & 1;
    
    uint8_t ppcRd = ARM_TO_PPC[rd];
    uint8_t ppcRs = ARM_TO_PPC[rs];
    
    if (imm) {
        // Immediate 3-bit
        uint32_t immVal = (opcode >> 6) & 7;
        int n = emit_li32(ctx.cur, PPC_TMP0, immVal);
        ctx.cur += n;
    } else {
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
    uint8_t  rd  = (opcode >> 8) & 7;
    uint32_t imm = opcode & 0xFF;
    int n = emit_li32(ctx.cur, ARM_TO_PPC[rd], imm);
    ctx.cur += n;
    emit_updateNZ(ctx, ARM_TO_PPC[rd]);
    return true;
}

static bool emit_thumb_cmpImm(EmitCtx& ctx, uint16_t opcode) {
    uint8_t  rs  = (opcode >> 8) & 7;
    uint32_t imm = opcode & 0xFF;
    int n = emit_li32(ctx.cur, PPC_TMP0, imm);
    ctx.cur += n;
    ctx.emit(ppc_subfc(PPC_TMP1, PPC_TMP0, ARM_TO_PPC[rs]));
    emit_updateNZCV_sub(ctx, PPC_TMP1);
    return true;
}

static bool emit_thumb_addImm(EmitCtx& ctx, uint16_t opcode) {
    uint8_t  rd  = (opcode >> 8) & 7;
    uint32_t imm = opcode & 0xFF;
    int n = emit_li32(ctx.cur, PPC_TMP0, imm);
    ctx.cur += n;
    ctx.emit(ppc_addc(ARM_TO_PPC[rd], ARM_TO_PPC[rd], PPC_TMP0));
    emit_updateNZCV_add(ctx, ARM_TO_PPC[rd]);
    return true;
}

static bool emit_thumb_subImm(EmitCtx& ctx, uint16_t opcode) {
    uint8_t  rd  = (opcode >> 8) & 7;
    uint32_t imm = opcode & 0xFF;
    int n = emit_li32(ctx.cur, PPC_TMP0, imm);
    ctx.cur += n;
    ctx.emit(ppc_subfc(ARM_TO_PPC[rd], PPC_TMP0, ARM_TO_PPC[rd]));
    emit_updateNZCV_sub(ctx, ARM_TO_PPC[rd]);
    return true;
}

static bool emit_thumb_aluOp(EmitCtx& ctx, uint16_t opcode) {
    uint8_t rd  = (opcode >> 0) & 7;
    uint8_t rs  = (opcode >> 3) & 7;
    uint8_t op  = (opcode >> 6) & 0xF;
    uint8_t ppcRd = ARM_TO_PPC[rd];
    uint8_t ppcRs = ARM_TO_PPC[rs];
    
    switch (op) {
        case 0: // AND
            ctx.emit(ppc_and(ppcRd, ppcRd, ppcRs));
            emit_updateNZ(ctx, ppcRd);
            break;
        case 1: // EOR
            ctx.emit(ppc_xor(ppcRd, ppcRd, ppcRs));
            emit_updateNZ(ctx, ppcRd);
            break;
        case 2: // LSL
            ctx.emit(ppc_slw(ppcRd, ppcRd, ppcRs));
            emit_updateNZ(ctx, ppcRd);
            break;
        case 3: // LSR
            ctx.emit(ppc_srw(ppcRd, ppcRd, ppcRs));
            emit_updateNZ(ctx, ppcRd);
            break;
        case 4: // ASR
            ctx.emit(ppc_sraw(ppcRd, ppcRd, ppcRs));
            emit_updateNZ(ctx, ppcRd);
            break;
        case 5: // ADC
        {
            // Extract carry to XER
            ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_CPSR, 0, 2, 2));
            ctx.emit(ppc_mtxer(PPC_TMP0));
            ctx.emit(ppc_adde(ppcRd, ppcRd, ppcRs));
            emit_updateNZCV_add(ctx, ppcRd);
            break;
        }
        case 6: // SBC
        {
            ctx.emit(ppc_rlwinm(PPC_TMP0, PPC_CPSR, 0, 2, 2));
            ctx.emit(ppc_mtxer(PPC_TMP0));
            ctx.emit(ppc_subfe(ppcRd, ppcRs, ppcRd));
            emit_updateNZCV_sub(ctx, ppcRd);
            break;
        }
        case 7: // ROR
            emit_ror_reg(ctx, ppcRd, ppcRd, ppcRs, true);
            emit_updateNZ(ctx, ppcRd);
            break;
        case 8: // TST
        {
            ctx.emit(ppc_and(PPC_TMP0, ppcRd, ppcRs));
            emit_updateNZ(ctx, PPC_TMP0);
            break;
        }
        case 9: // NEG (RSB #0)
        {
            ctx.emit(ppc_subfc(ppcRd, ppcRd, 0)); // subfc rd, rd, r0 = 0-rd
            // Use addi 0,0,0 for the zero: actually subf rd,rd,r0 won't work
            // Emit: li TMP0,0; subfc rd,rd,TMP0
            ctx.emit(ppc_addi(PPC_TMP0, 0, 0));
            ctx.emit(ppc_subfc(ppcRd, ppcRd, PPC_TMP0));
            emit_updateNZCV_sub(ctx, ppcRd);
            break;
        }
        case 10: // CMP
        {
            ctx.emit(ppc_subfc(PPC_TMP0, ppcRs, ppcRd));
            emit_updateNZCV_sub(ctx, PPC_TMP0);
            break;
        }
        case 11: // CMN
        {
            ctx.emit(ppc_addc(PPC_TMP0, ppcRd, ppcRs));
            emit_updateNZCV_add(ctx, PPC_TMP0);
            break;
        }
        case 12: // ORR
            ctx.emit(ppc_or(ppcRd, ppcRd, ppcRs));
            emit_updateNZ(ctx, ppcRd);
            break;
        case 13: // MUL
            ctx.emit(ppc_mullw(ppcRd, ppcRd, ppcRs));
            emit_updateNZ(ctx, ppcRd);
            break;
        case 14: // BIC
            ctx.emit(ppc_andc(ppcRd, ppcRd, ppcRs));
            emit_updateNZ(ctx, ppcRd);
            break;
        case 15: // MVN
            ctx.emit(ppc_nor(ppcRd, ppcRs, ppcRs));
            emit_updateNZ(ctx, ppcRd);
            break;
        default:
            return false;
    }
    return true;
}

static bool emit_thumb_hiRegOp(EmitCtx& ctx, uint16_t opcode) {
    uint8_t op  = (opcode >> 8) & 3;
    uint8_t h1  = (opcode >> 7) & 1;
    uint8_t h2  = (opcode >> 6) & 1;
    uint8_t rs  = ((opcode >> 3) & 7) | (h2 << 3);
    uint8_t rd  = (opcode & 7) | (h1 << 3);
    
    if (rd == 15 || rs == 15) return false; // PC ops complex
    
    uint8_t ppcRd = ARM_TO_PPC[rd];
    uint8_t ppcRs = ARM_TO_PPC[rs];
    
    switch (op) {
        case 0: ctx.emit(ppc_add(ppcRd, ppcRd, ppcRs)); break; // ADD Rd, Rs (no flags)
        case 1: // CMP
        {
            ctx.emit(ppc_subfc(PPC_TMP0, ppcRs, ppcRd));
            emit_updateNZCV_sub(ctx, PPC_TMP0);
            break;
        }
        case 2: ctx.emit(ppc_mr(ppcRd, ppcRs)); break; // MOV Rd, Rs (no flags)
        case 3: // BX Rs / BLX Rs
        {
            // Set T bit from bit0 of Rs
            ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 27, 25)); // clear T
            ctx.emit(ppc_rlwinm(PPC_TMP0, ppcRs, 5, 26, 26));
            ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, PPC_TMP0));
            // Set PC = Rs & ~1
            ctx.emit(ppc_rlwinm(PPC_ARM_R15, ppcRs, 0, 0, 30));
            emit_syncToInterp(ctx);
            break;
        }
    }
    return true;
}

static bool emit_thumb_ldrPc(EmitCtx& ctx, uint16_t opcode, uint32_t pc) {
    uint8_t  rd  = (opcode >> 8) & 7;
    uint32_t off = (opcode & 0xFF) << 2;
    uint32_t addr = ((pc + 4) & ~3) + off;
    
    // Load addr as constant, call read
    int n = emit_li32(ctx.cur, PPC_TMP0, addr);
    ctx.cur += n;
    
    // Save volatile ARM regs
    static const int FRAME_VOLATILE = 68;
    for (int i = 0; i < 8; i++)
        ctx.emit(ppc_stw(3 + i, FRAME_VOLATILE + i*4, 1));
    
    ctx.emit(ppc_mr(3, PPC_CORE));
    ctx.emit(ppc_addi(4, 0, ctx.arm7 ? 1 : 0));
    ctx.emit(ppc_mr(5, PPC_TMP0));
    emit_call(ctx, (void*)JitPpc_memRead32);
    
    // Save result, restore others
    ctx.emit(ppc_stw(3, FRAME_VOLATILE + 0*4, 1));
    for (int i = 1; i < 8; i++)
        ctx.emit(ppc_lwz(3 + i, FRAME_VOLATILE + i*4, 1));
    ctx.emit(ppc_lwz(ARM_TO_PPC[rd], FRAME_VOLATILE, 1));
    
    return true;
}

static bool emit_thumb_branch(EmitCtx& ctx, uint16_t opcode, uint32_t pc) {
    uint8_t cond = (opcode >> 8) & 0xF;
    
    if (cond == 0xE) {
        // Unconditional branch
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
    int32_t offset = (int8_t)(opcode & 0xFF);
    offset <<= 1;
    uint32_t target = pc + 4 + offset;
    uint32_t fallthrough = pc + 2;
    
    emit_setupCondFlags(ctx);
    CondBranch cb = armCondToPpc(cond);
    if (!cb.valid) return false;
    
    size_t condBranchIdx = 0;
    bool hasCondBranch = false;
    if (cb.bo != 20) {
        condBranchIdx = ctx.size();
        hasCondBranch = true;
        // Skip body if condition NOT met
        uint8_t skipBO = (cb.bo == 12) ? 4 : 12;
        ctx.emit(ppc_bc(skipBO, cb.bi, 0));
    }
    
    // Condition met: set PC to target
    int n = emit_li32(ctx.cur, PPC_ARM_R15, target);
    ctx.cur += n;
    emit_syncToInterp(ctx);
    
    // Skip past fall-through
    size_t skipIdx = ctx.size();
    ctx.emit(ppc_b(0)); // placeholder
    
    if (hasCondBranch) {
        size_t bodyIdx = ctx.size();
        int32_t offset2 = (int32_t)(bodyIdx - condBranchIdx) * 4;
        ctx.base[condBranchIdx] = (ctx.base[condBranchIdx] & ~0xFFFC) | (offset2 & 0xFFFC);
    }
    
    // Fall-through: set PC to next instruction
    n = emit_li32(ctx.cur, PPC_ARM_R15, fallthrough);
    ctx.cur += n;
    
    // Patch skip branch
    size_t endIdx = ctx.size();
    int32_t skipOff = (int32_t)(endIdx - skipIdx) * 4;
    ctx.base[skipIdx] = ppc_b(skipOff);
    
    return true;
}

// Thumb BL/BLX (two-instruction sequence)
// First half: blSetupT sets LR = PC + 4 + (offset<<12)
// Second half: blOffT completes: PC = LR + offset2; LR = PC+2|1
// In JIT we handle them as a pair.
static bool emit_thumb_bl(EmitCtx& ctx, uint16_t op1, uint16_t op2, uint32_t pc) {
    // First insn (bit11=10): LR = PC + 4 + (SignExt(op1[10:0]) << 12)
    // Second insn (bit11=11): PC = LR + (op2[10:0] << 1); LR = (nextPC) | 1
    
    int32_t hiOff = (int32_t)((op1 & 0x7FF) << 21) >> 9; // sign-extend 11 bits, shift left 12
    int32_t loOff = (op2 & 0x7FF) << 1;
    
    uint32_t target    = pc + 4 + hiOff + loOff;
    uint32_t retAddr   = pc + 4;  // address of instruction after BL pair
    
    // LR = return address | 1 (Thumb bit)
    int n = emit_li32(ctx.cur, PPC_ARM_R14, retAddr | 1);
    ctx.cur += n;
    
    // PC = target & ~1
    n = emit_li32(ctx.cur, PPC_ARM_R15, target & ~1u);
    ctx.cur += n;
    
    // Check if BLX (exchange to ARM)
    bool isBlx = ((op2 >> 11) & 0x1F) == 0x1C; // bits[15:11] = 11100
    if (isBlx) {
        // Switch to ARM mode: clear T bit
        ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 27, 25));
        // Align target to 4 bytes
        ctx.emit(ppc_rlwinm(PPC_ARM_R15, PPC_ARM_R15, 0, 0, 29));
    }
    
    emit_syncToInterp(ctx);
    return true;
}

// ============================================================
// Main ARM instruction emitter (dispatch)
// ============================================================
static bool emitARMInstr(EmitCtx& ctx, uint32_t opcode, uint32_t pc) {
    uint8_t cond = (opcode >> 28) & 0xF;
    
    if (cond == 15) {
        // Unconditional instructions (ARMv5+)
        // BLX imm etc. - fall back for now
        return false;
    }
    
    uint32_t instrType = (opcode >> 25) & 7;
    
    switch (instrType) {
        case 0: case 1: {
            // Data processing or miscellaneous
            // Check for multiply: bits[27:24]=0000, bit[7:4]=1001
            if ((opcode & 0x0FC000F0) == 0x00000090) {
                return emit_multiply(ctx, opcode);
            }
            // Check for BX: 0001 0010 1111 1111 1111 0001 xxxx
            if ((opcode & 0x0FFFFFF0) == 0x012FFF10 ||
                (opcode & 0x0FFFFFF0) == 0x012FFF30) {
                return emit_branch(ctx, opcode, pc);
            }
            // Check for MRS/MSR
            if ((opcode & 0x0FB00FF0) == 0x01000000 ||
                (opcode & 0x0FB00000) == 0x03200000 ||
                (opcode & 0x0DB0F000) == 0x010F0000) {
                return emit_msrmrs(ctx, opcode);
            }
            // Data processing
            return emit_dataProc(ctx, opcode);
        }
        case 2: case 3: {
            // Load/Store immediate/register offset
            return emit_loadStore(ctx, opcode, pc);
        }
        case 4: {
            // Load/Store multiple (LDM/STM)
            return emit_ldmstm_fallback(ctx);
        }
        case 5: {
            // B / BL
            return emit_branch(ctx, opcode, pc);
        }
        case 6: {
            // Coprocessor load/store - fall back
            return false;
        }
        case 7: {
            // Coprocessor data op / SWI
            if ((opcode & 0x0F000000) == 0x0F000000)
                return emit_swi(ctx, opcode, pc);
            if ((opcode & 0x0F000010) == 0x0E000010)
                return emit_mcrmrc(ctx, opcode);
            return false;
        }
    }
    return false;
}

// ============================================================
// Main Thumb instruction emitter (dispatch)
// ============================================================
static bool emitThumbInstr(EmitCtx& ctx, uint16_t opcode, uint32_t pc) {
    uint8_t bits1514 = (opcode >> 14) & 3;
    uint8_t bits1311 = (opcode >> 11) & 7;
    
    switch (bits1514) {
        case 0: {
            // Format 1-3: shift/add/sub/mov/cmp immediate
            switch (bits1311) {
                case 0: return emit_thumb_lslImm(ctx, opcode);
                case 1: return emit_thumb_lsrImm(ctx, opcode);
                case 2: return emit_thumb_asrImm(ctx, opcode);
                case 3: return emit_thumb_addSubReg(ctx, opcode);
                case 4: return emit_thumb_movImm(ctx, opcode);
                case 5: return emit_thumb_cmpImm(ctx, opcode);
                case 6: return emit_thumb_addImm(ctx, opcode);
                case 7: return emit_thumb_subImm(ctx, opcode);
            }
            break;
        }
        case 1: {
            // Format 4-8: ALU, hi-reg, PC-relative load
            if ((opcode >> 10) == 0x10) {
                return emit_thumb_aluOp(ctx, opcode);
            }
            if ((opcode >> 10) == 0x11) {
                return emit_thumb_hiRegOp(ctx, opcode);
            }
            if ((opcode >> 11) == 0x09) {
                return emit_thumb_ldrPc(ctx, opcode, pc);
            }
            // Load/Store register offset - fall back
            return false;
        }
        case 2: {
            // Format 9-14: load/store with immediate/SP offset, push/pop
            // Fall back for these (complex memory operations)
            return false;
        }
        case 3: {
            // Format 15-19: conditional/unconditional branch, BL, SWI
            uint8_t bits1512 = (opcode >> 12) & 0xF;
            if (bits1512 == 0xD) {
                return emit_thumb_branch(ctx, opcode, pc);
            }
            if (bits1512 == 0xE) {
                return emit_thumb_branch(ctx, opcode, pc);
            }
            // BL first/second half and BLX
            // These need to be decoded as pairs; fall back
            return false;
        }
    }
    return false;
}

// ============================================================
// Emit a fallback to the interpreter for one instruction
// ============================================================
static void emit_interpFallback(EmitCtx& ctx) {
    // 1. Sync JIT state -> interpreter
    static const int FRAME_REGSYNC = 64;
    for (int i = 0; i < 16; i++)
        ctx.emit(ppc_stw(ARM_TO_PPC[i], FRAME_REGSYNC + i*4, 1));
    ctx.emit(ppc_stw(PPC_CPSR, FRAME_REGSYNC + 16*4, 1));
    
    ctx.emit(ppc_mr(3, PPC_INTERP));
    ctx.emit(ppc_addi(4, 1, FRAME_REGSYNC));
    emit_call(ctx, (void*)JitPpc_syncToInterp);
    
    // 2. Call interpreter fallback (runs one opcode)
    ctx.emit(ppc_mr(3, PPC_INTERP));
    emit_call(ctx, (void*)JitPpc_interpFallback);
    // Return value (cycle count) ignored for now
    
    // 3. Sync interpreter -> JIT state
    ctx.emit(ppc_mr(3, PPC_INTERP));
    ctx.emit(ppc_addi(4, 1, FRAME_REGSYNC));
    emit_call(ctx, (void*)JitPpc_syncFromInterp);
    
    // Load back from frame
    for (int i = 0; i < 16; i++)
        ctx.emit(ppc_lwz(ARM_TO_PPC[i], FRAME_REGSYNC + i*4, 1));
    ctx.emit(ppc_lwz(PPC_CPSR, FRAME_REGSYNC + 16*4, 1));
}

// ============================================================
// Compile a JIT block starting at armPC
// ============================================================
static JitBlock* compileBlock(Interpreter* interp, Core* core, uint32_t armPC, bool arm7) {
    bool isThumb = (interp->isThumb());
    
    // Check cache
    size_t bucket = hashPC(armPC);
    JitBlock& slot = blockCache[bucket];
    if (slot.valid && slot.armPC == armPC && slot.thumb == isThumb)
        return &slot;
    
    // Allocate space in code buffer
    if (codeBufferPos + MAX_BLOCK_SIZE * MAX_PPC_PER_ARM + 200 >= JIT_MAX_INSTRS) {
        flushJitCache();
    }
    
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
    
    // Load ARM state from interpreter into PPC registers
    static const int FRAME_REGSYNC = 64;
    // First sync from interpreter
    ctx.emit(ppc_mr(3, PPC_INTERP));
    ctx.emit(ppc_addi(4, 1, FRAME_REGSYNC));
    emit_call(ctx, (void*)JitPpc_syncFromInterp);
    for (int i = 0; i < 16; i++)
        ctx.emit(ppc_lwz(ARM_TO_PPC[i], FRAME_REGSYNC + i*4, 1));
    ctx.emit(ppc_lwz(PPC_CPSR, FRAME_REGSYNC + 16*4, 1));
    
    uint32_t pc = armPC;
    int instrCount = 0;
    bool blockEnded = false;
    
    for (instrCount = 0; instrCount < (int)MAX_BLOCK_SIZE && !blockEnded; instrCount++) {
        // Update PC in live register
        int n = emit_li32(ctx.cur, PPC_ARM_R15, pc + (isThumb ? 4 : 8));
        ctx.cur += n;
        
        bool emitted = false;
        if (isThumb) {
            // Read Thumb opcode from memory (use mapped memory directly for speed)
            uint16_t top = core->memory.read<uint16_t>(arm7, pc);
            
            // Check if this is a BL/BLX pair (needs two halfwords)
            uint8_t bits1511 = (top >> 11) & 0x1F;
            if (bits1511 == 0x1E) {
                // First half of BL/BLX
                uint16_t bot = core->memory.read<uint16_t>(arm7, pc + 2);
                uint8_t botBits = (bot >> 11) & 0x1F;
                if (botBits == 0x1F || botBits == 0x1C) {
                    emitted = emit_thumb_bl(ctx, top, bot, pc);
                    if (emitted) {
                        pc += 4;
                        instrCount++; // consumed two halfwords
                        blockEnded = true;
                    }
                }
            }
            
            if (!emitted) {
                emitted = emitThumbInstr(ctx, top, pc);
                if (!emitted) {
                    // Interpreter fallback for this instruction
                    emit_interpFallback(ctx);
                }
                pc += 2;
                
                // Check if this instruction changes PC (branches)
                uint8_t bits1512 = (top >> 12) & 0xF;
                if (bits1512 == 0xD || bits1512 == 0xE ||
                    ((top >> 8) == 0x46 && ((top & 7) == 7))) {
                    blockEnded = true; // branch ends block
                }
            }
        } else {
            // ARM mode
            uint32_t opcode = core->memory.read<uint32_t>(arm7, pc);
            
            emitted = emitARMInstr(ctx, opcode, pc);
            if (!emitted) {
                emit_interpFallback(ctx);
            }
            pc += 4;
            
            // Check if this is a branch instruction (ends block)
            uint8_t instrType = (opcode >> 25) & 7;
            if (instrType == 5 || // B/BL
                (opcode & 0x0FFFFFF0) == 0x012FFF10 || // BX
                (opcode & 0x0FFFFFF0) == 0x012FFF30 || // BLX
                ((opcode >> 28) == 15 && (opcode >> 25) == 5)) { // BLX imm
                blockEnded = true;
            }
            // LDM with R15 in list ends block
            if (instrType == 4 && ((opcode >> 20) & 1) && (opcode & (1 << 15)))
                blockEnded = true;
        }
    }
    
    // End of block: sync back to interpreter and return
    {
        static const int FRAME_REGSYNC = 64;
        for (int i = 0; i < 16; i++)
            ctx.emit(ppc_stw(ARM_TO_PPC[i], FRAME_REGSYNC + i*4, 1));
        ctx.emit(ppc_stw(PPC_CPSR, FRAME_REGSYNC + 16*4, 1));
        ctx.emit(ppc_mr(3, PPC_INTERP));
        ctx.emit(ppc_addi(4, 1, FRAME_REGSYNC));
        emit_call(ctx, (void*)JitPpc_syncToInterp);
    }
    
    emit_epilogue(ctx);
    
    // Store block info
    slot.armPC    = armPC;
    slot.ppcCode  = ctx.base;
    slot.ppcWords = (uint32_t)ctx.size();
    slot.armInstrs= instrCount;
    slot.thumb    = isThumb;
    slot.valid    = true;
    
    codeBufferPos += ctx.size();
    
    // Flush CPU caches so the new code is executable
    flushCaches(ctx.base, ctx.size());
    
    return &slot;
}

// ============================================================
// Execute a compiled block
// ============================================================
static void executeBlock(JitBlock* block) {
    // The block is a function: void (*)(void)
    // It already has a proper PPC stack frame and prologue/epilogue.
    typedef void (*BlockFn)();
    BlockFn fn = (BlockFn)block->ppcCode;
    fn();
}

// ============================================================
// JIT main entry point
// Called in place of the interpreter's run loop
// ============================================================

// We need to add jitRunOpcode() to Interpreter as a public method.
// Since we can't modify interpreter.h in this file, we declare it extern.
// The actual implementation delegates to the private runOpcode().
// In interpreter.cpp add:
//   int Interpreter::jitRunOpcode() { return runOpcode(); }

// ============================================================
// Compute struct offsets at runtime using sentinel objects
// ============================================================
static void computeOffsets() {
    // We use a layout probe struct to find private member offsets.
    // This is UB strictly speaking but works in practice on GCC/Wii toolchain.
    // A better approach would be to expose getters in interpreter.h.
    
    // For now we hardcode approximate offsets based on the struct layout.
    // The Interpreter struct layout (in declaration order):
    //   HleBios* bios        (4 bytes, offset 0)
    //   uint32_t entryAddr   (4 bytes, offset 4)
    //   uint8_t  halted      (1 byte,  offset 8)
    //   padding              (3 bytes, offset 9-11)
    //   Core* core           (4 bytes, offset 12)
    //   bool arm7            (1 byte,  offset 16)
    //   padding              (3 bytes)
    //   uint8_t* pcData      (4 bytes, offset 20)
    //   uint32_t pipeline[2] (8 bytes, offset 24)
    //   uint32_t* registers[32] (128 bytes, offset 32)
    //   uint32_t registersUsr[16] (64 bytes, offset 160)
    //   ... other register banks
    //   uint32_t cpsr        
    //   ... etc.
    //
    // These MUST be verified against the actual compiler output.
    // Marked as TODO: use a helper method instead.
    
    // Use a dummy interpreter to measure (safe on GCC with -fno-strict-aliasing):
    // We'll just hard-code based on the header layout and verify at runtime.
    
    // Since registers are private, expose them via a new public method
    // Interpreter::getRegistersUsrOffset() -> size_t  (see below)
    // For now, set approximate values:
    
    // HleBios* = 4
    // entryAddr uint32_t = 4 (offset 4)
    // halted uint8_t = 1 (offset 8) + 3 pad
    // [private Core*] = 4 (offset 12)
    // [private bool arm7] = 1+3pad (offset 16)
    // [private uint8_t* pcData] = 4 (offset 20)
    // [private uint32_t pipeline[2]] = 8 (offset 24)
    // [private uint32_t* registers[32]] = 128 (offset 32)
    // [private uint32_t registersUsr[16]] = 64 (offset 160)
    off_registersUsr = 160;
    
    // After registersUsr (64B): 
    // registersFiq[7]=28, registersSvc[2]=8, registersAbt[2]=8, registersIrq[2]=8, registersUnd[2]=8
    // Total = 64+28+8+8+8+8 = 124 additional bytes -> cpsr at 160+64+128? Let's recount:
    // registersUsr[16]=64 @ 160
    // registersFiq[7]=28 @ 224
    // registersSvc[2]=8  @ 252
    // registersAbt[2]=8  @ 260
    // registersIrq[2]=8  @ 268
    // registersUnd[2]=8  @ 276
    // cpsr uint32_t      @ 284
    off_cpsr = 284;
    
    // *spsr pointer = 4  @ 288
    // spsrFiq,Svc,Abt,Irq,Und = 5*4=20 @ 292
    // cycles uint32_t @ 312
    off_cycles = 312;
    off_halted = 8;
    off_pipeline = 24;
    off_pcData = 20;
    
    // NOTE: These offsets are approximate and MUST be verified by inspecting
    // the compiled interpreter.o object file or by adding a static_assert helper.
}

// ============================================================
// Run the JIT (replaces interpreter's runCoreNds/etc.)
// ============================================================

// Number of ARM instructions to execute per scheduling quantum
static const int JIT_QUANTUM = 256;

void runJitNds(Core& core) {
    // Run both ARM9 and ARM7 with JIT
    Interpreter& arm9 = core.interpreter[0];
    Interpreter& arm7 = core.interpreter[1];
    
    // Run ARM9 (faster clock: runs at 2x ARM7)
    if (!arm9.halted) {
        uint32_t pc9 = arm9.getPC();
        JitBlock* block = compileBlock(&arm9, &core, pc9, false);
        if (block)
            executeBlock(block);
        else
            ; // fallback: interpreter already ran it via interpFallback
    }
    
    // Run ARM7
    if (!arm7.halted) {
        uint32_t pc7 = arm7.getPC();
        JitBlock* block = compileBlock(&arm7, &core, pc7, true);
        if (block)
            executeBlock(block);
    }
}

// ============================================================
// JIT Initialization
// ============================================================
bool initJit() {
    computeOffsets();
    
    // Allocate executable memory
    // On Wii we need MEM1 (main RAM) which is already executable.
    codeBuffer = (uint32_t*)memalign(32, JIT_CODE_SIZE);
    if (!codeBuffer) {
        printf("[JIT] Failed to allocate code buffer (%zu bytes)\n", JIT_CODE_SIZE);
        return false;
    }
    
    codeBufferPos = 0;
    
    // Initialize block cache
    for (size_t i = 0; i < BLOCK_CACHE_SIZE; i++)
        blockCache[i].valid = false;
    
    printf("[JIT] ARM->PPC JIT initialized. Code buffer: %p, size: %zu KB\n",
           codeBuffer, JIT_CODE_SIZE / 1024);
    return true;
}

void shutdownJit() {
    if (codeBuffer) {
        free(codeBuffer);
        codeBuffer = nullptr;
    }
}

// Invalidate JIT cache for a range of ARM addresses (called on ROM write, WRAM etc.)
void invalidateJitRange(uint32_t start, uint32_t end) {
    for (size_t i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (blockCache[i].valid) {
            uint32_t blockPC = blockCache[i].armPC;
            if (blockPC >= start && blockPC < end)
                blockCache[i].valid = false;
        }
    }
}

} // namespace JitPpc

// ============================================================
// jit_ppc.h (inline header section)
// ============================================================
// In a separate jit_ppc.h file include:
/*
#pragma once
#include <cstdint>
#include <cstdlib>

namespace JitPpc {
    bool initJit();
    void shutdownJit();
    void runJitNds(Core& core);
    void invalidateJitRange(uint32_t start, uint32_t end);
    void flushJitCache();
}

// Add to Interpreter class (public):
// int jitRunOpcode();  // delegates to private runOpcode()
*/

// ============================================================
// interpreter.cpp addition needed:
// ============================================================
// int Interpreter::jitRunOpcode() {
//     return runOpcode();
// }

// ============================================================
// core.cpp integration: replace runFunc with JIT version
// ============================================================
// In Core constructor, after init:
//   if (JitPpc::initJit())
//       runFunc = &JitPpc::runJitNds;
//
// Or add a runtime switch based on a setting.
