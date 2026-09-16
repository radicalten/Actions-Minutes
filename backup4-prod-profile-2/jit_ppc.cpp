// jit_ppc.cpp — ARM (ARMv4T / ARMv5TE-lite) -> PowerPC (Broadway, Wii) dynamic recompiler
//
// Revision 9 — triage fixes for "NDS white screen / GBA black screen" hangs.
//
//   F1  setC_xer()   mask (0,29,29) kept XER value bit 2 (reserved, always 0) -> C never set.
//                    CA is XER value bit 29 == MSB-relative bit 2 == CPSR.C: no rotate needed.
//   F2  setV_add/sub  mask (29,28,28) masks value bit 3 while the rotate produced value bit 28
//                    -> the AND is always 0, V was never asserted.  Mask must be (3,3).
//   F3  operand destruction: setNZ() used mfcr TA, and setC_xer() used mfxer TA, both of which
//                    destroyed op2/Rn that the V emitters read afterwards.  Both now use TG (r9),
//                    which is only live inside emitBlockXfer()'s argument setup.
//   F4  setC_imm()   mask (29,29) -> (2,2) so the shift-out bit actually reaches CPSR.C.
//   F5  register-specified shifts now produce the ARM carry (amount 0 keeps C, amount 32/33+
//                    are handled explicitly; PPC shifts by >=32 are not ARM shifts).
//   F6  Thumb ALU LSL/LSR/ASR/ROR now produce the carry too (same helper as F5).
//   F6b the shifter carry lives in RSHC (r10), not TC (r5), so CMP/CMN/TST/TEQ - which use TC as
//                    their result - no longer overwrite it before setC_imm() reads it.
//   F7  emitLS() writeback: pre-index writeback is no longer dropped when rd==rn, post-index
//                    writeback is computed from the saved base instead of the loaded register.
//   F8  block yield: BLK_ARMS 8 -> 48, BLK_WDS 800 -> 2600, 2-way block cache with LRU,
//                    invalidation gated by a per-page "has JIT code" bitmap (O(1) for data writes).
//                    Cycle charge for EXIT_FALLBACK no longer discards the block's own work.
//   F9  event-dispatch counters (tickInline + JitHelp_tick) so the log can distinguish
//                    "stuck in code" from "event never dispatched".
//   F10 stuck-PC watchdog: ring buffer of the last 32 blocks, dumps on a 4000x PC repeat.
//   F11 differential harness: for the first JIT_DIFF_BLOCKS store-free blocks, re-run the same
//                    instructions under the interpreter and log the first diverging register/CPSR.
//
// Each behaviour change can be disabled individually for bisecting (see the switch block below).
// The interpreter remains the ground truth: every switch below degenerates to "send it to
// jitRunOpcode()", which is the behaviour you already know works.

#include "jit_ppc.h"
#include "core.h"
#include "interpreter.h"
#include "memory.h"
#include "defines.h"
#include "debug_log.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <malloc.h>

extern "C" {
#include <ogc/cache.h>
#include <ogc/system.h>
}

// ═══════════════════════════════════════════════════════════════════════
// Switch block — flip these to bisect.  Defaults are the fixed build.
// ═══════════════════════════════════════════════════════════════════════
#define JIT_NO_S            0   // 1 = every S-flagged ALU op goes to the interpreter (bisect #1)
#define JIT_NO_THUMB        0   // 1 = no Thumb codegen at all (bisect #2)
#define JIT_NO_BLOCKXFER    0   // 1 = no LDM/STM/LDMIA codegen (bisect #4)
#define JIT_REG_SHIFT_CARRY 1   // 0 = reg-shift + S falls back instead of using F5
#define JIT_THUMB_ALU_CARRY 1   // 0 = Thumb ALU shifts fall back instead of using F6
#define JIT_LONG_BLOCKS     1   // 1 = blocks end only at branches/fallback/limits (F8)
#define JIT_WATCHDOG        1   // 1 = pinned-PC ring dump (F10)
#define JIT_EV_COUNTERS     1   // 1 = count every scheduler dispatch + IRQ (F9)
#define JIT_DIFF            1   // 1 = differential JIT-vs-interpreter check (F11)
#define JIT_INVALIDATE      1   // 1 = honour invalidateJitRange() (required unless bisecting)
#define JIT_CHARGE_MAX      1   // 1 = charge max(c0,c1*2) as before; 0 = charge the sum

static const int EXIT_NORMAL   = 0;
static const int EXIT_FALLBACK = 1;

static uint32_t g_exitPC[2]    = {};
static uint32_t g_exitCPSR[2]  = {};
static int      g_exitReason[2]= {};

static const uint32_t CYCLES_PER_INSN_ARM9 = 2;
static const uint32_t CYCLES_PER_INSN_ARM7 = 1;

static const int ITERS_NDS = 32;
static const int ITERS_GBA = 32;

// ── Frame layout ──────────────────────────────────────────────────────
static const int FRAME_SIZE    = 256;
static const int FRAME_LR_OFF  = FRAME_SIZE + 4;
static const int FRAME_SAVE    = 16;
static const int FRAME_CORE    = 88;
static const int FRAME_INTERP  = 92;
static const int FRAME_CPUIDX  = 96;
static const int FRAME_SCR0    = 100;   // shift scratch 0 (op2)
static const int FRAME_SCR1    = 104;   // shift scratch 1 (address / Rn)
static const int FRAME_SCR2    = 108;   // shift scratch 2 (R15 value)
static const int FRAME_REGSYNC = 112;
static const int FRAME_CPSR    = 172;
static const int FRAME_PC      = 176;

static_assert(FRAME_SIZE % 16 == 0,            "frame align");
static_assert(FRAME_SAVE + 18*4 == FRAME_CORE, "save map");
static_assert(FRAME_REGSYNC + 15*4 == FRAME_CPSR, "regsync map");
static_assert(FRAME_PC + 4 <= FRAME_SIZE,       "pc fits");

// ── Debug logging ─────────────────────────────────────────────────────
static const size_t FB_LOG_MAX = 512;
struct FbEntry { uint32_t pc; uint32_t op; };
static FbEntry  g_fbLog[FB_LOG_MAX];
static size_t   g_fbLogCount  = 0;
static uint32_t g_totalFB[2]  = {};
static uint32_t g_totalJIT[2] = {};

static bool fbAlreadyLogged(uint32_t pc, uint32_t op) {
    for (size_t i = 0; i < g_fbLogCount; i++)
        if (g_fbLog[i].pc == pc && g_fbLog[i].op == op) return true;
    return false;
}
static void fbLogOnce(bool thumb, int cpu, uint32_t pc, uint32_t op) {
    g_totalFB[cpu]++;
    if (fbAlreadyLogged(pc, op)) return;
    if (g_fbLogCount < FB_LOG_MAX) {
        g_fbLog[g_fbLogCount].pc = pc;
        g_fbLog[g_fbLogCount].op = op;
        g_fbLogCount++;
    }
    if (thumb) DebugLog("[JIT] thumb FB cpu%d pc=%08X op=%04X\n", cpu, pc, op);
    else       DebugLog("[JIT] arm   FB cpu%d pc=%08X op=%08X\n", cpu, pc, op);
}

namespace JitPpc {

// ═══════════════════════════════════════════════════════════════════════
// PPC encoders
// ═══════════════════════════════════════════════════════════════════════
static inline uint32_t ppc_blr()  { return 0x4E800020u; }
static inline uint32_t ppc_bctr(bool lk=false) {
    return (19u<<26)|(20u<<21)|(528u<<1)|(lk?1u:0u);
}
static inline uint32_t ppc_bc(uint8_t bo,uint8_t bi,int16_t off,bool lk=false){
    return (16u<<26)|((bo&31u)<<21)|((bi&31u)<<16)|((uint32_t)(off&0xFFFC))|(lk?1u:0u);
}
static inline uint32_t ppc_b(int32_t off,bool lk=false){
    return (18u<<26)|((uint32_t)(off&0x03FFFFFC))|(lk?1u:0u);
}
static inline uint32_t ppc_addi(uint8_t rt,uint8_t ra,int16_t i){
    return (14u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i;
}
static inline uint32_t ppc_addis(uint8_t rt,uint8_t ra,int16_t i){
    return (15u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i;
}
static inline uint32_t ppc_addic(uint8_t rt,uint8_t ra,int16_t i){
    return (12u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i;
}
static inline uint32_t ppc_ori(uint8_t ra,uint8_t rs,uint16_t i){
    return (24u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i;
}
static inline uint32_t ppc_andi_record(uint8_t ra,uint8_t rs,uint16_t i){
    return (28u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i;   // andi. -> CR0
}
static inline uint32_t ppc_stw(uint8_t rs,int16_t d,uint8_t ra){
    return (36u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d;
}
static inline uint32_t ppc_stwu(uint8_t rs,int16_t d,uint8_t ra){
    return (37u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d;
}
static inline uint32_t ppc_lwz(uint8_t rt,int16_t d,uint8_t ra){
    return (32u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d;
}
static inline uint32_t ppc_cmpi(uint8_t cr,uint8_t ra,int16_t i){
    return (11u<<26)|((cr&7u)<<23)|((uint32_t)ra<<16)|(uint16_t)i;
}
static inline uint32_t ppc_subfic(uint8_t rt,uint8_t ra,int16_t i){
    return (8u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i;
}
static inline uint32_t Xf(uint8_t rt,uint8_t ra,uint8_t rb,uint32_t x,bool rc=false){
    return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|
           ((uint32_t)rb<<11)|(x<<1)|(rc?1u:0u);
}
static inline uint32_t XOf(uint8_t rt,uint8_t ra,uint8_t rb,bool oe,uint32_t x,bool rc=false){
    return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|
           ((uint32_t)rb<<11)|(oe?0x400u:0u)|(x<<1)|(rc?1u:0u);
}
static inline uint32_t ppc_add  (uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,266);}
static inline uint32_t ppc_addc (uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,10); }
static inline uint32_t ppc_adde (uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,138);}
static inline uint32_t ppc_subf (uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,40); }
static inline uint32_t ppc_subfc(uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,8);  }
static inline uint32_t ppc_subfe(uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,136);}
static inline uint32_t ppc_mullw(uint8_t d,uint8_t a,uint8_t b){return XOf(d,a,b,false,235);}
static inline uint32_t ppc_and  (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,28); }
static inline uint32_t ppc_or   (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,444);}
static inline uint32_t ppc_xor  (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,316);}
static inline uint32_t ppc_andc (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,60); }
static inline uint32_t ppc_nor  (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,124);}
static inline uint32_t ppc_mr   (uint8_t a,uint8_t s)           {return ppc_or(a,s,s);}
static inline uint32_t ppc_slw  (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,24); }
static inline uint32_t ppc_srw  (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,536);}
static inline uint32_t ppc_sraw (uint8_t a,uint8_t s,uint8_t b){return Xf(s,a,b,792);}
static inline uint32_t ppc_extsb(uint8_t a,uint8_t s)           {return Xf(s,a,0,954);}
static inline uint32_t ppc_extsh(uint8_t a,uint8_t s)           {return Xf(s,a,0,922);}
static inline uint32_t ppc_rlwinm(uint8_t a,uint8_t s,uint8_t sh,
                                   uint8_t mb,uint8_t me,bool rc=false){
    return (21u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)|
           ((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1)|(rc?1u:0u);
}
static inline uint32_t ppc_rlwimi(uint8_t a,uint8_t s,uint8_t sh,uint8_t mb,uint8_t me){
    return (20u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)|
           ((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1);
}
static inline uint32_t ppc_rlwnm(uint8_t a,uint8_t s,uint8_t b,uint8_t mb,uint8_t me){
    return (23u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)|
           ((uint32_t)b<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1);
}
static inline uint32_t ppc_srawi(uint8_t a,uint8_t s,uint8_t sh){
    return (31u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)|((uint32_t)sh<<11)|(824u<<1);
}
static inline uint32_t ppc_mtspr(uint16_t spr,uint8_t rs){
    uint8_t lo=spr&31,hi=(spr>>5)&31;
    return (31u<<26)|((uint32_t)rs<<21)|((uint32_t)lo<<16)|((uint32_t)hi<<11)|(467u<<1);
}
static inline uint32_t ppc_mfspr(uint8_t rt,uint16_t spr){
    uint8_t lo=spr&31,hi=(spr>>5)&31;
    return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)lo<<16)|((uint32_t)hi<<11)|(339u<<1);
}
static inline uint32_t ppc_mtctr(uint8_t s){return ppc_mtspr(9,s);}
static inline uint32_t ppc_mtlr (uint8_t s){return ppc_mtspr(8,s);}
static inline uint32_t ppc_mflr (uint8_t t){return ppc_mfspr(t,8);}
static inline uint32_t ppc_mtxer(uint8_t s){return ppc_mtspr(1,s);}
static inline uint32_t ppc_mfxer(uint8_t t){return ppc_mfspr(t,1);}
static inline uint32_t ppc_mfcr (uint8_t t){
    return (31u<<26)|((uint32_t)t<<21)|(19u<<1);
}

