// jit_ppc.cpp — ARM → PPC JIT (Wii/Broadway)
// Correctness-focused: FALLBACK does not double-setPC; 1 insn/block while stabilizing.
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

static uint32_t g_exitPC[2]     = {};
static uint32_t g_exitCPSR[2]   = {};
static int      g_exitReason[2] = {};

static const uint32_t CYCLES_PER_SLICE = 64;

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
    return (18u << 26) | ((uint32_t)(off & 0x03FFFFFC)) | (lk ? 1u : 0u);
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

// r14-r28 = ARM r0-r14, r29 = CPSR; temps r3-r10; r11 = call
static const uint8_t RA[15] = {14,15,16,17,18,19,20,21,22,23,24,25,26,27,28};
static const uint8_t RCPSR = 29;
static const uint8_t TA = 3, TB = 4, TC = 5, TD = 6, TE = 7, TF = 8, TG = 9;
static const uint8_t RCALL = 11;

// ========================= code buffer =========================
static const size_t JIT_BYTES = 2u * 1024u * 1024u;
static const size_t JIT_WORDS = JIT_BYTES / 4;

// Keep at 1 until boot is stable, then raise to 16–24.
static const size_t BLK_ARMS = 1;
static const size_t BLK_WDS  = BLK_ARMS * 280 + 128;

static uint32_t* codeBuf   = nullptr;
static size_t    codePos   = 0;
static uint32_t  cacheGen  = 0;
static bool      g_jitLive = false;
static uint32_t  g_dbgFB   = 0;
static uint32_t  g_trace   = 0;

struct JitBlock {
    uint32_t  armPC;
    uint32_t* code;
    uint32_t  nW;
    uint32_t  gen;
    bool      thumb;
    bool      valid;
};

static const size_t CSIZ = 1u << 12;
static JitBlock cache[CSIZ];

static size_t hashPC(uint32_t pc) { return (pc >> 1) & (CSIZ - 1); }

void flushJitCache() {
    codePos = 0;
    ++cacheGen;
    for (size_t i = 0; i < CSIZ; i++) cache[i].valid = false;
}

static void flushICache(uint32_t* p, size_t nW) {
    DCFlushRange(p, nW * 4);
    ICInvalidateRange(p, nW * 4);
}

// ========================= emit context =========================
struct Ctx {
    uint32_t *base, *cur;
    size_t cap;
    bool thumb, arm7, done, overflow;
    uint32_t blockPC;
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

    void call(void* fn) {
        uint32_t a = (uint32_t)(uintptr_t)fn;
        if (a < 0x80000000u || a >= 0x81800000u) {
            overflow = true;
            return;
        }
        uint16_t hi = (uint16_t)(a >> 16), lo = (uint16_t)(a & 0xFFFF);
        E(ppc_addis(RCALL, 0, (int16_t)hi));
        if (lo) E(ppc_ori(RCALL, RCALL, lo));
        E(ppc_mtctr(RCALL));
        E(ppc_bctr(true));
    }

    void ldCore()   { E(ppc_lwz(TA, FRAME_CORE,   1)); }
    void ldInterp() { E(ppc_lwz(TA, FRAME_INTERP, 1)); }
    void ldCpu()    { E(ppc_lwz(TB, FRAME_CPUIDX, 1)); }
};

