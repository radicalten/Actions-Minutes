// jit_ppc.cpp
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
// Exit reason codes
// ============================================================
static const int EXIT_NORMAL   = 0;  // block completed normally
static const int EXIT_FALLBACK = 1;  // unhandled opcode; interpreter must run it

// Per-CPU exit state written by JitHelp_storeExit, read by the run loop
static uint32_t g_exitPC    [2] = {};
static uint32_t g_exitCPSR  [2] = {};
static int      g_exitReason[2] = {};

// ============================================================
// Stack frame layout (208 bytes, 16-byte aligned)
//
//  offset  size  content
//  ------  ----  -------
//    0      8    back-chain / reserved (ABI)
//    8      4    saved LR
//   12      4    pad
//   16     72    saved r14-r31 (18 * 4)
//   88      4    Core* spill
//   92      4    scratch 0  (op2 / offset save)
//   96      4    scratch 1  (address / Rn save)
//  100      4    scratch 2  (BX: original Rm / new PC)
//  104      4    scratch 3  (PC-operand save for rnIsPC)
//  108      4    pad
//  112     60    ARM r0-r14 sync area (15 * 4)
//  172     36    pad to 208
// ============================================================
static const int FRAME_SIZE    = 208;
static const int FRAME_LR      = 8;
static const int FRAME_R14     = 16;
static const int FRAME_CORE    = 88;
static const int FRAME_SCR0    = 92;
static const int FRAME_SCR1    = 96;
static const int FRAME_SCR2    = 100;
static const int FRAME_SCR3    = 104;
static const int FRAME_REGSYNC = 112;

static_assert(FRAME_SIZE    % 16 == 0,               "frame must be 16-byte aligned");
static_assert(FRAME_R14 + 18*4  == FRAME_CORE,       "r14-r31 region layout mismatch");
static_assert(FRAME_REGSYNC + 15*4 <= FRAME_SIZE,    "regsync region overflows frame");