static int emit_li32(uint32_t* out,uint8_t rt,uint32_t v){
    uint16_t hi=(uint16_t)(v>>16),lo=(uint16_t)(v&0xFFFF);
    if(!hi&&!lo){out[0]=ppc_addi(rt,0,0);return 1;}
    if(!hi){
        if(lo<0x8000){out[0]=ppc_addi(rt,0,(int16_t)lo);return 1;}
        out[0]=ppc_addi(rt,0,0);out[1]=ppc_ori(rt,rt,lo);return 2;
    }
    if(!lo){out[0]=ppc_addis(rt,0,(int16_t)hi);return 1;}
    out[0]=ppc_addis(rt,0,(int16_t)hi);
    out[1]=ppc_ori(rt,rt,lo);
    return 2;
}

// ARM register map: RA[0..14] = r14..r28, RCPSR = r29.
// Scratch: r3..r12 are volatile; r2 (RTOC) and r13 (SDA) are avoided on purpose.
static const uint8_t RA[15]={14,15,16,17,18,19,20,21,22,23,24,25,26,27,28};
static const uint8_t RCPSR=29;
static const uint8_t TA=3,TB=4,TC=5,TD=6,TE=7,TF=8,TG=9;
static const uint8_t RSHC=10;      // shifter carry-out (LSB holds the ARM C bit)
static const uint8_t RCALL=11;
static const uint8_t TK=12;        // shift-amount scratch

// ═══════════════════════════════════════════════════════════════════════
// Code buffer + block cache
// ═══════════════════════════════════════════════════════════════════════
static const size_t JIT_BYTES=2u*1024u*1024u;
static const size_t JIT_WORDS=JIT_BYTES/4;
#if JIT_LONG_BLOCKS
static const size_t BLK_ARMS=48;
static const size_t BLK_WDS =2600;
#else
static const size_t BLK_ARMS=8;
static const size_t BLK_WDS =800;
#endif

static uint32_t* codeBuf =nullptr;
static size_t    codePos =0;
static uint32_t  cacheGen=0;
static bool      g_jitLive=false;

struct JitBlock {
    uint32_t  armPC;
    uint32_t  endPC;          // first PC not covered by this block (for invalidation)
    uint32_t* code;
    uint32_t  nW;
    uint32_t  gen;
    uint32_t  insnCount;
    uint32_t  use;            // LRU stamp
    bool      thumb;
    bool      hasStore;       // block contains a memory store (diff harness skips those)
    bool      valid;
};

static const size_t CSIZ=1u<<13;      // buckets
static JitBlock cache[CSIZ*2];        // 2-way
static uint32_t g_useCounter=0;

// Per-4KB-page "has JIT code" bitmap: makes invalidation O(1) for the overwhelmingly
// common case of a store into memory that never held compiled code.
static const uint32_t PAGE_BITS_BYTES = 256u*1024u*1024u;
static uint8_t  g_pageSeen[PAGE_BITS_BYTES>>12];
static inline size_t pageIdx(uint32_t addr){ return (size_t)(addr>>12); }
static inline void markPage(uint32_t addr){
    if(addr<PAGE_BITS_BYTES) g_pageSeen[pageIdx(addr)]=1;
}

static inline size_t bucketOf(uint32_t pc){ return ((pc>>1)&(CSIZ-1))*2; }

void flushJitCache(){
    codePos=0;++cacheGen;
    for(size_t i=0;i<CSIZ*2;i++)cache[i].valid=false;
    memset(g_pageSeen,0,sizeof(g_pageSeen));
    DebugLog("[JIT] cache flushed gen=%u\n",cacheGen);
}

static void flushICache(uint32_t* p,size_t nW){
    DCFlushRange(p,nW*4);
    ICInvalidateRange(p,nW*4);
}

// ═══════════════════════════════════════════════════════════════════════
// Emit context
// ═══════════════════════════════════════════════════════════════════════
static const int MAX_MARK=12, MAX_FIX=32;

struct Ctx {
    uint32_t *base,*cur;
    size_t cap;
    bool thumb,arm7,done,overflow,hasStore;
    uint32_t blockPC;
    int cpuIdx;
    Interpreter* interp;
    Core* core;
    int insnCount;

    size_t mark[MAX_MARK];
    struct Fixup { uint32_t at; uint8_t mk; };
    Fixup  fix[MAX_FIX];
    int    nFix;
    int    nextMark;

    void E(uint32_t w){
        if((size_t)(cur-base)<cap)*cur++=w;
        else overflow=true;
    }
    size_t sz()  const{return(size_t)(cur-base);}
    size_t rem() const{size_t u=sz();return u<cap?cap-u:0;}

    void li(uint8_t rt,uint32_t v){
        uint32_t t[2];int n=emit_li32(t,rt,v);
        for(int i=0;i<n;i++)E(t[i]);
    }
    void call(void* fn){
        uint32_t a=(uint32_t)(uintptr_t)fn;
        if(a<0x80000000u||a>=0x81800000u){overflow=true;return;}
        uint16_t hi=(uint16_t)(a>>16),lo=(uint16_t)(a&0xFFFF);
        E(ppc_addis(RCALL,0,(int16_t)hi));
        if(lo)E(ppc_ori(RCALL,RCALL,lo));
        E(ppc_mtctr(RCALL));
        E(ppc_bctr(true));
    }
    void ldCore()  {E(ppc_lwz(TA,FRAME_CORE,  1));}
    void ldInterp(){E(ppc_lwz(TA,FRAME_INTERP,1));}
    void ldCpu()   {E(ppc_lwz(TB,FRAME_CPUIDX,1));}

    // ── local branch fixups ───────────────────────────────────────────
    int  NEWMARK(){ return (nextMark<MAX_MARK)?nextMark++:-1; }
    void SETMARK(int m){ if(m>=0&&m<MAX_MARK) mark[m]=sz(); }
    void FWD(uint8_t bo,uint8_t bi,int m){          // branch forward to SETMARK(m)
        if(m>=0&&nFix<MAX_FIX){ fix[nFix].at=(uint32_t)sz(); fix[nFix].mk=(uint8_t)m; nFix++; }
        E(ppc_bc(bo,bi,0));
    }
    // RESOLVE_FROM lets a nested emitter (emitRegShiftCarry) resolve only the fixups it
    // created, so an enclosing condition-skip fixup stays pending until its own emitter
    // closes it.  Marks are indexed, so nested allocation above nextMark cannot collide.
    int  baseFix() const { return nFix; }
    void RESOLVE_FROM(int b0){
        if(b0<0)b0=0;
        for(int i=b0;i<nFix;i++){
            size_t at=fix[i].at; int m=fix[i].mk;
            if(at>=cap||m<0||m>=MAX_MARK) continue;
            int32_t d=(int32_t)((mark[m]-at)*4);
            if(d<0||d>32764){ overflow=true; continue; }
            base[at]|=(uint32_t)((uint16_t)d);      // low 2 bits of BD are always 0
        }
        nFix=b0;
    }
    void RESOLVE(){ RESOLVE_FROM(0); nextMark=0; }
};

// ═══════════════════════════════════════════════════════════════════════
// C helpers
// ═══════════════════════════════════════════════════════════════════════
static uint32_t g_evCount[MAX_TASKS] = {};
static uint32_t g_irqCount[2]        = {};

extern "C" {

int JitHelp_testCond(uint32_t cpsr,uint32_t cond){
    const uint32_t N=(cpsr>>31)&1u,Z=(cpsr>>30)&1u,
                   C=(cpsr>>29)&1u,V=(cpsr>>28)&1u;
    switch(cond&15u){
        case  0:return(int)Z;
        case  1:return(int)(Z^1u);
        case  2:return(int)C;
        case  3:return(int)(C^1u);
        case  4:return(int)N;
        case  5:return(int)(N^1u);
        case  6:return(int)V;
        case  7:return(int)(V^1u);
        case  8:return(int)(C&(Z^1u));
        case  9:return(int)((C^1u)|Z);
        case 10:return N==V;
        case 11:return N!=V;
        case 12:return(Z==0u&&N==V);
        case 13:return(Z==1u||N!=V);
        case 14:return 1;
        default:return 0;
    }
}

int JitHelp_syncFrom(Interpreter* interp,uint32_t* regs,uint32_t* outCPSR){
    if(!interp||!regs||!outCPSR)return -1;
    if(!interp->isReady())return -1;
    uint32_t** p=interp->getRegisters();
    if(!p)return -1;
    for(int i=0;i<15;i++){
        if(!p[i])return -1;
        regs[i]=*p[i];
    }
    *outCPSR=interp->getCpsrRef();
    return 0;
}

int JitHelp_commit(Interpreter* interp,int cpu,
                   uint32_t* regs,uint32_t cpsr,
                   uint32_t pc,int reason){
    if(!interp||!regs||cpu<0||cpu>1)return -1;
    uint32_t** p=interp->getRegisters();
    if(!p)return -1;
    for(int i=0;i<15;i++){
        if(!p[i])return -1;
        *p[i]=regs[i];
    }
    interp->getCpsrRef()=cpsr;
    g_exitPC[cpu]    =pc;
    g_exitCPSR[cpu]  =cpsr;
    g_exitReason[cpu]=reason;
    interp->setPC(pc);
    return 0;
}

uint32_t JitHelp_r32(Core*c,int a,uint32_t ad){return c?c->memory.read<uint32_t>((bool)a,ad):0;}
uint16_t JitHelp_r16(Core*c,int a,uint32_t ad){return c?c->memory.read<uint16_t>((bool)a,ad):0;}
uint8_t  JitHelp_r8 (Core*c,int a,uint32_t ad){return c?c->memory.read<uint8_t> ((bool)a,ad):0;}
void JitHelp_w32(Core*c,int a,uint32_t ad,uint32_t v){if(c)c->memory.write<uint32_t>((bool)a,ad,v);}
void JitHelp_w16(Core*c,int a,uint32_t ad,uint16_t v){if(c)c->memory.write<uint16_t>((bool)a,ad,v);}
void JitHelp_w8 (Core*c,int a,uint32_t ad,uint8_t  v){if(c)c->memory.write<uint8_t> ((bool)a,ad,v);}

int JitHelp_armBlock(Core* core,int arm7,uint32_t op,
                     uint32_t* regs,uint32_t pcForR15,
                     uint32_t* pcOut,uint32_t* cpsrInOut){
    if(!core||!regs||!pcOut||!cpsrInOut)return -1;
    const bool p=(op>>24)&1,u=(op>>23)&1,S=(op>>22)&1;
    const bool w=(op>>21)&1,l=(op>>20)&1;
    const uint8_t  rn  =(op>>16)&0xF;
    const uint16_t list=(uint16_t)(op&0xFFFF);
    if(S||rn>14||!list)return -1;
    int n=0;
    for(int i=0;i<16;i++)if(list&(1u<<i))n++;
    const uint32_t base=regs[rn];
    uint32_t addr,wb;
    if(u){wb=base+(uint32_t)n*4u;addr=p?base+4u:base;}
    else {wb=base-(uint32_t)n*4u;addr=p?wb      :wb+4u;}
    int wrotePC=0;
    if(l){
        for(int i=0;i<16;i++){
            if(!(list&(1u<<i)))continue;
            uint32_t val=core->memory.read<uint32_t>((bool)arm7,addr);addr+=4;
            if(i==15){
                if(val&1u){*cpsrInOut|=(1u<<5);*pcOut=val&~1u;}
                else      {*cpsrInOut&=~(1u<<5);*pcOut=val&~3u;}
                wrotePC=1;
            }else regs[i]=val;
        }
        if(w&&!(list&(1u<<rn)))regs[rn]=wb;
    }else{
        for(int i=0;i<16;i++){
            if(!(list&(1u<<i)))continue;
            core->memory.write<uint32_t>((bool)arm7,addr,(i==15)?pcForR15:regs[i]);
            addr+=4;
        }
        if(w)regs[rn]=wb;
    }
    return wrotePC;
}

int JitHelp_thumbPushPop(Core* core,int arm7,uint32_t op,
                         uint32_t* regs,uint32_t* pcOut,uint32_t* cpsrInOut){
    if(!core||!regs||!pcOut||!cpsrInOut)return -1;
    const bool load=(op>>11)&1,R=(op>>8)&1;
    const uint8_t list=(uint8_t)(op&0xFF);
    int n=0;
    for(int i=0;i<8;i++)if(list&(1u<<i))n++;
    if(R)n++;
    if(!load){
        uint32_t sp=regs[13]-(uint32_t)n*4u,addr=sp;
        for(int i=0;i<8;i++){
            if(!(list&(1u<<i)))continue;
            core->memory.write<uint32_t>((bool)arm7,addr,regs[i]);addr+=4;
        }
        if(R)core->memory.write<uint32_t>((bool)arm7,addr,regs[14]);
        regs[13]=sp;return 0;
    }
    uint32_t addr=regs[13];
    for(int i=0;i<8;i++){
        if(!(list&(1u<<i)))continue;
        regs[i]=core->memory.read<uint32_t>((bool)arm7,addr);addr+=4;
    }
    int wrotePC=0;
    if(R){
        uint32_t val=core->memory.read<uint32_t>((bool)arm7,addr);addr+=4;
        if(val&1u){*cpsrInOut|=(1u<<5);*pcOut=val&~1u;}
        else      {*cpsrInOut&=~(1u<<5);*pcOut=val&~3u;}
        wrotePC=1;
    }
    regs[13]=addr;return wrotePC;
}

int JitHelp_thumbBlock(Core* core,int arm7,uint32_t op,uint32_t* regs){
    if(!core||!regs)return -1;
    const bool    load=(op>>11)&1;
    const uint8_t rb=(op>>8)&7,list=(uint8_t)(op&0xFF);
    if(!list){regs[rb]+=0x40;return 0;}
    uint32_t addr=regs[rb],wb=addr;
    for(int i=0;i<8;i++)if(list&(1u<<i))wb+=4;
    const bool rbIn=(list&(1u<<rb))!=0;
    if(load){
        for(int i=0;i<8;i++){
            if(!(list&(1u<<i)))continue;
            regs[i]=core->memory.read<uint32_t>((bool)arm7,addr);addr+=4;
        }
        if(!rbIn)regs[rb]=wb;
    }else{
        for(int i=0;i<8;i++){
            if(!(list&(1u<<i)))continue;
            core->memory.write<uint32_t>((bool)arm7,addr,regs[i]);addr+=4;
        }
        regs[rb]=wb;
    }
    return 0;
}

// F9: scheduler dispatches are counted so the log can prove whether VBlank/SPU/timer
// events are being delivered while the CPUs are pinned.
void JitHelp_tick(Core* core,uint32_t cycles){
    if(!core)return;
    core->globalCycles+=cycles;
    while(!core->events.empty()&&
          core->globalCycles>=core->events.front().cycles){
        SchedEvent e=core->events.front();
        core->events.erase(core->events.begin());
        if(e.task>=0&&e.task<MAX_TASKS&&core->tasks[e.task].fn){
#if JIT_EV_COUNTERS
            g_evCount[e.task]++;
#endif
            core->tasks[e.task]();
        }
    }
}

uint32_t JitHelp_getEvCount(int task){
    if(task<0||task>=MAX_TASKS)return 0;
    return g_evCount[task];
}
uint32_t JitHelp_getIrqCount(int cpu){
    if(cpu<0||cpu>1)return 0;
    return g_irqCount[cpu];
}

} // extern "C"

