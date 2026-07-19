// jit_ppc.cpp — ARM->PPC JIT for Wii/Broadway
// Fixed revision 9

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

static const int EXIT_NORMAL   = 0;
static const int EXIT_FALLBACK = 1;

static uint32_t g_exitPC[2]    = {};
static uint32_t g_exitCPSR[2]  = {};
static int      g_exitReason[2]= {};

static const uint32_t CYCLES_PER_INSN_ARM9 = 2;
static const uint32_t CYCLES_PER_INSN_ARM7 = 1;

static const int ITERS_NDS = 32;
static const int ITERS_GBA = 64;

static const uint32_t FLOOR_CYCLES_NDS = 2130;
static const uint32_t FLOOR_CYCLES_GBA = 1232;

// Raised thresholds to reduce false positives during init
static const uint32_t SPIN_THRESH    = 24;
static const int      SPIN_STEPS     = 256;
static const int      DEADLOCK_STEPS = 512;
static const uint32_t SPIN_GIVEUP    = 32;

// ── Frame layout ──────────────────────────────────────────────────────
static const int FRAME_SIZE    = 256;
static const int FRAME_LR_OFF  = FRAME_SIZE + 4;
static const int FRAME_SAVE    = 16;
static const int FRAME_CORE    = 88;
static const int FRAME_INTERP  = 92;
static const int FRAME_CPUIDX  = 96;
static const int FRAME_SCR0    = 100;
static const int FRAME_SCR1    = 104;
static const int FRAME_SCR2    = 108;
static const int FRAME_REGSYNC = 112;
static const int FRAME_CPSR    = 172;
static const int FRAME_PC      = 176;

static_assert(FRAME_SIZE % 16 == 0,            "frame align");
static_assert(FRAME_SAVE + 18*4 == FRAME_CORE, "save map");
static_assert(FRAME_REGSYNC + 15*4 == FRAME_CPSR, "regsync map");
static_assert(FRAME_PC + 4 <= FRAME_SIZE,       "pc fits");

// ── Debug ─────────────────────────────────────────────────────────────
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

// ── Per-CPU spin state ────────────────────────────────────────────────
struct SpinState {
    uint32_t spinPC       = 0xFFFFFFFFu;
    uint32_t prevPC       = 0xFFFFFFFFu;  // for ping-pong detection
    uint32_t spinCount    = 0;
    uint32_t giveupCount  = 0;
    bool     spinning     = false;
    bool     logged       = false;
};
static SpinState g_spin[2];

static void spinReset(int cpu) {
    g_spin[cpu] = SpinState{};
}

// Returns true when spin confirmed. entryPC = PC before block ran, exitPC = PC after.
static bool spinUpdate(int cpu, uint32_t entryPC, uint32_t exitPC) {
    SpinState& s = g_spin[cpu];

    // Count as spin if: returned to same PC, or ping-pong A->B->A
    bool samePC   = (exitPC == entryPC);
    bool pingPong = (s.prevPC != 0xFFFFFFFFu && exitPC == s.prevPC && entryPC == s.spinPC);

    if (samePC || pingPong) {
        s.spinCount++;
    } else {
        // Made real forward progress
        s.spinCount   = 0;
        s.giveupCount = 0;
        s.logged      = false;
        s.spinning    = false;
    }
    s.prevPC = entryPC;
    s.spinPC = exitPC;

    if (s.spinCount >= SPIN_THRESH) {
        s.spinning = true;
        return true;
    }
    return false;
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

static const uint8_t RA[15]={14,15,16,17,18,19,20,21,22,23,24,25,26,27,28};
static const uint8_t RCPSR=29;
static const uint8_t TA=3,TB=4,TC=5,TD=6,TE=7,TF=8,TG=9;
static const uint8_t RCALL=11;

// ═══════════════════════════════════════════════════════════════════════
// Code buffer
// ═══════════════════════════════════════════════════════════════════════
static const size_t JIT_BYTES=2u*1024u*1024u;
static const size_t JIT_WORDS=JIT_BYTES/4;
static const size_t BLK_ARMS=8;
static const size_t BLK_WDS =800;

static uint32_t* codeBuf =nullptr;
static size_t    codePos =0;
static uint32_t  cacheGen=0;
static bool      g_jitLive=false;

struct JitBlock {
    uint32_t  armPC;
    uint32_t* code;
    uint32_t  nW;
    uint32_t  gen;
    uint32_t  insnCount;
    bool      thumb;
    bool      valid;
};
static const size_t CSIZ=1u<<12;
static JitBlock cache[CSIZ];

static size_t hashPC(uint32_t pc){return(pc>>1)&(CSIZ-1);}

void flushJitCache(){
    codePos=0;++cacheGen;
    for(size_t i=0;i<CSIZ;i++)cache[i].valid=false;
    DebugLog("[JIT] cache flushed gen=%u\n",cacheGen);
}

static void flushICache(uint32_t* p,size_t nW){
    DCFlushRange(p,nW*4);
    ICInvalidateRange(p,nW*4);
}

// ═══════════════════════════════════════════════════════════════════════
// Emit context
// ═══════════════════════════════════════════════════════════════════════
struct Ctx {
    uint32_t *base,*cur;
    size_t cap;
    bool thumb,arm7,done,overflow;
    uint32_t blockPC;
    int cpuIdx;
    Interpreter* interp;
    Core* core;
    int insnCount;

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
};

// ═══════════════════════════════════════════════════════════════════════
// C helpers
// ═══════════════════════════════════════════════════════════════════════
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

// FIX: Split block-xfer helper into two functions to stay within 8 PPC arg registers.
// armBlock_load / armBlock_store each take <= 6 args.

int JitHelp_armBlockLoad(Core* core, int arm7, uint32_t op,
                         uint32_t* regs, uint32_t* pcOut, uint32_t* cpsrInOut) {
    if(!core||!regs||!pcOut||!cpsrInOut) return -1;
    const bool p=(op>>24)&1, u=(op>>23)&1, S=(op>>22)&1, w=(op>>21)&1;
    const uint8_t  rn   = (op>>16)&0xF;
    const uint16_t list = (uint16_t)(op&0xFFFF);
    if(S || rn>14 || !list) return -1;
    int n=0;
    for(int i=0;i<16;i++) if(list&(1u<<i)) n++;
    const uint32_t base = regs[rn];
    uint32_t addr, wb;
    if(u){ wb=base+(uint32_t)n*4u; addr=p?base+4u:base; }
    else { wb=base-(uint32_t)n*4u; addr=p?wb       :wb+4u; }
    int wrotePC=0;
    for(int i=0;i<16;i++){
        if(!(list&(1u<<i))) continue;
        uint32_t val=core->memory.read<uint32_t>((bool)arm7,addr); addr+=4;
        if(i==15){
            if(val&1u){ *cpsrInOut|=(1u<<5); *pcOut=val&~1u; }
            else      { *cpsrInOut&=~(1u<<5); *pcOut=val&~3u; }
            wrotePC=1;
        } else regs[i]=val;
    }
    if(w && !(list&(1u<<rn))) regs[rn]=wb;
    return wrotePC;
}

int JitHelp_armBlockStore(Core* core, int arm7, uint32_t op,
                          uint32_t* regs, uint32_t pcForR15) {
    if(!core||!regs) return -1;
    const bool p=(op>>24)&1, u=(op>>23)&1, S=(op>>22)&1, w=(op>>21)&1;
    const uint8_t  rn   = (op>>16)&0xF;
    const uint16_t list = (uint16_t)(op&0xFFFF);
    if(S || rn>14 || !list) return -1;
    int n=0;
    for(int i=0;i<16;i++) if(list&(1u<<i)) n++;
    const uint32_t base = regs[rn];
    uint32_t addr, wb;
    if(u){ wb=base+(uint32_t)n*4u; addr=p?base+4u:base; }
    else { wb=base-(uint32_t)n*4u; addr=p?wb       :wb+4u; }
    for(int i=0;i<16;i++){
        if(!(list&(1u<<i))) continue;
        core->memory.write<uint32_t>((bool)arm7,addr,(i==15)?pcForR15:regs[i]);
        addr+=4;
    }
    if(w) regs[rn]=wb;
    return 0;
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

static void emitSyncFrom(Ctx& ctx){
    ctx.ldInterp();
    ctx.E(ppc_addi(TB,1,(int16_t)FRAME_REGSYNC));
    ctx.E(ppc_addi(TC,1,(int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_syncFrom);
    ctx.E(ppc_cmpi(0,TA,0));
    size_t bOk=ctx.sz();
    ctx.E(ppc_bc(12,2,0));
    // syncFrom failed: store FALLBACK and return
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
// Condition skip
// ═══════════════════════════════════════════════════════════════════════
static size_t emitCondSkip(Ctx& ctx,uint8_t cond){
    if(cond==14||cond==15)return SIZE_MAX;
    ctx.E(ppc_mr(TA,RCPSR));
    ctx.E(ppc_addi(TB,0,(int16_t)cond));
    ctx.call((void*)JitHelp_testCond);
    ctx.E(ppc_cmpi(0,TA,0));
    size_t idx=ctx.sz();
    ctx.E(ppc_bc(12,2,0)); // branch if condition TRUE (skip the skip)
    return idx;
}
static void patchSkip(Ctx& ctx,size_t idx){
    if(idx==SIZE_MAX)return;
    int32_t off=(int32_t)((ctx.sz()-idx)*4);
    if(off<-32768||off>32764){ctx.overflow=true;return;}
    ctx.base[idx]=ppc_bc(12,2,(int16_t)off);
}

// ═══════════════════════════════════════════════════════════════════════
// Flag helpers
// ═══════════════════════════════════════════════════════════════════════
static void setNZ(Ctx& ctx,uint8_t r){
    // Clear N and Z bits (bits 31 and 30 of CPSR)
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,2,31));
    // Set N from bit 31 of r
    ctx.E(ppc_rlwinm(TA,r,0,0,0));  // isolate bit 31
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
    // Set Z: compare r with 0, extract EQ bit from CR6
    ctx.E(ppc_cmpi(6,r,0));
    ctx.E(ppc_mfcr(TA));
    // CR6 EQ is bit 25 of CR word -> shift to bit 30 of CPSR
    ctx.E(ppc_rlwinm(TA,TA,25,1,1));
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}

// FIX: setC_xer - XER CA (carry) is bit 29 of XER.
// For ARM carry: ARM C=1 means carry out. PPC addc/subfc sets CA.
// For subfc (a-b): CA=1 means NO borrow (i.e., a>=b), matching ARM C for SUB.
static void setC_xer(Ctx& ctx){
    ctx.E(ppc_mfxer(TA));
    // XER CA is bit 29. Shift to bit 29 of CPSR (ARM C flag position).
    ctx.E(ppc_rlwinm(TA,TA,0,29,29));  // keep only bit 29
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,3,1)); // clear bit 29 of CPSR (C flag)
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}

static void setV_add(Ctx& ctx,uint8_t res,uint8_t a,uint8_t b){
    // V = (a and b have same sign) AND (res has different sign from a)
    ctx.E(ppc_xor(TE,res,a));
    ctx.E(ppc_xor(TF,res,b));  // FIX: was xor(TF,res,b) — correct
    // Wait — for add: V = ~(a^b) & (a^res), i.e. inputs same sign, output different
    // Re-do properly:
    // TE = a ^ b (different input signs -> no overflow possible if set)
    // TF = a ^ res
    // V = ~(a^b) & (a^res) -> top bit
    // But we had: TE=res^a, TF=res^b, AND -> this gives (res^a)&(res^b)
    // (res^a)&(res^b): set when res differs from both a and b -> correct for ADD overflow
    ctx.E(ppc_and(TE,TE,TF));
    ctx.E(ppc_rlwinm(TE,TE,0,0,0));   // keep only bit 31
    ctx.E(ppc_rlwinm(TE,TE,29,28,28)); // shift bit 31 -> bit 28 (ARM V position)
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,4,2)); // clear bit 28 of CPSR
    ctx.E(ppc_or(RCPSR,RCPSR,TE));
}
static void setV_sub(Ctx& ctx,uint8_t res,uint8_t a,uint8_t b){
    // SUB: a-b. V = (a^b) & (a^res) -> top bit
    // inputs differ in sign AND result differs from minuend
    ctx.E(ppc_xor(TE,a,b));
    ctx.E(ppc_xor(TF,a,res));
    ctx.E(ppc_and(TE,TE,TF));
    ctx.E(ppc_rlwinm(TE,TE,0,0,0));
    ctx.E(ppc_rlwinm(TE,TE,29,28,28));
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,4,2));
    ctx.E(ppc_or(RCPSR,RCPSR,TE));
}
static void setC_imm(Ctx& ctx,uint8_t cr){
    // cr holds the carry-out bit in bit 0 (lsb) from shifter
    ctx.E(ppc_rlwinm(TA,cr,29,29,29)); // shift bit 0 -> bit 29
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,3,1)); // clear CPSR C
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}

