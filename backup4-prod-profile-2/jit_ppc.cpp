// jit_ppc.cpp — fixed version
#include "jit_ppc.h"
#include "core.h"
#include "interpreter.h"
#include "memory.h"
#include "defines.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <malloc.h>

extern "C" {
    #include <ogc/cache.h>
}

// ============================================================
// Frame layout — ABI compliant, no overlap between areas
//
//   [r1+ 0]  back-chain word
//   [r1+ 4]  ABI LR save area (for callees)
//   [r1+ 8]  our saved LR                     FRAME_LR
//   [r1+12]  pad
//   [r1+16]  r14..r29 saved (16*4 = 64 bytes) FRAME_R14
//   [r1+80]  volatile save: r3..r10 (8*4=32)  FRAME_VOLSAVE
//   [r1+112] ARM reg sync: r0-r15+cpsr(17*4)  FRAME_REGSYNC
//   [r1+180] pad to 192
// ============================================================
static const int FRAME_SIZE    = 192;   // 16-byte aligned
static const int FRAME_LR      = 8;
static const int FRAME_R14     = 16;    // r14-r29 (callee-saved)
static const int FRAME_VOLSAVE = 80;    // r3-r10 volatile save (around calls)
static const int FRAME_REGSYNC = 112;   // ARM r0-r15 + CPSR

static_assert(FRAME_SIZE % 16 == 0,                      "frame alignment");
static_assert(FRAME_R14 + 16*4 <= FRAME_VOLSAVE,         "saved regs fit");
static_assert(FRAME_VOLSAVE + 8*4 <= FRAME_REGSYNC,      "vol save fits");
static_assert(FRAME_REGSYNC + 17*4 <= FRAME_SIZE,        "sync area fits");

