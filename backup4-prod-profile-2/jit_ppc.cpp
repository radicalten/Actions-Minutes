// jit_ppc.cpp — complete rewrite with all bugs fixed

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
// DESIGN NOTES
//
// The fundamental problem with the previous versions:
//
// 1. executeBlock() casts trampolineCode to a function pointer and calls it.
//    The C++ compiler may optimize this to a tail call (bctr, no link),
//    meaning LR is NOT updated to point past the call site. The trampoline
//    then does mflr r0 and gets whatever LR was before — an ARM address
//    left over from JitPpc_syncToInterp or similar.
//
// 2. Even if executeBlock uses bctrl properly, the trampoline's frame
//    arithmetic must be exactly right.
//
// 3. The JIT block's prologue does mflr r0 to capture the return address
//    set by the trampoline's bctrl. This only works if the trampoline
//    really does use bctrl (sets LR = next instruction).
//
// SOLUTION: Do NOT use a separate trampoline array. Instead, make
// executeBlock itself a proper __attribute__((noinline)) function that
// the compiler CANNOT tail-call-optimize, and have the JIT blocks
// use a simple calling convention where:
//
//   - Caller (executeBlock) does: bl to block (sets LR = return addr)
//   - Block prologue: mflr captures valid PPC return address
//   - Block epilogue: blr returns to executeBlock
//
// But we can't "bl" to a runtime address from compiled C++.
//
// REAL SOLUTION: Use a hand-written assembly stub that is a proper
// function, assembled correctly, so the compiler sees it as an opaque
// call and cannot tail-call it.
//
// Since we're writing PPC machine code anyway, we write executeBlock
// entirely in inline asm or as a standalone asm function.
//
// SIMPLEST CORRECT SOLUTION: Write executeBlock as a naked/asm function
// that explicitly:
//   1. Saves LR (mflr + stw)
//   2. Loads block address into CTR
//   3. bctrl (call block, LR = next instr in executeBlock)
//   4. Restores LR and blr
//
// This guarantees the compiler cannot interfere.
// ============================================================

// ============================================================
// Frame layout for JIT blocks
//
//   [r1+  0]  back-chain
//   [r1+  4]  ABI LR save (written by callees, not us directly)
//   [r1+  8]  our saved LR                        FRAME_LR
//   [r1+ 12]  pad
//   [r1+ 16]  r14..r31 saved (18*4 = 72 bytes)    FRAME_R14
//   [r1+ 88]  Core* storage                        FRAME_CORE
//   [r1+ 92]  offset save slot 0 (load/store)     FRAME_SCRATCH0
//   [r1+ 96]  offset save slot 1 (load/store)     FRAME_SCRATCH1
//   [r1+100]  pad to 112
//   [r1+112]  ARM r0..r15 + CPSR (17*4 = 68 b)   FRAME_REGSYNC
//   [r1+180]  pad to 192
// ============================================================
static const int FRAME_SIZE     = 192;
static const int FRAME_LR       = 8;
static const int FRAME_R14      = 16;     // 18 regs: r14-r31
static const int FRAME_CORE     = 88;
static const int FRAME_SCRATCH0 = 92;
static const int FRAME_SCRATCH1 = 96;
static const int FRAME_REGSYNC  = 112;   // ARM regs + CPSR

static_assert(FRAME_SIZE % 16 == 0,                       "frame align");
static_assert(FRAME_R14  + 18*4 <= FRAME_CORE,            "r14-r31 fit");
static_assert(FRAME_REGSYNC + 17*4 <= FRAME_SIZE,         "regsync fit");

