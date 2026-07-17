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
// Frame layout
//   [r1+ 0]  back-chain
//   [r1+ 4]  ABI LR area (callees write here, not us)
//   [r1+ 8]  our saved LR                    FRAME_LR
//   [r1+12]  pad
//   [r1+16]  r14..r29 (16 x 4 = 64 bytes)    FRAME_R14
//   [r1+80]  ARM sync area (17 x 4 = 68 b)   FRAME_REGSYNC
//   [r1+148] pad to 176
// ============================================================
static const int FRAME_SIZE    = 176;
static const int FRAME_LR      = 8;
static const int FRAME_R14     = 16;
static const int FRAME_REGSYNC = 80;

static_assert(FRAME_SIZE % 16 == 0,               "frame alignment");
static_assert(FRAME_REGSYNC + 17*4 <= FRAME_SIZE, "sync area fits");
static_assert(FRAME_R14    + 16*4  <= FRAME_REGSYNC, "saved regs fit");

namespace JitPpc {

// ============================================================
// PPC instruction builders
// ============================================================
static inline uint32_t ppc_b(int32_t o,bool aa=false,bool lk=false)
    {return(18u<<26)|((uint32_t)(o&0x3FFFFFC))|(aa?2u:0u)|(lk?1u:0u);}
static inline uint32_t ppc_bc(uint8_t bo,uint8_t bi,int16_t o,bool lk=false)
    {return(16u<<26)|((uint32_t)(bo&0x1F)<<21)|((uint32_t)(bi&0x1F)<<16)
           |((uint32_t)(o&0xFFFC))|(lk?1u:0u);}
static inline uint32_t ppc_bclr(uint8_t bo,uint8_t bi,bool lk=false)
    {return(19u<<26)|((uint32_t)(bo&0x1F)<<21)|((uint32_t)(bi&0x1F)<<16)
           |(16u<<1)|(lk?1u:0u);}
static inline uint32_t ppc_bctr(bool lk=false)
    {return(19u<<26)|(20u<<21)|(528u<<1)|(lk?1u:0u);}
static inline uint32_t ppc_addi(uint8_t rt,uint8_t ra,int16_t i)
    {return(14u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i;}
static inline uint32_t ppc_addis(uint8_t rt,uint8_t ra,int16_t i)
    {return(15u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i;}
static inline uint32_t ppc_ori(uint8_t ra,uint8_t rs,uint16_t i)
    {return(24u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i;}
static inline uint32_t ppc_oris(uint8_t ra,uint8_t rs,uint16_t i)
    {return(25u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i;}
static inline uint32_t ppc_andi(uint8_t ra,uint8_t rs,uint16_t i)
    {return(28u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i;}
static inline uint32_t ppc_xori(uint8_t ra,uint8_t rs,uint16_t i)
    {return(26u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|i;}
static inline uint32_t ppc_lwz(uint8_t rt,int16_t d,uint8_t ra)
    {return(32u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d;}
static inline uint32_t ppc_stw(uint8_t rs,int16_t d,uint8_t ra)
    {return(36u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d;}
static inline uint32_t ppc_lbz(uint8_t rt,int16_t d,uint8_t ra)
    {return(34u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d;}
static inline uint32_t ppc_stb(uint8_t rs,int16_t d,uint8_t ra)
    {return(38u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d;}
static inline uint32_t ppc_lhz(uint8_t rt,int16_t d,uint8_t ra)
    {return(40u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d;}
static inline uint32_t ppc_lha(uint8_t rt,int16_t d,uint8_t ra)
    {return(42u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d;}
static inline uint32_t ppc_sth(uint8_t rs,int16_t d,uint8_t ra)
    {return(44u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d;}
static inline uint32_t ppc_cmpi(uint8_t cr,uint8_t ra,int16_t i,bool l=false)
    {return(11u<<26)|((uint32_t)(cr&7)<<23)|(l?(1u<<21):0u)
           |((uint32_t)ra<<16)|(uint16_t)i;}
static inline uint32_t ppc_cmpli(uint8_t cr,uint8_t ra,uint16_t i,bool l=false)
    {return(10u<<26)|((uint32_t)(cr&7)<<23)|(l?(1u<<21):0u)
           |((uint32_t)ra<<16)|i;}
static inline uint32_t ppc_subfic(uint8_t rt,uint8_t ra,int16_t i)
    {return(8u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)i;}
static inline uint32_t ppc_Xform(uint32_t op,uint8_t rt,uint8_t ra,uint8_t rb,
                                   uint32_t xop,bool rc=false)
    {return(op<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)
           |((uint32_t)rb<<11)|(xop<<1)|(rc?1u:0u);}
static inline uint32_t ppc_XOform(uint32_t op,uint8_t rt,uint8_t ra,uint8_t rb,
                                    bool oe,uint32_t xop,bool rc=false)
    {return(op<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)
           |((uint32_t)rb<<11)|(oe?(1u<<10):0u)|(xop<<1)|(rc?1u:0u);}
static inline uint32_t ppc_add(uint8_t rt,uint8_t ra,uint8_t rb,bool rc=false)
    {return ppc_XOform(31,rt,ra,rb,false,266,rc);}
static inline uint32_t ppc_addc(uint8_t rt,uint8_t ra,uint8_t rb,bool rc=false)
    {return ppc_XOform(31,rt,ra,rb,false,10,rc);}
static inline uint32_t ppc_adde(uint8_t rt,uint8_t ra,uint8_t rb,bool rc=false)
    {return ppc_XOform(31,rt,ra,rb,false,138,rc);}
static inline uint32_t ppc_subf(uint8_t rt,uint8_t ra,uint8_t rb,bool rc=false)
    {return ppc_XOform(31,rt,ra,rb,false,40,rc);}
static inline uint32_t ppc_subfc(uint8_t rt,uint8_t ra,uint8_t rb,bool rc=false)
    {return ppc_XOform(31,rt,ra,rb,false,8,rc);}
static inline uint32_t ppc_subfe(uint8_t rt,uint8_t ra,uint8_t rb,bool rc=false)
    {return ppc_XOform(31,rt,ra,rb,false,136,rc);}
static inline uint32_t ppc_neg(uint8_t rt,uint8_t ra,bool rc=false)
    {return ppc_XOform(31,rt,ra,0,false,104,rc);}
static inline uint32_t ppc_mullw(uint8_t rt,uint8_t ra,uint8_t rb,bool rc=false)
    {return ppc_XOform(31,rt,ra,rb,false,235,rc);}
static inline uint32_t ppc_mulhw(uint8_t rt,uint8_t ra,uint8_t rb,bool rc=false)
    {return ppc_XOform(31,rt,ra,rb,false,75,rc);}
static inline uint32_t ppc_mulhwu(uint8_t rt,uint8_t ra,uint8_t rb,bool rc=false)
    {return ppc_XOform(31,rt,ra,rb,false,11,rc);}
static inline uint32_t ppc_divwu(uint8_t rt,uint8_t ra,uint8_t rb,bool rc=false)
    {return ppc_XOform(31,rt,ra,rb,false,459,rc);}
static inline uint32_t ppc_divw(uint8_t rt,uint8_t ra,uint8_t rb,bool rc=false)
    {return ppc_XOform(31,rt,ra,rb,false,491,rc);}
static inline uint32_t ppc_and(uint8_t ra,uint8_t rs,uint8_t rb,bool rc=false)
    {return ppc_Xform(31,rs,ra,rb,28,rc);}
static inline uint32_t ppc_or(uint8_t ra,uint8_t rs,uint8_t rb,bool rc=false)
    {return ppc_Xform(31,rs,ra,rb,444,rc);}
static inline uint32_t ppc_xor(uint8_t ra,uint8_t rs,uint8_t rb,bool rc=false)
    {return ppc_Xform(31,rs,ra,rb,316,rc);}
static inline uint32_t ppc_andc(uint8_t ra,uint8_t rs,uint8_t rb,bool rc=false)
    {return ppc_Xform(31,rs,ra,rb,60,rc);}
static inline uint32_t ppc_nor(uint8_t ra,uint8_t rs,uint8_t rb,bool rc=false)
    {return ppc_Xform(31,rs,ra,rb,124,rc);}
static inline uint32_t ppc_eqv(uint8_t ra,uint8_t rs,uint8_t rb,bool rc=false)
    {return ppc_Xform(31,rs,ra,rb,284,rc);}
static inline uint32_t ppc_nand(uint8_t ra,uint8_t rs,uint8_t rb,bool rc=false)
    {return ppc_Xform(31,rs,ra,rb,476,rc);}
static inline uint32_t ppc_orc(uint8_t ra,uint8_t rs,uint8_t rb,bool rc=false)
    {return ppc_Xform(31,rs,ra,rb,412,rc);}
static inline uint32_t ppc_mr(uint8_t ra,uint8_t rs){return ppc_or(ra,rs,rs);}
static inline uint32_t ppc_nop(){return ppc_ori(0,0,0);}
static inline uint32_t ppc_slw(uint8_t ra,uint8_t rs,uint8_t rb,bool rc=false)
    {return ppc_Xform(31,rs,ra,rb,24,rc);}
static inline uint32_t ppc_srw(uint8_t ra,uint8_t rs,uint8_t rb,bool rc=false)
    {return ppc_Xform(31,rs,ra,rb,536,rc);}
static inline uint32_t ppc_sraw(uint8_t ra,uint8_t rs,uint8_t rb,bool rc=false)
    {return ppc_Xform(31,rs,ra,rb,792,rc);}
static inline uint32_t ppc_rlwinm(uint8_t ra,uint8_t rs,uint8_t sh,
                                    uint8_t mb,uint8_t me,bool rc=false)
    {return(21u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)
           |((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1)|(rc?1u:0u);}
static inline uint32_t ppc_rlwimi(uint8_t ra,uint8_t rs,uint8_t sh,
                                    uint8_t mb,uint8_t me,bool rc=false)
    {return(20u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)
           |((uint32_t)sh<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1)|(rc?1u:0u);}
static inline uint32_t ppc_rlwnm(uint8_t ra,uint8_t rs,uint8_t rb,
                                   uint8_t mb,uint8_t me,bool rc=false)
    {return(23u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)
           |((uint32_t)rb<<11)|((uint32_t)mb<<6)|((uint32_t)me<<1)|(rc?1u:0u);}
static inline uint32_t ppc_srawi(uint8_t ra,uint8_t rs,uint8_t sh,bool rc=false)
    {return(31u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)
           |((uint32_t)sh<<11)|(824u<<1)|(rc?1u:0u);}
static inline uint32_t ppc_cntlzw(uint8_t ra,uint8_t rs,bool rc=false)
    {return ppc_Xform(31,rs,ra,0,26,rc);}
static inline uint32_t ppc_cmp(uint8_t cr,uint8_t ra,uint8_t rb)
    {return(31u<<26)|((uint32_t)(cr&7)<<23)|((uint32_t)ra<<16)
           |((uint32_t)rb<<11)|(0u<<1);}
static inline uint32_t ppc_cmpl(uint8_t cr,uint8_t ra,uint8_t rb)
    {return(31u<<26)|((uint32_t)(cr&7)<<23)|((uint32_t)ra<<16)
           |((uint32_t)rb<<11)|(32u<<1);}
static inline uint32_t ppc_mtspr(uint16_t spr,uint8_t rs){
    uint8_t lo=spr&0x1F,hi=(spr>>5)&0x1F;
    return(31u<<26)|((uint32_t)rs<<21)|((uint32_t)lo<<16)
          |((uint32_t)hi<<11)|(467u<<1);}
static inline uint32_t ppc_mfspr(uint8_t rt,uint16_t spr){
    uint8_t lo=spr&0x1F,hi=(spr>>5)&0x1F;
    return(31u<<26)|((uint32_t)rt<<21)|((uint32_t)lo<<16)
          |((uint32_t)hi<<11)|(339u<<1);}
static inline uint32_t ppc_mtctr(uint8_t rs){return ppc_mtspr(9,rs);}
static inline uint32_t ppc_mfctr(uint8_t rt){return ppc_mfspr(rt,9);}
static inline uint32_t ppc_mtlr(uint8_t rs) {return ppc_mtspr(8,rs);}
static inline uint32_t ppc_mflr(uint8_t rt) {return ppc_mfspr(rt,8);}
static inline uint32_t ppc_mtxer(uint8_t rs){return ppc_mtspr(1,rs);}
static inline uint32_t ppc_mfxer(uint8_t rt){return ppc_mfspr(rt,1);}
static inline uint32_t ppc_mfcr(uint8_t rt)
    {return(31u<<26)|((uint32_t)rt<<21)|(19u<<1);}
static inline uint32_t ppc_mtcrf(uint8_t fxm,uint8_t rs)
    {return(31u<<26)|((uint32_t)rs<<21)|((uint32_t)(fxm&0xFF)<<12)|(144u<<1);}
static inline uint32_t ppc_isync(){return(19u<<26)|(150u<<1);}
static inline uint32_t ppc_sync() {return(31u<<26)|(598u<<1);}
static inline uint32_t ppc_eieio(){return(31u<<26)|(854u<<1);}
static inline uint32_t ppc_lwzx(uint8_t rt,uint8_t ra,uint8_t rb)
    {return ppc_Xform(31,rt,ra,rb,23);}
static inline uint32_t ppc_lbzx(uint8_t rt,uint8_t ra,uint8_t rb)
    {return ppc_Xform(31,rt,ra,rb,87);}
static inline uint32_t ppc_lhzx(uint8_t rt,uint8_t ra,uint8_t rb)
    {return ppc_Xform(31,rt,ra,rb,279);}
static inline uint32_t ppc_lhax(uint8_t rt,uint8_t ra,uint8_t rb)
    {return ppc_Xform(31,rt,ra,rb,343);}
static inline uint32_t ppc_stwx(uint8_t rs,uint8_t ra,uint8_t rb)
    {return ppc_Xform(31,rs,ra,rb,151);}
static inline uint32_t ppc_stbx(uint8_t rs,uint8_t ra,uint8_t rb)
    {return ppc_Xform(31,rs,ra,rb,215);}
static inline uint32_t ppc_sthx(uint8_t rs,uint8_t ra,uint8_t rb)
    {return ppc_Xform(31,rs,ra,rb,407);}
static inline uint32_t ppc_stwu(uint8_t rs,int16_t d,uint8_t ra)
    {return(37u<<26)|((uint32_t)rs<<21)|((uint32_t)ra<<16)|(uint16_t)d;}
static inline uint32_t ppc_lwzu(uint8_t rt,int16_t d,uint8_t ra)
    {return(33u<<26)|((uint32_t)rt<<21)|((uint32_t)ra<<16)|(uint16_t)d;}
static inline uint32_t ppc_blr(){return ppc_bclr(20,0);}
static inline uint32_t ppc_extsb(uint8_t ra,uint8_t rs,bool rc=false)
    {return ppc_Xform(31,rs,ra,0,954,rc);}
static inline uint32_t ppc_extsh(uint8_t ra,uint8_t rs,bool rc=false)
    {return ppc_Xform(31,rs,ra,0,922,rc);}

// ============================================================
// emit_li32
// ============================================================
static int emit_li32(uint32_t* out,uint8_t rt,uint32_t imm){
    uint16_t lo=(uint16_t)(imm&0xFFFF),hi=(uint16_t)(imm>>16);
    if(imm==0){out[0]=ppc_addi(rt,0,0);return 1;}
    if(hi==0){
        if(lo<0x8000u){out[0]=ppc_addi(rt,0,(int16_t)lo);return 1;}
        out[0]=ppc_addi(rt,0,0);out[1]=ppc_ori(rt,rt,lo);return 2;
    }
    if(lo==0){out[0]=ppc_addis(rt,0,(int16_t)hi);return 1;}
    out[0]=ppc_addis(rt,0,(int16_t)hi);out[1]=ppc_ori(rt,rt,lo);return 2;
}

// ============================================================
// Register mapping
// ============================================================
static const uint8_t PPC_ARM_R0  =3,  PPC_ARM_R1  =4,
                     PPC_ARM_R2  =5,  PPC_ARM_R3  =6,
                     PPC_ARM_R4  =7,  PPC_ARM_R5  =8,
                     PPC_ARM_R6  =9,  PPC_ARM_R7  =10,
                     PPC_ARM_R8  =14, PPC_ARM_R9  =15,
                     PPC_ARM_R10 =16, PPC_ARM_R11 =17,
                     PPC_ARM_R12 =18, PPC_ARM_R13 =19,
                     PPC_ARM_R14 =20, PPC_ARM_R15 =21,
                     PPC_CPSR    =22, PPC_INTERP  =23,
                     PPC_CORE    =24, PPC_TMP0    =25,
                     PPC_TMP1    =26, PPC_TMP2    =11,
                     PPC_TMP3    =12, PPC_CALL_TMP=29;

static const uint8_t ARM_TO_PPC[16]={
    3,4,5,6,7,8,9,10,14,15,16,17,18,19,20,21
};

#define CPSR_N (1u<<31)
#define CPSR_Z (1u<<30)
#define CPSR_C (1u<<29)
#define CPSR_V (1u<<28)
#define CPSR_T (1u<<5)

// ============================================================
// Code buffer
// ============================================================
static const size_t JIT_CODE_SIZE  =4*1024*1024;
static const size_t JIT_MAX_INSTRS =JIT_CODE_SIZE/4;
static const size_t MAX_BLOCK_SIZE =128;
static const size_t MAX_PPC_PER_ARM=96;

static uint32_t* codeBuffer    =nullptr;
static size_t    codeBufferPos =0;

// ============================================================
// Block cache
// ============================================================
struct JitBlock{
    uint32_t  armPC;
    uint32_t* ppcCode;
    uint32_t  ppcWords;
    uint32_t  armInstrs;
    bool      thumb;
    bool      valid;
};

static const size_t BLOCK_CACHE_SIZE=8192;
static JitBlock blockCache[BLOCK_CACHE_SIZE];

static inline size_t hashPC(uint32_t pc){return(pc>>1)&(BLOCK_CACHE_SIZE-1);}

void flushJitCache(){
    codeBufferPos=0;
    for(size_t i=0;i<BLOCK_CACHE_SIZE;i++) blockCache[i].valid=false;
}

// ============================================================
// Emit context
// ============================================================
struct EmitCtx{
    uint32_t* base; uint32_t* cur; size_t capacity;
    bool thumb,arm7; uint32_t armPC;
    Interpreter* interp; Core* core;
    bool hasExplicitReturn;
    void emit(uint32_t w){if((size_t)(cur-base)<capacity)*cur++=w;}
    size_t size()const{return(size_t)(cur-base);}
};

static void flushCaches(uint32_t* s,size_t w){
    DCFlushRange(s,w*4); ICInvalidateRange(s,w*4);
}

// ============================================================
// Trampoline — guarantees BCTRL semantics so the JIT block's
// mflr captures a valid PPC return address, not a stale LR.
//
// Without this, if executeBlock's compiler-generated call uses
// BCTR (tail-call) instead of BCTRL, the block's prologue saves
// a stale LR (e.g. an ARM address), and the epilogue's blr
// jumps there, causing an ISI at that ARM address.
//
// The trampoline is a hand-coded PPC function that:
//   1. Saves its own LR (per ABI)
//   2. Moves r3 (block address) into CTR
//   3. Uses BCTRL — sets LR = return address within trampoline
//   4. Block's mflr now captures a valid PPC address
//   5. Block's blr returns into trampoline
//   6. Trampoline restores LR and returns to C++ caller
// ============================================================
static uint32_t trampolineCode[8] __attribute__((aligned(32))) = {
    0x7C0802A6,  // mflr  r0
    0x90010004,  // stw   r0, 4(r1)      (ABI: save LR at caller frame +4)
    0x9421FFF0,  // stwu  r1, -16(r1)    (allocate minimal frame)
    0x7C6903A6,  // mtctr r3             (r3 = block->ppcCode, first arg)
    0x4E800421,  // bctrl               (call block; LR = next instr)
    0x80210000,  // lwz   r1, 0(r1)      (restore stack)
    0x80010004,  // lwz   r0, 4(r1)      (restore LR)
    0x4E800020,  // blr                  (return to C++ caller)
};
// Note: the block's prologue does stwu r1,-176(r1) which allocates
// its own frame on top of our minimal 16-byte frame.  The block's
// epilogue deallocates its own frame and blr's back to the instruction
// after bctrl in the trampoline.  The trampoline then restores r1
// via lwz r1,0(r1) (reading back-chain) and returns normally.

static bool trampolineReady = false;

static void initTrampoline(){
    if(!trampolineReady){
        DCFlushRange(trampolineCode,sizeof(trampolineCode));
        ICInvalidateRange(trampolineCode,sizeof(trampolineCode));
        trampolineReady=true;
    }
}

// ============================================================
// Condition helpers
// ============================================================
static void emit_setupCondFlags(EmitCtx& ctx){
    ctx.emit(ppc_rlwinm(PPC_TMP0,PPC_CPSR,0,28,31));
    ctx.emit(ppc_mtcrf(0x80,PPC_TMP0));
}

struct CondBranch{uint8_t bo,bi;bool valid;};
static CondBranch armCondToPpc(uint8_t c){
    switch(c){
        case 0: return{12,2,true}; case 1: return{4,2,true};
        case 2: return{12,1,true}; case 3: return{4,1,true};
        case 4: return{12,3,true}; case 5: return{4,3,true};
        case 6: return{12,0,true}; case 7: return{4,0,true};
        case 14:return{20,0,true}; default:return{0,0,false};
    }
}

// ============================================================
// Struct offsets
// ============================================================
static size_t off_registersUsr=0,off_cpsr=0,off_cycles=0;
static size_t off_halted=0,off_pipeline=0,off_pcData=0;

// ============================================================
// C helpers
// ============================================================
extern "C" {

void JitPpc_syncToInterp(Interpreter* interp,uint32_t* regs){
    uint32_t** p=interp->getRegisters();
    for(int i=0;i<15;i++) *p[i]=regs[i];
    interp->getCpsrRef()=regs[16];
    interp->setPC(regs[15]);
}

void JitPpc_syncFromInterp(Interpreter* interp,uint32_t* regs){
    uint32_t** p=interp->getRegisters();
    for(int i=0;i<15;i++) regs[i]=*p[i];
    regs[16]=interp->getCpsrRef();
}

int JitPpc_interpFallback(Interpreter* interp){
    return interp->jitRunOpcode();
}

uint32_t JitPpc_memRead32(Core* c,bool a7,uint32_t addr)
    {return c->memory.read<uint32_t>(a7,addr);}
uint16_t JitPpc_memRead16(Core* c,bool a7,uint32_t addr)
    {return c->memory.read<uint16_t>(a7,addr);}
uint8_t  JitPpc_memRead8(Core* c,bool a7,uint32_t addr)
    {return c->memory.read<uint8_t>(a7,addr);}
void JitPpc_memWrite32(Core* c,bool a7,uint32_t addr,uint32_t v)
    {c->memory.write<uint32_t>(a7,addr,v);}
void JitPpc_memWrite16(Core* c,bool a7,uint32_t addr,uint16_t v)
    {c->memory.write<uint16_t>(a7,addr,v);}
void JitPpc_memWrite8(Core* c,bool a7,uint32_t addr,uint8_t v)
    {c->memory.write<uint8_t>(a7,addr,v);}

void JitPpc_addCycles(Interpreter* interp,uint32_t cyc){
    uint32_t* p=(uint32_t*)((uint8_t*)interp+off_cycles);
    *p+=cyc;
}

// Advance globalCycles and fire due events.
// globalCycles must advance or scanline/timer events never fire
// and the screen stays black.
void JitPpc_runScheduler(Core* core){
    core->globalCycles+=64;
    while(!core->events.empty()&&
          core->globalCycles>=core->events.front().cycles){
        SchedEvent evt=core->events.front();
        core->events.erase(core->events.begin());
        core->tasks[evt.task]();
    }
}

} // extern "C"

// ============================================================
// emit_call — uses PPC_CALL_TMP (r29), never PPC_TMP3 (r12)
// ============================================================
static void emit_call(EmitCtx& ctx,void* fn){
    uint32_t addr=(uint32_t)(uintptr_t)fn;
    int n=emit_li32(ctx.cur,PPC_CALL_TMP,addr); ctx.cur+=n;
    ctx.emit(ppc_mtctr(PPC_CALL_TMP));
    ctx.emit(ppc_bctr(true));
}

// ============================================================
// Prologue / epilogue
// ============================================================
static void emit_prologue(EmitCtx& ctx,Interpreter* interp,Core* core){
    ctx.emit(ppc_stwu(1,-(int16_t)FRAME_SIZE,1));
    ctx.emit(ppc_mflr(0));
    ctx.emit(ppc_stw(0,FRAME_LR,1));
    for(int r=14;r<=29;r++) ctx.emit(ppc_stw(r,FRAME_R14+(r-14)*4,1));
    int n=emit_li32(ctx.cur,PPC_INTERP,(uint32_t)(uintptr_t)interp);ctx.cur+=n;
    n=emit_li32(ctx.cur,PPC_CORE,(uint32_t)(uintptr_t)core);ctx.cur+=n;
    n=emit_li32(ctx.cur,PPC_ARM_R15,ctx.armPC);ctx.cur+=n;
}

static void emit_epilogue(EmitCtx& ctx){
    for(int r=14;r<=29;r++) ctx.emit(ppc_lwz(r,FRAME_R14+(r-14)*4,1));
    ctx.emit(ppc_lwz(0,FRAME_LR,1));
    ctx.emit(ppc_mtlr(0));
    ctx.emit(ppc_addi(1,1,(int16_t)FRAME_SIZE));
    ctx.emit(ppc_blr());
}

// ============================================================
// Sync helpers
// ============================================================
static void emit_syncToInterp(EmitCtx& ctx){
    for(int i=0;i<16;i++)
        ctx.emit(ppc_stw(ARM_TO_PPC[i],FRAME_REGSYNC+i*4,1));
    ctx.emit(ppc_stw(PPC_CPSR,FRAME_REGSYNC+16*4,1));
    ctx.emit(ppc_mr(3,PPC_INTERP));
    ctx.emit(ppc_addi(4,1,(int16_t)FRAME_REGSYNC));
    emit_call(ctx,(void*)JitPpc_syncToInterp);
}

static void emit_syncFromInterp(EmitCtx& ctx){
    ctx.emit(ppc_mr(3,PPC_INTERP));
    ctx.emit(ppc_addi(4,1,(int16_t)FRAME_REGSYNC));
    emit_call(ctx,(void*)JitPpc_syncFromInterp);
    for(int i=0;i<15;i++)
        ctx.emit(ppc_lwz(ARM_TO_PPC[i],FRAME_REGSYNC+i*4,1));
    ctx.emit(ppc_lwz(PPC_CPSR,FRAME_REGSYNC+16*4,1));
}

// ============================================================
// CPSR flag helpers
// ============================================================
static void emit_updateNZ(EmitCtx& ctx,uint8_t r){
    ctx.emit(ppc_rlwinm(PPC_CPSR,PPC_CPSR,0,2,31));
    ctx.emit(ppc_rlwimi(PPC_CPSR,r,0,0,0));
    ctx.emit(ppc_cmpi(0,r,0));
    ctx.emit(ppc_mfcr(PPC_TMP0));
    ctx.emit(ppc_rlwinm(PPC_TMP0,PPC_TMP0,1,30,30));
    ctx.emit(ppc_or(PPC_CPSR,PPC_CPSR,PPC_TMP0));
}
static void emit_updateC_fromXER(EmitCtx& ctx){
    ctx.emit(ppc_mfxer(PPC_TMP0));
    ctx.emit(ppc_rlwinm(PPC_TMP0,PPC_TMP0,0,2,2));
    ctx.emit(ppc_rlwinm(PPC_CPSR,PPC_CPSR,0,3,1));
    ctx.emit(ppc_or(PPC_CPSR,PPC_CPSR,PPC_TMP0));
}
static void emit_updateV_fromXER(EmitCtx& ctx){
    ctx.emit(ppc_mfxer(PPC_TMP0));
    ctx.emit(ppc_rlwinm(PPC_TMP0,PPC_TMP0,30,29,29));
    ctx.emit(ppc_rlwinm(PPC_CPSR,PPC_CPSR,0,4,2));
    ctx.emit(ppc_or(PPC_CPSR,PPC_CPSR,PPC_TMP0));
}
static void emit_updateNZCV_add(EmitCtx& ctx,uint8_t r)
    {emit_updateNZ(ctx,r);emit_updateC_fromXER(ctx);emit_updateV_fromXER(ctx);}
static void emit_updateNZCV_sub(EmitCtx& ctx,uint8_t r)
    {emit_updateNZ(ctx,r);emit_updateC_fromXER(ctx);emit_updateV_fromXER(ctx);}
static void emit_updateC_fromTMP1(EmitCtx& ctx){
    ctx.emit(ppc_rlwinm(PPC_TMP2,PPC_TMP1,29,29,29));
    ctx.emit(ppc_rlwinm(PPC_CPSR,PPC_CPSR,0,3,1));
    ctx.emit(ppc_or(PPC_CPSR,PPC_CPSR,PPC_TMP2));
}

// ============================================================
// ARM immediate
// ============================================================
static uint32_t armImm(uint32_t op){
    uint32_t imm=op&0xFF,rot=((op>>8)&0xF)*2;
    if(!rot)return imm;
    return(imm>>rot)|(imm<<(32-rot));
}

// ============================================================
// Shift helpers
// ============================================================
static void emit_lsl_imm(EmitCtx& ctx,uint8_t d,uint8_t s,uint8_t i,bool sc=false){
    if(i==0){if(d!=s)ctx.emit(ppc_mr(d,s));if(sc)ctx.emit(ppc_rlwinm(PPC_TMP1,PPC_CPSR,3,31,31));}
    else if(i<32){if(sc)ctx.emit(ppc_rlwinm(PPC_TMP1,s,i,31,31));ctx.emit(ppc_rlwinm(d,s,i,0,31-i));}
    else if(i==32){if(sc)ctx.emit(ppc_rlwinm(PPC_TMP1,s,0,31,31));ctx.emit(ppc_addi(d,0,0));}
    else{if(sc)ctx.emit(ppc_addi(PPC_TMP1,0,0));ctx.emit(ppc_addi(d,0,0));}
}
static void emit_lsr_imm(EmitCtx& ctx,uint8_t d,uint8_t s,uint8_t i,bool sc=false){
    if(i==0||i==32){if(sc)ctx.emit(ppc_rlwinm(PPC_TMP1,s,1,31,31));ctx.emit(ppc_addi(d,0,0));}
    else if(i<32){if(sc)ctx.emit(ppc_rlwinm(PPC_TMP1,s,33-i,31,31));ctx.emit(ppc_rlwinm(d,s,32-i,i,31));}
    else{if(sc)ctx.emit(ppc_addi(PPC_TMP1,0,0));ctx.emit(ppc_addi(d,0,0));}
}
static void emit_asr_imm(EmitCtx& ctx,uint8_t d,uint8_t s,uint8_t i,bool sc=false){
    if(i==0||i>=32){if(sc)ctx.emit(ppc_rlwinm(PPC_TMP1,s,1,31,31));ctx.emit(ppc_srawi(d,s,31));}
    else{if(sc)ctx.emit(ppc_rlwinm(PPC_TMP1,s,33-i,31,31));ctx.emit(ppc_srawi(d,s,i));}
}
static void emit_ror_imm(EmitCtx& ctx,uint8_t d,uint8_t s,uint8_t i,bool sc=false){
    if(i==0){
        ctx.emit(ppc_rlwinm(PPC_TMP1,PPC_CPSR,3,31,31));
        if(sc)ctx.emit(ppc_rlwinm(PPC_TMP0,s,0,31,31));
        ctx.emit(ppc_rlwinm(d,s,31,1,31));
        ctx.emit(ppc_rlwimi(d,PPC_TMP1,31,0,0));
        if(sc)ctx.emit(ppc_mr(PPC_TMP1,PPC_TMP0));
    }else{
        i&=31;if(!i)i=32;
        if(sc)ctx.emit(ppc_rlwinm(PPC_TMP1,s,33-i,31,31));
        ctx.emit(ppc_rlwinm(d,s,32-i,0,31));
    }
}
static void emit_lsl_reg(EmitCtx& ctx,uint8_t d,uint8_t s,uint8_t sr,bool sc=false)
    {if(sc)ctx.emit(ppc_addi(PPC_TMP1,0,0));ctx.emit(ppc_slw(d,s,sr));}
static void emit_lsr_reg(EmitCtx& ctx,uint8_t d,uint8_t s,uint8_t sr,bool sc=false)
    {if(sc)ctx.emit(ppc_addi(PPC_TMP1,0,0));ctx.emit(ppc_srw(d,s,sr));}
static void emit_asr_reg(EmitCtx& ctx,uint8_t d,uint8_t s,uint8_t sr,bool sc=false)
    {if(sc)ctx.emit(ppc_addi(PPC_TMP1,0,0));ctx.emit(ppc_sraw(d,s,sr));}
static void emit_ror_reg(EmitCtx& ctx,uint8_t d,uint8_t s,uint8_t sr,bool sc=false){
    if(sc)ctx.emit(ppc_addi(PPC_TMP1,0,0));
    ctx.emit(ppc_subfic(PPC_TMP0,sr,32));
    ctx.emit(ppc_rlwnm(d,s,PPC_TMP0,0,31));
}

static bool emit_shifterOp(EmitCtx& ctx,uint32_t op,uint8_t dst,bool sc,bool& cv){
    cv=sc;
    bool isImm=(op>>25)&1;
    if(isImm){
        uint32_t v=armImm(op);
        int n=emit_li32(ctx.cur,dst,v);ctx.cur+=n;
        if(sc){uint32_t rot=((op>>8)&0xF)*2;
            if(rot){int n2=emit_li32(ctx.cur,PPC_TMP1,v>>31);ctx.cur+=n2;}
            else cv=false;}
        return true;
    }
    uint8_t rm=op&0xF,pRm=ARM_TO_PPC[rm];
    bool isReg=(op>>4)&1;uint8_t stype=(op>>5)&3;
    if(!isReg){
        uint8_t sa=(op>>7)&0x1F;
        switch(stype){
            case 0:emit_lsl_imm(ctx,dst,pRm,sa,sc);break;
            case 1:emit_lsr_imm(ctx,dst,pRm,sa?sa:32,sc);break;
            case 2:emit_asr_imm(ctx,dst,pRm,sa?sa:32,sc);break;
            case 3:emit_ror_imm(ctx,dst,pRm,sa,sc);break;
        }
    }else{
        uint8_t rs=(op>>8)&0xF,pRs=ARM_TO_PPC[rs];
        ctx.emit(ppc_rlwinm(PPC_TMP2,pRs,0,24,31));
        ctx.emit(ppc_mr(PPC_TMP3,pRm));
        switch(stype){
            case 0:emit_lsl_reg(ctx,dst,PPC_TMP3,PPC_TMP2,sc);break;
            case 1:emit_lsr_reg(ctx,dst,PPC_TMP3,PPC_TMP2,sc);break;
            case 2:emit_asr_reg(ctx,dst,PPC_TMP3,PPC_TMP2,sc);break;
            case 3:emit_ror_reg(ctx,dst,PPC_TMP3,PPC_TMP2,sc);break;
        }
        cv=false;
    }
    return true;
}

// ============================================================
// Data processing
// ============================================================
enum ArmDpOp{
    DP_AND=0,DP_EOR=1,DP_SUB=2,DP_RSB=3,
    DP_ADD=4,DP_ADC=5,DP_SBC=6,DP_RSC=7,
    DP_TST=8,DP_TEQ=9,DP_CMP=10,DP_CMN=11,
    DP_ORR=12,DP_MOV=13,DP_BIC=14,DP_MVN=15
};

static void patchBranchOffset(EmitCtx& ctx,size_t bi,size_t ti){
    int32_t off=(int32_t)((ti-bi)*4);
    ctx.base[bi]=(ctx.base[bi]&0xFFFF0003u)|(uint32_t)(off&0xFFFC);
}

static bool emit_dataProc(EmitCtx& ctx,uint32_t op){
    uint8_t cond=(op>>28)&0xF,dpOp=(op>>21)&0xF;
    bool setCC=(op>>20)&1;
    uint8_t rn=(op>>16)&0xF,rd=(op>>12)&0xF;
    if(rd==15&&setCC)return false;
    if(rd==15)return false;
    uint8_t pRd=ARM_TO_PPC[rd],pRn=ARM_TO_PPC[rn];

    size_t cbi=0;bool hcb=false;
    if(cond!=14){
        emit_setupCondFlags(ctx);
        CondBranch cb=armCondToPpc(cond);
        if(!cb.valid)return false;
        if(cb.bo!=20){cbi=ctx.size();hcb=true;
            ctx.emit(ppc_bc((cb.bo==12)?4:12,cb.bi,0));}
    }

    bool needC=(dpOp==DP_ADC||dpOp==DP_SBC||dpOp==DP_RSC);
    bool needSC=setCC&&(dpOp==DP_AND||dpOp==DP_EOR||dpOp==DP_TST||
                        dpOp==DP_TEQ||dpOp==DP_ORR||dpOp==DP_MOV||
                        dpOp==DP_BIC||dpOp==DP_MVN);
    bool cv=false;
    if(!emit_shifterOp(ctx,op,PPC_TMP0,needSC,cv))return false;

    if(needC){ctx.emit(ppc_rlwinm(PPC_TMP1,PPC_CPSR,0,2,2));ctx.emit(ppc_mtxer(PPC_TMP1));}

    bool test=(dpOp==DP_TST||dpOp==DP_TEQ||dpOp==DP_CMP||dpOp==DP_CMN);
    uint8_t res=test?PPC_TMP1:pRd;

    switch((ArmDpOp)dpOp){
        case DP_AND:case DP_TST:ctx.emit(ppc_and(res,pRn,PPC_TMP0));break;
        case DP_EOR:case DP_TEQ:ctx.emit(ppc_xor(res,pRn,PPC_TMP0));break;
        case DP_SUB:case DP_CMP:ctx.emit(ppc_subfc(res,PPC_TMP0,pRn));break;
        case DP_RSB:            ctx.emit(ppc_subfc(res,pRn,PPC_TMP0));break;
        case DP_ADD:case DP_CMN:ctx.emit(ppc_addc(res,pRn,PPC_TMP0));break;
        case DP_ADC:            ctx.emit(ppc_adde(res,pRn,PPC_TMP0));break;
        case DP_SBC:            ctx.emit(ppc_subfe(res,PPC_TMP0,pRn));break;
        case DP_RSC:            ctx.emit(ppc_subfe(res,pRn,PPC_TMP0));break;
        case DP_ORR:            ctx.emit(ppc_or(res,pRn,PPC_TMP0));break;
        case DP_MOV:if(res!=PPC_TMP0)ctx.emit(ppc_mr(res,PPC_TMP0));break;
        case DP_BIC:            ctx.emit(ppc_andc(res,pRn,PPC_TMP0));break;
        case DP_MVN:            ctx.emit(ppc_nor(res,PPC_TMP0,PPC_TMP0));break;
    }

    if(setCC){
        switch((ArmDpOp)dpOp){
            case DP_ADD:case DP_CMN:case DP_ADC:emit_updateNZCV_add(ctx,res);break;
            case DP_SUB:case DP_CMP:case DP_RSB:
            case DP_SBC:case DP_RSC:emit_updateNZCV_sub(ctx,res);break;
            default:emit_updateNZ(ctx,res);if(cv)emit_updateC_fromTMP1(ctx);break;
        }
    }
    if(hcb)patchBranchOffset(ctx,cbi,ctx.size());
    return true;
}

// ============================================================
// Branch
// ============================================================
static bool emit_branch(EmitCtx& ctx,uint32_t op,uint32_t pc){
    uint8_t cond=(op>>28)&0xF;

    // BX Rm
    if((op&0x0FFFFFF0)==0x012FFF10){
        uint8_t rm=op&0xF;if(rm==15)return false;
        uint8_t pRm=ARM_TO_PPC[rm];
        size_t cbi=0;bool hcb=false;
        if(cond!=14){
            emit_setupCondFlags(ctx);
            CondBranch cb=armCondToPpc(cond);
            if(!cb.valid)return false;
            if(cb.bo!=20){cbi=ctx.size();hcb=true;
                ctx.emit(ppc_bc((cb.bo==12)?4:12,cb.bi,0));}
        }
        ctx.emit(ppc_rlwinm(PPC_CPSR,PPC_CPSR,0,27,25));
        ctx.emit(ppc_rlwinm(PPC_TMP0,pRm,0,31,31));
        ctx.emit(ppc_rlwinm(PPC_TMP0,PPC_TMP0,5,26,26));
        ctx.emit(ppc_or(PPC_CPSR,PPC_CPSR,PPC_TMP0));
        ctx.emit(ppc_rlwinm(PPC_ARM_R15,pRm,0,0,30));
        emit_syncToInterp(ctx);emit_epilogue(ctx);
        if(hcb){
            patchBranchOffset(ctx,cbi,ctx.size());
            int n=emit_li32(ctx.cur,PPC_ARM_R15,pc+4);ctx.cur+=n;
            emit_syncToInterp(ctx);emit_epilogue(ctx);
        }
        ctx.hasExplicitReturn=true;return true;
    }

    // B / BL
    if((op&0x0E000000)==0x0A000000){
        bool lk=(op>>24)&1;
        int32_t off=(int32_t)(op<<8)>>6;
        uint32_t tgt=pc+8+off;
        size_t cbi=0;bool hcb=false;
        if(cond!=14){
            emit_setupCondFlags(ctx);
            CondBranch cb=armCondToPpc(cond);
            if(!cb.valid)return false;
            if(cb.bo!=20){cbi=ctx.size();hcb=true;
                ctx.emit(ppc_bc((cb.bo==12)?4:12,cb.bi,0));}
        }
        if(lk){int n=emit_li32(ctx.cur,PPC_ARM_R14,pc+4);ctx.cur+=n;}
        {int n=emit_li32(ctx.cur,PPC_ARM_R15,tgt);ctx.cur+=n;}
        emit_syncToInterp(ctx);emit_epilogue(ctx);
        if(hcb){
            patchBranchOffset(ctx,cbi,ctx.size());
            int n=emit_li32(ctx.cur,PPC_ARM_R15,pc+4);ctx.cur+=n;
            emit_syncToInterp(ctx);emit_epilogue(ctx);
        }
        ctx.hasExplicitReturn=true;return true;
    }
    return false;
}

// ============================================================
// Load/Store
// ============================================================
static bool emit_loadStore(EmitCtx& ctx,uint32_t op,uint32_t pc){
    uint8_t cond=(op>>28)&0xF;
    bool ld=(op>>20)&1,by=(op>>22)&1,up=(op>>23)&1;
    bool pre=(op>>24)&1,wb=(op>>21)&1,imm=!((op>>25)&1);
    uint8_t rn=(op>>16)&0xF,rd=(op>>12)&0xF;
    if(rd==15||rn==15)return false;
    uint8_t pRn=ARM_TO_PPC[rn],pRd=ARM_TO_PPC[rd];

    size_t cbi=0;bool hcb=false;
    if(cond!=14){
        emit_setupCondFlags(ctx);
        CondBranch cb=armCondToPpc(cond);
        if(!cb.valid)return false;
        if(cb.bo!=20){cbi=ctx.size();hcb=true;
            ctx.emit(ppc_bc((cb.bo==12)?4:12,cb.bi,0));}
    }

    if(imm){uint32_t o=op&0xFFF;int n=emit_li32(ctx.cur,PPC_TMP0,o);ctx.cur+=n;}
    else{uint8_t rm=op&0xF;ctx.emit(ppc_mr(PPC_TMP0,ARM_TO_PPC[rm]));}

    if(pre){
        if(up)ctx.emit(ppc_add(PPC_TMP1,pRn,PPC_TMP0));
        else  ctx.emit(ppc_subf(PPC_TMP1,PPC_TMP0,pRn));
    }else ctx.emit(ppc_mr(PPC_TMP1,pRn));

    for(int i=0;i<8;i++) ctx.emit(ppc_stw(3+i,FRAME_REGSYNC+i*4,1));
    ctx.emit(ppc_mr(3,PPC_CORE));
    ctx.emit(ppc_addi(4,0,ctx.arm7?1:0));
    ctx.emit(ppc_mr(5,PPC_TMP1));
    if(!ld)ctx.emit(ppc_mr(6,pRd));

    void* fn;
    if(ld)fn=by?(void*)JitPpc_memRead8:(void*)JitPpc_memRead32;
    else  fn=by?(void*)JitPpc_memWrite8:(void*)JitPpc_memWrite32;
    emit_call(ctx,fn);

    if(ld)ctx.emit(ppc_mr(PPC_TMP0,3));
    for(int i=0;i<8;i++) ctx.emit(ppc_lwz(3+i,FRAME_REGSYNC+i*4,1));
    if(ld)ctx.emit(ppc_mr(pRd,PPC_TMP0));

    if(!pre){
        if(up)ctx.emit(ppc_add(pRn,pRn,PPC_TMP0));
        else  ctx.emit(ppc_subf(pRn,PPC_TMP0,pRn));
    }else if(wb&&rn!=rd)ctx.emit(ppc_mr(pRn,PPC_TMP1));

    if(hcb)patchBranchOffset(ctx,cbi,ctx.size());
    return true;
}

// ============================================================
// Multiply
// ============================================================
static bool emit_multiply(EmitCtx& ctx,uint32_t op){
    bool sc=(op>>20)&1,acc=(op>>21)&1,lng=(op>>23)&1;
    uint8_t rd=(op>>16)&0xF,rn=(op>>12)&0xF;
    uint8_t rs=(op>>8)&0xF,rm=op&0xF;
    if(rd==15||rm==15||rs==15)return false;
    if(lng)return false;
    uint8_t pRd=ARM_TO_PPC[rd],pRn=ARM_TO_PPC[rn];
    uint8_t pRs=ARM_TO_PPC[rs],pRm=ARM_TO_PPC[rm];
    if(acc){ctx.emit(ppc_mullw(PPC_TMP0,pRm,pRs));ctx.emit(ppc_add(pRd,PPC_TMP0,pRn));}
    else    ctx.emit(ppc_mullw(pRd,pRm,pRs));
    if(sc)emit_updateNZ(ctx,pRd);
    return true;
}

// ============================================================
// Interpreter fallback — ends block
// ============================================================
static void emit_interpFallback(EmitCtx& ctx){
    emit_syncToInterp(ctx);
    ctx.emit(ppc_mr(3,PPC_INTERP));
    emit_call(ctx,(void*)JitPpc_interpFallback);
    emit_syncFromInterp(ctx);
}

// ============================================================
// Thumb emitters
// ============================================================
static bool emit_thumb_lslImm(EmitCtx& ctx,uint16_t op){
    uint8_t rd=op&7,rs=(op>>3)&7,i=(op>>6)&0x1F;
    emit_lsl_imm(ctx,ARM_TO_PPC[rd],ARM_TO_PPC[rs],i,true);
    emit_updateNZ(ctx,ARM_TO_PPC[rd]);emit_updateC_fromTMP1(ctx);return true;}
static bool emit_thumb_lsrImm(EmitCtx& ctx,uint16_t op){
    uint8_t rd=op&7,rs=(op>>3)&7,i=(op>>6)&0x1F;
    emit_lsr_imm(ctx,ARM_TO_PPC[rd],ARM_TO_PPC[rs],i?i:32,true);
    emit_updateNZ(ctx,ARM_TO_PPC[rd]);emit_updateC_fromTMP1(ctx);return true;}
static bool emit_thumb_asrImm(EmitCtx& ctx,uint16_t op){
    uint8_t rd=op&7,rs=(op>>3)&7,i=(op>>6)&0x1F;
    emit_asr_imm(ctx,ARM_TO_PPC[rd],ARM_TO_PPC[rs],i?i:32,true);
    emit_updateNZ(ctx,ARM_TO_PPC[rd]);emit_updateC_fromTMP1(ctx);return true;}

static bool emit_thumb_addSubReg(EmitCtx& ctx,uint16_t op){
    uint8_t rd=op&7,rs=(op>>3)&7;bool sub=(op>>9)&1,imm=(op>>10)&1;
    uint8_t pRd=ARM_TO_PPC[rd],pRs=ARM_TO_PPC[rs];
    if(imm){uint32_t v=(op>>6)&7;int n=emit_li32(ctx.cur,PPC_TMP0,v);ctx.cur+=n;}
    else{uint8_t rn=(op>>6)&7;ctx.emit(ppc_mr(PPC_TMP0,ARM_TO_PPC[rn]));}
    if(sub){ctx.emit(ppc_subfc(pRd,PPC_TMP0,pRs));emit_updateNZCV_sub(ctx,pRd);}
    else   {ctx.emit(ppc_addc(pRd,pRs,PPC_TMP0));emit_updateNZCV_add(ctx,pRd);}
    return true;}

static bool emit_thumb_movImm(EmitCtx& ctx,uint16_t op){
    uint8_t rd=(op>>8)&7;int n=emit_li32(ctx.cur,ARM_TO_PPC[rd],op&0xFF);ctx.cur+=n;
    emit_updateNZ(ctx,ARM_TO_PPC[rd]);return true;}
static bool emit_thumb_cmpImm(EmitCtx& ctx,uint16_t op){
    uint8_t rs=(op>>8)&7;int n=emit_li32(ctx.cur,PPC_TMP0,op&0xFF);ctx.cur+=n;
    ctx.emit(ppc_subfc(PPC_TMP1,PPC_TMP0,ARM_TO_PPC[rs]));
    emit_updateNZCV_sub(ctx,PPC_TMP1);return true;}
static bool emit_thumb_addImm8(EmitCtx& ctx,uint16_t op){
    uint8_t rd=(op>>8)&7;int n=emit_li32(ctx.cur,PPC_TMP0,op&0xFF);ctx.cur+=n;
    ctx.emit(ppc_addc(ARM_TO_PPC[rd],ARM_TO_PPC[rd],PPC_TMP0));
    emit_updateNZCV_add(ctx,ARM_TO_PPC[rd]);return true;}
static bool emit_thumb_subImm8(EmitCtx& ctx,uint16_t op){
    uint8_t rd=(op>>8)&7;int n=emit_li32(ctx.cur,PPC_TMP0,op&0xFF);ctx.cur+=n;
    ctx.emit(ppc_subfc(ARM_TO_PPC[rd],PPC_TMP0,ARM_TO_PPC[rd]));
    emit_updateNZCV_sub(ctx,ARM_TO_PPC[rd]);return true;}

static bool emit_thumb_aluOp(EmitCtx& ctx,uint16_t op){
    uint8_t rd=op&7,rs=(op>>3)&7,o=(op>>6)&0xF;
    uint8_t pRd=ARM_TO_PPC[rd],pRs=ARM_TO_PPC[rs];
    switch(o){
        case 0: ctx.emit(ppc_and(pRd,pRd,pRs));emit_updateNZ(ctx,pRd);break;
        case 1: ctx.emit(ppc_xor(pRd,pRd,pRs));emit_updateNZ(ctx,pRd);break;
        case 2: ctx.emit(ppc_slw(pRd,pRd,pRs));emit_updateNZ(ctx,pRd);break;
        case 3: ctx.emit(ppc_srw(pRd,pRd,pRs));emit_updateNZ(ctx,pRd);break;
        case 4: ctx.emit(ppc_sraw(pRd,pRd,pRs));emit_updateNZ(ctx,pRd);break;
        case 5: ctx.emit(ppc_rlwinm(PPC_TMP0,PPC_CPSR,0,2,2));
                ctx.emit(ppc_mtxer(PPC_TMP0));
                ctx.emit(ppc_adde(pRd,pRd,pRs));emit_updateNZCV_add(ctx,pRd);break;
        case 6: ctx.emit(ppc_rlwinm(PPC_TMP0,PPC_CPSR,0,2,2));
                ctx.emit(ppc_mtxer(PPC_TMP0));
                ctx.emit(ppc_subfe(pRd,pRs,pRd));emit_updateNZCV_sub(ctx,pRd);break;
        case 7: emit_ror_reg(ctx,pRd,pRd,pRs,true);emit_updateNZ(ctx,pRd);break;
        case 8: ctx.emit(ppc_and(PPC_TMP0,pRd,pRs));emit_updateNZ(ctx,PPC_TMP0);break;
        case 9: ctx.emit(ppc_addi(PPC_TMP0,0,0));
                ctx.emit(ppc_subfc(pRd,pRd,PPC_TMP0));emit_updateNZCV_sub(ctx,pRd);break;
        case 10:ctx.emit(ppc_subfc(PPC_TMP0,pRs,pRd));emit_updateNZCV_sub(ctx,PPC_TMP0);break;
        case 11:ctx.emit(ppc_addc(PPC_TMP0,pRd,pRs));emit_updateNZCV_add(ctx,PPC_TMP0);break;
        case 12:ctx.emit(ppc_or(pRd,pRd,pRs));emit_updateNZ(ctx,pRd);break;
        case 13:ctx.emit(ppc_mullw(pRd,pRd,pRs));emit_updateNZ(ctx,pRd);break;
        case 14:ctx.emit(ppc_andc(pRd,pRd,pRs));emit_updateNZ(ctx,pRd);break;
        case 15:ctx.emit(ppc_nor(pRd,pRs,pRs));emit_updateNZ(ctx,pRd);break;
        default:return false;
    }
    return true;
}

static bool emit_thumb_hiRegOp(EmitCtx& ctx,uint16_t op){
    uint8_t o=(op>>8)&3,h1=(op>>7)&1,h2=(op>>6)&1;
    uint8_t rs=((op>>3)&7)|(h2<<3),rd=(op&7)|(h1<<3);
    if(rd==15||rs==15)return false;
    uint8_t pRd=ARM_TO_PPC[rd],pRs=ARM_TO_PPC[rs];
    switch(o){
        case 0:ctx.emit(ppc_add(pRd,pRd,pRs));break;
        case 1:ctx.emit(ppc_subfc(PPC_TMP0,pRs,pRd));emit_updateNZCV_sub(ctx,PPC_TMP0);break;
        case 2:ctx.emit(ppc_mr(pRd,pRs));break;
        case 3:
            ctx.emit(ppc_rlwinm(PPC_CPSR,PPC_CPSR,0,27,25));
            ctx.emit(ppc_rlwinm(PPC_TMP0,pRs,0,31,31));
            ctx.emit(ppc_rlwinm(PPC_TMP0,PPC_TMP0,5,26,26));
            ctx.emit(ppc_or(PPC_CPSR,PPC_CPSR,PPC_TMP0));
            ctx.emit(ppc_rlwinm(PPC_ARM_R15,pRs,0,0,30));
            emit_syncToInterp(ctx);emit_epilogue(ctx);
            ctx.hasExplicitReturn=true;break;
    }
    return true;
}

static bool emit_thumb_ldrPc(EmitCtx& ctx,uint16_t op,uint32_t pc){
    uint8_t rd=(op>>8)&7;
    uint32_t addr=((pc+4)&~3u)+((op&0xFF)<<2);
    int n=emit_li32(ctx.cur,PPC_TMP1,addr);ctx.cur+=n;
    for(int i=0;i<8;i++)ctx.emit(ppc_stw(3+i,FRAME_REGSYNC+i*4,1));
    ctx.emit(ppc_mr(3,PPC_CORE));ctx.emit(ppc_addi(4,0,ctx.arm7?1:0));
    ctx.emit(ppc_mr(5,PPC_TMP1));
    emit_call(ctx,(void*)JitPpc_memRead32);
    ctx.emit(ppc_mr(PPC_TMP0,3));
    for(int i=0;i<8;i++)ctx.emit(ppc_lwz(3+i,FRAME_REGSYNC+i*4,1));
    ctx.emit(ppc_mr(ARM_TO_PPC[rd],PPC_TMP0));return true;
}

static bool emit_thumb_branch(EmitCtx& ctx,uint16_t op,uint32_t pc){
    uint8_t cond=(op>>8)&0xF;
    if(cond==0xF)return false;
    if(cond==0xE){
        int32_t off=(int8_t)(op&0xFF);off<<=1;
        uint32_t tgt=pc+4+off;
        int n=emit_li32(ctx.cur,PPC_ARM_R15,tgt);ctx.cur+=n;
        emit_syncToInterp(ctx);emit_epilogue(ctx);
        ctx.hasExplicitReturn=true;return true;
    }
    int32_t off=(int8_t)(op&0xFF);off<<=1;
    uint32_t tgt=pc+4+off,fall=pc+2;
    emit_setupCondFlags(ctx);
    CondBranch cb=armCondToPpc(cond);if(!cb.valid)return false;
    size_t cbi=ctx.size();
    ctx.emit(ppc_bc((cb.bo==12)?4:12,cb.bi,0));
    {int n=emit_li32(ctx.cur,PPC_ARM_R15,tgt);ctx.cur+=n;}
    emit_syncToInterp(ctx);emit_epilogue(ctx);
    patchBranchOffset(ctx,cbi,ctx.size());
    {int n=emit_li32(ctx.cur,PPC_ARM_R15,fall);ctx.cur+=n;}
    emit_syncToInterp(ctx);emit_epilogue(ctx);
    ctx.hasExplicitReturn=true;return true;
}

static bool emit_thumb_bl(EmitCtx& ctx,uint16_t op1,uint16_t op2,uint32_t pc){
    int32_t hi=(int32_t)((op1&0x7FF)<<21)>>9,lo=(op2&0x7FF)<<1;
    uint32_t tgt=pc+4+hi+lo,ret=pc+4;
    int n=emit_li32(ctx.cur,PPC_ARM_R14,ret|1u);ctx.cur+=n;
    n=emit_li32(ctx.cur,PPC_ARM_R15,tgt&~1u);ctx.cur+=n;
    uint8_t bb=(op2>>11)&0x1F;
    if(bb==0x1C){ctx.emit(ppc_rlwinm(PPC_CPSR,PPC_CPSR,0,27,25));
                 ctx.emit(ppc_rlwinm(PPC_ARM_R15,PPC_ARM_R15,0,0,29));}
    emit_syncToInterp(ctx);emit_epilogue(ctx);
    ctx.hasExplicitReturn=true;return true;
}

// ============================================================
// ARM dispatch
// ============================================================
static bool emitARMInstr(EmitCtx& ctx,uint32_t op,uint32_t pc){
    uint8_t cond=(op>>28)&0xF;if(cond==15)return false;
    uint32_t it=(op>>25)&7;
    switch(it){
        case 0:case 1:
            if((op&0x0FC000F0)==0x00000090)return emit_multiply(ctx,op);
            if((op&0x0FFFFFF0)==0x012FFF10||(op&0x0FFFFFF0)==0x012FFF30)
                return emit_branch(ctx,op,pc);
            if((op&0x0FB00FF0)==0x01000000||(op&0x0FB00000)==0x03200000||
               (op&0x0DB0F000)==0x010F0000)return false;
            return emit_dataProc(ctx,op);
        case 2:case 3:return emit_loadStore(ctx,op,pc);
        case 4:return false;
        case 5:return emit_branch(ctx,op,pc);
        case 6:return false;
        case 7:
            if((op&0x0F000000)==0x0F000000)return false;
            if((op&0x0F000010)==0x0E000010)return false;
            return false;
    }
    return false;
}

// ============================================================
// Thumb dispatch
// ============================================================
static bool emitThumbInstr(EmitCtx& ctx,uint16_t op,uint32_t pc){
    uint8_t b14=(op>>14)&3,b11=(op>>11)&7;
    switch(b14){
        case 0:
            switch(b11){
                case 0:return emit_thumb_lslImm(ctx,op);
                case 1:return emit_thumb_lsrImm(ctx,op);
                case 2:return emit_thumb_asrImm(ctx,op);
                case 3:return emit_thumb_addSubReg(ctx,op);
                case 4:return emit_thumb_movImm(ctx,op);
                case 5:return emit_thumb_cmpImm(ctx,op);
                case 6:return emit_thumb_addImm8(ctx,op);
                case 7:return emit_thumb_subImm8(ctx,op);
            }break;
        case 1:{
            uint8_t b10=(op>>10)&3;
            if(b10==0)return emit_thumb_aluOp(ctx,op);
            if(b10==1)return emit_thumb_hiRegOp(ctx,op);
            if(b11==0x09)return emit_thumb_ldrPc(ctx,op,pc);
            return false;}
        case 2:return false;
        case 3:{
            uint8_t b12=(op>>12)&0xF;
            if(b12==0xD||b12==0xE)return emit_thumb_branch(ctx,op,pc);
            return false;}
    }
    return false;
}

// ============================================================
// PC validity — prevent compiling from unmapped regions
// ============================================================
static bool isValidArmPC(uint32_t pc,bool isGba){
    pc&=~1u;
    if(isGba){
        return(pc<=0x00003FFEu)||
               (pc>=0x02000000u&&pc<=0x0203FFFEu)||
               (pc>=0x03000000u&&pc<=0x03007FFEu)||
               (pc>=0x08000000u&&pc<=0x0DFFFFFFu);
    }else{
        return(pc<=0x00003FFEu)||
               (pc>=0x01000000u&&pc<=0x013FFFFEu)||
               (pc>=0x02000000u&&pc<=0x023FFFFEu)||
               (pc>=0x03000000u&&pc<=0x037FFFFEu)||
               (pc>=0xFFFF0000u);
    }
}

// ============================================================
// Block compiler
// ============================================================
static JitBlock* compileBlock(Interpreter* interp,Core* core,
                               uint32_t armPC,bool arm7){
    if(!isValidArmPC(armPC,core->gbaMode)){
        printf("[JIT] bad PC 0x%08X (arm7=%d gba=%d)\n",armPC,arm7,core->gbaMode);
        return nullptr;
    }

    bool isThumb=interp->isThumb();
    size_t bucket=hashPC(armPC);
    JitBlock& slot=blockCache[bucket];
    if(slot.valid&&slot.armPC==armPC&&slot.thumb==isThumb)return &slot;

    if(codeBufferPos+MAX_BLOCK_SIZE*MAX_PPC_PER_ARM+512>=JIT_MAX_INSTRS)
        flushJitCache();

    EmitCtx ctx;
    ctx.base=codeBuffer+codeBufferPos;ctx.cur=ctx.base;
    ctx.capacity=JIT_MAX_INSTRS-codeBufferPos;
    ctx.thumb=isThumb;ctx.arm7=arm7;ctx.armPC=armPC;
    ctx.interp=interp;ctx.core=core;ctx.hasExplicitReturn=false;

    emit_prologue(ctx,interp,core);
    emit_syncFromInterp(ctx);

    uint32_t pc=armPC;int ic=0;bool ended=false;

    for(ic=0;ic<(int)MAX_BLOCK_SIZE&&!ended;ic++){
        {int n=emit_li32(ctx.cur,PPC_ARM_R15,pc+(isThumb?4u:8u));ctx.cur+=n;}

        if(isThumb){
            uint16_t top=core->memory.read<uint16_t>(arm7,pc);
            uint8_t b11=(top>>11)&0x1F;
            if(b11==0x1E){
                uint16_t bot=core->memory.read<uint16_t>(arm7,pc+2);
                uint8_t bb=(bot>>11)&0x1F;
                if(bb==0x1F||bb==0x1C){
                    if(emit_thumb_bl(ctx,top,bot,pc)){pc+=4;ic++;ended=true;continue;}
                }
            }
            bool ok=emitThumbInstr(ctx,top,pc);
            if(!ok){emit_interpFallback(ctx);ended=true;}
            else{
                pc+=2;
                if(((top>>12)&0xF)==0xD||((top>>12)&0xF)==0xE)ended=true;
                if(ctx.hasExplicitReturn)ended=true;
            }
        }else{
            uint32_t op=core->memory.read<uint32_t>(arm7,pc);
            bool ok=emitARMInstr(ctx,op,pc);
            if(!ok){emit_interpFallback(ctx);ended=true;}
            else{
                pc+=4;
                uint32_t it=(op>>25)&7;
                if(it==5||(op&0x0FFFFFF0)==0x012FFF10||
                   (op&0x0FFFFFF0)==0x012FFF30)ended=true;
                if(it==4&&((op>>20)&1)&&(op&(1u<<15)))ended=true;
                if(ctx.hasExplicitReturn)ended=true;
            }
        }
    }

    if(!ctx.hasExplicitReturn){emit_syncToInterp(ctx);emit_epilogue(ctx);}

    slot.armPC=armPC;slot.ppcCode=ctx.base;
    slot.ppcWords=(uint32_t)ctx.size();slot.armInstrs=(uint32_t)ic;
    slot.thumb=isThumb;slot.valid=true;
    codeBufferPos+=ctx.size();
    flushCaches(ctx.base,ctx.size());
    return &slot;
}

// ============================================================
// Execute block via trampoline
// ============================================================
static void executeBlock(const JitBlock* block){
    typedef void(*TrampolineFn)(void*);
    ((TrampolineFn)trampolineCode)(block->ppcCode);
}

// ============================================================
// Run entry points
// ============================================================
void runJitNds(Core& core){
    if(!core.interpreter[0].halted){
        uint32_t pc=core.interpreter[0].getActualPC();
        JitBlock* b=compileBlock(&core.interpreter[0],&core,pc,false);
        if(b)executeBlock(b);
        else core.interpreter[0].jitRunOpcode();
    }
    if(!core.interpreter[1].halted){
        uint32_t pc=core.interpreter[1].getActualPC();
        JitBlock* b=compileBlock(&core.interpreter[1],&core,pc,true);
        if(b)executeBlock(b);
        else core.interpreter[1].jitRunOpcode();
    }
    JitPpc_runScheduler(&core);
}

void runJitGba(Core& core){
    if(!core.interpreter[1].halted){
        uint32_t pc=core.interpreter[1].getActualPC();
        JitBlock* b=compileBlock(&core.interpreter[1],&core,pc,true);
        if(b)executeBlock(b);
        else core.interpreter[1].jitRunOpcode();
    }
    JitPpc_runScheduler(&core);
}

// ============================================================
// Offset computation
// ============================================================
static void computeOffsets(){
    off_halted      =Interpreter::offset_halted();
    off_pcData      =Interpreter::offset_pcData();
    off_pipeline    =Interpreter::offset_pipeline();
    off_registersUsr=Interpreter::offset_registersUsr();
    off_cpsr        =Interpreter::offset_cpsr();
    off_cycles      =Interpreter::offset_cycles();
    printf("[JIT] halted=%zu pcData=%zu pipeline=%zu "
           "regUsr=%zu cpsr=%zu cycles=%zu\n",
           off_halted,off_pcData,off_pipeline,
           off_registersUsr,off_cpsr,off_cycles);
}

// ============================================================
// initJit / shutdownJit / invalidateJitRange
// ============================================================
bool initJit(Core* core){
    computeOffsets();
    initTrampoline();

    codeBuffer=(uint32_t*)memalign(32,JIT_CODE_SIZE);
    if(!codeBuffer){printf("[JIT] alloc failed\n");return false;}
    codeBufferPos=0;
    for(size_t i=0;i<BLOCK_CACHE_SIZE;i++)blockCache[i].valid=false;

    printf("[JIT] ready buf=%p %zuKB\n",(void*)codeBuffer,JIT_CODE_SIZE/1024);

    if(core)core->setRunFunc(core->gbaMode?runJitGba:runJitNds);
    return true;
}

void shutdownJit(Core* core){
    if(core)core->setRunFunc(core->gbaMode
        ?static_cast<void(*)(Core&)>(&Interpreter::runCoreSingle<true,0>)
        :&Interpreter::runCoreNds);
    if(codeBuffer){free(codeBuffer);codeBuffer=nullptr;}
}

void invalidateJitRange(uint32_t start,uint32_t end){
    for(size_t i=0;i<BLOCK_CACHE_SIZE;i++)
        if(blockCache[i].valid&&blockCache[i].armPC>=start&&
           blockCache[i].armPC<end)blockCache[i].valid=false;
}

} // namespace JitPpc