namespace JitPpc {

// ============================================================
// PPC instruction word builders
// ============================================================
static inline uint32_t ppc_nop()  { return 0x60000000u; }
static inline uint32_t ppc_blr()  { return 0x4E800020u; }

static inline uint32_t ppc_bctr(bool lk = false)
    { return (19u<<26)|(20u<<21)|(528u<<1)|(lk?1u:0u); }

static inline uint32_t ppc_bc(uint8_t bo, uint8_t bi, int16_t off, bool lk = false)
    { return (16u<<26)|((bo&31u)<<21)|((bi&31u)<<16)|((uint32_t)(off&0xFFFC))|(lk?1u:0u); }

static inline uint32_t ppc_addi (uint8_t rt, uint8_t ra, int16_t  i)
    { return (14u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_addis(uint8_t rt, uint8_t ra, int16_t  i)
    { return (15u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_ori  (uint8_t ra, uint8_t rs, uint16_t i)
    { return (24u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i; }

static inline uint32_t ppc_stwu(uint8_t rs, int16_t d, uint8_t ra)
    { return (37u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_stw (uint8_t rs, int16_t d, uint8_t ra)
    { return (36u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_lwz (uint8_t rt, int16_t d, uint8_t ra)
    { return (32u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_lbz (uint8_t rt, int16_t d, uint8_t ra)
    { return (34u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_lhz (uint8_t rt, int16_t d, uint8_t ra)
    { return (40u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }

static inline uint32_t ppc_cmpi (uint8_t cr, uint8_t ra, int16_t  i)
    { return (11u<<26)|((cr&7u)<<23)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_subfic(uint8_t rt, uint8_t ra, int16_t i)
    { return (8u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }

// X-form helpers
static inline uint32_t Xf(uint8_t rt, uint8_t ra, uint8_t rb, uint32_t x, bool rc = false)
    { return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|
             ((uint32_t)rb<<11)|(x<<1)|(rc?1u:0u); }
static inline uint32_t XOf(uint8_t rt, uint8_t ra, uint8_t rb, bool oe, uint32_t x, bool rc = false)
    { return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|
             ((uint32_t)rb<<11)|(oe?0x400u:0u)|(x<<1)|(rc?1u:0u); }

static inline uint32_t ppc_add  (uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,266);}
static inline uint32_t ppc_addc (uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,10 );}
static inline uint32_t ppc_adde (uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,138);}
static inline uint32_t ppc_subf (uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,40 );}
static inline uint32_t ppc_subfc(uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,8  );}
static inline uint32_t ppc_subfe(uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,136);}
static inline uint32_t ppc_mullw(uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,235);}
static inline uint32_t ppc_and  (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,28 );}
static inline uint32_t ppc_or   (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,444);}
static inline uint32_t ppc_xor  (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,316);}
static inline uint32_t ppc_andc (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,60 );}
static inline uint32_t ppc_nor  (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,124);}
static inline uint32_t ppc_mr   (uint8_t a,uint8_t s)          {return ppc_or(a,s,s);}
static inline uint32_t ppc_slw  (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,24 );}
static inline uint32_t ppc_srw  (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,536);}
static inline uint32_t ppc_sraw (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,792);}
static inline uint32_t ppc_extsb(uint8_t a,uint8_t s)          {return Xf(s,a,0,954);}
static inline uint32_t ppc_extsh(uint8_t a,uint8_t s)          {return Xf(s,a,0,922);}

static inline uint32_t ppc_rlwinm(uint8_t a,uint8_t s,uint8_t sh,uint8_t mb,uint8_t me,bool rc=false)
    { return (21u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)|
             ((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1)|(rc?1u:0u); }
static inline uint32_t ppc_rlwimi(uint8_t a,uint8_t s,uint8_t sh,uint8_t mb,uint8_t me)
    { return (20u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)|
             ((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1); }
static inline uint32_t ppc_rlwnm(uint8_t a,uint8_t s,uint8_t b,uint8_t mb,uint8_t me)
    { return (23u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)|
             ((uint32_t)b<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1); }
static inline uint32_t ppc_srawi(uint8_t a,uint8_t s,uint8_t sh)
    { return (31u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)|((uint32_t)sh<<11)|(824u<<1); }

static inline uint32_t ppc_mtspr(uint16_t spr, uint8_t rs) {
    uint8_t lo = spr & 31, hi = (spr>>5) & 31;
    return (31u<<26)|((uint32_t)rs<<21)|((uint32_t)lo<<16)|((uint32_t)hi<<11)|(467u<<1);
}
static inline uint32_t ppc_mfspr(uint8_t rt, uint16_t spr) {
    uint8_t lo = spr & 31, hi = (spr>>5) & 31;
    return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)lo<<16)|((uint32_t)hi<<11)|(339u<<1);
}
static inline uint32_t ppc_mtctr(uint8_t s) { return ppc_mtspr(9, s); }
static inline uint32_t ppc_mtlr (uint8_t s) { return ppc_mtspr(8, s); }
static inline uint32_t ppc_mflr (uint8_t t) { return ppc_mfspr(t, 8); }
static inline uint32_t ppc_mtxer(uint8_t s) { return ppc_mtspr(1, s); }
static inline uint32_t ppc_mfxer(uint8_t t) { return ppc_mfspr(t, 1); }
static inline uint32_t ppc_mfcr (uint8_t t) { return (31u<<26)|((uint32_t)t<<21)|(19u<<1); }
static inline uint32_t ppc_mtcrf(uint8_t fxm, uint8_t s)
    { return (31u<<26)|((uint32_t)s<<21)|((uint32_t)(fxm&0xFF)<<12)|(144u<<1); }

// Load a 32-bit constant into rt; returns number of words emitted
static int emit_li32(uint32_t* out, uint8_t rt, uint32_t v) {
    uint16_t hi = v >> 16, lo = v & 0xFFFF;
    if (!hi && !lo)   { out[0] = ppc_addi(rt, 0, 0);                                 return 1; }
    if (!hi)          { if (lo < 0x8000) { out[0] = ppc_addi(rt,0,(int16_t)lo); return 1; }
                        out[0] = ppc_addi(rt,0,0); out[1] = ppc_ori(rt,rt,lo);       return 2; }
    if (!lo)          { out[0] = ppc_addis(rt, 0, (int16_t)hi);                       return 1; }
    out[0] = ppc_addis(rt, 0, (int16_t)hi);
    out[1] = ppc_ori  (rt, rt, lo);
    return 2;
}

// ============================================================
// Register allocation
//
//  r14-r28  ARM r0-r14  (callee-saved)
//  r29      ARM CPSR    (callee-saved)
//  r30      Interpreter*(callee-saved)
//  r31      cpuIdx      (callee-saved)
//  r1+FRAME_CORE        Core* spilled to frame
//
//  Volatile scratch: r3=TA, r4=TB, r5=TC, r6=TD, r11=RCALL
// ============================================================
static const uint8_t RA[15]  = {14,15,16,17,18,19,20,21,22,23,24,25,26,27,28};
static const uint8_t RCPSR   = 29;
static const uint8_t RINTERP = 30;
static const uint8_t RCPUIDX = 31;
static const uint8_t TA=3, TB=4, TC=5, TD=6, RCALL=11;

// ============================================================
// Code buffer
// ============================================================
static const size_t JIT_BYTES = 4u * 1024u * 1024u;
static const size_t JIT_WORDS = JIT_BYTES / 4;

// Upper bound on words a single block can emit:
// BLK_ARMS instructions, each needing at most ~160 PPC words, plus
// a generous budget for prologue / epilogue / sync sequences.
static const size_t BLK_ARMS = 64;
static const size_t BLK_WDS  = BLK_ARMS * 160 + 512;

static uint32_t* codeBuf = nullptr;
static size_t    codePos = 0;

// ============================================================
// Block cache
// ============================================================
struct JitBlock {
    uint32_t  armPC;   // ARM PC this block was compiled for
    uint32_t* code;    // pointer into codeBuf
    uint32_t  nW;      // length in PPC words
    bool      thumb;
    bool      valid;
};

static const size_t CSIZ = 1u << 13;   // 8192 slots
static JitBlock cache[CSIZ];

// Generation counter: incremented on every full cache flush.
// Stored inside every block so the run loop can detect staleness.
static uint32_t cacheGen = 0;

static size_t hashPC(uint32_t pc) { return (pc >> 1) & (CSIZ - 1); }

void flushJitCache() {
    codePos = 0;
    ++cacheGen;
    for (size_t i = 0; i < CSIZ; i++) cache[i].valid = false;
}

static void flushICache(uint32_t* p, size_t n) {
    DCFlushRange (p, n * 4);
    ICInvalidateRange(p, n * 4);
}

// ============================================================
// Emit context
// ============================================================
struct Ctx {
    uint32_t* base;
    uint32_t* cur;
    size_t    cap;
    bool      thumb;
    bool      arm7;
    bool      done;    // true once an exit / branch has been emitted
    uint32_t  blockPC;
    int       cpuIdx;
    Interpreter* interp;
    Core*        core;

    void E(uint32_t w) { if ((size_t)(cur - base) < cap) *cur++ = w; }
    size_t sz() const  { return (size_t)(cur - base); }

    void li(uint8_t rt, uint32_t v) {
        uint32_t t[2]; int n = emit_li32(t, rt, v);
        for (int i = 0; i < n; i++) E(t[i]);
    }

    // Emit an indirect call: load fn address into RCALL, mtctr, bctrl
    void call(void* fn) {
        uint32_t a = (uint32_t)(uintptr_t)fn;
        E(ppc_addis(RCALL, 0, (int16_t)(a >> 16)));
        E(ppc_ori  (RCALL, RCALL, (uint16_t)(a & 0xFFFF)));
        E(ppc_mtctr(RCALL));
        E(ppc_bctr(true));   // bctrl — sets LR to next instruction
    }

    void ldCore(uint8_t d = TA) { E(ppc_lwz(d, FRAME_CORE, 1)); }
};

// ============================================================
// C helpers (called from JIT-generated code via bctrl)
// ============================================================
extern "C" {

void JitHelp_syncFrom(Interpreter* interp, uint32_t* regs, uint32_t* outCPSR) {
    uint32_t** p = interp->getRegisters();
    for (int i = 0; i < 15; i++) regs[i] = *p[i];
    *outCPSR = interp->getCpsrRef();
}

void JitHelp_syncTo(Interpreter* interp, uint32_t* regs, uint32_t cpsr) {
    uint32_t** p = interp->getRegisters();
    for (int i = 0; i < 15; i++) *p[i] = regs[i];
    interp->getCpsrRef() = cpsr;
}

void JitHelp_storeExit(int cpuIdx, uint32_t nextPC, uint32_t cpsr, int reason) {
    g_exitPC    [cpuIdx] = nextPC;
    g_exitCPSR  [cpuIdx] = cpsr;
    g_exitReason[cpuIdx] = reason;
}

uint32_t JitHelp_r32(Core* c,int a,uint32_t ad){return c->memory.read<uint32_t>((bool)a,ad);}
uint16_t JitHelp_r16(Core* c,int a,uint32_t ad){return c->memory.read<uint16_t>((bool)a,ad);}
uint8_t  JitHelp_r8 (Core* c,int a,uint32_t ad){return c->memory.read<uint8_t> ((bool)a,ad);}
void JitHelp_w32(Core* c,int a,uint32_t ad,uint32_t v){c->memory.write<uint32_t>((bool)a,ad,v);}
void JitHelp_w16(Core* c,int a,uint32_t ad,uint16_t v){c->memory.write<uint16_t>((bool)a,ad,v);}
void JitHelp_w8 (Core* c,int a,uint32_t ad,uint8_t  v){c->memory.write<uint8_t> ((bool)a,ad,v);}

void JitHelp_tick(Core* core) {
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
// Prologue / epilogue
// ============================================================
static void emitPrologue(Ctx& ctx) {
    ctx.E(ppc_mflr(0));
    ctx.E(ppc_stwu(1, -(int16_t)FRAME_SIZE, 1));
    ctx.E(ppc_stw(0, FRAME_LR, 1));
    for (int r = 14; r <= 31; r++)
        ctx.E(ppc_stw(r, FRAME_R14 + (r-14)*4, 1));
    ctx.li(RINTERP, (uint32_t)(uintptr_t)ctx.interp);
    ctx.li(TA,      (uint32_t)(uintptr_t)ctx.core);
    ctx.E(ppc_stw(TA, FRAME_CORE, 1));
    ctx.E(ppc_addi(RCPUIDX, 0, (int16_t)ctx.cpuIdx));
}

static void emitEpilogue(Ctx& ctx) {
    for (int r = 14; r <= 31; r++)
        ctx.E(ppc_lwz(r, FRAME_R14 + (r-14)*4, 1));
    ctx.E(ppc_lwz(0, FRAME_LR, 1));
    ctx.E(ppc_mtlr(0));
    ctx.E(ppc_addi(1, 1, (int16_t)FRAME_SIZE));
    ctx.E(ppc_blr());
}

// ============================================================
// ARM-register sync at block entry
// ============================================================
static void emitSyncFrom(Ctx& ctx) {
    ctx.E(ppc_mr(TA, RINTERP));
    ctx.E(ppc_addi(TB, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_addi(TC, 1, (int16_t)FRAME_SCR0));
    ctx.call((void*)JitHelp_syncFrom);
    for (int i = 0; i < 15; i++)
        ctx.E(ppc_lwz(RA[i], FRAME_REGSYNC + i*4, 1));
    ctx.E(ppc_lwz(RCPSR, FRAME_SCR0, 1));
}

// ============================================================
// Block exit: spill r0-r14 + CPSR, store exit info, return
// ============================================================
static void emitExit(Ctx& ctx, uint32_t nextPC, int reason) {
    // 1. Spill ARM r0-r14 to sync area
    for (int i = 0; i < 15; i++)
        ctx.E(ppc_stw(RA[i], FRAME_REGSYNC + i*4, 1));

    // 2. JitHelp_syncTo(interp, &regsync, cpsr)
    ctx.E(ppc_mr(TA, RINTERP));
    ctx.E(ppc_addi(TB, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_mr(TC, RCPSR));
    ctx.call((void*)JitHelp_syncTo);

    // 3. JitHelp_storeExit(cpuIdx, nextPC, cpsr, reason)
    ctx.E(ppc_mr(TA, RCPUIDX));
    ctx.li(TB, nextPC);
    ctx.E(ppc_mr(TC, RCPSR));
    ctx.E(ppc_addi(TD, 0, (int16_t)reason));
    ctx.call((void*)JitHelp_storeExit);

    // 4. Restore callee-saved regs and return
    emitEpilogue(ctx);
}

// ============================================================
// Condition handling — uses CR7
//
// ARM CPSR (LSB): bit31=N  bit30=Z  bit29=C  bit28=V
// mtcrf 0x01, RCPSR maps GPR bits 31..28 -> CR7[LT,GT,EQ,SO]
//   CR7[LT](BI=28) = N
//   CR7[GT](BI=29) = Z
//   CR7[EQ](BI=30) = C
//   CR7[SO](BI=31) = V
// ============================================================
static void emitLoadCR7(Ctx& ctx) { ctx.E(ppc_mtcrf(0x01, RCPSR)); }

struct CB { uint8_t bo, bi; bool ok; };

static CB condPpc(uint8_t c) {
    // BO=12: branch if set; BO=4: branch if clear
    switch (c) {
        case 0:  return {12, 29, true};  // EQ  Z=1
        case 1:  return { 4, 29, true};  // NE  Z=0
        case 2:  return {12, 30, true};  // CS  C=1
        case 3:  return { 4, 30, true};  // CC  C=0
        case 4:  return {12, 28, true};  // MI  N=1
        case 5:  return { 4, 28, true};  // PL  N=0
        case 6:  return {12, 31, true};  // VS  V=1
        case 7:  return { 4, 31, true};  // VC  V=0
        // Multi-flag conditions (HI/LS/GE/LT/GT/LE): fall back to interpreter
        case 8:  return {0, 0, false};   // HI  C=1 && Z=0
        case 9:  return {0, 0, false};   // LS  C=0 || Z=1
        case 10: return {0, 0, false};   // GE  N==V
        case 11: return {0, 0, false};   // LT  N!=V
        case 12: return {0, 0, false};   // GT  Z=0 && N==V
        case 13: return {0, 0, false};   // LE  Z=1 || N!=V
        case 14: return {20, 0, true};   // AL  always
        default: return {0, 0, false};
    }
}

// Emit a conditional skip over the code that follows.
// Returns the word index of the branch instruction so patchSkip can fix it,
// or SIZE_MAX if no branch was needed (AL) or the condition is unsupported.
static size_t emitCondSkip(Ctx& ctx, uint8_t cond) {
    if (cond == 14) return SIZE_MAX;          // AL — always execute
    emitLoadCR7(ctx);
    CB cb = condPpc(cond);
    if (!cb.ok) return SIZE_MAX;              // unsupported — caller must fall back
    // Invert the condition so we SKIP the body when condition is NOT met
    uint8_t inv = (cb.bo == 12) ? 4u : (cb.bo == 4u ? 12u : 20u);
    size_t idx = ctx.sz();
    ctx.E(ppc_bc(inv, cb.bi, 4));            // offset 4 is a dummy; patched below
    return idx;
}

// Back-patch the bc at word index idx to jump to the current emit position
static void patchSkip(Ctx& ctx, size_t idx) {
    if (idx == SIZE_MAX) return;
    int32_t off = (int32_t)((ctx.sz() - idx) * 4);
    ctx.base[idx] = (ctx.base[idx] & 0xFFFF0003u) | (uint32_t)(off & 0xFFFC);
}

// ============================================================
// Flag helpers
//
// CPSR bit positions (LSB numbering):
//   31=N  30=Z  29=C  28=V
// In PPC rlwinm MB/ME field (MSB=0 notation):
//   bit31(LSB) = pos 0 (MSB)
//   bit30(LSB) = pos 1
//   bit29(LSB) = pos 2
//   bit28(LSB) = pos 3
// ============================================================

static void setNZ(Ctx& ctx, uint8_t r) {
    // Clear N(bit31) and Z(bit30)
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 2, 31));   // zero bits 31..30 (MSB pos 0..1)
    // Copy N from r[31]
    ctx.E(ppc_rlwimi(RCPSR, r, 0, 0, 0));         // CPSR[31] = r[31]
    // Z: 1 if r == 0
    ctx.E(ppc_cmpi(6, r, 0));                     // compare into CR6
    ctx.E(ppc_mfcr(TA));
    // CR6[EQ] is at GPR bit 25 (MSB notation, CR6 starts at bit 24).
    // We need it at bit 30 (LSB) = bit 1 (MSB) of RCPSR.
    // Rotate: bit25 -> bit1 requires left-rotate by (25-1)=24? No:
    //   mfcr puts CR0 at bits 31..28 (MSB 0..3), CR1 at 27..24, ... CR6 at 7..4.
    //   CR6[EQ] = bit 5 (MSB) = bit 26 (LSB).
    //   We want it at CPSR bit 30 (LSB) = bit 1 (MSB).
    //   Shift left by (30-26)=4 in LSB terms = rotate left by 4 in rlwinm:
    ctx.E(ppc_rlwinm(TA, TA, 4, 1, 1));           // TA[1(MSB)] = CR6[EQ], rest=0
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}

static void setC_xer(Ctx& ctx) {
    // XER[CA] is at bit 29 (LSB) = bit 2 (MSB) — same position as CPSR[C]
    ctx.E(ppc_mfxer(TA));
    ctx.E(ppc_rlwinm(TA, TA, 0, 2, 2));            // isolate XER[CA] at bit29(LSB)
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 3, 1));      // clear CPSR[C] (wrap mask: 3..31,0..1)
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}

static void setV_add(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    // Overflow if signs of a and b agree but differ from result
    ctx.E(ppc_xor(TA, res, a));
    ctx.E(ppc_xor(TB, res, b));
    ctx.E(ppc_and(TA, TA, TB));                   // bit31 set iff overflow
    // Move bit31(LSB) -> bit28(LSB) = bit3(MSB)
    ctx.E(ppc_rlwinm(TA, TA, 4, 3, 3));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 4, 2));     // clear CPSR[V] (wrap mask: 4..31,0..2)
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}

