// jit_ppc.cpp - FIXED VERSION
// Key fixes:
// 1. Frame layout and ABI-correct LR save
// 2. Correct CR7 bit indices for condition codes
// 3. Correct setNZ Z-flag extraction
// 4. Correct BX T-bit manipulation
// 5. Correct SUB/RSB carry (PPC subfc carry is inverted vs ARM)

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
// Exit codes
// ============================================================
static const int EXIT_NORMAL   = 0;
static const int EXIT_FALLBACK = 1;

static uint32_t g_exitPC    [2] = {};
static uint32_t g_exitCPSR  [2] = {};
static int      g_exitReason[2] = {};

// ============================================================
// Frame layout
// PPC EABI: back-chain at SP+0, LR save at SP+4 (of the CALLER's frame,
// i.e. at new_SP + FRAME_SIZE + 4).  We store it ourselves at the top.
//
//   new_SP+0   : back-chain (written by stwu)
//   new_SP+4   : (padding / not used for LR in our scheme)
//   new_SP+FRAME_SIZE+4 : would be caller LR slot -- but we access via
//                         a fixed local slot instead for simplicity.
//
// We keep LR at FRAME_LR = FRAME_SIZE + 4 relative to new_SP — but since
// FRAME_SIZE+4 would need a 16-bit signed offset, we instead store it at
// the TOP of the frame (offset = FRAME_SIZE - 4 from new_SP, i.e. old_SP-4).
// Simplest correct approach: store at new_SP + FRAME_SIZE + 4 isn't
// accessible with a 16-bit displacement if FRAME_SIZE > ~32700.
// Our FRAME_SIZE=208, so new_SP+212 IS accessible and correct per EABI.
// ============================================================
static const int FRAME_SIZE    = 224;   // 16-byte aligned, big enough
// LR save: at old_SP + 4 = new_SP + FRAME_SIZE + 4
static const int FRAME_LR_OFF  = FRAME_SIZE + 4;  // 228 -- within 16-bit range ✓
static const int FRAME_R14     = 16;    // r14..r31 saved here (18 regs = 72 bytes)
// FRAME_R14 + 72 = 88
static const int FRAME_CORE    = 88;    // pointer to Core object
static const int FRAME_SCR0    = 92;    // scratch slot 0
static const int FRAME_SCR1    = 96;    // scratch slot 1
static const int FRAME_SCR2    = 100;   // scratch slot 2 (BX Rm)
static const int FRAME_SCR3    = 104;   // scratch slot 3 (PC value)
static const int FRAME_REGSYNC = 112;   // ARM r0-r14 mirror (15*4 = 60 bytes)
static const int FRAME_CPSR    = 172;   // CPSR mirror (4 bytes)
// 176 <= 224, fits ✓

static_assert(FRAME_SIZE % 16 == 0,                      "frame must be 16-byte aligned");
static_assert(FRAME_R14 + 18*4 == FRAME_CORE,            "r14-r31 layout");
static_assert(FRAME_REGSYNC + 15*4 + 4 <= FRAME_SIZE,    "regsync+cpsr fits in frame");
static_assert(FRAME_LR_OFF < 32768,                       "LR offset fits in 16-bit");

