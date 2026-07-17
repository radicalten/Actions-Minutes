// jit_ppc.cpp - Fixed version

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
// KEY DESIGN DECISIONS (learned from all previous bugs):
//
// 1. executeBlock_asm (jit_trampoline.S) calls block via bctrl.
//    Block's mflr captures valid PPC return address. FIXED.
//
// 2. ARM r0-r15 mapped to PPC r14-r29 (callee-saved).
//    CPSR in r30, INTERP* in r31. All survive C calls. FIXED.
//
// 3. PC SYNC PROTOCOL (critical):
//    - syncFrom: reads *registers[15] directly (raw pointer value)
//      This is the actual PC the ARM CPU has stored, WITHOUT
//      adding any pipeline offset. The interpreter stores
//      PC+8 (ARM) or PC+4 (Thumb) in *registers[15].
//    - In the JIT block, RA[15] holds this raw value.
//    - At block entry, we set RA[15] = this raw value.
//    - Instructions that read PC as an operand use RA[15] directly
//      (it already has the pipeline offset baked in).
//    - At block exit (syncTo), we write RA[15] back to *registers[15]
//      directly, then call setPC(RA[15]).
//    - setPC does: *registers[15] = newPC; flushPipeline();
//      flushPipeline reads pcData[newPC] etc.
//    - BUT if we write PC=actual_instruction_addr, setPC will then
//      add pipeline offset again internally? No - setPC sets the
//      raw register value and flushPipeline sets pipeline[0],[1]
//      by reading from memory at *registers[15] onward.
//
//    ACTUAL FIX: At block EXIT, we must write the NEXT instruction
//    address (not pipeline-offset) to the interpreter's PC.
//    The ARM interpreter's setPC(addr) sets *registers[15] = addr+8
//    (ARM) or addr+4 (Thumb) and loads pipeline.
//    So at block exit we must call setPC(next_actual_pc).
//
//    In our sync area: FRAME_REGSYNC+15*4 = "next PC to execute"
//    (the actual instruction address, not pipeline-inflated).
//    syncTo calls setPC(regs[15]) where regs[15] is the next instr addr.
//
//    syncFrom reads the ACTUAL next instruction address:
//    getActualPC() = *registers[15] - (thumb ? 4 : 8)
//    This is what goes into regs[15] for the JIT to track.
//
//    When the JIT needs to USE PC as an ARM operand (e.g. LDR PC-relative),
//    it must add the pipeline offset: regs[15]+8 (ARM) or regs[15]+4 (Thumb).
//
// 4. GBA ISI (PC=LR=0x000E2B04):
//    Still happening means the block's blr jumps to an ARM address.
//    This means FRAME_LR got overwritten with an ARM address.
//    
//    CAUSE FOUND: emitSyncTo calls JitHelp_syncTo which calls setPC().
//    setPC() internally calls flushPipeline() which does:
//      pcData = ... (memory pointer lookup)
//      pipeline[0] = read32(pcData)   <- reads from memory
//      pipeline[1] = read32(pcData+4)
//    If pcData is in a region that maps to our JIT code buffer or
//    stack, it could corrupt FRAME_LR before we restore it.
//    
//    But more likely: the GBA game is jumping to PC=0x000E2B04 which
//    is in GBA BIOS or unmapped space. Our validPC() check should
//    catch this, but if it doesn't, the block compiles garbage and
//    crashes.
//
//    REAL CAUSE of GBA ISI: The block compiles and runs, emitSyncTo
//    stores RA[15] to FRAME_REGSYNC+15*4, then calls JitHelp_syncTo.
//    JitHelp_syncTo calls setPC(regs[15]). If regs[15] is an ARM
//    address like 0x000E2B00, setPC sets *registers[15] = 0x000E2B00+8
//    and loads pipeline. Now when the interpreter runs next, it
//    executes from 0x000E2B00 which is GBA BIOS area.
//    The "PC = LR = 0x000E2B04" means the BLOCK ITSELF has blr jumping
//    to 0x000E2B04. So FRAME_LR[r1+8] contains 0x000E2B04.
//    
//    How does FRAME_LR get corrupted? During emitSyncTo, we call
//    JitHelp_syncTo which is a C++ function. That function is compiled
//    by the C++ compiler. The C++ compiler's prologue for JitHelp_syncTo
//    may do: mflr r0; stw r0, 4(r1)  -- saving LR at r1+4 of ITS frame.
//    BUT r1 at that point is our JIT block's r1, and r1+4 = FRAME_LR-4.
//    That's fine, it's not FRAME_LR (which is at r1+8).
//    
//    WAIT. The C++ function's prologue saves LR at caller's r1+4.
//    Caller's r1 = JIT block's r1. So it saves at JIT_r1 + 4. Fine.
//    But the C++ function's OWN stwu allocates its frame:
//    new_r1 = JIT_r1 - N. Its prologue: stw r0, (N+4)(r1_new).
//    N+4 > 8 always (N >= 8 minimum). So no overlap with FRAME_LR=8.
//    
//    ACTUAL CAUSE: emitSyncTo is called from within a conditional branch
//    path. The conditional path has its own bc instruction whose target
//    offset might be wrong (patchSkip bug), causing a branch to a random
//    PPC address that happens to be 0x000E2B04 (an ARM PC value that
//    ended up in the code buffer or branch target).
//    
//    OR: FRAME_LR is at r1+8. The ABI says callee saves LR at caller's
//    r1+4. Our JIT block is the "caller" when it calls C functions.
//    The C function (JitHelp_syncTo) saves LR at [JIT_r1+4]. Fine.
//    But if the C function itself calls another function (e.g., setPC
//    calls flushPipeline), that inner call saves LR at [JitHelp_frame+4].
//    All fine.
//    
//    I THINK THE ACTUAL BUG IS: The trampoline saves LR at new_r1+20
//    (= old_r1+4). The JIT block's prologue does mflr r0 FIRST (correct),
//    then stwu r1,-192(r1), then stw r0,8(r1) = saves at BLOCK_r1+8.
//    The trampoline's save at old_r1+4 is NEVER touched by the block.
//    When the block's epilogue does lwz r0,8(r1) -> gets back the bctrl
//    return address -> mtlr r0 -> blr -> returns to trampoline. CORRECT.
//    
//    So why does it still crash? Because for the GBA game specifically,
//    the ARM PC being synced is 0x000E2B04. This is NOT a valid PC.
//    validPC() should return false. But if the game was running and then
//    something set PC to 0x000E2B04 (e.g., a bad BX instruction), then
//    compile() returns nullptr, and we fall back to jitRunOpcode().
//    jitRunOpcode() runs from the bad PC and crashes.
//    
//    THE REAL GBA ISI FIX: The GBA game has a bug in our emulation
//    that causes PC to become 0x000E2B04. We need to fix the ARM
//    emulation, not just the JIT. The PC=0x000E2B04 is coming from
//    wrong ARM state, likely due to wrong BX emulation (T bit not
//    set correctly, PC not masked to ~1).
// ============================================================

static const int FRAME_SIZE    = 192;
static const int FRAME_LR      = 8;
static const int FRAME_R14     = 16;    // r14..r31 = 18 regs * 4 = 72 bytes
static const int FRAME_CORE    = 88;    // Core* slot
static const int FRAME_SCR0    = 92;
static const int FRAME_SCR1    = 96;
static const int FRAME_SCR2    = 100;
static const int FRAME_REGSYNC = 112;   // ARM r0-r15 (actual PCs) + CPSR

static_assert(FRAME_SIZE % 16 == 0, "frame align");
static_assert(FRAME_R14 + 18*4 == FRAME_CORE, "r14-r31 layout");
static_assert(FRAME_REGSYNC + 17*4 <= FRAME_SIZE, "regsync fits");