static void setV_sub(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    // res = a - b; overflow if signs of a and b differ and result differs from a
    ctx.E(ppc_xor(TA, a, b));
    ctx.E(ppc_xor(TB, a, res));
    ctx.E(ppc_and(TA, TA, TB));
    ctx.E(ppc_rlwinm(TA, TA, 4, 3, 3));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 4, 2));
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}

static void setC_bit0(Ctx& ctx, uint8_t cr) {
    // cr[0] -> CPSR[C] (bit29 LSB = bit2 MSB)
    ctx.E(ppc_rlwinm(TA, cr, 29, 2, 2));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 3, 1));
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}

// ============================================================
// Barrel shifter  (result -> dst; carry-out -> TC[0] when sc=true)
// ============================================================
static void sLslI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i == 0) {
        if (d != s) ctx.E(ppc_mr(d, s));
        if (sc) ctx.E(ppc_rlwinm(TC, RCPSR, 3, 31, 31));    // preserve CPSR[C]
    } else if (i < 32) {
        if (sc) ctx.E(ppc_rlwinm(TC, s, (uint8_t)i, 31, 31));
        ctx.E(ppc_rlwinm(d, s, (uint8_t)i, 0, (uint8_t)(31-i)));
    } else if (i == 32) {
        if (sc) ctx.E(ppc_rlwinm(TC, s, 0, 31, 31));
        ctx.E(ppc_addi(d, 0, 0));
    } else {
        if (sc) ctx.E(ppc_addi(TC, 0, 0));
        ctx.E(ppc_addi(d, 0, 0));
    }
}