// ========================= C helpers =========================
extern "C" {

int JitHelp_testCond(uint32_t cpsr, uint32_t cond) {
    const uint32_t N = (cpsr >> 31) & 1u;
    const uint32_t Z = (cpsr >> 30) & 1u;
    const uint32_t C = (cpsr >> 29) & 1u;
    const uint32_t V = (cpsr >> 28) & 1u;
    switch (cond & 15u) {
        case  0: return (int)Z;
        case  1: return (int)(Z ^ 1u);
        case  2: return (int)C;
        case  3: return (int)(C ^ 1u);
        case  4: return (int)N;
        case  5: return (int)(N ^ 1u);
        case  6: return (int)V;
        case  7: return (int)(V ^ 1u);
        case  8: return (int)(C & (Z ^ 1u));
        case  9: return (int)((C ^ 1u) | Z);
        case 10: return N == V;
        case 11: return N != V;
        case 12: return (Z == 0u && N == V);
        case 13: return (Z == 1u || N != V);
        case 14: return 1;
        default: return 0;
    }
}

int JitHelp_syncFrom(Interpreter* interp, uint32_t* regs, uint32_t* outCPSR) {
    if (!interp || !regs || !outCPSR) return -1;
    if (!interp->isReady()) return -1;
    uint32_t** p = interp->getRegisters();
    if (!p) return -1;
    for (int i = 0; i < 15; i++) {
        if (!p[i]) return -1;
        regs[i] = *p[i];
    }
    *outCPSR = interp->getCpsrRef();
    return 0;
}

// CRITICAL:
//  - Always write r0..r14 + cpsr into the interpreter.
//  - Call setPC ONLY on EXIT_NORMAL.
//  - On EXIT_FALLBACK the runner does setPC(pc) once, then jitRunOpcode() once.
int JitHelp_commit(Interpreter* interp, int cpu,
                   uint32_t* regs, uint32_t cpsr,
                   uint32_t pc, int reason) {
    if (!interp || !regs || cpu < 0 || cpu > 1) return -1;
    if (pc >= 0x80000000u) {
        DebugLog("[JIT] BAD-PC commit cpu=%d pc=%08X cpsr=%08X reason=%d\n",
                  cpu, pc, cpsr, reason);
        g_exitReason[cpu] = EXIT_FALLBACK;
        return -1;
    }

    uint32_t** p = interp->getRegisters();
    if (!p) return -1;

    for (int i = 0; i < 15; i++) {
        if (!p[i]) return -1;
        *p[i] = regs[i];
    }
    interp->getCpsrRef() = cpsr;

    g_exitPC[cpu]     = pc;
    g_exitCPSR[cpu]   = cpsr;
    g_exitReason[cpu] = reason;

    if (reason == EXIT_NORMAL)
        interp->setPC(pc);

    return 0;
}

uint32_t JitHelp_r32(Core* c, int a, uint32_t ad) {
    return c ? c->memory.read<uint32_t>((bool)a, ad) : 0;
}
uint16_t JitHelp_r16(Core* c, int a, uint32_t ad) {
    return c ? c->memory.read<uint16_t>((bool)a, ad) : 0;
}
uint8_t JitHelp_r8(Core* c, int a, uint32_t ad) {
    return c ? c->memory.read<uint8_t>((bool)a, ad) : 0;
}
void JitHelp_w32(Core* c, int a, uint32_t ad, uint32_t v) {
    if (c) c->memory.write<uint32_t>((bool)a, ad, v);
}
void JitHelp_w16(Core* c, int a, uint32_t ad, uint16_t v) {
    if (c) c->memory.write<uint16_t>((bool)a, ad, v);
}
void JitHelp_w8(Core* c, int a, uint32_t ad, uint8_t v) {
    if (c) c->memory.write<uint8_t>((bool)a, ad, v);
}

int JitHelp_armBlock(Core* core, int arm7, uint32_t op,
                     uint32_t* regs, uint32_t pcForR15,
                     uint32_t* pcOut, uint32_t* cpsrInOut) {
    if (!core || !regs || !pcOut || !cpsrInOut) return -1;

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
                if (val & 1u) {
                    *cpsrInOut |= 1u << 5;
                    *pcOut = val & ~1u;
                } else {
                    *cpsrInOut &= ~(1u << 5);
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
    if (!core || !regs || !pcOut || !cpsrInOut) return -1;

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
        if (val & 1u) {
            *cpsrInOut |= 1u << 5;
            *pcOut = val & ~1u;
        } else {
            *cpsrInOut &= ~(1u << 5);
            *pcOut = val & ~3u;
        }
        wrotePC = 1;
    }
    regs[13] = addr;
    return wrotePC;
}

int JitHelp_thumbBlock(Core* core, int arm7, uint32_t op, uint32_t* regs) {
    if (!core || !regs) return -1;

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
           core->globalCycles >= core->events.front().cycles) {
        SchedEvent e = core->events.front();
        core->events.erase(core->events.begin());
        if (e.task >= 0 && e.task < MAX_TASKS && core->tasks[e.task].fn)
            core->tasks[e.task]();
    }
}

} // extern "C"

// ========================= prologue / epilogue / commit =========================
static void emitPrologue(Ctx& ctx) {
    ctx.E(ppc_mflr(0));
    ctx.E(ppc_stwu(1, -(int16_t)FRAME_SIZE, 1));
    ctx.E(ppc_stw(0, (int16_t)FRAME_LR_OFF, 1));
    for (int r = 14; r <= 31; r++)
        ctx.E(ppc_stw(r, FRAME_SAVE + (r - 14) * 4, 1));

    ctx.li(TA, (uint32_t)(uintptr_t)ctx.core);
    ctx.E(ppc_stw(TA, FRAME_CORE, 1));
    ctx.li(TA, (uint32_t)(uintptr_t)ctx.interp);
    ctx.E(ppc_stw(TA, FRAME_INTERP, 1));
    ctx.E(ppc_addi(TA, 0, (int16_t)ctx.cpuIdx));
    ctx.E(ppc_stw(TA, FRAME_CPUIDX, 1));
}

static void emitEpilogue(Ctx& ctx) {
    for (int r = 14; r <= 31; r++)
        ctx.E(ppc_lwz(r, FRAME_SAVE + (r - 14) * 4, 1));
    ctx.E(ppc_lwz(0, (int16_t)FRAME_LR_OFF, 1));
    ctx.E(ppc_mtlr(0));
    ctx.E(ppc_addi(1, 1, (int16_t)FRAME_SIZE));
    ctx.E(ppc_blr());
}