// ═══════════════════════════════════════════════════════════════════════
// Prologue / Epilogue
// ═══════════════════════════════════════════════════════════════════════
static void emitPrologue(Ctx& ctx){
    ctx.E(ppc_mflr(0));
    ctx.E(ppc_stwu(1,-(int16_t)FRAME_SIZE,1));
    ctx.E(ppc_stw(0,(int16_t)FRAME_LR_OFF,1));
    for(int r=14;r<=31;r++)ctx.E(ppc_stw(r,FRAME_SAVE+(r-14)*4,1));
    ctx.li(TA,(uint32_t)(uintptr_t)ctx.core);
    ctx.E(ppc_stw(TA,FRAME_CORE,1));
    ctx.li(TA,(uint32_t)(uintptr_t)ctx.interp);
    ctx.E(ppc_stw(TA,FRAME_INTERP,1));
    ctx.E(ppc_addi(TA,0,(int16_t)ctx.cpuIdx));
    ctx.E(ppc_stw(TA,FRAME_CPUIDX,1));
}

static void emitEpilogue(Ctx& ctx){
    for(int r=14;r<=31;r++)ctx.E(ppc_lwz(r,FRAME_SAVE+(r-14)*4,1));
    ctx.E(ppc_lwz(0,(int16_t)FRAME_LR_OFF,1));
    ctx.E(ppc_mtlr(0));
    ctx.E(ppc_addi(1,1,(int16_t)FRAME_SIZE));
    ctx.E(ppc_blr());
}

// Pull the interpreter's architectural state into the JIT register file.
static void emitSyncFrom(Ctx& ctx){
    ctx.ldInterp();
    ctx.E(ppc_addi(TB,1,(int16_t)FRAME_REGSYNC));
    ctx.E(ppc_addi(TC,1,(int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_syncFrom);
    ctx.E(ppc_cmpi(0,TA,0));
    size_t bOk=ctx.sz();
    ctx.E(ppc_bc(12,2,0));
    ctx.li(TA,(uint32_t)(uintptr_t)g_exitReason);
    ctx.E(ppc_lwz(TB,FRAME_CPUIDX,1));
    ctx.E(ppc_rlwinm(TB,TB,2,0,29));
    ctx.E(ppc_add(TA,TA,TB));
    ctx.E(ppc_addi(TB,0,EXIT_FALLBACK));
    ctx.E(ppc_stw(TB,0,TA));
    emitEpilogue(ctx);
    {int32_t d=(int32_t)((ctx.sz()-bOk)*4);ctx.base[bOk]=ppc_bc(12,2,(int16_t)d);}
    for(int i=0;i<15;i++)ctx.E(ppc_lwz(RA[i],FRAME_REGSYNC+i*4,1));
    ctx.E(ppc_lwz(RCPSR,FRAME_CPSR,1));
}

static void emitSpill(Ctx& ctx){
    for(int i=0;i<15;i++)ctx.E(ppc_stw(RA[i],FRAME_REGSYNC+i*4,1));
    ctx.E(ppc_stw(RCPSR,FRAME_CPSR,1));
}
static void emitReload(Ctx& ctx){
    for(int i=0;i<15;i++)ctx.E(ppc_lwz(RA[i],FRAME_REGSYNC+i*4,1));
    ctx.E(ppc_lwz(RCPSR,FRAME_CPSR,1));
}

static void emitCommitExit(Ctx& ctx,uint32_t nextPC,int reason){
    emitSpill(ctx);
    ctx.ldInterp();ctx.ldCpu();
    ctx.E(ppc_addi(TC,1,(int16_t)FRAME_REGSYNC));
    ctx.E(ppc_mr(TD,RCPSR));
    ctx.li(TE,nextPC);
    ctx.E(ppc_addi(TF,0,(int16_t)reason));
    ctx.call((void*)JitHelp_commit);
    emitEpilogue(ctx);
}
static void emitCommitExitDyn(Ctx& ctx,int reason){
    emitSpill(ctx);
    ctx.ldInterp();ctx.ldCpu();
    ctx.E(ppc_addi(TC,1,(int16_t)FRAME_REGSYNC));
    ctx.E(ppc_mr(TD,RCPSR));
    ctx.E(ppc_lwz(TE,FRAME_PC,1));
    ctx.E(ppc_addi(TF,0,(int16_t)reason));
    ctx.call((void*)JitHelp_commit);
    emitEpilogue(ctx);
}

// ═══════════════════════════════════════════════════════════════════════
// Condition skip — inline for the single-bit conditions, helper otherwise
//
// andi. rA,rS,UI  sets CR0 from (CPSR & UI), so CR0.EQ == 1 means "the flag bit
// is clear".  EQ/CS/MI/VS must skip when the bit is clear -> bc(12,2) [beq cr0];
// NE/CC/PL/VC must skip when the bit is set   -> bc(4,2)  [bne cr0].
// CR0 is never used by the flag writers (they use cr6), so this is safe.
// ═══════════════════════════════════════════════════════════════════════
static int openCondSkip(Ctx& ctx,uint8_t cond){
    if(cond==14)return -1;
    if(cond==15){                     // ARM "never": branch over the whole body
        int m=ctx.NEWMARK();
        if(m<0){ctx.overflow=true;return -1;}
        ctx.FWD(20,0,m);
        return m;
    }

    uint32_t bits=0; bool wantSet=false, inlineable=false;
    switch(cond){
        case 0: bits=0x40000000u; wantSet=true;  inlineable=true; break; // EQ (Z)
        case 1: bits=0x40000000u; wantSet=false; inlineable=true; break; // NE
        case 2: bits=0x20000000u; wantSet=true;  inlineable=true; break; // CS (C)
        case 3: bits=0x20000000u; wantSet=false; inlineable=true; break; // CC
        case 4: bits=0x80000000u; wantSet=true;  inlineable=true; break; // MI (N)
        case 5: bits=0x80000000u; wantSet=false; inlineable=true; break; // PL
        case 6: bits=0x10000000u; wantSet=true;  inlineable=true; break; // VS (V)
        case 7: bits=0x10000000u; wantSet=false; inlineable=true; break; // VC
        default: break;
    }

    int m=ctx.NEWMARK();
    if(m<0){ctx.overflow=true;return -1;}

    if(inlineable){
        ctx.E(ppc_andi_record(TA,RCPSR,(uint16_t)(bits>>16)));
        ctx.FWD(wantSet?12:4,2,m);
    }else{
        ctx.E(ppc_mr(TA,RCPSR));
        ctx.E(ppc_addi(TB,0,(int16_t)cond));
        ctx.call((void*)JitHelp_testCond);
        ctx.E(ppc_cmpi(0,TA,0));
        ctx.FWD(12,2,m);          // beq cr0 : testCond()==0 -> skip the body
    }
    return m;
}
static void closeCondSkip(Ctx& ctx,int m){ if(m>=0)ctx.SETMARK(m); ctx.RESOLVE(); }

// ═══════════════════════════════════════════════════════════════════════
// Flag write-back  (F1..F4)
//
// The ONLY rule that matters: PPC rlwinm's MB/ME are MSB-relative bit numbers.
//   bit31 -> MB/ME = 0     bit30 -> 1     bit29 (ARM C) -> 2
//   bit28 (ARM V) -> 3     bit3 -> 28
// ═══════════════════════════════════════════════════════════════════════
static void setNZ(Ctx& ctx,uint8_t r){
    // F3: TG (r9) instead of TA (r3) - TA/TD hold op1/op2 for the V emitters.
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,2,31));        // clear N,Z (MSB 2..31 == value bits 29..0)
    ctx.E(ppc_rlwimi(RCPSR,r,0,0,0));             // N = r[31]
    ctx.E(ppc_cmpi(6,r,0));
    ctx.E(ppc_mfcr(TG));
    ctx.E(ppc_rlwinm(TG,TG,25,1,1));              // cr6.EQ (value bit 5) -> value bit 30 (Z)
    ctx.E(ppc_or(RCPSR,RCPSR,TG));
}
static void setC_xer(Ctx& ctx){
    // F1: CA is XER value bit 29 == MSB-relative bit 2 == CPSR.C.  No rotate needed.
    // F3: TG instead of TA, so op1/op2 survive for the V emitters that follow.
    ctx.E(ppc_mfxer(TG));
    ctx.E(ppc_rlwinm(TG,TG,0,2,2));
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,3,1));         // clear C (MSB 3..1 wraps around bit 2)
    ctx.E(ppc_or(RCPSR,RCPSR,TG));
}
static void setV_add(Ctx& ctx,uint8_t res,uint8_t a,uint8_t b){
    ctx.E(ppc_xor(TE,res,a));ctx.E(ppc_xor(TF,res,b));ctx.E(ppc_and(TE,TE,TF));
    ctx.E(ppc_rlwinm(TE,TE,0,0,0));               // sign -> value bit 31
    ctx.E(ppc_rlwinm(TE,TE,29,3,3));              // F2: V -> value bit 28 (was 28,28 -> bit 3)
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,4,2));         // clear V
    ctx.E(ppc_or(RCPSR,RCPSR,TE));
}
static void setV_sub(Ctx& ctx,uint8_t res,uint8_t a,uint8_t b){
    ctx.E(ppc_xor(TE,a,b));ctx.E(ppc_xor(TF,a,res));ctx.E(ppc_and(TE,TE,TF));
    ctx.E(ppc_rlwinm(TE,TE,0,0,0));
    ctx.E(ppc_rlwinm(TE,TE,29,3,3));              // F2
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,4,2));
    ctx.E(ppc_or(RCPSR,RCPSR,TE));
}
static void setC_imm(Ctx& ctx,uint8_t cr){
    // F4: SH=29 moves the LSB to value bit 29; mask (2,2) keeps value bit 29.
    //     The old mask (29,29) kept value bit 2, so the OR wrote a constant 0.
    ctx.E(ppc_rlwinm(TA,cr,29,2,2));
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,3,1));
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}
static void primeCarry(Ctx& ctx){                  // XER.CA = CPSR.C  (for adde/subfe)
    ctx.E(ppc_rlwinm(TA,RCPSR,3,31,31));          // value bit 29 -> value bit 0
    ctx.E(ppc_addic(0,TA,-1));
}

