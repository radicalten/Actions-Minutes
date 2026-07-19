// jit_ppc.cpp — ARM->PPC JIT for Wii/Broadway  
// Fixed revision 4:
// 1. MSR: implement full control-field (mask=1,3,9,b,f) mode switches via fallback
//    The key fix: MSR with control bits must be handled — emit fallback so
//    interpreter does the mode switch with correct banked register swap.
// 2. GBA spin at VBlank wait: detect the single-instruction B . loop and
//    run the interpreter for a full scanline worth of cycles before resuming JIT.
// 3. NDS: ARM7 stuck waiting for IPCSYNC — the MSR mode switch fallback
//    was clobbering state; fix by always falling back MSR with control bits.
// 4. NDS cycle accounting: ARM7 cycles must pace correctly relative to ARM9.
// 5. GBA: when burst escapes and immediately re-enters, use longer burst window.

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
static const int ITERS_GBA = 32;

// Floor: one scanline worth of cycles per runJit* call.
// NDS: 355*6 = 2130 ARM9 cycles
// GBA: 308*4 = 1232 cycles
static const uint32_t FLOOR_CYCLES_NDS = 2130;
static const uint32_t FLOOR_CYCLES_GBA = 1232;

// Spin detection
static const uint32_t SPIN_THRESH   = 6;
static const int      SPIN_STEPS    = 256;  // increased for GBA VBlank wait
static const uint32_t SPIN_CYC_STEP = 4;

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

// ── Loop detector ─────────────────────────────────────────────────────
struct LoopState {
    uint32_t fbPC    = 0xFFFFFFFFu;
    uint32_t fbCount = 0;
    bool     fbDumped= false;

    uint32_t normHist[4]  = {0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu};
    uint32_t normCount    = 0;
    uint32_t normBurstPC  = 0xFFFFFFFFu;
    uint32_t normBurstExit= 0xFFFFFFFFu;
    bool     normBursting = false;
};
static LoopState s_loop[2];

static void resetNormSpin(LoopState& ls) {
    for(int j=0;j<4;j++) ls.normHist[j]=0xFFFFFFFFu;
    ls.normCount     = 0;
    ls.normBurstPC   = 0xFFFFFFFFu;
    ls.normBurstExit = 0xFFFFFFFFu;
    ls.normBursting  = false;
}

