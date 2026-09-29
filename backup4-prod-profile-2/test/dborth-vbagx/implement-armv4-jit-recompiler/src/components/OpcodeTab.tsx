import { useState } from 'react';

interface OpcodeGroup {
  fmt: string;
  name: string;
  encoding: string;
  supported: 'yes' | 'partial' | 'fallback';
  flags: string;
  ppcEmit: string;
  notes: string;
  cycles: string;
}

const opcodes: OpcodeGroup[] = [
  {
    fmt: 'F1', name: 'Move Shifted Register (LSL/LSR/ASR)',
    encoding: '000[op5][offset5][Rs3][Rd3]',
    supported: 'yes', flags: 'N,Z,C',
    ppcEmit: 'SLWI/SRWI/SRAWI + RLWINM(N) + CNTLZW(Z) + RLWINM(C)',
    notes: 'LSR/ASR #0 treated as #32. C flag from shifted-out bit.',
    cycles: '1S'
  },
  {
    fmt: 'F2', name: 'ADD/SUB Register or Imm3',
    encoding: '00011[I][Op][Rn/imm3][Rs3][Rd3]',
    supported: 'yes', flags: 'N,Z,C,V',
    ppcEmit: 'ADDC/SUBFC/SUBFIC + MFXER (XER CA→C, XER OV→V)',
    notes: 'V flag via XER[OV]. All 4 variants (ADD reg/imm, SUB reg/imm).',
    cycles: '1S'
  },
  {
    fmt: 'F3', name: 'MOV/CMP/ADD/SUB Imm8',
    encoding: '001[Op2][Rd3][imm8]',
    supported: 'yes', flags: 'N,Z (MOV/CMP); N,Z,C,V (ADD/SUB)',
    ppcEmit: 'LI + MERGE_FLAG_BIT. ADD/SUB: ADDI + MFXER',
    notes: 'MOV: N always 0 (imm is u8). Z: compile-time if imm==0.',
    cycles: '1S'
  },
  {
    fmt: 'F4', name: 'ALU Operations (AND/EOR/LSL/LSR/ASR/ADC/SBC/ROR/TST/NEG/CMP/CMN/ORR/MUL/BIC/MVN)',
    encoding: '010000[Op4][Rs3][Rd3]',
    supported: 'yes', flags: 'N,Z (most); +C (shifts/ADC/SBC); +V (ADC/SBC/NEG)',
    ppcEmit: 'See per-op below. ADC uses EXTRACT_FLAG_BIT(C)+ADDE. SBC uses SUBFE.',
    notes: 'MUL: MULLW (low 32 bits only, no 64-bit MUL for flags — approximated). ROR: RLWNM.',
    cycles: '1S (MUL: 1S+mS)'
  },
  {
    fmt: 'F5', name: 'Hi Register / BX',
    encoding: '010001[Op2][H1][H2][Rs3/Hs3][Rd3/Hd3]',
    supported: 'yes', flags: 'None (except CMP Hi: N,Z,C,V)',
    ppcEmit: 'ADD/MOV: use R15–R28 pool extended to R8–R14 range. BX: guard on target bit0.',
    notes: 'BX with non-THUMB target bails to interpreter (no ARM JIT). MOV Rd=PC treated as branch.',
    cycles: '1S (BX: 2S+1N)'
  },
  {
    fmt: 'F6', name: 'PC-Relative Load (LDR Rd, [PC, #imm])',
    encoding: '01001[Rd3][imm8] — addr=(PC+4)&~3 + imm×4',
    supported: 'yes', flags: 'None',
    ppcEmit: 'LIS/ORI(addr) + bank-check + page-lookup + LWBRX',
    notes: 'Address computed at compile time. Bailout if page is null/bad bank.',
    cycles: '1S+1N'
  },
  {
    fmt: 'F7', name: 'Load/Store Register Offset (STR/LDR/STRB/LDRB)',
    encoding: '0101[L][B][0][Ro3][Rb3][Rd3]',
    supported: 'yes', flags: 'None',
    ppcEmit: 'ADD(ea) + SRWI(bank) + CMPWi + page-lookup + LWBRX/LBZX/STWBRX/STBX',
    notes: 'Bank guard bails on I/O (4), BIOS (0), SRAM (0xD+). SMC guard on stores to EWRAM/IWRAM.',
    cycles: '1S+1N (LDR), 2N (STR)'
  },
  {
    fmt: 'F8', name: 'Load/Store Sign-Extended Byte/Halfword',
    encoding: '0101[H][S][1][Ro3][Rb3][Rd3]',
    supported: 'yes', flags: 'None',
    ppcEmit: 'LHBRX / LHAX / STBX / STHBRX',
    notes: 'Sign-extended: LHAX (PPC native sign-extend halfword). Byte sign: EXTSB.',
    cycles: '1S+1N / 2N'
  },
  {
    fmt: 'F9', name: 'Load/Store Word/Byte Immediate Offset',
    encoding: '011[B][L][offset5][Rb3][Rd3]',
    supported: 'yes', flags: 'None',
    ppcEmit: 'Same guard template as F7 but EA=Rb+offset (offset computed at compile time).',
    notes: 'Compile-time offset folded into ADDI before bank check.',
    cycles: '1S+1N / 2N'
  },
  {
    fmt: 'F10', name: 'Load/Store Halfword Immediate Offset',
    encoding: '1000[L][offset5][Rb3][Rd3]',
    supported: 'yes', flags: 'None',
    ppcEmit: 'LHBRX / STHBRX with guarded page lookup.',
    notes: 'offset×2. Unaligned: masked with ~1.',
    cycles: '1S+1N / 2N'
  },
  {
    fmt: 'F11', name: 'SP-Relative Load/Store',
    encoding: '1001[L][Rd3][imm8] — addr=SP+imm×4',
    supported: 'yes', flags: 'None',
    ppcEmit: 'FindOrAllocateHostReg(13) + ADDI(sp,off) + page-lookup + LWBRX/STWBRX',
    notes: 'SP is GBA R13, lazily allocated. Offset word-aligned (×4).',
    cycles: '1S+1N / 2N'
  },
  {
    fmt: 'F12', name: 'Load Address (ADD Rd, PC/SP, #imm)',
    encoding: '1010[SP][Rd3][imm8]',
    supported: 'yes', flags: 'None',
    ppcEmit: 'LIS/ORI(pc+off) or ADDI(sp_reg, off×4)',
    notes: 'PC-relative address computed at compile time as constant.',
    cycles: '1S'
  },
  {
    fmt: 'F13', name: 'ADD SP, #±imm',
    encoding: '10110000[S][imm7]',
    supported: 'yes', flags: 'None',
    ppcEmit: 'FindOrAllocateHostReg(13) + ADDI(sp, ±off×4)',
    notes: 'S bit negates offset. No flags affected.',
    cycles: '1S'
  },
  {
    fmt: 'F14', name: 'PUSH/POP',
    encoding: '1011[L][R][rlist8]',
    supported: 'yes', flags: 'None',
    ppcEmit: 'Expand register list at compile time; emit STW/LWZ per bit set. PUSH R=1 includes LR, POP R=1 includes PC.',
    notes: 'POP PC: load into R29 (PC reg), then end block. Unrolled at compile time.',
    cycles: '(n+1)S + 2N'
  },
  {
    fmt: 'F15', name: 'LDMIA/STMIA',
    encoding: '1100[L][Rb3][rlist8]',
    supported: 'yes', flags: 'None',
    ppcEmit: 'Unrolled at compile time. EA=Rb, then LWZ/STW per register, ADDI(Rb,4) per step. Writeback to Rb.',
    notes: 'Base register writeback always performed (base included in list: UNPREDICTABLE, handled as ARM spec).',
    cycles: '(n-1)S + 2N / (n-1)S + 2N'
  },
  {
    fmt: 'F16', name: 'Conditional Branch (Bcc)',
    encoding: '1101[cond4][soffset8]',
    supported: 'yes', flags: 'Reads N,Z,C,V',
    ppcEmit: 'EXTRACT_FLAG_BIT + CMPWI + BEQ/BNE. Taken path: write target PC, end block. Fall-through: continue trace.',
    notes: 'BL (cond=F) dispatched separately as F19. BKPT (cond=E) bails.',
    cycles: '2S+1N (taken) / 1S (not taken)'
  },
  {
    fmt: 'F17', name: 'SWI (Software Interrupt)',
    encoding: '11011111[comment8]',
    supported: 'fallback', flags: 'All',
    ppcEmit: 'Bail to C++ interpreter (NooDS BIOS HLE handles SWI)',
    notes: 'Complex mode-change and stack semantics; not worth JIT-ing.',
    cycles: 'C++ fallback'
  },
  {
    fmt: 'F18', name: 'Unconditional Branch (B)',
    encoding: '11100[offset11]',
    supported: 'yes', flags: 'None',
    ppcEmit: 'LIS/ORI(targetPC) into R29, then branch to linker stub epilogue.',
    notes: 'Always ends the current trace block. Target may be chained by linker stub on next execution.',
    cycles: '2S+1N'
  },
  {
    fmt: 'F19', name: 'BL / BLX Offset (Long Branch)',
    encoding: 'H=0: 11110[off11_hi] + H=1: 11111[off11_lo]',
    supported: 'yes', flags: 'None',
    ppcEmit: 'Pair detected at compile time. LR = return PC (written to regCache[14]). Branch to target.',
    notes: 'Both half-words must be present in the trace window. BLX (H=01) switches to ARM → bail.',
    cycles: '3S+1N'
  },
  {
    fmt: 'ARM', name: 'ARM mode instructions (32-bit)',
    encoding: '[cond4][xxxx...] where bit[0] of PC = 0',
    supported: 'fallback', flags: 'All',
    ppcEmit: 'Not JIT-compiled. Fall through to NooDS ARM32 interpreter.',
    notes: 'ARM mode is entered via BX with target bit0=0. The JIT exits cleanly before the BX executes.',
    cycles: 'C++ interpreter'
  },
];

