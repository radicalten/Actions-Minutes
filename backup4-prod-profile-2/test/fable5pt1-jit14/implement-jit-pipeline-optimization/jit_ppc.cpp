// jit_ppc.cpp — ARM → PPC JIT (Wii/Broadway)
//
// Review fix plan applied ("ARM7/ARM9 → Broadway JIT", payoff ÷ effort order):
//
//  P0.1  No pipeline refill on normal exits. JitHelp_commit writes r0-r14 + CPSR and
//        sets r15 to exactly what setPC() would have produced (self-calibrated once per
//        CPU/mode, so it does not depend on the interpreter's r15 convention), marks the
//        pipeline dirty, and the runner refills lazily only when the interpreter must run.
//        Register sync at block entry is 30 inline loads (pointer, then value) - no C call,
//        no isReady()/null checks in the hot path (they happen once when the stub is built).
//  P0.2  One shared exit stub (spill + commit + epilogue) and one per-CPU entry stub
//        (prologue + register sync + bctr) emitted once. Blocks exit with
//        li TE,pc ; li TF,reason ; li TG,cycles ; b exitStub  (4-5 words).
//  P0.3  Conditional B/BL/BX/LDM-pc/LDR-pc/ALU-pc/Thumb Bcc emit only the taken exit and
//        keep compiling the fall-through. BLK_ARMS=48, BLK_WDS=4096, BLK_MARGIN=192.
//        Blocks never cross a 4 KB page (keeps page invalidation exact).
//  P0.4  Code buffer in cached MEM2 (16 MB, SYS_AllocArena2MemLo) with a MEM1 fallback;
//        MEM1 address restrictions removed from Ctx::call()/initJit(). Direct-mapped
//        cache replaced by a two-level page table: pageTab[cpu][pc>>12] -> 2048 slots
//        indexed by (pc & 0xFFF) >> 1, entry = code | thumb. Lookup = 2 loads, no
//        eviction, invalidation = zero the page's slot array.
//  P0.5  Hot fallbacks compiled: Rm=PC in immediate shifter (mov lr,pc / add rX,pc,rY),
//        BLX #imm (ARM9), LDR pc,[..] (+ ARM LDR/LDRH rX,[pc,#imm] literals),
//        ALU writes to PC (mov pc,lr), Thumb BLX suffix guarded on ARM7.
//        SWI intentionally stays a fallback: performing exception() from the runner needs
//        the interpreter's "r15 during execution" convention (+8/+4 vs resting state),
//        which differs between forks; the fallback is correct by construction. If you add
//        `int jitException(uint8_t v){return exception(v);}` to Interpreter, flip
//        JIT_INLINE_SWI to 1 below and set r15 = pc + (thumb ? 4 : 8) there.
//  P1.a  Condition codes inline: mtcrf 0x80,RCPSR puts NZCV in CR0; simple conditions are
//        one bc, compound ones one/two CR-logical ops into cr1. JitHelp_testCond removed.
//  Nits  emitMrsMsr tests all operands before emitting; lookup/compile split; page-table
//        removes the direct-mapped eviction leak; runner does no per-block sanity dance;
//        per-block cycle accounting (1 + memory) replaces the fixed 64 cycles per 24 blocks;
//        RRX temp no longer clobbers the shifter destination when dst == TA.
//
// Still open (P1.b/c/d, P2): dead-flag elimination, inline page-map memory fast path,
// inline PUSH/POP/LDM/STM and register shifts, dispatcher loop + block linking.
// Every piece of emitted code here carries over to those.

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
#include <ogc/system.h>
}

#ifndef JIT_INLINE_SWI
#define JIT_INLINE_SWI 0
#endif

static const int EXIT_NORMAL   = 0;
static const int EXIT_FALLBACK = 1;
static const int EXIT_SWI      = 2;   // only used when JIT_INLINE_SWI

static uint32_t g_exitPC[2]      = {};
static uint32_t g_exitCycles[2]  = {};
static int      g_exitReason[2]  = {};
static bool     g_pipeDirty[2]   = {};     // interpreter pipeline stale after a JIT exit
static bool     g_r15Calib[2][2] = {};     // [cpu][thumb] : r15 offset learned from setPC()
static uint32_t g_r15Off[2][2]   = {};
static uint32_t g_cpuTime[2]     = {};     // per-CPU timeline in globalCycles units
static uint32_t* volatile g_nextBlock = nullptr;   // read by the entry stub

// Frame (256 B). LR at FRAME_SIZE+4 (EABI).
static const int FRAME_SIZE    = 256;
static const int FRAME_LR_OFF  = FRAME_SIZE + 4;
static const int FRAME_SAVE    = 16;   // r14..r31
static const int FRAME_CORE    = 88;
static const int FRAME_INTERP  = 92;
static const int FRAME_CPUIDX  = 96;
static const int FRAME_SCR0    = 100;
static const int FRAME_SCR1    = 104;
static const int FRAME_SCR2    = 108;
static const int FRAME_REGSYNC = 112;  // r0..r14
static const int FRAME_CPSR    = 172;
static const int FRAME_PC      = 176;

static_assert(FRAME_SIZE % 16 == 0, "align");
static_assert(FRAME_SAVE + 18 * 4 == FRAME_CORE, "save map");
static_assert(FRAME_REGSYNC + 15 * 4 <= FRAME_CPSR, "regsync");