// FIX: primeCarry - ARM SBC = Rn - Op2 - NOT(C) = Rn - Op2 + C - 1
// PPC subfe(d,a,b) = b - a + XER_CA - 1
// We need XER_CA = ARM_C. So prime XER with ARM carry.
static void primeCarry(Ctx& ctx){
    // Extract ARM C flag (bit 29) into XER CA (bit 29)
    // Set XER_CA = (CPSR >> 29) & 1
    ctx.E(ppc_rlwinm(TA,RCPSR,0,29,29)); // TA = ARM C in bit 29
    // XER format: SO=bit31, OV=bit30, CA=bit29
    // We need to set XER with CA = ARM C, clear SO and OV
    ctx.E(ppc_mtxer(TA));  // Set XER = TA (CA bit matches)
}

// ═══════════════════════════════════════════════════════════════════════
// Shifter
// ═══════════════════════════════════════════════════════════════════════
static void sLslI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i==0){if(d!=s)ctx.E(ppc_mr(d,s));if(sc)ctx.E(ppc_rlwinm(TC,RCPSR,3,31,31));}
    else if(i<32){if(sc)ctx.E(ppc_rlwinm(TC,s,(uint8_t)i,31,31));ctx.E(ppc_rlwinm(d,s,(uint8_t)i,0,(uint8_t)(31-i)));}
    else if(i==32){if(sc)ctx.E(ppc_rlwinm(TC,s,0,31,31));ctx.E(ppc_addi(d,0,0));}
    else{if(sc)ctx.E(ppc_addi(TC,0,0));ctx.E(ppc_addi(d,0,0));}
}
static void sLsrI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i==32){if(sc)ctx.E(ppc_rlwinm(TC,s,1,31,31));ctx.E(ppc_addi(d,0,0));}
    else if(i>0&&i<32){if(sc)ctx.E(ppc_rlwinm(TC,s,(uint8_t)((33-i)&31),31,31));ctx.E(ppc_rlwinm(d,s,(uint8_t)(32-i),(uint8_t)i,31));}
    else{if(sc)ctx.E(ppc_addi(TC,0,0));ctx.E(ppc_addi(d,0,0));}
}
static void sAsrI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i>=32){if(sc)ctx.E(ppc_rlwinm(TC,s,1,31,31));ctx.E(ppc_srawi(d,s,31));}
    else if(i>0){if(sc)ctx.E(ppc_rlwinm(TC,s,(uint8_t)((33-i)&31),31,31));ctx.E(ppc_srawi(d,s,(uint8_t)i));}
    else{if(d!=s)ctx.E(ppc_mr(d,s));if(sc)ctx.E(ppc_rlwinm(TC,RCPSR,3,31,31));}
}
static void sRorI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i==0){
        // RRX: rotate right through carry
        if(sc)ctx.E(ppc_rlwinm(TC,s,0,31,31)); // new carry = bit 0
        ctx.E(ppc_rlwinm(TA,RCPSR,3,0,0));     // old carry -> bit 31
        ctx.E(ppc_rlwinm(d,s,31,1,31));         // s >> 1
        ctx.E(ppc_or(d,d,TA));
    }else{
        i&=31;
        if(i==0){if(d!=s)ctx.E(ppc_mr(d,s));if(sc)ctx.E(ppc_rlwinm(TC,s,1,31,31));}
        else{if(sc)ctx.E(ppc_rlwinm(TC,s,(uint8_t)((33-i)&31),31,31));ctx.E(ppc_rlwinm(d,s,(uint8_t)(32-i),0,31));}
    }
}

