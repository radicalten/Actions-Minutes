// jit_ppc.cpp — correctness-first ARM(JIT) → PPC recompiler
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

static const int EXIT_NORMAL   = 0;
static const int EXIT_FALLBACK = 1;

static uint32_t g_exitPC    [2] = {};
static uint32_t g_exitCPSR  [2] = {};
static int      g_exitReason[2] = {};

// Guest cycles to advance per dispatched block (coarse but steady)
static const uint32_t CYCLES_PER_BLOCK = 64;

// Frame layout (256 bytes, 16-byte aligned). LR saved at old_SP+4.
static const int FRAME_SIZE    = 256;
static const int FRAME_LR_OFF  = FRAME_SIZE + 4; // 260
static const int FRAME_R14     = 16;             // r14..r31 saved here
static const int FRAME_CORE    = 88;             // 16 + 18*4
static const int FRAME_SCR0    = 92;
static const int FRAME_SCR1    = 96;
static const int FRAME_SCR2    = 100;
static const int FRAME_SCR3    = 104;
static const int FRAME_REGSYNC = 112;            // 15 regs
static const int FRAME_CPSR    = 172;
static const int FRAME_PC      = 176;
static const int FRAME_REASON  = 180;

static_assert(FRAME_SIZE % 16 == 0,                   "frame 16-byte aligned");
static_assert(FRAME_R14 + 18 * 4 == FRAME_CORE,       "r14-r31 layout");
static_assert(FRAME_REGSYNC + 15 * 4 <= FRAME_CPSR,   "regsync fits");
static_assert(FRAME_LR_OFF < 32768,                   "LR offset in range");

namespace JitPpc {

// ============================================================
// PPC encoders
// ============================================================
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
    uint16_t hi = v >> 16, lo = v & 0xFFFF;
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

// ============================================================
// Register allocation
// r14-r28 = ARM r0-r14
// r29 = CPSR
// r30 = Interpreter*
// r31 = cpu index
// Volatile temps: r3=TA r4=TB r5=TC r6=TD r7=TE r8=TF r11=RCALL
// ============================================================
static const uint8_t RA[15] = {14,15,16,17,18,19,20,21,22,23,24,25,26,27,28};
static const uint8_t RCPSR = 29, RINTERP = 30, RCPUIDX = 31;
static const uint8_t TA = 3, TB = 4, TC = 5, TD = 6, TE = 7, TF = 8, RCALL = 11;

// ============================================================
// Code buffer
// ============================================================
static const size_t JIT_BYTES = 2u * 1024u * 1024u; // 2MB — safer on MEM1
static const size_t JIT_WORDS = JIT_BYTES / 4;
static const size_t BLK_ARMS  = 32;
static const size_t BLK_WDS   = BLK_ARMS * 256 + 256;

static uint32_t* codeBuf = nullptr;
static size_t    codePos = 0;
static uint32_t  cacheGen = 0;
static bool      g_jitLive = false;

// Rate-limited debug
static uint32_t g_dbgFallbacks = 0;

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

static void flushICache(uint32_t* p, size_t n) {
    DCFlushRange(p, n * 4);
    ICInvalidateRange(p, n * 4);
}

// ============================================================
// Emit context
// ============================================================
struct Ctx {
    uint32_t *base, *cur;
    size_t cap;
    bool thumb, arm7, done, overflow;
    uint32_t blockPC;
    int cpuIdx;
    Interpreter* interp;
    Core* core;

    void E(uint32_t w) {
        if ((size_t)(cur - base) < cap) {
            *cur++ = w;
        } else {
            overflow = true;
        }
    }
    size_t sz() const { return (size_t)(cur - base); }
    size_t remaining() const {
        size_t used = sz();
        return (used < cap) ? (cap - used) : 0;
    }

    void li(uint8_t rt, uint32_t v) {
        uint32_t t[2];
        int n = emit_li32(t, rt, v);
        for (int i = 0; i < n; i++) E(t[i]);
    }

    void call(void* fn) {
        uint32_t a = (uint32_t)(uintptr_t)fn;
        // Guard: only call into cached MEM1 code
        if (a < 0x80000000u || a >= 0x81800000u) {
            overflow = true;
            return;
        }
        uint16_t hi = a >> 16, lo = a & 0xFFFF;
        E(ppc_addis(RCALL, 0, (int16_t)hi));
        if (lo) E(ppc_ori(RCALL, RCALL, lo));
        E(ppc_mtctr(RCALL));
        E(ppc_bctr(true));
    }