namespace JitPpc {

// ========================= PPC encoders =========================
static inline uint32_t ppc_blr() { return 0x4E800020u; }
static inline uint32_t ppc_bctr(bool lk = false) {
    return (19u << 26) | (20u << 21) | (528u << 1) | (lk ? 1u : 0u);
}
static inline uint32_t ppc_bc(uint8_t bo, uint8_t bi, int16_t off, bool lk = false) {
    return (16u << 26) | ((bo & 31u) << 21) | ((bi & 31u) << 16) |
           ((uint32_t)(off & 0xFFFC)) | (lk ? 1u : 0u);
}
static inline uint32_t ppc_b(int32_t off, bool lk = false) {
    return (18u << 26) | ((uint32_t)off & 0x03FFFFFCu) | (lk ? 1u : 0u);
}
static inline uint32_t ppc_addi(uint8_t rt, uint8_t ra, int16_t i) {
    return (14u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) | (uint16_t)i;
}
static inline uint32_t ppc_addis(uint8_t rt, uint8_t ra, int16_t i) {
    return (15u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) | (uint16_t)i;
}
static inline uint32_t ppc_ori(uint8_t ra, uint8_t rs, uint16_t i) {
    return (24u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | i;
}
static inline uint32_t ppc_stwu(uint8_t rs, int16_t d, uint8_t ra) {
    return (37u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | (uint16_t)d;
}
static inline uint32_t ppc_stw(uint8_t rs, int16_t d, uint8_t ra) {
    return (36u << 26) | ((uint32_t)rs << 21) | ((uint32_t)ra << 16) | (uint16_t)d;
}
static inline uint32_t ppc_lwz(uint8_t rt, int16_t d, uint8_t ra) {
    return (32u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) | (uint16_t)d;
}
static inline uint32_t ppc_cmpi(uint8_t cr, uint8_t ra, int16_t i) {
    return (11u << 26) | ((cr & 7u) << 23) | ((uint32_t)ra << 16) | (uint16_t)i;
}
static inline uint32_t ppc_subfic(uint8_t rt, uint8_t ra, int16_t i) {
    return (8u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) | (uint16_t)i;
}
static inline uint32_t Xf(uint8_t rt, uint8_t ra, uint8_t rb, uint32_t x, bool rc = false) {
    return (31u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) |
           ((uint32_t)rb << 11) | (x << 1) | (rc ? 1u : 0u);
}
static inline uint32_t XOf(uint8_t rt, uint8_t ra, uint8_t rb, bool oe, uint32_t x, bool rc = false) {
    return (31u << 26) | ((uint32_t)rt << 21) | ((uint32_t)ra << 16) |
           ((uint32_t)rb << 11) | (oe ? 0x400u : 0u) | (x << 1) | (rc ? 1u : 0u);
}
static inline uint32_t ppc_add  (uint8_t d, uint8_t a, uint8_t b) { return XOf(d, a, b, false, 266); }
static inline uint32_t ppc_addc (uint8_t d, uint8_t a, uint8_t b) { return XOf(d, a, b, false, 10); }
static inline uint32_t ppc_adde (uint8_t d, uint8_t a, uint8_t b) { return XOf(d, a, b, false, 138); }
static inline uint32_t ppc_subf (uint8_t d, uint8_t a, uint8_t b) { return XOf(d, a, b, false, 40); }
static inline uint32_t ppc_subfc(uint8_t d, uint8_t a, uint8_t b) { return XOf(d, a, b, false, 8); }
static inline uint32_t ppc_subfe(uint8_t d, uint8_t a, uint8_t b) { return XOf(d, a, b, false, 136); }
static inline uint32_t ppc_mullw(uint8_t d, uint8_t a, uint8_t b) { return XOf(d, a, b, false, 235); }
static inline uint32_t ppc_and  (uint8_t a, uint8_t s, uint8_t b) { return Xf(s, a, b, 28); }
static inline uint32_t ppc_or   (uint8_t a, uint8_t s, uint8_t b) { return Xf(s, a, b, 444); }
static inline uint32_t ppc_xor  (uint8_t a, uint8_t s, uint8_t b) { return Xf(s, a, b, 316); }
static inline uint32_t ppc_andc (uint8_t a, uint8_t s, uint8_t b) { return Xf(s, a, b, 60); }
static inline uint32_t ppc_nor  (uint8_t a, uint8_t s, uint8_t b) { return Xf(s, a, b, 124); }
static inline uint32_t ppc_mr   (uint8_t a, uint8_t s)            { return ppc_or(a, s, s); }
static inline uint32_t ppc_slw  (uint8_t a, uint8_t s, uint8_t b) { return Xf(s, a, b, 24); }
static inline uint32_t ppc_srw  (uint8_t a, uint8_t s, uint8_t b) { return Xf(s, a, b, 536); }
static inline uint32_t ppc_sraw (uint8_t a, uint8_t s, uint8_t b) { return Xf(s, a, b, 792); }
static inline uint32_t ppc_extsb(uint8_t a, uint8_t s)            { return Xf(s, a, 0, 954); }
static inline uint32_t ppc_extsh(uint8_t a, uint8_t s)            { return Xf(s, a, 0, 922); }
static inline uint32_t ppc_rlwinm(uint8_t a, uint8_t s, uint8_t sh,
                                  uint8_t mb, uint8_t me, bool rc = false) {
    return (21u << 26) | ((uint32_t)s << 21) | ((uint32_t)a << 16) |
           ((uint32_t)sh << 11) | ((uint32_t)mb << 6) | ((uint32_t)me << 1) | (rc ? 1u : 0u);
}
static inline uint32_t ppc_rlwimi(uint8_t a, uint8_t s, uint8_t sh, uint8_t mb, uint8_t me) {
    return (20u << 26) | ((uint32_t)s << 21) | ((uint32_t)a << 16) |
           ((uint32_t)sh << 11) | ((uint32_t)mb << 6) | ((uint32_t)me << 1);
}
static inline uint32_t ppc_rlwnm(uint8_t a, uint8_t s, uint8_t b, uint8_t mb, uint8_t me) {
    return (23u << 26) | ((uint32_t)s << 21) | ((uint32_t)a << 16) |
           ((uint32_t)b << 11) | ((uint32_t)mb << 6) | ((uint32_t)me << 1);
}
static inline uint32_t ppc_srawi(uint8_t a, uint8_t s, uint8_t sh) {
    return (31u << 26) | ((uint32_t)s << 21) | ((uint32_t)a << 16) |
           ((uint32_t)sh << 11) | (824u << 1);
}
static inline uint32_t ppc_mtspr(uint16_t spr, uint8_t rs) {
    uint8_t lo = spr & 31, hi = (spr >> 5) & 31;
    return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)lo << 16) |
           ((uint32_t)hi << 11) | (467u << 1);
}
static inline uint32_t ppc_mfspr(uint8_t rt, uint16_t spr) {
    uint8_t lo = spr & 31, hi = (spr >> 5) & 31;
    return (31u << 26) | ((uint32_t)rt << 21) | ((uint32_t)lo << 16) |
           ((uint32_t)hi << 11) | (339u << 1);
}
static inline uint32_t ppc_mtctr(uint8_t s) { return ppc_mtspr(9, s); }
static inline uint32_t ppc_mtlr (uint8_t s) { return ppc_mtspr(8, s); }
static inline uint32_t ppc_mflr (uint8_t t) { return ppc_mfspr(t, 8); }
static inline uint32_t ppc_mtxer(uint8_t s) { return ppc_mtspr(1, s); }
static inline uint32_t ppc_mfxer(uint8_t t) { return ppc_mfspr(t, 1); }
static inline uint32_t ppc_mfcr (uint8_t t) {
    return (31u << 26) | ((uint32_t)t << 21) | (19u << 1);
}
// mtcrf FXM,RS  (XFX form, XO=144). FXM bit 7 (0x80) selects CR0.
static inline uint32_t ppc_mtcrf(uint8_t fxm, uint8_t rs) {
    return (31u << 26) | ((uint32_t)rs << 21) | ((uint32_t)fxm << 12) | (144u << 1);
}
// CR-logical ops (XL form)
static inline uint32_t ppc_crop(uint32_t xo, uint8_t bt, uint8_t ba, uint8_t bb) {
    return (19u << 26) | ((uint32_t)bt << 21) | ((uint32_t)ba << 16) | ((uint32_t)bb << 11) | (xo << 1);
}
#define CRANDC(bt, ba, bb) ppc_crop(129, bt, ba, bb)
#define CREQV(bt, ba, bb)  ppc_crop(289, bt, ba, bb)

static int emit_li32(uint32_t* out, uint8_t rt, uint32_t v) {
    uint16_t hi = (uint16_t)(v >> 16), lo = (uint16_t)(v & 0xFFFF);
    if (!hi && !lo) { out[0] = ppc_addi(rt, 0, 0); return 1; }
    if (!hi) {
        if (lo < 0x8000) { out[0] = ppc_addi(rt, 0, (int16_t)lo); return 1; }
        out[0] = ppc_addi(rt, 0, 0);
        out[1] = ppc_ori(rt, rt, lo);
        return 2;
    }
    if (!lo) { out[0] = ppc_addis(rt, 0, (int16_t)hi); return 1; }
    out[0] = ppc_addis(rt, 0, (int16_t)hi);
    out[1] = ppc_ori(rt, rt, lo);
    return 2;
}

// r14-r28 = ARM r0-r14, r29 = CPSR; temps r3-r10; r11 = call; r12 = flag scratch
static const uint8_t RA[15] = {14,15,16,17,18,19,20,21,22,23,24,25,26,27,28};
static const uint8_t RCPSR = 29;
static const uint8_t TA = 3, TB = 4, TC = 5, TD = 6, TE = 7, TF = 8, TG = 9;
static const uint8_t TH = 10;   // result temp for TST/TEQ/CMP/CMN/ALU-pc (never the shifter carry)
static const uint8_t TX = 12;   // scratch for flag code ONLY – never holds an operand
static const uint8_t RCALL = 11;

// ========================= code buffer / block table =========================
static const size_t JIT_BYTES_MEM2   = 16u * 1024u * 1024u;
static const size_t JIT_BYTES_MEM1   = 2u * 1024u * 1024u;      // fallback if MEM2 is exhausted
static const size_t PAGE_SLOTS       = 2048;                    // (pc & 0xFFF) >> 1
static const size_t PAGE_ARENA_BYTES = 2u * 1024u * 1024u;      // 256 slot arrays
static const size_t PAGE_ARENA_CNT   = PAGE_ARENA_BYTES / (PAGE_SLOTS * 4);
static const size_t STUB_WORDS       = 512;                     // reserved at buffer start
static const size_t TOP_PAGES        = 0x10010;                 // pc < 0x10000000, + 16 BIOS pages

static const size_t BLK_ARMS   = 48;
static const size_t BLK_WDS    = 4096;
static const size_t BLK_MARGIN = 192;   // largest single emitter + exit

static void*     g_arena     = nullptr;   // raw allocation (code + page arena), never freed
static size_t    g_arenaBytes= 0;
static uint32_t* codeBuf     = nullptr;
static size_t    g_jitWords  = 0;
static size_t    codePos     = 0;
static size_t    g_stubPos   = 0;
static uint32_t* g_exitStub  = nullptr;
static uint32_t* g_entryStub[2] = {nullptr, nullptr};
static bool      g_jitLive   = false;
static uint32_t  g_dbgFB     = 0;

static uint32_t* g_pageArena = nullptr;
static size_t    g_pageUsed  = 0;
static uint32_t* g_pageTab[2][TOP_PAGES];
static uint32_t  g_livePages[PAGE_ARENA_CNT];
static size_t    g_nLive     = 0;

// A block whose first instruction is not JIT-able maps to this sentinel:
// the runner steps the interpreter directly without entering generated code.
static uint32_t  g_fbSentinel[4] __attribute__((aligned(16)));

static inline int32_t pageIndex(uint32_t pc) {
    if (pc < 0x10000000u) return (int32_t)(pc >> 12);
    if (pc >= 0xFFFF0000u) return (int32_t)(0x10000u + ((pc >> 12) & 0xFu));
    return -1;
}

static void flushICache(uint32_t* p, size_t nW) {
    DCFlushRange(p, nW * 4);
    ICInvalidateRange(p, nW * 4);
}

void flushJitCache() {
    codePos = STUB_WORDS;
    for (size_t i = 0; i < g_nLive; i++) {
        uint32_t v = g_livePages[i];
        g_pageTab[v >> 20][v & 0xFFFFFu] = nullptr;
    }
    g_nLive = 0;
    g_pageUsed = 0;
}

static uint32_t* lookupBlock(int cpu, uint32_t pc, bool thumb) {
    int32_t pi = pageIndex(pc);
    if (pi < 0) return nullptr;
    uint32_t* pg = g_pageTab[cpu][pi];
    if (!pg) return nullptr;
    uint32_t e = pg[(pc & 0xFFFu) >> 1];
    if (!e || (e & 1u) != (thumb ? 1u : 0u)) return nullptr;
    return (uint32_t*)(uintptr_t)(e & ~1u);
}

// Returns (allocating on first use) the slot array for pc's page. May flush the cache.
static uint32_t* getPage(int cpu, uint32_t pc) {
    int32_t pi = pageIndex(pc);
    if (pi < 0 || !g_pageArena) return nullptr;
    if (g_pageTab[cpu][pi]) return g_pageTab[cpu][pi];
    if (g_pageUsed >= PAGE_ARENA_CNT) flushJitCache();
    uint32_t* pg = g_pageArena + g_pageUsed * PAGE_SLOTS;
    g_pageUsed++;
    memset(pg, 0, PAGE_SLOTS * 4);
    g_pageTab[cpu][pi] = pg;
    g_livePages[g_nLive++] = ((uint32_t)cpu << 20) | (uint32_t)pi;
    return pg;
}

// ========================= emit context =========================
struct Ctx {
    uint32_t *base, *cur;
    size_t cap;
    bool thumb, arm7, done, overflow;
    uint32_t blockPC;
    uint32_t cycles;          // estimated cycles from block start to the current point
    int cpuIdx;
    Interpreter* interp;
    Core* core;

    void E(uint32_t w) {
        if ((size_t)(cur - base) < cap) *cur++ = w;
        else overflow = true;
    }
    size_t sz()  const { return (size_t)(cur - base); }
    size_t rem() const { size_t u = sz(); return u < cap ? cap - u : 0; }

    void li(uint8_t rt, uint32_t v) {
        uint32_t t[2];
        int n = emit_li32(t, rt, v);
        for (int i = 0; i < n; i++) E(t[i]);
    }

    void call(void* fn) {              // any 32-bit address (helpers live in MEM1, code in MEM2)
        li(RCALL, (uint32_t)(uintptr_t)fn);
        E(ppc_mtctr(RCALL));
        E(ppc_bctr(true));
    }