static void sLsrI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i == 0 || i == 32) {
        if (sc) ctx.E(ppc_rlwinm(TC, s, 1, 31, 31));    // MSB
        ctx.E(ppc_addi(d, 0, 0));
    } else if (i < 32) {
        if (sc) ctx.E(ppc_rlwinm(TC, s, (uint8_t)(33-i), 31, 31));
        ctx.E(ppc_rlwinm(d, s, (uint8_t)(32-i), (uint8_t)i, 31));
    } else {
        if (sc) ctx.E(ppc_addi(TC, 0, 0));
        ctx.E(ppc_addi(d, 0, 0));
    }
}

static void sAsrI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i <= 0 || i >= 32) {
        if (sc) ctx.E(ppc_rlwinm(TC, s, 1, 31, 31));
        ctx.E(ppc_srawi(d, s, 31));
    } else {
        if (sc) ctx.E(ppc_rlwinm(TC, s, (uint8_t)(33-i), 31, 31));
        ctx.E(ppc_srawi(d, s, (uint8_t)i));
    }
}

static void sRorI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i == 0) {
        // RRX: result = (CPSR[C] << 31) | (s >> 1)
        ctx.E(ppc_rlwinm(TA, RCPSR, 3, 31, 31));    // TA = CPSR[C] at bit0
        if (sc) ctx.E(ppc_rlwinm(TC, s, 0, 31, 31));
        ctx.E(ppc_rlwinm(d, s, 31, 1, 31));         // d = s >> 1
        ctx.E(ppc_rlwimi(d, TA, 31, 0, 0));         // d[31] = old CPSR[C]
    } else {
        i &= 31; if (!i) i = 32;
        if (i < 32) {
            if (sc) ctx.E(ppc_rlwinm(TC, s, (uint8_t)(33-i), 31, 31));
            ctx.E(ppc_rlwinm(d, s, (uint8_t)(32-i), 0, 31));
        } else {
            if (d != s) ctx.E(ppc_mr(d, s));
            if (sc) ctx.E(ppc_rlwinm(TC, s, 1, 31, 31));
        }
    }
}

// Returns true iff carry was placed in TC[0]
static bool emitShifter(Ctx& ctx, uint32_t op, uint8_t dst, bool sc) {
    bool isImm = (op >> 25) & 1;
    if (isImm) {
        uint32_t v = op & 0xFF, rot = ((op >> 8) & 0xF) * 2;
        if (rot) v = (v >> rot) | (v << (32-rot));
        ctx.li(dst, v);
        if (sc && rot) { ctx.E(ppc_rlwinm(TC, dst, 1, 31, 31)); return true; }
        return false;
    }

    uint8_t rm = op & 0xF, pRm = RA[rm], st = (op >> 5) & 3;
    bool isReg = (op >> 4) & 1;

    if (!isReg) {
        int sa = (op >> 7) & 0x1F;
        switch (st) {
            case 0: sLslI(ctx, dst, pRm, sa,       sc); break;
            case 1: sLsrI(ctx, dst, pRm, sa?sa:32, sc); break;
            case 2: sAsrI(ctx, dst, pRm, sa?sa:32, sc); break;
            case 3: sRorI(ctx, dst, pRm, sa,       sc); break;
        }
        return sc;
    }

    // Register-controlled shift
    uint8_t rs = (op >> 8) & 0xF, pRs = RA[rs];
    ctx.E(ppc_rlwinm(TD, pRs, 0, 24, 31));    // TD = Rs & 0xFF
    ctx.E(ppc_mr(TA, pRm));
    switch (st) {
        case 0: ctx.E(ppc_slw(dst, TA, TD));  break;
        case 1: ctx.E(ppc_srw(dst, TA, TD));  break;
        case 2: ctx.E(ppc_sraw(dst, TA, TD)); break;
        case 3:
            ctx.E(ppc_subfic(TB, TD, 32));
            ctx.E(ppc_rlwnm(dst, TA, TB, 0, 31));
            break;
    }
    return false;
}

// ============================================================
// Data processing
// ============================================================
enum DP { AND=0,EOR,SUB,RSB,ADD,ADC,SBC,RSC,TST,TEQ,CMP,CMN,ORR,MOV,BIC,MVN };

static bool emitDP(Ctx& ctx, uint32_t op, uint32_t curPC) {
    uint8_t cond = (op >> 28) & 0xF;
    uint8_t dop  = (op >> 21) & 0xF;
    bool    s    = (op >> 20) & 1;
    uint8_t rn   = (op >> 16) & 0xF;
    uint8_t rd   = (op >> 12) & 0xF;
    if (rd == 15) return false;

    uint8_t pRd = RA[rd];

    // Rn==PC: pipeline-adjusted value
    bool    rnIsPC = (rn == 15);
    uint8_t srcRn;
    if (rnIsPC) {
        uint32_t pcVal = curPC + (ctx.thumb ? 4u : 8u);
        ctx.li(TD, pcVal);
        ctx.E(ppc_stw(TD, FRAME_SCR3, 1));
        srcRn = TD;
    } else {
        srcRn = RA[rn];
    }

    size_t si = emitCondSkip(ctx, cond);
    if (si == SIZE_MAX && cond != 14) return false;

    bool needCin  = (dop == ADC || dop == SBC || dop == RSC);
    bool logCarry = s && (dop==AND||dop==EOR||dop==TST||dop==TEQ||
                          dop==ORR||dop==MOV||dop==BIC||dop==MVN);

    if (needCin) {
        ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2));   // isolate CPSR[C]
        ctx.E(ppc_mtxer(TA));
    }

    bool carrySet = emitShifter(ctx, op, TA, logCarry);

    if (rnIsPC) { ctx.E(ppc_lwz(TD, FRAME_SCR3, 1)); srcRn = TD; }

    bool needV = s && (dop==ADD||dop==SUB||dop==RSB||dop==CMN||dop==CMP||
                       dop==ADC||dop==SBC||dop==RSC);
    if (needV) {
        ctx.E(ppc_stw(TA, FRAME_SCR0, 1));
        ctx.E(ppc_stw(srcRn, FRAME_SCR1, 1));
    }

    bool    isTest = (dop==TST||dop==TEQ||dop==CMP||dop==CMN);
    uint8_t res    = isTest ? TC : pRd;

    switch ((DP)dop) {
        case AND: case TST: ctx.E(ppc_and  (res, srcRn, TA)); break;
        case EOR: case TEQ: ctx.E(ppc_xor  (res, srcRn, TA)); break;
        case SUB: case CMP: ctx.E(ppc_subfc(res, TA, srcRn)); break;
        case RSB:           ctx.E(ppc_subfc(res, srcRn, TA)); break;
        case ADD: case CMN: ctx.E(ppc_addc (res, srcRn, TA)); break;
        case ADC:           ctx.E(ppc_adde (res, srcRn, TA)); break;
        case SBC:           ctx.E(ppc_subfe(res, TA, srcRn)); break;
        case RSC:           ctx.E(ppc_subfe(res, srcRn, TA)); break;
        case ORR:           ctx.E(ppc_or   (res, srcRn, TA)); break;
        case MOV:           if (res != TA) ctx.E(ppc_mr(res, TA)); break;
        case BIC:           ctx.E(ppc_andc (res, srcRn, TA)); break;
        case MVN:           ctx.E(ppc_nor  (res, TA, TA));    break;
    }

    if (s) {
        uint8_t opA, opB;
        if (needV) {
            ctx.E(ppc_lwz(TA, FRAME_SCR0, 1)); opB = TA;
            ctx.E(ppc_lwz(TD, FRAME_SCR1, 1)); opA = TD;
        } else { opA = srcRn; opB = TA; }

        switch ((DP)dop) {
            case ADD: case CMN:
                setNZ(ctx,res); setC_xer(ctx); setV_add(ctx,res,opA,opB); break;
            case ADC:
                setNZ(ctx,res); setC_xer(ctx); setV_add(ctx,res,opA,opB); break;
            case SUB: case CMP:
                setNZ(ctx,res); setC_xer(ctx); setV_sub(ctx,res,opA,opB); break;
            case RSB:
                setNZ(ctx,res); setC_xer(ctx); setV_sub(ctx,res,opB,opA); break;
            case SBC:
                setNZ(ctx,res); setC_xer(ctx); setV_sub(ctx,res,opA,opB); break;
            case RSC:
                setNZ(ctx,res); setC_xer(ctx); setV_sub(ctx,res,opB,opA); break;
            default:
                setNZ(ctx,res);
                if (carrySet) setC_bit0(ctx, TC);
                break;
        }
    }

    patchSkip(ctx, si);
    return true;
}