const STATUS_COLORS = {
  yes:      { bg: 'bg-green-900 border-green-700',  text: 'text-green-300',  label: '✅ JIT-compiled' },
  partial:  { bg: 'bg-yellow-900 border-yellow-700', text: 'text-yellow-300', label: '⚠ Partial'       },
  fallback: { bg: 'bg-red-900 border-red-700',       text: 'text-red-300',    label: '↩ C++ Fallback'  },
};

export default function OpcodeTab() {
  const [filter, setFilter] = useState<'all'|'yes'|'fallback'>('all');
  const [search, setSearch] = useState('');
  const [open,   setOpen]   = useState<number | null>(null);

  const visible = opcodes.filter(o => {
    if (filter !== 'all' && o.supported !== filter) return false;
    if (search && !o.name.toLowerCase().includes(search.toLowerCase()) &&
        !o.fmt.toLowerCase().includes(search.toLowerCase())) return false;
    return true;
  });

  const total    = opcodes.length;
  const compiled = opcodes.filter(o => o.supported === 'yes').length;
  const fallback = opcodes.filter(o => o.supported === 'fallback').length;

  return (
    <div>
      {/* Stats */}
      <div className="grid grid-cols-3 gap-4 mb-6">
        <div className="bg-green-950 border border-green-700 rounded-lg p-4 text-center">
          <div className="text-3xl font-bold text-green-300">{compiled}</div>
          <div className="text-xs text-gray-400 mt-1">JIT-compiled formats</div>
        </div>
        <div className="bg-red-950 border border-red-700 rounded-lg p-4 text-center">
          <div className="text-3xl font-bold text-red-300">{fallback}</div>
          <div className="text-xs text-gray-400 mt-1">C++ fallback</div>
        </div>
        <div className="bg-blue-950 border border-blue-700 rounded-lg p-4 text-center">
          <div className="text-3xl font-bold text-blue-300">{Math.round(compiled/total*100)}%</div>
          <div className="text-xs text-gray-400 mt-1">Hot-path coverage</div>
        </div>
      </div>

      {/* Filters */}
      <div className="flex flex-wrap gap-3 mb-4">
        <input
          type="text" placeholder="Search opcode…"
          value={search} onChange={e => setSearch(e.target.value)}
          className="bg-gray-800 border border-gray-600 rounded px-3 py-1.5 text-xs text-gray-200 focus:outline-none focus:border-green-500 w-48"
        />
        {(['all','yes','fallback'] as const).map(f => (
          <button key={f} onClick={() => setFilter(f)}
            className={`px-3 py-1.5 rounded text-xs font-medium transition-colors ${
              filter === f ? 'bg-green-700 text-white' : 'bg-gray-800 text-gray-400 hover:bg-gray-700'
            }`}>
            {f === 'all' ? 'All' : f === 'yes' ? '✅ JIT-compiled' : '↩ Fallback'}
          </button>
        ))}
      </div>

      {/* Table */}
      <div className="space-y-2">
        {visible.map((op, i) => {
          const st = STATUS_COLORS[op.supported];
          const idx = opcodes.indexOf(op);
          return (
            <div key={i} className={`border rounded-lg overflow-hidden cursor-pointer transition-colors ${open === idx ? st.bg : 'bg-gray-900 border-gray-700 hover:border-gray-500'}`}
                 onClick={() => setOpen(open === idx ? null : idx)}>
              <div className="flex items-center gap-3 px-4 py-3">
                <span className="text-gray-500 font-mono text-xs w-8">{op.fmt}</span>
                <div className="flex-1">
                  <span className="font-bold text-sm text-white">{op.name}</span>
                  <div className="text-xs text-gray-500 font-mono mt-0.5">{op.encoding}</div>
                </div>
                <div className="flex items-center gap-3">
                  <span className="hidden sm:block text-xs text-gray-400">{op.cycles}</span>
                  <span className={`text-xs font-medium ${st.text}`}>{st.label}</span>
                  <span className="text-gray-500 text-xs">{open === idx ? '▲' : '▼'}</span>
                </div>
              </div>
              {open === idx && (
                <div className="px-4 pb-4 pt-2 border-t border-gray-700 grid grid-cols-1 md:grid-cols-2 gap-4">
                  <div>
                    <div className="text-xs text-gray-500 mb-1">Flags Affected</div>
                    <div className="text-xs text-yellow-300 font-mono">{op.flags}</div>
                    <div className="text-xs text-gray-500 mt-3 mb-1">Cycle Cost</div>
                    <div className="text-xs text-blue-300 font-mono">{op.cycles}</div>
                    <div className="text-xs text-gray-500 mt-3 mb-1">Notes</div>
                    <div className="text-xs text-gray-300 leading-relaxed">{op.notes}</div>
                  </div>
                  <div>
                    <div className="text-xs text-gray-500 mb-1">PPC Emission</div>
                    <div className="bg-gray-950 rounded p-2 font-mono text-xs text-green-300 leading-relaxed">{op.ppcEmit}</div>
                  </div>
                </div>
              )}
            </div>
          );
        })}
      </div>

      {/* ALU op detail table */}
      <div className="mt-8">
        <h3 className="text-green-400 font-bold mb-3">Format 4 ALU Operations — Full Breakdown</h3>
        <div className="overflow-x-auto">
          <table className="w-full text-xs border-collapse">
            <thead>
              <tr className="bg-gray-800 text-gray-300">
                <th className="border border-gray-700 px-3 py-2 text-left">Op</th>
                <th className="border border-gray-700 px-3 py-2 text-left">Mnemonic</th>
                <th className="border border-gray-700 px-3 py-2 text-left">PPC Emission</th>
                <th className="border border-gray-700 px-3 py-2 text-left">Flags</th>
              </tr>
            </thead>
            <tbody>
              {[
                ['0x0','AND','PPC_AND(Rd,Rd,Rs) + N+Z','N,Z'],
                ['0x1','EOR','PPC_XOR(Rd,Rd,Rs) + N+Z','N,Z'],
                ['0x2','LSL','PPC_SLW(Rd,Rd,Rs) + N+Z+C','N,Z,C'],
                ['0x3','LSR','PPC_SRW(Rd,Rd,Rs) + N+Z+C','N,Z,C'],
                ['0x4','ASR','PPC_SRAW(Rd,Rd,Rs) + N+Z+C','N,Z,C'],
                ['0x5','ADC','EXTRACT_FLAG_BIT(C)+PPC_ADDE(Rd,Rd,Rs)+MFXER','N,Z,C,V'],
                ['0x6','SBC','EXTRACT_FLAG_BIT(C)+PPC_SUBFE(Rd,Rs,Rd)+MFXER','N,Z,C,V'],
                ['0x7','ROR','PPC_RLWNM(Rd,Rd,Rs,0,31) + N+Z+C','N,Z,C'],
                ['0x8','TST','PPC_AND(R8,Rd,Rs) + N+Z (no writeback)','N,Z'],
                ['0x9','NEG','PPC_SUBFC(Rd,Rs,R0)+MFXER','N,Z,C,V'],
                ['0xA','CMP','PPC_SUBFC(R8,Rs,Rd)+N+Z+MFXER (no writeback)','N,Z,C,V'],
                ['0xB','CMN','PPC_ADDC(R8,Rd,Rs)+MFXER (no writeback)','N,Z,C,V'],
                ['0xC','ORR','PPC_OR(Rd,Rd,Rs) + N+Z','N,Z'],
                ['0xD','MUL','PPC_MULLW(Rd,Rd,Rs) + N+Z','N,Z (C=V undef)'],
                ['0xE','BIC','PPC_NOT(R8,Rs)+PPC_AND(Rd,Rd,R8)+N+Z','N,Z'],
                ['0xF','MVN','PPC_NOT(Rd,Rs)+N+Z','N,Z'],
              ].map(([op,mn,ppc,fl],i) => (
                <tr key={i} className={i%2===0?'bg-gray-900':'bg-gray-950'}>
                  <td className="border border-gray-700 px-3 py-1.5 text-yellow-300 font-mono">{op}</td>
                  <td className="border border-gray-700 px-3 py-1.5 text-green-300 font-bold">{mn}</td>
                  <td className="border border-gray-700 px-3 py-1.5 text-gray-300 font-mono">{ppc}</td>
                  <td className="border border-gray-700 px-3 py-1.5 text-blue-300">{fl}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  );
}
