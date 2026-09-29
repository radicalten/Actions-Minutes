import { useState } from 'react';

const Step = ({ num, title, color, children }: { num: string; title: string; color: string; children: React.ReactNode }) => {
  const [open, setOpen] = useState(true);
  const colors: Record<string, string> = {
    green:  'border-green-600 bg-green-950',
    blue:   'border-blue-600 bg-blue-950',
    yellow: 'border-yellow-600 bg-yellow-950',
    red:    'border-red-600 bg-red-950',
    purple: 'border-purple-600 bg-purple-950',
    orange: 'border-orange-600 bg-orange-950',
  };
  const headerColors: Record<string, string> = {
    green: 'text-green-300', blue: 'text-blue-300', yellow: 'text-yellow-300',
    red: 'text-red-300', purple: 'text-purple-300', orange: 'text-orange-300',
  };
  return (
    <div className={`border rounded-lg overflow-hidden mb-4 ${colors[color]}`}>
      <button
        onClick={() => setOpen(!open)}
        className="w-full flex items-center gap-3 px-4 py-3 text-left"
      >
        <span className={`font-bold text-xl w-8 text-center ${headerColors[color]}`}>{num}</span>
        <span className={`font-bold text-sm ${headerColors[color]}`}>{title}</span>
        <span className="ml-auto text-gray-400 text-xs">{open ? '▲' : '▼'}</span>
      </button>
      {open && (
        <div className="px-4 pb-4 border-t border-gray-700">
          {children}
        </div>
      )}
    </div>
  );
};

const Code = ({ children }: { children: string }) => (
  <pre className="bg-gray-950 border border-gray-700 rounded p-3 text-xs text-green-300 overflow-x-auto my-2 leading-relaxed whitespace-pre">
    <code>{children}</code>
  </pre>
);

const Note = ({ children }: { children: React.ReactNode }) => (
  <div className="bg-yellow-950 border border-yellow-700 rounded p-3 text-xs text-yellow-200 my-2">
    ⚠ {children}
  </div>
);