    void ldCore()   { E(ppc_lwz(TA, FRAME_CORE,   1)); }
    void ldInterp() { E(ppc_lwz(TA, FRAME_INTERP, 1)); }
    void ldCpu()    { E(ppc_lwz(TB, FRAME_CPUIDX, 1)); }
};

// ========================= C helpers =========================
extern "C" {

// Commit r0-r14 + CPSR. r15 is set to what setPC(pc) would produce (learned once per
// CPU/mode from setPC itself), but the pipeline is NOT refilled: the next thing to run is
// almost always another block. The runner refills lazily before any interpreter step.
int JitHelp_commit(Interpreter* interp, int cpu,
                   uint32_t* regs, uint32_t cpsr,
                   uint32_t pc, int reason, uint32_t cycles) {
    uint32_t** p = interp->getRegisters();
    for (int i = 0; i < 15; i++) *p[i] = regs[i];
    interp->getCpsrRef() = cpsr;

    const int m = (cpsr >> 5) & 1;            // T bit
    pc &= m ? ~1u : ~3u;

    if (g_r15Calib[cpu][m]) {
        *p[15] = pc + g_r15Off[cpu][m];
        g_pipeDirty[cpu] = true;
    } else {                                  // first exit in this mode: learn the offset
        interp->setPC(pc);
        g_r15Off[cpu][m]   = *p[15] - pc;
        g_r15Calib[cpu][m] = true;
        g_pipeDirty[cpu]   = false;
    }

    g_exitPC[cpu]     = pc;
    g_exitReason[cpu] = reason;
    g_exitCycles[cpu] = cycles;
    return 0;
}

uint32_t JitHelp_r32(Core* c, int a, uint32_t ad) {
    return c->memory.read<uint32_t>((bool)a, ad);
}
// Single-word LDR: ARM rotates an unaligned word load by (addr & 3) * 8.
uint32_t JitHelp_ldr32(Core* c, int a, uint32_t ad) {
    uint32_t v = c->memory.read<uint32_t>((bool)a, ad);
    if (ad & 3u) { unsigned sh = (ad & 3u) * 8u; v = (v >> sh) | (v << (32u - sh)); }
    return v;
}
uint16_t JitHelp_r16(Core* c, int a, uint32_t ad) {
    return c->memory.read<uint16_t>((bool)a, ad);
}
uint8_t JitHelp_r8(Core* c, int a, uint32_t ad) {
    return c->memory.read<uint8_t>((bool)a, ad);
}
void JitHelp_w32(Core* c, int a, uint32_t ad, uint32_t v) {
    c->memory.write<uint32_t>((bool)a, ad, v);
}
void JitHelp_w16(Core* c, int a, uint32_t ad, uint16_t v) {
    c->memory.write<uint16_t>((bool)a, ad, v);
}
void JitHelp_w8(Core* c, int a, uint32_t ad, uint8_t v) {
    c->memory.write<uint8_t>((bool)a, ad, v);
}

int JitHelp_armBlock(Core* core, int arm7, uint32_t op,
                     uint32_t* regs, uint32_t pcForR15,
                     uint32_t* pcOut, uint32_t* cpsrInOut) {
    const bool p = (op >> 24) & 1;
    const bool u = (op >> 23) & 1;
    const bool S = (op >> 22) & 1;
    const bool w = (op >> 21) & 1;
    const bool l = (op >> 20) & 1;
    const uint8_t rn = (op >> 16) & 0xF;
    const uint16_t list = (uint16_t)(op & 0xFFFF);

    if (S || rn > 14 || !list) return -1;

    int n = 0;
    for (int i = 0; i < 16; i++) if (list & (1u << i)) n++;

    const uint32_t base = regs[rn];
    uint32_t addr, wb;
    if (u) {
        wb = base + (uint32_t)n * 4u;
        addr = p ? base + 4u : base;
    } else {
        wb = base - (uint32_t)n * 4u;
        addr = p ? wb : wb + 4u;
    }

    int wrotePC = 0;
    if (l) {
        for (int i = 0; i < 16; i++) {
            if (!(list & (1u << i))) continue;
            uint32_t val = core->memory.read<uint32_t>((bool)arm7, addr);
            addr += 4;
            if (i == 15) {
                if (!arm7 && (val & 1u)) {              // ARM9 interworks
                    *cpsrInOut |= 1u << 5;
                    *pcOut = val & ~1u;
                } else {                                // ARM7 (ARMv4T) stays ARM
                    *pcOut = val & ~3u;
                }
                wrotePC = 1;
            } else {
                regs[i] = val;
            }
        }
        if (w && !(list & (1u << rn)))
            regs[rn] = wb;
    } else {
        for (int i = 0; i < 16; i++) {
            if (!(list & (1u << i))) continue;
            uint32_t val = (i == 15) ? pcForR15 : regs[i];
            core->memory.write<uint32_t>((bool)arm7, addr, val);
            addr += 4;
        }
        if (w) regs[rn] = wb;
    }
    return wrotePC;
}

int JitHelp_thumbPushPop(Core* core, int arm7, uint32_t op,
                         uint32_t* regs, uint32_t* pcOut, uint32_t* cpsrInOut) {
    const bool load = (op >> 11) & 1;
    const bool R    = (op >> 8) & 1;
    const uint8_t list = (uint8_t)(op & 0xFF);

    int n = 0;
    for (int i = 0; i < 8; i++) if (list & (1u << i)) n++;
    if (R) n++;

    if (!load) {
        uint32_t sp = regs[13] - (uint32_t)n * 4u;
        uint32_t addr = sp;
        for (int i = 0; i < 8; i++) {
            if (!(list & (1u << i))) continue;
            core->memory.write<uint32_t>((bool)arm7, addr, regs[i]);
            addr += 4;
        }
        if (R)
            core->memory.write<uint32_t>((bool)arm7, addr, regs[14]);
        regs[13] = sp;
        return 0;
    }

    uint32_t addr = regs[13];
    for (int i = 0; i < 8; i++) {
        if (!(list & (1u << i))) continue;
        regs[i] = core->memory.read<uint32_t>((bool)arm7, addr);
        addr += 4;
    }
    int wrotePC = 0;
    if (R) {
        uint32_t val = core->memory.read<uint32_t>((bool)arm7, addr);
        addr += 4;
        if (!arm7 && !(val & 1u)) {                     // ARM9 -> ARM
            *cpsrInOut &= ~(1u << 5);
            *pcOut = val & ~3u;
        } else {                                        // stay Thumb
            *pcOut = val & ~1u;
        }
        wrotePC = 1;
    }
    regs[13] = addr;
    return wrotePC;
}

int JitHelp_thumbBlock(Core* core, int arm7, uint32_t op, uint32_t* regs) {
    const bool load = (op >> 11) & 1;
    const uint8_t rb = (op >> 8) & 7;
    const uint8_t list = (uint8_t)(op & 0xFF);

    if (!list) {
        regs[rb] += 0x40;
        return 0;
    }

    uint32_t addr = regs[rb];
    uint32_t wb = addr;
    for (int i = 0; i < 8; i++) if (list & (1u << i)) wb += 4;
    const bool rbIn = (list & (1u << rb)) != 0;

    if (load) {
        for (int i = 0; i < 8; i++) {
            if (!(list & (1u << i))) continue;
            regs[i] = core->memory.read<uint32_t>((bool)arm7, addr);
            addr += 4;
        }
        if (!rbIn) regs[rb] = wb;
    } else {
        for (int i = 0; i < 8; i++) {
            if (!(list & (1u << i))) continue;
            core->memory.write<uint32_t>((bool)arm7, addr, regs[i]);
            addr += 4;
        }
        regs[rb] = wb;
    }
    return 0;
}

void JitHelp_tick(Core* core, uint32_t cycles) {
    if (!core) return;
    core->globalCycles += cycles;
    while (!core->events.empty() &&
           (int32_t)(core->globalCycles - core->events.front().cycles) >= 0) {
        SchedEvent e = core->events.front();
        core->events.erase(core->events.begin());
        if (e.task >= 0 && e.task < MAX_TASKS && core->tasks[e.task].fn)
            core->tasks[e.task]();
    }
}

} // extern "C"

// ========================= shared stubs / exits =========================
static void emitEpilogue(Ctx& ctx) {
    for (int r = 14; r <= 31; r++)
        ctx.E(ppc_lwz(r, FRAME_SAVE + (r - 14) * 4, 1));
    ctx.E(ppc_lwz(0, (int16_t)FRAME_LR_OFF, 1));
    ctx.E(ppc_mtlr(0));
    ctx.E(ppc_addi(1, 1, (int16_t)FRAME_SIZE));
    ctx.E(ppc_blr());
}

static void emitSpill(Ctx& ctx) {
    for (int i = 0; i < 15; i++)
        ctx.E(ppc_stw(RA[i], FRAME_REGSYNC + i * 4, 1));
    ctx.E(ppc_stw(RCPSR, FRAME_CPSR, 1));
}

static void emitReload(Ctx& ctx) {
    for (int i = 0; i < 15; i++)
        ctx.E(ppc_lwz(RA[i], FRAME_REGSYNC + i * 4, 1));
    ctx.E(ppc_lwz(RCPSR, FRAME_CPSR, 1));
}

// Exit stub: expects TE = pc, TF = reason, TG = cycles.
static void emitExitStubBody(Ctx& ctx) {
    emitSpill(ctx);
    ctx.ldInterp();                                  // r3
    ctx.ldCpu();                                     // r4
    ctx.E(ppc_addi(TC, 1, (int16_t)FRAME_REGSYNC));  // r5
    ctx.E(ppc_mr(TD, RCPSR));                        // r6  (r7,r8,r9 already set)
    ctx.call((void*)JitHelp_commit);
    emitEpilogue(ctx);
}

static void emitJumpExitStub(Ctx& ctx) {
    if (!g_exitStub || ctx.rem() == 0) { ctx.overflow = true; return; }
    int32_t d = (int32_t)((intptr_t)g_exitStub - (intptr_t)ctx.cur);
    ctx.E(ppc_b(d));
}

static void emitCommitExit(Ctx& ctx, uint32_t nextPC, int reason) {
    ctx.li(TE, nextPC);
    ctx.E(ppc_addi(TF, 0, (int16_t)reason));
    ctx.li(TG, ctx.cycles);
    emitJumpExitStub(ctx);
}

static void emitCommitExitDyn(Ctx& ctx, int reason) {
    ctx.E(ppc_lwz(TE, FRAME_PC, 1));
    ctx.E(ppc_addi(TF, 0, (int16_t)reason));
    ctx.li(TG, ctx.cycles);
    emitJumpExitStub(ctx);
}

static bool buildExitStub() {
    if (g_exitStub) return true;
    Ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.base = codeBuf + g_stubPos;
    ctx.cur  = ctx.base;
    ctx.cap  = STUB_WORDS - g_stubPos;
    emitExitStubBody(ctx);
    if (ctx.overflow) return false;
    flushICache(ctx.base, ctx.sz());
    g_stubPos += ctx.sz();
    g_exitStub = ctx.base;
    return true;
}

// Entry stub (per CPU): prologue, frame constants, inline register sync, jump to block.
static bool ensureEntryStub(int cpu, Interpreter* interp, Core* core) {
    if (g_entryStub[cpu]) return true;
    uint32_t** regs = interp->getRegisters();
    if (!regs) return false;
    for (int i = 0; i < 16; i++) if (!regs[i]) return false;

    Ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.base = codeBuf + g_stubPos;
    ctx.cur  = ctx.base;
    ctx.cap  = STUB_WORDS - g_stubPos;

    ctx.E(ppc_mflr(0));
    ctx.E(ppc_stwu(1, -(int16_t)FRAME_SIZE, 1));
    ctx.E(ppc_stw(0, (int16_t)FRAME_LR_OFF, 1));
    for (int r = 14; r <= 31; r++)
        ctx.E(ppc_stw(r, FRAME_SAVE + (r - 14) * 4, 1));

    ctx.li(TA, (uint32_t)(uintptr_t)core);   ctx.E(ppc_stw(TA, FRAME_CORE, 1));
    ctx.li(TA, (uint32_t)(uintptr_t)interp); ctx.E(ppc_stw(TA, FRAME_INTERP, 1));
    ctx.E(ppc_addi(TA, 0, (int16_t)cpu));    ctx.E(ppc_stw(TA, FRAME_CPUIDX, 1));

    // Inline sync: registers[] is an array of pointers (banked per mode) -> load ptr, then value.
    ctx.li(TB, (uint32_t)(uintptr_t)regs);
    for (int i = 0; i < 15; i++) {
        ctx.E(ppc_lwz(TA, (int16_t)(i * 4), TB));
        ctx.E(ppc_lwz(RA[i], 0, TA));
    }
    ctx.li(TA, (uint32_t)(uintptr_t)&interp->getCpsrRef());
    ctx.E(ppc_lwz(RCPSR, 0, TA));

    ctx.li(TA, (uint32_t)(uintptr_t)&g_nextBlock);
    ctx.E(ppc_lwz(TA, 0, TA));
    ctx.E(ppc_mtctr(TA));
    ctx.E(ppc_bctr(false));

    if (ctx.overflow) return false;
    flushICache(ctx.base, ctx.sz());
    g_stubPos += ctx.sz();
    g_entryStub[cpu] = ctx.base;
    return true;
}

// ========================= conditions / branch patching =========================
// mtcrf 0x80,RCPSR : CR0.LT=N (bit0) CR0.GT=Z (bit1) CR0.EQ=C (bit2) CR0.SO=V (bit3); cr1 bit4 scratch.
// Returns the index of the skip branch (taken when the condition is FALSE), or SIZE_MAX.
static size_t emitCondSkip(Ctx& ctx, uint8_t cond) {
    if (cond >= 14) return SIZE_MAX;
    ctx.E(ppc_mtcrf(0x80, RCPSR));
    uint8_t bi; bool skipIfTrue;
    switch (cond) {
        case 0:  bi = 1; skipIfTrue = false; break;                         // EQ : skip if !Z
        case 1:  bi = 1; skipIfTrue = true;  break;                         // NE
        case 2:  bi = 2; skipIfTrue = false; break;                         // CS
        case 3:  bi = 2; skipIfTrue = true;  break;                         // CC
        case 4:  bi = 0; skipIfTrue = false; break;                         // MI
        case 5:  bi = 0; skipIfTrue = true;  break;                         // PL
        case 6:  bi = 3; skipIfTrue = false; break;                         // VS
        case 7:  bi = 3; skipIfTrue = true;  break;                         // VC
        case 8:  ctx.E(CRANDC(4, 2, 1)); bi = 4; skipIfTrue = false; break; // HI : C && !Z
        case 9:  ctx.E(CRANDC(4, 2, 1)); bi = 4; skipIfTrue = true;  break; // LS
        case 10: ctx.E(CREQV (4, 0, 3)); bi = 4; skipIfTrue = false; break; // GE : N == V
        case 11: ctx.E(CREQV (4, 0, 3)); bi = 4; skipIfTrue = true;  break; // LT
        case 12: ctx.E(CREQV (4, 0, 3)); ctx.E(CRANDC(4, 4, 1)); bi = 4; skipIfTrue = false; break; // GT
        default: ctx.E(CREQV (4, 0, 3)); ctx.E(CRANDC(4, 4, 1)); bi = 4; skipIfTrue = true;  break; // LE
    }
    size_t idx = ctx.sz();
    ctx.E(ppc_bc(skipIfTrue ? 12 : 4, bi, 0));   // BO=12: branch if bit set, BO=4: if clear
    return idx;
}

static void patchSkip(Ctx& ctx, size_t idx) {
    if (idx == SIZE_MAX) return;
    int32_t off = (int32_t)((ctx.sz() - idx) * 4);
    if (off < -32768 || off > 32764) { ctx.overflow = true; return; }
    ctx.base[idx] = (ctx.base[idx] & 0xFFFF0003u) | ((uint32_t)off & 0xFFFCu);   // keep BO/BI
}

static void patchBc(Ctx& ctx, size_t idx, uint8_t bo, uint8_t bi) {
    int32_t off = (int32_t)((ctx.sz() - idx) * 4);
    if (off < -32768 || off > 32764) { ctx.overflow = true; return; }
    ctx.base[idx] = ppc_bc(bo, bi, (int16_t)off);
}
static void patchB(Ctx& ctx, size_t idx) {
    ctx.base[idx] = ppc_b((int32_t)((ctx.sz() - idx) * 4));
}

// ========================= flags =========================
static void setNZ(Ctx& ctx, uint8_t r) {
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 2, 31));   // clear N,Z
    ctx.E(ppc_rlwimi(RCPSR, r, 0, 0, 0));        // N = r[31]
    ctx.E(ppc_cmpi(6, r, 0));
    ctx.E(ppc_mfcr(TX));
    ctx.E(ppc_rlwinm(TX, TX, 25, 1, 1));         // CR6.EQ -> bit 30
    ctx.E(ppc_or(RCPSR, RCPSR, TX));
}
static void setC_xer(Ctx& ctx) {
    ctx.E(ppc_mfxer(TX));
    ctx.E(ppc_rlwinm(TX, TX, 0, 2, 2));          // keep CA (bit 29)
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 3, 1));    // clear C
    ctx.E(ppc_or(RCPSR, RCPSR, TX));
}
static void setV_add(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    ctx.E(ppc_xor(TE, res, a));
    ctx.E(ppc_xor(TF, res, b));
    ctx.E(ppc_and(TE, TE, TF));
    ctx.E(ppc_rlwinm(TE, TE, 29, 3, 3));         // bit31 -> bit28 (V)
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 4, 2));    // clear V
    ctx.E(ppc_or(RCPSR, RCPSR, TE));
}
static void setV_sub(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    ctx.E(ppc_xor(TE, a, b));
    ctx.E(ppc_xor(TF, a, res));
    ctx.E(ppc_and(TE, TE, TF));
    ctx.E(ppc_rlwinm(TE, TE, 29, 3, 3));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 4, 2));
    ctx.E(ppc_or(RCPSR, RCPSR, TE));
}
static void setC_bit0(Ctx& ctx, uint8_t cr) {
    ctx.E(ppc_rlwinm(TX, cr, 29, 2, 2));         // bit0 -> bit29
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 3, 1));
    ctx.E(ppc_or(RCPSR, RCPSR, TX));
}