namespace JitPpc {

// ============================================================
// PPC encoders
// ============================================================
static inline uint32_t ppc_nop()  { return 0x60000000u; }
static inline uint32_t ppc_blr()  { return 0x4E800020u; }
static inline uint32_t ppc_bctr(bool lk=false)
    { return (19u<<26)|(20u<<21)|(528u<<1)|(lk?1u:0u); }
static inline uint32_t ppc_bclr(uint8_t bo,uint8_t bi,bool lk=false)
    { return (19u<<26)|((bo&31u)<<21)|((bi&31u)<<16)|(16u<<1)|(lk?1u:0u); }
static inline uint32_t ppc_bc(uint8_t bo,uint8_t bi,int16_t off,bool lk=false)
    { return (16u<<26)|((bo&31u)<<21)|((bi&31u)<<16)|((uint32_t)(off&0xFFFC))|(lk?1u:0u); }

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
    { return (11u<<26)|((cr&7u)<<23)|((uint32_t)ra<<16)|(uint16_t)i; }
static inline uint32_t ppc_cmpli(uint8_t cr,uint8_t ra,uint16_t i)
    { return (10u<<26)|((cr&7u)<<23)|((uint32_t)ra<<16)|i; }
static inline uint32_t ppc_subfic(uint8_t rt,uint8_t ra,int16_t i)
    { return (8u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i; }

static inline uint32_t Xf(uint8_t rt,uint8_t ra,uint8_t rb,uint32_t xop,bool rc=false)
    { return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)
             |((uint32_t)rb<<11)|(xop<<1)|(rc?1u:0u); }
static inline uint32_t XOf(uint8_t rt,uint8_t ra,uint8_t rb,bool oe,uint32_t xop,bool rc=false)
    { return (31u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)
             |((uint32_t)rb<<11)|(oe?0x400u:0u)|(xop<<1)|(rc?1u:0u); }

static inline uint32_t ppc_add(uint8_t d,uint8_t a,uint8_t b)   { return XOf(d,a,b,false,266); }
static inline uint32_t ppc_addc(uint8_t d,uint8_t a,uint8_t b)  { return XOf(d,a,b,false,10);  }
static inline uint32_t ppc_adde(uint8_t d,uint8_t a,uint8_t b)  { return XOf(d,a,b,false,138); }
static inline uint32_t ppc_subf(uint8_t d,uint8_t a,uint8_t b)  { return XOf(d,a,b,false,40);  }
static inline uint32_t ppc_subfc(uint8_t d,uint8_t a,uint8_t b) { return XOf(d,a,b,false,8);   }
static inline uint32_t ppc_subfe(uint8_t d,uint8_t a,uint8_t b) { return XOf(d,a,b,false,136); }
static inline uint32_t ppc_neg(uint8_t d,uint8_t a)              { return XOf(d,a,0,false,104); }
static inline uint32_t ppc_mullw(uint8_t d,uint8_t a,uint8_t b) { return XOf(d,a,b,false,235); }
static inline uint32_t ppc_mulhw(uint8_t d,uint8_t a,uint8_t b) { return XOf(d,a,b,false,75);  }
static inline uint32_t ppc_mulhwu(uint8_t d,uint8_t a,uint8_t b){ return XOf(d,a,b,false,11);  }

static inline uint32_t ppc_and(uint8_t a,uint8_t s,uint8_t b)   { return Xf(s,a,b,28);  }
static inline uint32_t ppc_or(uint8_t a,uint8_t s,uint8_t b)    { return Xf(s,a,b,444); }
static inline uint32_t ppc_xor(uint8_t a,uint8_t s,uint8_t b)   { return Xf(s,a,b,316); }
static inline uint32_t ppc_andc(uint8_t a,uint8_t s,uint8_t b)  { return Xf(s,a,b,60);  }
static inline uint32_t ppc_nor(uint8_t a,uint8_t s,uint8_t b)   { return Xf(s,a,b,124); }
static inline uint32_t ppc_mr(uint8_t a,uint8_t s)               { return ppc_or(a,s,s); }
static inline uint32_t ppc_slw(uint8_t a,uint8_t s,uint8_t b)   { return Xf(s,a,b,24);  }
static inline uint32_t ppc_srw(uint8_t a,uint8_t s,uint8_t b)   { return Xf(s,a,b,536); }
static inline uint32_t ppc_sraw(uint8_t a,uint8_t s,uint8_t b)  { return Xf(s,a,b,792); }
static inline uint32_t ppc_cntlzw(uint8_t a,uint8_t s)          { return Xf(s,a,0,26);  }
static inline uint32_t ppc_extsb(uint8_t a,uint8_t s)           { return Xf(s,a,0,954); }
static inline uint32_t ppc_extsh(uint8_t a,uint8_t s)           { return Xf(s,a,0,922); }
static inline uint32_t ppc_lwzx(uint8_t t,uint8_t a,uint8_t b)  { return Xf(t,a,b,23);  }
static inline uint32_t ppc_lbzx(uint8_t t,uint8_t a,uint8_t b)  { return Xf(t,a,b,87);  }
static inline uint32_t ppc_lhzx(uint8_t t,uint8_t a,uint8_t b)  { return Xf(t,a,b,279); }
static inline uint32_t ppc_stwx(uint8_t s,uint8_t a,uint8_t b)  { return Xf(s,a,b,151); }
static inline uint32_t ppc_stbx(uint8_t s,uint8_t a,uint8_t b)  { return Xf(s,a,b,215); }
static inline uint32_t ppc_sthx(uint8_t s,uint8_t a,uint8_t b)  { return Xf(s,a,b,407); }
static inline uint32_t ppc_cmp(uint8_t cr,uint8_t a,uint8_t b)
    { return (31u<<26)|((cr&7u)<<23)|((uint32_t)a<<16)|((uint32_t)b<<11); }
static inline uint32_t ppc_cmpl(uint8_t cr,uint8_t a,uint8_t b)
    { return (31u<<26)|((cr&7u)<<23)|((uint32_t)a<<16)|((uint32_t)b<<11)|(32u<<1); }
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

static int emit_li32(uint32_t* out, uint8_t rt, uint32_t v) {
    // rt must not be r0
    uint16_t hi=v>>16, lo=v&0xFFFF;
    if(!hi&&!lo)    { out[0]=ppc_addi(rt,0,0);                       return 1; }
    if(!hi) {
        if(lo<0x8000) { out[0]=ppc_addi(rt,0,(int16_t)lo);           return 1; }
        out[0]=ppc_addi(rt,0,0); out[1]=ppc_ori(rt,rt,lo);           return 2;
    }
    if(!lo)         { out[0]=ppc_addis(rt,0,(int16_t)hi);            return 1; }
    out[0]=ppc_addis(rt,0,(int16_t)hi); out[1]=ppc_ori(rt,rt,lo);   return 2;
}

// ============================================================
// Register mapping
// PPC r14-r29 = ARM r0-r15 (callee-saved)
// PPC r30     = CPSR       (callee-saved)
// PPC r31     = Interpreter* (callee-saved)
// Core*       = in frame at FRAME_CORE
//
// Volatile: r3(TA),r4(TB),r5(TC),r6(TD),r11(RCALL)
// ============================================================
static const uint8_t RA[16]={14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29};
static const uint8_t RCPSR=30, RINTERP=31;
static const uint8_t TA=3,TB=4,TC=5,TD=6,RCALL=11;

// ============================================================
// Code buffer & block cache
// ============================================================
static const size_t JIT_BYTES   = 4u*1024u*1024u;
static const size_t JIT_WORDS   = JIT_BYTES/4;
static const size_t BLK_ARMS    = 64;
static const size_t BLK_WDS     = BLK_ARMS*128+256;

static uint32_t* codeBuf = nullptr;
static size_t    codePos = 0;

struct JitBlock { uint32_t armPC; uint32_t* code; uint32_t nW; bool thumb,valid; };
static const size_t CSIZ=1u<<13;
static JitBlock cache[CSIZ];
static size_t hashPC(uint32_t pc){return (pc>>1)&(CSIZ-1);}

void flushJitCache(){ codePos=0; for(size_t i=0;i<CSIZ;i++) cache[i].valid=false; }
static void dcbst(uint32_t* p,size_t n){ DCFlushRange(p,n*4); ICInvalidateRange(p,n*4); }

// ============================================================
// Emit context
// ============================================================
struct Ctx {
    uint32_t *base,*cur; size_t cap;
    bool thumb,arm7,done;
    uint32_t blockPC;
    Interpreter* interp; Core* core;

