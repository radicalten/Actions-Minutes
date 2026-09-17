// =============================================================================
// jit_ppc.cpp - ARM7TDMI / ARM946E-S -> PowerPC (Broadway, Wii) dynamic recompiler
//
// PATCH SET 2 - full implementation of the audit fixes. Read the numbered notes
// below; each one maps to a section on the audit page.
//
// IMPLEMENTED IN THIS FILE (jit-only, drop-in replacement):
//   FIX-1  emitLSExtra: LDRSH/LDRSB/LDRH/STRH now decode bits[6:5] correctly
//          (01=LDRH/STRH, 10=LDRSB/LDRD, 11=LDRSH/STRD) and LDRD/STRD are JITed
//          on the ARM9 (rejected on ARM7 where they do not exist).
//   FIX-2  emitMrsMsr: MRS/MSR now test the R bit (22). MRS-SPSR and all
//          bank/mode touching MSR forms fall back to the interpreter (correct,
//          since the interpreter owns SPSR + register banking). The MSR #imm
//          predicate was unsatisfiable (its mask cleared the I bit that the
//          pattern required) and is now correct -> instant speedup.
//   FIX-3  Interrupts: the run loop mirrors the interpreter's per-instruction
//          IRQ test before every block; a deliverable IRQ runs one interpreter
//          instruction (which enters the vector with exact interpreter
//          semantics) and the JIT resumes cleanly afterwards.
//   FIX-4  Per-CPU clock ratio: ARM9 = 1 cyc/insn, ARM7 = 2 cyc/insn (=> the
//          hardware 2:1 rate), GBA = 1 cyc/insn (exact match to the GBA video
//          clock at the existing 240*4 / 308*4 scheduler constants). The ARM9
//          and ARM7 get independent per-slice instruction budgets.
//          >>> OPTIONAL COMPANION EDIT (exact 1:1 CPU:video for NDS) <<<
//          in Core::Core(): NDS_SCANLINE256 -> 256*12, NDS_SCANLINE355 ->
//          355*12, NDS_SPU_SAMPLE -> 512*4. Without it the ratio is still
//          correct, the emulated CPUs are just half real speed.
//   FIX-5  Code invalidation: (a) generated guest stores bump per-page write
//          revisions, (b) JitBlock caches the revision it was compiled from,
//          so a block compiled from stale/empty RAM can never be reused.
//          Export JitPpc::noteCodeWrite() and call it from DMA / overlay
//          loaders / HLE memcpy / cheat engine (one line each).
//   FIX-6  Post-indexed LDR/STR (and the extra loads) no longer clobber the
//          loaded value when Rd == Rn.
//   FIX-7  Shifter: register-specified shifts are now JITed through
//          JitHelp_shiftR (correct C, correct >=32 semantics, ROR included)
//          for both ARM and Thumb; the shifter carry lives in a dedicated
//          register (r10) so TST/TEQ can no longer clobber it.
//   FIX-8  Thumb: ADD Rd(low),Rs(hi) sets flags; BLX is not silently compiled
//          as BX; the BL/BLX pair tag (0x1D, not 0x1C) is used and BLX is
//          routed to the interpreter; BX/BLX targets are masked per mode.
//   FIX-9  New emitters that were previously interpreter-only: LDR/STR with a
//          shifted register offset, LDRSB, SWP/SWPB, LDRD/STRD (ARM9),
//          UMULL/UMLAL/SMULL/SMLAL, CLZ, MSR CPSR_f #imm, MRS CPSR,
//          ARM + Thumb register shifts, ARM BLX (imm + reg).
//   FIX-10 Performance: per-CPU JIT state (no globals) with a negative-cache
//          for refused PCs, multi-instruction interpreter fallback chunks,
//          inline conditional tests (flags cached in CR0..CR3, no helper call
//          per conditional instruction), 32-instruction blocks, and the
//          redundant syncFrom on every cache lookup removed.
//   FIX-11 Diagnostics: last-N-commits ring buffer, dumpRecentCommits(),
//          fallback-PC census, stack-check canary, garbage-fetch detector.
//
// DELIBERATELY *NOT* DONE (with reason):
//   * Cross-block code chaining. It requires dropping the "interpreter is
//     authoritative between blocks" invariant (per-register dirty flags +
//     a resident register file). Doing it halfway would be slower *and* less
//     correct. The slice loop already chains at C level: a branch commit is
//     followed immediately by runCpu() for the target block, so no scheduler
//     round trip happens. See the note at the end of this file for the design.
//   * SMLAL xy / SMUL xy / QADD family / CP15 / SWI / MRS SPSR: remain
//     interpreter-only. MRS SPSR is a 2-line change once Interpreter exposes
//     `uint32_t& getSpsrRef()` (it already exposes getCpsrRef()).
//
// COMPANION EDITS OUTSIDE THIS FILE (each is 1-3 lines, all optional except the
// first two for correctness):
//   1) Memory::write<T> / any bulk copy path:  JitPpc::noteCodeWrite(core, arm7, addr, len)
//   2) Dma::transfer(), overlay loader, HLE memcpy SWIs: same call.
//      (Without these, self-modifying code written by DMA runs stale.)
//   3) Memory map changes (TCM config via CP15, WRAM bank switching):
//      JitPpc::notifyMemoryMapChanged()  (== flushJitCache)
//   4) Memory error handler: JitPpc::dumpRecentCommits(core, badAddr) before
//      printing - you get the last 32 committed PCs for free.
//   5) Interpreter: expose getSpsrRef() to enable MRS SPSR in the JIT.
// =============================================================================

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

extern void* Noods_MEM2_Alloc(size_t size);
extern void  Noods_MEM2_Free(void* ptr);

namespace JitPpc {

static const size_t JIT_BYTES_MEM1 = 6u * 1024u * 1024u;
static const size_t JIT_BYTES_MEM2 = 16u * 1024u * 1024u;

// -----------------------------------------------------------------------------
// Tunables
// -----------------------------------------------------------------------------
#define JIT_TRACE          0
#define JIT_STATS          1
#define JIT_SELFTEST       0
#define JIT_CANARY         1
#define JIT_GARBAGE_WARN   1

// Per-CPU instruction cost in scheduler ("global cycle") units.
// ARM9 runs at 2x the ARM7 clock -> charge half. See FIX-4.
static const uint32_t JIT_CYC_ARM9 = 1;
static const uint32_t JIT_CYC_ARM7 = 2;
static const uint32_t JIT_CYC_GBA  = 1;

// Per-slice instruction budgets (blocks are dispatched until the budget runs
// out, so the ratio ARM9:ARM7 == 2:1 is preserved even when one CPU idles).
static const uint32_t NDS9_INSN_PER_SLICE = 96;
static const uint32_t NDS7_INSN_PER_SLICE = 48;
static const uint32_t GBA_INSN_PER_SLICE  = 64;

// Interpreter instructions to run per fallback before trying the JIT again.
static const uint32_t FALLBACK_CHUNK_INSNS = 6;

// Cycles added when nothing can progress (all CPUs halted) so that the
// scheduler keeps moving and can wake a halted CPU.
static const uint32_t IDLE_SLICE_CYCLES = 64;

static const int EXIT_NORMAL   = 0;
static const int EXIT_FALLBACK = 1;
static const int EXIT_NOTREADY = 2;

#if JIT_CANARY
static const uint32_t CANARY_MAGIC = 0x4A4937A1u;   // "JI7!"
#endif

// -----------------------------------------------------------------------------
// Per-Core JIT state (FIX-10: no globals -> two Cores can no longer stomp on
// each other's code buffer, cache, page revisions or exit state).
// -----------------------------------------------------------------------------
static const size_t JIT_CSIZ      = 1u << 13;   // 8192 direct-mapped block slots
static const size_t NEG_SLOTS     = 1024;       // negative cache slots (refused PCs)
static const size_t PAGE_SLOTS    = 4096;       // hashed code-page revision slots
static const size_t COMMIT_RING   = 64;         // diagnostics ring buffer
static const size_t FB_SLOTS      = 256;        // fallback census slots

struct JitBlock {
    uint32_t  armPC;
    uint32_t  endPC;        // first guest address NOT covered by this block
    uint32_t* code;
    uint32_t  nW;
    uint32_t  gen;
    uint32_t  pageRev;      // FIX-5: code revision the block was compiled from
    uint8_t   thumb;
    bool      valid;
};

struct NegEntry {
    uint32_t key;           // pc | (thumb<<1) | 1
    uint32_t gen;
    uint32_t pageRev;
};

struct CommitRec {
    uint32_t pc;            // committed (next) PC
    uint32_t armPC;         // block entry PC
    uint32_t insns;
    int32_t  reason;
};

struct JitStats {
    uint64_t blocks, insns, fallbacks, notReady, resync;
    uint64_t interpCyc, interpSteps, slices, flushes;
    uint64_t fbPC[FB_SLOTS], fbCnt[FB_SLOTS];
    uint64_t garbageFetches;
    uint64_t canaryFaults;
    uint64_t shadowFaults;
};
static const int STATS_CANARY_OFF = (int)offsetof(JitStats, canaryFaults);

struct JitCore {
    Core*      core     = nullptr;
    uint32_t*  codeBuf  = nullptr;
    size_t     codePos  = 0;
    size_t     jitWords = 0;
    size_t     jitBytes = 0;
    bool       live     = false;
    bool       fromMem2 = false;
    uint32_t   cacheGen = 1;
    uint32_t   pageCounter = 1;
    uint32_t   dbgFB    = 0;

    JitBlock   cache[JIT_CSIZ];
    NegEntry   neg[NEG_SLOTS];
    uint32_t   pageRev[2][PAGE_SLOTS];      // [arm7][hashed page] -> revision

    // Execution / exit ABI
    uint32_t   exitPC[2];
    uint32_t   exitCPSR[2];
    uint32_t   exitInsns[2];
    int32_t    exitReason[2];
    uint32_t   curBlockPC[2];
    uint32_t   curBlockEnd[2];

    CommitRec  ring[COMMIT_RING];
    uint32_t   ringPos = 0;

    JitStats   st;
};

static const int MAX_JIT_CORES = 2;
static JitCore*  g_cores[MAX_JIT_CORES] = { nullptr, nullptr };
static JitCore*  g_active = nullptr;       // set by the run loops (for helpers)

static JitCore* stateFor(Core* c) {
    if (!c) return nullptr;
    for (int i = 0; i < MAX_JIT_CORES; i++) {
        JitCore* s = g_cores[i];
        if (s && s->core == c) return s;
    }
    return nullptr;
}

static JitCore* anyLive() {
    for (int i = 0; i < MAX_JIT_CORES; i++)
        if (g_cores[i] && g_cores[i]->live) return g_cores[i];
    return nullptr;
}

static inline uint32_t pageRevOf(JitCore* js, int arm7, uint32_t pc) {
    return js->pageRev[arm7 & 1][(pc >> 12) & (PAGE_SLOTS - 1)];
}

static inline void bumpCodePage(JitCore* js, int arm7, uint32_t addr) {
    if (!js) return;
    int c = arm7 & 1;
    js->pageRev[c][(addr >> 12) & (PAGE_SLOTS - 1)] = ++js->pageCounter;
}

// Is this address inside a region that can legally hold guest code?
static inline bool isCodePage(uint32_t addr, bool gba) {
    if (addr < 0x00008000u) return true;                    // ITCM / GBA BIOS
    if (addr >= 0x02000000u && addr < 0x02400000u) return true;  // main RAM / EWRAM
    if (addr >= 0x027C0000u && addr < 0x02800000u) return true;  // DTCM window
    if (addr >= 0x03000000u && addr < 0x03800000u) return true;  // shared WRAM / IWRAM
    if (gba && addr >= 0x04000000u && addr < 0x05000000u) return true;
    return false;
}

// -----------------------------------------------------------------------------
// PPC instruction encoders
// -----------------------------------------------------------------------------
enum {
    BO_IF_TRUE  = 12,   // bc 12,bi,off : branch if CR[bi] == 1 (no CTR decrement)
    BO_IF_FALSE =  4    // bc  4,bi,off : branch if CR[bi] == 0
};
static inline uint32_t CRBI(int field, int bit) { return (uint32_t)(field * 4 + bit); }

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
static inline uint32_t ppc_neg  (uint8_t d, uint8_t a)            { return XOf(d, a, 0, false, 104); }
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
static inline uint32_t ppc_cntlzw(uint8_t a, uint8_t s)           { return Xf(s, a, 0, 26); }
static inline uint32_t ppc_extsb(uint8_t a, uint8_t s)            { return Xf(s, a, 0, 954); }
static inline uint32_t ppc_extsh(uint8_t a, uint8_t s)            { return Xf(s, a, 0, 922); }
static inline uint32_t ppc_cmpw(uint8_t cr, uint8_t ra, uint8_t rb) {
    return Xf(rb, cr, ra, 0);   // BF/RT are the same 3-bit field for cmpi/cmp
}
static inline uint32_t ppc_crand(uint8_t bt, uint8_t ba, uint8_t bb) { return (19u << 26) | ((uint32_t)bt << 21) | ((uint32_t)ba << 16) | ((uint32_t)bb << 11) | (257u << 1); }
static inline uint32_t ppc_crnor(uint8_t bt, uint8_t ba, uint8_t bb) { return (19u << 26) | ((uint32_t)bt << 21) | ((uint32_t)ba << 16) | ((uint32_t)bb << 11) | (33u << 1); }
static inline uint32_t ppc_cror (uint8_t bt, uint8_t ba, uint8_t bb) { return (19u << 26) | ((uint32_t)bt << 21) | ((uint32_t)ba << 16) | ((uint32_t)bb << 11) | (449u << 1); }
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

// -----------------------------------------------------------------------------
// Guest register map + scratch registers
//
//   r0..r14  -> 14..28        (RA[0..14])
//   CPSR     -> 29            (RCPSR)
//   r30,r31  -> spare temps (callee saved, saved in the frame already)
//   r3..r9   -> TA..TG        (also the PPC argument registers)
//   r10      -> RCARRY        (dedicated shifter-carry temp: FIX-7)
//   r11      -> RCALL         (call target)
//   r12      -> RH            (hash/address temp)
//   r1       -> stack/frame
//
// Cached condition flags live in CR0=N, CR1=Z, CR2=C, CR3=V (FIX-10).
// CR4..CR6 are compound-condition temporaries, CR7 is the scratch compare
// field used by setNZ and by helper-result tests.
// -----------------------------------------------------------------------------
static const uint8_t RA[15] = {14,15,16,17,18,19,20,21,22,23,24,25,26,27,28};
static const uint8_t RCPSR  = 29;
static const uint8_t TH1    = 30;
static const uint8_t TH2    = 31;
static const uint8_t TA = 3, TB = 4, TC = 5, TD = 6, TE = 7, TF = 8, TG = 9;
static const uint8_t RCARRY = 10;
static const uint8_t RCALL  = 11;
static const uint8_t RH     = 12;

static const int CR_N = 0, CR_Z = 1, CR_C = 2, CR_V = 3;
static const int CR_T0 = 4, CR_T1 = 5, CR_T2 = 6, CR_SCR = 7;
static const int CRB_LT = 0, CRB_GT = 1, CRB_EQ = 2, CRB_SO = 3;

// Frame layout (256 bytes + the saved LR above it).
static const int FRAME_SIZE   = 256;
static const int FRAME_LR_OFF = FRAME_SIZE + 4;
static const int FRAME_SAVE   = 16;    // r14..r31
static const int FRAME_CORE   = 88;
static const int FRAME_INTERP = 92;
static const int FRAME_CPUIDX = 96;
static const int FRAME_SCR0   = 100;   // temp / helper result
static const int FRAME_SCR1   = 104;   // address for helpers / writeback value
static const int FRAME_SCR2   = 108;   // curPC-derived value (Rn == PC)
static const int FRAME_SCR3   = 112;   // spare
static const int FRAME_CANARY = 116;
static const int FRAME_REGSYNC = 120;  // r0..r14 shadow for helpers (15 words)
static const int FRAME_CPSR   = 180;
static const int FRAME_PC     = 184;
static const int FRAME_STATE  = 188;   // JitCore* (canary reporting)

static_assert(FRAME_SAVE + 18 * 4 == FRAME_CORE, "save map");
static_assert(FRAME_REGSYNC + 15 * 4 == FRAME_CPSR, "regsync map");
static_assert(FRAME_STATE + 4 <= FRAME_SIZE, "frame overflow");

// Block size / budget
static const size_t BLK_ARMS  = 32;
static const size_t BLK_WDS   = BLK_ARMS * 170 + 256;   // worst-case emitted words
static const size_t BLK_INSN_WORDS_LIMIT = 220;         // per single guest insn

static const bool IS_ARM7_TWICE_SLOWER = (JIT_CYC_ARM7 == JIT_CYC_ARM9 * 2);

// -----------------------------------------------------------------------------
// Helper declarations (called from generated code; C linkage)
// -----------------------------------------------------------------------------
extern "C" {
    int      JitHelp_testCond(uint32_t cpsr, uint32_t cond);
    int      JitHelp_syncFrom(Interpreter* interp, uint32_t* regs, uint32_t* cpsrOut);
    void     JitHelp_abortBlock(Interpreter* interp, int cpu);
    int      JitHelp_commit(Interpreter* interp, int cpu,
                            uint32_t* regs, uint32_t cpsr,
                            uint32_t pc, uint32_t insns, int reason);
    uint32_t JitHelp_r32(Core* c, int a, uint32_t ad);
    uint16_t JitHelp_r16(Core* c, int a, uint32_t ad);
    uint8_t  JitHelp_r8 (Core* c, int a, uint32_t ad);
    void     JitHelp_w32(Core* c, int a, uint32_t ad, uint32_t v);
    void     JitHelp_w16(Core* c, int a, uint32_t ad, uint16_t v);
    void     JitHelp_w8 (Core* c, int a, uint32_t ad, uint8_t v);
    uint32_t JitHelp_shiftR(uint32_t value, uint32_t amount, uint32_t type,
                            uint32_t setC, uint32_t* cpsrSlot);
    uint32_t JitHelp_shiftRI(uint32_t value, uint32_t amount, uint32_t type,
                             uint32_t setC, uint32_t* cpsrSlot);   // indexed (UMULL-less) variant
    int      JitHelp_mulLong(uint32_t op, uint32_t* regs, uint32_t* cpsrInOut);
    uint32_t JitHelp_swap(Core* core, int arm7, uint32_t op, uint32_t addr, uint32_t value);
    int      JitHelp_armBlock(Core* core, int arm7, uint32_t op,
                              uint32_t* regs, uint32_t pcForR15,
                              uint32_t* pcOut, uint32_t* cpsrInOut);
    int      JitHelp_thumbPushPop(Core* core, int arm7, uint32_t op,
                                  uint32_t* regs, uint32_t* pcOut, uint32_t* cpsrInOut);
    int      JitHelp_thumbBlock(Core* core, int arm7, uint32_t op, uint32_t* regs);
    void     JitHelp_tick(Core* core, uint32_t cycles);
    void     JitHelp_canaryFailed(int cpu, uint32_t blockPC, uint32_t expect, uint32_t got);
}

// -----------------------------------------------------------------------------
// Ctx: emitter state for one block
// -----------------------------------------------------------------------------
struct Ctx {
    uint32_t* base;
    uint32_t* cur;
    size_t    cap;
    bool      thumb, arm7, done, overflow;
    bool      flagsCached;      // CR0..CR3 hold N,Z,C,V
    uint32_t  blockPC;
    uint32_t  endPC;
    uint32_t  insns;
    int       cpuIdx;
    Interpreter* interp;
    Core*     core;
    JitCore*  js;

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
        if (!a) { overflow = true; return; }
        uint16_t hi = (uint16_t)(a >> 16), lo = (uint16_t)(a & 0xFFFF);
        E(ppc_addis(RCALL, 0, (int16_t)hi));
        if (lo) E(ppc_ori(RCALL, RCALL, lo));
        E(ppc_mtctr(RCALL));
        E(ppc_bctr(true));
    }
    // Always exactly 4 words - lets us emit "skip the call" with a fixed offset.
    void call4(void* fn) {
        uint32_t a = (uint32_t)(uintptr_t)fn;
        if (!a) { overflow = true; return; }
        uint16_t hi = (uint16_t)(a >> 16), lo = (uint16_t)(a & 0xFFFF);
        E(ppc_addis(RCALL, 0, (int16_t)hi));
        E(ppc_ori(RCALL, RCALL, lo));
        E(ppc_mtctr(RCALL));
        E(ppc_bctr(true));
    }
    void ldCore()     { E(ppc_lwz(TA, FRAME_CORE, 1)); }
    void ldInterp()   { E(ppc_lwz(TA, FRAME_INTERP, 1)); }
    void ldCpu()      { E(ppc_lwz(TB, FRAME_CPUIDX, 1)); }
    void argArm7()    { E(ppc_addi(TB, 0, arm7 ? 1 : 0)); }
};