// ========================= shifter =========================
static inline uint8_t rotBitTo0(int bit) { return (uint8_t)((32 - bit) & 31); }

static void sLslI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i == 0) {
        if (d != s) ctx.E(ppc_mr(d, s));
        if (sc) ctx.E(ppc_rlwinm(TC, RCPSR, 3, 31, 31));
    } else if (i < 32) {
        if (sc) ctx.E(ppc_rlwinm(TC, s, (uint8_t)i, 31, 31));
        ctx.E(ppc_rlwinm(d, s, (uint8_t)i, 0, (uint8_t)(31 - i)));
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
        if (sc) ctx.E(ppc_rlwinm(TC, s, 1, 31, 31));                 // C = bit31
        ctx.E(ppc_addi(d, 0, 0));
    } else if (i < 32) {
        if (sc) ctx.E(ppc_rlwinm(TC, s, rotBitTo0(i - 1), 31, 31));  // C = bit(i-1)
        ctx.E(ppc_rlwinm(d, s, (uint8_t)(32 - i), (uint8_t)i, 31));
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
        if (sc) ctx.E(ppc_rlwinm(TC, s, rotBitTo0(i - 1), 31, 31));
        ctx.E(ppc_srawi(d, s, (uint8_t)i));
    }
}
static void sRorI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i == 0) {                                                    // RRX
        if (sc) ctx.E(ppc_rlwinm(TC, s, 0, 31, 31));
        ctx.E(ppc_rlwinm(TG, RCPSR, 2, 0, 0));                       // C -> bit31 (TG: never d)
        ctx.E(ppc_rlwinm(d, s, 31, 1, 31));
        ctx.E(ppc_or(d, d, TG));
        return;
    }
    i &= 31;
    if (i == 0) {                                                    // ROR #32
        if (d != s) ctx.E(ppc_mr(d, s));
        if (sc) ctx.E(ppc_rlwinm(TC, s, 1, 31, 31));
        return;
    }
    if (sc) ctx.E(ppc_rlwinm(TC, s, rotBitTo0(i - 1), 31, 31));
    ctx.E(ppc_rlwinm(d, s, (uint8_t)(32 - i), 0, 31));
}

// Returns true if TC holds the shifter carry-out.
static bool emitShifter(Ctx& ctx, uint32_t op, uint8_t dst, bool sc, uint32_t curPC) {
    if ((op >> 25) & 1) {
        uint32_t v = op & 0xFF;
        uint32_t rot = ((op >> 8) & 0xF) * 2;
        if (rot) v = (v >> rot) | (v << (32 - rot));
        ctx.li(dst, v);
        if (sc && rot) {
            ctx.E(ppc_rlwinm(TC, dst, 1, 31, 31));
            return true;
        }
        return false;
    }
    uint8_t rm = op & 0xF;
    uint8_t st = (op >> 5) & 3;
    if (!((op >> 4) & 1)) {
        uint8_t src;
        if (rm == 15) { ctx.li(TB, curPC + 8u); src = TB; }   // r15 = PC+8 for immediate shifts
        else          src = RA[rm];
        int sa = (op >> 7) & 0x1F;
        switch (st) {
            case 0: sLslI(ctx, dst, src, sa, sc); break;
            case 1: sLsrI(ctx, dst, src, sa ? sa : 32, sc); break;
            case 2: sAsrI(ctx, dst, src, sa ? sa : 32, sc); break;
            default: sRorI(ctx, dst, src, sa, sc); break;
        }
        return sc;
    }
    uint8_t rs = (op >> 8) & 0xF;
    if (rm == 15 || rs == 15) return false;      // PC+12 quirk -> callers reject before emitting
    ctx.E(ppc_rlwinm(TD, RA[rs], 0, 24, 31));
    ctx.E(ppc_mr(TA, RA[rm]));
    switch (st) {
        case 0: ctx.E(ppc_slw(dst, TA, TD)); break;
        case 1: ctx.E(ppc_srw(dst, TA, TD)); break;
        case 2: ctx.E(ppc_sraw(dst, TA, TD)); break;
        default:
            ctx.E(ppc_subfic(TB, TD, 32));
            ctx.E(ppc_rlwnm(dst, TA, TB, 0, 31));
            break;
    }
    return false;
}

// ========================= dynamic jumps =========================
// Interworking jump: value in TA. T = bit0, PC = value & ~1 (commit aligns further).
static void emitBX_TA(Ctx& ctx) {
    ctx.E(ppc_rlwinm(TB, TA, 0, 0, 30));
    ctx.E(ppc_stw(TB, FRAME_PC, 1));
    ctx.E(ppc_rlwinm(TC, TA, 5, 26, 26));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 27, 25));
    ctx.E(ppc_or(RCPSR, RCPSR, TC));
    emitCommitExitDyn(ctx, EXIT_NORMAL);
}
static void emitBX_SCR0(Ctx& ctx) {
    ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
    emitBX_TA(ctx);
}

// Jump without touching the T bit (Thumb ADD/MOV pc)
static void emitJumpSameMode_SCR0(Ctx& ctx) {
    ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
    ctx.E(ppc_rlwinm(TB, TA, 0, 0, 30));      // & ~1
    ctx.E(ppc_stw(TB, FRAME_PC, 1));
    emitCommitExitDyn(ctx, EXIT_NORMAL);
}

// ========================= ARM emitters =========================
enum DP { AND=0,EOR,SUB,RSB,ADD,ADC,SBC,RSC,TST,TEQ,CMP,CMN,ORR,MOV,BIC,MVN };

static bool emitDP(Ctx& ctx, uint32_t op, uint32_t curPC) {
    const uint8_t cond = (op >> 28) & 0xF;
    const uint8_t dop  = (op >> 21) & 0xF;
    const bool s = (op >> 20) & 1;
    const uint8_t rn = (op >> 16) & 0xF;
    const uint8_t rd = (op >> 12) & 0xF;
    const bool imm = (op >> 25) & 1;
    const bool regShift = !imm && ((op >> 4) & 1);

    if (cond == 15) return false;

    const bool isTest = (dop == TST || dop == TEQ || dop == CMP || dop == CMN);
    if (isTest && !s) return false;          // MRS/MSR/BX/CLZ/QADD/SMUL space, never a DP op
    if (rd == 15 && (s || isTest)) return false;   // SPSR restore -> interpreter

    // Register-specified shifts with any PC operand (PC+12 quirk) -> interpreter
    if (regShift && (rn == 15 || (op & 0xF) == 15 || ((op >> 8) & 0xF) == 15)) return false;

    const bool logical = (dop == AND || dop == EOR || dop == TST || dop == TEQ ||
                          dop == ORR || dop == MOV || dop == BIC || dop == MVN);
    if (s && regShift && logical) return false;    // carry from reg shift not modelled

    size_t si = emitCondSkip(ctx, cond);

    const bool rnIsPC = (rn == 15);
    if (rnIsPC) {
        ctx.li(TD, curPC + 8u);
        ctx.E(ppc_stw(TD, FRAME_SCR2, 1));
    }
    if (dop == ADC || dop == SBC || dop == RSC) {
        ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2));
        ctx.E(ppc_mtxer(TA));
    }

    const bool logC = s && logical;
    const bool cset = emitShifter(ctx, op, TA, logC, curPC);

    uint8_t srcRn = RA[rn];
    if (rnIsPC) {
        ctx.E(ppc_lwz(TD, FRAME_SCR2, 1));
        srcRn = TD;
    }

    const bool needV = s && (dop == ADD || dop == SUB || dop == RSB || dop == CMN ||
                             dop == CMP || dop == ADC || dop == SBC || dop == RSC);
    if (needV) {
        ctx.E(ppc_stw(TA, FRAME_SCR0, 1));
        ctx.E(ppc_stw(srcRn, FRAME_SCR1, 1));
    }

    // Test results and PC writes go to TH (r10) – never TC, which holds the shifter carry-out.
    const uint8_t res = (isTest || rd == 15) ? TH : RA[rd];

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
        case MVN:           ctx.E(ppc_nor  (res, TA, TA)); break;
    }

    if (s) {
        uint8_t opA = srcRn, opB = TA;
        if (needV) {
            ctx.E(ppc_lwz(TA, FRAME_SCR0, 1)); opB = TA;
            ctx.E(ppc_lwz(TD, FRAME_SCR1, 1)); opA = TD;
        }
        switch ((DP)dop) {
            case ADD: case CMN: case ADC:
                setNZ(ctx, res); setC_xer(ctx); setV_add(ctx, res, opA, opB); break;
            case SUB: case CMP: case SBC:
                setNZ(ctx, res); setC_xer(ctx); setV_sub(ctx, res, opA, opB); break;
            case RSB: case RSC:
                setNZ(ctx, res); setC_xer(ctx); setV_sub(ctx, res, opB, opA); break;
            default:
                setNZ(ctx, res);
                if (cset) setC_bit0(ctx, TC);
                break;
        }
    }

    if (rd == 15) {                                  // mov pc,lr / add pc,pc,rX : no interwork (ARMv4/v5)
        ctx.E(ppc_rlwinm(TB, TH, 0, 0, 29));
        ctx.E(ppc_stw(TB, FRAME_PC, 1));
        emitCommitExitDyn(ctx, EXIT_NORMAL);
        if (si == SIZE_MAX) ctx.done = true;
        else patchSkip(ctx, si);                     // conditional: fall through
        return true;
    }
    patchSkip(ctx, si);
    return true;
}