    void ldCore(uint8_t d = TA) { E(ppc_lwz(d, FRAME_CORE, 1)); }
};

// ============================================================
// C helpers — conditions, memory, commit, block transfer
// ============================================================
extern "C" {

// ARM condition test — single source of truth (no mtcrf games)
int JitHelp_testCond(uint32_t cpsr, uint32_t cond) {
    const uint32_t N = (cpsr >> 31) & 1;
    const uint32_t Z = (cpsr >> 30) & 1;
    const uint32_t C = (cpsr >> 29) & 1;
    const uint32_t V = (cpsr >> 28) & 1;
    switch (cond & 0xF) {
        case  0: return Z;
        case  1: return Z ^ 1;
        case  2: return C;
        case  3: return C ^ 1;
        case  4: return N;
        case  5: return N ^ 1;
        case  6: return V;
        case  7: return V ^ 1;
        case  8: return C & (Z ^ 1);
        case  9: return (C ^ 1) | Z;
        case 10: return (N == V) ? 1 : 0;
        case 11: return (N != V) ? 1 : 0;
        case 12: return (Z == 0 && N == V) ? 1 : 0;
        case 13: return (Z == 1 || N != V) ? 1 : 0;
        case 14: return 1; // AL
        default: return 0; // NV
    }
}

void JitHelp_syncFrom(Interpreter* interp, uint32_t* regs, uint32_t* outCPSR) {
    uint32_t** p = interp->getRegisters();
    for (int i = 0; i < 15; i++) regs[i] = *p[i];
    *outCPSR = interp->getCpsrRef();
}

// Commit guest state through the interpreter's own setPC (pipeline-correct).
// MUST be the only place that publishes PC back to the core after a block.
void JitHelp_commit(Interpreter* interp, int cpu,
                    uint32_t* regs, uint32_t cpsr,
                    uint32_t pc, int reason) {
    uint32_t** p = interp->getRegisters();
    for (int i = 0; i < 15; i++) *p[i] = regs[i];
    interp->getCpsrRef() = cpsr;
    // setPC applies +4/+8 pipeline skew + flushPipeline
    interp->setPC(pc);

    g_exitPC[cpu]     = pc;
    g_exitCPSR[cpu]   = cpsr;
    g_exitReason[cpu] = reason;
}

uint32_t JitHelp_r32(Core* c, int a, uint32_t ad) { return c->memory.read<uint32_t>((bool)a, ad); }
uint16_t JitHelp_r16(Core* c, int a, uint32_t ad) { return c->memory.read<uint16_t>((bool)a, ad); }
uint8_t  JitHelp_r8 (Core* c, int a, uint32_t ad) { return c->memory.read<uint8_t> ((bool)a, ad); }
void JitHelp_w32(Core* c, int a, uint32_t ad, uint32_t v) { c->memory.write<uint32_t>((bool)a, ad, v); }
void JitHelp_w16(Core* c, int a, uint32_t ad, uint16_t v) { c->memory.write<uint16_t>((bool)a, ad, v); }
void JitHelp_w8 (Core* c, int a, uint32_t ad, uint8_t  v) { c->memory.write<uint8_t> ((bool)a, ad, v); }

// ARM LDM/STM without S bit. Returns 1 if PC was loaded.
int JitHelp_armBlock(Core* core, int arm7, uint32_t op,
                     uint32_t* regs, uint32_t pcForR15,
                     uint32_t* pcOut, uint32_t* cpsrInOut) {
    const bool p    = (op >> 24) & 1;
    const bool u    = (op >> 23) & 1;
    const bool bitS = (op >> 22) & 1;
    const bool w    = (op >> 21) & 1;
    const bool l    = (op >> 20) & 1;
    const uint8_t  rn   = (op >> 16) & 0xF;
    const uint16_t list = (uint16_t)(op & 0xFFFF);

    if (bitS || rn > 14 || list == 0) return -1;

    int n = 0;
    for (int i = 0; i < 16; i++) if (list & (1u << i)) n++;

    const uint32_t base = regs[rn];
    uint32_t addr, wb;
    if (u) {
        wb   = base + (uint32_t)n * 4u;
        addr = p ? base + 4u : base;
    } else {
        wb   = base - (uint32_t)n * 4u;
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
                    *cpsrInOut |=  (1u << 5);
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
        if (w) {
            // If Rn in list, ARM loads overwrite writeback — already done.
            if (!(list & (1u << rn)))
                regs[rn] = wb;
        }
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

// Thumb PUSH/POP. Returns 1 if PC loaded.
int JitHelp_thumbPushPop(Core* core, int arm7, uint32_t op,
                         uint32_t* regs, uint32_t* pcOut, uint32_t* cpsrInOut) {
    const bool load = (op >> 11) & 1; // POP
    const bool R    = (op >> 8) & 1;
    const uint8_t list = op & 0xFF;

    int n = 0;
    for (int i = 0; i < 8; i++) if (list & (1u << i)) n++;
    if (R) n++;

    if (!load) {
        // PUSH = STMDB SP!
        uint32_t sp = regs[13] - (uint32_t)n * 4u;
        uint32_t addr = sp;
        for (int i = 0; i < 8; i++) {
            if (!(list & (1u << i))) continue;
            core->memory.write<uint32_t>((bool)arm7, addr, regs[i]);
            addr += 4;
        }
        if (R) {
            core->memory.write<uint32_t>((bool)arm7, addr, regs[14]);
        }
        regs[13] = sp;
        return 0;
    }

    // POP = LDMIA SP!
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
            *cpsrInOut |=  (1u << 5);
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

// Thumb LDMIA/STMIA Rb!, {list}
int JitHelp_thumbBlock(Core* core, int arm7, uint32_t op, uint32_t* regs) {
    const bool load = (op >> 11) & 1;
    const uint8_t rb   = (op >> 8) & 7;
    const uint8_t list = op & 0xFF;

    if (list == 0) {
        regs[rb] += 0x40; // ARM empty-list quirk
        return 0;
    }

    uint32_t addr = regs[rb];
    uint32_t wb = addr;
    for (int i = 0; i < 8; i++) if (list & (1u << i)) wb += 4;
    const bool rbInList = (list & (1u << rb)) != 0;

    if (load) {
        for (int i = 0; i < 8; i++) {
            if (!(list & (1u << i))) continue;
            regs[i] = core->memory.read<uint32_t>((bool)arm7, addr);
            addr += 4;
        }
        if (!rbInList) regs[rb] = wb;
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
    core->globalCycles += cycles;
    while (!core->events.empty() &&
           core->globalCycles >= core->events.front().cycles) {
        SchedEvent e = core->events.front();
        core->events.erase(core->events.begin());
        if (e.task < MAX_TASKS && core->tasks[e.task].fn)
            core->tasks[e.task]();
    }
}

} // extern "C"

// ============================================================
// Prologue / epilogue / commit
// ============================================================
static void emitPrologue(Ctx& ctx) {
    ctx.E(ppc_mflr(0));
    ctx.E(ppc_stwu(1, -(int16_t)FRAME_SIZE, 1));
    ctx.E(ppc_stw(0, (int16_t)FRAME_LR_OFF, 1));
    for (int r = 14; r <= 31; r++)
        ctx.E(ppc_stw(r, FRAME_R14 + (r - 14) * 4, 1));
    ctx.li(RINTERP, (uint32_t)(uintptr_t)ctx.interp);
    ctx.li(TA, (uint32_t)(uintptr_t)ctx.core);
    ctx.E(ppc_stw(TA, FRAME_CORE, 1));
    ctx.E(ppc_addi(RCPUIDX, 0, (int16_t)ctx.cpuIdx));
}

static void emitEpilogue(Ctx& ctx) {
    for (int r = 14; r <= 31; r++)
        ctx.E(ppc_lwz(r, FRAME_R14 + (r - 14) * 4, 1));
    ctx.E(ppc_lwz(0, (int16_t)FRAME_LR_OFF, 1));
    ctx.E(ppc_mtlr(0));
    ctx.E(ppc_addi(1, 1, (int16_t)FRAME_SIZE));
    ctx.E(ppc_blr());
}

static void emitSyncFrom(Ctx& ctx) {
    ctx.E(ppc_mr(TA, RINTERP));
    ctx.E(ppc_addi(TB, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_addi(TC, 1, (int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_syncFrom);
    for (int i = 0; i < 15; i++)
        ctx.E(ppc_lwz(RA[i], FRAME_REGSYNC + i * 4, 1));
    ctx.E(ppc_lwz(RCPSR, FRAME_CPSR, 1));
}

// Spill GPRs+CPSR, commit via interpreter setPC, return from block.
// nextPCImm: if true, TB already holds nothing — we li the PC.
static void emitCommitExit(Ctx& ctx, uint32_t nextPC, int reason) {
    for (int i = 0; i < 15; i++)
        ctx.E(ppc_stw(RA[i], FRAME_REGSYNC + i * 4, 1));
    ctx.E(ppc_stw(RCPSR, FRAME_CPSR, 1));

    // commit(interp, cpu, regs*, cpsr, pc, reason)
    ctx.E(ppc_mr(TA, RINTERP));                          // r3
    ctx.E(ppc_mr(TB, RCPUIDX));                          // r4
    ctx.E(ppc_addi(TC, 1, (int16_t)FRAME_REGSYNC));      // r5
    ctx.E(ppc_mr(TD, RCPSR));                            // r6
    ctx.li(TE, nextPC);                                  // r7
    ctx.E(ppc_addi(TF, 0, (int16_t)reason));             // r8
    ctx.call((void*)JitHelp_commit);
    emitEpilogue(ctx);
}

// Same but PC is already in FRAME_PC
static void emitCommitExitDyn(Ctx& ctx, int reason) {
    for (int i = 0; i < 15; i++)
        ctx.E(ppc_stw(RA[i], FRAME_REGSYNC + i * 4, 1));
    ctx.E(ppc_stw(RCPSR, FRAME_CPSR, 1));

    ctx.E(ppc_mr(TA, RINTERP));
    ctx.E(ppc_mr(TB, RCPUIDX));
    ctx.E(ppc_addi(TC, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_mr(TD, RCPSR));
    ctx.E(ppc_lwz(TE, FRAME_PC, 1));
    ctx.E(ppc_addi(TF, 0, (int16_t)reason));
    ctx.call((void*)JitHelp_commit);
    emitEpilogue(ctx);
}

// ============================================================
// Conditions via C helper (bulletproof)
//
// emitCondSkip:
//   mr r3, CPSR; li r4, cond; call testCond
//   cmpwi r3, 0
//   beq after_body          ; skip body if FALSE
//   <body>
// after_body:
// Returns index of beq to patch; SIZE_MAX = always execute (AL)
// ============================================================
static size_t emitCondSkip(Ctx& ctx, uint8_t cond) {
    if (cond == 14) return SIZE_MAX; // AL
    if (cond == 15) return SIZE_MAX; // NV — caller must fallback

    ctx.E(ppc_mr(TA, RCPSR));
    ctx.E(ppc_addi(TB, 0, (int16_t)cond));
    ctx.call((void*)JitHelp_testCond);
    ctx.E(ppc_cmpi(0, TA, 0));
    size_t idx = ctx.sz();
    ctx.E(ppc_bc(12, 2, 0)); // beq placeholder → after body
    return idx;
}

static void patchSkip(Ctx& ctx, size_t idx) {
    if (idx == SIZE_MAX) return;
    int32_t off = (int32_t)((ctx.sz() - idx) * 4);
    // BD is byte offset; must fit in 16-bit signed BD field
    if (off < -32768 || off > 32767) {
        ctx.overflow = true;
        return;
    }
    ctx.base[idx] = ppc_bc(12, 2, (int16_t)off); // beq +off
}

// ============================================================
// Flag helpers (CPSR N=31 Z=30 C=29 V=28)
// ============================================================
static void setNZ(Ctx& ctx, uint8_t r) {
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 2, 31));       // clear N,Z
    ctx.E(ppc_rlwimi(RCPSR, r, 0, 0, 0));             // N from sign(r)
    ctx.E(ppc_cmpi(6, r, 0));
    ctx.E(ppc_mfcr(TA));
    ctx.E(ppc_rlwinm(TA, TA, 25, 1, 1));              // CR6.EQ → Z
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}
static void setC_xer(Ctx& ctx) {
    ctx.E(ppc_mfxer(TA));
    ctx.E(ppc_rlwinm(TA, TA, 0, 2, 2));               // XER.CA
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 3, 1));          // clear C
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}
static void setV_add(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    ctx.E(ppc_xor(TA, res, a));
    ctx.E(ppc_xor(TB, res, b));
    ctx.E(ppc_and(TA, TA, TB));
    ctx.E(ppc_rlwinm(TA, TA, 0, 0, 0));
    ctx.E(ppc_rlwinm(TA, TA, 3, 3, 3));               // → V bit
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 4, 2));
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}
static void setV_sub(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    ctx.E(ppc_xor(TA, a, b));
    ctx.E(ppc_xor(TB, a, res));
    ctx.E(ppc_and(TA, TA, TB));
    ctx.E(ppc_rlwinm(TA, TA, 0, 0, 0));
    ctx.E(ppc_rlwinm(TA, TA, 3, 3, 3));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 4, 2));
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}
static void setC_bit0(Ctx& ctx, uint8_t cr) {
    ctx.E(ppc_rlwinm(TA, cr, 29, 2, 2));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 3, 1));
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
}