// ═══════════════════════════════════════════════════════════════════════
// Shifter helpers.  Carry-out is written into RSHC with the ARM carry bit in
// the LSB (that is what the fixed setC_imm() expects).
// ═══════════════════════════════════════════════════════════════════════
static void sLslI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i==0){if(d!=s)ctx.E(ppc_mr(d,s));if(sc)ctx.E(ppc_rlwinm(RSHC,RCPSR,3,31,31));}
    else if(i<32){if(sc)ctx.E(ppc_rlwinm(RSHC,s,(uint8_t)i,31,31));
                  ctx.E(ppc_rlwinm(d,s,(uint8_t)i,0,(uint8_t)(31-i)));}
    else if(i==32){if(sc)ctx.E(ppc_rlwinm(RSHC,s,0,31,31));ctx.E(ppc_addi(d,0,0));}
    else{if(sc)ctx.E(ppc_addi(RSHC,0,0));ctx.E(ppc_addi(d,0,0));}
}
static void sLsrI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i==32){if(sc)ctx.E(ppc_rlwinm(RSHC,s,1,31,31));ctx.E(ppc_addi(d,0,0));}
    else if(i>0&&i<32){if(sc)ctx.E(ppc_rlwinm(RSHC,s,(uint8_t)((33-i)&31),31,31));
                       ctx.E(ppc_rlwinm(d,s,(uint8_t)(32-i),(uint8_t)i,31));}
    else{if(sc)ctx.E(ppc_addi(RSHC,0,0));ctx.E(ppc_addi(d,0,0));}
}
static void sAsrI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i>=32){if(sc)ctx.E(ppc_rlwinm(RSHC,s,1,31,31));ctx.E(ppc_srawi(d,s,31));}
    else if(i>0){if(sc)ctx.E(ppc_rlwinm(RSHC,s,(uint8_t)((33-i)&31),31,31));
                 ctx.E(ppc_srawi(d,s,(uint8_t)i));}
    else{if(d!=s)ctx.E(ppc_mr(d,s));if(sc)ctx.E(ppc_rlwinm(RSHC,RCPSR,3,31,31));}
}
static void sRorI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i==0){
        // imm ROR #0 is really RRX: result = (C << 31) | (s >> 1), carry = s[0]
        if(sc)ctx.E(ppc_rlwinm(RSHC,s,0,31,31));
        ctx.E(ppc_rlwinm(TA,RCPSR,2,0,0));
        ctx.E(ppc_rlwinm(d,s,31,1,31));
        ctx.E(ppc_or(d,d,TA));
    }else{
        i&=31;
        if(i==0){if(d!=s)ctx.E(ppc_mr(d,s));if(sc)ctx.E(ppc_rlwinm(RSHC,s,1,31,31));}
        else{if(sc)ctx.E(ppc_rlwinm(RSHC,s,(uint8_t)((33-i)&31),31,31));
             ctx.E(ppc_rlwinm(d,s,(uint8_t)(32-i),0,31));}
    }
}

// F5: register-specified shift WITH the ARM carry, for every amount (0, 1..31, 32, >32).
//   type 0 = LSL, 1 = LSR, 2 = ASR, 3 = ROR
//   value  : src (Rs)      amount : amtReg (low 8 bits)
//   result : dst           carry  : RSHC LSB
// Clobbers TB, TK, RSHC.  dst may alias src or amtReg.
//   Layout of the emitted sequence (M* = local branch mark):
//
//     rlwinm TK,amt,0,24,31          ; amount = amtReg & 0xFF
//     cmpi   0,TK,0    ; beq  -> MZERO
//     <type specific>
//        LSL/LSR/ASR: cmpi 0,TK,32 ; bge -> MBIG
//                     addi TB,TK,-1 ; srw RSHC,src,TB   (C = src[amount-1])
//                     slw/srw/sraw dst,src,TK
//        ROR:         rlwinm TK,TK,0,27,31 (n = amount%32) ; beq -> MRMUL
//                     subfic TB,TK,32 ; rlwnm dst,src,TB,0,31
//                     rlwinm RSHC,dst,1,31,31            (C = src[n-1])
//     b -> MGDONE
//   MBIG: LSL/LSR:  cmpi 0,TK,32 ; bgt -> MGT
//                   rlwinm RSHC,src,(0|1),31,31          (amount==32: C = src[0] / src[31])
//                   b -> MZ
//        MGT:       addi RSHC,0,0                           (amount>32: C = 0)
//        MZ:        addi dst,0,0
//        ASR:       rlwinm RSHC,src,1,31,31 ; srawi dst,src,31
//     b -> MGDONE
//   MRMUL: mr dst,src ; rlwinm RSHC,src,1,31,31           (ROR by a multiple of 32)
//   MGDONE: b -> MEND
//   MZERO: mr dst,src ; rlwinm RSHC,RCPSR,3,31,31         (C unchanged: re-read CPSR bit 29)
//   MEND:
static void emitRegShiftCarry(Ctx& ctx,int type,uint8_t dst,uint8_t src,uint8_t amtReg){
    const int savedFix =ctx.baseFix();      // keep any enclosing condition-skip pending
    const int savedMark=ctx.nextMark;
    const int mZero  =ctx.NEWMARK();
    const int mGDone =ctx.NEWMARK();
    const int mEnd   =ctx.NEWMARK();
    const int mBig   =ctx.NEWMARK();
    const int mGt    =ctx.NEWMARK();
    const int mRmul  =ctx.NEWMARK();
    if(mZero<0||mGDone<0||mEnd<0||mBig<0||mGt<0||mRmul<0){
        ctx.overflow=true;ctx.nextMark=savedMark;return;
    }

    ctx.E(ppc_rlwinm(TK,amtReg,0,24,31));      // amount = amtReg & 0xFF
    ctx.E(ppc_cmpi(0,TK,0));
    ctx.FWD(12,2,mZero);                       // amount == 0 -> result = src, C unchanged

    if(type==3){                               // ── ROR ──
        ctx.E(ppc_rlwinm(TK,TK,0,27,31));      // n = amount & 31
        ctx.E(ppc_cmpi(0,TK,0));
        ctx.FWD(12,2,mRmul);                   // n == 0 (amount is a multiple of 32)
        ctx.E(ppc_subfic(TB,TK,32));
        ctx.E(ppc_rlwnm(dst,src,TB,0,31));     // load_shift = rotl(src, 32-n) = ror(src, n)
        ctx.E(ppc_rlwinm(RSHC,dst,1,31,31));   // C = src[n-1] == result[31]
        ctx.FWD(20,0,mGDone);
        ctx.SETMARK(mRmul);
        ctx.E(ppc_mr(dst,src));
        ctx.E(ppc_rlwinm(RSHC,src,1,31,31));   // C = src[31]
        ctx.FWD(20,0,mGDone);
    }else{
        ctx.E(ppc_cmpi(0,TK,32));
        ctx.FWD(4,0,mBig);                     // !(amount < 32) -> MBIG
        ctx.E(ppc_addi(TB,TK,-1));             // amount-1 (0..30)
        ctx.E(ppc_srw(RSHC,src,TB));           // C = src[amount-1]
        if(type==0)     ctx.E(ppc_slw(dst,src,TK));
        else if(type==1)ctx.E(ppc_srw(dst,src,TK));
        else            ctx.E(ppc_sraw(dst,src,TK));
        ctx.FWD(20,0,mGDone);

        ctx.SETMARK(mBig);                     // amount >= 32
        if(type==0||type==1){
            ctx.E(ppc_cmpi(0,TK,32));
            ctx.FWD(12,1,mGt);                 // amount > 32
            ctx.E(ppc_rlwinm(RSHC,src,(uint8_t)(type==0?0:1),31,31));
            int mZ=ctx.NEWMARK();
            if(mZ<0){ctx.overflow=true;return;}
            ctx.FWD(20,0,mZ);
            ctx.SETMARK(mGt);
            ctx.E(ppc_addi(RSHC,0,0));
            ctx.SETMARK(mZ);
            ctx.E(ppc_addi(dst,0,0));          // result = 0 for amount >= 32
        }else{                                 // ASR: sign bits, C = src[31] for 32 or more
            ctx.E(ppc_rlwinm(RSHC,src,1,31,31));
            ctx.E(ppc_srawi(dst,src,31));
        }
        ctx.FWD(20,0,mGDone);
    }

    ctx.SETMARK(mGDone);
    ctx.FWD(20,0,mEnd);                        // all general paths skip the amount==0 body

    ctx.SETMARK(mZero);
    if(dst!=src)ctx.E(ppc_mr(dst,src));
    ctx.E(ppc_rlwinm(RSHC,RCPSR,3,31,31));     // C = old C (value bit 29 -> value bit 0)

    ctx.SETMARK(mEnd);
    ctx.RESOLVE_FROM(savedFix);                // resolve only our own branches
    ctx.nextMark=savedMark;                    // hand the mark slots back to the caller
}

// ═══════════════════════════════════════════════════════════════════════
// Shifter dispatch (used by ARM data-processing and single data transfer)
// ═══════════════════════════════════════════════════════════════════════
static bool emitShifter(Ctx& ctx,uint32_t op,uint8_t dst,bool sc){
    if((op>>25)&1){                            // immediate form
        uint32_t v=op&0xFF,rot=((op>>8)&0xF)*2;
        if(rot)v=(v>>rot)|(v<<(32-rot));
        ctx.li(dst,v);
        if(sc&&rot){ctx.E(ppc_rlwinm(RSHC,dst,1,31,31));return true;}   // C = imm[31] after rotate
        return false;
    }
    uint8_t rm=op&0xF;
    if(rm==15)return false;
    uint8_t st=(op>>5)&3;
    if(!((op>>4)&1)){                          // immediate shift
        int sa=(op>>7)&0x1F;
        switch(st){
            case 0:sLslI(ctx,dst,RA[rm],sa,sc);break;
            case 1:sLsrI(ctx,dst,RA[rm],sa?sa:32,sc);break;
            case 2:sAsrI(ctx,dst,RA[rm],sa?sa:32,sc);break;
            default:sRorI(ctx,dst,RA[rm],sa,sc);break;
        }
        return sc;
    }
    // register-specified shift (F5)
    uint8_t rs=(op>>8)&0xF;
    if(rs==15)return false;
#if JIT_REG_SHIFT_CARRY
    emitRegShiftCarry(ctx,st,dst,RA[rm],RA[rs]);
    return true;                                // carry (or "keep C" for amount 0) is in RSHC
#else
    return false;                               // bisect: let the interpreter do it
#endif
}

// ═══════════════════════════════════════════════════════════════════════
// ARM data-processing
// ═══════════════════════════════════════════════════════════════════════
enum DP{AND=0,EOR,SUB,RSB,ADD,ADC,SBC,RSC,TST,TEQ,CMP,CMN,ORR,MOV,BIC,MVN};

static bool emitDP(Ctx& ctx,uint32_t op,uint32_t curPC){
    uint8_t cond=(op>>28)&0xF,dop=(op>>21)&0xF;
    bool s=(op>>20)&1;
    uint8_t rn=(op>>16)&0xF,rd=(op>>12)&0xF;
    if(cond==15||rd==15)return false;
    bool immForm=(op>>25)&1,regShift=(op>>4)&1;
    if(!immForm){if((op&0xF)==15)return false;if(regShift&&((op>>8)&0xF)==15)return false;}
    if(rn==15&&regShift&&!immForm)return false;
    if(rn==15&&!immForm)return false;                       // R15 as a shifted source: rare, keep in the interpreter

#if JIT_NO_S
    if(s)return false;                                      // bisect #1
#endif
    // ADC/SBC/RSC need the incoming carry in XER.CA, but the register-shift sequence
    // itself clobbers the very registers/XER state involved; keep that combination in
    // the interpreter rather than pretending.
    if(regShift&&!immForm&&(dop==ADC||dop==SBC||dop==RSC))return false;

    int si=openCondSkip(ctx,cond);
    if(rn==15){ctx.li(TD,curPC+(ctx.thumb?4u:8u));ctx.E(ppc_stw(TD,FRAME_SCR2,1));}
    if(dop==ADC||dop==SBC||dop==RSC)primeCarry(ctx);

    bool logC=(s&&(dop==AND||dop==EOR||dop==TST||dop==TEQ||dop==ORR||dop==MOV||dop==BIC||dop==MVN));
    bool cset=emitShifter(ctx,op,TA,logC);

    uint8_t srcRn;
    if(rn==15){ctx.E(ppc_lwz(TD,FRAME_SCR2,1));srcRn=TD;}else srcRn=RA[rn];
    bool needV=(s&&(dop==ADD||dop==SUB||dop==RSB||dop==CMN||dop==CMP||dop==ADC||dop==SBC||dop==RSC));
    if(needV){ctx.E(ppc_stw(TA,FRAME_SCR0,1));ctx.E(ppc_stw(srcRn,FRAME_SCR1,1));}
    bool isTest=(dop==TST||dop==TEQ||dop==CMP||dop==CMN);
    uint8_t res=isTest?TC:RA[rd];
    switch((DP)dop){
        case AND:case TST:ctx.E(ppc_and  (res,srcRn,TA));break;
        case EOR:case TEQ:ctx.E(ppc_xor  (res,srcRn,TA));break;
        case SUB:case CMP:ctx.E(ppc_subfc(res,TA,srcRn));break;
        case RSB:         ctx.E(ppc_subfc(res,srcRn,TA));break;
        case ADD:case CMN:ctx.E(ppc_addc (res,srcRn,TA));break;
        case ADC:         ctx.E(ppc_adde (res,srcRn,TA));break;
        case SBC:         ctx.E(ppc_subfe(res,TA,srcRn));break;
        case RSC:         ctx.E(ppc_subfe(res,srcRn,TA));break;
        case ORR:         ctx.E(ppc_or   (res,srcRn,TA));break;
        case MOV:         if(res!=TA)ctx.E(ppc_mr(res,TA));break;
        case BIC:         ctx.E(ppc_andc (res,srcRn,TA));break;
        case MVN:         ctx.E(ppc_nor  (res,TA,TA));   break;
    }
    if(s){
        uint8_t opA=srcRn,opB=TA;
        if(needV){ctx.E(ppc_lwz(TA,FRAME_SCR0,1));opB=TA;ctx.E(ppc_lwz(TD,FRAME_SCR1,1));opA=TD;}
        switch((DP)dop){
            case ADD:case CMN:case ADC:setNZ(ctx,res);
                                       setC_xer(ctx);
                                       setV_add(ctx,res,opA,opB);break;
            case SUB:case CMP:case SBC:setNZ(ctx,res);
                                       setC_xer(ctx);
                                       setV_sub(ctx,res,opA,opB);break;
            case RSB:case RSC:         setNZ(ctx,res);
                                       setC_xer(ctx);
                                       setV_sub(ctx,res,opB,opA);break;
            default:setNZ(ctx,res);
                    if(cset)setC_imm(ctx,RSHC);         // F6b: carry lives in RSHC, not TC
                    break;
        }
    }
    closeCondSkip(ctx,si);
    return true;
}