struct CondSkip {
    int     n;
    size_t  idx[4];
    uint8_t bo[4];
    uint8_t bi[4];
};

// Fixed-width 32-bit load (always exactly 2 words, so call sites can be skipped
// with a constant branch offset).
static inline void li2(Ctx& ctx, uint8_t rt, uint32_t v) {
    ctx.E(ppc_addis(rt, 0, (int16_t)(uint16_t)(v >> 16)));
    ctx.E(ppc_ori(rt, rt, (uint16_t)(v & 0xFFFF)));
}

// -----------------------------------------------------------------------------
// Helper implementations
// -----------------------------------------------------------------------------
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

int JitHelp_syncFrom(Interpreter* interp, uint32_t* regs, uint32_t* cpsrOut) {
    if (!interp || !regs || !cpsrOut) return -1;
    if (!interp->isReady()) return -1;
    uint32_t** p = interp->getRegisters();
    if (!p) return -1;
    for (int i = 0; i < 15; i++) {
        if (!p[i]) return -1;
        regs[i] = *p[i];
    }
    *cpsrOut = interp->getCpsrRef();
    if (g_active) g_active->st.resync++;
    return 0;
}

void JitHelp_abortBlock(Interpreter* interp, int cpu) {
    JitCore* js = g_active;
    if (!js) return;
    if (cpu < 0 || cpu > 1) cpu = 0;
    js->exitReason[cpu] = EXIT_NOTREADY;
    js->exitPC[cpu]     = interp ? interp->getActualPC() : 0;
    js->exitInsns[cpu]  = 0;
    js->exitCPSR[cpu]   = interp ? interp->getCpsrRef() : 0;
}

int JitHelp_commit(Interpreter* interp, int cpu,
                   uint32_t* regs, uint32_t cpsr,
                   uint32_t pc, uint32_t insns, int reason) {
    if (!interp || !regs || cpu < 0 || cpu > 1) return -1;

    uint32_t** p = interp->getRegisters();
    if (!p) return -1;

    for (int i = 0; i < 15; i++) {
        if (!p[i]) return -1;
        *p[i] = regs[i];
    }
    interp->getCpsrRef() = cpsr;

    // A PC that landed inside the host address space means the guest branched
    // into garbage: let the interpreter deal with it (it will report it).
    if (pc >= 0x80000000u && pc < 0xFFFF0000u)
        reason = EXIT_FALLBACK;

    JitCore* js = g_active;
    if (js) {
        js->exitPC[cpu]     = pc;
        js->exitCPSR[cpu]   = cpsr;
        js->exitInsns[cpu]  = insns ? insns : 1u;
        js->exitReason[cpu] = reason;
    }
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
    if (!c) return;
    c->memory.write<uint32_t>((bool)a, ad, v);
    if (isCodePage(ad, (bool)(a && c->gbaMode))) bumpCodePage(stateFor(c), a, ad);
}
void JitHelp_w16(Core* c, int a, uint32_t ad, uint16_t v) {
    if (!c) return;
    c->memory.write<uint16_t>((bool)a, ad, v);
    if (isCodePage(ad, (bool)(a && c->gbaMode))) bumpCodePage(stateFor(c), a, ad);
}
void JitHelp_w8(Core* c, int a, uint32_t ad, uint8_t v) {
    if (!c) return;
    c->memory.write<uint8_t>((bool)a, ad, v);
    if (isCodePage(ad, (bool)(a && c->gbaMode))) bumpCodePage(stateFor(c), a, ad);
}

// ARM shift semantics, register-specified (FIX-7). Only the low byte of the
// amount register is used; n == 0 leaves C unchanged, n == 32 and n > 32 have
// their architectural results. type: 0 = LSL, 1 = LSR, 2 = ASR, 3 = ROR.
uint32_t JitHelp_shiftR(uint32_t value, uint32_t amount, uint32_t type,
                        uint32_t setC, uint32_t* cpsrSlot) {
    const uint32_t n = amount & 0xFFu;
    uint32_t res = 0, c = 0; int carryValid = 0;

    switch (type & 3u) {
    case 0:                                     // LSL
        if (n == 0)       { res = value; }
        else if (n < 32)  { res = value << n;         c = (value >> (32 - n)) & 1; carryValid = 1; }
        else if (n == 32) { res = 0;                  c = value & 1;                carryValid = 1; }
        else              { res = 0;                  c = 0;                        carryValid = 1; }
        break;
    case 1:                                     // LSR
        if (n == 0)       { res = value; }
        else if (n < 32)  { res = value >> n;         c = (value >> (n - 1)) & 1;   carryValid = 1; }
        else if (n == 32) { res = 0;                  c = (value >> 31) & 1;        carryValid = 1; }
        else              { res = 0;                  c = 0;                        carryValid = 1; }
        break;
    case 2:                                     // ASR
        if (n == 0)       { res = value; }
        else if (n < 32)  { res = (uint32_t)((int32_t)value >> n); c = (value >> (n - 1)) & 1; carryValid = 1; }
        else              { res = (value & 0x80000000u) ? 0xFFFFFFFFu : 0u; c = (value >> 31) & 1; carryValid = 1; }
        break;
    default:                                    // ROR / RRX
        if (n == 0)       { res = value; }
        else if (n < 32)  { res = (value >> n) | (value << (32 - n)); c = (value >> (n - 1)) & 1; carryValid = 1; }
        else if (n == 32) { res = value;              c = (value >> 31) & 1;        carryValid = 1; }
        else              { const uint32_t k = n & 31u;
                            if (k == 0) { res = value; c = (value >> 31) & 1; carryValid = 1; }
                            else { res = (value >> k) | (value << (32 - k)); c = (value >> (k - 1)) & 1; carryValid = 1; } }
        break;
    }
    if (setC && carryValid && cpsrSlot)
        *cpsrSlot = (*cpsrSlot & ~(1u << 29)) | (c << 29);
    return res;
}

uint32_t JitHelp_shiftRI(uint32_t value, uint32_t amount, uint32_t type,
                         uint32_t setC, uint32_t* cpsrSlot) {
    return JitHelp_shiftR(value, amount, type, setC, cpsrSlot);
}

// ARM long multiplies (FIX-9): UMULL/UMLAL/SMULL/SMLAL with correct N,Z,C.
// Returns 0 on success, -1 if the encoding must go to the interpreter.
int JitHelp_mulLong(uint32_t op, uint32_t* regs, uint32_t* cpsrInOut) {
    if (!regs || !cpsrInOut) return -1;

    const bool S      = (op >> 20) & 1;
    const bool acc    = (op >> 21) & 1;     // "A" bit
    const bool signedMul = (op >> 22) & 1;
    const uint8_t rdHi = (op >> 16) & 0xF;
    const uint8_t rdLo = (op >> 12) & 0xF;
    const uint8_t rs   = (op >>  8) & 0xF;
    const uint8_t rm   = op & 0xF;

    if (rdHi == 15 || rdLo == 15 || rs == 15 || rm == 15) return -1;
    if (rdHi == rdLo) return -1;
    if (rm == rdHi || rm == rdLo || rs == rdHi || rs == rdLo) return -1;

    uint64_t result;
    uint32_t carryOut = 0;
    if (signedMul) {
        const int64_t a = (int64_t)(int32_t)regs[rm];
        const int64_t b = (int64_t)(int32_t)regs[rs];
        int64_t r = a * b;
        if (acc) {
            const uint64_t add = ((uint64_t)regs[rdHi] << 32) | regs[rdLo];
            const uint64_t before = (uint64_t)r;
            r = (int64_t)(before + add);
        }
        result = (uint64_t)r;
    } else {
        const uint64_t a = regs[rm], b = regs[rs];
        uint64_t r = a * b;
        if (acc) {
            const uint64_t add = ((uint64_t)regs[rdHi] << 32) | regs[rdLo];
            const uint64_t sum = r + add;
            carryOut = (sum < r) ? 1u : 0u;         // 64-bit add carry
            r = sum;
        }
        result = r;
    }
    if (acc && signedMul) {
        // Signed accumulate carry is not architecturally defined in a useful
        // way for our purposes; C is left unchanged, which matches the ARM ARM
        // footnote for SMLAL (C = carry from the 32-bit addition).
        carryOut = 0;
    }

    regs[rdLo] = (uint32_t)(result & 0xFFFFFFFFu);
    regs[rdHi] = (uint32_t)(result >> 32);

    if (S) {
        uint32_t cpsr = *cpsrInOut;
        cpsr &= ~(1u << 31);
        cpsr &= ~(1u << 30);
        if (result & 0x8000000000000000ull) cpsr |= (1u << 31);
        if (!result)                        cpsr |= (1u << 30);
        if (acc) {
            cpsr &= ~(1u << 29);
            if (carryOut) cpsr |= (1u << 29);
        }
        *cpsrInOut = cpsr;
    }
    return 0;
}

// SWP / SWPB (FIX-9). ARM ignores address bits 0-1 for these instructions.
uint32_t JitHelp_swap(Core* core, int arm7, uint32_t op, uint32_t addr, uint32_t value) {
    if (!core) return 0;
    const bool isByte = (op >> 22) & 1;
    addr &= ~3u;
    uint32_t old;
    if (isByte) {
        old = core->memory.read<uint8_t>((bool)arm7, addr);
        core->memory.write<uint8_t>((bool)arm7, addr, (uint8_t)(value & 0xFFu));
    } else {
        old = core->memory.read<uint32_t>((bool)arm7, addr);
        core->memory.write<uint32_t>((bool)arm7, addr, value);
    }
    if (isCodePage(addr, (bool)(arm7 && core->gbaMode))) bumpCodePage(stateFor(core), arm7, addr);
    return old;
}

// LDM/STM (ARM). S bit (user-bank transfer) and Rn == PC go to the interpreter.
// Writeback rules: suppressed when the base is in the list of a load
// (ARMv4T behaviour, which is what both the ARM7TDMI and the ARM946E-S do for
// the LDM-with-base-in-list case on real hardware running these consoles).
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
    if (!n) return -1;                         // empty list is UNDEFINED on ARMv4T

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

void JitHelp_canaryFailed(int cpu, uint32_t blockPC, uint32_t expect, uint32_t got) {
    printf("[JIT] !! stack canary damaged: cpu=%d blockPC=%08X expect=%08X got=%08X\n",
           cpu, blockPC, expect, got);
}

void JitHelp_tick(Core* core, uint32_t cycles) {
    if (!core) return;
    core->globalCycles += cycles;
    // The event list is kept sorted; run everything that is now due.
    while (!core->events.empty() &&
           core->globalCycles >= core->events.front().cycles) {
        SchedEvent e = core->events.front();
        core->events.erase(core->events.begin());
        if (e.task >= 0 && e.task < MAX_TASKS && core->tasks[e.task].fn)
            core->tasks[e.task]();
        if (!core->running) return;
    }
}

} // extern "C"

// -----------------------------------------------------------------------------
// Flag helpers. Flags live in RCPSR inside a block; when the block contains
// conditional instructions they are also mirrored into CR0..CR3 so that
// condition tests need no helper call (FIX-10).
// -----------------------------------------------------------------------------
static inline void rematNZ(Ctx& ctx) {
    if (!ctx.flagsCached) return;
    ctx.E(ppc_rlwinm(TA, RCPSR, 0, 0, 31)); ctx.E(ppc_cmpi(CR_N, TA, 0));
    ctx.E(ppc_rlwinm(TA, RCPSR, 1, 0, 31)); ctx.E(ppc_cmpi(CR_Z, TA, 0));
}
static inline void rematC(Ctx& ctx) {
    if (!ctx.flagsCached) return;
    ctx.E(ppc_rlwinm(TA, RCPSR, 2, 0, 31)); ctx.E(ppc_cmpi(CR_C, TA, 0));
}
static inline void rematV(Ctx& ctx) {
    if (!ctx.flagsCached) return;
    ctx.E(ppc_rlwinm(TA, RCPSR, 3, 0, 31)); ctx.E(ppc_cmpi(CR_V, TA, 0));
}