static bool normSpinDetect(LoopState& ls, uint32_t entryPC, uint32_t exitPC) {
    if(ls.normBursting) return false;

    ls.normHist[3] = ls.normHist[2];
    ls.normHist[2] = ls.normHist[1];
    ls.normHist[1] = ls.normHist[0];
    ls.normHist[0] = exitPC;

    if(exitPC == entryPC){
        ls.normCount++;
        if(ls.normCount >= SPIN_THRESH){
            ls.normBurstPC   = entryPC;
            ls.normBurstExit = exitPC;
            return true;
        }
        return false;
    }

    if(ls.normHist[0] != 0xFFFFFFFFu &&
       ls.normHist[1] != 0xFFFFFFFFu &&
       ls.normHist[2] != 0xFFFFFFFFu &&
       ls.normHist[3] != 0xFFFFFFFFu &&
       ls.normHist[0] == ls.normHist[2] &&
       ls.normHist[1] == ls.normHist[3] &&
       ls.normHist[0] != ls.normHist[1]){
        ls.normCount++;
        if(ls.normCount >= SPIN_THRESH){
            ls.normBurstPC   = ls.normHist[1];
            ls.normBurstExit = ls.normHist[0];
            return true;
        }
        return false;
    }

    ls.normCount = 0;
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

// CPSR written before setPC so flushPipeline sees correct thumb bit
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

void JitHelp_tick(Core* core,uint32_t cycles){
    if(!core)return;
    core->globalCycles+=cycles;
    while(!core->events.empty()&&
          core->globalCycles>=core->events.front().cycles){
        SchedEvent e=core->events.front();
        core->events.erase(core->events.begin());
        if(e.task>=0&&e.task<MAX_TASKS&&core->tasks[e.task].fn)
            core->tasks[e.task]();
    }
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
    ctx.E(ppc_bc(12,2,0));
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
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,2,31));
    ctx.E(ppc_rlwimi(RCPSR,r,0,0,0));
    ctx.E(ppc_cmpi(6,r,0));
    ctx.E(ppc_mfcr(TA));
    ctx.E(ppc_rlwinm(TA,TA,25,1,1));
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}
static void setC_xer(Ctx& ctx){
    ctx.E(ppc_mfxer(TA));
    ctx.E(ppc_rlwinm(TA,TA,0,29,29));
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,3,1));
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}
static void setV_add(Ctx& ctx,uint8_t res,uint8_t a,uint8_t b){
    ctx.E(ppc_xor(TE,res,a));ctx.E(ppc_xor(TF,res,b));ctx.E(ppc_and(TE,TE,TF));
    ctx.E(ppc_rlwinm(TE,TE,0,0,0));ctx.E(ppc_rlwinm(TE,TE,29,28,28));
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,4,2));ctx.E(ppc_or(RCPSR,RCPSR,TE));
}
static void setV_sub(Ctx& ctx,uint8_t res,uint8_t a,uint8_t b){
    ctx.E(ppc_xor(TE,a,b));ctx.E(ppc_xor(TF,a,res));ctx.E(ppc_and(TE,TE,TF));
    ctx.E(ppc_rlwinm(TE,TE,0,0,0));ctx.E(ppc_rlwinm(TE,TE,29,28,28));
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,4,2));ctx.E(ppc_or(RCPSR,RCPSR,TE));
}
static void setC_imm(Ctx& ctx,uint8_t cr){
    ctx.E(ppc_rlwinm(TA,cr,29,29,29));
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,3,1));
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}
static void primeCarry(Ctx& ctx){
    ctx.E(ppc_rlwinm(TA,RCPSR,3,31,31));
    ctx.E(ppc_addic(0,TA,-1));
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
        if(sc)ctx.E(ppc_rlwinm(TC,s,0,31,31));
        ctx.E(ppc_rlwinm(TA,RCPSR,2,0,0));
        ctx.E(ppc_rlwinm(d,s,31,1,31));
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
        if(sc&&rot){ctx.E(ppc_rlwinm(TC,dst,1,31,31));return true;}
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
            case ADD:case CMN:case ADC:setNZ(ctx,res);setC_xer(ctx);setV_add(ctx,res,opA,opB);break;
            case SUB:case CMP:case SBC:setNZ(ctx,res);setC_xer(ctx);setV_sub(ctx,res,opA,opB);break;
            case RSB:case RSC:         setNZ(ctx,res);setC_xer(ctx);setV_sub(ctx,res,opB,opA);break;
            default:setNZ(ctx,res);if(cset)setC_imm(ctx,TC);break;
        }
    }
    patchSkip(ctx,si);return true;
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

// ── MSR/MRS ──────────────────────────────────────────────────────────
// CRITICAL FIX: MSR with any control bits (mask & 0x7 != 0) changes the
// CPU mode and swaps banked registers.  The JIT cannot do this safely
// because it holds r0-r14 in fixed PPC registers that correspond to the
// CURRENT mode's register file.  Attempting to patch CPSR in-place and
// continue emitting would use the wrong register aliases.
//
// The correct solution is to fall back to the interpreter for ANY MSR
// that touches control bits.  Only MSR that touches ONLY the flags field
// (mask == 0x8, bits 31:24) is safe to handle inline because it doesn't
// swap register banks.
//
// MRS is always safe (it reads CPSR/SPSR into a GPR, no mode switch).
static bool emitMrsMsr(Ctx& ctx,uint32_t op,uint32_t){
    uint8_t cond=(op>>28)&0xF;if(cond==15)return false;

    // MRS Rd, CPSR  (0x010F0000 pattern)
    if((op&0x0FBF0FFF)==0x010F0000){
        uint8_t rd=(op>>12)&0xF;if(rd==15)return false;
        size_t si=emitCondSkip(ctx,cond);
        ctx.E(ppc_mr(RA[rd],RCPSR));
        patchSkip(ctx,si);
        return true;
    }

    // MRS Rd, SPSR  (0x014F0000 pattern) — always fall back (SPSR is mode-dependent)
    if((op&0x0FBF0FFF)==0x014F0000) return false;

    // MSR — check the field mask
    uint8_t mask=(op>>16)&0xF;

    // If control bits are involved (mask & 0x7), we MUST fall back to the
    // interpreter because a mode switch swaps banked registers.
    // This covers mask values: 0x1(c), 0x2(x), 0x3(cx), 0x4(s), 0x5(cs),
    // 0x6(xs), 0x7(cxs), 0x9(cf), 0xB(cxf), 0xD(csf), 0xF(cxsf).
    if(mask & 0x7) return false;

    // mask == 0x8: flags field only (bits 31:24) — safe to handle inline.
    // MSR CPSR_f, #imm
    if((op&0x0DB0F000)==0x0320F000){
        uint32_t imm=op&0xFF,rot=((op>>8)&0xF)*2;
        if(rot)imm=(imm>>rot)|(imm<<(32-rot));
        imm&=0xFF000000u;  // only flags field
        size_t si=emitCondSkip(ctx,cond);
        ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,8,31));  // clear bits 31:24
        ctx.li(TA,imm);
        ctx.E(ppc_or(RCPSR,RCPSR,TA));
        patchSkip(ctx,si);
        return true;
    }

    // MSR CPSR_f, Rm
    if((op&0x0DB0FFF0)==0x0120F000){
        uint8_t rm=op&0xF;if(rm==15)return false;
        size_t si=emitCondSkip(ctx,cond);
        ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,8,31));          // clear bits 31:24
        ctx.E(ppc_rlwinm(TA,RA[rm],0,0,7));              // keep only bits 31:24 of Rm
        ctx.E(ppc_or(RCPSR,RCPSR,TA));
        patchSkip(ctx,si);
        return true;
    }

    // MSR SPSR_f — fall back (SPSR is mode-dependent)
    return false;
}