static bool emitBX(Ctx& ctx, uint32_t op, uint32_t curPC) {
    (void)curPC;
    uint8_t cond = (op >> 28) & 0xF;
    uint8_t rm = op & 0xF;
    if (rm == 15 || cond == 15) return false;

    size_t si = emitCondSkip(ctx, cond);
    ctx.E(ppc_mr(TA, RA[rm]));
    emitBX_TA(ctx);
    if (si == SIZE_MAX) { ctx.done = true; return true; }
    patchSkip(ctx, si);                              // not taken: keep going
    return true;
}

// BLX #imm (cond field == 1111, bits 27..25 == 101): ARM9 only, always switches to Thumb.
static bool emitBlxImm(Ctx& ctx, uint32_t op, uint32_t curPC) {
    if (ctx.arm7) return false;
    int32_t off = ((int32_t)(op << 8) >> 6) | (int32_t)(((op >> 24) & 1u) << 1);
    uint32_t tgt = (curPC + 8u + (uint32_t)off) & ~1u;
    ctx.li(RA[14], curPC + 4);
    ctx.E(ppc_ori(RCPSR, RCPSR, 0x20));             // set T
    emitCommitExit(ctx, tgt, EXIT_NORMAL);
    ctx.done = true;
    return true;
}

static bool emitBranch(Ctx& ctx, uint32_t op, uint32_t curPC) {
    if ((op & 0x0FFFFFF0) == 0x012FFF10) return emitBX(ctx, op, curPC);
    if ((op & 0x0FFFFFF0) == 0x012FFF30) return false;      // BLX Rm -> interpreter
    if ((op & 0x0E000000) != 0x0A000000) return false;

    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;
    bool lk = (op >> 24) & 1;
    int32_t off = (int32_t)(op << 8) >> 6;
    uint32_t tgt = curPC + 8u + (uint32_t)off;

    size_t si = emitCondSkip(ctx, cond);
    if (lk) ctx.li(RA[14], curPC + 4);
    emitCommitExit(ctx, tgt, EXIT_NORMAL);           // taken: leave the block
    if (si == SIZE_MAX) { ctx.done = true; return true; }
    patchSkip(ctx, si);                              // not taken: keep going
    return true;
}

static bool emitLS(Ctx& ctx, uint32_t op, uint32_t curPC) {
    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;

    bool ld = (op >> 20) & 1;
    bool by = (op >> 22) & 1;
    bool up = (op >> 23) & 1;
    bool pre = (op >> 24) & 1;
    bool wb = (op >> 21) & 1;
    bool immO = !((op >> 25) & 1);
    uint8_t rn = (op >> 16) & 0xF;
    uint8_t rd = (op >> 12) & 0xF;

    if (!immO && ((op & 0xF) == 15 || ((op >> 4) & 1))) return false;
    if (rn == 15 && (!pre || wb || !immO)) return false;   // literal loads only
    if (rd == 15 && (!ld || by)) return false;             // STR pc / LDRB pc -> interpreter

    size_t si = emitCondSkip(ctx, cond);

    if (immO) {
        ctx.li(TA, op & 0xFFF);
    } else {
        uint8_t rm = op & 0xF;
        uint8_t sh = (op >> 5) & 3;
        int sa = (op >> 7) & 0x1F;
        if (sh == 0) sLslI(ctx, TA, RA[rm], sa, false);
        else if (sh == 1) sLsrI(ctx, TA, RA[rm], sa ? sa : 32, false);
        else if (sh == 2) sAsrI(ctx, TA, RA[rm], sa ? sa : 32, false);
        else sRorI(ctx, TA, RA[rm], sa, false);
    }

    if (rn == 15) {                                  // ldr rX,[pc,#imm] : constant address
        uint32_t base = curPC + 8u;
        ctx.li(TB, up ? base + (op & 0xFFF) : base - (op & 0xFFF));
    } else if (pre) {
        if (up) ctx.E(ppc_add(TB, RA[rn], TA));
        else    ctx.E(ppc_subf(TB, TA, RA[rn]));
    } else {
        ctx.E(ppc_mr(TB, RA[rn]));
    }

    ctx.E(ppc_stw(TA, FRAME_SCR0, 1));
    ctx.E(ppc_stw(TB, FRAME_SCR1, 1));
    ctx.ldCore();
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.E(ppc_lwz(TC, FRAME_SCR1, 1));
    if (!ld) ctx.E(ppc_mr(TD, RA[rd]));
    ctx.call(ld ? (by ? (void*)JitHelp_r8 : (void*)JitHelp_ldr32)
                : (by ? (void*)JitHelp_w8 : (void*)JitHelp_w32));
    if (ld) {
        if (rd == 15) ctx.E(ppc_stw(TA, FRAME_SCR2, 1));
        else          ctx.E(ppc_mr(RA[rd], TA));
    }

    if (rn != 15) {
        ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
        if (!pre) {
            if (up) ctx.E(ppc_add(RA[rn], RA[rn], TA));
            else    ctx.E(ppc_subf(RA[rn], TA, RA[rn]));
        } else if (wb && rn != rd) {
            ctx.E(ppc_lwz(RA[rn], FRAME_SCR1, 1));
        }
    }

    if (ld && rd == 15) {                            // LDR pc: ARM9 interworks, ARM7 stays ARM
        ctx.E(ppc_lwz(TA, FRAME_SCR2, 1));
        if (ctx.arm7) ctx.E(ppc_rlwinm(TA, TA, 0, 0, 29));
        emitBX_TA(ctx);
        if (si == SIZE_MAX) ctx.done = true;
        else patchSkip(ctx, si);
        return true;
    }
    patchSkip(ctx, si);
    return true;
}

static bool emitLSExtra(Ctx& ctx, uint32_t op, uint32_t curPC) {
    if ((op & 0x0E000090) != 0x00000090) return false;
    if (((op >> 25) & 7) != 0) return false;

    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;

    bool p = (op >> 24) & 1;
    bool u = (op >> 23) & 1;
    bool w = (op >> 21) & 1;
    bool l = (op >> 20) & 1;
    bool imm = (op >> 22) & 1;
    uint8_t rn = (op >> 16) & 0xF;
    uint8_t rd = (op >> 12) & 0xF;
    uint8_t sh = (op >> 5) & 3;

    // ALL rejections happen before any code is emitted.
    if (rd == 15 || sh == 0) return false;               // sh==0 also rejects SWP/MUL
    if (!imm && (op & 0xF) == 15) return false;          // Rm == PC
    if (!l && sh != 1) return false;                     // LDRD / STRD -> interpreter
    if (rn == 15 && (!p || w || !imm)) return false;     // literal loads only

    size_t si = emitCondSkip(ctx, cond);

    uint32_t immV = ((op >> 4) & 0xF0) | (op & 0xF);
    if (imm) ctx.li(TA, immV);
    else     ctx.E(ppc_mr(TA, RA[op & 0xF]));

    if (rn == 15) {
        uint32_t base = curPC + 8u;
        ctx.li(TB, u ? base + immV : base - immV);
    } else if (p) {
        if (u) ctx.E(ppc_add(TB, RA[rn], TA));
        else   ctx.E(ppc_subf(TB, TA, RA[rn]));
    } else {
        ctx.E(ppc_mr(TB, RA[rn]));
    }

    ctx.E(ppc_stw(TA, FRAME_SCR0, 1));
    ctx.E(ppc_stw(TB, FRAME_SCR1, 1));
    ctx.ldCore();
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.E(ppc_lwz(TC, FRAME_SCR1, 1));

    if (!l) {
        ctx.E(ppc_mr(TD, RA[rd]));
        ctx.call((void*)JitHelp_w16);
    } else {
        if (sh == 2) ctx.call((void*)JitHelp_r8);
        else         ctx.call((void*)JitHelp_r16);

        if (sh == 2) ctx.E(ppc_extsb(RA[rd], TA));
        else if (sh == 3) ctx.E(ppc_extsh(RA[rd], TA));
        else ctx.E(ppc_mr(RA[rd], TA));
    }

    if (rn != 15) {
        ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
        if (!p) {
            if (u) ctx.E(ppc_add(RA[rn], RA[rn], TA));
            else   ctx.E(ppc_subf(RA[rn], TA, RA[rn]));
        } else if (w && rn != rd) {
            ctx.E(ppc_lwz(RA[rn], FRAME_SCR1, 1));
        }
    }
    patchSkip(ctx, si);
    return true;
}

static bool emitMul(Ctx& ctx, uint32_t op) {
    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;
    bool s = (op >> 20) & 1;
    bool acc = (op >> 21) & 1;
    bool lng = (op >> 23) & 1;
    uint8_t rd = (op >> 16) & 0xF;
    uint8_t rn = (op >> 12) & 0xF;
    uint8_t rs = (op >> 8) & 0xF;
    uint8_t rm = op & 0xF;
    if (lng || rd == 15 || rm == 15 || rs == 15 || (acc && rn == 15)) return false;

    size_t si = emitCondSkip(ctx, cond);
    if (acc) {
        ctx.E(ppc_mullw(TA, RA[rm], RA[rs]));
        ctx.E(ppc_add(RA[rd], TA, RA[rn]));
    } else {
        ctx.E(ppc_mullw(RA[rd], RA[rm], RA[rs]));
    }
    if (s) setNZ(ctx, RA[rd]);
    patchSkip(ctx, si);
    return true;
}

// MRS / MSR (CPSR flags / full CPSR read; mode switches → fallback).
// Every operand is tested BEFORE anything is emitted.
static bool emitMrsMsr(Ctx& ctx, uint32_t op, uint32_t /*curPC*/) {
    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;

    // MRS Rd, CPSR
    if ((op & 0x0FBF0FFF) == 0x010F0000) {
        uint8_t rd = (op >> 12) & 0xF;
        if (rd == 15) return false;
        size_t si = emitCondSkip(ctx, cond);
        ctx.E(ppc_mr(RA[rd], RCPSR));
        patchSkip(ctx, si);
        return true;
    }

    // MSR CPSR_f, #imm  (flags field only)
    if ((op & 0x0DB0F000) == 0x0320F000) {
        uint8_t mask = (op >> 16) & 0xF;
        if (mask != 0x8) return false;
        uint32_t imm = op & 0xFF;
        uint32_t rot = ((op >> 8) & 0xF) * 2;
        if (rot) imm = (imm >> rot) | (imm << (32 - rot));
        imm &= 0xFF000000u;

        size_t si = emitCondSkip(ctx, cond);
        ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 8, 31));      // clear NZCVQ (bits 31..24)
        ctx.li(TA, imm);
        ctx.E(ppc_or(RCPSR, RCPSR, TA));
        patchSkip(ctx, si);
        return true;
    }

    // MSR CPSR_f, Rm
    if ((op & 0x0DB0FFF0) == 0x0120F000) {
        uint8_t mask = (op >> 16) & 0xF;
        uint8_t rm = op & 0xF;
        if (mask != 0x8 || rm == 15) return false;
        size_t si = emitCondSkip(ctx, cond);
        ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 8, 31));
        ctx.E(ppc_rlwinm(TA, RA[rm], 0, 0, 7));        // keep Rm[31:24]
        ctx.E(ppc_or(RCPSR, RCPSR, TA));
        patchSkip(ctx, si);
        return true;
    }

    return false; // SPSR / c/x/s fields / mode → interpreter
}

