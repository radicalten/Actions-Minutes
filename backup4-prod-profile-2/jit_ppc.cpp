// ============================================================================
//  jit_ppc.cpp — ARM → PowerPC dynamic recompiler for NooDS on Wii (Broadway)
//  Full rewrite implementing the audit plan P1..P13.
//
//  P1  direct `bl` helper calls                    4 insns + ctr dep -> 1 insn
//  P2  inline condition tests (andis./rlwnm+xor)   call + ~26 cyc -> 2..5 cyc
//  P3  block chaining (C entry / body split, link lists, cycle budget)
//  P4  shadow register file + lazy commit          sync 92 -> 18 cyc, commit
//      only when the interpreter is handed control (removes the setPC() hazard)
//  P5  R15 as operand (MOV lr,pc, shifter operands, offset registers)
//  P6  writes to PC (MOV|ADD pc, LDR pc, POP {pc}) including the conditional
//      form where the post-index writeback must still happen
//  P7  CP15 emitted (emitCoproc) + translation invalidation on cache/control
//      writes (Cp15::write companion patch)
//  P8  inline shift-by-register via slw/srw/sraw (XER.CA == ARM's C bit)
//  P9  chain budget derived from the scheduler deadline; works with either the
//      std::vector scheduler or the fixed-array one (companion patch)
//  P10 instrumentation: block signature verification, IRQ storm detector,
//      per-CPU counters, ring-buffered hot logging, bad-PC bitmap
//  P11 Thumb BLX (suffix 0x1D!), ARMv5 J1/J2 offsets, LDRD guard
//  P12 fast RAM window for loads (cached window always; true inline optional)
//  P13 one cache line per exit, bitmap instead of a 512-entry scan, exact
//      cycle accounting (conditional operand cost + taken-branch penalty)
//
//  INVARIANTS
//  ----------
//  I1 r14..r28 = guest r0..r14, r29 = CPSR, r30 = fast-window base, r31 = mask.
//     Scratch: TA=r3 TB=r4 TC=r5 TD=r6 TE=r7 TF=r8 TG=r9 TS=r10 RCALL=r11 TR=r12.
//     Only r14..r31 survive a chained branch, so a body must end with TA..TR dead.
//  I2 A link is a plain `b` (not `bl`). Every block in a chain shares the head
//     frame; the head's epilogue is the only return.
//  I3 A PC write never falls through: store FRAME_PC (+T bit), exit with NORMAL,
//     set ctx.done.
//  I4 A *conditional* PC write uses FRAME_FLAG: the body sits inside the
//     condition skip, the flag records "wrote PC", and the flag test is emitted
//     outside the skip so an unconditional writeback still executes.
//  I5 Only the C dispatcher calls JitHelp_takeOver/JitHelp_release. Generated
//     code writes the shadow, never the Interpreter. Any C code that touches the
//     Interpreter must call JitPpc::invalidateInterpreterView(cpu).
//  I6 Every exit records pendingPC + reason in the shadow; cycles are accumulated
//     by the emitter as a constant, so exits perform no allocation and no calls.
//  I7 The fast-window registers are baked into already-emitted blocks: changing
//     the window (Memory::updateMap*) requires a JIT flush. See P12 notes.
// ============================================================================

#include "jit_ppc.h"
#include "core.h"
#include "interpreter.h"
#include "memory.h"
#include "cp15.h"
#include "defines.h"
#include "debug_log.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <malloc.h>
#include <vector>

extern "C" {
#include <ogc/cache.h>
#include <ogc/system.h>
}

#ifndef JIT_LOG_ENABLE
#define JIT_LOG_ENABLE 0          // 1 = also print boot/fail messages
#endif
#if JIT_LOG_ENABLE
#define JLOG(...) DebugLog(__VA_ARGS__)
#else
#define JLOG(...) do{}while(0)
#endif

// ============================================================================
//  Configuration — every optimisation has a switch so you can bisect safely
// ============================================================================
namespace JitCfg {
    constexpr bool kDirectCalls  = true;   // P1
    constexpr bool kInlineCond   = true;   // P2
    constexpr bool kChain        = true;   // P3
    constexpr bool kShadow       = true;   // P4
    constexpr bool kPcWrites     = true;   // P5/P6
    constexpr bool kCp15         = true;   // P7
    constexpr bool kInlineShift  = true;   // P8
    constexpr bool kVerifyBlocks = false;  // P10: audit invalidation coverage
    constexpr bool kStormDump    = true;   // P10
    constexpr bool kFastWindow   = true;   // P12a: helper fast path (safe)
    constexpr bool kInlineRam    = false;  // P12b: true inline LDR (risky)
    constexpr bool kSafeEntry    = true;   // defensive shadow->valid check
    constexpr int  kBlkInsnsMax  = 96;     // guest insns per block body
    constexpr int  kMaxLinks     = 24;     // incoming chain links per block
}

static const int EXIT_NORMAL   = 0;
static const int EXIT_FALLBACK = 1;
static const int EXIT_BUDGET   = 2;
static const int EXIT_PCWRITE  = 3;

extern "C" void JitHelp_takeOver(Interpreter*, struct JitCpuState*);
extern "C" void JitHelp_release(Interpreter*, struct JitCpuState*);

