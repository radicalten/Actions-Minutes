const RegRow = ({ reg, role, type, notes }: { reg: string; role: string; type: string; notes: string }) => {
  const typeColors: Record<string, string> = {
    'ABI Reserved': 'text-red-400',
    'Volatile':     'text-yellow-400',
    'Non-Volatile': 'text-green-400',
    'Fixed JIT':    'text-blue-400',
    'Lazy Pool':    'text-purple-400',
  };
  return (
    <tr className="border-b border-gray-800 hover:bg-gray-800 transition-colors">
      <td className="px-3 py-2 font-mono font-bold text-white text-sm">{reg}</td>
      <td className={`px-3 py-2 text-xs font-medium ${typeColors[type] || 'text-gray-400'}`}>{type}</td>
      <td className="px-3 py-2 text-xs text-blue-300 font-bold">{role}</td>
      <td className="px-3 py-2 text-xs text-gray-400">{notes}</td>
    </tr>
  );
};

const regs = [
  { reg: 'R0',     role: 'Constant 0 (addr base)',      type: 'ABI Reserved',  notes: 'Used as base reg for 0-offset addressing. NEVER use as arithmetic scratch.' },
  { reg: 'R1',     role: 'Host Stack Pointer',           type: 'ABI Reserved',  notes: '128-byte frame allocated by trampoline. JIT-emitted code refs 80/84/88/92(r1) for stashed pointers.' },
  { reg: 'R2',     role: 'Host SDA2',                    type: 'ABI Reserved',  notes: 'devkitPPC / GCC system reserved. Touch = random corruption.' },
  { reg: 'R3',     role: 'Cycles accumulator / ret',     type: 'Volatile',      notes: 'Zeroed by trampoline on entry. Each epilogue ADDs this block\'s cycles then branches to linker stub.' },
  { reg: 'R4',     role: 'nextPC / scratch',             type: 'Volatile',      notes: 'Epilogue: MR R4,R29 (GBA PC). Linker stub reads R4 as next-block hash input.' },
  { reg: 'R5',     role: 'busPrefetchCount (live)',       type: 'Volatile',      notes: 'Eagerly loaded from *busPrefetchCount on entry. Trampoline STWs it back on return. JIT accumulates prefetch ticks here.' },
  { reg: 'R6',     role: 'PPC_REG_FLAGS (packed N/Z/C/V)', type: 'Fixed JIT',  notes: 'Top nibble = IBM bits 0-3 = N,Z,C,V. EnsureFlagsLoaded() faults in; FlushDirtyFlags() writes back. Never alias for scratch while flags are live.' },
  { reg: 'R7',     role: 'General scratch',               type: 'Volatile',     notes: 'Free for any use within an instruction emitter. Not preserved across instructions.' },
  { reg: 'R8',     role: 'General scratch',               type: 'Volatile',     notes: 'Frequently used for flag bit extraction/insertion and memory value temp.' },
  { reg: 'R9',     role: 'General scratch',               type: 'Volatile',     notes: 'Used by EnsureFlagsLoaded as flags* base; also general scratch in emitters.' },
  { reg: 'R10',    role: 'Base page pointer / math',      type: 'Volatile',     notes: 'EA base, hash table address, gbaRegs pointer reloads, misc math.' },
  { reg: 'R11',    role: 'Bank / mask / condition',       type: 'Volatile',     notes: 'Bank index, page table index, hash index in linker stub, condition scratch.' },
  { reg: 'R12',    role: 'Target addr / operand',         type: 'Volatile',     notes: 'Execute pointer in linker stub, EA target, BL destination, load result before store.' },
  { reg: 'R13',    role: 'Host SDA (small data area)',    type: 'ABI Reserved',  notes: 'GCC/devkitPPC system reserved. Do not touch.' },
  { reg: 'R14',    role: 'PPC_REG_GBA_REGS_PTR',          type: 'Fixed JIT',    notes: 'Non-volatile. Base pointer to C++ gbaRegs[0..15] u32 array. Set by trampoline (mr 14,6). Never overwrite.' },
  { reg: 'R15',    role: 'Lazy GBA pool — GBA R0',        type: 'Lazy Pool',    notes: 'Allocated on first use of GBA R0 this block. Spilled to gbaRegs[0] on eviction or epilogue.' },
  { reg: 'R16',    role: 'Lazy GBA pool — GBA R1',        type: 'Lazy Pool',    notes: '' },
  { reg: 'R17',    role: 'Lazy GBA pool — GBA R2',        type: 'Lazy Pool',    notes: '' },
  { reg: 'R18',    role: 'Lazy GBA pool — GBA R3',        type: 'Lazy Pool',    notes: '' },
  { reg: 'R19',    role: 'Lazy GBA pool — GBA R4',        type: 'Lazy Pool',    notes: '' },
  { reg: 'R20',    role: 'Lazy GBA pool — GBA R5',        type: 'Lazy Pool',    notes: '' },
  { reg: 'R21',    role: 'Lazy GBA pool — GBA R6',        type: 'Lazy Pool',    notes: '' },
  { reg: 'R22',    role: 'Lazy GBA pool — GBA R7',        type: 'Lazy Pool',    notes: '' },
  { reg: 'R23',    role: 'Lazy GBA pool — GBA R8',        type: 'Lazy Pool',    notes: '' },
  { reg: 'R24',    role: 'Lazy GBA pool — GBA R9',        type: 'Lazy Pool',    notes: '' },
  { reg: 'R25',    role: 'Lazy GBA pool — GBA R10',       type: 'Lazy Pool',    notes: '' },
  { reg: 'R26',    role: 'Lazy GBA pool — GBA R11',       type: 'Lazy Pool',    notes: '' },
  { reg: 'R27',    role: 'Lazy GBA pool — GBA R12',       type: 'Lazy Pool',    notes: '' },
  { reg: 'R28',    role: 'Lazy GBA pool — GBA R13 (SP)',  type: 'Lazy Pool',    notes: 'GBA SP = R13. Allocated lazily. Most PUSH/POP handlers fault this in immediately.' },
  { reg: 'R29',    role: 'PPC_REG_PC (GBA R15 / PC)',     type: 'Fixed JIT',    notes: 'Non-volatile. GBA pipeline PC (already +4 from fetch). Eagerly loaded by trampoline (lwz 29,60(r6)). Epilogue MR R4,R29. Trampoline STW R29 back to gbaRegs[15] on return.' },
  { reg: 'R30',    role: 'gbaReadTable base pointer',      type: 'Fixed JIT',    notes: 'Non-volatile. Pointer to NooDS read-page table array (readMap7[] or readMap9A[] depending on CPU). Set by trampoline (mr 30,r8). Memory guards index via SRWI(bank,24)+SLWI(×4)+LWZX.' },
  { reg: 'R31',    role: 'General scratch',                type: 'Non-Volatile', notes: 'Saved/restored by trampoline. Available as extra scratch in complex emitters (e.g., PUSH/POP multi-reg unrolling).' },
];