    void E(uint32_t w){ if((size_t)(cur-base)<cap) *cur++=w; }
    size_t sz() const { return (size_t)(cur-base); }
    void li(uint8_t rt,uint32_t v){
        uint32_t t[2]; int n=emit_li32(t,rt,v);
        for(int i=0;i<n;i++) E(t[i]);
    }
    // Call via r11 (standard PPC indirect call scratch)
    void call(void* fn){
        uint32_t a=(uint32_t)(uintptr_t)fn;
        E(ppc_addis(RCALL,0,(int16_t)(a>>16)));
        E(ppc_ori(RCALL,RCALL,(uint16_t)(a&0xFFFF)));
        E(ppc_mtctr(RCALL));
        E(ppc_bctr(true)); // bctrl
    }
    void ldCore(uint8_t d=TA){ E(ppc_lwz(d,FRAME_CORE,1)); }
};

// ============================================================
// C helpers
// ============================================================
extern "C" {

// syncFrom: read ARM state from interpreter into regs[]
// regs[0..14] = r0..r14 (raw values)
// regs[15]    = ACTUAL next-instruction PC (not pipeline-offset)
// regs[16]    = CPSR
void JitHelp_syncFrom(Interpreter* interp, uint32_t* regs) {
    uint32_t** p = interp->getRegisters();
    for(int i=0;i<15;i++) regs[i]=*p[i];
    // getActualPC() returns *registers[15] - (thumb ? 4 : 8)
    // This is the actual instruction address being executed.
    regs[15] = interp->getActualPC();
    regs[16] = interp->getCpsrRef();
}

// syncTo: write ARM state from regs[] back to interpreter
// regs[15] = actual next-instruction PC to jump to
// We call setPC(regs[15]) which sets *registers[15]=regs[15]+8
// (or +4 for Thumb) and loads the pipeline.
void JitHelp_syncTo(Interpreter* interp, uint32_t* regs) {
    uint32_t** p = interp->getRegisters();
    for(int i=0;i<15;i++) *p[i]=regs[i];
    interp->getCpsrRef() = regs[16];
    interp->setPC(regs[15]);
}

int JitHelp_fallback(Interpreter* interp){ return interp->jitRunOpcode(); }

uint32_t JitHelp_r32(Core* c,int a,uint32_t ad){return c->memory.read<uint32_t>((bool)a,ad);}
uint16_t JitHelp_r16(Core* c,int a,uint32_t ad){return c->memory.read<uint16_t>((bool)a,ad);}
uint8_t  JitHelp_r8 (Core* c,int a,uint32_t ad){return c->memory.read<uint8_t> ((bool)a,ad);}
void JitHelp_w32(Core* c,int a,uint32_t ad,uint32_t v){c->memory.write<uint32_t>((bool)a,ad,v);}
void JitHelp_w16(Core* c,int a,uint32_t ad,uint16_t v){c->memory.write<uint16_t>((bool)a,ad,v);}
void JitHelp_w8 (Core* c,int a,uint32_t ad,uint8_t  v){c->memory.write<uint8_t> ((bool)a,ad,v);}

void JitHelp_tick(Core* core){
    core->globalCycles+=64;
    while(!core->events.empty()&&core->globalCycles>=core->events.front().cycles){
        SchedEvent e=core->events.front();
        core->events.erase(core->events.begin());
        core->tasks[e.task]();
    }
}

} // extern "C"

// ============================================================
// Prologue / Epilogue
// ============================================================
static void emitPrologue(Ctx& ctx){
    ctx.E(ppc_mflr(0));                           // capture LR from bctrl
    ctx.E(ppc_stwu(1,-(int16_t)FRAME_SIZE,1));    // allocate frame
    ctx.E(ppc_stw(0,FRAME_LR,1));                 // save LR at [r1+8]
    for(int r=14;r<=31;r++)
        ctx.E(ppc_stw(r,FRAME_R14+(r-14)*4,1));   // save r14-r31
    ctx.li(RINTERP,(uint32_t)(uintptr_t)ctx.interp);
    ctx.li(TA,(uint32_t)(uintptr_t)ctx.core);
    ctx.E(ppc_stw(TA,FRAME_CORE,1));
}

static void emitEpilogue(Ctx& ctx){
    for(int r=14;r<=31;r++)
        ctx.E(ppc_lwz(r,FRAME_R14+(r-14)*4,1));
    ctx.E(ppc_lwz(0,FRAME_LR,1));
    ctx.E(ppc_mtlr(0));
    ctx.E(ppc_addi(1,1,(int16_t)FRAME_SIZE));
    ctx.E(ppc_blr());
}

// ============================================================
// Sync helpers
//
// CRITICAL: RA[15] in the JIT block holds the ACTUAL instruction
// address (not pipeline-offset). When an ARM instruction needs
// to READ PC as an operand (e.g., MOV r0, PC), it must add the
// pipeline offset (8 for ARM, 4 for Thumb) to RA[15].
// We provide emit_getPC_operand() for this purpose.
// ============================================================
static void emitSyncTo(Ctx& ctx){
    // Store actual-PC (RA[15]) and other regs to sync area
    for(int i=0;i<16;i++) ctx.E(ppc_stw(RA[i],FRAME_REGSYNC+i*4,1));
    ctx.E(ppc_stw(RCPSR,FRAME_REGSYNC+16*4,1));
    ctx.E(ppc_mr(TA,RINTERP));
    ctx.E(ppc_addi(TB,1,(int16_t)FRAME_REGSYNC));
    ctx.call((void*)JitHelp_syncTo);
}

static void emitSyncFrom(Ctx& ctx){
    ctx.E(ppc_mr(TA,RINTERP));
    ctx.E(ppc_addi(TB,1,(int16_t)FRAME_REGSYNC));
    ctx.call((void*)JitHelp_syncFrom);
    for(int i=0;i<16;i++) ctx.E(ppc_lwz(RA[i],FRAME_REGSYNC+i*4,1));
    ctx.E(ppc_lwz(RCPSR,FRAME_REGSYNC+16*4,1));
    // After syncFrom: RA[15] = getActualPC() = actual instruction address
    // We must advance it past the current instruction BEFORE emitting it.
    // Actually NO: we leave RA[15] as getActualPC() and advance it
    // in the compile loop as we emit each instruction.
}

// Emit code to load (RA[15] + pipeline_offset) into dst register.
// This gives the PC value that ARM instructions see when they read PC.
static void emitGetPCoperand(Ctx& ctx, uint8_t dst){
    uint32_t pipeOff = ctx.thumb ? 4u : 8u;
    // RA[15] currently holds the actual instruction address.
    // ARM instructions that read PC get: actual_addr + pipeline_offset
    ctx.E(ppc_addi(dst,RA[15],(int16_t)pipeOff));
}

// ============================================================
// Condition flags
// CR7 = ARM NZCV (mapped from CPSR bits 31-28)
// ============================================================
static void emitLoadCR7(Ctx& ctx){ ctx.E(ppc_mtcrf(0x01,RCPSR)); }

struct CB{uint8_t bo,bi;bool ok;};
static CB condPpc(uint8_t c){
    switch(c){
        case 0:  return{12,29,true};  // EQ: Z=CR7_GT(bit29)
        case 1:  return{ 4,29,true};  // NE
        case 2:  return{12,30,true};  // CS: C=CR7_EQ(bit30)
        case 3:  return{ 4,30,true};  // CC
        case 4:  return{12,28,true};  // MI: N=CR7_LT(bit28)
        case 5:  return{ 4,28,true};  // PL
        case 6:  return{12,31,true};  // VS: V=CR7_SO(bit31)
        case 7:  return{ 4,31,true};  // VC
        case 14: return{20, 0,true};  // AL
        default: return{ 0, 0,false};
    }
}

static size_t emitCondSkip(Ctx& ctx,uint8_t cond){
    if(cond==14) return SIZE_MAX;
    emitLoadCR7(ctx);
    CB cb=condPpc(cond); if(!cb.ok) return SIZE_MAX;
    uint8_t inv=(cb.bo==12)?4:(cb.bo==4?12:cb.bo);
    size_t idx=ctx.sz();
    ctx.E(ppc_bc(inv,cb.bi,0));
    return idx;
}
static void patchSkip(Ctx& ctx,size_t idx){
    if(idx==SIZE_MAX) return;
    int32_t off=(int32_t)((ctx.sz()-idx)*4);
    ctx.base[idx]=(ctx.base[idx]&0xFFFF0003u)|(uint32_t)(off&0xFFFC);
}

// ============================================================
// CPSR flag updates
// ============================================================
static void setNZ(Ctx& ctx,uint8_t r){
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,2,31));  // clear N,Z
    ctx.E(ppc_rlwimi(RCPSR,r,0,0,0));        // N = r[31]
    ctx.E(ppc_cmpi(6,r,0));                  // cr6: r==0?
    ctx.E(ppc_mfcr(TA));
    // CR6_EQ is bit5 of CR word; rotate left 25 -> bit30 (Z position)
    ctx.E(ppc_rlwinm(TA,TA,25,30,30));
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}
static void setC_xer(Ctx& ctx){
    ctx.E(ppc_mfxer(TA));
    ctx.E(ppc_rlwinm(TA,TA,0,29,29));        // XER.CA at bit29
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,3,1));   // clear C (bit29)
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}
static void setV_add(Ctx& ctx,uint8_t res,uint8_t a,uint8_t b){
    ctx.E(ppc_xor(TA,res,a));
    ctx.E(ppc_xor(TB,res,b));
    ctx.E(ppc_and(TA,TA,TB));
    ctx.E(ppc_rlwinm(TA,TA,4,28,28));
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,4,2));
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}
static void setV_sub(Ctx& ctx,uint8_t res,uint8_t a,uint8_t b){
    ctx.E(ppc_xor(TA,a,b));
    ctx.E(ppc_xor(TB,a,res));
    ctx.E(ppc_and(TA,TA,TB));
    ctx.E(ppc_rlwinm(TA,TA,4,28,28));
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,4,2));
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}
static void setC_bit0(Ctx& ctx,uint8_t cr){
    ctx.E(ppc_rlwinm(TA,cr,29,29,29));
    ctx.E(ppc_rlwinm(RCPSR,RCPSR,0,3,1));
    ctx.E(ppc_or(RCPSR,RCPSR,TA));
}