// ============================================================
// Shifter
// ============================================================
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
        // RRX
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

// returns whether shifter carry was produced into TC bit0
static bool emitShifter(Ctx& ctx, uint32_t op, uint8_t dst, bool sc) {
    bool isImm = (op >> 25) & 1;
    if (isImm) {
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
    uint8_t pRm = RA[rm];
    uint8_t st = (op >> 5) & 3;
    bool isReg = (op >> 4) & 1;
    if (!isReg) {
        int sa = (op >> 7) & 0x1F;
        switch (st) {
            case 0: sLslI(ctx, dst, pRm, sa, sc); break;
            case 1: sLsrI(ctx, dst, pRm, sa ? sa : 32, sc); break;
            case 2: sAsrI(ctx, dst, pRm, sa ? sa : 32, sc); break;
            case 3: sRorI(ctx, dst, pRm, sa, sc); break;
        }
        return sc;
    }
    uint8_t rs = (op >> 8) & 0xF;
    if (rs == 15) return false;
    // Register-specified shift: coarse (no carry-out) — OK for non-S or fallback
    ctx.E(ppc_rlwinm(TD, RA[rs], 0, 24, 31));
    ctx.E(ppc_mr(TA, pRm));
    switch (st) {
        case 0: ctx.E(ppc_slw (dst, TA, TD)); break;
        case 1: ctx.E(ppc_srw (dst, TA, TD)); break;
        case 2: ctx.E(ppc_sraw(dst, TA, TD)); break;
        case 3:
            ctx.E(ppc_subfic(TB, TD, 32));
            ctx.E(ppc_rlwnm(dst, TA, TB, 0, 31));
            break;
    }
    return false; // no carry tracking for reg shifts
}

// ============================================================
// Data processing
// ============================================================
enum DP {
    AND=0,EOR,SUB,RSB,ADD,ADC,SBC,RSC,TST,TEQ,CMP,CMN,ORR,MOV,BIC,MVN
};

static bool emitDP(Ctx& ctx, uint32_t op, uint32_t curPC) {
    uint8_t cond = (op >> 28) & 0xF;
    uint8_t dop  = (op >> 21) & 0xF;
    bool s       = (op >> 20) & 1;
    uint8_t rn   = (op >> 16) & 0xF;
    uint8_t rd   = (op >> 12) & 0xF;

    if (cond == 15) return false;
    if (rd == 15) return false; // PC write → fallback
    if (rn == 15 && ((op >> 4) & 1) && !((op >> 25) & 1)) return false;
    if (!((op >> 25) & 1) && (op & 0xF) == 15) return false;
    if (!((op >> 25) & 1) && ((op >> 4) & 1) && (((op >> 8) & 0xF) == 15)) return false;

    // Register-specified shift + S + logical → need carry; fallback for safety
    if (s && !((op >> 25) & 1) && ((op >> 4) & 1)) {
        uint8_t d = dop;
        if (d == AND || d == EOR || d == TST || d == TEQ ||
            d == ORR || d == MOV || d == BIC || d == MVN)
            return false;
    }

    uint8_t pRd = RA[rd];
    bool rnIsPC = (rn == 15);

    size_t si = emitCondSkip(ctx, cond);
    if (si == SIZE_MAX && cond != 14) return false;

    if (rnIsPC) {
        ctx.li(TD, curPC + (ctx.thumb ? 4u : 8u));
        ctx.E(ppc_stw(TD, FRAME_SCR3, 1));
    }

    bool needCin = (dop == ADC || dop == SBC || dop == RSC);
    if (needCin) {
        ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2));
        ctx.E(ppc_mtxer(TA));
    }

    bool logCarry = s && (dop == AND || dop == EOR || dop == TST || dop == TEQ ||
                          dop == ORR || dop == MOV || dop == BIC || dop == MVN);
    bool carrySet = emitShifter(ctx, op, TA, logCarry);

    uint8_t srcRn = rnIsPC ? TD : RA[rn];
    if (rnIsPC) {
        ctx.E(ppc_lwz(TD, FRAME_SCR3, 1));
        srcRn = TD;
    }

    bool needV = s && (dop == ADD || dop == SUB || dop == RSB || dop == CMN ||
                       dop == CMP || dop == ADC || dop == SBC || dop == RSC);
    if (needV) {
        ctx.E(ppc_stw(TA, FRAME_SCR0, 1));
        ctx.E(ppc_stw(srcRn, FRAME_SCR1, 1));
    }

    bool isTest = (dop == TST || dop == TEQ || dop == CMP || dop == CMN);
    uint8_t res = isTest ? TC : pRd;

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
        uint8_t opA, opB;
        if (needV) {
            ctx.E(ppc_lwz(TA, FRAME_SCR0, 1)); opB = TA;
            ctx.E(ppc_lwz(TD, FRAME_SCR1, 1)); opA = TD;
        } else {
            opA = srcRn; opB = TA;
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
                if (carrySet) setC_bit0(ctx, TC);
                break;
        }
    }

    patchSkip(ctx, si);
    return true;
}