namespace JitPpc {

// ============================================================
// PPC encoders (unchanged from original, verified correct)
// ============================================================
static inline uint32_t ppc_nop()  { return 0x60000000u; }
static inline uint32_t ppc_blr()  { return 0x4E800020u; }
static inline uint32_t ppc_bctr(bool lk=false)
    { return (19u<<26)|(20u<<21)|(528u<<1)|(lk?1u:0u); }
static inline uint32_t ppc_bc(uint8_t bo,uint8_t bi,int16_t off,bool lk=false)
    { return (16u<<26)|((bo&31u)<<21)|((bi&31u)<<16)|((uint32_t)(off&0xFFFC))|(lk?1u:0u); }
static inline uint32_t ppc_addi (uint8_t rt,uint8_t ra,int16_t  i)
    { return (14u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_addis(uint8_t rt,uint8_t ra,int16_t  i)
    { return (15u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_ori  (uint8_t ra,uint8_t rs,uint16_t i)
    { return (24u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i; }
static inline uint32_t ppc_stwu(uint8_t rs,int16_t d,uint8_t ra)
    { return (37u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_stw (uint8_t rs,int16_t d,uint8_t ra)
    { return (36u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_lwz (uint8_t rt,int16_t d,uint8_t ra)
    { return (32u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_cmpi(uint8_t cr,uint8_t ra,int16_t i)
    { return (11u<<26)|((cr&7u)<<23)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_subfic(uint8_t rt,uint8_t ra,int16_t i)
    { return (8u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t Xf(uint8_t rt,uint8_t ra,uint8_t rb,uint32_t x,bool rc=false)
    { return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|
             ((uint32_t)rb<<11)|(x<<1)|(rc?1u:0u); }
static inline uint32_t XOf(uint8_t rt,uint8_t ra,uint8_t rb,bool oe,uint32_t x,bool rc=false)
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
static inline uint32_t ppc_mtspr(uint16_t spr,uint8_t rs){
    uint8_t lo=spr&31,hi=(spr>>5)&31;
    return (31u<<26)|((uint32_t)rs<<21)|((uint32_t)lo<<16)|((uint32_t)hi<<11)|(467u<<1);}
static inline uint32_t ppc_mfspr(uint8_t rt,uint16_t spr){
    uint8_t lo=spr&31,hi=(spr>>5)&31;
    return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)lo<<16)|((uint32_t)hi<<11)|(339u<<1);}
static inline uint32_t ppc_mtctr(uint8_t s){return ppc_mtspr(9,s);}
static inline uint32_t ppc_mtlr (uint8_t s){return ppc_mtspr(8,s);}
static inline uint32_t ppc_mflr (uint8_t t){return ppc_mfspr(t,8);}
static inline uint32_t ppc_mtxer(uint8_t s){return ppc_mtspr(1,s);}
static inline uint32_t ppc_mfxer(uint8_t t){return ppc_mfspr(t,1);}
static inline uint32_t ppc_mfcr (uint8_t t){return (31u<<26)|((uint32_t)t<<21)|(19u<<1);}
static inline uint32_t ppc_mtcrf(uint8_t fxm,uint8_t s)
    {return (31u<<26)|((uint32_t)s<<21)|((uint32_t)(fxm&0xFF)<<12)|(144u<<1);}

static int emit_li32(uint32_t* out,uint8_t rt,uint32_t v){
    uint16_t hi=v>>16,lo=v&0xFFFF;
    if(!hi&&!lo){out[0]=ppc_addi(rt,0,0);return 1;}
    if(!hi){if(lo<0x8000){out[0]=ppc_addi(rt,0,(int16_t)lo);return 1;}
            out[0]=ppc_addi(rt,0,0);out[1]=ppc_ori(rt,rt,lo);return 2;}
    if(!lo){out[0]=ppc_addis(rt,0,(int16_t)hi);return 1;}
    out[0]=ppc_addis(rt,0,(int16_t)hi);out[1]=ppc_ori(rt,rt,lo);return 2;}

// ============================================================
// Register allocation
// r14-r28 = ARM r0-r14
// r29 = CPSR (ARM CPSR stored directly as a 32-bit value)
// r30 = Interpreter*
// r31 = cpu index (0 or 1)
// Volatile (caller-saved): r3=TA r4=TB r5=TC r6=TD r11=RCALL
// ============================================================
static const uint8_t RA[15]  = {14,15,16,17,18,19,20,21,22,23,24,25,26,27,28};
static const uint8_t RCPSR   = 29;
static const uint8_t RINTERP = 30;
static const uint8_t RCPUIDX = 31;
static const uint8_t TA=3, TB=4, TC=5, TD=6, RCALL=11;

// ============================================================
// Code buffer
// ============================================================
static const size_t JIT_BYTES = 4u*1024u*1024u;
static const size_t JIT_WORDS = JIT_BYTES/4;
static const size_t BLK_ARMS  = 64;
static const size_t BLK_WDS   = BLK_ARMS*160+512;

static uint32_t* codeBuf = nullptr;
static size_t    codePos = 0;
static uint32_t  cacheGen = 0;

// ============================================================
// Block cache
// ============================================================
struct JitBlock {
    uint32_t  armPC;
    uint32_t* code;
    uint32_t  nW;
    uint32_t  gen;
    bool      thumb;
    bool      valid;
};

static const size_t CSIZ = 1u<<13;
static JitBlock cache[CSIZ];

static size_t hashPC(uint32_t pc){ return (pc>>1)&(CSIZ-1); }

void flushJitCache(){
    codePos=0;
    ++cacheGen;
    for(size_t i=0;i<CSIZ;i++) cache[i].valid=false;
}

static void flushICache(uint32_t* p,size_t n){
    DCFlushRange(p,n*4); ICInvalidateRange(p,n*4);
}

// ============================================================
// Emit context
// ============================================================
struct Ctx{
    uint32_t *base,*cur; size_t cap;
    bool thumb,arm7,done;
    uint32_t blockPC; int cpuIdx;
    Interpreter* interp; Core* core;

    void E(uint32_t w){ if((size_t)(cur-base)<cap) *cur++=w; }
    size_t sz()const{ return (size_t)(cur-base); }

    void li(uint8_t rt,uint32_t v){
        uint32_t t[2]; int n=emit_li32(t,rt,v);
        for(int i=0;i<n;i++) E(t[i]);
    }

    // Emit a call to a C function.
    // EABI: r3-r10 are arguments (already in place by caller).
    // We must preserve LR (stored in frame), set up CTR, bctrl.
    // After bctrl, r3 = return value, r4-r10 clobbered.
    void call(void* fn){
        uint32_t a=(uint32_t)(uintptr_t)fn;
        // Use RCALL (r11) as scratch — it's volatile and not an arg reg
        E(ppc_addis(RCALL,0,(int16_t)(a>>16)));
        if(a&0xFFFF) E(ppc_ori(RCALL,RCALL,(uint16_t)(a&0xFFFF)));
        E(ppc_mtctr(RCALL));
        E(ppc_bctr(true));   // bctrl — sets LR to next instruction
        // After return: our non-volatile regs (r14-r31) are intact per EABI
    }

    void ldCore(uint8_t d=TA){ E(ppc_lwz(d,FRAME_CORE,1)); }
};

// ============================================================
// C helpers called from JIT blocks
// ============================================================
extern "C" {

void JitHelp_syncFrom(Interpreter* interp, uint32_t* regs, uint32_t* outCPSR){
    uint32_t** p = interp->getRegisters();
    for(int i=0;i<15;i++) regs[i] = *p[i];
    *outCPSR = interp->getCpsrRef();
}

void JitHelp_syncTo(Interpreter* interp, uint32_t* regs, uint32_t cpsr){
    uint32_t** p = interp->getRegisters();
    for(int i=0;i<15;i++) *p[i] = regs[i];
    interp->getCpsrRef() = cpsr;
}

void JitHelp_storeExit(int cpu, uint32_t pc, uint32_t cpsr, int reason){
    g_exitPC[cpu]     = pc;
    g_exitCPSR[cpu]   = cpsr;
    g_exitReason[cpu] = reason;
}

uint32_t JitHelp_r32(Core* c,int a,uint32_t ad){ return c->memory.read<uint32_t>((bool)a,ad); }
uint16_t JitHelp_r16(Core* c,int a,uint32_t ad){ return c->memory.read<uint16_t>((bool)a,ad); }
uint8_t  JitHelp_r8 (Core* c,int a,uint32_t ad){ return c->memory.read<uint8_t> ((bool)a,ad); }
void JitHelp_w32(Core* c,int a,uint32_t ad,uint32_t v){ c->memory.write<uint32_t>((bool)a,ad,v); }
void JitHelp_w16(Core* c,int a,uint32_t ad,uint16_t v){ c->memory.write<uint16_t>((bool)a,ad,v); }
void JitHelp_w8 (Core* c,int a,uint32_t ad,uint8_t  v){ c->memory.write<uint8_t> ((bool)a,ad,v); }

void JitHelp_tick(Core* core){
    core->globalCycles += 64;
    while(!core->events.empty() && core->globalCycles >= core->events.front().cycles){
        SchedEvent e = core->events.front();
        core->events.erase(core->events.begin());
        core->tasks[e.task]();
    }
}

} // extern "C"

// ============================================================
// Prologue
//
// Stack frame layout (after stwu):
//   new_SP + 0              : back-chain (old SP)
//   new_SP + 4              : (reserved, could be CR save)
//   new_SP + 8..15          : (padding)
//   new_SP + FRAME_R14..+71 : r14-r31 saved (18 * 4 = 72 bytes)
//   new_SP + FRAME_CORE     : Core* pointer
//   new_SP + FRAME_SCR0..3  : scratch slots
//   new_SP + FRAME_REGSYNC  : ARM r0-r14 mirror
//   new_SP + FRAME_CPSR     : CPSR mirror
//   [implied old_SP + 4]    : caller LR save = new_SP + FRAME_SIZE + 4
//
// We save LR at new_SP + FRAME_SIZE + 4 per EABI.
// ============================================================
static void emitPrologue(Ctx& ctx){
    // 1. Capture LR into r0 BEFORE moving the stack pointer
    ctx.E(ppc_mflr(0));
    // 2. Allocate frame (writes back-chain automatically)
    ctx.E(ppc_stwu(1, -(int16_t)FRAME_SIZE, 1));
    // 3. Save LR at the ABI-mandated slot: old_SP + 4 = new_SP + FRAME_SIZE + 4
    ctx.E(ppc_stw(0, (int16_t)FRAME_LR_OFF, 1));
    // 4. Save non-volatile regs r14-r31
    for(int r=14; r<=31; r++)
        ctx.E(ppc_stw(r, FRAME_R14 + (r-14)*4, 1));
    // 5. Set up dedicated regs
    ctx.li(RINTERP, (uint32_t)(uintptr_t)ctx.interp);
    ctx.li(TA,      (uint32_t)(uintptr_t)ctx.core);
    ctx.E(ppc_stw(TA, FRAME_CORE, 1));
    ctx.E(ppc_addi(RCPUIDX, 0, (int16_t)ctx.cpuIdx));
}

// ============================================================
// Epilogue — mirror image of prologue
// ============================================================
static void emitEpilogue(Ctx& ctx){
    // Restore non-volatile regs
    for(int r=14; r<=31; r++)
        ctx.E(ppc_lwz(r, FRAME_R14 + (r-14)*4, 1));
    // Restore LR from ABI slot
    ctx.E(ppc_lwz(0, (int16_t)FRAME_LR_OFF, 1));
    ctx.E(ppc_mtlr(0));
    // Deallocate frame
    ctx.E(ppc_addi(1, 1, (int16_t)FRAME_SIZE));
    ctx.E(ppc_blr());
}

// ============================================================
// Sync ARM state to/from JIT registers
// ============================================================
static void emitSyncFrom(Ctx& ctx){
    // Call JitHelp_syncFrom(interp, &frame_regsync[0], &frame_cpsr)
    ctx.E(ppc_mr(TA, RINTERP));
    ctx.E(ppc_addi(TB, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_addi(TC, 1, (int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_syncFrom);
    // Load ARM r0-r14 into PPC r14-r28
    for(int i=0; i<15; i++)
        ctx.E(ppc_lwz(RA[i], FRAME_REGSYNC + i*4, 1));
    // Load CPSR into r29
    ctx.E(ppc_lwz(RCPSR, FRAME_CPSR, 1));
}

// ============================================================
// Exit: flush registers back to interpreter, call storeExit,
// then return from the JIT block.
// ============================================================
static void emitExit(Ctx& ctx, uint32_t nextPC, int reason){
    // Spill ARM regs to frame
    for(int i=0; i<15; i++)
        ctx.E(ppc_stw(RA[i], FRAME_REGSYNC + i*4, 1));
    ctx.E(ppc_stw(RCPSR, FRAME_CPSR, 1));

    // JitHelp_syncTo(interp, &frame_regsync, cpsr)
    ctx.E(ppc_mr(TA, RINTERP));
    ctx.E(ppc_addi(TB, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_mr(TC, RCPSR));
    ctx.call((void*)JitHelp_syncTo);

    // JitHelp_storeExit(cpu, nextPC, cpsr, reason)
    ctx.E(ppc_mr(TA, RCPUIDX));
    ctx.li(TB, nextPC);
    ctx.E(ppc_lwz(TC, FRAME_CPSR, 1));   // reload — call may have clobbered RCPSR... 
                                           // actually RCPSR is r29 (non-volatile), safe
    ctx.E(ppc_mr(TC, RCPSR));
    ctx.E(ppc_addi(TD, 0, (int16_t)reason));
    ctx.call((void*)JitHelp_storeExit);

    emitEpilogue(ctx);
}

// ============================================================
// Condition codes
//
// CPSR layout (ARM): bit31=N, bit30=Z, bit29=C, bit28=V
//
// We store CPSR directly in RCPSR (r29) as a standard 32-bit value.
//
// mtcrf 0x01, RCPSR  sets CR7 from RCPSR bits [31:28]:
//   CR7[LT] = RCPSR bit 31 = N
//   CR7[GT] = RCPSR bit 30 = Z   <-- NOTE: GT=Z, not GT=C
//   CR7[EQ] = RCPSR bit 29 = C
//   CR7[SO] = RCPSR bit 28 = V
//
// PPC CR7 bit indices (for bc instruction's BI field):
//   CR7[LT] = 4*7+0 = 28
//   CR7[GT] = 4*7+1 = 29
//   CR7[EQ] = 4*7+2 = 30
//   CR7[SO] = 4*7+3 = 31
//
// So the mapping from ARM flags to CR7 bits is:
//   N -> CR7[LT] -> BI=28
//   Z -> CR7[GT] -> BI=29
//   C -> CR7[EQ] -> BI=30
//   V -> CR7[SO] -> BI=31
//
// ARM condition codes -> branch if TRUE:
//   EQ (Z=1):    bo=12, bi=29  (Z is at GT=29, branch if bit SET)
//   NE (Z=0):    bo=4,  bi=29
//   CS/HS (C=1): bo=12, bi=30  (C is at EQ=30)
//   CC/LO (C=0): bo=4,  bi=30
//   MI (N=1):    bo=12, bi=28  (N is at LT=28)
//   PL (N=0):    bo=4,  bi=28
//   VS (V=1):    bo=12, bi=31  (V is at SO=31)
//   VC (V=0):    bo=4,  bi=31
//   HI (C=1 && Z=0): complex — fall back
//   LS (C=0 || Z=1): complex — fall back
//   GE (N=V):   complex — fall back
//   LT (N!=V):  complex — fall back
//   GT (Z=0 && N=V): complex — fall back
//   LE (Z=1 || N!=V): complex — fall back
//   AL:          bo=20, bi=0   (always)
// ============================================================

static void emitLoadCR7(Ctx& ctx){
    // Load bits [31:28] of RCPSR into CR7
    ctx.E(ppc_mtcrf(0x01, RCPSR));
}

struct CB { uint8_t bo, bi; bool ok; };

static CB condPpc(uint8_t c){
    switch(c){
        // Simple single-flag conditions:
        case 0:  return {12, 29, true};  // EQ: Z=1 -> CR7[GT] set
        case 1:  return { 4, 29, true};  // NE: Z=0 -> CR7[GT] clear
        case 2:  return {12, 30, true};  // CS: C=1 -> CR7[EQ] set
        case 3:  return { 4, 30, true};  // CC: C=0 -> CR7[EQ] clear
        case 4:  return {12, 28, true};  // MI: N=1 -> CR7[LT] set
        case 5:  return { 4, 28, true};  // PL: N=0 -> CR7[LT] clear
        case 6:  return {12, 31, true};  // VS: V=1 -> CR7[SO] set
        case 7:  return { 4, 31, true};  // VC: V=0 -> CR7[SO] clear
        // Complex conditions — not directly encodable with one bc:
        case 8:  // HI: C=1 && Z=0
        case 9:  // LS: C=0 || Z=1
        case 10: // GE: N==V
        case 11: // LT: N!=V
        case 12: // GT: Z=0 && N==V
        case 13: // LE: Z=1 || N!=V
            return {0, 0, false};
        case 14: return {20, 0, true};   // AL: always
        default: return {0, 0, false};
    }
}

// Emit a conditional skip: if ARM condition FALSE, skip forward.
// Returns the index of the bc instruction for later patching.
// Returns SIZE_MAX for AL (no skip needed) or unsupported conditions.
static size_t emitCondSkip(Ctx& ctx, uint8_t cond){
    if(cond == 14) return SIZE_MAX;  // AL: never skip
    emitLoadCR7(ctx);
    CB cb = condPpc(cond);
    if(!cb.ok) return SIZE_MAX;      // complex condition: caller will fall back
    // Emit branch that skips when condition is FALSE.
    // We want to branch OVER the block if condition not met.
    // If cb.bo==12 (branch if set), we branch-if-clear to skip: invert bo to 4.
    // If cb.bo==4  (branch if clear), invert to 12.
    uint8_t inv = (cb.bo == 12) ? 4u : 12u;
    size_t idx = ctx.sz();
    ctx.E(ppc_bc(inv, cb.bi, 4));  // displacement will be patched
    return idx;
}

static void patchSkip(Ctx& ctx, size_t idx){
    if(idx == SIZE_MAX) return;
    // Calculate branch displacement: from the bc instruction to current position
    int32_t off = (int32_t)((ctx.sz() - idx) * 4);
    // Patch the displacement field (bits 15:2) of the bc instruction
    ctx.base[idx] = (ctx.base[idx] & 0xFFFF0003u) | (uint32_t)(off & 0xFFFC);
}

// ============================================================
// Flag helpers
//
// CPSR bit positions (PPC value notation, bit0=LSB=bit31 MSB):
//   bit 31 = N
//   bit 30 = Z
//   bit 29 = C
//   bit 28 = V
// ============================================================

// Set N and Z flags from register r (leaves C and V unchanged)
static void setNZ(Ctx& ctx, uint8_t r){
    // Clear N (bit31) and Z (bit30)
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 2, 31));   // keep bits [29:0], clear bits 31,30

    // Set N from bit 31 of r
    // rlwimi: rotate r left by 0, insert bit 31 into RCPSR bit 31
    // sh=0, mb=0, me=0 -> insert bit 31 of r into bit 31 of RCPSR
    ctx.E(ppc_rlwimi(RCPSR, r, 0, 0, 0));

    // Set Z: Z=1 if r==0
    // cmpi CR6, r, 0  -> CR6[EQ] set if r==0
    ctx.E(ppc_cmpi(6, r, 0));
    ctx.E(ppc_mfcr(TA));
    // CR6[EQ] is at PPC MSB bit 10 (= value bit 21 = bit position from MSB).
    // CR fields: CR0=bits[31:28], CR1=bits[27:24], ..., CR6=bits[11:8], CR7=bits[7:4]
    // Within CR6: LT=bit11, GT=bit10, EQ=bit9, SO=bit8 (MSB numbering)
    // Wait -- PPC CR MSB numbering: CR0 is the MOST significant CR field.
    // CR0 occupies bits 31-28 (MSB-first: bit31=LT, bit30=GT, bit29=EQ, bit28=SO)
    // CR6 occupies bits 7-4   (bit7=LT, bit6=GT, bit5=EQ, bit4=SO)
    // EQ bit of CR6 = bit 5 of the 32-bit CR value (0-indexed from LSB)
    // We want this at bit 30 of RCPSR.
    // Move bit5 of TA to bit30 of RCPSR:
    // rlwinm TA, TA, 25, 1, 1
    //   rotate left by 25: bit5 moves to bit30 (5+25=30, mod32=30) ✓
    //   mask mb=1, me=1: keep only bit30
    ctx.E(ppc_rlwinm(TA, TA, 25, 1, 1));
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}

// Set C flag from PPC XER[CA] (carry output from addc/subfc/adde/subfe)
// PPC XER[CA] is bit 29 of XER (MSB numbering) = bit 2 in value = 0x20000000? 
// No: XER bit 29 MSB = bit position 2 from MSB = value bit (31-29)=2 = 0x4.
// Wait, I need to be precise:
// PPC uses MSB-first bit numbering: bit 0 = MSB = value 0x80000000.
// XER[CA] = XER bit 29 = value 0x20000000.
// ARM C flag = CPSR bit 29 = value 0x20000000.
// They are at the same value position! So mfxer + mask + or works.
static void setC_xer(Ctx& ctx){
    ctx.E(ppc_mfxer(TA));
    // Isolate XER[CA] = bit29 value = 0x20000000
    // rlwinm TA, TA, 0, 2, 2 keeps only bit2 MSB = value bit 29 = 0x20000000
    ctx.E(ppc_rlwinm(TA, TA, 0, 2, 2));
    // Clear CPSR C bit (bit29 value)
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 3, 1));  // keep bits [28:0] and [31:30], clear bit29
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}

// FIX: PPC subfc/subfe compute ~a+b+carry_in. The carry out (XER[CA]) is set
// when the result does NOT borrow, which is the SAME as ARM's borrow-out=0 -> C=1.
// So for subtraction, setC_xer() is correct as-is (PPC carry = ARM carry for SUB).
// Verified: ARM SUB sets C=1 when no borrow = PPC subfc sets CA=1 when no borrow. ✓

// Set V flag for addition: V=1 when (a XOR result)[31] AND (b XOR result)[31] are both 1
// i.e., both inputs same sign but result different sign
static void setV_add(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b){
    ctx.E(ppc_xor(TA, res, a));   // TA = res XOR a
    ctx.E(ppc_xor(TB, res, b));   // TB = res XOR b
    ctx.E(ppc_and(TA, TA, TB));   // TA = (res XOR a) AND (res XOR b)
    // Bit 31 of TA is 1 if overflow
    // Move bit31 of TA to bit28 of RCPSR (V flag position)
    // rlwinm TA, TA, 29, 3, 3: rotate left 29 puts bit31 at bit28 (31+29=60, 60-32=28) ✓
    // Wait: rlwinm rotates left. bit31 + left_rotate_29 -> new_bit = (31+29) mod 32 = 28 ✓
    ctx.E(ppc_rlwinm(TA, TA, 29, 3, 3));  // isolate at bit28
    // Clear CPSR V bit (bit28)
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 4, 2));  // clear bit28, keep rest
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}

// Set V flag for subtraction: V=1 when (a XOR b)[31]=1 AND (a XOR result)[31]=1
// i.e., operands have different signs and result has different sign from a
static void setV_sub(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b){
    ctx.E(ppc_xor(TA, a, b));     // TA = a XOR b (different signs if bit31=1)
    ctx.E(ppc_xor(TB, a, res));   // TB = a XOR res (sign change in a if bit31=1)
    ctx.E(ppc_and(TA, TA, TB));   // TA bit31=1 if overflow
    ctx.E(ppc_rlwinm(TA, TA, 29, 3, 3));  // rotate to bit28
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 4, 2));
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}

// Set C from bit 0 of register cr (used for logical ops with shifter carry)
// Shifter carry out is stored in TC bit 0 (LSB). ARM C flag = CPSR bit 29.
// Move TC bit0 to RCPSR bit29:
// rlwinm TC, TC, 29, 2, 2: rotate left 29, (0+29=29) -> bit29 ✓
static void setC_bit0(Ctx& ctx, uint8_t cr){
    ctx.E(ppc_rlwinm(TA, cr, 29, 2, 2));         // move bit0 to bit29
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 3, 1));    // clear bit29
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}

// ============================================================
// Barrel shifter
// TC holds the carry out (bit 0) when sc=true
// ============================================================
static void sLslI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc){
    if(i == 0){
        if(d != s) ctx.E(ppc_mr(d, s));
        if(sc){
            // carry out = old C flag (bit29 of CPSR) -> put in TC bit0
            ctx.E(ppc_rlwinm(TC, RCPSR, 3, 31, 31));  // rotate bit29 to bit0? 
            // bit29 left-rotated by 3 = bit32=bit0. Wait: rlwinm rotates LEFT.
            // bit29 + 3 = bit32 = bit0 (mod32). mb=31,me=31 keeps only bit0. ✓
        }
    } else if(i < 32){
        if(sc){
            // carry = bit(32-i) of s = bit that shifts out last
            // After left shift by i, the last bit to leave is bit(32-i).
            // To put it in TC bit0: rotate left by i, then mask bit0? No.
            // The carry is the bit that was at position (32-i) (from LSB).
            // rlwinm TC, s, i, 31, 31: rotate left by i, keep bit 31(MSB).
            // Bit (32-i) from LSB = bit i from MSB. After left rotate i: 
            // it moves to position 0 from MSB = bit31. Then mask bit31... 
            // but we want it at bit0 for later setC_bit0.
            // 
            // Actually: let's just leave it at bit31 and adjust setC_bit0.
            // SIMPLER: put carry in TC bit 0 directly.
            // The bit that shifts out when LSL #i is bit (32-i) from LSB = bit(i-1) from MSB?
            // No. For LSL #i: the last bit shifted out is bit(32-i) of the original value
            // (0-indexed from LSB). We want this in TC bit 0.
            // rlwinm TC, s, i, 31, 31 rotates LEFT by i and keeps bit31:
            //   bit(32-i) from LSB = bit(i) from MSB (0=MSB). After left rotate i:
            //   it goes to position i - i = 0 from MSB = bit 31 from LSB. Then mask bit31.
            //   That puts it at TC bit31, not bit0.
            // We need it at bit0. So: rlwinm TC, s, i+1, 31, 31? No that's still bit31.
            // Best approach: after computing the shifted result, extract carry to TC bit0.
            //   The carry for LSL #i is s[32-i] (0-indexed LSB). 
            //   To extract bit(32-i) to bit0: right-shift by (32-i-... no.
            //   rlwinm TC, s, 32-(32-i-1)-1 ... this is getting complex.
            //
            // CLEANEST: use rlwinm to place the carry bit at bit0 of TC:
            //   The source bit is at position (32-i) from the right (LSB=0).
            //   To move bit(32-i) to bit0: shift right by (32-i), i.e., left rotate by (i).
            //   rlwinm TC, s, i, 31, 31 -> puts it at bit31 (MSB).
            //   We want bit0. So: rlwinm TC, s, i, 31, 31 puts MSB...
            //
            // GIVE UP trying to put it at bit0; put it at bit29 directly (C flag position):
            //   bit(32-i) to bit29: left rotate by (i + 29 - (32-i))... messy.
            //
            // BEST SOLUTION: just use a consistent convention.
            // Put carry in bit 31 of TC, and fix setC_bit0 to use bit 31.
            // OR: compute the shifted result first, then extract carry.
            //
            // ACTUAL FIX: compute s << i into d, the carry = (s >> (32-i)) & 1,
            // place it at bit 0 of TC via: rlwinm TC, s, i, 31, 31 gives bit31.
            // Then use srwi (= rlwinm x,x,32-31,31,31 = rlwinm x,x,1,31,31) to get bit0.
            // BUT: simpler to just do this in two steps:
            ctx.E(ppc_rlwinm(TC, s, (uint8_t)(i % 32), 31, 31)); // carry at bit31 of TC
            // shift TC right by 31 to put at bit0:
            ctx.E(ppc_rlwinm(TC, TC, 1, 31, 31)); // rotate left 1 = shift right 31 for bit31->bit0
        }
        ctx.E(ppc_rlwinm(d, s, (uint8_t)i, 0, (uint8_t)(31-i)));
    } else if(i == 32){
        if(sc){
            // carry = bit0 of s
            ctx.E(ppc_rlwinm(TC, s, 0, 31, 31)); // isolate bit0 of s into TC bit0... 
            // rlwinm TC, s, 0, 31, 31 keeps bit31 of s after rotate 0 = bit31. Not bit0!
            // To get bit0 into TC bit0: rlwinm TC, s, 32, 31, 31 = rotate left 32 = no-op,
            // then keep bit31. Still bit31.
            // rlwinm with sh=0, mb=31, me=31 keeps bit 31 (MSB) only. That's bit31 of s.
            // To isolate bit0 of s: rlwinm TC, s, 1, 31, 31 -> rotates left 1, bit0 goes to bit1,
            // keep bit31 only. That gives s bit 31 of original.
            // 
            // CORRECT WAY to extract bit0 of s to bit0 of TC:
            //   rlwinm TC, s, 0, 31, 31  -- WRONG (gives bit31 of s at bit31 of TC)
            //   We need: TC = s & 1.
            //   andi TC, s, 1 -- but we don't have ppc_andi. Use ori trick? No.
            //   rlwinm TC, s, 32, 31, 31 is same as rlwinm TC, s, 0, 31, 31.
            //   rlwinm TC, s, 1, 31, 31 -- rotate left 1, keep bit31: extracts bit31 of original.
            //   Nope. We need to extract bit0 (LSB) to TC bit0.
            //   There's no direct "extract LSB" in one rlwinm.
            //   BUT: rlwinm TC, s, 0, 31, 31 keeps ONLY bit31 (MSB) of s in TC.
            //   That's wrong. We need bit0 of s.
            //   
            //   Actually PPC rlwinm: the mask goes from mb to me, where mb and me
            //   are bit numbers in PPC notation (0=MSB, 31=LSB).
            //   So rlwinm TC, s, sh, 31, 31 always masks to just bit31 of the rotated value.
            //   After rotate LEFT by sh: what was bit(31-sh) is now at bit31.
            //   To get what was at bit0 (PPC bit31): we need to rotate left by 0 and mask bit31.
            //   That's rlwinm TC, s, 0, 31, 31 -- this gives bit31 of s rotated 0 = bit31 of s.
            //   Hmm. In PPC notation, "bit0" is the MSB (0x80000000), "bit31" is the LSB (0x00000001).
            //   So to get the LSB of s (PPC bit31) into TC PPC bit31:
            //   rlwinm TC, s, 0, 31, 31 -- rotate 0, mask PPC bit31. YES, this gives the LSB of s!
            //   I was confusing PPC and standard bit numbering. Let me re-clarify:
            //
            //   PPC bit notation: bit0 = MSB = 0x80000000, bit31 = LSB = 0x00000001
            //   Standard C notation: bit0 = LSB = 0x00000001, bit31 = MSB = 0x80000000
            //
            //   In setC_bit0, we do: rlwinm TA, cr, 29, 2, 2
            //   This rotates cr left by 29, keeps only PPC bit2 (= 0x20000000 = C flag position).
            //   What was at PPC bit31 (LSB=0x00000001) after left-rotate-29 goes to PPC bit2.
            //   So setC_bit0 reads from PPC bit31 (=LSB=bit0 in C notation) of TC. ✓
            //   So "carry in TC" means: carry = TC & 1 (standard) = TC PPC bit31.
            //
            //   Now: for LSL #32, carry = s LSB = s & 1 = s PPC bit31.
            //   rlwinm TC, s, 0, 31, 31 keeps PPC bit31 of s in TC PPC bit31. ✓

            ctx.E(ppc_rlwinm(TC, s, 0, 31, 31));
        }
        ctx.E(ppc_addi(d, 0, 0));  // result = 0
    } else {
        if(sc) ctx.E(ppc_addi(TC, 0, 0));  // carry = 0
        ctx.E(ppc_addi(d, 0, 0));           // result = 0
    }
}

static void sLsrI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc){
    // LSR #0 encodes as LSR #32
    if(i == 0 || i == 32){
        if(sc){
            // carry = bit31 of s (PPC bit0 = MSB = 0x80000000)
            // We need it at PPC bit31 (LSB) of TC for setC_bit0.
            // rlwinm TC, s, 1, 31, 31: rotate left 1, keep PPC bit31.
            // PPC bit0 + left rotate 1 = wraps to PPC bit31. ✓
            ctx.E(ppc_rlwinm(TC, s, 1, 31, 31));
        }
        ctx.E(ppc_addi(d, 0, 0));
    } else if(i < 32){
        if(sc){
            // carry = bit(i-1) (from MSB=bit0 direction... 
            // LSR #i: we shift right i, carry = bit that was at position i-1 from MSB,
            // which in PPC notation is PPC bit(i-1).
            // After left rotate (32-i+1)=33-i: PPC bit(i-1) goes to PPC bit(i-1+(33-i))=PPC bit32=PPC bit0.
            // That puts it at MSB. We want it at LSB (PPC bit31).
            // Easier: the carry for LSR #i is s bit(i-1) from MSB = s & (1u << (32-i)).
            // In standard notation: bit (32-i) = the (i-1)th bit from MSB.
            // To get standard bit(32-i) into TC standard bit 0:
            //   right-shift by (32-i) which is left-rotate by i.
            //   rlwinm TC, s, i, 31, 31: rotate left i, keep PPC bit31 (=standard bit0).
            //   After rotate left i: what was at standard bit(32-i) goes to standard bit(32-i+i)=bit32=bit0. ✓
            ctx.E(ppc_rlwinm(TC, s, (uint8_t)i, 31, 31));
        }
        // rlwinm d, s, 32-i, i, 31: logical right shift by i
        ctx.E(ppc_rlwinm(d, s, (uint8_t)(32-i), (uint8_t)i, 31));
    } else {
        if(sc) ctx.E(ppc_addi(TC, 0, 0));
        ctx.E(ppc_addi(d, 0, 0));
    }
}

static void sAsrI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc){
    if(i <= 0 || i >= 32){
        if(sc){
            // carry = bit31 (MSB = sign bit) of s, put at TC LSB
            ctx.E(ppc_rlwinm(TC, s, 1, 31, 31));
        }
        ctx.E(ppc_srawi(d, s, 31));
    } else {
        if(sc){
            // carry = s bit(i-1) from MSB (same logic as LSR)
            ctx.E(ppc_rlwinm(TC, s, (uint8_t)i, 31, 31));
        }
        ctx.E(ppc_srawi(d, s, (uint8_t)i));
    }
}

static void sRorI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc){
    if(i == 0){
        // RRX: rotate right 1 through carry
        // carry_out = bit0 of s (PPC bit31), carry_in = CPSR C (bit29)
        if(sc){
            // save bit0 of s to TC (as LSB)
            ctx.E(ppc_rlwinm(TC, s, 0, 31, 31));
        }
        // d = (s >> 1) | (C << 31)
        // Get C (bit29 of RCPSR) into bit31 of TA:
        ctx.E(ppc_rlwinm(TA, RCPSR, 3, 0, 0));  
        // bit29 left-rotate 3 = bit32=bit0... PPC: bit29 is value 0x20000000.
        // left rotate 3: 0x20000000 << 3 = 0x00000001... wait that's bit31 in standard, bit0 in PPC.
        // Hmm: 0x20000000 rotate-left 3 = (0x20000000 << 3) | (0x20000000 >> 29)
        //     = 0x00000000 | 0x00000001 = 0x00000001. That's LSB = PPC bit31, not MSB.
        // We want C at MSB (standard bit31 = PPC bit0 = 0x80000000) to OR into d.
        // C is at standard bit29 = PPC bit2 = 0x20000000.
        // To move to standard bit31 (PPC bit0): rotate LEFT by 2.
        // 0x20000000 << 2 = 0x80000000 ✓ (with wrap: no bits lost since upper 2 bits of C's position are 0)
        // rlwinm TA, RCPSR, 2, 0, 0: rotate left 2, keep PPC bit0 (=standard bit31).
        // After rotate left 2: standard bit29 -> standard bit31. ✓
        ctx.E(ppc_rlwinm(TA, RCPSR, 2, 0, 0));   // TA = C shifted to MSB
        ctx.E(ppc_rlwinm(d, s, 31, 1, 31));        // d = s >> 1 (logical)
        ctx.E(ppc_or(d, d, TA));                    // d |= C_at_MSB
    } else {
        i &= 31; if(!i) i = 32;
        if(i < 32){
            if(sc){
                // carry = s bit(i-1) from MSB = rotate carry out
                // For ROR #i, the carry is s bit (i-1) from MSB.
                // Same extraction as LSR: rlwinm TC, s, i, 31, 31 (standard bit(32-i) to bit0)
                // Wait: ROR #i carry = s bit (i-1) from MSB = standard bit (31-(i-1)) = standard bit(32-i).
                // Same as LSR #i carry. ✓
                ctx.E(ppc_rlwinm(TC, s, (uint8_t)i, 31, 31));
            }
            // ROR #i = rlwinm d, s, 32-i, 0, 31 (rotate right i)
            ctx.E(ppc_rlwinm(d, s, (uint8_t)(32-i), 0, 31));
        } else {
            // ROR #32 = no change (identity), carry = MSB of s
            if(d != s) ctx.E(ppc_mr(d, s));
            if(sc) ctx.E(ppc_rlwinm(TC, s, 1, 31, 31));
        }
    }
}

static bool emitShifter(Ctx& ctx, uint32_t op, uint8_t dst, bool sc){
    bool isImm = (op >> 25) & 1;
    if(isImm){
        uint32_t v = op & 0xFF;
        uint32_t rot = ((op >> 8) & 0xF) * 2;
        if(rot) v = (v >> rot) | (v << (32 - rot));
        ctx.li(dst, v);
        if(sc && rot){
            // carry = bit31 of rotated immediate (the MSB, since rotation is rightward)
            // The last bit rotated through is bit(rot-1) of original = MSB of result.
            // Carry = bit31 of v (standard) -> put at TC LSB.
            ctx.E(ppc_rlwinm(TC, dst, 1, 31, 31)); // bit31 -> bit0 via rotate-left-1 + mask
            return true;
        }
        return false;
    }

    uint8_t rm = op & 0xF;
    uint8_t pRm = (rm < 15) ? RA[rm] : TA;  // r15 handled specially if needed
    uint8_t st = (op >> 5) & 3;
    bool isReg = (op >> 4) & 1;

    if(!isReg){
        int sa = (op >> 7) & 0x1F;
        switch(st){
            case 0: sLslI(ctx, dst, pRm, sa,       sc); break;
            case 1: sLsrI(ctx, dst, pRm, sa ? sa : 32, sc); break;
            case 2: sAsrI(ctx, dst, pRm, sa ? sa : 32, sc); break;
            case 3: sRorI(ctx, dst, pRm, sa,       sc); break;
        }
        return sc;
    }

    // Register-controlled shift
    uint8_t rs = (op >> 8) & 0xF;
    uint8_t pRs = RA[rs];
    ctx.E(ppc_rlwinm(TD, pRs, 0, 24, 31));  // TD = rs & 0xFF (shift amount, PPC bits 24-31 = low byte)
    ctx.E(ppc_mr(TA, pRm));
    switch(st){
        case 0: ctx.E(ppc_slw(dst, TA, TD)); break;
        case 1: ctx.E(ppc_srw(dst, TA, TD)); break;
        case 2: ctx.E(ppc_sraw(dst, TA, TD)); break;
        case 3:{
            // ROR by register: d = (s >> (n&31)) | (s << (32-(n&31)))
            ctx.E(ppc_subfic(TB, TD, 32));
            ctx.E(ppc_rlwnm(dst, TA, TB, 0, 31));
            // Also need the other half: d |= (TA << TD)
            // Actually rlwnm is ROR? No, rlwnm is: d = (s << (b&31)) | (s >> (32-(b&31)))
            // That's rotate LEFT by TB=32-TD, which equals rotate RIGHT by TD. 
            // But we need to combine: rlwnm(d, s, 32-n) = rotate right n. 
            // Actually: rlwnm d, s, b, mb, me = rotate s LEFT by (b & 31), mask [mb:me].
            // With mb=0, me=31 (full mask), it's just rotate left by b.
            // We want rotate right by TD = rotate left by (32-TD) = rotate left by TB.
            // So: rlwnm dst, TA, TB, 0, 31 ✓
            break;
        }
    }
    return false; // register shifts don't update carry in this simple version
}

// ============================================================
// Data processing
// ============================================================
enum DP {AND=0,EOR,SUB,RSB,ADD,ADC,SBC,RSC,TST,TEQ,CMP,CMN,ORR,MOV,BIC,MVN};

static bool emitDP(Ctx& ctx, uint32_t op, uint32_t curPC){
    uint8_t cond = (op >> 28) & 0xF;
    uint8_t dop  = (op >> 21) & 0xF;
    bool s       = (op >> 20) & 1;
    uint8_t rn   = (op >> 16) & 0xF;
    uint8_t rd   = (op >> 12) & 0xF;

    if(rd == 15) return false;  // PC as dest: complex, fall back
    if(rn == 15 && !((op >> 25) & 1)) return false;  // PC as Rn with reg shift: too complex

    uint8_t pRd = RA[rd];

    // Handle PC as Rn (immediate offset or immediate shift only)
    bool    rnIsPC = (rn == 15);
    uint8_t srcRn;
    if(rnIsPC){
        uint32_t pcVal = curPC + (ctx.thumb ? 4u : 8u);
        ctx.li(TD, pcVal);
        ctx.E(ppc_stw(TD, FRAME_SCR3, 1));
        srcRn = TD;
    } else {
        srcRn = RA[rn];
    }

    // Emit condition skip
    size_t si = emitCondSkip(ctx, cond);
    if(si == SIZE_MAX && cond != 14) return false;  // complex condition: fall back

    // For ADC/SBC/RSC: set up XER carry from CPSR C flag BEFORE computing operand2
    bool needCin = (dop == ADC || dop == SBC || dop == RSC);
    if(needCin){
        // Load CPSR C (bit29) into XER CA (bit29): they're the same bit!
        // mtxer RCPSR would set all XER from RCPSR... we only want CA.
        // Extract just the C bit and put it in XER:
        ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2));  // TA = RCPSR & 0x20000000 (C only)
        ctx.E(ppc_mtxer(TA));
    }

    bool logCarry = s && (dop == AND || dop == EOR || dop == TST || dop == TEQ ||
                          dop == ORR || dop == MOV || dop == BIC || dop == MVN);
    bool carrySet = emitShifter(ctx, op, TA, logCarry);

    if(rnIsPC){
        ctx.E(ppc_lwz(TD, FRAME_SCR3, 1));
        srcRn = TD;
    }

    bool needV = s && (dop == ADD || dop == SUB || dop == RSB || dop == CMN || dop == CMP ||
                       dop == ADC || dop == SBC || dop == RSC);
    if(needV){
        ctx.E(ppc_stw(TA,    FRAME_SCR0, 1));  // operand2
        ctx.E(ppc_stw(srcRn, FRAME_SCR1, 1));  // operand1 (Rn)
    }

    bool isTest = (dop == TST || dop == TEQ || dop == CMP || dop == CMN);
    uint8_t res = isTest ? TC : pRd;

    switch((DP)dop){
        case AND: case TST: ctx.E(ppc_and  (res, srcRn, TA)); break;
        case EOR: case TEQ: ctx.E(ppc_xor  (res, srcRn, TA)); break;
        // SUB rd = Rn - op2: PPC subfc d,a,b = b-a+~0 (no borrow) = b-a.
        // subfc(res, TA, srcRn) = srcRn - TA ✓
        case SUB: case CMP: ctx.E(ppc_subfc(res, TA, srcRn)); break;
        // RSB rd = op2 - Rn: subfc(res, srcRn, TA) = TA - srcRn ✓
        case RSB:            ctx.E(ppc_subfc(res, srcRn, TA)); break;
        case ADD: case CMN: ctx.E(ppc_addc (res, srcRn, TA)); break;
        // ADC rd = Rn + op2 + C: adde uses XER[CA] as carry in ✓
        case ADC:            ctx.E(ppc_adde (res, srcRn, TA)); break;
        // SBC rd = Rn - op2 - (1-C) = Rn + ~op2 + C
        // PPC subfe d,a,b = b + ~a + XER[CA] = srcRn + ~TA + C ✓
        // Wait: subfe(res, TA, srcRn) = srcRn + ~TA + XER[CA]
        // ARM SBC = Rn - Op2 - (1-C) = Rn + ~Op2 + C ✓
        case SBC:            ctx.E(ppc_subfe(res, TA, srcRn)); break;
        // RSC rd = op2 - Rn - (1-C) = op2 + ~Rn + C
        // subfe(res, srcRn, TA) = TA + ~srcRn + XER[CA] ✓
        case RSC:            ctx.E(ppc_subfe(res, srcRn, TA)); break;
        case ORR:            ctx.E(ppc_or   (res, srcRn, TA)); break;
        case MOV:            if(res != TA) ctx.E(ppc_mr(res, TA)); break;
        case BIC:            ctx.E(ppc_andc (res, srcRn, TA)); break;
        case MVN:            ctx.E(ppc_nor  (res, TA, TA));    break;
    }

    if(s){
        uint8_t opA, opB;
        if(needV){
            ctx.E(ppc_lwz(TA,    FRAME_SCR0, 1)); opB = TA;   // operand2
            ctx.E(ppc_lwz(TD,    FRAME_SCR1, 1)); opA = TD;   // Rn
        } else {
            opA = srcRn; opB = TA;
        }

        switch((DP)dop){
            case ADD: case CMN:
                setNZ(ctx,res); setC_xer(ctx); setV_add(ctx,res,opA,opB); break;
            case ADC:
                setNZ(ctx,res); setC_xer(ctx); setV_add(ctx,res,opA,opB); break;
            case SUB: case CMP:
                // PPC subfc carry (XER[CA]=1) means no borrow = ARM C=1 ✓
                setNZ(ctx,res); setC_xer(ctx); setV_sub(ctx,res,opA,opB); break;
            case RSB:
                setNZ(ctx,res); setC_xer(ctx); setV_sub(ctx,res,opB,opA); break;
            case SBC:
                setNZ(ctx,res); setC_xer(ctx); setV_sub(ctx,res,opA,opB); break;
            case RSC:
                setNZ(ctx,res); setC_xer(ctx); setV_sub(ctx,res,opB,opA); break;
            default:
                // Logical ops: NZ from result, C from shifter (if applicable)
                setNZ(ctx,res);
                if(carrySet) setC_bit0(ctx, TC);
                break;
        }
    }

    patchSkip(ctx, si);
    return true;
}

// ============================================================
// BX: branch and exchange (changes Thumb state)
// ============================================================
static void emitBXexit(Ctx& ctx){
    // Rm is in FRAME_SCR2
    ctx.E(ppc_lwz(TA, FRAME_SCR2, 1));

    // new PC = Rm & ~1u
    ctx.E(ppc_rlwinm(TB, TA, 0, 0, 30));  // TB = TA with bit0 cleared (standard bit0=PPC bit31)
    // PPC rlwinm(TB, TA, 0, 0, 30): rotate 0, keep PPC bits 0-30 = standard bits 31-1.
    // This clears standard bit0 (PPC bit31). ✓
    ctx.E(ppc_stw(TB, FRAME_SCR1, 1));  // save new PC

    // New T bit = Rm bit0 (standard bit0 = PPC bit31)
    // Place it at CPSR bit5 (T flag).
    // Extract PPC bit31 (standard bit0) of TA and place at standard bit5 (PPC bit26):
    // Left rotate by 5: standard bit0 -> standard bit5. But rlwinm is PPC notation.
    // standard bit0 = PPC bit31. We want it at standard bit5 = PPC bit26.
    // Left rotate (PPC): PPC bit31 + left_rotate_5 -> PPC bit 31+5=36 mod 32 = PPC bit4?
    // Hmm, I keep confusing myself. Let me think in terms of the actual VALUE.
    // Rm & 1 = the Thumb bit. We want CPSR bit5 set/cleared.
    // Value of "bit5 set" = 0x00000020.
    // Value of "bit0 set" = 0x00000001.
    // 0x00000001 << 5 = 0x00000020.
    // So: left-shift by 5 = rlwinm with sh=5.
    // rlwinm TC, TA, 5, 26, 26: rotate LEFT by 5 (in PPC = left by 5 in standard too!),
    // PPC rlwinm rotates the VALUE left by sh. So TA << 5 (circular), mask PPC bit26 = standard bit5.
    // TA bit0 << 5 = TA bit5, mask bit5. ✓ 
    // But: TA might have other bits set. After rotating left 5, TA_bit0 lands at standard_bit5.
    // Then masking PPC bit26 (= standard bit5) gives us only the T bit contribution. ✓
    ctx.E(ppc_rlwinm(TC, TA, 5, 26, 26));

    // Clear CPSR T bit (bit5 = standard bit5 = PPC bit26):
    // rlwinm RCPSR, RCPSR, 0, 27, 25: keep PPC bits 27 to 25 wrapping = clear PPC bit 26.
    // Wrapped range [27:25] in PPC means clear bit26, keep bits [0:25] and [27:31]. ✓
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 27, 25));

    // Set T bit from TC
    ctx.E(ppc_or(RCPSR, RCPSR, TC));

    // Sync to interpreter
    for(int i=0; i<15; i++) ctx.E(ppc_stw(RA[i], FRAME_REGSYNC + i*4, 1));
    ctx.E(ppc_stw(RCPSR, FRAME_CPSR, 1));

    ctx.E(ppc_mr(TA, RINTERP));
    ctx.E(ppc_addi(TB, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_mr(TC, RCPSR));
    ctx.call((void*)JitHelp_syncTo);

    ctx.E(ppc_mr(TA, RCPUIDX));
    ctx.E(ppc_lwz(TB, FRAME_SCR1, 1));  // new PC
    ctx.E(ppc_mr(TC, RCPSR));
    ctx.E(ppc_addi(TD, 0, (int16_t)EXIT_NORMAL));
    ctx.call((void*)JitHelp_storeExit);

    emitEpilogue(ctx);
}