const flagLayout = [
  { bit: '0 (MSB / IBM bit 0 / conv bit 31)', name: 'N', desc: 'Negative flag. Set when result bit 31 = 1.' },
  { bit: '1 (IBM bit 1 / conv bit 30)',        name: 'Z', desc: 'Zero flag. Set when result == 0.' },
  { bit: '2 (IBM bit 2 / conv bit 29)',        name: 'C', desc: 'Carry flag. Set by ADDC/SUBFC/shifts. Read via XER[CA] or MFXER.' },
  { bit: '3 (IBM bit 3 / conv bit 28)',        name: 'V', desc: 'Overflow flag. Set by ADD/SUB overflow. Read via XER[OV] or MFXER.' },
  { bit: '4–31',                               name: '—', desc: 'Unused / garbage. Never read.' },
];

const gbaRegs = [
  { reg: 'R0–R12',  offset: '0–48',  role: 'General purpose',    note: 'Lazily mapped to PPC R15–R28' },
  { reg: 'R13 (SP)', offset: '52',   role: 'Stack Pointer',       note: 'PPC R28 when allocated' },
  { reg: 'R14 (LR)', offset: '56',   role: 'Link Register',       note: 'Not in lazy pool — accessed via FindOrAllocateHostReg(14). Written by BL.' },
  { reg: 'R15 (PC)', offset: '60',   role: 'Program Counter',     note: 'Eagerly loaded into PPC R29. Always +4 ahead of fetch (pipeline).' },
];