// ============================================================
// Shifter
// Carry output in TC bit0 when sc=true
// ============================================================
static void sLslI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i==0){ if(d!=s)ctx.E(ppc_mr(d,s));
              if(sc)ctx.E(ppc_rlwinm(TC,RCPSR,3,31,31)); }
    else if(i<32){ if(sc)ctx.E(ppc_rlwinm(TC,s,i,31,31));
                   ctx.E(ppc_rlwinm(d,s,(uint8_t)i,0,(uint8_t)(31-i))); }
    else if(i==32){ if(sc)ctx.E(ppc_rlwinm(TC,s,0,31,31));
                    ctx.E(ppc_addi(d,0,0)); }
    else{ if(sc)ctx.E(ppc_addi(TC,0,0)); ctx.E(ppc_addi(d,0,0)); }
}
static void sLsrI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i==0||i==32){ if(sc)ctx.E(ppc_rlwinm(TC,s,1,31,31));
                     ctx.E(ppc_addi(d,0,0)); }
    else if(i<32){ if(sc)ctx.E(ppc_rlwinm(TC,s,(uint8_t)(33-i),31,31));
                   ctx.E(ppc_rlwinm(d,s,(uint8_t)(32-i),(uint8_t)i,31)); }
    else{ if(sc)ctx.E(ppc_addi(TC,0,0)); ctx.E(ppc_addi(d,0,0)); }
}
static void sAsrI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i<=0||i>=32){ if(sc)ctx.E(ppc_rlwinm(TC,s,1,31,31));
                     ctx.E(ppc_srawi(d,s,31)); }
    else{ if(sc)ctx.E(ppc_rlwinm(TC,s,(uint8_t)(33-i),31,31));
          ctx.E(ppc_srawi(d,s,(uint8_t)i)); }
}
static void sRorI(Ctx& ctx,uint8_t d,uint8_t s,int i,bool sc){
    if(i==0){ // RRX
        ctx.E(ppc_rlwinm(TA,RCPSR,3,31,31));
        if(sc)ctx.E(ppc_rlwinm(TC,s,0,31,31));
        ctx.E(ppc_rlwinm(d,s,31,1,31));
        ctx.E(ppc_rlwimi(d,TA,31,0,0));
    } else {
        i&=31; if(!i)i=32;
        if(i<32){ if(sc)ctx.E(ppc_rlwinm(TC,s,(uint8_t)(33-i),31,31));
                  ctx.E(ppc_rlwinm(d,s,(uint8_t)(32-i),0,31)); }
        else    { if(d!=s)ctx.E(ppc_mr(d,s));
                  if(sc)ctx.E(ppc_rlwinm(TC,s,1,31,31)); }
    }
}

static bool emitShifter(Ctx& ctx,uint32_t op,uint8_t dst,bool sc){
    bool isImm=(op>>25)&1;
    if(isImm){
        uint32_t v=op&0xFF,rot=((op>>8)&0xF)*2;
        if(rot) v=(v>>rot)|(v<<(32-rot));
        ctx.li(dst,v);
        if(sc&&rot){ ctx.E(ppc_rlwinm(TC,dst,1,31,31)); return true; }
        return false;
    }
    uint8_t rm=op&0xF,pRm=RA[rm],st=(op>>5)&3;
    bool isReg=(op>>4)&1;
    if(!isReg){
        int sa=(op>>7)&0x1F;
        switch(st){
            case 0: sLslI(ctx,dst,pRm,sa,sc); break;
            case 1: sLsrI(ctx,dst,pRm,sa?sa:32,sc); break;
            case 2: sAsrI(ctx,dst,pRm,sa?sa:32,sc); break;
            case 3: sRorI(ctx,dst,pRm,sa,sc); break;
        }
        return sc;
    } else {
        uint8_t rs=(op>>8)&0xF,pRs=RA[rs];
        ctx.E(ppc_rlwinm(TD,pRs,0,24,31));
        ctx.E(ppc_mr(TA,pRm));
        switch(st){
            case 0: ctx.E(ppc_slw(dst,TA,TD)); break;
            case 1: ctx.E(ppc_srw(dst,TA,TD)); break;
            case 2: ctx.E(ppc_sraw(dst,TA,TD)); break;
            case 3: ctx.E(ppc_subfic(TB,TD,32));
                    ctx.E(ppc_rlwnm(dst,TA,TB,0,31)); break;
        }
        return false;
    }
}

// ============================================================
// Data processing
// ============================================================
enum DP{AND=0,EOR,SUB,RSB,ADD,ADC,SBC,RSC,TST,TEQ,CMP,CMN,ORR,MOV,BIC,MVN};