static bool emitBlockXfer(Ctx& ctx,uint32_t op,uint32_t curPC){
    uint8_t cond=(op>>28)&0xF;
    if(cond==15||((op>>22)&1))return false;
    uint8_t rn=(op>>16)&0xF;uint16_t list=(uint16_t)(op&0xFFFF);
    if(rn>14||!list)return false;
    bool load=(op>>20)&1,loadPC=load&&(list&0x8000);
    size_t si=emitCondSkip(ctx,cond);
    emitSpill(ctx);
    ctx.li(TA,curPC+8);ctx.E(ppc_stw(TA,FRAME_SCR2,1));
    ctx.li(TA,curPC+4);ctx.E(ppc_stw(TA,FRAME_PC,1));
    ctx.ldCore();ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.li(TC,op);ctx.E(ppc_addi(TD,1,(int16_t)FRAME_REGSYNC));
    ctx.E(ppc_lwz(TE,FRAME_SCR2,1));
    ctx.E(ppc_addi(TF,1,(int16_t)FRAME_PC));ctx.E(ppc_addi(TG,1,(int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_armBlock);
    ctx.E(ppc_cmpi(0,TA,0));
    size_t bOk=ctx.sz();ctx.E(ppc_bc(4,0,0));
    emitReload(ctx);emitCommitExit(ctx,curPC,EXIT_FALLBACK);
    {int32_t d=(int32_t)((ctx.sz()-bOk)*4);ctx.base[bOk]=ppc_bc(4,0,(int16_t)d);}
    emitReload(ctx);
    if(loadPC){
        ctx.E(ppc_cmpi(0,TA,1));size_t b=ctx.sz();ctx.E(ppc_bc(12,2,0));
        emitCommitExit(ctx,curPC+4,EXIT_NORMAL);
        {int32_t d=(int32_t)((ctx.sz()-b)*4);ctx.base[b]=ppc_bc(12,2,(int16_t)d);}
        emitCommitExitDyn(ctx,EXIT_NORMAL);
        if(si!=SIZE_MAX){patchSkip(ctx,si);emitCommitExit(ctx,curPC+4,EXIT_NORMAL);}
        ctx.done=true;return true;
    }
    patchSkip(ctx,si);return true;
}
static bool dispARM(Ctx& ctx,uint32_t op,uint32_t curPC){
    uint8_t cond=(op>>28)&0xF;if(cond==15)return false;
    if((op&0x0F000000)==0x0F000000)return false;
    if((op&0x0FFFFFF0)==0x012FFF10||(op&0x0FFFFFF0)==0x012FFF30)return emitBranch(ctx,op,curPC);
    // MSR/MRS: handle the 0x01000000 space carefully.
    // Note: some multiply instructions also fall in this space when bit 7
    // and bit 4 are set — the dispatcher already checked for those above
    // via the it==0 case, so here we only see genuine MRS/MSR.
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
    if(o==3){if(rs==15)ctx.li(TA,(curPC+4)&~1u);else ctx.E(ppc_mr(TA,RA[rs]));ctx.E(ppc_stw(TA,FRAME_SCR0,1));emitBX_target(ctx);ctx.done=true;return true;}
    if(rd==15){
        if(o==1)return false;
        if(rs==15)ctx.li(TA,curPC+4);else ctx.E(ppc_mr(TA,RA[rs]));
        if(o==0){ctx.li(TB,curPC+4);ctx.E(ppc_add(TA,TB,TA));}
        ctx.E(ppc_stw(TA,FRAME_SCR0,1));emitBX_target(ctx);ctx.done=true;return true;
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
    if(blx){tgt&=~3u;ctx.li(TA,~(1u<<5));ctx.E(ppc_and(RCPSR,RCPSR,TA));}
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
// Inline scheduler tick
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
// Per-CPU runner
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

    if(pc==0xFFFFFFFFu){
        interp.jitRunOpcode();
        return cycPerInsn;
    }

    if(!validPC(pc,gba)){
        static uint32_t lastBadPC[2]={0u,0u};
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

    const int      reason=g_exitReason[cpu];
    const uint32_t expc  =g_exitPC[cpu];
    g_totalJIT[cpu]++;

    DebugLog("[JIT] cpu%d %08X -> %08X r=%d cpsr=%08X\n",
             cpu,pc,expc,reason,interp.getCpsrRef());

    LoopState& ls=s_loop[cpu];

    if(reason==EXIT_FALLBACK){
        if(!ls.normBursting) resetNormSpin(ls);

        if(ls.fbPC==expc){
            ls.fbCount++;
            if(ls.fbCount==SPIN_THRESH&&!ls.fbDumped){
                ls.fbDumped=true;
                DebugLog("[JIT] SPIN cpu%d stuck at %08X cpsr=%08X thumb=%d\n",
                         cpu,expc,interp.getCpsrRef(),(int)interp.isThumb());
                int stride=interp.isThumb()?2:4;
                for(int k=-2;k<=4;k++){
                    uint32_t a=expc+(uint32_t)(k*stride);
                    uint32_t w=interp.isThumb()
                        ?(uint32_t)core.memory.read<uint16_t>(arm7,a)
                        :core.memory.read<uint32_t>(arm7,a);
                    DebugLog("[JIT]   [%08X]=%08X%s\n",a,w,k==0?" <--":"");
                }
                uint32_t** regs=interp.getRegisters();
                if(regs)for(int i=0;i<15;i++)if(regs[i])DebugLog("[JIT]   r%d=%08X\n",i,*regs[i]);
            }
            if(ls.fbCount>=SPIN_THRESH){
                for(int i=0;i<SPIN_STEPS;i++){
                    if(interp.halted)break;
                    interp.jitRunOpcode();
                    tickInline(core,SPIN_CYC_STEP);
                    if(interp.halted)break;
                    uint32_t newPC=interp.getActualPC();
                    if(newPC!=expc){
                        DebugLog("[JIT] SPIN cpu%d escaped to %08X after %d steps\n",cpu,newPC,i+1);
                        ls.fbPC=0xFFFFFFFFu;ls.fbCount=0;ls.fbDumped=false;
                        resetNormSpin(ls);
                        break;
                    }
                }
                return cycPerInsn;
            }
        }else{
            if(ls.fbDumped)DebugLog("[JIT] SPIN cpu%d escaped to %08X\n",cpu,expc);
            ls.fbPC   =expc;
            ls.fbCount=1;
            ls.fbDumped=false;
        }
        interp.jitRunOpcode();
        return cycPerInsn;

    }else{
        ls.fbPC   =0xFFFFFFFFu;
        ls.fbCount=0;
        ls.fbDumped=false;

        if(normSpinDetect(ls,pc,expc)){
            const uint32_t spinA=ls.normBurstPC;
            const uint32_t spinB=ls.normBurstExit;

            DebugLog("[JIT] NORM SPIN cpu%d detected at %08X->%08X, bursting\n",cpu,spinA,spinB);
            ls.normBursting=true;

            bool escaped=false;
            for(int i=0;i<SPIN_STEPS;i++){
                if(interp.halted){escaped=true;break;}
                interp.jitRunOpcode();
                tickInline(core,SPIN_CYC_STEP);
                if(interp.halted){escaped=true;break;}
                uint32_t newPC=interp.getActualPC();
                if(newPC!=spinA&&newPC!=spinB){
                    DebugLog("[JIT] NORM SPIN cpu%d escaped %08X->%08X after %d steps\n",
                             cpu,spinA,newPC,i+1);
                    escaped=true;
                    break;
                }
            }

            resetNormSpin(ls);

            if(!escaped){
                DebugLog("[JIT] NORM SPIN cpu%d burst exhausted at %08X\n",cpu,spinA);
            }

            return cycPerInsn;
        }

        uint32_t n=b->insnCount>0?b->insnCount:1u;
        return n*cycPerInsn;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Status log
// ═══════════════════════════════════════════════════════════════════════
static uint32_t g_statusTick=0;
static void logStatus(Core& core){
    g_statusTick++;
    if((g_statusTick&0x3FF)!=0)return;
    DebugLog("[JIT] STATUS jit0=%u fb0=%u jit1=%u fb1=%u pos=%zu gen=%u\n",
             g_totalJIT[0],g_totalFB[0],g_totalJIT[1],g_totalFB[1],codePos,cacheGen);
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

    // Run both CPUs. ARM9 is master clock.
    // ARM7 runs at half the clock rate, so one ARM7 cycle = 2 ARM9 cycles.
    // We run them interleaved: for each ARM9 step, do one ARM7 step.
    // globalCycles counts ARM9 cycles.
    uint32_t acc0=0;
    for(int i=0;i<ITERS_NDS;i++){
        acc0 += runCpu(core,0,false);  // ARM9: contributes to master clock
        runCpu(core,1,false);          // ARM7: runs but doesn't add to acc
    }

    // Always advance at least one scanline so events fire on time.
    // If both CPUs were halted, still advance so IRQs can fire.
    uint32_t charge=(acc0>FLOOR_CYCLES_NDS)?acc0:FLOOR_CYCLES_NDS;
    tickInline(core,charge);
    logStatus(core);
}

void runJitGba(Core& core){
    if(!g_jitLive||!codeBuf){Interpreter::runCoreSingle<true,0>(core);return;}

    // GBA mode: only cpu1 (ARM7) runs. cpu0 is permanently halted.
    // Must always advance at least FLOOR_CYCLES_GBA so VBlank events fire.
    uint32_t acc=0;
    for(int i=0;i<ITERS_GBA;i++){
        uint32_t c=runCpu(core,1,true);
        // When halted (HALT opcode waiting for IRQ), charge 1 cycle so
        // the accumulator reflects time passing — otherwise the floor
        // never gets reached and the VBlank IRQ never fires.
        acc += (c>0)?c:CYCLES_PER_INSN_ARM7;
    }

    uint32_t charge=(acc>FLOOR_CYCLES_GBA)?acc:FLOOR_CYCLES_GBA;
    tickInline(core,charge);
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
    s_loop[0]={};s_loop[1]={};
    for(int j=0;j<4;j++){s_loop[0].normHist[j]=0xFFFFFFFFu;s_loop[1].normHist[j]=0xFFFFFFFFu;}
    s_loop[0].fbPC=s_loop[1].fbPC=0xFFFFFFFFu;
    s_loop[0].normBurstPC=s_loop[0].normBurstExit=0xFFFFFFFFu;
    s_loop[1].normBurstPC=s_loop[1].normBurstExit=0xFFFFFFFFu;
    s_loop[0].normBursting=s_loop[1].normBursting=false;
    for(size_t i=0;i<CSIZ;i++)cache[i].valid=false;
    memset(g_exitPC,  0,sizeof g_exitPC);
    memset(g_exitCPSR,0,sizeof g_exitCPSR);
    g_exitReason[0]=g_exitReason[1]=EXIT_FALLBACK;
    memset(codeBuf,0,JIT_BYTES);
    DCFlushRange(codeBuf,JIT_BYTES);
    ICInvalidateRange(codeBuf,JIT_BYTES);
    g_jitLive=true;
    printf("[JIT] ready buf=%p (%zuKB) tramp=%p BLK_ARMS=%zu\n",
           (void*)codeBuf,JIT_BYTES>>10,(void*)tr,BLK_ARMS);
    DebugLog("[JIT] init buf=%p tramp=%p BLK_ARMS=%zu\n",
             (void*)codeBuf,(void*)tr,BLK_ARMS);
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
    for(size_t i=0;i<CSIZ;i++)cache[i].valid=false;
}

void invalidateJitRange(uint32_t start,uint32_t end){
    for(size_t i=0;i<CSIZ;i++)
        if(cache[i].valid&&cache[i].armPC>=start&&cache[i].armPC<end)
            cache[i].valid=false;
}

} // namespace JitPpc