static bool emitBlockXfer(Ctx& ctx, uint32_t op, uint32_t curPC) {
    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15 || ((op >> 22) & 1)) return false;   // S bit (user regs / SPSR) -> interpreter

    uint8_t  rn   = (op >> 16) & 0xF;
    uint16_t list = (uint16_t)(op & 0xFFFF);
    if (rn > 14 || !list) return false;

    bool load   = (op >> 20) & 1;
    bool loadPC = load && (list & 0x8000);

    size_t si = emitCondSkip(ctx, cond);

    emitSpill(ctx);
    ctx.li(TA, curPC + 12); ctx.E(ppc_stw(TA, FRAME_SCR2, 1));  // value stored for r15 by STM
    ctx.li(TA, curPC + 4);  ctx.E(ppc_stw(TA, FRAME_PC,   1));  // default next PC

    ctx.ldCore();                                       // r3 = core
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));           // r4 = arm7
    ctx.li(TC, op);                                     // r5 = opcode
    ctx.E(ppc_addi(TD, 1, (int16_t)FRAME_REGSYNC));     // r6 = regs
    ctx.E(ppc_lwz (TE, FRAME_SCR2, 1));                 // r7 = pcForR15
    ctx.E(ppc_addi(TF, 1, (int16_t)FRAME_PC));          // r8 = pcOut
    ctx.E(ppc_addi(TG, 1, (int16_t)FRAME_CPSR));        // r9 = cpsrInOut
    ctx.call((void*)JitHelp_armBlock);

    // r3 < 0 -> helper refused (guest regs untouched) -> let interpreter do it
    ctx.E(ppc_cmpi(0, TA, 0));
    size_t bFail = ctx.sz();
    ctx.E(ppc_bc(12, 0, 0));                            // blt fail   (patched below)

    emitReload(ctx);                                    // pick up regs written by helper
    size_t bJoin = SIZE_MAX;
    if (loadPC) {
        emitCommitExitDyn(ctx, EXIT_NORMAL);            // FRAME_PC = popped PC, FRAME_CPSR updated
    } else {
        bJoin = ctx.sz();
        ctx.E(ppc_b(0));                                // jump over the fail stub
    }

    patchBc(ctx, bFail, 12, 0);                         // fail:
    emitCommitExit(ctx, curPC, EXIT_FALLBACK);

    if (bJoin != SIZE_MAX) patchB(ctx, bJoin);          // join:

    if (loadPC && si == SIZE_MAX) { ctx.done = true; return true; }
    patchSkip(ctx, si);                                 // conditional LDM pc: fall through
    return true;
}

static bool dispARM(Ctx& ctx, uint32_t op, uint32_t curPC) {
    if ((op & 0xFE000000) == 0xFA000000) return emitBlxImm(ctx, op, curPC);

    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;

    // SWI → interpreter (see header note)
    if ((op & 0x0F000000) == 0x0F000000) {
#if JIT_INLINE_SWI
        emitCommitExit(ctx, curPC, EXIT_SWI);
        ctx.done = true;
        return true;
#else
        return false;
#endif
    }

    // MRS / MSR (register forms) only: bits 27-23 = 00010, bit 20 = 0, bits 7-4 = 0000.
    if ((op & 0x0F9000F0) == 0x01000000)
        return emitMrsMsr(ctx, op, curPC);

    uint32_t it = (op >> 25) & 7;
    switch (it) {
        case 0:
            if ((op & 0x0FC000F0) == 0x00000090) return emitMul(ctx, op);
            if ((op & 0x0FFFFFF0) == 0x012FFF10 ||
                (op & 0x0FFFFFF0) == 0x012FFF30)
                return emitBranch(ctx, op, curPC);
            if ((op & 0x0E000090) == 0x00000090) return emitLSExtra(ctx, op, curPC);
            return emitDP(ctx, op, curPC);
        case 1:
            return emitDP(ctx, op, curPC);
        case 2:
        case 3:
            return emitLS(ctx, op, curPC);
        case 4:
            return emitBlockXfer(ctx, op, curPC);
        case 5:
            return emitBranch(ctx, op, curPC);
        default:
            return false;
    }
}

// ========================= Thumb =========================
static bool emitT_shifts(Ctx& ctx, uint16_t op) {
    uint8_t ty = (op >> 11) & 3, rd = op & 7, rs = (op >> 3) & 7;
    int i = (op >> 6) & 0x1F;
    switch (ty) {
        case 0: sLslI(ctx, RA[rd], RA[rs], i, true); break;
        case 1: sLsrI(ctx, RA[rd], RA[rs], i ? i : 32, true); break;
        case 2: sAsrI(ctx, RA[rd], RA[rs], i ? i : 32, true); break;
        default: return false;
    }
    setNZ(ctx, RA[rd]);
    setC_bit0(ctx, TC);
    return true;
}

static bool emitT_addSub3(Ctx& ctx, uint16_t op) {
    uint8_t rd = op & 7, rs = (op >> 3) & 7;
    bool sub = (op >> 9) & 1, imm3 = (op >> 10) & 1;
    if (imm3) ctx.li(TA, (op >> 6) & 7);
    else ctx.E(ppc_mr(TA, RA[(op >> 6) & 7]));
    ctx.E(ppc_mr(TB, RA[rs]));
    if (sub) {
        ctx.E(ppc_subfc(RA[rd], TA, TB));
        setNZ(ctx, RA[rd]); setC_xer(ctx); setV_sub(ctx, RA[rd], TB, TA);
    } else {
        ctx.E(ppc_addc(RA[rd], TB, TA));
        setNZ(ctx, RA[rd]); setC_xer(ctx); setV_add(ctx, RA[rd], TB, TA);
    }
    return true;
}

static bool emitT_imm8(Ctx& ctx, uint16_t op) {
    uint8_t ty = (op >> 11) & 3, rd = (op >> 8) & 7, imm = op & 0xFF;
    uint8_t p = RA[rd];
    switch (ty) {
        case 0: ctx.li(p, imm); setNZ(ctx, p); return true;
        case 1: // CMP Rd, #imm
            ctx.li(TA, imm); ctx.E(ppc_mr(TB, p)); ctx.E(ppc_subfc(TH, TA, TB));
            setNZ(ctx, TH); setC_xer(ctx); setV_sub(ctx, TH, TB, TA); return true;
        case 2:
            ctx.li(TA, imm); ctx.E(ppc_mr(TB, p)); ctx.E(ppc_addc(p, TB, TA));
            setNZ(ctx, p); setC_xer(ctx); setV_add(ctx, p, TB, TA); return true;
        case 3:
            ctx.li(TA, imm); ctx.E(ppc_mr(TB, p)); ctx.E(ppc_subfc(p, TA, TB));
            setNZ(ctx, p); setC_xer(ctx); setV_sub(ctx, p, TB, TA); return true;
    }
    return false;
}

static bool emitT_alu(Ctx& ctx, uint16_t op) {
    uint8_t rd = op & 7, rs = (op >> 3) & 7, o = (op >> 6) & 0xF;
    uint8_t d = RA[rd], s = RA[rs];
    switch (o) {
        case 0: ctx.E(ppc_and(d, d, s)); setNZ(ctx, d); break;
        case 1: ctx.E(ppc_xor(d, d, s)); setNZ(ctx, d); break;
        // LSL/LSR/ASR/ROR by register also update C -> interpreter until implemented (P1)
        case 2: case 3: case 4: case 7: return false;
        case 5:
            ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2)); ctx.E(ppc_mtxer(TA));
            ctx.E(ppc_mr(TB, d)); ctx.E(ppc_adde(d, TB, s));
            setNZ(ctx, d); setC_xer(ctx); setV_add(ctx, d, TB, s); break;
        case 6:
            ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2)); ctx.E(ppc_mtxer(TA));
            ctx.E(ppc_mr(TB, d)); ctx.E(ppc_subfe(d, s, TB));
            setNZ(ctx, d); setC_xer(ctx); setV_sub(ctx, d, TB, s); break;
        case 8: ctx.E(ppc_and(TA, d, s)); setNZ(ctx, TA); break;
        case 9:
            ctx.E(ppc_addi(TA, 0, 0)); ctx.E(ppc_subfc(d, s, TA));
            setNZ(ctx, d); setC_xer(ctx); setV_sub(ctx, d, TA, s); break;
        case 10:
            ctx.E(ppc_mr(TB, d)); ctx.E(ppc_subfc(TA, s, TB));
            setNZ(ctx, TA); setC_xer(ctx); setV_sub(ctx, TA, TB, s); break;
        case 11:
            ctx.E(ppc_mr(TB, d)); ctx.E(ppc_addc(TA, TB, s));
            setNZ(ctx, TA); setC_xer(ctx); setV_add(ctx, TA, TB, s); break;
        case 12: ctx.E(ppc_or(d, d, s)); setNZ(ctx, d); break;
        case 13: ctx.E(ppc_mullw(d, d, s)); setNZ(ctx, d); break;
        case 14: ctx.E(ppc_andc(d, d, s)); setNZ(ctx, d); break;
        case 15: ctx.E(ppc_nor(d, s, s)); setNZ(ctx, d); break;
        default: return false;
    }
    return true;
}

static bool emitT_hiReg(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t o  = (op >> 8) & 3;
    uint8_t rs = ((op >> 3) & 7) | (((op >> 6) & 1) << 3);
    uint8_t rd = (op & 7)        | (((op >> 7) & 1) << 3);

    if (o == 3) {                                   // BX / BLX Rs
        bool link = (op >> 7) & 1;                  // H1 set => BLX (ARMv5 only)
        if (link && ctx.arm7) return false;         // undefined on ARM7 -> interpreter
        if (rs == 15) ctx.li(TA, curPC + 4);
        else          ctx.E(ppc_mr(TA, RA[rs]));
        if (link) ctx.li(RA[14], (curPC + 2) | 1u);
        emitBX_TA(ctx);
        ctx.done = true;
        return true;
    }
    if (rd == 15) {                                 // ADD pc,Rs / MOV pc,Rs : stay in Thumb
        if (o == 1) return false;
        if (o == 2) {
            if (rs == 15) ctx.li(TA, curPC + 4);
            else          ctx.E(ppc_mr(TA, RA[rs]));
        } else {
            ctx.li(TA, curPC + 4);
            if (rs != 15) ctx.E(ppc_add(TA, TA, RA[rs]));
            else          ctx.E(ppc_add(TA, TA, TA));
        }
        ctx.E(ppc_stw(TA, FRAME_SCR0, 1));
        emitJumpSameMode_SCR0(ctx);
        ctx.done = true;
        return true;
    }
    if (rs == 15) {
        ctx.li(TA, curPC + 4);
        if (o == 0) ctx.E(ppc_add(RA[rd], RA[rd], TA));
        else if (o == 1) {
            ctx.E(ppc_mr(TB, RA[rd]));
            ctx.E(ppc_subfc(TH, TA, TB));
            setNZ(ctx, TH); setC_xer(ctx); setV_sub(ctx, TH, TB, TA);
        } else ctx.E(ppc_mr(RA[rd], TA));
        return true;
    }
    if (o == 0) ctx.E(ppc_add(RA[rd], RA[rd], RA[rs]));
    else if (o == 1) {
        ctx.E(ppc_mr(TB, RA[rd]));
        ctx.E(ppc_subfc(TH, RA[rs], TB));
        setNZ(ctx, TH); setC_xer(ctx); setV_sub(ctx, TH, TB, RA[rs]);
    } else ctx.E(ppc_mr(RA[rd], RA[rs]));
    return true;
}

static bool emitT_ldrPc(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t rd = (op >> 8) & 7;
    uint32_t addr = ((curPC + 4) & ~3u) + ((uint32_t)(op & 0xFF) << 2);
    ctx.ldCore();
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.li(TC, addr);
    ctx.call((void*)JitHelp_r32);     // always word aligned
    ctx.E(ppc_mr(RA[rd], TA));
    return true;
}

