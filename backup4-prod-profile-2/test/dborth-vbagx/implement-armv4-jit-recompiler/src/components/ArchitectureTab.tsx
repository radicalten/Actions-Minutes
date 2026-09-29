import { useState } from 'react';

const Box = ({ title, children, color = 'green' }: { title: string; children: React.ReactNode; color?: string }) => {
  const colors: Record<string, string> = {
    green:  'border-green-600 bg-green-950',
    blue:   'border-blue-600 bg-blue-950',
    yellow: 'border-yellow-600 bg-yellow-950',
    red:    'border-red-600 bg-red-950',
    purple: 'border-purple-600 bg-purple-950',
    gray:   'border-gray-600 bg-gray-900',
    orange: 'border-orange-600 bg-orange-950',
  };
  const titleColors: Record<string, string> = {
    green:  'text-green-300',
    blue:   'text-blue-300',
    yellow: 'text-yellow-300',
    red:    'text-red-300',
    purple: 'text-purple-300',
    gray:   'text-gray-300',
    orange: 'text-orange-300',
  };
  return (
    <div className={`border rounded-lg p-4 ${colors[color]}`}>
      <h3 className={`font-bold text-sm mb-2 ${titleColors[color]}`}>{title}</h3>
      {children}
    </div>
  );
};

const Arrow = ({ label, direction = 'down' }: { label?: string; direction?: 'down' | 'right' }) => (
  <div className={`flex ${direction === 'right' ? 'flex-row items-center' : 'flex-col items-center'} my-1`}>
    {direction === 'down' ? (
      <>
        <div className="w-0.5 h-4 bg-gray-500" />
        <div className="text-gray-400 text-xs">{label}</div>
        <div className="text-gray-400 text-lg leading-none">▼</div>
      </>
    ) : (
      <>
        <div className="h-0.5 w-4 bg-gray-500" />
        {label && <div className="text-gray-400 text-xs mx-1">{label}</div>}
        <div className="text-gray-400 text-lg leading-none">▶</div>
      </>
    )}
  </div>
);

const Section = ({ title, children }: { title: string; children: React.ReactNode }) => (
  <div className="mb-8">
    <h2 className="text-lg font-bold text-green-400 border-b border-green-800 pb-1 mb-4">{title}</h2>
    {children}
  </div>
);