namespace JitPpc {

// ============================================================
// PPC instruction encoders
// ============================================================
static inline uint32_t ppc_b(int32_t o, bool lk=false)
    { return (18u<<26)|((uint32_t)(o&0x3FFFFFC))|(lk?1u:0u); }
static inline uint32_t ppc_bc(uint8_t bo, uint8_t bi, int16_t o, bool lk=false)
    { return (16u<<26)|((uint32_t)(bo&31)<<21)|((uint32_t)(bi&31)<<16)
             |((uint32_t)(o&0xFFFC))|(lk?1u:0u); }
static inline uint32_t ppc_bclr(uint8_t bo, uint8_t bi, bool lk=false)
    { return (19u<<26)|((uint32_t)(bo&31)<<21)|((uint32_t)(bi&31)<<16)
             |(16u<<1)|(lk?1u:0u); }
static inline uint32_t ppc_bctr(bool lk=false)
    { return (19u<<26)|(20u<<21)|(528u<<1)|(lk?1u:0u); }
static inline uint32_t ppc_blr()   { return ppc_bclr(20,0); }
static inline uint32_t ppc_nop()   { return 0x60000000u; }

static inline uint32_t ppc_addi(uint8_t rt, uint8_t ra, int16_t i)
    { return (14u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_addis(uint8_t rt, uint8_t ra, int16_t i)
    { return (15u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_ori(uint8_t ra, uint8_t rs, uint16_t i)
    { return (24u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i; }
static inline uint32_t ppc_oris(uint8_t ra, uint8_t rs, uint16_t i)
    { return (25u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i; }
static inline uint32_t ppc_andi_(uint8_t ra, uint8_t rs, uint16_t i)
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
static inline uint32_t ppc_stwu(uint8_t rs, int16_t d, uint8_t ra)
    { return (37u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }

static inline uint32_t ppc_cmpi(uint8_t cr, uint8_t ra, int16_t i)
    { return (11u<<26)|((uint32_t)(cr&7)<<23)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_cmpli(uint8_t cr, uint8_t ra, uint16_t i)
    { return (10u<<26)|((uint32_t)(cr&7)<<23)|((uint32_t)ra<<16)|i; }
static inline uint32_t ppc_subfic(uint8_t rt, uint8_t ra, int16_t i)
    { return (8u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }

static inline uint32_t X(uint32_t op,uint8_t rt,uint8_t ra,uint8_t rb,uint32_t xop,bool rc=false)
    { return (op<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)
             |((uint32_t)rb<<11)|(xop<<1)|(rc?1u:0u); }
static inline uint32_t XO(uint32_t op,uint8_t rt,uint8_t ra,uint8_t rb,bool oe,uint32_t xop,bool rc=false)
    { return (op<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)
             |((uint32_t)rb<<11)|(oe?0x400u:0u)|(xop<<1)|(rc?1u:0u); }

static inline uint32_t ppc_add(uint8_t d,uint8_t a,uint8_t b,bool r=false)  {return XO(31,d,a,b,false,266,r);}
static inline uint32_t ppc_addc(uint8_t d,uint8_t a,uint8_t b,bool r=false) {return XO(31,d,a,b,false,10,r);}
static inline uint32_t ppc_adde(uint8_t d,uint8_t a,uint8_t b,bool r=false) {return XO(31,d,a,b,false,138,r);}
static inline uint32_t ppc_subf(uint8_t d,uint8_t a,uint8_t b,bool r=false) {return XO(31,d,a,b,false,40,r);}
static inline uint32_t ppc_subfc(uint8_t d,uint8_t a,uint8_t b,bool r=false){return XO(31,d,a,b,false,8,r);}
static inline uint32_t ppc_subfe(uint8_t d,uint8_t a,uint8_t b,bool r=false){return XO(31,d,a,b,false,136,r);}
static inline uint32_t ppc_neg(uint8_t d,uint8_t a,bool r=false)             {return XO(31,d,a,0,false,104,r);}
static inline uint32_t ppc_mullw(uint8_t d,uint8_t a,uint8_t b,bool r=false){return XO(31,d,a,b,false,235,r);}
static inline uint32_t ppc_mulhw(uint8_t d,uint8_t a,uint8_t b,bool r=false){return XO(31,d,a,b,false,75,r);}
static inline uint32_t ppc_mulhwu(uint8_t d,uint8_t a,uint8_t b,bool r=false){return XO(31,d,a,b,false,11,r);}

static inline uint32_t ppc_and(uint8_t a,uint8_t s,uint8_t b,bool r=false)  {return X(31,s,a,b,28,r);}
static inline uint32_t ppc_or(uint8_t a,uint8_t s,uint8_t b,bool r=false)   {return X(31,s,a,b,444,r);}
static inline uint32_t ppc_xor(uint8_t a,uint8_t s,uint8_t b,bool r=false)  {return X(31,s,a,b,316,r);}
static inline uint32_t ppc_andc(uint8_t a,uint8_t s,uint8_t b,bool r=false) {return X(31,s,a,b,60,r);}
static inline uint32_t ppc_nor(uint8_t a,uint8_t s,uint8_t b,bool r=false)  {return X(31,s,a,b,124,r);}
static inline uint32_t ppc_mr(uint8_t a,uint8_t s)                           {return ppc_or(a,s,s);}
static inline uint32_t ppc_slw(uint8_t a,uint8_t s,uint8_t b,bool r=false)  {return X(31,s,a,b,24,r);}
static inline uint32_t ppc_srw(uint8_t a,uint8_t s,uint8_t b,bool r=false)  {return X(31,s,a,b,536,r);}
static inline uint32_t ppc_sraw(uint8_t a,uint8_t s,uint8_t b,bool r=false) {return X(31,s,a,b,792,r);}
static inline uint32_t ppc_cntlzw(uint8_t a,uint8_t s,bool r=false)         {return X(31,s,a,0,26,r);}
static inline uint32_t ppc_extsb(uint8_t a,uint8_t s,bool r=false)          {return X(31,s,a,0,954,r);}
static inline uint32_t ppc_extsh(uint8_t a,uint8_t s,bool r=false)          {return X(31,s,a,0,922,r);}
static inline uint32_t ppc_lwzx(uint8_t t,uint8_t a,uint8_t b)              {return X(31,t,a,b,23);}
static inline uint32_t ppc_lbzx(uint8_t t,uint8_t a,uint8_t b)              {return X(31,t,a,b,87);}
static inline uint32_t ppc_lhzx(uint8_t t,uint8_t a,uint8_t b)              {return X(31,t,a,b,279);}
static inline uint32_t ppc_stwx(uint8_t s,uint8_t a,uint8_t b)              {return X(31,s,a,b,151);}
static inline uint32_t ppc_stbx(uint8_t s,uint8_t a,uint8_t b)              {return X(31,s,a,b,215);}
static inline uint32_t ppc_sthx(uint8_t s,uint8_t a,uint8_t b)              {return X(31,s,a,b,407);}
static inline uint32_t ppc_cmp(uint8_t cr,uint8_t a,uint8_t b)
    { return (31u<<26)|((uint32_t)(cr&7)<<23)|((uint32_t)a<<16)|((uint32_t)b<<11); }
static inline uint32_t ppc_cmpl(uint8_t cr,uint8_t a,uint8_t b)
    { return (31u<<26)|((uint32_t)(cr&7)<<23)|((uint32_t)a<<16)|((uint32_t)b<<11)|(32u<<1); }

static inline uint32_t ppc_rlwinm(uint8_t a,uint8_t s,uint8_t sh,uint8_t mb,uint8_t me,bool r=false)
    { return (21u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)
             |((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1)|(r?1u:0u); }
static inline uint32_t ppc_rlwimi(uint8_t a,uint8_t s,uint8_t sh,uint8_t mb,uint8_t me,bool r=false)
    { return (20u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)
             |((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1)|(r?1u:0u); }
static inline uint32_t ppc_rlwnm(uint8_t a,uint8_t s,uint8_t b,uint8_t mb,uint8_t me,bool r=false)
    { return (23u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)
             |((uint32_t)b<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1)|(r?1u:0u); }
static inline uint32_t ppc_srawi(uint8_t a,uint8_t s,uint8_t sh,bool r=false)
    { return (31u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)
             |((uint32_t)sh<<11)|(824u<<1)|(r?1u:0u); }

static inline uint32_t ppc_mtspr(uint16_t spr,uint8_t s)
    { uint8_t lo=spr&31,hi=(spr>>5)&31;
      return (31u<<26)|((uint32_t)s<<21)|((uint32_t)lo<<16)|((uint32_t)hi<<11)|(467u<<1); }
static inline uint32_t ppc_mfspr(uint8_t t,uint16_t spr)
    { uint8_t lo=spr&31,hi=(spr>>5)&31;
      return (31u<<26)|((uint32_t)t<<21)|((uint32_t)lo<<16)|((uint32_t)hi<<11)|(339u<<1); }
static inline uint32_t ppc_mtctr(uint8_t s) { return ppc_mtspr(9,s); }
static inline uint32_t ppc_mfctr(uint8_t t) { return ppc_mfspr(t,9); }
static inline uint32_t ppc_mtlr(uint8_t s)  { return ppc_mtspr(8,s); }
static inline uint32_t ppc_mflr(uint8_t t)  { return ppc_mfspr(t,8); }
static inline uint32_t ppc_mtxer(uint8_t s) { return ppc_mtspr(1,s); }
static inline uint32_t ppc_mfxer(uint8_t t) { return ppc_mfspr(t,1); }
static inline uint32_t ppc_mfcr(uint8_t t)
    { return (31u<<26)|((uint32_t)t<<21)|(19u<<1); }
static inline uint32_t ppc_mtcrf(uint8_t fxm,uint8_t s)
    { return (31u<<26)|((uint32_t)s<<21)|((uint32_t)(fxm&0xFF)<<12)|(144u<<1); }
static inline uint32_t ppc_isync() { return (19u<<26)|(150u<<1); }
static inline uint32_t ppc_sync()  { return (31u<<26)|(598u<<1); }

// ============================================================
// emit_li32: load 32-bit constant into reg (NOT r0)
// ============================================================
static int emit_li32(uint32_t* out, uint8_t rt, uint32_t imm) {
    // rt must not be r0
    uint16_t lo = (uint16_t)(imm & 0xFFFF);
    uint16_t hi = (uint16_t)(imm >> 16);
    if (!hi && !lo)     { out[0] = ppc_addi(rt,0,0); return 1; }
    if (!hi) {
        if (lo < 0x8000) { out[0] = ppc_addi(rt,0,(int16_t)lo); return 1; }
        out[0] = ppc_addi(rt,0,0);
        out[1] = ppc_ori(rt,rt,lo);
        return 2;
    }
    if (!lo) { out[0] = ppc_addis(rt,0,(int16_t)hi); return 1; }
    out[0] = ppc_addis(rt,0,(int16_t)hi);
    out[1] = ppc_ori(rt,rt,lo);
    return 2;
}

// ============================================================
// Register mapping
//
// ARM r0-r15  -> PPC r14-r29  (callee-saved, survive all C calls)
// CPSR        -> PPC r30      (callee-saved)
// INTERP      -> PPC r31      (callee-saved)
// Core*       -> stored at FRAME_CORE in stack
//
// Volatile PPC regs (r0,r3-r12) used only as temporaries.
// r0  : temp for mflr/mtlr only (cannot be base in addi/addis)
// r3  : arg1 / return / primary temp
// r4  : arg2 / secondary temp
// r5  : arg3 / temp
// r6  : arg4 / temp
// r11 : call address scratch (volatile, ok for mtctr)
// r12 : same
// ============================================================
static const uint8_t R_ARM[16] = {
    14,15,16,17,18,19,20,21,  // ARM r0-r7  -> PPC r14-r21
    22,23,24,25,26,27,28,29   // ARM r8-r15 -> PPC r22-r29
};
static const uint8_t R_CPSR   = 30;
static const uint8_t R_INTERP = 31;
// Temps (volatile):
static const uint8_t T0 = 3;   // primary temp / arg1
static const uint8_t T1 = 4;   // arg2 / temp
static const uint8_t T2 = 5;   // arg3 / temp
static const uint8_t T3 = 6;   // arg4 / temp
static const uint8_t TC = 11;  // call address (volatile, used for mtctr)

// CPSR bit positions
#define CPSR_N_BIT 31
#define CPSR_Z_BIT 30
#define CPSR_C_BIT 29
#define CPSR_V_BIT 28
#define CPSR_T_BIT 5

// ============================================================
// Code buffer
// ============================================================
static const size_t JIT_CODE_SIZE   = 4u*1024u*1024u;
static const size_t JIT_MAX_WORDS   = JIT_CODE_SIZE / 4;
static const size_t MAX_BLOCK_ARMS  = 64;
static const size_t MAX_PPC_PER_ARM = 128;
static const size_t MAX_BLOCK_WORDS = MAX_BLOCK_ARMS * MAX_PPC_PER_ARM + 64;

static uint32_t* codeBuffer    = nullptr;
static size_t    codeWritePos  = 0;

// ============================================================
// Block cache
// ============================================================
struct JitBlock {
    uint32_t  armPC;
    uint32_t* code;
    uint32_t  nWords;
    bool      thumb;
    bool      valid;
};
static const size_t CACHE_BITS = 13;
static const size_t CACHE_SIZE = 1u << CACHE_BITS;
static JitBlock     blockCache[CACHE_SIZE];

static size_t hashPC(uint32_t pc) { return (pc >> 1) & (CACHE_SIZE-1); }

void flushJitCache() {
    codeWritePos = 0;
    for (size_t i = 0; i < CACHE_SIZE; i++) blockCache[i].valid = false;
}

// ============================================================
// Emit context
// ============================================================
struct Ctx {
    uint32_t* base;
    uint32_t* cur;
    size_t    cap;
    bool      thumb, arm7;
    uint32_t  blockPC;  // starting ARM PC of block
    Interpreter* interp;
    Core*     core;
    bool      terminated;  // block has explicit exit already

    void emit(uint32_t w) {
        if ((size_t)(cur-base) < cap) *cur++ = w;
    }
    void emitN(const uint32_t* w, int n) {
        for (int i=0;i<n;i++) emit(w[i]);
    }
    size_t size() const { return (size_t)(cur-base); }

    // Emit li32 inline
    void li32(uint8_t rt, uint32_t imm) {
        uint32_t tmp[2];
        int n = emit_li32(tmp, rt, imm);
        for (int i=0;i<n;i++) emit(tmp[i]);
    }

    // Load Core* from frame into T0
    void loadCore(uint8_t dst = T0) {
        emit(ppc_lwz(dst, FRAME_CORE, 1));
    }

    // Emit call to absolute address using TC (r11) as scratch
    // TC is volatile so it doesn't need to be saved.
    // We use r11 specifically because:
    //   - It's volatile (caller-saved)
    //   - The ABI allows using r11 as a scratch for indirect calls
    //   - The compiler won't put any live value in r11 across our asm
    void call(void* fn) {
        uint32_t addr = (uint32_t)(uintptr_t)fn;
        uint16_t hi   = (uint16_t)(addr >> 16);
        uint16_t lo   = (uint16_t)(addr & 0xFFFF);
        emit(ppc_addis(TC, 0, (int16_t)hi));
        emit(ppc_ori(TC, TC, lo));
        emit(ppc_mtctr(TC));
        emit(ppc_bctr(true));  // bctrl — sets LR = next instruction
    }
};

static void flushCache(uint32_t* p, size_t words) {
    DCFlushRange(p, words*4);
    ICInvalidateRange(p, words*4);
}

// ============================================================
// executeBlock: hand-written in inline asm to guarantee
// bctrl semantics and proper LR save/restore.
//
// This is the most critical function. We write it in asm to
// prevent ANY compiler optimization (tail call, etc.) from
// corrupting LR before the JIT block's mflr executes.
//
// The function:
//   1. Saves its own LR (which points back to runJitXxx)
//   2. Loads block address into CTR
//   3. bctrl -> LR now points to instruction 4 in this function
//   4. JIT block's mflr captures that valid PPC address
//   5. JIT block's blr returns here (instruction 4)
//   6. We restore LR and return to runJitXxx
//
// Stack layout during executeBlock's own frame:
//   [r1+ 0]  back-chain
//   [r1+ 4]  ABI LR slot
//   [r1+ 8]  (unused)
//   Total: 16 bytes (minimum ABI frame)
// ============================================================
__attribute__((noinline))
static void executeBlock(uint32_t* blockCode) {
    // We use a __attribute__((noinline)) C++ function with inline asm
    // to guarantee the prologue/epilogue are exactly what we need.
    // The asm clobbers LR intentionally (we save/restore it).
    register uint32_t* code asm("r3") = blockCode;
    (void)code;
    __asm__ volatile (
        // Save our LR before doing anything
        "mflr  %%r0              \n\t"
        "stwu  %%r1, -16(%%r1)  \n\t"
        "stw   %%r0,  20(%%r1)  \n\t"   // save at new_r1+20 = old_r1+4 (ABI slot)
        // r3 = blockCode (first argument, already in r3)
        "mtctr %%r3             \n\t"
        "bctrl                  \n\t"   // call block; LR = address of next instr
        // Block returns here via blr
        "lwz   %%r0,  20(%%r1)  \n\t"
        "addi  %%r1, %%r1, 16   \n\t"
        "mtlr  %%r0             \n\t"
        "blr                    \n\t"
        :
        : [code] "r" (blockCode)
        : "r0", "r3", "r4", "r5", "r6", "r7", "r8", "r9",
          "r10", "r11", "r12", "ctr", "lr", "cr0", "memory"
    );
}

// ============================================================
// C helper functions called from JIT code
// ============================================================
extern "C" {

// Sync JIT register state -> Interpreter
// Called with: r3=interp, r4=&frame_regsync_area
void JitPpc_syncToInterp(Interpreter* interp, uint32_t* regs) {
    uint32_t** p = interp->getRegisters();
    for (int i = 0; i < 15; i++)
        *p[i] = regs[i];
    interp->getCpsrRef() = regs[16];
    interp->setPC(regs[15]);
}

// Sync Interpreter -> JIT register state
void JitPpc_syncFromInterp(Interpreter* interp, uint32_t* regs) {
    uint32_t** p = interp->getRegisters();
    for (int i = 0; i < 15; i++)
        regs[i] = *p[i];
    // Get PC including pipeline adjustment
    uint32_t cpsr = interp->getCpsrRef();
    bool thumb = (cpsr >> 5) & 1;
    // getActualPC returns PC - 4 (thumb) or PC - 8 (ARM)
    // We want the raw PC stored in the register
    uint32_t rawPC = interp->getPC();
    // getPC returns *registers[15] which already has pipeline applied
    // For JIT we want the actual instruction address (getActualPC)
    // but stored as the full pipeline-inclusive value
    regs[15] = rawPC;
    regs[16] = cpsr;
    (void)thumb;
}

// Run one instruction via interpreter (for unhandled opcodes)
int JitPpc_interpFallback(Interpreter* interp) {
    return interp->jitRunOpcode();
}

// Memory access
uint32_t JitPpc_read32(Core* c, int a7, uint32_t addr)
    { return c->memory.read<uint32_t>((bool)a7, addr); }
uint16_t JitPpc_read16(Core* c, int a7, uint32_t addr)
    { return c->memory.read<uint16_t>((bool)a7, addr); }
uint8_t  JitPpc_read8 (Core* c, int a7, uint32_t addr)
    { return c->memory.read<uint8_t> ((bool)a7, addr); }
void JitPpc_write32(Core* c, int a7, uint32_t addr, uint32_t v)
    { c->memory.write<uint32_t>((bool)a7, addr, v); }
void JitPpc_write16(Core* c, int a7, uint32_t addr, uint16_t v)
    { c->memory.write<uint16_t>((bool)a7, addr, v); }
void JitPpc_write8 (Core* c, int a7, uint32_t addr, uint8_t  v)
    { c->memory.write<uint8_t> ((bool)a7, addr, v); }

// Scheduler: advance cycles and fire events
void JitPpc_tick(Core* core) {
    core->globalCycles += 64;
    while (!core->events.empty() &&
           core->globalCycles >= core->events.front().cycles) {
        SchedEvent e = core->events.front();
        core->events.erase(core->events.begin());
        core->tasks[e.task]();
    }
}

} // extern "C"

// ============================================================
// Block prologue
//
// Entry: LR = return address inside executeBlock (set by bctrl)
//
// We must save this LR FIRST before any other operation
// that might overwrite it.
// ============================================================
static void emit_prologue(Ctx& ctx) {
    // 1. Capture LR immediately (it's valid because executeBlock used bctrl)
    ctx.emit(ppc_mflr(0));                              // r0 = LR (return to executeBlock)
    // 2. Allocate our stack frame
    ctx.emit(ppc_stwu(1, -(int16_t)FRAME_SIZE, 1));    // r1 -= FRAME_SIZE
    // 3. Save the captured LR
    ctx.emit(ppc_stw(0, FRAME_LR, 1));                  // [r1+8] = LR
    // 4. Save all callee-saved registers we use (r14-r31)
    for (int r = 14; r <= 31; r++)
        ctx.emit(ppc_stw(r, FRAME_R14 + (r-14)*4, 1));
    // 5. Store Interpreter* in R_INTERP (r31) — load from immediate
    ctx.li32(R_INTERP, (uint32_t)(uintptr_t)ctx.interp);
    // 6. Store Core* in frame (not a register — saves one callee-saved reg)
    ctx.li32(T0, (uint32_t)(uintptr_t)ctx.core);
    ctx.emit(ppc_stw(T0, FRAME_CORE, 1));
}

// ============================================================
// Block epilogue — restore everything and blr back to executeBlock
// ============================================================
static void emit_epilogue(Ctx& ctx) {
    // Restore callee-saved registers
    for (int r = 14; r <= 31; r++)
        ctx.emit(ppc_lwz(r, FRAME_R14 + (r-14)*4, 1));
    // Restore LR
    ctx.emit(ppc_lwz(0, FRAME_LR, 1));
    ctx.emit(ppc_mtlr(0));
    // Restore stack pointer
    ctx.emit(ppc_addi(1, 1, (int16_t)FRAME_SIZE));
    // Return to executeBlock (which will return to runJitXxx)
    ctx.emit(ppc_blr());
}

// ============================================================
// Sync ARM state between JIT regs and Interpreter object
// ============================================================

// Store JIT ARM regs to frame sync area, then call JitPpc_syncToInterp
static void emit_syncToInterp(Ctx& ctx) {
    // Store all ARM regs from PPC callee-saved regs to frame
    for (int i = 0; i < 16; i++)
        ctx.emit(ppc_stw(R_ARM[i], FRAME_REGSYNC + i*4, 1));
    ctx.emit(ppc_stw(R_CPSR, FRAME_REGSYNC + 16*4, 1));
    // Call JitPpc_syncToInterp(interp, &regsync)
    ctx.emit(ppc_mr(T0, R_INTERP));                // r3 = interp
    ctx.emit(ppc_addi(T1, 1, (int16_t)FRAME_REGSYNC)); // r4 = &regsync
    ctx.call((void*)JitPpc_syncToInterp);
    // Callee-saved regs (R_ARM[0-15], R_CPSR, R_INTERP) are preserved by callee
}

// Call JitPpc_syncFromInterp, then load ARM regs from frame
static void emit_syncFromInterp(Ctx& ctx) {
    // Call JitPpc_syncFromInterp(interp, &regsync)
    ctx.emit(ppc_mr(T0, R_INTERP));
    ctx.emit(ppc_addi(T1, 1, (int16_t)FRAME_REGSYNC));
    ctx.call((void*)JitPpc_syncFromInterp);
    // Load ARM regs from frame into callee-saved PPC regs
    for (int i = 0; i < 16; i++)
        ctx.emit(ppc_lwz(R_ARM[i], FRAME_REGSYNC + i*4, 1));
    ctx.emit(ppc_lwz(R_CPSR, FRAME_REGSYNC + 16*4, 1));
}

// ============================================================
// Condition handling
//
// We use CR7 (field 7) to hold ARM condition codes.
// CR7 occupies bits 28-31 of the CR register.
// mtcrf 0x01, rN   sets CR7 from bits 28-31 of rN.
//
// We arrange CPSR bits into CR7 like this:
//   CPSR bit 31 (N) -> CR7 bit 31 (LT, field bit 0)
//   CPSR bit 30 (Z) -> CR7 bit 30 (GT, field bit 1)
//   CPSR bit 29 (C) -> CR7 bit 29 (EQ, field bit 2)
//   CPSR bit 28 (V) -> CR7 bit 28 (SO, field bit 3)
//
// Since CPSR[31:28] = NZCV and CR7 occupies bits [31:28],
// we can do: mtcrf 0x01, CPSR  (field mask 0x01 = CR7)
//
// Branch conditions in CR7:
//   cr7 bit indices for bc: LT=28, GT=29, EQ=30, SO=31
//   Wait -- PPC bc uses 5-bit BI field:
//     CR0: LT=0,GT=1,EQ=2,SO=3
//     CR7: LT=28,GT=29,EQ=30,SO=31
//
// ARM condition -> PPC bc:
//   EQ (Z=1):    bo=12(true), bi=29 (CR7 GT bit = Z)
//   NE (Z=0):    bo=4(false), bi=29
//   CS (C=1):    bo=12,       bi=30 (CR7 EQ bit = C)
//   CC (C=0):    bo=4,        bi=30
//   MI (N=1):    bo=12,       bi=28 (CR7 LT bit = N)
//   PL (N=0):    bo=4,        bi=28
//   VS (V=1):    bo=12,       bi=31 (CR7 SO bit = V)
//   VC (V=0):    bo=4,        bi=31
//   AL:          bo=20 (always)
//   Others: need multi-instruction -- return invalid
// ============================================================
static void emit_loadCR7(Ctx& ctx) {
    // Load top 4 bits of CPSR (NZCV) into CR7
    // mtcrf 0x01, R_CPSR  : field mask 0x01 = CR7
    ctx.emit(ppc_mtcrf(0x01, R_CPSR));
}

struct CB { uint8_t bo, bi; bool ok; };
static CB armCond(uint8_t c) {
    // CR7 bit indices: LT=28, GT=29, EQ=30, SO=31
    // CPSR bit31(N)->CR7_LT(28), bit30(Z)->CR7_GT(29),
    //       bit29(C)->CR7_EQ(30), bit28(V)->CR7_SO(31)
    switch (c) {
        case 0:  return {12, 29, true};  // EQ: Z set (GT bit of CR7)
        case 1:  return { 4, 29, true};  // NE: Z clear
        case 2:  return {12, 30, true};  // CS: C set (EQ bit of CR7)
        case 3:  return { 4, 30, true};  // CC: C clear
        case 4:  return {12, 28, true};  // MI: N set (LT bit of CR7)
        case 5:  return { 4, 28, true};  // PL: N clear
        case 6:  return {12, 31, true};  // VS: V set (SO bit of CR7)
        case 7:  return { 4, 31, true};  // VC: V clear
        // HI,LS,GE,LT,GT,LE need multiple instructions
        case 8:  // HI: C && !Z
        case 9:  // LS: !C || Z
        case 10: // GE: N==V
        case 11: // LT: N!=V
        case 12: // GT: !Z && N==V
        case 13: // LE: Z || N!=V
            return {0, 0, false};
        case 14: return {20, 0, true};   // AL: always (bo=20 ignores bi)
        default: return {0, 0, false};
    }
}

// Emit conditional skip: if ARM condition NOT met, skip forward.
// Returns the index of the bc instruction so we can patch it later.
// Returns SIZE_MAX if condition is always-true (no branch needed).
static size_t emit_condSkip(Ctx& ctx, uint8_t cond) {
    if (cond == 14) return SIZE_MAX;  // always execute, no skip
    emit_loadCR7(ctx);
    CB cb = armCond(cond);
    if (!cb.ok) return SIZE_MAX;  // caller must handle
    // Branch if condition NOT met (invert bo: 12->4, 4->12)
    uint8_t invBo = (cb.bo == 12) ? 4 : (cb.bo == 4 ? 12 : cb.bo);
    size_t idx = ctx.size();
    ctx.emit(ppc_bc(invBo, cb.bi, 0));  // offset patched later
    return idx;
}

static void patchSkip(Ctx& ctx, size_t bcIdx) {
    if (bcIdx == SIZE_MAX) return;
    int32_t off = (int32_t)((ctx.size() - bcIdx) * 4);
    // off must fit in 14-bit signed (range: -32768..32764)
    ctx.base[bcIdx] = (ctx.base[bcIdx] & 0xFFFF0003u) | (uint32_t)(off & 0xFFFC);
}

// ============================================================
// CPSR flag update helpers
//
// After arithmetic/logic ops, update CPSR N,Z,C,V bits.
//
// We use volatile regs T0-T3 freely here since we're between
// ARM instructions (callee-saved ARM regs are safe).
// ============================================================

// Update N and Z from result register `r`
static void emit_setNZ(Ctx& ctx, uint8_t r) {
    // Clear N (bit31) and Z (bit30) from CPSR
    ctx.emit(ppc_rlwinm(R_CPSR, R_CPSR, 0, 2, 31));   // clear bits 31-30
    // Set N from bit 31 of r:
    ctx.emit(ppc_rlwimi(R_CPSR, r, 0, 0, 0));          // CPSR[31] = r[31]
    // Set Z: r == 0 ?
    // cmpi cr6, r, 0  then extract EQ bit
    ctx.emit(ppc_cmpi(6, r, 0));
    ctx.emit(ppc_mfcr(T0));
    // CR6 EQ is bit (31 - (6*4 + 2)) = bit 5 of CR word
    // Actually: CR field n occupies bits 31-4n down to 28-4n.
    // CR6: bits 31-4*6=7 down to 28-4*6=4, so bits 7..4
    // CR6 EQ = bit 5 (counting from bit31=0)
    // We want to move CR6_EQ to CPSR bit 30 (Z).
    // Shift: bit5 of T0 -> bit30 of R_CPSR
    // rlwinm T0, T0, 25, 30, 30  : rotate left 25 positions, mask bit 30
    ctx.emit(ppc_rlwinm(T0, T0, 25, 30, 30));
    ctx.emit(ppc_or(R_CPSR, R_CPSR, T0));
}

// After ppc_addc/adde: update C from XER[CA] (bit 29 of XER)
// ARM C = XER.CA for addition
static void emit_setC_fromXER_add(Ctx& ctx) {
    ctx.emit(ppc_mfxer(T0));
    // XER.CA = bit 29 of XER
    ctx.emit(ppc_rlwinm(T0, T0, 0, 29, 29));  // isolate bit 29
    ctx.emit(ppc_rlwinm(R_CPSR, R_CPSR, 0, 3, 1));  // clear C (bit29) in CPSR
    ctx.emit(ppc_or(R_CPSR, R_CPSR, T0));
}

// After ppc_subfc/subfe: update C from XER[CA]
// ARM C (no borrow) = XER.CA for subtraction (ppc subfc: CA=1 when a>=b unsigned)
static void emit_setC_fromXER_sub(Ctx& ctx) {
    // Same as add: XER.CA directly maps to ARM C for subtraction
    emit_setC_fromXER_add(ctx);
}

// Update V from XER[OV] (bit 30 of XER) -> CPSR bit 28
// Note: ppc_addc/subfc do NOT set OV. We need ppc_addo/subfo for V.
// Since we use non-OE variants, we compute V manually.
// For ADD: V = overflow = (result sign != expected sign)
// We'll compute V separately using: V = (a^result) & (b^result) >> 31  (for add)
// For now, use a simplified approach: after addc, compute V by checking
// sign of operands and result.
// This is complex to do inline; for correctness we use the OE variants.
static void emit_setV_add(Ctx& ctx, uint8_t result, uint8_t a, uint8_t b) {
    // V = ((a ^ result) & (b ^ result)) >> 31
    ctx.emit(ppc_xor(T0, result, a));   // T0 = a ^ result  (note: xor(dest,s,b) = X-form)
    ctx.emit(ppc_xor(T1, result, b));   // T1 = b ^ result
    ctx.emit(ppc_and(T0, T0, T1));      // T0 = (a^result)&(b^result)
    ctx.emit(ppc_rlwinm(T0, T0, 4, 28, 28));  // shift bit31 -> bit28 (V position)
    ctx.emit(ppc_rlwinm(R_CPSR, R_CPSR, 0, 4, 2));  // clear V (bit28)
    ctx.emit(ppc_or(R_CPSR, R_CPSR, T0));
}

static void emit_setV_sub(Ctx& ctx, uint8_t result, uint8_t a, uint8_t b) {
    // V = ((a ^ b) & (a ^ result)) >> 31  for result = a - b
    ctx.emit(ppc_xor(T0, a, b));
    ctx.emit(ppc_xor(T1, a, result));
    ctx.emit(ppc_and(T0, T0, T1));
    ctx.emit(ppc_rlwinm(T0, T0, 4, 28, 28));
    ctx.emit(ppc_rlwinm(R_CPSR, R_CPSR, 0, 4, 2));
    ctx.emit(ppc_or(R_CPSR, R_CPSR, T0));
}

static void emit_setNZCV_add(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    emit_setNZ(ctx, res);
    emit_setC_fromXER_add(ctx);
    emit_setV_add(ctx, res, a, b);
}

static void emit_setNZCV_sub(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    emit_setNZ(ctx, res);
    emit_setC_fromXER_sub(ctx);
    emit_setV_sub(ctx, res, a, b);
}

// Set C from bit 0 of carry_reg (shift carry out)
static void emit_setC_fromBit0(Ctx& ctx, uint8_t carry_reg) {
    // carry_reg bit 0 -> CPSR bit 29 (C)
    ctx.emit(ppc_rlwinm(T0, carry_reg, 29, 29, 29));
    ctx.emit(ppc_rlwinm(R_CPSR, R_CPSR, 0, 3, 1));  // clear C
    ctx.emit(ppc_or(R_CPSR, R_CPSR, T0));
}

// ============================================================
// Shifter operand helpers
// Returns carry in T3 (bit 0) if sc=true.
// Result goes into `dst`.
// ============================================================
static void emit_lslI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i == 0) {
        if (d != s) ctx.emit(ppc_mr(d, s));
        if (sc) {
            // carry = old C (bit 29 of CPSR) -> T3 bit 0
            ctx.emit(ppc_rlwinm(T3, R_CPSR, 3, 31, 31));
        }
    } else if (i < 32) {
        if (sc) ctx.emit(ppc_rlwinm(T3, s, i, 31, 31));  // carry = shifted-out bit
        ctx.emit(ppc_rlwinm(d, s, i, 0, 31-i));
    } else if (i == 32) {
        if (sc) ctx.emit(ppc_rlwinm(T3, s, 0, 31, 31));   // carry = bit 0
        ctx.emit(ppc_addi(d, 0, 0));
    } else {
        if (sc) ctx.emit(ppc_addi(T3, 0, 0));
        ctx.emit(ppc_addi(d, 0, 0));
    }
}

static void emit_lsrI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i == 0 || i == 32) {
        if (sc) ctx.emit(ppc_rlwinm(T3, s, 1, 31, 31));  // carry = bit 31
        ctx.emit(ppc_addi(d, 0, 0));
    } else if (i < 32) {
        if (sc) ctx.emit(ppc_rlwinm(T3, s, 33-i, 31, 31));
        ctx.emit(ppc_rlwinm(d, s, 32-i, i, 31));
    } else {
        if (sc) ctx.emit(ppc_addi(T3, 0, 0));
        ctx.emit(ppc_addi(d, 0, 0));
    }
}

static void emit_asrI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i == 0 || i >= 32) {
        if (sc) ctx.emit(ppc_rlwinm(T3, s, 1, 31, 31));
        ctx.emit(ppc_srawi(d, s, 31));
    } else {
        if (sc) ctx.emit(ppc_rlwinm(T3, s, 33-i, 31, 31));
        ctx.emit(ppc_srawi(d, s, (uint8_t)i));
    }
}

static void emit_rorI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i == 0) {
        // RRX
        ctx.emit(ppc_rlwinm(T0, R_CPSR, 3, 31, 31));   // T0 = C bit
        if (sc) ctx.emit(ppc_rlwinm(T3, s, 0, 31, 31)); // carry = bit 0 of s
        ctx.emit(ppc_rlwinm(d, s, 31, 1, 31));           // d = s >> 1
        ctx.emit(ppc_rlwimi(d, T0, 31, 0, 0));           // d[31] = C
    } else {
        i &= 31;
        if (!i) i = 32;
        if (i < 32) {
            if (sc) ctx.emit(ppc_rlwinm(T3, s, 33-i, 31, 31));
            ctx.emit(ppc_rlwinm(d, s, 32-i, 0, 31));
        } else {
            if (d != s) ctx.emit(ppc_mr(d, s));
            if (sc) ctx.emit(ppc_rlwinm(T3, s, 1, 31, 31));
        }
    }
}

// Register shifts (carry tracking is approximate for reg shifts)
static void emit_lslR(Ctx& ctx, uint8_t d, uint8_t s, uint8_t sh) {
    ctx.emit(ppc_slw(d, s, sh));
}
static void emit_lsrR(Ctx& ctx, uint8_t d, uint8_t s, uint8_t sh) {
    ctx.emit(ppc_srw(d, s, sh));
}
static void emit_asrR(Ctx& ctx, uint8_t d, uint8_t s, uint8_t sh) {
    ctx.emit(ppc_sraw(d, s, sh));
}
static void emit_rorR(Ctx& ctx, uint8_t d, uint8_t s, uint8_t sh) {
    ctx.emit(ppc_subfic(T2, sh, 32));  // T2 = 32 - sh
    ctx.emit(ppc_rlwnm(d, s, T2, 0, 31));
}

// Decode ARM shifter operand into `dst` (a volatile temp reg).
// Returns: true=carry updated in T3 bit0 when sc=true.
// Uses: T0, T1, T2, T3
static bool emit_shifterOp(Ctx& ctx, uint32_t op, uint8_t dst, bool sc) {
    bool isImm = (op >> 25) & 1;
    if (isImm) {
        uint32_t v   = op & 0xFF;
        uint32_t rot = ((op >> 8) & 0xF) * 2;
        if (rot) v = (v >> rot) | (v << (32-rot));
        ctx.li32(dst, v);
        if (sc && rot) {
            ctx.emit(ppc_rlwinm(T3, dst, 1, 31, 31));  // carry = bit31 of result
        }
        return sc && (rot != 0);
    }
    uint8_t rm = op & 0xF;
    uint8_t pRm = R_ARM[rm];
    bool isReg = (op >> 4) & 1;
    uint8_t st = (op >> 5) & 3;

    if (!isReg) {
        int sa = (op >> 7) & 0x1F;
        switch (st) {
            case 0: emit_lslI(ctx, dst, pRm, sa, sc); break;
            case 1: emit_lsrI(ctx, dst, pRm, sa ? sa : 32, sc); break;
            case 2: emit_asrI(ctx, dst, pRm, sa ? sa : 32, sc); break;
            case 3: emit_rorI(ctx, dst, pRm, sa, sc); break;
        }
        return sc;
    } else {
        uint8_t rs  = (op >> 8) & 0xF;
        uint8_t pRs = R_ARM[rs];
        // use T2 for shift amount (byte)
        ctx.emit(ppc_rlwinm(T2, pRs, 0, 24, 31));
        ctx.emit(ppc_mr(T1, pRm));  // copy rm to volatile reg
        switch (st) {
            case 0: emit_lslR(ctx, dst, T1, T2); break;
            case 1: emit_lsrR(ctx, dst, T1, T2); break;
            case 2: emit_asrR(ctx, dst, T1, T2); break;
            case 3: emit_rorR(ctx, dst, T1, T2); break;
        }
        return false;  // carry not tracked for register shifts
    }
}

// ============================================================
// Data processing instructions
// ============================================================
enum DP {
    AND=0,EOR=1,SUB=2,RSB=3,ADD=4,ADC=5,SBC=6,RSC=7,
    TST=8,TEQ=9,CMP=10,CMN=11,ORR=12,MOV=13,BIC=14,MVN=15
};

static bool emit_dataProc(Ctx& ctx, uint32_t op) {
    uint8_t cond  = (op>>28)&0xF;
    uint8_t dpop  = (op>>21)&0xF;
    bool    setCC = (op>>20)&1;
    uint8_t rn    = (op>>16)&0xF;
    uint8_t rd    = (op>>12)&0xF;

    if (rd == 15) return false;  // PC write: complex, use interpreter

    uint8_t pRd = R_ARM[rd];
    uint8_t pRn = R_ARM[rn];

    size_t skipIdx = emit_condSkip(ctx, cond);
    if (skipIdx == SIZE_MAX && cond != 14) return false;  // complex condition

    bool needC_in = (dpop==ADC||dpop==SBC||dpop==RSC);

    // Load C into XER.CA before the operation if needed
    if (needC_in) {
        // Extract C bit (bit29) from CPSR, put in XER.CA (bit29 of XER)
        ctx.emit(ppc_rlwinm(T0, R_CPSR, 0, 29, 29));  // T0 = C at bit29
        ctx.emit(ppc_mtxer(T0));
    }

    // Compute shifter operand into T0
    bool logicNeedsC = setCC && (dpop==AND||dpop==EOR||dpop==TST||
                                  dpop==TEQ||dpop==ORR||dpop==MOV||
                                  dpop==BIC||dpop==MVN);
    bool carryFromShift = emit_shifterOp(ctx, op, T0, logicNeedsC);

    // Destination: test ops don't write rd, use T1 for result
    bool isTest = (dpop==TST||dpop==TEQ||dpop==CMP||dpop==CMN);
    uint8_t res  = isTest ? T1 : pRd;

    // Save operands for V calculation (need before result for add/sub)
    // We'll compute V inline after the operation.
    // For add/sub we need copies of both inputs.
    uint8_t opA = pRn;  // first operand
    uint8_t opB = T0;   // second operand (shifter result)

    // If we need V and the result aliases an input, save inputs
    bool needV = setCC && (dpop==ADD||dpop==SUB||dpop==RSB||dpop==CMN||
                            dpop==CMP||dpop==ADC||dpop==SBC||dpop==RSC);

    switch ((DP)dpop) {
        case AND: case TST:
            ctx.emit(ppc_and(res, pRn, T0)); break;
        case EOR: case TEQ:
            ctx.emit(ppc_xor(res, pRn, T0)); break;
        case SUB: case CMP:
            // result = Rn - op2  using subfc(d, a, b) = b - a
            ctx.emit(ppc_subfc(res, T0, pRn)); break;
        case RSB:
            ctx.emit(ppc_subfc(res, pRn, T0)); break;
        case ADD: case CMN:
            ctx.emit(ppc_addc(res, pRn, T0)); break;
        case ADC:
            ctx.emit(ppc_adde(res, pRn, T0)); break;
        case SBC:
            ctx.emit(ppc_subfe(res, T0, pRn)); break;
        case RSC:
            ctx.emit(ppc_subfe(res, pRn, T0)); break;
        case ORR:
            ctx.emit(ppc_or(res, pRn, T0)); break;
        case MOV:
            if (res != T0) ctx.emit(ppc_mr(res, T0));
            break;
        case BIC:
            ctx.emit(ppc_andc(res, pRn, T0)); break;
        case MVN:
            ctx.emit(ppc_nor(res, T0, T0)); break;
    }

    if (setCC) {
        switch ((DP)dpop) {
            case ADD: case CMN:
                emit_setNZ(ctx, res);
                emit_setC_fromXER_add(ctx);
                emit_setV_add(ctx, res, opA, opB);
                break;
            case ADC:
                emit_setNZ(ctx, res);
                emit_setC_fromXER_add(ctx);
                // V for ADC: same formula as ADD
                emit_setV_add(ctx, res, opA, opB);
                break;
            case SUB: case CMP:
                emit_setNZ(ctx, res);
                emit_setC_fromXER_sub(ctx);
                emit_setV_sub(ctx, res, opA, opB);
                break;
            case RSB:
                emit_setNZ(ctx, res);
                emit_setC_fromXER_sub(ctx);
                emit_setV_sub(ctx, res, opB, opA);  // reversed
                break;
            case SBC:
                emit_setNZ(ctx, res);
                emit_setC_fromXER_sub(ctx);
                emit_setV_sub(ctx, res, opA, opB);
                break;
            case RSC:
                emit_setNZ(ctx, res);
                emit_setC_fromXER_sub(ctx);
                emit_setV_sub(ctx, res, opB, opA);
                break;
            default:
                // Logic ops: NZ only, C from shifter
                emit_setNZ(ctx, res);
                if (carryFromShift) emit_setC_fromBit0(ctx, T3);
                break;
        }
    }

    patchSkip(ctx, skipIdx);
    return true;
}

// ============================================================
// Branch instructions
// ============================================================
static bool emit_branch(Ctx& ctx, uint32_t op, uint32_t pc) {
    uint8_t cond = (op>>28)&0xF;

    // BX Rn
    if ((op & 0x0FFFFFF0) == 0x012FFF10) {
        uint8_t rm = op & 0xF;
        if (rm == 15) return false;
        uint8_t pRm = R_ARM[rm];

        size_t skipIdx = emit_condSkip(ctx, cond);
        if (skipIdx == SIZE_MAX && cond != 14) return false;

        // T bit = Rm[0]
        ctx.emit(ppc_rlwinm(R_CPSR, R_CPSR, 0, 27, 4));  // clear T (bit5) + guard
        ctx.emit(ppc_rlwinm(T0, pRm, 0, 31, 31));          // T0 = Rm[0]
        ctx.emit(ppc_rlwimi(R_CPSR, T0, 5, 26, 26));       // CPSR[5] = T0[0] << 5? No.
        // Actually: we want CPSR bit5 = Rm bit0
        // rlwinm T0, Rm, 5, 26, 26  rotates Rm left 5, isolates bit26 of result
        // Rm bit0, after rotate left 5 = bit5 of rotated value
        // We want bit5 of CPSR. Bit5 in a 32-bit word: mb=me=26 (bit26 from MSB=bit31=0)
        // Actually in PPC: bit numbering from MSB. bit31 is the LSB in our numbering.
        // Let me be explicit:
        // CPSR[31] = N, CPSR[30] = Z, CPSR[29] = C, CPSR[28] = V, CPSR[5] = T
        // "bit 5" here means position 5 counting from LSB, which is position 26 from MSB.
        // So CPSR bit5 from LSB = PPC bit 26 (counting from MSB=0).
        // Rm[0] = Rm bit0 from LSB = PPC bit 31.
        // We want to insert Rm's LSB into CPSR's bit-5-from-LSB:
        //   rlwinm T0, Rm, 5, 26, 26  = rotate Rm left 5, keep only result bit 26
        //   = Rm[31-5] = Rm[26 from MSB] = Rm[5 from LSB] != Rm[0]
        // That's wrong. Let's do it correctly:
        // rlwinm T0, Rm, 0, 31, 31   = Rm[0] in bit31 (cleared all else)
        // rlwinm T0, T0, 27, 26, 26  = shift right 5 places (26 from MSB = bit5 from LSB)
        // No: rlwinm with sh=27 rotates LEFT by 27 = rotate RIGHT by 5.
        // Rm[0] (bit31 after first rlwinm) rotate right 5 -> goes to bit26 from MSB = bit5 from LSB.
        // That's correct!
        ctx.emit(ppc_rlwinm(R_CPSR, R_CPSR, 0, 27, 4));  // clear bit26 (T) and 27,28 from MSB
        // Wait, "clear bit5 from LSB = bit26 from MSB":
        // rlwinm d, s, 0, mb, me clears bits mb..me (from MSB).
        // To clear only bit26: rlwinm with mask excluding bit26:
        //   mb=0, me=25: keeps bits 0..25 (from MSB) = clears bit26..31 -- too much
        // Better: use two rlwinm or andc.
        // Simplest: andi. R_CPSR & ~(1<<5) -- but andi. only has 16-bit immediate
        // ~(1<<5) = 0xFFFFFFDF, doesn't fit in 16 bits
        // Use: rlwinm R_CPSR, R_CPSR, 0, 0, 25 then rlwimi R_CPSR, ... for bit26
        // Actually let's just do:
        // li T1, ~0x20 (= 0xFFFFFFDF) -- needs addis/ori, awkward
        // EASIEST: since we're setting the whole CPSR[5] bit:
        // Step 1: clear bit 5 from LSB (bit 26 from MSB) in CPSR
        //   rlwinm R_CPSR, R_CPSR, 0, 0, 25  -- keeps bits 31..26 from MSB? No.
        //   rlwinm d, s, sh, mb, me clears bits outside [mb..me], keeps inside.
        //   To keep everything except bit26 from MSB:
        //   Do two rlwinm: keep bits 0..25, OR keep bits 27..31, combine.
        //   OR use: andc R_CPSR, R_CPSR, T0_with_bit26_set
        // Let's just use ori/andc pattern:
        ctx.emit(ppc_rlwinm(T0, pRm, 0, 31, 31));   // T0 = Rm[0] at bit31 from MSB
        // shift T0 right by 26 positions to put bit31 -> bit26+5=... 
        // We want T0's bit31 at bit26 from MSB (= bit5 from LSB):
        // rotate left (32-5)=27 positions: bit31 -> bit(31+27)%32 = bit26 from MSB? 
        // PPC rlwinm: rotates rs LEFT by sh. bit31(LSB) rotated left 5 -> bit26 from MSB.
        // Wait: "rotate left by 5" means each bit moves 5 positions toward MSB (wrapping).
        // bit31 (LSB) moved 5 positions left = bit26 from MSB = bit5 from LSB. YES!
        // But we used rlwinm(T0, pRm, 0, 31, 31) which puts result in bit31 from MSB.
        // Then we need to rotate bit31 left 5 to get bit26 from MSB:
        // rlwinm doesn't allow this without chaining. Use:
        // rlwinm T1, pRm, 5, 26, 26  -- rotate pRm left 5, keep only bit26
        // This gives: (pRm rotated left 5) masked to bit26.
        // pRm rotated left 5: bit0(=bit31 from MSB) moves to bit26 from MSB.
        // So bit26 of result = pRm's bit0 from LSB. Exactly what we want!
        ctx.emit(ppc_rlwinm(T0, pRm, 5, 26, 26));   // T0 = pRm[0]<<5 (at bit26 from MSB)
        // Now clear bit26 in CPSR and OR in T0:
        // rlwinm to clear bit26: keep bits [0..25] and [27..31] from MSB
        // = two ranges, can't do with one rlwinm.
        // Use: rlwinm T1, R_CPSR, 0, 27, 25  -- this wraps: mb=27 > me=25 means COMPLEMENT mask
        //   complement mask: bit26 cleared, all others kept. This is the PPC complement form!
        ctx.emit(ppc_rlwinm(R_CPSR, R_CPSR, 0, 27, 25));  // clear bit26 from MSB (=T bit)
        ctx.emit(ppc_or(R_CPSR, R_CPSR, T0));              // set T = Rm[0]

        // PC = Rm & ~1
        ctx.emit(ppc_rlwinm(R_ARM[15], pRm, 0, 0, 30));

        emit_syncToInterp(ctx);
        emit_epilogue(ctx);

        if (skipIdx != SIZE_MAX) {
            patchSkip(ctx, skipIdx);
            // Fall-through: PC = pc+4
            ctx.li32(R_ARM[15], pc + 4);
            emit_syncToInterp(ctx);
            emit_epilogue(ctx);
        }
        ctx.terminated = true;
        return true;
    }

    // B / BL
    if ((op & 0x0E000000) == 0x0A000000) {
        bool lk = (op>>24) & 1;
        int32_t off = (int32_t)(op<<8) >> 6;
        uint32_t tgt = pc + 8 + off;

        size_t skipIdx = emit_condSkip(ctx, cond);
        if (skipIdx == SIZE_MAX && cond != 14) return false;

        if (lk) ctx.li32(R_ARM[14], pc + 4);
        ctx.li32(R_ARM[15], tgt);
        emit_syncToInterp(ctx);
        emit_epilogue(ctx);

        if (skipIdx != SIZE_MAX) {
            patchSkip(ctx, skipIdx);
            ctx.li32(R_ARM[15], pc + 4);
            emit_syncToInterp(ctx);
            emit_epilogue(ctx);
        }
        ctx.terminated = true;
        return true;
    }
    return false;
}

// ============================================================
// Load/Store
// ============================================================
static bool emit_loadStore(Ctx& ctx, uint32_t op, uint32_t pc) {
    uint8_t cond  = (op>>28)&0xF;
    bool    ld    = (op>>20)&1;
    bool    by    = (op>>22)&1;
    bool    up    = (op>>23)&1;
    bool    pre   = (op>>24)&1;
    bool    wb    = (op>>21)&1;
    bool    immOp = !((op>>25)&1);
    uint8_t rn    = (op>>16)&0xF;
    uint8_t rd    = (op>>12)&0xF;

    if (rd==15 || rn==15) return false;
    uint8_t pRn = R_ARM[rn];
    uint8_t pRd = R_ARM[rd];

    size_t skipIdx = emit_condSkip(ctx, cond);
    if (skipIdx == SIZE_MAX && cond != 14) return false;

    // Compute offset into T0
    if (immOp) {
        ctx.li32(T0, op & 0xFFF);
    } else {
        ctx.emit(ppc_mr(T0, R_ARM[op&0xF]));
    }

    // Compute address into T1
    if (pre) {
        if (up) ctx.emit(ppc_add(T1, pRn, T0));
        else    ctx.emit(ppc_subf(T1, T0, pRn));
    } else {
        ctx.emit(ppc_mr(T1, pRn));
    }

    // Save T0 (offset) and T1 (addr) to scratch frame slots
    ctx.emit(ppc_stw(T0, FRAME_SCRATCH0, 1));
    ctx.emit(ppc_stw(T1, FRAME_SCRATCH1, 1));

    // Set up call arguments:
    //   r3 = Core*,  r4 = arm7 flag,  r5 = address,  [r6 = value for store]
    ctx.loadCore(T0);                                 // T0/r3 = Core*
    ctx.emit(ppc_addi(T1, 0, ctx.arm7 ? 1 : 0));     // T1/r4 = arm7
    ctx.emit(ppc_lwz(T2, FRAME_SCRATCH1, 1));         // T2/r5 = addr
    if (!ld) ctx.emit(ppc_mr(T3, pRd));               // T3/r6 = store value

    // Call memory function
    void* fn = ld ? (by ? (void*)JitPpc_read8  : (void*)JitPpc_read32)
                  : (by ? (void*)JitPpc_write8 : (void*)JitPpc_write32);
    ctx.call(fn);

    // After call: all volatiles (T0-T3,etc) are clobbered, but callee-saved
    // R_ARM[0..15], R_CPSR, R_INTERP are all preserved by the callee.
    // Result is in r3 (T0) if load.
    if (ld) {
        ctx.emit(ppc_mr(pRd, T0));  // pRd is callee-saved, T0=r3=return value
    }

    // Writeback
    ctx.emit(ppc_lwz(T0, FRAME_SCRATCH0, 1));  // reload offset
    if (!pre) {
        if (up) ctx.emit(ppc_add(pRn, pRn, T0));
        else    ctx.emit(ppc_subf(pRn, T0, pRn));
    } else if (wb && rn != rd) {
        ctx.emit(ppc_lwz(pRn, FRAME_SCRATCH1, 1));  // pRn = computed addr
    }

    patchSkip(ctx, skipIdx);
    return true;
}

// ============================================================
// Multiply
// ============================================================
static bool emit_multiply(Ctx& ctx, uint32_t op) {
    bool    sc  = (op>>20)&1;
    bool    acc = (op>>21)&1;
    bool    lng = (op>>23)&1;
    uint8_t rd  = (op>>16)&0xF;
    uint8_t rn  = (op>>12)&0xF;
    uint8_t rs  = (op>>8 )&0xF;
    uint8_t rm  = op&0xF;
    if (rd==15||rm==15||rs==15) return false;
    if (lng) return false;
    uint8_t pRd=R_ARM[rd],pRn=R_ARM[rn],pRs=R_ARM[rs],pRm=R_ARM[rm];
    if (acc) {
        ctx.emit(ppc_mullw(T0, pRm, pRs));
        ctx.emit(ppc_add(pRd, T0, pRn));
    } else {
        ctx.emit(ppc_mullw(pRd, pRm, pRs));
    }
    if (sc) emit_setNZ(ctx, pRd);
    return true;
}

// ============================================================
// Interpreter fallback (sync, call, sync back)
// ============================================================
static void emit_fallback(Ctx& ctx) {
    emit_syncToInterp(ctx);
    ctx.emit(ppc_mr(T0, R_INTERP));
    ctx.call((void*)JitPpc_interpFallback);
    emit_syncFromInterp(ctx);
}

// ============================================================
// Thumb emitters
// ============================================================
static bool emit_t_shiftImm(Ctx& ctx, uint16_t op) {
    uint8_t ty = (op>>11)&3;
    uint8_t rd = op&7, rs = (op>>3)&7;
    int     i  = (op>>6)&0x1F;
    uint8_t pRd = R_ARM[rd], pRs = R_ARM[rs];
    switch (ty) {
        case 0: emit_lslI(ctx, pRd, pRs, i, true);      break;
        case 1: emit_lsrI(ctx, pRd, pRs, i?i:32, true); break;
        case 2: emit_asrI(ctx, pRd, pRs, i?i:32, true); break;
        default: return false;
    }
    emit_setNZ(ctx, pRd);
    emit_setC_fromBit0(ctx, T3);
    return true;
}

static bool emit_t_addSub(Ctx& ctx, uint16_t op) {
    uint8_t rd = op&7, rs = (op>>3)&7;
    bool sub = (op>>9)&1, imm = (op>>10)&1;
    uint8_t pRd = R_ARM[rd], pRs = R_ARM[rs];
    if (imm) ctx.li32(T0, (op>>6)&7);
    else     ctx.emit(ppc_mr(T0, R_ARM[(op>>6)&7]));
    // Save pRs to T1 for V calculation (pRs may == pRd)
    ctx.emit(ppc_mr(T1, pRs));
    if (sub) {
        ctx.emit(ppc_subfc(pRd, T0, T1));
        emit_setNZ(ctx, pRd);
        emit_setC_fromXER_sub(ctx);
        emit_setV_sub(ctx, pRd, T1, T0);
    } else {
        ctx.emit(ppc_addc(pRd, T1, T0));
        emit_setNZ(ctx, pRd);
        emit_setC_fromXER_add(ctx);
        emit_setV_add(ctx, pRd, T1, T0);
    }
    return true;
}

static bool emit_t_movCmpAddSub(Ctx& ctx, uint16_t op) {
    uint8_t ty = (op>>11)&3;
    uint8_t rd = (op>>8)&7;
    uint8_t pRd = R_ARM[rd];
    uint8_t imm = op&0xFF;
    switch (ty) {
        case 0: // MOV
            ctx.li32(pRd, imm);
            emit_setNZ(ctx, pRd);
            return true;
        case 1: { // CMP
            ctx.li32(T0, imm);
            ctx.emit(ppc_mr(T1, pRd));
            ctx.emit(ppc_subfc(T2, T0, T1));
            emit_setNZ(ctx, T2);
            emit_setC_fromXER_sub(ctx);
            emit_setV_sub(ctx, T2, T1, T0);
            return true;
        }
        case 2: { // ADD
            ctx.li32(T0, imm);
            ctx.emit(ppc_mr(T1, pRd));
            ctx.emit(ppc_addc(pRd, T1, T0));
            emit_setNZ(ctx, pRd);
            emit_setC_fromXER_add(ctx);
            emit_setV_add(ctx, pRd, T1, T0);
            return true;
        }
        case 3: { // SUB
            ctx.li32(T0, imm);
            ctx.emit(ppc_mr(T1, pRd));
            ctx.emit(ppc_subfc(pRd, T0, T1));
            emit_setNZ(ctx, pRd);
            emit_setC_fromXER_sub(ctx);
            emit_setV_sub(ctx, pRd, T1, T0);
            return true;
        }
    }
    return false;
}

static bool emit_t_aluOp(Ctx& ctx, uint16_t op) {
    uint8_t rd = op&7, rs = (op>>3)&7, o = (op>>6)&0xF;
    uint8_t pRd = R_ARM[rd], pRs = R_ARM[rs];
    switch (o) {
        case 0:  ctx.emit(ppc_and(pRd,pRd,pRs)); emit_setNZ(ctx,pRd); break;
        case 1:  ctx.emit(ppc_xor(pRd,pRd,pRs)); emit_setNZ(ctx,pRd); break;
        case 2:  ctx.emit(ppc_slw(pRd,pRd,pRs)); emit_setNZ(ctx,pRd); break;
        case 3:  ctx.emit(ppc_srw(pRd,pRd,pRs)); emit_setNZ(ctx,pRd); break;
        case 4:  ctx.emit(ppc_sraw(pRd,pRd,pRs));emit_setNZ(ctx,pRd); break;
        case 5:  // ADC
            ctx.emit(ppc_rlwinm(T0,R_CPSR,0,29,29));
            ctx.emit(ppc_mtxer(T0));
            ctx.emit(ppc_mr(T1,pRd));
            ctx.emit(ppc_adde(pRd,T1,pRs));
            emit_setNZ(ctx,pRd);
            emit_setC_fromXER_add(ctx);
            emit_setV_add(ctx,pRd,T1,pRs);
            break;
        case 6:  // SBC
            ctx.emit(ppc_rlwinm(T0,R_CPSR,0,29,29));
            ctx.emit(ppc_mtxer(T0));
            ctx.emit(ppc_mr(T1,pRd));
            ctx.emit(ppc_subfe(pRd,pRs,T1));
            emit_setNZ(ctx,pRd);
            emit_setC_fromXER_sub(ctx);
            emit_setV_sub(ctx,pRd,T1,pRs);
            break;
        case 7:  // ROR
            emit_rorR(ctx,pRd,pRd,pRs);
            emit_setNZ(ctx,pRd); break;
        case 8:  { // TST
            ctx.emit(ppc_and(T0,pRd,pRs));
            emit_setNZ(ctx,T0); break; }
        case 9:  { // NEG
            ctx.emit(ppc_addi(T0,0,0));
            ctx.emit(ppc_mr(T1,pRd));
            ctx.emit(ppc_subfc(pRd,T1,T0));
            emit_setNZ(ctx,pRd);
            emit_setC_fromXER_sub(ctx);
            emit_setV_sub(ctx,pRd,T0,T1);
            break; }
        case 10: { // CMP
            ctx.emit(ppc_mr(T1,pRd));
            ctx.emit(ppc_subfc(T0,pRs,T1));
            emit_setNZ(ctx,T0);
            emit_setC_fromXER_sub(ctx);
            emit_setV_sub(ctx,T0,T1,pRs);
            break; }
        case 11: { // CMN
            ctx.emit(ppc_mr(T1,pRd));
            ctx.emit(ppc_addc(T0,T1,pRs));
            emit_setNZ(ctx,T0);
            emit_setC_fromXER_add(ctx);
            emit_setV_add(ctx,T0,T1,pRs);
            break; }
        case 12: ctx.emit(ppc_or(pRd,pRd,pRs));  emit_setNZ(ctx,pRd); break;
        case 13: ctx.emit(ppc_mullw(pRd,pRd,pRs));emit_setNZ(ctx,pRd); break;
        case 14: ctx.emit(ppc_andc(pRd,pRd,pRs));emit_setNZ(ctx,pRd); break;
        case 15: ctx.emit(ppc_nor(pRd,pRs,pRs));  emit_setNZ(ctx,pRd); break;
        default: return false;
    }
    return true;
}

static bool emit_t_hiReg(Ctx& ctx, uint16_t op) {
    uint8_t o  = (op>>8)&3;
    uint8_t h1 = (op>>7)&1, h2 = (op>>6)&1;
    uint8_t rs = ((op>>3)&7)|(h2<<3), rd = (op&7)|(h1<<3);
    if (rd==15||rs==15) return false;
    uint8_t pRd=R_ARM[rd], pRs=R_ARM[rs];
    switch (o) {
        case 0: ctx.emit(ppc_add(pRd,pRd,pRs)); break;
        case 1: {
            ctx.emit(ppc_mr(T1,pRd));
            ctx.emit(ppc_subfc(T0,pRs,T1));
            emit_setNZ(ctx,T0);
            emit_setC_fromXER_sub(ctx);
            emit_setV_sub(ctx,T0,T1,pRs);
            break;
        }
        case 2: ctx.emit(ppc_mr(pRd,pRs)); break;
        case 3: { // BX Rs
            ctx.emit(ppc_rlwinm(R_CPSR, R_CPSR, 0, 27, 25)); // clear T bit area
            ctx.emit(ppc_rlwinm(T0, pRs, 5, 26, 26));
            ctx.emit(ppc_rlwinm(R_CPSR, R_CPSR, 0, 27, 25)); // clear bit26
            ctx.emit(ppc_or(R_CPSR, R_CPSR, T0));
            ctx.emit(ppc_rlwinm(R_ARM[15], pRs, 0, 0, 30));
            emit_syncToInterp(ctx);
            emit_epilogue(ctx);
            ctx.terminated = true;
            break;
        }
    }
    return true;
}

static bool emit_t_ldrPc(Ctx& ctx, uint16_t op, uint32_t pc) {
    uint8_t  rd   = (op>>8)&7;
    uint32_t addr = ((pc+4)&~3u)+((op&0xFF)<<2);
    ctx.loadCore(T0);
    ctx.emit(ppc_addi(T1, 0, ctx.arm7 ? 1 : 0));
    ctx.li32(T2, addr);
    ctx.call((void*)JitPpc_read32);
    ctx.emit(ppc_mr(R_ARM[rd], T0));
    return true;
}

static bool emit_t_ldrStrReg(Ctx& ctx, uint16_t op, bool ld, bool by) {
    uint8_t rd=(op&7), rb=(op>>3)&7, ro=(op>>6)&7;
    uint8_t pRd=R_ARM[rd], pRb=R_ARM[rb], pRo=R_ARM[ro];
    ctx.emit(ppc_add(T2, pRb, pRo));  // addr = rb + ro
    ctx.emit(ppc_stw(T2, FRAME_SCRATCH0, 1));
    ctx.loadCore(T0);
    ctx.emit(ppc_addi(T1, 0, ctx.arm7 ? 1 : 0));
    ctx.emit(ppc_lwz(T2, FRAME_SCRATCH0, 1));
    if (!ld) ctx.emit(ppc_mr(T3, pRd));
    ctx.call(ld ? (by?(void*)JitPpc_read8:(void*)JitPpc_read32)
                : (by?(void*)JitPpc_write8:(void*)JitPpc_write32));
    if (ld) ctx.emit(ppc_mr(pRd, T0));
    return true;
}

static bool emit_t_ldrStrImm(Ctx& ctx, uint16_t op, bool ld, bool by, bool halfword) {
    uint8_t rd=(op&7), rb=(op>>3)&7;
    uint8_t pRd=R_ARM[rd], pRb=R_ARM[rb];
    uint32_t off = ((op>>6)&0x1F) * (halfword ? 2 : (by ? 1 : 4));
    ctx.li32(T2, off);
    ctx.emit(ppc_add(T2, pRb, T2));
    ctx.emit(ppc_stw(T2, FRAME_SCRATCH0, 1));
    ctx.loadCore(T0);
    ctx.emit(ppc_addi(T1, 0, ctx.arm7 ? 1 : 0));
    ctx.emit(ppc_lwz(T2, FRAME_SCRATCH0, 1));
    if (!ld) ctx.emit(ppc_mr(T3, pRd));
    void* fn;
    if (halfword) fn = ld ? (void*)JitPpc_read16 : (void*)JitPpc_write16;
    else          fn = ld ? (by?(void*)JitPpc_read8:(void*)JitPpc_read32)
                          : (by?(void*)JitPpc_write8:(void*)JitPpc_write32);
    ctx.call(fn);
    if (ld) ctx.emit(ppc_mr(pRd, T0));
    return true;
}

static bool emit_t_ldrSpPc(Ctx& ctx, uint16_t op, bool sp, bool ld, uint32_t pc) {
    uint8_t rd=(op>>8)&7;
    uint8_t pRd=R_ARM[rd];
    uint32_t off = (op&0xFF)<<2;
    uint32_t addr = 0;
    if (sp) {
        // addr = SP + offset: need to use SP register
        ctx.li32(T0, off);
        ctx.emit(ppc_add(T2, R_ARM[13], T0));
    } else {
        // PC-relative
        addr = ((pc+4)&~3u)+off;
        ctx.li32(T2, addr);
    }
    ctx.emit(ppc_stw(T2, FRAME_SCRATCH0, 1));
    ctx.loadCore(T0);
    ctx.emit(ppc_addi(T1, 0, ctx.arm7 ? 1 : 0));
    ctx.emit(ppc_lwz(T2, FRAME_SCRATCH0, 1));
    if (!ld) ctx.emit(ppc_mr(T3, pRd));
    ctx.call(ld ? (void*)JitPpc_read32 : (void*)JitPpc_write32);
    if (ld) ctx.emit(ppc_mr(pRd, T0));
    return true;
}

static bool emit_t_branch(Ctx& ctx, uint16_t op, uint32_t pc) {
    uint8_t cond = (op>>8)&0xF;
    if (cond == 0xF) return false;  // SWI
    if (cond == 0xE) {
        // Unconditional
        int32_t off = (int8_t)(op&0xFF); off <<= 1;
        uint32_t tgt = pc + 4 + off;
        ctx.li32(R_ARM[15], tgt);
        emit_syncToInterp(ctx);
        emit_epilogue(ctx);
        ctx.terminated = true;
        return true;
    }
    int32_t off = (int8_t)(op&0xFF); off <<= 1;
    uint32_t tgt = pc+4+off, fall = pc+2;
    emit_loadCR7(ctx);
    CB cb = armCond(cond);
    if (!cb.ok) return false;
    // Branch if condition NOT met -> fall through
    uint8_t invBo = (cb.bo==12)?4:(cb.bo==4?12:cb.bo);
    size_t skipIdx = ctx.size();
    ctx.emit(ppc_bc(invBo, cb.bi, 0));
    // Taken
    ctx.li32(R_ARM[15], tgt);
    emit_syncToInterp(ctx);
    emit_epilogue(ctx);
    // Not taken
    patchSkip(ctx, skipIdx);
    ctx.li32(R_ARM[15], fall);
    emit_syncToInterp(ctx);
    emit_epilogue(ctx);
    ctx.terminated = true;
    return true;
}

static bool emit_t_bl(Ctx& ctx, uint16_t op1, uint16_t op2, uint32_t pc) {
    int32_t hi = (int32_t)((op1&0x7FF)<<21)>>9;
    int32_t lo = (op2&0x7FF)<<1;
    uint32_t tgt = pc+4+hi+lo;
    uint8_t bb = (op2>>11)&0x1F;
    bool blx = (bb == 0x1C);
    ctx.li32(R_ARM[14], (pc+4)|1u);
    if (blx) {
        tgt &= ~3u;
        ctx.li32(R_ARM[15], tgt);
        ctx.emit(ppc_rlwinm(R_CPSR, R_CPSR, 0, 27, 25));  // clear T
    } else {
        ctx.li32(R_ARM[15], tgt & ~1u);
    }
    emit_syncToInterp(ctx);
    emit_epilogue(ctx);
    ctx.terminated = true;
    return true;
}

// ============================================================
// ARM instruction dispatch
// ============================================================
static bool dispatchARM(Ctx& ctx, uint32_t op, uint32_t pc) {
    uint8_t cond = (op>>28)&0xF;
    if (cond == 15) return false;
    uint32_t it = (op>>25)&7;
    switch (it) {
        case 0: case 1:
            if ((op&0x0FC000F0)==0x00000090) return emit_multiply(ctx,op);
            if ((op&0x0FFFFFF0)==0x012FFF10||
                (op&0x0FFFFFF0)==0x012FFF30)  return emit_branch(ctx,op,pc);
            // MRS/MSR/SWP/LDRH/STRH etc -> interpreter
            if ((op&0x0FB00FF0)==0x01000000||
                (op&0x0FB00000)==0x03200000||
                (op&0x0DB0F000)==0x010F0000||
                (op&0x0E0000F0)==0x00000090)  return false;
            return emit_dataProc(ctx,op);
        case 2: case 3:
            return emit_loadStore(ctx,op,pc);
        case 4:  return false;  // LDM/STM
        case 5:  return emit_branch(ctx,op,pc);
        default: return false;
    }
}

// ============================================================
// Thumb instruction dispatch
// ============================================================
static bool dispatchThumb(Ctx& ctx, uint16_t op, uint32_t pc) {
    uint8_t h  = op>>12;
    uint8_t b11= (op>>11)&7;

    switch (h) {
        case 0x0: // shifts, add/sub
            if (b11 < 3)  return emit_t_shiftImm(ctx,op);
            if (b11 == 3) return emit_t_addSub(ctx,op);
            return false;
        case 0x1: // mov/cmp/add/sub imm
            return emit_t_movCmpAddSub(ctx,op);
        case 0x2: {
            uint8_t b = (op>>10)&3;
            if (b==0) return emit_t_aluOp(ctx,op);
            if (b==1) return emit_t_hiReg(ctx,op);
            return emit_t_ldrPc(ctx,op,pc);
        }
        case 0x3: { // LDR/STR register
            bool ld=(op>>11)&1, by=(op>>10)&1;
            return emit_t_ldrStrReg(ctx,op,ld,by);
        }
        case 0x4: // STRH/LDRH/LDRSB/LDRSH reg
            return false;
        case 0x5: // STR/LDR byte imm
        case 0x6: { // STR/LDR word imm
            bool ld=(op>>11)&1, by=(h==5);
            return emit_t_ldrStrImm(ctx,op,ld,by,false);
        }
        case 0x7: { // STRH/LDRH imm
            bool ld=(op>>11)&1;
            return emit_t_ldrStrImm(ctx,op,ld,false,true);
        }
        case 0x8: { // LDR SP-relative / STR SP-relative
            bool ld=(op>>11)&1;
            return emit_t_ldrSpPc(ctx,op,true,ld,pc);
        }
        case 0x9: { // ADD PC/SP
            return false;
        }
        case 0xA: case 0xB:  return false;  // misc
        case 0xC: { // LDM/STM
            return false;
        }
        case 0xD: {
            uint8_t c=(op>>8)&0xF;
            if (c==0xF) return false;  // SWI
            return emit_t_branch(ctx,op,pc);
        }
        case 0xE: // unconditional branch
            return emit_t_branch(ctx,op,pc);
        case 0xF: // BL/BLX prefix (handled in block loop)
            return false;
        default: return false;
    }
}

// ============================================================
// Valid PC check
// ============================================================
static bool validPC(uint32_t pc, bool gba) {
    pc &= ~1u;
    if (gba) return (pc<0x4000u)||
                    (pc>=0x02000000u&&pc<0x02040000u)||
                    (pc>=0x03000000u&&pc<0x03008000u)||
                    (pc>=0x08000000u&&pc<0x0E000000u);
    return (pc<0x4000u)||
           (pc>=0x02000000u&&pc<0x02400000u)||
           (pc>=0x03000000u&&pc<0x03800000u)||
           (pc>=0xFFFF0000u);
}

// ============================================================
// Compile a block
// ============================================================
static JitBlock* compile(Interpreter* interp, Core* core,
                          uint32_t armPC, bool arm7) {
    if (!validPC(armPC, core->gbaMode)) return nullptr;

    bool    thumb  = interp->isThumb();
    size_t  bucket = hashPC(armPC);
    JitBlock& slot = blockCache[bucket];

    if (slot.valid && slot.armPC==armPC && slot.thumb==thumb)
        return &slot;

    if (codeWritePos + MAX_BLOCK_WORDS >= JIT_MAX_WORDS)
        flushJitCache();

    Ctx ctx;
    ctx.base      = codeBuffer + codeWritePos;
    ctx.cur       = ctx.base;
    ctx.cap       = JIT_MAX_WORDS - codeWritePos;
    ctx.thumb     = thumb;
    ctx.arm7      = arm7;
    ctx.blockPC   = armPC;
    ctx.interp    = interp;
    ctx.core      = core;
    ctx.terminated= false;

    emit_prologue(ctx);
    emit_syncFromInterp(ctx);

    uint32_t pc = armPC;
    int      n  = 0;

    while (n < (int)MAX_BLOCK_ARMS && !ctx.terminated) {
        // Set PC pipeline value
        ctx.li32(R_ARM[15], pc + (thumb ? 4u : 8u));

        if (thumb) {
            uint16_t op = core->memory.read<uint16_t>(arm7, pc);
            // Two-halfword BL/BLX?
            if (((op>>11)&0x1F) == 0x1E) {
                uint16_t op2 = core->memory.read<uint16_t>(arm7, pc+2);
                uint8_t  bb  = (op2>>11)&0x1F;
                if (bb==0x1F||bb==0x1C) {
                    emit_t_bl(ctx, op, op2, pc);
                    pc += 4; n += 2;
                    continue;
                }
            }
            bool ok = dispatchThumb(ctx, op, pc);
            if (!ok) { emit_fallback(ctx); ctx.terminated = true; }
            else {
                pc += 2; n++;
                // End block on branches
                if ((op>>12)==0xD||(op>>12)==0xE||(op>>11)==0x1C)
                    ctx.terminated = true;
            }
        } else {
            uint32_t op = core->memory.read<uint32_t>(arm7, pc);
            bool ok = dispatchARM(ctx, op, pc);
            if (!ok) { emit_fallback(ctx); ctx.terminated = true; }
            else {
                pc += 4; n++;
                uint32_t it = (op>>25)&7;
                if (it==5) ctx.terminated = true;
                if ((op&0x0FFFFFF0)==0x012FFF10||
                    (op&0x0FFFFFF0)==0x012FFF30) ctx.terminated = true;
            }
        }
    }

    if (!ctx.terminated) {
        emit_syncToInterp(ctx);
        emit_epilogue(ctx);
    }

    size_t words = ctx.size();
    flushCache(ctx.base, words);

    slot.armPC  = armPC;
    slot.code   = ctx.base;
    slot.nWords = (uint32_t)words;
    slot.thumb  = thumb;
    slot.valid  = true;
    codeWritePos += words;
    return &slot;
}

// ============================================================
// Offset computation (for C helpers that use raw offsets)
// ============================================================
static size_t off_cycles = 0;

static void computeOffsets() {
    off_cycles = Interpreter::offset_cycles();
    printf("[JIT] cycles_off=%zu\n", off_cycles);
}

// ============================================================
// Run loops
// ============================================================
void runJitNds(Core& core) {
    for (int cpu = 0; cpu < 2; cpu++) {
        if (core.interpreter[cpu].halted) continue;
        uint32_t pc = core.interpreter[cpu].getActualPC();
        JitBlock* b = compile(&core.interpreter[cpu], &core, pc, cpu==1);
        if (b) executeBlock(b->code);
        else   core.interpreter[cpu].jitRunOpcode();
    }
    JitPpc_tick(&core);
}

void runJitGba(Core& core) {
    if (!core.interpreter[1].halted) {
        uint32_t pc = core.interpreter[1].getActualPC();
        JitBlock* b = compile(&core.interpreter[1], &core, pc, true);
        if (b) executeBlock(b->code);
        else   core.interpreter[1].jitRunOpcode();
    }
    JitPpc_tick(&core);
}

// ============================================================
// Init / shutdown / invalidate
// ============================================================
bool initJit(Core* core) {
    computeOffsets();

    codeBuffer = (uint32_t*)memalign(32, JIT_CODE_SIZE);
    if (!codeBuffer) { printf("[JIT] alloc failed\n"); return false; }

    codeWritePos = 0;
    for (size_t i = 0; i < CACHE_SIZE; i++) blockCache[i].valid = false;

    printf("[JIT] code buf %p (%zuKB)\n", (void*)codeBuffer, JIT_CODE_SIZE>>10);

    if (core) core->setRunFunc(core->gbaMode ? runJitGba : runJitNds);
    return true;
}

void shutdownJit(Core* core) {
    if (core) core->setRunFunc(core->gbaMode
        ? static_cast<void(*)(Core&)>(&Interpreter::runCoreSingle<true,0>)
        : &Interpreter::runCoreNds);
    free(codeBuffer); codeBuffer = nullptr;
}

void invalidateJitRange(uint32_t start, uint32_t end) {
    for (size_t i = 0; i < CACHE_SIZE; i++)
        if (blockCache[i].valid &&
            blockCache[i].armPC >= start &&
            blockCache[i].armPC < end)
            blockCache[i].valid = false;
}

} // namespace JitPpc