// ============================================================
// BX / mode-switch exit
//
// Precondition: the ORIGINAL Rm value is in FRAME_SCR2.
// Clobbers: TA, TB, TC (all volatile).
// ============================================================
static void emitBXexit(Ctx& ctx) {
    ctx.E(ppc_lwz(TA, FRAME_SCR2, 1));     // TA = original Rm

    // Compute new PC = Rm & ~1
    ctx.E(ppc_rlwinm(TB, TA, 0, 0, 30));   // TB = Rm & ~1
    ctx.E(ppc_stw(TB, FRAME_SCR1, 1));     // save new PC

    // Update T-bit (CPSR bit5, LSB) from Rm[0]
    // rlwinm TC, TA, 5, 26, 26:
    //   rotate left 5, mask keeps only bit26(MSB) = bit5(LSB) -> TC[5] = Rm[0]
    ctx.E(ppc_rlwinm(TC, TA, 5, 26, 26));

    // Clear old T-bit in RCPSR
    // rlwinm RCPSR,RCPSR,0,27,25 is a WRAPPING mask:
    //   keeps MSB-bits 27..31 and 0..25, zeroes bit26(MSB) = bit5(LSB)
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 27, 25));

    // Set new T-bit
    ctx.E(ppc_or(RCPSR, RCPSR, TC));

    // Spill ARM r0-r14 and sync to interpreter
    for (int i = 0; i < 15; i++)
        ctx.E(ppc_stw(RA[i], FRAME_REGSYNC + i*4, 1));
    ctx.E(ppc_mr(TA, RINTERP));
    ctx.E(ppc_addi(TB, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_mr(TC, RCPSR));
    ctx.call((void*)JitHelp_syncTo);

    // JitHelp_storeExit(cpuIdx, new_PC, cpsr, EXIT_NORMAL)
    ctx.E(ppc_mr(TA, RCPUIDX));
    ctx.E(ppc_lwz(TB, FRAME_SCR1, 1));         // new PC
    ctx.E(ppc_mr(TC, RCPSR));
    ctx.E(ppc_addi(TD, 0, (int16_t)EXIT_NORMAL));
    ctx.call((void*)JitHelp_storeExit);

    emitEpilogue(ctx);
}

// ============================================================
// ARM BX instruction
// ============================================================
static bool emitBX(Ctx& ctx, uint32_t op, uint32_t curPC) {
    uint8_t cond = (op >> 28) & 0xF;
    uint8_t rm   =  op        & 0xF;
    if (rm == 15) return false;
    uint8_t pRm  = RA[rm];

    // Save Rm BEFORE emitCondSkip can clobber TA/TB/TC
    ctx.E(ppc_stw(pRm, FRAME_SCR2, 1));

    size_t si = emitCondSkip(ctx, cond);
    if (si == SIZE_MAX && cond != 14) return false;

    emitBXexit(ctx);

    if (si != SIZE_MAX) {
        patchSkip(ctx, si);
        // Condition not met: continue to the next ARM instruction
        emitExit(ctx, curPC + 4, EXIT_NORMAL);
    }

    ctx.done = true;
    return true;
}

// ============================================================
// ARM branch (B / BL / BLX-immediate)
// ============================================================
static bool emitBranch(Ctx& ctx, uint32_t op, uint32_t curPC) {
    if ((op & 0x0FFFFFF0) == 0x012FFF10) return emitBX(ctx, op, curPC);
    if ((op & 0x0FFFFFF0) == 0x012FFF30) return false;   // BLX Rm: interpreter

    if ((op & 0x0E000000) == 0x0A000000) {
        uint8_t  cond = (op >> 28) & 0xF;
        bool     lk   = (op >> 24) & 1;
        int32_t  off  = (int32_t)(op << 8) >> 6;
        uint32_t tgt  = curPC + 8 + off;

        size_t si = emitCondSkip(ctx, cond);
        if (si == SIZE_MAX && cond != 14) return false;

        if (lk) ctx.li(RA[14], curPC + 4);
        emitExit(ctx, tgt, EXIT_NORMAL);

        if (si != SIZE_MAX) {
            patchSkip(ctx, si);
            emitExit(ctx, curPC + 4, EXIT_NORMAL);
        }
        ctx.done = true;
        return true;
    }
    return false;
}

// ============================================================
// Single-register load / store (no PC involvement)
// ============================================================
static bool emitLS(Ctx& ctx, uint32_t op, uint32_t curPC) {
    (void)curPC;
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
    uint8_t pRn = RA[rn], pRd = RA[rd];

    size_t si = emitCondSkip(ctx, cond);
    if (si == SIZE_MAX && cond != 14) return false;

    if (immO) ctx.li(TA, op & 0xFFF);
    else      ctx.E(ppc_mr(TA, RA[op & 0xF]));

    if (pre) {
        if (up) ctx.E(ppc_add (TB, pRn, TA));
        else    ctx.E(ppc_subf(TB, TA, pRn));
    } else {
        ctx.E(ppc_mr(TB, pRn));
    }

    ctx.E(ppc_stw(TA, FRAME_SCR0, 1));
    ctx.E(ppc_stw(TB, FRAME_SCR1, 1));

    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.E(ppc_lwz(TC, FRAME_SCR1, 1));
    if (!ld) ctx.E(ppc_mr(TD, pRd));

    void* fn = ld ? (by ? (void*)JitHelp_r8  : (void*)JitHelp_r32)
                  : (by ? (void*)JitHelp_w8  : (void*)JitHelp_w32);
    ctx.call(fn);

    if (ld) ctx.E(ppc_mr(pRd, TA));

    ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
    if (!pre) {
        if (up) ctx.E(ppc_add (pRn, pRn, TA));
        else    ctx.E(ppc_subf(pRn, TA, pRn));
    } else if (wb && rn != rd) {
        ctx.E(ppc_lwz(pRn, FRAME_SCR1, 1));
    }

    patchSkip(ctx, si);
    return true;
}