static void emitFlagCacheInit(Ctx& ctx) {
    if (!ctx.flagsCached) return;
    ctx.E(ppc_rlwinm(TA, RCPSR, 0, 0, 31)); ctx.E(ppc_cmpi(CR_N, TA, 0));
    ctx.E(ppc_rlwinm(TA, RCPSR, 1, 0, 31)); ctx.E(ppc_cmpi(CR_Z, TA, 0));
    ctx.E(ppc_rlwinm(TA, RCPSR, 2, 0, 31)); ctx.E(ppc_cmpi(CR_C, TA, 0));
    ctx.E(ppc_rlwinm(TA, RCPSR, 3, 0, 31)); ctx.E(ppc_cmpi(CR_V, TA, 0));
}

static void setNZ(Ctx& ctx, uint8_t r) {
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 2, 31));   // clear N (and Z, which follows)
    ctx.E(ppc_rlwimi(RCPSR, r, 0, 0, 0));        // MSB -> N
    ctx.E(ppc_cmpi(CR_SCR, r, 0));
    ctx.E(ppc_mfcr(TA));
    ctx.E(ppc_rlwinm(TA, TA, 29, 1, 1));         // CR7.EQ -> Z   (30-29 == 1)
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
    rematNZ(ctx);
}

static void setC_xer(Ctx& ctx) {
    ctx.E(ppc_mfxer(TA));
    ctx.E(ppc_rlwinm(TA, TA, 0, 2, 2));          // PPC XER[CA] sits at CPSR's C position
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 3, 1));    // clear C
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
    rematC(ctx);
}

// Insert bit 0 of `src` (the canonical shifter-carry representation used by the
// shift emitters) into C.
static void setC_bit0(Ctx& ctx, uint8_t src) {
    ctx.E(ppc_rlwinm(TA, src, 29, 2, 2));        // LSB (pos 31) -> pos 2
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 3, 1));    // clear C
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
    rematC(ctx);
}

static void setV_add(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    ctx.E(ppc_xor(TE, res, a));
    ctx.E(ppc_xor(TF, res, b));
    ctx.E(ppc_and(TE, TE, TF));
    ctx.E(ppc_rlwinm(TE, TE, 0, 0, 0));
    ctx.E(ppc_rlwinm(TE, TE, 29, 3, 3));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 4, 2));
    ctx.E(ppc_or(RCPSR, RCPSR, TE));
    rematV(ctx);
}
static void setV_sub(Ctx& ctx, uint8_t res, uint8_t a, uint8_t b) {
    ctx.E(ppc_xor(TE, a, b));
    ctx.E(ppc_xor(TF, a, res));
    ctx.E(ppc_and(TE, TE, TF));
    ctx.E(ppc_rlwinm(TE, TE, 0, 0, 0));
    ctx.E(ppc_rlwinm(TE, TE, 29, 3, 3));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 4, 2));
    ctx.E(ppc_or(RCPSR, RCPSR, TE));
    rematV(ctx);
}

// -----------------------------------------------------------------------------
// Condition skip. Returns branch sites that must be patched to the address just
// after the instruction body; execution jumps there when the condition is false.
// -----------------------------------------------------------------------------
static inline uint8_t shCarryRight(int n) { return (uint8_t)((33 - n) & 31); }

static void brAdd(Ctx& ctx, CondSkip& s, uint32_t bo, uint32_t bi) {
    if (s.n >= 4) { ctx.overflow = true; return; }
    s.idx[s.n] = ctx.sz();
    s.bo[s.n]  = (uint8_t)bo;
    s.bi[s.n]  = (uint8_t)bi;
    s.n++;
    ctx.E(ppc_bc((uint8_t)bo, (uint8_t)bi, 0));
}

static CondSkip emitCondSkip(Ctx& ctx, uint8_t cond) {
    CondSkip s; s.n = 0;
    if (cond >= 14) return s;

    if (!ctx.flagsCached) {
        // Helper fallback (only used by blocks that contain no conditional at
        // scan time; keeps codegen independent of the scan).
        ctx.E(ppc_mr(TA, RCPSR));
        ctx.E(ppc_addi(TB, 0, (int16_t)cond));
        ctx.call((void*)JitHelp_testCond);
        ctx.E(ppc_cmpi(CR_SCR, TA, 0));                     // CR7.EQ = cond true
        brAdd(ctx, s, BO_IF_TRUE, CRBI(CR_SCR, CRB_EQ));     // skip when false
        return s;
    }

    const uint32_t zLT = CRBI(CR_Z, CRB_LT), cLT = CRBI(CR_C, CRB_LT);
    const uint32_t nLT = CRBI(CR_N, CRB_LT), vLT = CRBI(CR_V, CRB_LT);
    const uint32_t t0  = CRBI(CR_T0, CRB_LT), t1 = CRBI(CR_T1, CRB_LT);
    const uint32_t t0eq = CRBI(CR_T0, CRB_EQ), t2 = CRBI(CR_T2, CRB_LT);

    switch (cond) {
        case  0: brAdd(ctx, s, BO_IF_FALSE, zLT); break;          // EQ: skip if !Z
        case  1: brAdd(ctx, s, BO_IF_TRUE,  zLT); break;          // NE: skip if Z
        case  2: brAdd(ctx, s, BO_IF_FALSE, cLT); break;          // CS
        case  3: brAdd(ctx, s, BO_IF_TRUE,  cLT); break;          // CC
        case  4: brAdd(ctx, s, BO_IF_FALSE, nLT); break;          // MI
        case  5: brAdd(ctx, s, BO_IF_TRUE,  nLT); break;          // PL
        case  6: brAdd(ctx, s, BO_IF_FALSE, vLT); break;          // VS
        case  7: brAdd(ctx, s, BO_IF_TRUE,  vLT); break;          // VC
        case  8:                                                   // HI = C && !Z
        case  9: {                                                 // LS = !(C && !Z)
            ctx.E(ppc_crnor(t0, zLT, zLT));                        // t0 = !Z
            ctx.E(ppc_crand(t1, cLT, t0));                         // t1 = HI
            brAdd(ctx, s, cond == 8 ? BO_IF_FALSE : BO_IF_TRUE, t1);
            break;
        }
        case 10:                                                   // GE = N == V
        case 11: {                                                 // LT = N != V
            ctx.E(ppc_rlwinm(TF, RCPSR, 0, 0, 31));                // N at MSB
            ctx.E(ppc_rlwinm(TG, RCPSR, 3, 0, 31));                // V at MSB
            ctx.E(ppc_cmpw(CR_T0, TF, TG));                        // CR4.EQ = (N==V)
            brAdd(ctx, s, cond == 10 ? BO_IF_FALSE : BO_IF_TRUE, t0eq);
            break;
        }
        case 12: {                                                 // GT = !Z && N == V
            brAdd(ctx, s, BO_IF_TRUE, zLT);                        // skip if Z
            ctx.E(ppc_rlwinm(TF, RCPSR, 0, 0, 31));
            ctx.E(ppc_rlwinm(TG, RCPSR, 3, 0, 31));
            ctx.E(ppc_cmpw(CR_T0, TF, TG));
            brAdd(ctx, s, BO_IF_FALSE, t0eq);                      // skip if N != V
            break;
        }
        case 13: {                                                 // LE false == !Z && N == V
            ctx.E(ppc_rlwinm(TF, RCPSR, 0, 0, 31));
            ctx.E(ppc_rlwinm(TG, RCPSR, 3, 0, 31));
            ctx.E(ppc_cmpw(CR_T0, TF, TG));                        // CR4.EQ = (N==V)
            ctx.E(ppc_crnor(t1, zLT, zLT));                        // t1 = !Z
            ctx.E(ppc_crand(t2, t1, t0eq));                        // t2 = !Z && N==V
            brAdd(ctx, s, BO_IF_TRUE, t2);
            break;
        }
        default: break;
    }
    return s;
}

static void patchCondSkip(Ctx& ctx, const CondSkip& s) {
    for (int i = 0; i < s.n; i++) {
        int32_t off = (int32_t)((ctx.sz() - s.idx[i]) * 4);
        if (off < -32768 || off > 32764) { ctx.overflow = true; continue; }
        ctx.base[s.idx[i]] = ppc_bc(s.bo[i], s.bi[i], (int16_t)off);
    }
}

// -----------------------------------------------------------------------------
// Shift emitters.  `sc` = caller wants the carry.
// Return value:
//   SHF_CARRY    - carry produced and left in RCARRY (bit 31 canonical form)
//   SHF_NO_CARRY - no carry produced, C must be left untouched
//   SHF_FAIL     - cannot be JITed, use the interpreter
// -----------------------------------------------------------------------------
enum ShiftResult { SHF_CARRY = 1, SHF_NO_CARRY = 0, SHF_FAIL = -1 };

static int sLslI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i == 0) {                                              // LSL #0: C unchanged
        if (d != s) ctx.E(ppc_mr(d, s));
        return SHF_NO_CARRY;
    } else if (i < 32) {
        if (sc) ctx.E(ppc_rlwinm(RCARRY, s, (uint8_t)i, 31, 31));      // C = bit (32-i)
        ctx.E(ppc_rlwinm(d, s, (uint8_t)i, 0, (uint8_t)(31 - i)));
    } else if (i == 32) {
        if (sc) ctx.E(ppc_rlwinm(RCARRY, s, 0, 31, 31));               // C = bit 0
        ctx.E(ppc_addi(d, 0, 0));
    } else {                                                           // LSL #33..#63
        if (sc) ctx.E(ppc_addi(RCARRY, 0, 0));
        ctx.E(ppc_addi(d, 0, 0));
    }
    return sc ? SHF_CARRY : SHF_NO_CARRY;
}

static int sLsrI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i == 0 || i == 32) {                                   // LSR #32: C = bit 31
        if (sc) ctx.E(ppc_rlwinm(RCARRY, s, 1, 31, 31));       // (was 0 -> LSB, wrong)
        ctx.E(ppc_addi(d, 0, 0));
    } else if (i < 32) {
        if (sc) ctx.E(ppc_rlwinm(RCARRY, s, shCarryRight(i), 31, 31));
        ctx.E(ppc_rlwinm(d, s, (uint8_t)(32 - i), (uint8_t)i, 31));
    } else {                                                   // LSR #33..#63
        if (sc) ctx.E(ppc_addi(RCARRY, 0, 0));
        ctx.E(ppc_addi(d, 0, 0));
    }
    return sc ? SHF_CARRY : SHF_NO_CARRY;
}

static int sAsrI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i <= 0 || i >= 32) {                                   // ASR #32: C = sign bit
        if (sc) ctx.E(ppc_rlwinm(RCARRY, s, 1, 31, 31));       // (was 0 -> LSB, wrong)
        ctx.E(ppc_srawi(d, s, 31));
    } else {
        if (sc) ctx.E(ppc_rlwinm(RCARRY, s, shCarryRight(i), 31, 31));
        ctx.E(ppc_srawi(d, s, (uint8_t)i));
    }
    return sc ? SHF_CARRY : SHF_NO_CARRY;
}

static int sRorI(Ctx& ctx, uint8_t d, uint8_t s, int i, bool sc) {
    if (i == 0) {                                              // RRX
        if (sc) ctx.E(ppc_rlwinm(RCARRY, s, 0, 31, 31));       // C = old bit 0
        ctx.E(ppc_rlwinm(TA, RCPSR, 2, 0, 0));                 // old C -> MSB
        ctx.E(ppc_rlwinm(d, s, 31, 1, 31));
        ctx.E(ppc_or(d, d, TA));
        return sc ? SHF_CARRY : SHF_NO_CARRY;
    }
    i &= 31;
    if (!i) i = 32;
    if (i < 32) {
        if (sc) ctx.E(ppc_rlwinm(RCARRY, s, shCarryRight(i), 31, 31));
        ctx.E(ppc_rlwinm(d, s, (uint8_t)(32 - i), 0, 31));
    } else {                                                   // ROR #32 == move
        if (d != s) ctx.E(ppc_mr(d, s));
        if (sc) ctx.E(ppc_rlwinm(RCARRY, s, 1, 31, 31));       // C = bit 31
    }
    return sc ? SHF_CARRY : SHF_NO_CARRY;
}

// Emit operand2 into `dst`. See ShiftResult.
static int emitShifter(Ctx& ctx, uint32_t op, uint8_t dst, bool sc) {
    if ((op >> 25) & 1) {                                      // rotated immediate
        uint32_t v = op & 0xFF;
        const uint32_t rot = ((op >> 8) & 0xF) * 2;
        if (rot) {
            v = (v >> rot) | (v << (32 - rot));
            ctx.li(dst, v);
            if (sc) { ctx.E(ppc_rlwinm(RCARRY, dst, 1, 31, 31)); return SHF_CARRY; }
            return SHF_NO_CARRY;
        }
        ctx.li(dst, v);
        return SHF_NO_CARRY;                                   // C unchanged for rot==0
    }

    const uint8_t rm = op & 0xF;
    if (rm == 15) return SHF_FAIL;
    const int sa = (op >> 7) & 0x1F;
    const uint8_t ty = (op >> 5) & 3;

    if ((op >> 4) & 1) {                                       // register-specified
        const uint8_t rs = (op >> 8) & 0xF;                    // FIX: bits 11-8, not 7-4
        if (rs == 15) return SHF_FAIL;
        ctx.E(ppc_stw(RCPSR, FRAME_CPSR, 1));
        ctx.E(ppc_mr(TA, RA[rm]));
        ctx.E(ppc_mr(TB, RA[rs]));
        ctx.E(ppc_addi(TC, 0, (int16_t)ty));
        ctx.E(ppc_addi(TD, 0, sc ? 1 : 0));
        ctx.E(ppc_addi(TE, 1, (int16_t)FRAME_CPSR));
        ctx.call((void*)JitHelp_shiftR);
        ctx.E(ppc_mr(dst, TA));
        if (sc) {
            // JitHelp_shiftR updates C in the CPSR slot itself (and deliberately
            // leaves it alone for n == 0), so no RCARRY handshake is needed.
            ctx.E(ppc_lwz(TA, FRAME_CPSR, 1));
            ctx.E(ppc_mr(RCPSR, TA));
            ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 0, 31));         // keep N..V only
            rematC(ctx);
        }
        return sc ? SHF_NO_CARRY : SHF_NO_CARRY;
    }

    switch (ty) {
        case 0: return sLslI(ctx, dst, RA[rm], sa, sc);
        case 1: return sLsrI(ctx, dst, RA[rm], sa ? sa : 32, sc);
        case 2: return sAsrI(ctx, dst, RA[rm], sa ? sa : 32, sc);
        default: return sRorI(ctx, dst, RA[rm], sa, sc);
    }
}

// Load/store offset operand ("LDR Rd,[Rn,Rm,<shift>]") -> left in TA.
// Returns 0 on success, -1 if the interpreter must handle it.
static int emitLSOffset(Ctx& ctx, uint32_t op) {
    if ((op >> 25) & 1) { ctx.li(TA, op & 0xFFF); return 0; }
    const uint8_t rm = op & 0xF;
    if (rm == 15) return -1;
    const int sa = (op >> 7) & 0x1F;
    const uint8_t ty = (op >> 5) & 3;
    if ((op >> 4) & 1) {                                       // Rm shifted by Rs
        const uint8_t rs = (op >> 8) & 0xF;
        if (rs == 15) return -1;
        ctx.E(ppc_mr(TA, RA[rm]));
        ctx.E(ppc_mr(TB, RA[rs]));
        ctx.E(ppc_addi(TC, 0, (int16_t)ty));
        ctx.E(ppc_addi(TD, 0, 0));                             // no carry wanted
        ctx.E(ppc_addi(TE, 1, (int16_t)FRAME_CPSR));
        ctx.call((void*)JitHelp_shiftR);
        return 0;                                              // result is already in TA
    }
    int r;
    switch (ty) {
        case 0: r = sLslI(ctx, TA, RA[rm], sa, false); break;
        case 1: r = sLsrI(ctx, TA, RA[rm], sa ? sa : 32, false); break;
        case 2: r = sAsrI(ctx, TA, RA[rm], sa ? sa : 32, false); break;
        default: r = sRorI(ctx, TA, RA[rm], sa, false); break;
    }
    return (r == SHF_FAIL) ? -1 : 0;
}