static void emitSyncFrom(Ctx& ctx) {
    ctx.ldInterp();
    ctx.E(ppc_addi(TB, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_addi(TC, 1, (int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_syncFrom);

    // r3 == 0 → success
    ctx.E(ppc_cmpi(0, TA, 0));
    ctx.E(ppc_bc(12, 2, 8)); // beq +8 → success
    size_t bFail = ctx.sz();
    ctx.E(ppc_b(0));         // → fail

    // success: load guest regs
    for (int i = 0; i < 15; i++)
        ctx.E(ppc_lwz(RA[i], FRAME_REGSYNC + i * 4, 1));
    ctx.E(ppc_lwz(RCPSR, FRAME_CPSR, 1));
    size_t bBody = ctx.sz();
    ctx.E(ppc_b(0));         // → body

    // fail: leave guest alone, return to host (reason stays default FALLBACK)
    {
        int32_t d = (int32_t)((ctx.sz() - bFail) * 4);
        ctx.base[bFail] = ppc_b(d);
    }
    emitEpilogue(ctx);

    // body
    {
        int32_t d = (int32_t)((ctx.sz() - bBody) * 4);
        ctx.base[bBody] = ppc_b(d);
    }
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

static void emitCommitExit(Ctx& ctx, uint32_t nextPC, int reason) {
    emitSpill(ctx);
    ctx.ldInterp();                                 // r3
    ctx.ldCpu();                                    // r4
    ctx.E(ppc_addi(TC, 1, (int16_t)FRAME_REGSYNC)); // r5
    ctx.E(ppc_mr(TD, RCPSR));                       // r6
    ctx.li(TE, nextPC);                             // r7
    ctx.E(ppc_addi(TF, 0, (int16_t)reason));        // r8
    ctx.call((void*)JitHelp_commit);
    emitEpilogue(ctx);
}

static void emitCommitExitDyn(Ctx& ctx, int reason) {
    emitSpill(ctx);
    ctx.ldInterp();
    ctx.ldCpu();
    ctx.E(ppc_addi(TC, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_mr(TD, RCPSR));
    ctx.E(ppc_lwz(TE, FRAME_PC, 1));
    ctx.E(ppc_addi(TF, 0, (int16_t)reason));
    ctx.call((void*)JitHelp_commit);
    emitEpilogue(ctx);
}

// ========================= conditions =========================
static size_t emitCondSkip(Ctx& ctx, uint8_t cond) {
    if (cond == 14 || cond == 15) return SIZE_MAX;
    ctx.E(ppc_mr(TA, RCPSR));
    ctx.E(ppc_addi(TB, 0, (int16_t)cond));
    ctx.call((void*)JitHelp_testCond);
    ctx.E(ppc_cmpi(0, TA, 0));
    size_t idx = ctx.sz();
    ctx.E(ppc_bc(12, 2, 0)); // beq after_body
    return idx;
}

static void patchSkip(Ctx& ctx, size_t idx) {
    if (idx == SIZE_MAX) return;
    int32_t off = (int32_t)((ctx.sz() - idx) * 4);
    if (off < -32768 || off > 32764) {
        ctx.overflow = true;
        return;
    }
    ctx.base[idx] = ppc_bc(12, 2, (int16_t)off);
}

// ========================= flags =========================
static void setNZ(Ctx& ctx, uint8_t r) {
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 2, 31));
    ctx.E(ppc_rlwimi(RCPSR, r, 0, 0, 0));
    ctx.E(ppc_cmpi(6, r, 0));
    ctx.E(ppc_mfcr(TA));
    ctx.E(ppc_rlwinm(TA, TA, 25, 1, 1));
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}
static void setC_xer(Ctx& ctx) {
    ctx.E(ppc_mfxer(TA));
    ctx.E(ppc_rlwinm(TA, TA, 0, 2, 2));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 3, 1));
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}
static void setV_add(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    ctx.E(ppc_xor(TE, res, a));
    ctx.E(ppc_xor(TF, res, b));
    ctx.E(ppc_and(TE, TE, TF));
    ctx.E(ppc_rlwinm(TE, TE, 0, 0, 0));
    ctx.E(ppc_rlwinm(TE, TE, 3, 3, 3));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 4, 2));
    ctx.E(ppc_or(RCPSR, RCPSR, TE));
}
static void setV_sub(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    ctx.E(ppc_xor(TE, a, b));
    ctx.E(ppc_xor(TF, a, res));
    ctx.E(ppc_and(TE, TE, TF));
    ctx.E(ppc_rlwinm(TE, TE, 0, 0, 0));
    ctx.E(ppc_rlwinm(TE, TE, 3, 3, 3));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 4, 2));
    ctx.E(ppc_or(RCPSR, RCPSR, TE));
}
static void setC_bit0(Ctx& ctx, uint8_t cr) {
    ctx.E(ppc_rlwinm(TA, cr, 29, 2, 2));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 3, 1));
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}

// ========================= shifter =========================
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
        if (sc) ctx.E(ppc_rlwinm(TC, s, 1, 31, 31));
        ctx.E(ppc_addi(d, 0, 0));
    } else if (i < 32) {
        if (sc) ctx.E(ppc_rlwinm(TC, s, (uint8_t)i, 31, 31));
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
        if (sc) ctx.E(ppc_rlwinm(TC, s, (uint8_t)i, 31, 31));
        ctx.E(ppc_srawi(d, s, (uint8_t)i));
    }
}
static void sRorI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i == 0) {
        if (sc) ctx.E(ppc_rlwinm(TC, s, 0, 31, 31));
        ctx.E(ppc_rlwinm(TA, RCPSR, 2, 0, 0));
        ctx.E(ppc_rlwinm(d, s, 31, 1, 31));
        ctx.E(ppc_or(d, d, TA));
    } else {
        i &= 31;
        if (!i) i = 32;
        if (i < 32) {
            if (sc) ctx.E(ppc_rlwinm(TC, s, (uint8_t)i, 31, 31));
            ctx.E(ppc_rlwinm(d, s, (uint8_t)(32 - i), 0, 31));
        } else {
            if (d != s) ctx.E(ppc_mr(d, s));
            if (sc) ctx.E(ppc_rlwinm(TC, s, 1, 31, 31));
        }
    }
}