static bool emitDP(Ctx& ctx,uint32_t op,uint32_t curPC){
    uint8_t cond=(op>>28)&0xF,dop=(op>>21)&0xF;
    bool    s=(op>>20)&1;
    uint8_t rn=(op>>16)&0xF,rd=(op>>12)&0xF;
    if(rd==15) return false; // PC destination: complex, use interpreter

    uint8_t pRd=RA[rd],pRn=RA[rn];

    // If rn==15, ARM instruction reads PC+8/PC+4 (pipeline value).
    // We need to use the pipeline-offset value for rn=15.
    // We'll load it into a temp if needed.
    bool rnIsPC=(rn==15);

    size_t si=emitCondSkip(ctx,cond);
    if(si==SIZE_MAX&&cond!=14) return false;

    // Load rn into TB_RN if it's PC (need pipeline offset)
    uint8_t srcRn=pRn;
    if(rnIsPC){
        // ARM pipeline: PC operand = actual_addr + 8 (ARM) or +4 (Thumb)
        emitGetPCoperand(ctx,TB);
        srcRn=TB;
    }

    bool needCin=(dop==ADC||dop==SBC||dop==RSC);
    bool logCarry=s&&(dop==AND||dop==EOR||dop==TST||dop==TEQ||
                      dop==ORR||dop==MOV||dop==BIC||dop==MVN);

    if(needCin){ ctx.E(ppc_rlwinm(TA,RCPSR,0,29,29)); ctx.E(ppc_mtxer(TA)); }

    // Shifter op into TA
    // BUT if rnIsPC we already used TB for srcRn.
    // emitShifter may use TA,TB,TC,TD. If srcRn=TB, we must not clobber TB.
    // emitShifter result goes into dst (first arg).
    // If shifter uses register shifts, it uses TD and TA as temporaries.
    // TA is ok to clobber (shifter result goes there).
    // TB: emitShifter only reads TB for ROR reg shift via subfic(TB,...).
    // If srcRn=TB and shifter does ROR reg, TB gets clobbered.
    // SAFE: if rnIsPC, save TB to TC first, then restore.
    // Actually simpler: put srcRn result in a different reg.
    // Use TD for srcRn when rnIsPC.
    if(rnIsPC){
        emitGetPCoperand(ctx,TD);
        srcRn=TD;
    }

    bool carrySet=emitShifter(ctx,op,TA,logCarry);

    // Save srcRn to frame scratch if we need V (srcRn may be volatile)
    bool needV=s&&(dop==ADD||dop==SUB||dop==RSB||dop==CMN||dop==CMP||
                    dop==ADC||dop==SBC||dop==RSC);
    if(needV){
        // srcRn is either pRn (callee-saved, safe) or TD (volatile).
        // TA = shifter result (volatile).
        // Save TA to SCR0 and srcRn to SCR1 if volatile.
        ctx.E(ppc_stw(TA,FRAME_SCR0,1));
        if(rnIsPC) ctx.E(ppc_stw(srcRn,FRAME_SCR1,1));
    }

    bool isTest=(dop==TST||dop==TEQ||dop==CMP||dop==CMN);
    uint8_t res=isTest?TC:pRd;

    switch((DP)dop){
        case AND:case TST: ctx.E(ppc_and(res,srcRn,TA)); break;
        case EOR:case TEQ: ctx.E(ppc_xor(res,srcRn,TA)); break;
        case SUB:case CMP: ctx.E(ppc_subfc(res,TA,srcRn)); break;
        case RSB:          ctx.E(ppc_subfc(res,srcRn,TA)); break;
        case ADD:case CMN: ctx.E(ppc_addc(res,srcRn,TA)); break;
        case ADC:          ctx.E(ppc_adde(res,srcRn,TA)); break;
        case SBC:          ctx.E(ppc_subfe(res,TA,srcRn)); break;
        case RSC:          ctx.E(ppc_subfe(res,srcRn,TA)); break;
        case ORR:          ctx.E(ppc_or(res,srcRn,TA)); break;
        case MOV:          if(res!=TA)ctx.E(ppc_mr(res,TA)); break;
        case BIC:          ctx.E(ppc_andc(res,srcRn,TA)); break;
        case MVN:          ctx.E(ppc_nor(res,TA,TA)); break;
    }

    if(s){
        // Reload operands for V calculation
        uint8_t opA=srcRn,opB=TA;
        if(needV){
            ctx.E(ppc_lwz(TA,FRAME_SCR0,1));  // original shifter op
            opB=TA;
            if(rnIsPC){ ctx.E(ppc_lwz(TD,FRAME_SCR1,1)); opA=TD; }
            else opA=pRn; // callee-saved, unchanged
        }
        switch((DP)dop){
            case ADD:case CMN:
                setNZ(ctx,res);setC_xer(ctx);setV_add(ctx,res,opA,opB);break;
            case ADC:
                setNZ(ctx,res);setC_xer(ctx);setV_add(ctx,res,opA,opB);break;
            case SUB:case CMP:
                setNZ(ctx,res);setC_xer(ctx);setV_sub(ctx,res,opA,opB);break;
            case RSB:
                setNZ(ctx,res);setC_xer(ctx);setV_sub(ctx,res,opB,opA);break;
            case SBC:
                setNZ(ctx,res);setC_xer(ctx);setV_sub(ctx,res,opA,opB);break;
            case RSC:
                setNZ(ctx,res);setC_xer(ctx);setV_sub(ctx,res,opB,opA);break;
            default:
                setNZ(ctx,res);
                if(carrySet) setC_bit0(ctx,TC);
                break;
        }
    }
    patchSkip(ctx,si);
    return true;
}

// ============================================================
// Branch
// ============================================================
static bool emitBranch(Ctx& ctx,uint32_t op,uint32_t curPC){
    uint8_t cond=(op>>28)&0xF;

    // BX Rm
    if((op&0x0FFFFFF0)==0x012FFF10){
        uint8_t rm=op&0xF; if(rm==15)return false;
        uint8_t pRm=RA[rm];
        size_t si=emitCondSkip(ctx,cond);
        if(si==SIZE_MAX&&cond!=14)return false;

        // New T bit = Rm[0]
        // Clear T (bit26 from MSB = bit5 from LSB) in RCPSR
        ctx.E(ppc_rlwinm(TA,RCPSR,0,0,25));   // keep bits [31..27] from MSB
        ctx.E(ppc_rlwinm(TB,RCPSR,0,27,31));  // keep bits [25..0] from MSB
        ctx.E(ppc_or(RCPSR,TA,TB));            // bit26=0 (T cleared)
        // Set T = Rm[0]: rotate Rm left 5, isolate bit26
        ctx.E(ppc_rlwinm(TA,pRm,5,26,26));
        ctx.E(ppc_or(RCPSR,RCPSR,TA));

        // New PC = Rm & ~1 (this is the ACTUAL instruction address)
        ctx.E(ppc_rlwinm(RA[15],pRm,0,0,30));

        emitSyncTo(ctx); emitEpilogue(ctx);
        if(si!=SIZE_MAX){
            patchSkip(ctx,si);
            // Fall-through: PC stays at curPC+4
            ctx.li(RA[15],curPC+4);
            emitSyncTo(ctx); emitEpilogue(ctx);
        }
        ctx.done=true; return true;
    }

    // B / BL
    if((op&0x0E000000)==0x0A000000){
        bool lk=(op>>24)&1;
        int32_t off=(int32_t)(op<<8)>>6;
        // Branch target: curPC + 8 + offset
        // curPC is the actual instruction address (getActualPC value).
        // ARM branch: target = PC + 8 + offset where PC=instr_addr+8
        // So target = instr_addr + 8 + offset
        uint32_t tgt=curPC+8+off;
        size_t si=emitCondSkip(ctx,cond);
        if(si==SIZE_MAX&&cond!=14)return false;
        if(lk) ctx.li(RA[14],curPC+4);   // LR = return addr (next instr)
        ctx.li(RA[15],tgt);               // actual target addr
        emitSyncTo(ctx); emitEpilogue(ctx);
        if(si!=SIZE_MAX){
            patchSkip(ctx,si);
            ctx.li(RA[15],curPC+4);
            emitSyncTo(ctx); emitEpilogue(ctx);
        }
        ctx.done=true; return true;
    }
    return false;
}