export default function RegisterMapTab() {
  return (
    <div>
      {/* Host PPC register table */}
      <h2 className="text-lg font-bold text-green-400 border-b border-green-800 pb-1 mb-4">
        Host PPC Register Contract (R0–R31)
      </h2>
      <div className="mb-2 text-xs text-gray-400">
        This is the <strong className="text-white">authoritative</strong> register contract shared between JITCompiler.cpp, JITTrampoline.S, and JITCache.cpp.
        Any deviation between these files is a silent correctness bug (not a compile error).
      </div>
      <div className="overflow-x-auto mb-8">
        <table className="w-full text-xs border-collapse bg-gray-900 rounded-lg overflow-hidden">
          <thead>
            <tr className="bg-gray-800 text-gray-300">
              <th className="px-3 py-2 text-left border-b border-gray-700">Reg</th>
              <th className="px-3 py-2 text-left border-b border-gray-700">Type</th>
              <th className="px-3 py-2 text-left border-b border-gray-700">JIT Role</th>
              <th className="px-3 py-2 text-left border-b border-gray-700">Notes</th>
            </tr>
          </thead>
          <tbody>
            {regs.map((r, i) => <RegRow key={i} {...r} />)}
          </tbody>
        </table>
      </div>

      {/* Packed flags */}
      <h2 className="text-lg font-bold text-green-400 border-b border-green-800 pb-1 mb-4">
        Packed Flag Register (PPC_REG_FLAGS = R6)
      </h2>
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6 mb-8">
        <div>
          <p className="text-xs text-gray-400 mb-3 leading-relaxed">
            All four GBA condition flags (N/Z/C/V) live packed together in R6's <strong className="text-white">top nibble</strong> (IBM bits 0–3, conventional bits 31–28).
            The remaining 28 low bits are garbage/don't-care and are never read.
            This frees R7/R8/R9 back into general scratch at the cost of one extra rlwimi/rlwinm per flag access.
          </p>
          <div className="overflow-x-auto">
            <table className="w-full text-xs border-collapse bg-gray-900 rounded-lg overflow-hidden">
              <thead>
                <tr className="bg-gray-800 text-gray-300">
                  <th className="px-3 py-2 text-left border-b border-gray-700">R6 Bit Position</th>
                  <th className="px-3 py-2 text-left border-b border-gray-700">Flag</th>
                  <th className="px-3 py-2 text-left border-b border-gray-700">Description</th>
                </tr>
              </thead>
              <tbody>
                {flagLayout.map((f, i) => (
                  <tr key={i} className="border-b border-gray-800">
                    <td className="px-3 py-2 font-mono text-yellow-300">{f.bit}</td>
                    <td className="px-3 py-2 font-bold text-blue-300">{f.name}</td>
                    <td className="px-3 py-2 text-gray-400">{f.desc}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
        <div className="bg-gray-900 border border-gray-700 rounded-lg p-4">
          <div className="text-sm font-bold text-green-300 mb-3">Macro Usage</div>
          <pre className="text-xs text-gray-300 leading-relaxed overflow-x-auto">{`// WRITE: merge bit31 of srcReg into flag slot targetBit
PPC_MERGE_FLAG_BIT(FLAG_BIT_N, PPC_R8, 0)
// → rlwimi R6, R8, (0+31-0)&31=31, 0, 0

PPC_MERGE_FLAG_BIT(FLAG_BIT_Z, PPC_R8, 0)
// → rlwimi R6, R8, (0+31-1)&31=30, 1, 1

PPC_MERGE_FLAG_BIT(FLAG_BIT_C, PPC_R8, 0)
// → rlwimi R6, R8, (0+31-2)&31=29, 2, 2

// READ: extract flag as 0/1 in bit31 of dstReg
PPC_EXTRACT_FLAG_BIT(PPC_R8, FLAG_BIT_C)
// → rlwinm R8, R6, (2+1)&31=3, 31, 31

// MERGE from non-zero shift (e.g. XER CA at bit29):
// After MFXER into R8, XER[CA] is at IBM bit 29 = conventional bit 2
PPC_MERGE_FLAG_BIT(FLAG_BIT_C, PPC_R8, 3)
// srcReg bit31 after rlwinm(R8,R8,3,31,31) lands in FLAG_BIT_C`}</pre>
        </div>
      </div>

      {/* GBA register memory layout */}
      <h2 className="text-lg font-bold text-green-400 border-b border-green-800 pb-1 mb-4">
        GBA Register Array Layout (gbaRegs[] in NooDS)
      </h2>
      <p className="text-xs text-gray-400 mb-3">
        R14 (PPC_REG_GBA_REGS_PTR) points to NooDS's <code className="text-green-300">registersUsr[]</code> (or the currently-banked equivalent).
        Each GBA register is a <code className="text-green-300">uint32_t</code> at offset <code className="text-green-300">reg×4</code>.
        In NooDS, registers are accessed via pointer indirection (<code className="text-green-300">uint32_t* registers[16]</code>),
        so the JIT needs to use the <strong className="text-white">flat array address</strong> passed in via gbaRegs argument,
        which should be <code className="text-green-300">&amp;registersUsr[0]</code>.
      </p>
      <div className="overflow-x-auto">
        <table className="w-full text-xs border-collapse bg-gray-900 rounded-lg overflow-hidden">
          <thead>
            <tr className="bg-gray-800 text-gray-300">
              <th className="px-3 py-2 text-left border-b border-gray-700">GBA Register</th>
              <th className="px-3 py-2 text-left border-b border-gray-700">gbaRegs[] Byte Offset</th>
              <th className="px-3 py-2 text-left border-b border-gray-700">Role</th>
              <th className="px-3 py-2 text-left border-b border-gray-700">PPC Allocation</th>
            </tr>
          </thead>
          <tbody>
            {gbaRegs.map((r, i) => (
              <tr key={i} className="border-b border-gray-800">
                <td className="px-3 py-2 font-bold text-blue-300 font-mono">{r.reg}</td>
                <td className="px-3 py-2 text-yellow-300 font-mono">{r.offset}</td>
                <td className="px-3 py-2 text-gray-300">{r.role}</td>
                <td className="px-3 py-2 text-purple-300">{r.note}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      {/* Stack frame diagram */}
      <h2 className="text-lg font-bold text-green-400 border-b border-green-800 pb-1 mb-4 mt-8">
        JITTrampoline Stack Frame
      </h2>
      <div className="bg-gray-900 border border-gray-700 rounded-lg p-4 font-mono text-xs overflow-x-auto">
        <div className="text-gray-400 mb-2">128-byte frame (r1 = SP after stwu 1,-128(1)):</div>
        {[
          ['128+4(r1)', 'Caller LR (saved by mflr 0 / stw 0,4(1) before stwu)'],
          ['8..76(r1)', 'R14–R31 saved by stmw 14,8(r1) (18 regs × 4 = 72 bytes)'],
          ['80(r1)', 'gbaRegs pointer (R6 on entry) ← used by JIT: lwz 14,80(1)'],
          ['84(r1)', 'flags array pointer (R7 on entry) ← EnsureFlagsLoaded: lwz 9,84(1)'],
          ['88(r1)', 'outResult pointer (R4 on entry) ← bailout pads: lwz 10,88(1)'],
          ['92(r1)', 'busPrefetchCount pointer (R5 on entry)'],
          ['96–127(r1)', 'Padding / reserved'],
        ].map(([off, desc], i) => (
          <div key={i} className="flex gap-4 py-0.5">
            <span className="text-yellow-300 w-36 flex-shrink-0">{off}</span>
            <span className="text-gray-300">{desc}</span>
          </div>
        ))}
      </div>
    </div>
  );
}