static bool emitShifter(Ctx& ctx, uint32_t op, uint8_t dst, bool sc) {
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
    if (rm == 15) return false;
    uint8_t st = (op >> 5) & 3;
    if (!((op >> 4) & 1)) {
        int sa = (op >> 7) & 0x1F;
        switch (st) {
            case 0: sLslI(ctx, dst, RA[rm], sa, sc); break;
            case 1: sLsrI(ctx, dst, RA[rm], sa ? sa : 32, sc); break;
            case 2: sAsrI(ctx, dst, RA[rm], sa ? sa : 32, sc); break;
            default: sRorI(ctx, dst, RA[rm], sa, sc); break;
        }
        return sc;
    }
    uint8_t rs = (op >> 8) & 0xF;
    if (rs == 15) return false;
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

// ========================= ARM emitters =========================
enum DP { AND=0,EOR,SUB,RSB,ADD,ADC,SBC,RSC,TST,TEQ,CMP,CMN,ORR,MOV,BIC,MVN };

static bool emitDP(Ctx& ctx, uint32_t op, uint32_t curPC) {
    uint8_t cond = (op >> 28) & 0xF;
    uint8_t dop  = (op >> 21) & 0xF;
    bool s = (op >> 20) & 1;
    uint8_t rn = (op >> 16) & 0xF;
    uint8_t rd = (op >> 12) & 0xF;

    if (cond == 15 || rd == 15) return false;
    if (rn == 15 && ((op >> 4) & 1) && !((op >> 25) & 1)) return false;
    if (!((op >> 25) & 1) && (op & 0xF) == 15) return false;
    if (!((op >> 25) & 1) && ((op >> 4) & 1) && (((op >> 8) & 0xF) == 15)) return false;
    if (s && !((op >> 25) & 1) && ((op >> 4) & 1)) {
        if (dop == AND || dop == EOR || dop == TST || dop == TEQ ||
            dop == ORR || dop == MOV || dop == BIC || dop == MVN)
            return false;
    }

    size_t si = emitCondSkip(ctx, cond);
    if (si == SIZE_MAX && cond != 14) return false;

    bool rnIsPC = (rn == 15);
    if (rnIsPC) {
        ctx.li(TD, curPC + (ctx.thumb ? 4u : 8u));
        ctx.E(ppc_stw(TD, FRAME_SCR2, 1));
    }
    if (dop == ADC || dop == SBC || dop == RSC) {
        ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2));
        ctx.E(ppc_mtxer(TA));
    }

    bool logC = s && (dop == AND || dop == EOR || dop == TST || dop == TEQ ||
                      dop == ORR || dop == MOV || dop == BIC || dop == MVN);
    bool cset = emitShifter(ctx, op, TA, logC);

    uint8_t srcRn = rnIsPC ? TD : RA[rn];
    if (rnIsPC) {
        ctx.E(ppc_lwz(TD, FRAME_SCR2, 1));
        srcRn = TD;
    }

    bool needV = s && (dop == ADD || dop == SUB || dop == RSB || dop == CMN ||
                       dop == CMP || dop == ADC || dop == SBC || dop == RSC);
    if (needV) {
        ctx.E(ppc_stw(TA, FRAME_SCR0, 1));
        ctx.E(ppc_stw(srcRn, FRAME_SCR1, 1));
    }

    bool isTest = (dop == TST || dop == TEQ || dop == CMP || dop == CMN);
    uint8_t res = isTest ? TC : RA[rd];

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
    patchSkip(ctx, si);
    return true;
}

static void emitBX_SCR0(Ctx& ctx) {
    ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
    ctx.E(ppc_rlwinm(TB, TA, 0, 0, 30));
    ctx.E(ppc_stw(TB, FRAME_PC, 1));
    ctx.E(ppc_rlwinm(TC, TA, 5, 26, 26));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 27, 25));
    ctx.E(ppc_or(RCPSR, RCPSR, TC));
    emitCommitExitDyn(ctx, EXIT_NORMAL);
}

static bool emitBX(Ctx& ctx, uint32_t op, uint32_t curPC) {
    uint8_t cond = (op >> 28) & 0xF;
    uint8_t rm = op & 0xF;
    if (rm == 15 || cond == 15) return false;

    size_t si = emitCondSkip(ctx, cond);
    if (si == SIZE_MAX && cond != 14) return false;

    ctx.E(ppc_stw(RA[rm], FRAME_SCR0, 1));
    emitBX_SCR0(ctx);

    if (si != SIZE_MAX) {
        patchSkip(ctx, si);
        emitCommitExit(ctx, curPC + 4, EXIT_NORMAL);
    }
    ctx.done = true;
    return true;
}

static bool emitBranch(Ctx& ctx, uint32_t op, uint32_t curPC) {
    if ((op & 0x0FFFFFF0) == 0x012FFF10) return emitBX(ctx, op, curPC);
    if ((op & 0x0FFFFFF0) == 0x012FFF30) return false;
    if ((op & 0x0E000000) != 0x0A000000) return false;

    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;
    bool lk = (op >> 24) & 1;
    int32_t off = (int32_t)(op << 8) >> 6;
    uint32_t tgt = curPC + 8u + (uint32_t)off;

    size_t si = emitCondSkip(ctx, cond);
    if (si == SIZE_MAX && cond != 14) return false;
    if (lk) ctx.li(RA[14], curPC + 4);
    emitCommitExit(ctx, tgt, EXIT_NORMAL);
    if (si != SIZE_MAX) {
        patchSkip(ctx, si);
        emitCommitExit(ctx, curPC + 4, EXIT_NORMAL);
    }
    ctx.done = true;
    return true;
}

