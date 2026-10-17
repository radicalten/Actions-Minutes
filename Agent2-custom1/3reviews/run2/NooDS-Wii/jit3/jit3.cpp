/*
    v3-NooDS-Wii: jit3, the driver for the PowerPC recompiler in armjit.cpp.

    The emitter translates one ARM instruction at a time and knows nothing about
    memory, pages or dispatch; this file owns all of that:

      * a 1 MB arena of emitted host code, with a direct-mapped table of
        (pc, page) -> block, so a hot PC is a hash probe and an indirect call;
      * a negative cache, so a PC that has been refused once is not re-scanned
        on every visit (and a generation counter, so a remapped guest page
        throws every block away at once);
      * the counters the per-frame report prints, and the diagnostics the
        harness reads back off the SD card;
      * the startup self-test, which assembles four tiny ARM programs, runs each
        once interpreted and once compiled, and diffs the results.

    What the driver must *not* do is lose cycles or let the guest see a different
    machine than the interpreter does: a block returns its own cycle count in r3
    and the next guest PC in r4, the pipeline is left in the interpreter's own
    convention, and any instruction the emitter cannot translate is handed to
    the interpreter (which is why the emulated result is identical either way -
    see NOTES.md, and `tools/run_ab.sh VERIFY=1` for the differential check).
*/

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <ogc/cache.h>

#include "jit3.h"
#include "armjit.hpp"
#include "../core.h"
#include "../interpreter.h"
#include "../memory.h"