// ============================================================================
//  Encoders (with the additions the plan needs: andis., cmplw/cmpli, slw/srw/
//  sraw, rlwnm, bl, b)
// ============================================================================
namespace Enc {
static inline uint32_t blr() { return 0x4E800020u; }
static inline uint32_t bl(intptr_t d){ return (18u<<26)|(((uint32_t)d)&0x03FFFFFCu)|1u; }
static inline uint32_t b (intptr_t d){ return (18u<<26)|(((uint32_t)d)&0x03FFFFFCu); }
static inline uint32_t bc(uint8_t bo,uint8_t bi,int16_t off,bool lk=false){
    return (16u<<26)|((bo&31u)<<21)|((bi&31u)<<16)|((uint32_t)(off&0xFFFC))|(lk?1u:0u); }
static inline uint32_t addi (uint8_t rt,uint8_t ra,int16_t i){ return (14u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t addis(uint8_t rt,uint8_t ra,int16_t i){ return (15u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t addic(uint8_t rt,uint8_t ra,int16_t i){ return (12u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t subfic(uint8_t rt,uint8_t ra,int16_t i){ return (8u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ori  (uint8_t ra,uint8_t rs,uint16_t i){ return (24u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i; }
static inline uint32_t xori (uint8_t ra,uint8_t rs,uint16_t i){ return (26u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i; }
static inline uint32_t andis_(uint8_t ra,uint8_t rs,uint16_t i){ return (29u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i; }
static inline uint32_t cmpi (uint8_t cr,uint8_t ra,int16_t i){ return (11u<<26)|((cr&7u)<<23)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t cmpli(uint8_t cr,uint8_t ra,uint16_t i){ return (10u<<26)|((cr&7u)<<23)|((uint32_t)ra<<16)|i; }
static inline uint32_t stw (uint8_t rs,int16_t d,uint8_t ra){ return (36u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t stwu(uint8_t rs,int16_t d,uint8_t ra){ return (37u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t lwz (uint8_t rt,int16_t d,uint8_t ra){ return (32u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d; }
static inline uint32_t Xf(uint8_t rt,uint8_t ra,uint8_t rb,uint32_t x,bool rc=false){
    return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|((uint32_t)rb<<11)|(x<<1)|(rc?1u:0u); }
static inline uint32_t add  (uint8_t d,uint8_t a,uint8_t b){ return Xf(d,a,b,266); }
static inline uint32_t addc (uint8_t d,uint8_t a,uint8_t b){ return Xf(d,a,b,10);  }
static inline uint32_t adde (uint8_t d,uint8_t a,uint8_t b){ return Xf(d,a,b,138); }
static inline uint32_t subf (uint8_t d,uint8_t a,uint8_t b){ return Xf(d,a,b,40);  }
static inline uint32_t subfc(uint8_t d,uint8_t a,uint8_t b){ return Xf(d,a,b,8);   }
static inline uint32_t subfe(uint8_t d,uint8_t a,uint8_t b){ return Xf(d,a,b,136); }
static inline uint32_t neg  (uint8_t d,uint8_t a){ return Xf(d,a,0,104); }
static inline uint32_t mullw(uint8_t d,uint8_t a,uint8_t b){ return Xf(d,a,b,235); }
static inline uint32_t and_ (uint8_t a,uint8_t s,uint8_t b){ return Xf(s,a,b,28);  }
static inline uint32_t or_  (uint8_t a,uint8_t s,uint8_t b){ return Xf(s,a,b,444); }
static inline uint32_t xor_ (uint8_t a,uint8_t s,uint8_t b){ return Xf(s,a,b,316); }
static inline uint32_t andc (uint8_t a,uint8_t s,uint8_t b){ return Xf(s,a,b,60);  }
static inline uint32_t nor  (uint8_t a,uint8_t s,uint8_t b){ return Xf(s,a,b,124); }
static inline uint32_t mr   (uint8_t a,uint8_t s)          { return or_(a,s,s); }
static inline uint32_t extsb(uint8_t a,uint8_t s){ return Xf(s,a,0,954); }
static inline uint32_t extsh(uint8_t a,uint8_t s){ return Xf(s,a,0,922); }
static inline uint32_t cntlzw(uint8_t a,uint8_t s){ return Xf(s,a,0,26); }
static inline uint32_t slw (uint8_t d,uint8_t a,uint8_t b){ return Xf(d,a,b,24);  }
static inline uint32_t srw (uint8_t d,uint8_t a,uint8_t b){ return Xf(d,a,b,536); }
static inline uint32_t sraw(uint8_t d,uint8_t a,uint8_t b){ return Xf(d,a,b,792); }
static inline uint32_t rlwinm(uint8_t a,uint8_t s,uint8_t sh,uint8_t mb,uint8_t me,bool rc=false){
    return (21u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)|((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1)|(rc?1u:0u); }
static inline uint32_t rlwimi(uint8_t a,uint8_t s,uint8_t sh,uint8_t mb,uint8_t me){
    return (20u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)|((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1); }
static inline uint32_t rlwnm(uint8_t a,uint8_t s,uint8_t b,uint8_t mb,uint8_t me){
    return (23u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)|((uint32_t)b<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1); }
static inline uint32_t srawi(uint8_t a,uint8_t s,uint8_t sh){
    return (31u<<26)|((uint32_t)s<<21)|((uint32_t)a<<16)|((uint32_t)sh<<11)|(824u<<1); }
static inline uint32_t mtspr(uint16_t spr,uint8_t rs){
    uint8_t lo=spr&31,hi=(spr>>5)&31;
    return (31u<<26)|((uint32_t)rs<<21)|((uint32_t)lo<<16)|((uint32_t)hi<<11)|(467u<<1); }
static inline uint32_t mfspr(uint8_t rt,uint16_t spr){
    uint8_t lo=spr&31,hi=(spr>>5)&31;
    return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)lo<<16)|((uint32_t)hi<<11)|(339u<<1); }
static inline uint32_t mtctr(uint8_t s){ return mtspr(9,s); }
static inline uint32_t mtlr (uint8_t s){ return mtspr(8,s); }
static inline uint32_t mflr (uint8_t t){ return mfspr(t,8); }
static inline uint32_t mfxer(uint8_t t){ return mfspr(t,1); }
static inline uint32_t mfcr (uint8_t t){ return (31u<<26)|((uint32_t)t<<21)|(19u<<1); }

// CR field f occupies CR bits 4f..4f+3: LT, GT, EQ, SO
static inline uint8_t crLT(int f){ return (uint8_t)(f*4+0); }
static inline uint8_t crGT(int f){ return (uint8_t)(f*4+1); }
static inline uint8_t crEQ(int f){ return (uint8_t)(f*4+2); }

static inline int liSeq(uint8_t rt,uint32_t v,uint32_t* buf){
    uint16_t hi=(uint16_t)(v>>16), lo=(uint16_t)(v&0xFFFF);
    if(!hi&&!lo){ buf[0]=addi(rt,0,0); return 1; }
    if(!hi){ if(lo<0x8000){ buf[0]=addi(rt,0,(int16_t)lo); return 1; }
             buf[0]=addi(rt,0,0); buf[1]=ori(rt,rt,lo); return 2; }
    if(!lo){ buf[0]=addis(rt,0,(int16_t)hi); return 1; }
    buf[0]=addis(rt,0,(int16_t)hi); buf[1]=ori(rt,rt,lo); return 2;
}
} // namespace Enc
using namespace Enc;

// ============================================================================
//  P4 — shadow register file
// ============================================================================
struct JitCpuState {
    uint32_t  regs[15];       // guest r0..r14
    uint32_t  cpsr;           // guest CPSR (N,Z,C,V,... in ARM positions)
    uint32_t  pendingPC;      // PC the JIT stopped at (no +8 bias)
    uint32_t  cycles;         // guest cycles accumulated for the current group
    uint32_t  budget;         // chain budget, in guest cycles (P9)
    int32_t   reason;         // EXIT_*
    uint8_t   valid;          // 0 => the interpreter owns guest state
    uint8_t   thumb;
    uint16_t  pad;
    uint32_t  fastBase;       // P12 guest window
    uint32_t  fastMask;
};
static JitCpuState g_state[2] __attribute__((aligned(64)));

static const int SH_REGS   = (int)offsetof(JitCpuState,regs);
static const int SH_CPSR   = (int)offsetof(JitCpuState,cpsr);
static const int SH_PC     = (int)offsetof(JitCpuState,pendingPC);
static const int SH_CYC    = (int)offsetof(JitCpuState,cycles);
static const int SH_BUDGET = (int)offsetof(JitCpuState,budget);
static const int SH_REASON = (int)offsetof(JitCpuState,reason);
static const int SH_VALID  = (int)offsetof(JitCpuState,valid);
static const int SH_THUMB  = (int)offsetof(JitCpuState,thumb);
static const int SH_BASE   = (int)offsetof(JitCpuState,fastBase);
static const int SH_MASK   = (int)offsetof(JitCpuState,fastMask);

struct MemWindow { uint8_t* host; uint32_t base, mask; };
static MemWindow g_win[2] = {};

extern "C" {

void JitHelp_takeOver(Interpreter* in, JitCpuState* sh){
    sh->valid = 0;
    if(!in || !in->isReady()) return;
    uint32_t** p = in->getRegisters();
    if(!p) return;
    for(int i=0;i<15;i++){ if(!p[i]) return; sh->regs[i]=*p[i]; }
    sh->cpsr      = in->getCpsrRef();
    sh->pendingPC = in->getActualPC();
    sh->thumb     = (uint8_t)in->isThumb();
    sh->valid     = 1;
}
void JitHelp_release(Interpreter* in, JitCpuState* sh){
    if(!sh->valid) return;
    uint32_t** p = in->getRegisters();
    if(!p) return;
    for(int i=0;i<15;i++) *p[i] = sh->regs[i];
    in->getCpsrRef() = sh->cpsr;
    in->setPC(sh->pendingPC);       // expense paid only on hand-off (I5)
    sh->valid = 0;
}

// ---- P2/P8 slow paths ------------------------------------------------------
uint32_t JitHelp_shiftReg(uint32_t type,uint32_t val,uint32_t amt,uint32_t* cpsr,int setC){
    amt &= 0xFFu;
    uint32_t C=(*cpsr>>29)&1u, res;
    switch(type&3u){
    case 0:
        if(amt==0) res=val;
        else if(amt<32){ C=(val>>(32-amt))&1u; res=val<<amt; }
        else if(amt==32){ C=val&1u; res=0; }
        else { C=0; res=0; }
        break;
    case 1:
        if(amt==0) res=val;
        else if(amt<32){ C=(val>>(amt-1))&1u; res=val>>amt; }
        else if(amt==32){ C=val>>31; res=0; }
        else { C=0; res=0; }
        break;
    case 2:
        if(amt==0) res=val;
        else if(amt<32){ C=(val>>(amt-1))&1u; res=(uint32_t)((int32_t)val>>amt); }
        else { C=val>>31; res=(uint32_t)((int32_t)val>>31); }
        break;
    default:
        if(amt==0) res=val;
        else { uint32_t r=amt&31u; if(r==0){ C=val>>31; res=val; } else { C=(val>>(r-1))&1u; res=(val>>r)|(val<<(32u-r)); } }
        break;
    }
    if(setC) *cpsr = (*cpsr & ~(1u<<29)) | (C<<29);
    return res;
}

// ---- LDM/STM (operates straight on the shadow array) ----------------------
int JitHelp_armBlock(Core* core,int arm7,uint32_t op,uint32_t* regs,uint32_t pcForR15,
                     uint32_t* pcOut,uint32_t* cpsrInOut){
    if(!core||!regs||!pcOut||!cpsrInOut) return -1;
    const bool p=(op>>24)&1,u=(op>>23)&1,S=(op>>22)&1,w=(op>>21)&1,l=(op>>20)&1;
    const uint8_t  rn  =(op>>16)&0xF;
    const uint16_t list=(uint16_t)(op&0xFFFF);
    if(S||rn>14||!list) return -1;
    int n=0; for(int i=0;i<16;i++) if(list&(1u<<i)) n++;
    const uint32_t base=regs[rn];
    uint32_t addr,wb;
    if(u){ wb=base+(uint32_t)n*4u; addr=p?base+4u:base; }
    else { wb=base-(uint32_t)n*4u; addr=p?wb:wb+4u; }
    int wrotePC=0;
    if(l){
        for(int i=0;i<16;i++){
            if(!(list&(1u<<i))) continue;
            uint32_t val=core->memory.read<uint32_t>((bool)arm7,addr); addr+=4;
            if(i==15){
                if(!arm7&&(val&1u)){ *cpsrInOut|=(1u<<5);  *pcOut=val&~1u; }
                else               { *cpsrInOut&=~(1u<<5); *pcOut=val&~3u; }
                wrotePC=1;
            } else regs[i]=val;
        }
        if(w&&!(list&(1u<<rn))) regs[rn]=wb;
    } else {
        int first=-1; for(int i=0;i<16;i++) if(list&(1u<<i)){ first=i; break; }
        for(int i=0;i<16;i++){
            if(!(list&(1u<<i))) continue;
            uint32_t val=(i==15)?pcForR15:((i==rn&&w&&i!=first)?wb:regs[i]);
            core->memory.write<uint32_t>((bool)arm7,addr,val); addr+=4;
        }
        if(w) regs[rn]=wb;
    }
    return wrotePC;
}

int JitHelp_thumbPushPop(Core* core,int arm7,uint32_t op,uint32_t* regs,
                         uint32_t* pcOut,uint32_t* cpsrInOut){
    if(!core||!regs||!pcOut||!cpsrInOut) return -1;
    const bool load=(op>>11)&1,R=(op>>8)&1;
    const uint8_t list=(uint8_t)(op&0xFF);
    int n=0; for(int i=0;i<8;i++) if(list&(1u<<i)) n++;
    if(R) n++;
    if(!load){
        uint32_t sp=regs[13]-(uint32_t)n*4u, addr=sp;
        for(int i=0;i<8;i++){ if(!(list&(1u<<i))) continue; core->memory.write<uint32_t>((bool)arm7,addr,regs[i]); addr+=4; }
        if(R) core->memory.write<uint32_t>((bool)arm7,addr,regs[14]);
        regs[13]=sp; return 0;
    }
    uint32_t addr=regs[13];
    for(int i=0;i<8;i++){ if(!(list&(1u<<i))) continue; regs[i]=core->memory.read<uint32_t>((bool)arm7,addr); addr+=4; }
    int wrotePC=0;
    if(R){
        uint32_t val=core->memory.read<uint32_t>((bool)arm7,addr); addr+=4;
        if(arm7) *pcOut=val&~1u;                       // ARMv4T: stays in Thumb
        else if(val&1u){ *cpsrInOut|=(1u<<5);  *pcOut=val&~1u; }
        else           { *cpsrInOut&=~(1u<<5); *pcOut=val&~3u; }
        wrotePC=1;
    }
    regs[13]=addr; return wrotePC;
}
int JitHelp_thumbBlock(Core* core,int arm7,uint32_t op,uint32_t* regs){
    if(!core||!regs) return -1;
    const bool load=(op>>11)&1;
    const uint8_t rb=(op>>8)&7, list=(uint8_t)(op&0xFF);
    if(!list){ regs[rb]+=0x40; return 0; }
    uint32_t addr=regs[rb];
    int n=0; for(int i=0;i<8;i++) if(list&(1u<<i)) n++;
    const uint32_t wb=addr+(uint32_t)n*4u;
    const bool rbIn=(list&(1u<<rb))!=0;
    if(load){
        for(int i=0;i<8;i++){ if(!(list&(1u<<i))) continue; regs[i]=core->memory.read<uint32_t>((bool)arm7,addr); addr+=4; }
        if(!rbIn) regs[rb]=wb;
    } else {
        int first=-1; for(int i=0;i<8;i++) if(list&(1u<<i)){ first=i; break; }
        for(int i=0;i<8;i++){
            if(!(list&(1u<<i))) continue;
            core->memory.write<uint32_t>((bool)arm7,addr,(i==rb&&i!=first)?wb:regs[i]); addr+=4;
        }
        regs[rb]=wb;
    }
    return 0;
}

// ---- P7: CP15 --------------------------------------------------------------
uint32_t JitHelp_cp15(Core* core,int arm7,uint32_t op,uint32_t val){
    (void)arm7;
    if(!core) return 0;
    const int op1=(op>>21)&7, crn=(op>>16)&0xF, crm=op&0xF, op2=(op>>5)&7;
    if((op>>20)&1) return core->cp15.read(op1,crn,crm,op2,false);
    core->cp15.write(op1,crn,crm,op2,val);      // + P7b flush (companion patch)
    return 0;
}

// ---- memory ----------------------------------------------------------------
static inline bool winHit(int cpu,uint32_t ad,uint32_t* off){
    const MemWindow& w=g_win[cpu];
    if(!w.host) return false;
    const uint32_t d=ad-w.base;
    if(d>w.mask) return false;
    *off=d; return true;
}
uint32_t JitHelp_r32(Core* c,int a,uint32_t ad){
    if(JitCfg::kFastWindow){ uint32_t o; if(c&&winHit(a,ad,&o)) return *(const uint32_t*)(g_win[a].host+o); }
    return c?c->memory.read<uint32_t>((bool)a,ad):0; }
uint32_t JitHelp_ldr32(Core* c,int a,uint32_t ad){
    if(!c) return 0;
    if(JitCfg::kFastWindow){ uint32_t o; if(winHit(a,ad,&o)){ uint32_t v=*(const uint32_t*)(g_win[a].host+o), r=(ad&3u)*8u; return r?((v>>r)|(v<<(32u-r))):v; } }
    uint32_t v=c->memory.read<uint32_t>((bool)a,ad&~3u), r=(ad&3u)*8u;
    return r?((v>>r)|(v<<(32u-r))):v; }
uint16_t JitHelp_r16(Core* c,int a,uint32_t ad){
    if(JitCfg::kFastWindow){ uint32_t o; if(c&&winHit(a,ad,&o)) return *(const uint16_t*)(g_win[a].host+o); }
    return c?c->memory.read<uint16_t>((bool)a,ad):0; }
uint8_t JitHelp_r8(Core* c,int a,uint32_t ad){
    if(JitCfg::kFastWindow){ uint32_t o; if(c&&winHit(a,ad,&o)) return g_win[a].host[o]; }
    return c?c->memory.read<uint8_t>((bool)a,ad):0; }
uint32_t JitHelp_ldrh(Core* c,int a,uint32_t ad){
    if(JitCfg::kFastWindow){ uint32_t o; if(c&&winHit(a,ad&~1u,&o)){ uint32_t v=*(const uint16_t*)(g_win[a].host+o); return (a&&(ad&1u))?((v>>8)|(v<<24)):v; } }
    if(!c) return 0;
    uint32_t v=c->memory.read<uint16_t>((bool)a,ad&~1u);
    return (a&&(ad&1u))?((v>>8)|(v<<24)):v; }
uint32_t JitHelp_ldrsh(Core* c,int a,uint32_t ad){
    if(a&&(ad&1u)) return (uint32_t)(int32_t)(int8_t)JitHelp_r8(c,a,ad);
    return (uint32_t)(int32_t)(int16_t)JitHelp_r16(c,a,ad&~1u); }
void JitHelp_w32(Core* c,int a,uint32_t ad,uint32_t v){ if(c)c->memory.write<uint32_t>((bool)a,ad,v); }
void JitHelp_w16(Core* c,int a,uint32_t ad,uint16_t v){ if(c)c->memory.write<uint16_t>((bool)a,ad,v); }
void JitHelp_w8 (Core* c,int a,uint32_t ad,uint8_t  v){ if(c)c->memory.write<uint8_t>((bool)a,ad,v); }

// ---- P10: signature over the guest bytes a block was built from ------------
uint32_t JitHelp_sigBlock(Core* core,int arm7,uint32_t pc,uint32_t thumb,uint32_t insns){
    if(!core) return 0;
    uint32_t h=0x811C9DC5u;
    const uint32_t step = thumb?2u:4u;
    for(uint32_t i=0;i<insns;i++){
        uint32_t w = thumb ? core->memory.read<uint16_t>((bool)arm7,pc+i*step)
                           : core->memory.read<uint32_t>((bool)arm7,pc+i*step);
        h=(h^w)*16777619u;
    }
    return h;
}

} // extern "C"

// ============================================================================
//  P10 — instrumentation: counters + MEM1 ring buffer (never fwrite in a storm)
// ============================================================================
struct JitStats {
    uint32_t insnJit[2]={}, insnInterp[2]={}, cTrips[2]={}, chains[2]={};
    uint32_t compileFail=0, stale[2]={}, storm[2]={}, badPC[2]={};
    uint32_t exits[4]={}, guestCycles[2]={}, blocksLive=0;
};
static JitStats g_stats;

static const size_t RING_BYTES=16384;
static char     g_ring[RING_BYTES];
static size_t   g_ringLen=0;
static uint32_t g_ringDrops=0;

static void ringAdd(const char* fmt,...) __attribute__((format(printf,1,2)));
static void ringAdd(const char* fmt,...){
    if(g_ringLen+192>=RING_BYTES){ g_ringDrops++; return; }
    va_list ap; va_start(ap,fmt);
    int n=vsnprintf(g_ring+g_ringLen,RING_BYTES-g_ringLen-1,fmt,ap);
    va_end(ap);
    if(n>0) g_ringLen+=(size_t)n;
}

// ============================================================================
//  Code buffer + block cache
// ============================================================================
namespace JitPpc { namespace {

static const size_t JIT_BYTES = 4u*1024u*1024u;   // 4 MB: chaining needs headroom
static const size_t JIT_WORDS = JIT_BYTES/4;

static uint32_t* codeBuf=nullptr;
static size_t    codePos=0;
static uint32_t  cacheGen=0;
static bool      g_live=false;

static const uint8_t RA[15]={14,15,16,17,18,19,20,21,22,23,24,25,26,27,28};
static const uint8_t RCPSR=29, RWINB=30, RWINM=31;
static const uint8_t TA=3,TB=4,TC=5,TD=6,TE=7,TF=8,TG=9,TS=10,RCALL=11,TR=12;

// ---- frame layout (I1: nothing here collides with r14..r31 saves) ----------
static const int FRAME_SIZE   =256;
static const int FRAME_LR_OFF =FRAME_SIZE+4;
static const int FRAME_SAVE   =16;                 // r14..r31 -> 16..88
static const int FRAME_SHADOW =92;
static const int FRAME_CORE   =96;
static const int FRAME_INTERP =100;
static const int FRAME_CPUIDX =104;
static const int FRAME_PC     =108;
static const int FRAME_BUDGET =112;
static const int FRAME_SCR0   =116;
static const int FRAME_SCR1   =120;
static const int FRAME_SCR2   =124;
static const int FRAME_FLAG   =128;
static_assert(FRAME_SIZE%16==0,"frame align");
static_assert(FRAME_SAVE+18*4==FRAME_SHADOW,"save map");
static_assert(FRAME_FLAG+4<=FRAME_SIZE,"frame fits");

struct LinkSite { uint32_t* site; uint32_t* stub; };

struct JitBlock {
    uint32_t  armPC=0, endPC=0, sig=0, nW=0, insnCount=0, cycles=0;
    uint32_t* code=nullptr;      // C entry  (prologue + reload + body)
    uint32_t* body=nullptr;      // chained entry (body only)
    uint8_t   cpu=0, linkCount=0;
    bool      thumb=false, valid=false;
    uint32_t  gen=0;
    LinkSite  links[JitCfg::kMaxLinks];
    void addLink(uint32_t* site,uint32_t* stub){
        if(linkCount<JitCfg::kMaxLinks) links[linkCount++]={site,stub};
    }
};

static const size_t CSIZ=1u<<13;               // 8192 blocks
static JitBlock cache[CSIZ];
static uint16_t g_pageBlocks[1u<<16];
static uint8_t  g_badPC[2][8192];              // P13: 1 bit per 4 guest bytes

static inline uint32_t pageIdx(uint32_t a){ return (a>>12)&0xFFFFu; }
static void pageAdd(uint32_t s,uint32_t e){ uint32_t a=pageIdx(s),b=pageIdx(e-1); g_pageBlocks[a]++; if(b!=a) g_pageBlocks[b]++; }
static void pageSub(uint32_t s,uint32_t e){ uint32_t a=pageIdx(s),b=pageIdx(e-1); if(g_pageBlocks[a])g_pageBlocks[a]--; if(b!=a&&g_pageBlocks[b])g_pageBlocks[b]--; }
static inline size_t hashPC(uint32_t pc,int cpu){ return ((pc>>1)^(pc>>13)^((uint32_t)cpu<<11))&(CSIZ-1); }

static inline bool badPCBit(int cpu,uint32_t pc){ return (g_badPC[cpu][(pc>>2)&0x1FFF]&(1u<<((pc>>2)&7)))!=0; }
static inline void markBadPC(int cpu,uint32_t pc){ g_badPC[cpu][(pc>>2)&0x1FFF]|=(uint8_t)(1u<<((pc>>2)&7)); }

static void flushICache(uint32_t* p,size_t nW){ DCFlushRange(p,nW*4); ICInvalidateRange(p,nW*4); }

// ---- emit context ----------------------------------------------------------
struct Ctx {
    uint32_t *base=nullptr,*cur=nullptr; size_t cap=0;
    bool thumb=false,arm7=false,done=false,overflow=false;
    uint32_t blockPC=0;
    int cpuIdx=0;
    Interpreter* interp=nullptr;
    Core* core=nullptr;
    int insnCount=0;
    uint32_t cyc=0;

    void E(uint32_t w){ if((size_t)(cur-base)<cap) *cur++=w; else overflow=true; }
    size_t sz() const { return (size_t)(cur-base); }
    size_t rem() const { size_t u=sz(); return u<cap?cap-u:0; }
    void li(uint8_t rt,uint32_t v){ uint32_t t[2]; int n=liSeq(rt,v,t); for(int i=0;i<n;i++) E(t[i]); }
    void ldShadowTo(uint8_t rt){ E(lwz(rt,FRAME_SHADOW,1)); }
    void ldShadow(){ ldShadowTo(TA); }
    void ldCore(){ E(lwz(TA,FRAME_CORE,1)); }
    void ldInterp(){ E(lwz(TA,FRAME_INTERP,1)); }
    void ldCpu(){ E(lwz(TB,FRAME_CPUIDX,1)); }

    // P1: one instruction when the callee is inside MEM1 (it always is here)
    void call(void* fn){
        uint32_t a=(uint32_t)(uintptr_t)fn;
        if(JitCfg::kDirectCalls && a>=0x80000000u && a<0x81800000u){
            intptr_t disp=(intptr_t)a-(intptr_t)cur;
            if(disp>=-(1<<25) && disp<=(1<<25)-4){ E(bl(disp)); return; }
        }
        uint16_t hi=(uint16_t)(a>>16), lo=(uint16_t)(a&0xFFFF);
        E(addis(RCALL,0,(int16_t)hi));
        if(lo) E(ori(RCALL,RCALL,lo));
        E(mtctr(RCALL));
        E(bc(20,0,0,true));                       // bctrl
    }
};

// ---------------------------------------------------------------------------
//  Branch-site patching.  Every branch template carries disp 0, so the final
//  displacement is ORed in (this preserves bo/bi, which P2 needs).
// ---------------------------------------------------------------------------
struct Skip { size_t a=SIZE_MAX, b=SIZE_MAX; };

static void patchOne(Ctx& C,size_t idx){
    if(idx==SIZE_MAX) return;
    int32_t d=(int32_t)((C.sz()-idx)*4);
    if(d<-32768||d>32764){ C.overflow=true; return; }
    C.base[idx] |= (uint32_t)d & 0xFFFCu;
}
static void patchSkip(Ctx& C,Skip s){ patchOne(C,s.a); patchOne(C,s.b); }
static void patchTo(Ctx& C,size_t idx,size_t targetAbs){
    if(idx==SIZE_MAX) return;
    int32_t d=(int32_t)((targetAbs-idx)*4);
    if(d<-32768||d>32764){ C.overflow=true; return; }
    C.base[idx] |= (uint32_t)d & 0xFFFCu;
}

// ============================================================================
//  P2 — inline condition evaluation.
//  CPSR: N=31 Z=30 C=29 V=28.  andis. TS,RCPSR,mask sets CR0 from the masked
//  value, so CR0.EQ means "that flag is clear": 2 instructions, no call, no
//  mfcr, and no dependency on CR fields surviving a helper call.
// ============================================================================
static Skip emitCondSkip(Ctx& C,uint8_t cond){
    Skip s;
    if(cond==14||cond==0xF||!JitCfg::kInlineCond) return s;
    switch(cond){
    case 0x0: C.E(andis_(TS,RCPSR,0x4000)); s.a=C.sz(); C.E(bc(12,crEQ(0),0)); break; // EQ
    case 0x1: C.E(andis_(TS,RCPSR,0x4000)); s.a=C.sz(); C.E(bc(4 ,crEQ(0),0)); break; // NE
    case 0x2: C.E(andis_(TS,RCPSR,0x2000)); s.a=C.sz(); C.E(bc(12,crEQ(0),0)); break; // CS
    case 0x3: C.E(andis_(TS,RCPSR,0x2000)); s.a=C.sz(); C.E(bc(4 ,crEQ(0),0)); break; // CC
    case 0x4: C.E(andis_(TS,RCPSR,0x8000)); s.a=C.sz(); C.E(bc(12,crEQ(0),0)); break; // MI
    case 0x5: C.E(andis_(TS,RCPSR,0x8000)); s.a=C.sz(); C.E(bc(4 ,crEQ(0),0)); break; // PL
    case 0x6: C.E(andis_(TS,RCPSR,0x1000)); s.a=C.sz(); C.E(bc(12,crEQ(0),0)); break; // VS
    case 0x7: C.E(andis_(TS,RCPSR,0x1000)); s.a=C.sz(); C.E(bc(4 ,crEQ(0),0)); break; // VC
    case 0x8: C.E(andis_(TS,RCPSR,0x2000)); s.a=C.sz(); C.E(bc(12,crEQ(0),0));       // HI=C&&!Z
              C.E(andis_(TS,RCPSR,0x4000)); s.b=C.sz(); C.E(bc(4 ,crEQ(0),0)); break;
    case 0x9: C.E(andis_(TS,RCPSR,0x2000)); s.a=C.sz(); C.E(bc(4 ,crEQ(0),0));       // LS=!C|Z
              C.E(andis_(TS,RCPSR,0x4000)); s.b=C.sz(); C.E(bc(12,crEQ(0),0)); break;
    default:                                                                         // GE/LT/GT/LE
        C.E(rlwinm(TS,RCPSR,3,0,0));            // V into bit31
        C.E(xor_(TS,TS,RCPSR));                 // bit31 = N^V
        C.E(andis_(TS,TS,0x8000));
        if(cond==0xA){ s.a=C.sz(); C.E(bc(4 ,crEQ(0),0)); }        // GE: skip if N!=V
        else if(cond==0xB){ s.a=C.sz(); C.E(bc(12,crEQ(0),0)); }   // LT: skip if N==V
        else if(cond==0xC){ s.a=C.sz(); C.E(bc(4 ,crEQ(0),0));     // GT: N!=V or Z!=0
                            C.E(andis_(TS,RCPSR,0x4000)); s.b=C.sz(); C.E(bc(4 ,crEQ(0),0)); }
        else { s.a=C.sz(); C.E(bc(12,crEQ(0),0));                  // LE: N==V and Z==0
               C.E(andis_(TS,RCPSR,0x4000)); s.b=C.sz(); C.E(bc(12,crEQ(0),0)); }
        break;
    }
    return s;
}

// ============================================================================
//  Flags.  Scratch: TS/TE/TF only, so operands in TA..TD survive.
//  rlwinm masks use IBM numbering (bit 0 = MSB): N=0, Z=1, C=2, V=3.
// ============================================================================
static void setNZ(Ctx& C,uint8_t r){
    C.E(rlwinm(RCPSR,RCPSR,0,2,31));      // clear N,Z
    C.E(rlwimi(RCPSR,r,0,0,0));           // N = r[31]
    C.E(cmpi(6,r,0));                     // CR6.EQ <= (r==0)
    C.E(mfcr(TS));
    C.E(rlwinm(TS,TS,25,1,1));            // CR6.EQ -> bit30
    C.E(or_(RCPSR,RCPSR,TS));
}
static void setC_xer(Ctx& C){
    C.E(mfxer(TS));
    C.E(rlwinm(TS,TS,0,2,2));             // XER.CA sits at bit29 == ARM C
    C.E(rlwinm(RCPSR,RCPSR,0,3,1));       // clear C
    C.E(or_(RCPSR,RCPSR,TS));
}
static void setC_bit0(Ctx& C,uint8_t r){
    C.E(rlwinm(TS,r,29,2,2));
    C.E(rlwinm(RCPSR,RCPSR,0,3,1));
    C.E(or_(RCPSR,RCPSR,TS));
}
static void setV_add(Ctx& C,uint8_t res,uint8_t a,uint8_t b){
    C.E(xor_(TE,res,a)); C.E(xor_(TF,res,b)); C.E(and_(TE,TE,TF));
    C.E(rlwinm(TE,TE,29,3,3)); C.E(rlwinm(RCPSR,RCPSR,0,4,2)); C.E(or_(RCPSR,RCPSR,TE));
}
static void setV_sub(Ctx& C,uint8_t res,uint8_t a,uint8_t b){   // a - b
    C.E(xor_(TE,a,b)); C.E(xor_(TF,a,res)); C.E(and_(TE,TE,TF));
    C.E(rlwinm(TE,TE,29,3,3)); C.E(rlwinm(RCPSR,RCPSR,0,4,2)); C.E(or_(RCPSR,RCPSR,TE));
}
// Carry-in.  addc/adde take CA as carry-in; subfc/subfe take CA as
// "no borrow".  Both want CA == CPSR.C, and one addic gives exactly that:
//     TS  = CPSR.C                       (0 or 1)
//     addic r0,TS,-1   ->  CA = 1 iff TS != 0  (0xFFFFFFFF + 0 = no carry)
// so this single pair of instructions serves ADC/SBC/RSC/ADD/SUB carry logic.
// It must be emitted *after* the shifter (which may clobber CA) and before the
// addc/subfc that consumes it — that ordering is why emitDP/the Thumb ALU ops
// look the way they do.
static void primeCarry(Ctx& C){             // XER.CA <= CPSR.C
    C.E(rlwinm(TS,RCPSR,3,31,31));
    C.E(addic(0,TS,-1));
}

// ============================================================================
//  Shifter, immediate amounts.  Carry-out lands in TC bit0 when sc.
// ============================================================================
static void sLslI(Ctx& C,uint8_t d,uint8_t s,int i,bool sc){
    if(i==0){ if(sc)C.E(rlwinm(TC,RCPSR,3,31,31)); if(d!=s)C.E(mr(d,s)); }
    else if(i<32){ if(sc)C.E(rlwinm(TC,s,(uint8_t)i,31,31));
                   if(i==31)C.E(addi(d,0,0)); else C.E(rlwinm(d,s,(uint8_t)i,0,(uint8_t)(31-i))); }
    else if(i==32){ if(sc)C.E(rlwinm(TC,s,0,31,31)); C.E(addi(d,0,0)); }
    else { if(sc)C.E(addi(TC,0,0)); C.E(addi(d,0,0)); }
}
static void sLsrI(Ctx& C,uint8_t d,uint8_t s,int i,bool sc){
    if(i==32){ if(sc)C.E(rlwinm(TC,s,1,31,31)); C.E(addi(d,0,0)); }
    else if(i>=1&&i<32){ if(sc)C.E(rlwinm(TC,s,(uint8_t)((33-i)&31),31,31));
                         C.E(rlwinm(d,s,(uint8_t)(32-i),(uint8_t)i,31)); }
    else { if(sc)C.E(addi(TC,0,0)); C.E(addi(d,0,0)); }
}
static void sAsrI(Ctx& C,uint8_t d,uint8_t s,int i,bool sc){
    if(i>=32){ if(sc)C.E(rlwinm(TC,s,1,31,31)); C.E(srawi(d,s,31)); }
    else if(i>0){ if(sc)C.E(rlwinm(TC,s,(uint8_t)((33-i)&31),31,31)); C.E(srawi(d,s,(uint8_t)i)); }
    else { if(sc)C.E(rlwinm(TC,RCPSR,3,31,31)); if(d!=s)C.E(mr(d,s)); }
}
static void sRorI(Ctx& C,uint8_t d,uint8_t s,int i,bool sc){
    if(i==0){                                              // RRX
        if(sc)C.E(rlwinm(TC,s,0,31,31));
        C.E(rlwinm(TS,RCPSR,2,0,0));
        C.E(rlwinm(d,s,31,1,31));
        C.E(or_(d,d,TS));
    } else {
        i&=31;
        if(i==0){ if(sc)C.E(rlwinm(TC,s,1,31,31)); if(d!=s)C.E(mr(d,s)); }
        else { if(sc)C.E(rlwinm(TC,s,(uint8_t)((33-i)&31),31,31));
               C.E(rlwinm(d,s,(uint8_t)(32-i),0,31)); }
    }
}

// ============================================================================
//  P8 — shift by register.  slw/srw/sraw give ARM's carry in XER.CA for
//  amounts 1..31; the helper covers amt==0 (C preserved) and amt>=32.
//  ROR has no cheap CA on PPC, so it always uses the helper.
// ============================================================================
static void emitShiftHelper(Ctx& C,uint32_t type,uint8_t dst,uint8_t val,uint8_t amt,bool setC){
    uint8_t v=val, m=amt;
    C.E(mr(TB,v));
    C.E(mr(TC,m));                       // copy before TD/TA are reused
    C.ldShadowTo(TF);
    C.E(addi(TD,TF,SH_CPSR));
    C.E(addi(TA,0,(int16_t)type));
    C.E(addi(TE,0,setC?1:0));
    C.call((void*)&JitHelp_shiftReg);
    if(dst!=TA) C.E(mr(dst,TA));
}
static bool emitShiftReg(Ctx& C,uint32_t type,uint8_t dst,uint8_t val,uint8_t amt,bool setC){
    if(!JitCfg::kInlineShift || type==3){ emitShiftHelper(C,type,dst,val,amt,setC); return true; }
    uint8_t v=val;
    if(v==TD){ C.E(mr(TF,v)); v=TF; }     // TD is about to hold the masked amount
    C.E(rlwinm(TD,amt,0,24,31));          // ARM masks the shift amount to 8 bits
    C.E(cmpi(0,TD,0));
    size_t z=C.sz(); C.E(bc(4,crEQ(0),0));         // amt==0 -> helper
    C.E(cmpli(1,TD,32));
    size_t m=C.sz(); C.E(bc(4,crLT(1),0));         // CR1.LT clear => amt>=32 -> helper
    if(type==0) C.E(slw (v==dst?dst:dst,v,TD));    // result + XER.CA
    if(type==1) C.E(srw (dst,v,TD));
    if(type==2) C.E(sraw(dst,v,TD));
    if(setC){ C.E(mfxer(TS)); C.E(rlwinm(RCPSR,RCPSR,0,3,1)); C.E(rlwinm(TS,TS,0,2,2)); C.E(or_(RCPSR,RCPSR,TS)); }
    size_t j=C.sz(); C.E(b(0));                    // over the helper
    size_t helperStart=C.sz();
    patchTo(C,z,helperStart); patchTo(C,m,helperStart);
    emitShiftHelper(C,type,dst,val,amt,setC);
    C.base[j]=b((intptr_t)((C.sz()-j)*4));
    return true;
}
// ============================================================================
//  Prologue / reload / epilogue / exits
// ============================================================================
static void emitReloadFromShadow(Ctx& C){
    C.ldShadowTo(TA);
    for(int i=0;i<15;i++) C.E(lwz(RA[i],SH_REGS+i*4,TA));
    C.E(lwz(RCPSR,SH_CPSR,TA));
    C.E(lwz(RWINB,SH_BASE,TA));
    C.E(lwz(RWINM,SH_MASK,TA));
}
static void emitEpilogue(Ctx& C){
    for(int r=14;r<=31;r++) C.E(lwz(r,FRAME_SAVE+(r-14)*4,1));
    C.E(lwz(0,FRAME_LR_OFF,1));
    C.E(mtlr(0));
    C.E(addi(1,1,(int16_t)FRAME_SIZE));
    C.E(blr());
}
// Exit to C: write the whole shadow back, record PC/reason/cycles, return.
static void emitExitToC(Ctx& C,uint32_t nextPC,int reason){
    C.ldShadowTo(TA);
    for(int i=0;i<15;i++) C.E(stw(RA[i],SH_REGS+i*4,TA));
    C.E(stw(RCPSR,SH_CPSR,TA));
    C.li(TB,nextPC);
    C.E(stw(TB,SH_PC,TA));
    C.E(addi(TC,0,(int16_t)reason));
    C.E(stw(TC,SH_REASON,TA));
    C.E(lwz(TD,SH_CYC,TA));
    C.E(addi(TD,TD,(int16_t)C.cyc));
    C.E(stw(TD,SH_CYC,TA));
    emitEpilogue(C);
}
static void emitExitToCDyn(Ctx& C,int reason){          // PC already in FRAME_PC
    C.ldShadowTo(TA);
    for(int i=0;i<15;i++) C.E(stw(RA[i],SH_REGS+i*4,TA));
    C.E(stw(RCPSR,SH_CPSR,TA));
    C.E(lwz(TB,FRAME_PC,1));
    C.E(stw(TB,SH_PC,TA));
    C.E(addi(TC,0,(int16_t)reason));
    C.E(stw(TC,SH_REASON,TA));
    C.E(lwz(TD,SH_CYC,TA));
    C.E(addi(TD,TD,(int16_t)C.cyc));
    C.E(stw(TD,SH_CYC,TA));
    emitEpilogue(C);
}
// I4: conditional PC writes.  Flag = "this instruction wrote PC".
static void emitSetPCFlag(Ctx& C,uint32_t v){
    C.li(TD,v); C.E(stw(TD,FRAME_FLAG,1));
}
static void emitPCFlagGate(Ctx& C){
    C.E(lwz(TD,FRAME_FLAG,1));
    C.E(cmpi(0,TD,0));
    size_t keep=C.sz(); C.E(bc(12,crEQ(0),0));          // flag==0 -> keep compiling
    emitExitToCDyn(C,EXIT_PCWRITE);
    patchOne(C,keep);
}
// Load-to-PC / POP {pc} / LDM-with-PC: ARMv5 interworks on bit 0, ARM7 does not.
static void emitPCWriteDyn(Ctx& C){                     // value in FRAME_PC
    C.E(lwz(TA,FRAME_PC,1));
    if(C.arm7){
        C.E(rlwinm(TB,TA,0,0,29));
        C.E(stw(TB,FRAME_PC,1));
        C.E(rlwinm(RCPSR,RCPSR,0,27,25));
    } else {
        C.E(rlwinm(TC,TA,0,31,31));                     // bit0
        C.E(rlwinm(TD,TC,1,30,30));                     // bit0<<1
        C.E(xori(TD,TD,2));                             // 2 (ARM) / 0 (Thumb)
        C.E(ori(TD,TD,1));                              // 3 / 1
        C.E(andc(TB,TA,TD));
        C.E(stw(TB,FRAME_PC,1));
        C.E(rlwinm(TC,TC,5,26,26));                     // -> T
        C.E(rlwinm(RCPSR,RCPSR,0,27,25));
        C.E(or_(RCPSR,RCPSR,TC));
    }
    emitExitToCDyn(C,EXIT_NORMAL);
}
// BX/BLX target: value in FRAME_SCR0 (always interworks, both CPUs)
static void emitBXTarget(Ctx& C){
    C.E(lwz(TA,FRAME_SCR0,1));
    C.E(rlwinm(TC,TA,0,31,31));
    C.E(rlwinm(TD,TC,1,30,30));
    C.E(xori(TD,TD,2));
    C.E(ori(TD,TD,1));
    C.E(andc(TB,TA,TD));
    C.E(stw(TB,FRAME_PC,1));
    C.E(rlwinm(TC,TC,5,26,26));
    C.E(rlwinm(RCPSR,RCPSR,0,27,25));
    C.E(or_(RCPSR,RCPSR,TC));
    emitExitToCDyn(C,EXIT_NORMAL);
}

// ============================================================================
//  P3 — chaining.  Forward declarations first (compile() publishes blocks).
// ============================================================================
struct PendingLink { uint32_t* site; uint32_t* stub; uint32_t targetPC; uint8_t cpu; bool thumb; };
static std::vector<PendingLink> g_pending;
static JitBlock* compile(Interpreter* interp,Core* core,uint32_t armPC,bool arm7,int cpuIdx);

static JitBlock* lookupValid(uint32_t pc,int cpu,bool thumb){
    JitBlock& b=cache[hashPC(pc,cpu)];
    if(b.valid&&b.armPC==pc&&b.cpu==(uint8_t)cpu&&b.thumb==thumb&&b.gen==cacheGen) return &b;
    return nullptr;
}
static void resolvePendingFor(JitBlock* t){
    for(size_t i=0;i<g_pending.size();){
        PendingLink& l=g_pending[i];
        if(l.targetPC==t->armPC && l.cpu==t->cpu && l.thumb==t->thumb){
            *l.site = b((intptr_t)((t->body)-l.site));
            t->addLink(l.site,l.stub);
            g_pending[i]=g_pending.back(); g_pending.pop_back();
        } else ++i;
    }
}
// A site with no link yet must still be *correct*: point it at a local stub that
// exits to C with the intended PC.  Compiling the target later repoints it.
static void emitLinkOrExit(Ctx& C,uint32_t targetPC){
    JitBlock* t = JitCfg::kChain?lookupValid(targetPC,C.cpuIdx,C.thumb):nullptr;
    size_t site=C.sz(); C.E(b(0));
    size_t stub=C.sz();
    emitExitToC(C,targetPC,EXIT_NORMAL);
    if(t){
        C.base[site]=b((intptr_t)((t->body)-(C.base+site)));
        t->addLink(C.base+site,nullptr);
    } else {
        C.base[site]=b((intptr_t)((stub-site)*4));
        g_pending.push_back({C.base+site,C.base+stub,targetPC,(uint8_t)C.cpuIdx,C.thumb});
    }
}
// End of a body that fell through (or of a taken branch): spend budget, link.
static void emitBodyEnd(Ctx& C,uint32_t nextPC,int exitReason=EXIT_NORMAL){
    if(!JitCfg::kChain){ emitExitToC(C,nextPC,exitReason); return; }
    JitBlock* t=lookupValid(nextPC,C.cpuIdx,C.thumb);
    if(!t){ emitLinkOrExit(C,nextPC); return; }
    C.E(lwz(TB,FRAME_BUDGET,1));
    C.E(addi(TB,TB,-(int16_t)C.cyc));
    C.E(stw(TB,FRAME_BUDGET,1));
    C.E(cmpi(0,TB,0));
    size_t over=C.sz(); C.E(bc(4,crLT(0),0));           // budget exhausted -> exit
    size_t site=C.sz(); C.E(b(0));
    C.base[site]=b((intptr_t)((t->body)-(C.base+site)));
    t->addLink(C.base+site,nullptr);
    patchOne(C,over);
    emitExitToC(C,nextPC,EXIT_BUDGET);
}
static void unlinkIncoming(JitBlock* b){
    for(uint8_t i=0;i<b->linkCount;i++){
        LinkSite& l=b->links[i];
        uint32_t* dst = b->valid? b->body : l.stub;
        if(!dst) dst = b->code;
        *l.site = b((intptr_t)(dst-l.site));
    }
    if(!b->valid) b->linkCount=0;
}

// ============================================================================
//  ARM data processing — P5 (R15 operand) and P6a (write to PC)
// ============================================================================
enum DPo { DP_AND=0,DP_EOR,DP_SUB,DP_RSB,DP_ADD,DP_ADC,DP_SBC,DP_RSC,
           DP_TST,DP_TEQ,DP_CMP,DP_CMN,DP_ORR,DP_MOV,DP_BIC,DP_MVN };

static bool emitShifter(Ctx& C,uint32_t op,uint8_t dst,bool sc,uint32_t curPC){
    if((op>>25)&1){
        uint32_t v=op&0xFF, rot=((op>>8)&0xF)*2;
        if(rot) v=(v>>rot)|(v<<(32-rot));
        C.li(dst,v);
        if(sc&&rot){ C.E(rlwinm(TC,dst,1,31,31)); return true; }
        return false;
    }
    const uint8_t rm=op&0xF, rs=(op>>8)&0xF, st=(op>>5)&3;
    const bool regShift=(op>>4)&1;
    uint8_t src;
    if(rm==15){ C.li(TE,curPC+(regShift?12u:8u)); src=TE; }   // P5
    else src=RA[rm];
    if(!regShift){
        int sa=(op>>7)&0x1F;
        switch(st){
            case 0: sLslI(C,dst,src,sa,sc); break;
            case 1: sLsrI(C,dst,src,sa?sa:32,sc); break;
            case 2: sAsrI(C,dst,src,sa?sa:32,sc); break;
            default:sRorI(C,dst,src,sa,sc); break;
        }
        return sc;
    }
    uint8_t amt;
    if(rs==15){ C.li(TD,curPC+8u); amt=TD; }                  // P5
    else amt=RA[rs];
    emitShiftReg(C,st,dst,src,amt,sc);
    return false;                                            // C handled by that path
}

static bool emitDP(Ctx& C,uint32_t op,uint32_t curPC){
    const uint8_t cond=(op>>28)&0xF, dop=(op>>21)&0xF;
    const bool s=(op>>20)&1;
    const uint8_t rn=(op>>16)&0xF, rd=(op>>12)&0xF;
    const bool isTest=(dop==DP_TST||dop==DP_TEQ||dop==DP_CMP||dop==DP_CMN);
    if(cond==15) return false;
    if(isTest&&!s) return false;                 // S=0 with a test opcode is the
                                                 // MRS/MSR/CLZ/misc encoding space
    const bool dpPC = (!isTest && rd==15);
    if(dpPC && s) return false;                  // S=1 with Rd=PC: exception return
    if(dpPC && !JitCfg::kPcWrites) return false;
    const bool immForm=(op>>25)&1, regShift=!immForm&&((op>>4)&1);
    if(rn==15 && regShift) return false;         // rare: rn=R15 with register shift

    C.cyc += 1;
    Skip si = emitCondSkip(C,cond);
    const bool logC = s && (dop==DP_AND||dop==DP_EOR||dop==DP_TST||dop==DP_TEQ||
                            dop==DP_ORR||dop==DP_MOV||dop==DP_BIC||dop==DP_MVN);
    bool cset = emitShifter(C,op,TA,logC,curPC);
    uint8_t srcRn;
    if(rn==15){ C.li(TD,curPC+8u); srcRn=TD; } else srcRn=RA[rn];
    if(dop==DP_ADC||dop==DP_SBC||dop==DP_RSC) primeCarry(C);
    const bool needV = s && (dop==DP_ADD||dop==DP_SUB||dop==DP_RSB||dop==DP_CMN||
                             dop==DP_CMP||dop==DP_ADC||dop==DP_SBC||dop==DP_RSC);
    if(needV){ C.E(stw(TA,FRAME_SCR0,1)); C.E(stw(srcRn,FRAME_SCR1,1)); }
    uint8_t res = (isTest||dpPC)?TR:RA[rd];
    switch((DPo)dop){
        case DP_AND: case DP_TST: C.E(and_(res,srcRn,TA)); break;
        case DP_EOR: case DP_TEQ: C.E(xor_(res,srcRn,TA)); break;
        case DP_SUB: case DP_CMP: C.E(subfc(res,TA,srcRn)); break;
        case DP_RSB:              C.E(subfc(res,srcRn,TA)); break;
        case DP_ADD: case DP_CMN: C.E(addc(res,srcRn,TA)); break;
        case DP_ADC:              C.E(adde(res,srcRn,TA)); break;
        case DP_SBC:              C.E(subfe(res,TA,srcRn)); break;
        case DP_RSC:              C.E(subfe(res,srcRn,TA)); break;
        case DP_ORR:              C.E(or_(res,srcRn,TA)); break;
        case DP_MOV:              if(res!=TA) C.E(mr(res,TA)); break;
        case DP_BIC:              C.E(andc(res,srcRn,TA)); break;
        case DP_MVN:              C.E(nor(res,TA,TA)); break;
        default: break;
    }
    if(s){
        uint8_t opA=srcRn, opB=TA;
        if(needV){ C.E(lwz(TA,FRAME_SCR0,1)); opB=TA; C.E(lwz(TD,FRAME_SCR1,1)); opA=TD; }
        switch((DPo)dop){
            case DP_ADD: case DP_CMN: case DP_ADC: setNZ(C,res); setC_xer(C); setV_add(C,res,opA,opB); break;
            case DP_SUB: case DP_CMP: case DP_SBC: setNZ(C,res); setC_xer(C); setV_sub(C,res,opA,opB); break;
            case DP_RSB: case DP_RSC:              setNZ(C,res); setC_xer(C); setV_sub(C,res,opB,opA); break;
            default: setNZ(C,res); if(cset) setC_bit0(C,TC); break;
        }
    }
    if(dpPC){                                    // P6a: no interworking, PC aligned
        C.E(rlwinm(TR,TR,0,0,29));
        C.E(stw(TR,FRAME_PC,1));
        C.E(rlwinm(RCPSR,RCPSR,0,27,25));        // stays in ARM state
        emitExitToCDyn(C,EXIT_NORMAL);
        C.done=true;
    }
    patchSkip(C,si);
    return true;
}

// ============================================================================
//  ARM single load/store — P6b/P6c (PC destination, conditional writeback)
// ============================================================================
static bool emitInlineLdr32(Ctx& C,uint8_t addr,uint8_t dst);
static bool emitLS(Ctx& C,uint32_t op,uint32_t curPC){
    const uint8_t cond=(op>>28)&0xF;
    if(cond==15) return false;
    const bool ld=(op>>20)&1, by=(op>>22)&1, up=(op>>23)&1, pre=(op>>24)&1, wb=(op>>21)&1, immO=!((op>>25)&1);
    const uint8_t rn=(op>>16)&0xF, rd=(op>>12)&0xF;
    const bool toPC = (rd==15&&ld);
    if(rd==15&&!ld) return false;                       // storing PC: rare, interpreter
    if(toPC&&!JitCfg::kPcWrites) return false;
    if(rn==15&&(!immO||!pre||wb)) return false;
    if(!immO&&(((op&0xF)==15)||((op>>4)&1))) return false;

    C.cyc += 2;
    const bool condPC = toPC && cond!=14;
    if(condPC) emitSetPCFlag(C,0);
    Skip si = emitCondSkip(C,cond);
    // offset -> TA ; address -> TB
    if(immO) C.li(TA,op&0xFFF);
    else {
        uint8_t rm=op&0xF, sh=(op>>5)&3; int sa=(op>>7)&0x1F;
        if(sh==0) sLslI(C,TA,RA[rm],sa,false);
        else if(sh==1) sLsrI(C,TA,RA[rm],sa?sa:32,false);
        else if(sh==2) sAsrI(C,TA,RA[rm],sa?sa:32,false);
        else sRorI(C,TA,RA[rm],sa,false);
    }
    if(rn==15){
        uint32_t base=curPC+8u;
        C.li(TB, up? base+(op&0xFFF) : base-(op&0xFFF));
    }
    else if(pre){ if(up) C.E(add(TB,RA[rn],TA)); else C.E(subf(TB,TA,RA[rn])); }
    else C.E(mr(TB,RA[rn]));

    bool inlined=false;
    // P12b is word loads only, and never for a PC destination: guest r15 has no
    // host register (RA[] covers r0..r14 only) — PC writes go through FRAME_PC.
    if(ld&&!by&&pre&&!toPC&&JitCfg::kInlineRam){
        C.E(stw(TB,FRAME_SCR0,1));                       // address survives the fast path
        inlined=emitInlineLdr32(C,TB,RA[rd]);
        if(inlined) C.E(mr(TA,RA[rd]));                  // TA becomes the value temp
    }
    if(!inlined){
        C.E(stw(TB,FRAME_SCR0,1));
        C.ldCore(); C.E(addi(TB,0,C.arm7?1:0)); C.E(lwz(TC,FRAME_SCR0,1));
        if(!ld) C.E(mr(TD,RA[rd]));
        void* fn = ld ? (by?(void*)&JitHelp_r8:(void*)&JitHelp_ldr32)
                      : (by?(void*)&JitHelp_w8:(void*)&JitHelp_w32);
        C.call(fn);
    }
    if(ld){
        if(toPC){
            C.E(stw(TA,FRAME_PC,1));
            if(condPC){ emitSetPCFlag(C,1); }
        } else C.E(mr(RA[rd],TA));
    }
    // writeback (unconditional for post-index: I4)
    if(rn!=15){
        const bool wbOK = !(ld&&rd==rn);
        if(!pre){
            if(wbOK){
                C.E(lwz(TA,FRAME_SCR0,1));
                if(up) C.E(add(RA[rn],RA[rn],TA)); else C.E(subf(RA[rn],TA,RA[rn]));
            }
        } else if(wb&&wbOK){
            if(ld&&rd!=rn) C.E(lwz(RA[rn],FRAME_SCR0,1));
            else { C.E(lwz(TA,FRAME_SCR0,1)); C.E(mr(RA[rn],TA)); }
        }
    }
    patchSkip(C,si);
    if(toPC){
        if(condPC) emitPCFlagGate(C);                    // exits only when PC was written
        else { emitPCWriteDyn(C); C.done=true; }
    }
    return true;
}

// extra load/store (halfword / signed byte / doubleword)
static bool emitLSExtra(Ctx& C,uint32_t op,uint32_t){
    if((op&0x0E000090)!=0x00000090) return false;
    const uint8_t cond=(op>>28)&0xF; if(cond==15) return false;
    const bool p=(op>>24)&1,u=(op>>23)&1,w=(op>>21)&1,l=(op>>20)&1,imm=(op>>22)&1;
    const uint8_t rn=(op>>16)&0xF, rd=(op>>12)&0xF, sh=(op>>5)&3;
    if(rd==15||rn==15) return false;
    if(sh==0) return false;                  // P11: LDRD/STRD -> interpreter (no miscompile)
    if(!l&&sh!=1) return false;
    if(!imm&&(op&0xF)==15) return false;
    C.cyc += 2;
    Skip si = emitCondSkip(C,cond);
    if(imm) C.li(TA,((op>>4)&0xF0)|(op&0xF));
    else C.E(mr(TA,RA[op&0xF]));
    if(p){ if(u) C.E(add(TB,RA[rn],TA)); else C.E(subf(TB,TA,RA[rn])); }
    else C.E(mr(TB,RA[rn]));
    C.E(stw(TA,FRAME_SCR0,1)); C.E(stw(TB,FRAME_SCR1,1));
    C.ldCore(); C.E(addi(TB,0,C.arm7?1:0)); C.E(lwz(TC,FRAME_SCR1,1));
    if(!l){ C.E(mr(TD,RA[rd])); C.call((void*)&JitHelp_w16); }
    else if(sh==1){ C.call((void*)&JitHelp_ldrh); C.E(mr(RA[rd],TA)); }
    else if(sh==2){ C.call((void*)&JitHelp_r8);   C.E(extsb(RA[rd],TA)); }
    else          { C.call((void*)&JitHelp_ldrsh); C.E(mr(RA[rd],TA)); }
    const bool wbOK=!(l&&rd==rn);
    if(!p){ if(wbOK){ C.E(lwz(TA,FRAME_SCR0,1)); if(u) C.E(add(RA[rn],RA[rn],TA)); else C.E(subf(RA[rn],TA,RA[rn])); } }
    else if(w&&wbOK){ C.E(lwz(TA,FRAME_SCR1,1)); C.E(mr(RA[rn],TA)); }
    patchSkip(C,si);
    return true;
}

static bool emitMul(Ctx& C,uint32_t op){
    const uint8_t cond=(op>>28)&0xF; if(cond==15) return false;
    const bool s=(op>>20)&1, acc=(op>>21)&1, lng=(op>>23)&1;
    const uint8_t rd=(op>>16)&0xF, rn=(op>>12)&0xF, rs=(op>>8)&0xF, rm=op&0xF;
    if(lng||rd==15||rm==15||rs==15||(acc&&rn==15)) return false;
    C.cyc += 2;
    Skip si = emitCondSkip(C,cond);
    if(acc){ C.E(mullw(TA,RA[rm],RA[rs])); C.E(add(RA[rd],TA,RA[rn])); }
    else C.E(mullw(RA[rd],RA[rm],RA[rs]));
    if(s) setNZ(C,RA[rd]);
    patchSkip(C,si);
    return true;
}

// MRS/MSR (flags-only inline; mode/I/F and SPSR go to the interpreter)
static bool emitMrsMsr(Ctx& C,uint32_t op,uint32_t){
    const uint8_t cond=(op>>28)&0xF; if(cond==15) return false;
    if((op&0x0FBF0FFF)==0x010F0000){                                  // MRS
        if(op&(1u<<22)) return false;
        const uint8_t rd=(op>>12)&0xF; if(rd==15) return false;
        C.cyc += 1;
        Skip si=emitCondSkip(C,cond);
        C.E(mr(RA[rd],RCPSR));
        patchSkip(C,si);
        return true;
    }
    const bool imm=(op>>25)&1;
    if(imm){ if((op&0x0FB0F000)!=0x0320F000) return false; }
    else   { if((op&0x0FB0FFF0)!=0x0120F000) return false; }
    if(op&(1u<<22)) return false;                                     // SPSR
    if(((op>>16)&0xF)!=0x8) return false;                             // flags field only
    C.cyc += 1;
    Skip si=emitCondSkip(C,cond);
    if(imm){
        uint32_t v=op&0xFF, rot=((op>>8)&0xF)*2;
        if(rot) v=(v>>rot)|(v<<(32-rot));
        v&=0xFF000000u;
        C.E(rlwinm(RCPSR,RCPSR,0,8,31));
        C.li(TA,v); C.E(or_(RCPSR,RCPSR,TA));
    } else {
        const uint8_t rm=op&0xF;
        if(rm==15){ patchSkip(C,si); return false; }
        C.E(rlwinm(RCPSR,RCPSR,0,8,31));   // clear the flags byte
        C.E(rlwinm(TA,RA[rm],0,0,7));      // take N,Z,C,V from Rm
        C.E(or_(RCPSR,RCPSR,TA));
    }
    patchSkip(C,si);
    return true;
}

// ============================================================================
//  P7 — coprocessor / CP15
// ============================================================================
static bool emitCoproc(Ctx& C,uint32_t op,uint32_t){
    if(!JitCfg::kCp15) return false;
    const uint8_t cond=(op>>28)&0xF; if(cond==15) return false;
    const uint8_t cp=(op>>8)&0xF, rd=(op>>12)&0xF;
    const bool isRead=((op>>20)&1)!=0;
    if(cp!=15||rd==15) return false;
    C.cyc += 2;
    Skip si=emitCondSkip(C,cond);
    C.ldCore();
    C.E(addi(TB,0,C.arm7?1:0));
    C.li(TC,op);
    C.E(mr(TD,RA[rd]));
    C.call((void*)&JitHelp_cp15);
    if(isRead) C.E(mr(RA[rd],TA));
    patchSkip(C,si);
    return true;
}

// ============================================================================
//  ARM block transfer (LDM/STM), incl. PC in the list
// ============================================================================
static bool emitBlockXfer(Ctx& C,uint32_t op,uint32_t curPC){
    const uint8_t cond=(op>>28)&0xF;
    if(cond==15||((op>>22)&1)) return false;             // S bit -> interpreter
    const uint8_t rn=(op>>16)&0xF;
    const uint16_t list=(uint16_t)(op&0xFFFF);
    if(rn>14||!list) return false;
    const bool load=(op>>20)&1, loadPC=load&&(list&0x8000);
    if(loadPC&&!JitCfg::kPcWrites) return false;
    int n=0; for(int i=0;i<16;i++) if(list&(1u<<i)) n++;
    C.cyc += 1u+(uint32_t)n;

    const bool condPC = loadPC && cond!=14;
    if(condPC) emitSetPCFlag(C,0);
    Skip si=emitCondSkip(C,cond);

    // helper args: core=TA arm7=TB op=TC regs=TD pcForR15=TE pcOut=TF cpsrInOut=TG
    C.ldShadowTo(TS);                                    // TS = shadow (scratch, arg8 free)
    C.E(addi(TD,TS,SH_REGS));
    C.li(TE,curPC+8u); C.E(stw(TE,FRAME_PC,1));
    C.E(addi(TF,TS,SH_PC));
    C.E(addi(TG,TS,SH_CPSR));
    C.ldCore();
    C.E(addi(TB,0,C.arm7?1:0));
    C.li(TC,op);
    C.call((void*)&JitHelp_armBlock);                    // -1 error, 0 ok, 1 wrote PC

    C.E(cmpi(0,TA,0));
    size_t bad=C.sz(); C.E(bc(4,crLT(0),0));             // <0 -> interpreter fallback
    if(loadPC){
        C.E(cmpi(0,TA,1));
        size_t noPC=C.sz(); C.E(bc(12,crEQ(0),0));       // 0 -> no PC write
        if(condPC) emitSetPCFlag(C,1);
        else { emitPCWriteDyn(C); C.done=true; }         // PC + T already in the shadow
        patchOne(C,noPC);
    }
    patchSkip(C,si);
    if(condPC) emitPCFlagGate(C);                        // exits only if PC was written
    patchOne(C,bad);
    emitExitToC(C,loadPC&&!condPC?(curPC+4u):curPC,EXIT_FALLBACK);
    return true;
}
// ============================================================================
//  P12b — inline word load through the fast window (optional, kInlineRam)
//  Window policy in r30 (guest base) / r31 (size-1).  rlwnm provides the
//  rotate a misaligned ARM LDR needs, so no alignment branch is necessary:
//      offset = addr - base ; if (offset > size-1) -> helper
//      host   = *(uint8_t**)(&g_win[cpu].host) + offset
//      rot    = (addr & 3) * 8      -> rlwnm rotate-left by (32-rot)&31
// ============================================================================
static inline uint32_t cmplw(uint8_t cr,uint8_t ra,uint8_t rb){
    return (31u<<26)|((uint32_t)(cr&7u)<<23)|((uint32_t)ra<<16)|((uint32_t)rb<<11)|(32u<<1); }

static bool emitInlineLdr32(Ctx& C,uint8_t addr,uint8_t dst){
    if(!JitCfg::kInlineRam) return false;
    C.E(subf(TD,RWINB,addr));                    // offset candidate
    C.E(cmplw(6,TD,RWINM));                      // CR6.GT => outside window
    size_t slow=C.sz(); C.E(bc(4,crGT(6),0));
    C.li(TF,(uint32_t)(uintptr_t)&g_win[C.cpuIdx].host);
    C.E(lwz(TF,0,TF));
    C.E(add(TF,TF,TD));
    C.E(lwz(TC,0,TF));                           // aligned word
    C.E(rlwinm(TE,addr,0,29,30));                // (addr & 6) in bits 1..2
    C.E(rlwinm(TE,TE,3,0,28));                   // <<3  -> low5 = (addr & 3)*8
    C.E(subfic(TE,TE,32));                       // (32-rot) & 31 for rlwnm
    C.E(rlwnm(dst,TC,TE,0,31));                  // rotate right by rot
    size_t doneSlot=C.sz(); C.E(b(0));
    patchTo(C,slow,C.sz());                      // ---- slow path ----
    C.E(mr(TC,addr));
    C.ldCore(); C.E(addi(TB,0,C.arm7?1:0));
    C.call((void*)&JitHelp_ldr32);
    C.E(mr(dst,TA));
    C.base[doneSlot]=b((intptr_t)((C.sz()-doneSlot)*4));
    return true;
}

// ============================================================================
//  ARM branches
// ============================================================================
static bool emitBX(Ctx& C,uint32_t op,uint32_t){
    const uint8_t cond=(op>>28)&0xF, rm=op&0xF;
    if(rm==15||cond==15) return false;
    C.cyc += 3;
    Skip si=emitCondSkip(C,cond);
    C.E(stw(RA[rm],FRAME_SCR0,1));
    emitBXTarget(C);
    if(si.a!=SIZE_MAX){ patchSkip(C,si); emitExitToC(C,0,EXIT_NORMAL); }   // cond false: fall through
    C.done=true;
    return true;
}
// chaining variant that may also switch instruction sets (Thumb BL/BLX, ARM BLX)
static void emitBodyEndMode(Ctx& C,uint32_t targetPC,bool targetThumb,int reason){
    if(!JitCfg::kChain){ emitExitToC(C,targetPC,reason); return; }
    JitBlock* t=lookupValid(targetPC,C.cpuIdx,targetThumb);
    if(!t){
        size_t site=C.sz(); C.E(b(0));
        size_t stub=C.sz(); emitExitToC(C,targetPC,EXIT_NORMAL);
        C.base[site]=b((intptr_t)((stub-site)*4));
        g_pending.push_back({C.base+site,C.base+stub,targetPC,(uint8_t)C.cpuIdx,targetThumb});
        return;
    }
    C.E(lwz(TB,FRAME_BUDGET,1));
    C.E(addi(TB,TB,-(int16_t)C.cyc));
    C.E(stw(TB,FRAME_BUDGET,1));
    C.E(cmpi(0,TB,0));
    size_t over=C.sz(); C.E(bc(4,crLT(0),0));
    size_t site=C.sz(); C.E(b(0));
    C.base[site]=b((intptr_t)((t->body)-(C.base+site)));
    t->addLink(C.base+site,nullptr);
    patchOne(C,over);
    emitExitToC(C,targetPC,EXIT_BUDGET);
}
static bool emitBranch(Ctx& C,uint32_t op,uint32_t curPC){
    if((op&0x0FFFFFF0)==0x012FFF10) return emitBX(C,op,curPC);
    if((op&0x0FFFFFF0)==0x012FFF30){
        // BLX (register): interworking call.  lr = pc+4 | (T ? 1 : 0) is the
        // ARMv5 rule for a *BLX to ARM*: return address keeps the state bit.
        const uint8_t cond=(op>>28)&0xF, rm=op&0xF;
        if(rm==15||cond==15) return false;
        C.cyc += 3;
        Skip si=emitCondSkip(C,cond);
        C.li(RA[14],curPC+4u);
        C.E(stw(RA[rm],FRAME_SCR0,1));
        emitBXTarget(C);
        if(si.a!=SIZE_MAX){ patchSkip(C,si); emitExitToC(C,curPC+4u,EXIT_NORMAL); }
        C.done=true;
        return true;
    }
    if((op&0x0E000000)!=0x0A000000) return false;
    const uint8_t cond=(op>>28)&0xF;
    if(cond==15) return false;                                 // BLX immediate: interpreter
    const bool lk=(op>>24)&1;
    const int32_t off=((int32_t)(op<<8))>>6;
    const uint32_t tgt=curPC+8u+(uint32_t)off;
    C.cyc += lk?3:2;
    if(cond==14){
        if(lk) C.li(RA[14],curPC+4u);
        emitBodyEnd(C,tgt,EXIT_NORMAL);
        C.done=true;
        return true;
    }
    Skip si=emitCondSkip(C,cond);
    if(lk) C.li(RA[14],curPC+4u);
    emitBodyEndMode(C,tgt,C.thumb,EXIT_NORMAL);
    patchSkip(C,si);
    emitBodyEnd(C,curPC+4u,EXIT_NORMAL);
    C.done=true;
    return true;
}

// ============================================================================
//  Thumb emitters
// ============================================================================
static void emitThumbPushPop(Ctx& C,uint16_t op,uint32_t curPC);
static bool emitT_shifts(Ctx& C,uint16_t op){
    const uint8_t ty=(op>>11)&3, rd=op&7, rs=(op>>3)&7; const int i=(op>>6)&0x1F;
    switch(ty){
        case 0: sLslI(C,RA[rd],RA[rs],i,true); break;
        case 1: sLsrI(C,RA[rd],RA[rs],i?i:32,true); break;
        case 2: sAsrI(C,RA[rd],RA[rs],i?i:32,true); break;
        default: return false;
    }
    setNZ(C,RA[rd]); setC_bit0(C,TC);
    return true;
}
static bool emitT_addSub3(Ctx& C,uint16_t op){
    const uint8_t rd=op&7, rs=(op>>3)&7;
    const bool sub=(op>>9)&1, imm3=(op>>10)&1;
    if(imm3) C.li(TA,(op>>6)&7); else C.E(mr(TA,RA[(op>>6)&7]));
    C.E(mr(TB,RA[rs]));
    if(sub){ C.E(subfc(RA[rd],TA,TB)); setNZ(C,RA[rd]); setC_xer(C); setV_sub(C,RA[rd],TB,TA); }
    else   { C.E(addc (RA[rd],TB,TA)); setNZ(C,RA[rd]); setC_xer(C); setV_add(C,RA[rd],TB,TA); }
    return true;
}
static bool emitT_imm8(Ctx& C,uint16_t op){
    const uint8_t ty=(op>>11)&3, rd=(op>>8)&7; const uint32_t imm=op&0xFF; const uint8_t p=RA[rd];
    switch(ty){
    case 0: C.li(p,imm); setNZ(C,p); return true;
    case 1: C.li(TA,imm); C.E(mr(TB,p)); C.E(subfc(TC,TA,TB)); setNZ(C,TC); setC_xer(C); setV_sub(C,TC,TB,TA); return true;
    case 2: C.li(TA,imm); C.E(mr(TB,p)); C.E(addc (p,TB,TA));  setNZ(C,p);  setC_xer(C); setV_add(C,p,TB,TA);  return true;
    default:C.li(TA,imm); C.E(mr(TB,p)); C.E(subfc(p,TA,TB));  setNZ(C,p);  setC_xer(C); setV_sub(C,p,TB,TA);  return true;
    }
}
static bool emitT_alu(Ctx& C,uint16_t op){
    const uint8_t rd=op&7, rs=(op>>3)&7, o=(op>>6)&0xF;
    const uint8_t d=RA[rd], s=RA[rs];
    switch(o){
    case 0:  C.E(and_(d,d,s)); setNZ(C,d); break;
    case 1:  C.E(xor_(d,d,s)); setNZ(C,d); break;
    case 2:  emitShiftReg(C,0,d,d,s,true); setNZ(C,d); break;
    case 3:  emitShiftReg(C,1,d,d,s,true); setNZ(C,d); break;
    case 4:  emitShiftReg(C,2,d,d,s,true); setNZ(C,d); break;
    case 5:  primeCarry(C); C.E(mr(TB,d)); C.E(adde(d,TB,s));   setNZ(C,d); setC_xer(C); setV_add(C,d,TB,s); break;
    case 6:  primeCarry(C); C.E(mr(TB,d)); C.E(subfe(d,s,TB));  setNZ(C,d); setC_xer(C); setV_sub(C,d,TB,s); break;
    case 7:  emitShiftReg(C,3,d,d,s,true); setNZ(C,d); break;
    case 8:  C.E(and_(TA,d,s)); setNZ(C,TA); break;                       // TST
    case 9:  C.E(addi(TA,0,0)); C.E(subfc(d,s,TA)); setNZ(C,d); setC_xer(C); setV_sub(C,d,TA,s); break;  // NEG
    case 10: C.E(mr(TB,d)); C.E(subfc(TA,s,TB)); setNZ(C,TA); setC_xer(C); setV_sub(C,TA,TB,s); break;   // CMP
    case 11: C.E(mr(TB,d)); C.E(addc (TA,TB,s)); setNZ(C,TA); setC_xer(C); setV_add(C,TA,TB,s); break;   // CMN
    case 12: C.E(or_(d,d,s)); setNZ(C,d); break;
    case 13: C.E(mullw(d,d,s)); setNZ(C,d); break;
    case 14: C.E(andc(d,d,s)); setNZ(C,d); break;
    default: C.E(nor(d,s,s)); setNZ(C,d); break;
    }
    return true;
}
static bool emitT_hiReg(Ctx& C,uint16_t op,uint32_t curPC){
    const uint8_t o=(op>>8)&3, rs=(op>>3)&0xF, rd=(uint8_t)((op&7)|((op>>4)&8));
    if(o==3){                                              // BX / BLX Rm
        const bool link=(op>>7)&1;
        if(link&&C.arm7) return false;                     // BLX (reg) is ARMv5+
        C.cyc += 3;
        if(rs==15) C.li(TA,(curPC+4)&~3u); else C.E(mr(TA,RA[rs]));
        C.E(stw(TA,FRAME_SCR0,1));
        if(link) C.li(RA[14],(curPC+2)|1u);
        emitBXTarget(C); C.done=true; return true;
    }
    if(rd==15){                                            // ADD/MOV pc: stays in Thumb
        if(o==1) return false;
        if(rs==15) C.li(TA,curPC+4); else C.E(mr(TA,RA[rs]));
        if(o==0){ C.li(TB,curPC+4); C.E(add(TA,TB,TA)); }
        C.E(ori(TA,TA,1u));
        C.E(stw(TA,FRAME_SCR0,1));
        emitBXTarget(C); C.done=true; return true;
    }
    if(rs==15) C.li(TA,curPC+4); else C.E(mr(TA,RA[rs]));
    switch(o){
    case 0: C.E(add(RA[rd],RA[rd],TA)); break;
    case 1: C.E(mr(TB,RA[rd])); C.E(subfc(TC,TA,TB)); setNZ(C,TC); setC_xer(C); setV_sub(C,TC,TB,TA); break;
    default:C.E(mr(RA[rd],TA)); break;
    }
    return true;
}
static bool emitT_ldrPc(Ctx& C,uint16_t op,uint32_t curPC){
    const uint8_t rd=(op>>8)&7;
    const uint32_t addr=((curPC+4)&~3u)+((uint32_t)(op&0xFF)<<2);
    C.ldCore(); C.E(addi(TB,0,C.arm7?1:0)); C.li(TC,addr);
    C.call((void*)&JitHelp_ldr32); C.E(mr(RA[rd],TA));
    return true;
}
static bool emitT_memReg(Ctx& C,uint16_t op){
    const uint8_t rd=op&7, rb=(op>>3)&7, ro=(op>>6)&7, k=(op>>9)&7;
    void* fn=nullptr; bool ld=true, sxb=false;
    switch(k){
    case 0: fn=(void*)&JitHelp_w32;   ld=false; break;
    case 1: fn=(void*)&JitHelp_w16;   ld=false; break;
    case 2: fn=(void*)&JitHelp_w8;    ld=false; break;
    case 3: fn=(void*)&JitHelp_r8;    sxb=true;  break;
    case 4: fn=(void*)&JitHelp_ldr32; break;
    case 5: fn=(void*)&JitHelp_ldrh;  break;
    case 6: fn=(void*)&JitHelp_r8;    break;
    default:fn=(void*)&JitHelp_ldrsh; break;
    }
    C.E(add(TC,RA[rb],RA[ro])); C.E(stw(TC,FRAME_SCR0,1));
    C.ldCore(); C.E(addi(TB,0,C.arm7?1:0)); C.E(lwz(TC,FRAME_SCR0,1));
    if(!ld) C.E(mr(TD,RA[rd]));
    C.call(fn);
    if(ld){ if(sxb) C.E(extsb(RA[rd],TA)); else C.E(mr(RA[rd],TA)); }
    return true;
}
static bool emitT_memImm(Ctx& C,uint16_t op){
    const uint8_t rd=op&7, rb=(op>>3)&7; const bool ld=(op>>11)&1;
    const uint8_t h=(op>>12)&0xF; const bool by=(h==7), hw=(h==8);
    const uint32_t off=(uint32_t)((op>>6)&0x1F)*(hw?2u:by?1u:4u);
    C.li(TC,off); C.E(add(TC,RA[rb],TC)); C.E(stw(TC,FRAME_SCR0,1));
    C.ldCore(); C.E(addi(TB,0,C.arm7?1:0)); C.E(lwz(TC,FRAME_SCR0,1));
    if(!ld) C.E(mr(TD,RA[rd]));
    void* fn = ld ? (hw?(void*)&JitHelp_ldrh:by?(void*)&JitHelp_r8:(void*)&JitHelp_ldr32)
                  : (hw?(void*)&JitHelp_w16:by?(void*)&JitHelp_w8:(void*)&JitHelp_w32);
    C.call(fn);
    if(ld) C.E(mr(RA[rd],TA));
    return true;
}
static bool emitT_spLoad(Ctx& C,uint16_t op){
    const bool ld=(op>>11)&1; const uint8_t rd=(op>>8)&7;
    C.li(TA,(uint32_t)(op&0xFF)<<2); C.E(add(TC,RA[13],TA));
    C.E(stw(TC,FRAME_SCR0,1));
    C.ldCore(); C.E(addi(TB,0,C.arm7?1:0)); C.E(lwz(TC,FRAME_SCR0,1));
    if(!ld) C.E(mr(TD,RA[rd]));
    C.call(ld?(void*)&JitHelp_ldr32:(void*)&JitHelp_w32);
    if(ld) C.E(mr(RA[rd],TA));
    return true;
}
static bool emitT_addSpPc(Ctx& C,uint16_t op,uint32_t curPC){
    const uint8_t h=(op>>12)&0xF;
    if(h==0xA){
        const uint8_t rd=(op>>8)&7; const bool sp=(op>>11)&1;
        const uint32_t imm=(uint32_t)(op&0xFF)<<2;
        if(sp){ C.li(TA,imm); C.E(add(RA[rd],RA[13],TA)); }
        else C.li(RA[rd],((curPC+4)&~3u)+imm);
        return true;
    }
    if((op&0xFF00)==0xB000){
        const uint32_t imm=(uint32_t)(op&0x7F)<<2;
        C.li(TA,imm);
        if(op&0x80) C.E(subf(RA[13],TA,RA[13]));
        else        C.E(add(RA[13],RA[13],TA));
        return true;
    }
    return false;
}
static void emitThumbPushPop(Ctx& C,uint16_t op,uint32_t curPC){
    // args: core=TA arm7=TB op=TC regs=TD pcOut=TE cpsr=TF
    C.ldShadowTo(TS);
    C.E(addi(TD,TS,SH_REGS));
    C.E(addi(TE,TS,SH_PC));
    C.E(addi(TF,TS,SH_CPSR));
    C.ldCore(); C.E(addi(TB,0,C.arm7?1:0)); C.li(TC,(uint32_t)op);
    C.call((void*)&JitHelp_thumbPushPop);
    C.E(stw(TA,FRAME_SCR0,1));
    const bool isPop=((op>>11)&1)!=0, hasR=((op>>8)&1)!=0;
    if(isPop&&hasR){
        C.E(lwz(TA,FRAME_SCR0,1)); C.E(cmpi(0,TA,1));
        size_t noPC=C.sz(); C.E(bc(12,crEQ(0),0));
        emitPCWriteDyn(C);                                   // PC + T set by the helper
        C.done=true;
        patchOne(C,noPC);
    }
    (void)curPC;
}
static bool emitT_pushPop(Ctx& C,uint16_t op,uint32_t curPC){
    bool isPop=((op>>11)&1)!=0;
    int n=0; for(int i=0;i<8;i++) if(op&(1u<<i)) n++;
    if(op&0x100) n++;
    C.cyc += 1u+(uint32_t)n;
    bool pcInList = isPop && (op&0x100);
    Skip si=emitCondSkip(C,14);                              // unconditional
    (void)si;
    emitThumbPushPop(C,op,curPC);
    return true;
}
static bool emitT_ldmStm(Ctx& C,uint16_t op){
    int n=0; for(int i=0;i<8;i++) if(op&(1u<<i)) n++;
    C.cyc += 1u+(uint32_t)n;
    C.ldShadowTo(TS);
    C.E(addi(TD,TS,SH_REGS));
    C.ldCore(); C.E(addi(TB,0,C.arm7?1:0)); C.li(TC,(uint32_t)op);
    C.call((void*)&JitHelp_thumbBlock);
    return true;
}
static bool emitT_branch(Ctx& C,uint16_t op,uint32_t curPC){
    const uint8_t h=(op>>12)&0xF;
    if(h==0xE){
        if(((op>>11)&1)!=0) return false;                    // lone BL/BLX suffix
        const int32_t off=((int32_t)(int16_t)(op<<5))>>4;
        C.cyc += 3;
        emitBodyEnd(C,(uint32_t)(curPC+4+off),EXIT_NORMAL);
        C.done=true;
        return true;
    }
    if(h==0xD){
        const uint8_t cond=(op>>8)&0xF;
        if(cond==0xF||cond==0xE) return false;               // SWI / undefined
        const int32_t off=((int32_t)(int8_t)(op&0xFF))<<1;
        C.cyc += 3;
        Skip si=emitCondSkip(C,cond);
        if(si.a==SIZE_MAX) return false;
        emitBodyEndMode(C,(uint32_t)(curPC+4+off),true,EXIT_NORMAL);
        patchSkip(C,si);
        emitBodyEnd(C,curPC+2,EXIT_NORMAL);
        C.done=true;
        return true;
    }
    return false;
}
static bool emitT_bl(Ctx& C,uint16_t op1,uint16_t op2,uint32_t curPC,bool blx){
    const int S=(op1>>10)&1, J1=(op2>>13)&1, J2=(op2>>11)&1;
    const int I1=(J1^S)?0:1, I2=(J2^S)?0:1;                  // P11: ARMv5 J1/J2
    uint32_t off=((uint32_t)(S?0xFF000000u:0))|((uint32_t)I1<<23)|((uint32_t)I2<<22)
                |((uint32_t)(op1&0x3FF)<<12)|((uint32_t)(op2&0x7FF)<<1);
    uint32_t tgt=(uint32_t)(curPC+4u+off);
    C.li(RA[14],(curPC+4u)|1u);                              // Thumb return address
    C.cyc += 4;
    if(blx){
        tgt&=~3u;
        C.E(rlwinm(RCPSR,RCPSR,0,27,25));                    // leave Thumb state
    }
    emitBodyEndMode(C,tgt&~1u,!blx,EXIT_NORMAL);
    C.done=true;
    return true;
}

// ============================================================================
//  Dispatchers + per-instruction cost
// ============================================================================
static uint32_t armCycles(uint32_t op){
    switch((op>>25)&7){
        case 0: return ((op&0x0E000090)==0x00000090)?2u:1u;
        case 1: return 1u;
        case 2: case 3: return 2u;
        case 4: { uint32_t n=0; for(int i=0;i<16;i++) if(op&(1u<<i)) n++; return 1u+n; }
        case 5: { uint32_t n=0; for(int i=0;i<16;i++) if(op&(1u<<i)) n++; return 1u+n; }
        default: return 1u;
    }
}
static uint32_t thumbCycles(uint16_t op){
    switch((op>>12)&0xF){
        case 0x4: return ((op>>11)&1)?2u:1u;
        case 0x5: case 0x6: case 0x7: case 0x8: case 0x9: return 2u;
        case 0xB: return ((op&0xF600)==0xB400)?1u+((op&0x100)?1u:0u):1u;
        case 0xC: return 1u;
        case 0xD: case 0xE: return 3u;
        default: return 1u;
    }
}
static bool dispARM(Ctx& C,uint32_t op,uint32_t curPC){
    const uint8_t cond=(op>>28)&0xF; if(cond==15) return false;
    if((op&0x0F000000)==0x0F000000) return false;                   // SWI -> interpreter
    if((op&0x0FB00FF0)==0x01000090) return false;                   // SWP/SWPB -> interpreter
    if((op&0x0FFFFFF0)==0x012FFF10||(op&0x0FFFFFF0)==0x012FFF30) return emitBranch(C,op,curPC);
    if((op&0x0F900000)==0x01000000||(op&0x0FB00000)==0x03200000) return emitMrsMsr(C,op,curPC);
    if((op&0x0F000000)==0x0E000000) return emitCoproc(C,op,curPC);
    switch((op>>25)&7){
        case 0:
            if((op&0x0FC000F0)==0x00000090) return emitMul(C,op);
            if((op&0x0E000090)==0x00000090) return emitLSExtra(C,op,curPC);
            return emitDP(C,op,curPC);
        case 1: return emitDP(C,op,curPC);
        case 2: case 3: return emitLS(C,op,curPC);
        case 4: return emitBlockXfer(C,op,curPC);
        case 5: return emitBranch(C,op,curPC);
        default: return false;
    }
}
static bool dispThumb(Ctx& C,uint16_t op,uint32_t curPC){
    switch((op>>12)&0xF){
        case 0x0: return emitT_shifts(C,op);
        case 0x1: if(!((op>>11)&1)) return emitT_shifts(C,op);
                  return emitT_addSub3(C,op);
        case 0x2: case 0x3: return emitT_imm8(C,op);
        case 0x4: {
            const uint8_t b=(op>>10)&3;
            if(b==0) return emitT_alu(C,op);
            if(b==1) return emitT_hiReg(C,op,curPC);
            return emitT_ldrPc(C,op,curPC);
        }
        case 0x5: return emitT_memReg(C,op);
        case 0x6: case 0x7: case 0x8: return emitT_memImm(C,op);
        case 0x9: return emitT_spLoad(C,op);
        case 0xA: return emitT_addSpPc(C,op,curPC);
        case 0xB:
            if((op&0xFF00)==0xB000) return emitT_addSpPc(C,op,curPC);
            if((op&0xF600)==0xB400) return emitT_pushPop(C,op,curPC);
            return false;
        case 0xC: return emitT_ldmStm(C,op);
        case 0xD: case 0xE: return emitT_branch(C,op,curPC);
        default: return false;
    }
}
static bool validPC(uint32_t pc,bool gba){
    pc&=~1u;
    if(pc>=0x80000000u&&pc<0xFFFF0000u) return false;
    if(gba){
        return (pc<=0x00003FFFu)||(pc>=0x02000000u&&pc<0x02040000u)||
               (pc>=0x03000000u&&pc<0x03008000u)||(pc>=0x06000000u&&pc<0x06018000u)||
               (pc>=0x08000000u&&pc<0x0E000000u);
    }
    return (pc<0x00008000u)||(pc>=0x01000000u&&pc<0x02000000u)||
           (pc>=0x02000000u&&pc<0x02400000u)||(pc>=0x03000000u&&pc<0x03810000u)||
           (pc>=0x06000000u&&pc<0x07000000u)||(pc>=0x08000000u&&pc<0x0A000000u)||
           (pc>=0xFFFF0000u);
}

// ============================================================================
//  Prologue + compile
// ============================================================================
static void emitReload(Ctx& C);            // defined above? -> forward
static void emitPrologue(Ctx& C){
    C.E(mflr(0));
    C.E(stwu(1,-(int16_t)FRAME_SIZE,1));
    C.E(stw(0,FRAME_LR_OFF,1));
    for(int r=14;r<=31;r++) C.E(stw(r,FRAME_SAVE+(r-14)*4,1));
    C.li(TA,(uint32_t)(uintptr_t)&g_state[C.cpuIdx]); C.E(stw(TA,FRAME_SHADOW,1));
    C.li(TB,(uint32_t)(uintptr_t)C.core);            C.E(stw(TB,FRAME_CORE,1));
    C.li(TB,(uint32_t)(uintptr_t)C.interp);          C.E(stw(TB,FRAME_INTERP,1));
    C.E(addi(TB,0,(int16_t)C.cpuIdx));               C.E(stw(TB,FRAME_CPUIDX,1));
    C.E(lwz(TB,SH_BUDGET,TA));                       C.E(stw(TB,FRAME_BUDGET,1));
    size_t validCheck=SIZE_MAX;
    if(JitCfg::kSafeEntry){                          // I5: defensive, cheap, once per C trip
        C.E(lwz(TB,SH_VALID,TA)); C.E(cmpi(0,TB,0));
        validCheck=C.sz(); C.E(bc(4,crEQ(0),0));     // valid!=0 -> reload
        C.li(TC,(uint32_t)(uintptr_t)C.interp); C.E(mr(TD,TA));
        C.call((void*)&JitHelp_takeOver);
    }
    size_t reloadAt=C.sz();
    patchOne(C,validCheck);
    emitReloadFromShadow(C);
    (void)reloadAt;
}
static JitBlock* compile(Interpreter* interp,Core* core,uint32_t armPC,bool arm7,int cpuIdx){
    if(!codeBuf||!g_live||!interp||!core) return nullptr;
    if(!interp->isReady()) return nullptr;
    if(!validPC(armPC,core->gbaMode)) return nullptr;
    const bool thumb=interp->isThumb();

    JitBlock* cached=lookupValid(armPC,cpuIdx,thumb);
    if(cached) return cached;

    const size_t bkt=hashPC(armPC,cpuIdx);
    JitBlock& slot=cache[bkt];
    if(slot.valid){ pageSub(slot.armPC,slot.endPC); unlinkIncoming(&slot); slot.valid=false; }
    if(codePos+4096>=JIT_WORDS) flushJitCache();            // codeBuf reset -> recompute base

    Ctx C;
    C.base=codeBuf+codePos; C.cur=C.base;
    C.cap=JIT_WORDS-codePos; if(C.cap>4096) C.cap=4096;
    C.thumb=thumb; C.arm7=arm7; C.blockPC=armPC;
    C.cpuIdx=cpuIdx; C.interp=interp; C.core=core;

    emitPrologue(C);
    uint32_t* const bodyPtr=C.cur;                          // chained entry point
    uint32_t curPC=armPC; int n=0;
    while(n<JitCfg::kBlkInsnsMax&&!C.done&&!C.overflow){
        if(C.rem()<512){ C.done=true; emitExitToC(C,curPC,EXIT_NORMAL); break; }
        if(!validPC(curPC,core->gbaMode)){ C.done=true; emitExitToC(C,curPC,EXIT_FALLBACK); break; }
        if(thumb){
            uint16_t op=core->memory.read<uint16_t>(arm7,curPC);
            if(((op>>11)&0x1F)==0x1E){                      // BL / BLX prefix
                if(!validPC(curPC+2,core->gbaMode)){ C.done=true; emitExitToC(C,curPC,EXIT_FALLBACK); break; }
                uint16_t op2=core->memory.read<uint16_t>(arm7,curPC+2);
                const uint8_t bb=(op2>>11)&0x1F;
                if(bb==0x1F||(bb==0x1D&&!arm7)){            // P11: 0x1D == BLX
                    emitT_bl(C,op,op2,curPC,bb==0x1D);
                    curPC+=4; n+=2; C.insnCount+=2;
                    continue;
                }
            }
            if(!dispThumb(C,op,curPC)){
                JHOT("[JIT] thumb FB cpu%d pc=%08X op=%04X\n",cpuIdx,curPC,(unsigned)op);
                C.done=true; emitExitToC(C,curPC,EXIT_FALLBACK);
            } else { C.cyc+=thumbCycles(op); curPC+=2; n++; C.insnCount++; }
        } else {
            uint32_t op=core->memory.read<uint32_t>(arm7,curPC);
            if(!dispARM(C,op,curPC)){
                JHOT("[JIT] arm   FB cpu%d pc=%08X op=%08X\n",cpuIdx,curPC,op);
                C.done=true; emitExitToC(C,curPC,EXIT_FALLBACK);
            } else {
                C.cyc+=armCycles(op); curPC+=4; n++; C.insnCount++;
            }
        }
    }
    if(!C.done&&!C.overflow) emitBodyEnd(C,curPC,EXIT_NORMAL);

    if(C.overflow||C.sz()<24){
        g_stats.compileFail++;
        JHOT("[JIT] compile FAIL pc=%08X overflow=%d sz=%u\n",armPC,(int)C.overflow,(unsigned)C.sz());
        return nullptr;
    }
    if(C.base[C.sz()-1]!=0x4E800020u){                       // must end in blr
        g_stats.compileFail++;
        JHOT("[JIT] compile no-BLR pc=%08X\n",armPC);
        return nullptr;
    }
    const size_t wds=C.sz();
    flushICache(C.base,wds);

    slot.armPC=armPC; slot.endPC=curPC>armPC?curPC:armPC+(thumb?2u:4u);
    slot.code=C.base; slot.body=bodyPtr; slot.nW=(uint32_t)wds;
    slot.gen=cacheGen; slot.thumb=thumb; slot.cpu=(uint8_t)cpuIdx;
    slot.insnCount=(uint32_t)C.insnCount; slot.cycles=C.cyc;
    slot.linkCount=0; slot.valid=true;
    slot.sig=JitHelp_sigBlock(core,arm7?1:0,armPC,thumb?1u:0u,(uint32_t)C.insnCount);
    pageAdd(slot.armPC,slot.endPC);
    codePos+=wds;
    g_stats.blocksLive++;
    resolvePendingFor(&slot);
    return &slot;
}
// ============================================================================
//  Scheduler access (P9).  Both shapes are supported; set JIT_HAVE_FIXED_SCHED
//  to 1 once you apply the fixed-array companion patch (see the bottom).
// ============================================================================
#ifndef JIT_HAVE_FIXED_SCHED
#define JIT_HAVE_FIXED_SCHED 0
#endif

static inline uint32_t nextEventCycles(Core& core){
#if JIT_HAVE_FIXED_SCHED
    return core.evCount? core.events[0].cycles : 0xFFFFFFFFu;
#else
    return core.events.empty()? 0xFFFFFFFFu : core.events.front().cycles;
#endif
}
static inline void runDueTasks(Core& core){
#if JIT_HAVE_FIXED_SCHED
    while(core.evCount && core.events[0].cycles<=core.globalCycles){
        const SchedTask t=core.events[0].task;
        --core.evCount;
        for(int i=0;i<core.evCount;i++) core.events[i]=core.events[i+1];
        if(core.tasks[t].fn) core.tasks[t]();
    }
#else
    while(!core.events.empty() && core.events.front().cycles<=core.globalCycles){
        const SchedTask t=core.events.front().task;
        core.events.erase(core.events.begin());
        if(core.tasks[t].fn) core.tasks[t]();
    }
#endif
}
// P9: the chain budget *is* the interrupt deadline.  A chain can therefore
// never delay a scanline/SPU/timer task or an ARMx_INTERRUPT delivery beyond
// the same bound the interpreter had — this is what makes chaining safe.
static uint32_t chainBudget(Core& core,int cpu){
    const uint32_t due=nextEventCycles(core);
    if(due==0xFFFFFFFFu) return 512u;
    uint32_t d=(due>core.globalCycles)?(due-core.globalCycles):64u;
    if(cpu==1) d>>=1;                       // ARM7 time runs at half rate globally
    if(d<64u) d=64u;
    if(d>4096u) d=4096u;
    return d;
}

static inline uint32_t interpStep(Interpreter& in){
    const int c=in.jitRunOpcode();
    return c>0?(uint32_t)c:1u;
}
static inline uint32_t interpStep(Core& core,int cpu){
    // The interpreter executes exactly one instruction, so the shadow is dead
    // afterwards: hand the state over first, drop ownership after (I5).
    Interpreter& in=core.interpreter[cpu];
    JitCpuState& sh=g_state[cpu];
    JitHelp_release(&in,&sh);
    const uint32_t c=interpStep(in);
    sh.valid=0;
    return c;
}

static uint32_t runCpu(Core& core,int cpu,bool gba){
    Interpreter& interp=core.interpreter[cpu];
    JitCpuState& sh=g_state[cpu];
    if(interp.halted) return 0;
    const bool arm7=(cpu==1);

    if(!sh.valid) JitHelp_takeOver(&interp,&sh);
    if(!sh.valid) return interpStep(core,cpu);            // interpreter owns state

    const uint32_t pc=sh.pendingPC;
    if(!validPC(pc,gba)){
        g_stats.badPC[cpu]++;
        JHOT("[JIT] cpu%d PC out of range %08X -> interpreter\n",cpu,pc);
        return interpStep(core,cpu);
    }
    if(badPCBit(cpu,pc)) return interpStep(core,cpu);     // P13: no recompile churn

    JitBlock* b=compile(&interp,&core,pc,arm7,cpu);
    if(!b||!b->insnCount){ markBadPC(cpu,pc); return interpStep(core,cpu); }

    if(JitCfg::kVerifyBlocks &&                                    // P10
       JitHelp_sigBlock(&core,arm7?1:0,pc,b->thumb?1u:0u,b->insnCount)!=b->sig){
        g_stats.stale[cpu]++;
        JHOT("[JIT] STALE cpu%d pc=%08X -> invalidate\n",cpu,pc);
        invalidateJitRange(pc,pc+0x1000u);
        return interpStep(core,cpu);
    }

    sh.cycles     = 0;
    sh.reason     = EXIT_NORMAL;
    sh.pendingPC  = pc;
    sh.budget     = chainBudget(core,cpu);
    g_stats.cTrips[cpu]++;

    executeBlock_asm(b->code);

    uint32_t cyc=sh.cycles;
    g_stats.guestCycles[cpu]+=cyc;
    if(cyc==0) cyc=1u;

    if(sh.reason==EXIT_FALLBACK){
        // The block committed at the instruction it could not emit, so that one
        // instruction is what the interpreter must retire next (I6).
        g_stats.insnInterp[cpu]++;
        markBadPC(cpu,sh.pendingPC);                     // never re-attempt compiling here
        return cyc+interpStep(core,cpu);
    }
    g_stats.insnJit[cpu]+=b->insnCount;
    if(sh.reason<=3) g_stats.exits[sh.reason]++;

    if(JitCfg::kStormDump){                              // P10
        static uint32_t lastPC[2]={~0u,~0u};
        static uint16_t same[2]={0,0};
        static bool     said[2]={false,false};
        const uint32_t after=sh.pendingPC;
        if(after==lastPC[cpu]){
            if(++same[cpu]==64 && !said[cpu]){
                said[cpu]=true; g_stats.storm[cpu]++;
                ringAdd("[JIT] STORM cpu%d pc=%08X cpsr=%08X T=%d ime=%u ie=%08X irf=%08X\n",
                        cpu,after,sh.cpsr,(int)((sh.cpsr>>5)&1u),
                        interp.readIme(),interp.readIe(),interp.readIrf());
                ringAdd("[JIT] STORM pcReg=%08X halted=%d exit=%d\n",
                        interp.getPC(),(int)interp.halted,sh.reason);
            }
        } else { lastPC[cpu]=after; same[cpu]=0; said[cpu]=false; }
    }
    return cyc;
}

} // namespace (anon)

// ============================================================================
//  Public API
// ============================================================================
void invalidateJitRange(uint32_t start,uint32_t end){
    if(!g_live||end<=start) return;
    bool hit=false;
    for(uint32_t p=start&~0xFFFu;p<end&&!hit;p+=0x1000u){
        if(g_pageBlocks[pageIdx(p)]) hit=true;
        if(p>0xFFFFF000u) break;
    }
    if(!hit) return;
    for(size_t i=0;i<CSIZ;i++){
        JitBlock& b=cache[i];
        if(b.valid&&b.armPC<end&&b.endPC>start){
            pageSub(b.armPC,b.endPC);
            unlinkIncoming(&b);
            b.valid=false;
            if(g_stats.blocksLive) g_stats.blocksLive--;
        }
    }
    // pending links whose target just died must not dangle: point them at their
    // own stub, which exits to C with the intended PC.
    for(size_t i=0;i<g_pending.size();){
        PendingLink& l=g_pending[i];
        if(l.targetPC>=start&&l.targetPC<end){
            *l.site = b((intptr_t)(l.stub-l.site));
            g_pending[i]=g_pending.back(); g_pending.pop_back();
        } else ++i;
    }
    for(uint32_t a=start;a<end;a+=4) markBadPC(0,a), markBadPC(1,a);   // re-examine code
}

void flushJitCache(){
    // Note: invalidating everything is correct but blunt (2 MB..4 MB of code).
    // Prefer invalidateJitRange() where you know the affected pages; this is the
    // safe default for CP15 control/cache writes, map changes and loadStates.
    codePos=0; ++cacheGen;
    for(size_t i=0;i<CSIZ;i++){ cache[i].valid=false; cache[i].linkCount=0; }
    memset(g_pageBlocks,0,sizeof g_pageBlocks);
    memset(g_badPC,0,sizeof g_badPC);
    g_pending.clear();
    g_stats.blocksLive=0;
    // The shadow may describe memory attributes that no longer exist: drop
    // ownership so the next block entry re-reads the interpreter (I5).
    g_state[0].valid=0; g_state[1].valid=0;
}
// P12: Memory::updateMap9/updateMap7 must call this (see companion patch).
// Both the window policy registers and the helper fast path read it, and the
// values are baked into emitted blocks, so a call here must be followed by a
// flush (which updateMap* does anyway when the map changes).
void setMemWindow(int cpu,uint8_t* host,uint32_t base,uint32_t size){
    if(cpu<0||cpu>1) return;
    g_win[cpu].host=host;
    g_win[cpu].base=base;
    g_win[cpu].mask=size?size-1u:0u;
    g_state[cpu].fastBase=base;
    g_state[cpu].fastMask=g_win[cpu].mask;
}
// I5: any C code that mutates the Interpreter directly must say so.
void invalidateInterpreterView(int cpu){
    if(cpu<0||cpu>1) return;
    g_state[cpu].valid=0;
}
void invalidateInterpreterViewAll(){ invalidateInterpreterView(0); invalidateInterpreterView(1); }

void flushLog(){
    if(!g_ringLen) return;
#if JIT_LOG_ENABLE
    for(size_t i=0;i<g_ringLen;i+=1024){
        char tmp[1025]; size_t n=g_ringLen-i; if(n>1024) n=1024;
        memcpy(tmp,g_ring+i,n); tmp[n]=0; DebugLog("%s",tmp);
    }
#else
    (void)g_ring;
#endif
    if(g_ringDrops){
#if JIT_LOG_ENABLE
        DebugLog("[JIT] log ring dropped %u lines\n",g_ringDrops);
#endif
        g_ringDrops=0;
    }
    g_ringLen=0;
}
const void* getStats(){ return &g_stats; }

// ============================================================================
//  Run functions — same contract as Interpreter::runCoreNds / runCoreSingle:
//  run until core.running is cleared, then return.
// ============================================================================
void runJitNds(Core& core){
    if(!g_live||!codeBuf){ Interpreter::runCoreNds(core); return; }

    Interpreter& arm9=core.interpreter[0];
    Interpreter& arm7=core.interpreter[1];
    const bool dsi=core.dsiMode;
    uint32_t t9=core.globalCycles, t7=core.globalCycles, dsiOdd=0;

    core.running=1;
    while(core.running){
        uint32_t due=nextEventCycles(core);
        bool early=false;
        while(core.globalCycles<due){
            bool any=false;
            if(!arm9.halted){
                any=true;
                if(core.globalCycles>=t9){
                    uint32_t c=runCpu(core,0,false);
                    if(dsi){ c+=dsiOdd; dsiOdd=c&1u; c>>=1; }
                    t9=core.globalCycles+c;
                }
            }
            if(!arm7.halted){
                any=true;
                if(core.globalCycles>=t7) t7=core.globalCycles+(runCpu(core,1,false)<<1);
            }
            if(!any){ core.globalCycles=due; break; }        // both halted
            const uint32_t n9=arm9.halted?0xFFFFFFFFu:t9;
            const uint32_t n7=arm7.halted?0xFFFFFFFFu:t7;
            const uint32_t next=(n9<n7)?n9:n7;
            if(next>core.globalCycles) core.globalCycles=next;
            if(!core.running){ early=true; break; }          // updateRun() inside a block
        }
        if(early) break;
        if(due==0xFFFFFFFFu) break;
        core.globalCycles=due;
        runDueTasks(core);
        // RESET_CYCLES rebased everything: rebase our local targets too.
        if(core.globalCycles<due){
            const uint32_t d=due-core.globalCycles;
            t9=(t9>d)?t9-d:0u;
            t7=(t7>d)?t7-d:0u;
        }
    }
}

void runJitGba(Core& core){
    if(!g_live||!codeBuf){ Interpreter::runCoreSingle<true,0>(core); return; }
    Interpreter& cpu=core.interpreter[1];

    core.running=1;
    while(core.running){
        const uint32_t due=nextEventCycles(core);
        bool early=false;
        while(!cpu.halted && core.globalCycles<due){
            core.globalCycles+=runCpu(core,1,true);
            if(!core.running){ early=true; break; }
        }
        if(early) break;
        if(due==0xFFFFFFFFu) break;
        core.globalCycles=due;
        runDueTasks(core);
    }
}

// ============================================================================
//  Init / shutdown
// ============================================================================
bool initJit(Core* core){
    g_live=false; codeBuf=nullptr;
    void* raw=memalign(32,JIT_BYTES);
    if(!raw){ printf("[JIT] memalign failed\n"); return false; }
    uintptr_t addr=(uintptr_t)raw;
    bool ok=(addr>=0x80000000u && addr+JIT_BYTES<=0x81800000u);
    if(!ok && addr<0x01800000u){ addr|=0x80000000u; ok=(addr+JIT_BYTES<=0x81800000u); }
    else if(!ok && addr>=0xC0000000u && addr<0xC1800000u){ addr-=0x40000000u; ok=(addr+JIT_BYTES<=0x81800000u); }
    if(!ok){ printf("[JIT] buffer not in MEM1: %p\n",raw); free(raw); return false; }
    uintptr_t tr=(uintptr_t)(void*)executeBlock_asm;
    if(tr<0x80000000u||tr>=0x81800000u){ printf("[JIT] bad trampoline %p\n",(void*)tr); free(raw); return false; }

    codeBuf=(uint32_t*)addr; codePos=0; cacheGen=1;
    memset(codeBuf,0,JIT_BYTES);
    DCFlushRange(codeBuf,JIT_BYTES); ICInvalidateRange(codeBuf,JIT_BYTES);
    flushJitCache();
    memset(&g_stats,0,sizeof g_stats);
    g_ringLen=0; g_ringDrops=0;
    for(int i=0;i<2;i++){
        memset(&g_state[i],0,sizeof g_state[i]);
        g_state[i].valid=0;
        g_state[i].reason=EXIT_FALLBACK;
        g_state[i].budget=512;
    }
    g_live=true;
    printf("[JIT] ready buf=%p (%uKB) tramp=%p chain=%d shadow=%d inline=%d\n",
           (void*)codeBuf,(unsigned)(JIT_BYTES>>10),(void*)tr,
           (int)JitCfg::kChain,(int)JitCfg::kShadow,(int)JitCfg::kInlineCond);
    if(core) core->setRunFunc(core->gbaMode?runJitGba:runJitNds);
    return true;
}
void shutdownJit(Core* core){
    g_live=false;
    if(core) core->setRunFunc(core->gbaMode
        ? static_cast<void(*)(Core&)>(&Interpreter::runCoreSingle<true,0>)
        : &Interpreter::runCoreNds);
    codeBuf=nullptr; codePos=0;
    for(size_t i=0;i<CSIZ;i++){ cache[i].valid=false; cache[i].linkCount=0; }
    memset(g_pageBlocks,0,sizeof g_pageBlocks);
    g_pending.clear();
    invalidateInterpreterViewAll();
}
} // namespace JitPpc

/* ============================================================================
 *  COMPANION PATCHES — the only edits needed outside this file
 * ============================================================================
 *
 * 1) jit_ppc.h — add:
 *      namespace JitPpc {
 *          void setMemWindow(int cpu, uint8_t* host, uint32_t base, uint32_t size);
 *          void invalidateInterpreterView(int cpu);
 *          void invalidateInterpreterViewAll();
 *          void flushLog();
 *          const void* getStats();
 *      }
 *
 * 2) cp15.cpp — P7b, the half that stops stale blocks and IRQ livelocks:
 *      void Cp15::write(int op1, int crn, int crm, uint32_t value){
 *          switch (crn) {
 *              case 1: case 2: case 3: case 5: case 6: case 9:   // control, TTB,
 *              case 7:                                          // DAC, PU, TCM,
 *                  JitPpc::flushJitCache();                     // cache maint.
 *                  break;
 *              default: break;
 *          }
 *          ...existing body...
 *      }
 *    Rationale: MCR p15,0,Rt,c7,c5,0 is the guest saying "I just wrote code"
 *    (that is exactly the op right before your IRQ storm's SWI stub).
 *
 * 3) memory.cpp — P12 window + invalidation coverage:
 *      // in Memory::updateMap9 / updateMap7, after rebuilding the map:
 *      JitPpc::setMemWindow(0, mainRamHostPtr9, 0x02000000u, 0x00400000u);
 *      JitPpc::setMemWindow(1, wramHostPtr7, 0x03000000u, 0x00800000u);
 *      JitPpc::flushJitCache();          // the window is baked into blocks (I7)
 *      // and in every guest-visible write path that is not already covered:
 *      Core::invalidateJitPage(addr);    // DMA, ITCM/DTCM, cart writes,
 *                                        // HLE/BIOS patching, save loading
 *
 * 4) core.cpp — logging + shadow ownership hygiene:
 *      void Core::endFrame(){ ... JitPpc::flushLog(); ... }        // P10
 *      void Core::enterGbaMode(){ ... JitPpc::invalidateInterpreterViewAll();
 *                                      JitPpc::flushJitCache(); ... }
 *      void Core::loadState(FILE* f){ ... JitPpc::invalidateInterpreterViewAll(); }
 *      // also anywhere else C mutates Interpreter directly (HLE, directBoot).
 *
 * 5) P9 optional (removes std::vector traffic from the hot loop):
 *      in core.h:  static const size_t MAX_EVENTS = MAX_TASKS + 4;
 *                  SchedEvent events[MAX_EVENTS]; int evCount = 0;
 *      Core::schedule(): shift-insert keeping insertion order for ties;
 *      runDueTasks():    head-pop, bounded by evCount;
 *      saveState/loadState: count + first evCount entries.
 *      Then build with -DJIT_HAVE_FIXED_SCHED=1.
 *
 * 6) P10 signature verification: build with kVerifyBlocks=true and play for a
 *    minute.  Any [JIT] STALE line names a PC whose block survived a guest code
 *    write, i.e. a hole in (3).  Zero STALE lines = coverage is complete.
 *
 * 7) Arm7 HLE: hleArm7 runs guest ARM7 code; it must call
 *    JitPpc::invalidateInterpreterView(1) whenever it touches Interpreter[1].
 *
 * HOW TO READ THE GENERATED CODE (when it misbehaves)
 *    - entry from C: [prologue][valid check -> takeOver][reload][body]... [exit]
 *    - entry from a chain: jumps straight to a body label; only the head of the
 *      chain has a frame, and only the head's epilogue returns.
 *    - every body ends in one of: link `b`, budget bb, exit-to-C (stores the
 *      whole shadow and blr).
 *    - a conditional PC write compiles to: flag=0, skip-over-body, body,
 *      writeback, flag test (outside the skip), exit-or-continue.
 * ============================================================================ */