static bool emitLS(Ctx& ctx, uint32_t op, uint32_t) {
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

    if (rd == 15 || rn == 15) return false;
    if (!immO && ((op & 0xF) == 15 || ((op >> 4) & 1))) return false;

    size_t si = emitCondSkip(ctx, cond);
    if (si == SIZE_MAX && cond != 14) return false;

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

    if (pre) {
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
    ctx.call(ld ? (by ? (void*)JitHelp_r8 : (void*)JitHelp_r32)
                : (by ? (void*)JitHelp_w8 : (void*)JitHelp_w32));
    if (ld) ctx.E(ppc_mr(RA[rd], TA));

    ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
    if (!pre) {
        if (up) ctx.E(ppc_add(RA[rn], RA[rn], TA));
        else    ctx.E(ppc_subf(RA[rn], TA, RA[rn]));
    } else if (wb && rn != rd) {
        ctx.E(ppc_lwz(RA[rn], FRAME_SCR1, 1));
    }
    patchSkip(ctx, si);
    return true;
}

static bool emitLSExtra(Ctx& ctx, uint32_t op, uint32_t) {
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
    if (rd == 15 || rn == 15 || sh == 0) return false;

    size_t si = emitCondSkip(ctx, cond);
    if (si == SIZE_MAX && cond != 14) return false;

    if (imm) {
        ctx.li(TA, ((op >> 4) & 0xF0) | (op & 0xF));
    } else {
        if ((op & 0xF) == 15) return false;
        ctx.E(ppc_mr(TA, RA[op & 0xF]));
    }

    if (p) {
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
        if (sh != 1) return false;
        ctx.E(ppc_mr(TD, RA[rd]));
        ctx.call((void*)JitHelp_w16);
    } else {
        if (sh == 1) ctx.call((void*)JitHelp_r16);
        else if (sh == 2) ctx.call((void*)JitHelp_r8);
        else ctx.call((void*)JitHelp_r16);

        if (sh == 2) ctx.E(ppc_extsb(RA[rd], TA));
        else if (sh == 3) ctx.E(ppc_extsh(RA[rd], TA));
        else ctx.E(ppc_mr(RA[rd], TA));
    }

    ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
    if (!p) {
        if (u) ctx.E(ppc_add(RA[rn], RA[rn], TA));
        else   ctx.E(ppc_subf(RA[rn], TA, RA[rn]));
    } else if (w && rn != rd) {
        ctx.E(ppc_lwz(RA[rn], FRAME_SCR1, 1));
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
    if (si == SIZE_MAX && cond != 14) return false;

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

// MRS / MSR (CPSR flags / full CPSR read; mode switches → fallback)
static bool emitMrsMsr(Ctx& ctx, uint32_t op, uint32_t /*curPC*/) {
    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;

    // MRS Rd, CPSR
    if ((op & 0x0FBF0FFF) == 0x010F0000) {
        uint8_t rd = (op >> 12) & 0xF;
        if (rd == 15) return false;
        size_t si = emitCondSkip(ctx, cond);
        if (si == SIZE_MAX && cond != 14) return false;
        ctx.E(ppc_mr(RA[rd], RCPSR));
        patchSkip(ctx, si);
        return true;
    }

    // MSR CPSR_f, #imm  (flags field only)
    if ((op & 0x0DB0F000) == 0x0320F000) {
        uint8_t mask = (op >> 16) & 0xF;
        if (mask != 0x8) return false; // only f
        uint32_t imm = op & 0xFF;
        uint32_t rot = ((op >> 8) & 0xF) * 2;
        if (rot) imm = (imm >> rot) | (imm << (32 - rot));
        imm &= 0xFF000000u;

        size_t si = emitCondSkip(ctx, cond);
        if (si == SIZE_MAX && cond != 14) return false;
        // clear NZCVQ (top 8 of CPSR in our model: bits 31..24)
        ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 8, 31));
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
        if (si == SIZE_MAX && cond != 14) return false;
        ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 8, 31));
        ctx.E(ppc_rlwinm(TA, RA[rm], 0, 0, 7)); // keep Rm[31:24]
        ctx.E(ppc_or(RCPSR, RCPSR, TA));
        patchSkip(ctx, si);
        return true;
    }

    return false; // SPSR / c/x/s fields / mode → interpreter
}