const phases = [
  {
    num: '1',
    title: 'Dispatch Loop Entry',
    color: 'gray',
    desc: 'NooDS-Wii ARM7 interpreter calls into JIT dispatch at each THUMB fetch. The JIT hash table is probed with ((pc>>1)^(pc>>13)) & (HASH_TABLE_SIZE-1).',
    detail: 'On a cache miss, JITCompileThumbTrace() is called. On a hit, if execute != nullptr, ExecuteJITTrace() is invoked. A "don\'t JIT" fallback stub (execute==nullptr, length>0) skips compilation permanently for that PC.'
  },
  {
    num: '2',
    title: 'Trace Compilation (JITCompiler.cpp)',
    color: 'blue',
    desc: 'Walk THUMB instructions one at a time from startPC, up to JIT_TRACE_MAX_INSTRUCTIONS (42). Each recognized opcode emits native PPC words into the arena bump allocator.',
    detail: 'Unrecognized opcodes or unsafe edge cases cause an immediate bail. The compiler never guesses — it falls back silently to C++ interpreter. Three parallel state trackers: regCache[15] (lazy GBA R0–R14 in PPC R15–R28), packed flags (PPC_REG_FLAGS/R6), and deferred bailout list.'
  },
  {
    num: '3',
    title: 'Lazy Register Allocation',
    color: 'purple',
    desc: 'GBA R0–R14 are faulted into host PPC R15–R28 on first use via FindOrAllocateHostReg(), using LRU eviction when all 14 slots are full.',
    detail: 'An allocatedHostRegsMask bitmask tracks which PPC regs are live. On eviction, the oldest (lowest currentAge) dirty register is spilled via STW back to gbaRegs[]. Registers are only spilled to memory at eviction or block epilogue — never mid-block otherwise.'
  },
  {
    num: '4',
    title: 'Packed Flag Management',
    color: 'orange',
    desc: 'N/Z/C/V are packed into the top nibble of PPC R6 (PPC_REG_FLAGS). Bits 0–3 (IBM numbering = bits 31–28 conventional) hold N, Z, C, V respectively.',
    detail: 'EnsureFlagsLoaded() faults in all 4 flags from memory on first use per block. EmitFlagBit() and EmitFlagConstant() write flags via PPC_MERGE_FLAG_BIT (rlwimi). PPC_EXTRACT_FLAG_BIT reads a single flag as 0/1 in bit 31 of a scratch register.'
  },
  {
    num: '5',
    title: 'Deferred Bailout Architecture',
    color: 'red',
    desc: 'Guard failures (bad bank, null page, SMC hit, unsupported BX target) emit only a forward branch at the guard site. The full state is pre-flushed before the guard branch.',
    detail: 'A second pass at block-end generates compact bailout landing pads in the unused arena space after the main block body. Each pad sets JITResult.bailedOut=1, stores cycles+PC, and jumps to ExecuteJITTrace_Return. Max 256 deferred bailouts per block; overflow traps with PPC TWI.'
  },
  {
    num: '6',
    title: 'Block Chaining (JITCache Linker Stub)',
    color: 'yellow',
    desc: 'Every block\'s normal epilogue branches to jitCache.linkerStubAddress (re-emitted on flushCache()). The stub hashes the next PC, does a direct-mapped lookup, and either patches the caller\'s branch word in-place or returns to C++.',
    detail: 'Self-patching: the stub reads LR−4 (the caller\'s branch instruction), computes the relative offset to the target block, and ORI\'s 0x4800 into the top to form a direct PPC branch (B opcode). DCBST+SYNC+ICBI+SYNC+ISYNC maintains coherency. Disabled on Wii U (MEM2 cache model differs).'
  },
  {
    num: '7',
    title: 'SMC Guard & Invalidation',
    color: 'red',
    desc: 'Every guest store to EWRAM (bank 2) or IWRAM (bank 3) checks smcPageFlags[] (1KB page granularity). If a compiled block lives on the written page, invalidateSMCTarget() patches the block\'s first instruction to an unconditional bail branch.',
    detail: 'smcRegistry[] is an intrusive per-page linked list of BasicBlock*. The block\'s own next pointer is used (nextSMC field), so no heap allocation per SMC registration. Eviction in registerBlock() carefully unlinks from the SMC chain before overwriting the slot.'
  },
  {
    num: '8',
    title: 'ABI Trampoline (JITTrampoline.S)',
    color: 'green',
    desc: 'ExecuteJITTrace() saves host non-volatile registers R14–R31 to a 128-byte stack frame, stashes all pointer args at fixed offsets (gbaRegs@80, flags@84, outResult@88, busPrefetchCount@92), eagerly loads PC into R29 and prefetch count into R5, then bctr into the compiled block.',
    detail: 'ExecuteJITTrace_Return (the linker stub\'s cache-miss/fallback landing pad) writes cycles (R3) and nextPC (R4) into JITResult, flushes R5→*busPrefetchCount, R29→gbaRegs[15], then restores R14–R31 and LR and returns. GBA R0–R14 and flags must already be flushed by the JIT block before any exit.'
  },
];

