import { useState } from 'react';

const Code = ({ children }: { children: string }) => (
  <pre className="bg-gray-950 border border-gray-700 rounded p-3 text-xs text-green-300 overflow-x-auto my-2 leading-relaxed whitespace-pre">
    <code>{children}</code>
  </pre>
);

const Section = ({ title, children }: { title: string; children: React.ReactNode }) => (
  <div className="mb-8">
    <h2 className="text-lg font-bold text-green-400 border-b border-green-800 pb-1 mb-4">{title}</h2>
    {children}
  </div>
);

const MAKEFILE = `#---------------------------------------------------------------------------------
# NooDS-Wii Makefile — with ARMv4→PPC JIT support
# Based on: https://github.com/radicalten/NooDS-Wii
# JIT: modeled after dborth/vbagx JIT architecture
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPPC)),)
  $(error "Please set DEVKITPPC in your environment.")
endif
ifeq ($(strip $(DEVKITPRO)),)
  $(error "Please set DEVKITPRO in your environment.")
endif

include $(DEVKITPPC)/wii_rules

#---------------------------------------------------------------------------------
TARGET      := boot
BUILD       := build
SOURCES     := src/wii src/jit
INCLUDES    := src/jit src/core

# NooDS core sources (all existing cpp files from NooDS-Wii)
COREFILES  := src/core/core.cpp \\
              src/core/save_states.cpp \\
              src/core/settings.cpp \\
              src/core/arm/interpreter.cpp \\
              src/core/arm/interpreter_alu.cpp \\
              src/core/arm/interpreter_branch.cpp \\
              src/core/arm/interpreter_lookup.cpp \\
              src/core/arm/interpreter_transfer.cpp \\
              src/core/arm/timers.cpp \\
              src/core/arm/cp15.cpp

# JIT sources — new
JITFILES   := src/jit/JITCache.cpp \\
              src/jit/JITCompiler.cpp

# Assembly trampoline
JITASM     := src/jit/JITTrampoline.S

# Optional debug sources (comment out for release)
# JITDEBUG := src/jit/JITDifferential.cpp \\
#             src/jit/JITDebugStateLog.cpp

SOURCES_CPP := $(COREFILES) $(JITFILES) $(JITDEBUG)
SOURCES_ASM := $(JITASM)

#---------------------------------------------------------------------------------
# Compiler / linker flags
#---------------------------------------------------------------------------------
CFLAGS   := -g -O2 -Wall \\
            -DHW_RVL \\
            -DNOODS_JIT=1 \\
            -ffunction-sections \\
            -fdata-sections \\
            $(MACHDEP)

# Uncomment for differential testing:
# CFLAGS += -DJIT_DIFFERENTIAL_TESTING

# JIT compile flags — force PowerPC 750 target for Broadway
JITCFLAGS := $(CFLAGS) -mcpu=750 -mhard-float

CXXFLAGS := $(CFLAGS) -std=c++17 -fno-exceptions -fno-rtti

ASFLAGS  := -mregnames -mcpu=750

LDFLAGS  := -g $(MACHDEP) -Wl,-Map,$(notdir $@).map \\
            -Wl,--gc-sections

LIBS     := -lwiiuse -lbte -logc -lm -lz

#---------------------------------------------------------------------------------
# Pattern rules for JIT assembly
#---------------------------------------------------------------------------------
$(BUILD)/JITTrampoline.o: src/jit/JITTrampoline.S
\t@echo "[AS] $<"
\t$(CC) $(ASFLAGS) -c $< -o $@

#---------------------------------------------------------------------------------
# Default build target
#---------------------------------------------------------------------------------
all: $(TARGET).dol

$(TARGET).elf: $(SOURCES_CPP:.cpp=.o) $(SOURCES_ASM:.S=.o)
\t$(LD) $^ $(LDFLAGS) $(LIBS) -o $@

$(TARGET).dol: $(TARGET).elf
\t$(ELF2DOL) $< $@

clean:
\trm -rf $(BUILD) *.elf *.dol

.PHONY: all clean`;

const TOOLCHAIN = `# devkitPPC setup (Ubuntu/Debian/WSL)
# 1. Install devkitPro pacman
wget https://apt.devkitpro.org/install-devkitpro-pacman
chmod +x install-devkitpro-pacman
sudo ./install-devkitpro-pacman

# 2. Install Wii development tools
sudo dkp-pacman -S wii-dev

# 3. Set environment variables (add to ~/.bashrc)
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
export PATH=$PATH:$DEVKITPPC/bin:$DEVKITPRO/tools/bin

# 4. Verify
powerpc-eabi-gcc --version
# Should output: powerpc-eabi-gcc (devkitPPC) 13.x.x

# 5. Build
cd NooDS-Wii
make -j$(nproc)`;