// ============================================================
// Load/Store
// ============================================================
static bool emitLS(Ctx& ctx,uint32_t op,uint32_t curPC){
    uint8_t cond=(op>>28)&0xF;
    bool ld=(op>>20)&1,by=(op>>22)&1,up=(op>>23)&1;
    bool pre=(op>>24)&1,wb=(op>>21)&1,immO=!((op>>25)&1);
    uint8_t rn=(op>>16)&0xF,rd=(op>>12)&0xF;
    if(rd==15||rn==15)return false;
    uint8_t pRn=RA[rn],pRd=RA[rd];

    size_t si=emitCondSkip(ctx,cond);
    if(si==SIZE_MAX&&cond!=14)return false;

    if(immO) ctx.li(TA,op&0xFFF);
    else     ctx.E(ppc_mr(TA,RA[op&0xF]));

    if(pre){
        if(up)ctx.E(ppc_add(TB,pRn,TA));
        else  ctx.E(ppc_subf(TB,TA,pRn));
    } else ctx.E(ppc_mr(TB,pRn));

    ctx.E(ppc_stw(TA,FRAME_SCR0,1));
    ctx.E(ppc_stw(TB,FRAME_SCR1,1));

    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.E(ppc_lwz(TC,FRAME_SCR1,1));
    if(!ld) ctx.E(ppc_mr(TD,pRd));

    void* fn=ld?(by?(void*)JitHelp_r8:(void*)JitHelp_r32)
               :(by?(void*)JitHelp_w8:(void*)JitHelp_w32);
    ctx.call(fn);

    if(ld) ctx.E(ppc_mr(pRd,TA));

    ctx.E(ppc_lwz(TA,FRAME_SCR0,1));
    if(!pre){ if(up)ctx.E(ppc_add(pRn,pRn,TA));
              else  ctx.E(ppc_subf(pRn,TA,pRn)); }
    else if(wb&&rn!=rd) ctx.E(ppc_lwz(pRn,FRAME_SCR1,1));

    patchSkip(ctx,si);
    return true;
}

// ============================================================
// Multiply
// ============================================================
static bool emitMul(Ctx& ctx,uint32_t op){
    bool s=(op>>20)&1,acc=(op>>21)&1,lng=(op>>23)&1;
    uint8_t rd=(op>>16)&0xF,rn=(op>>12)&0xF,rs=(op>>8)&0xF,rm=op&0xF;
    if(rd==15||rm==15||rs==15)return false;
    if(lng)return false;
    uint8_t pRd=RA[rd],pRn=RA[rn],pRs=RA[rs],pRm=RA[rm];
    if(acc){ ctx.E(ppc_mullw(TA,pRm,pRs)); ctx.E(ppc_add(pRd,TA,pRn)); }
    else     ctx.E(ppc_mullw(pRd,pRm,pRs));
    if(s) setNZ(ctx,pRd);
    return true;
}

static void emitFallback(Ctx& ctx){
    emitSyncTo(ctx);
    ctx.E(ppc_mr(TA,RINTERP));
    ctx.call((void*)JitHelp_fallback);
    emitSyncFrom(ctx);
}

// ============================================================
// ARM dispatch
// ============================================================
static bool dispARM(Ctx& ctx,uint32_t op,uint32_t curPC){
    uint8_t cond=(op>>28)&0xF;
    if(cond==15)return false;
    uint32_t it=(op>>25)&7;
    switch(it){
        case 0:case 1:
            if((op&0x0FC000F0)==0x00000090) return emitMul(ctx,op);
            if((op&0x0FFFFFF0)==0x012FFF10||
               (op&0x0FFFFFF0)==0x012FFF30)  return emitBranch(ctx,op,curPC);
            if((op&0x0FB00FF0)==0x01000000||
               (op&0x0FB00000)==0x03200000||
               (op&0x0DB0F000)==0x010F0000||
               (op&0x0E000090)==0x00000090)  return false;
            return emitDP(ctx,op,curPC);
        case 2:case 3: return emitLS(ctx,op,curPC);
        case 4: return false;
        case 5: return emitBranch(ctx,op,curPC);
        default: return false;
    }
}

// ============================================================
// Thumb emitters
// ============================================================
static bool emitT_shifts(Ctx& ctx,uint16_t op){
    uint8_t ty=(op>>11)&3,rd=op&7,rs=(op>>3)&7;
    int i=(op>>6)&0x1F;
    uint8_t pRd=RA[rd],pRs=RA[rs];
    switch(ty){
        case 0: sLslI(ctx,pRd,pRs,i,true); break;
        case 1: sLsrI(ctx,pRd,pRs,i?i:32,true); break;
        case 2: sAsrI(ctx,pRd,pRs,i?i:32,true); break;
        default: return false;
    }
    setNZ(ctx,pRd); setC_bit0(ctx,TC); return true;
}

static bool emitT_addSub3(Ctx& ctx,uint16_t op){
    uint8_t rd=op&7,rs=(op>>3)&7;
    bool sub=(op>>9)&1,imm3=(op>>10)&1;
    uint8_t pRd=RA[rd],pRs=RA[rs];
    if(imm3) ctx.li(TA,(op>>6)&7);
    else     ctx.E(ppc_mr(TA,RA[(op>>6)&7]));
    ctx.E(ppc_mr(TB,pRs));
    if(sub){ ctx.E(ppc_subfc(pRd,TA,TB)); setNZ(ctx,pRd);setC_xer(ctx);setV_sub(ctx,pRd,TB,TA); }
    else   { ctx.E(ppc_addc(pRd,TB,TA));  setNZ(ctx,pRd);setC_xer(ctx);setV_add(ctx,pRd,TB,TA); }
    return true;
}

static bool emitT_imm8(Ctx& ctx,uint16_t op){
    uint8_t ty=(op>>11)&3,rd=(op>>8)&7;
    uint8_t pRd=RA[rd]; uint8_t imm=op&0xFF;
    switch(ty){
        case 0: ctx.li(pRd,imm); setNZ(ctx,pRd); return true;
        case 1:{ ctx.li(TA,imm); ctx.E(ppc_mr(TB,pRd));
                 ctx.E(ppc_subfc(TC,TA,TB));
                 setNZ(ctx,TC);setC_xer(ctx);setV_sub(ctx,TC,TB,TA); return true; }
        case 2:{ ctx.li(TA,imm); ctx.E(ppc_mr(TB,pRd));
                 ctx.E(ppc_addc(pRd,TB,TA));
                 setNZ(ctx,pRd);setC_xer(ctx);setV_add(ctx,pRd,TB,TA); return true; }
        case 3:{ ctx.li(TA,imm); ctx.E(ppc_mr(TB,pRd));
                 ctx.E(ppc_subfc(pRd,TA,TB));
                 setNZ(ctx,pRd);setC_xer(ctx);setV_sub(ctx,pRd,TB,TA); return true; }
    }
    return false;
}

