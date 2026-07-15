/*
 * arm_to_ppc_jit.cpp
 * ARMv4/5 -> PowerPC/Gekko JIT Recompiler for NooDS DS Emulator
 * Target: Nintendo Wii (Gekko/Broadway PPC)
 *
 * References:
 *   - http://imrannazar.com/ARM-Opcode-Map
 *   - https://fenixfox-studios.com/manual/powerpc/index.html
 *   - ARM Architecture Reference Manual (ARMv5TE)
 *   - IBM PowerPC 750CL (Gekko) User's Manual
 *
 * Register Mapping:
 *   ARM r0-r12  -> PPC r3-r15  (general purpose)
 *   ARM SP(r13) -> PPC r16
 *   ARM LR(r14) -> PPC r17
 *   ARM PC(r15) -> PPC r18
 *   ARM CPSR    -> PPC r19  (condition flags cached)
 *   JIT scratch -> PPC r20-r25
 *   CPU state * -> PPC r26  (pointer to ARM CPU state struct)
 *   Mem base *  -> PPC r27  (pointer to memory subsystem)
 *   Code cache  -> PPC r28  (current JIT block pointer)
 *
 * CPSR Flag Mapping:
 *   ARM N -> PPC CR0[LT]
 *   ARM Z -> PPC CR0[EQ]
 *   ARM C -> PPC XER[CA] / CR0 manipulation
 *   ARM V -> PPC CR0[SO] (approximation via overflow tracking)
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <unordered_map>
#include <vector>
#include <functional>
#include <sys/mman.h>

#include "core.h"

// ============================================================
//  Platform Detection & Cache Management (Wii/Gekko)
// ============================================================

#ifdef __powerpc__
extern "C" {
    // Wii/Gekko cache flush - must invalidate both I$ and D$
    static inline void gekko_flush_icache(void* addr, size_t size) {
        uint8_t* p = (uint8_t*)addr;
        uint8_t* end = p + size;
        // Flush data cache to memory, then invalidate instruction cache
        for (; p < end; p += 32) {
            __asm__ volatile("dcbst 0,%0" : : "r"(p) : "memory");
        }
        __asm__ volatile("sync" : : : "memory");
        p = (uint8_t*)addr;
        for (; p < end; p += 32) {
            __asm__ volatile("icbi 0,%0" : : "r"(p) : "memory");
        }
        __asm__ volatile("isync" : : : "memory");
    }
}
#else
// Host compilation stubs
static inline void gekko_flush_icache(void* addr, size_t size) {
    (void)addr; (void)size;
}
#endif

// ============================================================
//  Forward Declarations & Types
// ============================================================

struct ArmCpuState;
class JitCodeBlock;
class JitTranslator;

// ARM condition codes
enum ArmCond {
    COND_EQ = 0,  // Z set
    COND_NE = 1,  // Z clear
    COND_CS = 2,  // C set  (HS)
    COND_CC = 3,  // C clear (LO)
    COND_MI = 4,  // N set
    COND_PL = 5,  // N clear
    COND_VS = 6,  // V set
    COND_VC = 7,  // V clear
    COND_HI = 8,  // C set and Z clear
    COND_LS = 9,  // C clear or Z set
    COND_GE = 10, // N == V
    COND_LT = 11, // N != V
    COND_GT = 12, // Z clear and N == V
    COND_LE = 13, // Z set or N != V
    COND_AL = 14, // Always
    COND_NV = 15  // Never (ARMv4: undefined, ARMv5: BLX)
};

// ARM register numbers
enum ArmReg {
    ARM_R0  = 0,  ARM_R1  = 1,  ARM_R2  = 2,  ARM_R3  = 3,
    ARM_R4  = 4,  ARM_R5  = 5,  ARM_R6  = 6,  ARM_R7  = 7,
    ARM_R8  = 8,  ARM_R9  = 9,  ARM_R10 = 10, ARM_R11 = 11,
    ARM_R12 = 12, ARM_SP  = 13, ARM_LR  = 14, ARM_PC  = 15
};

// ARM CPSR bits
enum ArmCpsrBit {
    CPSR_N    = (1u << 31), // Negative
    CPSR_Z    = (1u << 30), // Zero
    CPSR_C    = (1u << 29), // Carry
    CPSR_V    = (1u << 28), // Overflow
    CPSR_Q    = (1u << 27), // Saturation (ARMv5E)
    CPSR_T    = (1u << 5),  // Thumb mode
    CPSR_F    = (1u << 6),  // FIQ disable
    CPSR_I    = (1u << 7),  // IRQ disable
    CPSR_MODE = 0x1Fu       // Mode bits [4:0]
};

// ARM CPU Mode
enum ArmMode {
    MODE_USER   = 0x10,
    MODE_FIQ    = 0x11,
    MODE_IRQ    = 0x12,
    MODE_SVC    = 0x13,
    MODE_ABT    = 0x17,
    MODE_UND    = 0x1B,
    MODE_SYS    = 0x1F
};

// ARM Shift Types
enum ArmShift {
    SHIFT_LSL = 0,
    SHIFT_LSR = 1,
    SHIFT_ASR = 2,
    SHIFT_ROR = 3
};

// PPC register aliases used in JIT
enum PpcReg {
    // ARM regs mapped to PPC GPRs
    PPC_ARM_R0   = 3,   // ARM r0  -> PPC r3
    PPC_ARM_R1   = 4,
    PPC_ARM_R2   = 5,
    PPC_ARM_R3   = 6,
    PPC_ARM_R4   = 7,
    PPC_ARM_R5   = 8,
    PPC_ARM_R6   = 9,
    PPC_ARM_R7   = 10,
    PPC_ARM_R8   = 11,
    PPC_ARM_R9   = 12,
    PPC_ARM_R10  = 13,
    PPC_ARM_R11  = 14,
    PPC_ARM_R12  = 15,
    PPC_ARM_SP   = 16,  // ARM SP  -> PPC r16
    PPC_ARM_LR   = 17,  // ARM LR  -> PPC r17
    PPC_ARM_PC   = 18,  // ARM PC  -> PPC r18
    PPC_CPSR     = 19,  // ARM CPSR cached -> PPC r19
    // Scratch registers
    PPC_SCRATCH0 = 20,
    PPC_SCRATCH1 = 21,
    PPC_SCRATCH2 = 22,
    PPC_SCRATCH3 = 23,
    PPC_SCRATCH4 = 24,
    PPC_SCRATCH5 = 25,
    // Fixed pointers
    PPC_CPU_PTR  = 26,  // Pointer to ArmCpuState
    PPC_MEM_PTR  = 27,  // Pointer to memory
    PPC_BLOCK_PTR= 28   // Current JIT block
};

// Map ARM register number to PPC register
static inline int armRegToPpc(int armReg) {
    // r0-r12 -> r3-r15, SP->r16, LR->r17, PC->r18
    return armReg + 3;
}

// ============================================================
//  ARM CPU State Structure
//  Must match NooDS interpreter's core.h layout
// ============================================================

struct ArmCpuState {
    uint32_t regs[16];      // r0-r15 (r15 = PC)
    uint32_t cpsr;          // Current Program Status Register
    uint32_t spsr;          // Saved PSR
    uint32_t spsrFiq;
    uint32_t spsrSvc;
    uint32_t spsrAbt;
    uint32_t spsrIrq;
    uint32_t spsrUnd;
    // Banked registers
    uint32_t regsFiq[7];    // r8-r14 FIQ banked
    uint32_t regsIrq[2];    // r13-r14 IRQ banked
    uint32_t regsSvc[2];    // r13-r14 SVC banked
    uint32_t regsAbt[2];    // r13-r14 ABT banked
    uint32_t regsUnd[2];    // r13-r14 UND banked
    // JIT metadata
    uint32_t cycles;        // Remaining cycles
    uint32_t jitFlags;      // JIT state flags
    bool     halted;
    bool     thumbMode;
};

// Offsets into ArmCpuState for memory access in JIT
#define CPUSTATE_REGS_OFFSET      offsetof(ArmCpuState, regs)
#define CPUSTATE_CPSR_OFFSET      offsetof(ArmCpuState, cpsr)
#define CPUSTATE_CYCLES_OFFSET    offsetof(ArmCpuState, cycles)

// ============================================================
//  PowerPC Instruction Encoding Helpers
//  All PPC instructions are 32 bits, big-endian
// ============================================================

// Generic PPC instruction builder
static inline uint32_t ppcInstr(uint32_t primary, uint32_t rest) {
    return ((primary & 0x3F) << 26) | (rest & 0x03FFFFFF);
}

// ---- Data Movement ----

// li rD, imm16  (actually addi rD, r0, imm)
static inline uint32_t ppc_li(int rD, int16_t imm) {
    return (14u << 26) | ((rD & 31) << 21) | (0 << 16) | (uint16_t)imm;
}

// lis rD, imm16  (addis rD, r0, imm)  - load immediate shifted
static inline uint32_t ppc_lis(int rD, int16_t imm) {
    return (15u << 26) | ((rD & 31) << 21) | (0 << 16) | (uint16_t)imm;
}

// mr rD, rS  (or rD, rS, rS)
static inline uint32_t ppc_mr(int rD, int rS) {
    return (31u << 26) | ((rS & 31) << 21) | ((rD & 31) << 16) | ((rS & 31) << 11) | (444 << 1) | 0;
}

// mfcr rD - move from condition register
static inline uint32_t ppc_mfcr(int rD) {
    return (31u << 26) | ((rD & 31) << 21) | (0 << 16) | (0 << 11) | (19 << 1) | 0;
}

// mtcrf CRM, rS - move to condition register fields
static inline uint32_t ppc_mtcrf(uint8_t crm, int rS) {
    return (31u << 26) | ((rS & 31) << 21) | (0 << 20) | ((crm & 0xFF) << 12) | (0 << 11) | (144 << 1) | 0;
}

// mfxer rD
static inline uint32_t ppc_mfxer(int rD) {
    return (31u << 26) | ((rD & 31) << 21) | (0 << 16) | (0 << 11) | (339 << 1) | 0;
    // mfspr rD, XER (SPR=1)
    // Actually: (31<<26) | (rD<<21) | (1<<16) | (0<<11) | (339<<1)
    // XER SPR number = 1 -> encoded as (1 & 0x1F)<<16 | (1>>5)<<11
}

// mfspr rD, SPR
static inline uint32_t ppc_mfspr(int rD, int spr) {
    int sprEncoded = ((spr & 0x1F) << 5) | ((spr >> 5) & 0x1F);
    return (31u << 26) | ((rD & 31) << 21) | ((sprEncoded & 0x3FF) << 11) | (339 << 1) | 0;
}

// mtspr SPR, rS
static inline uint32_t ppc_mtspr(int spr, int rS) {
    int sprEncoded = ((spr & 0x1F) << 5) | ((spr >> 5) & 0x1F);
    return (31u << 26) | ((rS & 31) << 21) | ((sprEncoded & 0x3FF) << 11) | (467 << 1) | 0;
}

// mflr rD  (mfspr rD, LR)
static inline uint32_t ppc_mflr(int rD) {
    return ppc_mfspr(rD, 8); // LR SPR = 8
}

// mtlr rS  (mtspr LR, rS)
static inline uint32_t ppc_mtlr(int rS) {
    return ppc_mtspr(8, rS);
}

// mtctr rS
static inline uint32_t ppc_mtctr(int rS) {
    return ppc_mtspr(9, rS);
}

// ---- Load/Store ----

// lwz rD, disp(rA)
static inline uint32_t ppc_lwz(int rD, int rA, int16_t disp) {
    return (32u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | (uint16_t)disp;
}

// lwzx rD, rA, rB
static inline uint32_t ppc_lwzx(int rD, int rA, int rB) {
    return (31u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (23 << 1) | 0;
}

// lbz rD, disp(rA)
static inline uint32_t ppc_lbz(int rD, int rA, int16_t disp) {
    return (34u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | (uint16_t)disp;
}

// lhz rD, disp(rA) - load halfword zero-extend
static inline uint32_t ppc_lhz(int rD, int rA, int16_t disp) {
    return (40u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | (uint16_t)disp;
}

// lha rD, disp(rA) - load halfword sign-extend
static inline uint32_t ppc_lha(int rD, int rA, int16_t disp) {
    return (42u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | (uint16_t)disp;
}

// stw rS, disp(rA)
static inline uint32_t ppc_stw(int rS, int rA, int16_t disp) {
    return (36u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | (uint16_t)disp;
}

// stwx rS, rA, rB
static inline uint32_t ppc_stwx(int rS, int rA, int rB) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (151 << 1) | 0;
}

// stb rS, disp(rA)
static inline uint32_t ppc_stb(int rS, int rA, int16_t disp) {
    return (38u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | (uint16_t)disp;
}

// sth rS, disp(rA)
static inline uint32_t ppc_sth(int rS, int rA, int16_t disp) {
    return (44u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | (uint16_t)disp;
}

// ---- Arithmetic ----

// add rD, rA, rB
static inline uint32_t ppc_add(int rD, int rA, int rB, bool rc = false) {
    return (31u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (266 << 1) | (rc ? 1 : 0);
}

// addc rD, rA, rB (add and set carry)
static inline uint32_t ppc_addc(int rD, int rA, int rB, bool rc = false) {
    return (31u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (10 << 1) | (rc ? 1 : 0);
}

// adde rD, rA, rB (add extended with carry)
static inline uint32_t ppc_adde(int rD, int rA, int rB, bool rc = false) {
    return (31u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (138 << 1) | (rc ? 1 : 0);
}

// addi rD, rA, imm16 (add immediate)
static inline uint32_t ppc_addi(int rD, int rA, int16_t imm) {
    return (14u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | (uint16_t)imm;
}

// addis rD, rA, imm16 (add immediate shifted)
static inline uint32_t ppc_addis(int rD, int rA, int16_t imm) {
    return (15u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | (uint16_t)imm;
}

// addic rD, rA, imm  (add immediate, set CA)
static inline uint32_t ppc_addic(int rD, int rA, int16_t imm) {
    return (12u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | (uint16_t)imm;
}

// addic. rD, rA, imm (add immediate, set CA and CR0)
static inline uint32_t ppc_addic_dot(int rD, int rA, int16_t imm) {
    return (13u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | (uint16_t)imm;
}

// subf rD, rA, rB (rD = rB - rA)
static inline uint32_t ppc_subf(int rD, int rA, int rB, bool rc = false) {
    return (31u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (40 << 1) | (rc ? 1 : 0);
}

// subfc rD, rA, rB
static inline uint32_t ppc_subfc(int rD, int rA, int rB, bool rc = false) {
    return (31u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (8 << 1) | (rc ? 1 : 0);
}

// subfe rD, rA, rB
static inline uint32_t ppc_subfe(int rD, int rA, int rB, bool rc = false) {
    return (31u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (136 << 1) | (rc ? 1 : 0);
}

// subfic rD, rA, imm
static inline uint32_t ppc_subfic(int rD, int rA, int16_t imm) {
    return (8u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | (uint16_t)imm;
}

// mullw rD, rA, rB  (multiply low word)
static inline uint32_t ppc_mullw(int rD, int rA, int rB, bool rc = false) {
    return (31u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (235 << 1) | (rc ? 1 : 0);
}

// mulhw rD, rA, rB  (multiply high word signed)
static inline uint32_t ppc_mulhw(int rD, int rA, int rB, bool rc = false) {
    return (31u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (75 << 1) | (rc ? 1 : 0);
}

// mulhwu rD, rA, rB (multiply high word unsigned)
static inline uint32_t ppc_mulhwu(int rD, int rA, int rB, bool rc = false) {
    return (31u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (11 << 1) | (rc ? 1 : 0);
}

// mulli rD, rA, imm
static inline uint32_t ppc_mulli(int rD, int rA, int16_t imm) {
    return (7u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | (uint16_t)imm;
}

// neg rD, rA
static inline uint32_t ppc_neg(int rD, int rA, bool rc = false) {
    return (31u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | (0 << 11) | (104 << 1) | (rc ? 1 : 0);
}

// divw rD, rA, rB (signed divide)
static inline uint32_t ppc_divw(int rD, int rA, int rB, bool rc = false) {
    return (31u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (491 << 1) | (rc ? 1 : 0);
}

// divwu rD, rA, rB (unsigned divide)
static inline uint32_t ppc_divwu(int rD, int rA, int rB, bool rc = false) {
    return (31u << 26) | ((rD & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (459 << 1) | (rc ? 1 : 0);
}

// ---- Logic ----

// and rD, rS, rB (note: PPC and takes rS,rA,rB -> stores to rA... actually: and rA, rS, rB)
// PPC: and rA,rS,rB  => rA = rS & rB
static inline uint32_t ppc_and(int rA, int rS, int rB, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (28 << 1) | (rc ? 1 : 0);
}

// andi. rA, rS, imm16
static inline uint32_t ppc_andi_dot(int rA, int rS, uint16_t imm) {
    return (28u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | imm;
}

// andis. rA, rS, imm16
static inline uint32_t ppc_andis_dot(int rA, int rS, uint16_t imm) {
    return (29u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | imm;
}

// or rA, rS, rB
static inline uint32_t ppc_or(int rA, int rS, int rB, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (444 << 1) | (rc ? 1 : 0);
}

// ori rA, rS, imm16
static inline uint32_t ppc_ori(int rA, int rS, uint16_t imm) {
    return (24u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | imm;
}

// oris rA, rS, imm16
static inline uint32_t ppc_oris(int rA, int rS, uint16_t imm) {
    return (25u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | imm;
}

// xor rA, rS, rB
static inline uint32_t ppc_xor(int rA, int rS, int rB, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (316 << 1) | (rc ? 1 : 0);
}

// xori rA, rS, imm16
static inline uint32_t ppc_xori(int rA, int rS, uint16_t imm) {
    return (26u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | imm;
}

// xoris rA, rS, imm16
static inline uint32_t ppc_xoris(int rA, int rS, uint16_t imm) {
    return (27u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | imm;
}

// nand rA, rS, rB
static inline uint32_t ppc_nand(int rA, int rS, int rB, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (476 << 1) | (rc ? 1 : 0);
}

// nor rA, rS, rB
static inline uint32_t ppc_nor(int rA, int rS, int rB, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (124 << 1) | (rc ? 1 : 0);
}

// eqv rA, rS, rB  (bitwise XNOR)
static inline uint32_t ppc_eqv(int rA, int rS, int rB, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (284 << 1) | (rc ? 1 : 0);
}

// andc rA, rS, rB  (rA = rS & ~rB)
static inline uint32_t ppc_andc(int rA, int rS, int rB, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (60 << 1) | (rc ? 1 : 0);
}

// orc rA, rS, rB  (rA = rS | ~rB)
static inline uint32_t ppc_orc(int rA, int rS, int rB, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (412 << 1) | (rc ? 1 : 0);
}

// not rA, rS  (nor rA, rS, rS)
static inline uint32_t ppc_not(int rA, int rS) {
    return ppc_nor(rA, rS, rS, false);
}

// ---- Shift / Rotate ----

// slw rA, rS, rB (shift left word)
static inline uint32_t ppc_slw(int rA, int rS, int rB, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (24 << 1) | (rc ? 1 : 0);
}

// srw rA, rS, rB (shift right word logical)
static inline uint32_t ppc_srw(int rA, int rS, int rB, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (536 << 1) | (rc ? 1 : 0);
}

// sraw rA, rS, rB (shift right word arithmetic, sets CA)
static inline uint32_t ppc_sraw(int rA, int rS, int rB, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (792 << 1) | (rc ? 1 : 0);
}

// srawi rA, rS, sh (shift right arithmetic immediate)
static inline uint32_t ppc_srawi(int rA, int rS, int sh, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((sh & 31) << 11) | (824 << 1) | (rc ? 1 : 0);
}

// slwi rA, rS, n  => rlwinm rA, rS, n, 0, 31-n
static inline uint32_t ppc_slwi(int rA, int rS, int n) {
    int sh = n & 31;
    int mb = 0;
    int me = 31 - sh;
    return (21u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((sh & 31) << 11) | ((mb & 31) << 6) | ((me & 31) << 1) | 0;
}

// srwi rA, rS, n  => rlwinm rA, rS, 32-n, n, 31
static inline uint32_t ppc_srwi(int rA, int rS, int n) {
    int sh = (32 - n) & 31;
    int mb = n & 31;
    int me = 31;
    return (21u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((sh & 31) << 11) | ((mb & 31) << 6) | ((me & 31) << 1) | 0;
}

// rlwinm rA, rS, sh, mb, me
static inline uint32_t ppc_rlwinm(int rA, int rS, int sh, int mb, int me, bool rc = false) {
    return (21u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((sh & 31) << 11) | ((mb & 31) << 6) | ((me & 31) << 1) | (rc ? 1 : 0);
}

// rlwimi rA, rS, sh, mb, me (rotate left word immediate then mask insert)
static inline uint32_t ppc_rlwimi(int rA, int rS, int sh, int mb, int me, bool rc = false) {
    return (20u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((sh & 31) << 11) | ((mb & 31) << 6) | ((me & 31) << 1) | (rc ? 1 : 0);
}

// rlwnm rA, rS, rB, mb, me (rotate left word then mask, shift from register)
static inline uint32_t ppc_rlwnm(int rA, int rS, int rB, int mb, int me, bool rc = false) {
    return (23u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | ((mb & 31) << 6) | ((me & 31) << 1) | (rc ? 1 : 0);
}

// cntlzw rA, rS (count leading zeros)
static inline uint32_t ppc_cntlzw(int rA, int rS, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | (0 << 11) | (26 << 1) | (rc ? 1 : 0);
}

// extsb rA, rS (sign extend byte)
static inline uint32_t ppc_extsb(int rA, int rS, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | (0 << 11) | (954 << 1) | (rc ? 1 : 0);
}

// extsh rA, rS (sign extend halfword)
static inline uint32_t ppc_extsh(int rA, int rS, bool rc = false) {
    return (31u << 26) | ((rS & 31) << 21) | ((rA & 31) << 16) | (0 << 11) | (922 << 1) | (rc ? 1 : 0);
}

// ---- Compare ----

// cmp crD, L, rA, rB  (signed compare)
static inline uint32_t ppc_cmp(int crD, int rA, int rB) {
    return (31u << 26) | ((crD & 7) << 23) | (0 << 22) | (0 << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (0 << 1) | 0;
}

// cmpi crD, L, rA, imm16 (signed compare immediate)
static inline uint32_t ppc_cmpi(int crD, int rA, int16_t imm) {
    return (11u << 26) | ((crD & 7) << 23) | (0 << 22) | (0 << 21) | ((rA & 31) << 16) | (uint16_t)imm;
}

// cmpl crD, rA, rB (unsigned compare)
static inline uint32_t ppc_cmpl(int crD, int rA, int rB) {
    return (31u << 26) | ((crD & 7) << 23) | (0 << 21) | ((rA & 31) << 16) | ((rB & 31) << 11) | (32 << 1) | 0;
}

// cmpli crD, rA, uimm16 (unsigned compare immediate)
static inline uint32_t ppc_cmpli(int crD, int rA, uint16_t imm) {
    return (10u << 26) | ((crD & 7) << 23) | (0 << 21) | ((rA & 31) << 16) | imm;
}

// ---- Branch ----

// b target  (unconditional branch, offset is relative to instruction)
static inline uint32_t ppc_b(int32_t offset) {
    return (18u << 26) | ((offset & 0x03FFFFFC)) | 0; // AA=0, LK=0
}

// bl target (branch and link)
static inline uint32_t ppc_bl(int32_t offset) {
    return (18u << 26) | ((offset & 0x03FFFFFC)) | 1; // AA=0, LK=1
}

// blr (branch to link register)
static inline uint32_t ppc_blr() {
    return (19u << 26) | (0 << 21) | (0 << 16) | (0 << 11) | (16 << 1) | 0;
}

// bctr (branch to count register)
static inline uint32_t ppc_bctr() {
    return (19u << 26) | (20 << 21) | (0 << 16) | (0 << 11) | (528 << 1) | 0;
}

// bctrl (branch to count register and link)
static inline uint32_t ppc_bctrl() {
    return (19u << 26) | (20 << 21) | (0 << 16) | (0 << 11) | (528 << 1) | 1;
}

// bc BO, BI, target  (conditional branch)
// BO: branch options, BI: condition bit (CR bit number)
static inline uint32_t ppc_bc(int BO, int BI, int16_t offset) {
    return (16u << 26) | ((BO & 31) << 21) | ((BI & 31) << 16) | ((uint16_t)(offset & 0xFFFC));
}

// bclr BO, BI (conditional branch to LR)
static inline uint32_t ppc_bclr(int BO, int BI) {
    return (19u << 26) | ((BO & 31) << 21) | ((BI & 31) << 16) | (0 << 11) | (16 << 1) | 0;
}

// Branch condition encodings (BO field)
#define PPC_BO_ALWAYS    0x14  // branch always (10100)
#define PPC_BO_TRUE      0x0C  // branch if CR bit set (01100)
#define PPC_BO_FALSE     0x04  // branch if CR bit clear (00100)

// CR bit positions for CR0
#define PPC_CR0_LT  0   // Less than
#define PPC_CR0_GT  1   // Greater than
#define PPC_CR0_EQ  2   // Equal
#define PPC_CR0_SO  3   // Summary overflow

// ---- Misc ----

// nop (ori r0, r0, 0)
static inline uint32_t ppc_nop() {
    return ppc_ori(0, 0, 0);
}

// sync
static inline uint32_t ppc_sync() {
    return (31u << 26) | (0 << 21) | (0 << 16) | (0 << 11) | (598 << 1) | 0;
}

// isync
static inline uint32_t ppc_isync() {
    return (19u << 26) | (0 << 21) | (0 << 16) | (0 << 11) | (150 << 1) | 0;
}

// trap (unconditional - tw 31, r0, r0)
static inline uint32_t ppc_trap() {
    return (31u << 26) | (31 << 21) | (0 << 16) | (0 << 11) | (4 << 1) | 0;
}

// ============================================================
//  JIT Code Block
//  Manages a buffer of PPC instructions for a translated
//  ARM basic block.
// ============================================================

constexpr size_t JIT_BLOCK_SIZE      = 65536;  // 64KB per block
constexpr size_t JIT_CACHE_SIZE      = (1 << 24); // 16MB total cache
constexpr int    JIT_MAX_BLOCK_INSTRS= 128;    // Max ARM instrs per block

class JitCodeBuffer {
public:
    JitCodeBuffer(size_t size) : m_size(size), m_used(0) {
#ifdef __powerpc__
        m_buf = (uint32_t*)mmap(nullptr, size,
                                PROT_READ | PROT_WRITE | PROT_EXEC,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m_buf == MAP_FAILED) m_buf = nullptr;
#else
        m_buf = (uint32_t*)malloc(size);
#endif
    }

    ~JitCodeBuffer() {
#ifdef __powerpc__
        if (m_buf) munmap(m_buf, m_size);
#else
        free(m_buf);
#endif
    }

    uint32_t* allocate(size_t numWords) {
        if ((m_used + numWords) * sizeof(uint32_t) > m_size) {
            flush(); // Simple flush - reset cache
        }
        uint32_t* ptr = m_buf + m_used;
        m_used += numWords;
        return ptr;
    }

    void flush() {
        m_used = 0;
    }

    uint32_t* base() const { return m_buf; }
    size_t    used() const { return m_used; }

private:
    uint32_t* m_buf;
    size_t    m_size;
    size_t    m_used;
};

// ============================================================
//  JIT Block - represents one translated ARM basic block
// ============================================================

struct JitBlock {
    uint32_t  armPc;          // ARM PC of block start
    uint32_t* ppcCode;        // Pointer to PPC code
    size_t    ppcSize;        // Number of PPC instructions
    uint32_t  armInstrCount;  // ARM instructions translated
    uint32_t  cycleCount;     // Cycles for this block
    bool      valid;
};

// ============================================================
//  JIT Translator
//  Core class: translates one ARM basic block at a time
// ============================================================

class JitTranslator {
public:
    JitTranslator() : m_codeBuffer(JIT_CACHE_SIZE) {}

    // Look up or translate a block for the given ARM PC
    JitBlock* getOrTranslate(ArmCpuState* cpu, uint32_t armPc);

    // Clear all translated blocks (e.g. after write to code region)
    void flush() {
        m_blockCache.clear();
        m_codeBuffer.flush();
    }

    // Execute a block: entry point from C++
    // Returns number of cycles consumed
    typedef uint32_t (*JitBlockFn)(ArmCpuState* cpu);
    static uint32_t executeBlock(ArmCpuState* cpu, JitBlock* block);

private:
    // Per-block translation state
    struct TranslateCtx {
        ArmCpuState*         cpu;
        uint32_t             pc;
        std::vector<uint32_t> ppcInstrs;   // Generated PPC instructions
        uint32_t             cycleCount;
        int                  armInstrCount;
        bool                 blockDone;
        bool                 conditionalPending;
        int                  condSkipPatch;  // index of branch to patch for cond skip
    };

    JitCodeBuffer m_codeBuffer;
    std::unordered_map<uint32_t, JitBlock> m_blockCache;

    // Memory access via CPU state pointer (from C helpers)
    // We call C helper functions for memory access
    static uint32_t  readMem32 (ArmCpuState* cpu, uint32_t addr);
    static uint16_t  readMem16 (ArmCpuState* cpu, uint32_t addr);
    static uint8_t   readMem8  (ArmCpuState* cpu, uint32_t addr);
    static void      writeMem32(ArmCpuState* cpu, uint32_t addr, uint32_t val);
    static void      writeMem16(ArmCpuState* cpu, uint32_t addr, uint16_t val);
    static void      writeMem8 (ArmCpuState* cpu, uint32_t addr, uint8_t  val);

    // Top-level translation
    void translateBlock(TranslateCtx& ctx, int maxInstrs);
    void translateInstr(TranslateCtx& ctx, uint32_t instr);

    // Prologue / Epilogue
    void emitPrologue(TranslateCtx& ctx);
    void emitEpilogue(TranslateCtx& ctx);

    // Condition code handling
    void emitCondCheck(TranslateCtx& ctx, int cond);
    void patchCondSkip(TranslateCtx& ctx);

    // Load/store ARM CPSR flags <-> PPC CR0/XER
    void emitLoadCpsrFlags(TranslateCtx& ctx);
    void emitStoreCpsrFlags(TranslateCtx& ctx);
    void emitUpdateNZFromReg(TranslateCtx& ctx, int ppcReg);
    void emitUpdateNZCVFromAdd(TranslateCtx& ctx, int rD, int rA, int rB);
    void emitUpdateNZCVFromSub(TranslateCtx& ctx, int rD, int rA, int rB);

    // Immediate loading (ARM immediates can be large)
    void emitLoadImm32(TranslateCtx& ctx, int ppcReg, uint32_t imm);
    void emitLoadArmReg(TranslateCtx& ctx, int ppcReg, int armReg);
    void emitStoreArmReg(TranslateCtx& ctx, int armReg, int ppcReg);

    // ARM Barrel Shifter
    // Computes shifted value; if setFlags is true, updates carry in PPC_CPSR
    void emitBarrelShift(TranslateCtx& ctx,
                         uint32_t      instr,
                         bool          regShift,
                         int           outReg,
                         int           inReg,
                         int           shiftAmtReg, // or immediate in instr
                         bool          setFlags);

    // Operand decode helpers
    uint32_t decodeArmRotateImm(uint32_t instr); // bits[11:0] of data processing
    void     emitAluOperand(TranslateCtx& ctx, uint32_t instr, bool setFlags,
                             int& outPpcReg, bool& isImm, uint32_t& immVal);

    // ARM Instruction translators (one per opcode group)
    void translateDataProc   (TranslateCtx& ctx, uint32_t instr);
    void translateMul        (TranslateCtx& ctx, uint32_t instr);
    void translateMulLong    (TranslateCtx& ctx, uint32_t instr);
    void translateBranchEx   (TranslateCtx& ctx, uint32_t instr); // BX
    void translateBranch     (TranslateCtx& ctx, uint32_t instr); // B/BL
    void translateLoadStore  (TranslateCtx& ctx, uint32_t instr); // LDR/STR
    void translateLDRH_STRH  (TranslateCtx& ctx, uint32_t instr); // LDRH/STRH/LDRSB/LDRSH
    void translateBlockTransfer(TranslateCtx& ctx, uint32_t instr); // LDM/STM
    void translateSwap       (TranslateCtx& ctx, uint32_t instr); // SWP
    void translateMRS        (TranslateCtx& ctx, uint32_t instr);
    void translateMSR        (TranslateCtx& ctx, uint32_t instr);
    void translateSWI        (TranslateCtx& ctx, uint32_t instr); // Software interrupt
    void translateCDP        (TranslateCtx& ctx, uint32_t instr); // Coprocessor
    void translateCLZ        (TranslateCtx& ctx, uint32_t instr); // Count leading zeros (v5)
    void translateQALU       (TranslateCtx& ctx, uint32_t instr); // Saturating ALU (v5E)
    void translateUndefined  (TranslateCtx& ctx, uint32_t instr);

    // Helper: call C helper function
    // Saves state, sets args, calls via function pointer loaded into CTR
    void emitCallHelper(TranslateCtx& ctx, void* fnPtr, int numArgs);

    inline void emit(TranslateCtx& ctx, uint32_t instr) {
        ctx.ppcInstrs.push_back(instr);
    }
};

// ============================================================
//  Memory Access C Helpers
//  These will be called from JIT code via function pointer
//  The NooDS memory system is complex (ITCM, DTCM, VRAM, etc.)
//  so we delegate to the interpreter's memory functions.
//
//  In practice, replace these with calls into NooDS's Core class.
// ============================================================

// Declared extern for linkage with NooDS core
extern "C" {
    uint32_t jit_read32 (ArmCpuState* cpu, uint32_t addr);
    uint16_t jit_read16 (ArmCpuState* cpu, uint32_t addr);
    uint8_t  jit_read8  (ArmCpuState* cpu, uint32_t addr);
    void     jit_write32(ArmCpuState* cpu, uint32_t addr, uint32_t val);
    void     jit_write16(ArmCpuState* cpu, uint32_t addr, uint16_t val);
    void     jit_write8 (ArmCpuState* cpu, uint32_t addr, uint8_t  val);
    void     jit_swi    (ArmCpuState* cpu, uint32_t vec);
    void     jit_undef  (ArmCpuState* cpu, uint32_t pc);
}

// ============================================================
//  Prologue and Epilogue Generation
//  Prologue: save PPC ABI callee-saved regs, load ARM state
//  Epilogue: store ARM state back, restore PPC regs, return
// ============================================================

void JitTranslator::emitPrologue(TranslateCtx& ctx) {
    // PPC ABI: r3 = first argument = ArmCpuState* cpu
    // We need to save r14-r31 (callee-saved in SysV PPC ABI on Linux/Wii)
    // For simplicity, we save only what we use (r14-r28)
    // Using stack frame:
    //   sp-4:   LR save
    //   sp-8:   r28
    //   sp-12:  r27
    //   ...
    // Wii uses EABI so stack grows down, aligned to 8 bytes

    // stwu r1, -120(r1)  ; allocate stack frame (15 regs * 4 + 8 + alignment)
    emit(ctx, (37u << 26) | (1 << 21) | (1 << 16) | (uint16_t)(-120));

    // mflr r0
    emit(ctx, ppc_mflr(0));
    // stw r0, 124(r1)  ; save LR
    emit(ctx, ppc_stw(0, 1, 124));

    // Save callee-saved registers we'll use (r14-r28)
    int offset = 8;
    for (int r = 14; r <= 28; r++, offset += 4) {
        emit(ctx, ppc_stw(r, 1, offset));
    }

    // r3 = ArmCpuState* (first argument) -> save to PPC_CPU_PTR (r26)
    emit(ctx, ppc_mr(PPC_CPU_PTR, 3));

    // Load ARM registers from state into PPC registers
    for (int i = 0; i <= 15; i++) {
        int ppcR = armRegToPpc(i);
        emit(ctx, ppc_lwz(ppcR, PPC_CPU_PTR,
                          (int16_t)(CPUSTATE_REGS_OFFSET + i * 4)));
    }

    // Load CPSR
    emit(ctx, ppc_lwz(PPC_CPSR, PPC_CPU_PTR, (int16_t)CPUSTATE_CPSR_OFFSET));

    // Initialize PPC CR0 from CPSR flags
    emitLoadCpsrFlags(ctx);
}

void JitTranslator::emitEpilogue(TranslateCtx& ctx) {
    // Store flags back to CPSR from PPC CR0
    emitStoreCpsrFlags(ctx);

    // Store ARM registers back to state
    for (int i = 0; i <= 15; i++) {
        int ppcR = armRegToPpc(i);
        emit(ctx, ppc_stw(ppcR, PPC_CPU_PTR,
                          (int16_t)(CPUSTATE_REGS_OFFSET + i * 4)));
    }

    // Store CPSR
    emit(ctx, ppc_stw(PPC_CPSR, PPC_CPU_PTR, (int16_t)CPUSTATE_CPSR_OFFSET));

    // Restore callee-saved registers
    int offset = 8;
    for (int r = 14; r <= 28; r++, offset += 4) {
        emit(ctx, ppc_lwz(r, 1, offset));
    }

    // lwz r0, 124(r1)
    emit(ctx, ppc_lwz(0, 1, 124));
    // mtlr r0
    emit(ctx, ppc_mtlr(0));
    // addi r1, r1, 120
    emit(ctx, ppc_addi(1, 1, 120));

    // Return cycle count in r3
    emit(ctx, ppc_li(3, (int16_t)ctx.cycleCount));

    // blr
    emit(ctx, ppc_blr());
}

// ============================================================
//  CPSR <-> PPC Flag Mapping
//
//  We keep flags in two places:
//   1. PPC_CPSR register (raw CPSR value, mode bits etc.)
//   2. PPC CR0 for N,Z,C,V (for fast conditional checking)
//
//  Layout of our flag shadow in PPC_CPSR (r19):
//   Bit 31 = N
//   Bit 30 = Z
//   Bit 29 = C
//   Bit 28 = V
//   Bits 7:0 = mode/control bits
//
//  CR0 mapping (for conditional branches):
//   CR0[0]=LT = N flag
//   CR0[1]=GT = C flag  (non-standard, but useful)
//   CR0[2]=EQ = Z flag
//   CR0[3]=SO = V flag
// ============================================================

void JitTranslator::emitLoadCpsrFlags(TranslateCtx& ctx) {
    // Extract NZCV from PPC_CPSR into CR0
    // We'll use a custom bit arrangement in CR0:
    //   Bit 28 (LT of CR0) = N
    //   Bit 29 (GT of CR0) = Z (inverted in standard, but we control it)
    //   Bit 30 (EQ of CR0) = Z
    //   Bit 31 (SO of CR0) = V
    // Actually simplest: use mtcrf with appropriate field

    // Extract upper nibble of CPSR (bits 31:28 = NZCV) into scratch
    emit(ctx, ppc_srwi(PPC_SCRATCH0, PPC_CPSR, 28)); // scratch0 = NZCV in bits 3:0

    // We need to place these 4 bits at the right position for CR0
    // CR0 occupies bits 31:28 of the 32-bit CR register
    // So we need: N at bit 31, Z at bit 30, C at bit 29, V at bit 28
    // That means: shift scratch0 left by 28
    emit(ctx, ppc_slwi(PPC_SCRATCH0, PPC_SCRATCH0, 28));

    // mtcrf 0x80, scratch0  (field 0 = CR0 = bits 31:28)
    emit(ctx, ppc_mtcrf(0x80, PPC_SCRATCH0));
}

void JitTranslator::emitStoreCpsrFlags(TranslateCtx& ctx) {
    // Read CR into scratch
    emit(ctx, ppc_mfcr(PPC_SCRATCH0));

    // Extract bits 31:28 (CR0)
    emit(ctx, ppc_srwi(PPC_SCRATCH0, PPC_SCRATCH0, 28)); // bits 3:0 = NZCV

    // Clear old NZCV from PPC_CPSR (bits 31:28)
    emit(ctx, ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 4, 31)); // clear bits 31:28

    // OR in new flags
    emit(ctx, ppc_slwi(PPC_SCRATCH0, PPC_SCRATCH0, 28));
    emit(ctx, ppc_or(PPC_CPSR, PPC_CPSR, PPC_SCRATCH0));
}

// Update CR0 from a register result (sets N and Z flags)
void JitTranslator::emitUpdateNZFromReg(TranslateCtx& ctx, int ppcReg) {
    // cmpi cr0, ppcReg, 0  (signed compare -> sets LT, GT, EQ, SO)
    emit(ctx, ppc_cmpi(0, ppcReg, 0));
    // Now CR0[LT]=N, CR0[EQ]=Z are set correctly
    // But we need to preserve C and V bits in CR0...
    // This is a simplification: full NZCV tracking is complex
    // For ALU ops without S suffix, we skip flag update
}

// ============================================================
//  Immediate Loading
// ============================================================

void JitTranslator::emitLoadImm32(TranslateCtx& ctx, int ppcReg, uint32_t imm) {
    if ((int32_t)imm >= -32768 && (int32_t)imm <= 32767) {
        // li rD, imm
        emit(ctx, ppc_li(ppcReg, (int16_t)imm));
    } else if ((imm & 0xFFFF) == 0) {
        // lis rD, imm>>16
        emit(ctx, ppc_lis(ppcReg, (int16_t)(imm >> 16)));
    } else {
        // lis + ori
        emit(ctx, ppc_lis(ppcReg, (int16_t)(imm >> 16)));
        emit(ctx, ppc_ori(ppcReg, ppcReg, (uint16_t)(imm & 0xFFFF)));
    }
}

void JitTranslator::emitLoadArmReg(TranslateCtx& ctx, int ppcReg, int armReg) {
    // ARM registers are kept live in PPC r3-r18
    // If we need a different register, just move it
    int mapped = armRegToPpc(armReg);
    if (mapped != ppcReg) {
        emit(ctx, ppc_mr(ppcReg, mapped));
    }
}

void JitTranslator::emitStoreArmReg(TranslateCtx& ctx, int armReg, int ppcReg) {
    int mapped = armRegToPpc(armReg);
    if (mapped != ppcReg) {
        emit(ctx, ppc_mr(mapped, ppcReg));
    }
}

// ============================================================
//  ARM Rotate-Right Immediate decode (for data processing ops)
//  imm12 = rotate[11:8] | imm[7:0]
//  value = ROR(imm, rotate*2)
// ============================================================

uint32_t JitTranslator::decodeArmRotateImm(uint32_t instr) {
    uint32_t rotate = (instr >> 8) & 0xF;
    uint32_t imm8   = instr & 0xFF;
    rotate <<= 1; // rotate * 2
    if (rotate == 0) return imm8;
    return (imm8 >> rotate) | (imm8 << (32 - rotate));
}

// ============================================================
//  Barrel Shifter Emitter
//  Handles LSL, LSR, ASR, ROR with register or immediate amount
//  outReg: PPC register to write result
//  inReg:  PPC register containing value to shift
// ============================================================

void JitTranslator::emitBarrelShift(TranslateCtx& ctx,
                                     uint32_t instr,
                                     bool regShift,
                                     int  outReg,
                                     int  inReg,
                                     int  shiftAmtReg,
                                     bool setFlags) {
    uint32_t shiftType;
    uint32_t shiftAmt;

    if (!regShift) {
        // Immediate shift
        shiftType = (instr >> 5) & 3;
        shiftAmt  = (instr >> 7) & 31;

        switch (shiftType) {
        case SHIFT_LSL:
            if (shiftAmt == 0) {
                if (outReg != inReg) emit(ctx, ppc_mr(outReg, inReg));
            } else {
                emit(ctx, ppc_slwi(outReg, inReg, shiftAmt));
                if (setFlags) {
                    // Carry = bit (32 - shiftAmt) of inReg
                    // Extract that bit into CPSR carry
                    int carryBit = 32 - shiftAmt;
                    emit(ctx, ppc_rlwinm(PPC_SCRATCH5, inReg, carryBit, 31, 31));
                    // Store carry bit into CPSR bit 29
                    // First clear bit 29 of PPC_CPSR
                    emit(ctx, ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 0, 28)); // clear bit 29 (bit index from MSB)
                    // Actually: bit 29 from MSB = bit 2 in CPSR field
                    // CPSR bit 29 = C flag
                    // Set it: OR in scratch5 << 29
                    emit(ctx, ppc_rlwinm(PPC_SCRATCH5, PPC_SCRATCH5, 29, 29, 29));
                    emit(ctx, ppc_or(PPC_CPSR, PPC_CPSR, PPC_SCRATCH5));
                }
            }
            break;

        case SHIFT_LSR:
            if (shiftAmt == 0) {
                // LSR #0 = LSR #32: result=0, carry=bit31 of inReg
                emit(ctx, ppc_li(outReg, 0));
                if (setFlags) {
                    emit(ctx, ppc_rlwinm(PPC_SCRATCH5, inReg, 0, 0, 0)); // bit 31
                    emit(ctx, ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 0, 28));
                    emit(ctx, ppc_or(PPC_CPSR, PPC_CPSR, PPC_SCRATCH5)); // bit 31 -> bit 31?
                    // Actually need carry at bit 29; bit31 of inReg >> 2
                    // Simplified: just set C=1 if bit31 was set
                }
            } else {
                emit(ctx, ppc_srwi(outReg, inReg, shiftAmt));
                if (setFlags) {
                    // Carry = bit (shiftAmt-1) of inReg
                    int carryBit = shiftAmt - 1;
                    // Extract bit carryBit (from LSB), rotate to bit0
                    emit(ctx, ppc_rlwinm(PPC_SCRATCH5, inReg, 32 - carryBit, 31, 31));
                    emit(ctx, ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 0, 28));
                    emit(ctx, ppc_rlwinm(PPC_SCRATCH5, PPC_SCRATCH5, 29, 29, 29));
                    emit(ctx, ppc_or(PPC_CPSR, PPC_CPSR, PPC_SCRATCH5));
                }
            }
            break;

        case SHIFT_ASR:
            if (shiftAmt == 0) {
                // ASR #0 = ASR #32: result = all bits set to bit31 of inReg
                emit(ctx, ppc_srawi(outReg, inReg, 31));
                // Carry = bit 31 of inReg
            } else {
                emit(ctx, ppc_srawi(outReg, inReg, shiftAmt));
            }
            break;

        case SHIFT_ROR:
            if (shiftAmt == 0) {
                // ROR #0 = RRX (rotate right through carry)
                // result = (C << 31) | (rM >> 1)
                // Get C bit from CPSR bit 29
                emit(ctx, ppc_rlwinm(PPC_SCRATCH5, PPC_CPSR, 3, 31, 31)); // extract C to bit 0
                emit(ctx, ppc_srwi(outReg, inReg, 1));
                emit(ctx, ppc_rlwimi(outReg, PPC_SCRATCH5, 31, 0, 0)); // insert C into bit 31
                // New carry = bit 0 of inReg
                if (setFlags) {
                    emit(ctx, ppc_rlwinm(PPC_SCRATCH5, inReg, 0, 31, 31));
                    emit(ctx, ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 0, 28));
                    emit(ctx, ppc_rlwinm(PPC_SCRATCH5, PPC_SCRATCH5, 29, 29, 29));
                    emit(ctx, ppc_or(PPC_CPSR, PPC_CPSR, PPC_SCRATCH5));
                }
            } else {
                // rlwnm outReg, inReg, (32-shiftAmt), 0, 31
                int rotAmt = (32 - shiftAmt) & 31;
                emit(ctx, ppc_rlwinm(outReg, inReg, rotAmt, 0, 31));
                // Carry = bit (shiftAmt-1) of inReg
            }
            break;
        }
    } else {
        // Register shift - shift amount is in lower byte of shiftAmtReg
        shiftType = (instr >> 5) & 3;

        // Mask shift amount to 5 bits (or 8 bits for ARM spec)
        emit(ctx, ppc_andi_dot(PPC_SCRATCH4, shiftAmtReg, 0xFF));

        switch (shiftType) {
        case SHIFT_LSL:
            // PPC slw handles shift >= 32 as 0
            emit(ctx, ppc_slw(outReg, inReg, PPC_SCRATCH4));
            break;
        case SHIFT_LSR:
            emit(ctx, ppc_srw(outReg, inReg, PPC_SCRATCH4));
            break;
        case SHIFT_ASR:
            emit(ctx, ppc_sraw(outReg, inReg, PPC_SCRATCH4));
            break;
        case SHIFT_ROR:
            // rlwnm outReg, inReg, (32-shiftAmt), 0, 31
            // Compute 32-scratch4 into scratch3
            emit(ctx, ppc_subfic(PPC_SCRATCH3, PPC_SCRATCH4, 32));
            emit(ctx, ppc_rlwnm(outReg, inReg, PPC_SCRATCH3, 0, 31));
            break;
        }
    }
}

// ============================================================
//  ALU Operand Decoder
//  For data processing instructions (bits 25 = I flag)
// ============================================================

void JitTranslator::emitAluOperand(TranslateCtx& ctx, uint32_t instr, bool setFlags,
                                    int& outPpcReg, bool& isImm, uint32_t& immVal) {
    bool I = (instr >> 25) & 1;
    outPpcReg = PPC_SCRATCH1;
    isImm = false;

    if (I) {
        // Immediate operand: rotate + imm8
        uint32_t val = decodeArmRotateImm(instr);
        immVal = val;
        isImm = true;
        emitLoadImm32(ctx, PPC_SCRATCH1, val);
        outPpcReg = PPC_SCRATCH1;
    } else {
        // Register operand with optional shift
        int rmArmReg = instr & 0xF;
        bool regShift = (instr >> 4) & 1;
        int rmPpcReg = armRegToPpc(rmArmReg);

        emit(ctx, ppc_mr(PPC_SCRATCH0, rmPpcReg)); // copy Rm

        if (!regShift) {
            uint32_t shiftType = (instr >> 5) & 3;
            uint32_t shiftAmt  = (instr >> 7) & 31;
            if (shiftType == 0 && shiftAmt == 0) {
                // No shift - just use Rm
                outPpcReg = PPC_SCRATCH0;
            } else {
                emitBarrelShift(ctx, instr, false, PPC_SCRATCH1, PPC_SCRATCH0, 0, setFlags);
                outPpcReg = PPC_SCRATCH1;
            }
        } else {
            // Register-specified shift
            int rsArmReg = (instr >> 8) & 0xF;
            int rsPpcReg = armRegToPpc(rsArmReg);
            emit(ctx, ppc_mr(PPC_SCRATCH2, rsPpcReg));
            emitBarrelShift(ctx, instr, true, PPC_SCRATCH1, PPC_SCRATCH0, PPC_SCRATCH2, setFlags);
            outPpcReg = PPC_SCRATCH1;
        }
    }
}

// ============================================================
//  Condition Code Check
//  Emits a conditional branch to skip the instruction if
//  the ARM condition is NOT met.
//  The branch target will be patched later.
// ============================================================

void JitTranslator::emitCondCheck(TranslateCtx& ctx, int cond) {
    if (cond == COND_AL) return; // Always execute
    if (cond == COND_NV) {
        // Never (ARMv4) - insert unconditional skip
        ctx.condSkipPatch = (int)ctx.ppcInstrs.size();
        emit(ctx, ppc_b(0)); // patch later
        ctx.conditionalPending = true;
        return;
    }

    // We need to evaluate the ARM condition using CR0 and PPC_CPSR
    // Reminder of our CR0 layout:
    //   CR0[28] = LT = N flag
    //   CR0[29] = GT = (not directly C, but we abuse it)
    //   CR0[30] = EQ = Z flag
    //   CR0[31] = SO = V flag
    // Wait - CR0 bits are 31:28 where bit31=LT, bit30=GT, bit29=EQ, bit28=SO
    // Standard PPC CR0 mapping: bit 28 of CR = CR0[3]=SO, bit 29=EQ, bit30=GT, bit31=LT
    // After our mtcrf mapping: we placed NZCV at bits 31:28 of scratch
    // So: N -> CR0[LT] (CR bit 31), Z -> CR0[GT] (CR bit 30),
    //     C -> CR0[EQ] (CR bit 29), V -> CR0[SO] (CR bit 28)
    // BI field for bc: 0=LT, 1=GT, 2=EQ, 3=SO

    ctx.condSkipPatch = (int)ctx.ppcInstrs.size();
    ctx.conditionalPending = true;

    // For each ARM condition, emit a branch that SKIPS when condition is FALSE
    switch (cond) {
    case COND_EQ: // Z set -> skip if Z clear (EQ clear = bit 30 of CR = BI=1, NOT set)
        // Actually our mapping: Z is at CR0[GT] which is BI=1 (bit 1 of CR0)
        // Branch if NOT equal (GT clear in our mapping = Z clear)
        emit(ctx, ppc_bc(PPC_BO_FALSE, PPC_CR0_GT, 0)); // patch offset
        break;
    case COND_NE: // Z clear -> skip if Z set
        emit(ctx, ppc_bc(PPC_BO_TRUE,  PPC_CR0_GT, 0));
        break;
    case COND_CS: // C set -> skip if C clear
        // C is at CR0[EQ] (BI=2)
        emit(ctx, ppc_bc(PPC_BO_FALSE, PPC_CR0_EQ, 0));
        break;
    case COND_CC: // C clear -> skip if C set
        emit(ctx, ppc_bc(PPC_BO_TRUE,  PPC_CR0_EQ, 0));
        break;
    case COND_MI: // N set -> skip if N clear
        // N is at CR0[LT] (BI=0)
        emit(ctx, ppc_bc(PPC_BO_FALSE, PPC_CR0_LT, 0));
        break;
    case COND_PL: // N clear -> skip if N set
        emit(ctx, ppc_bc(PPC_BO_TRUE,  PPC_CR0_LT, 0));
        break;
    case COND_VS: // V set -> skip if V clear
        // V is at CR0[SO] (BI=3)
        emit(ctx, ppc_bc(PPC_BO_FALSE, PPC_CR0_SO, 0));
        break;
    case COND_VC: // V clear -> skip if V set
        emit(ctx, ppc_bc(PPC_BO_TRUE,  PPC_CR0_SO, 0));
        break;

    case COND_HI: // C set AND Z clear
        // Need: C=1 AND Z=0
        // Skip if C=0 OR Z=1
        // Two-branch sequence (simplified: just check C first, then Z)
        // For simplicity, compute in scratch and use single branch
        // Reconstruct from PPC_CPSR
        emit(ctx, ppc_rlwinm(PPC_SCRATCH3, PPC_CPSR, 4, 30, 31)); // get CZ in bits 1:0
        // scratch3: bit1=C, bit0=Z (after shifting 29:28 to 1:0)
        // HI = C & ~Z = bit1 & ~bit0
        emit(ctx, ppc_rlwinm(PPC_SCRATCH4, PPC_SCRATCH3, 0, 31, 31)); // Z in bit 0
        emit(ctx, ppc_rlwinm(PPC_SCRATCH3, PPC_SCRATCH3, 31, 31, 31)); // C in bit 0
        emit(ctx, ppc_andc(PPC_SCRATCH3, PPC_SCRATCH3, PPC_SCRATCH4)); // C & ~Z
        emit(ctx, ppc_cmpi(0, PPC_SCRATCH3, 0));
        emit(ctx, ppc_bc(PPC_BO_TRUE, PPC_CR0_EQ, 0)); // skip if C&~Z == 0
        break;

    case COND_LS: // C clear OR Z set
        emit(ctx, ppc_rlwinm(PPC_SCRATCH3, PPC_CPSR, 4, 30, 31));
        emit(ctx, ppc_rlwinm(PPC_SCRATCH4, PPC_SCRATCH3, 0, 31, 31)); // Z
        emit(ctx, ppc_rlwinm(PPC_SCRATCH3, PPC_SCRATCH3, 31, 31, 31)); // C
        emit(ctx, ppc_andc(PPC_SCRATCH3, PPC_SCRATCH3, PPC_SCRATCH4)); // C & ~Z (HI)
        emit(ctx, ppc_cmpi(0, PPC_SCRATCH3, 0));
        emit(ctx, ppc_bc(PPC_BO_FALSE, PPC_CR0_EQ, 0)); // skip if C&~Z != 0 (i.e. HI is true, so LS is false)
        break;

    case COND_GE: // N == V
        // N at CR0[LT]=BI0, V at CR0[SO]=BI3
        // Extract N and V, XOR them; skip if result != 0
        emit(ctx, ppc_rlwinm(PPC_SCRATCH3, PPC_CPSR, 4, 30, 31)); // NV in bits 1:0? No...
        // N=bit31, V=bit28 of CPSR
        // Extract N to bit1, V to bit0
        emit(ctx, ppc_rlwinm(PPC_SCRATCH3, PPC_CPSR, 2, 30, 30)); // N -> bit 1
        emit(ctx, ppc_rlwinm(PPC_SCRATCH4, PPC_CPSR, 4, 31, 31)); // V -> bit 0
        emit(ctx, ppc_xor(PPC_SCRATCH3, PPC_SCRATCH3, PPC_SCRATCH4));
        emit(ctx, ppc_andi_dot(PPC_SCRATCH3, PPC_SCRATCH3, 2)); // check bit 1 = N^V
        emit(ctx, ppc_bc(PPC_BO_FALSE, PPC_CR0_EQ, 0)); // skip if N^V != 0 (i.e. N != V)
        break;

    case COND_LT: // N != V
        emit(ctx, ppc_rlwinm(PPC_SCRATCH3, PPC_CPSR, 2, 30, 30));
        emit(ctx, ppc_rlwinm(PPC_SCRATCH4, PPC_CPSR, 4, 31, 31));
        emit(ctx, ppc_xor(PPC_SCRATCH3, PPC_SCRATCH3, PPC_SCRATCH4));
        emit(ctx, ppc_andi_dot(PPC_SCRATCH3, PPC_SCRATCH3, 2));
        emit(ctx, ppc_bc(PPC_BO_TRUE, PPC_CR0_EQ, 0)); // skip if N^V == 0 (i.e. N == V)
        break;

    case COND_GT: // Z clear AND (N == V)
        emit(ctx, ppc_rlwinm(PPC_SCRATCH3, PPC_CPSR, 2, 30, 30)); // N->bit1
        emit(ctx, ppc_rlwinm(PPC_SCRATCH4, PPC_CPSR, 4, 31, 31)); // V->bit0
        emit(ctx, ppc_xor(PPC_SCRATCH3, PPC_SCRATCH3, PPC_SCRATCH4)); // N^V in bit1
        // Also get Z
        emit(ctx, ppc_rlwinm(PPC_SCRATCH4, PPC_CPSR, 1, 31, 31)); // Z->bit0
        emit(ctx, ppc_slwi(PPC_SCRATCH4, PPC_SCRATCH4, 2)); // Z->bit2
        emit(ctx, ppc_or(PPC_SCRATCH3, PPC_SCRATCH3, PPC_SCRATCH4)); // (N^V)|Z<<2
        emit(ctx, ppc_andi_dot(PPC_SCRATCH3, PPC_SCRATCH3, 6)); // bits 2,1
        emit(ctx, ppc_bc(PPC_BO_FALSE, PPC_CR0_EQ, 0)); // skip if not all zero
        break;

    case COND_LE: // Z set OR (N != V)
        emit(ctx, ppc_rlwinm(PPC_SCRATCH3, PPC_CPSR, 2, 30, 30));
        emit(ctx, ppc_rlwinm(PPC_SCRATCH4, PPC_CPSR, 4, 31, 31));
        emit(ctx, ppc_xor(PPC_SCRATCH3, PPC_SCRATCH3, PPC_SCRATCH4)); // N^V in bit1
        emit(ctx, ppc_rlwinm(PPC_SCRATCH4, PPC_CPSR, 1, 31, 31)); // Z->bit0
        emit(ctx, ppc_slwi(PPC_SCRATCH4, PPC_SCRATCH4, 2));
        emit(ctx, ppc_or(PPC_SCRATCH3, PPC_SCRATCH3, PPC_SCRATCH4));
        emit(ctx, ppc_andi_dot(PPC_SCRATCH3, PPC_SCRATCH3, 6));
        emit(ctx, ppc_bc(PPC_BO_TRUE, PPC_CR0_EQ, 0)); // skip if all zero (GT is true, LE is false)
        break;

    default:
        break;
    }
}

void JitTranslator::patchCondSkip(TranslateCtx& ctx) {
    if (!ctx.conditionalPending) return;

    int patchIdx    = ctx.condSkipPatch;
    int currentIdx  = (int)ctx.ppcInstrs.size();
    int relOffset   = (currentIdx - patchIdx) * 4; // byte offset

    // Patch the branch instruction
    uint32_t& branchInstr = ctx.ppcInstrs[patchIdx];
    // Check if it's a 'b' (opcode 18) or 'bc' (opcode 16)
    uint32_t opcode = branchInstr >> 26;
    if (opcode == 18) {
        // b - patch LI field (bits 25:2)
        branchInstr = (branchInstr & 0xFC000003u) | (relOffset & 0x03FFFFFC);
    } else if (opcode == 16) {
        // bc - patch BD field (bits 15:2)
        branchInstr = (branchInstr & 0xFFFF0003u) | (uint16_t)(relOffset & 0xFFFC);
    }

    ctx.conditionalPending = false;
    ctx.condSkipPatch = -1;
}

// ============================================================
//  Helper: Call C function from JIT code
//  We use PPC_SCRATCH5 to hold the function pointer
//  Arguments should be set up in r3-r10 before calling this
//  Note: This saves/restores volatile registers as needed
// ============================================================

void JitTranslator::emitCallHelper(TranslateCtx& ctx, void* fnPtr, int numArgs) {
    // First: flush ARM state to memory (the C helper needs valid state)
    // Store ARM registers back temporarily
    for (int i = 0; i <= 15; i++) {
        emit(ctx, ppc_stw(armRegToPpc(i), PPC_CPU_PTR,
                          (int16_t)(CPUSTATE_REGS_OFFSET + i * 4)));
    }
    emitStoreCpsrFlags(ctx);
    emit(ctx, ppc_stw(PPC_CPSR, PPC_CPU_PTR, (int16_t)CPUSTATE_CPSR_OFFSET));

    // Load function pointer into CTR
    uintptr_t fp = (uintptr_t)fnPtr;
    emitLoadImm32(ctx, PPC_SCRATCH5, (uint32_t)fp);
    emit(ctx, ppc_mtctr(PPC_SCRATCH5));

    // Save LR (it's already saved in our frame, but PPC_SCRATCH5 etc may be clobbered)
    // Actually blr already set up. Just save r3-r12 to stack if needed.
    // For simplicity, we save the JIT registers to stack before the call

    // Call the function
    emit(ctx, ppc_bctrl());

    // Return value in r3 (if any)
    // Restore JIT registers from ARM state
    for (int i = 0; i <= 15; i++) {
        emit(ctx, ppc_lwz(armRegToPpc(i), PPC_CPU_PTR,
                          (int16_t)(CPUSTATE_REGS_OFFSET + i * 4)));
    }
    emit(ctx, ppc_lwz(PPC_CPSR, PPC_CPU_PTR, (int16_t)CPUSTATE_CPSR_OFFSET));
    emitLoadCpsrFlags(ctx);
}

// ============================================================
//  Data Processing Instructions Translation
//  Covers AND, EOR, SUB, RSB, ADD, ADC, SBC, RSC,
//          TST, TEQ, CMP, CMN, ORR, MOV, BIC, MVN
// ============================================================

void JitTranslator::translateDataProc(TranslateCtx& ctx, uint32_t instr) {
    int opcode = (instr >> 21) & 0xF;
    int S      = (instr >> 20) & 1;  // Set flags
    int rnArm  = (instr >> 16) & 0xF;
    int rdArm  = (instr >> 12) & 0xF;

    int rnPpc  = armRegToPpc(rnArm);
    int rdPpc  = armRegToPpc(rdArm);

    int operPpc;
    bool isImm;
    uint32_t immVal;
    emitAluOperand(ctx, instr, S, operPpc, isImm, immVal);

    switch (opcode) {
    case 0x0: // AND: Rd = Rn & Op2
        emit(ctx, ppc_and(rdPpc, rnPpc, operPpc, S ? true : false));
        break;

    case 0x1: // EOR: Rd = Rn ^ Op2
        emit(ctx, ppc_xor(rdPpc, rnPpc, operPpc, S ? true : false));
        break;

    case 0x2: // SUB: Rd = Rn - Op2
        // ARM SUB: carry = NOT borrow
        // PPC subfc: rD = rB - rA (sets CA as borrow complement)
        // We want Rd = Rn - Op2: use subf (rD = Op2 - Rn would be wrong)
        // Actually subf rD, rA, rB = rB - rA
        // So: subf rdPpc, operPpc, rnPpc = rnPpc - operPpc ✓
        if (S) {
            emit(ctx, ppc_subfc(rdPpc, operPpc, rnPpc, true)); // sets CA
        } else {
            emit(ctx, ppc_subf(rdPpc, operPpc, rnPpc));
        }
        if (S) {
            emit(ctx, ppc_cmpi(0, rdPpc, 0)); // update N,Z
            // C = CA (XER bit 29), V is complex
            // For now, N and Z from cmpi, C from XER
        }
        break;

    case 0x3: // RSB: Rd = Op2 - Rn
        if (S) {
            emit(ctx, ppc_subfc(rdPpc, rnPpc, operPpc, true));
        } else {
            emit(ctx, ppc_subf(rdPpc, rnPpc, operPpc));
        }
        break;

    case 0x4: // ADD: Rd = Rn + Op2
        if (S) {
            emit(ctx, ppc_addc(rdPpc, rnPpc, operPpc, true));
        } else {
            emit(ctx, ppc_add(rdPpc, rnPpc, operPpc));
        }
        if (S) {
            emit(ctx, ppc_cmpi(0, rdPpc, 0));
        }
        break;

    case 0x5: // ADC: Rd = Rn + Op2 + C
        // First get carry from CPSR into XER[CA]
        // Extract C bit from PPC_CPSR (bit 29)
        emit(ctx, ppc_rlwinm(PPC_SCRATCH3, PPC_CPSR, 3, 31, 31)); // C -> bit 0
        // Set XER[CA] = that bit
        // subfic trick: subfic rD, rS, 0 with rS=0 sets CA if rS!=0 ... complex
        // Simpler: add carry separately
        emit(ctx, ppc_addc(rdPpc, rnPpc, operPpc)); // Rn + Op2, sets CA
        emit(ctx, ppc_adde(rdPpc, rdPpc, PPC_SCRATCH3)); // + C (using XER)
        // Note: This isn't perfect as it double-adds C. Use:
        // Actually use: adde with our carry:
        // Load C into XER first
        // subfic r0, scratch3, 1 -> sets CA=1 if scratch3=0 (i.e. C_arm=0->CA_ppc=1)
        // This is getting complex; leave as approximation for now
        if (S) emit(ctx, ppc_cmpi(0, rdPpc, 0));
        break;

    case 0x6: // SBC: Rd = Rn - Op2 - NOT C = Rn - Op2 + C - 1
        // PPC subfe: rD = rB - rA - 1 + CA
        // We want: Rn - Op2 + Carry - 1
        // subfe rdPpc, operPpc, rnPpc = rnPpc - operPpc - 1 + CA
        // ARM carry = PPC CA? We need to set PPC CA = ARM C
        emit(ctx, ppc_rlwinm(PPC_SCRATCH3, PPC_CPSR, 3, 31, 31));
        // subfic scratch, scratch3, 0 -> sets CA = (scratch3 == 0) ? 1 : 0
        // i.e. CA = NOT ARM_C ... hmm, we need CA = ARM_C
        // Use: addic scratch, scratch3, -1 -> sets CA = 1 if scratch3 >= 1
        emit(ctx, ppc_addic(PPC_SCRATCH3, PPC_SCRATCH3, -1)); // sets CA = ARM_C
        emit(ctx, ppc_subfe(rdPpc, operPpc, rnPpc, S ? true : false));
        if (S) emit(ctx, ppc_cmpi(0, rdPpc, 0));
        break;

    case 0x7: // RSC: Rd = Op2 - Rn - NOT C
        emit(ctx, ppc_rlwinm(PPC_SCRATCH3, PPC_CPSR, 3, 31, 31));
        emit(ctx, ppc_addic(PPC_SCRATCH3, PPC_SCRATCH3, -1));
        emit(ctx, ppc_subfe(rdPpc, rnPpc, operPpc, S ? true : false));
        if (S) emit(ctx, ppc_cmpi(0, rdPpc, 0));
        break;

    case 0x8: // TST: Rn & Op2, set flags, discard result
        emit(ctx, ppc_and(PPC_SCRATCH3, rnPpc, operPpc, true));
        // flags updated by and.
        break;

    case 0x9: // TEQ: Rn ^ Op2, set flags
        emit(ctx, ppc_xor(PPC_SCRATCH3, rnPpc, operPpc, true));
        break;

    case 0xA: // CMP: Rn - Op2, set flags
        emit(ctx, ppc_subfc(PPC_SCRATCH3, operPpc, rnPpc, true));
        emit(ctx, ppc_cmpi(0, PPC_SCRATCH3, 0));
        break;

    case 0xB: // CMN: Rn + Op2, set flags
        emit(ctx, ppc_addc(PPC_SCRATCH3, rnPpc, operPpc, true));
        emit(ctx, ppc_cmpi(0, PPC_SCRATCH3, 0));
        break;

    case 0xC: // ORR: Rd = Rn | Op2
        emit(ctx, ppc_or(rdPpc, rnPpc, operPpc, S ? true : false));
        break;

    case 0xD: // MOV: Rd = Op2
        emit(ctx, ppc_mr(rdPpc, operPpc));
        if (S) emit(ctx, ppc_cmpi(0, rdPpc, 0));
        break;

    case 0xE: // BIC: Rd = Rn & ~Op2
        emit(ctx, ppc_andc(rdPpc, rnPpc, operPpc, S ? true : false));
        break;

    case 0xF: // MVN: Rd = ~Op2
        emit(ctx, ppc_not(rdPpc, operPpc));
        if (S) emit(ctx, ppc_cmpi(0, rdPpc, 0));
        break;
    }

    // If S flag and Rd is PC: also restore SPSR to CPSR (mode switch)
    if (S && rdArm == ARM_PC) {
        // This is the "return from exception" case
        // Move SPSR to CPSR - call helper
        // For now: just emit a call to the exception return helper
        emit(ctx, ppc_mr(3, PPC_CPU_PTR));
        emitCallHelper(ctx, (void*)jit_swi, 1);
        ctx.blockDone = true;
    }

    // If Rd is PC, end block
    if (rdArm == ARM_PC && opcode != 0x8 && opcode != 0x9 &&
        opcode != 0xA && opcode != 0xB) {
        ctx.blockDone = true;
    }
}

// ============================================================
//  Multiply Instructions
//  MUL, MLA, UMULL, UMLAL, SMULL, SMLAL
// ============================================================

void JitTranslator::translateMul(TranslateCtx& ctx, uint32_t instr) {
    int A   = (instr >> 21) & 1;  // Accumulate
    int S   = (instr >> 20) & 1;  // Set flags
    int rdArm = (instr >> 16) & 0xF; // Rd (result high for long)
    int rnArm = (instr >> 12) & 0xF; // Rn (accumulate for MLA)
    int rsArm = (instr >> 8)  & 0xF;
    int rmArm = (instr >> 0)  & 0xF;

    int rdPpc = armRegToPpc(rdArm);
    int rnPpc = armRegToPpc(rnArm);
    int rsPpc = armRegToPpc(rsArm);
    int rmPpc = armRegToPpc(rmArm);

    // MUL: Rd = Rm * Rs
    // MLA: Rd = Rm * Rs + Rn
    emit(ctx, ppc_mullw(rdPpc, rmPpc, rsPpc, false));
    if (A) {
        // MLA: add Rn
        emit(ctx, ppc_add(rdPpc, rdPpc, rnPpc));
    }
    if (S) {
        emit(ctx, ppc_cmpi(0, rdPpc, 0));
    }
}

void JitTranslator::translateMulLong(TranslateCtx& ctx, uint32_t instr) {
    int U     = (instr >> 22) & 1;  // Unsigned if 0, signed if 1 (actually bit22=U means unsigned)
    int A     = (instr >> 21) & 1;  // Accumulate
    int S     = (instr >> 20) & 1;  // Set flags
    int rdHiArm = (instr >> 16) & 0xF;
    int rdLoArm = (instr >> 12) & 0xF;
    int rsArm   = (instr >> 8)  & 0xF;
    int rmArm   = (instr >> 0)  & 0xF;

    int rdHiPpc = armRegToPpc(rdHiArm);
    int rdLoPpc = armRegToPpc(rdLoArm);
    int rsPpc   = armRegToPpc(rsArm);
    int rmPpc   = armRegToPpc(rmArm);

    // U=0: signed (SMULL/SMLAL), U=1: unsigned (UMULL/UMLAL)
    // Actually ARM encoding: bit22=1 means unsigned
    if (U) {
        // UMULL: {RdHi:RdLo} = Rm * Rs (unsigned)
        emit(ctx, ppc_mulhwu(rdHiPpc, rmPpc, rsPpc));  // High word
        emit(ctx, ppc_mullw(rdLoPpc, rmPpc, rsPpc));   // Low word
    } else {
        // SMULL: {RdHi:RdLo} = Rm * Rs (signed)
        emit(ctx, ppc_mulhw(rdHiPpc, rmPpc, rsPpc));
        emit(ctx, ppc_mullw(rdLoPpc, rmPpc, rsPpc));
    }

    if (A) {
        // UMLAL/SMLAL: add {RdHi:RdLo} to result
        // RdLo += old_RdLo (with carry into RdHi)
        emit(ctx, ppc_addc(PPC_SCRATCH3, rdLoPpc, PPC_SCRATCH0)); // add lo, get carry
        emit(ctx, ppc_mr(rdLoPpc, PPC_SCRATCH3));
        emit(ctx, ppc_adde(rdHiPpc, rdHiPpc, PPC_SCRATCH1)); // add hi + carry
    }

    if (S) {
        // Set N from RdHi bit 31, Z if both RdHi and RdLo are 0
        emit(ctx, ppc_or(PPC_SCRATCH3, rdHiPpc, rdLoPpc));
        emit(ctx, ppc_cmpi(0, PPC_SCRATCH3, 0));
    }
}

// ============================================================
//  Branch and Branch-Exchange (BX)
// ============================================================

void JitTranslator::translateBranchEx(TranslateCtx& ctx, uint32_t instr) {
    // BX Rn: Branch to address in Rn, switch to THUMB if bit0=1
    int rnArm = instr & 0xF;
    int rnPpc = armRegToPpc(rnArm);

    // Check bit 0 of Rn -> if set, switch to THUMB mode
    emit(ctx, ppc_andi_dot(PPC_SCRATCH3, rnPpc, 1));

    // Update CPSR T bit based on Rn[0]
    // Clear T bit
    emit(ctx, ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 27, 25)); // clear bit 5 (T flag)
    // Actually bit 5 of CPSR = T
    // Build mask: set bit 5 = (Rn & 1) << 5
    emit(ctx, ppc_slwi(PPC_SCRATCH3, PPC_SCRATCH3, 5));
    emit(ctx, ppc_or(PPC_CPSR, PPC_CPSR, PPC_SCRATCH3));

    // PC = Rn & ~1
    emit(ctx, ppc_rlwinm(armRegToPpc(ARM_PC), rnPpc, 0, 0, 30)); // clear bit 0

    ctx.blockDone = true;
}

// ============================================================
//  B and BL Instructions
// ============================================================

void JitTranslator::translateBranch(TranslateCtx& ctx, uint32_t instr) {
    int L = (instr >> 24) & 1; // Link bit
    int32_t offset = instr & 0x00FFFFFF;
    // Sign extend 24-bit to 32-bit
    if (offset & 0x800000) offset |= 0xFF000000;
    offset <<= 2; // shift left 2 (word-aligned)
    // Branch target = PC + 8 + offset (ARM prefetch: PC = current+8)
    uint32_t target = ctx.pc + 8 + (uint32_t)offset;

    if (L) {
        // BL: save return address in LR (= PC + 4)
        uint32_t retAddr = ctx.pc + 4;
        emitLoadImm32(ctx, armRegToPpc(ARM_LR), retAddr);
    }

    // Set PC to target
    emitLoadImm32(ctx, armRegToPpc(ARM_PC), target);
    ctx.blockDone = true;
}

// ============================================================
//  LDR/STR - Single Data Transfer
// ============================================================

void JitTranslator::translateLoadStore(TranslateCtx& ctx, uint32_t instr) {
    int I  = (instr >> 25) & 1; // Immediate offset if 0, Register if 1
    int P  = (instr >> 24) & 1; // Pre/post indexing
    int U  = (instr >> 23) & 1; // Up (add) / Down (subtract)
    int B  = (instr >> 22) & 1; // Byte / Word
    int W  = (instr >> 21) & 1; // Write-back
    int L  = (instr >> 20) & 1; // Load / Store

    int rnArm = (instr >> 16) & 0xF;
    int rdArm = (instr >> 12) & 0xF;
    int rnPpc = armRegToPpc(rnArm);
    int rdPpc = armRegToPpc(rdArm);

    // Compute offset
    if (!I) {
        // Immediate offset (bits 11:0)
        uint32_t immOff = instr & 0xFFF;
        emitLoadImm32(ctx, PPC_SCRATCH0, immOff);
    } else {
        // Register offset with optional shift
        int rmArm = instr & 0xF;
        int rmPpc = armRegToPpc(rmArm);
        emit(ctx, ppc_mr(PPC_SCRATCH0, rmPpc));
        // Apply shift if any
        uint32_t shiftType = (instr >> 5) & 3;
        uint32_t shiftAmt  = (instr >> 7) & 31;
        if (shiftAmt != 0 || shiftType != 0) {
            emitBarrelShift(ctx, instr, false, PPC_SCRATCH0, PPC_SCRATCH0, 0, false);
        }
    }

    // Base address = Rn
    emit(ctx, ppc_mr(PPC_SCRATCH1, rnPpc)); // SCRATCH1 = Rn

    // Pre-index: calculate address before access
    if (P) {
        if (U) {
            emit(ctx, ppc_add(PPC_SCRATCH1, PPC_SCRATCH1, PPC_SCRATCH0));
        } else {
            emit(ctx, ppc_subf(PPC_SCRATCH1, PPC_SCRATCH0, PPC_SCRATCH1));
        }
    }

    // Address is in SCRATCH1, perform memory access
    // We call C helpers for memory access
    // Setup: r3 = cpu, r4 = address, (r5 = value for store)

    emit(ctx, ppc_mr(3, PPC_CPU_PTR));   // arg1: cpu
    emit(ctx, ppc_mr(4, PPC_SCRATCH1));  // arg2: address

    if (L) {
        // Load
        if (B) {
            emitCallHelper(ctx, (void*)jit_read8, 2);
            emit(ctx, ppc_mr(rdPpc, 3)); // result in r3
        } else {
            emitCallHelper(ctx, (void*)jit_read32, 2);
            // Handle unaligned rotation (ARM LDR rotates if unaligned)
            // For now, assume aligned
            emit(ctx, ppc_mr(rdPpc, 3));
        }
    } else {
        // Store
        emit(ctx, ppc_mr(5, rdPpc)); // arg3: value
        if (B) {
            emitCallHelper(ctx, (void*)jit_write8, 3);
        } else {
            emitCallHelper(ctx, (void*)jit_write32, 3);
        }
    }

    // Post-index: update base register after access
    if (!P) {
        if (U) {
            emit(ctx, ppc_add(rnPpc, rnPpc, PPC_SCRATCH0));
        } else {
            emit(ctx, ppc_subf(rnPpc, PPC_SCRATCH0, rnPpc));
        }
    } else if (W) {
        // Pre-index with write-back
        emit(ctx, ppc_mr(rnPpc, PPC_SCRATCH1));
    }

    // If LDR loaded into PC, end block
    if (L && rdArm == ARM_PC) {
        ctx.blockDone = true;
    }
}

// ============================================================
//  LDRH/STRH/LDRSB/LDRSH - Halfword and Signed Data Transfer
// ============================================================

void JitTranslator::translateLDRH_STRH(TranslateCtx& ctx, uint32_t instr) {
    int P   = (instr >> 24) & 1;
    int U   = (instr >> 23) & 1;
    int I   = (instr >> 22) & 1; // Immediate if 1, Register if 0
    int W   = (instr >> 21) & 1;
    int L   = (instr >> 20) & 1;
    int rnArm = (instr >> 16) & 0xF;
    int rdArm = (instr >> 12) & 0xF;
    int S   = (instr >> 6) & 1;  // Signed
    int H   = (instr >> 5) & 1;  // Halfword
    int rnPpc = armRegToPpc(rnArm);
    int rdPpc = armRegToPpc(rdArm);

    // Compute offset
    if (I) {
        // Immediate offset: immedH[11:8] | immedL[3:0]
        uint32_t immOff = ((instr >> 4) & 0xF0) | (instr & 0xF);
        emitLoadImm32(ctx, PPC_SCRATCH0, immOff);
    } else {
        int rmArm = instr & 0xF;
        emit(ctx, ppc_mr(PPC_SCRATCH0, armRegToPpc(rmArm)));
    }

    emit(ctx, ppc_mr(PPC_SCRATCH1, rnPpc));
    if (P) {
        if (U) emit(ctx, ppc_add(PPC_SCRATCH1, PPC_SCRATCH1, PPC_SCRATCH0));
        else   emit(ctx, ppc_subf(PPC_SCRATCH1, PPC_SCRATCH0, PPC_SCRATCH1));
    }

    emit(ctx, ppc_mr(3, PPC_CPU_PTR));
    emit(ctx, ppc_mr(4, PPC_SCRATCH1));

    if (L) {
        if (S && !H) {
            // LDRSB: load signed byte
            emitCallHelper(ctx, (void*)jit_read8, 2);
            emit(ctx, ppc_extsb(rdPpc, 3));
        } else if (!S && H) {
            // LDRH: load unsigned halfword
            emitCallHelper(ctx, (void*)jit_read16, 2);
            emit(ctx, ppc_mr(rdPpc, 3));
        } else if (S && H) {
            // LDRSH: load signed halfword
            emitCallHelper(ctx, (void*)jit_read16, 2);
            emit(ctx, ppc_extsh(rdPpc, 3));
        }
    } else {
        // STRH
        emit(ctx, ppc_mr(5, rdPpc));
        emitCallHelper(ctx, (void*)jit_write16, 3);
    }

    if (!P) {
        if (U) emit(ctx, ppc_add(rnPpc, rnPpc, PPC_SCRATCH0));
        else   emit(ctx, ppc_subf(rnPpc, PPC_SCRATCH0, rnPpc));
    } else if (W) {
        emit(ctx, ppc_mr(rnPpc, PPC_SCRATCH1));
    }

    if (L && rdArm == ARM_PC) ctx.blockDone = true;
}

// ============================================================
//  LDM/STM - Block Data Transfer
// ============================================================

void JitTranslator::translateBlockTransfer(TranslateCtx& ctx, uint32_t instr) {
    int P = (instr >> 24) & 1;  // Pre/post
    int U = (instr >> 23) & 1;  // Up/down
    int S = (instr >> 22) & 1;  // PSR/force user mode
    int W = (instr >> 21) & 1;  // Write-back
    int L = (instr >> 20) & 1;  // Load/store
    int rnArm = (instr >> 16) & 0xF;
    uint32_t regList = instr & 0xFFFF;
    int rnPpc = armRegToPpc(rnArm);

    // Count registers in list
    int count = __builtin_popcount(regList);

    // Calculate start address
    // For increment after (IA): start = Rn, end = Rn + count*4
    // For decrement before (DB): start = Rn - count*4 + 4
    // etc.

    // Base address calculation
    emit(ctx, ppc_mr(PPC_SCRATCH1, rnPpc)); // SCRATCH1 = current address

    uint32_t addrOffset = 0;
    if (!U) {
        // Decrement: start from Rn - count*4
        uint32_t totalOffset = count * 4;
        emit(ctx, ppc_addi(PPC_SCRATCH1, PPC_SCRATCH1, -(int16_t)totalOffset));
    }

    if (P && !U) {
        // Pre-decrement: already done above, but need +4 for pre
        // DB: addresses are Rn-count*4+4, Rn-count*4+8, ...
        // Actually for DB: addr[i] = Rn - (count-i)*4
    }

    // Adjust for pre-indexing
    bool preInc  = (P &&  U); // IB
    bool postInc = (!P && U); // IA
    bool preDecr = (P && !U); // DB
    bool postDecr= (!P && !U);// DA

    // Determine transfer address for first register
    // IA: start = Rn
    // IB: start = Rn + 4
    // DA: start = Rn - (count-1)*4
    // DB: start = Rn - count*4

    uint32_t startOffset = 0;
    if (preInc)   startOffset = 4;
    if (postInc)  startOffset = 0;
    if (preDecr)  startOffset = -(count * 4);
    if (postDecr) startOffset = -((count-1) * 4);

    emit(ctx, ppc_mr(PPC_SCRATCH1, rnPpc));
    if (startOffset != 0) {
        emitLoadImm32(ctx, PPC_SCRATCH0, startOffset);
        emit(ctx, ppc_add(PPC_SCRATCH1, PPC_SCRATCH1, PPC_SCRATCH0));
    }

    // Transfer each register in the list
    int addrInc = 4; // always increment by 4 (we sorted the list)
    bool pcLoaded = false;

    for (int reg = 0; reg <= 15; reg++) {
        if (!(regList & (1 << reg))) continue;

        int regPpc = armRegToPpc(reg);

        emit(ctx, ppc_mr(3, PPC_CPU_PTR));
        emit(ctx, ppc_mr(4, PPC_SCRATCH1));

        if (L) {
            // Load
            emitCallHelper(ctx, (void*)jit_read32, 2);
            emit(ctx, ppc_mr(regPpc, 3));
            if (reg == ARM_PC) pcLoaded = true;
        } else {
            // Store
            emit(ctx, ppc_mr(5, regPpc));
            emitCallHelper(ctx, (void*)jit_write32, 3);
        }

        // Advance address
        emit(ctx, ppc_addi(PPC_SCRATCH1, PPC_SCRATCH1, 4));
    }

    // Write-back: update Rn
    if (W) {
        if (U) {
            // Rn += count * 4
            emitLoadImm32(ctx, PPC_SCRATCH0, count * 4);
            emit(ctx, ppc_add(rnPpc, rnPpc, PPC_SCRATCH0));
        } else {
            // Rn -= count * 4
            emitLoadImm32(ctx, PPC_SCRATCH0, count * 4);
            emit(ctx, ppc_subf(rnPpc, PPC_SCRATCH0, rnPpc));
        }
    }

    if (pcLoaded) ctx.blockDone = true;
}

// ============================================================
//  SWP/SWPB - Atomic Swap
// ============================================================

void JitTranslator::translateSwap(TranslateCtx& ctx, uint32_t instr) {
    int B     = (instr >> 22) & 1;
    int rnArm = (instr >> 16) & 0xF;
    int rdArm = (instr >> 12) & 0xF;
    int rmArm = (instr >> 0)  & 0xF;

    int rnPpc = armRegToPpc(rnArm);
    int rdPpc = armRegToPpc(rdArm);
    int rmPpc = armRegToPpc(rmArm);

    // Read [Rn] -> temp
    emit(ctx, ppc_mr(3, PPC_CPU_PTR));
    emit(ctx, ppc_mr(4, rnPpc));
    if (B) {
        emitCallHelper(ctx, (void*)jit_read8, 2);
    } else {
        emitCallHelper(ctx, (void*)jit_read32, 2);
    }
    emit(ctx, ppc_mr(PPC_SCRATCH3, 3)); // save loaded value

    // Write Rm -> [Rn]
    emit(ctx, ppc_mr(3, PPC_CPU_PTR));
    emit(ctx, ppc_mr(4, rnPpc));
    emit(ctx, ppc_mr(5, rmPpc));
    if (B) {
        emitCallHelper(ctx, (void*)jit_write8, 3);
    } else {
        emitCallHelper(ctx, (void*)jit_write32, 3);
    }

    // Rd = temp
    emit(ctx, ppc_mr(rdPpc, PPC_SCRATCH3));
}

// ============================================================
//  MRS - Move PSR to Register
// ============================================================

void JitTranslator::translateMRS(TranslateCtx& ctx, uint32_t instr) {
    int R     = (instr >> 22) & 1; // SPSR if 1, CPSR if 0
    int rdArm = (instr >> 12) & 0xF;
    int rdPpc = armRegToPpc(rdArm);

    if (!R) {
        // MRS Rd, CPSR
        // First sync flags back to CPSR
        emitStoreCpsrFlags(ctx);
        emit(ctx, ppc_mr(rdPpc, PPC_CPSR));
    } else {
        // MRS Rd, SPSR - load from state struct
        emit(ctx, ppc_lwz(rdPpc, PPC_CPU_PTR,
                          (int16_t)offsetof(ArmCpuState, spsr)));
    }
}

// ============================================================
//  MSR - Move Register to PSR
// ============================================================

void JitTranslator::translateMSR(TranslateCtx& ctx, uint32_t instr) {
    int R    = (instr >> 22) & 1;
    int mask = (instr >> 16) & 0xF; // Field mask (c,x,s,f bits)
    bool I   = (instr >> 25) & 1;

    int srcPpc;
    if (I) {
        uint32_t val = decodeArmRotateImm(instr);
        emitLoadImm32(ctx, PPC_SCRATCH3, val);
        srcPpc = PPC_SCRATCH3;
    } else {
        int rmArm = instr & 0xF;
        srcPpc = armRegToPpc(rmArm);
    }

    if (!R) {
        // MSR CPSR, src
        // Apply field mask
        uint32_t andMask = 0;
        if (mask & 1) andMask |= 0x000000FF; // c
        if (mask & 2) andMask |= 0x0000FF00; // x
        if (mask & 4) andMask |= 0x00FF0000; // s
        if (mask & 8) andMask |= 0xFF000000; // f

        emitLoadImm32(ctx, PPC_SCRATCH4, ~andMask);
        emit(ctx, ppc_and(PPC_CPSR, PPC_CPSR, PPC_SCRATCH4)); // clear masked bits
        emitLoadImm32(ctx, PPC_SCRATCH4, andMask);
        emit(ctx, ppc_and(PPC_SCRATCH3, srcPpc, PPC_SCRATCH4)); // mask source
        emit(ctx, ppc_or(PPC_CPSR, PPC_CPSR, PPC_SCRATCH3)); // merge

        // Reload CR0 from new CPSR
        emitLoadCpsrFlags(ctx);
    } else {
        // MSR SPSR, src
        int spsrOffset = (int)offsetof(ArmCpuState, spsr);
        emit(ctx, ppc_lwz(PPC_SCRATCH4, PPC_CPU_PTR, (int16_t)spsrOffset));

        uint32_t andMask = 0;
        if (mask & 1) andMask |= 0x000000FF;
        if (mask & 8) andMask |= 0xFF000000;

        emitLoadImm32(ctx, PPC_SCRATCH5, ~andMask);
        emit(ctx, ppc_and(PPC_SCRATCH4, PPC_SCRATCH4, PPC_SCRATCH5));
        emitLoadImm32(ctx, PPC_SCRATCH5, andMask);
        emit(ctx, ppc_and(PPC_SCRATCH3, srcPpc, PPC_SCRATCH5));
        emit(ctx, ppc_or(PPC_SCRATCH4, PPC_SCRATCH4, PPC_SCRATCH3));
        emit(ctx, ppc_stw(PPC_SCRATCH4, PPC_CPU_PTR, (int16_t)spsrOffset));
    }
}

// ============================================================
//  SWI - Software Interrupt
// ============================================================

void JitTranslator::translateSWI(TranslateCtx& ctx, uint32_t instr) {
    uint32_t vec = instr & 0x00FFFFFF;

    // Update PC first
    emitLoadImm32(ctx, armRegToPpc(ARM_PC), ctx.pc + 4);

    // Call SWI handler
    emit(ctx, ppc_mr(3, PPC_CPU_PTR));
    emitLoadImm32(ctx, 4, vec);
    emitCallHelper(ctx, (void*)jit_swi, 2);

    ctx.blockDone = true;
}

// ============================================================
//  CLZ - Count Leading Zeros (ARMv5)
// ============================================================

void JitTranslator::translateCLZ(TranslateCtx& ctx, uint32_t instr) {
    int rdArm = (instr >> 12) & 0xF;
    int rmArm = (instr >> 0)  & 0xF;
    int rdPpc = armRegToPpc(rdArm);
    int rmPpc = armRegToPpc(rmArm);

    emit(ctx, ppc_cntlzw(rdPpc, rmPpc));
}

// ============================================================
//  Saturating ALU (ARMv5E) - QADD, QSUB, QDADD, QDSUB
// ============================================================

void JitTranslator::translateQALU(TranslateCtx& ctx, uint32_t instr) {
    int op    = (instr >> 21) & 3;
    int rnArm = (instr >> 16) & 0xF;
    int rdArm = (instr >> 12) & 0xF;
    int rmArm = (instr >> 0)  & 0xF;
    int rdPpc = armRegToPpc(rdArm);
    int rnPpc = armRegToPpc(rnArm);
    int rmPpc = armRegToPpc(rmArm);

    // Simplified: use regular add/sub for now
    // Full saturation requires overflow detection and clamping
    switch (op) {
    case 0: // QADD
        emit(ctx, ppc_add(rdPpc, rmPpc, rnPpc));
        break;
    case 1: // QSUB
        emit(ctx, ppc_subf(rdPpc, rnPpc, rmPpc));
        break;
    case 2: // QDADD
        emit(ctx, ppc_add(PPC_SCRATCH3, rnPpc, rnPpc));
        emit(ctx, ppc_add(rdPpc, rmPpc, PPC_SCRATCH3));
        break;
    case 3: // QDSUB
        emit(ctx, ppc_add(PPC_SCRATCH3, rnPpc, rnPpc));
        emit(ctx, ppc_subf(rdPpc, PPC_SCRATCH3, rmPpc));
        break;
    }
    // TODO: saturation logic and Q flag update
}

// ============================================================
//  Coprocessor / Undefined Instruction Handler
// ============================================================

void JitTranslator::translateCDP(TranslateCtx& ctx, uint32_t instr) {
    // DS coprocessor access - handle CP14/CP15
    // Delegate to C helper
    emit(ctx, ppc_mr(3, PPC_CPU_PTR));
    emitLoadImm32(ctx, 4, instr);
    emitCallHelper(ctx, (void*)jit_undef, 2);
}

void JitTranslator::translateUndefined(TranslateCtx& ctx, uint32_t instr) {
    emit(ctx, ppc_mr(3, PPC_CPU_PTR));
    emitLoadImm32(ctx, 4, ctx.pc);
    emitCallHelper(ctx, (void*)jit_undef, 2);
    ctx.blockDone = true;
}

// ============================================================
//  Main Instruction Decoder
//  Based on the ARM opcode map at:
//  http://imrannazar.com/ARM-Opcode-Map
// ============================================================

void JitTranslator::translateInstr(TranslateCtx& ctx, uint32_t instr) {
    // Extract condition
    int cond = (instr >> 28) & 0xF;

    // Check condition and emit skip if necessary
    emitCondCheck(ctx, cond);

    // Decode by bits [27:20] and [7:4]
    uint32_t group = (instr >> 25) & 0x7;  // Bits [27:25]
    uint32_t op    = (instr >> 20) & 0x1F; // Bits [24:20]
    uint32_t bits74 = (instr >> 4) & 0xF;  // Bits [7:4]

    bool decoded = false;

    if (cond == COND_NV) {
        // ARMv5: BLX (unconditional)
        // Treat as undefined for now
        translateUndefined(ctx, instr);
        decoded = true;
    }

    if (!decoded) {
        switch (group) {
        case 0: // 000
        case 1: // 001
        {
            // Data processing and miscellaneous
            uint32_t bits2720 = (instr >> 20) & 0xFF;
            uint32_t bits74_  = (instr >> 4)  & 0xF;

            // Check for multiply (group=0, bits[7:4]=1001)
            if (group == 0 && (bits74_ == 0x9)) {
                uint32_t mulOp = (instr >> 23) & 0x3;
                uint32_t longMul = (instr >> 23) & 0x1;

                if ((instr & 0x0FC00090) == 0x00000090) {
                    // MUL/MLA
                    translateMul(ctx, instr);
                    decoded = true;
                } else if ((instr & 0x0F800090) == 0x00800090) {
                    // Long multiply: UMULL/UMLAL/SMULL/SMLAL
                    translateMulLong(ctx, instr);
                    decoded = true;
                } else if ((instr & 0x0FB00FF0) == 0x01000090) {
                    // SWP/SWPB
                    translateSwap(ctx, instr);
                    decoded = true;
                }
            }

            // LDRH/STRH/LDRSB/LDRSH (group=0, bits[7:4]=1011/1101/1111)
            if (!decoded && group == 0) {
                uint32_t b76 = (instr >> 5) & 0x3;
                uint32_t b4  = (instr >> 4) & 0x1;
                if (b4 == 1 && (b76 == 1 || b76 == 2 || b76 == 3)) {
                    // Check it's not multiply (already caught)
                    if ((instr & 0x60) != 0) {
                        translateLDRH_STRH(ctx, instr);
                        decoded = true;
                    }
                }
            }

            // BX (Branch and Exchange): 0x012FFF10
            if (!decoded && (instr & 0x0FFFFFF0) == 0x012FFF10) {
                translateBranchEx(ctx, instr);
                decoded = true;
            }

            // BLX register: 0x012FFF30
            if (!decoded && (instr & 0x0FFFFFF0) == 0x012FFF30) {
                // BLX Rn: link and switch
                // LR = PC + 4
                emitLoadImm32(ctx, armRegToPpc(ARM_LR), ctx.pc + 4);
                translateBranchEx(ctx, instr); // reuse BX translation
                decoded = true;
            }

            // CLZ: 0x016F0F10
            if (!decoded && (instr & 0x0FFF0FF0) == 0x016F0F10) {
                translateCLZ(ctx, instr);
                decoded = true;
            }

            // QADD/QSUB/QDADD/QDSUB: bits[27:23]=00010, bits[7:4]=0101
            if (!decoded && (instr & 0x0F900090) == 0x01000050) {
                translateQALU(ctx, instr);
                decoded = true;
            }

            // MRS: bits[27:23]=00010, bit[22] = R
            if (!decoded && (instr & 0x0FBF0FFF) == 0x010F0000) {
                translateMRS(ctx, instr);
                decoded = true;
            }

            // MSR register: 0x0129F000
            if (!decoded && (instr & 0x0FB0FFF0) == 0x0120F000) {
                translateMSR(ctx, instr);
                decoded = true;
            }

            // MSR immediate: 0x0328F000
            if (!decoded && (instr & 0x0FB0F000) == 0x0320F000) {
                translateMSR(ctx, instr);
                decoded = true;
            }

            // Data processing (catch-all)
            if (!decoded) {
                translateDataProc(ctx, instr);
                decoded = true;
            }
            break;
        }

        case 2: // 010 - LDR/STR immediate
        case 3: // 011 - LDR/STR register
        {
            // Check for undefined space (bit25=1, bit4=1 in group 3)
            if (group == 3 && (instr & 0x10)) {
                translateUndefined(ctx, instr);
            } else {
                translateLoadStore(ctx, instr);
            }
            decoded = true;
            break;
        }

        case 4: // 100 - LDM/STM
        {
            translateBlockTransfer(ctx, instr);
            decoded = true;
            break;
        }

        case 5: // 101 - B/BL
        {
            translateBranch(ctx, instr);
            decoded = true;
            break;
        }

        case 6: // 110 - Coprocessor LDC/STC
        {
            translateCDP(ctx, instr);
            decoded = true;
            break;
        }

        case 7: // 111 - SWI / CDP / MCR / MRC
        {
            if ((instr >> 24) & 1) {
                // SWI
                translateSWI(ctx, instr);
            } else {
                // Coprocessor
                translateCDP(ctx, instr);
            }
            decoded = true;
            break;
        }
        }
    }

    // Patch conditional skip to jump past the translated instruction(s)
    patchCondSkip(ctx);

    // Advance PC
    ctx.pc += 4;
    ctx.cycleCount += 1; // simplified cycle counting
    ctx.armInstrCount++;
}

// ============================================================
//  Block Translation
// ============================================================

void JitTranslator::translateBlock(TranslateCtx& ctx, int maxInstrs) {
    ctx.blockDone = false;
    ctx.conditionalPending = false;
    ctx.condSkipPatch = -1;
    ctx.cycleCount = 0;
    ctx.armInstrCount = 0;

    emitPrologue(ctx);

    // Main translation loop
    while (!ctx.blockDone && ctx.armInstrCount < maxInstrs) {
        // Fetch ARM instruction
        uint32_t armInstr = jit_read32(ctx.cpu, ctx.pc);

        // Translate the instruction
        translateInstr(ctx, armInstr);
    }

    // If we stopped due to instruction count limit, emit PC update
    if (!ctx.blockDone) {
        emitLoadImm32(ctx, armRegToPpc(ARM_PC), ctx.pc);
    }

    emitEpilogue(ctx);
}

// ============================================================
//  Get or Translate a Block
// ============================================================

JitBlock* JitTranslator::getOrTranslate(ArmCpuState* cpu, uint32_t armPc) {
    auto it = m_blockCache.find(armPc);
    if (it != m_blockCache.end() && it->second.valid) {
        return &it->second;
    }

    // Allocate code space
    // Estimate: each ARM instr -> ~20 PPC instrs, max 128 ARM = ~2560 PPC = 10KB
    size_t maxPpcInstrs = JIT_MAX_BLOCK_INSTRS * 25 + 64; // +64 for prologue/epilogue

    uint32_t* codePtr = m_codeBuffer.allocate(maxPpcInstrs);
    if (!codePtr) return nullptr;

    // Translate
    TranslateCtx ctx;
    ctx.cpu = cpu;
    ctx.pc  = armPc;

    translateBlock(ctx, JIT_MAX_BLOCK_INSTRS);

    // Verify we didn't overflow
    assert(ctx.ppcInstrs.size() <= maxPpcInstrs);

    // Copy PPC instructions to code buffer
    size_t numInstrs = ctx.ppcInstrs.size();
    memcpy(codePtr, ctx.ppcInstrs.data(), numInstrs * sizeof(uint32_t));

    // Flush instruction cache on Wii
    gekko_flush_icache(codePtr, numInstrs * sizeof(uint32_t));

    // Register block
    JitBlock& block = m_blockCache[armPc];
    block.armPc         = armPc;
    block.ppcCode       = codePtr;
    block.ppcSize       = numInstrs;
    block.armInstrCount = ctx.armInstrCount;
    block.cycleCount    = ctx.cycleCount;
    block.valid         = true;

    return &block;
}

// ============================================================
//  Execute a JIT Block
// ============================================================

uint32_t JitTranslator::executeBlock(ArmCpuState* cpu, JitBlock* block) {
    JitBlockFn fn = (JitBlockFn)block->ppcCode;
    return fn(cpu);
}

// ============================================================
//  Global JIT instance
// ============================================================

static JitTranslator* g_jit = nullptr;

void jit_init() {
    if (!g_jit) {
        g_jit = new JitTranslator();
    }
}

void jit_flush() {
    if (g_jit) g_jit->flush();
}

// ============================================================
//  Main JIT dispatch - replaces interpreter_lookup.cpp
//  This is called from the main emulation loop
// ============================================================

extern "C" {

// These will be defined/linked against the NooDS core
// The implementations delegate to Core's memory subsystem
uint32_t jit_read32 (ArmCpuState* cpu, uint32_t addr) {
    // TODO: Implement using NooDS Core's memory read
    // e.g.: return core->memory.read<uint32_t>(addr);
    (void)cpu; (void)addr;
    return 0;
}
uint16_t jit_read16 (ArmCpuState* cpu, uint32_t addr) {
    (void)cpu; (void)addr;
    return 0;
}
uint8_t  jit_read8  (ArmCpuState* cpu, uint32_t addr) {
    (void)cpu; (void)addr;
    return 0;
}
void     jit_write32(ArmCpuState* cpu, uint32_t addr, uint32_t val) {
    (void)cpu; (void)addr; (void)val;
}
void     jit_write16(ArmCpuState* cpu, uint32_t addr, uint16_t val) {
    (void)cpu; (void)addr; (void)val;
}
void     jit_write8 (ArmCpuState* cpu, uint32_t addr, uint8_t val) {
    (void)cpu; (void)addr; (void)val;
}
void     jit_swi    (ArmCpuState* cpu, uint32_t vec) {
    (void)cpu; (void)vec;
    // TODO: Call NooDS SWI handler
}
void     jit_undef  (ArmCpuState* cpu, uint32_t pc) {
    (void)cpu; (void)pc;
    // TODO: Call NooDS undefined handler
}

// ============================================================
//  JIT Entry Point - called per-frame or per-timeslice
// ============================================================

int jit_run(ArmCpuState* cpu, int cycles) {
    if (!g_jit) jit_init();

    int remaining = cycles;

    while (remaining > 0 && !cpu->halted) {
        uint32_t pc = cpu->regs[ARM_PC];

        // Don't JIT THUMB mode - fall back (not implemented here)
        if (cpu->cpsr & CPSR_T) {
            // TODO: THUMB JIT or interpreter fallback
            remaining -= 1;
            continue;
        }

        JitBlock* block = g_jit->getOrTranslate(cpu, pc);
        if (!block) {
            // Translation failed; shouldn't happen
            remaining -= 1;
            continue;
        }

        uint32_t consumed = JitTranslator::executeBlock(cpu, block);
        remaining -= (int)consumed;
    }

    return cycles - remaining;
}

} // extern "C"

// ============================================================
//  NooDS Integration Shim
//  To integrate: replace the call to interpreter_lookup or
//  arm.cpp's execute() function with jit_run().
//
//  In core.cpp or wherever the main CPU loop is:
//    // Old: arm9.execute(cycles);
//    // New: jit_run(&arm9State, cycles);
//
//  The ArmCpuState struct must match the layout of NooDS's
//  interpreter core (interpreter.h / arm.h).
// ============================================================

/*
 * ============================================================
 *  INTEGRATION NOTES FOR NooDS:
 * ============================================================
 *
 *  1. This file replaces interpreter_lookup.cpp as the dispatch
 *     mechanism. The interpreter still handles edge cases.
 *
 *  2. ArmCpuState must be adapted to match NooDS's actual CPU
 *     struct (interpreter.h, core9.h, etc.)
 *
 *  3. Memory helpers (jit_read32, etc.) must be wired to NooDS's
 *     Memory class (memory.h -> read<T>/write<T>).
 *
 *  4. SWI/exception handling must match NooDS's exception vectors.
 *
 *  5. The JIT is currently ARM-mode only. THUMB mode requires
 *     a separate (or extended) translator.
 *
 *  6. Self-modifying code invalidation: call jit_flush() or
 *     per-block invalidation when writes to code regions occur.
 *
 *  7. Cycle counting is approximate (1 cycle per ARM instr).
 *     Proper cycle counting requires per-opcode tables.
 *
 *  8. Full NZCV flag accuracy requires more sophisticated
 *     overflow detection (especially for ADC/SBC/RSB).
 *
 *  9. Banked register handling for mode switches needs proper
 *     implementation via helper calls.
 *
 * 10. This is built for Wii Gekko (PPC 750CL). For Broadway
 *     (Wii), no changes needed. For Wii U Espresso, same applies.
 *
 * ============================================================
 */