const VERIFY_PPC = `# Verify JIT code is actually PPC (not x86):
powerpc-eabi-objdump -d build/JITTrampoline.o | head -40
# Should show: mflr, stwu, stmw, lwz, mr, mtctr, bctr etc.

powerpc-eabi-objdump -d build/JITCache.o | grep -A5 "flushCache"
# Should show linker stub emission code

powerpc-eabi-objdump -d build/JITCompiler.o | head -60
# Should show the trace compiler entry point

# Verify static buffer sizes
powerpc-eabi-nm --print-size build/NooDS-Wii.elf | grep jitArena
# Should show ~8MB symbol

# Check final binary size
ls -lh boot.dol`;

const DEBUGFLAGS = `# Debug build (logging + differential testing):
make CFLAGS="-g -O0 -DHW_RVL -DNOODS_JIT=1 \\
             -DJIT_DIFFERENTIAL_TESTING \\
             -DJIT_STATE_LOGGING \\
             -DPROFILING=1"

# Release build (max performance):
make CFLAGS="-O3 -DHW_RVL -DNOODS_JIT=1 \\
             -fomit-frame-pointer \\
             -funroll-loops \\
             -mcpu=750 \\
             -mhard-float"

# GCN (GameCube) build:
make CFLAGS="-g -O2 -DHW_DOL -DNOODS_JIT=1"`;

const issues = [
  {
    problem: 'Compiled code crashes immediately on first block execution',
    cause: 'Cache coherency: the Wii CPU fetches instructions from I-cache which may not reflect what you wrote to D-cache.',
    fix: `// After every emitted block, BEFORE the first execution:
JIT_CODE_MARK_DIRTY(blockStart, emittedBytes);
// Expands to: DCStoreRange + ICInvalidateRange
// Also after every linker stub self-patch:
DCStoreRange(patchedWord, 4);
__asm__ volatile ("sync");
ICInvalidateRange(patchedWord, 4);
__asm__ volatile ("sync; isync");`
  },
  {
    problem: 'JIT produces wrong flag values (N/Z/C/V diverge from interpreter)',
    cause: 'PPC_MERGE_FLAG_BIT shift calculation or EnsureFlagsLoaded not called before a flag write.',
    fix: `// Enable differential testing:
-DJIT_DIFFERENTIAL_TESTING
// The JIT_DIFFERENTIAL_THUMB_HOOK will log the exact first divergence.
// Check: does EnsureFlagsLoaded() get called before every EmitFlagBit()?
// Check: PPC_MERGE_FLAG_BIT shift math — (sh + 31 - targetBit) & 31`
  },
  {
    problem: 'Linker stub self-patch causes hang or illegal instruction',
    cause: 'Branch offset out of range (>±32MB), or cache not flushed after patch.',
    fix: `// Verify the relative branch fits in 26-bit signed offset:
// Max range: ±32MB. The 8MB arena should always be in range.
// If blocks are in MEM2 (>64MB), use an absolute branch via CTR instead:
*e++ = PPC_LIS(PPC_R12, targetAddr >> 16);
*e++ = PPC_ORI(PPC_R12, PPC_R12, targetAddr & 0xFFFF);
*e++ = PPC_MTCTR(PPC_R12);
*e++ = PPC_BCTR();`
  },
  {
    problem: 'Arena fills up and flushCache() thrashes (recompiling same blocks repeatedly)',
    cause: 'Block table too small relative to ROM\'s working set. Frequent evictions cause the same PCs to be recompiled.',
    fix: `// Increase HASH_TABLE_SIZE (doubles memory use per doubling):
#define HASH_TABLE_SIZE 131072  // 128K slots instead of 64K
// Or increase JIT_ARENA_SIZE if MEM1 allows.
// Profile: add PROFILER counters to count flushCache() calls per second.`
  },
  {
    problem: 'SMC guard causes infinite recompilation loop',
    cause: 'Code is being written to EWRAM/IWRAM at the same address as code being executed (typical for BIOS or self-patching game code).',
    fix: `// The JIT correctly handles this: invalidateSMCTarget patches the block to bail,
// then the next dispatch compiles a fresh block (which may also get SMC'd).
// If the loop is truly infinite, the game is continuously self-modifying.
// In that case, disable JIT for that specific PC via the "don't JIT" slot:
cache.registerBlock(problematicPC, 1, nullptr);  // force fallback`
  },
  {
    problem: 'GBA mode: wrong initial register values on first trace',
    cause: 'flatRegs pointer passed to ExecuteJITTrace doesn\'t match what the JIT expects (NooDS banked pointer model).',
    fix: `// In GBA mode, only user-mode registers are ever active.
// Pass &registersUsr[0] — NOT &registers[0] (which is an array of pointers!):
uint32_t* flatRegs = &registersUsr[0];
// Verify: registersUsr[15] should equal *registers[15] in GBA mode.
// Add an assert: assert(&registersUsr[0] == registers[0]);  // R0 pointer check`
  },
];