const fileMap = [
  { file: 'JIT.h',                role: 'Top-level public interface. JITResult struct, JITCompileThumbTrace() forward decl, ExecuteJITTrace declarations, JITWriteScope RAII, JIT_TRACE_MAX_INSTRUCTIONS.' },
  { file: 'JITCache.h/.cpp',      role: '8MB arena bump allocator, 65536-bucket direct-mapped block hash table, linker stub emitter (flushCache), SMC registry (smcPageFlags[], smcRegistry[]), block registration/eviction, invalidateSMCTarget.' },
  { file: 'JITPPCEmitter.h',      role: 'All PPC instruction encoding macros (PPC_ADD, PPC_LWZ, PPC_RLWIMI, PPC_BEQ, PPC_DCBST, etc.), register name defines, packed-flag bit layout, PPC_MERGE_FLAG_BIT / PPC_EXTRACT_FLAG_BIT.' },
  { file: 'JITCompiler.cpp',      role: 'Main compiler. JITCompileThumbTrace(): walks THUMB instructions, emits PPC, manages regCache/flags/bailouts, EmitPrefetchSync, deferred bailout second-pass, arena bookkeeping, dcbst/icbi flush.' },
  { file: 'JITTrampoline.S',      role: 'Hand-written PPC assembly ABI bridge. ExecuteJITTrace (prologue, stack frame, eager loads, bctr entry) and ExecuteJITTrace_Return (writeback, restore, blr).' },
  { file: 'JITDifferential.h/.cpp','role': 'Optional differential testing (JIT_DIFFERENTIAL_TESTING). Runs JIT vs C++ interpreter side-by-side, compares state, logs mismatches. JIT_RecordMemoryWrite hook, SMC abort detection, per-opcode mismatch statistics.' },
  { file: 'JITDebugStateLog.h/.cpp','role': 'Optional state logger (JIT_STATE_LOGGING). Logs full CPU state per block execution to SD card, for offline alignment comparison via align_traces.py.' },
];