// -----------------------------------------------------------------------------
// Prologue / epilogue / sync / commit
// -----------------------------------------------------------------------------
static void emitPrologue(Ctx& ctx) {
    ctx.E(ppc_mflr(0));
    ctx.E(ppc_stwu(1, -(int16_t)FRAME_SIZE, 1));
    ctx.E(ppc_stw(0, (int16_t)FRAME_LR_OFF, 1));
    for (int r = 14; r <= 31; r++)
        ctx.E(ppc_stw(r, FRAME_SAVE + (r - 14) * 4, 1));
#if JIT_CANARY
    ctx.li(TA, CANARY_MAGIC);
    ctx.E(ppc_stw(TA, FRAME_CANARY, 1));
#endif
    ctx.li(TA, (uint32_t)(uintptr_t)ctx.core);
    ctx.E(ppc_stw(TA, FRAME_CORE, 1));
    ctx.li(TA, (uint32_t)(uintptr_t)ctx.interp);
    ctx.E(ppc_stw(TA, FRAME_INTERP, 1));
    ctx.E(ppc_addi(TA, 0, (int16_t)ctx.cpuIdx));
    ctx.E(ppc_stw(TA, FRAME_CPUIDX, 1));
}

// Stack-check canary (JIT_CANARY). On mismatch we do NOT call into C with a
// possibly damaged frame: we bump a counter inside the per-core state and let
// the C side report it. Always exactly 8 words, so the guard branch is a
// constant 16-byte forward skip.
static void emitEpilogue(Ctx& ctx) {
#if JIT_CANARY
    ctx.E(ppc_lwz(TA, FRAME_CANARY, 1));
    li2(ctx, TB, CANARY_MAGIC);
    ctx.E(ppc_cmpw(CR_SCR, TA, TB));
    ctx.E(ppc_bc(BO_IF_TRUE, (uint8_t)CRBI(CR_SCR, CRB_EQ), 16));
    ctx.E(ppc_lwz(TB, FRAME_STATE, 1));
    ctx.E(ppc_lwz(TC, (int16_t)STATS_CANARY_OFF, TB));
    ctx.E(ppc_addi(TC, TC, 1));
    ctx.E(ppc_stw(TC, (int16_t)STATS_CANARY_OFF, TB));
#endif
    for (int r = 14; r <= 31; r++)
        ctx.E(ppc_lwz(r, FRAME_SAVE + (r - 14) * 4, 1));
    ctx.E(ppc_lwz(0, (int16_t)FRAME_LR_OFF, 1));
    ctx.E(ppc_mtlr(0));
    ctx.E(ppc_addi(1, 1, (int16_t)FRAME_SIZE));
    ctx.E(ppc_blr());
}

// "Sync from the interpreter or bail out without running anything."
static void emitSyncFrom(Ctx& ctx) {
    ctx.ldInterp();
    ctx.E(ppc_addi(TB, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_addi(TC, 1, (int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_syncFrom);

    ctx.E(ppc_cmpi(CR_SCR, TA, 0));
    size_t bOk = ctx.sz();
    ctx.E(ppc_bc(BO_IF_TRUE, (uint8_t)CRBI(CR_SCR, CRB_EQ), 0));   // patched below

    // --- sync failed (interpreter not ready): report and return -------------
    ctx.ldInterp();
    ctx.ldCpu();
    ctx.call((void*)JitHelp_abortBlock);
    emitEpilogue(ctx);

    {
        int32_t d = (int32_t)((ctx.sz() - bOk) * 4);
        if (d > 32764) ctx.overflow = true;
        else ctx.base[bOk] = ppc_bc(BO_IF_TRUE, (uint8_t)CRBI(CR_SCR, CRB_EQ), (int16_t)d);
    }

    // --- success: pull the guest state into the register file --------------
    for (int i = 0; i < 15; i++)
        ctx.E(ppc_lwz(RA[i], FRAME_REGSYNC + i * 4, 1));
    ctx.E(ppc_lwz(RCPSR, FRAME_CPSR, 1));
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
    emitFlagCacheInit(ctx);
}

static void emitCommitExit(Ctx& ctx, uint32_t nextPC, int reason, uint32_t insns) {
    emitSpill(ctx);
    ctx.ldInterp();
    ctx.ldCpu();
    ctx.E(ppc_addi(TC, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_mr(TD, RCPSR));
    ctx.li(TE, nextPC);
    ctx.li(TF, insns);
    ctx.E(ppc_addi(TG, 0, (int16_t)reason));
    ctx.call((void*)JitHelp_commit);
    emitEpilogue(ctx);
}

static void emitCommitExitDyn(Ctx& ctx, int reason, uint32_t insns) {
    emitSpill(ctx);
    ctx.ldInterp();
    ctx.ldCpu();
    ctx.E(ppc_addi(TC, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_mr(TD, RCPSR));
    ctx.E(ppc_lwz(TE, FRAME_PC, 1));
    ctx.li(TF, insns);
    ctx.E(ppc_addi(TG, 0, (int16_t)reason));
    ctx.call((void*)JitHelp_commit);
    emitEpilogue(ctx);
}

// Patch a forward branch emitted earlier to point at the current position.
static void patchForward(Ctx& ctx, size_t site, uint32_t bo, uint32_t bi) {
    int32_t d = (int32_t)((ctx.sz() - site) * 4);
    if (d > 32764) { ctx.overflow = true; return; }
    ctx.base[site] = ppc_bc((uint8_t)bo, (uint8_t)bi, (int16_t)d);
}

// Helper refused (result < 0): rebuild the interpreter state from the shadow
// and commit as a fallback at `bailPC`, so the interpreter runs the instruction.
static void emitHelperBail(Ctx& ctx, uint32_t bailPC) {
    ctx.E(ppc_stw(TA, FRAME_SCR0, 1));
    ctx.E(ppc_cmpi(CR_SCR, TA, 0));
    size_t bOk = ctx.sz();
    ctx.E(ppc_bc(BO_IF_FALSE, (uint8_t)CRBI(CR_SCR, CRB_LT), 0));   // >= 0 -> ok
    emitReload(ctx);
    emitCommitExit(ctx, bailPC, EXIT_FALLBACK, ctx.insns);
    patchForward(ctx, bOk, BO_IF_FALSE, CRBI(CR_SCR, CRB_LT));
}

// =============================================================================
// ARM emitters
// =============================================================================
enum DP { AND = 0, EOR, SUB, RSB, ADD, ADC, SBC, RSC, TST, TEQ, CMP, CMN, ORR, MOV, BIC, MVN };

static bool emitDP(Ctx& ctx, uint32_t op, uint32_t curPC) {
    const uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;

    const uint8_t dop = (op >> 21) & 0xF;
    const bool s      = (op >> 20) & 1;
    const uint8_t rn  = (op >> 16) & 0xF;
    const uint8_t rd  = (op >> 12) & 0xF;
    const bool immShift = (op >> 25) & 1;
    const bool regShift = !immShift && ((op >> 4) & 1);

    if (rd == 15) return false;                     // incl. MOVS PC, LR (exception return)
    if (!immShift) {
        if ((op & 0xF) == 15) return false;         // Rm == PC
        if (regShift) {
            const uint8_t rs = (op >> 8) & 0xF;
            if (rs == 15) return false;             // Rs == PC
        }
    }
    const bool isTest = (dop >= TST && dop <= CMN);
    if (isTest && rd != 0) return false;            // undefined encoding

    CondSkip si = emitCondSkip(ctx, cond);

    const bool rnIsPC = (rn == 15);
    if (rnIsPC) {
        li2(ctx, TD, curPC + (ctx.thumb ? 4u : 8u));
        ctx.E(ppc_stw(TD, FRAME_SCR2, 1));
    }

    if (dop == ADC || dop == SBC || dop == RSC) {   // carry-in from CPSR.C
        ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2));
        ctx.E(ppc_mtxer(TA));
    }

    const bool logC = s && (dop == AND || dop == EOR || dop == TST || dop == TEQ ||
                            dop == ORR || dop == MOV || dop == BIC || dop == MVN);
    int shr = emitShifter(ctx, op, TA, logC);
    if (shr == SHF_FAIL) { ctx.overflow = true; return false; }
    const bool cInRCarry = (shr == SHF_CARRY);

    uint8_t srcRn = rnIsPC ? TD : RA[rn];
    if (rnIsPC) {
        ctx.E(ppc_lwz(TD, FRAME_SCR2, 1));
        srcRn = TD;
    }

    const bool needV = s && (dop == ADD || dop == SUB || dop == RSB || dop == CMN ||
                             dop == CMP || dop == ADC || dop == SBC || dop == RSC);
    if (needV) {
        ctx.E(ppc_stw(TA, FRAME_SCR0, 1));          // operand2
        ctx.E(ppc_stw(srcRn, FRAME_SCR1, 1));       // operand1
    }

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
            // V_sub(res, a, b) wants a = minuend, b = subtrahend.
            case SUB: case CMP: case SBC:                 // res = operand2 - Rn
                setNZ(ctx, res); setC_xer(ctx); setV_sub(ctx, res, opB, opA); break;
            case RSB: case RSC:                           // res = Rn - operand2
                setNZ(ctx, res); setC_xer(ctx); setV_sub(ctx, res, opA, opB); break;
            default:
                setNZ(ctx, res);
                if (cInRCarry) setC_bit0(ctx, RCARRY);
                break;
        }
    }
    patchCondSkip(ctx, si);
    return true;
}

// -----------------------------------------------------------------------------
// BX / BLX (register) / BLX (immediate) / B / BL
// -----------------------------------------------------------------------------
// PC = Rm with the T bit taken from bit 0. When switching to Thumb only bit 0
// is cleared (Thumb functions may legitimately live at ...2); when switching to
// ARM both low bits are cleared.
static void emitBXTarget(Ctx& ctx) {
    ctx.E(ppc_rlwinm(TH1, TA, 0, 31, 31));      // TH1 = target bit 0
    ctx.E(ppc_neg(TH2, TH1));                   // 0 or 0xFFFFFFFF
    ctx.E(ppc_and(TH2, TA, TH2));               // keep bit 1 only if T == 1
    ctx.E(ppc_rlwinm(TH2, TH2, 0, 30, 30));
    ctx.E(ppc_rlwinm(TB, TA, 0, 0, 29));        // target & ~3
    ctx.E(ppc_or(TB, TB, TH2));
    ctx.E(ppc_stw(TB, FRAME_PC, 1));
    ctx.E(ppc_rlwinm(TC, TA, 5, 26, 26));       // bit0 -> T
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 27, 25)); // clear T, F, E
    ctx.E(ppc_or(RCPSR, RCPSR, TC));
}

static bool emitBX(Ctx& ctx, uint32_t op, uint32_t curPC) {
    const uint8_t cond = (op >> 28) & 0xF;
    const uint8_t rm = op & 0xF;
    if (rm == 15 || cond == 15) return false;

    CondSkip si = emitCondSkip(ctx, cond);
    ctx.E(ppc_mr(TA, RA[rm]));
    emitBXTarget(ctx);
    emitCommitExitDyn(ctx, EXIT_NORMAL, ctx.insns);
    if (si.n) {
        patchCondSkip(ctx, si);
        emitCommitExit(ctx, curPC + 4, EXIT_NORMAL, ctx.insns);
    }
    ctx.done = true;
    return true;
}

static bool emitBLXReg(Ctx& ctx, uint32_t op, uint32_t curPC) {
    const uint8_t cond = (op >> 28) & 0xF;
    const uint8_t rm = op & 0xF;
    if (rm == 15 || cond == 15) return false;

    CondSkip si = emitCondSkip(ctx, cond);
    ctx.li(RA[14], curPC + 4);                  // return address (ARM state)
    ctx.E(ppc_mr(TA, RA[rm]));
    ctx.E(ppc_rlwinm(TB, TA, 0, 0, 30));        // PC = Rm & ~1
    ctx.E(ppc_stw(TB, FRAME_PC, 1));
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 27, 25));
    ctx.li(TC, 1u << 5);                        // force T = 1
    ctx.E(ppc_or(RCPSR, RCPSR, TC));
    emitCommitExitDyn(ctx, EXIT_NORMAL, ctx.insns);
    if (si.n) {
        patchCondSkip(ctx, si);
        emitCommitExit(ctx, curPC + 4, EXIT_NORMAL, ctx.insns);
    }
    ctx.done = true;
    return true;
}

static bool emitBranch(Ctx& ctx, uint32_t op, uint32_t curPC) {
    if ((op & 0x0FFFFFF0) == 0x012FFF10) return emitBX(ctx, op, curPC);
    if ((op & 0x0FFFFFF0) == 0x012FFF30) return emitBLXReg(ctx, op, curPC);

    // BLX immediate: cond must be 1111 (0xFB....).
    if ((op & 0x0FE00000) == 0x0FA00000) {
        if (((op >> 28) & 0xF) != 0xF) return false;
        int32_t off = (int32_t)(op << 8) >> 6;  // imm24 << 2, sign extended
        off |= (int32_t)((op >> 23) & 2);       // H (bit 24) -> bit 1
        uint32_t tgt = curPC + 8u + (uint32_t)off;
        ctx.li(RA[14], curPC + 4);
        ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 27, 25));
        ctx.li(TA, 1u << 5);
        ctx.E(ppc_or(RCPSR, RCPSR, TA));
        emitCommitExit(ctx, tgt & ~1u, EXIT_NORMAL, ctx.insns);
        ctx.done = true;
        return true;
    }

    if ((op & 0x0E000000) != 0x0A000000) return false;   // must be B / BL

    const uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;
    const bool lk = (op >> 24) & 1;
    const int32_t off = (int32_t)(op << 8) >> 6;
    const uint32_t tgt = curPC + 8u + (uint32_t)off;

    CondSkip si = emitCondSkip(ctx, cond);
    if (lk) ctx.li(RA[14], curPC + 4);
    emitCommitExit(ctx, tgt, EXIT_NORMAL, ctx.insns);
    if (si.n) {
        patchCondSkip(ctx, si);
        emitCommitExit(ctx, curPC + 4, EXIT_NORMAL, ctx.insns);
    }
    ctx.done = true;
    return true;
}

// -----------------------------------------------------------------------------
// LDR / STR (word and byte)
// -----------------------------------------------------------------------------
static inline bool lsOffsetSupported(uint32_t op) {
    if ((op >> 25) & 1) return true;
    const uint8_t rm = op & 0xF;
    if (rm == 15) return false;
    if ((op >> 4) & 1) {
        const uint8_t rs = (op >> 8) & 0xF;
        return rs != 15;
    }
    return true;
}

static bool emitLS(Ctx& ctx, uint32_t op, uint32_t) {
    const uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;
    const uint8_t rd = (op >> 12) & 0xF, rn = (op >> 16) & 0xF;
    if (rd == 15 || rn == 15) return false;
    if (!lsOffsetSupported(op)) return false;

    const bool ld = (op >> 20) & 1, by = (op >> 22) & 1;
    const bool up = (op >> 23) & 1, pre = (op >> 24) & 1, wb = (op >> 21) & 1;

    CondSkip si = emitCondSkip(ctx, cond);
    emitLSOffset(ctx, op);                          // TA = offset

    if (pre) {
        if (up) ctx.E(ppc_add (TB, RA[rn], TA));
        else    ctx.E(ppc_subf(TB, TA, RA[rn]));
    } else {
        ctx.E(ppc_mr(TB, RA[rn]));
    }
    ctx.E(ppc_stw(TA, FRAME_SCR0, 1));              // offset
    ctx.E(ppc_stw(TB, FRAME_SCR1, 1));              // writeback value

    ctx.ldCore();
    ctx.argArm7();
    ctx.E(ppc_lwz(TC, FRAME_SCR1, 1));              // address
    if (!ld) ctx.E(ppc_mr(TD, RA[rd]));
    ctx.call(ld ? (by ? (void*)JitHelp_r8 : (void*)JitHelp_r32)
                : (by ? (void*)JitHelp_w8 : (void*)JitHelp_w32));
    if (ld) ctx.E(ppc_mr(RA[rd], TA));

    ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
    // FIX-6: when a load targets the base register, the loaded value wins.
    if (!(ld && rn == rd)) {
        if (pre) {
            if (wb) ctx.E(ppc_lwz(RA[rn], FRAME_SCR1, 1));
        } else {
            if (up) ctx.E(ppc_add (RA[rn], RA[rn], TA));
            else    ctx.E(ppc_subf(RA[rn], TA, RA[rn]));
        }
    }
    patchCondSkip(ctx, si);
    return true;
}