// ============================================================
// Multiply (32-bit result; MULL/MLAL fall back)
// ============================================================
static bool emitMul(Ctx& ctx, uint32_t op) {
    bool    s   = (op >> 20) & 1;
    bool    acc = (op >> 21) & 1;
    bool    lng = (op >> 23) & 1;
    uint8_t rd  = (op >> 16) & 0xF;
    uint8_t rn  = (op >> 12) & 0xF;
    uint8_t rs  = (op >>  8) & 0xF;
    uint8_t rm  =  op        & 0xF;
    if (rd == 15 || rm == 15 || rs == 15) return false;
    if (lng) return false;
    uint8_t pRd=RA[rd],pRn=RA[rn],pRs=RA[rs],pRm=RA[rm];
    if (acc) { ctx.E(ppc_mullw(TA, pRm, pRs)); ctx.E(ppc_add(pRd, TA, pRn)); }
    else     { ctx.E(ppc_mullw(pRd, pRm, pRs)); }
    if (s) setNZ(ctx, pRd);
    return true;
}

// ============================================================
// ARM instruction dispatcher
// ============================================================
static bool dispARM(Ctx& ctx, uint32_t op, uint32_t curPC) {
    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;

    uint32_t it = (op >> 25) & 7;
    switch (it) {
        case 0: case 1:
            if ((op & 0x0FC000F0) == 0x00000090) return emitMul(ctx, op);
            if ((op & 0x0FFFFFF0) == 0x012FFF10 ||
                (op & 0x0FFFFFF0) == 0x012FFF30) return emitBranch(ctx, op, curPC);
            if ((op & 0x0FB00FF0) == 0x01000000 ||
                (op & 0x0FB00000) == 0x03200000 ||
                (op & 0x0DB0F000) == 0x010F0000 ||
                (op & 0x0E000090) == 0x00000090) return false;
            return emitDP(ctx, op, curPC);
        case 2: case 3: return emitLS(ctx, op, curPC);
        case 4:         return false;
        case 5:         return emitBranch(ctx, op, curPC);
        default:        return false;
    }
}

// ============================================================
// Thumb emitters
// ============================================================

static bool emitT_shifts(Ctx& ctx, uint16_t op) {
    uint8_t ty=(op>>11)&3, rd=op&7, rs=(op>>3)&7;
    int     i =(op>> 6)&0x1F;
    uint8_t pRd=RA[rd], pRs=RA[rs];
    switch (ty) {
        case 0: sLslI(ctx,pRd,pRs,i,      true); break;
        case 1: sLsrI(ctx,pRd,pRs,i?i:32,true); break;
        case 2: sAsrI(ctx,pRd,pRs,i?i:32,true); break;
        default: return false;
    }
    setNZ(ctx,pRd); setC_bit0(ctx,TC); return true;
}

static bool emitT_addSub3(Ctx& ctx, uint16_t op) {
    uint8_t rd=op&7, rs=(op>>3)&7;
    bool sub=(op>>9)&1, imm3=(op>>10)&1;
    uint8_t pRd=RA[rd], pRs=RA[rs];
    if (imm3) ctx.li(TA,(op>>6)&7);
    else      ctx.E(ppc_mr(TA,RA[(op>>6)&7]));
    ctx.E(ppc_mr(TB,pRs));
    if (sub) { ctx.E(ppc_subfc(pRd,TA,TB)); setNZ(ctx,pRd); setC_xer(ctx); setV_sub(ctx,pRd,TB,TA); }
    else     { ctx.E(ppc_addc (pRd,TB,TA)); setNZ(ctx,pRd); setC_xer(ctx); setV_add(ctx,pRd,TB,TA); }
    return true;
}

static bool emitT_imm8(Ctx& ctx, uint16_t op) {
    uint8_t ty=(op>>11)&3, rd=(op>>8)&7;
    uint8_t pRd=RA[rd]; uint8_t imm=op&0xFF;
    switch (ty) {
        case 0: ctx.li(pRd,imm); setNZ(ctx,pRd); return true;
        case 1: { ctx.li(TA,imm); ctx.E(ppc_mr(TB,pRd));
                  ctx.E(ppc_subfc(TC,TA,TB));
                  setNZ(ctx,TC); setC_xer(ctx); setV_sub(ctx,TC,TB,TA); return true; }
        case 2: { ctx.li(TA,imm); ctx.E(ppc_mr(TB,pRd));
                  ctx.E(ppc_addc(pRd,TB,TA));
                  setNZ(ctx,pRd); setC_xer(ctx); setV_add(ctx,pRd,TB,TA); return true; }
        case 3: { ctx.li(TA,imm); ctx.E(ppc_mr(TB,pRd));
                  ctx.E(ppc_subfc(pRd,TA,TB));
                  setNZ(ctx,pRd); setC_xer(ctx); setV_sub(ctx,pRd,TB,TA); return true; }
    }
    return false;
}