export default function ArchitectureTab() {
  const [openPhase, setOpenPhase] = useState<number | null>(null);

  return (
    <div>
      <Section title="System Overview">
        <div className="grid grid-cols-1 lg:grid-cols-3 gap-4 mb-6">
          <Box title="🎮 Guest CPU — ARM7TDMI (ARMv4T)" color="blue">
            <ul className="text-xs text-gray-300 space-y-1">
              <li>• 16 registers R0–R15 (R15 = PC)</li>
              <li>• THUMB (16-bit) and ARM (32-bit) modes</li>
              <li>• N/Z/C/V condition flags in CPSR</li>
              <li>• Little-endian, 32-bit bus</li>
              <li>• GBA: ~16.78 MHz; NDS ARM7: ~33.5 MHz</li>
              <li>• EWRAM, IWRAM, ROM, VRAM, OAM, I/O</li>
              <li>• JIT covers THUMB mode only (hottest path)</li>
              <li>• ARM mode falls back to NooDS interpreter</li>
            </ul>
          </Box>
          <Box title="⚡ JIT Engine — Trace Compiler" color="green">
            <ul className="text-xs text-gray-300 space-y-1">
              <li>• THUMB-only trace JIT (no ARM32 JIT)</li>
              <li>• Up to 42 instrs per trace block</li>
              <li>• Lazy register alloc (R15–R28 host pool)</li>
              <li>• Packed flags in single register (R6)</li>
              <li>• Deferred bailouts (pre-flush + fwd branch)</li>
              <li>• Self-patching block chaining</li>
              <li>• SMC guard (1KB page granularity)</li>
              <li>• 8MB arena (Wii/GCN), 32MB (Wii U)</li>
              <li>• 65536 hash table slots (Wii/GCN)</li>
            </ul>
          </Box>
          <Box title="🖥 Host CPU — Broadway/Gekko PPC 750" color="yellow">
            <ul className="text-xs text-gray-300 space-y-1">
              <li>• 32 GPRs (R0–R31), 32-bit words</li>
              <li>• Big-endian (GBA is LE → lwbrx/stwbrx)</li>
              <li>• PowerPC 750CXe (Wii) / 750CL (GCN)</li>
              <li>• ~729 MHz (Wii), ~485 MHz (GCN)</li>
              <li>• XER register for carry flag</li>
              <li>• dcbst/icbi required for self-modifying code</li>
              <li>• No MMU in homebrew context</li>
              <li>• EABI calling convention (non-volatile R14–R31)</li>
            </ul>
          </Box>
        </div>

        {/* Pipeline Diagram */}
        <div className="bg-gray-900 border border-gray-700 rounded-lg p-4 mb-4">
          <h3 className="text-green-300 font-bold text-sm mb-3">Execution Pipeline</h3>
          <div className="flex flex-col items-center gap-0">
            <div className="bg-blue-900 border border-blue-600 rounded px-4 py-2 text-xs text-blue-200 w-full max-w-lg text-center">
              NooDS-Wii ARM7 Interpreter Dispatch Loop (interpreter.cpp · runOpcode)
            </div>
            <Arrow label="THUMB fetch at PC" />
            <div className="bg-gray-800 border border-gray-600 rounded px-4 py-2 text-xs text-gray-200 w-full max-w-lg text-center">
              JIT Dispatch Hook — jitCache.getBlock(pc)
            </div>
            <div className="flex gap-4 w-full max-w-lg justify-center mt-1 mb-1">
              <div className="flex flex-col items-center">
                <Arrow label="Cache Hit" />
                <div className="bg-green-900 border border-green-600 rounded px-3 py-2 text-xs text-green-200 text-center">
                  ExecuteJITTrace()<br/>(JITTrampoline.S)
                </div>
              </div>
              <div className="flex flex-col items-center">
                <Arrow label="Cache Miss" />
                <div className="bg-purple-900 border border-purple-600 rounded px-3 py-2 text-xs text-purple-200 text-center">
                  JITCompileThumbTrace()<br/>(JITCompiler.cpp)
                </div>
              </div>
              <div className="flex flex-col items-center">
                <Arrow label="Bail/Fallback" />
                <div className="bg-red-900 border border-red-600 rounded px-3 py-2 text-xs text-red-200 text-center">
                  C++ Interpreter<br/>(thumbInstrs[])
                </div>
              </div>
            </div>
            <Arrow label="JITResult (cycles, nextPC)" />
            <div className="bg-yellow-900 border border-yellow-600 rounded px-4 py-2 text-xs text-yellow-200 w-full max-w-lg text-center">
              Linker Stub — Self-patch or return to C++
            </div>
            <Arrow label="cpuTotalTicks += cycles" />
            <div className="bg-blue-900 border border-blue-600 rounded px-4 py-2 text-xs text-blue-200 w-full max-w-lg text-center">
              NooDS Event Scheduler — next event / interrupt check
            </div>
          </div>
        </div>
      </Section>

      <Section title="Compilation Phases">
        <div className="space-y-3">
          {phases.map((phase, i) => (
            <div
              key={i}
              className={`border rounded-lg overflow-hidden cursor-pointer transition-colors ${
                openPhase === i ? 'border-green-500 bg-gray-900' : 'border-gray-700 bg-gray-900 hover:border-gray-500'
              }`}
              onClick={() => setOpenPhase(openPhase === i ? null : i)}
            >
              <div className="flex items-center gap-3 px-4 py-3">
                <span className="text-green-400 font-bold text-lg w-6 text-center">{phase.num}</span>
                <div className="flex-1">
                  <span className="font-bold text-sm text-white">{phase.title}</span>
                  <p className="text-xs text-gray-400 mt-0.5">{phase.desc}</p>
                </div>
                <span className="text-gray-500 text-xs">{openPhase === i ? '▲' : '▼'}</span>
              </div>
              {openPhase === i && (
                <div className="px-4 pb-4 pt-1 border-t border-gray-700">
                  <p className="text-xs text-gray-300 leading-relaxed">{phase.detail}</p>
                </div>
              )}
            </div>
          ))}
        </div>
      </Section>

      <Section title="Source File Map">
        <div className="overflow-x-auto">
          <table className="w-full text-xs border-collapse">
            <thead>
              <tr className="bg-gray-800 text-gray-300">
                <th className="border border-gray-700 px-3 py-2 text-left w-64">File</th>
                <th className="border border-gray-700 px-3 py-2 text-left">Role</th>
              </tr>
            </thead>
            <tbody>
              {fileMap.map((f, i) => (
                <tr key={i} className={i % 2 === 0 ? 'bg-gray-900' : 'bg-gray-950'}>
                  <td className="border border-gray-700 px-3 py-2 text-green-300 font-bold">{f.file}</td>
                  <td className="border border-gray-700 px-3 py-2 text-gray-300">{f.role}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </Section>

      <Section title="Memory & Arena Layout">
        <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
          <Box title="JIT Code Arena (Wii/GCN)" color="green">
            <ul className="text-xs text-gray-300 space-y-1">
              <li>• <span className="text-yellow-300">Size:</span> 8MB (JIT_ARENA_SIZE)</li>
              <li>• <span className="text-yellow-300">Allocator:</span> 32-byte aligned bump pointer</li>
              <li>• <span className="text-yellow-300">Overflow:</span> flushCache() resets to zero</li>
              <li>• <span className="text-yellow-300">Start:</span> Linker stub at offset 0</li>
              <li>• <span className="text-yellow-300">Blocks:</span> Appended sequentially after stub</li>
              <li>• <span className="text-yellow-300">Rewind:</span> rewindJITMemory() for unused reservation</li>
              <li>• <span className="text-yellow-300">Coherency:</span> DCStoreRange + ICInvalidateRange on each emitted block</li>
            </ul>
          </Box>
          <Box title="Hash Table (Block Index)" color="blue">
            <ul className="text-xs text-gray-300 space-y-1">
              <li>• <span className="text-yellow-300">Slots:</span> 65536 (Wii/GCN), 1M (Wii U)</li>
              <li>• <span className="text-yellow-300">Hash:</span> ((pc&gt;&gt;1) ^ (pc&gt;&gt;13)) &amp; (SIZE-1)</li>
              <li>• <span className="text-yellow-300">Collision:</span> Direct eviction (no chaining)</li>
              <li>• <span className="text-yellow-300">BasicBlock:</span> 16-byte aligned struct (startPC, length, execute, nextSMC)</li>
              <li>• <span className="text-yellow-300">execute==nullptr, length==0:</span> Uncompiled slot</li>
              <li>• <span className="text-yellow-300">execute==nullptr, length&gt;0:</span> "Don't JIT" fallback</li>
              <li>• <span className="text-yellow-300">SMC list:</span> nextSMC intrusive pointer per block</li>
            </ul>
          </Box>
          <Box title="SMC Tracking" color="red">
            <ul className="text-xs text-gray-300 space-y-1">
              <li>• <span className="text-yellow-300">smcPageFlags[SMC_MAP_SIZE]:</span> 1 byte per 1KB page</li>
              <li>• <span className="text-yellow-300">SMC_MAP_SIZE:</span> 65536 (covers 64MB @1KB/page)</li>
              <li>• <span className="text-yellow-300">Tracked banks:</span> 0x02 (EWRAM) and 0x03 (IWRAM)</li>
              <li>• <span className="text-yellow-300">On write hit:</span> Walk smcRegistry[page] chain, patch block's first PPC word to unconditional bail branch</li>
              <li>• <span className="text-yellow-300">JIT_SMC_GUARD macro:</span> Must be inserted into NooDS CPUWrite*() for EWRAM/IWRAM paths</li>
            </ul>
          </Box>
          <Box title="Stack Frame Layout (JITTrampoline.S)" color="purple">
            <div className="text-xs text-gray-300 font-mono space-y-0.5">
              <div className="flex gap-4"><span className="text-yellow-300 w-16">128+4(r1)</span><span>Saved LR (caller)</span></div>
              <div className="flex gap-4"><span className="text-yellow-300 w-16">8–88(r1)</span><span>Saved R14–R31 (stmw 14,8(r1))</span></div>
              <div className="flex gap-4"><span className="text-yellow-300 w-16">80(r1)</span><span>gbaRegs pointer</span></div>
              <div className="flex gap-4"><span className="text-yellow-300 w-16">84(r1)</span><span>flags array pointer ← EnsureFlagsLoaded uses this</span></div>
              <div className="flex gap-4"><span className="text-yellow-300 w-16">88(r1)</span><span>outResult (JITResult*)</span></div>
              <div className="flex gap-4"><span className="text-yellow-300 w-16">92(r1)</span><span>busPrefetchCount pointer</span></div>
            </div>
          </Box>
        </div>
      </Section>
    </div>
  );
}