export default function IntegrationTab() {
  return (
    <div className="max-w-4xl">
      <div className="bg-blue-950 border border-blue-700 rounded-lg p-4 mb-6 text-xs text-blue-200 leading-relaxed">
        <strong className="text-blue-300">Integration Overview:</strong> The JIT slots into NooDS-Wii without touching any existing emulation logic except:
        (1) the ARM7 THUMB dispatch in <code>interpreter.cpp</code>,
        (2) the NooDS memory write functions for SMC invalidation, and
        (3) the Makefile / build system.
        Everything else is self-contained in <code>src/jit/</code>.
      </div>

      <Step num="1" title="Create the JIT directory structure" color="green">
        <p className="text-xs text-gray-300 mt-3 mb-2">Add a new <code className="text-green-300">src/jit/</code> directory alongside the existing NooDS-Wii source:</p>
        <Code>{`NooDS-Wii/
├── src/
│   ├── core/          ← existing NooDS emulator core (don't touch)
│   │   ├── arm/
│   │   │   ├── interpreter.h
│   │   │   ├── interpreter.cpp   ← MODIFY: add JIT dispatch hook
│   │   │   └── ...
│   │   └── memory/
│   │       └── ...               ← MODIFY: add JIT_SMC_GUARD to writes
│   └── jit/           ← NEW: create this directory
│       ├── JIT.h
│       ├── JITCache.h
│       ├── JITCache.cpp
│       ├── JITPPCEmitter.h
│       ├── JITCompiler.cpp
│       ├── JITTrampoline.S
│       ├── JITDifferential.h     (optional, debug only)
│       ├── JITDifferential.cpp   (optional, debug only)
│       └── JITDebugStateLog.h    (optional, debug only)
├── Makefile                      ← MODIFY: add JIT sources
└── ...`}</Code>
      </Step>

      <Step num="2" title="Static memory allocation (Wii homebrew)" color="blue">
        <p className="text-xs text-gray-300 mt-3 mb-2">
          On Wii, JIT memory must be statically allocated. The Wii does not support mprotect/mmap with exec permission —
          all allocated memory is executable by default under the Homebrew environment. Declare the buffers as global arrays:
        </p>
        <Code>{`// In your main platform file (e.g., src/wii/main.cpp or src/wii/platform.cpp)
#include "jit/JIT.h"
#include <stdint.h>

// -----------------------------------------------------------------------
// JIT static buffers — must be in MEM1 (first 24MB) for Broadway cache
// coherency to work reliably. Use __attribute__((aligned(32))) for DCBST.
// -----------------------------------------------------------------------
static uint32_t __attribute__((aligned(32)))
    jitArenaBuffer[JIT_ARENA_SIZE / 4];

static BasicBlock __attribute__((aligned(16)))
    jitBlockTable[HASH_TABLE_SIZE];

static BasicBlock* __attribute__((aligned(4)))
    jitSmcRegistry[SMC_MAP_SIZE];

static uint8_t __attribute__((aligned(4)))
    jitSmcPageFlags[SMC_MAP_SIZE];

// Call this once, before running any emulation
void JIT_PlatformInit() {
    jitCache.initialize(
        jitArenaBuffer,
        jitBlockTable,
        jitSmcRegistry,
        jitSmcPageFlags
    );
}`}</Code>
        <Note>
          Total static footprint: ~8MB (arena) + ~1MB (block table, 65536×16 bytes) + ~256KB (SMC registry) + ~64KB (SMC flags) ≈ <strong>9.3MB</strong>.
          Wii has 24MB MEM1. Ensure this doesn't push your total above the limit (NooDS itself uses significant memory for VRAM/WRAM/ROM buffers).
          Consider reducing HASH_TABLE_SIZE to 32768 if memory is tight.
        </Note>
      </Step>

      <Step num="3" title="Hook the ARM7 THUMB dispatch loop" color="purple">
        <p className="text-xs text-gray-300 mt-3 mb-2">
          In <code className="text-green-300">src/core/arm/interpreter.cpp</code>, inside the <code>runOpcode()</code> function's THUMB branch,
          add the JIT dispatch <strong>before</strong> the existing <code>thumbInstrs[]</code> table lookup:
        </p>
        <Code>{`// In Interpreter::runOpcode() — THUMB mode branch (cpsr & BIT(5))
// BEFORE: pipeline[1] = ...; return (this->*thumbInstrs[...])(opcode);

// -----------------------------------------------------------------------
// JIT DISPATCH HOOK
// -----------------------------------------------------------------------
#if defined(HW_RVL) || defined(HW_DOL)
#include "../../jit/JIT.h"

// Only JIT when:
//   - ARM7 in GBA mode (NDS ARM7 JIT is also possible but needs separate tuning)
//   - CPU is not halted / in exception
//   - JIT cache is initialized
if (arm7 && core->gbaMode && jitCache.isReady()) {
    uint32_t pc = *registers[15] - 2; // fetch address (before pipeline advance)
    BasicBlock* block = jitCache.getBlock(pc);

    if (!block) {
        // Cache miss: compile this trace
        block = JITCompileThumbTrace(pc, jitCache);
    }

    if (block && block->execute) {
        // Build the JITResult struct (16-byte aligned, on stack is fine)
        JITResult result __attribute__((aligned(32))) = {};

        // Pass flat gbaRegs array. In NooDS, registers[] is an array of
        // pointers to banked register arrays. For GBA mode, we can use
        // the user-mode register flat array directly:
        uint32_t* flatRegs = &registersUsr[0]; // base of all user regs

        // NooDS GBA mode condition flags — extract from cpsr:
        uint32_t flags[4] = {
            (cpsr >> 31) & 1, // N
            (cpsr >> 30) & 1, // Z
            (cpsr >> 29) & 1, // C
            (cpsr >> 28) & 1, // V
        };
        // Note: adapt CPUFlags layout to match your JITResult expectations.
        // Alternatively, keep flags[] as a simple uint32_t[4] in your state.

        // Execute the compiled trace
        ExecuteJITTrace(
            block->execute,
            &result,
            &busPrefetchCount,  // your prefetch state variable
            flatRegs,
            (CPUFlags*)flags,   // cast or adapt as needed
            arm7 ? core->memory.readMap7 : core->memory.readMap9A
        );

        // Apply JIT results back to NooDS state
        cpuTotalTicks += result.cycles;   // advance tick counter
        if (result.instructions > 0 || result.bailedOut) {
            // Restore PC from JIT result
            *registers[15] = result.nextPC + 2; // +2 for pipeline model
            cpsr = (cpsr & 0x0FFFFFFF)           // clear old flags
                 | ((flags[0] & 1) << 31)         // N
                 | ((flags[1] & 1) << 30)         // Z
                 | ((flags[2] & 1) << 29)         // C
                 | ((flags[3] & 1) << 28);         // V
            flushPipeline();
        }

        // SMC invalidation
        if (result.smcHit) {
            jitCache.invalidateSMCTarget(result.smcAddress);
        }

        // Return cycles consumed so NooDS scheduler can advance
        return (int)result.cycles;
    }
}
// JIT unavailable/miss — fall through to normal interpreter
#endif
// ---- existing interpreter path ----
pipeline[1] = ...;  // existing NooDS code
return (this->*thumbInstrs[...])(opcode);`}</Code>
        <Note>
          NooDS uses a pointer-indirection register model (<code>uint32_t* registers[16]</code>) rather than a flat array.
          The JIT needs a flat <code>uint32_t[16]</code> layout. In GBA mode, the user-mode register banks are always active,
          so <code>&amp;registersUsr[0]</code> is the safe flat address. For NDS ARM7, you need to handle banked registers
          by flushing banked state into a temporary flat array before calling ExecuteJITTrace.
        </Note>
      </Step>

      <Step num="4" title="Insert SMC guard into NooDS memory writes" color="red">
        <p className="text-xs text-gray-300 mt-3 mb-2">
          In NooDS's memory write functions (typically in <code className="text-green-300">src/core/memory/</code>),
          add <code>JIT_SMC_GUARD(address)</code> on every write to EWRAM (bank 0x02) and IWRAM (bank 0x03).
          These are the only RAM regions that can hold JIT-compiled code.
        </p>
        <Code>{`// In NooDS memory/memory.cpp or wherever write<T> is implemented:
#include "../jit/JIT.h"  // for JIT_SMC_GUARD

template <typename T>
void Memory::write(bool arm7, uint32_t addr, T value) {
    // ... existing NooDS memory routing ...
    switch (addr >> 24) {
        case 2: // EWRAM
            JIT_SMC_GUARD(addr);  // ← INSERT HERE
            // ... existing write ...
            break;
        case 3: // IWRAM
            JIT_SMC_GUARD(addr);  // ← INSERT HERE
            // ... existing write ...
            break;
        // ... other cases unchanged ...
    }
}`}</Code>
        <Note>
          JIT_SMC_GUARD is a no-op when NOODS_JIT==0 (non-Wii builds), so it's safe to add unconditionally.
          The smcPageFlags[] array is always valid (never null) even before jitCache.initialize() is called.
        </Note>
      </Step>

      <Step num="5" title="Adapt the flags interface" color="orange">
        <p className="text-xs text-gray-300 mt-3 mb-2">
          The JIT passes flags as a <code>uint32_t[4]</code> array (indices 0=N, 1=Z, 2=C, 3=V, each as a 0/1 value).
          NooDS stores GBA flags in the CPSR bits 31–28. Add a simple adapter:
        </p>
        <Code>{`// In your JIT dispatch site (or a small helper header):

// Extract CPSR flags into flat uint32_t[4] for JIT entry
inline void CPSR_to_FlatFlags(uint32_t cpsr, uint32_t flags[4]) {
    flags[0] = (cpsr >> 31) & 1; // N
    flags[1] = (cpsr >> 30) & 1; // Z
    flags[2] = (cpsr >> 29) & 1; // C
    flags[3] = (cpsr >> 28) & 1; // V
}

// Write flat flags back to CPSR after JIT execution
inline uint32_t FlatFlags_to_CPSR(uint32_t cpsr, const uint32_t flags[4]) {
    return (cpsr & 0x0FFFFFFF)
         | ((flags[0] & 1) << 31)
         | ((flags[1] & 1) << 30)
         | ((flags[2] & 1) << 29)
         | ((flags[3] & 1) << 28);
}

// The JIT's EnsureFlagsLoaded / FlushDirtyFlags use 84(r1) as
// a 'uint32_t*' (pointer to the flat flags[4] array).
// Make sure the pointer you stash at 84(r1) (via the trampoline's
// stw 7,84(r1)) is exactly &flags[0] — a uint32_t[4] on the stack
// is fine, since the trampoline keeps the frame alive for the duration.`}</Code>
      </Step>

      <Step num="6" title="Read page table adaptation (NooDS readMap)" color="yellow">
        <p className="text-xs text-gray-300 mt-3 mb-2">
          The JIT's memory guard template indexes into a <code>void* readPageTable[]</code> array, one pointer per 4KB page,
          exactly like NooDS's <code>readMap7[]</code> / <code>readMap9A[]</code>.
          Pass the correct map based on which CPU is being JIT-compiled:
        </p>
        <Code>{`// In the dispatch hook (Step 3):

// ARM7 in GBA mode: use readMap7 (or readMap9A for ARM9, but ARM9 JIT not done yet)
void* readTable = arm7 ? (void*)core->memory.readMap7
                        : (void*)core->memory.readMap9A;

ExecuteJITTrace(
    block->execute,
    &result,
    &busPrefetchCount,
    flatRegs,
    (CPUFlags*)flags,
    readTable        // passed as R8 on entry → mr 30,8 in trampoline
);

// In JITPPCEmitter.h — the read guard template:
// *emitPtr++ = PPC_SRWI(PPC_R11, PPC_R10, 24);     // bank = EA >> 24
// *emitPtr++ = PPC_SLWI(PPC_R11, PPC_R11, 2);      // × 4 (ptr size)
// *emitPtr++ = PPC_ADD(PPC_R11, PPC_R30_TABLE, R11);// table[bank]
// *emitPtr++ = PPC_LWZ(PPC_R12, PPC_R11, 0);       // page base
// NooDS readMap uses 4KB pages (addr >> 12); the JIT uses the same:
// *emitPtr++ = PPC_RLWINM(PPC_R11, PPC_R10, 0, 20, 31); // low 12 bits = page offset`}</Code>
        <Note>
            NooDS's <code>readMap7[]</code> / <code>readMap9A[]</code> are arrays of <code>uint8_t*</code>, indexed by
          <code>addr {">> 12"}</code> (one entry per 4KB page). Each non-null entry points to the start of the mapped memory region
          for that page. This is identical to VBA-GX's <code>gbaReadTable</code> layout used by the JIT.
        </Note>
      </Step>

      <Step num="7" title="Cycle accounting and scheduler integration" color="green">
        <p className="text-xs text-gray-300 mt-3 mb-2">
          NooDS uses a global event scheduler. The JIT must not execute past a pending event boundary.
          Add a <strong>yield guard</strong> in the JIT trace: before compiling each instruction, check if
          the accumulated static cycle count would exceed the yield threshold (JIT_YIELD_THRESHOLD cycles ≈ a few hundred).
        </p>
        <Code>{`// In JITCompiler.cpp, after each instruction's static cycle accumulation:
// (the JIT statically tracks a "chunkStaticCycles" estimate)

chunkStaticCycles += cyclesThisInsn;
if (chunkStaticCycles >= JIT_YIELD_THRESHOLD) {
    // Force a block boundary here — the linker stub will return to C++
    // and the scheduler will check for pending events before re-entering
    endBlock = true;
}

// The JIT also emits a runtime quota guard at the block epilogue that
// checks R3 (accumulated cycles) against a ceiling loaded at block entry:
// This prevents runaway block chaining from starving the scheduler.

// In the dispatcher (Step 3), after ExecuteJITTrace returns:
cpuTotalTicks += result.cycles;
// NooDS scheduler check: if cpuTotalTicks >= core->events[0].cycles, return
// (the existing NooDS dispatch loop already handles this when runOpcode returns)`}</Code>
      </Step>

      <Step num="8" title="Flush/initialize lifecycle" color="blue">
        <p className="text-xs text-gray-300 mt-3 mb-2">
          The JIT cache must be reset whenever the emulated system state is reset (power cycle, ROM change, save state load):
        </p>
        <Code>{`// In your NooDS-Wii reset/load handler:
void OnSystemReset() {
    jitCache.flushCache();
    // flushCache() re-emits the linker stub and clears all hash table slots.
    // No need to re-call initialize() — the arena/table pointers are preserved.
}

// On save state LOAD (the code that was JIT-compiled may no longer be valid):
void OnSaveStateLoad() {
    jitCache.flushCache(); // Force full recompile of all blocks
}

// On game ROM change / unmount:
void OnGameUnload() {
    jitCache.flushCache();
    // Optionally: jitCache.destroy() + JIT_PlatformInit() to fully reset
}`}</Code>
      </Step>

      <Step num="9" title="Enable differential testing (optional but recommended)" color="purple">
        <p className="text-xs text-gray-300 mt-3 mb-2">
          During development, build with <code>-DJIT_DIFFERENTIAL_TESTING</code> to run the JIT and C++ interpreter
          side-by-side and log any divergence. This is the most effective way to find flag computation bugs:
        </p>
        <Code>{`// Build flag:
# In Makefile:
CFLAGS += -DJIT_DIFFERENTIAL_TESTING

// In dispatcher (replaces the normal JIT execution block):
#ifdef JIT_DIFFERENTIAL_TESTING
#include "../../jit/JITDifferential.h"

uint16_t startOpcode = ...; // current THUMB opcode
int diffClockTicks = 0;
int diffResult = JIT_RunDifferentialThumbHook_Impl(
    pc, block, startOpcode, &diffClockTicks, thumbInstrs);

// diffResult:
//  -1 → proceed with normal JIT execution (sampled out or pc already checked)
//   1 → event boundary hit, return from dispatch loop
//   2 → handled (JIT executed), continue dispatch loop
//   3 → SMC or bail, interpreter handles this cycle
if (diffResult == 1) return 0;       // event boundary
if (diffResult == 2) return diffClockTicks; // JIT handled
if (diffResult == 3) { /* fall through to interpreter */ }
// -1: fall through to normal JIT path
#endif`}</Code>
      </Step>

      <Step num="10" title="ARM mode and mode-switching correctness" color="orange">
        <p className="text-xs text-gray-300 mt-3 mb-2">
          The JIT only handles THUMB mode. Several situations require exiting the JIT cleanly and returning to the C++ interpreter:
        </p>
        <div className="space-y-2 mt-2">
          {[
            ['BX with target bit0=0', 'Switches to ARM mode. JIT detects this in the BX handler (Format 5) and ends the block. The C++ interpreter continues in ARM mode.'],
            ['SWI / Software Interrupt', 'Always bails to C++ interpreter. NooDS HLE BIOS handles SWI — the JIT should never attempt to JIT across a SWI.'],
            ['Interrupt taken (IRQ/FIQ)', 'NooDS scheduler delivers interrupts between runOpcode() calls, so the JIT dispatch naturally exits at the next block boundary. No special handling needed.'],
            ['Undefined instruction', 'JIT bails on unrecognized opcodes. C++ interpreter takes the undefined instruction exception path.'],
            ['BLX to ARM', 'Format 19 BLX (H=01): JIT ends block before the BLX. C++ interpreter handles the ARM branch and mode switch.'],
            ['PSR transfer (MRS/MSR)', 'These are ARM-mode-only instructions. Not present in THUMB — no handling needed in THUMB JIT.'],
          ].map(([title, desc], i) => (
            <div key={i} className="flex gap-3 bg-gray-900 rounded p-3 border border-gray-700">
              <span className="text-yellow-300 font-bold text-xs whitespace-nowrap">{title}</span>
              <span className="text-xs text-gray-300">{desc}</span>
            </div>
          ))}
        </div>
      </Step>
    </div>
  );
}