// ============================================================
// Branches
// ============================================================
static void emitBX_from_SCR2(Ctx& ctx) {
    // value in FRAME_SCR2
    ctx.E(ppc_lwz(TA, FRAME_SCR2, 1));
    // dest = val & ~1
    ctx.E(ppc_rlwinm(TB, TA, 0, 0, 30));
    ctx.E(ppc_stw(TB, FRAME_PC, 1));
    // T = bit0 → CPSR bit5
    ctx.E(ppc_rlwinm(TC, TA, 5, 26, 26));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 27, 25)); // clear T
    ctx.E(ppc_or(RCPSR, RCPSR, TC));
    emitCommitExitDyn(ctx, EXIT_NORMAL);
}

static bool emitBX(Ctx& ctx, uint32_t op, uint32_t curPC) {
    uint8_t cond = (op >> 28) & 0xF;
    uint8_t rm = op & 0xF;
    if (rm == 15 || cond == 15) return false;

    size_t si = emitCondSkip(ctx, cond);
    if (si == SIZE_MAX && cond != 14) return false;

    ctx.E(ppc_stw(RA[rm], FRAME_SCR2, 1));
    emitBX_from_SCR2(ctx);

    if (si != SIZE_MAX) {
        patchSkip(ctx, si);
        emitCommitExit(ctx, curPC + 4, EXIT_NORMAL);
    }
    ctx.done = true;
    return true;
}

static bool emitBranch(Ctx& ctx, uint32_t op, uint32_t curPC) {
    if ((op & 0x0FFFFFF0) == 0x012FFF10) return emitBX(ctx, op, curPC);
    if ((op & 0x0FFFFFF0) == 0x012FFF30) return false; // BLX reg

    if ((op & 0x0E000000) == 0x0A000000) {
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
    return false;
}

// ============================================================
// Single load/store (word/byte, imm or reg LSL#0..31 / simple shifts)
// ============================================================
static bool emitLS(Ctx& ctx, uint32_t op, uint32_t /*curPC*/) {
    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;

    bool ld   = (op >> 20) & 1;
    bool by   = (op >> 22) & 1;
    bool up   = (op >> 23) & 1;
    bool pre  = (op >> 24) & 1;
    bool wb   = (op >> 21) & 1;
    bool immO = !((op >> 25) & 1);
    uint8_t rn = (op >> 16) & 0xF;
    uint8_t rd = (op >> 12) & 0xF;

    if (rd == 15 || rn == 15) return false;
    if (!immO && (op & 0xF) == 15) return false;
    if (!immO && ((op >> 4) & 1)) return false; // reg shift amount

    uint8_t pRn = RA[rn], pRd = RA[rd];

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
        if (up) ctx.E(ppc_add(TB, pRn, TA));
        else    ctx.E(ppc_subf(TB, TA, pRn));
    } else {
        ctx.E(ppc_mr(TB, pRn));
    }

    ctx.E(ppc_stw(TA, FRAME_SCR0, 1)); // offset
    ctx.E(ppc_stw(TB, FRAME_SCR1, 1)); // address

    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.E(ppc_lwz(TC, FRAME_SCR1, 1));
    if (!ld) ctx.E(ppc_mr(TD, pRd));

    ctx.call(ld ? (by ? (void*)JitHelp_r8 : (void*)JitHelp_r32)
                : (by ? (void*)JitHelp_w8 : (void*)JitHelp_w32));
    if (ld) ctx.E(ppc_mr(pRd, TA));

    ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
    if (!pre) {
        if (up) ctx.E(ppc_add(pRn, pRn, TA));
        else    ctx.E(ppc_subf(pRn, TA, pRn));
    } else if (wb && rn != rd) {
        ctx.E(ppc_lwz(pRn, FRAME_SCR1, 1));
    }

    patchSkip(ctx, si);
    return true;
}