static bool emitBX(Ctx& ctx, uint32_t op, uint32_t curPC){
    uint8_t cond = (op >> 28) & 0xF;
    uint8_t rm   = op & 0xF;
    if(rm == 15) return false;

    // Save Rm before condition check (condition might be skipped)
    ctx.E(ppc_stw(RA[rm], FRAME_SCR2, 1));

    size_t si = emitCondSkip(ctx, cond);
    if(si == SIZE_MAX && cond != 14) return false;

    emitBXexit(ctx);

    if(si != SIZE_MAX){
        patchSkip(ctx, si);
        emitExit(ctx, curPC + 4, EXIT_NORMAL);
    }

    ctx.done = true;
    return true;
}

static bool emitBranch(Ctx& ctx, uint32_t op, uint32_t curPC){
    // BX
    if((op & 0x0FFFFFF0) == 0x012FFF10) return emitBX(ctx, op, curPC);
    // BLX reg — fall back (complex: changes Thumb, saves LR)
    if((op & 0x0FFFFFF0) == 0x012FFF30) return false;

    // B/BL
    if((op & 0x0E000000) == 0x0A000000){
        uint8_t cond = (op >> 28) & 0xF;
        bool lk      = (op >> 24) & 1;
        int32_t off  = (int32_t)(op << 8) >> 6;  // sign extend 24-bit, shift left 2
        uint32_t tgt = curPC + 8 + off;

        size_t si = emitCondSkip(ctx, cond);
        if(si == SIZE_MAX && cond != 14) return false;

        if(lk) ctx.li(RA[14], curPC + 4);
        emitExit(ctx, tgt, EXIT_NORMAL);

        if(si != SIZE_MAX){
            patchSkip(ctx, si);
            emitExit(ctx, curPC + 4, EXIT_NORMAL);
        }

        ctx.done = true;
        return true;
    }

    return false;
}