static bool emitT_memReg(Ctx& ctx, uint16_t op) {
    uint8_t rd = op & 7, rb = (op >> 3) & 7, ro = (op >> 6) & 7, k = (op >> 9) & 7;
    void* fn = nullptr;
    bool ld = true, sxb = false, sxh = false;
    switch (k) {
        case 0: fn = (void*)JitHelp_w32; ld = false; break;
        case 1: fn = (void*)JitHelp_w16; ld = false; break;
        case 2: fn = (void*)JitHelp_w8;  ld = false; break;
        case 3: fn = (void*)JitHelp_r8;  sxb = true; break;
        case 4: fn = (void*)JitHelp_ldr32; break;
        case 5: fn = (void*)JitHelp_r16; break;
        case 6: fn = (void*)JitHelp_r8; break;
        case 7: fn = (void*)JitHelp_r16; sxh = true; break;
        default: return false;
    }
    ctx.E(ppc_add(TC, RA[rb], RA[ro]));
    ctx.E(ppc_stw(TC, FRAME_SCR0, 1));
    ctx.ldCore();
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.E(ppc_lwz(TC, FRAME_SCR0, 1));
    if (!ld) ctx.E(ppc_mr(TD, RA[rd]));
    ctx.call(fn);
    if (ld) {
        if (sxb) ctx.E(ppc_extsb(RA[rd], TA));
        else if (sxh) ctx.E(ppc_extsh(RA[rd], TA));
        else ctx.E(ppc_mr(RA[rd], TA));
    }
    return true;
}

static bool emitT_memImm(Ctx& ctx, uint16_t op) {
    uint8_t rd = op & 7, rb = (op >> 3) & 7;
    bool ld = (op >> 11) & 1;
    uint8_t h = (op >> 12) & 0xF;
    bool by = (h == 7), hw = (h == 8);
    uint32_t off = ((op >> 6) & 0x1F) * (hw ? 2u : by ? 1u : 4u);
    ctx.li(TC, off);
    ctx.E(ppc_add(TC, RA[rb], TC));
    ctx.E(ppc_stw(TC, FRAME_SCR0, 1));
    ctx.ldCore();
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.E(ppc_lwz(TC, FRAME_SCR0, 1));
    if (!ld) ctx.E(ppc_mr(TD, RA[rd]));
    void* fn = ld ? (hw ? (void*)JitHelp_r16 : by ? (void*)JitHelp_r8 : (void*)JitHelp_ldr32)
                  : (hw ? (void*)JitHelp_w16 : by ? (void*)JitHelp_w8 : (void*)JitHelp_w32);
    ctx.call(fn);
    if (ld) ctx.E(ppc_mr(RA[rd], TA));
    return true;
}

static bool emitT_spLoad(Ctx& ctx, uint16_t op, uint32_t curPC) {
    bool ld = (op >> 11) & 1;
    uint8_t rd = (op >> 8) & 7;
    bool sp = ((op >> 12) & 0xF) == 0x9;
    uint32_t off = (uint32_t)(op & 0xFF) << 2;
    if (sp) {
        ctx.li(TA, off);
        ctx.E(ppc_add(TC, RA[13], TA));
    } else {
        ctx.li(TC, ((curPC + 4) & ~3u) + off);
    }
    ctx.E(ppc_stw(TC, FRAME_SCR0, 1));
    ctx.ldCore();
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.E(ppc_lwz(TC, FRAME_SCR0, 1));
    if (!ld) ctx.E(ppc_mr(TD, RA[rd]));
    ctx.call(ld ? (void*)JitHelp_ldr32 : (void*)JitHelp_w32);
    if (ld) ctx.E(ppc_mr(RA[rd], TA));
    return true;
}

static bool emitT_addSpPc(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t h = (op >> 12) & 0xF;
    if (h == 0xA) {                                   // ADD Rd, PC/SP, #imm8*4
        uint8_t rd = (op >> 8) & 7;
        bool sp = (op >> 11) & 1;
        uint32_t imm = (uint32_t)(op & 0xFF) << 2;
        if (sp) { ctx.li(TA, imm); ctx.E(ppc_add(RA[rd], RA[13], TA)); }
        else    { ctx.li(RA[rd], ((curPC + 4) & ~3u) + imm); }
        return true;
    }
    if (h == 0xB) {                                   // ADD/SUB SP, #imm7*4 : 1011 0000 S imm7
        if (((op >> 8) & 0xF) != 0) return false;
        uint32_t imm = (uint32_t)(op & 0x7F) << 2;
        ctx.li(TA, imm);
        if (op & 0x80) ctx.E(ppc_subf(RA[13], TA, RA[13]));   // SUB sp
        else           ctx.E(ppc_add (RA[13], RA[13], TA));   // ADD sp
        return true;
    }
    return false;
}