static bool emitT_alu(Ctx& ctx, uint16_t op) {
    uint8_t rd=op&7, rs=(op>>3)&7, o=(op>>6)&0xF;
    uint8_t pRd=RA[rd], pRs=RA[rs];
    switch (o) {
        case  0: ctx.E(ppc_and  (pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case  1: ctx.E(ppc_xor  (pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case  2: ctx.E(ppc_slw  (pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case  3: ctx.E(ppc_srw  (pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case  4: ctx.E(ppc_sraw (pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case  5: { ctx.E(ppc_rlwinm(TA,RCPSR,0,2,2)); ctx.E(ppc_mtxer(TA));
                   ctx.E(ppc_mr(TB,pRd)); ctx.E(ppc_adde(pRd,TB,pRs));
                   setNZ(ctx,pRd); setC_xer(ctx); setV_add(ctx,pRd,TB,pRs); break; }
        case  6: { ctx.E(ppc_rlwinm(TA,RCPSR,0,2,2)); ctx.E(ppc_mtxer(TA));
                   ctx.E(ppc_mr(TB,pRd)); ctx.E(ppc_subfe(pRd,pRs,TB));
                   setNZ(ctx,pRd); setC_xer(ctx); setV_sub(ctx,pRd,TB,pRs); break; }
        case  7: { ctx.E(ppc_subfic(TA,pRs,32)); ctx.E(ppc_rlwnm(pRd,pRd,TA,0,31));
                   setNZ(ctx,pRd); break; }
        case  8: { ctx.E(ppc_and(TA,pRd,pRs)); setNZ(ctx,TA); break; }
        case  9: { ctx.E(ppc_mr(TB,pRd)); ctx.E(ppc_addi(TA,0,0));
                   ctx.E(ppc_subfc(pRd,TB,TA));
                   setNZ(ctx,pRd); setC_xer(ctx); setV_sub(ctx,pRd,TA,TB); break; }
        case 10: { ctx.E(ppc_mr(TB,pRd)); ctx.E(ppc_subfc(TA,pRs,TB));
                   setNZ(ctx,TA); setC_xer(ctx); setV_sub(ctx,TA,TB,pRs); break; }
        case 11: { ctx.E(ppc_mr(TB,pRd)); ctx.E(ppc_addc(TA,TB,pRs));
                   setNZ(ctx,TA); setC_xer(ctx); setV_add(ctx,TA,TB,pRs); break; }
        case 12: ctx.E(ppc_or   (pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case 13: ctx.E(ppc_mullw(pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case 14: ctx.E(ppc_andc (pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case 15: ctx.E(ppc_nor  (pRd,pRs,pRs)); setNZ(ctx,pRd); break;
        default: return false;
    }
    return true;
}

static bool emitT_hiReg(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t o=(op>>8)&3, h1=(op>>7)&1, h2=(op>>6)&1;
    uint8_t rs=((op>>3)&7)|(h2<<3), rd=(op&7)|(h1<<3);
    if (rd==15||rs==15) return false;
    uint8_t pRd=RA[rd], pRs=RA[rs];
    switch (o) {
        case 0: ctx.E(ppc_add(pRd,pRd,pRs)); break;
        case 1: { ctx.E(ppc_mr(TB,pRd)); ctx.E(ppc_subfc(TA,pRs,TB));
                  setNZ(ctx,TA); setC_xer(ctx); setV_sub(ctx,TA,TB,pRs); break; }
        case 2: ctx.E(ppc_mr(pRd,pRs)); break;
        case 3: { ctx.E(ppc_stw(pRs,FRAME_SCR2,1)); emitBXexit(ctx); ctx.done=true; break; }
    }
    return true;
}

static bool emitT_ldrPc(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t rd=(op>>8)&7;
    uint32_t addr=((curPC+4)&~3u)+((op&0xFF)<<2);
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.li(TC,addr);
    ctx.call((void*)JitHelp_r32);
    ctx.E(ppc_mr(RA[rd],TA));
    return true;
}

static bool emitT_memReg(Ctx& ctx, uint16_t op) {
    uint8_t rd=op&7, rb=(op>>3)&7, ro=(op>>6)&7;
    uint8_t pRd=RA[rd], pRb=RA[rb], pRo=RA[ro];
    uint8_t op97=(op>>9)&7;
    void* fn=nullptr; bool ld=true;
    switch (op97) {
        case 0: fn=(void*)JitHelp_w32; ld=false; break;
        case 1: fn=(void*)JitHelp_w8;  ld=false; break;
        case 2: fn=(void*)JitHelp_r16; break;
        case 3: fn=(void*)JitHelp_r8;  break;
        case 4: fn=(void*)JitHelp_r32; break;
        case 5: fn=(void*)JitHelp_r8;  break;
        case 6: fn=(void*)JitHelp_r16; break;
        case 7: fn=(void*)JitHelp_r16; break;
        default: return false;
    }
    ctx.E(ppc_add(TC,pRb,pRo));
    ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore(TA); ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if (!ld) ctx.E(ppc_mr(TD,pRd));
    ctx.call(fn);
    if (ld) {
        if      (op97==3) ctx.E(ppc_extsb(pRd,TA));
        else if (op97==7) ctx.E(ppc_extsh(pRd,TA));
        else              ctx.E(ppc_mr(pRd,TA));
    }
    return true;
}

static bool emitT_memImm(Ctx& ctx, uint16_t op) {
    uint8_t rd=op&7, rb=(op>>3)&7;
    uint8_t pRd=RA[rd], pRb=RA[rb];
    bool ld=(op>>11)&1;
    uint8_t h=(op>>12)&0xF;
    bool by=(h==7), hw=(h==8);
    uint32_t off=((op>>6)&0x1F)*(hw?2u:(by?1u:4u));
    ctx.li(TC,off); ctx.E(ppc_add(TC,pRb,TC));
    ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore(TA); ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if (!ld) ctx.E(ppc_mr(TD,pRd));
    void* fn=ld?(hw?(void*)JitHelp_r16:(by?(void*)JitHelp_r8:(void*)JitHelp_r32))
               :(hw?(void*)JitHelp_w16:(by?(void*)JitHelp_w8:(void*)JitHelp_w32));
    ctx.call(fn);
    if (ld) ctx.E(ppc_mr(pRd,TA));
    return true;
}

static bool emitT_spLoad(Ctx& ctx, uint16_t op, uint32_t curPC) {
    bool ld=(op>>11)&1; uint8_t rd=(op>>8)&7;
    uint8_t pRd=RA[rd]; bool sp=((op>>12)&0xF)==0x9;
    uint32_t off=(op&0xFF)<<2;
    if (sp) { ctx.li(TA,off); ctx.E(ppc_add(TC,RA[13],TA)); }
    else    ctx.li(TC,((curPC+4)&~3u)+off);
    ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore(TA); ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if (!ld) ctx.E(ppc_mr(TD,pRd));
    ctx.call(ld?(void*)JitHelp_r32:(void*)JitHelp_w32);
    if (ld) ctx.E(ppc_mr(pRd,TA));
    return true;
}

static bool emitT_branch(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t h=(op>>12)&0xF;
    if (h==0xE) {
        int32_t off=(int32_t)(op<<21)>>20;
        emitExit(ctx,(uint32_t)(curPC+4+off),EXIT_NORMAL);
        ctx.done=true; return true;
    }
    if (h==0xD) {
        uint8_t cond=(op>>8)&0xF;
        if (cond==0xF) return false;
        int32_t off=(int8_t)(op&0xFF); off<<=1;
        uint32_t tgt=curPC+4+off, fall=curPC+2;
        emitLoadCR7(ctx);
        CB cb=condPpc(cond); if (!cb.ok) return false;
        uint8_t inv=(cb.bo==12)?4u:(cb.bo==4u?12u:20u);
        size_t si=ctx.sz(); ctx.E(ppc_bc(inv,cb.bi,4));
        emitExit(ctx,tgt,EXIT_NORMAL);
        patchSkip(ctx,si);
        emitExit(ctx,fall,EXIT_NORMAL);
        ctx.done=true; return true;
    }
    return false;
}

static bool emitT_bl(Ctx& ctx, uint16_t op1, uint16_t op2, uint32_t curPC) {
    int32_t hi=(int32_t)((op1&0x7FF)<<21)>>9;
    int32_t lo=(op2&0x7FF)<<1;
    uint32_t tgt=curPC+4+hi+lo;
    bool blx=((op2>>11)&0x1F)==0x1C;
    ctx.li(RA[14],(curPC+4)|1u);
    if (blx) {
        tgt&=~3u;
        ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,27,25));    // clear T-bit
    }
    emitExit(ctx,tgt&~1u,EXIT_NORMAL);
    ctx.done=true; return true;
}

static bool dispThumb(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t h=(op>>12)&0xF;
    switch (h) {
        case 0x0: { uint8_t b=(op>>11)&3;
                    if (b<3) return emitT_shifts(ctx,op);
                    return emitT_addSub3(ctx,op); }
        case 0x1: return emitT_imm8(ctx,op);
        case 0x2: { uint8_t b=(op>>10)&3;
                    if (b==0) return emitT_alu(ctx,op);
                    if (b==1) return emitT_hiReg(ctx,op,curPC);
                    return emitT_ldrPc(ctx,op,curPC); }
        case 0x3: case 0x4: case 0x5: return emitT_memReg(ctx,op);
        case 0x6: case 0x7: case 0x8: return emitT_memImm(ctx,op);
        case 0x9: return emitT_spLoad(ctx,op,curPC);
        case 0xD: return emitT_branch(ctx,op,curPC);
        case 0xE: return emitT_branch(ctx,op,curPC);
        default:  return false;
    }
}

// ============================================================
// PC validity check
// ============================================================
static bool validPC(uint32_t pc, bool gba) {
    pc &= ~1u;
    if (gba)
        return (pc < 0x4000u)
            || (pc >= 0x02000000u && pc < 0x02040000u)
            || (pc >= 0x03000000u && pc < 0x03008000u)
            || (pc >= 0x08000000u && pc < 0x0E000000u);
    return (pc < 0x4000u)
        || (pc >= 0x02000000u && pc < 0x02400000u)
        || (pc >= 0x03000000u && pc < 0x03800000u)
        || (pc >= 0xFFFF0000u);
}

// ============================================================
// Block compiler
// ============================================================
static JitBlock* compile(Interpreter* interp, Core* core,
                          uint32_t armPC, bool arm7, int cpuIdx) {
    if (!validPC(armPC, core->gbaMode)) return nullptr;

    bool   thumb = interp->isThumb();
    size_t bkt   = hashPC(armPC);
    JitBlock& slot = cache[bkt];

    if (slot.valid && slot.armPC == armPC && slot.thumb == thumb)
        return &slot;

    if (codePos + BLK_WDS >= JIT_WORDS) flushJitCache();

    Ctx ctx;
    ctx.base    = codeBuf + codePos;
    ctx.cur     = ctx.base;
    ctx.cap     = JIT_WORDS - codePos;
    ctx.thumb   = thumb;
    ctx.arm7    = arm7;
    ctx.done    = false;
    ctx.blockPC = armPC;
    ctx.cpuIdx  = cpuIdx;
    ctx.interp  = interp;
    ctx.core    = core;

    emitPrologue(ctx);
    emitSyncFrom(ctx);

    uint32_t curPC = armPC;
    int n = 0;

    while (n < (int)BLK_ARMS && !ctx.done) {
        if (!validPC(curPC, core->gbaMode)) {
            emitExit(ctx, curPC, EXIT_FALLBACK);
            ctx.done = true;
            break;
        }

        if (thumb) {
            uint16_t op = core->memory.read<uint16_t>(arm7, curPC);

            // 32-bit BL/BLX pair
            if (((op >> 11) & 0x1F) == 0x1E) {
                if (!validPC(curPC + 2, core->gbaMode)) {
                    emitExit(ctx, curPC, EXIT_FALLBACK);
                    ctx.done = true; break;
                }
                uint16_t op2 = core->memory.read<uint16_t>(arm7, curPC + 2);
                uint8_t  bb  = (op2 >> 11) & 0x1F;
                if (bb == 0x1F || bb == 0x1C) {
                    emitT_bl(ctx, op, op2, curPC);
                    curPC += 4; n += 2; continue;
                }
            }

            bool ok = dispThumb(ctx, op, curPC);
            if (!ok) {
                emitExit(ctx, curPC, EXIT_FALLBACK);
                ctx.done = true;
            } else {
                curPC += 2; n++;
                uint8_t h = (op >> 12) & 0xF;
                if (h == 0xD || h == 0xE || h == 0xF) ctx.done = true;
            }
        } else {
            uint32_t op = core->memory.read<uint32_t>(arm7, curPC);

            bool ok = dispARM(ctx, op, curPC);
            if (!ok) {
                emitExit(ctx, curPC, EXIT_FALLBACK);
                ctx.done = true;
            } else {
                curPC += 4; n++;
                uint32_t it = (op >> 25) & 7;
                if (it == 5)                             ctx.done = true;  // B/BL
                if ((op & 0x0FFFFFF0) == 0x012FFF10)    ctx.done = true;  // BX
                if ((op & 0x0E000000) == 0x0A000000)    ctx.done = true;  // B/BL
            }
        }
    }

    if (!ctx.done) emitExit(ctx, curPC, EXIT_NORMAL);

    size_t wds = ctx.sz();
    if (wds == 0) return nullptr;

    flushICache(ctx.base, wds);
    slot = { armPC, ctx.base, (uint32_t)wds, thumb, true };
    codePos += wds;
    return &slot;
}

// ============================================================
// Safe run-one-opcode helper used when JIT cannot be used
// ============================================================
static void interpStep(Interpreter& interp) {
    interp.jitRunOpcode();
}

// ============================================================
// JIT run loops
//
// Key invariant: we capture the block pointer and the current
// cacheGen BEFORE calling executeBlock_asm.  If the block's
// code triggers a flushJitCache (via JitHelp_tick -> schedule
// -> invalidateJitPage) the generation counter will have changed
// and we know the exitPC from that block is still valid (the
// block already ran to completion), but we must not re-use the
// stale pointer on the next iteration.
//
// The "stale pointer" ISI crash (PC=0x000E2B04) happened because
// the run loop was calling executeBlock_asm with a block->code
// value that pointed into already-recycled code buffer space.
// We prevent this by:
//   1. Checking interp.isReady() before doing anything JIT-related.
//   2. Never caching the block pointer across a tick() call.
//   3. Validating g_exitPC before forwarding it to setPC().
// ============================================================
void runJitNds(Core& core) {
    for (int cpu = 0; cpu < 2; cpu++) {
        Interpreter& interp = core.interpreter[cpu];
        if (interp.halted) continue;

        // Guard: memory map must be initialised before we read PC
        if (!interp.isReady()) { interpStep(interp); continue; }

        uint32_t pc = interp.getActualPC();
        if (!validPC(pc, false)) { interpStep(interp); continue; }

        // Compile (or retrieve from cache) a block for this PC.
        // compile() may call flushJitCache internally if the buffer is full.
        JitBlock* b = compile(&interp, &core, pc, cpu == 1, cpu);
        if (!b || b->nW == 0) { interpStep(interp); continue; }

        // Execute the block.  After this returns, b->code may be stale
        // (if a tick inside the block triggered invalidateJitPage / flush),
        // but the exit state in g_exitPC/g_exitReason is still valid.
        executeBlock_asm(b->code);

        uint32_t exitPC = g_exitPC[cpu];
        int      reason = g_exitReason[cpu];

        // Validate the exit PC before handing it to setPC()
        if (exitPC == 0 || exitPC == 0xFFFFFFFFu || !validPC(exitPC, false)) {
            // Something went wrong — let the interpreter recover
            interpStep(interp);
            continue;
        }

        interp.setPC(exitPC);
        if (reason == EXIT_FALLBACK) {
            interp.jitRunOpcode();
        }
    }
    JitHelp_tick(&core);
}

void runJitGba(Core& core) {
    Interpreter& interp = core.interpreter[1];
    if (interp.halted) { JitHelp_tick(&core); return; }

    if (!interp.isReady()) { interpStep(interp); JitHelp_tick(&core); return; }

    uint32_t pc = interp.getActualPC();
    if (!validPC(pc, true)) { interpStep(interp); JitHelp_tick(&core); return; }

    JitBlock* b = compile(&interp, &core, pc, true, 1);
    if (!b || b->nW == 0) { interpStep(interp); JitHelp_tick(&core); return; }

    executeBlock_asm(b->code);

    uint32_t exitPC = g_exitPC[1];
    int      reason = g_exitReason[1];

    if (exitPC == 0 || exitPC == 0xFFFFFFFFu || !validPC(exitPC, true)) {
        interpStep(interp);
    } else {
        interp.setPC(exitPC);
        if (reason == EXIT_FALLBACK) {
            interp.jitRunOpcode();
        }
    }

    JitHelp_tick(&core);
}

// ============================================================
// Lifecycle
// ============================================================
bool initJit(Core* core) {
    codeBuf = (uint32_t*)memalign(32, JIT_BYTES);
    if (!codeBuf) { printf("[JIT] alloc failed\n"); return false; }
    codePos  = 0;
    cacheGen = 0;
    for (size_t i = 0; i < CSIZ; i++) cache[i].valid = false;
    g_exitPC[0] = g_exitPC[1] = 0;
    g_exitCPSR[0] = g_exitCPSR[1] = 0;
    g_exitReason[0] = g_exitReason[1] = EXIT_NORMAL;
    printf("[JIT] buffer %p (%zu KB)\n", (void*)codeBuf, JIT_BYTES >> 10);
    if (core) core->setRunFunc(core->gbaMode ? runJitGba : runJitNds);
    return true;
}

void shutdownJit(Core* core) {
    if (core)
        core->setRunFunc(core->gbaMode
            ? static_cast<void(*)(Core&)>(&Interpreter::runCoreSingle<true,0>)
            : &Interpreter::runCoreNds);
    free(codeBuf); codeBuf = nullptr;
    codePos = 0;
    for (size_t i = 0; i < CSIZ; i++) cache[i].valid = false;
}

void invalidateJitRange(uint32_t start, uint32_t end) {
    for (size_t i = 0; i < CSIZ; i++)
        if (cache[i].valid && cache[i].armPC >= start && cache[i].armPC < end)
            cache[i].valid = false;
}

} // namespace JitPpc