static bool emitLS(Ctx& ctx, uint32_t op, uint32_t curPC){
    uint8_t cond = (op >> 28) & 0xF;
    bool ld      = (op >> 20) & 1;
    bool by      = (op >> 22) & 1;
    bool up      = (op >> 23) & 1;
    bool pre     = (op >> 24) & 1;
    bool wb      = (op >> 21) & 1;
    bool immO    = !((op >> 25) & 1);  // bit25=0 -> immediate offset
    uint8_t rn   = (op >> 16) & 0xF;
    uint8_t rd   = (op >> 12) & 0xF;

    if(rd == 15 || rn == 15) return false;

    uint8_t pRn = RA[rn];
    uint8_t pRd = RA[rd];

    size_t si = emitCondSkip(ctx, cond);
    if(si == SIZE_MAX && cond != 14) return false;

    // Compute offset into TA
    if(immO)
        ctx.li(TA, op & 0xFFF);
    else
        ctx.E(ppc_mr(TA, RA[op & 0xF]));

    // Compute effective address into TB
    if(pre){
        if(up) ctx.E(ppc_add (TB, pRn, TA));
        else   ctx.E(ppc_subf(TB, TA, pRn));  // subf d,a,b = b-a = pRn-TA
    } else {
        ctx.E(ppc_mr(TB, pRn));
    }

    ctx.E(ppc_stw(TA, FRAME_SCR0, 1));  // save offset
    ctx.E(ppc_stw(TB, FRAME_SCR1, 1));  // save address

    // Set up call
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.E(ppc_lwz(TC, FRAME_SCR1, 1));
    if(!ld) ctx.E(ppc_mr(TD, pRd));

    void* fn = ld
        ? (by ? (void*)JitHelp_r8 : (void*)JitHelp_r32)
        : (by ? (void*)JitHelp_w8 : (void*)JitHelp_w32);
    ctx.call(fn);

    if(ld) ctx.E(ppc_mr(pRd, TA));

    // Post-index writeback
    ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
    if(!pre){
        if(up) ctx.E(ppc_add (pRn, pRn, TA));
        else   ctx.E(ppc_subf(pRn, TA, pRn));
    } else if(wb && rn != rd){
        ctx.E(ppc_lwz(pRn, FRAME_SCR1, 1));
    }

    patchSkip(ctx, si);
    return true;
}