namespace JitPpc {

// ============================================================
// PPC instruction builders (unchanged — these are correct)
// ============================================================
static inline uint32_t ppc_b(int32_t o, bool aa=false, bool lk=false)
    { return (18u<<26)|((uint32_t)(o&0x3FFFFFC))|(aa?2u:0u)|(lk?1u:0u); }
static inline uint32_t ppc_bc(uint8_t bo, uint8_t bi, int16_t o, bool lk=false)
    { return (16u<<26)|((uint32_t)(bo&0x1F)<<21)|((uint32_t)(bi&0x1F)<<16)
             |((uint32_t)(o&0xFFFC))|(lk?1u:0u); }
static inline uint32_t ppc_bclr(uint8_t bo, uint8_t bi, bool lk=false)
    { return (19u<<26)|((uint32_t)(bo&0x1F)<<21)|((uint32_t)(bi&0x1F)<<16)
             |(16u<<1)|(lk?1u:0u); }
static inline uint32_t ppc_bctr(bool lk=false)
    { return (19u<<26)|(20u<<21)|(528u<<1)|(lk?1u:0u); }
static inline uint32_t ppc_addi(uint8_t rt, uint8_t ra, int16_t i)
    { return (14u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_addis(uint8_t rt, uint8_t ra, int16_t i)
    { return (15u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_ori(uint8_t ra, uint8_t rs, uint16_t i)
    { return (24u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i; }
static inline uint32_t ppc_oris(uint8_t ra, uint8_t rs, uint16_t i)
    { return (25u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i; }
static inline uint32_t ppc_andi(uint8_t ra, uint8_t rs, uint16_t i)
    { return (28u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i; }
static inline uint32_t ppc_xori(uint8_t ra, uint8_t rs, uint16_t i)
    { return (26u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i; }
static inline uint32_t ppc_lwz(uint8_t rt, int16_t d, uint8_t ra)
    { return (32u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_stw(uint8_t rs, int16_t d, uint8_t ra)
    { return (36u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_lbz(uint8_t rt, int16_t d, uint8_t ra)
    { return (34u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_stb(uint8_t rs, int16_t d, uint8_t ra)
    { return (38u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_lhz(uint8_t rt, int16_t d, uint8_t ra)
    { return (40u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_lha(uint8_t rt, int16_t d, uint8_t ra)
    { return (42u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_sth(uint8_t rs, int16_t d, uint8_t ra)
    { return (44u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_cmpi(uint8_t cr, uint8_t ra, int16_t i, bool l=false)
    { return (11u<<26)|((uint32_t)(cr&7)<<23)|(l?(1u<<21):0u)
             |((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_cmpli(uint8_t cr, uint8_t ra, uint16_t i, bool l=false)
    { return (10u<<26)|((uint32_t)(cr&7)<<23)|(l?(1u<<21):0u)
             |((uint32_t)ra<<16)|i; }
static inline uint32_t ppc_subfic(uint8_t rt, uint8_t ra, int16_t i)
    { return (8u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_Xform(uint32_t op, uint8_t rt, uint8_t ra, uint8_t rb,
                                   uint32_t xop, bool rc=false)
    { return (op<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)
             |((uint32_t)rb<<11)|(xop<<1)|(rc?1u:0u); }
static inline uint32_t ppc_XOform(uint32_t op, uint8_t rt, uint8_t ra, uint8_t rb,
                                    bool oe, uint32_t xop, bool rc=false)
    { return (op<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)
             |((uint32_t)rb<<11)|(oe?(1u<<10):0u)|(xop<<1)|(rc?1u:0u); }
static inline uint32_t ppc_add(uint8_t rt, uint8_t ra, uint8_t rb, bool rc=false)
    { return ppc_XOform(31,rt,ra,rb,false,266,rc); }
static inline uint32_t ppc_addc(uint8_t rt, uint8_t ra, uint8_t rb, bool rc=false)
    { return ppc_XOform(31,rt,ra,rb,false,10,rc); }
static inline uint32_t ppc_adde(uint8_t rt, uint8_t ra, uint8_t rb, bool rc=false)
    { return ppc_XOform(31,rt,ra,rb,false,138,rc); }
static inline uint32_t ppc_subf(uint8_t rt, uint8_t ra, uint8_t rb, bool rc=false)
    { return ppc_XOform(31,rt,ra,rb,false,40,rc); }
static inline uint32_t ppc_subfc(uint8_t rt, uint8_t ra, uint8_t rb, bool rc=false)
    { return ppc_XOform(31,rt,ra,rb,false,8,rc); }
static inline uint32_t ppc_subfe(uint8_t rt, uint8_t ra, uint8_t rb, bool rc=false)
    { return ppc_XOform(31,rt,ra,rb,false,136,rc); }
static inline uint32_t ppc_neg(uint8_t rt, uint8_t ra, bool rc=false)
    { return ppc_XOform(31,rt,ra,0,false,104,rc); }
static inline uint32_t ppc_mullw(uint8_t rt, uint8_t ra, uint8_t rb, bool rc=false)
    { return ppc_XOform(31,rt,ra,rb,false,235,rc); }
static inline uint32_t ppc_mulhw(uint8_t rt, uint8_t ra, uint8_t rb, bool rc=false)
    { return ppc_XOform(31,rt,ra,rb,false,75,rc); }
static inline uint32_t ppc_mulhwu(uint8_t rt, uint8_t ra, uint8_t rb, bool rc=false)
    { return ppc_XOform(31,rt,ra,rb,false,11,rc); }
static inline uint32_t ppc_divwu(uint8_t rt, uint8_t ra, uint8_t rb, bool rc=false)
    { return ppc_XOform(31,rt,ra,rb,false,459,rc); }
static inline uint32_t ppc_divw(uint8_t rt, uint8_t ra, uint8_t rb, bool rc=false)
    { return ppc_XOform(31,rt,ra,rb,false,491,rc); }
static inline uint32_t ppc_and(uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false)
    { return ppc_Xform(31,rs,ra,rb,28,rc); }
static inline uint32_t ppc_or(uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false)
    { return ppc_Xform(31,rs,ra,rb,444,rc); }
static inline uint32_t ppc_xor(uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false)
    { return ppc_Xform(31,rs,ra,rb,316,rc); }
static inline uint32_t ppc_andc(uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false)
    { return ppc_Xform(31,rs,ra,rb,60,rc); }
static inline uint32_t ppc_nor(uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false)
    { return ppc_Xform(31,rs,ra,rb,124,rc); }
static inline uint32_t ppc_eqv(uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false)
    { return ppc_Xform(31,rs,ra,rb,284,rc); }
static inline uint32_t ppc_nand(uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false)
    { return ppc_Xform(31,rs,ra,rb,476,rc); }
static inline uint32_t ppc_orc(uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false)
    { return ppc_Xform(31,rs,ra,rb,412,rc); }
static inline uint32_t ppc_mr(uint8_t ra, uint8_t rs) { return ppc_or(ra,rs,rs); }
static inline uint32_t ppc_nop() { return ppc_ori(0,0,0); }
static inline uint32_t ppc_slw(uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false)
    { return ppc_Xform(31,rs,ra,rb,24,rc); }
static inline uint32_t ppc_srw(uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false)
    { return ppc_Xform(31,rs,ra,rb,536,rc); }
static inline uint32_t ppc_sraw(uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false)
    { return ppc_Xform(31,rs,ra,rb,792,rc); }
static inline uint32_t ppc_rlwinm(uint8_t ra, uint8_t rs, uint8_t sh,
                                    uint8_t mb, uint8_t me, bool rc=false)
    { return (21u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)
             |((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1)|(rc?1u:0u); }
static inline uint32_t ppc_rlwimi(uint8_t ra, uint8_t rs, uint8_t sh,
                                    uint8_t mb, uint8_t me, bool rc=false)
    { return (20u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)
             |((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1)|(rc?1u:0u); }
static inline uint32_t ppc_rlwnm(uint8_t ra, uint8_t rs, uint8_t rb,
                                   uint8_t mb, uint8_t me, bool rc=false)
    { return (23u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)
             |((uint32_t)rb<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1)|(rc?1u:0u); }
static inline uint32_t ppc_srawi(uint8_t ra, uint8_t rs, uint8_t sh, bool rc=false)
    { return (31u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)
             |((uint32_t)sh<<11)|(824u<<1)|(rc?1u:0u); }
static inline uint32_t ppc_cntlzw(uint8_t ra, uint8_t rs, bool rc=false)
    { return ppc_Xform(31,rs,ra,0,26,rc); }
static inline uint32_t ppc_cmp(uint8_t cr, uint8_t ra, uint8_t rb)
    { return (31u<<26)|((uint32_t)(cr&7)<<23)|((uint32_t)ra<<16)
             |((uint32_t)rb<<11)|(0u<<1); }
static inline uint32_t ppc_cmpl(uint8_t cr, uint8_t ra, uint8_t rb)
    { return (31u<<26)|((uint32_t)(cr&7)<<23)|((uint32_t)ra<<16)
             |((uint32_t)rb<<11)|(32u<<1); }
static inline uint32_t ppc_mtspr(uint16_t spr, uint8_t rs) {
    uint8_t lo=spr&0x1F, hi=(spr>>5)&0x1F;
    return (31u<<26)|((uint32_t)rs<<21)|((uint32_t)lo<<16)
           |((uint32_t)hi<<11)|(467u<<1);
}
static inline uint32_t ppc_mfspr(uint8_t rt, uint16_t spr) {
    uint8_t lo=spr&0x1F, hi=(spr>>5)&0x1F;
    return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)lo<<16)
           |((uint32_t)hi<<11)|(339u<<1);
}
static inline uint32_t ppc_mtctr(uint8_t rs) { return ppc_mtspr(9,rs); }
static inline uint32_t ppc_mfctr(uint8_t rt) { return ppc_mfspr(rt,9); }
static inline uint32_t ppc_mtlr(uint8_t rs)  { return ppc_mtspr(8,rs); }
static inline uint32_t ppc_mflr(uint8_t rt)  { return ppc_mfspr(rt,8); }
static inline uint32_t ppc_mtxer(uint8_t rs) { return ppc_mtspr(1,rs); }
static inline uint32_t ppc_mfxer(uint8_t rt) { return ppc_mfspr(rt,1); }
static inline uint32_t ppc_mfcr(uint8_t rt)
    { return (31u<<26)|((uint32_t)rt<<21)|(19u<<1); }
static inline uint32_t ppc_mtcrf(uint8_t fxm, uint8_t rs)
    { return (31u<<26)|((uint32_t)rs<<21)|((uint32_t)(fxm&0xFF)<<12)|(144u<<1); }
static inline uint32_t ppc_isync() { return (19u<<26)|(150u<<1); }
static inline uint32_t ppc_sync()  { return (31u<<26)|(598u<<1); }
static inline uint32_t ppc_eieio() { return (31u<<26)|(854u<<1); }
static inline uint32_t ppc_lwzx(uint8_t rt, uint8_t ra, uint8_t rb)
    { return ppc_Xform(31,rt,ra,rb,23); }
static inline uint32_t ppc_lbzx(uint8_t rt, uint8_t ra, uint8_t rb)
    { return ppc_Xform(31,rt,ra,rb,87); }
static inline uint32_t ppc_lhzx(uint8_t rt, uint8_t ra, uint8_t rb)
    { return ppc_Xform(31,rt,ra,rb,279); }
static inline uint32_t ppc_lhax(uint8_t rt, uint8_t ra, uint8_t rb)
    { return ppc_Xform(31,rt,ra,rb,343); }
static inline uint32_t ppc_stwx(uint8_t rs, uint8_t ra, uint8_t rb)
    { return ppc_Xform(31,rs,ra,rb,151); }
static inline uint32_t ppc_stbx(uint8_t rs, uint8_t ra, uint8_t rb)
    { return ppc_Xform(31,rs,ra,rb,215); }
static inline uint32_t ppc_sthx(uint8_t rs, uint8_t ra, uint8_t rb)
    { return ppc_Xform(31,rs,ra,rb,407); }
static inline uint32_t ppc_stwu(uint8_t rs, int16_t d, uint8_t ra)
    { return (37u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_lwzu(uint8_t rt, int16_t d, uint8_t ra)
    { return (33u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_blr() { return ppc_bclr(20,0); }
static inline uint32_t ppc_extsb(uint8_t ra, uint8_t rs, bool rc=false)
    { return ppc_Xform(31,rs,ra,0,954,rc); }
static inline uint32_t ppc_extsh(uint8_t ra, uint8_t rs, bool rc=false)
    { return ppc_Xform(31,rs,ra,0,922,rc); }

// ============================================================
// emit_li32 — load 32-bit immediate into register
// NOTE: Must not use r0 as rt (addis/addi with rA=0 use literal 0)
// ============================================================
static int emit_li32(uint32_t* out, uint8_t rt, uint32_t imm) {
    // rt must not be r0!
    uint16_t lo = (uint16_t)(imm & 0xFFFF);
    uint16_t hi = (uint16_t)(imm >> 16);
    if (imm == 0) { out[0] = ppc_addi(rt, 0, 0); return 1; }
    if (hi == 0) {
        if (lo < 0x8000u) { out[0] = ppc_addi(rt, 0, (int16_t)lo); return 1; }
        out[0] = ppc_addi(rt, 0, 0);
        out[1] = ppc_ori(rt, rt, lo);
        return 2;
    }
    if (lo == 0) { out[0] = ppc_addis(rt, 0, (int16_t)hi); return 1; }
    out[0] = ppc_addis(rt, 0, (int16_t)hi);
    out[1] = ppc_ori(rt, rt, lo);
    return 2;
}

// ============================================================
// Register mapping
//
// ARM r0-r7  -> PPC r14-r21  (callee-saved, survive C calls)
// ARM r8-r14 -> PPC r22-r28  (callee-saved)
// ARM r15    -> PPC r29      (callee-saved, PC)
// CPSR       -> PPC r30      (callee-saved)
// INTERP     -> PPC r31      (callee-saved)
// CORE       -> PPC r27      (callee-saved) -- share with ARM r13
//
// Wait -- r27 is ARM r13 (SP). Need to rethink.
//
// Better: use a clean split:
// ARM r0-r12  -> PPC r14-r26  (13 regs, all callee-saved)
// ARM r13     -> PPC r27      (callee-saved)
// ARM r14     -> PPC r28      (callee-saved)
// ARM r15(PC) -> PPC r29      (callee-saved)
// CPSR        -> PPC r30      (callee-saved)
// INTERP      -> PPC r31      (callee-saved)
// CORE        -> dedicate PPC r13? No, r13 is SDA in some ABIs.
//
// On Wii/devkitPPC: r13 = SDA base (read-only for us), r2 = SDA2 base.
// We must not touch r1 (sp), r2 (SDA2), r13 (SDA).
//
// Use PPC r11 for CORE (volatile, but we save/restore it via FRAME_R14? No.)
// Best: store CORE in the frame and reload as needed, or use a callee-saved reg.
//
// Revised mapping using r14-r31 for all persistent values:
// ============================================================

// PPC registers for ARM state (all callee-saved: r14-r31)
static const uint8_t PPC_ARM_R0  = 14;   // ARM r0
static const uint8_t PPC_ARM_R1  = 15;   // ARM r1
static const uint8_t PPC_ARM_R2  = 16;   // ARM r2
static const uint8_t PPC_ARM_R3  = 17;   // ARM r3
static const uint8_t PPC_ARM_R4  = 18;   // ARM r4
static const uint8_t PPC_ARM_R5  = 19;   // ARM r5
static const uint8_t PPC_ARM_R6  = 20;   // ARM r6
static const uint8_t PPC_ARM_R7  = 21;   // ARM r7
static const uint8_t PPC_ARM_R8  = 22;   // ARM r8
static const uint8_t PPC_ARM_R9  = 23;   // ARM r9
static const uint8_t PPC_ARM_R10 = 24;   // ARM r10
static const uint8_t PPC_ARM_R11 = 25;   // ARM r11
static const uint8_t PPC_ARM_R12 = 26;   // ARM r12
static const uint8_t PPC_ARM_R13 = 27;   // ARM r13 (SP)
static const uint8_t PPC_ARM_R14 = 28;   // ARM r14 (LR)
static const uint8_t PPC_ARM_R15 = 29;   // ARM r15 (PC)
static const uint8_t PPC_CPSR    = 30;   // ARM CPSR
static const uint8_t PPC_INTERP  = 31;   // Interpreter*

// Volatile temporaries (NOT saved across C calls, must save/restore manually)
static const uint8_t PPC_TMP0    = 3;    // volatile (arg/ret)
static const uint8_t PPC_TMP1    = 4;    // volatile (arg)
static const uint8_t PPC_TMP2    = 5;    // volatile (arg)
static const uint8_t PPC_TMP3    = 6;    // volatile (arg)
static const uint8_t PPC_CORE_V  = 7;    // volatile, reload from frame when needed

// CORE pointer: stored in frame, loaded into PPC_CORE_V before use
// This avoids wasting a callee-saved register on a pointer used infrequently.

// ARM to PPC register mapping
static const uint8_t ARM_TO_PPC[16] = {
    14, 15, 16, 17, 18, 19, 20, 21,   // r0-r7
    22, 23, 24, 25, 26, 27, 28, 29    // r8-r15
};

// Frame slot for Core* (saved in frame to free up callee-saved registers)
static const int FRAME_CORE = FRAME_VOLSAVE + 8*4;  // = 80+32 = 112... wait
// That overlaps FRAME_REGSYNC! Let me recalculate:

// Actually let's store CORE pointer right after FRAME_R14 area (at FRAME_VOLSAVE)
// FRAME_VOLSAVE = 80: use slots 80..111 for volatile saves + CORE ptr
// slot 80: CORE* (4 bytes)
// slots 84..111: volatile saves for r4-r10 (7 * 4 = 28 bytes)
// FRAME_REGSYNC = 112: ARM registers (unchanged)
static const int FRAME_CORE_SLOT = 80;    // Core* stored here
static const int FRAME_VOLSAVE_START = 84; // r4-r10 saved here (7 regs)

#define CPSR_N (1u<<31)
#define CPSR_Z (1u<<30)
#define CPSR_C (1u<<29)
#define CPSR_V (1u<<28)
#define CPSR_T (1u<<5)

// ============================================================
// Code buffer
// ============================================================
static const size_t JIT_CODE_SIZE   = 4*1024*1024;
static const size_t JIT_MAX_INSTRS  = JIT_CODE_SIZE/4;
static const size_t MAX_BLOCK_SIZE  = 128;
static const size_t MAX_PPC_PER_ARM = 96;

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

static inline size_t hashPC(uint32_t pc) { return (pc>>1) & (BLOCK_CACHE_SIZE-1); }

void flushJitCache() {
    codeBufferPos = 0;
    for (size_t i = 0; i < BLOCK_CACHE_SIZE; i++)
        blockCache[i].valid = false;
}

// ============================================================
// Emit context
// ============================================================
struct EmitCtx {
    uint32_t* base;
    uint32_t* cur;
    size_t    capacity;
    bool      thumb, arm7;
    uint32_t  armPC;
    Interpreter* interp;
    Core*     core;
    bool      hasExplicitReturn;

    void emit(uint32_t w) {
        if ((size_t)(cur - base) < capacity) *cur++ = w;
    }
    size_t size() const { return (size_t)(cur - base); }
};

static void flushCaches(uint32_t* s, size_t w) {
    DCFlushRange(s, w*4);
    ICInvalidateRange(s, w*4);
}

// ============================================================
// Trampoline — fixed version
//
// Layout of stack at various points:
//
//   On entry to trampoline (called by C++ executeBlock):
//     r1 = caller_sp (executeBlock's frame)
//     LR = return address in executeBlock
//     r3 = block->ppcCode
//
//   After stwu r1,-48(r1):  [trampoline frame = 48 bytes]
//     new_r1+0  = back-chain = caller_sp
//     new_r1+4  = ABI LR save slot (we'll use it)
//     new_r1+8  = our extra save area (not needed, just padding)
//
//   We save LR at new_r1+4 (standard ABI location).
//   Then bctrl sets LR = address of instruction after bctrl.
//   Block's mflr r0 captures this correctly.
//   Block runs, block's blr returns to instruction after bctrl.
//   We restore r1 via back-chain, then lwz r0 from old_r1+4.
//   Wait — after lwz r1,0(r1): r1 = caller_sp
//   Then lwz r0,4(r1): loads from caller_sp+4 = caller's LR save!
//   That's WRONG — we saved our LR at new_r1+4, not caller_sp+4.
//
//   FIX: Save LR at new_r1+8 (our own frame slot), load from new_r1+8
//   before restoring r1:
//
//   mflr  r0              ; capture LR
//   stwu  r1, -48(r1)     ; allocate frame
//   stw   r0, 8(r1)       ; save at new_r1+8
//   mtctr r3              ; block ptr
//   bctrl                 ; call block
//   ; block returns here
//   lwz   r0, 8(r1)       ; restore LR from new_r1+8
//   addi  r1, r1, 48      ; deallocate frame
//   mtlr  r0              ; restore LR
//   blr                   ; return to executeBlock caller
// ============================================================
static uint32_t trampolineCode[10] __attribute__((aligned(32)));
static bool trampolineReady = false;

static void initTrampoline() {
    if (trampolineReady) return;

    // Build trampoline instructions
    trampolineCode[0] = ppc_mflr(0);           // mflr r0
    trampolineCode[1] = ppc_stwu(1, -48, 1);   // stwu r1, -48(r1)
    trampolineCode[2] = ppc_stw(0, 8, 1);      // stw r0, 8(r1)
    trampolineCode[3] = ppc_mtctr(3);           // mtctr r3
    trampolineCode[4] = ppc_bctr(true);         // bctrl
    // block's blr returns here:
    trampolineCode[5] = ppc_lwz(0, 8, 1);      // lwz r0, 8(r1)
    trampolineCode[6] = ppc_addi(1, 1, 48);    // addi r1, r1, 48
    trampolineCode[7] = ppc_mtlr(0);           // mtlr r0
    trampolineCode[8] = ppc_blr();             // blr
    trampolineCode[9] = ppc_nop();             // padding

    DCFlushRange(trampolineCode, sizeof(trampolineCode));
    ICInvalidateRange(trampolineCode, sizeof(trampolineCode));
    trampolineReady = true;
}

// ============================================================
// Condition helpers
// ============================================================
static void emit_setupCondFlags(EmitCtx& ctx) {
    // Extract top 4 bits of CPSR (N,Z,C,V) into CR0
    // CPSR bits 31-28 = N,Z,C,V -> CR bits: we use mtcrf
    // ARM: N=bit31 Z=bit30 C=bit29 V=bit28
    // PPC CR field 0: LT=bit0 GT=bit1 EQ=bit2 SO=bit3 (of CR[0..3])
    // We'll pack NZCV into CR7 (bits 28-31 of CR word = field 7)
    // to avoid conflicting with comparisons that use CR0.
    //
    // Actually, let's keep it simple: extract into a temp, use mtcrf.
    // mtcrf 0x01, rN  sets CR7 from bits 28-31 of rN
    // ARM CPSR: N=31 Z=30 C=29 V=28
    // CR7: bit28=LT bit29=GT bit30=EQ bit31=SO
    // So we need N->LT, Z->EQ, C->GT, V->SO? That's messy.
    //
    // Simpler: shift CPSR right 28 to get NZCV in bits 3-0,
    // then use mtcrf 0x80 to set CR0 from bits 31-28 of the shifted value.
    // After rlwinm rT, CPSR, 0, 0, 3 (= top 4 bits of CPSR into bits 31-28):
    // bits 31=N, 30=Z, 29=C, 28=V -> CR0: LT=N, GT=Z, EQ=C, SO=V
    // Then bc/bne etc. for ARM conditions:
    //   EQ: CR0_EQ set <=> Z set <=> CR0 bit 2 (EQ) -- but we put Z in GT (bit 1)
    //
    // This is getting complicated. Let me use a dedicated approach:
    // Store NZCV in a scratch reg and test individual bits.
    // For branch conditions, use PPC rlwinm + cmpi pattern.
    //
    // Actually the simplest correct approach for all ARM conditions:
    // We keep CPSR in PPC_CPSR register. For each condition check, we
    // test the relevant bits directly.

    // For now: extract top nibble into TMP0, load into CR field
    // Map: ARM CPSR bit31(N)->CR0_LT, bit30(Z)->CR0_GT, bit29(C)->CR0_EQ, bit28(V)->CR0_SO
    // mtcrf 0x80 sets CR0 (field 0 = bits 31..28 of the source reg)
    // So we rotate CPSR right 0 (keep as-is), mtcrf 0x80 -> CR0
    // CR0_LT = CPSR bit 31 = N  ✓
    // CR0_GT = CPSR bit 30 = Z  (inverted meaning but we use it as Z flag)
    // CR0_EQ = CPSR bit 29 = C
    // CR0_SO = CPSR bit 28 = V
    ctx.emit(ppc_mtcrf(0x80, PPC_CPSR));  // CR0 <- top 4 bits of CPSR
}

// ARM condition to PPC branch condition
// CR0 mapping: LT=N(bit31), GT=Z(bit30), EQ=C(bit29), SO=V(bit28)
// CR0 bit indices: LT=0, GT=1, EQ=2, SO=3
// PPC bc: BO=12 means "branch if CRbi set", BO=4 means "branch if CRbi clear"
struct CondBranch { uint8_t bo, bi; bool valid; };

static CondBranch armCondToPpc(uint8_t c) {
    // After mtcrf 0x80: LT=N, GT=Z, EQ=C, SO=V
    // CR0 bits: LT=0, GT=1, EQ=2, SO=3
    switch (c) {
        case 0:  return {12, 1, true};  // EQ: Z set -> GT set
        case 1:  return {4,  1, true};  // NE: Z clear -> GT clear
        case 2:  return {12, 2, true};  // CS: C set -> EQ set
        case 3:  return {4,  2, true};  // CC: C clear -> EQ clear
        case 4:  return {12, 0, true};  // MI: N set -> LT set
        case 5:  return {4,  0, true};  // PL: N clear -> LT clear
        case 6:  return {12, 3, true};  // VS: V set -> SO set
        case 7:  return {4,  3, true};  // VC: V clear -> SO clear
        // HI: C set AND Z clear -> EQ set AND GT clear
        // LS, GE, LT, GT, LE: need multi-step, fall through to false
        case 8:  // HI: C && !Z — cannot express in single bc, need two
        case 9:  // LS: !C || Z
        case 10: // GE: N == V -> LT == SO
        case 11: // LT: N != V -> LT != SO
        case 12: // GT: !Z && N==V
        case 13: // LE: Z || N!=V
            return {0, 0, false};  // need multi-instruction sequence
        case 14: return {20, 0, true}; // AL: always
        default: return {0, 0, false};
    }
}

// ============================================================
// Struct offsets
// ============================================================
static size_t off_registersUsr = 0;
static size_t off_cpsr = 0;
static size_t off_cycles = 0;
static size_t off_halted = 0;
static size_t off_pipeline = 0;
static size_t off_pcData = 0;

// ============================================================
// C helpers
// ============================================================
extern "C" {

// Sync JIT register state back to Interpreter
void JitPpc_syncToInterp(Interpreter* interp, uint32_t* regs) {
    uint32_t** p = interp->getRegisters();
    for (int i = 0; i < 15; i++)
        *p[i] = regs[i];
    interp->getCpsrRef() = regs[16];
    interp->setPC(regs[15]);
}

// Sync Interpreter state into JIT register area
void JitPpc_syncFromInterp(Interpreter* interp, uint32_t* regs) {
    uint32_t** p = interp->getRegisters();
    for (int i = 0; i < 15; i++)
        regs[i] = *p[i];
    regs[15] = interp->getPC();   // get actual PC (with pipeline offset)
    regs[16] = interp->getCpsrRef();
}

// Run one opcode via interpreter (fallback)
int JitPpc_interpFallback(Interpreter* interp) {
    return interp->jitRunOpcode();
}

// Memory access helpers
uint32_t JitPpc_memRead32(Core* c, int a7, uint32_t addr)
    { return c->memory.read<uint32_t>((bool)a7, addr); }
uint16_t JitPpc_memRead16(Core* c, int a7, uint32_t addr)
    { return c->memory.read<uint16_t>((bool)a7, addr); }
uint8_t  JitPpc_memRead8(Core* c, int a7, uint32_t addr)
    { return c->memory.read<uint8_t>((bool)a7, addr); }
void JitPpc_memWrite32(Core* c, int a7, uint32_t addr, uint32_t v)
    { c->memory.write<uint32_t>((bool)a7, addr, v); }
void JitPpc_memWrite16(Core* c, int a7, uint32_t addr, uint16_t v)
    { c->memory.write<uint16_t>((bool)a7, addr, v); }
void JitPpc_memWrite8(Core* c, int a7, uint32_t addr, uint8_t v)
    { c->memory.write<uint8_t>((bool)a7, addr, v); }

// Add cycles
void JitPpc_addCycles(Interpreter* interp, uint32_t cyc) {
    uint32_t* p = (uint32_t*)((uint8_t*)interp + off_cycles);
    *p += cyc;
}

// Run scheduler — advance globalCycles and fire due events
void JitPpc_runScheduler(Core* core) {
    core->globalCycles += 64;
    while (!core->events.empty() &&
           core->globalCycles >= core->events.front().cycles) {
        SchedEvent evt = core->events.front();
        core->events.erase(core->events.begin());
        core->tasks[evt.task]();
    }
}

} // extern "C"

// ============================================================
// emit_call — load function address into r12 (volatile),
//             then bctrl. r12 is volatile so it survives
//             only until the call. We do NOT use any
//             callee-saved register for the call temp.
// ============================================================
static void emit_call(EmitCtx& ctx, void* fn) {
    uint32_t addr = (uint32_t)(uintptr_t)fn;
    // Use r12 (PPC_TMP3 = 6 in old code, but now r12 directly)
    // r12 is a volatile scratch register per PowerPC EABI
    // emit_li32 cannot use r0 as destination! Use r12 (= register 12).
    uint16_t lo = (uint16_t)(addr & 0xFFFF);
    uint16_t hi = (uint16_t)(addr >> 16);
    // r12 = hi<<16 | lo
    ctx.emit(ppc_addis(12, 0, (int16_t)hi));
    ctx.emit(ppc_ori(12, 12, lo));
    ctx.emit(ppc_mtctr(12));
    ctx.emit(ppc_bctr(true));  // bctrl
}

// ============================================================
// Reload Core pointer from frame into a volatile register
// ============================================================
static void emit_loadCore(EmitCtx& ctx, uint8_t dst) {
    ctx.emit(ppc_lwz(dst, FRAME_CORE_SLOT, 1));
}

// ============================================================
// Prologue — CORRECT ORDER: mflr before stwu
// ============================================================
static void emit_prologue(EmitCtx& ctx, Interpreter* interp, Core* core) {
    // 1. Capture LR BEFORE allocating stack frame (critical for ABI)
    ctx.emit(ppc_mflr(0));
    // 2. Allocate frame
    ctx.emit(ppc_stwu(1, -(int16_t)FRAME_SIZE, 1));
    // 3. Save our LR in the frame
    ctx.emit(ppc_stw(0, FRAME_LR, 1));
    // 4. Save callee-saved registers r14-r31
    for (int r = 14; r <= 31; r++)
        ctx.emit(ppc_stw(r, FRAME_R14 + (r-14)*4, 1));
    // 5. Load Interpreter pointer
    {
        int n = emit_li32(ctx.cur, PPC_INTERP, (uint32_t)(uintptr_t)interp);
        ctx.cur += n;
    }
    // 6. Store Core pointer in frame slot (not in a register)
    {
        int n = emit_li32(ctx.cur, 3, (uint32_t)(uintptr_t)core);
        ctx.cur += n;
        ctx.emit(ppc_stw(3, FRAME_CORE_SLOT, 1));
    }
    // 7. Load initial ARM PC
    {
        int n = emit_li32(ctx.cur, PPC_ARM_R15, ctx.armPC);
        ctx.cur += n;
    }
}

// ============================================================
// Epilogue — restore in reverse order, then blr
// ============================================================
static void emit_epilogue(EmitCtx& ctx) {
    // Restore callee-saved registers r14-r31
    for (int r = 14; r <= 31; r++)
        ctx.emit(ppc_lwz(r, FRAME_R14 + (r-14)*4, 1));
    // Restore LR
    ctx.emit(ppc_lwz(0, FRAME_LR, 1));
    ctx.emit(ppc_mtlr(0));
    // Deallocate frame
    ctx.emit(ppc_addi(1, 1, (int16_t)FRAME_SIZE));
    // Return
    ctx.emit(ppc_blr());
}

// ============================================================
// Save/restore volatile registers r3-r10 around C calls
// (These hold ARM registers in the new mapping since we moved
//  ARM regs to callee-saved r14-r31. So NO save needed!)
// The volatile regs r3-r10 are only used as temporaries and
// function arguments. They don't hold persistent ARM state.
// Therefore we don't need FRAME_VOLSAVE at all.
// ============================================================

// ============================================================
// Sync helpers — copy between JIT registers and Interpreter
// ============================================================
static void emit_syncToInterp(EmitCtx& ctx) {
    // Store ARM regs (in PPC r14-r29) into FRAME_REGSYNC
    for (int i = 0; i < 16; i++)
        ctx.emit(ppc_stw(ARM_TO_PPC[i], FRAME_REGSYNC + i*4, 1));
    // Store CPSR
    ctx.emit(ppc_stw(PPC_CPSR, FRAME_REGSYNC + 16*4, 1));
    // Call JitPpc_syncToInterp(interp, &regs)
    ctx.emit(ppc_mr(3, PPC_INTERP));
    ctx.emit(ppc_addi(4, 1, (int16_t)FRAME_REGSYNC));
    emit_call(ctx, (void*)JitPpc_syncToInterp);
}

static void emit_syncFromInterp(EmitCtx& ctx) {
    // Call JitPpc_syncFromInterp(interp, &regs)
    ctx.emit(ppc_mr(3, PPC_INTERP));
    ctx.emit(ppc_addi(4, 1, (int16_t)FRAME_REGSYNC));
    emit_call(ctx, (void*)JitPpc_syncFromInterp);
    // Load ARM regs from FRAME_REGSYNC into PPC r14-r29
    for (int i = 0; i < 16; i++)
        ctx.emit(ppc_lwz(ARM_TO_PPC[i], FRAME_REGSYNC + i*4, 1));
    // Load CPSR
    ctx.emit(ppc_lwz(PPC_CPSR, FRAME_REGSYNC + 16*4, 1));
}

// ============================================================
// CPSR flag update helpers
// ============================================================
static void emit_updateNZ(EmitCtx& ctx, uint8_t r) {
    // Clear N and Z bits in CPSR (bits 31 and 30)
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 2, 31)); // clear bits 31-30
    // Set N from bit 31 of r
    ctx.emit(ppc_rlwimi(PPC_CPSR, r, 0, 0, 0)); // insert bit 31 of r into bit 31 of CPSR
    // Set Z: compare r with 0, extract EQ bit
    ctx.emit(ppc_cmpi(0, r, 0));
    ctx.emit(ppc_mfcr(3));  // CR -> r3 (volatile, ok)
    // CR0 EQ is bit 29 of CR word (bit 2 of field 0)
    // We want to move it to CPSR bit 30 (Z)
    ctx.emit(ppc_rlwinm(3, 3, 3, 30, 30));  // shift CR0_EQ to bit 30
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, 3));
}

static void emit_updateC_fromXER(EmitCtx& ctx) {
    ctx.emit(ppc_mfxer(3));
    // XER CA is bit 29 of XER -> we want CPSR bit 29 (C)
    ctx.emit(ppc_rlwinm(3, 3, 0, 29, 29));  // extract bit 29
    // Clear C in CPSR
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 3, 1)); // clear bit 29
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, 3));
}

static void emit_updateV_fromXER(EmitCtx& ctx) {
    ctx.emit(ppc_mfxer(3));
    // XER OV is bit 30 of XER -> we want CPSR bit 28 (V)
    ctx.emit(ppc_rlwinm(3, 3, 30, 28, 28));  // rotate OV(bit30) to bit28
    // Clear V in CPSR
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 4, 2)); // clear bit 28
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, 3));
}

static void emit_updateNZCV_add(EmitCtx& ctx, uint8_t r) {
    emit_updateNZ(ctx, r);
    emit_updateC_fromXER(ctx);
    emit_updateV_fromXER(ctx);
}

static void emit_updateNZCV_sub(EmitCtx& ctx, uint8_t r) {
    // For subtraction: PPC subfc sets CA=1 when there's NO borrow
    // ARM C=1 when no borrow (borrow = !CA on PPC for subtraction)
    // PPC subfc(a-b): CA=1 if a >= b (unsigned)
    // ARM SUB: C=1 if a >= b (unsigned) — same!
    // So C = XER.CA directly for subtraction too.
    emit_updateNZ(ctx, r);
    emit_updateC_fromXER(ctx);
    emit_updateV_fromXER(ctx);
}

static void emit_updateC_fromTMP1(EmitCtx& ctx) {
    // TMP1 (r4) holds the carry bit in bit 0
    // Need to move it to CPSR bit 29 (C)
    ctx.emit(ppc_rlwinm(3, 4, 29, 29, 29));  // r3 = TMP1 bit 0 -> bit 29
    ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 3, 1)); // clear C
    ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, 3));
}

// ============================================================
// ARM immediate decode
// ============================================================
static uint32_t armImm(uint32_t op) {
    uint32_t imm = op & 0xFF;
    uint32_t rot = ((op >> 8) & 0xF) * 2;
    if (!rot) return imm;
    return (imm >> rot) | (imm << (32 - rot));
}

// ============================================================
// Shift helpers — dst,src,shift registers; TMP1 = carry out
// ============================================================
static void emit_lsl_imm(EmitCtx& ctx, uint8_t d, uint8_t s, uint8_t i, bool sc=false) {
    if (i == 0) {
        if (d != s) ctx.emit(ppc_mr(d, s));
        if (sc) ctx.emit(ppc_rlwinm(4, PPC_CPSR, 3, 31, 31)); // carry = old C
    } else if (i < 32) {
        if (sc) ctx.emit(ppc_rlwinm(4, s, i, 31, 31)); // carry = bit that shifts out
        ctx.emit(ppc_rlwinm(d, s, i, 0, 31-i));
    } else if (i == 32) {
        if (sc) ctx.emit(ppc_rlwinm(4, s, 0, 31, 31)); // carry = bit 0 of s
        ctx.emit(ppc_addi(d, 0, 0));
    } else {
        if (sc) ctx.emit(ppc_addi(4, 0, 0));
        ctx.emit(ppc_addi(d, 0, 0));
    }
}

static void emit_lsr_imm(EmitCtx& ctx, uint8_t d, uint8_t s, uint8_t i, bool sc=false) {
    if (i == 0 || i == 32) {
        if (sc) ctx.emit(ppc_rlwinm(4, s, 1, 31, 31)); // carry = bit 31
        ctx.emit(ppc_addi(d, 0, 0));
    } else if (i < 32) {
        if (sc) ctx.emit(ppc_rlwinm(4, s, 33-i, 31, 31));
        ctx.emit(ppc_rlwinm(d, s, 32-i, i, 31));
    } else {
        if (sc) ctx.emit(ppc_addi(4, 0, 0));
        ctx.emit(ppc_addi(d, 0, 0));
    }
}

static void emit_asr_imm(EmitCtx& ctx, uint8_t d, uint8_t s, uint8_t i, bool sc=false) {
    if (i == 0 || i >= 32) {
        if (sc) ctx.emit(ppc_rlwinm(4, s, 1, 31, 31));
        ctx.emit(ppc_srawi(d, s, 31));
    } else {
        if (sc) ctx.emit(ppc_rlwinm(4, s, 33-i, 31, 31));
        ctx.emit(ppc_srawi(d, s, i));
    }
}

static void emit_ror_imm(EmitCtx& ctx, uint8_t d, uint8_t s, uint8_t i, bool sc=false) {
    if (i == 0) {
        // RRX: rotate right through carry
        ctx.emit(ppc_rlwinm(3, PPC_CPSR, 3, 31, 31)); // r3 = C bit
        if (sc) ctx.emit(ppc_rlwinm(4, s, 0, 31, 31)); // carry out = bit 0 of s
        ctx.emit(ppc_rlwinm(d, s, 31, 1, 31));          // d = s >> 1
        ctx.emit(ppc_rlwimi(d, 3, 31, 0, 0));           // d[31] = C
    } else {
        i &= 31;
        if (!i) i = 32;
        if (i < 32) {
            if (sc) ctx.emit(ppc_rlwinm(4, s, 33-i, 31, 31));
            ctx.emit(ppc_rlwinm(d, s, 32-i, 0, 31));
        }
    }
}

static void emit_lsl_reg(EmitCtx& ctx, uint8_t d, uint8_t s, uint8_t sr, bool sc=false) {
    if (sc) ctx.emit(ppc_addi(4, 0, 0)); // TODO: proper carry
    ctx.emit(ppc_slw(d, s, sr));
}
static void emit_lsr_reg(EmitCtx& ctx, uint8_t d, uint8_t s, uint8_t sr, bool sc=false) {
    if (sc) ctx.emit(ppc_addi(4, 0, 0));
    ctx.emit(ppc_srw(d, s, sr));
}
static void emit_asr_reg(EmitCtx& ctx, uint8_t d, uint8_t s, uint8_t sr, bool sc=false) {
    if (sc) ctx.emit(ppc_addi(4, 0, 0));
    ctx.emit(ppc_sraw(d, s, sr));
}
static void emit_ror_reg(EmitCtx& ctx, uint8_t d, uint8_t s, uint8_t sr, bool sc=false) {
    if (sc) ctx.emit(ppc_addi(4, 0, 0));
    // ror d, s, sr: d = (s >> sr) | (s << (32-sr))
    ctx.emit(ppc_subfic(3, sr, 32));  // r3 = 32 - sr
    ctx.emit(ppc_rlwnm(d, s, 3, 0, 31));
}

static bool emit_shifterOp(EmitCtx& ctx, uint32_t op, uint8_t dst, bool sc, bool& cv) {
    cv = sc;
    bool isImm = (op >> 25) & 1;
    if (isImm) {
        uint32_t v = armImm(op);
        int n = emit_li32(ctx.cur, dst, v); ctx.cur += n;
        if (sc) {
            uint32_t rot = ((op >> 8) & 0xF) * 2;
            if (rot) {
                // carry = bit 31 of result
                ctx.emit(ppc_rlwinm(4, dst, 1, 31, 31));
            } else {
                cv = false; // no carry update for rot==0
            }
        }
        return true;
    }
    uint8_t rm = op & 0xF;
    uint8_t pRm = ARM_TO_PPC[rm];
    bool isReg = (op >> 4) & 1;
    uint8_t stype = (op >> 5) & 3;
    if (!isReg) {
        uint8_t sa = (op >> 7) & 0x1F;
        switch (stype) {
            case 0: emit_lsl_imm(ctx, dst, pRm, sa, sc); break;
            case 1: emit_lsr_imm(ctx, dst, pRm, sa ? sa : 32, sc); break;
            case 2: emit_asr_imm(ctx, dst, pRm, sa ? sa : 32, sc); break;
            case 3: emit_ror_imm(ctx, dst, pRm, sa, sc); break;
        }
    } else {
        uint8_t rs = (op >> 8) & 0xF;
        uint8_t pRs = ARM_TO_PPC[rs];
        ctx.emit(ppc_rlwinm(5, pRs, 0, 24, 31)); // r5 = rs & 0xFF
        ctx.emit(ppc_mr(6, pRm));                 // r6 = rm
        switch (stype) {
            case 0: emit_lsl_reg(ctx, dst, 6, 5, sc); break;
            case 1: emit_lsr_reg(ctx, dst, 6, 5, sc); break;
            case 2: emit_asr_reg(ctx, dst, 6, 5, sc); break;
            case 3: emit_ror_reg(ctx, dst, 6, 5, sc); break;
        }
        cv = false;
    }
    return true;
}

// ============================================================
// Data processing
// ============================================================
enum ArmDpOp {
    DP_AND=0,DP_EOR=1,DP_SUB=2,DP_RSB=3,
    DP_ADD=4,DP_ADC=5,DP_SBC=6,DP_RSC=7,
    DP_TST=8,DP_TEQ=9,DP_CMP=10,DP_CMN=11,
    DP_ORR=12,DP_MOV=13,DP_BIC=14,DP_MVN=15
};

static void patchBranchOffset(EmitCtx& ctx, size_t bi, size_t ti) {
    int32_t off = (int32_t)((ti - bi) * 4);
    ctx.base[bi] = (ctx.base[bi] & 0xFFFF0003u) | (uint32_t)(off & 0xFFFC);
}

static bool emit_dataProc(EmitCtx& ctx, uint32_t op) {
    uint8_t cond  = (op >> 28) & 0xF;
    uint8_t dpOp  = (op >> 21) & 0xF;
    bool    setCC = (op >> 20) & 1;
    uint8_t rn    = (op >> 16) & 0xF;
    uint8_t rd    = (op >> 12) & 0xF;

    if (rd == 15 && setCC) return false;  // MOVS PC/etc = mode change
    if (rd == 15) return false;           // PC writes handled separately

    uint8_t pRd = ARM_TO_PPC[rd];
    uint8_t pRn = ARM_TO_PPC[rn];

    size_t cbi = 0; bool hcb = false;
    if (cond != 14) {
        emit_setupCondFlags(ctx);
        CondBranch cb = armCondToPpc(cond);
        if (!cb.valid) return false;
        if (cb.bo != 20) {
            cbi = ctx.size(); hcb = true;
            // Branch OVER the instruction if condition NOT met
            ctx.emit(ppc_bc((cb.bo == 12) ? 4 : 12, cb.bi, 0));
        }
    }

    bool needC = (dpOp == DP_ADC || dpOp == DP_SBC || dpOp == DP_RSC);
    bool needSC = setCC && (dpOp == DP_AND || dpOp == DP_EOR || dpOp == DP_TST ||
                             dpOp == DP_TEQ || dpOp == DP_ORR || dpOp == DP_MOV ||
                             dpOp == DP_BIC || dpOp == DP_MVN);

    // Compute shifter operand into r3 (volatile temp)
    bool cv = false;
    // Use r3 as destination for shifter op
    // But r3 is also used by emit_call! We need a non-conflicting temp.
    // Since we're not calling any C function during data processing,
    // using r3 as a temp here is fine.
    if (!emit_shifterOp(ctx, op, 3, needSC, cv)) return false;

    // Load carry into XER if needed
    if (needC) {
        ctx.emit(ppc_rlwinm(5, PPC_CPSR, 0, 29, 29)); // r5 = C bit
        ctx.emit(ppc_mtxer(5));
    }

    bool test = (dpOp == DP_TST || dpOp == DP_TEQ ||
                 dpOp == DP_CMP || dpOp == DP_CMN);
    uint8_t res = test ? 5 : pRd;  // test ops don't write rd

    switch ((ArmDpOp)dpOp) {
        case DP_AND: case DP_TST: ctx.emit(ppc_and(res, pRn, 3)); break;
        case DP_EOR: case DP_TEQ: ctx.emit(ppc_xor(res, pRn, 3)); break;
        case DP_SUB: case DP_CMP: ctx.emit(ppc_subfc(res, 3, pRn)); break;
        case DP_RSB:               ctx.emit(ppc_subfc(res, pRn, 3)); break;
        case DP_ADD: case DP_CMN: ctx.emit(ppc_addc(res, pRn, 3)); break;
        case DP_ADC:               ctx.emit(ppc_adde(res, pRn, 3)); break;
        case DP_SBC:               ctx.emit(ppc_subfe(res, 3, pRn)); break;
        case DP_RSC:               ctx.emit(ppc_subfe(res, pRn, 3)); break;
        case DP_ORR:               ctx.emit(ppc_or(res, pRn, 3)); break;
        case DP_MOV:
            if (res != 3) ctx.emit(ppc_mr(res, 3));
            break;
        case DP_BIC:               ctx.emit(ppc_andc(res, pRn, 3)); break;
        case DP_MVN:               ctx.emit(ppc_nor(res, 3, 3)); break;
    }

    if (setCC) {
        switch ((ArmDpOp)dpOp) {
            case DP_ADD: case DP_CMN: case DP_ADC:
                emit_updateNZCV_add(ctx, res); break;
            case DP_SUB: case DP_CMP: case DP_RSB:
            case DP_SBC: case DP_RSC:
                emit_updateNZCV_sub(ctx, res); break;
            default:
                emit_updateNZ(ctx, res);
                if (cv) emit_updateC_fromTMP1(ctx);
                break;
        }
    }

    if (hcb) patchBranchOffset(ctx, cbi, ctx.size());
    return true;
}

// ============================================================
// Branch
// ============================================================
static bool emit_branch(EmitCtx& ctx, uint32_t op, uint32_t pc) {
    uint8_t cond = (op >> 28) & 0xF;

    // BX Rm
    if ((op & 0x0FFFFFF0) == 0x012FFF10) {
        uint8_t rm = op & 0xF;
        if (rm == 15) return false;
        uint8_t pRm = ARM_TO_PPC[rm];

        size_t cbi = 0; bool hcb = false;
        if (cond != 14) {
            emit_setupCondFlags(ctx);
            CondBranch cb = armCondToPpc(cond);
            if (!cb.valid) return false;
            if (cb.bo != 20) {
                cbi = ctx.size(); hcb = true;
                ctx.emit(ppc_bc((cb.bo == 12) ? 4 : 12, cb.bi, 0));
            }
        }

        // Update T bit in CPSR from bit 0 of Rm
        ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 27, 25)); // clear T (bit 5) and bits 26-27
        ctx.emit(ppc_rlwinm(3, pRm, 0, 31, 31));              // r3 = bit 0 of Rm
        ctx.emit(ppc_rlwinm(3, 3, 5, 26, 26));                // r3 = bit 0 << 5
        ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, 3));
        // PC = Rm & ~1
        ctx.emit(ppc_rlwinm(PPC_ARM_R15, pRm, 0, 0, 30));

        emit_syncToInterp(ctx);
        emit_epilogue(ctx);

        if (hcb) {
            patchBranchOffset(ctx, cbi, ctx.size());
            int n = emit_li32(ctx.cur, PPC_ARM_R15, pc + 4); ctx.cur += n;
            emit_syncToInterp(ctx);
            emit_epilogue(ctx);
        }
        ctx.hasExplicitReturn = true;
        return true;
    }

    // B / BL
    if ((op & 0x0E000000) == 0x0A000000) {
        bool lk = (op >> 24) & 1;
        int32_t off = (int32_t)(op << 8) >> 6;
        uint32_t tgt = pc + 8 + off;

        size_t cbi = 0; bool hcb = false;
        if (cond != 14) {
            emit_setupCondFlags(ctx);
            CondBranch cb = armCondToPpc(cond);
            if (!cb.valid) return false;
            if (cb.bo != 20) {
                cbi = ctx.size(); hcb = true;
                ctx.emit(ppc_bc((cb.bo == 12) ? 4 : 12, cb.bi, 0));
            }
        }

        if (lk) {
            int n = emit_li32(ctx.cur, PPC_ARM_R14, pc + 4); ctx.cur += n;
        }
        {
            int n = emit_li32(ctx.cur, PPC_ARM_R15, tgt); ctx.cur += n;
        }
        emit_syncToInterp(ctx);
        emit_epilogue(ctx);

        if (hcb) {
            patchBranchOffset(ctx, cbi, ctx.size());
            int n = emit_li32(ctx.cur, PPC_ARM_R15, pc + 4); ctx.cur += n;
            emit_syncToInterp(ctx);
            emit_epilogue(ctx);
        }
        ctx.hasExplicitReturn = true;
        return true;
    }
    return false;
}

// ============================================================
// Load/Store — fixed to not overlap FRAME_REGSYNC
// ============================================================
static bool emit_loadStore(EmitCtx& ctx, uint32_t op, uint32_t pc) {
    uint8_t cond = (op >> 28) & 0xF;
    bool ld   = (op >> 20) & 1;
    bool by   = (op >> 22) & 1;
    bool up   = (op >> 23) & 1;
    bool pre  = (op >> 24) & 1;
    bool wb   = (op >> 21) & 1;
    bool immO = !((op >> 25) & 1);
    uint8_t rn = (op >> 16) & 0xF;
    uint8_t rd = (op >> 12) & 0xF;

    if (rd == 15 || rn == 15) return false;
    uint8_t pRn = ARM_TO_PPC[rn];
    uint8_t pRd = ARM_TO_PPC[rd];

    size_t cbi = 0; bool hcb = false;
    if (cond != 14) {
        emit_setupCondFlags(ctx);
        CondBranch cb = armCondToPpc(cond);
        if (!cb.valid) return false;
        if (cb.bo != 20) {
            cbi = ctx.size(); hcb = true;
            ctx.emit(ppc_bc((cb.bo == 12) ? 4 : 12, cb.bi, 0));
        }
    }

    // Compute offset into r3 (volatile)
    if (immO) {
        uint32_t o = op & 0xFFF;
        int n = emit_li32(ctx.cur, 3, o); ctx.cur += n;
    } else {
        uint8_t rm = op & 0xF;
        ctx.emit(ppc_mr(3, ARM_TO_PPC[rm]));
    }

    // Compute effective address into r4
    if (pre) {
        if (up) ctx.emit(ppc_add(4, pRn, 3));
        else    ctx.emit(ppc_subf(4, 3, pRn));
    } else {
        ctx.emit(ppc_mr(4, pRn));
    }

    // Save offset (r3) to frame so it survives the call
    ctx.emit(ppc_stw(3, FRAME_VOLSAVE, 1));    // save offset
    // Save address (r4) to frame
    ctx.emit(ppc_stw(4, FRAME_VOLSAVE+4, 1));  // save addr

    // Load Core pointer
    emit_loadCore(ctx, 3);  // r3 = core
    ctx.emit(ppc_addi(4, 0, ctx.arm7 ? 1 : 0)); // r4 = arm7 flag
    ctx.emit(ppc_lwz(5, FRAME_VOLSAVE+4, 1));    // r5 = addr

    if (!ld) {
        // For store: r6 = value to store (pRd is callee-saved, still valid)
        ctx.emit(ppc_mr(6, pRd));
    }

    // Call memory function
    void* fn;
    if (ld)  fn = by ? (void*)JitPpc_memRead8  : (void*)JitPpc_memRead32;
    else     fn = by ? (void*)JitPpc_memWrite8 : (void*)JitPpc_memWrite32;
    emit_call(ctx, fn);

    // After call: r3 = return value (if load)
    if (ld) {
        // Move result into destination ARM register (callee-saved)
        ctx.emit(ppc_mr(pRd, 3));
    }

    // Handle writeback
    if (!pre) {
        // Post-index: always writeback rn = rn +/- offset
        ctx.emit(ppc_lwz(3, FRAME_VOLSAVE, 1));  // reload offset
        if (up) ctx.emit(ppc_add(pRn, pRn, 3));
        else    ctx.emit(ppc_subf(pRn, 3, pRn));
    } else if (wb && rn != rd) {
        // Pre-index with writeback: rn = computed addr
        ctx.emit(ppc_lwz(pRn, FRAME_VOLSAVE+4, 1));
    }

    if (hcb) patchBranchOffset(ctx, cbi, ctx.size());
    return true;
}

// ============================================================
// Multiply
// ============================================================
static bool emit_multiply(EmitCtx& ctx, uint32_t op) {
    bool sc  = (op >> 20) & 1;
    bool acc = (op >> 21) & 1;
    bool lng = (op >> 23) & 1;
    uint8_t rd = (op >> 16) & 0xF;
    uint8_t rn = (op >> 12) & 0xF;
    uint8_t rs = (op >> 8)  & 0xF;
    uint8_t rm = op & 0xF;

    if (rd == 15 || rm == 15 || rs == 15) return false;
    if (lng) return false;

    uint8_t pRd = ARM_TO_PPC[rd];
    uint8_t pRn = ARM_TO_PPC[rn];
    uint8_t pRs = ARM_TO_PPC[rs];
    uint8_t pRm = ARM_TO_PPC[rm];

    if (acc) {
        ctx.emit(ppc_mullw(3, pRm, pRs));   // r3 = rm * rs
        ctx.emit(ppc_add(pRd, 3, pRn));      // pRd = r3 + rn
    } else {
        ctx.emit(ppc_mullw(pRd, pRm, pRs));
    }

    if (sc) emit_updateNZ(ctx, pRd);
    return true;
}

// ============================================================
// Interpreter fallback — sync state, call interpreter, sync back
// ============================================================
static void emit_interpFallback(EmitCtx& ctx) {
    emit_syncToInterp(ctx);
    ctx.emit(ppc_mr(3, PPC_INTERP));
    emit_call(ctx, (void*)JitPpc_interpFallback);
    emit_syncFromInterp(ctx);
}

// ============================================================
// Thumb emitters (fixed to use new register mapping)
// ============================================================
static bool emit_thumb_lslImm(EmitCtx& ctx, uint16_t op) {
    uint8_t rd = op & 7, rs = (op>>3) & 7, i = (op>>6) & 0x1F;
    emit_lsl_imm(ctx, ARM_TO_PPC[rd], ARM_TO_PPC[rs], i, true);
    emit_updateNZ(ctx, ARM_TO_PPC[rd]);
    emit_updateC_fromTMP1(ctx);
    return true;
}

static bool emit_thumb_lsrImm(EmitCtx& ctx, uint16_t op) {
    uint8_t rd = op & 7, rs = (op>>3) & 7, i = (op>>6) & 0x1F;
    emit_lsr_imm(ctx, ARM_TO_PPC[rd], ARM_TO_PPC[rs], i ? i : 32, true);
    emit_updateNZ(ctx, ARM_TO_PPC[rd]);
    emit_updateC_fromTMP1(ctx);
    return true;
}

static bool emit_thumb_asrImm(EmitCtx& ctx, uint16_t op) {
    uint8_t rd = op & 7, rs = (op>>3) & 7, i = (op>>6) & 0x1F;
    emit_asr_imm(ctx, ARM_TO_PPC[rd], ARM_TO_PPC[rs], i ? i : 32, true);
    emit_updateNZ(ctx, ARM_TO_PPC[rd]);
    emit_updateC_fromTMP1(ctx);
    return true;
}

static bool emit_thumb_addSubReg(EmitCtx& ctx, uint16_t op) {
    uint8_t rd = op & 7, rs = (op>>3) & 7;
    bool sub = (op >> 9) & 1, imm = (op >> 10) & 1;
    uint8_t pRd = ARM_TO_PPC[rd], pRs = ARM_TO_PPC[rs];

    if (imm) {
        uint32_t v = (op >> 6) & 7;
        int n = emit_li32(ctx.cur, 3, v); ctx.cur += n;
    } else {
        uint8_t rn = (op >> 6) & 7;
        ctx.emit(ppc_mr(3, ARM_TO_PPC[rn]));
    }

    if (sub) {
        ctx.emit(ppc_subfc(pRd, 3, pRs));
        emit_updateNZCV_sub(ctx, pRd);
    } else {
        ctx.emit(ppc_addc(pRd, pRs, 3));
        emit_updateNZCV_add(ctx, pRd);
    }
    return true;
}

static bool emit_thumb_movImm(EmitCtx& ctx, uint16_t op) {
    uint8_t rd = (op >> 8) & 7;
    int n = emit_li32(ctx.cur, ARM_TO_PPC[rd], op & 0xFF); ctx.cur += n;
    emit_updateNZ(ctx, ARM_TO_PPC[rd]);
    return true;
}

static bool emit_thumb_cmpImm(EmitCtx& ctx, uint16_t op) {
    uint8_t rs = (op >> 8) & 7;
    int n = emit_li32(ctx.cur, 3, op & 0xFF); ctx.cur += n;
    ctx.emit(ppc_subfc(5, 3, ARM_TO_PPC[rs]));
    emit_updateNZCV_sub(ctx, 5);
    return true;
}

static bool emit_thumb_addImm8(EmitCtx& ctx, uint16_t op) {
    uint8_t rd = (op >> 8) & 7;
    int n = emit_li32(ctx.cur, 3, op & 0xFF); ctx.cur += n;
    ctx.emit(ppc_addc(ARM_TO_PPC[rd], ARM_TO_PPC[rd], 3));
    emit_updateNZCV_add(ctx, ARM_TO_PPC[rd]);
    return true;
}

static bool emit_thumb_subImm8(EmitCtx& ctx, uint16_t op) {
    uint8_t rd = (op >> 8) & 7;
    int n = emit_li32(ctx.cur, 3, op & 0xFF); ctx.cur += n;
    ctx.emit(ppc_subfc(ARM_TO_PPC[rd], 3, ARM_TO_PPC[rd]));
    emit_updateNZCV_sub(ctx, ARM_TO_PPC[rd]);
    return true;
}

static bool emit_thumb_aluOp(EmitCtx& ctx, uint16_t op) {
    uint8_t rd = op & 7, rs = (op>>3) & 7, o = (op>>6) & 0xF;
    uint8_t pRd = ARM_TO_PPC[rd], pRs = ARM_TO_PPC[rs];

    switch (o) {
        case 0:  ctx.emit(ppc_and(pRd, pRd, pRs)); emit_updateNZ(ctx, pRd); break;
        case 1:  ctx.emit(ppc_xor(pRd, pRd, pRs)); emit_updateNZ(ctx, pRd); break;
        case 2:  ctx.emit(ppc_slw(pRd, pRd, pRs)); emit_updateNZ(ctx, pRd); break;
        case 3:  ctx.emit(ppc_srw(pRd, pRd, pRs)); emit_updateNZ(ctx, pRd); break;
        case 4:  ctx.emit(ppc_sraw(pRd, pRd, pRs)); emit_updateNZ(ctx, pRd); break;
        case 5:  // ADC
            ctx.emit(ppc_rlwinm(3, PPC_CPSR, 0, 29, 29));
            ctx.emit(ppc_mtxer(3));
            ctx.emit(ppc_adde(pRd, pRd, pRs));
            emit_updateNZCV_add(ctx, pRd); break;
        case 6:  // SBC
            ctx.emit(ppc_rlwinm(3, PPC_CPSR, 0, 29, 29));
            ctx.emit(ppc_mtxer(3));
            ctx.emit(ppc_subfe(pRd, pRs, pRd));
            emit_updateNZCV_sub(ctx, pRd); break;
        case 7:  // ROR
            emit_ror_reg(ctx, pRd, pRd, pRs, true);
            emit_updateNZ(ctx, pRd); break;
        case 8:  // TST
            ctx.emit(ppc_and(5, pRd, pRs));
            emit_updateNZ(ctx, 5); break;
        case 9:  // NEG
            ctx.emit(ppc_addi(3, 0, 0));
            ctx.emit(ppc_subfc(pRd, pRd, 3));
            emit_updateNZCV_sub(ctx, pRd); break;
        case 10: // CMP
            ctx.emit(ppc_subfc(5, pRs, pRd));
            emit_updateNZCV_sub(ctx, 5); break;
        case 11: // CMN
            ctx.emit(ppc_addc(5, pRd, pRs));
            emit_updateNZCV_add(ctx, 5); break;
        case 12: ctx.emit(ppc_or(pRd, pRd, pRs)); emit_updateNZ(ctx, pRd); break;
        case 13: ctx.emit(ppc_mullw(pRd, pRd, pRs)); emit_updateNZ(ctx, pRd); break;
        case 14: ctx.emit(ppc_andc(pRd, pRd, pRs)); emit_updateNZ(ctx, pRd); break;
        case 15: ctx.emit(ppc_nor(pRd, pRs, pRs)); emit_updateNZ(ctx, pRd); break;
        default: return false;
    }
    return true;
}

static bool emit_thumb_hiRegOp(EmitCtx& ctx, uint16_t op) {
    uint8_t o  = (op >> 8) & 3;
    uint8_t h1 = (op >> 7) & 1;
    uint8_t h2 = (op >> 6) & 1;
    uint8_t rs = ((op >> 3) & 7) | (h2 << 3);
    uint8_t rd = (op & 7) | (h1 << 3);

    if (rd == 15 || rs == 15) return false;
    uint8_t pRd = ARM_TO_PPC[rd], pRs = ARM_TO_PPC[rs];

    switch (o) {
        case 0: ctx.emit(ppc_add(pRd, pRd, pRs)); break;
        case 1:
            ctx.emit(ppc_subfc(5, pRs, pRd));
            emit_updateNZCV_sub(ctx, 5); break;
        case 2: ctx.emit(ppc_mr(pRd, pRs)); break;
        case 3: // BX
            ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 27, 25));
            ctx.emit(ppc_rlwinm(3, pRs, 0, 31, 31));
            ctx.emit(ppc_rlwinm(3, 3, 5, 26, 26));
            ctx.emit(ppc_or(PPC_CPSR, PPC_CPSR, 3));
            ctx.emit(ppc_rlwinm(PPC_ARM_R15, pRs, 0, 0, 30));
            emit_syncToInterp(ctx);
            emit_epilogue(ctx);
            ctx.hasExplicitReturn = true;
            break;
    }
    return true;
}

static bool emit_thumb_ldrPc(EmitCtx& ctx, uint16_t op, uint32_t pc) {
    uint8_t rd = (op >> 8) & 7;
    uint32_t addr = ((pc + 4) & ~3u) + ((op & 0xFF) << 2);

    int n = emit_li32(ctx.cur, 5, addr); ctx.cur += n;  // r5 = addr
    emit_loadCore(ctx, 3);                               // r3 = core
    ctx.emit(ppc_addi(4, 0, ctx.arm7 ? 1 : 0));         // r4 = arm7
    // r5 already has addr
    emit_call(ctx, (void*)JitPpc_memRead32);
    // r3 = result
    ctx.emit(ppc_mr(ARM_TO_PPC[rd], 3));
    return true;
}

static bool emit_thumb_branch(EmitCtx& ctx, uint16_t op, uint32_t pc) {
    uint8_t cond = (op >> 8) & 0xF;
    if (cond == 0xF) return false;

    if (cond == 0xE) {
        // Unconditional branch
        int32_t off = (int8_t)(op & 0xFF); off <<= 1;
        uint32_t tgt = pc + 4 + off;
        int n = emit_li32(ctx.cur, PPC_ARM_R15, tgt); ctx.cur += n;
        emit_syncToInterp(ctx);
        emit_epilogue(ctx);
        ctx.hasExplicitReturn = true;
        return true;
    }

    // Conditional branch
    int32_t off = (int8_t)(op & 0xFF); off <<= 1;
    uint32_t tgt = pc + 4 + off;
    uint32_t fall = pc + 2;

    emit_setupCondFlags(ctx);
    CondBranch cb = armCondToPpc(cond);
    if (!cb.valid) return false;

    size_t cbi = ctx.size();
    ctx.emit(ppc_bc((cb.bo == 12) ? 4 : 12, cb.bi, 0)); // branch if NOT taken

    // Taken path
    {int n = emit_li32(ctx.cur, PPC_ARM_R15, tgt); ctx.cur += n;}
    emit_syncToInterp(ctx);
    emit_epilogue(ctx);

    // Not-taken path
    patchBranchOffset(ctx, cbi, ctx.size());
    {int n = emit_li32(ctx.cur, PPC_ARM_R15, fall); ctx.cur += n;}
    emit_syncToInterp(ctx);
    emit_epilogue(ctx);

    ctx.hasExplicitReturn = true;
    return true;
}

static bool emit_thumb_bl(EmitCtx& ctx, uint16_t op1, uint16_t op2, uint32_t pc) {
    int32_t hi = (int32_t)((op1 & 0x7FF) << 21) >> 9;
    int32_t lo = (op2 & 0x7FF) << 1;
    uint32_t tgt = pc + 4 + hi + lo;
    uint32_t ret = pc + 4;

    int n = emit_li32(ctx.cur, PPC_ARM_R14, ret | 1u); ctx.cur += n;
    n = emit_li32(ctx.cur, PPC_ARM_R15, tgt & ~1u); ctx.cur += n;

    uint8_t bb = (op2 >> 11) & 0x1F;
    if (bb == 0x1C) {
        // BLX: clear T bit
        ctx.emit(ppc_rlwinm(PPC_CPSR, PPC_CPSR, 0, 27, 25));
        ctx.emit(ppc_rlwinm(PPC_ARM_R15, PPC_ARM_R15, 0, 0, 29));
    }

    emit_syncToInterp(ctx);
    emit_epilogue(ctx);
    ctx.hasExplicitReturn = true;
    return true;
}

// ============================================================
// ARM instruction dispatch
// ============================================================
static bool emitARMInstr(EmitCtx& ctx, uint32_t op, uint32_t pc) {
    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;  // unconditional instructions

    uint32_t it = (op >> 25) & 7;
    switch (it) {
        case 0: case 1:
            if ((op & 0x0FC000F0) == 0x00000090) return emit_multiply(ctx, op);
            if ((op & 0x0FFFFFF0) == 0x012FFF10 ||
                (op & 0x0FFFFFF0) == 0x012FFF30)  return emit_branch(ctx, op, pc);
            // MRS/MSR, SWP, etc.
            if ((op & 0x0FB00FF0) == 0x01000000 ||
                (op & 0x0FB00000) == 0x03200000 ||
                (op & 0x0DB0F000) == 0x010F0000)  return false;
            return emit_dataProc(ctx, op);
        case 2: case 3: return emit_loadStore(ctx, op, pc);
        case 4: return false;  // LDM/STM
        case 5: return emit_branch(ctx, op, pc);
        case 6: return false;  // coprocessor
        case 7:
            if ((op & 0x0F000000) == 0x0F000000) return false;  // SWI
            if ((op & 0x0F000010) == 0x0E000010) return false;  // MCR/MRC
            return false;
    }
    return false;
}

// ============================================================
// Thumb instruction dispatch
// ============================================================
static bool emitThumbInstr(EmitCtx& ctx, uint16_t op, uint32_t pc) {
    uint8_t b14 = (op >> 14) & 3;
    uint8_t b11 = (op >> 11) & 7;

    switch (b14) {
        case 0:
            switch (b11) {
                case 0: return emit_thumb_lslImm(ctx, op);
                case 1: return emit_thumb_lsrImm(ctx, op);
                case 2: return emit_thumb_asrImm(ctx, op);
                case 3: return emit_thumb_addSubReg(ctx, op);
                case 4: return emit_thumb_movImm(ctx, op);
                case 5: return emit_thumb_cmpImm(ctx, op);
                case 6: return emit_thumb_addImm8(ctx, op);
                case 7: return emit_thumb_subImm8(ctx, op);
            }
            break;
        case 1: {
            uint8_t b10 = (op >> 10) & 3;
            if (b10 == 0) return emit_thumb_aluOp(ctx, op);
            if (b10 == 1) return emit_thumb_hiRegOp(ctx, op);
            if (b11 == 0x09) return emit_thumb_ldrPc(ctx, op, pc);
            return false;
        }
        case 2: return false;  // load/store — not yet implemented
        case 3: {
            uint8_t b12 = (op >> 12) & 0xF;
            if (b12 == 0xD || b12 == 0xE) return emit_thumb_branch(ctx, op, pc);
            return false;
        }
    }
    return false;
}

// ============================================================
// PC validity check
// ============================================================
static bool isValidArmPC(uint32_t pc, bool isGba) {
    pc &= ~1u;
    if (isGba) {
        return (pc <= 0x00003FFEu) ||
               (pc >= 0x02000000u && pc <= 0x0203FFFEu) ||
               (pc >= 0x03000000u && pc <= 0x03007FFEu) ||
               (pc >= 0x08000000u && pc <= 0x0DFFFFFFu);
    } else {
        return (pc <= 0x00003FFEu) ||
               (pc >= 0x01000000u && pc <= 0x013FFFFEu) ||
               (pc >= 0x02000000u && pc <= 0x023FFFFEu) ||
               (pc >= 0x03000000u && pc <= 0x037FFFFEu) ||
               (pc >= 0xFFFF0000u);
    }
}

// ============================================================
// Block compiler
// ============================================================
static JitBlock* compileBlock(Interpreter* interp, Core* core,
                               uint32_t armPC, bool arm7) {
    if (!isValidArmPC(armPC, core->gbaMode)) {
        printf("[JIT] bad PC 0x%08X (arm7=%d gba=%d)\n",
               armPC, arm7, core->gbaMode);
        return nullptr;
    }

    bool isThumb = interp->isThumb();
    size_t bucket = hashPC(armPC);
    JitBlock& slot = blockCache[bucket];

    if (slot.valid && slot.armPC == armPC && slot.thumb == isThumb)
        return &slot;

    if (codeBufferPos + MAX_BLOCK_SIZE * MAX_PPC_PER_ARM + 512 >= JIT_MAX_INSTRS)
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
    ctx.hasExplicitReturn = false;

    emit_prologue(ctx, interp, core);
    emit_syncFromInterp(ctx);

    uint32_t pc = armPC;
    int ic = 0;
    bool ended = false;

    for (ic = 0; ic < (int)MAX_BLOCK_SIZE && !ended; ic++) {
        // Update PC register to reflect pipeline (ARM: PC = instr+8, Thumb: instr+4)
        {
            int n = emit_li32(ctx.cur, PPC_ARM_R15, pc + (isThumb ? 4u : 8u));
            ctx.cur += n;
        }

        if (isThumb) {
            uint16_t top = core->memory.read<uint16_t>(arm7, pc);
            uint8_t  b11 = (top >> 11) & 0x1F;

            // Check for BL/BLX (two-halfword instruction)
            if (b11 == 0x1E) {
                uint16_t bot = core->memory.read<uint16_t>(arm7, pc + 2);
                uint8_t  bb  = (bot >> 11) & 0x1F;
                if (bb == 0x1F || bb == 0x1C) {
                    if (emit_thumb_bl(ctx, top, bot, pc)) {
                        pc += 4; ic++; ended = true;
                        continue;
                    }
                }
            }

            bool ok = emitThumbInstr(ctx, top, pc);
            if (!ok) {
                emit_interpFallback(ctx);
                ended = true;
            } else {
                pc += 2;
                if (((top >> 12) & 0xF) == 0xD ||
                    ((top >> 12) & 0xF) == 0xE) ended = true;
                if (ctx.hasExplicitReturn) ended = true;
            }
        } else {
            uint32_t op = core->memory.read<uint32_t>(arm7, pc);
            bool ok = emitARMInstr(ctx, op, pc);
            if (!ok) {
                emit_interpFallback(ctx);
                ended = true;
            } else {
                pc += 4;
                uint32_t it = (op >> 25) & 7;
                if (it == 5) ended = true;  // B/BL
                if ((op & 0x0FFFFFF0) == 0x012FFF10 ||
                    (op & 0x0FFFFFF0) == 0x012FFF30) ended = true; // BX
                if (it == 4 && ((op >> 20) & 1) &&
                    (op & (1u << 15))) ended = true; // LDM with PC
                if (ctx.hasExplicitReturn) ended = true;
            }
        }
    }

    if (!ctx.hasExplicitReturn) {
        emit_syncToInterp(ctx);
        emit_epilogue(ctx);
    }

    slot.armPC    = armPC;
    slot.ppcCode  = ctx.base;
    slot.ppcWords = (uint32_t)ctx.size();
    slot.armInstrs = (uint32_t)ic;
    slot.thumb    = isThumb;
    slot.valid    = true;
    codeBufferPos += ctx.size();

    flushCaches(ctx.base, ctx.size());
    return &slot;
}

// ============================================================
// Execute block via trampoline
// ============================================================
static void executeBlock(const JitBlock* block) {
    typedef void (*TrampolineFn)(uint32_t*);
    ((TrampolineFn)trampolineCode)(block->ppcCode);
}

// ============================================================
// Run entry points
// ============================================================
void runJitNds(Core& core) {
    if (!core.interpreter[0].halted) {
        uint32_t pc = core.interpreter[0].getActualPC();
        JitBlock* b = compileBlock(&core.interpreter[0], &core, pc, false);
        if (b) executeBlock(b);
        else   core.interpreter[0].jitRunOpcode();
    }
    if (!core.interpreter[1].halted) {
        uint32_t pc = core.interpreter[1].getActualPC();
        JitBlock* b = compileBlock(&core.interpreter[1], &core, pc, true);
        if (b) executeBlock(b);
        else   core.interpreter[1].jitRunOpcode();
    }
    JitPpc_runScheduler(&core);
}

void runJitGba(Core& core) {
    if (!core.interpreter[1].halted) {
        uint32_t pc = core.interpreter[1].getActualPC();
        JitBlock* b = compileBlock(&core.interpreter[1], &core, pc, true);
        if (b) executeBlock(b);
        else   core.interpreter[1].jitRunOpcode();
    }
    JitPpc_runScheduler(&core);
}

// ============================================================
// Offset computation
// ============================================================
static void computeOffsets() {
    off_halted       = Interpreter::offset_halted();
    off_pcData       = Interpreter::offset_pcData();
    off_pipeline     = Interpreter::offset_pipeline();
    off_registersUsr = Interpreter::offset_registersUsr();
    off_cpsr         = Interpreter::offset_cpsr();
    off_cycles       = Interpreter::offset_cycles();

    printf("[JIT] halted=%zu pcData=%zu pipeline=%zu "
           "regUsr=%zu cpsr=%zu cycles=%zu\n",
           off_halted, off_pcData, off_pipeline,
           off_registersUsr, off_cpsr, off_cycles);
}

// ============================================================
// initJit / shutdownJit / invalidateJitRange
// ============================================================
bool initJit(Core* core) {
    computeOffsets();
    initTrampoline();

    codeBuffer = (uint32_t*)memalign(32, JIT_CODE_SIZE);
    if (!codeBuffer) {
        printf("[JIT] alloc failed\n");
        return false;
    }

    codeBufferPos = 0;
    for (size_t i = 0; i < BLOCK_CACHE_SIZE; i++)
        blockCache[i].valid = false;

    printf("[JIT] ready buf=%p %zuKB\n",
           (void*)codeBuffer, JIT_CODE_SIZE/1024);

    if (core)
        core->setRunFunc(core->gbaMode ? runJitGba : runJitNds);

    return true;
}

void shutdownJit(Core* core) {
    if (core) {
        core->setRunFunc(core->gbaMode
            ? static_cast<void(*)(Core&)>(&Interpreter::runCoreSingle<true,0>)
            : &Interpreter::runCoreNds);
    }
    if (codeBuffer) {
        free(codeBuffer);
        codeBuffer = nullptr;
    }
}

void invalidateJitRange(uint32_t start, uint32_t end) {
    for (size_t i = 0; i < BLOCK_CACHE_SIZE; i++) {
        if (blockCache[i].valid &&
            blockCache[i].armPC >= start &&
            blockCache[i].armPC < end)
            blockCache[i].valid = false;
    }
}

} // namespace JitPpc