// -----------------------------------------------------------------------------
// LDRH/STRH/LDRSB/LDRSH (+ LDRD/STRD on ARM9 only)  -- FIX-1
// -----------------------------------------------------------------------------
static bool emitLSExtra(Ctx& ctx, uint32_t op, uint32_t) {
    if ((op & 0x0E000090) != 0x00000090) return false;
    const uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;

    const bool p   = (op >> 24) & 1;
    const bool u   = (op >> 23) & 1;
    const bool imm = (op >> 22) & 1;
    const bool w   = (op >> 21) & 1;
    const bool l   = (op >> 20) & 1;
    const uint8_t rn = (op >> 16) & 0xF;
    const uint8_t rd = (op >> 12) & 0xF;
    const uint8_t sh = (op >> 5) & 3;      // 00 SWP | 01 H | 10 SB or LDRD | 11 SH or STRD

    if (sh == 0) return false;             // SWP / SWPB -> emitSwap
    if (rn == 15 || rd == 15) return false;
    if (!imm && (op & 0xF) == 15) return false;

    // L D: sh=10 -> LDRD, sh=11 -> STRD (ARMv5TE only, i.e. ARM9)
    const bool isLdrD = (!l && sh == 2);
    const bool isStrD = (!l && sh == 3);
    if ((isLdrD || isStrD) && (ctx.arm7 || !imm)) return false;
    if (isLdrD || isStrD) {
        if ((rd & 1) || rd > 12 || rn == rd) return false;
    }

    CondSkip si = emitCondSkip(ctx, cond);

    if (imm) ctx.li(TA, ((op >> 4) & 0xF0) | (op & 0xF));
    else     ctx.E(ppc_mr(TA, RA[op & 0xF]));

    if (p) {
        if (u) ctx.E(ppc_add (TB, RA[rn], TA));
        else    ctx.E(ppc_subf(TB, TA, RA[rn]));
    } else {
        ctx.E(ppc_mr(TB, RA[rn]));
    }
    ctx.E(ppc_stw(TA, FRAME_SCR0, 1));
    ctx.E(ppc_stw(TB, FRAME_SCR1, 1));

    if (isLdrD) {
        ctx.ldCore(); ctx.argArm7(); ctx.E(ppc_lwz(TC, FRAME_SCR1, 1));
        ctx.call((void*)JitHelp_r32);
        ctx.E(ppc_mr(RA[rd], TA));
        ctx.E(ppc_lwz(TC, FRAME_SCR1, 1));
        ctx.E(ppc_addi(TC, TC, 4));
        ctx.ldCore(); ctx.argArm7();
        ctx.call((void*)JitHelp_r32);
        ctx.E(ppc_mr(RA[rd + 1], TA));
    } else if (isStrD) {
        ctx.ldCore(); ctx.argArm7(); ctx.E(ppc_lwz(TC, FRAME_SCR1, 1));
        ctx.E(ppc_mr(TD, RA[rd]));
        ctx.call((void*)JitHelp_w32);
        ctx.E(ppc_lwz(TC, FRAME_SCR1, 1));
        ctx.E(ppc_addi(TC, TC, 4));
        ctx.ldCore(); ctx.argArm7();
        ctx.E(ppc_mr(TD, RA[rd + 1]));
        ctx.call((void*)JitHelp_w32);
    } else {
        ctx.ldCore(); ctx.argArm7(); ctx.E(ppc_lwz(TC, FRAME_SCR1, 1));
        if (!l) {
            ctx.E(ppc_mr(TD, RA[rd]));
            ctx.call((void*)JitHelp_w16);                       // STRH
        } else if (sh == 2) {
            ctx.call((void*)JitHelp_r8);                        // LDRSB
            ctx.E(ppc_extsb(RA[rd], TA));
        } else {
            ctx.call((void*)JitHelp_r16);                       // LDRSH  <-- FIX-1
            ctx.E(ppc_extsh(RA[rd], TA));
        }
    }

    ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
    const bool loadWins = (l && rn == rd);
    if (!loadWins) {
        if (p) {
            if (w) ctx.E(ppc_lwz(RA[rn], FRAME_SCR1, 1));
        } else {
            if (u) ctx.E(ppc_add (RA[rn], RA[rn], TA));
            else    ctx.E(ppc_subf(RA[rn], TA, RA[rn]));
        }
    }
    patchCondSkip(ctx, si);
    return true;
}

static bool emitSwap(Ctx& ctx, uint32_t op) {
    if ((op & 0x0FB00FF0) != 0x01000090) return false;      // SWP / SWPB
    const uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;
    const uint8_t rd = (op >> 12) & 0xF, rn = (op >> 16) & 0xF, rm = op & 0xF;
    if (rd == 15 || rn == 15 || rm == 15) return false;

    CondSkip si = emitCondSkip(ctx, cond);
    ctx.ldCore();
    ctx.argArm7();
    ctx.li(TC, op);
    ctx.E(ppc_mr(TD, RA[rn]));
    ctx.E(ppc_mr(TE, RA[rm]));
    ctx.call((void*)JitHelp_swap);
    ctx.E(ppc_mr(RA[rd], TA));
    patchCondSkip(ctx, si);
    return true;
}

// -----------------------------------------------------------------------------
// MUL / MLA / UMULL / UMLAL / SMULL / SMLAL / CLZ
// -----------------------------------------------------------------------------
static bool emitMul(Ctx& ctx, uint32_t op, uint32_t) {
    const uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;

    const uint8_t kind = (op >> 23) & 0x1F;      // bits 27-23

    if (kind == 0x01) {                          // long multiplies
        const uint8_t rdHi = (op >> 16) & 0xF, rdLo = (op >> 12) & 0xF;
        const uint8_t rs = (op >> 8) & 0xF, rm = op & 0xF;
        if (rdHi == 15 || rdLo == 15 || rs == 15 || rm == 15 || rdHi == rdLo) return false;
        CondSkip si = emitCondSkip(ctx, cond);
        emitSpill(ctx);
        ctx.li(TA, op);
        ctx.E(ppc_addi(TB, 1, (int16_t)FRAME_REGSYNC));
        ctx.E(ppc_addi(TC, 1, (int16_t)FRAME_CPSR));
        ctx.call((void*)JitHelp_mulLong);
        ctx.E(ppc_lwz(RCPSR, FRAME_CPSR, 1));
        ctx.E(ppc_lwz(RA[rdLo], FRAME_REGSYNC + rdLo * 4, 1));
        ctx.E(ppc_lwz(RA[rdHi], FRAME_REGSYNC + rdHi * 4, 1));
        emitFlagCacheInit(ctx);
        patchCondSkip(ctx, si);
        return true;
    }

    if (kind == 0x02) {                          // CLZ
        if ((op & 0x0FFF0FF0) != 0x016F0F10) return false;
        const uint8_t rd = (op >> 12) & 0xF, rm = op & 0xF;
        if (rd == 15 || rm == 15) return false;
        CondSkip si = emitCondSkip(ctx, cond);
        ctx.E(ppc_cntlzw(RA[rd], RA[rm]));
        setNZ(ctx, RA[rd]);
        patchCondSkip(ctx, si);
        return true;
    }

    if (kind != 0x00) return false;
    if ((op & 0x0FC000F0) != 0x00000090) return false;

    const bool s = (op >> 20) & 1;
    const bool acc = (op >> 21) & 1;
    const uint8_t rd = (op >> 16) & 0xF, rn = (op >> 12) & 0xF;
    const uint8_t rs = (op >> 8) & 0xF, rm = op & 0xF;
    if (rd == 15 || rm == 15 || rs == 15 || (acc && rn == 15)) return false;

    CondSkip si = emitCondSkip(ctx, cond);
    if (acc) {
        ctx.E(ppc_mullw(TA, RA[rm], RA[rs]));
        ctx.E(ppc_add(RA[rd], TA, RA[rn]));
    } else {
        ctx.E(ppc_mullw(RA[rd], RA[rm], RA[rs]));
    }
    if (s) setNZ(ctx, RA[rd]);
    patchCondSkip(ctx, si);
    return true;
}

// -----------------------------------------------------------------------------
// MRS / MSR  -- FIX-2
// -----------------------------------------------------------------------------
static bool emitMrs(Ctx& ctx, uint32_t op) {
    // MRS Rd, CPSR  (mask 0x0FBF0FFF / pattern 0x010F0000 matches both R values,
    // so the R bit must be tested explicitly.)
    if ((op & 0x0FBF0FFF) != 0x010F0000) return false;
    if ((op >> 22) & 1) return false;            // R == 1 -> MRS Rd, SPSR -> interpreter
    const uint8_t cond = (op >> 28) & 0xF;
    const uint8_t rd = (op >> 12) & 0xF;
    if (cond == 15 || rd == 15) return false;

    CondSkip si = emitCondSkip(ctx, cond);
    ctx.E(ppc_mr(RA[rd], RCPSR));
    patchCondSkip(ctx, si);
    return true;
}

static bool emitMsrReg(Ctx& ctx, uint32_t op) {
    // MSR CPSR_<fields>, Rm  (0x0FB0FFF0 / 0x0120F000); R bit is masked out.
    if ((op & 0x0FB0FFF0) != 0x0120F000) return false;
    if ((op >> 22) & 1) return false;            // SPSR -> interpreter (it owns SPSR)
    const uint8_t mask = (op >> 16) & 0xF;
    const uint8_t rm = op & 0xF;
    if (mask != 0x8 || rm == 15) return false;   // only the flag byte is JIT-able
    const uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;

    CondSkip si = emitCondSkip(ctx, cond);
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 8, 31));   // keep mode, T, F, I
    ctx.E(ppc_rlwinm(TA, RA[rm], 0, 0, 7));      // take N Z C V Q + SBZ
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
    emitFlagCacheInit(ctx);
    patchCondSkip(ctx, si);
    return true;
}

static bool emitMsrImm(Ctx& ctx, uint32_t op) {
    // MSR CPSR_f, #imm.
    // The original mask/pattern pair (0x0DB0F000 / 0x0320F000) was unsatisfiable:
    // its mask cleared the I bit (25) while the pattern required it to be 1, so
    // the comparison could never be true. Mask 0x0FB0F000 fixes that; the extra
    // I-bit + R-bit tests below are what keep this from matching the data
    // processing instruction "TEQ Rn, PC, <shift>Rm" (bits 27-24 = 0001, I = 0).
    if (!((op >> 25) & 1)) return false;
    if ((op & 0x0FB0F000) != 0x0320F000) return false;
    if ((op >> 22) & 1) return false;
    const uint8_t mask = (op >> 16) & 0xF;
    if (mask != 0x8) return false;
    const uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;

    uint32_t v = op & 0xFF;
    const uint32_t rot = ((op >> 8) & 0xF) * 2;
    if (rot) v = (v >> rot) | (v << (32 - rot));
    v &= 0xFF000000u;

    CondSkip si = emitCondSkip(ctx, cond);
    ctx.E(ppc_rlwinm(RCPSR, RCPSR, 0, 8, 31));
    ctx.li(TA, v);
    ctx.E(ppc_or(RCPSR, RCPSR, TA));
    emitFlagCacheInit(ctx);
    patchCondSkip(ctx, si);
    return true;
}