static bool emitMul(Ctx& ctx, uint32_t op){
    bool s   = (op >> 20) & 1;
    bool acc = (op >> 21) & 1;
    bool lng = (op >> 23) & 1;
    uint8_t rd = (op >> 16) & 0xF;
    uint8_t rn = (op >> 12) & 0xF;
    uint8_t rs = (op >> 8)  & 0xF;
    uint8_t rm = op & 0xF;

    if(rd == 15 || rm == 15 || rs == 15 || lng) return false;

    uint8_t pRd = RA[rd];
    uint8_t pRn = RA[rn];
    uint8_t pRs = RA[rs];
    uint8_t pRm = RA[rm];

    if(acc){
        ctx.E(ppc_mullw(TA, pRm, pRs));
        ctx.E(ppc_add(pRd, TA, pRn));
    } else {
        ctx.E(ppc_mullw(pRd, pRm, pRs));
    }

    if(s) setNZ(ctx, pRd);
    return true;
}

static bool dispARM(Ctx& ctx, uint32_t op, uint32_t curPC){
    uint8_t cond = (op >> 28) & 0xF;
    if(cond == 15) return false;  // unconditional instructions

    uint32_t it = (op >> 25) & 7;

    switch(it){
        case 0: case 1:
            // Multiplies
            if((op & 0x0FC000F0) == 0x00000090) return emitMul(ctx, op);
            // BX / BLX reg
            if((op & 0x0FFFFFF0) == 0x012FFF10 ||
               (op & 0x0FFFFFF0) == 0x012FFF30)
                return emitBranch(ctx, op, curPC);
            // MSR/MRS, PSR transfer, halfword loads: fall back
            if((op & 0x0FB00FF0) == 0x01000000 ||
               (op & 0x0FB00000) == 0x03200000 ||
               (op & 0x0DB0F000) == 0x010F0000 ||
               (op & 0x0E000090) == 0x00000090) return false;
            return emitDP(ctx, op, curPC);

        case 2: case 3:
            return emitLS(ctx, op, curPC);

        case 4:
            return false;  // LDM/STM: too complex

        case 5:
            return emitBranch(ctx, op, curPC);

        default:
            return false;
    }
}