namespace Jit3 {

// ------------------------------------------------------------------ constants

constexpr uint32_t kArenaWords = 1u << 18;        // 1 MB of host code
constexpr uint32_t kTableBits  = 13;
constexpr uint32_t kTableSize  = 1u << kTableBits;
constexpr uint32_t kNegBits    = 13;
constexpr uint32_t kNegSize    = 1u << kNegBits;

// Most host words a single block may use.  The emitter is handed this as its cap
// and the arena keeps this much free before a compile, so a block that needs more
// fails the compile (and is interpreted) instead of running off the end of the
// arena.  Passing the whole remaining arena here once let a long block write into
// whatever follows the arena in memory - the block table.
constexpr uint32_t kMaxBlockWords = 1200;

constexpr uint32_t kPcFromCpu = 0xFFFF0000u;      // exitDelegate(): "PC is in the CPU"

// Where the self-test programs live and how big they may be, and how many
// per-instruction cycle counts the self-test compares.
constexpr uint32_t kScratchPc    = 0x023E0000;    // ARM9 main RAM, top of the 4 MB
constexpr uint32_t kScratchMax   = 64 * 4;        // words reserved for a test program
constexpr uint32_t kJit3MaxSteps = 64;            // per-step cycle trace capacity

namespace {

// One table entry.  `off` is a word offset into the arena; `gen` is the memory
// generation the block was compiled in, so invalidateAll() can retire every
// block by bumping the generation instead of walking the table.
// The first four fields are ChainEntry, in that order: the emitted chain probe
// reads offsets 0 (pc), 4 (gen) and 8 (warm entry) directly, so the layout and the
// probe cannot drift apart.  Keep them first.
struct JitBlockEntry {
    uint32_t pc;              // guest PC of the block's first instruction
    uint32_t gen;             // memory generation it was compiled in
    uint32_t warmPtr;         // host address of the entry point past the prologue
    uint8_t *page;
    uint32_t off;
    uint32_t instrs;
    uint32_t cycles;          // guest cycles the block is statically known to take
    uint32_t delegs;          // of instrs, how many the interpreter executes inline
    uint32_t delegWord;       // the first delegated word, for the entry-weighted census
};

uint32_t arena[kArenaWords] __attribute__((aligned(64)));
uint32_t arenaUsed = 0;
uint32_t generation = 1;
JitBlockEntry table[kTableSize];

struct JitNegEntry { uint32_t pc; uint8_t *page; uint32_t gen; uint32_t cls; };
JitNegEntry neg[kNegSize];

Stats g_stats;
bool g_enabled = true;
bool g_trace = false;
bool g_verify = false;

// Block chaining (see Cfg::chain in armjit.hpp).  On by default: it is the single
// biggest win measured so far - the driver dispatch count drops ~9x on this ROM.
bool g_chain = true;
bool g_chainCount = false;        // count chain hops (costs five words per hop)
int32_t g_chainBudget = 3000;     // guest cycles one chain may cover per dispatch
uint32_t g_chainHits = 0;         // chain hops taken, when counting is on

// One guest instruction per dispatch (0 = the normal limit).  Both the self-test
// and the verifier need it: with multi-instruction blocks the compiled side
// executes a *different* instruction stream - the terminating branch is folded
// into the block that precedes it - so cycles and registers cannot be compared
// one instruction at a time.
uint32_t g_blockLimit = 0;
// The self-test normally runs one guest instruction per dispatch (a trace line per
// instruction is what makes a mismatch attributable).  With several instructions per
// block the same comparison still holds - the runner replays that many interpreter
// steps - and it is the only way the *composition* of Thumb emitters (a conditional
// branch with a continuation, a fused BL, chaining) is checked against the real
// interpreter instead of against the compiler's own idea of it.
uint32_t g_selfTestBlocks = 0;

// Verifier state: how far it got, and how much it has complained.
uint32_t g_verifyBlocks = 0;
uint32_t g_verifyBad = 0;
uint32_t g_verifyLogged = 0;

// Refusal census: which instruction classes the emitter turns away (weighted by
// how often they are dispatched) and the hottest individual PCs.  Counting is a
// couple of memory ops per refusal, so it is always on.
uint32_t g_refuseClass[8];
uint32_t g_delegClass[8];        // delegated words, by the same classes (per compile)
uint32_t g_delegEntry[8];        // ... weighted by how often the block was entered
uint32_t g_hotAddr[8];           // hottest delegated PCs, by entry count
uint32_t g_hotCount[8];
uint32_t g_refusePc[64];
uint32_t g_refuseThumb = 0;      // how many of the refusals were Thumb halfwords
uint32_t g_refuseHits[64];

// The dispatch ABI comes straight from the emitter: a two-word struct is
// returned in r3:r4 by the PowerPC SVR4 calling convention, which is exactly
// where callHelper()/epilogue() leave the cycle count and the next guest PC.
struct BlockResult { uint32_t cycles, exitPc; };
typedef BlockResult (*BlockFn)(Interpreter *, uint32_t);

uint32_t hostPage(void *p) { return (uint32_t)(uintptr_t)p >> 12; }

// A block's identity is (pc, page, generation, CPU mode).  The mode rides in the
// low bit of the generation word so that the emitted chain probe keeps comparing
// exactly one field (it reads `gen` at offset 4) and still cannot jump from an ARM
// block into a Thumb one at the same address.  invalidateAll() bumps the
// generation, which retires both modes at once.
uint32_t modeGen(bool thumb) { return (generation << 1) | (thumb ? 1u : 0u); }

// The slot mix has to be computable from the guest PC *alone*, because that is all
// the emitted chain probe has.  A plain (pc >> 2) is unusable for this ROM: 36 KB of
// code span means the low bits alone are far from uniform, and the table starts
// colliding with itself (measurably: self-recompiling blocks, compiles climbing
// without bound).  Mixing the middle bits in fixes it, and the probe reproduces
// exactly this arithmetic.
uint32_t slotFor(uint32_t pc, uint8_t *page) {
    (void)page;                                            // kept for the driver's own check
    return ((pc >> 2) ^ (pc >> 14)) & (kTableSize - 1);
}
uint32_t negSlotFor(uint32_t pc, uint8_t *page) {
    return ((pc >> 2) * 2654435761u ^ hostPage(page)) & (kNegSize - 1);
}

// Coarse class of an ARM word, for the refusal census.
uint32_t classifyRefuse(uint32_t word) {
    const uint32_t cls = (word >> 25) & 7;
    if (cls == 0 && ((word >> 4) & 1) && ((word >> 7) & 1)) return 6;  // misc: extra l/s, swap
    if (cls == 0 || cls == 1) return 0;                                 // data processing
    if (cls == 2 || cls == 3) return 1;                                 // ldr/str
    if (cls == 4) return 2;                                             // ldm/stm
    if (cls == 5) return 3;                                             // b/bl
    if (cls == 6) return 4;                                             // coprocessor
    return 5;                                                           // coprocessor/svc/undefined
}

// The same census for Thumb halfwords: the classes are deliberately the same
// numbers, so the ARM and Thumb lines can be read side by side.
uint32_t classifyRefuseThumb(uint16_t word) {
    if ((word >> 13) <= 1) return 0;                            // shift / add-sub / mov-cmp imm
    if ((word & 0xFC00u) == 0x4000u) return 0;                  // the ALU row
    if ((word & 0xF000u) == 0x5000u) return 1;                  // load/store, register offset
    if ((word >> 13) == 3 || (word >> 13) == 4) return 1;       // load/store imm5, SP-relative
    if ((word >> 12) == 12) return 2;                           // LDMIA/STMIA
    if ((word & 0xF600u) == 0xB400u) return 2;                  // PUSH/POP
    if ((word >> 12) == 13 || (word >> 13) == 7) return 3;      // Bcc / SWI / B / BL / BLX
    if ((word & 0xFC00u) == 0x4400u) return 3;                  // BX / BLX register
    return 5;
}

// Append a line to the stage file.  Every write is followed by fclose(), and the
// card is flushed on close, so a record survives the process dying right after -
// libfat keeps its write-back cache otherwise.
void stageAppend(const char *fmt, ...) {
    FILE *f = fopen("sd:/jit3-stage.txt", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// One line per divergence, plus a heartbeat every so often so a run that stops
// early still says how far verification got.  Every write is closed immediately.
void verifyAppend(const char *fmt, ...) {
    FILE *f = fopen("sd:/jit3-verify.txt", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

void noteCompile(uint32_t pc, bool thumb, uint32_t words, const uint32_t *code,
                 uint32_t instrs, const BlockInfo &info) {
    g_stats.compiles++;
    g_stats.blocks++;
    if (!g_trace) return;
    (void)thumb;                       // Thumb blocks arrive in slice 2
    char line[512];
    int n = snprintf(line, sizeof(line), "jit3: code pc=%08X off=%06X words=%u instrs=%u",
                     pc, (uint32_t)(code - arena), words, instrs);
    // The words this block hands to the interpreter, if any: the delegated
    // instruction is the one the block boundary is *not* paying for, so it is the
    // first thing to look at when a block is hot and the run is still slow.
    for (uint32_t i = 0; i < info.delegWordCount; i++)
        n += snprintf(line + n, sizeof(line) - n, " deleg=%08X", info.delegWords[i]);
    n += snprintf(line + n, sizeof(line) - n, ":");
    for (uint32_t i = 0; i < words; i++) {
        if (n + 12 >= (int)sizeof(line)) break;    // the word list may be cut short
        n += snprintf(line + n, sizeof(line) - n, " %08X", code[i]);
    }
    stageAppend("%s", line);
}

void noteRefusal(uint32_t pc, uint8_t *page, uint32_t word, bool thumb) {
    JitNegEntry &e = neg[negSlotFor(pc, page)];
    e.pc = pc; e.page = page; e.gen = modeGen(thumb);
    e.cls = thumb ? classifyRefuseThumb((uint16_t)word) : classifyRefuse(word);
    g_stats.refuses++;
    if (thumb) g_refuseThumb++;
    g_refuseClass[e.cls & 7]++;
    const uint32_t slot = (pc >> 2) & 63;
    if (g_refusePc[slot] != pc) { g_refusePc[slot] = pc; g_refuseHits[slot] = 0; }
    g_refuseHits[slot]++;
}

bool negHas(uint32_t pc, uint8_t *page, bool thumb) {
    const JitNegEntry &e = neg[negSlotFor(pc, page)];
    if (e.gen != modeGen(thumb) || e.pc != pc || e.page != page) return false;
    g_stats.refuses++;
    if (thumb) g_refuseThumb++;
    g_stats.nocached++;
    g_refuseClass[e.cls & 7]++;
    const uint32_t slot = (pc >> 2) & 63;
    if (g_refusePc[slot] == pc) g_refuseHits[slot]++;
    return true;
}

uint32_t unusedHelper(void *, uint32_t) { return 0; }

uint32_t gmRead32(void *ctx, uint32_t addr) {
    return ((Interpreter *)ctx)->jitFetch32(addr);
}
uint32_t gmRead16(void *ctx, uint32_t addr) {
    return ((Interpreter *)ctx)->jitFetch16(addr);
}

} // namespace

// ------------------------------------------------------------------ accessors

bool enabled() { return g_enabled; }
void setEnabled(bool on) { g_enabled = on; }
bool trace() { return g_trace; }
void setTrace(bool on) { g_trace = on; }
bool verify() { return g_verify; }
bool chain() { return g_chain; }
void setChain(bool on) { g_chain = on; }
void setChainCount(bool on) { g_chainCount = on; g_chainHits = 0; }
uint32_t chainHits() { return g_chainHits; }
void setVerify(bool on) {
    g_verify = on;
    g_blockLimit = on ? 1 : 0;   // per-instruction verification, see g_blockLimit
}

// Self-test block length; see g_selfTestBlocks.  Not part of the shipped behaviour:
// nothing outside jit3SelfTest() reads it.
void setSelfTestBlocks(uint32_t n) { g_selfTestBlocks = n; }

const Stats &stats() { return g_stats; }
void resetStats() { g_stats = Stats{}; }

uint32_t codeBytes() { return arenaUsed * 4; }
uint32_t arenaBytes() { return kArenaWords * 4; }

void invalidateAll() {
    memset(table, 0, sizeof(table));
    memset(neg, 0, sizeof(neg));
    arenaUsed = 0;
    generation++;
    g_stats.blocks = 0;
    __asm__ __volatile__("sync");
}

// Append the refusal census to the stage file: class totals plus the eight
// hottest refused PCs.  Called from the per-frame report, so it costs nothing
// unless the run is being watched.
void dumpRefusals() {
    stageAppend("# deleg:  dp=%u ldrstr=%u ldmstm=%u branch=%u coproc=%u other=%u miscls=%u",
                g_delegClass[0], g_delegClass[1], g_delegClass[2], g_delegClass[3],
                g_delegClass[4], g_delegClass[5], g_delegClass[6]);
    stageAppend("# deleg-entry: dp=%u ldrstr=%u ldmstm=%u branch=%u coproc=%u other=%u miscls=%u",
                g_delegEntry[0], g_delegEntry[1], g_delegEntry[2], g_delegEntry[3],
                g_delegEntry[4], g_delegEntry[5], g_delegEntry[6]);
    stageAppend("# refuse: dp=%u ldrstr=%u ldmstm=%u branch=%u coproc=%u other=%u miscls=%u "
                "| thumb=%u nocached=%u compiles=%u blocks=%u",
                g_refuseClass[0], g_refuseClass[1], g_refuseClass[2], g_refuseClass[3],
                g_refuseClass[4], g_refuseClass[5], g_refuseClass[6],
                g_refuseThumb, g_stats.nocached, g_stats.compiles, g_stats.blocks);
    for (int rank = 0; rank < 8; rank++) {
        uint32_t best = 0, bestI = 0xFFFFFFFFu;
        for (uint32_t i = 0; i < 64; i++) {
            if (g_refuseHits[i] > best) { best = g_refuseHits[i]; bestI = i; }
        }
        if (bestI == 0xFFFFFFFFu || !best) break;
        const uint32_t pc = g_refusePc[bestI];
        stageAppend("# refuse #%d pc=%08X n=%u", rank + 1, pc, best);
        g_refuseHits[bestI] = 0;
    }
}

} // namespace Jit3

using namespace Jit3;      // gives this file's Interpreter members the internals

// --------------------------------------------------------------- the guest side

// The interpreter's own fast path and the emitter's guest reads have to agree on
// what "this address" means, or a block would be built from one page and entered
// for another.
uint8_t *Interpreter::jitReadMap(uint32_t addr) {
    // Same page table the interpreter's own fast path uses; the returned pointer
    // is the page base, which doubles as the block table's identity for "this
    // guest page, as currently mapped".
    uint8_t **map = arm7 ? core->memory.readMap7 : core->memory.readMap9A;
    return map[addr >> 12];
}

uint16_t Interpreter::jitFetch16(uint32_t addr) {
    return core->memory.read<uint16_t>(arm7, addr);
}

uint32_t Interpreter::jitFetch32(uint32_t addr) {
    return core->memory.read<uint32_t>(arm7, addr);
}

// One guest instruction, handed to the interpreter by the glue.  The block has
// already written every guest register back, so the CPU is consistent; all that
// is left is the pipeline: build it for `pc` and run exactly one opcode.
uint32_t Interpreter::jit3InterpOne(Interpreter *cpu, uint32_t pc) {
    // The JIT is switched off for this one instruction, for two reasons: the
    // helper is called *from inside* a compiled block, and a nested dispatch could
    // compile over the arena the running block still occupies; and a delegated
    // instruction that remaps memory (a DMA or CP15 write) drops every block, so
    // nothing must be compiled between that and the block's own return.
    const bool was = Jit3::enabled();
    Jit3::setEnabled(false);
    cpu->jit3InterpAt(pc);
    const uint32_t cycles = (uint32_t)cpu->jit3StepOne();
    Jit3::setEnabled(was);
    return cycles;
}

// The compiled load/store helpers.  Each mirrors the interpreter handler the
// instruction would otherwise be delegated to (ldrOf/ldrbOf/strOf/strbOf in
// interpreter_transfer.cpp) for everything the emitted code cannot do itself: the
// access and the misaligned-read rotation.  The address, the register write-back,
// the destination register and the cycle charge all stay in the block.
uint32_t Interpreter::jit3Load32(Interpreter *cpu, uint32_t addr) {
    const uint32_t value = cpu->core->memory.read<uint32_t>(cpu->arm7, addr);
    if (addr & 0x3) {                       // ldrOf: rotate misaligned reads
        const uint8_t shift = (uint8_t)((addr & 0x3) << 3);
        return (value << (32 - shift)) | (value >> shift);
    }
    return value;
}

uint32_t Interpreter::jit3Load8(Interpreter *cpu, uint32_t addr) {
    return cpu->core->memory.read<uint8_t>(cpu->arm7, addr);      // ldrbOf: no rotation
}

uint32_t Interpreter::jit3Load16(Interpreter *cpu, uint32_t addr) {
    uint32_t v = cpu->core->memory.read<uint16_t>(cpu->arm7, addr);
    // ldrhOf: an ARM7 misaligned halfword read comes back rotated, not shifted
    if (addr & cpu->arm7) v = (v << 24) | (v >> 8);
    return v;
}

uint32_t Interpreter::jit3LoadS8(Interpreter *cpu, uint32_t addr) {
    // ldrsbOf: sign-extended byte.  (The change of type *is* the sign extension,
    // and it happens here rather than in the block so the block needs no temp.)
    return (uint32_t)(int32_t)(int8_t)cpu->core->memory.read<uint8_t>(cpu->arm7, addr);
}

uint32_t Interpreter::jit3LoadS16(Interpreter *cpu, uint32_t addr) {
    int32_t v = (int16_t)cpu->core->memory.read<uint16_t>(cpu->arm7, addr);
    // ldrshOf: on ARM7 a misaligned read is *shifted* down, sign bits included
    if (addr & cpu->arm7) v = (int32_t)((int16_t)v >> 8);
    return (uint32_t)v;
}

void Interpreter::jit3Store16(Interpreter *cpu, uint32_t addr, uint32_t value) {
    cpu->core->memory.write<uint16_t>(cpu->arm7, addr, (uint16_t)value);   // strhOf
}

void Interpreter::jit3Store32(Interpreter *cpu, uint32_t addr, uint32_t value) {
    cpu->core->memory.write<uint32_t>(cpu->arm7, addr, value);    // strOf
}

void Interpreter::jit3Store8(Interpreter *cpu, uint32_t addr, uint32_t value) {
    cpu->core->memory.write<uint8_t>(cpu->arm7, addr, (uint8_t)value);   // strbOf
}

void Interpreter::jit3InterpAt(uint32_t pc) {
    *registers[15] = pc;
    flushPipeline();
}

// ---------------------------------------------------------------- dispatcher

// The emitted code reads guest registers straight out of the Interpreter object,
// so the offsets have to come from the real class.  jit3Config() lives in this
// file because Cfg does; interpreter.h only sees an opaque pointer.
void Interpreter::jit3Config(void *cfgOut) {
    Cfg &cfg = *(Cfg *)cfgOut;
    cfg = Cfg();
    cfg.arm7        = arm7;
    cfg.offRegsUsr  = (int32_t)((const char *)&registersUsr[0] - (const char *)this);
    cfg.offRegsPtr  = (int32_t)((const char *)&registers[0]     - (const char *)this);
    cfg.offCpsr     = (int32_t)((const char *)&cpsr              - (const char *)this);
    cfg.offHalted   = (int32_t)((const char *)&halted            - (const char *)this);
    cfg.offCore     = (int32_t)((const char *)core               - (const char *)this);
    for (int i = 0; i < HelperCount; i++) cfg.helper[i] = (uintptr_t)&unusedHelper;
    cfg.helper[H_InterpOne] = (uintptr_t)&Interpreter::jit3InterpOne;
    cfg.helper[H_Load32]    = (uintptr_t)&Interpreter::jit3Load32;
    cfg.helper[H_Load8]     = (uintptr_t)&Interpreter::jit3Load8;
    cfg.helper[H_Load16]    = (uintptr_t)&Interpreter::jit3Load16;
    cfg.helper[H_LoadS8]    = (uintptr_t)&Interpreter::jit3LoadS8;
    cfg.helper[H_LoadS16]   = (uintptr_t)&Interpreter::jit3LoadS16;
    cfg.helper[H_Store16]   = (uintptr_t)&Interpreter::jit3Store16;
    cfg.helper[H_Store32]   = (uintptr_t)&Interpreter::jit3Store32;
    cfg.helper[H_Store8]    = (uintptr_t)&Interpreter::jit3Store8;
    cfg.maxBlockInstrs = g_blockLimit ? g_blockLimit : 24;
    cfg.maxBlockWords  = kMaxBlockWords;
    cfg.haltCheckOnIoStore = true;

    // Block chaining.  The verifier compares a block against the interpreter's own
    // instruction loop, so a block that ran a whole *chain* would not be comparable:
    // chaining is off whenever verification is on.
    const bool chaining = g_chain && !g_verify;
    cfg.chain       = chaining;
    cfg.chainTable  = (uintptr_t)&table[0];
    cfg.chainMask   = kTableSize - 1;
    cfg.chainStride = (uint32_t)sizeof(JitBlockEntry);
    cfg.chainArena  = (uintptr_t)&arena[0];
    // The mode rides in the low bit (see modeGen); the driver overwrites this per
    // dispatch, and an external caller that forgets it merely gets a probe that
    // cannot hit, never one that hits the wrong block.
    cfg.chainGen    = modeGen(false);
    cfg.chainBudget = g_chainBudget;
    cfg.chainCounter = (chaining && g_chainCount) ? &g_chainHits : nullptr;
}

int Interpreter::jit3Run() {
    if (!Jit3::enabled()) return -1;
    // One of these two counters moves for every guest instruction: `runs` is the
    // driver being asked, `nowork` is the driver answering "not mine" and the
    // interpreter executing the word itself (Thumb, halted, unmapped, refused).
    // The split is how the delegate cost was found: with loads and stores handed
    // to the interpreter inline, half of the instructions *inside* compiled blocks
    // were paying a full interpreter call plus the register-file marshalling.
    g_stats.runs++;
    if (halted) { g_stats.nowork++; g_stats.solo++; return -1; }

    // Thumb and ARM are both compiled now.  The convention differs: the pipeline PC
    // counts one *step* past the instruction being executed - 4 in ARM mode, 2 in
    // Thumb - so the logical PC is registers[15] minus the step, and the same figure
    // decides which fetch helper and which emitter the block goes through.
    const bool thumb = (cpsr & BIT(5)) != 0;
    const uint32_t pc = *registers[15] - (thumb ? 2u : 4u);
    uint8_t *page = jitReadMap(pc);
    if (!page) { g_stats.nowork++; g_stats.solo++; return -1; }

    const uint32_t slot = slotFor(pc, page);
    JitBlockEntry &e = table[slot];
    BlockFn code;

    if (e.gen == modeGen(thumb) && e.pc == pc && e.page == page) {
        code = (BlockFn)(void *)(arena + e.off);
    }
    else {
        if (negHas(pc, page, thumb)) { g_stats.nowork++; g_stats.solo++; return -1; }

        // Keep one worst-case block in reserve: compileBlock() reports overflow
        // by failing, which would otherwise be indistinguishable from a refusal.
        if (arenaUsed + kMaxBlockWords > kArenaWords) {
            invalidateAll();          // the counter for this lives in Stats::flushes
            g_stats.flushes++;
        }

        Cfg cfg;
        jit3Config(&cfg);
        // Every compiled block bakes the generation it was compiled in, and the chain
        // probe compares against it.  jit3Config() reads it right before the compile as
        // well; this line is the invariant, stated where it is relied on.
        cfg.chainGen = modeGen(thumb);
        GuestMem gm;
        gm.ctx = this;
        gm.read32 = gmRead32;
        gm.read16 = gmRead16;

        uint32_t *out = arena + arenaUsed;
        uint32_t words = 0;
        BlockInfo info;
        // The cap is what the emitter may use for *this* block, and it has to be
        // the same figure the reserve above assumes.
        const uint32_t cap = (kArenaWords - arenaUsed) < kMaxBlockWords
                           ? (kArenaWords - arenaUsed) : kMaxBlockWords;
        if (!Jit3::compileBlock(cfg, gm, pc, thumb, out, cap, words, info)) {
            noteRefusal(pc, page, thumb ? jitFetch16(pc) : jitFetch32(pc), thumb);
            g_stats.nowork++;
            g_stats.solo++;
            return -1;
        }

        // The block is about to be called: publish it to the instruction stream
        // before anything can jump to it.
        DCStoreRange(out, words * 4);
        ICInvalidateRange(out, words * 4);

        e.pc = pc; e.page = page; e.off = arenaUsed; e.gen = modeGen(thumb);
        // The warm entry is a host address, not an arena offset: the arena is a
        // static array, so the address stays valid for as long as the generation
        // does - which is exactly what the probe checks.
        e.warmPtr = (uint32_t)(uintptr_t)(out + info.warmOffset);
        e.instrs = info.instrCount; e.cycles = info.cycles;
        e.delegs = info.delegInstrs;
        e.delegWord = info.delegWordCount ? info.delegWords[0] : 0;
        arenaUsed += words;
        noteCompile(pc, thumb, words, out, info.instrCount, info);
        for (uint32_t i = 0; i < info.delegWordCount; i++)
            g_delegClass[classifyRefuse(info.delegWords[i])]++;
        code = (BlockFn)(void *)out;
    }

    g_stats.entered++;
    g_stats.instrs += e.instrs;
    g_stats.instrsDeleg += e.delegs;
    // Entry-weighted delegate census: the per-compile census above says which words
    // *exist*, this says what they cost.  A block compiled once and entered 400,000
    // times is invisible in the first and obvious in the second.
    if (e.delegWord) {
        g_delegEntry[classifyRefuse(e.delegWord)] += e.delegs;
        // keep the eight hottest delegated pcs, with their entry counts
        uint32_t *lo = &g_hotCount[0]; int worst = 0;
        for (int i = 1; i < 8; i++) if (g_hotCount[i] < lo[worst]) worst = i;
        int hit = -1;
        for (int i = 0; i < 8; i++) if (g_hotAddr[i] == pc) { hit = i; break; }
        if (hit >= 0) g_hotCount[hit] += e.delegs;
        else if (g_hotCount[worst] == 0 || e.delegs >= 1) {
            if (g_hotCount[worst] == 0) { g_hotAddr[worst] = pc; g_hotCount[worst] = e.delegs; }
        }
    }

    if (g_verify) {
        // Copy the entry fields first: a delegated instruction can remap guest
        // memory, and the invalidateAll() that follows zeroes this very table
        // entry while the block is still running (it is called from inside the
        // block), which would make the report describe a block that no longer
        // exists.
        const uint32_t vInstrs = e.instrs;
        const uint32_t vPc = pc;
        // State as the block will see it.
        uint32_t sUsr[16];
        for (int i = 0; i < 16; i++) sUsr[i] = registersUsr[i];
        const uint32_t sCpsr = cpsr, sPc = *registers[15];
        const bool sHalt = halted;

        const BlockResult rj = code(this, 0u);
        uint32_t jUsr[16];
        for (int i = 0; i < 16; i++) jUsr[i] = registersUsr[i];
        const uint32_t jCpsr = cpsr, jPcReg = *registers[15];
        const bool jHalt = halted;
        // The interpreter's pipeline PC counts one step past the logical PC, and the
        // step follows the T bit - which the block may have changed (BX) or left alone.
        const uint32_t jStep = (jCpsr & BIT(5)) ? 2u : 4u;
        const uint32_t jExit = (rj.exitPc == kPcFromCpu) ? (jPcReg - jStep)
                                                         : (rj.exitPc & ~(jStep - 1u));

        // Same PC, same instruction count, plain interpreter.
        for (int i = 0; i < 16; i++) registersUsr[i] = sUsr[i];
        cpsr = sCpsr;
        halted = sHalt;
        *registers[15] = sPc - ((sCpsr & BIT(5)) ? 2u : 4u);   // logical PC: flushPipeline() rebuilds
        flushPipeline();
        Jit3::setEnabled(false);
        int rc = 0;
        for (uint32_t i = 0; i < vInstrs; i++) rc += jit3StepOne();
        Jit3::setEnabled(true);
        const uint32_t refExit = *registers[15] - ((cpsr & BIT(5)) ? 2u : 4u);

        g_verifyBlocks++;
        int bad = -1;
        if ((int)rj.cycles != rc)      bad = 100;
        else if (jExit != refExit)     bad = 101;
        else if (jCpsr != cpsr)        bad = 102;
        else if (jHalt != halted)      bad = 103;
        else {
            for (int i = 0; i < 16; i++) {
                if (jUsr[i] != registersUsr[i]) { bad = i; break; }
            }
        }

        if (bad >= 0) {
            g_verifyBad++;
            if (g_verifyLogged < 64) {
                g_verifyLogged++;
                char words[128];
                int n = 0;
                words[0] = '\0';
                const uint32_t shown = vInstrs < 6 ? vInstrs : 6;
                const bool thumbPre = (sCpsr & BIT(5)) != 0;
                for (uint32_t i = 0; i < shown; i++)
                    n += snprintf(words + n, sizeof(words) - n, "%s%08X",
                                  i ? " " : "",
                                  thumbPre ? jitFetch16(vPc + i * 2) : jitFetch32(vPc + i * 4));
                verifyAppend("bad pc=%08X instrs=%u field=%s cyc=%d/%d exit=%08X/%08X "
                             "cpsr=%08X/%08X halt=%d/%d r%02d=%08X/%08X pre_r%02d=%08X | %s",
                             vPc, vInstrs,
                             bad < 16 ? "reg" : (bad == 100 ? "cycles" : bad == 101 ? "pc"
                                      : bad == 102 ? "cpsr" : "halt"),
                             rj.cycles, rc, jExit, refExit, jCpsr, cpsr,
                             (int)jHalt, (int)halted,
                             bad < 16 ? bad : 0,
                             bad < 16 ? jUsr[bad] : 0u, bad < 16 ? registersUsr[bad] : 0u,
                             bad < 16 ? bad : 0, bad < 16 ? sUsr[bad] : 0u,
                             words);
                if (bad < 16 && g_verifyLogged <= 4) {
                    char pre[192], jitd[192], refd[192];
                    int a = 0, b = 0, c = 0;
                    for (int i = 0; i < 16; i++) {
                        a += snprintf(pre + a, sizeof(pre) - a, " %08X", sUsr[i]);
                        b += snprintf(jitd + b, sizeof(jitd) - b, " %08X", jUsr[i]);
                        c += snprintf(refd + c, sizeof(refd) - c, " %08X", registersUsr[i]);
                    }
                    verifyAppend("  pre r0-r15:%s", pre);
                    verifyAppend("  jit r0-r15:%s", jitd);
                    verifyAppend("  ref r0-r15:%s", refd);
                }
            }
        }
        if ((g_verifyBlocks & 0x3FF) == 0x3FF)
            verifyAppend("# verify: blocks=%u bad=%u", g_verifyBlocks, g_verifyBad);
        return rc;                            // continue on the interpreter's answer
    }

    const BlockResult r = code(this, 0u);
    // The step is fixed by the T bit *as the block left it*, so a BX into THUMB (or
    // out of it) is re-entered at the right granularity.  Reading it before the
    // exit value is what makes the two modes symmetric: "PC is in the CPU" means
    // registers[15] is already the pipeline PC and the conversion is the identity.
    const uint32_t step = (cpsr & BIT(5)) ? 2u : 4u;
    const uint32_t exitPc = (r.exitPc == kPcFromCpu) ? (*registers[15] - step)
                                                     : (r.exitPc & ~(step - 1u));
    // Leave the pipeline PC in the interpreter's convention but do not refill
    // pipeline[0]/pipeline[1] - flushPipeline() would go through the memory maps
    // twice.  runOpcode() refills on demand if the next instruction is not a block.
    // The step follows the T bit (a block can BX into THUMB), and the deferred
    // refill puts the logical PC back before calling flushPipeline(), which is
    // what adds the step.
    *registers[15] = (exitPc & ~(step - 1)) + step;
    pipelineStale = true;
    if (r.exitPc == kPcFromCpu) g_stats.delegates++;
    return (int)r.cycles;
}

// ----------------------------------------------------------------- self-test

namespace {

// A tiny ARM encoder, so the test programs read like the instructions they are.
constexpr uint32_t AL = 14;

uint32_t rotImm(uint32_t value8, uint32_t rot) { return (rot << 8) | (value8 & 0xFF); }

uint32_t encDP(uint32_t cond, uint32_t dp, bool s, uint32_t rd, uint32_t rn, uint32_t op2) {
    return (cond << 28) | (dp << 21) | (s ? (1u << 20) : 0u) | (rn << 16) | (rd << 12) | op2;
}
uint32_t encMov(uint32_t rd, uint32_t imm8)     { return encDP(AL, 13, false, rd, 0, rotImm(imm8, 0)); }
uint32_t encMovS(uint32_t rd, uint32_t imm8)    { return encDP(AL, 13, true,  rd, 0, rotImm(imm8, 0)); }
uint32_t encSubS(uint32_t rd, uint32_t rn, uint32_t imm8) {
    return encDP(AL, 2, true, rd, rn, rotImm(imm8, 0));
}
uint32_t encAdd(uint32_t rd, uint32_t rn, uint32_t imm8) {
    return encDP(AL, 4, false, rd, rn, rotImm(imm8, 0));
}
uint32_t encCmpReg(uint32_t rn, uint32_t rm)    { return encDP(AL, 10, true, 0, rn, rm); }
uint32_t encShift(uint32_t dp, bool s, uint32_t rd, uint32_t rm, uint32_t type, uint32_t amt) {
    return encDP(AL, dp, s, rd, 0, (amt << 7) | ((type & 3) << 5) | rm);
}
uint32_t encB(uint32_t at, uint32_t target, uint32_t cond = AL) {
    const int32_t off = (int32_t)(target - at - 8) >> 2;
    return (cond << 28) | 0x0A000000u | ((uint32_t)off & 0x00FFFFFFu);
}
uint32_t encBl(uint32_t at, uint32_t target) {
    const int32_t off = (int32_t)(target - at - 8) >> 2;
    return 0xEB000000u | ((uint32_t)off & 0x00FFFFFFu);
}
constexpr uint32_t kLt = 11;
constexpr uint32_t kBxLr = 0xE12FFF1Eu;

// The same idea for Thumb: one halfword per entry, addresses in halfwords, and the
// operand orders spelled out because Thumb's two-source forms are not the ARM ones.
uint32_t tMovIm(uint32_t rd, uint32_t imm)   { return 0x2000u | (rd << 8) | (imm & 0xFF); }
uint32_t tCmpIm(uint32_t rd, uint32_t imm)   { return 0x2800u | (rd << 8) | (imm & 0xFF); }
uint32_t tAddIm8(uint32_t rd, uint32_t imm)  { return 0x3000u | (rd << 8) | (imm & 0xFF); }
uint32_t tSubIm8(uint32_t rd, uint32_t imm)  { return 0x3800u | (rd << 8) | (imm & 0xFF); }
// Format 1: kind 0 = LSL, 1 = LSR, 2 = ASR (imm5 = 0 means 32 for the right shifts)
uint32_t tShiftIm(uint32_t kind, uint32_t rd, uint32_t rs, uint32_t amt) {
    return (kind << 11) | ((amt & 0x1F) << 6) | (rs << 3) | rd;
}
// Format 2, register form: ADD/SUB Rd, Rs, Rn (Rs in bits 5-3, Rn in 8-6)
uint32_t tReg2(bool subOp, uint32_t rd, uint32_t rs, uint32_t rn) {
    return (subOp ? 0x1A00u : 0x1800u) | (rn << 6) | (rs << 3) | rd;
}
// Format 2, three-bit immediate form
uint32_t tImm3(bool subOp, uint32_t rd, uint32_t rs, uint32_t i3) {
    return (subOp ? 0x1E00u : 0x1C00u) | ((i3 & 7) << 6) | (rs << 3) | rd;
}
// The ALU row: op 0 AND 1 EOR 2 LSL 3 LSR 4 ASR 5 ADC 6 SBC 7 ROR 8 TST 9 NEG
//              10 CMP 11 CMN 12 ORR 13 MUL 14 BIC 15 MVN
uint32_t tAlu(uint32_t op, uint32_t rd, uint32_t rs) { return 0x4000u | (op << 6) | (rs << 3) | rd; }
// High registers: kind 0 = ADD, 1 = CMP, 2 = MOV
uint32_t tHi(uint32_t kind, uint32_t rd, uint32_t rs) {
    return (0x4400u + (kind << 8)) | ((rd >> 3) << 7) | ((rs >> 3) << 6) | ((rs & 7) << 3) | (rd & 7);
}
uint32_t tBx(uint32_t rs)                    { return 0x4700u | (rs << 3); }
uint32_t tLdrPc(uint32_t rd, uint32_t imm8)  { return 0x4800u | (rd << 8) | (imm8 & 0xFF); }
// Register offset: 0 STR 1 STRH 2 STRB 3 LDRSB 4 LDR 5 LDRH 6 LDRB 7 LDRSH
uint32_t tMemReg(uint32_t op, uint32_t rd, uint32_t rb, uint32_t ro) {
    return 0x5000u | (op << 9) | (ro << 6) | (rb << 3) | rd;
}
// Immediate 5: 0 STR 1 LDR 2 STRB 3 LDRB 4 STRH 5 LDRH
uint32_t tMemIm(uint32_t op, uint32_t rd, uint32_t rb, uint32_t imm5) {
    return (0x6000u + (op << 11)) | ((imm5 & 0x1F) << 6) | (rb << 3) | rd;
}
uint32_t tStrSp(uint32_t rd, uint32_t imm8)  { return 0x9000u | (rd << 8) | (imm8 & 0xFF); }
uint32_t tLdrSp(uint32_t rd, uint32_t imm8)  { return 0x9800u | (rd << 8) | (imm8 & 0xFF); }
uint32_t tAdr(uint32_t rd, uint32_t imm8)    { return 0xA000u | (rd << 8) | (imm8 & 0xFF); }
uint32_t tAddSp(uint32_t rd, uint32_t imm8)  { return 0xA800u | (rd << 8) | (imm8 & 0xFF); }
uint32_t tSpAdj(bool subOp, uint32_t imm7)   { return 0xB000u | (subOp ? 0x80u : 0u) | (imm7 & 0x7F); }
uint32_t tPush(uint32_t list8, bool lr = false) { return 0xB400u | (lr ? 0x100u : 0u) | (list8 & 0xFF); }
uint32_t tPop(uint32_t list8, bool pc = false)  { return 0xBC00u | (pc ? 0x100u : 0u) | (list8 & 0xFF); }
uint32_t tStmia(uint32_t rb, uint32_t list8) { return 0xC000u | (rb << 8) | (list8 & 0xFF); }
uint32_t tLdmia(uint32_t rb, uint32_t list8) { return 0xC800u | (rb << 8) | (list8 & 0xFF); }
uint32_t tBcc(uint32_t at, uint32_t target, uint32_t cond) {
    return 0xD000u | (cond << 8) | (((target - at - 4) >> 1) & 0xFF);
}
uint32_t tB(uint32_t at, uint32_t target)    { return 0xE000u | (((target - at - 4) >> 1) & 0x7FF); }
uint32_t tBlHi(uint32_t at, uint32_t target) {
    const uint32_t off = target - at - 4;
    return 0xF000u | ((off >> 12) & 0x7FF);
}
uint32_t tBlLo(uint32_t at, uint32_t target) {
    const uint32_t off = target - at - 4;
    return 0xF800u | ((off >> 1) & 0x7FF);
}

struct TestProg {
    const char *name;
    uint32_t words;          // words (ARM) or halfwords (Thumb) to write at kScratchPc
    bool thumb = false;
    uint32_t loopAddr = 0;   // absolute address of the terminating self-loop (0 = last entry)
    // Filled by the generator: the program is assembled with absolute addresses,
    // so it needs to know where it will live.
    void (*build)(uint32_t base, uint32_t *out);
};

// `b +4` in both instruction sets: a *terminator* that falls into the next
// instruction.  Putting one in front of each program's sentinel instruction keeps
// every block that a self-test run can build from covering that sentinel, because a
// block cannot continue past an unconditional branch.  Without it a multi-instruction
// block swallows the sentinel, executes it and charges for it, while the interpreter
// reference stops *at* it - a difference in the harness's stop rule that looks
// exactly like a mispriced branch.
constexpr uint32_t kTermArm   = 0xEAFFFFFFu;   // b +4
constexpr uint32_t kTermThumb = 0xE7FFu;       // b +2

void buildFlags(uint32_t base, uint32_t *c) {
    c[0] = encMovS(0, 5);                                     // movs r0, #5
    c[1] = encSubS(1, 0, 3);                                  // subs r1, r0, #3
    c[2] = encDP(AL, 5, true, 2, 1, 1);                       // adcs r2, r1, r1
    c[3] = encDP(AL, 1, false, 3, 2, 0);                      // eor  r3, r2, r0
    c[4] = encDP(AL, 10, true, 0, 3, rotImm(5, 0));           // cmp  r3, #5
    c[5] = encB(base + 20, base + 20);                        // b .
    // Keep the sentinel reachable only as a *block start*: a block that covers it
    // executes and charges it, which the reference (stopping at the sentinel) does
    // not - a test artifact, not a JIT difference (see the self-test note).
    c[6] = c[5];
    c[5] = kTermArm;
}

void buildLoop(uint32_t base, uint32_t *c) {
    c[0] = encMov(0, 0);                                      // mov r0, #0
    c[1] = encMov(1, 10);                                     // mov r1, #10
    c[2] = encAdd(0, 0, 1);                                   // l: add r0, r0, #1
    c[3] = encCmpReg(0, 1);                                   //    cmp r0, r1
    c[4] = encB(base + 16, base + 8, kLt);                    //    blt l
    c[5] = encB(base + 20, base + 20);                        //    b .
    c[6] = c[5]; c[5] = kTermArm;
}

void buildCall(uint32_t base, uint32_t *c) {
    c[0] = encMov(0, 0);                                      // mov r0, #0
    c[1] = encBl(base + 4, base + 12);                        // bl sub
    c[2] = encB(base + 8, base + 8);                          // b .
    c[3] = encAdd(0, 0, 2);                                   // sub: add r0, r0, #2
    c[4] = kBxLr;                                             //      bx lr
    c[5] = c[4]; c[4] = kTermArm;
}

void buildMix(uint32_t base, uint32_t *c) {
    c[0] = encMov(0, 0x10);                                   // mov r0, #0x10
    c[1] = encShift(13, false, 1, 0, 0, 4);                   // mov r1, r0, lsl #4
    c[2] = encDP(AL, 4, true, 2, 1, (3u << 7) | (3u << 5) | 0u); // adds r2, r1, r0, ror #3
    c[3] = encShift(13, true, 3, 2, 1, 1);                    // movs r3, r2, lsr #1
    c[4] = encDP(AL, 12, false, 4, 3, 0);                     // orr r4, r3, r0
    c[5] = encB(base + 20, base + 20);                        // b .
    c[6] = c[5]; c[5] = kTermArm;
}

// ---- Thumb programs.  Every one of them ends in a self-loop, and between the two
// runs the *step* the interpreter takes is what the block has to reproduce, so the
// programs are written to end up exercising one family each.

void buildTShift(uint32_t base, uint32_t *c) {
    c[0] = tMovIm(0, 0x0B);                                   // movs r0, #0x0B
    c[1] = tShiftIm(0, 0, 0, 4);                              // lsls r0, r0, #4
    c[2] = tShiftIm(1, 1, 0, 2);                              // lsrs r1, r0, #2
    c[3] = tShiftIm(2, 2, 1, 1);                              // asrs r2, r1, #1
    c[4] = tShiftIm(1, 3, 0, 0);                              // lsrs r3, r0, #0  (== #32, C = bit31)
    c[5] = tShiftIm(2, 4, 0, 0);                              // asrs r4, r0, #0
    c[6] = tShiftIm(0, 5, 0, 0);                              // lsls r5, r0, #0  (C preserved)
    c[7] = tMovIm(3, 3);                                      // movs r3, #3
    c[8] = tAddIm8(3, 5);                                     // adds r3, #5
    c[9] = tSubIm8(3, 1);                                     // subs r3, #1
    c[10] = tCmpIm(3, 7);                                     // cmp  r3, #7
    c[11] = tReg2(false, 4, 0, 2);                            // adds r4, r0, r2
    c[12] = tReg2(true, 5, 4, 2);                             // subs r5, r4, r2
    c[13] = tImm3(false, 6, 5, 3);                            // adds r6, r5, #3
    c[14] = tImm3(true, 7, 6, 2);                             // subs r7, r6, #2
    c[15] = tB(base + 30, base + 30);                         // b .
    c[16] = c[15]; c[15] = kTermThumb;
}

void buildTMem(uint32_t base, uint32_t *c) {
    c[0] = tAdr(0, 192);                                      // add r0, pc, #192 -> the data area
    c[1] = tMovIm(1, 0xAB);                                   // movs r1, #0xAB
    c[2] = tMemIm(0, 1, 0, 0);                                // str  r1, [r0, #0]
    c[3] = tMemIm(0, 1, 0, 2);                                // str  r1, [r0, #8]
    c[4] = tMemIm(2, 1, 0, 3);                                // strb r1, [r0, #12]
    c[5] = tMemIm(4, 1, 0, 3);                                // strh r1, [r0, #6]
    c[6] = tMemIm(1, 2, 0, 0);                                // ldr  r2, [r0, #0]
    c[7] = tMemIm(1, 3, 0, 2);                                // ldr  r3, [r0, #8]
    c[8] = tMemIm(3, 4, 0, 3);                                // ldrb r4, [r0, #3]
    c[9] = tMemIm(5, 5, 0, 3);                                // ldrh r5, [r0, #6]
    c[10] = tMovIm(6, 4);                                     // movs r6, #4
    c[11] = tMemReg(4, 7, 0, 6);                              // ldr  r7, [r0, r6]
    c[12] = tMemReg(0, 2, 0, 6);                              // str  r2, [r0, r6]
    c[13] = tMemReg(6, 4, 0, 6);                              // ldrb r4, [r0, r6]
    c[14] = tMemReg(3, 5, 0, 6);                              // ldrsb r5, [r0, r6]
    c[15] = tMemReg(7, 6, 0, 6);                              // ldrsh r6, [r0, r6]
    c[16] = tStrSp(2, 1);                                     // str r2, [sp, #4]
    c[17] = tLdrSp(3, 1);                                     // ldr r3, [sp, #4]
    c[18] = tAddSp(4, 4);                                     // add r4, sp, #16
    c[19] = tSpAdj(false, 4);                                 // add sp, #16
    c[20] = tSpAdj(true, 4);                                  // sub sp, #16
    c[21] = tMovIm(6, 5);                                     // movs r6, #5
    c[22] = tMemReg(4, 7, 0, 6);                              // ldr r7, [r0, r6]: misaligned word
    c[23] = tB(base + 46, base + 46);                         // b .
    c[24] = c[23]; c[23] = kTermThumb;
}

void buildTAlu(uint32_t base, uint32_t *c) {
    c[0] = tMovIm(0, 0x0F);                                   // movs r0, #0x0F
    c[1] = tMovIm(1, 0x03);                                   // movs r1, #0x03
    c[2] = tAlu(1, 2, 1);                                     // eors r2, r1
    c[3] = tAlu(0, 3, 0);                                     // ands r3, r0
    c[4] = tAlu(12, 4, 2);                                    // orrs r4, r2
    c[5] = tAlu(13, 5, 1);                                    // muls r5, r1
    c[6] = tAlu(2, 6, 1);                                     // lsls r6, r1
    c[7] = tAlu(3, 7, 0);                                     // lsrs r7, r0
    c[8] = tAlu(4, 0, 7);                                     // asrs r0, r7
    c[9] = tAlu(7, 1, 0);                                     // rors r1, r0
    c[10] = tAlu(5, 2, 1);                                    // adcs r2, r1
    c[11] = tAlu(6, 3, 2);                                    // sbcs r3, r2
    c[12] = tAlu(8, 4, 0);                                    // tst  r4, r0
    c[13] = tAlu(9, 5, 4);                                    // negs r5, r4
    c[14] = tAlu(10, 6, 5);                                   // cmp  r6, r5
    c[15] = tAlu(11, 7, 6);                                   // cmn  r7, r6
    c[16] = tAlu(14, 0, 7);                                   // bics r0, r7
    c[17] = tAlu(15, 1, 0);                                   // mvns r1, r0
    c[18] = tHi(2, 8, 1);                                     // mov  r8, r1
    c[19] = tHi(0, 9, 8);                                     // add  r9, r8
    c[20] = tHi(1, 9, 8);                                     // cmp  r9, r8
    c[21] = tHi(2, 10, 9);                                    // mov  r10, r9
    c[22] = tHi(0, 11, 10);                                   // add  r11, r10
    c[23] = tHi(2, 12, 11);                                   // mov  r12, r11
    c[24] = tB(base + 48, base + 48);                         // b .
    c[25] = c[24]; c[24] = kTermThumb;
}

void buildTBranch(uint32_t base, uint32_t *c) {
    c[0] = tMovIm(0, 1);                                      // movs r0, #1
    c[1] = tCmpIm(0, 1);                                      // cmp  r0, #1
    c[2] = tBcc(base + 4, base + 8, 0);                       // beq  -> taken, to halfword 4
    c[3] = tMovIm(1, 0x55);                                   // (skipped)
    c[4] = tMovIm(2, 0x77);                                   // movs r2, #0x77
    c[5] = tCmpIm(2, 0x77);                                   // cmp  r2, #0x77
    c[6] = tBcc(base + 12, base + 16, 1);                     // bne  -> not taken, falls through
    c[7] = tMovIm(3, 0x99);                                   // movs r3, #0x99
    c[8] = tBlHi(base + 16, base + 24);                       // bl sub  (halfword 12)
    c[9] = tBlLo(base + 18, base + 24);
    c[10] = tB(base + 20, base + 20);                         // .   <- LR lands on halfword 10
    c[11] = tMovIm(6, 0);                                     // (never run)
    c[12] = tMovIm(5, 0x22);                                  // sub: movs r5, #0x22
    c[13] = tBx(14);                                          //      bx lr
}

void buildTStack(uint32_t base, uint32_t *c) {
    c[0] = tMovIm(0, 0x11);                                   // movs r0, #0x11
    c[1] = tMovIm(1, 0x22);                                   // movs r1, #0x22
    c[2] = tPush(0x03);                                       // push {r0, r1}
    c[3] = tMovIm(0, 0);                                      // movs r0, #0
    c[4] = tMovIm(1, 0);                                      // movs r1, #0
    c[5] = tPop(0x03);                                        // pop  {r0, r1}
    c[6] = tAdr(4, 192);                                      // add r4, pc, #192 -> the data area
    c[7] = tStmia(4, 0x03);                                   // stmia r4!, {r0, r1}
    c[8] = tMovIm(0, 0);                                      // movs r0, #0
    c[9] = tMovIm(1, 0);                                      // movs r1, #0
    c[10] = tLdmia(4, 0x03);                                  // ldmia r4!, {r0, r1}
    c[11] = tAdr(5, 2);                                       // add r5, pc, #2 -> halfword 14, | 1 below
    c[12] = tMovIm(6, 1);                                     // movs r6, #1
    c[13] = tAlu(12, 5, 6);                                   // orrs r5, r6  (T bit set)
    c[14] = tPush(0x20);                                      // push {r5}
    c[15] = tPop(0x00, true);                                 // pop  {pc}   (a terminator, delegated)
    c[16] = tB(base + 32, base + 32);                         // b .  <- where the popped PC lands
    c[17] = c[16]; c[16] = kTermThumb;
}

// The mode switch: Thumb -> ARM -> Thumb.  The ARM stub (two words at base+0x20)
// loads a literal and branches to it with bit 0 set, which is what makes bxRegT /
// bxReg *set* the T bit; the driver has to re-enter at halfword granularity after it.
void buildTSwitch(uint32_t base, uint32_t *c) {
    c[0] = tLdrPc(0, 2);                                      // ldr r0, [pc, #8] -> the ARM stub address
    c[1] = tBx(0);                                            // bx  r0 (even -> ARM state)
    c[2] = tB(base + 4, base + 4);                            // b .   <- the ARM stub lands here
    c[6] = (base + 0x20) & 0xFFFF;                            // literal at base+12: the ARM stub's address
    c[7] = ((base + 0x20) >> 16) & 0xFFFF;
    c[16] = 0x1004;                                           // ldr r1, [pc, #4]   (ARM, at base+0x20)
    c[17] = 0xE59F;
    c[18] = 0xFF11;                                           // bx  r1             (ARM, at base+0x24)
    c[19] = 0xE12F;
    c[22] = (base + 5) & 0xFFFF;                              // literal at base+0x2C: the Thumb loop | T
    c[23] = ((base + 5) >> 16) & 0xFFFF;
}

const TestProg kPrograms[] = {
    { "flags", 7, false, 0, buildFlags },
    { "loop",  7, false, 0, buildLoop },
    { "call",  6, false, 0, buildCall },
    { "mix",   7, false, 0, buildMix },
    { "t-shf", 17, true, 0, buildTShift },
    { "t-mem", 25, true, 0, buildTMem },
    { "t-alu", 26, true, 0, buildTAlu },
    { "t-br",  14, true, kScratchPc + 20, buildTBranch },   // the loop sits before the subroutine
    { "t-stk", 18, true, 0, buildTStack },
    { "t-sw",  24, true, kScratchPc + 4, buildTSwitch },
};
constexpr uint32_t kProgramCount = sizeof(kPrograms) / sizeof(kPrograms[0]);
constexpr uint32_t kMaxWords = kScratchMax / 4;

char g_selfTestText[2048];
int  g_selfTestFails = 0;

} // namespace

// Run `words` of code living at kScratchPc to completion (the programs end in a
// branch to themselves) and report the resulting state.  `useJit` false gives the
// reference run out of the plain interpreter; `trace` (may be null) receives the
// logical PC, the CPSR and the cycle count of each individual dispatch, so a
// disagreement can be attributed to the instruction it starts at - and, when the
// flags differ at the end, to the instruction that set them.  `thumb` sets the T bit
// for both runs, and `loopAddr` names the terminating self-loop when it is not the
// last instruction.
Jit3::Snapshot Interpreter::jit3RunScratch(uint32_t words, bool useJit, ScratchTrace *trace,
                                          bool thumb, uint32_t loopAddr, uint32_t stepLimit) {
    const uint32_t savedRegs[16] = { registersUsr[0], registersUsr[1], registersUsr[2], registersUsr[3],
                                     registersUsr[4], registersUsr[5], registersUsr[6], registersUsr[7],
                                     registersUsr[8], registersUsr[9], registersUsr[10], registersUsr[11],
                                     registersUsr[12], registersUsr[13], registersUsr[14], registersUsr[15] };
    const uint32_t savedCpsr = cpsr;
    const uint32_t savedPc = *registers[15];

    for (int i = 0; i < 16; i++) registersUsr[i] = 0;
    registersUsr[13] = kScratchPc + 0x800;        // a stack that is not the game's
    registersUsr[14] = 0;
    cpsr = 0x1F | (thumb ? BIT(5) : 0);           // system mode, requested state, flags clear

    Jit3::setEnabled(useJit);
    Jit3::resetStats();
    *registers[15] = kScratchPc;
    flushPipeline();

    Jit3::Snapshot s{};
    const uint32_t step = thumb ? 2u : 4u;
    if (!loopAddr) loopAddr = kScratchPc + (words - 1) * step;
    for (uint32_t i = 0; i < 4096; i++) {
        const uint32_t here = *registers[15] - step;
        // Either stop at the terminating self-loop (one guest instruction at a time,
        // where both runs reach it at the same point), or run a caller-given number of
        // instructions and ignore the loop: a block that covers several instructions
        // cannot stop *at* the loop, so the reference has to be told how far to go.
        if (stepLimit) { if (i >= stepLimit) break; }
        else if (i && here == loopAddr) break;
        if (trace && s.steps < kJit3MaxSteps) {
            trace[s.steps].pc = here;
            trace[s.steps].cpsr = cpsr;
        }
        const int one = jit3StepOne();
        s.cycles += one;
        if (trace && s.steps < kJit3MaxSteps) trace[s.steps].cycles = (uint8_t)one;
        s.steps++;
    }

    for (int i = 0; i < 16; i++) s.regs[i] = registersUsr[i];
    s.cpsr = cpsr;
    s.pc = *registers[15] - ((cpsr & BIT(5)) ? 2u : 4u);
    // Instructions, not dispatches: the blocks' own count plus the ones the driver
    // handed back to the interpreter.  For the reference run this is its step count.
    s.instrs = useJit ? (Jit3::stats().instrs + Jit3::stats().solo) : s.steps;

    for (int i = 0; i < 16; i++) registersUsr[i] = savedRegs[i];
    cpsr = savedCpsr;
    *registers[15] = savedPc - ((savedCpsr & BIT(5)) ? 2u : 4u);   // put the pipeline back as found
    flushPipeline();
    Jit3::setEnabled(true);
    return s;
}

// Assemble each program at kScratchPc, run it twice (interpreter, then JIT) and
// compare everything the two runs left behind.  The interpreter's run is the
// reference, so a failure means the JIT disagrees with the emulator, not with
// some other idea of what the program should do.
int Interpreter::jit3SelfTest() {
    uint32_t code[kMaxWords];
    uint32_t saved[kMaxWords];
    uint32_t totalJit = 0, totalRef = 0, dispatched = 0, reference = 0;
    int fails = 0;
    int n = 0;

    // The JIT is what is being tested, but the reference run must not touch it.
    Jit3::resetStats();
    Jit3::invalidateAll();

    for (uint32_t p = 0; p < kProgramCount; p++) {
        const TestProg &prog = kPrograms[p];
        uint32_t words = prog.words;
        if (words > kMaxWords) continue;

        const uint32_t slots = words;                 // halfwords for a Thumb program
        for (uint32_t i = 0; i < slots; i++) {
            saved[i] = prog.thumb ? core->memory.read<uint16_t>(false, kScratchPc + i * 2)
                                  : core->memory.read<uint32_t>(false, kScratchPc + i * 4);
        }
        for (uint32_t i = 0; i < slots; i++) code[i] = 0;
        prog.build(kScratchPc, code);
        for (uint32_t i = 0; i < slots; i++) {
            if (prog.thumb) core->memory.write<uint16_t>(false, kScratchPc + i * 2, (uint16_t)code[i]);
            else            core->memory.write<uint32_t>(false, kScratchPc + i * 4, code[i]);
        }

        // (tracing stays as configured: the harness decides, see StartJit3Report)
        ScratchTrace refStep[kJit3MaxSteps] = {}, jitStep[kJit3MaxSteps] = {};
        Jit3::invalidateAll();                    // nothing compiled may survive the rewrite
        const uint32_t savedLimit = g_blockLimit;
        g_blockLimit = g_selfTestBlocks ? g_selfTestBlocks : 1;
        // Chaining would let one dispatch cover several blocks, and this test compares
        // against the interpreter's instruction stream - so it runs with the fast path
        // off.  The flag is saved and restored rather than cleared: leaving it off was
        // a real bug, and it is invisible in the test output (every count still
        // matched - there were simply no chain hops left).
        const bool savedChain = g_chain;
        Jit3::setChain(false);
        const Jit3::Snapshot jit = jit3RunScratch(words, true, jitStep, prog.thumb, prog.loopAddr);
        const Jit3::Stats jitStats = Jit3::stats();   // the reference run resets them
        Jit3::setChain(savedChain);
        g_blockLimit = savedLimit;                // the verifier may have asked for 1 too

        // With one instruction per dispatch the two runs reach the terminating
        // self-loop together and the reference can stop there by itself.  With a
        // longer block the JIT steps over it (a block cannot end at an arbitrary
        // halfword), so the reference is replayed for exactly as many guest
        // instructions as the JIT executed - otherwise the block's own charge for the
        // loop instruction looks like a cycle disagreement.
        const bool perStep = g_selfTestBlocks <= 1;
        Jit3::invalidateAll();
        const Jit3::Snapshot ref = jit3RunScratch(words, false, refStep, prog.thumb, prog.loopAddr);

        const Stats &st = jitStats;
        dispatched += st.entered;
        reference += ref.steps;
        totalJit += (uint32_t)jit.cycles;
        totalRef += (uint32_t)ref.cycles;

        // First step that disagrees - same PC, same entry flags, same cycle charge.
        // A pure cycle disagreement still leaves the registers equal, so this names
        // the instruction that is mispriced instead of just saying "9 vs 6"; a flag
        // disagreement names the instruction whose flags were left different.
        uint32_t firstBad = kJit3MaxSteps;
        for (uint32_t i = 0; perStep && i < kJit3MaxSteps; i++) {
            if (refStep[i].cycles != jitStep[i].cycles || refStep[i].pc != jitStep[i].pc ||
                refStep[i].cpsr != jitStep[i].cpsr) { firstBad = i; break; }
        }

        // 4096 is the runner's own cap: a block that landed *past* the loop and never
        // came back would otherwise compare two runs that both ran to the cap.
        // Instructions executed, not instructions covered: a block that ends in a
        // conditional branch also *contains* the fall-through, which did not execute,
        // so `ins=` below is a coverage figure and only the cycles/state verdict is
        // strict.  4096 is the runner's cap - a run that hit it never reached its
        // sentinel.
        bool ok = jit.steps < 4096 && (jit.cycles == ref.cycles) &&
                  (jit.pc == ref.pc) && (jit.cpsr == ref.cpsr);
        for (int i = 0; i < 16 && ok; i++) ok = (jit.regs[i] == ref.regs[i]);
        if (!ok) fails++;

        n += snprintf(g_selfTestText + n, sizeof(g_selfTestText) - n,
                      "p%u(%-5s) %s cyc=%d/%d disp=%u/%u ins=%u/%u pc=%08X/%08X cpsr=%08X/%08X%s",
                      p, prog.name, ok ? "ok  " : "FAIL",
                      jit.cycles, ref.cycles, st.entered, ref.steps, jit.instrs, ref.instrs,
                      jit.pc, ref.pc, jit.cpsr, ref.cpsr,
                      firstBad < kJit3MaxSteps ? " bad=" : "\n");
        // For a failing program (and only then - the text buffer has to hold every
        // verdict), dump the first few steps of both runs: the step index, the logical
        // PC, the entry CPSR and the cycle charge, JIT then interpreter.  A block that
        // covered more instructions than one shows up as a PC that jumped.
        if (!ok) {
            // One instruction per dispatch: the two lists line up, print them side by
            // side.  Longer blocks: the dispatch list is a list of *blocks*, so it is
            // printed as J= entries with the instruction count of each, and the
            // reference's own steps follow - which is the pair that shows a block
            // charging the wrong amount for the instructions it covered.
            if (perStep) {
                const uint32_t shown = ref.steps < 4 ? ref.steps : 4;
                for (uint32_t i = 0; i < shown && n < (int)sizeof(g_selfTestText) - 96; i++) {
                    n += snprintf(g_selfTestText + n, sizeof(g_selfTestText) - n,
                                  "  s%u j=%08X/%08X/%u r=%08X/%08X/%u\n", i,
                                  jitStep[i].pc, jitStep[i].cpsr, jitStep[i].cycles,
                                  refStep[i].pc, refStep[i].cpsr, refStep[i].cycles);
                }
            } else {
                for (uint32_t i = 0; i < kJit3MaxSteps && n < (int)sizeof(g_selfTestText) - 96; i++) {
                    if (!jitStep[i].pc && !jitStep[i].cycles && i) break;
                    n += snprintf(g_selfTestText + n, sizeof(g_selfTestText) - n,
                                  "  J%u pc=%08X cyc=%u\n", i, jitStep[i].pc, jitStep[i].cycles);
                }
                for (uint32_t i = 0; i < kJit3MaxSteps && n < (int)sizeof(g_selfTestText) - 96; i++) {
                    if (!refStep[i].pc && !refStep[i].cycles && i) break;
                    n += snprintf(g_selfTestText + n, sizeof(g_selfTestText) - n,
                                  "  R%u pc=%08X cyc=%u\n", i, refStep[i].pc, refStep[i].cycles);
                }
            }
        }
        if (firstBad < kJit3MaxSteps && n < (int)sizeof(g_selfTestText) - 128) {
            const uint32_t opw = prog.thumb ? jitFetch16(jitStep[firstBad].pc)
                                            : jitFetch32(jitStep[firstBad].pc);
            n += snprintf(g_selfTestText + n, sizeof(g_selfTestText) - n,
                          "%u cyc=%u/%u pc=%08X/%08X cpsr=%08X/%08X pc%d=%08X op=%08X\n",
                          firstBad, jitStep[firstBad].cycles, refStep[firstBad].cycles,
                          jitStep[firstBad].pc, refStep[firstBad].pc,
                          jitStep[firstBad].cpsr, refStep[firstBad].cpsr,
                          (int)prog.thumb, jitStep[firstBad].pc, opw);
        }

        for (uint32_t i = 0; i < slots; i++) {
            if (prog.thumb) core->memory.write<uint16_t>(false, kScratchPc + i * 2, (uint16_t)saved[i]);
            else            core->memory.write<uint32_t>(false, kScratchPc + i * 4, saved[i]);
        }
    }

    Jit3::invalidateAll();
    g_selfTestFails = fails;
    stageAppend("# selftest: programs=%u fail=%d jit_cycles=%u ref_cycles=%u dispatched=%u ref_steps=%u",
                kProgramCount, fails, totalJit, totalRef, dispatched, reference);
    {   // keep the per-program lines next to the header, in the same file
        char *line = g_selfTestText;
        while (*line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            stageAppend("%s", line);
            if (!nl) break;
            line = nl + 1;
        }
    }
    return fails;
}

const char *Interpreter::jit3SelfTestText() { return g_selfTestText; }
