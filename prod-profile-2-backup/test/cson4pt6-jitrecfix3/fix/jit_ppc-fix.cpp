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
// Frame layout for JIT blocks
// (allocated by each block's prologue)
//
//   [r1+  0]  back-chain (points to trampoline's frame)
//   [r1+  4]  ABI LR save area (callees may write here)
//   [r1+  8]  our saved LR (= return addr inside trampoline)
//   [r1+ 12]  pad
//   [r1+ 16]  r14..r31 saved  (18 regs * 4 = 72 bytes)
//   [r1+ 88]  Core* pointer
//   [r1+ 92]  scratch slot 0
//   [r1+ 96]  scratch slot 1
//   [r1+100]  scratch slot 2
//   [r1+104]  pad to 112
//   [r1+112]  ARM r0..r15 + CPSR (17 * 4 = 68 bytes)
//   [r1+180]  pad to 192
// ============================================================
static const int FRAME_SIZE    = 192;
static const int FRAME_LR      = 8;
static const int FRAME_R14     = 16;
static const int FRAME_CORE    = 88;
static const int FRAME_SCR0    = 92;
static const int FRAME_SCR1    = 96;
static const int FRAME_SCR2    = 100;
static const int FRAME_REGSYNC = 112;

static_assert(FRAME_SIZE % 16 == 0,                    "frame align");
static_assert(FRAME_R14 + 18*4 == FRAME_CORE,          "layout");
static_assert(FRAME_REGSYNC + 17*4 <= FRAME_SIZE,      "regsync fits");