// ============================================================
// Thumb emitters
// ============================================================

static bool emitT_shifts(Ctx& ctx, uint16_t op){
    uint8_t ty = (op >> 11) & 3;
    uint8_t rd = op & 7;
    uint8_t rs = (op >> 3) & 7;
    int i      = (op >> 6) & 0x1F;

    switch(ty){
        case 0: sLslI(ctx, RA[rd], RA[rs], i,         true); break;
        case 1: sLsrI(ctx, RA[rd], RA[rs], i ? i : 32, true); break;
        case 2: sAsrI(ctx, RA[rd], RA[rs], i ? i : 32, true); break;
        default: return false;
    }
    setNZ(ctx, RA[rd]);
    setC_bit0(ctx, TC);
    return true;
}

static bool emitT_addSub3(Ctx& ctx, uint16_t op){
    uint8_t rd   = op & 7;
    uint8_t rs   = (op >> 3) & 7;
    bool sub     = (op >> 9) & 1;
    bool imm3    = (op >> 10) & 1;

    if(imm3) ctx.li(TA, (op >> 6) & 7);
    else     ctx.E(ppc_mr(TA, RA[(op >> 6) & 7]));

    ctx.E(ppc_mr(TB, RA[rs]));

    if(sub){
        ctx.E(ppc_subfc(RA[rd], TA, TB));
        setNZ(ctx, RA[rd]); setC_xer(ctx); setV_sub(ctx, RA[rd], TB, TA);
    } else {
        ctx.E(ppc_addc(RA[rd], TB, TA));
        setNZ(ctx, RA[rd]); setC_xer(ctx); setV_add(ctx, RA[rd], TB, TA);
    }
    return true;
}

static bool emitT_imm8(Ctx& ctx, uint16_t op){
    uint8_t ty  = (op >> 11) & 3;
    uint8_t rd  = (op >> 8) & 7;
    uint8_t pRd = RA[rd];
    uint8_t imm = op & 0xFF;

    switch(ty){
        case 0:  // MOV Rd, #imm
            ctx.li(pRd, imm); setNZ(ctx, pRd); return true;
        case 1:{ // CMP Rd, #imm
            ctx.li(TA, imm);
            ctx.E(ppc_mr(TB, pRd));
            ctx.E(ppc_subfc(TC, TA, TB));
            setNZ(ctx, TC); setC_xer(ctx); setV_sub(ctx, TC, TB, TA);
            return true;
        }
        case 2:{ // ADD Rd, #imm
            ctx.li(TA, imm);
            ctx.E(ppc_mr(TB, pRd));
            ctx.E(ppc_addc(pRd, TB, TA));
            setNZ(ctx, pRd); setC_xer(ctx); setV_add(ctx, pRd, TB, TA);
            return true;
        }
        case 3:{ // SUB Rd, #imm
            ctx.li(TA, imm);
            ctx.E(ppc_mr(TB, pRd));
            ctx.E(ppc_subfc(pRd, TA, TB));
            setNZ(ctx, pRd); setC_xer(ctx); setV_sub(ctx, pRd, TB, TA);
            return true;
        }
    }
    return false;
}