static bool emitT_pushPop(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t opA = (op >> 9) & 7;
    if (opA != 2 && opA != 6) return false;
    const bool pop = (op >> 11) & 1;
    const bool R   = (op >> 8) & 1;

    emitSpill(ctx);
    ctx.li(TA, curPC + 2);
    ctx.E(ppc_stw(TA, FRAME_PC, 1));

    ctx.ldCore();
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.li(TC, (uint32_t)op);
    ctx.E(ppc_addi(TD, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_addi(TE, 1, (int16_t)FRAME_PC));
    ctx.E(ppc_addi(TF, 1, (int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_thumbPushPop);
    emitReload(ctx);

    if (pop && R) {                            // POP {..., pc}
        emitCommitExitDyn(ctx, EXIT_NORMAL);   // helper wrote FRAME_PC (+ FRAME_CPSR on ARM9)
        ctx.done = true;
    }
    return true;
}

static bool emitT_ldmStm(Ctx& ctx, uint16_t op) {
    emitSpill(ctx);
    ctx.ldCore();
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.li(TC, (uint32_t)(uint16_t)op);
    ctx.E(ppc_addi(TD, 1, (int16_t)FRAME_REGSYNC));
    ctx.call((void*)JitHelp_thumbBlock);
    emitReload(ctx);
    return true;
}

static bool emitT_branch(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t h = (op >> 12) & 0xF;

    if (h == 0xE) {
        if (((op >> 11) & 1) != 0) return false; // BL/BLX second half handled elsewhere
        int32_t off = (int32_t)((int16_t)(op << 5)) >> 4;
        emitCommitExit(ctx, (uint32_t)(curPC + 4 + off), EXIT_NORMAL);
        ctx.done = true;
        return true;
    }

    if (h == 0xD) {
        uint8_t cond = (op >> 8) & 0xF;
        if (cond == 0xF) {                       // SWI
#if JIT_INLINE_SWI
            emitCommitExit(ctx, curPC, EXIT_SWI);
            ctx.done = true;
            return true;
#else
            return false;
#endif
        }
        if (cond == 0xE) return false;           // undefined

        int32_t off = ((int32_t)(int8_t)(op & 0xFF)) << 1;
        size_t si = emitCondSkip(ctx, cond);
        if (si == SIZE_MAX) return false;
        emitCommitExit(ctx, curPC + 4 + (uint32_t)off, EXIT_NORMAL);   // taken
        patchSkip(ctx, si);                                            // not taken: keep going
        return true;
    }
    return false;
}

static bool emitT_bl(Ctx& ctx, uint16_t op1, uint16_t op2, uint32_t curPC) {
    int32_t hi = (int32_t)((op1 & 0x7FF) << 21) >> 9;
    int32_t lo = (op2 & 0x7FF) << 1;
    uint32_t tgt = (uint32_t)(curPC + 4 + hi + lo);
    bool blx = ((op2 >> 11) & 0x1F) == 0x1C;
    if (blx && ctx.arm7) return false;
    ctx.li(RA[14], (curPC + 4) | 1u);
    if (blx) {
        tgt &= ~3u;
        ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 27, 25));
    }
    emitCommitExit(ctx, tgt & ~1u, EXIT_NORMAL);
    ctx.done = true;
    return true;
}

static bool dispThumb(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t h = (op >> 12) & 0xF;
    switch (h) {
        case 0x0:
            if (((op >> 11) & 3) < 3) return emitT_shifts(ctx, op);
            return emitT_addSub3(ctx, op);
        case 0x1:
            return emitT_imm8(ctx, op);
        case 0x2: {
            uint8_t b = (op >> 10) & 3;
            if (b == 0) return emitT_alu(ctx, op);
            if (b == 1) return emitT_hiReg(ctx, op, curPC);
            return emitT_ldrPc(ctx, op, curPC);
        }
        case 0x3: case 0x4: case 0x5:
            return emitT_memReg(ctx, op);
        case 0x6: case 0x7: case 0x8:
            return emitT_memImm(ctx, op);
        case 0x9:
            return emitT_spLoad(ctx, op, curPC);
        case 0xA:
            return emitT_addSpPc(ctx, op, curPC);
        case 0xB:
            if (((op >> 8) & 0xF) == 0) return emitT_addSpPc(ctx, op, curPC);
            if (((op >> 9) & 7) == 2 || ((op >> 9) & 7) == 6)
                return emitT_pushPop(ctx, op, curPC);
            return false;
        case 0xC:
            return emitT_ldmStm(ctx, op);
        case 0xD: case 0xE:
            return emitT_branch(ctx, op, curPC);
        default:
            return false;
    }
}

// ========================= cycle estimates =========================
// Constants (the interpreter charges real waitstates). 1 per instruction, +1 for memory,
// +1 per transferred register for block transfers. First knob to turn if a title misbehaves.
static uint32_t armCycles(uint32_t op) {
    uint32_t it = (op >> 25) & 7;
    if (it == 2 || it == 3) return 2;
    if (it == 4) return 1u + (uint32_t)__builtin_popcount(op & 0xFFFFu);
    if (it == 0 && (op & 0x0E000090u) == 0x00000090u && ((op >> 5) & 3)) return 2;
    return 1;
}
static uint32_t thumbCycles(uint16_t op) {
    uint8_t h = (op >> 12) & 0xF;
    if ((h >= 3 && h <= 9) || (h == 2 && ((op >> 11) & 1))) return 2;
    if (h == 0xB && (((op >> 9) & 7) == 2 || ((op >> 9) & 7) == 6))
        return 1u + (uint32_t)__builtin_popcount(op & 0x1FFu);
    if (h == 0xC) return 1u + (uint32_t)__builtin_popcount(op & 0xFFu);
    return 1;
}

// ========================= compile / run =========================
static bool validPC(uint32_t pc, bool gba) {
    pc &= ~1u;
    if (pc >= 0x80000000u && pc < 0xFFFF0000u) return false;
    if (gba) {
        return (pc < 0x4000u) ||
               (pc >= 0x02000000u && pc < 0x02040000u) ||
               (pc >= 0x03000000u && pc < 0x03008000u) ||
               (pc >= 0x06000000u && pc < 0x06018000u) ||
               (pc >= 0x08000000u && pc < 0x0E000000u);
    }
    return (pc < 0x8000u) ||
           (pc >= 0x02000000u && pc < 0x02400000u) ||
           (pc >= 0x03000000u && pc < 0x03800000u) ||
           (pc >= 0xFFFF0000u);
}

// With an HLE BIOS the interpreter's runOpcode() intercepts magic addresses in the
// BIOS range (IRQ entry / IRQ-return -> finishHleIrq). Those must never be JIT'd.
static inline bool hleBiosAddr(const Interpreter& interp, int cpu, uint32_t pc) {
    if (!interp.bios) return false;
    if (cpu == 1) return pc < 0x4000u;
    return pc >= 0xFFFF0000u;
}

static uint32_t* compileBlock(Interpreter* interp, Core* core,
                              uint32_t armPC, bool thumb, bool arm7, int cpu) {
    if (!codeBuf || !g_jitLive || !g_exitStub) return nullptr;

    if (codePos + BLK_WDS >= g_jitWords) flushJitCache();
    uint32_t* pg = getPage(cpu, armPC);                 // may flush (resets codePos only)
    if (!pg) return nullptr;
    if (!ensureEntryStub(cpu, interp, core)) return nullptr;

    const size_t   slot = (armPC & 0xFFFu) >> 1;
    const uint32_t tbit = thumb ? 1u : 0u;
    const bool     gba  = core->gbaMode;

    Ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.base = codeBuf + codePos;
    ctx.cur  = ctx.base;
    ctx.cap  = g_jitWords - codePos;
    if (ctx.cap > BLK_WDS) ctx.cap = BLK_WDS;
    ctx.thumb = thumb;
    ctx.arm7 = arm7;
    ctx.blockPC = armPC;
    ctx.cpuIdx = cpu;
    ctx.interp = interp;
    ctx.core = core;

    const uint32_t page = armPC & ~0xFFFu;
    uint32_t curPC = armPC;
    int n = 0;

    while (!ctx.done && !ctx.overflow) {
        if (n >= (int)BLK_ARMS || ctx.rem() < BLK_MARGIN ||
            (curPC & ~0xFFFu) != page || !validPC(curPC, gba)) {
            if (n == 0) break;                          // nothing usable -> sentinel below
            emitCommitExit(ctx, curPC, EXIT_NORMAL);
            ctx.done = true;
            break;
        }

        uint32_t* mark = ctx.cur;
        uint32_t cycMark = ctx.cycles;
        bool ok = false;
        uint32_t step = thumb ? 2u : 4u;

        if (thumb) {
            uint16_t op = core->memory.read<uint16_t>(arm7, curPC);
            bool handled = false;
            if (((op >> 11) & 0x1F) == 0x1E && validPC(curPC + 2, gba)) {
                uint16_t op2 = core->memory.read<uint16_t>(arm7, curPC + 2);
                uint8_t bb = (op2 >> 11) & 0x1F;
                if (bb == 0x1F || bb == 0x1C) {
                    ctx.cycles += 3;
                    ok = emitT_bl(ctx, op, op2, curPC);
                    step = 4; n++;
                    handled = true;
                }
            }
            if (!handled) {
                ctx.cycles += thumbCycles(op);
                ok = dispThumb(ctx, op, curPC);
            }
            if (!ok && g_dbgFB < 32) { printf("[JIT] thumb FB pc=%08X op=%04X\n", curPC, op); g_dbgFB++; }
        } else {
            uint32_t op = core->memory.read<uint32_t>(arm7, curPC);
            ctx.cycles += armCycles(op);
            ok = dispARM(ctx, op, curPC);
            if (!ok && g_dbgFB < 32) { printf("[JIT] arm FB pc=%08X op=%08X\n", curPC, op); g_dbgFB++; }
        }

        if (!ok || ctx.overflow) {
            ctx.cur = mark;                             // discard partial emission
            ctx.cycles = cycMark;
            ctx.overflow = false;
            if (n == 0) break;                          // first insn not JIT-able -> sentinel
            emitCommitExit(ctx, curPC, ok ? EXIT_NORMAL : EXIT_FALLBACK);
            ctx.done = true;
            break;
        }
        curPC += step;
        n++;
    }

    if (n == 0 || ctx.overflow || ctx.sz() == 0) {
        pg[slot] = (uint32_t)(uintptr_t)g_fbSentinel | tbit;
        return g_fbSentinel;
    }

    size_t wds = ctx.sz();
    flushICache(ctx.base, wds);
    pg[slot] = (uint32_t)(uintptr_t)ctx.base | tbit;
    codePos += wds;
    return ctx.base;
}

// Refill the interpreter pipeline if the last JIT exit left it stale.
static inline void ensurePipeline(Interpreter& interp, int cpu) {
    if (g_pipeDirty[cpu]) {
        g_pipeDirty[cpu] = false;
        interp.setPC(interp.getActualPC());
    }
}
static inline void interpStep(Interpreter& interp, int cpu) {
    ensurePipeline(interp, cpu);
    interp.jitRunOpcode();
}

// Runs one block (or one interpreter step). Returns estimated CPU cycles consumed.
static uint32_t runCpu(Core& core, int cpu, bool gba) {
    Interpreter& interp = core.interpreter[cpu];
    if (!interp.isReady()) { interpStep(interp, cpu); return 1; }

    const uint32_t pc = interp.getActualPC();
    if (!validPC(pc, gba) || hleBiosAddr(interp, cpu, pc)) {
        interpStep(interp, cpu);                     // interpreter owns HLE hooks
        return 2;
    }

    const bool thumb = interp.isThumb();
    const bool arm7  = (cpu == 1) || gba;
    uint32_t* code = lookupBlock(cpu, pc, thumb);
    if (!code) code = compileBlock(&interp, &core, pc, thumb, arm7, cpu);
    if (!code || code == g_fbSentinel) { interpStep(interp, cpu); return 2; }

    g_exitReason[cpu] = EXIT_FALLBACK;
    g_exitPC[cpu]     = pc;
    g_exitCycles[cpu] = 0;
    g_nextBlock = code;
    executeBlock_asm(g_entryStub[cpu]);

    uint32_t cyc = g_exitCycles[cpu];
    const int reason = g_exitReason[cpu];
    if (reason == EXIT_FALLBACK) {
        const uint32_t expc = g_exitPC[cpu];
        if (validPC(expc, gba)) { interp.setPC(expc); g_pipeDirty[cpu] = false; }
        else ensurePipeline(interp, cpu);
        interp.jitRunOpcode();
        cyc += 2;
    }
#if JIT_INLINE_SWI
    else if (reason == EXIT_SWI) {
        // Requires: Interpreter::jitException(uint8_t) public wrapper around exception().
        interp.jitException(0x08);
        cyc += 3;
    }
#endif
    return cyc ? cyc : 1;
}

void runJitNds(Core& core) {
    if (!g_jitLive || !codeBuf) {
        Interpreter::runCoreNds(core);
        return;
    }
    Interpreter& a9 = core.interpreter[0];
    Interpreter& a7 = core.interpreter[1];
    for (int it = 0; it < 32; it++) {
        const uint32_t g = core.globalCycles;
        if (!a9.halted && (int32_t)(g - g_cpuTime[0]) >= 0)
            g_cpuTime[0] = g + (runCpu(core, 0, false) << 1);     // ARM9: 2 global cycles each
        if (!a7.halted && (int32_t)(g - g_cpuTime[1]) >= 0)
            g_cpuTime[1] = g + (runCpu(core, 1, false) << 2);     // ARM7: 4 global cycles each

        const bool h9 = a9.halted != 0, h7 = a7.halted != 0;
        uint32_t next;
        if (h9 && h7)  next = core.events.empty() ? g + 64 : core.events.front().cycles;
        else if (h9)   next = g_cpuTime[1];
        else if (h7)   next = g_cpuTime[0];
        else           next = ((int32_t)(g_cpuTime[0] - g_cpuTime[1]) <= 0) ? g_cpuTime[0] : g_cpuTime[1];
        int32_t d = (int32_t)(next - g);
        JitHelp_tick(&core, d > 0 ? (uint32_t)d : 0u);
    }
}

void runJitGba(Core& core) {
    if (!g_jitLive || !codeBuf) {
        Interpreter::runCoreSingle<true, 0>(core);
        return;
    }
    Interpreter& a7 = core.interpreter[1];
    for (int it = 0; it < 32; it++) {
        if (!a7.halted) {
            JitHelp_tick(&core, runCpu(core, 1, true));
        } else {
            const uint32_t g = core.globalCycles;
            uint32_t next = core.events.empty() ? g + 64 : core.events.front().cycles;
            int32_t d = (int32_t)(next - g);
            JitHelp_tick(&core, d > 0 ? (uint32_t)d : 0u);
        }
    }
}

static bool allocArena() {
    if (g_arena) return true;
    size_t codeBytes = JIT_BYTES_MEM2;
    void* raw = nullptr;
#ifdef HW_RVL
    raw = SYS_AllocArena2MemLo(codeBytes + PAGE_ARENA_BYTES, 32);   // cached MEM2
#endif
    if (!raw) {
        codeBytes = JIT_BYTES_MEM1;
        raw = memalign(32, codeBytes + PAGE_ARENA_BYTES);
        if (!raw) { printf("[JIT] arena alloc failed\n"); return false; }
        printf("[JIT] MEM2 unavailable, using %zuKB in MEM1\n", codeBytes >> 10);
    }
    g_arena      = raw;
    g_arenaBytes = codeBytes + PAGE_ARENA_BYTES;
    codeBuf      = (uint32_t*)raw;
    g_jitWords   = codeBytes / 4;
    g_pageArena  = (uint32_t*)((uint8_t*)raw + codeBytes);
    return true;
}

bool initJit(Core* core) {
    g_jitLive = false;
    if (!allocArena()) return false;

    // Reset all state (stubs are re-emitted: they bake in Core/Interpreter addresses).
    memset(g_pageTab, 0, sizeof g_pageTab);
    g_nLive = 0; g_pageUsed = 0;
    codePos = STUB_WORDS;
    g_stubPos = 0;
    g_exitStub = nullptr;
    g_entryStub[0] = g_entryStub[1] = nullptr;
    g_dbgFB = 0;
    memset(g_exitPC, 0, sizeof g_exitPC);
    memset(g_exitCycles, 0, sizeof g_exitCycles);
    memset(g_pipeDirty, 0, sizeof g_pipeDirty);
    memset(g_r15Calib, 0, sizeof g_r15Calib);
    memset(g_r15Off, 0, sizeof g_r15Off);
    memset(g_cpuTime, 0, sizeof g_cpuTime);
    g_exitReason[0] = g_exitReason[1] = EXIT_NORMAL;

    memset(codeBuf, 0, g_jitWords * 4);
    DCFlushRange(codeBuf, g_jitWords * 4);
    ICInvalidateRange(codeBuf, g_jitWords * 4);

    g_jitLive = true;
    if (!buildExitStub()) {
        printf("[JIT] exit stub emission failed\n");
        g_jitLive = false;
        return false;
    }

    printf("[JIT] ready buf=%p (%zuKB %s) pages=%zu BLK_ARMS=%zu\n",
           (void*)codeBuf, (g_jitWords * 4) >> 10,
           ((uintptr_t)codeBuf >= 0x90000000u) ? "MEM2" : "MEM1",
           PAGE_ARENA_CNT, BLK_ARMS);

    if (core)
        core->setRunFunc(core->gbaMode ? runJitGba : runJitNds);
    return true;
}

void shutdownJit(Core* core) {
    g_jitLive = false;
    if (core) {
        core->setRunFunc(core->gbaMode
            ? static_cast<void(*)(Core&)>(&Interpreter::runCoreSingle<true, 0>)
            : &Interpreter::runCoreNds);
    }
    // Keep the arena (MEM2 allocations cannot be returned); drop everything that
    // references the Core/Interpreter so a later initJit() re-emits it.
    flushJitCache();
    g_stubPos = 0;
    g_exitStub = nullptr;
    g_entryStub[0] = g_entryStub[1] = nullptr;
}

// Drop every block starting in a page that overlaps [start, end). Blocks never cross
// pages, so clearing the page's slot array is exact. O(live pages).
void invalidateJitRange(uint32_t start, uint32_t end) {
    if (end <= start) return;
    for (size_t i = 0; i < g_nLive; i++) {
        uint32_t v = g_livePages[i];
        int cpu = (int)(v >> 20);
        uint32_t pi = v & 0xFFFFFu;
        uint32_t pageStart = (pi >= 0x10000u) ? (0xFFFF0000u + ((pi - 0x10000u) << 12)) : (pi << 12);
        uint32_t pageEnd = pageStart + 0x1000u;
        if (pageStart < end && start < pageEnd) {
            uint32_t* pg = g_pageTab[cpu][pi];
            if (pg) memset(pg, 0, PAGE_SLOTS * 4);
        }
    }
}

} // namespace JitPpc