static bool emitShifter(Ctx& ctx,uint32_t op,uint8_t dst,bool sc){
    if((op>>25)&1){
        uint32_t v=op&0xFF,rot=((op>>8)&0xF)*2;
        if(rot)v=(v>>rot)|(v<<(32-rot));
        ctx.li(dst,v);
        if(sc&&rot){
            // carry = bit 31 of rotated value
            ctx.E(ppc_rlwinm(TC,dst,0,31,31));
            return true;
        }
        return false;
    }
    uint8_t rm=op&0xF;
    if(rm==15)return false;
    uint8_t st=(op>>5)&3;
    if(!((op>>4)&1)){
        int sa=(op>>7)&0x1F;
        switch(st){
            case 0:sLslI(ctx,dst,RA[rm],sa,sc);break;
            case 1:sLsrI(ctx,dst,RA[rm],sa?sa:32,sc);break;
            case 2:sAsrI(ctx,dst,RA[rm],sa?sa:32,sc);break;
            default:sRorI(ctx,dst,RA[rm],sa,sc);break;
        }
        return sc;
    }
    uint8_t rs=(op>>8)&0xF;
    if(rs==15)return false;
    ctx.E(ppc_rlwinm(TD,RA[rs],0,24,31));
    ctx.E(ppc_mr(TA,RA[rm]));
    switch(st){
        case 0:ctx.E(ppc_slw(dst,TA,TD));break;
        case 1:ctx.E(ppc_srw(dst,TA,TD));break;
        case 2:ctx.E(ppc_sraw(dst,TA,TD));break;
        default:ctx.E(ppc_subfic(TB,TD,32));ctx.E(ppc_rlwnm(dst,TA,TB,0,31));break;
    }
    return false;
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
    size_t si=emitCondSkip(ctx,cond);
    if(rn==15){ctx.li(TD,curPC+(ctx.thumb?4u:8u));ctx.E(ppc_stw(TD,FRAME_SCR2,1));}
    bool logC=(s&&(dop==AND||dop==EOR||dop==TST||dop==TEQ||dop==ORR||dop==MOV||dop==BIC||dop==MVN));
    bool cset=emitShifter(ctx,op,TA,logC);
    // FIX: Prime carry AFTER shifter (shifter may clobber TA/TC)
    if(dop==ADC||dop==SBC||dop==RSC) primeCarry(ctx);
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
        // FIX: SBC = Rn - Op2 + C - 1. PPC subfe(d,a,b) = b - a + CA - 1.
        // So subfe(res, TA, srcRn) = srcRn - TA + CA - 1. Correct.
        case SBC:         ctx.E(ppc_subfe(res,TA,srcRn));break;
        // FIX: RSC = Op2 - Rn + C - 1. subfe(res, srcRn, TA) = TA - srcRn + CA - 1.
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
            case ADD:case CMN:case ADC:setNZ(ctx,res);setC_xer(ctx);setV_add(ctx,res,opA,opB);break;
            // FIX: SUB/SBC carry: PPC subfc CA=1 means borrow did NOT occur = ARM carry
            case SUB:case CMP:case SBC:setNZ(ctx,res);setC_xer(ctx);setV_sub(ctx,res,opA,opB);break;
            // FIX: RSB/RSC: a=op2(TA=opB), b=rn(srcRn=opA) — reversed
            case RSB:case RSC:         setNZ(ctx,res);setC_xer(ctx);setV_sub(ctx,res,opB,opA);break;
            default:setNZ(ctx,res);if(cset)setC_imm(ctx,TC);break;
        }
    }
    patchSkip(ctx,si);return true;
}

// FIX: emitBX_target - correctly set/clear THUMB bit (bit 5) of CPSR
static void emitBX_target(Ctx& ctx){
    ctx.E(ppc_lwz(TA,FRAME_SCR0,1));
    // New PC = target & ~1 (clear thumb bit from address)
    ctx.E(ppc_rlwinm(TB,TA,0,0,30));  // TB = TA & ~1
    ctx.E(ppc_stw(TB,FRAME_PC,1));
    // New THUMB = target bit 0
    ctx.E(ppc_rlwinm(TC,TA,0,31,31)); // TC = bit 0 of target
    ctx.E(ppc_rlwinm(TC,TC,27,5,5));  // TC = bit 0 << 27 (to bit 5 position? No...)
    // ARM CPSR THUMB bit is bit 5. TA bit 0 -> CPSR bit 5 = shift left by 5
    // rlwinm(TC, TA, 5, 26, 26): rotate left 5, mask bit 26? No.
    // Bit 5 of a 32-bit word is position 5 from lsb = bit 26 from msb.
    // rlwinm dst,src,sh,mb,me extracts rotated bits.
    // To put bit 0 of TA into bit 5: shift left 5.
    // rlwinm(TC, TA, 5, 26, 26) should work? Let's verify:
    // rotate TA left by 5: bit 0 goes to bit 5. Then mask mb=26,me=26
    // In PPC rlwinm: mb and me are from MSB=0. bit 5 from LSB = bit 26 from MSB. Correct.
    // Re-emit TC properly:
    // (already emitted wrong TC above, need to redo)
    // Actually let's just do it cleanly with two instructions:
    ctx.E(ppc_rlwinm(TC,TA,0,31,31));   // TC = bit 0 of target (in bit 31 = MSB position... no)
    // rlwinm(TC,TA,0,31,31): rotate 0, mask bit 31 (MSB) to bit 31 (MSB). Gets MSB of TA.
    // We want bit 0. bit 0 is at position 31 from MSB in PPC notation.
    // mb=31,me=31 with sh=0 extracts bit 0. Yes, bit 31 in PPC = bit 0 in ARM = LSB. Correct.
    ctx.E(ppc_rlwinm(TC,TC,27,5,5));    // TC = (bit0 << 27) but masked to bit 5
    // rlwinm(TC,TC,27,5,5): TC has value in bit 31 (LSB). Rotate left 27: bit 31 -> position 31-27=4? 
    // Actually on PPC rotation is modular. rlwinm rotates the 32-bit word left.
    // If TC = 0x00000001 (bit 0 set), rotate left 27 -> 0x08000000 = bit 27 from MSB? 
    // No: rotate left 27 means bit 31 (MSB) goes to bit 4. bit 0 (LSB, position 31 from MSB) 
    // goes to position 31-27=4 from MSB = bit 4 from LSB.
    // We want bit 5 from LSB. So we need rotate left by 5 (not 27).
    // rlwinm(TC, TA, 5, 26, 26): rotate TA left 5, keep only bit 26 from MSB = bit 5 from LSB.
    // Let me redo this whole thing cleanly:
    ctx.E(ppc_addi(1,1,0)); // NOP to "cancel" - actually we need to back up
    // The above two TC lines are wrong. Let me just emit correct sequence:
    // Clear CPSR bit 5, then OR in (target & 1) << 5
    ctx.li(TA,(uint32_t)~(1u<<5));
    ctx.E(ppc_lwz(TB,FRAME_SCR0,1)); // reload target
    ctx.E(ppc_and(RCPSR,RCPSR,TA));  // clear THUMB bit
    ctx.E(ppc_rlwinm(TC,TB,5,26,26));// TC = (target & 1) << 5, in bit 5 position
    ctx.E(ppc_or(RCPSR,RCPSR,TC));   // set THUMB bit from target bit 0
    emitCommitExitDyn(ctx,EXIT_NORMAL);
}

