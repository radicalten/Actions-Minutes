/*
    v3-NooDS-Wii: jit3, the PowerPC (Broadway) recompiler for the ARM7/ARM9 cores.

    This header is the driver's public surface: the counters the per-frame report
    prints, the switch that turns translation off (used by the self-test to get a
    reference run out of the plain interpreter), and the flush that drops every
    compiled block.

    The *emitter* has its own, host-testable contract in armjit.hpp; this file
    deliberately knows nothing about it.
*/

#pragma once

#include <cstdint>

namespace Jit3 {


struct Stats {
    uint32_t blocks;      // blocks currently resident in the code arena
    uint32_t compiles;    // compileBlock() calls that produced code
    uint32_t refuses;     // dispatches answered by the interpreter (no block)
    uint32_t flushes;     // arena exhausted -> everything dropped and recollected
    uint32_t entered;     // compiled blocks entered
    uint32_t instrs;      // guest instructions retired inside compiled blocks
    uint32_t delegates;   // instructions handed back to the interpreter from a block
    uint32_t nocached;    // refusals answered from the negative cache (no recompile)
    uint32_t instrsDeleg; // of `instrs`, how many the interpreter executed inline
    uint32_t runs;        // jit3Run() calls: every guest instruction asks the driver
    uint32_t nowork;      // of those, the ones the interpreter executed (no block)
    uint32_t solo;        // guest instructions the *driver* handed back (refusals,
                          // halted, unmapped): they leave through the interpreter, so
                          // `instrs` alone is not how many the guest executed
};

// Block chaining: the emitted fast path counts its hops here (only when the
// harness asks for it - the counter costs five instructions per hop), and the
// switch lets a measurement run turn the whole thing off.
bool chain();
void setChain(bool on);
void setChainCount(bool on);
uint32_t chainHits();

const Stats &stats();
void resetStats();

// Refusal census (which classes the emitter turns away, and the hottest PCs),
// appended to the stage file.  Cheap enough to leave counting on always; the
// printing only happens when the report asks for it.
void dumpRefusals();

// Drop every compiled block.  Called when the guest memory map changes (the
// page pointers a block was compiled against stop meaning anything), when a new
// ROM is loaded, and before/after the startup self-test.
void invalidateAll();

// Translation switch.  false makes every dispatch fall through to the ARM
// interpreter, which is how the self-test obtains its reference run.
bool enabled();
void setEnabled(bool on);

// Compile-time diagnostics go to sd:/jit3-stage.txt (append, flushed per write,
// so the record survives a crash).  Cheap, but only meaningful for one run.
void setTrace(bool on);
bool trace();

// Differential verification: every compiled block is also run through the plain
// interpreter for the same instruction count and the two results are compared.
// The interpreter's answer is the one that survives, so the emulation stays on
// the reference track and the *first* divergence is what gets reported
// (sd:/jit3-verify.txt).  It forces one guest instruction per dispatch, so it is
// a correctness instrument, not a measurement configuration.
void setVerify(bool on);
bool verify();

// Send *several* guest instructions per dispatch in the self-test instead of one.
// The interpreter is still the reference (the runner replays the same number of
// steps), this only changes how much of the emitter's block composition the test
// covers: a conditional branch with a continuation, a fused BL and chaining all
// need more than one instruction to appear at all.  Reset to 0 after the test.
void setSelfTestBlocks(uint32_t n);

// Bytes of host code emitted so far, and the size of the arena.
uint32_t codeBytes();
uint32_t arenaBytes();

// What one self-test program leaves behind, so the JIT run and the interpreter
// run can be compared field by field.
struct Snapshot {
    uint32_t regs[16];
    uint32_t cpsr;
    uint32_t pc;
    int32_t  cycles;
    uint32_t steps;      // dispatches (JIT) or opcodes (interpreter)
    uint32_t instrs;     // guest instructions executed (JIT: blocks + handed back)
};

} // namespace Jit3