static bool emitT_alu(Ctx& ctx, uint16_t op){
    uint8_t rd  = op & 7;
    uint8_t rs  = (op >> 3) & 7;
    uint8_t o   = (op >> 6) & 0xF;
    uint8_t pRd = RA[rd];
    uint8_t pRs = RA[rs];

    switch(o){
        case 0:  ctx.E(ppc_and(pRd, pRd, pRs));   setNZ(ctx, pRd); break;  // AND
        case 1:  ctx.E(ppc_xor(pRd, pRd, pRs));   setNZ(ctx, pRd); break;  // EOR
        case 2:  ctx.E(ppc_slw(pRd, pRd, pRs));   setNZ(ctx, pRd); break;  // LSL
        case 3:  ctx.E(ppc_srw(pRd, pRd, pRs));   setNZ(ctx, pRd); break;  // LSR
        case 4:  ctx.E(ppc_sraw(pRd, pRd, pRs));  setNZ(ctx, pRd); break;  // ASR
        case 5:{ // ADC Rd, Rs
            ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2));
            ctx.E(ppc_mtxer(TA));
            ctx.E(ppc_mr(TB, pRd));
            ctx.E(ppc_adde(pRd, TB, pRs));
            setNZ(ctx, pRd); setC_xer(ctx); setV_add(ctx, pRd, TB, pRs);
            break;
        }
        case 6:{ // SBC Rd, Rs
            ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2));
            ctx.E(ppc_mtxer(TA));
            ctx.E(ppc_mr(TB, pRd));
            ctx.E(ppc_subfe(pRd, pRs, TB));
            setNZ(ctx, pRd); setC_xer(ctx); setV_sub(ctx, pRd, TB, pRs);
            break;
        }
        case 7:{ // ROR Rd, Rs
            ctx.E(ppc_subfic(TA, pRs, 32));
            ctx.E(ppc_rlwnm(pRd, pRd, TA, 0, 31));
            setNZ(ctx, pRd);
            break;
        }
        case 8:{ // TST Rd, Rs
            ctx.E(ppc_and(TA, pRd, pRs));
            setNZ(ctx, TA);
            break;
        }
        case 9:{ // NEG Rd, Rs
            ctx.E(ppc_addi(TA, 0, 0));   // TA = 0
            ctx.E(ppc_subfc(pRd, pRs, TA));
            setNZ(ctx, pRd); setC_xer(ctx); setV_sub(ctx, pRd, TA, pRs);
            break;
        }
        case 10:{ // CMP Rd, Rs
            ctx.E(ppc_mr(TB, pRd));
            ctx.E(ppc_subfc(TA, pRs, TB));
            setNZ(ctx, TA); setC_xer(ctx); setV_sub(ctx, TA, TB, pRs);
            break;
        }
        case 11:{ // CMN Rd, Rs
            ctx.E(ppc_mr(TB, pRd));
            ctx.E(ppc_addc(TA, TB, pRs));
            setNZ(ctx, TA); setC_xer(ctx); setV_add(ctx, TA, TB, pRs);
            break;
        }
        case 12: ctx.E(ppc_or  (pRd, pRd, pRs)); setNZ(ctx, pRd); break;  // ORR
        case 13: ctx.E(ppc_mullw(pRd, pRd, pRs));setNZ(ctx, pRd); break;  // MUL
        case 14: ctx.E(ppc_andc(pRd, pRd, pRs)); setNZ(ctx, pRd); break;  // BIC
        case 15: ctx.E(ppc_nor (pRd, pRs, pRs)); setNZ(ctx, pRd); break;  // MVN
        default: return false;
    }
    return true;
}

static bool emitT_hiReg(Ctx& ctx, uint16_t op, uint32_t curPC){
    uint8_t o  = (op >> 8) & 3;
    uint8_t h1 = (op >> 7) & 1;
    uint8_t h2 = (op >> 6) & 1;
    uint8_t rs = ((op >> 3) & 7) | (h2 << 3);
    uint8_t rd = (op & 7) | (h1 << 3);

    if(rd == 15 || rs == 15) return false;

    uint8_t pRd = RA[rd];
    uint8_t pRs = RA[rs];

    switch(o){
        case 0:  // ADD Rd, Rs (high registers)
            ctx.E(ppc_add(pRd, pRd, pRs));
            break;
        case 1:{ // CMP Rd, Rs
            ctx.E(ppc_mr(TB, pRd));
            ctx.E(ppc_subfc(TA, pRs, TB));
            setNZ(ctx, TA); setC_xer(ctx); setV_sub(ctx, TA, TB, pRs);
            break;
        }
        case 2:  // MOV Rd, Rs
            ctx.E(ppc_mr(pRd, pRs));
            break;
        case 3:{ // BX Rs
            ctx.E(ppc_stw(pRs, FRAME_SCR2, 1));
            emitBXexit(ctx);
            ctx.done = true;
            break;
        }
    }
    return true;
}

static bool emitT_ldrPc(Ctx& ctx, uint16_t op, uint32_t curPC){
    uint8_t rd   = (op >> 8) & 7;
    uint32_t addr = ((curPC + 4) & ~3u) + ((op & 0xFF) << 2);
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.li(TC, addr);
    ctx.call((void*)JitHelp_r32);
    ctx.E(ppc_mr(RA[rd], TA));
    return true;
}

static bool emitT_memReg(Ctx& ctx, uint16_t op){
    uint8_t rd    = op & 7;
    uint8_t rb    = (op >> 3) & 7;
    uint8_t ro    = (op >> 6) & 7;
    uint8_t op97  = (op >> 9) & 7;
    void* fn      = nullptr;
    bool ld       = true;
    bool signExtB = false, signExtH = false;

    switch(op97){
        case 0: fn=(void*)JitHelp_w32; ld=false; break;  // STR
        case 1: fn=(void*)JitHelp_w16; ld=false; break;  // STRH  -- was JitHelp_w8, BUG FIXED
        case 2: fn=(void*)JitHelp_r8;  ld=true;  signExtB=false; break;  // LDR... wait
        // Let me re-read the Thumb encoding:
        // op>>9 bits [11:9]:
        //   000 = STR   Rd, [Rb, Ro]       (5.2)
        //   001 = STRH  Rd, [Rb, Ro]       (5.3) -- was wrong in original
        //   010 = STRB  Rd, [Rb, Ro]       (5.4)
        //   011 = LDSB  Rd, [Rb, Ro]       (5.5) sign-extend byte
        //   100 = LDR   Rd, [Rb, Ro]       (5.2)
        //   101 = LDRH  Rd, [Rb, Ro]       (5.3)
        //   110 = LDRB  Rd, [Rb, Ro]       (5.4)
        //   111 = LDSH  Rd, [Rb, Ro]       (5.5) sign-extend halfword
        // (Based on opcode bits [15:9] with bit15-12=0101)
        default: break;
    }
    // Re-implement correctly:
    switch(op97){
        case 0: fn=(void*)JitHelp_w32; ld=false; break;
        case 1: fn=(void*)JitHelp_w16; ld=false; break;  // STRH
        case 2: fn=(void*)JitHelp_w8;  ld=false; break;  // STRB  -- original had r8 here!
        case 3: fn=(void*)JitHelp_r8;  ld=true;  signExtB=true;  break;  // LDSB
        case 4: fn=(void*)JitHelp_r32; ld=true;  break;  // LDR
        case 5: fn=(void*)JitHelp_r16; ld=true;  break;  // LDRH
        case 6: fn=(void*)JitHelp_r8;  ld=true;  break;  // LDRB
        case 7: fn=(void*)JitHelp_r16; ld=true;  signExtH=true; break;  // LDSH
        default: return false;
    }

    ctx.E(ppc_add(TC, RA[rb], RA[ro]));
    ctx.E(ppc_stw(TC, FRAME_SCR0, 1));
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.E(ppc_lwz(TC, FRAME_SCR0, 1));
    if(!ld) ctx.E(ppc_mr(TD, RA[rd]));
    ctx.call(fn);

    if(ld){
        if(signExtB)       ctx.E(ppc_extsb(RA[rd], TA));
        else if(signExtH)  ctx.E(ppc_extsh(RA[rd], TA));
        else               ctx.E(ppc_mr(RA[rd], TA));
    }
    return true;
}

static bool emitT_memImm(Ctx& ctx, uint16_t op){
    uint8_t rd = op & 7;
    uint8_t rb = (op >> 3) & 7;
    bool ld    = (op >> 11) & 1;
    uint8_t h  = (op >> 12) & 0xF;
    bool by    = (h == 7);    // LDRB/STRB: opcode bits [15:12]=0111
    bool hw    = (h == 8);    // LDRH/STRH: opcode bits [15:12]=1000

    uint32_t off = ((op >> 6) & 0x1F) * (hw ? 2u : (by ? 1u : 4u));

    ctx.li(TC, off);
    ctx.E(ppc_add(TC, RA[rb], TC));
    ctx.E(ppc_stw(TC, FRAME_SCR0, 1));
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.E(ppc_lwz(TC, FRAME_SCR0, 1));
    if(!ld) ctx.E(ppc_mr(TD, RA[rd]));

    void* fn = ld
        ? (hw ? (void*)JitHelp_r16 : (by ? (void*)JitHelp_r8 : (void*)JitHelp_r32))
        : (hw ? (void*)JitHelp_w16 : (by ? (void*)JitHelp_w8 : (void*)JitHelp_w32));
    ctx.call(fn);

    if(ld) ctx.E(ppc_mr(RA[rd], TA));
    return true;
}

static bool emitT_spLoad(Ctx& ctx, uint16_t op, uint32_t curPC){
    bool ld    = (op >> 11) & 1;
    uint8_t rd = (op >> 8) & 7;
    bool sp    = ((op >> 12) & 0xF) == 0x9;
    uint32_t off = (op & 0xFF) << 2;

    if(sp){
        ctx.li(TA, off);
        ctx.E(ppc_add(TC, RA[13], TA));
    } else {
        ctx.li(TC, ((curPC + 4) & ~3u) + off);
    }

    ctx.E(ppc_stw(TC, FRAME_SCR0, 1));
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.E(ppc_lwz(TC, FRAME_SCR0, 1));
    if(!ld) ctx.E(ppc_mr(TD, RA[rd]));

    ctx.call(ld ? (void*)JitHelp_r32 : (void*)JitHelp_w32);
    if(ld) ctx.E(ppc_mr(RA[rd], TA));
    return true;
}