static bool emitBlockXfer(Ctx& ctx, uint32_t op, uint32_t curPC) {
    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15 || ((op >> 22) & 1)) return false;

    uint8_t rn = (op >> 16) & 0xF;
    uint16_t list = (uint16_t)(op & 0xFFFF);
    if (rn > 14 || !list) return false;

    bool load = (op >> 20) & 1;
    bool loadPC = load && (list & 0x8000);

    size_t si = emitCondSkip(ctx, cond);
    if (si == SIZE_MAX && cond != 14) return false;

    emitSpill(ctx);
    ctx.li(TA, curPC + 12);
    ctx.E(ppc_stw(TA, FRAME_SCR2, 1));
    ctx.li(TA, curPC + 4);
    ctx.E(ppc_stw(TA, FRAME_PC, 1));

    ctx.ldCore();
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.li(TC, op);
    ctx.E(ppc_addi(TD, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_lwz(TE, FRAME_SCR2, 1));
    ctx.E(ppc_addi(TF, 1, (int16_t)FRAME_PC));
    ctx.E(ppc_addi(TG, 1, (int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_armBlock);
    ctx.E(ppc_stw(TA, FRAME_SCR0, 1)); // result

    // result < 0 → FALLBACK at curPC
    ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
    ctx.E(ppc_cmpi(0, TA, 0));
    ctx.E(ppc_bc(4, 0, 8)); // bge +8
    size_t bOk = ctx.sz();
    ctx.E(ppc_b(0));
    emitReload(ctx);
    emitCommitExit(ctx, curPC, EXIT_FALLBACK);
    {
        int32_t d = (int32_t)((ctx.sz() - bOk) * 4);
        ctx.base[bOk] = ppc_b(d);
    }

    emitReload(ctx);

    if (loadPC) {
        ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
        ctx.E(ppc_cmpi(0, TA, 1));
        ctx.E(ppc_bc(4, 2, 8)); // bne +8
        size_t b = ctx.sz();
        ctx.E(ppc_b(0));
        emitCommitExitDyn(ctx, EXIT_NORMAL);
        {
            int32_t d = (int32_t)((ctx.sz() - b) * 4);
            ctx.base[b] = ppc_b(d);
        }
        emitCommitExit(ctx, curPC + 4, EXIT_NORMAL);
        if (si != SIZE_MAX) {
            patchSkip(ctx, si);
            emitCommitExit(ctx, curPC + 4, EXIT_NORMAL);
        }
        ctx.done = true;
        return true;
    }

    patchSkip(ctx, si);
    return true;
}

static bool dispARM(Ctx& ctx, uint32_t op, uint32_t curPC) {
    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;

    // SWI → interpreter
    if ((op & 0x0F000000) == 0x0F000000) return false;

    // MRS / MSR family
    if ((op & 0x0F900000) == 0x01000000) {
        if (emitMrsMsr(ctx, op, curPC)) return true;
        return false;
    }

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
        case 1:
            ctx.li(TA, imm); ctx.E(ppc_mr(TB, p)); ctx.E(ppc_subfc(TC, TA, TB));
            setNZ(ctx, TC); setC_xer(ctx); setV_sub(ctx, TC, TB, TA); return true;
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
        case 2: ctx.E(ppc_slw(d, d, s)); setNZ(ctx, d); break;
        case 3: ctx.E(ppc_srw(d, d, s)); setNZ(ctx, d); break;
        case 4: ctx.E(ppc_sraw(d, d, s)); setNZ(ctx, d); break;
        case 5:
            ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2)); ctx.E(ppc_mtxer(TA));
            ctx.E(ppc_mr(TB, d)); ctx.E(ppc_adde(d, TB, s));
            setNZ(ctx, d); setC_xer(ctx); setV_add(ctx, d, TB, s); break;
        case 6:
            ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2)); ctx.E(ppc_mtxer(TA));
            ctx.E(ppc_mr(TB, d)); ctx.E(ppc_subfe(d, s, TB));
            setNZ(ctx, d); setC_xer(ctx); setV_sub(ctx, d, TB, s); break;
        case 7:
            ctx.E(ppc_subfic(TA, s, 32)); ctx.E(ppc_rlwnm(d, d, TA, 0, 31));
            setNZ(ctx, d); break;
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
    uint8_t o = (op >> 8) & 3;
    uint8_t rs = ((op >> 3) & 7) | (((op >> 6) & 1) << 3);
    uint8_t rd = (op & 7) | (((op >> 7) & 1) << 3);

    if (o == 3) {
        if (rs == 15) ctx.li(TA, curPC + 4);
        else ctx.E(ppc_mr(TA, RA[rs]));
        ctx.E(ppc_stw(TA, FRAME_SCR0, 1));
        emitBX_SCR0(ctx);
        ctx.done = true;
        return true;
    }
    if (rd == 15) {
        if (o == 1) return false;
        if (o == 2) {
            if (rs == 15) ctx.li(TA, curPC + 4);
            else ctx.E(ppc_mr(TA, RA[rs]));
        } else {
            ctx.li(TA, curPC + 4);
            if (rs != 15) ctx.E(ppc_add(TA, TA, RA[rs]));
            else ctx.E(ppc_add(TA, TA, TA));
        }
        ctx.E(ppc_stw(TA, FRAME_SCR0, 1));
        emitBX_SCR0(ctx);
        ctx.done = true;
        return true;
    }
    if (rs == 15) {
        ctx.li(TA, curPC + 4);
        if (o == 0) ctx.E(ppc_add(RA[rd], RA[rd], TA));
        else if (o == 1) {
            ctx.E(ppc_mr(TB, RA[rd]));
            ctx.E(ppc_subfc(TC, TA, TB));
            setNZ(ctx, TC); setC_xer(ctx); setV_sub(ctx, TC, TB, TA);
        } else ctx.E(ppc_mr(RA[rd], TA));
        return true;
    }
    if (o == 0) ctx.E(ppc_add(RA[rd], RA[rd], RA[rs]));
    else if (o == 1) {
        ctx.E(ppc_mr(TB, RA[rd]));
        ctx.E(ppc_subfc(TA, RA[rs], TB));
        setNZ(ctx, TA); setC_xer(ctx); setV_sub(ctx, TA, TB, RA[rs]);
    } else ctx.E(ppc_mr(RA[rd], RA[rs]));
    return true;
}

static bool emitT_ldrPc(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t rd = (op >> 8) & 7;
    uint32_t addr = ((curPC + 4) & ~3u) + ((uint32_t)(op & 0xFF) << 2);
    ctx.ldCore();
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.li(TC, addr);
    ctx.call((void*)JitHelp_r32);
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
        case 4: fn = (void*)JitHelp_r32; break;
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
    void* fn = ld ? (hw ? (void*)JitHelp_r16 : by ? (void*)JitHelp_r8 : (void*)JitHelp_r32)
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
    ctx.call(ld ? (void*)JitHelp_r32 : (void*)JitHelp_w32);
    if (ld) ctx.E(ppc_mr(RA[rd], TA));
    return true;
}

static bool emitT_addSpPc(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t h = (op >> 12) & 0xF;
    if (h == 0xA) {
        uint8_t rd = (op >> 8) & 7;
        bool sp = (op >> 11) & 1;
        uint32_t imm = (uint32_t)(op & 0xFF) << 2;
        if (sp) {
            ctx.li(TA, imm);
            ctx.E(ppc_add(RA[rd], RA[13], TA));
        } else {
            ctx.li(RA[rd], ((curPC + 4) & ~3u) + imm);
        }
        return true;
    }
    if (h == 0xB) {
        uint8_t s = (op >> 8) & 0xF;
        if (s == 0) {
            ctx.li(TA, (uint32_t)(op & 0x7F) << 2);
            ctx.E(ppc_add(RA[13], RA[13], TA));
            return true;
        }
        if (s == 1) {
            ctx.li(TA, (uint32_t)(op & 0x7F) << 2);
            ctx.E(ppc_subf(RA[13], TA, RA[13]));
            return true;
        }
    }
    return false;
}