static bool emitMul(Ctx& ctx, uint32_t op) {
    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;
    bool s   = (op >> 20) & 1;
    bool acc = (op >> 21) & 1;
    bool lng = (op >> 23) & 1;
    uint8_t rd = (op >> 16) & 0xF;
    uint8_t rn = (op >> 12) & 0xF;
    uint8_t rs = (op >> 8) & 0xF;
    uint8_t rm = op & 0xF;
    if (lng || rd == 15 || rm == 15 || rs == 15 || rn == 15) return false;

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

// ============================================================
// ARM LDM/STM
// ============================================================
static bool emitBlockXfer(Ctx& ctx, uint32_t op, uint32_t curPC) {
    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;
    if ((op >> 22) & 1) return false; // S bit
    uint8_t rn = (op >> 16) & 0xF;
    uint16_t list = (uint16_t)(op & 0xFFFF);
    if (rn > 14 || list == 0) return false;

    bool load = (op >> 20) & 1;
    bool loadPC = load && (list & 0x8000);

    size_t si = emitCondSkip(ctx, cond);
    if (si == SIZE_MAX && cond != 14) return false;

    for (int i = 0; i < 15; i++)
        ctx.E(ppc_stw(RA[i], FRAME_REGSYNC + i * 4, 1));
    ctx.E(ppc_stw(RCPSR, FRAME_CPSR, 1));
    ctx.li(TA, curPC + 12);
    ctx.E(ppc_stw(TA, FRAME_SCR2, 1)); // pcForR15
    ctx.li(TA, curPC + 4);
    ctx.E(ppc_stw(TA, FRAME_PC, 1));   // default pcOut

    // armBlock(core, arm7, op, regs*, pcForR15, pcOut*, cpsr*)
    // r3..r9 = 7 args; 8th on stack — keep to 7 by packing pcForR15 in SCR2 and reading inside? 
    // EABI: r3-r10 = 8 int args. Use r3-r9 (7) + need 8th.
    // Signature uses 7: core, arm7, op, regs, pcForR15, pcOut, cpsr  → r3-r9

    ctx.ldCore(TA);                                      // r3
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));            // r4
    ctx.li(TC, op);                                      // r5
    ctx.E(ppc_addi(TD, 1, (int16_t)FRAME_REGSYNC));      // r6 regs
    ctx.E(ppc_lwz(TE, FRAME_SCR2, 1));                   // r7 pcForR15
    ctx.E(ppc_addi(TF, 1, (int16_t)FRAME_PC));           // r8 pcOut
    ctx.E(ppc_addi(9, 1, (int16_t)FRAME_CPSR));          // r9 cpsr
    ctx.call((void*)JitHelp_armBlock);
    // r3 = result
    ctx.E(ppc_stw(TA, FRAME_SCR0, 1));

    for (int i = 0; i < 15; i++)
        ctx.E(ppc_lwz(RA[i], FRAME_REGSYNC + i * 4, 1));
    ctx.E(ppc_lwz(RCPSR, FRAME_CPSR, 1));

    if (loadPC) {
        // if result==1 → dyn exit; else fall through (shouldn't)
        ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
        ctx.E(ppc_cmpi(0, TA, 1));
        // bne skip_dyn (+8 over b)
        ctx.E(ppc_bc(4, 2, 8)); // bne +8
        size_t bIdx = ctx.sz();
        ctx.E(ppc_b(0));
        emitCommitExitDyn(ctx, EXIT_NORMAL);
        {
            int32_t d = (int32_t)((ctx.sz() - bIdx) * 4);
            ctx.base[bIdx] = ppc_b(d);
        }
        // no PC write: continue at next
        emitCommitExit(ctx, curPC + 4, EXIT_NORMAL);
        ctx.done = true;
        // still need patch for cond false
        if (si != SIZE_MAX) {
            // body already ended with exits; patch false path after them
            patchSkip(ctx, si);
            emitCommitExit(ctx, curPC + 4, EXIT_NORMAL);
        }
        return true;
    }

    patchSkip(ctx, si);
    return true;
}

static bool dispARM(Ctx& ctx, uint32_t op, uint32_t curPC) {
    uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;
    uint32_t it = (op >> 25) & 7;
    switch (it) {
        case 0: case 1:
            if ((op & 0x0FC000F0) == 0x00000090) return emitMul(ctx, op);
            if ((op & 0x0FFFFFF0) == 0x012FFF10 ||
                (op & 0x0FFFFFF0) == 0x012FFF30)
                return emitBranch(ctx, op, curPC);
            // SWP / halfword / misc media → fallback
            if ((op & 0x0FB00FF0) == 0x01000000) return false;
            if ((op & 0x0E000090) == 0x00000090) return false;
            if ((op & 0x0FB00000) == 0x03200000) return false;
            if ((op & 0x0DB0F000) == 0x010F0000) return false;
            return emitDP(ctx, op, curPC);
        case 2: case 3:
            return emitLS(ctx, op, curPC);
        case 4:
            return emitBlockXfer(ctx, op, curPC);
        case 5:
            return emitBranch(ctx, op, curPC);
        default:
            return false;
    }
}

// ============================================================
// Thumb
// ============================================================
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
    uint8_t ty = (op >> 11) & 3, rd = (op >> 8) & 7, pRd = RA[rd], imm = op & 0xFF;
    switch (ty) {
        case 0: ctx.li(pRd, imm); setNZ(ctx, pRd); return true;
        case 1:
            ctx.li(TA, imm); ctx.E(ppc_mr(TB, pRd)); ctx.E(ppc_subfc(TC, TA, TB));
            setNZ(ctx, TC); setC_xer(ctx); setV_sub(ctx, TC, TB, TA); return true;
        case 2:
            ctx.li(TA, imm); ctx.E(ppc_mr(TB, pRd)); ctx.E(ppc_addc(pRd, TB, TA));
            setNZ(ctx, pRd); setC_xer(ctx); setV_add(ctx, pRd, TB, TA); return true;
        case 3:
            ctx.li(TA, imm); ctx.E(ppc_mr(TB, pRd)); ctx.E(ppc_subfc(pRd, TA, TB));
            setNZ(ctx, pRd); setC_xer(ctx); setV_sub(ctx, pRd, TB, TA); return true;
    }
    return false;
}
static bool emitT_alu(Ctx& ctx, uint16_t op) {
    uint8_t rd = op & 7, rs = (op >> 3) & 7, o = (op >> 6) & 0xF;
    uint8_t pRd = RA[rd], pRs = RA[rs];
    switch (o) {
        case 0:  ctx.E(ppc_and(pRd, pRd, pRs)); setNZ(ctx, pRd); break;
        case 1:  ctx.E(ppc_xor(pRd, pRd, pRs)); setNZ(ctx, pRd); break;
        case 2:  ctx.E(ppc_slw(pRd, pRd, pRs)); setNZ(ctx, pRd); break;
        case 3:  ctx.E(ppc_srw(pRd, pRd, pRs)); setNZ(ctx, pRd); break;
        case 4:  ctx.E(ppc_sraw(pRd, pRd, pRs)); setNZ(ctx, pRd); break;
        case 5:
            ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2)); ctx.E(ppc_mtxer(TA));
            ctx.E(ppc_mr(TB, pRd)); ctx.E(ppc_adde(pRd, TB, pRs));
            setNZ(ctx, pRd); setC_xer(ctx); setV_add(ctx, pRd, TB, pRs); break;
        case 6:
            ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2)); ctx.E(ppc_mtxer(TA));
            ctx.E(ppc_mr(TB, pRd)); ctx.E(ppc_subfe(pRd, pRs, TB));
            setNZ(ctx, pRd); setC_xer(ctx); setV_sub(ctx, pRd, TB, pRs); break;
        case 7:
            ctx.E(ppc_subfic(TA, pRs, 32)); ctx.E(ppc_rlwnm(pRd, pRd, TA, 0, 31));
            setNZ(ctx, pRd); break;
        case 8:  ctx.E(ppc_and(TA, pRd, pRs)); setNZ(ctx, TA); break;
        case 9:
            ctx.E(ppc_addi(TA, 0, 0)); ctx.E(ppc_subfc(pRd, pRs, TA));
            setNZ(ctx, pRd); setC_xer(ctx); setV_sub(ctx, pRd, TA, pRs); break;
        case 10:
            ctx.E(ppc_mr(TB, pRd)); ctx.E(ppc_subfc(TA, pRs, TB));
            setNZ(ctx, TA); setC_xer(ctx); setV_sub(ctx, TA, TB, pRs); break;
        case 11:
            ctx.E(ppc_mr(TB, pRd)); ctx.E(ppc_addc(TA, TB, pRs));
            setNZ(ctx, TA); setC_xer(ctx); setV_add(ctx, TA, TB, pRs); break;
        case 12: ctx.E(ppc_or(pRd, pRd, pRs)); setNZ(ctx, pRd); break;
        case 13: ctx.E(ppc_mullw(pRd, pRd, pRs)); setNZ(ctx, pRd); break;
        case 14: ctx.E(ppc_andc(pRd, pRd, pRs)); setNZ(ctx, pRd); break;
        case 15: ctx.E(ppc_nor(pRd, pRs, pRs)); setNZ(ctx, pRd); break;
        default: return false;
    }
    return true;
}