static void emitBX_target(Ctx& ctx){
    ctx.E(ppc_lwz(TA,FRAME_SCR0,1));
    ctx.E(ppc_rlwinm(TB,TA,0,0,30));ctx.E(ppc_stw(TB,FRAME_PC,1));
    ctx.E(ppc_rlwinm(TC,TA,0,31,31));ctx.E(ppc_rlwinm(TC,TC,5,26,26));
    ctx.li(TA,~(1u<<5));ctx.E(ppc_and(RCPSR,RCPSR,TA));ctx.E(ppc_or(RCPSR,RCPSR,TC));
    emitCommitExitDyn(ctx,EXIT_NORMAL);
}
static bool emitBX(Ctx& ctx,uint32_t op,uint32_t curPC){
    uint8_t cond=(op>>28)&0xF,rm=op&0xF;
    if(rm==15||cond==15)return false;
    int si=openCondSkip(ctx,cond);
    ctx.E(ppc_stw(RA[rm],FRAME_SCR0,1));
    emitBX_target(ctx);
    if(si>=0){ctx.SETMARK(si);emitCommitExit(ctx,curPC+4,EXIT_NORMAL);ctx.RESOLVE();}
    ctx.done=true;return true;
}
static bool emitBranch(Ctx& ctx,uint32_t op,uint32_t curPC){
    if((op&0x0FFFFFF0)==0x012FFF10)return emitBX(ctx,op,curPC);
    if((op&0x0FFFFFF0)==0x012FFF30)return false;
    if((op&0x0E000000)!=0x0A000000)return false;
    uint8_t cond=(op>>28)&0xF;
    if(cond==15)return false;
    bool lk=(op>>24)&1;
    int32_t off=(int32_t)(op<<8)>>6;
    uint32_t tgt=curPC+8u+(uint32_t)off;
    int si=openCondSkip(ctx,cond);
    if(lk)ctx.li(RA[14],curPC+4);
    emitCommitExit(ctx,tgt,EXIT_NORMAL);
    if(si>=0){ctx.SETMARK(si);emitCommitExit(ctx,curPC+4,EXIT_NORMAL);ctx.RESOLVE();}
    ctx.done=true;return true;
}

// F7: writeback handling for rd == rn, and correct base for post-index.
static bool emitLS(Ctx& ctx,uint32_t op,uint32_t curPC){
    uint8_t cond=(op>>28)&0xF;
    if(cond==15)return false;
    bool ld=(op>>20)&1,by=(op>>22)&1,up=(op>>23)&1,pre=(op>>24)&1,wb=(op>>21)&1,immO=!((op>>25)&1);
    uint8_t rn=(op>>16)&0xF,rd=(op>>12)&0xF;
    if(rd==15)return false;
    if(rn==15&&(!immO||!pre||wb))return false;
    if(!immO&&((((op>>4)&1))||((op&0xF)==15))){ /* handled by the reg-shift path below */ }
    int si=openCondSkip(ctx,cond);
    if(immO){
        ctx.li(TA,op&0xFFF);
    }else{
        uint8_t rm=op&0xF;uint8_t sh=(op>>5)&3;
        if(rm==15){closeCondSkip(ctx,si);return false;}
        if(!((op>>4)&1)){                        // immediate shift
            int sa=(op>>7)&0x1F;
            if(sh==0)sLslI(ctx,TA,RA[rm],sa,false);
            else if(sh==1)sLsrI(ctx,TA,RA[rm],sa?sa:32,false);
            else if(sh==2)sAsrI(ctx,TA,RA[rm],sa?sa:32,false);
            else sRorI(ctx,TA,RA[rm],sa,false);
        }else{                                   // register shift (F5 for free, no flags)
            uint8_t rs=(op>>8)&0xF;
            if(rs==15){closeCondSkip(ctx,si);return false;}
            emitRegShiftCarry(ctx,sh,TA,RA[rm],RA[rs]);
        }
    }
    if(rn==15){
        // PC-relative literal: the guards above guarantee immediate, pre-index, no W
        uint32_t base=ctx.thumb?(curPC+4):(curPC+8);
        if(!by)base&=~3u;
        uint32_t addr=up?base+(op&0xFFF):base-(op&0xFFF);
        ctx.E(ppc_stw(TA,FRAME_SCR0,1));
        ctx.li(TB,addr);
        ctx.E(ppc_stw(TB,FRAME_SCR1,1));
    }else{
        if(pre){if(up)ctx.E(ppc_add(TB,RA[rn],TA));else ctx.E(ppc_subf(TB,TA,RA[rn]));}
        else   ctx.E(ppc_mr(TB,RA[rn]));
        ctx.E(ppc_stw(TA,FRAME_SCR0,1));
        ctx.E(ppc_stw(TB,FRAME_SCR1,1));
    }
    ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.E(ppc_lwz(TC,FRAME_SCR1,1));            // address (always materialised into SCR1)
    if(!ld){ctx.E(ppc_mr(TD,RA[rd]));ctx.hasStore=true;}
    ctx.call(ld?(by?(void*)JitHelp_r8:(void*)JitHelp_r32):(by?(void*)JitHelp_w8:(void*)JitHelp_w32));
    if(ld)ctx.E(ppc_mr(RA[rd],TA));
    if(rn!=15){
        ctx.E(ppc_lwz(TA,FRAME_SCR0,1));
        if(!pre){
            // post-index: writeback = ORIGINAL base +/- offset (RA[rn] may have been
            // overwritten by the load when rd == rn, so use the saved base in SCR1)
            ctx.E(ppc_lwz(TB,FRAME_SCR1,1));
            if(up)ctx.E(ppc_add(RA[rn],TB,TA));
            else  ctx.E(ppc_subf(RA[rn],TA,TB));
        }else if(wb){
            // pre-index writeback wins, even when rd == rn (ARM discards the load)
            ctx.E(ppc_lwz(RA[rn],FRAME_SCR1,1));
        }
    }
    closeCondSkip(ctx,si);
    return true;
}

static bool emitLSExtra(Ctx& ctx,uint32_t op,uint32_t){
    if((op&0x0E000090)!=0x00000090)return false;
    if(((op>>25)&7)!=0)return false;
    uint8_t cond=(op>>28)&0xF;if(cond==15)return false;
    bool p=(op>>24)&1,u=(op>>23)&1,w=(op>>21)&1,l=(op>>20)&1,imm=(op>>22)&1;
    uint8_t rn=(op>>16)&0xF,rd=(op>>12)&0xF,sh=(op>>5)&3;
    if(rd==15||rn==15||sh==0)return false;
    int si=openCondSkip(ctx,cond);
    if(imm){ctx.li(TA,((op>>4)&0xF0)|(op&0xF));}
    else if((op&0xF)==15){closeCondSkip(ctx,si);return false;}
    else ctx.E(ppc_mr(TA,RA[op&0xF]));
    if(p){if(u)ctx.E(ppc_add(TB,RA[rn],TA));else ctx.E(ppc_subf(TB,TA,RA[rn]));}
    else ctx.E(ppc_mr(TB,RA[rn]));
    ctx.E(ppc_stw(TA,FRAME_SCR0,1));ctx.E(ppc_stw(TB,FRAME_SCR1,1));
    ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));ctx.E(ppc_lwz(TC,FRAME_SCR1,1));
    if(!l){
        if(sh!=1){closeCondSkip(ctx,si);return false;}
        ctx.E(ppc_mr(TD,RA[rd]));ctx.hasStore=true;ctx.call((void*)JitHelp_w16);
    }else{
        if(sh==1)ctx.call((void*)JitHelp_r16);
        else if(sh==2)ctx.call((void*)JitHelp_r8);
        else ctx.call((void*)JitHelp_r16);
        if(sh==2)ctx.E(ppc_extsb(RA[rd],TA));
        else if(sh==3)ctx.E(ppc_extsh(RA[rd],TA));
        else ctx.E(ppc_mr(RA[rd],TA));
    }
    ctx.E(ppc_lwz(TA,FRAME_SCR0,1));
    if(!p){
        ctx.E(ppc_lwz(TB,FRAME_SCR1,1));                  // F7: same post-index fix
        if(u)ctx.E(ppc_add(RA[rn],TB,TA));else ctx.E(ppc_subf(RA[rn],TA,TB));
    }else if(w)ctx.E(ppc_lwz(RA[rn],FRAME_SCR1,1));
    closeCondSkip(ctx,si);return true;
}

static bool emitMul(Ctx& ctx,uint32_t op){
    uint8_t cond=(op>>28)&0xF;if(cond==15)return false;
    bool s=(op>>20)&1,acc=(op>>21)&1,lng=(op>>23)&1;
    uint8_t rd=(op>>16)&0xF,rn=(op>>12)&0xF,rs=(op>>8)&0xF,rm=op&0xF;
    if(lng||rd==15||rm==15||rs==15||(acc&&rn==15))return false;
#if JIT_NO_S
    if(s)return false;
#endif
    int si=openCondSkip(ctx,cond);
    if(acc){ctx.E(ppc_mullw(TA,RA[rm],RA[rs]));ctx.E(ppc_add(RA[rd],TA,RA[rn]));}
    else ctx.E(ppc_mullw(RA[rd],RA[rm],RA[rs]));
    if(s)setNZ(ctx,RA[rd]);
    closeCondSkip(ctx,si);return true;
}

static bool emitMrsMsr(Ctx& ctx,uint32_t op,uint32_t){
    uint8_t cond=(op>>28)&0xF;if(cond==15)return false;

    if((op&0x0FBF0FFF)==0x010F0000){                 // MRS Rd, CPSR
        uint8_t rd=(op>>12)&0xF;if(rd==15)return false;
        int si=openCondSkip(ctx,cond);
        ctx.E(ppc_mr(RA[rd],RCPSR));
        closeCondSkip(ctx,si);
        return true;
    }
    if((op&0x0FBF0FFF)==0x014F0000)return false;     // MRS Rd, SPSR

    uint8_t mask=(op>>16)&0xF;
    if(mask&0x7)return false;                        // control bits: interpreter (mode switch)

    if((op&0x0DB0F000)==0x0320F000){                 // MSR CPSR_f, #imm
        uint32_t imm=op&0xFF,rot=((op>>8)&0xF)*2;
        if(rot)imm=(imm>>rot)|(imm<<(32-rot));
        imm&=0xFF000000u;
        int si=openCondSkip(ctx,cond);
        ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,8,31));
        ctx.li(TA,imm);
        ctx.E(ppc_or(RCPSR,RCPSR,TA));
        closeCondSkip(ctx,si);
        return true;
    }
    if((op&0x0DB0FFF0)==0x0120F000){                 // MSR CPSR_f, Rm
        uint8_t rm=op&0xF;if(rm==15)return false;
        int si=openCondSkip(ctx,cond);
        ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,8,31));
        ctx.E(ppc_rlwinm(TA,RA[rm],0,0,7));
        ctx.E(ppc_or(RCPSR,RCPSR,TA));
        closeCondSkip(ctx,si);
        return true;
    }
    return false;
}