namespace JitPpc {

// ============================================================
// PPC instruction encoders
// ============================================================
static inline uint32_t ppc_nop()  { return 0x60000000u; }
static inline uint32_t ppc_blr()  { return 0x4E800020u; }
static inline uint32_t ppc_bclr(uint8_t bo,uint8_t bi,bool lk=false)
    { return (19u<<26)|((uint32_t)(bo&31)<<21)|((uint32_t)(bi&31)<<16)|(16u<<1)|(lk?1u:0u); }
static inline uint32_t ppc_bctr(bool lk=false)
    { return (19u<<26)|(20u<<21)|(528u<<1)|(lk?1u:0u); }
static inline uint32_t ppc_bc(uint8_t bo,uint8_t bi,int16_t off,bool lk=false)
    { return (16u<<26)|((uint32_t)(bo&31)<<21)|((uint32_t)(bi&31)<<16)
             |((uint32_t)(off&0xFFFC))|(lk?1u:0u); }

static inline uint32_t ppc_addi(uint8_t rt,uint8_t ra,int16_t i)
    { return (14u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_addis(uint8_t rt,uint8_t ra,int16_t i)
    { return (15u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_ori(uint8_t ra,uint8_t rs,uint16_t i)
    { return (24u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i; }
static inline uint32_t ppc_oris(uint8_t ra,uint8_t rs,uint16_t i)
    { return (25u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i; }
static inline uint32_t ppc_stwu(uint8_t rs,int16_t d,uint8_t ra)
    { return (37u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_stw(uint8_t rs,int16_t d,uint8_t ra)
    { return (36u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_lwz(uint8_t rt,int16_t d,uint8_t ra)
    { return (32u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_lbz(uint8_t rt,int16_t d,uint8_t ra)
    { return (34u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_lhz(uint8_t rt,int16_t d,uint8_t ra)
    { return (40u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_lha(uint8_t rt,int16_t d,uint8_t ra)
    { return (42u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_stb(uint8_t rs,int16_t d,uint8_t ra)
    { return (38u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t ppc_sth(uint8_t rs,int16_t d,uint8_t ra)
    { return (44u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }

static inline uint32_t ppc_cmpi(uint8_t cr,uint8_t ra,int16_t i)
    { return (11u<<26)|((uint32_t)(cr&7)<<23)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_cmpli(uint8_t cr,uint8_t ra,uint16_t i)
    { return (10u<<26)|((uint32_t)(cr&7)<<23)|((uint32_t)ra<<16)|i; }
static inline uint32_t ppc_subfic(uint8_t rt,uint8_t ra,int16_t i)
    { return (8u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }

// X-form / XO-form helpers
static inline uint32_t Xf(uint8_t rt,uint8_t ra,uint8_t rb,uint32_t xop,bool rc=false)
    { return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)
             |((uint32_t)rb<<11)|(xop<<1)|(rc?1u:0u); }
static inline uint32_t XOf(uint8_t rt,uint8_t ra,uint8_t rb,bool oe,uint32_t xop,bool rc=false)
    { return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)
             |((uint32_t)rb<<11)|(oe?0x400u:0u)|(xop<<1)|(rc?1u:0u); }

static inline uint32_t ppc_add(uint8_t d,uint8_t a,uint8_t b)  { return XOf(d,a,b,false,266); }
static inline uint32_t ppc_addc(uint8_t d,uint8_t a,uint8_t b) { return XOf(d,a,b,false,10);  }
static inline uint32_t ppc_adde(uint8_t d,uint8_t a,uint8_t b) { return XOf(d,a,b,false,138); }
static inline uint32_t ppc_subf(uint8_t d,uint8_t a,uint8_t b) { return XOf(d,a,b,false,40);  }
static inline uint32_t ppc_subfc(uint8_t d,uint8_t a,uint8_t b){ return XOf(d,a,b,false,8);   }
static inline uint32_t ppc_subfe(uint8_t d,uint8_t a,uint8_t b){ return XOf(d,a,b,false,136); }
static inline uint32_t ppc_neg(uint8_t d,uint8_t a)             { return XOf(d,a,0,false,104); }
static inline uint32_t ppc_mullw(uint8_t d,uint8_t a,uint8_t b){ return XOf(d,a,b,false,235); }
static inline uint32_t ppc_mulhw(uint8_t d,uint8_t a,uint8_t b){ return XOf(d,a,b,false,75);  }
static inline uint32_t ppc_mulhwu(uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,11);  }

static inline uint32_t ppc_and(uint8_t a,uint8_t s,uint8_t b)  { return Xf(s,a,b,28);  }
static inline uint32_t ppc_or(uint8_t a,uint8_t s,uint8_t b)   { return Xf(s,a,b,444); }
static inline uint32_t ppc_xor(uint8_t a,uint8_t s,uint8_t b)  { return Xf(s,a,b,316); }
static inline uint32_t ppc_andc(uint8_t a,uint8_t s,uint8_t b) { return Xf(s,a,b,60);  }
static inline uint32_t ppc_nor(uint8_t a,uint8_t s,uint8_t b)  { return Xf(s,a,b,124); }
static inline uint32_t ppc_eqv(uint8_t a,uint8_t s,uint8_t b)  { return Xf(s,a,b,284); }
static inline uint32_t ppc_orc(uint8_t a,uint8_t s,uint8_t b)  { return Xf(s,a,b,412); }
static inline uint32_t ppc_mr(uint8_t a,uint8_t s)              { return ppc_or(a,s,s); }
static inline uint32_t ppc_slw(uint8_t a,uint8_t s,uint8_t b)  { return Xf(s,a,b,24);  }
static inline uint32_t ppc_srw(uint8_t a,uint8_t s,uint8_t b)  { return Xf(s,a,b,536); }
static inline uint32_t ppc_sraw(uint8_t a,uint8_t s,uint8_t b) { return Xf(s,a,b,792); }
static inline uint32_t ppc_cntlzw(uint8_t a,uint8_t s)         { return Xf(s,a,0,26);  }
static inline uint32_t ppc_extsb(uint8_t a,uint8_t s)          { return Xf(s,a,0,954); }
static inline uint32_t ppc_extsh(uint8_t a,uint8_t s)          { return Xf(s,a,0,922); }
static inline uint32_t ppc_lwzx(uint8_t t,uint8_t a,uint8_t b) { return Xf(t,a,b,23);  }
static inline uint32_t ppc_lbzx(uint8_t t,uint8_t a,uint8_t b) { return Xf(t,a,b,87);  }
static inline uint32_t ppc_lhzx(uint8_t t,uint8_t a,uint8_t b) { return Xf(t,a,b,279); }
static inline uint32_t ppc_stwx(uint8_t s,uint8_t a,uint8_t b) { return Xf(s,a,b,151); }
static inline uint32_t ppc_stbx(uint8_t s,uint8_t a,uint8_t b) { return Xf(s,a,b,215); }
static inline uint32_t ppc_sthx(uint8_t s,uint8_t a,uint8_t b) { return Xf(s,a,b,407); }
static inline uint32_t ppc_cmp(uint8_t cr,uint8_t a,uint8_t b)
    { return (31u<<26)|((uint32_t)(cr&7)<<23)|((uint32_t)a<<16)|((uint32_t)b<<11); }
static inline uint32_t ppc_cmpl(uint8_t cr,uint8_t a,uint8_t b)
    { return (31u<<26)|((uint32_t)(cr&7)<<23)|((uint32_t)a<<16)|((uint32_t)b<<11)|(32u<<1); }

static inline uint32_t ppc_rlwinm(uint8_t a,uint8_t s,uint8_t sh,uint8_t mb,uint8_t me,bool rc=false)
    { return (21u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)
             |((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1)|(rc?1u:0u); }
static inline uint32_t ppc_rlwimi(uint8_t a,uint8_t s,uint8_t sh,uint8_t mb,uint8_t me)
    { return (20u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)
             |((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1); }
static inline uint32_t ppc_rlwnm(uint8_t a,uint8_t s,uint8_t b,uint8_t mb,uint8_t me)
    { return (23u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)
             |((uint32_t)b<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1); }
static inline uint32_t ppc_srawi(uint8_t a,uint8_t s,uint8_t sh)
    { return (31u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)|((uint32_t)sh<<11)|(824u<<1); }

static inline uint32_t ppc_mtspr(uint16_t spr,uint8_t rs)
    { uint8_t lo=spr&31,hi=(spr>>5)&31;
      return (31u<<26)|((uint32_t)rs<<21)|((uint32_t)lo<<16)|((uint32_t)hi<<11)|(467u<<1); }
static inline uint32_t ppc_mfspr(uint8_t rt,uint16_t spr)
    { uint8_t lo=spr&31,hi=(spr>>5)&31;
      return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)lo<<16)|((uint32_t)hi<<11)|(339u<<1); }
static inline uint32_t ppc_mtctr(uint8_t s) { return ppc_mtspr(9,s); }
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
// emit_li32: emit 1 or 2 instructions to load 32-bit immediate
// rt must NOT be r0
// ============================================================
static int emit_li32(uint32_t* out, uint8_t rt, uint32_t v) {
    uint16_t hi = v>>16, lo = v&0xFFFF;
    if (!hi && !lo)      { out[0]=ppc_addi(rt,0,0);               return 1; }
    if (!hi) {
        if (lo<0x8000u)  { out[0]=ppc_addi(rt,0,(int16_t)lo);     return 1; }
        out[0]=ppc_addi(rt,0,0); out[1]=ppc_ori(rt,rt,lo);         return 2;
    }
    if (!lo)             { out[0]=ppc_addis(rt,0,(int16_t)hi);     return 1; }
    out[0]=ppc_addis(rt,0,(int16_t)hi); out[1]=ppc_ori(rt,rt,lo); return 2;
}

// ============================================================
// Register mapping
//
// ARM r0-r15 -> PPC r14-r29  (callee-saved: survive all C calls)
// CPSR       -> PPC r30      (callee-saved)
// INTERP*    -> PPC r31      (callee-saved)
// Core*      -> FRAME_CORE slot in stack (reload as needed)
//
// Volatile (caller-saved) temporaries: r3-r12
//   r3  = arg0 / return value / temp A
//   r4  = arg1 / temp B
//   r5  = arg2 / temp C
//   r6  = arg3 / temp D
//   r11 = call-address scratch (standard PPC convention)
//
// r0 can only be used with mflr/mtlr (addi with ra=0 reads 0, not r0)
// ============================================================
static const uint8_t RA[16] = {     // ARM r0-r15 -> PPC reg
    14,15,16,17,18,19,20,21,
    22,23,24,25,26,27,28,29
};
static const uint8_t RCPSR   = 30;
static const uint8_t RINTERP = 31;

// Volatile temps
static const uint8_t TA = 3;   // arg0/ret/primary temp
static const uint8_t TB = 4;   // arg1/secondary temp
static const uint8_t TC = 5;   // arg2/temp
static const uint8_t TD = 6;   // arg3/temp
static const uint8_t RCALL = 11; // indirect call address scratch

// ============================================================
// Code buffer
// ============================================================
static const size_t JIT_BYTES     = 4u*1024u*1024u;
static const size_t JIT_WORDS     = JIT_BYTES / 4;
static const size_t BLK_MAX_ARMS  = 64;
static const size_t BLK_MAX_WDS   = BLK_MAX_ARMS * 128 + 128;

static uint32_t* codeBuf  = nullptr;
static size_t    codePos  = 0;

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
static const size_t CBITS = 13;
static const size_t CSIZ  = 1u<<CBITS;
static JitBlock cache[CSIZ];
static size_t   hashPC(uint32_t pc) { return (pc>>1)&(CSIZ-1); }

void flushJitCache() {
    codePos = 0;
    for (size_t i=0;i<CSIZ;i++) cache[i].valid=false;
}

static void dcbst(uint32_t* p, size_t n) {
    DCFlushRange(p, n*4);
    ICInvalidateRange(p, n*4);
}

// ============================================================
// Emit context
// ============================================================
struct Ctx {
    uint32_t *base, *cur;
    size_t    cap;
    bool      thumb, arm7, done;
    uint32_t  blockPC;
    Interpreter* interp;
    Core*     core;

    void E(uint32_t w)  { if ((size_t)(cur-base)<cap) *cur++=w; }
    size_t sz() const   { return (size_t)(cur-base); }

    void li(uint8_t rt, uint32_t v) {
        uint32_t tmp[2];
        int n=emit_li32(tmp,rt,v);
        for(int i=0;i<n;i++) E(tmp[i]);
    }

    // Emit indirect call via RCALL (r11)
    // r11 is the standard scratch register for indirect calls in PPC EABI
    void call(void* fn) {
        uint32_t addr=(uint32_t)(uintptr_t)fn;
        uint16_t hi=addr>>16, lo=addr&0xFFFF;
        E(ppc_addis(RCALL,0,(int16_t)hi));
        E(ppc_ori(RCALL,RCALL,lo));
        E(ppc_mtctr(RCALL));
        E(ppc_bctr(true));   // bctrl
    }

    void ldCore(uint8_t dst=TA) { E(ppc_lwz(dst,FRAME_CORE,1)); }
};

// ============================================================
// C-callable helpers
// ============================================================
extern "C" {

void JitHelp_syncTo(Interpreter* interp, uint32_t* regs) {
    uint32_t** p=interp->getRegisters();
    for(int i=0;i<15;i++) *p[i]=regs[i];
    interp->getCpsrRef()=regs[16];
    interp->setPC(regs[15]);
}

void JitHelp_syncFrom(Interpreter* interp, uint32_t* regs) {
    uint32_t** p=interp->getRegisters();
    for(int i=0;i<15;i++) regs[i]=*p[i];
    regs[15]=interp->getPC();
    regs[16]=interp->getCpsrRef();
}

int JitHelp_fallback(Interpreter* interp) {
    return interp->jitRunOpcode();
}

uint32_t JitHelp_r32(Core* c,int a,uint32_t ad){return c->memory.read<uint32_t>((bool)a,ad);}
uint16_t JitHelp_r16(Core* c,int a,uint32_t ad){return c->memory.read<uint16_t>((bool)a,ad);}
uint8_t  JitHelp_r8 (Core* c,int a,uint32_t ad){return c->memory.read<uint8_t> ((bool)a,ad);}
void JitHelp_w32(Core* c,int a,uint32_t ad,uint32_t v){c->memory.write<uint32_t>((bool)a,ad,v);}
void JitHelp_w16(Core* c,int a,uint32_t ad,uint16_t v){c->memory.write<uint16_t>((bool)a,ad,v);}
void JitHelp_w8 (Core* c,int a,uint32_t ad,uint8_t  v){c->memory.write<uint8_t> ((bool)a,ad,v);}

void JitHelp_tick(Core* core) {
    core->globalCycles += 64;
    while(!core->events.empty() &&
          core->globalCycles >= core->events.front().cycles) {
        SchedEvent e=core->events.front();
        core->events.erase(core->events.begin());
        core->tasks[e.task]();
    }
}

} // extern "C"

// ============================================================
// Prologue
//
// On entry: LR = return address set by executeBlock_asm's bctrl
//           r3 = first argument passed by trampoline (unused; we
//                get everything from the embedded immediates)
//
// We MUST save LR as the very first thing.
// ============================================================
static void emitPrologue(Ctx& ctx) {
    ctx.E(ppc_mflr(0));                              // r0 = return addr (from bctrl)
    ctx.E(ppc_stwu(1,-(int16_t)FRAME_SIZE,1));       // allocate frame
    ctx.E(ppc_stw(0,FRAME_LR,1));                    // save LR at [r1+8]
    for(int r=14;r<=31;r++)                           // save r14-r31
        ctx.E(ppc_stw(r,FRAME_R14+(r-14)*4,1));
    ctx.li(RINTERP,(uint32_t)(uintptr_t)ctx.interp); // r31 = interp*
    ctx.li(TA,(uint32_t)(uintptr_t)ctx.core);        // TA = core*
    ctx.E(ppc_stw(TA,FRAME_CORE,1));                 // save core* in frame
}

static void emitEpilogue(Ctx& ctx) {
    for(int r=14;r<=31;r++)
        ctx.E(ppc_lwz(r,FRAME_R14+(r-14)*4,1));
    ctx.E(ppc_lwz(0,FRAME_LR,1));
    ctx.E(ppc_mtlr(0));
    ctx.E(ppc_addi(1,1,(int16_t)FRAME_SIZE));
    ctx.E(ppc_blr());
}

// ============================================================
// State sync
// ============================================================
static void emitSyncTo(Ctx& ctx) {
    for(int i=0;i<16;i++) ctx.E(ppc_stw(RA[i],FRAME_REGSYNC+i*4,1));
    ctx.E(ppc_stw(RCPSR,FRAME_REGSYNC+16*4,1));
    ctx.E(ppc_mr(TA,RINTERP));
    ctx.E(ppc_addi(TB,1,(int16_t)FRAME_REGSYNC));
    ctx.call((void*)JitHelp_syncTo);
}

static void emitSyncFrom(Ctx& ctx) {
    ctx.E(ppc_mr(TA,RINTERP));
    ctx.E(ppc_addi(TB,1,(int16_t)FRAME_REGSYNC));
    ctx.call((void*)JitHelp_syncFrom);
    for(int i=0;i<16;i++) ctx.E(ppc_lwz(RA[i],FRAME_REGSYNC+i*4,1));
    ctx.E(ppc_lwz(RCPSR,FRAME_REGSYNC+16*4,1));
}

// ============================================================
// Condition flags
//
// CR7 (field 7, bits 28-31 of CR) holds ARM NZCV:
//   mtcrf 0x01, RCPSR  -- field mask 0x01 selects CR7
//
// CPSR layout:  bit31=N  bit30=Z  bit29=C  bit28=V
// CR7 layout:   bit31=CR7_LT  bit30=CR7_GT  bit29=CR7_EQ  bit28=CR7_SO
//
// So: N->CR7_LT, Z->CR7_GT, C->CR7_EQ, V->CR7_SO
//
// PPC bc BI field for CR7:  LT=28, GT=29, EQ=30, SO=31
// ============================================================
static void emitLoadCR7(Ctx& ctx) {
    ctx.E(ppc_mtcrf(0x01,RCPSR));
}

struct CB { uint8_t bo,bi; bool ok; };
static CB condPpc(uint8_t c) {
    // BI for CR7: LT=28(N), GT=29(Z), EQ=30(C), SO=31(V)
    switch(c){
        case 0:  return{12,29,true};  // EQ  Z=1   CR7_GT set
        case 1:  return{ 4,29,true};  // NE  Z=0
        case 2:  return{12,30,true};  // CS  C=1   CR7_EQ set
        case 3:  return{ 4,30,true};  // CC  C=0
        case 4:  return{12,28,true};  // MI  N=1   CR7_LT set
        case 5:  return{ 4,28,true};  // PL  N=0
        case 6:  return{12,31,true};  // VS  V=1   CR7_SO set
        case 7:  return{ 4,31,true};  // VC  V=0
        case 14: return{20, 0,true};  // AL  always
        // Complex conditions (HI,LS,GE,LT,GT,LE) -> fallback
        default: return{ 0, 0,false};
    }
}

// Emit a conditional skip: if ARM cond NOT met, skip forward.
// Returns patch index or SIZE_MAX for always/invalid.
static size_t emitCondSkip(Ctx& ctx, uint8_t cond) {
    if (cond==14) return SIZE_MAX;
    emitLoadCR7(ctx);
    CB cb=condPpc(cond);
    if (!cb.ok) return SIZE_MAX;
    uint8_t invBo=(cb.bo==12)?4:(cb.bo==4?12:cb.bo);
    size_t idx=ctx.sz();
    ctx.E(ppc_bc(invBo,cb.bi,0));
    return idx;
}

static void patchSkip(Ctx& ctx, size_t idx) {
    if (idx==SIZE_MAX) return;
    int32_t off=(int32_t)((ctx.sz()-idx)*4);
    ctx.base[idx]=(ctx.base[idx]&0xFFFF0003u)|(uint32_t)(off&0xFFFC);
}

// ============================================================
// CPSR flag updates
// ============================================================

// Update N and Z bits in RCPSR from result register r
// Uses: TA (volatile)
static void setNZ(Ctx& ctx, uint8_t r) {
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,2,31));     // clear bits 31..30 (N,Z)
    ctx.E(ppc_rlwimi(RCPSR,r,0,0,0));           // CPSR[31] = r[31]  (N)
    // Z: is r == 0?  Use cmpwi cr6, r, 0  then extract EQ bit
    ctx.E(ppc_cmpi(6,r,0));                     // cr6: compare r with 0
    ctx.E(ppc_mfcr(TA));
    // CR6 occupies bits 7..4 of CR word (field 6, each field 4 bits from MSB).
    // CR6_EQ = bit 5 of CR word (counting from bit31=bit0).
    // We want it at CPSR bit 30 (Z).
    // rlwinm TA, TA, 25, 30, 30:
    //   rotate left 25: bit5 -> bit(5+25)%32 = bit30  ✓
    ctx.E(ppc_rlwinm(TA,TA,25,30,30));
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}

// After addc/adde: C = XER.CA (bit 29), V = computed from operands
static void setC_add(Ctx& ctx) {
    ctx.E(ppc_mfxer(TA));
    ctx.E(ppc_rlwinm(TA,TA,0,29,29));           // isolate XER.CA at bit29
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,3,1));       // clear CPSR.C (bit29)
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}

// After subfc/subfe: C = XER.CA (ARM: C=1 means no borrow; ppc subfc CA=1 when a>=b)
static void setC_sub(Ctx& ctx) { setC_add(ctx); }

// V for addition: V = ((a^result) & (b^result)) >> 31 -> CPSR bit28
// Uses TA,TB (callee must have saved original a,b if needed before operation)
static void setV_add(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    ctx.E(ppc_xor(TA,res,a));                   // TA = a ^ result
    ctx.E(ppc_xor(TB,res,b));                   // TB = b ^ result
    ctx.E(ppc_and(TA,TA,TB));                   // TA = (a^res)&(b^res)
    ctx.E(ppc_rlwinm(TA,TA,4,28,28));           // bit31 -> bit28 (V position)
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,4,2));      // clear CPSR.V (bit28)
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}

// V for subtraction (result = a - b): V = ((a^b) & (a^result)) >> 31 -> bit28
static void setV_sub(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    ctx.E(ppc_xor(TA,a,b));
    ctx.E(ppc_xor(TB,a,res));
    ctx.E(ppc_and(TA,TA,TB));
    ctx.E(ppc_rlwinm(TA,TA,4,28,28));
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,4,2));
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}

// Set C from bit0 of carry_reg -> CPSR bit29
static void setC_fromBit0(Ctx& ctx, uint8_t carry) {
    ctx.E(ppc_rlwinm(TA,carry,29,29,29));
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,3,1));
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}

// ============================================================
// Shifter helpers
// Carry result goes into TC (r5) bit 0 when sc=true
// ============================================================
static void shiftLslI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i==0){
        if(d!=s) ctx.E(ppc_mr(d,s));
        if(sc) ctx.E(ppc_rlwinm(TC,RCPSR,3,31,31)); // carry = old C
    } else if(i<32){
        if(sc) ctx.E(ppc_rlwinm(TC,s,i,31,31));
        ctx.E(ppc_rlwinm(d,s,(uint8_t)i,0,(uint8_t)(31-i)));
    } else if(i==32){
        if(sc) ctx.E(ppc_rlwinm(TC,s,0,31,31));
        ctx.E(ppc_addi(d,0,0));
    } else {
        if(sc) ctx.E(ppc_addi(TC,0,0));
        ctx.E(ppc_addi(d,0,0));
    }
}

static void shiftLsrI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i==0||i==32){
        if(sc) ctx.E(ppc_rlwinm(TC,s,1,31,31));
        ctx.E(ppc_addi(d,0,0));
    } else if(i<32){
        if(sc) ctx.E(ppc_rlwinm(TC,s,(uint8_t)(33-i),31,31));
        ctx.E(ppc_rlwinm(d,s,(uint8_t)(32-i),(uint8_t)i,31));
    } else {
        if(sc) ctx.E(ppc_addi(TC,0,0));
        ctx.E(ppc_addi(d,0,0));
    }
}

static void shiftAsrI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i<=0||i>=32){
        if(sc) ctx.E(ppc_rlwinm(TC,s,1,31,31));
        ctx.E(ppc_srawi(d,s,31));
    } else {
        if(sc) ctx.E(ppc_rlwinm(TC,s,(uint8_t)(33-i),31,31));
        ctx.E(ppc_srawi(d,s,(uint8_t)i));
    }
}

static void shiftRorI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i==0){
        // RRX: shift right 1 through carry
        ctx.E(ppc_rlwinm(TA,RCPSR,3,31,31)); // TA = old C
        if(sc) ctx.E(ppc_rlwinm(TC,s,0,31,31)); // carry out = bit0
        ctx.E(ppc_rlwinm(d,s,31,1,31));
        ctx.E(ppc_rlwimi(d,TA,31,0,0));
    } else {
        i&=31; if(!i) i=32;
        if(i<32){
            if(sc) ctx.E(ppc_rlwinm(TC,s,(uint8_t)(33-i),31,31));
            ctx.E(ppc_rlwinm(d,s,(uint8_t)(32-i),0,31));
        } else {
            if(d!=s) ctx.E(ppc_mr(d,s));
            if(sc) ctx.E(ppc_rlwinm(TC,s,1,31,31));
        }
    }
}

// Returns true if carry was tracked in TC
static bool emitShifterOp(Ctx& ctx, uint32_t op, uint8_t dst, bool needCarry) {
    bool isImm=(op>>25)&1;
    if(isImm){
        uint32_t v=op&0xFF, rot=((op>>8)&0xF)*2;
        if(rot) v=(v>>rot)|(v<<(32-rot));
        ctx.li(dst,v);
        if(needCarry && rot){
            ctx.E(ppc_rlwinm(TC,dst,1,31,31));
            return true;
        }
        return false;
    }
    uint8_t rm=op&0xF, pRm=RA[rm];
    bool    isReg=(op>>4)&1;
    uint8_t st=(op>>5)&3;
    if(!isReg){
        int sa=(op>>7)&0x1F;
        switch(st){
            case 0: shiftLslI(ctx,dst,pRm,sa,needCarry); break;
            case 1: shiftLsrI(ctx,dst,pRm,sa?sa:32,needCarry); break;
            case 2: shiftAsrI(ctx,dst,pRm,sa?sa:32,needCarry); break;
            case 3: shiftRorI(ctx,dst,pRm,sa,needCarry); break;
        }
        return needCarry;
    } else {
        uint8_t rs=(op>>8)&0xF, pRs=RA[rs];
        ctx.E(ppc_rlwinm(TD,pRs,0,24,31));  // TD = rs & 0xFF
        ctx.E(ppc_mr(TA,pRm));               // TA = rm (copy before possible alias)
        switch(st){
            case 0: ctx.E(ppc_slw(dst,TA,TD)); break;
            case 1: ctx.E(ppc_srw(dst,TA,TD)); break;
            case 2: ctx.E(ppc_sraw(dst,TA,TD)); break;
            case 3:
                ctx.E(ppc_subfic(TB,TD,32));
                ctx.E(ppc_rlwnm(dst,TA,TB,0,31));
                break;
        }
        return false; // carry not tracked for reg shifts
    }
}

// ============================================================
// Data processing
// ============================================================
enum DP{AND=0,EOR,SUB,RSB,ADD,ADC,SBC,RSC,TST,TEQ,CMP,CMN,ORR,MOV,BIC,MVN};

static bool emitDP(Ctx& ctx, uint32_t op) {
    uint8_t cond=(op>>28)&0xF;
    uint8_t dop =(op>>21)&0xF;
    bool    s   =(op>>20)&1;
    uint8_t rn  =(op>>16)&0xF;
    uint8_t rd  =(op>>12)&0xF;
    if(rd==15) return false;

    uint8_t pRd=RA[rd], pRn=RA[rn];

    size_t si=emitCondSkip(ctx,cond);
    if(si==SIZE_MAX && cond!=14) return false;

    bool needCin=(dop==ADC||dop==SBC||dop==RSC);
    bool logCarry=s&&(dop==AND||dop==EOR||dop==TST||dop==TEQ||
                      dop==ORR||dop==MOV||dop==BIC||dop==MVN);

    // Load carry into XER if needed
    if(needCin){
        ctx.E(ppc_rlwinm(TA,RCPSR,0,29,29));
        ctx.E(ppc_mtxer(TA));
    }

    // Shifter op into TA (volatile; callee-saved ARM regs won't be touched)
    bool carrySet=emitShifterOp(ctx,op,TA,logCarry);

    // For V calculation we need both operands before the result
    // Save pRn to TB if we need V (pRn may alias pRd for test ops)
    bool needV=s&&(dop==ADD||dop==SUB||dop==RSB||dop==CMN||dop==CMP||
                    dop==ADC||dop==SBC||dop==RSC);
    if(needV) ctx.E(ppc_mr(TB,pRn));  // TB = original rn

    bool isTest=(dop==TST||dop==TEQ||dop==CMP||dop==CMN);
    uint8_t res=isTest?TC:pRd;  // test results don't write rd

    switch((DP)dop){
        case AND:case TST: ctx.E(ppc_and(res,pRn,TA)); break;
        case EOR:case TEQ: ctx.E(ppc_xor(res,pRn,TA)); break;
        case SUB:case CMP: ctx.E(ppc_subfc(res,TA,pRn)); break;   // res=pRn-TA
        case RSB:          ctx.E(ppc_subfc(res,pRn,TA)); break;    // res=TA-pRn
        case ADD:case CMN: ctx.E(ppc_addc(res,pRn,TA)); break;
        case ADC:          ctx.E(ppc_adde(res,pRn,TA)); break;
        case SBC:          ctx.E(ppc_subfe(res,TA,pRn)); break;
        case RSC:          ctx.E(ppc_subfe(res,pRn,TA)); break;
        case ORR:          ctx.E(ppc_or(res,pRn,TA)); break;
        case MOV:          if(res!=TA) ctx.E(ppc_mr(res,TA)); break;
        case BIC:          ctx.E(ppc_andc(res,pRn,TA)); break;
        case MVN:          ctx.E(ppc_nor(res,TA,TA)); break;
    }

    if(s){
        switch((DP)dop){
            case ADD:case CMN:
                setNZ(ctx,res); setC_add(ctx); setV_add(ctx,res,TB,TA); break;
            case ADC:
                setNZ(ctx,res); setC_add(ctx); setV_add(ctx,res,TB,TA); break;
            case SUB:case CMP:
                setNZ(ctx,res); setC_sub(ctx); setV_sub(ctx,res,TB,TA); break;
            case RSB:
                // res = TA - pRn; a=TA, b=pRn (stored in TB before op)
                setNZ(ctx,res); setC_sub(ctx); setV_sub(ctx,res,TA,TB); break;
            case SBC:
                setNZ(ctx,res); setC_sub(ctx); setV_sub(ctx,res,TB,TA); break;
            case RSC:
                setNZ(ctx,res); setC_sub(ctx); setV_sub(ctx,res,TA,TB); break;
            default:
                setNZ(ctx,res);
                if(carrySet) setC_fromBit0(ctx,TC);
                break;
        }
    }
    patchSkip(ctx,si);
    return true;
}

// ============================================================
// Branch
// ============================================================
static bool emitBranch(Ctx& ctx, uint32_t op, uint32_t pc) {
    uint8_t cond=(op>>28)&0xF;

    // BX Rm
    if((op&0x0FFFFFF0)==0x012FFF10){
        uint8_t rm=op&0xF; if(rm==15) return false;
        uint8_t pRm=RA[rm];
        size_t si=emitCondSkip(ctx,cond);
        if(si==SIZE_MAX&&cond!=14) return false;

        // Update T bit: CPSR[5] = Rm[0]
        // rlwinm(RCPSR, RCPSR, 0, mb=27, me=25):
        //   complement mask (mb > me): clears bits 27..31 from MSB and 0..25 -- No.
        //   Actually rlwinm with mb>me is NOT the complement form in all encodings.
        //   Use safe approach: clear bit5 from LSB (bit26 from MSB) with two ops.
        // Clear CPSR bit 26 from MSB (bit 5 from LSB = T bit):
        ctx.E(ppc_rlwinm(TA,RCPSR,0,0,25));     // keep bits 0..25 from MSB (clear 26..31)
        ctx.E(ppc_rlwinm(TB,RCPSR,0,27,31));    // keep bits 27..31 from MSB
        ctx.E(ppc_or(RCPSR,TA,TB));             // rejoin (bit26 = 0 = T cleared)
        // Set T = Rm[0]: rotate Rm left 5 so bit0(=bit31) -> bit26 from MSB
        ctx.E(ppc_rlwinm(TA,pRm,5,26,26));      // TA = (pRm << 5) & (1<<26)
        ctx.E(ppc_or(RCPSR,RCPSR,TA));

        // PC = Rm & ~1
        ctx.E(ppc_rlwinm(RA[15],pRm,0,0,30));

        emitSyncTo(ctx);
        emitEpilogue(ctx);

        if(si!=SIZE_MAX){
            patchSkip(ctx,si);
            ctx.li(RA[15],pc+4);
            emitSyncTo(ctx);
            emitEpilogue(ctx);
        }
        ctx.done=true;
        return true;
    }

    // B / BL
    if((op&0x0E000000)==0x0A000000){
        bool lk=(op>>24)&1;
        int32_t off=(int32_t)(op<<8)>>6;
        uint32_t tgt=pc+8+off;
        size_t si=emitCondSkip(ctx,cond);
        if(si==SIZE_MAX&&cond!=14) return false;
        if(lk) ctx.li(RA[14],pc+4);
        ctx.li(RA[15],tgt);
        emitSyncTo(ctx);
        emitEpilogue(ctx);
        if(si!=SIZE_MAX){
            patchSkip(ctx,si);
            ctx.li(RA[15],pc+4);
            emitSyncTo(ctx);
            emitEpilogue(ctx);
        }
        ctx.done=true;
        return true;
    }
    return false;
}

// ============================================================
// Load/Store
// ============================================================
static bool emitLS(Ctx& ctx, uint32_t op, uint32_t pc) {
    uint8_t cond =(op>>28)&0xF;
    bool    ld   =(op>>20)&1;
    bool    by   =(op>>22)&1;
    bool    up   =(op>>23)&1;
    bool    pre  =(op>>24)&1;
    bool    wb   =(op>>21)&1;
    bool    immO =!((op>>25)&1);
    uint8_t rn   =(op>>16)&0xF;
    uint8_t rd   =(op>>12)&0xF;
    if(rd==15||rn==15) return false;
    uint8_t pRn=RA[rn], pRd=RA[rd];

    size_t si=emitCondSkip(ctx,cond);
    if(si==SIZE_MAX&&cond!=14) return false;

    // Compute offset into TA
    if(immO) ctx.li(TA,op&0xFFF);
    else     ctx.E(ppc_mr(TA,RA[op&0xF]));

    // Compute address into TB
    if(pre){
        if(up) ctx.E(ppc_add(TB,pRn,TA));
        else   ctx.E(ppc_subf(TB,TA,pRn));
    } else {
        ctx.E(ppc_mr(TB,pRn));
    }

    // Save offset (TA) and address (TB) before the call clobbers them
    ctx.E(ppc_stw(TA,FRAME_SCR0,1));
    ctx.E(ppc_stw(TB,FRAME_SCR1,1));

    // Setup call args: r3=core, r4=arm7, r5=addr, [r6=value]
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.E(ppc_lwz(TC,FRAME_SCR1,1));
    if(!ld) ctx.E(ppc_mr(TD,pRd));  // pRd is callee-saved, safe to read

    void* fn=ld?(by?(void*)JitHelp_r8:(void*)JitHelp_r32)
               :(by?(void*)JitHelp_w8:(void*)JitHelp_w32);
    ctx.call(fn);

    // After call: callee-saved regs (pRn,pRd,RCPSR,RINTERP) intact
    // TA=r3=return value if load
    if(ld) ctx.E(ppc_mr(pRd,TA));

    // Writeback
    ctx.E(ppc_lwz(TA,FRAME_SCR0,1));  // reload offset
    if(!pre){
        if(up) ctx.E(ppc_add(pRn,pRn,TA));
        else   ctx.E(ppc_subf(pRn,TA,pRn));
    } else if(wb&&rn!=rd){
        ctx.E(ppc_lwz(pRn,FRAME_SCR1,1));
    }

    patchSkip(ctx,si);
    return true;
}

// ============================================================
// Multiply
// ============================================================
static bool emitMul(Ctx& ctx, uint32_t op) {
    bool    s  =(op>>20)&1;
    bool    acc=(op>>21)&1;
    bool    lng=(op>>23)&1;
    uint8_t rd=(op>>16)&0xF,rn=(op>>12)&0xF;
    uint8_t rs=(op>>8)&0xF, rm=op&0xF;
    if(rd==15||rm==15||rs==15) return false;
    if(lng) return false;
    uint8_t pRd=RA[rd],pRn=RA[rn],pRs=RA[rs],pRm=RA[rm];
    if(acc){
        ctx.E(ppc_mullw(TA,pRm,pRs));
        ctx.E(ppc_add(pRd,TA,pRn));
    } else {
        ctx.E(ppc_mullw(pRd,pRm,pRs));
    }
    if(s) setNZ(ctx,pRd);
    return true;
}

// ============================================================
// Fallback to interpreter
// ============================================================
static void emitFallback(Ctx& ctx) {
    emitSyncTo(ctx);
    ctx.E(ppc_mr(TA,RINTERP));
    ctx.call((void*)JitHelp_fallback);
    emitSyncFrom(ctx);
}

// ============================================================
// ARM dispatch
// ============================================================
static bool dispARM(Ctx& ctx, uint32_t op, uint32_t pc) {
    uint8_t cond=(op>>28)&0xF;
    if(cond==15) return false;
    uint32_t it=(op>>25)&7;
    switch(it){
        case 0:case 1:
            if((op&0x0FC000F0)==0x00000090) return emitMul(ctx,op);
            if((op&0x0FFFFFF0)==0x012FFF10||
               (op&0x0FFFFFF0)==0x012FFF30) return emitBranch(ctx,op,pc);
            if((op&0x0FB00FF0)==0x01000000||
               (op&0x0FB00000)==0x03200000||
               (op&0x0DB0F000)==0x010F0000||
               (op&0x0E000090)==0x00000090) return false;
            return emitDP(ctx,op);
        case 2:case 3: return emitLS(ctx,op,pc);
        case 4: return false;
        case 5: return emitBranch(ctx,op,pc);
        default: return false;
    }
}

// ============================================================
// Thumb emitters
// ============================================================
static bool emitT_shifts(Ctx& ctx, uint16_t op) {
    uint8_t ty=(op>>11)&3,rd=op&7,rs=(op>>3)&7;
    int     i=(op>>6)&0x1F;
    uint8_t pRd=RA[rd],pRs=RA[rs];
    switch(ty){
        case 0: shiftLslI(ctx,pRd,pRs,i,true); break;
        case 1: shiftLsrI(ctx,pRd,pRs,i?i:32,true); break;
        case 2: shiftAsrI(ctx,pRd,pRs,i?i:32,true); break;
        default: return false;
    }
    setNZ(ctx,pRd);
    setC_fromBit0(ctx,TC);
    return true;
}

static bool emitT_addSub3(Ctx& ctx, uint16_t op) {
    uint8_t rd=op&7,rs=(op>>3)&7;
    bool sub=(op>>9)&1,imm3=(op>>10)&1;
    uint8_t pRd=RA[rd],pRs=RA[rs];
    if(imm3) ctx.li(TA,(op>>6)&7);
    else     ctx.E(ppc_mr(TA,RA[(op>>6)&7]));
    ctx.E(ppc_mr(TB,pRs));  // save original rs for V calc
    if(sub){
        ctx.E(ppc_subfc(pRd,TA,TB));
        setNZ(ctx,pRd); setC_sub(ctx); setV_sub(ctx,pRd,TB,TA);
    } else {
        ctx.E(ppc_addc(pRd,TB,TA));
        setNZ(ctx,pRd); setC_add(ctx); setV_add(ctx,pRd,TB,TA);
    }
    return true;
}

static bool emitT_imm8(Ctx& ctx, uint16_t op) {
    uint8_t ty=(op>>11)&3,rd=(op>>8)&7;
    uint8_t pRd=RA[rd];
    uint8_t imm=op&0xFF;
    switch(ty){
        case 0: // MOV
            ctx.li(pRd,imm);
            setNZ(ctx,pRd);
            return true;
        case 1:{ // CMP
            ctx.li(TA,imm);
            ctx.E(ppc_mr(TB,pRd));
            ctx.E(ppc_subfc(TC,TA,TB));
            setNZ(ctx,TC); setC_sub(ctx); setV_sub(ctx,TC,TB,TA);
            return true;
        }
        case 2:{ // ADD
            ctx.li(TA,imm);
            ctx.E(ppc_mr(TB,pRd));
            ctx.E(ppc_addc(pRd,TB,TA));
            setNZ(ctx,pRd); setC_add(ctx); setV_add(ctx,pRd,TB,TA);
            return true;
        }
        case 3:{ // SUB
            ctx.li(TA,imm);
            ctx.E(ppc_mr(TB,pRd));
            ctx.E(ppc_subfc(pRd,TA,TB));
            setNZ(ctx,pRd); setC_sub(ctx); setV_sub(ctx,pRd,TB,TA);
            return true;
        }
    }
    return false;
}

static bool emitT_alu(Ctx& ctx, uint16_t op) {
    uint8_t rd=op&7,rs=(op>>3)&7,o=(op>>6)&0xF;
    uint8_t pRd=RA[rd],pRs=RA[rs];
    switch(o){
        case 0:  ctx.E(ppc_and(pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case 1:  ctx.E(ppc_xor(pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case 2:  ctx.E(ppc_slw(pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case 3:  ctx.E(ppc_srw(pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case 4:  ctx.E(ppc_sraw(pRd,pRd,pRs));setNZ(ctx,pRd); break;
        case 5:{ // ADC
            ctx.E(ppc_rlwinm(TA,RCPSR,0,29,29));
            ctx.E(ppc_mtxer(TA));
            ctx.E(ppc_mr(TB,pRd));
            ctx.E(ppc_adde(pRd,TB,pRs));
            setNZ(ctx,pRd); setC_add(ctx); setV_add(ctx,pRd,TB,pRs); break;
        }
        case 6:{ // SBC
            ctx.E(ppc_rlwinm(TA,RCPSR,0,29,29));
            ctx.E(ppc_mtxer(TA));
            ctx.E(ppc_mr(TB,pRd));
            ctx.E(ppc_subfe(pRd,pRs,TB));
            setNZ(ctx,pRd); setC_sub(ctx); setV_sub(ctx,pRd,TB,pRs); break;
        }
        case 7:{ // ROR
            ctx.E(ppc_subfic(TA,pRs,32));
            ctx.E(ppc_rlwnm(pRd,pRd,TA,0,31));
            setNZ(ctx,pRd); break;
        }
        case 8:{ // TST
            ctx.E(ppc_and(TA,pRd,pRs)); setNZ(ctx,TA); break;
        }
        case 9:{ // NEG
            ctx.E(ppc_mr(TB,pRd));
            ctx.E(ppc_addi(TA,0,0));
            ctx.E(ppc_subfc(pRd,TB,TA));
            setNZ(ctx,pRd); setC_sub(ctx); setV_sub(ctx,pRd,TA,TB); break;
        }
        case 10:{ // CMP
            ctx.E(ppc_mr(TB,pRd));
            ctx.E(ppc_subfc(TA,pRs,TB));
            setNZ(ctx,TA); setC_sub(ctx); setV_sub(ctx,TA,TB,pRs); break;
        }
        case 11:{ // CMN
            ctx.E(ppc_mr(TB,pRd));
            ctx.E(ppc_addc(TA,TB,pRs));
            setNZ(ctx,TA); setC_add(ctx); setV_add(ctx,TA,TB,pRs); break;
        }
        case 12: ctx.E(ppc_or(pRd,pRd,pRs));  setNZ(ctx,pRd); break;
        case 13: ctx.E(ppc_mullw(pRd,pRd,pRs));setNZ(ctx,pRd); break;
        case 14: ctx.E(ppc_andc(pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case 15: ctx.E(ppc_nor(pRd,pRs,pRs));  setNZ(ctx,pRd); break;
        default: return false;
    }
    return true;
}

static bool emitT_hiReg(Ctx& ctx, uint16_t op) {
    uint8_t o=(op>>8)&3,h1=(op>>7)&1,h2=(op>>6)&1;
    uint8_t rs=((op>>3)&7)|(h2<<3), rd=(op&7)|(h1<<3);
    if(rd==15||rs==15) return false;
    uint8_t pRd=RA[rd],pRs=RA[rs];
    switch(o){
        case 0: ctx.E(ppc_add(pRd,pRd,pRs)); break;
        case 1:{
            ctx.E(ppc_mr(TB,pRd));
            ctx.E(ppc_subfc(TA,pRs,TB));
            setNZ(ctx,TA); setC_sub(ctx); setV_sub(ctx,TA,TB,pRs); break;
        }
        case 2: ctx.E(ppc_mr(pRd,pRs)); break;
        case 3:{ // BX
            // T = Rs[0]
            ctx.E(ppc_rlwinm(TA,RCPSR,0,0,25));
            ctx.E(ppc_rlwinm(TB,RCPSR,0,27,31));
            ctx.E(ppc_or(RCPSR,TA,TB));
            ctx.E(ppc_rlwinm(TA,pRs,5,26,26));
            ctx.E(ppc_or(RCPSR,RCPSR,TA));
            ctx.E(ppc_rlwinm(RA[15],pRs,0,0,30));
            emitSyncTo(ctx);
            emitEpilogue(ctx);
            ctx.done=true; break;
        }
    }
    return true;
}

static bool emitT_ldrPc(Ctx& ctx, uint16_t op, uint32_t pc) {
    uint8_t  rd=(op>>8)&7;
    uint32_t addr=((pc+4)&~3u)+((op&0xFF)<<2);
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.li(TC,addr);
    ctx.call((void*)JitHelp_r32);
    ctx.E(ppc_mr(RA[rd],TA));
    return true;
}

static bool emitT_memReg(Ctx& ctx, uint16_t op) {
    // Encoding: op[15:9] determines type
    uint8_t rb=(op>>3)&7, ro=(op>>6)&7;
    uint8_t pRb=RA[rb],pRo=RA[ro];
    uint8_t op79=(op>>9)&7;
    bool ld=(op>>11)&1;
    void* fn=nullptr;
    uint8_t rd=op&7;
    uint8_t pRd=RA[rd];

    // LDR/STR register: bits[15:12]=0101, bits[11:9]=L,B,?
    switch(op79){
        case 0b000: fn=(void*)JitHelp_w32; break;  // STR  Rd,[Rb,Ro]
        case 0b001: fn=(void*)JitHelp_w8;  break;  // STRB Rd,[Rb,Ro]
        case 0b010: fn=(void*)JitHelp_r16; break;  // LDRH
        case 0b011: fn=(void*)JitHelp_r8;  break;  // LDSB (sign - handle after)
        case 0b100: fn=(void*)JitHelp_r32; break;  // LDR
        case 0b101: fn=(void*)JitHelp_r8;  break;  // LDRB
        case 0b110: fn=(void*)JitHelp_r16; break;  // LDSH
        case 0b111: fn=(void*)JitHelp_r16; break;  // LDRSH (sign)
        default: return false;
    }
    ctx.E(ppc_add(TC,pRb,pRo));
    ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if(!ld) ctx.E(ppc_mr(TD,pRd));
    ctx.call(fn);
    if(ld){
        // Sign-extend for signed loads
        if(op79==0b011) ctx.E(ppc_extsb(pRd,TA));  // LDSB
        else if(op79==0b111) ctx.E(ppc_extsh(pRd,TA));  // LDSH
        else ctx.E(ppc_mr(pRd,TA));
    }
    return true;
}

static bool emitT_memImm(Ctx& ctx, uint16_t op) {
    uint8_t rd=op&7, rb=(op>>3)&7;
    uint8_t pRd=RA[rd],pRb=RA[rb];
    bool    ld=(op>>11)&1;
    uint8_t h=(op>>12)&0xF;
    // h=6: STR/LDR word imm5*4
    // h=7: STRB/LDRB byte imm5*1
    // h=8: STRH/LDRH halfword imm5*2
    bool by=(h==7),hw=(h==8);
    uint32_t off=((op>>6)&0x1F)*(hw?2:(by?1:4));
    ctx.li(TC,off);
    ctx.E(ppc_add(TC,pRb,TC));
    ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if(!ld) ctx.E(ppc_mr(TD,pRd));
    void* fn=ld?(hw?(void*)JitHelp_r16:(by?(void*)JitHelp_r8:(void*)JitHelp_r32))
               :(hw?(void*)JitHelp_w16:(by?(void*)JitHelp_w8:(void*)JitHelp_w32));
    ctx.call(fn);
    if(ld) ctx.E(ppc_mr(pRd,TA));
    return true;
}

static bool emitT_spLoad(Ctx& ctx, uint16_t op, uint32_t pc) {
    bool    ld=(op>>11)&1;
    uint8_t rd=(op>>8)&7;
    uint8_t pRd=RA[rd];
    bool    sp=((op>>12)&0xF)==0x9;
    uint32_t off=(op&0xFF)<<2;
    if(sp){
        ctx.li(TA,off);
        ctx.E(ppc_add(TC,RA[13],TA));
    } else {
        ctx.li(TC,((pc+4)&~3u)+off);
    }
    ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if(!ld) ctx.E(ppc_mr(TD,pRd));
    ctx.call(ld?(void*)JitHelp_r32:(void*)JitHelp_w32);
    if(ld) ctx.E(ppc_mr(pRd,TA));
    return true;
}

static bool emitT_branch(Ctx& ctx, uint16_t op, uint32_t pc) {
    uint8_t h=(op>>12)&0xF;
    if(h==0xE){ // unconditional
        int32_t off=(int32_t)(op<<21)>>20;
        ctx.li(RA[15],(uint32_t)(pc+4+off));
        emitSyncTo(ctx); emitEpilogue(ctx);
        ctx.done=true; return true;
    }
    if(h==0xD){ // conditional
        uint8_t cond=(op>>8)&0xF;
        if(cond==0xF) return false;
        int32_t off=(int8_t)(op&0xFF); off<<=1;
        uint32_t tgt=pc+4+off, fall=pc+2;
        emitLoadCR7(ctx);
        CB cb=condPpc(cond); if(!cb.ok) return false;
        uint8_t inv=(cb.bo==12)?4:(cb.bo==4?12:cb.bo);
        size_t si=ctx.sz();
        ctx.E(ppc_bc(inv,cb.bi,0));
        ctx.li(RA[15],tgt);
        emitSyncTo(ctx); emitEpilogue(ctx);
        patchSkip(ctx,si);
        ctx.li(RA[15],fall);
        emitSyncTo(ctx); emitEpilogue(ctx);
        ctx.done=true; return true;
    }
    return false;
}

static bool emitT_bl(Ctx& ctx, uint16_t op1, uint16_t op2, uint32_t pc) {
    int32_t hi=(int32_t)((op1&0x7FF)<<21)>>9;
    int32_t lo=(op2&0x7FF)<<1;
    uint32_t tgt=pc+4+hi+lo;
    bool blx=((op2>>11)&0x1F)==0x1C;
    ctx.li(RA[14],(pc+4)|1u);
    if(blx){
        ctx.li(RA[15],tgt&~3u);
        ctx.E(ppc_rlwinm(TA,RCPSR,0,0,25));  // clear T
        ctx.E(ppc_rlwinm(TB,RCPSR,0,27,31));
        ctx.E(ppc_or(RCPSR,TA,TB));
    } else {
        ctx.li(RA[15],tgt&~1u);
    }
    emitSyncTo(ctx); emitEpilogue(ctx);
    ctx.done=true; return true;
}

// ============================================================
// Thumb dispatch
// ============================================================
static bool dispThumb(Ctx& ctx, uint16_t op, uint32_t pc) {
    uint8_t h=(op>>12)&0xF;
    switch(h){
        case 0x0: {
            uint8_t b11=(op>>11)&3;
            if(b11<3) return emitT_shifts(ctx,op);
            return emitT_addSub3(ctx,op);
        }
        case 0x1: return emitT_imm8(ctx,op);
        case 0x2: {
            uint8_t b10=(op>>10)&3;
            if(b10==0) return emitT_alu(ctx,op);
            if(b10==1) return emitT_hiReg(ctx,op);
            return emitT_ldrPc(ctx,op,pc);
        }
        case 0x3: return emitT_memImm(ctx,op);   // STR/LDR word imm? Actually...
        // Let me reconsider the encoding:
        // h=0x5 (0101) = LDR/STR register offset
        // h=0x6 (0110) = LDR/STR word imm5
        // h=0x7 (0111) = LDRB/STRB byte imm5
        // h=0x8 (1000) = LDRH/STRH halfword imm5
        // h=0x9 (1001) = LDR/STR SP-relative
        // h=0xA (1010) = ADD PC/SP
        // h=0xD (1101) = conditional branch / SWI
        // h=0xE (1110) = unconditional branch
        // h=0xF (1111) = BL/BLX prefix
        case 0x4: return false; // LDRH/STRH register (signed) - complex
        case 0x5: return emitT_memReg(ctx,op);
        case 0x6: return emitT_memImm(ctx,op);
        case 0x7: return emitT_memImm(ctx,op);
        case 0x8: return emitT_memImm(ctx,op);
        case 0x9: return emitT_spLoad(ctx,op,pc);
        case 0xA: return false; // ADD PC/SP -> imm
        case 0xB: return false; // push/pop/misc
        case 0xC: return false; // LDM/STM
        case 0xD: return emitT_branch(ctx,op,pc);
        case 0xE: return emitT_branch(ctx,op,pc);
        case 0xF: return false; // BL prefix (handled in block loop)
        default:  return false;
    }
}

// ============================================================
// Valid PC
// ============================================================
static bool validPC(uint32_t pc, bool gba) {
    pc&=~1u;
    if(gba) return pc<0x4000u||
                   (pc>=0x02000000u&&pc<0x02040000u)||
                   (pc>=0x03000000u&&pc<0x03008000u)||
                   (pc>=0x08000000u&&pc<0x0E000000u);
    return pc<0x4000u||
           (pc>=0x02000000u&&pc<0x02400000u)||
           (pc>=0x03000000u&&pc<0x03800000u)||
           (pc>=0xFFFF0000u);
}

// ============================================================
// Compile a block
// ============================================================
static JitBlock* compile(Interpreter* interp, Core* core,
                          uint32_t armPC, bool arm7) {
    if(!validPC(armPC,core->gbaMode)) return nullptr;
    bool    thumb=interp->isThumb();
    size_t  bkt=hashPC(armPC);
    JitBlock& slot=cache[bkt];
    if(slot.valid&&slot.armPC==armPC&&slot.thumb==thumb) return &slot;

    if(codePos+BLK_MAX_WDS>=JIT_WORDS) flushJitCache();

    Ctx ctx;
    ctx.base=codeBuf+codePos; ctx.cur=ctx.base;
    ctx.cap=JIT_WORDS-codePos;
    ctx.thumb=thumb; ctx.arm7=arm7;
    ctx.blockPC=armPC; ctx.interp=interp; ctx.core=core;
    ctx.done=false;

    emitPrologue(ctx);
    emitSyncFrom(ctx);

    uint32_t pc=armPC; int n=0;

    while(n<(int)BLK_MAX_ARMS && !ctx.done){
        // ARM pipeline offset in PC register
        ctx.li(RA[15], pc+(thumb?4u:8u));

        if(thumb){
            uint16_t op=core->memory.read<uint16_t>(arm7,pc);
            // Two-halfword BL/BLX?
            if(((op>>11)&0x1F)==0x1E){
                uint16_t op2=core->memory.read<uint16_t>(arm7,pc+2);
                uint8_t bb=(op2>>11)&0x1F;
                if(bb==0x1F||bb==0x1C){
                    emitT_bl(ctx,op,op2,pc);
                    pc+=4; n+=2; continue;
                }
            }
            bool ok=dispThumb(ctx,op,pc);
            if(!ok){ emitFallback(ctx); ctx.done=true; }
            else{
                pc+=2; n++;
                if(ctx.done) break;
                uint8_t h=(op>>12)&0xF;
                if(h==0xD||h==0xE) ctx.done=true;
            }
        } else {
            uint32_t op=core->memory.read<uint32_t>(arm7,pc);
            bool ok=dispARM(ctx,op,pc);
            if(!ok){ emitFallback(ctx); ctx.done=true; }
            else{
                pc+=4; n++;
                if(ctx.done) break;
                uint32_t it=(op>>25)&7;
                if(it==5) ctx.done=true;
                if((op&0x0FFFFFF0)==0x012FFF10||
                   (op&0x0FFFFFF0)==0x012FFF30) ctx.done=true;
            }
        }
    }

    if(!ctx.done){
        emitSyncTo(ctx);
        emitEpilogue(ctx);
    }

    size_t wds=ctx.sz();
    dcbst(ctx.base,wds);
    slot={armPC,ctx.base,(uint32_t)wds,thumb,true};
    codePos+=wds;
    return &slot;
}

// ============================================================
// Run loops
// ============================================================
void runJitNds(Core& core) {
    for(int cpu=0;cpu<2;cpu++){
        if(core.interpreter[cpu].halted) continue;
        uint32_t pc=core.interpreter[cpu].getActualPC();
        JitBlock* b=compile(&core.interpreter[cpu],&core,pc,cpu==1);
        if(b) executeBlock_asm(b->code);
        else  core.interpreter[cpu].jitRunOpcode();
    }
    JitHelp_tick(&core);
}

void runJitGba(Core& core) {
    if(!core.interpreter[1].halted){
        uint32_t pc=core.interpreter[1].getActualPC();
        JitBlock* b=compile(&core.interpreter[1],&core,pc,true);
        if(b) executeBlock_asm(b->code);
        else  core.interpreter[1].jitRunOpcode();
    }
    JitHelp_tick(&core);
}

// ============================================================
// Init / shutdown / invalidate
// ============================================================
bool initJit(Core* core) {
    codeBuf=(uint32_t*)memalign(32,JIT_BYTES);
    if(!codeBuf){ printf("[JIT] alloc fail\n"); return false; }
    codePos=0;
    for(size_t i=0;i<CSIZ;i++) cache[i].valid=false;
    printf("[JIT] buf=%p (%zuKB)\n",(void*)codeBuf,JIT_BYTES>>10);
    if(core) core->setRunFunc(core->gbaMode?runJitGba:runJitNds);
    return true;
}

void shutdownJit(Core* core) {
    if(core) core->setRunFunc(core->gbaMode
        ?static_cast<void(*)(Core&)>(&Interpreter::runCoreSingle<true,0>)
        :&Interpreter::runCoreNds);
    free(codeBuf); codeBuf=nullptr;
}

void invalidateJitRange(uint32_t start, uint32_t end) {
    for(size_t i=0;i<CSIZ;i++)
        if(cache[i].valid&&cache[i].armPC>=start&&cache[i].armPC<end)
            cache[i].valid=false;
}

} // namespace JitPpc