static bool emitT_hiReg(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t o = (op >> 8) & 3;
    uint8_t h1 = (op >> 7) & 1, h2 = (op >> 6) & 1;
    uint8_t rs = ((op >> 3) & 7) | (h2 << 3);
    uint8_t rd = (op & 7) | (h1 << 3);

    if (o == 3) {
        // BX/BLX Rs
        if (rs == 15) {
            ctx.li(TA, (curPC + 4) | (ctx.thumb ? 1u : 0u));
            ctx.E(ppc_stw(TA, FRAME_SCR2, 1));
        } else {
            ctx.E(ppc_stw(RA[rs], FRAME_SCR2, 1));
        }
        emitBX_from_SCR2(ctx);
        ctx.done = true;
        return true;
    }

    // MOV/ADD/CMP with high regs
    uint8_t srcIsPC = (rs == 15);
    uint8_t dstIsPC = (rd == 15);

    if (dstIsPC && o == 1) return false; // CMP PC unsupported form rare

    if (dstIsPC) {
        // ADD/MOV PC, …
        if (o == 2) { // MOV PC, Rs
            if (srcIsPC) ctx.li(TA, curPC + 4);
            else ctx.E(ppc_mr(TA, RA[rs]));
            ctx.E(ppc_stw(TA, FRAME_SCR2, 1));
            emitBX_from_SCR2(ctx);
            ctx.done = true;
            return true;
        }
        if (o == 0) { // ADD PC, Rs
            ctx.li(TA, curPC + 4);
            if (!srcIsPC) ctx.E(ppc_add(TA, TA, RA[rs]));
            else ctx.E(ppc_add(TA, TA, TA)); // PC+PC
            ctx.E(ppc_stw(TA, FRAME_SCR2, 1));
            emitBX_from_SCR2(ctx);
            ctx.done = true;
            return true;
        }
        return false;
    }

    uint8_t pRd = RA[rd];
    if (srcIsPC) {
        ctx.li(TA, curPC + 4);
        switch (o) {
            case 0: ctx.E(ppc_add(pRd, pRd, TA)); break;
            case 1:
                ctx.E(ppc_mr(TB, pRd)); ctx.E(ppc_subfc(TC, TA, TB));
                setNZ(ctx, TC); setC_xer(ctx); setV_sub(ctx, TC, TB, TA); break;
            case 2: ctx.E(ppc_mr(pRd, TA)); break;
        }
        return true;
    }

    uint8_t pRs = RA[rs];
    switch (o) {
        case 0: ctx.E(ppc_add(pRd, pRd, pRs)); break;
        case 1:
            ctx.E(ppc_mr(TB, pRd)); ctx.E(ppc_subfc(TA, pRs, TB));
            setNZ(ctx, TA); setC_xer(ctx); setV_sub(ctx, TA, TB, pRs); break;
        case 2: ctx.E(ppc_mr(pRd, pRs)); break;
    }
    return true;
}

static bool emitT_ldrPc(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t rd = (op >> 8) & 7;
    uint32_t addr = ((curPC + 4) & ~3u) + ((op & 0xFF) << 2);
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.li(TC, addr);
    ctx.call((void*)JitHelp_r32);
    ctx.E(ppc_mr(RA[rd], TA));
    return true;
}

static bool emitT_memReg(Ctx& ctx, uint16_t op) {
    uint8_t rd = op & 7, rb = (op >> 3) & 7, ro = (op >> 6) & 7;
    uint8_t op97 = (op >> 9) & 7;
    void* fn = nullptr;
    bool ld = true, sxb = false, sxh = false;
    switch (op97) {
        case 0: fn = (void*)JitHelp_w32; ld = false; break;
        case 1: fn = (void*)JitHelp_w16; ld = false; break;
        case 2: fn = (void*)JitHelp_w8;  ld = false; break;
        case 3: fn = (void*)JitHelp_r8;  sxb = true; break;
        case 4: fn = (void*)JitHelp_r32; break;
        case 5: fn = (void*)JitHelp_r16; break;
        case 6: fn = (void*)JitHelp_r8;  break;
        case 7: fn = (void*)JitHelp_r16; sxh = true; break;
        default: return false;
    }
    ctx.E(ppc_add(TC, RA[rb], RA[ro]));
    ctx.E(ppc_stw(TC, FRAME_SCR0, 1));
    ctx.ldCore(TA);
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
    uint32_t off = ((op >> 6) & 0x1F) * (hw ? 2u : (by ? 1u : 4u));
    ctx.li(TC, off);
    ctx.E(ppc_add(TC, RA[rb], TC));
    ctx.E(ppc_stw(TC, FRAME_SCR0, 1));
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.E(ppc_lwz(TC, FRAME_SCR0, 1));
    if (!ld) ctx.E(ppc_mr(TD, RA[rd]));
    void* fn = ld ? (hw ? (void*)JitHelp_r16 : (by ? (void*)JitHelp_r8 : (void*)JitHelp_r32))
                  : (hw ? (void*)JitHelp_w16 : (by ? (void*)JitHelp_w8 : (void*)JitHelp_w32));
    ctx.call(fn);
    if (ld) ctx.E(ppc_mr(RA[rd], TA));
    return true;
}