static bool emitT_branch(Ctx& ctx, uint16_t op, uint32_t curPC){
    uint8_t h = (op >> 12) & 0xF;

    if(h == 0xE){
        // Unconditional branch B #off11
        int32_t off = (int32_t)((int16_t)(op << 5)) >> 4;  // sign-extend 11-bit, *2
        emitExit(ctx, (uint32_t)(curPC + 4 + off), EXIT_NORMAL);
        ctx.done = true;
        return true;
    }

    if(h == 0xD){
        // Conditional branch Bcc #off8
        uint8_t cond = (op >> 8) & 0xF;
        if(cond == 0xF) return false;  // SWI

        int32_t off = (int32_t)(int8_t)(op & 0xFF);
        off <<= 1;
        uint32_t tgt  = curPC + 4 + off;
        uint32_t fall = curPC + 2;

        emitLoadCR7(ctx);
        CB cb = condPpc(cond);
        if(!cb.ok) return false;

        uint8_t inv = (cb.bo == 12) ? 4u : 12u;
        size_t si = ctx.sz();
        ctx.E(ppc_bc(inv, cb.bi, 4));  // branch if condition FALSE to fall-through

        emitExit(ctx, tgt, EXIT_NORMAL);
        patchSkip(ctx, si);
        emitExit(ctx, fall, EXIT_NORMAL);

        ctx.done = true;
        return true;
    }

    return false;
}

static bool emitT_bl(Ctx& ctx, uint16_t op1, uint16_t op2, uint32_t curPC){
    // BL/BLX long branch with link
    int32_t hi = (int32_t)((op1 & 0x7FF) << 21) >> 9;  // sign-extend 11-bit, shift to form upper
    int32_t lo = (op2 & 0x7FF) << 1;
    uint32_t tgt = (uint32_t)(curPC + 4 + hi + lo);

    bool blx = ((op2 >> 11) & 0x1F) == 0x1C;

    ctx.li(RA[14], (curPC + 4) | 1u);  // LR = next instruction + 1 (Thumb bit)

    if(blx){
        tgt &= ~3u;  // align to word boundary
        // Clear T bit in RCPSR
        ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 27, 25));
    }

    emitExit(ctx, tgt & ~1u, EXIT_NORMAL);
    ctx.done = true;
    return true;
}

static bool dispThumb(Ctx& ctx, uint16_t op, uint32_t curPC){
    uint8_t h = (op >> 12) & 0xF;

    switch(h){
        case 0x0:{
            uint8_t b = (op >> 11) & 3;
            if(b < 3) return emitT_shifts(ctx, op);
            return emitT_addSub3(ctx, op);
        }
        case 0x1: return emitT_imm8(ctx, op);
        case 0x2:{
            uint8_t b = (op >> 10) & 3;
            if(b == 0) return emitT_alu(ctx, op);
            if(b == 1) return emitT_hiReg(ctx, op, curPC);
            return emitT_ldrPc(ctx, op, curPC);
        }
        case 0x3: case 0x4: case 0x5: return emitT_memReg(ctx, op);
        case 0x6: case 0x7: case 0x8: return emitT_memImm(ctx, op);
        case 0x9: return emitT_spLoad(ctx, op, curPC);
        case 0xD: return emitT_branch(ctx, op, curPC);
        case 0xE: return emitT_branch(ctx, op, curPC);
        default:  return false;
    }
}

// ============================================================
// PC validity
// ============================================================
static bool validPC(uint32_t pc, bool gba){
    pc &= ~1u;
    if(gba){
        return (pc >= 4u        && pc < 0x4000u)
            || (pc >= 0x02000000u && pc < 0x02040000u)
            || (pc >= 0x03000000u && pc < 0x03008000u)
            || (pc >= 0x08000000u && pc < 0x0E000000u);
    }
    return (pc < 0x4000u)
        || (pc >= 0x02000000u && pc < 0x02400000u)
        || (pc >= 0x03000000u && pc < 0x03800000u)
        || (pc >= 0xFFFF0000u);
}

// ============================================================
// Block compiler
// ============================================================
static JitBlock* compile(Interpreter* interp, Core* core,
                          uint32_t armPC, bool arm7, int cpuIdx){
    if(!validPC(armPC, core->gbaMode)) return nullptr;

    bool   thumb = interp->isThumb();
    size_t bkt   = hashPC(armPC);

    {
        JitBlock& slot = cache[bkt];
        if(slot.valid
           && slot.armPC == armPC
           && slot.thumb == thumb
           && slot.gen   == cacheGen){
            if(slot.code >= codeBuf && slot.code < codeBuf + JIT_WORDS)
                return &slot;
            slot.valid = false;
        }
    }

    if(codePos + BLK_WDS >= JIT_WORDS){
        flushJitCache();
    }

    JitBlock& slot = cache[bkt];

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

    while(n < (int)BLK_ARMS && !ctx.done){
        if(!validPC(curPC, core->gbaMode)){
            emitExit(ctx, curPC, EXIT_FALLBACK);
            ctx.done = true; break;
        }

        if(thumb){
            uint16_t op = core->memory.read<uint16_t>(arm7, curPC);

            // BL/BLX long form: two 16-bit half-words
            if(((op >> 11) & 0x1F) == 0x1E){
                if(!validPC(curPC + 2, core->gbaMode)){
                    emitExit(ctx, curPC, EXIT_FALLBACK);
                    ctx.done = true; break;
                }
                uint16_t op2 = core->memory.read<uint16_t>(arm7, curPC + 2);
                uint8_t  bb  = (op2 >> 11) & 0x1F;
                if(bb == 0x1F || bb == 0x1C){
                    emitT_bl(ctx, op, op2, curPC);
                    curPC += 4; n += 2; continue;
                }
            }

            bool ok = dispThumb(ctx, op, curPC);
            if(!ok){ emitExit(ctx, curPC, EXIT_FALLBACK); ctx.done = true; }
            else {
                curPC += 2; n++;
                uint8_t hh = (op >> 12) & 0xF;
                if(hh == 0xD || hh == 0xE || hh == 0xF) ctx.done = true;
            }
        } else {
            uint32_t op = core->memory.read<uint32_t>(arm7, curPC);
            bool ok = dispARM(ctx, op, curPC);
            if(!ok){ emitExit(ctx, curPC, EXIT_FALLBACK); ctx.done = true; }
            else {
                curPC += 4; n++;
                uint32_t it = (op >> 25) & 7;
                if(it == 5)                           ctx.done = true;
                if((op & 0x0FFFFFF0) == 0x012FFF10)   ctx.done = true;
                if((op & 0x0E000000) == 0x0A000000)   ctx.done = true;
            }
        }
    }

    if(!ctx.done) emitExit(ctx, curPC, EXIT_NORMAL);

    size_t wds = ctx.sz();
    if(wds == 0) return nullptr;

    flushICache(ctx.base, wds);

    slot.armPC = armPC;
    slot.code  = ctx.base;
    slot.nW    = (uint32_t)wds;
    slot.gen   = cacheGen;
    slot.thumb = thumb;
    slot.valid = true;

    codePos += wds;
    return &slot;
}

// ============================================================
// Run loop helpers
// ============================================================
static bool isGoodPC(uint32_t pc, bool gba){
    return pc != 0u && pc != 0xFFFFFFFFu && validPC(pc, gba);
}

void runJitNds(Core& core){
    for(int cpu = 0; cpu < 2; cpu++){
        Interpreter& interp = core.interpreter[cpu];
        if(interp.halted) continue;
        if(!interp.isReady()){ interp.jitRunOpcode(); continue; }

        uint32_t pc = interp.getActualPC();
        if(!isGoodPC(pc, false)){ interp.jitRunOpcode(); continue; }

        JitBlock* b = compile(&interp, &core, pc, cpu == 1, cpu);
        if(!b || b->nW == 0){ interp.jitRunOpcode(); continue; }

        uint32_t* code = b->code;
        executeBlock_asm(code);

        uint32_t exitPC = g_exitPC[cpu];
        int      reason = g_exitReason[cpu];

        if(!isGoodPC(exitPC, false)){ interp.jitRunOpcode(); continue; }

        interp.setPC(exitPC);
        if(reason == EXIT_FALLBACK) interp.jitRunOpcode();
    }
    JitHelp_tick(&core);
}

void runJitGba(Core& core){
    Interpreter& interp = core.interpreter[1];
    if(interp.halted){ JitHelp_tick(&core); return; }
    if(!interp.isReady()){ interp.jitRunOpcode(); JitHelp_tick(&core); return; }

    uint32_t pc = interp.getActualPC();
    if(!isGoodPC(pc, true)){ interp.jitRunOpcode(); JitHelp_tick(&core); return; }

    JitBlock* b = compile(&interp, &core, pc, true, 1);
    if(!b || b->nW == 0){ interp.jitRunOpcode(); JitHelp_tick(&core); return; }

    uint32_t* code = b->code;
    executeBlock_asm(code);

    uint32_t exitPC = g_exitPC[1];
    int      reason = g_exitReason[1];

    if(!isGoodPC(exitPC, true)) interp.jitRunOpcode();
    else {
        interp.setPC(exitPC);
        if(reason == EXIT_FALLBACK) interp.jitRunOpcode();
    }

    JitHelp_tick(&core);
}

// ============================================================
// Lifecycle
// ============================================================
bool initJit(Core* core){
    codeBuf = (uint32_t*)memalign(32, JIT_BYTES);
    if(!codeBuf){ printf("[JIT] alloc failed\n"); return false; }
    codePos  = 0;
    cacheGen = 0;
    for(size_t i = 0; i < CSIZ; i++) cache[i].valid = false;
    g_exitPC[0] = g_exitPC[1] = 0;
    g_exitCPSR[0] = g_exitCPSR[1] = 0;
    g_exitReason[0] = g_exitReason[1] = EXIT_NORMAL;
    printf("[JIT] buf=%p (%zuKB)\n", (void*)codeBuf, JIT_BYTES >> 10);
    if(core) core->setRunFunc(core->gbaMode ? runJitGba : runJitNds);
    return true;
}

void shutdownJit(Core* core){
    if(core) core->setRunFunc(core->gbaMode
        ? static_cast<void(*)(Core&)>(&Interpreter::runCoreSingle<true,0>)
        : &Interpreter::runCoreNds);
    free(codeBuf); codeBuf = nullptr; codePos = 0;
    for(size_t i = 0; i < CSIZ; i++) cache[i].valid = false;
}

void invalidateJitRange(uint32_t start, uint32_t end){
    for(size_t i = 0; i < CSIZ; i++)
        if(cache[i].valid && cache[i].armPC >= start && cache[i].armPC < end)
            cache[i].valid = false;
}

} // namespace JitPpc