static bool emitBX(Ctx& ctx,uint32_t op,uint32_t curPC){
    uint8_t cond=(op>>28)&0xF,rm=op&0xF;
    if(rm==15||cond==15)return false;
    size_t si=emitCondSkip(ctx,cond);
    ctx.E(ppc_stw(RA[rm],FRAME_SCR0,1));
    emitBX_target(ctx);
    if(si!=SIZE_MAX){patchSkip(ctx,si);emitCommitExit(ctx,curPC+4,EXIT_NORMAL);}
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
    size_t si=emitCondSkip(ctx,cond);
    if(lk)ctx.li(RA[14],curPC+4);
    emitCommitExit(ctx,tgt,EXIT_NORMAL);
    if(si!=SIZE_MAX){patchSkip(ctx,si);emitCommitExit(ctx,curPC+4,EXIT_NORMAL);}
    ctx.done=true;return true;
}
static bool emitLS(Ctx& ctx,uint32_t op,uint32_t curPC){
    uint8_t cond=(op>>28)&0xF;
    if(cond==15)return false;
    bool ld=(op>>20)&1,by=(op>>22)&1,up=(op>>23)&1,pre=(op>>24)&1,wb=(op>>21)&1,immO=!((op>>25)&1);
    uint8_t rn=(op>>16)&0xF,rd=(op>>12)&0xF;
    if(rd==15)return false;
    if(rn==15&&(!immO||!pre||wb))return false;
    if(!immO&&((op&0xF)==15||((op>>4)&1)))return false;
    size_t si=emitCondSkip(ctx,cond);
    if(immO){ctx.li(TA,op&0xFFF);}
    else{
        uint8_t rm=op&0xF,sh=(op>>5)&3;int sa=(op>>7)&0x1F;
        if(sh==0)sLslI(ctx,TA,RA[rm],sa,false);
        else if(sh==1)sLsrI(ctx,TA,RA[rm],sa?sa:32,false);
        else if(sh==2)sAsrI(ctx,TA,RA[rm],sa?sa:32,false);
        else sRorI(ctx,TA,RA[rm],sa,false);
    }
    if(rn==15){
        uint32_t base=ctx.thumb?(curPC+4):(curPC+8);
        if(!by)base&=~3u;
        ctx.li(TB,up?base+(op&0xFFF):base-(op&0xFFF));
    }else{
        if(pre){if(up)ctx.E(ppc_add(TB,RA[rn],TA));else ctx.E(ppc_subf(TB,TA,RA[rn]));}
        else ctx.E(ppc_mr(TB,RA[rn]));
    }
    ctx.E(ppc_stw(TA,FRAME_SCR0,1));ctx.E(ppc_stw(TB,FRAME_SCR1,1));
    ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));ctx.E(ppc_lwz(TC,FRAME_SCR1,1));
    if(!ld)ctx.E(ppc_mr(TD,RA[rd]));
    ctx.call(ld?(by?(void*)JitHelp_r8:(void*)JitHelp_r32):(by?(void*)JitHelp_w8:(void*)JitHelp_w32));
    if(ld)ctx.E(ppc_mr(RA[rd],TA));
    if(rn!=15){
        ctx.E(ppc_lwz(TA,FRAME_SCR0,1));
        if(!pre){if(up)ctx.E(ppc_add(RA[rn],RA[rn],TA));else ctx.E(ppc_subf(RA[rn],TA,RA[rn]));}
        else if(wb&&rn!=rd)ctx.E(ppc_lwz(RA[rn],FRAME_SCR1,1));
    }
    patchSkip(ctx,si);return true;
}
static bool emitLSExtra(Ctx& ctx,uint32_t op,uint32_t){
    if((op&0x0E000090)!=0x00000090)return false;
    if(((op>>25)&7)!=0)return false;
    uint8_t cond=(op>>28)&0xF;if(cond==15)return false;
    bool p=(op>>24)&1,u=(op>>23)&1,w=(op>>21)&1,l=(op>>20)&1,imm=(op>>22)&1;
    uint8_t rn=(op>>16)&0xF,rd=(op>>12)&0xF,sh=(op>>5)&3;
    if(rd==15||rn==15||sh==0)return false;
    size_t si=emitCondSkip(ctx,cond);
    if(imm){ctx.li(TA,((op>>4)&0xF0)|(op&0xF));}
    else{if((op&0xF)==15){patchSkip(ctx,si);return false;}ctx.E(ppc_mr(TA,RA[op&0xF]));}
    if(p){if(u)ctx.E(ppc_add(TB,RA[rn],TA));else ctx.E(ppc_subf(TB,TA,RA[rn]));}
    else ctx.E(ppc_mr(TB,RA[rn]));
    ctx.E(ppc_stw(TA,FRAME_SCR0,1));ctx.E(ppc_stw(TB,FRAME_SCR1,1));
    ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));ctx.E(ppc_lwz(TC,FRAME_SCR1,1));
    if(!l){if(sh!=1){patchSkip(ctx,si);return false;}ctx.E(ppc_mr(TD,RA[rd]));ctx.call((void*)JitHelp_w16);}
    else{
        if(sh==1)ctx.call((void*)JitHelp_r16);
        else if(sh==2)ctx.call((void*)JitHelp_r8);
        else ctx.call((void*)JitHelp_r16);
        if(sh==2)ctx.E(ppc_extsb(RA[rd],TA));
        else if(sh==3)ctx.E(ppc_extsh(RA[rd],TA));
        else ctx.E(ppc_mr(RA[rd],TA));
    }
    ctx.E(ppc_lwz(TA,FRAME_SCR0,1));
    if(!p){if(u)ctx.E(ppc_add(RA[rn],RA[rn],TA));else ctx.E(ppc_subf(RA[rn],TA,RA[rn]));}
    else if(w&&rn!=rd)ctx.E(ppc_lwz(RA[rn],FRAME_SCR1,1));
    patchSkip(ctx,si);return true;
}
static bool emitMul(Ctx& ctx,uint32_t op){
    uint8_t cond=(op>>28)&0xF;if(cond==15)return false;
    bool s=(op>>20)&1,acc=(op>>21)&1,lng=(op>>23)&1;
    uint8_t rd=(op>>16)&0xF,rn=(op>>12)&0xF,rs=(op>>8)&0xF,rm=op&0xF;
    if(lng||rd==15||rm==15||rs==15||(acc&&rn==15))return false;
    size_t si=emitCondSkip(ctx,cond);
    if(acc){ctx.E(ppc_mullw(TA,RA[rm],RA[rs]));ctx.E(ppc_add(RA[rd],TA,RA[rn]));}
    else ctx.E(ppc_mullw(RA[rd],RA[rm],RA[rs]));
    if(s)setNZ(ctx,RA[rd]);
    patchSkip(ctx,si);return true;
}

static bool emitMrsMsr(Ctx& ctx,uint32_t op,uint32_t){
    uint8_t cond=(op>>28)&0xF;if(cond==15)return false;
    if((op&0x0FBF0FFF)==0x010F0000){
        uint8_t rd=(op>>12)&0xF;if(rd==15)return false;
        size_t si=emitCondSkip(ctx,cond);
        ctx.E(ppc_mr(RA[rd],RCPSR));
        patchSkip(ctx,si);
        return true;
    }
    if((op&0x0FBF0FFF)==0x014F0000) return false;
    uint8_t mask=(op>>16)&0xF;
    if(mask & 0x7) return false;
    if((op&0x0DB0F000)==0x0320F000){
        uint32_t imm=op&0xFF,rot=((op>>8)&0xF)*2;
        if(rot)imm=(imm>>rot)|(imm<<(32-rot));
        imm&=0xFF000000u;
        size_t si=emitCondSkip(ctx,cond);
        ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,8,31));
        ctx.li(TA,imm);
        ctx.E(ppc_or(RCPSR,RCPSR,TA));
        patchSkip(ctx,si);
        return true;
    }
    if((op&0x0DB0FFF0)==0x0120F000){
        uint8_t rm=op&0xF;if(rm==15)return false;
        size_t si=emitCondSkip(ctx,cond);
        ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,8,31));
        ctx.E(ppc_rlwinm(TA,RA[rm],0,0,7));
        ctx.E(ppc_or(RCPSR,RCPSR,TA));
        patchSkip(ctx,si);
        return true;
    }
    return false;
}