static bool emitT_spLoad(Ctx& ctx, uint16_t op, uint32_t curPC) {
    bool ld = (op >> 11) & 1;
    uint8_t rd = (op >> 8) & 7;
    bool sp = ((op >> 12) & 0xF) == 0x9;
    uint32_t off = (op & 0xFF) << 2;
    if (sp) {
        ctx.li(TA, off);
        ctx.E(ppc_add(TC, RA[13], TA));
    } else {
        ctx.li(TC, ((curPC + 4) & ~3u) + off);
    }
    ctx.E(ppc_stw(TC, FRAME_SCR0, 1));
    ctx.ldCore(TA);
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
        uint32_t imm = (op & 0xFF) << 2;
        if (sp) {
            ctx.li(TA, imm);
            ctx.E(ppc_add(RA[rd], RA[13], TA));
        } else {
            ctx.li(RA[rd], ((curPC + 4) & ~3u) + imm);
        }
        return true;
    }
    if (h == 0xB) {
        uint8_t sub = (op >> 8) & 0xF;
        if (sub == 0x0) {
            ctx.li(TA, (op & 0x7F) << 2);
            ctx.E(ppc_add(RA[13], RA[13], TA));
            return true;
        }
        if (sub == 0x1) {
            ctx.li(TA, (op & 0x7F) << 2);
            ctx.E(ppc_subf(RA[13], TA, RA[13]));
            return true;
        }
    }
    return false;
}

