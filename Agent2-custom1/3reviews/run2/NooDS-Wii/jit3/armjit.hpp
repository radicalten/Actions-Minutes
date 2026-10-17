/*
    v3-NooDS-Wii: jit3 front-end (host-testable).

    This header intentionally has no dependency on NooDS/GEKKO headers.  Every
    decision about *what host code to emit* lives behind it, so the whole
    front-end can be compiled on a PC and differentially tested against a
    reference ARM model (see tools/host).  jit3.cpp supplies the addresses and
    offsets.
*/

#pragma once

#include <cstdint>

namespace Jit3 {

enum HelperId {
    H_Load8 = 0, H_Load16, H_Load32, H_Store8, H_Store16, H_Store32,
    H_LoadS8, H_LoadS16, // signed variants store a sign-extended value in r3
    H_InterpOne,        // execute exactly one guest instruction at a given PC
    HelperCount
};

// Host-side layout/entry-point description.
// One entry of the driver's block table, as the emitted chain probe reads it.
// It lives here, not in jit3.cpp, so that the emitter and the driver cannot
// disagree about the layout; the driver's JitBlockEntry holds one of these first.
struct ChainEntry {
    uint32_t pc;
    uint32_t gen;
    uint32_t warmPtr;    // absolute host address of the block's "registers resident" entry
    uint32_t page;       // host address of the guest page the block was compiled against
};

struct Cfg {
    bool arm7 = false;
    int32_t offRegsUsr   = 0;    // uint32_t registersUsr[16]
    int32_t offRegsPtr   = 0;    // uint32_t *registers[32]
    int32_t offCpsr      = 0;
    int32_t offHalted    = 0;
    int32_t offCore      = 0;
    uintptr_t helper[HelperCount] = {};
    uint32_t maxBlockInstrs = 24;
    uint32_t maxBlockWords  = 1200;
    bool haltCheckOnIoStore = true;   // ARM7: an unmapped store may hit HALTCNT

    // ---- block chaining ----------------------------------------------------
    // A block's exit can jump straight into the successor's warm entry (the point
    // past its prologue, where every guest register is already resident) instead of
    // returning to the C++ driver - which is where a dispatch's cost lives (the
    // table probe, the call, the prologue and the epilogue, per *block*).  The
    // emitted probe reads the driver's block table itself, so `chainTable`/
    // `chainMask`/`chainStride` describe that table, `chainGen` is the memory
    // generation the code was compiled in (a remap bumps it, so every stale probe
    // then misses) and `chainBudget` is how many guest cycles one chain may cover
    // before it has to hand control back to the driver.
    bool chain = false;
    uintptr_t chainTable = 0;
    uint32_t chainMask = 0;
    uint32_t chainStride = 0;
    uintptr_t chainArena = 0;
    uint32_t chainGen = 0;
    int32_t chainBudget = 0;
    uint32_t *chainCounter = nullptr; // incremented by the fast path when set
};

// Instruction fetch for the compiler (host callback; guest memory is LSB-first).
struct GuestMem {
    void* ctx = nullptr;
    uint32_t (*read16)(void* ctx, uint32_t addr) = nullptr;
    uint32_t (*read32)(void* ctx, uint32_t addr) = nullptr;

    uint32_t u32(uint32_t a) const { return read32 ? read32(ctx, a) : 0; }
    uint16_t u16(uint32_t a) const { return (uint16_t)(read16 ? read16(ctx, a) : 0); }
};

struct BlockInfo {
    uint32_t startPc   = 0;
    uint32_t exitPc    = 0;      // guest PC the dispatcher must resume at
    uint32_t instrCount = 0;
    uint32_t cycles    = 0;      // guest cycles consumed by the compiled block
    int32_t  warmOffset = 0;     // word offset of the "already resident" entry point
    int32_t  bodyOffset = 0;     // word offset of the first guest instruction
    bool     thumb = false;
    bool     delegated = false;  // block is a single instruction handed to the interpreter
    uint32_t delegInstrs = 0;    // how many words of this block go to the interpreter
    // The delegated words themselves (capped): the refusal census counts what the
    // emitter turns away, which is a different question from what it hands to the
    // interpreter *after* accepting the block, and the second one is what decides
    // whether the block boundary earns its keep.
    uint32_t delegWords[8] = {};
    uint32_t delegWordCount = 0;
    bool     endsWithBranch = false;
    bool     branchIsLink = false;
    bool     branchConditional = false;
    uint32_t branchTarget = 0;   // static target when known
};

// Compile one block starting at `pc`.  Returns false if the first instruction
// cannot be translated (the caller must then execute it in the interpreter).
bool compileBlock(const Cfg& cfg, const GuestMem& mem, uint32_t pc, bool thumb,
                  uint32_t* out, uint32_t cap, uint32_t& outWords, BlockInfo& info);

// Prove-or-delegate hook exposed for tooling: true when `op` would be translated
// natively (and would end the block if `terminates` comes back true).
bool canTranslateOp(const Cfg& cfg, uint32_t op, bool& terminates);

// Guest register indices as seen by the emitter (useful for the glue/tests).
enum GuestReg { G_R0 = 0, G_SP = 13, G_LR = 14, G_PC = 15 };

} // namespace Jit3