export default function BuildTab() {
  const [activeSection, setActiveSection] = useState(0);
  const sections = ['Makefile', 'Toolchain', 'Verify', 'Build Flags', 'Troubleshooting'];

  return (
    <div>
      {/* Tab sub-nav */}
      <div className="flex gap-2 mb-6 flex-wrap">
        {sections.map((s, i) => (
          <button
            key={i}
            onClick={() => setActiveSection(i)}
            className={`px-4 py-2 text-xs rounded font-medium transition-colors ${
              activeSection === i
                ? 'bg-green-700 text-white'
                : 'bg-gray-800 text-gray-400 hover:bg-gray-700 hover:text-gray-200'
            }`}
          >
            {s}
          </button>
        ))}
      </div>

      {activeSection === 0 && (
        <Section title="📄 Makefile — NooDS-Wii with JIT">
          <p className="text-xs text-gray-400 mb-3 leading-relaxed">
            Add the JIT source files to your existing NooDS-Wii Makefile. The key additions are:
            the <code className="text-green-300">src/jit/</code> directory in SOURCES,
            the <code className="text-green-300">JITTrampoline.S</code> assembly source (needs its own pattern rule),
            and the <code className="text-green-300">-DNOODS_JIT=1 -DHW_RVL -mcpu=750</code> compiler flags.
          </p>
          <Code>{MAKEFILE}</Code>
        </Section>
      )}

      {activeSection === 1 && (
        <Section title="🛠 Toolchain Setup (devkitPPC)">
          <p className="text-xs text-gray-400 mb-3">
            The JIT requires devkitPPC (PowerPC ELF cross-compiler targeting the Broadway/Gekko PPC 750).
          </p>
          <Code>{TOOLCHAIN}</Code>
          <div className="bg-blue-950 border border-blue-700 rounded p-3 text-xs text-blue-200 mt-3">
            <strong>Compiler version note:</strong> devkitPPC uses GCC targeting <code>powerpc-eabi</code>.
            The JIT assembly (<code>JITTrampoline.S</code>) uses standard PPC mnemonics with <code>-mregnames</code>
            (so <code>r14</code> instead of <code>14</code>). devkitPPC's GCC version 12+ supports this natively.
          </div>
        </Section>
      )}

      {activeSection === 2 && (
        <Section title="✅ Verify the Build">
          <p className="text-xs text-gray-400 mb-3">
            After building, verify the JIT compiled correctly and the binary looks reasonable:
          </p>
          <Code>{VERIFY_PPC}</Code>
          <div className="mt-4">
            <h3 className="text-green-300 font-bold text-sm mb-2">Expected output checks:</h3>
            <div className="space-y-2">
              {[
                ['JITTrampoline.o disassembly', 'Should show: ExecuteJITTrace: mflr 0 / stw 0,4(1) / stwu 1,-128(1) / stmw 14,8(1) / ...'],
                ['JITCache.o disassembly', 'Should show PPC_LIS/ORI/SRWI/XOR/RLWINM/CMPW/BNE in the flushCache() function (linker stub emitter)'],
                ['JITCompiler.o disassembly', 'Should show JITCompileThumbTrace function with many memory writes (emitting PPC words to arena)'],
                ['boot.dol size', 'Expect ~2-4MB larger than without JIT (JIT source code is large, JIT data buffers are BSS not in .dol)'],
                ['Runtime check', 'Boot on Wii, load a GBA ROM. GBA mode should show improved FPS vs pure interpreter. Check Gecko log for JIT initialization message.'],
              ].map(([check, desc], i) => (
                <div key={i} className="flex gap-3 bg-gray-900 rounded p-2 border border-gray-700">
                  <span className="text-green-300 font-bold text-xs whitespace-nowrap">✓ {check}</span>
                  <span className="text-xs text-gray-400">{desc}</span>
                </div>
              ))}
            </div>
          </div>
        </Section>
      )}

      {activeSection === 3 && (
        <Section title="🚩 Build Flags Reference">
          <Code>{DEBUGFLAGS}</Code>
          <div className="mt-4 overflow-x-auto">
            <table className="w-full text-xs border-collapse">
              <thead>
                <tr className="bg-gray-800 text-gray-300">
                  <th className="border border-gray-700 px-3 py-2 text-left">Flag</th>
                  <th className="border border-gray-700 px-3 py-2 text-left">Effect</th>
                  <th className="border border-gray-700 px-3 py-2 text-left">Use When</th>
                </tr>
              </thead>
              <tbody>
                {[
                  ['-DNOODS_JIT=1',               'Enables JIT compilation paths',                            'Always (required)'],
                  ['-DHW_RVL',                    'Wii target (Broadway PPC)',                                'Building for Wii'],
                  ['-DHW_DOL',                    'GameCube target (Gekko PPC)',                              'Building for GCN'],
                  ['-DJIT_DIFFERENTIAL_TESTING',  'Side-by-side JIT vs interpreter comparison + logging',    'Finding flag/reg bugs'],
                  ['-DJIT_STATE_LOGGING',         'Full CPU state log to SD card per block',                 'Tracing divergence offline'],
                  ['-DPROFILING=1',               'Enables PROFILER_INC/ADD counters (cache hits/misses/etc)','Performance analysis'],
                  ['-mcpu=750',                   'Target Broadway PPC 750 instruction set',                  'Always for JIT files'],
                  ['-mregnames',                  'Assembler: use r0–r31 names (for JITTrampoline.S)',        'Always for .S file'],
                  ['-O2 / -O3',                   'Optimize the JIT compiler itself (not the emitted code)', 'Release builds'],
                  ['-O0',                         'No optimization — easier to debug JIT compiler bugs',      'Debug builds'],
                  ['-DWIIU_JIT=1',                'Enable Wii U variant (no self-patching, larger arena)',    'Wii U / Cafe port'],
                ].map(([flag, effect, when], i) => (
                  <tr key={i} className={i % 2 === 0 ? 'bg-gray-900' : 'bg-gray-950'}>
                    <td className="border border-gray-700 px-3 py-2 text-green-300 font-mono">{flag}</td>
                    <td className="border border-gray-700 px-3 py-2 text-gray-300">{effect}</td>
                    <td className="border border-gray-700 px-3 py-2 text-yellow-300">{when}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </Section>
      )}

      {activeSection === 4 && (
        <Section title="🔧 Troubleshooting">
          <div className="space-y-4">
            {issues.map((issue, i) => (
              <div key={i} className="bg-gray-900 border border-gray-700 rounded-lg overflow-hidden">
                <div className="bg-red-950 border-b border-red-800 px-4 py-2">
                  <span className="text-red-300 font-bold text-sm">Problem: {issue.problem}</span>
                </div>
                <div className="px-4 py-3">
                  <div className="text-xs text-gray-400 mb-2">
                    <span className="text-yellow-300 font-bold">Cause: </span>{issue.cause}
                  </div>
                  <Code>{issue.fix}</Code>
                </div>
              </div>
            ))}
          </div>

          <div className="mt-6 bg-blue-950 border border-blue-700 rounded-lg p-4">
            <h3 className="text-blue-300 font-bold text-sm mb-3">Performance Expectations</h3>
            <div className="grid grid-cols-1 md:grid-cols-2 gap-4 text-xs text-gray-300">
              <div>
                <div className="text-green-300 font-bold mb-1">GBA mode (Wii ~729 MHz):</div>
                <ul className="space-y-1 text-gray-400">
                  <li>• Pure interpreter: 10–35 FPS (current NooDS-Wii)</li>
                  <li>• With THUMB JIT: target 45–60 FPS for most games</li>
                  <li>• ROM-only games (no BIOS HLE): higher hit rate</li>
                  <li>• Games with heavy ARM mode usage: limited gain</li>
                </ul>
              </div>
              <div>
                <div className="text-green-300 font-bold mb-1">Key factors:</div>
                <ul className="space-y-1 text-gray-400">
                  <li>• THUMB code ratio (most GBA games: 70–90% THUMB)</li>
                  <li>• Cache hit rate (depends on game's working set)</li>
                  <li>• Memory access pattern (ROM vs EWRAM vs IWRAM)</li>
                  <li>• JIT trace max size (42 instrs balances compile time vs code quality)</li>
                </ul>
              </div>
            </div>
          </div>
        </Section>
      )}
    </div>
  );
}