static bool emitT_pushPop(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t opA = (op >> 9) & 7;
    if (opA != 2 && opA != 6) return false;

    emitSpill(ctx);
    ctx.li(TA, curPC + 2);
    ctx.E(ppc_stw(TA, FRAME_PC, 1));

    ctx.ldCore();
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.li(TC, (uint32_t)(uint16_t)op);
    ctx.E(ppc_addi(TD, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_addi(TE, 1, (int16_t)FRAME_PC));
    ctx.E(ppc_addi(TF, 1, (int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_thumbPushPop);
    ctx.E(ppc_stw(TA, FRAME_SCR0, 1));
    emitReload(ctx);

    if (((op >> 11) & 1) && ((op >> 8) & 1)) {
        ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
        ctx.E(ppc_cmpi(0, TA, 1));
        ctx.E(ppc_bc(4, 2, 8));
        size_t b = ctx.sz();
        ctx.E(ppc_b(0));
        emitCommitExitDyn(ctx, EXIT_NORMAL);
        {
            int32_t d = (int32_t)((ctx.sz() - b) * 4);
            ctx.base[b] = ppc_b(d);
        }
        emitCommitExit(ctx, curPC + 2, EXIT_NORMAL);
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
        // Unconditional B (not BL suffix)
        if (((op >> 11) & 1) != 0) return false; // BL/BLX second half handled elsewhere
        int32_t off = (int32_t)((int16_t)(op << 5)) >> 4;
        emitCommitExit(ctx, (uint32_t)(curPC + 4 + off), EXIT_NORMAL);
        ctx.done = true;
        return true;
    }

    if (h == 0xD) {
        uint8_t cond = (op >> 8) & 0xF;
        if (cond == 0xF) return false; // SWI → interpreter
        if (cond == 0xE) return false; // undefined

        int32_t off = ((int32_t)(int8_t)(op & 0xFF)) << 1;
        size_t si = emitCondSkip(ctx, cond);
        if (si == SIZE_MAX) return false;

        emitCommitExit(ctx, curPC + 4 + (uint32_t)off, EXIT_NORMAL);
        patchSkip(ctx, si);
        emitCommitExit(ctx, curPC + 2, EXIT_NORMAL);
        ctx.done = true;
        return true;
    }
    return false;
}

static bool emitT_bl(Ctx& ctx, uint16_t op1, uint16_t op2, uint32_t curPC) {
    int32_t hi = (int32_t)((op1 & 0x7FF) << 21) >> 9;
    int32_t lo = (op2 & 0x7FF) << 1;
    uint32_t tgt = (uint32_t)(curPC + 4 + hi + lo);
    bool blx = ((op2 >> 11) & 0x1F) == 0x1C;
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
            if (((op >> 8) & 0xF) <= 1) return emitT_addSpPc(ctx, op, curPC);
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

// ========================= compile / run =========================
static bool validPC(uint32_t pc, bool gba) {
    pc &= ~1u;
    if (pc >= 0x80000000u) return false;
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

static JitBlock* compile(Interpreter* interp, Core* core,
                         uint32_t armPC, bool arm7, int cpuIdx) {
    if (!codeBuf || !g_jitLive || !interp || !core) return nullptr;
    if (!interp->isReady()) return nullptr;
    if (!validPC(armPC, core->gbaMode)) return nullptr;

    {
        uint32_t tmp[15], cpsr;
        if (JitHelp_syncFrom(interp, tmp, &cpsr) != 0)
            return nullptr;
    }

    bool thumb = interp->isThumb();
    size_t bkt = hashPC(armPC);
    {
        JitBlock& s = cache[bkt];
        if (s.valid && s.armPC == armPC && s.thumb == thumb && s.gen == cacheGen &&
            s.code >= codeBuf && s.code < codeBuf + JIT_WORDS && s.nW >= 16)
            return &s;
        s.valid = false;
    }

    if (codePos + BLK_WDS >= JIT_WORDS)
        flushJitCache();

    Ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.base = codeBuf + codePos;
    ctx.cur  = ctx.base;
    ctx.cap  = JIT_WORDS - codePos;
    if (ctx.cap > BLK_WDS) ctx.cap = BLK_WDS;
    ctx.thumb = thumb;
    ctx.arm7 = arm7;
    ctx.blockPC = armPC;
    ctx.cpuIdx = cpuIdx;
    ctx.interp = interp;
    ctx.core = core;

    emitPrologue(ctx);
    emitSyncFrom(ctx);

    uint32_t curPC = armPC;
    int n = 0;

    while (n < (int)BLK_ARMS && !ctx.done && !ctx.overflow) {
        if (ctx.rem() < 96) {
            emitCommitExit(ctx, curPC, EXIT_NORMAL);
            ctx.done = true;
            break;
        }
        if (!validPC(curPC, core->gbaMode)) {
            emitCommitExit(ctx, curPC, EXIT_FALLBACK);
            ctx.done = true;
            break;
        }

        if (thumb) {
            uint16_t op = core->memory.read<uint16_t>(arm7, curPC);
            if (((op >> 11) & 0x1F) == 0x1E) {
                if (!validPC(curPC + 2, core->gbaMode)) {
                    emitCommitExit(ctx, curPC, EXIT_FALLBACK);
                    ctx.done = true;
                    break;
                }
                uint16_t op2 = core->memory.read<uint16_t>(arm7, curPC + 2);
                uint8_t bb = (op2 >> 11) & 0x1F;
                if (bb == 0x1F || bb == 0x1C) {
                    emitT_bl(ctx, op, op2, curPC);
                    curPC += 4;
                    n += 2;
                    continue;
                }
            }
            if (!dispThumb(ctx, op, curPC)) {
            if (g_dbgFB < 4000) {
            DebugLog("[JIT] thumb FB pc=%08X op=%04X\n", curPC, op);
            g_dbgFB++;
            }
            emitCommitExit(ctx, curPC, EXIT_FALLBACK);
            ctx.done = true;
            } else {
            curPC += 2;
            n++;
            }
        } else {
            uint32_t op = core->memory.read<uint32_t>(arm7, curPC);
            if (!dispARM(ctx, op, curPC)) {
            if (g_dbgFB < 4000) {
            DebugLog("[JIT] arm FB pc=%08X op=%08X\n", curPC, op);
            g_dbgFB++;
            }
            emitCommitExit(ctx, curPC, EXIT_FALLBACK);
            ctx.done = true;
            } else {
                curPC += 4;
                n++;
                if (((op >> 25) & 7) == 5) ctx.done = true;
                if ((op & 0x0FFFFFF0) == 0x012FFF10) ctx.done = true;
                if ((op & 0x0E000000) == 0x0A000000) ctx.done = true;
            }
        }
    }

    if (!ctx.done && !ctx.overflow)
        emitCommitExit(ctx, curPC, EXIT_NORMAL);

    if (ctx.overflow || ctx.sz() < 16)
        return nullptr;

    // Must end with blr
    if (ctx.base[ctx.sz() - 1] != ppc_blr())
        return nullptr;

    size_t wds = ctx.sz();
    flushICache(ctx.base, wds);

    JitBlock& slot = cache[bkt];
    slot.armPC = armPC;
    slot.code  = ctx.base;
    slot.nW    = (uint32_t)wds;
    slot.gen   = cacheGen;
    slot.thumb = thumb;
    slot.valid = true;
    codePos += wds;
    return &slot;
}

static void runCpu(Core& core, int cpu, bool gba) {
    Interpreter& interp = core.interpreter[cpu];
    if (interp.halted) return;
    if (!interp.isReady()) {
        static int notReadyLogs = 0;
        if (notReadyLogs < 50) {
            DebugLog("[JIT] cpu%d not ready, interpreter step\n", cpu);
            notReadyLogs++;
        }
        interp.jitRunOpcode();
        return;
    }

    uint32_t pc = interp.getActualPC();
    if (!validPC(pc, gba)) {
        interp.jitRunOpcode();
        return;
    }

    g_exitReason[cpu] = EXIT_FALLBACK;
    g_exitPC[cpu]     = pc;

    const bool arm7 = (cpu == 1) || gba;
    JitBlock* b = compile(&interp, &core, pc, arm7, cpu);
    if (!b || !b->code || b->nW < 16 ||
        b->code < codeBuf || b->code + b->nW > codeBuf + JIT_WORDS) {
        interp.jitRunOpcode();
        return;
    }

    executeBlock_asm(b->code);

    const int reason = g_exitReason[cpu];
    const uint32_t expc = g_exitPC[cpu];

    DebugLog("[JIT] cpu%d %08X -> %08X r=%d cpsr=%08X\n",
          cpu, pc, expc, reason, interp.getCpsrRef());

    if (reason == EXIT_FALLBACK) {
    if (validPC(expc, gba))
        interp.setPC(expc);
    interp.jitRunOpcode();
    return;
    }

    // NORMAL: commit already called setPC(expc).
    }

void runJitNds(Core& core) {
    if (!g_jitLive || !codeBuf) {
        Interpreter::runCoreNds(core);
        return;
    }
    // Several guest steps per host call so FALLBACK-heavy BIOS still advances.
    for (int i = 0; i < 8; i++) {
        for (int cpu = 0; cpu < 2; cpu++)
            runCpu(core, cpu, false);
    }
    JitHelp_tick(&core, CYCLES_PER_SLICE);
}

void runJitGba(Core& core) {
    if (!g_jitLive || !codeBuf) {
        Interpreter::runCoreSingle<true, 0>(core);
        return;
    }
    for (int i = 0; i < 16; i++) {
        if (core.interpreter[1].halted) break;
        runCpu(core, 1, true);
    }
    JitHelp_tick(&core, CYCLES_PER_SLICE);
}

bool initJit(Core* core) {
    g_jitLive = false;
    codeBuf = nullptr;

    void* raw = memalign(32, JIT_BYTES);
    if (!raw) {
        printf("[JIT] memalign failed\n");
        return false;
    }

    uintptr_t addr = (uintptr_t)raw;
    bool ok = (addr >= 0x80000000u && addr + JIT_BYTES <= 0x81800000u);
    if (!ok && addr < 0x01800000u) {
        addr |= 0x80000000u;
        ok = (addr + JIT_BYTES <= 0x81800000u);
    } else if (!ok && addr >= 0xC0000000u && addr < 0xC1800000u) {
        addr -= 0x40000000u;
        ok = (addr + JIT_BYTES <= 0x81800000u);
    }
    if (!ok) {
        printf("[JIT] not in MEM1: %p\n", raw);
        free(raw);
        return false;
    }

    uintptr_t tr = (uintptr_t)(void*)executeBlock_asm;
    if (tr < 0x80000000u || tr >= 0x81800000u) {
        printf("[JIT] bad trampoline %p\n", (void*)tr);
        free(raw);
        return false;
    }

    codeBuf = (uint32_t*)addr;
    codePos = 0;
    cacheGen = 0;
    g_dbgFB = 0;
    g_trace = 0;
    for (size_t i = 0; i < CSIZ; i++) cache[i].valid = false;
    memset(g_exitPC, 0, sizeof g_exitPC);
    memset(g_exitCPSR, 0, sizeof g_exitCPSR);
    g_exitReason[0] = g_exitReason[1] = EXIT_NORMAL;

    memset(codeBuf, 0, JIT_BYTES);
    DCFlushRange(codeBuf, JIT_BYTES);
    ICInvalidateRange(codeBuf, JIT_BYTES);

    g_jitLive = true;
    printf("[JIT] ready buf=%p (%zuKB) tramp=%p BLK_ARMS=%zu\n",
           (void*)codeBuf, JIT_BYTES >> 10, (void*)tr, BLK_ARMS);
    printf("[JIT] FALLBACK: commit regs only; runner setPC+jitRunOpcode once\n");

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
    codeBuf = nullptr;
    codePos = 0;
    for (size_t i = 0; i < CSIZ; i++) cache[i].valid = false;
}

void invalidateJitRange(uint32_t start, uint32_t end) {
    for (size_t i = 0; i < CSIZ; i++)
        if (cache[i].valid && cache[i].armPC >= start && cache[i].armPC < end)
            cache[i].valid = false;
}

} // namespace JitPpc