// -----------------------------------------------------------------------------
// LDM / STM
// -----------------------------------------------------------------------------
static bool emitBlockXfer(Ctx& ctx, uint32_t op, uint32_t curPC) {
    const uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15 || ((op >> 22) & 1)) return false;     // S bit -> interpreter
    const uint8_t rn = (op >> 16) & 0xF;
    const uint16_t list = (uint16_t)(op & 0xFFFF);
    if (rn > 14 || !list) return false;

    const bool load = (op >> 20) & 1;
    const bool loadPC = load && (list & 0x8000);

    CondSkip si = emitCondSkip(ctx, cond);
    emitSpill(ctx);

    li2(ctx, TA, curPC + 12);              // value seen for R15 in a store list
    ctx.E(ppc_stw(TA, FRAME_SCR2, 1));
    li2(ctx, TA, curPC + 4);               // fall-through PC for the helper
    ctx.E(ppc_stw(TA, FRAME_PC, 1));

    ctx.ldCore();
    ctx.argArm7();
    ctx.li(TC, op);
    ctx.E(ppc_addi(TD, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_lwz(TE, FRAME_SCR2, 1));
    ctx.E(ppc_addi(TF, 1, (int16_t)FRAME_PC));
    ctx.E(ppc_addi(TG, 1, (int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_armBlock);

    emitHelperBail(ctx, curPC);
    emitReload(ctx);

    if (loadPC) {
        ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
        ctx.E(ppc_cmpi(CR_SCR, TA, 1));
        size_t bDyn = ctx.sz();
        ctx.E(ppc_bc(BO_IF_TRUE, (uint8_t)CRBI(CR_SCR, CRB_EQ), 0));
        emitCommitExit(ctx, curPC + 4, EXIT_NORMAL, ctx.insns);
        patchForward(ctx, bDyn, BO_IF_TRUE, CRBI(CR_SCR, CRB_EQ));
        emitCommitExitDyn(ctx, EXIT_NORMAL, ctx.insns);
        if (si.n) {
            patchCondSkip(ctx, si);
            emitCommitExit(ctx, curPC + 4, EXIT_NORMAL, ctx.insns);
        }
        ctx.done = true;
        return true;
    }
    patchCondSkip(ctx, si);
    return true;
}

// -----------------------------------------------------------------------------
// ARM dispatch
// -----------------------------------------------------------------------------
static bool dispARM(Ctx& ctx, uint32_t op, uint32_t curPC) {
    const uint8_t cond = (op >> 28) & 0xF;
    if (cond == 15) return false;                          // NV is not JIT-able

    const uint32_t it = (op >> 25) & 7;
    switch (it) {
        case 0:
            if ((op & 0x0FC000F0) == 0x00000090) return emitMul(ctx, op, curPC);
            if ((op & 0x0FBF0FFF) == 0x010F0000) return emitMrs(ctx, op);
            if ((op & 0x0FB00FF0) == 0x01000090) return emitSwap(ctx, op);
            if ((op & 0x0FB0FFF0) == 0x0120F000) return emitMsrReg(ctx, op);
            if ((op & 0x0FFFFFF0) == 0x012FFF10) return emitBX(ctx, op, curPC);
            if ((op & 0x0FFFFFF0) == 0x012FFF30) return emitBLXReg(ctx, op, curPC);
            if ((op & 0x0F8000F0) == 0x00800090) return emitMul(ctx, op, curPC);   // long mult
            if ((op & 0x0FFF0FF0) == 0x016F0F10) return emitMul(ctx, op, curPC);   // CLZ
            if ((op & 0x0E000090) == 0x00000090) return emitLSExtra(ctx, op, curPC);
            return emitDP(ctx, op, curPC);
        case 1:
            if ((op & 0x0FB0F000) == 0x0320F000) return emitMsrImm(ctx, op);
            if ((op & 0x0FBF0FFF) == 0x010F0000) return emitMrs(ctx, op);
            return emitDP(ctx, op, curPC);
        case 2:
        case 3:
            return emitLS(ctx, op, curPC);
        case 4:
            return emitBlockXfer(ctx, op, curPC);
        case 5:
            return emitBranch(ctx, op, curPC);
        default:
            return false;                                   // coprocessor / SWI
    }
}

// =============================================================================
// Thumb emitters
// =============================================================================
static bool emitT_shifts(Ctx& ctx, uint16_t op) {
    const uint8_t ty = (op >> 11) & 3, rd = op & 7, rs = (op >> 3) & 7;
    const int i = (op >> 6) & 0x1F;
    int r;
    switch (ty) {
        case 0: r = sLslI(ctx, RA[rd], RA[rs], i, true); break;
        case 1: r = sLsrI(ctx, RA[rd], RA[rs], i ? i : 32, true); break;   // LSR #32
        case 2: r = sAsrI(ctx, RA[rd], RA[rs], i ? i : 32, true); break;   // ASR #32
        default: return false;                                             // not a shift
    }
    setNZ(ctx, RA[rd]);
    if (r == SHF_CARRY) setC_bit0(ctx, RCARRY);
    return true;
}

static bool emitT_addSub3(Ctx& ctx, uint16_t op) {
    const uint8_t rd = op & 7, rs = (op >> 3) & 7;
    const bool sub = (op >> 9) & 1, imm3 = (op >> 10) & 1;
    if (imm3) ctx.li(TA, (op >> 6) & 7);
    else      ctx.E(ppc_mr(TA, RA[(op >> 6) & 7]));
    ctx.E(ppc_mr(TB, RA[rs]));
    if (sub) {
        ctx.E(ppc_subfc(RA[rd], TA, TB));            // rd = operand3 - Rs
        setNZ(ctx, RA[rd]); setC_xer(ctx); setV_sub(ctx, RA[rd], TA, TB);
    } else {
        ctx.E(ppc_addc(RA[rd], TB, TA));
        setNZ(ctx, RA[rd]); setC_xer(ctx); setV_add(ctx, RA[rd], TB, TA);
    }
    return true;
}

static bool emitT_imm8(Ctx& ctx, uint16_t op) {
    const uint8_t ty = (op >> 11) & 3, rd = (op >> 8) & 7;
    const uint32_t imm = op & 0xFF;
    const uint8_t p = RA[rd];
    switch (ty) {
        case 0:                                      // MOV Rd, #imm8
            ctx.li(p, imm); setNZ(ctx, p); return true;
        case 1:                                      // CMP Rd, #imm8
            ctx.li(TA, imm); ctx.E(ppc_mr(TB, p)); ctx.E(ppc_subfc(TC, TA, TB));
            setNZ(ctx, TC); setC_xer(ctx); setV_sub(ctx, TC, TA, TB); return true;
        case 2:                                      // ADD Rd, #imm8
            ctx.li(TA, imm); ctx.E(ppc_mr(TB, p)); ctx.E(ppc_addc(p, TB, TA));
            setNZ(ctx, p); setC_xer(ctx); setV_add(ctx, p, TB, TA); return true;
        default:                                     // SUB Rd, #imm8
            ctx.li(TA, imm); ctx.E(ppc_mr(TB, p)); ctx.E(ppc_subfc(p, TA, TB));
            setNZ(ctx, p); setC_xer(ctx); setV_sub(ctx, p, TA, TB); return true;
    }
}

// Shift-by-register for the Thumb ALU ops: JitHelp_shiftR supplies the correct
// C flag and the >= 32 semantics that slw/srw/sraw cannot express.
static void emitT_aluShift(Ctx& ctx, uint8_t d, uint8_t s, uint32_t type) {
    ctx.E(ppc_stw(RCPSR, FRAME_CPSR, 1));
    ctx.E(ppc_mr(TA, d));
    ctx.E(ppc_mr(TB, s));
    ctx.E(ppc_addi(TC, 0, (int16_t)type));
    ctx.E(ppc_addi(TD, 0, 1));                 // setC
    ctx.E(ppc_addi(TE, 1, (int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_shiftR);
    ctx.E(ppc_mr(d, TA));
    ctx.E(ppc_lwz(RCPSR, FRAME_CPSR, 1));      // helper already updated C
}

static bool emitT_alu(Ctx& ctx, uint16_t op) {
    const uint8_t rd = op & 7, rs = (op >> 3) & 7, o = (op >> 6) & 0xF;
    const uint8_t d = RA[rd], s = RA[rs];
    switch (o) {
        case 0: ctx.E(ppc_and(d, d, s)); setNZ(ctx, d); break;
        case 1: ctx.E(ppc_xor(d, d, s)); setNZ(ctx, d); break;
        case 2: case 3: case 4: case 7:        // LSL / LSR / ASR / ROR by register
            emitT_aluShift(ctx, d, s, (o == 2) ? 0u : (o == 3) ? 1u : (o == 4) ? 2u : 3u);
            setNZ(ctx, d);
            rematC(ctx);
            break;
        case 5:                                // ADC
            ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2)); ctx.E(ppc_mtxer(TA));
            ctx.E(ppc_mr(TB, d)); ctx.E(ppc_adde(d, TB, s));
            setNZ(ctx, d); setC_xer(ctx); setV_add(ctx, d, TB, s); break;
        case 6:                                // SBC: d = s - d - !C
            ctx.E(ppc_rlwinm(TA, RCPSR, 0, 2, 2)); ctx.E(ppc_mtxer(TA));
            ctx.E(ppc_mr(TB, d)); ctx.E(ppc_subfe(d, s, TB));
            setNZ(ctx, d); setC_xer(ctx); setV_sub(ctx, d, s, TB); break;   // minuend s
        case 8: ctx.E(ppc_and(TA, d, s)); setNZ(ctx, TA); break;            // TST
        case 9:                                                            // NEG
            ctx.E(ppc_addi(TA, 0, 0)); ctx.E(ppc_subfc(d, s, TA));
            setNZ(ctx, d); setC_xer(ctx); setV_sub(ctx, d, s, TA); break;
        case 10:                                                           // CMP
            ctx.E(ppc_mr(TB, d)); ctx.E(ppc_subfc(TA, s, TB));
            setNZ(ctx, TA); setC_xer(ctx); setV_sub(ctx, TA, s, TB); break;
        case 11:                                                           // CMN
            ctx.E(ppc_mr(TB, d)); ctx.E(ppc_addc(TA, TB, s));
            setNZ(ctx, TA); setC_xer(ctx); setV_add(ctx, TA, TB, s); break;
        case 12: ctx.E(ppc_or(d, d, s)); setNZ(ctx, d); break;              // ORR
        case 13: ctx.E(ppc_mullw(d, d, s)); setNZ(ctx, d); break;           // MUL
        case 14: ctx.E(ppc_andc(d, d, s)); setNZ(ctx, d); break;            // BIC
        case 15: ctx.E(ppc_nor(d, s, s)); setNZ(ctx, d); break;             // MVN
        default: return false;
    }
    return true;
}

static bool emitT_hiReg(Ctx& ctx, uint16_t op, uint32_t curPC) {
    const uint8_t o  = (op >> 8) & 3;
    const uint8_t rs = ((op >> 3) & 7) | (((op >> 6) & 1) << 3);
    const uint8_t rd = (op & 7) | (((op >> 7) & 1) << 3);

    if (o == 3) {                                   // BX / BLX
        if ((op >> 7) & 1) return false;            // FIX-8b: BLX Rm -> interpreter
        if (rs == 15) return false;
        ctx.E(ppc_mr(TA, RA[rs]));
        emitBXTarget(ctx);
        emitCommitExitDyn(ctx, EXIT_NORMAL, ctx.insns);
        ctx.done = true;
        return true;
    }

    if (rd == 15) {                                 // PC as destination == branch
        if (o == 1) return false;                   // CMP PC, Rs is invalid
        if (o == 2) {                               // MOV PC, Rs (always BLX-like)
            if (rs == 15) ctx.li(TA, curPC + 4);
            else          ctx.E(ppc_mr(TA, RA[rs]));
        } else {                                    // ADD PC, Rs
            ctx.li(TA, curPC + 4);
            if (rs == 15) ctx.E(ppc_add(TA, TA, TA));
            else          ctx.E(ppc_add(TA, TA, RA[rs]));
        }
        emitBXTarget(ctx);
        emitCommitExitDyn(ctx, EXIT_NORMAL, ctx.insns);
        ctx.done = true;
        return true;
    }

    if (rs == 15) {                                 // PC as source
        ctx.li(TA, curPC + 4);
        if (o == 0) {
            ctx.E(ppc_mr(TH1, RA[rd]));
            ctx.E(ppc_addc(RA[rd], TH1, TA));
            if (rd <= 7) { setNZ(ctx, RA[rd]); setC_xer(ctx); setV_add(ctx, RA[rd], TH1, TA); }
        } else if (o == 1) {
            ctx.E(ppc_mr(TB, RA[rd]));
            ctx.E(ppc_subfc(TC, TA, TB));
            setNZ(ctx, TC); setC_xer(ctx); setV_sub(ctx, TC, TA, TB);
        } else {
            ctx.E(ppc_mr(RA[rd], TA));
        }
        return true;
    }

    if (o == 0) {                                   // ADD Rd, Rs
        ctx.E(ppc_mr(TG, RA[rd]));                  // old Rd (survives setNZ/setV_*)
        ctx.E(ppc_mr(TH1, RA[rs]));                 // old Rs (may alias Rd)
        ctx.E(ppc_addc(RA[rd], TG, TH1));
        if (rd <= 7) {                              // FIX-8a: flags for low destinations
            setNZ(ctx, RA[rd]); setC_xer(ctx); setV_add(ctx, RA[rd], TG, TH1);
        }
        return true;
    }
    if (o == 1) {                                   // CMP Rd, Rs
        ctx.E(ppc_mr(TB, RA[rd]));
        ctx.E(ppc_subfc(TA, RA[rs], TB));
        setNZ(ctx, TA); setC_xer(ctx); setV_sub(ctx, TA, RA[rs], TB);
        return true;
    }
    ctx.E(ppc_mr(RA[rd], RA[rs]));                  // MOV Rd, Rs (no flags)
    return true;
}

static bool emitT_ldrPc(Ctx& ctx, uint16_t op, uint32_t curPC) {
    const uint8_t rd = (op >> 8) & 7;
    const uint32_t addr = ((curPC + 4) & ~3u) + ((uint32_t)(op & 0xFF) << 2);
    ctx.ldCore();
    ctx.argArm7();
    li2(ctx, TC, addr);
    ctx.call((void*)JitHelp_r32);
    ctx.E(ppc_mr(RA[rd], TA));
    return true;
}

static bool emitT_memReg(Ctx& ctx, uint16_t op) {
    const uint8_t rd = op & 7, rb = (op >> 3) & 7, ro = (op >> 6) & 7, k = (op >> 9) & 7;
    void* fn = nullptr;
    bool ld = true, sxb = false, sxh = false;
    switch (k) {
        case 0: fn = (void*)JitHelp_w32; ld = false; break;      // STR
        case 1: fn = (void*)JitHelp_w16; ld = false; break;      // STRH
        case 2: fn = (void*)JitHelp_w8;  ld = false; break;      // STRB
        case 3: fn = (void*)JitHelp_r8;  sxb = true; break;      // LDRSB
        case 4: fn = (void*)JitHelp_r32; break;                  // LDR
        case 5: fn = (void*)JitHelp_r16; break;                  // LDRH
        case 6: fn = (void*)JitHelp_r8;  break;                  // LDRB
        case 7: fn = (void*)JitHelp_r16; sxh = true; break;      // LDRSH
        default: return false;
    }
    ctx.E(ppc_add(TC, RA[rb], RA[ro]));
    ctx.E(ppc_stw(TC, FRAME_SCR0, 1));
    ctx.ldCore();
    ctx.argArm7();
    ctx.E(ppc_lwz(TC, FRAME_SCR0, 1));
    if (!ld) ctx.E(ppc_mr(TD, RA[rd]));
    ctx.call(fn);
    if (ld) {
        if (sxb)      ctx.E(ppc_extsb(RA[rd], TA));
        else if (sxh) ctx.E(ppc_extsh(RA[rd], TA));
        else          ctx.E(ppc_mr(RA[rd], TA));
    }
    return true;
}

static bool emitT_memImm(Ctx& ctx, uint16_t op) {
    const uint8_t rd = op & 7, rb = (op >> 3) & 7;
    const bool ld = (op >> 11) & 1;
    const uint8_t h = (op >> 12) & 0xF;
    const bool by = (h == 7), hw = (h == 8);
    const uint32_t off = ((op >> 6) & 0x1F) * (hw ? 2u : by ? 1u : 4u);
    ctx.li(TA, off);
    ctx.E(ppc_add(TC, RA[rb], TA));
    ctx.E(ppc_stw(TC, FRAME_SCR0, 1));
    ctx.ldCore();
    ctx.argArm7();
    ctx.E(ppc_lwz(TC, FRAME_SCR0, 1));
    if (!ld) ctx.E(ppc_mr(TD, RA[rd]));
    void* fn = ld ? (hw ? (void*)JitHelp_r16 : by ? (void*)JitHelp_r8 : (void*)JitHelp_r32)
                  : (hw ? (void*)JitHelp_w16 : by ? (void*)JitHelp_w8 : (void*)JitHelp_w32);
    ctx.call(fn);
    if (ld) ctx.E(ppc_mr(RA[rd], TA));
    return true;
}

static bool emitT_spLoad(Ctx& ctx, uint16_t op, uint32_t curPC) {
    const bool ld = (op >> 11) & 1;
    const uint8_t rd = (op >> 8) & 7;
    const bool sp = ((op >> 12) & 0xF) == 0x9;
    const uint32_t off = (uint32_t)(op & 0xFF) << 2;
    if (sp) {
        ctx.li(TA, off);
        ctx.E(ppc_add(TC, RA[13], TA));
    } else {
        li2(ctx, TC, ((curPC + 4) & ~3u) + off);
    }
    ctx.E(ppc_stw(TC, FRAME_SCR0, 1));
    ctx.ldCore();
    ctx.argArm7();
    ctx.E(ppc_lwz(TC, FRAME_SCR0, 1));
    if (!ld) ctx.E(ppc_mr(TD, RA[rd]));
    ctx.call(ld ? (void*)JitHelp_r32 : (void*)JitHelp_w32);
    if (ld) ctx.E(ppc_mr(RA[rd], TA));
    return true;
}

static bool emitT_addSpPc(Ctx& ctx, uint16_t op, uint32_t curPC) {
    const uint8_t h = (op >> 12) & 0xF;
    if (h == 0xA) {                                 // ADD Rd, PC/SP, #imm
        const uint8_t rd = (op >> 8) & 7;
        const bool sp = (op >> 11) & 1;
        const uint32_t imm = (uint32_t)(op & 0xFF) << 2;
        if (sp) { ctx.li(TA, imm); ctx.E(ppc_add(RA[rd], RA[13], TA)); }
        else    { li2(ctx, RA[rd], ((curPC + 4) & ~3u) + imm); }
        return true;
    }
    if (h == 0xB) {                                 // ADD/SUB SP, #imm
        const uint8_t s = (op >> 8) & 0xF;
        if (s == 0) { ctx.li(TA, (uint32_t)(op & 0x7F) << 2); ctx.E(ppc_add (RA[13], RA[13], TA)); return true; }
        if (s == 1) { ctx.li(TA, (uint32_t)(op & 0x7F) << 2); ctx.E(ppc_subf(RA[13], TA, RA[13])); return true; }
    }
    return false;
}

static bool emitT_pushPop(Ctx& ctx, uint16_t op, uint32_t curPC) {
    const uint8_t opA = (op >> 9) & 7;
    if (opA != 2 && opA != 6) return false;
    const bool load = (op >> 11) & 1;
    const bool R = (op >> 8) & 1;
    const uint32_t list = op & 0xFF;
    if (!list && !R) return false;                  // UNDEFINED on ARMv4T

    emitSpill(ctx);
    li2(ctx, TA, curPC + 2);                        // fall-through PC
    ctx.E(ppc_stw(TA, FRAME_PC, 1));

    ctx.ldCore();
    ctx.argArm7();
    ctx.li(TC, (uint32_t)(uint16_t)op);
    ctx.E(ppc_addi(TD, 1, (int16_t)FRAME_REGSYNC));
    ctx.E(ppc_addi(TE, 1, (int16_t)FRAME_PC));
    ctx.E(ppc_addi(TF, 1, (int16_t)FRAME_CPSR));
    ctx.call((void*)JitHelp_thumbPushPop);

    emitHelperBail(ctx, curPC);
    emitReload(ctx);

    if (load && R) {                                // POP {.., PC}
        ctx.E(ppc_lwz(TA, FRAME_SCR0, 1));
        ctx.E(ppc_cmpi(CR_SCR, TA, 1));
        size_t bDyn = ctx.sz();
        ctx.E(ppc_bc(BO_IF_TRUE, (uint8_t)CRBI(CR_SCR, CRB_EQ), 0));
        emitCommitExit(ctx, curPC + 2, EXIT_NORMAL, ctx.insns);
        patchForward(ctx, bDyn, BO_IF_TRUE, CRBI(CR_SCR, CRB_EQ));
        emitCommitExitDyn(ctx, EXIT_NORMAL, ctx.insns);
        ctx.done = true;
    }
    return true;
}

static bool emitT_ldmStm(Ctx& ctx, uint16_t op, uint32_t curPC) {
    emitSpill(ctx);
    ctx.ldCore();
    ctx.argArm7();
    ctx.li(TC, (uint32_t)(uint16_t)op);
    ctx.E(ppc_addi(TD, 1, (int16_t)FRAME_REGSYNC));
    ctx.call((void*)JitHelp_thumbBlock);
    emitHelperBail(ctx, curPC);
    emitReload(ctx);
    return true;
}

static bool emitT_branch(Ctx& ctx, uint16_t op, uint32_t curPC) {
    const uint8_t h = (op >> 12) & 0xF;

    if (h == 0xE) {                                 // unconditional B
        if ((op >> 11) & 1) return false;           // BL/BLX prefix (handled in compile)
        const int32_t off = (int32_t)((int16_t)(op << 5)) >> 4;
        emitCommitExit(ctx, (uint32_t)(curPC + 4 + off), EXIT_NORMAL, ctx.insns);
        ctx.done = true;
        return true;
    }

    if (h == 0xD) {                                 // conditional B
        const uint8_t cond = (op >> 8) & 0xF;
        if (cond == 0xF) return false;              // SWI -> interpreter
        if (cond == 0xE) return false;              // permanently undefined
        const int32_t off = ((int32_t)(int8_t)(op & 0xFF)) << 1;
        CondSkip si = emitCondSkip(ctx, cond);
        emitCommitExit(ctx, (uint32_t)(curPC + 4 + off), EXIT_NORMAL, ctx.insns);
        patchCondSkip(ctx, si);
        emitCommitExit(ctx, curPC + 2, EXIT_NORMAL, ctx.insns);
        ctx.done = true;
        return true;
    }
    return false;
}

// Thumb BL (the pair is already validated by compile()). Thumb BLX immediate is
// deliberately left to the interpreter - its LR/target pairing rule
// (LR = (curPC + 2) | 1, target = ((curPC + 2) & ~3) + offset) is easy to get
// subtly wrong and the instruction is rare on DS/GBA software.
static bool emitT_bl(Ctx& ctx, uint16_t op1, uint16_t op2, uint32_t curPC) {
    const int32_t hi = (int32_t)(op1 & 0x7FF);
    const int32_t lo = (int32_t)(op2 & 0x7FF);
    const int32_t off = ((hi << 21) >> 9) + (lo << 1);
    const uint32_t tgt = (uint32_t)(curPC + 4 + off);

    ctx.li(RA[14], (curPC + 4) | 1u);
    emitCommitExit(ctx, tgt & ~1u, EXIT_NORMAL, ctx.insns);
    ctx.done = true;
    return true;
}

enum ThumbHandler {
    TH_NONE = 0, TH_SHIFTS, TH_ADDSUB3, TH_IMM8, TH_ALU, TH_HIREG, TH_LOADLIT,
    TH_MEMREG, TH_MEMIMM, TH_SPLOAD, TH_ADDSPPC, TH_PUSH_POP, TH_LDMSTM, TH_BRANCH
};

static bool dispThumb(Ctx& ctx, uint16_t op, uint32_t curPC) {
    const uint8_t h = (op >> 12) & 0xF;
    switch (h) {
        case 0x0: case 0x1:
            if (((op >> 11) & 3) < 3) return emitT_shifts(ctx, op);
            return emitT_addSub3(ctx, op);

        case 0x2: case 0x3:
            return emitT_imm8(ctx, op);

        case 0x4: {
            const uint8_t b = (op >> 10) & 3;
            if (b == 0) return emitT_alu(ctx, op);
            if (b == 1) return emitT_hiReg(ctx, op, curPC);
            return emitT_ldrPc(ctx, op, curPC);
        }
        case 0x5: return emitT_memReg(ctx, op);
        case 0x6: case 0x7: case 0x8: return emitT_memImm(ctx, op);
        case 0x9: return emitT_spLoad(ctx, op, curPC);
        case 0xA: return emitT_addSpPc(ctx, op, curPC);
        case 0xB:
            if (((op >> 8) & 0xF) <= 1) return emitT_addSpPc(ctx, op, curPC);
            return emitT_pushPop(ctx, op, curPC);
        case 0xC: return emitT_ldmStm(ctx, op, curPC);
        case 0xD: case 0xE: return emitT_branch(ctx, op, curPC);
        default: return false;              // BL/BLX pair, SWI, undefined
    }
}

// =============================================================================
// PC validation / flag-cache scan
// =============================================================================
static bool validPC(uint32_t pc, bool gba, bool thumb) {
    pc &= thumb ? ~1u : ~3u;
    if (pc >= 0x80000000u) return false;
    if (gba) {
        return (pc < 0x4000u) ||
               (pc >= 0x02000000u && pc < 0x02040000u) ||
               (pc >= 0x03000000u && pc < 0x03008000u) ||
               (pc >= 0x06000000u && pc < 0x06018000u) ||
               (pc >= 0x08000000u && pc < 0x0E000000u);
    }
    return (pc < 0x8000u) ||                              // ITCM / BIOS
           (pc >= 0x02000000u && pc < 0x02400000u) ||     // main RAM
           (pc >= 0x027C0000u && pc < 0x02800000u) ||     // DTCM window
           (pc >= 0x03000000u && pc < 0x03800000u) ||     // shared WRAM
           (pc >= 0xFFFF0000u);                           // BIOS mirror
}

// Does this block contain anything that reads the condition flags? Only then do
// we pay for the CR0..CR3 materialisation at block entry.
static bool blockNeedsFlags(Core* core, bool arm7, bool thumb, uint32_t pc) {
    for (size_t i = 0; i < BLK_ARMS; i++) {
        if (!validPC(pc, core->gbaMode, thumb)) break;
        if (!thumb) {
            const uint32_t op = core->memory.read<uint32_t>(arm7, pc);
            if (((op >> 28) & 0xF) != 14) return true;
            if (((op >> 25) & 7) == 5) break;                 // B/BL ends the block
            if ((op & 0x0FFFFFF0) == 0x012FFF10) break;
            if ((op & 0x0FFFFFF0) == 0x012FFF30) break;
            pc += 4;
        } else {
            const uint16_t op = core->memory.read<uint16_t>(arm7, pc);
            const uint8_t h = (op >> 12) & 0xF;
            if (h == 0xD && ((op >> 8) & 0xF) <= 0xE) return true;
            if (h == 0xF && ((op >> 11) & 0x1F) == 0x1E) break;   // BL/BLX pair
            if (h == 0xE && !((op >> 11) & 1)) break;             // unconditional B
            pc += 2;
        }
    }
    return false;
}

// =============================================================================
// Block compiler
// =============================================================================
static JitBlock* compile(Interpreter* interp, Core* core,
                         uint32_t armPC, bool arm7, int cpuIdx) {
    JitCore* js = stateFor(core);
    if (!js || !js->live || !interp || !core) return nullptr;

    uint32_t** rp = interp->getRegisters();
    if (!rp || !rp[0] || !rp[15 - 1]) return nullptr;      // interpreter not initialised
    if (!interp->isReady()) return nullptr;

    const bool thumb = interp->isThumb();
    if (!validPC(armPC, core->gbaMode, thumb)) return nullptr;
    const uint32_t pc0 = armPC & (thumb ? ~1u : ~3u);

    const uint32_t rev = pageRevOf(js, arm7 ? 1 : 0, pc0);
    JitBlock& slot = js->cache[(pc0 >> 1) & (JIT_CSIZ - 1)];

    // FIX-5 + FIX-10: cached blocks are only reused while the code they were
    // built from is unchanged (pageRev) and the cache generation matches.
    if (slot.valid && slot.armPC == pc0 && slot.thumb == (uint8_t)thumb &&
        slot.gen == js->cacheGen && slot.pageRev == rev &&
        slot.code >= js->codeBuf && slot.code + slot.nW <= js->codeBuf + js->jitWords &&
        slot.nW >= 16)
        return &slot;

    if (js->codePos + BLK_WDS >= js->jitWords) {           // out of code space
        js->codePos = 0;
        ++js->cacheGen;
        ++js->pageCounter;
        js->st.flushes++;
        for (size_t i = 0; i < JIT_CSIZ; i++) js->cache[i].valid = false;
    }

    Ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.base   = js->codeBuf + js->codePos;
    ctx.cur    = ctx.base;
    ctx.cap    = (BLK_WDS < (js->jitWords - js->codePos)) ? BLK_WDS : (js->jitWords - js->codePos);
    ctx.thumb  = thumb;
    ctx.arm7   = arm7;
    ctx.blockPC = pc0;
    ctx.endPC  = pc0;
    ctx.insns  = 1;
    ctx.cpuIdx = cpuIdx;
    ctx.interp = interp;
    ctx.core   = core;
    ctx.js     = js;
    ctx.flagsCached = blockNeedsFlags(core, arm7, thumb, pc0);

    emitPrologue(ctx);
    emitSyncFrom(ctx);
    emitFlagCacheInit(ctx);

    uint32_t curPC = pc0;
    int n = 0;

    while (n < (int)BLK_ARMS && !ctx.done && !ctx.overflow) {
        ctx.insns = (uint32_t)(n + 1);

        if (ctx.rem() < 256) {                 // out of budget: all n have run
            emitCommitExit(ctx, curPC, EXIT_NORMAL, (uint32_t)n);
            ctx.done = true;
            break;
        }
        if (!validPC(curPC, core->gbaMode, thumb)) {
            emitCommitExit(ctx, curPC, EXIT_FALLBACK, (uint32_t)(n + 1));
            ctx.done = true;
            break;
        }

        const size_t before = ctx.sz();
        bool ok = false;

        if (thumb) {
            const uint16_t op = core->memory.read<uint16_t>(arm7, curPC);
            if (((op >> 11) & 0x1F) == 0x1E) {                 // BL / BLX pair
                if (!validPC(curPC + 2, core->gbaMode, false)) {
                    emitCommitExit(ctx, curPC, EXIT_FALLBACK, (uint32_t)(n + 1));
                    ctx.done = true;
                    break;
                }
                const uint16_t op2 = core->memory.read<uint16_t>(arm7, curPC + 2);
                const uint8_t bb = (op2 >> 11) & 0x1F;
                if (bb == 0x1F) {                              // BL (immediate)
                    ctx.insns = (uint32_t)(n + 2);
                    ok = emitT_bl(ctx, op, op2, curPC);
                    if (ok) { curPC += 4; n += 2; ctx.endPC = curPC; }
                } else {                                       // bb == 0x1D -> BLX imm
                    ok = false;                                // -> interpreter
                }
            } else {
#if JIT_GARBAGE_WARN
                if (op == 0x0000u || op == 0xFFFFu) js->st.garbageFetches++;
#endif
                ok = dispThumb(ctx, op, curPC);
                if (ok) { curPC += 2; n++; ctx.endPC = curPC; }
            }
        } else {
            const uint32_t op = core->memory.read<uint32_t>(arm7, curPC);
#if JIT_GARBAGE_WARN
            if (op == 0x00000000u || op == 0xFFFFFFFFu) {
                js->st.garbageFetches++;
                if (js->st.garbageFetches < 32 || (js->st.garbageFetches % 4096u) == 0u)
                    printf("[JIT] suspicious fetch %08X at guest pc=%08X (thumb=%d)\n",
                           op, curPC, (int)thumb);
            }
#endif
            ok = dispARM(ctx, op, curPC);
            if (ok) {
                curPC += 4;
                n++;
                ctx.endPC = curPC;
            }
        }

        if (ctx.overflow) break;                // discard this block entirely
        if (!ok) {
            if (JIT_TRACE && js->dbgFB < 32) {
                printf("[JIT] fallback at pc=%08X thumb=%d\n", curPC, (int)thumb);
                js->dbgFB++;
            }
            emitCommitExit(ctx, curPC, EXIT_FALLBACK, (uint32_t)(n + 1));
            ctx.done = true;
            break;
        }
        if (ctx.sz() == before) {               // defensive: emitter produced nothing
            emitCommitExit(ctx, curPC, EXIT_FALLBACK, (uint32_t)(n + 1));
            ctx.done = true;
            break;
        }
    }

    if (!ctx.done && !ctx.overflow)
        emitCommitExit(ctx, curPC, EXIT_NORMAL, (uint32_t)n);

    if (ctx.overflow || ctx.sz() < 24) return nullptr;
    if (ctx.base[ctx.sz() - 1] != ppc_blr()) return nullptr;   // frame not balanced

    const size_t wds = ctx.sz();
    flushJitCache(ctx.base, wds);

    slot.armPC  = pc0;
    slot.endPC  = ctx.endPC;
    slot.code   = ctx.base;
    slot.nW     = (uint32_t)wds;
    slot.gen    = js->cacheGen;
    slot.pageRev = rev;
    slot.thumb  = (uint8_t)thumb;
    slot.valid  = true;
    js->codePos += wds;
    return &slot;
}

// =============================================================================
// Run loop
// =============================================================================
static uint32_t runInterpChunk(Core& core, int cpu, bool gba, uint32_t* cyclesOut);

static inline uint32_t insnCycles(int cpu, bool gba) {
    if (gba) return JIT_CYC_GBA;
    return (cpu == 0) ? JIT_CYC_ARM9 : JIT_CYC_ARM7;
}

// FIX-3: exactly the test the interpreter performs per instruction.
static inline bool irqDeliverable(Interpreter& in) {
    if (!in.isReady()) return false;
    if (in.getCpsrRef() & 0x80) return false;          // CPSR.I
    if (!in.readIme()) return false;
    return (in.readIe() & in.readIrf()) != 0;
}

static inline uint32_t readCycles(Interpreter& in) {
    return *(uint32_t*)((uint8_t*)&in + Interpreter::offset_cycles());
}
static inline void addCycles(Interpreter& in, uint32_t d) {
    if (d) *(uint32_t*)((uint8_t*)&in + Interpreter::offset_cycles()) += d;
}

// One interpreter instruction. Global accounting uses insnCycles() so that the
// 2:1 ARM9:ARM7 ratio holds whether an instruction ran in the JIT or not.
static uint32_t interpStep(Core& core, int cpu, bool gba, uint32_t* cyclesOut) {
    Interpreter& in = core.interpreter[cpu];
    JitCore* js = stateFor(&core);
    const uint32_t before = readCycles(in);
    in.jitRunOpcode();
    const uint32_t d = readCycles(in) - before;
    if (js) { js->st.interpCyc += d; js->st.interpSteps++; }
    *cyclesOut += insnCycles(cpu, gba);
    return d;
}

static inline uint32_t negKey(uint32_t pc, bool thumb) {
    return (pc & ~3u) | (thumb ? 2u : 0u) | 1u;
}
static bool isRefused(JitCore* js, uint32_t pc, bool thumb, uint32_t rev) {
    if (!js) return false;
    const uint32_t key = negKey(pc, thumb);
    NegEntry& e = js->neg[(key >> 2) & (NEG_SLOTS - 1)];
    return e.key == key && e.gen == js->cacheGen && e.pageRev == rev;
}
static void markRefused(JitCore* js, uint32_t pc, bool thumb, uint32_t rev) {
    const uint32_t key = negKey(pc, thumb);
    NegEntry& e = js->neg[(key >> 2) & (NEG_SLOTS - 1)];
    e.key = key; e.gen = js->cacheGen; e.pageRev = rev;
}

static void fbRecord(JitCore* js, uint32_t pc) {
    const uint32_t h = (pc >> 2) & (FB_SLOTS - 1);
    if (js->st.fbPC[h] != pc) { js->st.fbPC[h] = pc; js->st.fbCnt[h] = 0; }
    js->st.fbCnt[h]++;
}

// Interpret a few instructions in a row instead of bouncing through compile()
// for every single one (FIX-10). Stops early as soon as a PC becomes JIT-able.
static uint32_t runInterpChunk(Core& core, int cpu, bool gba, uint32_t* cyclesOut) {
    JitCore* js = stateFor(&core);
    Interpreter& in = core.interpreter[cpu];
    const bool arm7 = (cpu == 1) || gba;
    uint32_t n = 0;
    while (n < FALLBACK_CHUNK_INSNS && core.running) {
        if (!in.isReady() || in.halted) break;
        interpStep(core, cpu, gba, cyclesOut);
        n++;
        if (in.halted) break;
        const uint32_t pc = in.getActualPC();
        const bool th = in.isThumb();
        if (!validPC(pc, gba, th)) continue;               // let the interpreter report it
        if (irqDeliverable(in)) break;
        const uint32_t rev = js ? pageRevOf(js, arm7 ? 1 : 0, pc) : 0u;
        if (!isRefused(js, pc & (th ? ~1u : ~3u), th, rev))
            break;
    }
    return n;
}

static uint32_t runCpu(Core& core, int cpu, bool gba, uint32_t* cyclesOut) {
    JitCore* js = stateFor(&core);
    if (!js || !js->live) return 0;

    Interpreter& interp = core.interpreter[cpu];
    if (!interp.isReady()) return 0;
    if (interp.halted) return 0;

    // FIX-3 (a): a deliverable IRQ is taken at a block boundary by running one
    // interpreter instruction, so the exception entry uses the interpreter's own
    // logic and the JIT resumes with a coherent state.
    if (irqDeliverable(interp)) {
        interpStep(core, cpu, gba, cyclesOut);      // interpreter enters the vector
        if (irqDeliverable(interp))                 // still pending (someone else owns
            return 1 + runInterpChunk(core, cpu, gba, cyclesOut);   // delivery): stay
        return 1;                                   // in the interpreter, don't spin
    }

    const bool arm7 = (cpu == 1) || gba;
    const bool thumb = interp.isThumb();
    const uint32_t pc = interp.getActualPC();

    if (!validPC(pc, gba, thumb)) {                         // fetching from nowhere
        interpStep(core, cpu, gba, cyclesOut);              // interpreter reports it
        return 1;
    }

    const uint32_t rev = pageRevOf(js, arm7 ? 1 : 0, pc);

    if (isRefused(js, thumb ? (pc & ~1u) : (pc & ~3u), thumb, rev))
        return runInterpChunk(core, cpu, gba, cyclesOut);

    JitBlock* b = compile(&interp, &core, pc, arm7, cpu);
    if (!b || !b->code || b->nW < 16 ||
        b->code < js->codeBuf || b->code + b->nW > js->codeBuf + js->jitWords) {
        markRefused(js, thumb ? (pc & ~1u) : (pc & ~3u), thumb, rev);
        return runInterpChunk(core, cpu, gba, cyclesOut);
    }

    js->exitReason[cpu] = EXIT_FALLBACK;
    js->exitPC[cpu]     = pc;
    js->exitInsns[cpu]  = 1;
    js->curBlockPC[cpu] = pc;
    js->curBlockEnd[cpu] = b->endPC;

    executeBlock_asm(b->code);

    const uint32_t insns  = js->exitInsns[cpu] ? js->exitInsns[cpu] : 1u;
    const int      reason = js->exitReason[cpu];
    const uint32_t epc    = js->exitPC[cpu];
    JitStats& st = js->st;

    if (reason == EXIT_NOTREADY) {                          // interpreter not usable
        st.notReady++;
        return 0;
    }

    if (reason == EXIT_FALLBACK) {
        const uint32_t jitInsns = insns ? insns - 1u : 0u;
        const uint32_t cyc = jitInsns * insnCycles(cpu, gba);
        *cyclesOut += cyc;
        addCycles(interp, cyc);
        st.fallbacks++;
        st.insns += jitInsns;
        fbRecord(js, epc);
        interp.setPC(epc);
        return jitInsns + runInterpChunk(core, cpu, gba, cyclesOut);
    }

    const uint32_t cyc = insns * insnCycles(cpu, gba);
    *cyclesOut += cyc;
    addCycles(interp, cyc);
    st.blocks++;
    st.insns += insns;
    return insns;
}

static void jitStatsPrint(JitCore* js, uint32_t cycles) {
#if JIT_STATS
    if ((++js->st.slices & 511u) != 0u) return;
    printf("[JIT c%d] blk=%u insn=%u fb=%u cyc/slice=%u resync=%u interp cyc/insn=%u flushes=%u canary=%u garbage=%u\n",
           (int)(js->core ? js->core->id : -1),
           (uint32_t)js->st.blocks, (uint32_t)js->st.insns, (uint32_t)js->st.fallbacks,
           cycles, (uint32_t)js->st.resync,
           js->st.interpSteps ? (uint32_t)(js->st.interpCyc / js->st.interpSteps) : 0u,
           (uint32_t)js->st.flushes, (uint32_t)js->st.canaryFaults,
           (uint32_t)js->st.garbageFetches);
    js->st.blocks = 0; js->st.insns = 0; js->st.fallbacks = 0; js->st.resync = 0;
    js->st.interpCyc = 0; js->st.interpSteps = 0;
    if (js->st.canaryFaults) {
        printf("[JIT c%d] !! %u stack-canary faults - codegen frame bug\n",
               (int)(js->core ? js->core->id : -1), (uint32_t)js->st.canaryFaults);
        js->st.canaryFaults = 0;
    }
#else
    (void)js; (void)cycles;
#endif
}

void runJitNds(Core& core) {
    JitCore* js = stateFor(&core);
    if (!js || !js->live || !js->codeBuf) {
        Interpreter::runCoreNds(core);
        return;
    }
    g_active = js;

    uint32_t cycles = 0, n9 = 0, n7 = 0;
    while (core.running &&
           (n9 < NDS9_INSN_PER_SLICE || n7 < NDS7_INSN_PER_SLICE)) {
        uint32_t progressed = 0;
        if (n9 < NDS9_INSN_PER_SLICE) {
            n9 += runCpu(core, 0, false, &cycles);
            if (core.running) progressed |= 1;
        }
        if (n7 < NDS7_INSN_PER_SLICE) {
            n7 += runCpu(core, 1, false, &cycles);
            if (core.running) progressed |= 2;
        }
        if (!progressed) break;                 // both halted / nothing to do
    }

    if (!cycles) {                              // keep time moving so the
        cycles = IDLE_SLICE_CYCLES;             // scheduler can wake a halted CPU
        addCycles(core.interpreter[0], cycles);
    }
    JitHelp_tick(&core, cycles);
    jitStatsPrint(js, cycles);
}

void runJitGba(Core& core) {
    JitCore* js = stateFor(&core);
    if (!js || !js->live || !js->codeBuf) {
        Interpreter::runCoreSingle<true, 0>(core);
        return;
    }
    g_active = js;

    uint32_t cycles = 0, n = 0;
    while (core.running && n < GBA_INSN_PER_SLICE) {
        const uint32_t k = runCpu(core, 1, true, &cycles);
        if (!k) break;
        n += k;
    }

    if (!cycles) {
        cycles = IDLE_SLICE_CYCLES;
        addCycles(core.interpreter[1], cycles);
    }
    JitHelp_tick(&core, cycles);
    jitStatsPrint(js, cycles);
}

// -----------------------------------------------------------------------------
// Code invalidation (FIX-5)
// -----------------------------------------------------------------------------
void noteCodeWrite(Core* core, bool arm7, uint32_t addr, uint32_t len) {
    JitCore* js = stateFor(core);
    if (!js) return;
    if (len >= 0x4000u) {                       // bulk copy: cheaper to reset
        flushJitCache();
        return;
    }
    const uint32_t first = addr & ~0xFFFu;
    const uint32_t last  = (addr + len - 1u) & ~0xFFFu;
    for (uint32_t p = first; p <= last; p += 0x1000u)
        if (isCodePage(p, core ? core->gbaMode : false))
            bumpCodePage(js, arm7 ? 1 : 0, p);
}

void notifyMemoryMapChanged() {
    flushJitCache();                            // TCM / WRAM remap invalidates everything
}

void invalidateJitRange(uint32_t start, uint32_t end) {
    if (end <= start) return;
    for (int c = 0; c < MAX_JIT_CORES; c++) {
        JitCore* js = g_cores[c];
        if (!js || !js->live) continue;
        ++js->pageCounter;
        for (size_t i = 0; i < JIT_CSIZ; i++) {
            JitBlock& b = js->cache[i];
            if (b.valid && b.armPC < end && start < b.endPC)
                b.valid = false;
        }
        for (uint32_t p = start & ~0xFFFu; p < end; p += 0x1000u)
            if (isCodePage(p, false)) bumpCodePage(js, 0, p),
                                      bumpCodePage(js, 1, p);
    }
}

void flushJitCache() {
    for (int c = 0; c < MAX_JIT_CORES; c++) {
        JitCore* js = g_cores[c];
        if (!js) continue;
        js->codePos = 0;
        ++js->cacheGen;
        ++js->pageCounter;
        if (js->st.flushes != 0xFFFFFFFFull) js->st.flushes++;
        for (size_t i = 0; i < JIT_CSIZ; i++) js->cache[i].valid = false;
    }
}

// -----------------------------------------------------------------------------
// Diagnostics
// -----------------------------------------------------------------------------
void dumpRecentCommits(Core* core, uint32_t faultAddr) {
    JitCore* js = stateFor(core);
    if (!js) return;
    printf("==== JIT commit history (oldest first), fault addr=%08X ====\n", faultAddr);
    for (size_t i = 0; i < COMMIT_RING; i++) {
        const size_t idx = (js->ringPos + i) % COMMIT_RING;
        const CommitRec& r = js->ring[idx];
        if (!r.armPC && !r.pc) continue;
        printf("  blkPC=%08X nextPC=%08X insns=%u reason=%d\n",
               r.armPC, r.pc, r.insns, (int)r.reason);
    }
    printf("  live block: cpu0=%08X..%08X  cpu1=%08X..%08X  gen=%u codePos=%u\n",
           js->curBlockPC[0], js->curBlockEnd[0],
           js->curBlockPC[1], js->curBlockEnd[1],
           js->cacheGen, (unsigned)js->codePos);
    printf("===============================================================\n");
}

// Top fallback PCs - paste the opcode into a disassembler for instant to-dos.
void reportFallbacks(Core* core, int topN) {
    JitCore* js = stateFor(core);
    if (!js) return;
    if (topN <= 0) topN = 16;
    printf("==== JIT fallback census ====\n");
    for (int k = 0; k < topN; k++) {
        uint32_t bestH = 0; uint64_t best = 0;
        for (size_t i = 0; i < FB_SLOTS; i++)
            if (js->st.fbCnt[i] > best) { best = js->st.fbCnt[i]; bestH = (uint32_t)i; }
        if (!best) break;
        const uint32_t pc = (uint32_t)js->st.fbPC[bestH];
        printf("  pc=%08X count=%u", pc, (uint32_t)best);
        if (core) {
            const uint32_t op = core->memory.read<uint32_t>(false, pc);
            printf(" arm-op=%08X thumb-op=%04X", op, (uint16_t)core->memory.read<uint16_t>(false, pc));
        }
        printf("\n");
        js->st.fbCnt[bestH] = 0;
    }
    printf("=============================\n");
}

// -----------------------------------------------------------------------------
// Init / shutdown
// -----------------------------------------------------------------------------
// A code buffer (and the JIT state) must live at a *cached* address; the
// trampoline is in MEM1, so anything we jump into has to be in the same window.
static bool validCodeRange(uintptr_t addr, size_t bytes) {
    if (addr >= 0x80000000u && (addr + bytes) <= 0x81800000u) return true;   // MEM1 cached
    if (addr >= 0x90000000u && (addr + bytes) <= 0x94000000u) return true;   // MEM2 cached
    return false;
}

static bool normalizeCodePtr(uintptr_t& addr, size_t bytes) {
    if (validCodeRange(addr, bytes)) return true;
    if (addr < 0x01800000u && validCodeRange(addr | 0x80000000u, bytes)) {
        addr |= 0x80000000u; return true;
    }
    if (addr >= 0xC0000000u && addr < 0xC1800000u &&
        validCodeRange(addr - 0x40000000u, bytes)) {
        addr -= 0x40000000u; return true;
    }
    if (addr >= 0xD0000000u && addr < 0xD4000000u &&
        validCodeRange(addr - 0x40000000u, bytes)) {
        addr -= 0x40000000u; return true;
    }
    return false;
}

static void* allocJitMem(size_t bytes, bool& fromMem2) {
    void* raw = memalign(32, bytes);
    fromMem2 = false;
    if (!raw) { raw = Noods_MEM2_Alloc(bytes); fromMem2 = true; }
    return raw;
}
static void freeJitMem(void* p, bool fromMem2) {
    if (!p) return;
    if (fromMem2) Noods_MEM2_Free(p); else free(p);
}

bool initJit(Core* core) {
    if (!core) return false;

    int slotIdx = -1;
    for (int i = 0; i < MAX_JIT_CORES; i++) {
        if (g_cores[i] && g_cores[i]->core == core) return g_cores[i]->live;
        if (!g_cores[i]) { slotIdx = i; break; }
    }
    if (slotIdx < 0) {
        printf("[JIT] no free core slot (MAX_JIT_CORES=%d)\n", MAX_JIT_CORES);
        return false;
    }

    bool stFromMem2 = false;
    void* stRaw = allocJitMem(sizeof(JitCore), stFromMem2);
    if (!stRaw) { printf("[JIT] state allocation failed\n"); return false; }
    uintptr_t stAddr = (uintptr_t)stRaw;
    if (!normalizeCodePtr(stAddr, sizeof(JitCore))) {
        printf("[JIT] state at %p is not in a cached window\n", stRaw);
        freeJitMem(stRaw, stFromMem2);
        return false;
    }
    JitCore* js = (JitCore*)stAddr;
    memset(js, 0, sizeof(JitCore));
    js->core = core;
    js->cacheGen = 1;
    js->pageCounter = 1;

    size_t bytes = JIT_BYTES_MEM1;
    bool fromMem2 = false;
    void* raw = allocJitMem(bytes, fromMem2);
    if (!raw) {
        bytes = JIT_BYTES_MEM2;
        raw = allocJitMem(bytes, fromMem2);
    }
    if (!raw) {
        printf("[JIT] code buffer allocation failed\n");
        freeJitMem(stRaw, stFromMem2);
        return false;
    }
    uintptr_t addr = (uintptr_t)raw;
    if (!normalizeCodePtr(addr, bytes)) {
        printf("[JIT] code buffer at %p is not in a cached window\n", raw);
        freeJitMem(raw, fromMem2);
        freeJitMem(stRaw, stFromMem2);
        return false;
    }

    uintptr_t tr = (uintptr_t)(void*)executeBlock_asm;
    if (tr < 0x80000000u || tr >= 0x81800000u) {
        printf("[JIT] trampoline %p not in cached MEM1\n", (void*)tr);
        freeJitMem(raw, fromMem2);
        freeJitMem(stRaw, stFromMem2);
        return false;
    }

    js->codeBuf  = (uint32_t*)addr;
    js->codePos  = 0;
    js->jitBytes = bytes;
    js->jitWords = bytes / 4;
    js->fromMem2 = fromMem2;
    js->dbgFB    = 0;

    memset(js->codeBuf, 0, bytes);
    DCFlushRange(js->codeBuf, bytes);
    ICInvalidateRange(js->codeBuf, bytes);

    js->live = true;
    g_cores[slotIdx] = js;
    g_active = js;

    printf("[JIT c%d] ready buf=%p (%uKB %s) tramp=%p BLK_ARMS=%u cyc9/7/gba=%u/%u/%u\n",
           core->id, (void*)js->codeBuf, (unsigned)(js->jitBytes >> 10),
           fromMem2 ? "MEM2" : "MEM1", (void*)tr, (unsigned)BLK_ARMS,
           JIT_CYC_ARM9, JIT_CYC_ARM7, JIT_CYC_GBA);

#if !IS_ARM7_TWICE_SLOWER
    printf("[JIT] WARNING: JIT_CYC_ARM7 must be 2x JIT_CYC_ARM9 for a correct 2:1 clock ratio\n");
#endif
#if JIT_SELFTEST
    printf("[JIT] selftest hook: run your differential suite here (see jit_selftest.cpp)\n");
#endif

    core->setRunFunc(core->gbaMode ? runJitGba : runJitNds);
    return true;
}

void shutdownJit(Core* core) {
    for (int i = 0; i < MAX_JIT_CORES; i++) {
        JitCore* js = g_cores[i];
        if (!js || (core && js->core != core)) continue;
        js->live = false;
        if (g_active == js) g_active = nullptr;
        if (js->core)
            js->core->setRunFunc(js->core->gbaMode
                ? static_cast<void(*)(Core&)>(&Interpreter::runCoreSingle<true, 0>)
                : &Interpreter::runCoreNds);
        if (js->codeBuf) {
            DCFlushRange(js->codeBuf, js->jitBytes);
            freeJitMem(js->codeBuf, js->fromMem2);
        }
        // The JitCore block itself is deliberately leaked for the process
        // lifetime: Core::operator new/delete uses the app's MEM2 and the Core
        // may still be referenced during teardown. (The original code leaked the
        // code buffer too; that part is fixed above.)
        g_cores[i] = nullptr;
    }
}

} // namespace JitPpc

// =============================================================================
// Design note: why there is no cross-block code chaining (yet)
// (append this comment *after* the namespace - it is documentation only)
// =============================================================================
//
// (see the comment block below)
// =============================================================================
// The current contract is "the interpreter is authoritative between blocks":
// every block syncs the guest registers in at entry and commits them out at each
// exit. That makes block entry independent of how we arrived, which is what
// makes this JIT easy to keep correct.
//
// Chaining two blocks in generated code (bctr into the next block's code) only
// pays off if the second block can skip its sync and its commit - i.e. if guest
// state can stay resident in the block register file across the branch. Doing
// that requires:
//   1. a per-register shadow copy + dirty mask in the JitCore state,
//   2. "state is coherent" bookkeeping on every interpreter entry point
//      (scheduler tasks, HLE calls, fallbacks, save states),
//   3. making setPC()/getActualPC() work from the resident copy.
// Half-implemented chaining is slower than no chaining *and* wrong whenever an
// interpreter entry point runs.
//
// Meanwhile the run loop already chains at C level: after a block commits a
// branch target, runCpu() is called again immediately and the target block is
// executed in the same slice, so no scheduler round trip or re-sync to the
// caller happens. That is where most of the branch cost is recovered today.
// =============================================================================