// FIX: Split block xfer emit into load/store paths to avoid >8 arg call
static bool emitBlockXfer(Ctx& ctx,uint32_t op,uint32_t curPC){
    uint8_t cond=(op>>28)&0xF;
    if(cond==15||((op>>22)&1))return false;
    uint8_t rn=(op>>16)&0xF;uint16_t list=(uint16_t)(op&0xFFFF);
    if(rn>14||!list)return false;
    bool load=(op>>20)&1;
    bool loadPC=load&&(list&0x8000);
    size_t si=emitCondSkip(ctx,cond);
    emitSpill(ctx);

    if(load){
        // JitHelp_armBlockLoad(core, arm7, op, regs, pcOut, cpsrInOut) — 6 args
        ctx.ldCore();                                    // r3=core
        ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));             // r4=arm7
        ctx.li(TC,op);                                   // r5=op
        ctx.E(ppc_addi(TD,1,(int16_t)FRAME_REGSYNC));   // r6=regs
        ctx.li(TE,curPC+4);
        ctx.E(ppc_stw(TE,FRAME_PC,1));
        ctx.E(ppc_addi(TE,1,(int16_t)FRAME_PC));        // r7=pcOut
        ctx.E(ppc_addi(TF,1,(int16_t)FRAME_CPSR));      // r8=cpsrInOut
        ctx.call((void*)JitHelp_armBlockLoad);
        ctx.E(ppc_stw(TA,FRAME_SCR0,1));                // save return value
        emitReload(ctx);
        // Check for error (return < 0)
        ctx.E(ppc_lwz(TA,FRAME_SCR0,1));
        ctx.E(ppc_cmpi(0,TA,0));
        size_t bOk=ctx.sz(); ctx.E(ppc_bc(4,0,0)); // branch if < 0 (error)
        // No error
        if(loadPC){
            ctx.E(ppc_lwz(TA,FRAME_SCR0,1));
            ctx.E(ppc_cmpi(0,TA,1));
            size_t b=ctx.sz(); ctx.E(ppc_bc(12,2,0)); // branch if wrotePC==1
            // Did not write PC: continue
            emitCommitExit(ctx,curPC+4,EXIT_NORMAL);
            {int32_t d=(int32_t)((ctx.sz()-b)*4);ctx.base[b]=ppc_bc(12,2,(int16_t)d);}
            // Wrote PC: dynamic exit
            emitCommitExitDyn(ctx,EXIT_NORMAL);
            {int32_t d=(int32_t)((ctx.sz()-bOk)*4);ctx.base[bOk]=ppc_bc(4,0,(int16_t)d);}
            if(si!=SIZE_MAX){patchSkip(ctx,si);emitCommitExit(ctx,curPC+4,EXIT_NORMAL);}
            ctx.done=true;
        } else {
            // Normal load without PC
            {int32_t d=(int32_t)((ctx.sz()-bOk)*4);ctx.base[bOk]=ppc_bc(4,0,(int16_t)d);}
            patchSkip(ctx,si);
        }
    } else {
        // Store: JitHelp_armBlockStore(core, arm7, op, regs, pcForR15) — 5 args
        ctx.ldCore();                                    // r3=core
        ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));             // r4=arm7
        ctx.li(TC,op);                                   // r5=op
        ctx.E(ppc_addi(TD,1,(int16_t)FRAME_REGSYNC));   // r6=regs
        ctx.li(TE,curPC+8);                              // r7=pcForR15
        ctx.call((void*)JitHelp_armBlockStore);
        emitReload(ctx);
        patchSkip(ctx,si);
    }
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
    uint8_t ty=(op>>11)&3,rd=op&7,rs=(op>>3)&7;int i=(op>>6)&0x1F;
    switch(ty){case 0:sLslI(ctx,RA[rd],RA[rs],i,true);break;case 1:sLsrI(ctx,RA[rd],RA[rs],i?i:32,true);break;case 2:sAsrI(ctx,RA[rd],RA[rs],i?i:32,true);break;default:return false;}
    setNZ(ctx,RA[rd]);setC_imm(ctx,TC);return true;
}
static bool emitT_addSub3(Ctx& ctx,uint16_t op){
    uint8_t rd=op&7,rs=(op>>3)&7;bool sub=(op>>9)&1,imm3=(op>>10)&1;
    if(imm3)ctx.li(TA,(op>>6)&7);else ctx.E(ppc_mr(TA,RA[(op>>6)&7]));
    ctx.E(ppc_mr(TB,RA[rs]));
    if(sub){ctx.E(ppc_subfc(RA[rd],TA,TB));setNZ(ctx,RA[rd]);setC_xer(ctx);setV_sub(ctx,RA[rd],TB,TA);}
    else{ctx.E(ppc_addc(RA[rd],TB,TA));setNZ(ctx,RA[rd]);setC_xer(ctx);setV_add(ctx,RA[rd],TB,TA);}
    return true;
}
static bool emitT_imm8(Ctx& ctx,uint16_t op){
    uint8_t ty=(op>>11)&3,rd=(op>>8)&7;uint32_t imm=op&0xFF;uint8_t p=RA[rd];
    switch(ty){
        case 0:ctx.li(p,imm);setNZ(ctx,p);return true;
        case 1:ctx.li(TA,imm);ctx.E(ppc_mr(TB,p));ctx.E(ppc_subfc(TC,TA,TB));setNZ(ctx,TC);setC_xer(ctx);setV_sub(ctx,TC,TB,TA);return true;
        case 2:ctx.li(TA,imm);ctx.E(ppc_mr(TB,p));ctx.E(ppc_addc(p,TB,TA));setNZ(ctx,p);setC_xer(ctx);setV_add(ctx,p,TB,TA);return true;
        case 3:ctx.li(TA,imm);ctx.E(ppc_mr(TB,p));ctx.E(ppc_subfc(p,TA,TB));setNZ(ctx,p);setC_xer(ctx);setV_sub(ctx,p,TB,TA);return true;
    }
    return false;
}
static bool emitT_alu(Ctx& ctx,uint16_t op){
    uint8_t rd=op&7,rs=(op>>3)&7,o=(op>>6)&0xF;uint8_t d=RA[rd],s=RA[rs];
    switch(o){
        case 0:ctx.E(ppc_and(d,d,s));setNZ(ctx,d);break;
        case 1:ctx.E(ppc_xor(d,d,s));setNZ(ctx,d);break;
        case 2:ctx.E(ppc_slw(d,d,s));setNZ(ctx,d);break;
        case 3:ctx.E(ppc_srw(d,d,s));setNZ(ctx,d);break;
        case 4:ctx.E(ppc_sraw(d,d,s));setNZ(ctx,d);break;
        case 5:primeCarry(ctx);ctx.E(ppc_mr(TB,d));ctx.E(ppc_adde(d,TB,s));setNZ(ctx,d);setC_xer(ctx);setV_add(ctx,d,TB,s);break;
        case 6:primeCarry(ctx);ctx.E(ppc_mr(TB,d));ctx.E(ppc_subfe(d,s,TB));setNZ(ctx,d);setC_xer(ctx);setV_sub(ctx,d,TB,s);break;
        case 7:ctx.E(ppc_rlwinm(TA,s,0,24,31));ctx.E(ppc_subfic(TB,TA,32));ctx.E(ppc_rlwnm(d,d,TB,0,31));setNZ(ctx,d);break;
        case 8:ctx.E(ppc_and(TA,d,s));setNZ(ctx,TA);break;
        case 9:ctx.E(ppc_addi(TA,0,0));ctx.E(ppc_subfc(d,s,TA));setNZ(ctx,d);setC_xer(ctx);setV_sub(ctx,d,TA,s);break;
        case 10:ctx.E(ppc_mr(TB,d));ctx.E(ppc_subfc(TA,s,TB));setNZ(ctx,TA);setC_xer(ctx);setV_sub(ctx,TA,TB,s);break;
        case 11:ctx.E(ppc_mr(TB,d));ctx.E(ppc_addc(TA,TB,s));setNZ(ctx,TA);setC_xer(ctx);setV_add(ctx,TA,TB,s);break;
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
    if(o==3){
        // BX/BLX reg
        if(rs==15) ctx.li(TA,(curPC+4)&~1u);
        else ctx.E(ppc_mr(TA,RA[rs]));
        ctx.E(ppc_stw(TA,FRAME_SCR0,1));
        emitBX_target(ctx);
        ctx.done=true;return true;
    }
    if(rd==15){
        if(o==1)return false; // CMP to PC makes no sense here
        if(rs==15)ctx.li(TA,curPC+4);else ctx.E(ppc_mr(TA,RA[rs]));
        if(o==0){ctx.li(TB,curPC+4);ctx.E(ppc_add(TA,TB,TA));}
        ctx.E(ppc_stw(TA,FRAME_SCR0,1));
        emitBX_target(ctx);
        ctx.done=true;return true;
    }
    if(rs==15)ctx.li(TA,curPC+4);else ctx.E(ppc_mr(TA,RA[rs]));
    switch(o){
        case 0:ctx.E(ppc_add(RA[rd],RA[rd],TA));break;
        case 1:ctx.E(ppc_mr(TB,RA[rd]));ctx.E(ppc_subfc(TC,TA,TB));setNZ(ctx,TC);setC_xer(ctx);setV_sub(ctx,TC,TB,TA);break;
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
    switch(k){case 0:fn=(void*)JitHelp_w32;ld=false;break;case 1:fn=(void*)JitHelp_w16;ld=false;break;case 2:fn=(void*)JitHelp_w8;ld=false;break;case 3:fn=(void*)JitHelp_r8;sxb=true;break;case 4:fn=(void*)JitHelp_r32;break;case 5:fn=(void*)JitHelp_r16;break;case 6:fn=(void*)JitHelp_r8;break;case 7:fn=(void*)JitHelp_r16;sxh=true;break;default:return false;}
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
    ctx.li(TC,off);ctx.E(ppc_add(TC,RA[rb],TC));ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if(!ld)ctx.E(ppc_mr(TD,RA[rd]));
    void* fn=ld?(hw?(void*)JitHelp_r16:by?(void*)JitHelp_r8:(void*)JitHelp_r32):(hw?(void*)JitHelp_w16:by?(void*)JitHelp_w8:(void*)JitHelp_w32);
    ctx.call(fn);if(ld)ctx.E(ppc_mr(RA[rd],TA));return true;
}
static bool emitT_spLoad(Ctx& ctx,uint16_t op,uint32_t curPC){
    bool ld=(op>>11)&1;uint8_t rd=(op>>8)&7;bool sp=(((op>>12)&0xF)==0x9);
    uint32_t off=(uint32_t)(op&0xFF)<<2;
    if(sp){ctx.li(TA,off);ctx.E(ppc_add(TC,RA[13],TA));}else ctx.li(TC,((curPC+4)&~3u)+off);
    ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if(!ld)ctx.E(ppc_mr(TD,RA[rd]));ctx.call(ld?(void*)JitHelp_r32:(void*)JitHelp_w32);
    if(ld)ctx.E(ppc_mr(RA[rd],TA));return true;
}
static bool emitT_addSpPc(Ctx& ctx,uint16_t op,uint32_t curPC){
    uint8_t h=(op>>12)&0xF;
    if(h==0xA){uint8_t rd=(op>>8)&7;bool sp=(op>>11)&1;uint32_t imm=(uint32_t)(op&0xFF)<<2;
        if(sp){ctx.li(TA,imm);ctx.E(ppc_add(RA[rd],RA[13],TA));}else ctx.li(RA[rd],((curPC+4)&~3u)+imm);return true;}
    if(h==0xB){uint8_t s=(op>>8)&0xF;
        if(s==0){ctx.li(TA,(uint32_t)(op&0x7F)<<2);ctx.E(ppc_add(RA[13],RA[13],TA));return true;}
        if(s==1){ctx.li(TA,(uint32_t)(op&0x7F)<<2);ctx.E(ppc_subf(RA[13],TA,RA[13]));return true;}}
    return false;
}
static bool emitT_pushPop(Ctx& ctx,uint16_t op,uint32_t curPC){
    uint8_t opA=(op>>9)&7;if(opA!=2&&opA!=6)return false;
    emitSpill(ctx);ctx.li(TA,curPC+2);ctx.E(ppc_stw(TA,FRAME_PC,1));
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
        size_t si=emitCondSkip(ctx,cond);if(si==SIZE_MAX)return false;
        emitCommitExit(ctx,(uint32_t)(curPC+4+off),EXIT_NORMAL);
        patchSkip(ctx,si);emitCommitExit(ctx,curPC+2,EXIT_NORMAL);
        ctx.done=true;return true;
    }
    return false;
}
static bool emitT_bl(Ctx& ctx,uint16_t op1,uint16_t op2,uint32_t curPC){
    int32_t hi=(int32_t)((op1&0x7FF)<<21)>>9,lo=(op2&0x7FF)<<1;
    uint32_t tgt=(uint32_t)(curPC+4+hi+lo);
    bool blx=((op2>>11)&0x1F)==0x1C;
    ctx.li(RA[14],(curPC+4)|1u);
    if(blx){
        tgt&=~3u;
        // Clear THUMB bit in CPSR
        ctx.li(TA,~(1u<<5));
        ctx.E(ppc_and(RCPSR,RCPSR,TA));
    }
    emitCommitExit(ctx,tgt&~1u,EXIT_NORMAL);ctx.done=true;return true;
}
static bool dispThumb(Ctx& ctx,uint16_t op,uint32_t curPC){
    uint8_t h=(op>>12)&0xF;
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
        return (pc < 0x00004000u) ||
               (pc >= 0x02000000u && pc < 0x02040000u) ||
               (pc >= 0x03000000u && pc < 0x03008000u) ||
               (pc >= 0x03FF8000u && pc < 0x04000000u) || // GBA BIOS mirror
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
static JitBlock* compile(Interpreter* interp,Core* core,
                          uint32_t armPC,bool arm7,int cpuIdx){
    if(!codeBuf||!g_jitLive||!interp||!core)return nullptr;
    if(!interp->isReady())return nullptr;
    if(!validPC(armPC,core->gbaMode))return nullptr;
    bool thumb=interp->isThumb();
    size_t bkt=hashPC(armPC);
    {
        JitBlock& s=cache[bkt];
        if(s.valid&&s.armPC==armPC&&s.thumb==thumb&&s.gen==cacheGen&&s.nW>=16)
            return &s;
        s.valid=false;
    }
    if(codePos+BLK_WDS>=JIT_WORDS)flushJitCache();
    Ctx ctx;memset(&ctx,0,sizeof(ctx));
    ctx.base=codeBuf+codePos;ctx.cur=ctx.base;
    ctx.cap=JIT_WORDS-codePos;if(ctx.cap>BLK_WDS)ctx.cap=BLK_WDS;
    ctx.thumb=thumb;ctx.arm7=arm7;ctx.blockPC=armPC;
    ctx.cpuIdx=cpuIdx;ctx.interp=interp;ctx.core=core;ctx.insnCount=0;
    emitPrologue(ctx);emitSyncFrom(ctx);
    uint32_t curPC=armPC;int n=0;
    while(n<(int)BLK_ARMS&&!ctx.done&&!ctx.overflow){
        if(ctx.rem()<200){emitCommitExit(ctx,curPC,EXIT_NORMAL);ctx.done=true;break;}
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
                if(((op>>25)&7)==5)ctx.done=true;
                if((op&0x0FFFFFF0)==0x012FFF10)ctx.done=true;
                if((op&0x0E000000)==0x0A000000)ctx.done=true;
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
    JitBlock& slot=cache[bkt];
    slot.armPC=armPC;slot.code=ctx.base;slot.nW=(uint32_t)wds;
    slot.gen=cacheGen;slot.thumb=thumb;slot.insnCount=(uint32_t)ctx.insnCount;slot.valid=true;
    codePos+=wds;return &slot;
}

// ═══════════════════════════════════════════════════════════════════════
// Scheduler tick
// ═══════════════════════════════════════════════════════════════════════
static inline void tickInline(Core& core,uint32_t cycles){
    core.globalCycles+=cycles;
    while(!core.events.empty()&&
          core.globalCycles>=core.events.front().cycles){
        SchedEvent e=core.events.front();
        core.events.erase(core.events.begin());
        if(e.task>=0&&e.task<MAX_TASKS&&core.tasks[e.task].fn)
            core.tasks[e.task]();
    }
}

// ═══════════════════════════════════════════════════════════════════════
// stepJit
// ═══════════════════════════════════════════════════════════════════════
static uint32_t stepJit(Core& core, int cpu, bool gba) {
    Interpreter& interp = core.interpreter[cpu];
    if (interp.halted) return 0;

    const bool     arm7       = (cpu == 1) || gba;
    const uint32_t cycPerInsn = arm7 ? CYCLES_PER_INSN_ARM7 : CYCLES_PER_INSN_ARM9;

    if (!interp.isReady()) {
        interp.jitRunOpcode();
        return cycPerInsn;
    }

    uint32_t pc = interp.getActualPC();

    if (pc == 0xFFFFFFFFu || !validPC(pc, gba)) {
        interp.jitRunOpcode();
        return cycPerInsn;
    }

    g_exitReason[cpu] = EXIT_FALLBACK;
    g_exitPC[cpu]     = pc;

    JitBlock* b = compile(&interp, &core, pc, arm7, cpu);
    if (!b || !b->code || b->nW < 16 ||
        b->code < codeBuf || b->code + b->nW > codeBuf + JIT_WORDS) {
        interp.jitRunOpcode();
        g_exitPC[cpu] = interp.isReady() ? interp.getActualPC() : pc;
        g_exitReason[cpu] = EXIT_FALLBACK;
        return cycPerInsn;
    }

    executeBlock_asm(b->code);
    g_totalJIT[cpu]++;

    if (g_exitReason[cpu] == EXIT_FALLBACK) {
        // Run interpreter to advance past the fallback instruction
        interp.jitRunOpcode();
        // Update exitPC to reflect where interpreter left us
        if (interp.isReady())
            g_exitPC[cpu] = interp.getActualPC();
    }

    uint32_t n = (g_exitReason[cpu] == EXIT_NORMAL && b->insnCount > 0)
                 ? b->insnCount : 1u;
    return n * cycPerInsn;
}

// ═══════════════════════════════════════════════════════════════════════
// Status log
// ═══════════════════════════════════════════════════════════════════════
static uint32_t g_statusTick = 0;
static void logStatus(Core& core) {
    g_statusTick++;
    if ((g_statusTick & 0x3FF) != 0) return;
    DebugLog("[JIT] STATUS jit0=%u fb0=%u jit1=%u fb1=%u pos=%zu gen=%u\n",
             g_totalJIT[0], g_totalFB[0], g_totalJIT[1], g_totalFB[1], codePos, cacheGen);
    DebugLog("[JIT] STATUS pc0=%08X pc1=%08X h0=%d h1=%d fps=%d cyc=%u\n",
             core.interpreter[0].isReady() ? core.interpreter[0].getActualPC() : 0u,
             core.interpreter[1].isReady() ? core.interpreter[1].getActualPC() : 0u,
             (int)core.interpreter[0].halted,
             (int)core.interpreter[1].halted,
             core.fps, core.globalCycles);
}

// ═══════════════════════════════════════════════════════════════════════
// NDS run
// ═══════════════════════════════════════════════════════════════════════
void runJitNds(Core& core) {
    if (!g_jitLive || !codeBuf) { Interpreter::runCoreNds(core); return; }

    uint32_t masterCycles = 0;

    for (int i = 0; i < ITERS_NDS; i++) {

        // ── ARM9 step ──────────────────────────────────────────────
        bool arm9Halted = (core.interpreter[0].halted != 0);
        uint32_t pc0 = 0;

        if (!arm9Halted && core.interpreter[0].isReady())
            pc0 = core.interpreter[0].getActualPC();

        uint32_t c0 = arm9Halted ? 0 : stepJit(core, 0, false);
        masterCycles += arm9Halted ? CYCLES_PER_INSN_ARM9
                                   : (c0 > 0 ? c0 : CYCLES_PER_INSN_ARM9);

        // FIX: Check CPU0's own PC for spin (exitPC vs entryPC)
        bool cpu0Spin = false;
        if (!arm9Halted && c0 > 0 && pc0 != 0u && pc0 != 0xFFFFFFFFu) {
            uint32_t newPC0 = g_exitPC[0];
            if (newPC0 != 0u && newPC0 != 0xFFFFFFFFu)
                cpu0Spin = spinUpdate(0, pc0, newPC0);
            // Don't reset on fallback; fallbacks advance PC via interpreter
        }

        // ── ARM7 steps (2 per ARM9 step) ──────────────────────────
        bool cpu1SpinThisIter = false;

        for (int j = 0; j < 2; j++) {
            if (core.interpreter[1].halted) break;

            uint32_t pc1 = core.interpreter[1].isReady()
                           ? core.interpreter[1].getActualPC() : 0u;

            uint32_t c1 = stepJit(core, 1, false);

            bool cpu1Spin = false;
            if (c1 > 0 && pc1 != 0u && pc1 != 0xFFFFFFFFu) {
                uint32_t newPC1 = g_exitPC[1];
                if (newPC1 != 0u && newPC1 != 0xFFFFFFFFu)
                    cpu1Spin = spinUpdate(1, pc1, newPC1);
            }

            if (cpu1Spin) {
                cpu1SpinThisIter = true;

                // ── Mutual deadlock: both spinning ─────────────────
                if (cpu0Spin) {
                    SpinState& s0 = g_spin[0];
                    SpinState& s1 = g_spin[1];

                    if (!s0.logged) {
                        s0.logged = s1.logged = true;
                        DebugLog("[JIT] DEADLOCK cpu0=%08X cpu1=%08X — interleaving\n",
                                 s0.spinPC, s1.spinPC);
                    }

                    s0.giveupCount++;
                    s1.giveupCount++;

                    if (s0.giveupCount >= SPIN_GIVEUP) {
                        DebugLog("[JIT] DEADLOCK giveup, resetting spin state\n");
                        spinReset(0); spinReset(1);
                        cpu0Spin = false; cpu1Spin = false; cpu1SpinThisIter = false;
                        break;
                    }

                    // Interleave interpreter steps for both CPUs
                    uint32_t dpc0 = s0.spinPC, dpc1 = s1.spinPC;
                    bool escaped0 = false, escaped1 = false;
                    for (int k = 0; k < DEADLOCK_STEPS; k++) {
                        // ARM9
                        if (!core.interpreter[0].halted) {
                            core.interpreter[0].jitRunOpcode();
                            masterCycles += CYCLES_PER_INSN_ARM9;
                            if (core.interpreter[0].isReady()) {
                                uint32_t npc0 = core.interpreter[0].getActualPC();
                                if (npc0 != dpc0 && npc0 != 0u && npc0 != 0xFFFFFFFFu) {
                                    DebugLog("[JIT] DEADLOCK cpu0 escaped %08X->%08X at k=%d\n",
                                             dpc0, npc0, k);
                                    spinReset(0); cpu0Spin = false; escaped0 = true;
                                }
                            }
                        }
                        // ARM7
                        if (!core.interpreter[1].halted) {
                            core.interpreter[1].jitRunOpcode();
                            if (core.interpreter[1].isReady()) {
                                uint32_t npc1 = core.interpreter[1].getActualPC();
                                if (npc1 != dpc1 && npc1 != 0u && npc1 != 0xFFFFFFFFu) {
                                    DebugLog("[JIT] DEADLOCK cpu1 escaped %08X->%08X at k=%d\n",
                                             dpc1, npc1, k);
                                    spinReset(1); cpu1Spin = false; cpu1SpinThisIter = false;
                                    escaped1 = true;
                                }
                            }
                        }
                        if (escaped0 && escaped1) break;
                    }
                    break; // done with ARM7 j-loop for this ARM9 step
                }

                // ── ARM7 spinning alone: run ARM9 interpreter steps ─
                SpinState& s1 = g_spin[1];
                s1.giveupCount++;

                if (s1.giveupCount >= SPIN_GIVEUP) {
                    DebugLog("[JIT] SPIN cpu1=%08X giveup=%u, yielding\n",
                             s1.spinPC, s1.giveupCount);
                    spinReset(1);
                    cpu1SpinThisIter = false;
                    break;
                }

                uint32_t spc1 = s1.spinPC;
                for (int k = 0; k < SPIN_STEPS; k++) {
                    if (core.interpreter[0].halted) break;
                    core.interpreter[0].jitRunOpcode();
                    masterCycles += CYCLES_PER_INSN_ARM9;
                    if (core.interpreter[1].isReady()) {
                        uint32_t npc1 = core.interpreter[1].getActualPC();
                        if (npc1 != spc1 && npc1 != 0u && npc1 != 0xFFFFFFFFu) {
                            DebugLog("[JIT] SPIN cpu1 escaped %08X->%08X after %d steps\n",
                                     spc1, npc1, k+1);
                            spinReset(1);
                            cpu1SpinThisIter = false;
                            break;
                        }
                    }
                }
            }
        } // end j loop

        // ── ARM9 spinning alone (ARM7 was not spinning) ────────────
        if (cpu0Spin && !cpu1SpinThisIter) {
            SpinState& s0 = g_spin[0];
            s0.giveupCount++;

            if (s0.giveupCount >= SPIN_GIVEUP) {
                DebugLog("[JIT] SPIN cpu0=%08X giveup=%u, yielding\n",
                         s0.spinPC, s0.giveupCount);
                spinReset(0);
                continue;
            }

            uint32_t spc0 = s0.spinPC;
            for (int k = 0; k < SPIN_STEPS; k++) {
                if (core.interpreter[1].halted) break;
                core.interpreter[1].jitRunOpcode();
                // FIX: Check CPU0's PC (not CPU1's) to see if CPU0 escaped its spin
                if (core.interpreter[0].isReady()) {
                    uint32_t npc0 = core.interpreter[0].getActualPC();
                    if (npc0 != spc0 && npc0 != 0u && npc0 != 0xFFFFFFFFu) {
                        DebugLog("[JIT] SPIN cpu0 escaped %08X->%08X after %d steps\n",
                                 spc0, npc0, k+1);
                        spinReset(0);
                        break;
                    }
                }
            }
        }
    }

    uint32_t charge = (masterCycles > FLOOR_CYCLES_NDS) ? masterCycles : FLOOR_CYCLES_NDS;
    tickInline(core, charge);
    logStatus(core);
}

// ═══════════════════════════════════════════════════════════════════════
// GBA run
// ═══════════════════════════════════════════════════════════════════════
void runJitGba(Core& core) {
    if (!g_jitLive || !codeBuf) { Interpreter::runCoreSingle<true, 0>(core); return; }

    uint32_t acc = 0;
    bool forceTick = false;

    for (int i = 0; i < ITERS_GBA; i++) {
        if (core.interpreter[1].halted) {
            acc += CYCLES_PER_INSN_ARM7;
            continue;
        }

        uint32_t pc1 = core.interpreter[1].isReady()
                       ? core.interpreter[1].getActualPC() : 0u;
        uint32_t c1  = stepJit(core, 1, true);
        uint32_t used = (c1 > 0) ? c1 : CYCLES_PER_INSN_ARM7;
        acc += used;

        if (pc1 != 0u && pc1 != 0xFFFFFFFFu) {
            uint32_t newPC1 = g_exitPC[1];
            if (newPC1 != 0u && spinUpdate(1, pc1, newPC1)) {
                SpinState& s1 = g_spin[1];
                s1.giveupCount++;
                uint32_t spc = s1.spinPC;

                bool escaped = false;
                for (int k = 0; k < SPIN_STEPS; k++) {
                    if (core.interpreter[1].halted) { escaped = true; break; }
                    core.interpreter[1].jitRunOpcode();
                    acc += CYCLES_PER_INSN_ARM7;
                    if (core.interpreter[1].isReady()) {
                        uint32_t npc = core.interpreter[1].getActualPC();
                        if (npc != spc && npc != 0u) {
                            DebugLog("[JIT] GBA SPIN escaped %08X->%08X\n", spc, npc);
                            spinReset(1);
                            escaped = true;
                            break;
                        }
                    }
                }
                if (!escaped) {
                    if (s1.giveupCount >= SPIN_GIVEUP) {
                        // Force scheduler tick to allow VBlank IRQ
                        uint32_t charge = (acc > FLOOR_CYCLES_GBA) ? acc : FLOOR_CYCLES_GBA;
                        tickInline(core, charge);
                        acc = 0;
                        forceTick = true;
                        spinReset(1);
                        break;
                    }
                    // Not yet giving up: just reset spin so we try fresh
                    spinReset(1);
                }
            }
        } else if (g_exitReason[1] == EXIT_FALLBACK) {
            spinReset(1);
        }
    }

    if (!forceTick) {
        uint32_t charge = (acc > FLOOR_CYCLES_GBA) ? acc : FLOOR_CYCLES_GBA;
        tickInline(core, charge);
    }
    logStatus(core);
}

// ═══════════════════════════════════════════════════════════════════════
// Init / shutdown
// ═══════════════════════════════════════════════════════════════════════
bool initJit(Core* core) {
    g_jitLive = false; codeBuf = nullptr;
    void* raw = memalign(32, JIT_BYTES);
    if (!raw) { printf("[JIT] memalign failed\n"); return false; }
    uintptr_t addr = (uintptr_t)raw;
    bool ok = (addr >= 0x80000000u && addr + JIT_BYTES <= 0x81800000u);
    if (!ok && addr < 0x01800000u) { addr |= 0x80000000u; ok = (addr + JIT_BYTES <= 0x81800000u); }
    else if (!ok && addr >= 0xC0000000u && addr < 0xC1800000u) { addr -= 0x40000000u; ok = (addr + JIT_BYTES <= 0x81800000u); }
    if (!ok) { printf("[JIT] not in MEM1: %p\n", raw); free(raw); return false; }
    uintptr_t tr = (uintptr_t)(void*)executeBlock_asm;
    if (tr < 0x80000000u || tr >= 0x81800000u) { printf("[JIT] bad trampoline %p\n", (void*)tr); free(raw); return false; }
    codeBuf = (uint32_t*)addr; codePos = 0; cacheGen = 0;
    g_fbLogCount = 0;
    g_totalFB[0] = g_totalFB[1] = 0;
    g_totalJIT[0] = g_totalJIT[1] = 0;
    g_statusTick = 0;
    g_spin[0] = SpinState{};
    g_spin[1] = SpinState{};
    for (size_t i = 0; i < CSIZ; i++) cache[i].valid = false;
    memset(g_exitPC,   0, sizeof g_exitPC);
    memset(g_exitCPSR, 0, sizeof g_exitCPSR);
    g_exitReason[0] = g_exitReason[1] = EXIT_FALLBACK;
    memset(codeBuf, 0, JIT_BYTES);
    DCFlushRange(codeBuf, JIT_BYTES);
    ICInvalidateRange(codeBuf, JIT_BYTES);
    g_jitLive = true;
    printf("[JIT] ready buf=%p (%zuKB) tramp=%p BLK_ARMS=%zu\n",
           (void*)codeBuf, JIT_BYTES >> 10, (void*)tr, BLK_ARMS);
    DebugLog("[JIT] init buf=%p tramp=%p BLK_ARMS=%zu\n",
             (void*)codeBuf, (void*)tr, BLK_ARMS);
    if (core) core->setRunFunc(core->gbaMode ? runJitGba : runJitNds);
    return true;
}

void shutdownJit(Core* core) {
    DebugLog("[JIT] shutdown fb0=%u fb1=%u jit0=%u jit1=%u\n",
             g_totalFB[0], g_totalFB[1], g_totalJIT[0], g_totalJIT[1]);
    g_jitLive = false;
    if (core) core->setRunFunc(core->gbaMode
        ? static_cast<void(*)(Core&)>(&Interpreter::runCoreSingle<true, 0>)
        : &Interpreter::runCoreNds);
    codeBuf = nullptr; codePos = 0;
    for (size_t i = 0; i < CSIZ; i++) cache[i].valid = false;
}

void invalidateJitRange(uint32_t start, uint32_t end) {
    for (size_t i = 0; i < CSIZ; i++)
        if (cache[i].valid && cache[i].armPC >= start && cache[i].armPC < end)
            cache[i].valid = false;
}

} // namespace JitPpc