static bool emitT_pushPop(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t opA = (op >> 9) & 0x7;
    if (opA != 0x2 && opA != 0x6) return false;

    for (int i = 0; i < 15; i++)
        ctx.E(ppc_stw(RA[i], FRAME_REGSYNC + i * 4, 1));
    ctx.E(ppc_stw(RCPSR, FRAME_CPSR, 1));
    ctx.li(TA, curPC + 2);
    ctx.E(ppc_stw(TA, FRAME_PC, 1));

    // thumbPushPop(core, arm7, op, regs*, pcOut*, cpsr*)
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.li(TC, (uint32_t)(uint16_t)op);
    ctx.E(ppc_addi(TD, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_addi(TE, 1, (int16_t)FRAME_PC));
    ctx.E(ppc_addi(TF, 1, (int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_thumbPushPop);
    ctx.E(ppc_stw(TA, FRAME_SCR0, 1));

    for (int i = 0; i < 15; i++)
        ctx.E(ppc_lwz(RA[i], FRAME_REGSYNC + i * 4, 1));
    ctx.E(ppc_lwz(RCPSR, FRAME_CPSR, 1));

    bool mayPC = ((op >> 11) & 1) && ((op >> 8) & 1);
    if (mayPC) {
        ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
        ctx.E(ppc_cmpi(0, TA, 1));
        ctx.E(ppc_bc(4, 2, 8)); // bne +8
        size_t bIdx = ctx.sz();
        ctx.E(ppc_b(0));
        emitCommitExitDyn(ctx, EXIT_NORMAL);
        {
            int32_t d = (int32_t)((ctx.sz() - bIdx) * 4);
            ctx.base[bIdx] = ppc_b(d);
        }
        emitCommitExit(ctx, curPC + 2, EXIT_NORMAL);
        ctx.done = true;
        return true;
    }
    return true;
}

static bool emitT_ldmStm(Ctx& ctx, uint16_t op) {
    for (int i = 0; i < 15; i++)
        ctx.E(ppc_stw(RA[i], FRAME_REGSYNC + i * 4, 1));
    ctx.ldCore(TA);
    ctx.E(ppc_addi(TB, 0, ctx.arm7 ? 1 : 0));
    ctx.li(TC, (uint32_t)(uint16_t)op);
    ctx.E(ppc_addi(TD, 1, (int16_t)FRAME_REGSYNC));
    ctx.call((void*)JitHelp_thumbBlock);
    for (int i = 0; i < 15; i++)
        ctx.E(ppc_lwz(RA[i], FRAME_REGSYNC + i * 4, 1));
    return true;
}

static bool emitT_branch(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t h = (op >> 12) & 0xF;
    if (h == 0xE) {
        int32_t off = (int32_t)((int16_t)(op << 5)) >> 4;
        emitCommitExit(ctx, (uint32_t)(curPC + 4 + off), EXIT_NORMAL);
        ctx.done = true;
        return true;
    }
    if (h == 0xD) {
        uint8_t cond = (op >> 8) & 0xF;
        if (cond >= 0xE) return false; // SWI
        int32_t off = ((int32_t)(int8_t)(op & 0xFF)) << 1;
        uint32_t tgt  = curPC + 4 + (uint32_t)off;
        uint32_t fall = curPC + 2;

        size_t si = emitCondSkip(ctx, cond);
        if (si == SIZE_MAX) return false;

        emitCommitExit(ctx, tgt, EXIT_NORMAL);
        patchSkip(ctx, si);
        emitCommitExit(ctx, fall, EXIT_NORMAL);
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
        ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 27, 25)); // clear T
    }
    emitCommitExit(ctx, tgt & ~1u, EXIT_NORMAL);
    ctx.done = true;
    return true;
}

static bool dispThumb(Ctx& ctx, uint16_t op, uint32_t curPC) {
    uint8_t h = (op >> 12) & 0xF;
    switch (h) {
        case 0x0: {
            uint8_t b = (op >> 11) & 3;
            if (b < 3) return emitT_shifts(ctx, op);
            return emitT_addSub3(ctx, op);
        }
        case 0x1: return emitT_imm8(ctx, op);
        case 0x2: {
            uint8_t b = (op >> 10) & 3;
            if (b == 0) return emitT_alu(ctx, op);
            if (b == 1) return emitT_hiReg(ctx, op, curPC);
            return emitT_ldrPc(ctx, op, curPC);
        }
        case 0x3: case 0x4: case 0x5: return emitT_memReg(ctx, op);
        case 0x6: case 0x7: case 0x8: return emitT_memImm(ctx, op);
        case 0x9: return emitT_spLoad(ctx, op, curPC);
        case 0xA: return emitT_addSpPc(ctx, op, curPC);
        case 0xB: {
            if (((op >> 8) & 0xF) <= 0x1) return emitT_addSpPc(ctx, op, curPC);
            uint8_t opA = (op >> 9) & 0x7;
            if (opA == 0x2 || opA == 0x6) return emitT_pushPop(ctx, op, curPC);
            return false;
        }
        case 0xC: return emitT_ldmStm(ctx, op);
        case 0xD: return emitT_branch(ctx, op, curPC);
        case 0xE: return emitT_branch(ctx, op, curPC);
        default:  return false;
    }
}

// ============================================================
// Compile / run
// ============================================================
static bool validPC(uint32_t pc, bool gba) {
    pc &= ~1u;
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
    if (!codeBuf || !g_jitLive) return nullptr;
    if (!validPC(armPC, core->gbaMode)) return nullptr;

    bool thumb = interp->isThumb();
    size_t bkt = hashPC(armPC);
    {
        JitBlock& slot = cache[bkt];
        if (slot.valid && slot.armPC == armPC && slot.thumb == thumb &&
            slot.gen == cacheGen &&
            slot.code >= codeBuf && slot.code < codeBuf + JIT_WORDS &&
            slot.nW > 0)
            return &slot;
        slot.valid = false;
    }

    if (codePos + BLK_WDS >= JIT_WORDS) flushJitCache();

    JitBlock& slot = cache[bkt];
    Ctx ctx;
    ctx.base = codeBuf + codePos;
    ctx.cur = ctx.base;
    ctx.cap = JIT_WORDS - codePos;
    if (ctx.cap > BLK_WDS) ctx.cap = BLK_WDS;
    ctx.thumb = thumb;
    ctx.arm7 = arm7;
    ctx.done = false;
    ctx.overflow = false;
    ctx.blockPC = armPC;
    ctx.cpuIdx = cpuIdx;
    ctx.interp = interp;
    ctx.core = core;

    emitPrologue(ctx);
    emitSyncFrom(ctx);

    uint32_t curPC = armPC;
    int n = 0;

    while (n < (int)BLK_ARMS && !ctx.done && !ctx.overflow) {
        // leave room for a large exit sequence
        if (ctx.remaining() < 64) {
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
            // BL/BLX prefix
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
            bool ok = dispThumb(ctx, op, curPC);
            if (!ok) {
                if (g_dbgFallbacks < 16) {
                    printf("[JIT] thumb fallback pc=%08X op=%04X\n", curPC, op);
                    g_dbgFallbacks++;
                }
                emitCommitExit(ctx, curPC, EXIT_FALLBACK);
                ctx.done = true;
            } else {
                curPC += 2;
                n++;
            }
        } else {
            uint32_t op = core->memory.read<uint32_t>(arm7, curPC);
            bool ok = dispARM(ctx, op, curPC);
            if (!ok) {
                if (g_dbgFallbacks < 16) {
                    printf("[JIT] arm fallback pc=%08X op=%08X\n", curPC, op);
                    g_dbgFallbacks++;
                }
                emitCommitExit(ctx, curPC, EXIT_FALLBACK);
                ctx.done = true;
            } else {
                curPC += 4;
                n++;
                uint32_t it = (op >> 25) & 7;
                if (it == 5) ctx.done = true;
                if ((op & 0x0FFFFFF0) == 0x012FFF10) ctx.done = true;
                if ((op & 0x0E000000) == 0x0A000000) ctx.done = true;
            }
        }
    }

    if (!ctx.done && !ctx.overflow)
        emitCommitExit(ctx, curPC, EXIT_NORMAL);

    if (ctx.overflow || ctx.sz() < 8) {
        // Do not publish a broken block
        return nullptr;
    }

    size_t wds = ctx.sz();
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

static bool isGoodPC(uint32_t pc, bool gba) {
    return pc != 0xFFFFFFFFu && validPC(pc, gba);
}

static void runCpu(Core& core, int cpu, bool gba) {
    Interpreter& interp = core.interpreter[cpu];
    if (interp.halted) return;
    if (!interp.isReady()) {
        interp.jitRunOpcode();
        return;
    }

    uint32_t pc = interp.getActualPC();
    if (!isGoodPC(pc, gba)) {
        interp.jitRunOpcode();
        return;
    }

    JitBlock* b = compile(&interp, &core, pc, /*arm7*/ (cpu == 1) || gba, cpu);
    if (!b || !b->code || b->nW == 0 ||
        b->code < codeBuf || b->code >= codeBuf + JIT_WORDS) {
        interp.jitRunOpcode();
        return;
    }

    executeBlock_asm(b->code);

    // JitHelp_commit already called setPC + synced regs/cpsr.
    // Only need to step the unhandled opcode on FALLBACK.
    if (g_exitReason[cpu] == EXIT_FALLBACK) {
        // commit left PC at the fallback instruction
        interp.jitRunOpcode();
    }
}

void runJitNds(Core& core) {
    if (!g_jitLive || !codeBuf) {
        Interpreter::runCoreNds(core);
        return;
    }
    for (int cpu = 0; cpu < 2; cpu++)
        runCpu(core, cpu, false);
    JitHelp_tick(&core, CYCLES_PER_BLOCK);
}

void runJitGba(Core& core) {
    if (!g_jitLive || !codeBuf) {
        Interpreter::runCoreSingle<true, 0>(core);
        return;
    }
    runCpu(core, 1, true);
    JitHelp_tick(&core, CYCLES_PER_BLOCK);
}

// ============================================================
// Lifecycle
// ============================================================
bool initJit(Core* core) {
    g_jitLive = false;
    codeBuf = nullptr;

    void* raw = memalign(32, JIT_BYTES);
    if (!raw) {
        printf("[JIT] memalign(%zu) failed — interpreter only\n", JIT_BYTES);
        return false;
    }

    uintptr_t addr = (uintptr_t)raw;
    bool inMem1 = (addr >= 0x80000000u && addr + JIT_BYTES <= 0x81800000u);

    if (!inMem1) {
        if (addr < 0x01800000u) {
            addr |= 0x80000000u;
            inMem1 = (addr + JIT_BYTES <= 0x81800000u);
        } else if (addr >= 0xC0000000u && addr < 0xC1800000u) {
            addr -= 0x40000000u;
            inMem1 = (addr + JIT_BYTES <= 0x81800000u);
        }
    }

    if (!inMem1) {
        printf("[JIT] %p not in executable MEM1 — interpreter only\n", raw);
        free(raw);
        return false;
    }

    // Verify executeBlock_asm is in MEM1 too
    uintptr_t tramp = (uintptr_t)(void*)executeBlock_asm;
    if (tramp < 0x80000000u || tramp >= 0x81800000u) {
        printf("[JIT] trampoline %p not in MEM1 — interpreter only\n", (void*)tramp);
        free(raw);
        return false;
    }

    codeBuf  = (uint32_t*)addr;
    codePos  = 0;
    cacheGen = 0;
    g_dbgFallbacks = 0;
    for (size_t i = 0; i < CSIZ; i++) cache[i].valid = false;
    g_exitPC[0] = g_exitPC[1] = 0;
    g_exitCPSR[0] = g_exitCPSR[1] = 0;
    g_exitReason[0] = g_exitReason[1] = EXIT_NORMAL;

    memset(codeBuf, 0, JIT_BYTES);
    DCFlushRange(codeBuf, JIT_BYTES);
    ICInvalidateRange(codeBuf, JIT_BYTES);

    g_jitLive = true;

    printf("[JIT] OK buf=%p (%zuKB) tramp=%p\n",
           (void*)codeBuf, JIT_BYTES >> 10, (void*)tramp);
    printf("[JIT] cond=C-helper commit=setPC overflow-guard on\n");

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