static bool emitBlockXfer(Ctx& ctx,uint32_t op,uint32_t curPC){
#if JIT_NO_BLOCKXFER
    (void)op;(void)curPC;(void)ctx;return false;
#endif
    uint8_t cond=(op>>28)&0xF;
    if(cond==15||((op>>22)&1))return false;
    uint8_t rn=(op>>16)&0xF;uint16_t list=(uint16_t)(op&0xFFFF);
    if(rn>14||!list)return false;
    bool load=(op>>20)&1,loadPC=load&&(list&0x8000);
    if(!load)ctx.hasStore=true;
    int si=openCondSkip(ctx,cond);
    emitSpill(ctx);
    ctx.li(TA,curPC+8);ctx.E(ppc_stw(TA,FRAME_SCR2,1));
    ctx.li(TA,curPC+4);ctx.E(ppc_stw(TA,FRAME_PC,1));
    ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.li(TC,op);ctx.E(ppc_addi(TD,1,(int16_t)FRAME_REGSYNC));
    ctx.E(ppc_lwz(TE,FRAME_SCR2,1));
    ctx.E(ppc_addi(TF,1,(int16_t)FRAME_PC));ctx.E(ppc_addi(TG,1,(int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_armBlock);

    ctx.E(ppc_cmpi(0,TA,0));
    size_t bGe=ctx.sz();
    ctx.E(ppc_bc(4,0,0));                            // bge cr0 : success path
    emitReload(ctx);
    emitCommitExit(ctx,curPC,EXIT_FALLBACK);
    {int32_t d=(int32_t)((ctx.sz()-bGe)*4);ctx.base[bGe]=ppc_bc(4,0,(int16_t)d);}
    emitReload(ctx);

    if(loadPC){
        ctx.E(ppc_cmpi(0,TA,1));
        size_t bEq=ctx.sz();
        ctx.E(ppc_bc(12,2,0));                       // beq : PC was written
        emitCommitExit(ctx,curPC+4,EXIT_NORMAL);
        {int32_t d=(int32_t)((ctx.sz()-bEq)*4);ctx.base[bEq]=ppc_bc(12,2,(int16_t)d);}
        emitCommitExitDyn(ctx,EXIT_NORMAL);
        if(si>=0){ctx.SETMARK(si);emitCommitExit(ctx,curPC+4,EXIT_NORMAL);}
        ctx.RESOLVE();
        ctx.done=true;
        return true;
    }
    closeCondSkip(ctx,si);
    return true;
}

static bool dispARM(Ctx& ctx,uint32_t op,uint32_t curPC){
    uint8_t cond=(op>>28)&0xF;if(cond==15)return false;
    if((op&0x0F000000)==0x0F000000)return false;
    if((op&0x0FFFFFF0)==0x012FFF10||(op&0x0FFFFFF0)==0x012FFF30)return emitBranch(ctx,op,curPC);
    if((op&0x0F900000)==0x01000000){if(emitMrsMsr(ctx,op,curPC))return true;return false;}
    uint32_t it=(op>>25)&7;
    switch(it){
        case 0:
            if((op&0x0FC000F0)==0x00000090)return emitMul(ctx,op);
            if((op&0x0E000090)==0x00000090)return emitLSExtra(ctx,op,curPC);
            return emitDP(ctx,op,curPC);
        case 1:return emitDP(ctx,op,curPC);
        case 2:case 3:return emitLS(ctx,op,curPC);
        case 4:return emitBlockXfer(ctx,op,curPC);
        case 5:return emitBranch(ctx,op,curPC);
        default:return false;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Thumb emitters
// ═══════════════════════════════════════════════════════════════════════
static bool emitT_shifts(Ctx& ctx,uint16_t op){
    if(JIT_NO_S)return false;                       // bisect #1
    uint8_t ty=(op>>11)&3,rd=op&7,rs=(op>>3)&7;int i=(op>>6)&0x1F;
    switch(ty){
        case 0:sLslI(ctx,RA[rd],RA[rs],i,true);break;
        case 1:sLsrI(ctx,RA[rd],RA[rs],i?i:32,true);break;
        case 2:sAsrI(ctx,RA[rd],RA[rs],i?i:32,true);break;
        default:return false;
    }
    setNZ(ctx,RA[rd]);setC_imm(ctx,RSHC);return true;
}
static bool emitT_addSub3(Ctx& ctx,uint16_t op){
    uint8_t rd=op&7,rs=(op>>3)&7;bool sub=(op>>9)&1,imm3=(op>>10)&1;
    if(imm3)ctx.li(TA,(op>>6)&7);else ctx.E(ppc_mr(TA,RA[(op>>6)&7]));
    ctx.E(ppc_mr(TB,RA[rs]));
    if(sub){ctx.E(ppc_subfc(RA[rd],TA,TB));setNZ(ctx,RA[rd]);setC_xer(ctx);setV_sub(ctx,RA[rd],TB,TA);}
    else   {ctx.E(ppc_addc (RA[rd],TB,TA));setNZ(ctx,RA[rd]);setC_xer(ctx);setV_add(ctx,RA[rd],TB,TA);}
    return true;
}
static bool emitT_imm8(Ctx& ctx,uint16_t op){
    uint8_t ty=(op>>11)&3,rd=(op>>8)&7;uint32_t imm=op&0xFF;uint8_t p=RA[rd];
    switch(ty){
        case 0:ctx.li(p,imm);setNZ(ctx,p);return true;
        case 1:ctx.li(TA,imm);ctx.E(ppc_mr(TB,p));ctx.E(ppc_subfc(TC,TA,TB));
               setNZ(ctx,TC);setC_xer(ctx);setV_sub(ctx,TC,TB,TA);return true;
        case 2:ctx.li(TA,imm);ctx.E(ppc_mr(TB,p));ctx.E(ppc_addc(p,TB,TA));
               setNZ(ctx,p);setC_xer(ctx);setV_add(ctx,p,TB,TA);return true;
        case 3:ctx.li(TA,imm);ctx.E(ppc_mr(TB,p));ctx.E(ppc_subfc(p,TA,TB));
               setNZ(ctx,p);setC_xer(ctx);setV_sub(ctx,p,TB,TA);return true;
    }
    return false;
}
static bool emitT_alu(Ctx& ctx,uint16_t op){
    if(JIT_NO_S)return false;                       // bisect #1: every ALU op sets flags
    uint8_t rd=op&7,rs=(op>>3)&7,o=(op>>6)&0xF;uint8_t d=RA[rd],s=RA[rs];
    switch(o){
        case 0:ctx.E(ppc_and(d,d,s));setNZ(ctx,d);break;
        case 1:ctx.E(ppc_xor(d,d,s));setNZ(ctx,d);break;
#if JIT_THUMB_ALU_CARRY
        case 2:case 3:case 4:                       // LSL/LSR/ASR (F6)
            emitRegShiftCarry(ctx,o-2,d,d,s);       // amount 0 keeps C, >=32 handled
            setNZ(ctx,d);
            setC_imm(ctx,RSHC);
            break;
#else
        case 2:case 3:case 4:return false;          // bisect: interpreter does the flags
#endif
        case 5:primeCarry(ctx);ctx.E(ppc_mr(TB,d));ctx.E(ppc_adde(d,TB,s));
               setNZ(ctx,d);setC_xer(ctx);setV_add(ctx,d,TB,s);break;
        case 6:primeCarry(ctx);ctx.E(ppc_mr(TB,d));ctx.E(ppc_subfe(d,s,TB));
               setNZ(ctx,d);setC_xer(ctx);setV_sub(ctx,d,TB,s);break;
        case 7:                                     // ROR (F6)
#if JIT_THUMB_ALU_CARRY
            emitRegShiftCarry(ctx,3,d,d,s);
            setNZ(ctx,d);
            setC_imm(ctx,RSHC);
#else
            return false;
#endif
            break;
        case 8:ctx.E(ppc_and(TA,d,s));setNZ(ctx,TA);break;
        case 9:ctx.E(ppc_addi(TA,0,0));ctx.E(ppc_subfc(d,s,TA));
               setNZ(ctx,d);setC_xer(ctx);setV_sub(ctx,d,TA,s);break;
        case 10:ctx.E(ppc_mr(TB,d));ctx.E(ppc_subfc(TA,s,TB));
                setNZ(ctx,TA);setC_xer(ctx);setV_sub(ctx,TA,TB,s);break;
        case 11:ctx.E(ppc_mr(TB,d));ctx.E(ppc_addc(TA,TB,s));
                setNZ(ctx,TA);setC_xer(ctx);setV_add(ctx,TA,TB,s);break;
        case 12:ctx.E(ppc_or(d,d,s));setNZ(ctx,d);break;
        case 13:ctx.E(ppc_mullw(d,d,s));setNZ(ctx,d);break;
        case 14:ctx.E(ppc_andc(d,d,s));setNZ(ctx,d);break;
        case 15:ctx.E(ppc_nor(d,s,s));setNZ(ctx,d);break;
        default:return false;
    }
    return true;
}
static bool emitT_hiReg(Ctx& ctx,uint16_t op,uint32_t curPC){
    uint8_t o=(op>>8)&3,rs=((op>>3)&7)|(((op>>6)&1)<<3),rd=(op&7)|(((op>>7)&1)<<3);
    if(o==3){                                        // BX/BLX Rs
        if(rs==15)ctx.li(TA,(curPC+4)&~1u);else ctx.E(ppc_mr(TA,RA[rs]));
        ctx.E(ppc_stw(TA,FRAME_SCR0,1));emitBX_target(ctx);ctx.done=true;return true;
    }
    if(rd==15){                                      // ADD/MOV pc,Rs  (tail call)
        if(o==1)return false;
        if(rs==15)ctx.li(TA,curPC+4);else ctx.E(ppc_mr(TA,RA[rs]));
        if(o==0){ctx.li(TB,curPC+4);ctx.E(ppc_add(TA,TB,TA));}
        ctx.E(ppc_stw(TA,FRAME_SCR0,1));emitBX_target(ctx);ctx.done=true;return true;
    }
    if(rs==15)ctx.li(TA,curPC+4);else ctx.E(ppc_mr(TA,RA[rs]));
    switch(o){
        case 0:ctx.E(ppc_add(RA[rd],RA[rd],TA));break;
        case 1:ctx.E(ppc_mr(TB,RA[rd]));ctx.E(ppc_subfc(TC,TA,TB));
               setNZ(ctx,TC);setC_xer(ctx);setV_sub(ctx,TC,TB,TA);break;
        case 2:ctx.E(ppc_mr(RA[rd],TA));break;
    }
    return true;
}
static bool emitT_ldrPc(Ctx& ctx,uint16_t op,uint32_t curPC){
    uint8_t rd=(op>>8)&7;uint32_t addr=((curPC+4)&~3u)+((uint32_t)(op&0xFF)<<2);
    ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));ctx.li(TC,addr);ctx.call((void*)JitHelp_r32);
    ctx.E(ppc_mr(RA[rd],TA));return true;
}
static bool emitT_memReg(Ctx& ctx,uint16_t op){
    uint8_t rd=op&7,rb=(op>>3)&7,ro=(op>>6)&7,k=(op>>9)&7;
    void* fn=nullptr;bool ld=true,sxb=false,sxh=false;
    switch(k){
        case 0:fn=(void*)JitHelp_w32;ld=false;ctx.hasStore=true;break;
        case 1:fn=(void*)JitHelp_w16;ld=false;ctx.hasStore=true;break;
        case 2:fn=(void*)JitHelp_w8; ld=false;ctx.hasStore=true;break;
        case 3:fn=(void*)JitHelp_r8; sxb=true;break;
        case 4:fn=(void*)JitHelp_r32;break;
        case 5:fn=(void*)JitHelp_r16;break;
        case 6:fn=(void*)JitHelp_r8; break;
        case 7:fn=(void*)JitHelp_r16;sxh=true;break;
        default:return false;
    }
    ctx.E(ppc_add(TC,RA[rb],RA[ro]));ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if(!ld)ctx.E(ppc_mr(TD,RA[rd]));ctx.call(fn);
    if(ld){if(sxb)ctx.E(ppc_extsb(RA[rd],TA));else if(sxh)ctx.E(ppc_extsh(RA[rd],TA));else ctx.E(ppc_mr(RA[rd],TA));}
    return true;
}
static bool emitT_memImm(Ctx& ctx,uint16_t op){
    uint8_t rd=op&7,rb=(op>>3)&7;bool ld=(op>>11)&1;
    uint8_t h=(op>>12)&0xF;bool by=(h==7),hw=(h==8);
    uint32_t off=((op>>6)&0x1F)*(hw?2u:by?1u:4u);
    if(!ld)ctx.hasStore=true;
    ctx.li(TC,off);ctx.E(ppc_add(TC,RA[rb],TC));ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if(!ld)ctx.E(ppc_mr(TD,RA[rd]));
    void* fn=ld?(hw?(void*)JitHelp_r16:by?(void*)JitHelp_r8:(void*)JitHelp_r32)
               :(hw?(void*)JitHelp_w16:by?(void*)JitHelp_w8:(void*)JitHelp_w32);
    ctx.call(fn);if(ld)ctx.E(ppc_mr(RA[rd],TA));return true;
}
static bool emitT_spLoad(Ctx& ctx,uint16_t op,uint32_t curPC){
    bool ld=(op>>11)&1;uint8_t rd=(op>>8)&7;bool sp=(((op>>12)&0xF)==0x9);
    uint32_t off=(uint32_t)(op&0xFF)<<2;
    if(sp){ctx.li(TA,off);ctx.E(ppc_add(TC,RA[13],TA));}else ctx.li(TC,((curPC+4)&~3u)+off);
    if(!ld)ctx.hasStore=true;
    ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if(!ld)ctx.E(ppc_mr(TD,RA[rd]));ctx.call(ld?(void*)JitHelp_r32:(void*)JitHelp_w32);
    if(ld)ctx.E(ppc_mr(RA[rd],TA));return true;
}
static bool emitT_addSpPc(Ctx& ctx,uint16_t op,uint32_t curPC){
    uint8_t h=(op>>12)&0xF;
    if(h==0xA){
        uint8_t rd=(op>>8)&7;bool sp=(op>>11)&1;uint32_t imm=(uint32_t)(op&0xFF)<<2;
        if(sp){ctx.li(TA,imm);ctx.E(ppc_add(RA[rd],RA[13],TA));}
        else ctx.li(RA[rd],((curPC+4)&~3u)+imm);
        return true;
    }
    if(h==0xB){
        uint8_t s=(op>>8)&0xF;
        if(s==0){ctx.li(TA,(uint32_t)(op&0x7F)<<2);ctx.E(ppc_add(RA[13],RA[13],TA));return true;}
        if(s==1){ctx.li(TA,(uint32_t)(op&0x7F)<<2);ctx.E(ppc_subf(RA[13],TA,RA[13]));return true;}
    }
    return false;
}
static bool emitT_pushPop(Ctx& ctx,uint16_t op,uint32_t curPC){
    uint8_t opA=(op>>9)&7;if(opA!=2&&opA!=6)return false;
    bool isStore=(opA==2);
    if(isStore)ctx.hasStore=true;
    emitSpill(ctx);
    ctx.li(TA,curPC+2);ctx.E(ppc_stw(TA,FRAME_PC,1));
    ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.li(TC,(uint32_t)(uint16_t)op);ctx.E(ppc_addi(TD,1,(int16_t)FRAME_REGSYNC));
    ctx.E(ppc_addi(TE,1,(int16_t)FRAME_PC));ctx.E(ppc_addi(TF,1,(int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_thumbPushPop);ctx.E(ppc_stw(TA,FRAME_SCR0,1));
    emitReload(ctx);
    bool isPop=((op>>11)&1),hasR=((op>>8)&1);
    if(isPop&&hasR){
        ctx.E(ppc_lwz(TA,FRAME_SCR0,1));ctx.E(ppc_cmpi(0,TA,1));
        size_t b=ctx.sz();ctx.E(ppc_bc(12,2,0));
        emitCommitExit(ctx,curPC+2,EXIT_NORMAL);
        {int32_t d=(int32_t)((ctx.sz()-b)*4);ctx.base[b]=ppc_bc(12,2,(int16_t)d);}
        emitCommitExitDyn(ctx,EXIT_NORMAL);ctx.done=true;
    }
    return true;
}
static bool emitT_ldmStm(Ctx& ctx,uint16_t op){
#if JIT_NO_BLOCKXFER
    (void)op;(void)ctx;return false;
#endif
    bool load=((op>>11)&1)!=0;
    if(!load)ctx.hasStore=true;
    emitSpill(ctx);ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.li(TC,(uint32_t)(uint16_t)op);ctx.E(ppc_addi(TD,1,(int16_t)FRAME_REGSYNC));
    ctx.call((void*)JitHelp_thumbBlock);emitReload(ctx);return true;
}
static bool emitT_branch(Ctx& ctx,uint16_t op,uint32_t curPC){
    uint8_t h=(op>>12)&0xF;
    if(h==0xE){
        if(((op>>11)&1)!=0)return false;
        int32_t off=(int32_t)((int16_t)(op<<5))>>4;
        emitCommitExit(ctx,(uint32_t)(curPC+4+off),EXIT_NORMAL);
        ctx.done=true;return true;
    }
    if(h==0xD){
        uint8_t cond=(op>>8)&0xF;if(cond==0xF||cond==0xE)return false;
        int32_t off=((int32_t)(int8_t)(op&0xFF))<<1;
        int si=openCondSkip(ctx,cond);
        if(si<0)return false;
        emitCommitExit(ctx,(uint32_t)(curPC+4+off),EXIT_NORMAL);
        ctx.SETMARK(si);emitCommitExit(ctx,curPC+2,EXIT_NORMAL);ctx.RESOLVE();
        ctx.done=true;return true;
    }
    return false;
}
static bool emitT_bl(Ctx& ctx,uint16_t op1,uint16_t op2,uint32_t curPC){
    int32_t hi=(int32_t)((op1&0x7FF)<<21)>>9,lo=(op2&0x7FF)<<1;
    uint32_t tgt=(uint32_t)(curPC+4+hi+lo);
    bool blx=((op2>>11)&0x1F)==0x1C;
    ctx.li(RA[14],(curPC+4)|1u);
    if(blx){tgt&=~3u;ctx.li(TA,~(1u<<5));ctx.E(ppc_and(RCPSR,RCPSR,TA));}
    emitCommitExit(ctx,tgt&~1u,EXIT_NORMAL);ctx.done=true;return true;
}
static bool dispThumb(Ctx& ctx,uint16_t op,uint32_t curPC){
    uint8_t h=(op>>12)&0xF;
    if(JIT_NO_S){
        // bisect #1: shifts (0x0), imm8 ALU (0x1) and the ALU/hi-reg group (0x2) all write flags
        if(h==0x0||h==0x1||h==0x2)return false;
    }
    switch(h){
        case 0x0:if(((op>>11)&3)<3)return emitT_shifts(ctx,op);return emitT_addSub3(ctx,op);
        case 0x1:return emitT_imm8(ctx,op);
        case 0x2:{uint8_t b=(op>>10)&3;if(b==0)return emitT_alu(ctx,op);if(b==1)return emitT_hiReg(ctx,op,curPC);return emitT_ldrPc(ctx,op,curPC);}
        case 0x3:case 0x4:case 0x5:return emitT_memReg(ctx,op);
        case 0x6:case 0x7:case 0x8:return emitT_memImm(ctx,op);
        case 0x9:return emitT_spLoad(ctx,op,curPC);
        case 0xA:return emitT_addSpPc(ctx,op,curPC);
        case 0xB:
            if(((op>>8)&0xF)<=1)return emitT_addSpPc(ctx,op,curPC);
            if(((op>>9)&7)==2||((op>>9)&7)==6)return emitT_pushPop(ctx,op,curPC);
            return false;
        case 0xC:return emitT_ldmStm(ctx,op);
        case 0xD:case 0xE:return emitT_branch(ctx,op,curPC);
        default:return false;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Address validation
// ═══════════════════════════════════════════════════════════════════════
static bool validPC(uint32_t pc, bool gba) {
    pc &= ~1u;
    if (pc >= 0x80000000u) return false;
    if (gba) {
        return (pc <= 0x00003FFFu) ||
               (pc >= 0x02000000u && pc < 0x02040000u) ||
               (pc >= 0x03000000u && pc < 0x03008000u) ||
               (pc >= 0x06000000u && pc < 0x06018000u) ||
               (pc >= 0x08000000u && pc < 0x0E000000u);
    }
    return (pc < 0x00008000u) ||
           (pc >= 0x01000000u && pc < 0x02000000u) ||
           (pc >= 0x02000000u && pc < 0x02400000u) ||
           (pc >= 0x03000000u && pc < 0x03810000u) ||
           (pc >= 0x06000000u && pc < 0x07000000u) ||
           (pc >= 0x08000000u && pc < 0x0A000000u) ||
           (pc >= 0xFFFF0000u);
}

// ═══════════════════════════════════════════════════════════════════════
// Compile
// ═══════════════════════════════════════════════════════════════════════
#if JIT_LONG_BLOCKS
static const size_t REM_GUARD = 400;      // worst case: one long reg-shift-S + commit
#else
static const size_t REM_GUARD = 200;
#endif

static JitBlock* compile(Interpreter* interp,Core* core,
                          uint32_t armPC,bool arm7,int cpuIdx){
    if(!codeBuf||!g_jitLive||!interp||!core)return nullptr;
    if(!interp->isReady())return nullptr;
    if(!validPC(armPC,core->gbaMode))return nullptr;
#if JIT_NO_THUMB
    if(interp->isThumb())return nullptr;               // bisect #2
#endif
    bool thumb=interp->isThumb();
    size_t b=bucketOf(armPC);

    for(int w=0;w<2;w++){
        JitBlock& s=cache[b+w];
        if(s.valid&&s.armPC==armPC&&s.thumb==thumb&&s.gen==cacheGen&&s.nW>=16){
            s.use=++g_useCounter;
            return &s;
        }
    }
    // victim: the unused way, else the least recently used way
    int victim=0;
    if(cache[b+0].valid&&!cache[b+1].valid)victim=1;
    else if(cache[b+0].valid&&cache[b+1].valid)
        victim=(cache[b+0].use<=cache[b+1].use)?0:1;
    cache[b+victim].valid=false;

    if(codePos+BLK_WDS>=JIT_WORDS)flushJitCache();

    Ctx ctx;memset(&ctx,0,sizeof(ctx));
    ctx.base=codeBuf+codePos;ctx.cur=ctx.base;
    ctx.cap=JIT_WORDS-codePos;if(ctx.cap>BLK_WDS)ctx.cap=BLK_WDS;
    ctx.thumb=thumb;ctx.arm7=arm7;ctx.blockPC=armPC;
    ctx.cpuIdx=cpuIdx;ctx.interp=interp;ctx.core=core;ctx.insnCount=0;
    ctx.hasStore=false;ctx.nFix=0;ctx.nextMark=0;
    emitPrologue(ctx);emitSyncFrom(ctx);
    uint32_t curPC=armPC;int n=0;
    while(n<(int)BLK_ARMS&&!ctx.done&&!ctx.overflow){
        if(ctx.rem()<REM_GUARD){emitCommitExit(ctx,curPC,EXIT_NORMAL);ctx.done=true;break;}
        if(!validPC(curPC,core->gbaMode)){emitCommitExit(ctx,curPC,EXIT_FALLBACK);ctx.done=true;break;}
        if(thumb){
            uint16_t op=core->memory.read<uint16_t>(arm7,curPC);
            if(((op>>11)&0x1F)==0x1E){
                if(!validPC(curPC+2,core->gbaMode)){emitCommitExit(ctx,curPC,EXIT_FALLBACK);ctx.done=true;break;}
                uint16_t op2=core->memory.read<uint16_t>(arm7,curPC+2);
                uint8_t bb=(op2>>11)&0x1F;
                if(bb==0x1F||bb==0x1C){emitT_bl(ctx,op,op2,curPC);curPC+=4;n+=2;ctx.insnCount+=2;continue;}
            }
            if(!dispThumb(ctx,op,curPC)){
                fbLogOnce(true,cpuIdx,curPC,(uint32_t)op);
                emitCommitExit(ctx,curPC,EXIT_FALLBACK);ctx.done=true;
            }else{curPC+=2;n++;ctx.insnCount++;}
        }else{
            uint32_t op=core->memory.read<uint32_t>(arm7,curPC);
            if(!dispARM(ctx,op,curPC)){
                fbLogOnce(false,cpuIdx,curPC,op);
                emitCommitExit(ctx,curPC,EXIT_FALLBACK);ctx.done=true;
            }else{
                curPC+=4;n++;ctx.insnCount++;
#if !JIT_LONG_BLOCKS
                if(((op>>25)&7)==5)ctx.done=true;
                if((op&0x0FFFFFF0)==0x012FFF10)ctx.done=true;
                if((op&0x0E000000)==0x0A000000)ctx.done=true;
#endif
            }
        }
    }
    if(!ctx.done&&!ctx.overflow)emitCommitExit(ctx,curPC,EXIT_NORMAL);
    if(ctx.overflow||ctx.sz()<16){
        DebugLog("[JIT] compile FAIL pc=%08X overflow=%d sz=%zu\n",armPC,(int)ctx.overflow,ctx.sz());
        return nullptr;
    }
    if(ctx.base[ctx.sz()-1]!=ppc_blr()){
        DebugLog("[JIT] compile no-BLR pc=%08X\n",armPC);
        return nullptr;
    }
    size_t wds=ctx.sz();flushICache(ctx.base,wds);
    JitBlock& slot=cache[b+victim];
    slot.armPC=armPC;slot.endPC=curPC;slot.code=ctx.base;slot.nW=(uint32_t)wds;
    slot.gen=cacheGen;slot.thumb=thumb;slot.insnCount=(uint32_t)ctx.insnCount;
    slot.hasStore=ctx.hasStore;slot.use=++g_useCounter;slot.valid=true;
    markPage(armPC);
    codePos+=wds;
    return &slot;
}

// ═══════════════════════════════════════════════════════════════════════
// Inline scheduler tick  (F9: dispatch counters)
// ═══════════════════════════════════════════════════════════════════════
static inline void tickInline(Core& core,uint32_t cycles){
    core.globalCycles+=cycles;
    while(!core.events.empty()&&
          core.globalCycles>=core.events.front().cycles){
        SchedEvent e=core.events.front();
        core.events.erase(core.events.begin());
        if(e.task>=0&&e.task<MAX_TASKS&&core.tasks[e.task].fn){
#if JIT_EV_COUNTERS
            g_evCount[e.task]++;
#endif
            core.tasks[e.task]();
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// F10: stuck-PC watchdog
// ═══════════════════════════════════════════════════════════════════════
#if JIT_WATCHDOG
struct BlkRec {
    uint32_t pc,cpsr,ops[4];
    int cpu,thumb,insns,reason;
};
static BlkRec   g_ring[32];
static int      g_ringIdx=0;
static uint32_t g_samePC[2]={~0u,~0u};
static uint32_t g_sameCount[2]={0,0};

static void traceBlock(Core& core,int cpu,const JitBlock* b,int reason,uint32_t cpsr){
    BlkRec& r=g_ring[g_ringIdx++ & 31];
    r.pc=b->armPC;r.cpsr=cpsr;r.cpu=cpu;r.thumb=(int)b->thumb;
    r.insns=(int)b->insnCount;r.reason=reason;
    if(b->thumb){ for(int i=0;i<4;i++) r.ops[i]=core.memory.read<uint16_t>((bool)cpu==1,b->armPC+i*2); }
    else        { for(int i=0;i<4;i++) r.ops[i]=core.memory.read<uint32_t>((bool)cpu==1,b->armPC+i*4); }

    if(b->armPC==g_samePC[cpu]){
        if(++g_sameCount[cpu]>=4000){
            DebugLog("[WATCH] cpu%d pinned at %08X cpsr=%08X lastExit=%d insns=%d thumb=%d\n",
                     cpu,b->armPC,cpsr,reason,(int)b->insnCount,(int)b->thumb);
            for(int i=16;i<32;i++){
                const BlkRec& x=g_ring[(g_ringIdx+i)&31];
                DebugLog("  [WATCH] cpu%d pc=%08X thumb=%d n=%2d exit=%d cpsr=%08X ops=%08X %08X %08X %08X\n",
                         x.cpu,x.pc,x.thumb,x.insns,x.reason,x.cpsr,x.ops[0],x.ops[1],x.ops[2],x.ops[3]);
            }
            DebugLog("[WATCH] hint: exit=0 with the same PC => a predicate never flips (check CPSR C/V).\n"
                     "[WATCH] hint: exit=1 with the same PC => missing emitter for the opcode at that PC.\n");
            g_sameCount[cpu]=0;
        }
    }else{g_samePC[cpu]=b->armPC;g_sameCount[cpu]=0;}
}
#endif

// ═══════════════════════════════════════════════════════════════════════
// F11: differential harness (JIT vs interpreter, store-free blocks only)
// ═══════════════════════════════════════════════════════════════════════
#if JIT_DIFF
static const int JIT_DIFF_BLOCKS = 4000;

struct Snap { uint32_t r[15]; uint32_t cpsr; uint32_t pc; };

static bool snapOf(Interpreter& in,Snap& s){
    if(!in.isReady())return false;
    uint32_t** p=in.getRegisters();
    if(!p)return false;
    for(int k=0;k<15;k++){ if(!p[k])return false; s.r[k]=*p[k]; }
    s.cpsr=in.getCpsrRef();
    s.pc=in.getActualPC();
    return true;
}
static void restoreOf(Interpreter& in,const Snap& s){
    uint32_t** p=in.getRegisters();
    for(int k=0;k<15;k++)*p[k]=s.r[k];
    in.getCpsrRef()=s.cpsr;
    in.setPC(s.pc);            // cpsr is restored first, so the T bit selects the right pipeline
}
static void diffBlock(Core& core,int cpu,Interpreter& interp,JitBlock* b){
    if(b->hasStore)return;     // stores cannot be replayed
    Snap before,afterJit,afterInt;
    if(!snapOf(interp,before))return;
    uint32_t jitExitPC=g_exitPC[cpu];
    int      jitReason=g_exitReason[cpu];
    if(!snapOf(interp,afterJit))return;

    restoreOf(interp,before);
    for(uint32_t k=0;k<b->insnCount;k++)interp.jitRunOpcode();
    if(!snapOf(interp,afterInt))return;

    uint32_t cpsrDiff=afterJit.cpsr^afterInt.cpsr;
    bool bad=(cpsrDiff!=0)||(afterJit.pc!=afterInt.pc);
    int badReg=-1;
    if(!bad) for(int k=0;k<15;k++) if(afterJit.r[k]!=afterInt.r[k]){bad=true;badReg=k;break;}

    if(bad){
        DebugLog("[DIFF] cpu%d pc=%08X thumb=%d insns=%u exit=%d\n",
                 cpu,b->armPC,(int)b->thumb,b->insnCount,jitReason);
        DebugLog("[DIFF]   pc   jit=%08X int=%08X\n",afterJit.pc,afterInt.pc);
        DebugLog("[DIFF]   cpsr jit=%08X int=%08X xor=%08X N%c Z%c C%c V%c T%c\n",
                 afterJit.cpsr,afterInt.cpsr,cpsrDiff,
                 (cpsrDiff&0x80000000u)?'!':'.',(cpsrDiff&0x40000000u)?'!':'.',
                 (cpsrDiff&0x20000000u)?'!':'.',(cpsrDiff&0x10000000u)?'!':'.',
                 (cpsrDiff&0x20u)?'!':'.');
        if(badReg>=0)
            DebugLog("[DIFF]   r%d jit=%08X int=%08X\n",badReg,afterJit.r[badReg],afterInt.r[badReg]);
        for(int i=0;i<4;i++){
            if(b->thumb)DebugLog("[DIFF]   op%d=%04X\n",i,core.memory.read<uint16_t>((bool)cpu==1,b->armPC+i*2));
            else        DebugLog("[DIFF]   op%d=%08X\n",i,core.memory.read<uint32_t>((bool)cpu==1,b->armPC+i*4));
        }
    }
    // always continue from the JIT's committed state
    restoreOf(interp,afterJit);
    g_exitPC[cpu]=jitExitPC;
    g_exitReason[cpu]=jitReason;
}

static int g_diffBudget=JIT_DIFF_BLOCKS;
#endif

// ═══════════════════════════════════════════════════════════════════════
// Per-CPU runner (single block, no blocking spin loops)
// ═══════════════════════════════════════════════════════════════════════
static uint32_t runCpu(Core& core,int cpu,bool gba){
    Interpreter& interp=core.interpreter[cpu];
    if(interp.halted) return 0;

    const bool     arm7      =(cpu==1)||gba;
    const uint32_t cycPerInsn=arm7?CYCLES_PER_INSN_ARM7:CYCLES_PER_INSN_ARM9;

    if(!interp.isReady()){
        interp.jitRunOpcode();
        return cycPerInsn;
    }

    uint32_t pc=interp.getActualPC();

    if(pc==0xFFFFFFFFu||!validPC(pc,gba)){
        static uint32_t lastBadPC[2]={~0u,~0u};
        if(pc!=lastBadPC[cpu]){
            DebugLog("[JIT] cpu%d bad PC %08X\n",cpu,pc);
            lastBadPC[cpu]=pc;
        }
        interp.jitRunOpcode();
        return cycPerInsn;
    }

    g_exitReason[cpu]=EXIT_FALLBACK;
    g_exitPC[cpu]    =pc;

    JitBlock* b=compile(&interp,&core,pc,arm7,cpu);
    if(!b||!b->code||b->nW<16||
       b->code<codeBuf||b->code+b->nW>codeBuf+JIT_WORDS){
        interp.jitRunOpcode();
        return cycPerInsn;
    }

    executeBlock_asm(b->code);

    const int reason=g_exitReason[cpu];
    g_totalJIT[cpu]++;

#if JIT_WATCHDOG
    traceBlock(core,cpu,b,reason,interp.getCpsrRef());
#endif
#if JIT_DIFF
    if(g_diffBudget>0){
        g_diffBudget--;
        diffBlock(core,cpu,interp,b);
    }
#endif

    // F8: charge for the work the block actually retired.  A fallback block already
    // executed b->insnCount instructions and committed at the faulting PC, so that
    // work must be charged in addition to the one interpreter instruction we run now.
    uint32_t n=b->insnCount>0?b->insnCount:1u;
    if(reason==EXIT_FALLBACK){
        interp.jitRunOpcode();
        return n*cycPerInsn+cycPerInsn;
    }
    return n*cycPerInsn;
}

// ═══════════════════════════════════════════════════════════════════════
// Status log
// ═══════════════════════════════════════════════════════════════════════
static uint32_t g_statusTick=0;
static void logStatus(Core& core){
    g_statusTick++;
    if((g_statusTick&0xFF)!=0)return;
    DebugLog("[JIT] STATUS jit0=%u fb0=%u jit1=%u fb1=%u pos=%zu gen=%u ev<reset=%u line256=%u spu=%u irq9=%u irq7=%u>\n",
             g_totalJIT[0],g_totalFB[0],g_totalJIT[1],g_totalFB[1],codePos,cacheGen,
             g_evCount[RESET_CYCLES],g_evCount[NDS_SCANLINE256],g_evCount[NDS_SPU_SAMPLE],
             g_evCount[ARM9_INTERRUPT],g_evCount[ARM7_INTERRUPT]);
    DebugLog("[JIT] STATUS pc0=%08X pc1=%08X h0=%d h1=%d fps=%d cyc=%u\n",
             core.interpreter[0].isReady()?core.interpreter[0].getActualPC():0u,
             core.interpreter[1].isReady()?core.interpreter[1].getActualPC():0u,
             (int)core.interpreter[0].halted,
             (int)core.interpreter[1].halted,
             core.fps,core.globalCycles);
}

// ═══════════════════════════════════════════════════════════════════════
// Run functions
// ═══════════════════════════════════════════════════════════════════════
void runJitNds(Core& core){
    if(!g_jitLive||!codeBuf){Interpreter::runCoreNds(core);return;}

    for(int i=0;i<ITERS_NDS;i++){
        uint32_t c0 = runCpu(core, 0, false);   // ARM9
        uint32_t c1 = runCpu(core, 1, false);   // ARM7

        uint32_t cyc7_in_9 = c1 * 2;
#if JIT_CHARGE_MAX
        uint32_t charge = (c0 > cyc7_in_9) ? c0 : cyc7_in_9;
#else
        uint32_t charge = c0 + cyc7_in_9;       // serialized work = elapsed emulated time
#endif
        if(charge == 0) charge = 8;             // both halted: idle step
        tickInline(core, charge);

        if(!core.running) return;               // updateRun()/endFrame() stopped us
    }

    logStatus(core);
}

void runJitGba(Core& core){
    if(!g_jitLive||!codeBuf){Interpreter::runCoreSingle<true,0>(core);return;}

    for(int i=0;i<ITERS_GBA;i++){
        uint32_t c = runCpu(core, 1, true);
        if(c == 0) c = 8;                       // halted: idle step so VBlank can fire
        tickInline(core, c);
        if(!core.running) return;
    }

    logStatus(core);
}

// ═══════════════════════════════════════════════════════════════════════
// Init / shutdown
// ═══════════════════════════════════════════════════════════════════════
bool initJit(Core* core){
    g_jitLive=false;codeBuf=nullptr;
    void* raw=memalign(32,JIT_BYTES);
    if(!raw){printf("[JIT] memalign failed\n");return false;}
    uintptr_t addr=(uintptr_t)raw;
    bool ok=(addr>=0x80000000u&&addr+JIT_BYTES<=0x81800000u);
    if(!ok&&addr<0x01800000u){addr|=0x80000000u;ok=(addr+JIT_BYTES<=0x81800000u);}
    else if(!ok&&addr>=0xC0000000u&&addr<0xC1800000u){addr-=0x40000000u;ok=(addr+JIT_BYTES<=0x81800000u);}
    if(!ok){printf("[JIT] not in MEM1: %p\n",raw);free(raw);return false;}
    uintptr_t tr=(uintptr_t)(void*)executeBlock_asm;
    if(tr<0x80000000u||tr>=0x81800000u){printf("[JIT] bad trampoline %p\n",(void*)tr);free(raw);return false;}
    codeBuf=(uint32_t*)addr;codePos=0;cacheGen=0;
    g_fbLogCount=0;
    g_totalFB[0]=g_totalFB[1]=0;
    g_totalJIT[0]=g_totalJIT[1]=0;
    g_statusTick=0;
    g_useCounter=0;
    memset(g_evCount,0,sizeof(g_evCount));
    g_irqCount[0]=g_irqCount[1]=0;
    for(size_t i=0;i<CSIZ*2;i++)cache[i].valid=false;
    memset(g_pageSeen,0,sizeof(g_pageSeen));
    memset(g_exitPC,  0,sizeof g_exitPC);
    memset(g_exitCPSR,0,sizeof g_exitCPSR);
    g_exitReason[0]=g_exitReason[1]=EXIT_FALLBACK;
#if JIT_WATCHDOG
    g_ringIdx=0;
    g_samePC[0]=g_samePC[1]=~0u;
    g_sameCount[0]=g_sameCount[1]=0;
#endif
#if JIT_DIFF
    g_diffBudget=JIT_DIFF_BLOCKS;
#endif
    memset(codeBuf,0,JIT_BYTES);
    DCFlushRange(codeBuf,JIT_BYTES);
    ICInvalidateRange(codeBuf,JIT_BYTES);
    g_jitLive=true;
    printf("[JIT] ready buf=%p (%zuKB) tramp=%p BLK_ARMS=%zu\n",
           (void*)codeBuf,JIT_BYTES>>10,(void*)tr,BLK_ARMS);
    DebugLog("[JIT] init buf=%p tramp=%p BLK_ARMS=%zu long=%d regShiftC=%d thumbAluC=%d diff=%d\n",
             (void*)codeBuf,(void*)tr,BLK_ARMS,JIT_LONG_BLOCKS,JIT_REG_SHIFT_CARRY,
             JIT_THUMB_ALU_CARRY,JIT_DIFF);
    if(core)core->setRunFunc(core->gbaMode?runJitGba:runJitNds);
    return true;
}

void shutdownJit(Core* core){
    DebugLog("[JIT] shutdown fb0=%u fb1=%u jit0=%u jit1=%u\n",
             g_totalFB[0],g_totalFB[1],g_totalJIT[0],g_totalJIT[1]);
    g_jitLive=false;
    if(core)core->setRunFunc(core->gbaMode
        ?static_cast<void(*)(Core&)>(&Interpreter::runCoreSingle<true,0>)
        :&Interpreter::runCoreNds);
    codeBuf=nullptr;codePos=0;
    for(size_t i=0;i<CSIZ*2;i++)cache[i].valid=false;
    memset(g_pageSeen,0,sizeof(g_pageSeen));
}

// F8: invalidation is O(1) unless the page ever held compiled code.
void invalidateJitRange(uint32_t start,uint32_t end){
#if JIT_INVALIDATE
    if(start>=PAGE_BITS_BYTES)return;
    if(end>PAGE_BITS_BYTES)end=PAGE_BITS_BYTES;
    bool any=false;
    for(uint32_t p=start&~0xFFFu;p<end;p+=0x1000u)
        if(g_pageSeen[pageIdx(p)]){any=true;g_pageSeen[pageIdx(p)]=0;}
    if(!any)return;
    for(size_t i=0;i<CSIZ*2;i++){
        JitBlock& s=cache[i];
        if(s.valid&&s.armPC<end&&s.endPC>start)s.valid=false;
    }
#else
    (void)start;(void)end;
#endif
}

} // namespace JitPpc