static bool emitT_alu(Ctx& ctx,uint16_t op){
    uint8_t rd=op&7,rs=(op>>3)&7,o=(op>>6)&0xF;
    uint8_t pRd=RA[rd],pRs=RA[rs];
    switch(o){
        case 0:  ctx.E(ppc_and(pRd,pRd,pRs));  setNZ(ctx,pRd); break;
        case 1:  ctx.E(ppc_xor(pRd,pRd,pRs));  setNZ(ctx,pRd); break;
        case 2:  ctx.E(ppc_slw(pRd,pRd,pRs));  setNZ(ctx,pRd); break;
        case 3:  ctx.E(ppc_srw(pRd,pRd,pRs));  setNZ(ctx,pRd); break;
        case 4:  ctx.E(ppc_sraw(pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case 5:{ ctx.E(ppc_rlwinm(TA,RCPSR,0,29,29)); ctx.E(ppc_mtxer(TA));
                 ctx.E(ppc_mr(TB,pRd)); ctx.E(ppc_adde(pRd,TB,pRs));
                 setNZ(ctx,pRd);setC_xer(ctx);setV_add(ctx,pRd,TB,pRs); break; }
        case 6:{ ctx.E(ppc_rlwinm(TA,RCPSR,0,29,29)); ctx.E(ppc_mtxer(TA));
                 ctx.E(ppc_mr(TB,pRd)); ctx.E(ppc_subfe(pRd,pRs,TB));
                 setNZ(ctx,pRd);setC_xer(ctx);setV_sub(ctx,pRd,TB,pRs); break; }
        case 7:{ ctx.E(ppc_subfic(TA,pRs,32));
                 ctx.E(ppc_rlwnm(pRd,pRd,TA,0,31)); setNZ(ctx,pRd); break; }
        case 8:{ ctx.E(ppc_and(TA,pRd,pRs)); setNZ(ctx,TA); break; }
        case 9:{ ctx.E(ppc_mr(TB,pRd)); ctx.E(ppc_addi(TA,0,0));
                 ctx.E(ppc_subfc(pRd,TB,TA));
                 setNZ(ctx,pRd);setC_xer(ctx);setV_sub(ctx,pRd,TA,TB); break; }
        case 10:{ ctx.E(ppc_mr(TB,pRd)); ctx.E(ppc_subfc(TA,pRs,TB));
                  setNZ(ctx,TA);setC_xer(ctx);setV_sub(ctx,TA,TB,pRs); break; }
        case 11:{ ctx.E(ppc_mr(TB,pRd)); ctx.E(ppc_addc(TA,TB,pRs));
                  setNZ(ctx,TA);setC_xer(ctx);setV_add(ctx,TA,TB,pRs); break; }
        case 12: ctx.E(ppc_or(pRd,pRd,pRs));   setNZ(ctx,pRd); break;
        case 13: ctx.E(ppc_mullw(pRd,pRd,pRs)); setNZ(ctx,pRd); break;
        case 14: ctx.E(ppc_andc(pRd,pRd,pRs));  setNZ(ctx,pRd); break;
        case 15: ctx.E(ppc_nor(pRd,pRs,pRs));   setNZ(ctx,pRd); break;
        default: return false;
    }
    return true;
}

static bool emitT_hiReg(Ctx& ctx,uint16_t op){
    uint8_t o=(op>>8)&3,h1=(op>>7)&1,h2=(op>>6)&1;
    uint8_t rs=((op>>3)&7)|(h2<<3),rd=(op&7)|(h1<<3);
    if(rd==15||rs==15)return false;
    uint8_t pRd=RA[rd],pRs=RA[rs];
    switch(o){
        case 0: ctx.E(ppc_add(pRd,pRd,pRs)); break;
        case 1:{ ctx.E(ppc_mr(TB,pRd)); ctx.E(ppc_subfc(TA,pRs,TB));
                 setNZ(ctx,TA);setC_xer(ctx);setV_sub(ctx,TA,TB,pRs); break; }
        case 2: ctx.E(ppc_mr(pRd,pRs)); break;
        case 3:{ // BX Rs
            // Clear T bit
            ctx.E(ppc_rlwinm(TA,RCPSR,0,0,25));
            ctx.E(ppc_rlwinm(TB,RCPSR,0,27,31));
            ctx.E(ppc_or(RCPSR,TA,TB));
            // Set T = Rs[0]
            ctx.E(ppc_rlwinm(TA,pRs,5,26,26));
            ctx.E(ppc_or(RCPSR,RCPSR,TA));
            // PC = Rs & ~1
            ctx.E(ppc_rlwinm(RA[15],pRs,0,0,30));
            emitSyncTo(ctx); emitEpilogue(ctx);
            ctx.done=true; break; }
    }
    return true;
}

static bool emitT_ldrPc(Ctx& ctx,uint16_t op,uint32_t curPC){
    uint8_t rd=(op>>8)&7;
    // Thumb LDR PC-relative: addr = (PC & ~3) + (imm8 << 2)
    // PC here = curPC + 4 (pipeline value for Thumb)
    uint32_t addr=((curPC+4)&~3u)+((op&0xFF)<<2);
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.li(TC,addr);
    ctx.call((void*)JitHelp_r32);
    ctx.E(ppc_mr(RA[rd],TA));
    return true;
}

static bool emitT_memReg(Ctx& ctx,uint16_t op){
    uint8_t rd=op&7,rb=(op>>3)&7,ro=(op>>6)&7;
    uint8_t pRd=RA[rd],pRb=RA[rb],pRo=RA[ro];
    uint8_t op97=(op>>9)&7;
    void* fn=nullptr; bool ld=true;
    switch(op97){
        case 0: fn=(void*)JitHelp_w32; ld=false; break;
        case 1: fn=(void*)JitHelp_w8;  ld=false; break;
        case 2: fn=(void*)JitHelp_r16; break;
        case 3: fn=(void*)JitHelp_r8;  break; // LDSB
        case 4: fn=(void*)JitHelp_r32; break;
        case 5: fn=(void*)JitHelp_r8;  break; // LDRB
        case 6: fn=(void*)JitHelp_r16; break;
        case 7: fn=(void*)JitHelp_r16; break; // LDRSH
        default: return false;
    }
    ctx.E(ppc_add(TC,pRb,pRo));
    ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore(TA); ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if(!ld) ctx.E(ppc_mr(TD,pRd));
    ctx.call(fn);
    if(ld){
        if(op97==3)  ctx.E(ppc_extsb(pRd,TA));
        else if(op97==7) ctx.E(ppc_extsh(pRd,TA));
        else         ctx.E(ppc_mr(pRd,TA));
    }
    return true;
}

static bool emitT_memImm(Ctx& ctx,uint16_t op){
    uint8_t rd=op&7,rb=(op>>3)&7;
    uint8_t pRd=RA[rd],pRb=RA[rb];
    bool ld=(op>>11)&1;
    uint8_t h=(op>>12)&0xF;
    bool by=(h==7),hw=(h==8);
    uint32_t off=((op>>6)&0x1F)*(hw?2u:(by?1u:4u));
    ctx.li(TC,off); ctx.E(ppc_add(TC,pRb,TC));
    ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore(TA); ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if(!ld) ctx.E(ppc_mr(TD,pRd));
    void* fn=ld?(hw?(void*)JitHelp_r16:(by?(void*)JitHelp_r8:(void*)JitHelp_r32))
               :(hw?(void*)JitHelp_w16:(by?(void*)JitHelp_w8:(void*)JitHelp_w32));
    ctx.call(fn);
    if(ld) ctx.E(ppc_mr(pRd,TA));
    return true;
}

static bool emitT_spLoad(Ctx& ctx,uint16_t op,uint32_t curPC){
    bool ld=(op>>11)&1; uint8_t rd=(op>>8)&7;
    uint8_t pRd=RA[rd]; bool sp=((op>>12)&0xF)==0x9;
    uint32_t off=(op&0xFF)<<2;
    if(sp){ ctx.li(TA,off); ctx.E(ppc_add(TC,RA[13],TA)); }
    else  { ctx.li(TC,((curPC+4)&~3u)+off); }
    ctx.E(ppc_stw(TC,FRAME_SCR0,1));
    ctx.ldCore(TA); ctx.E(ppc_addi(TB,0,ctx.arm7?1:0));
    ctx.E(ppc_lwz(TC,FRAME_SCR0,1));
    if(!ld) ctx.E(ppc_mr(TD,pRd));
    ctx.call(ld?(void*)JitHelp_r32:(void*)JitHelp_w32);
    if(ld) ctx.E(ppc_mr(pRd,TA));
    return true;
}

static bool emitT_branch(Ctx& ctx,uint16_t op,uint32_t curPC){
    uint8_t h=(op>>12)&0xF;
    if(h==0xE){ // unconditional B
        int32_t off=(int32_t)(op<<21)>>20;
        // Thumb branch: target = (curPC+4) + offset
        ctx.li(RA[15],(uint32_t)(curPC+4+off));
        emitSyncTo(ctx); emitEpilogue(ctx);
        ctx.done=true; return true;
    }
    if(h==0xD){ // conditional
        uint8_t cond=(op>>8)&0xF;
        if(cond==0xF)return false;
        int32_t off=(int8_t)(op&0xFF); off<<=1;
        uint32_t tgt=curPC+4+off,fall=curPC+2;
        emitLoadCR7(ctx);
        CB cb=condPpc(cond); if(!cb.ok)return false;
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

static bool emitT_bl(Ctx& ctx,uint16_t op1,uint16_t op2,uint32_t curPC){
    int32_t hi=(int32_t)((op1&0x7FF)<<21)>>9;
    int32_t lo=(op2&0x7FF)<<1;
    // BL target: (curPC+4) + (hi + lo)
    uint32_t tgt=curPC+4+hi+lo;
    bool blx=((op2>>11)&0x1F)==0x1C;
    ctx.li(RA[14],(curPC+4)|1u);  // LR = next instr | Thumb bit
    if(blx){
        tgt&=~3u;
        ctx.li(RA[15],tgt);
        // Switch to ARM: clear T bit
        ctx.E(ppc_rlwinm(TA,RCPSR,0,0,25));
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
static bool dispThumb(Ctx& ctx,uint16_t op,uint32_t curPC){
    uint8_t h=(op>>12)&0xF;
    switch(h){
        case 0x0:{ uint8_t b11=(op>>11)&3;
                   if(b11<3) return emitT_shifts(ctx,op);
                   return emitT_addSub3(ctx,op); }
        case 0x1: return emitT_imm8(ctx,op);
        case 0x2:{ uint8_t b10=(op>>10)&3;
                   if(b10==0) return emitT_alu(ctx,op);
                   if(b10==1) return emitT_hiReg(ctx,op);
                   return emitT_ldrPc(ctx,op,curPC); }
        case 0x3: return emitT_memImm(ctx,op);  // STR/LDR word imm (actually h=6)
        // THUMB ENCODING CORRECTION:
        // h=0x3 = 0011: ADD Rd, #imm8 / SUB etc -- no, that's h=0x1
        // Correct Thumb16 encoding by top 4 bits:
        // 0x0 (0000): LSL/LSR/ASR/ADD/SUB
        // 0x1 (0001): MOV/CMP/ADD/SUB imm8
        // 0x2 (0010): DP/HiReg/BX/LDR PC
        // 0x3 (0011): STR/LDR reg / STR/LDR byte reg
        // 0x4 (0100): STR/LDR halfword reg + signed
        // 0x5 (0101): (same, different sub-encodings)
        // 0x6 (0110): STR/LDR word imm5
        // 0x7 (0111): STRB/LDRB imm5
        // 0x8 (1000): STRH/LDRH imm5
        // 0x9 (1001): STR/LDR SP-relative
        // 0xA (1010): ADD PC/SP
        // 0xB (1011): misc (push/pop/etc)
        // 0xC (1100): STM/LDM
        // 0xD (1101): conditional branch / SWI
        // 0xE (1110): unconditional branch
        // 0xF (1111): BL/BLX prefix
        case 0x4: return emitT_memReg(ctx,op);
        case 0x5: return emitT_memReg(ctx,op);
        case 0x6: return emitT_memImm(ctx,op);
        case 0x7: return emitT_memImm(ctx,op);
        case 0x8: return emitT_memImm(ctx,op);
        case 0x9: return emitT_spLoad(ctx,op,curPC);
        case 0xA: return false;
        case 0xB: return false;
        case 0xC: return false;
        case 0xD: return emitT_branch(ctx,op,curPC);
        case 0xE: return emitT_branch(ctx,op,curPC);
        case 0xF: return false;
        default:  return false;
    }
}

// ============================================================
// Valid PC check
// ============================================================
static bool validPC(uint32_t pc,bool gba){
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
// Compile block
// curPC in the block = getActualPC() value at compile time.
// RA[15] throughout = actual instruction address (getActualPC).
// ============================================================
static JitBlock* compile(Interpreter* interp,Core* core,
                          uint32_t armPC,bool arm7){
    if(!validPC(armPC,core->gbaMode))return nullptr;
    bool thumb=interp->isThumb();
    size_t bkt=hashPC(armPC);
    JitBlock& slot=cache[bkt];
    if(slot.valid&&slot.armPC==armPC&&slot.thumb==thumb)return &slot;
    if(codePos+BLK_WDS>=JIT_WORDS) flushJitCache();

    Ctx ctx;
    ctx.base=codeBuf+codePos; ctx.cur=ctx.base;
    ctx.cap=JIT_WORDS-codePos;
    ctx.thumb=thumb; ctx.arm7=arm7;
    ctx.blockPC=armPC; ctx.interp=interp; ctx.core=core;
    ctx.done=false;

    emitPrologue(ctx);
    emitSyncFrom(ctx);
    // After emitSyncFrom: RA[15] = getActualPC() of first instruction

    uint32_t curPC=armPC; // tracks actual instruction address
    int n=0;

    while(n<(int)BLK_ARMS&&!ctx.done){
        // RA[15] should equal curPC at this point.
        // ARM instructions that read PC as operand get curPC + pipelineOffset.
        // We emit that via emitGetPCoperand() when needed.
        // After emitting an instruction, advance curPC by instruction size.

        if(thumb){
            uint16_t op=core->memory.read<uint16_t>(arm7,curPC);
            // Two-halfword BL/BLX?
            if(((op>>11)&0x1F)==0x1E){
                uint16_t op2=core->memory.read<uint16_t>(arm7,curPC+2);
                uint8_t bb=(op2>>11)&0x1F;
                if(bb==0x1F||bb==0x1C){
                    // Update RA[15] to curPC before emitting
                    // (BL uses curPC internally)
                    emitT_bl(ctx,op,op2,curPC);
                    curPC+=4; n+=2; continue;
                }
            }
            // Update RA[15] = curPC so the instruction can read PC correctly
            // Actually RA[15] is maintained as we go. At block start it's
            // set by syncFrom. We need to keep it updated.
            // Emit: RA[15] = curPC (the actual current instruction address)
            ctx.li(RA[15],curPC);

            bool ok=dispThumb(ctx,op,curPC);
            if(!ok){ emitFallback(ctx); ctx.done=true; }
            else{
                curPC+=2; n++;
                if(!ctx.done){
                    // Advance RA[15] for next instruction
                    ctx.li(RA[15],curPC);
                    uint8_t h=(op>>12)&0xF;
                    if(h==0xD||h==0xE) ctx.done=true;
                }
            }
        } else {
            uint32_t op=core->memory.read<uint32_t>(arm7,curPC);
            ctx.li(RA[15],curPC);

            bool ok=dispARM(ctx,op,curPC);
            if(!ok){ emitFallback(ctx); ctx.done=true; }
            else{
                curPC+=4; n++;
                if(!ctx.done){
                    ctx.li(RA[15],curPC);
                    uint32_t it=(op>>25)&7;
                    if(it==5)ctx.done=true;
                    if((op&0x0FFFFFF0)==0x012FFF10||
                       (op&0x0FFFFFF0)==0x012FFF30)ctx.done=true;
                }
            }
        }
    }

    if(!ctx.done){ emitSyncTo(ctx); emitEpilogue(ctx); }

    size_t wds=ctx.sz();
    dcbst(ctx.base,wds);
    slot={armPC,ctx.base,(uint32_t)wds,thumb,true};
    codePos+=wds;
    return &slot;
}

// ============================================================
// Run loops
// ============================================================
void runJitNds(Core& core){
    for(int cpu=0;cpu<2;cpu++){
        if(core.interpreter[cpu].halted)continue;
        uint32_t pc=core.interpreter[cpu].getActualPC();
        JitBlock* b=compile(&core.interpreter[cpu],&core,pc,cpu==1);
        if(b) executeBlock_asm(b->code);
        else  core.interpreter[cpu].jitRunOpcode();
    }
    JitHelp_tick(&core);
}

void runJitGba(Core& core){
    if(!core.interpreter[1].halted){
        uint32_t pc=core.interpreter[1].getActualPC();
        JitBlock* b=compile(&core.interpreter[1],&core,pc,true);
        if(b) executeBlock_asm(b->code);
        else  core.interpreter[1].jitRunOpcode();
    }
    JitHelp_tick(&core);
}

bool initJit(Core* core){
    codeBuf=(uint32_t*)memalign(32,JIT_BYTES);
    if(!codeBuf){printf("[JIT] alloc fail\n");return false;}
    codePos=0;
    for(size_t i=0;i<CSIZ;i++)cache[i].valid=false;
    printf("[JIT] buf=%p (%zuKB)\n",(void*)codeBuf,JIT_BYTES>>10);
    if(core)core->setRunFunc(core->gbaMode?runJitGba:runJitNds);
    return true;
}

void shutdownJit(Core* core){
    if(core)core->setRunFunc(core->gbaMode
        ?static_cast<void(*)(Core&)>(&Interpreter::runCoreSingle<true,0>)
        :&Interpreter::runCoreNds);
    free(codeBuf);codeBuf=nullptr;
}

void invalidateJitRange(uint32_t start,uint32_t end){
    for(size_t i=0;i<CSIZ;i++)
        if(cache[i].valid&&cache[i].armPC>=start&&cache[i].armPC<end)
            cache[i].valid=false;
}

} // namespace JitPpc
