/*
    Copyright (C) 2026 radicalten

    This file is part of NooDS-Wii.

    NooDS-Wii is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    NooDS-Wii is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NooDS-Wii. If not, see <https://www.gnu.org/licenses/>.
*/

// ─────────────────────────────────────────────────────────────────────────────
// Subroutine-threaded dispatch for the ARM/THUMB interpreter (PowerPC/Wii).
//
// Reference: Anton Ertl, "Threading techniques" (TU Wien)
//            https://www.complang.tuwien.ac.at/forth/threading/
//   Ranking for PowerPC:  subroutine threading  >  direct threading  >  switches
//
// The baseline interpreter dispatches through
//     int (Interpreter::*armInstrs[0x1000])(uint32_t)
// i.e. an array of *pointer-to-member-function*.  On the PPC ABI a PMF is
// 8 bytes wide (fn | adj<<32) and calling through one is not a simple
// `mtctr/bctr`.  Compiled with devkitPPC gcc 16.2 / -mcpu=750, one indirect
// dispatch of that form is:
//
//     rlwinm 8,4,28,28,31      ; opcode bits 7..4
//     rlwinm 10,4,16,20,27     ; opcode bits 27..20
//     or     10,10,8           ; index
//     lis    9,tab@ha
//     slwi   10,10,3           ; *8   <-- 8-byte stride
//     la     9,tab@l(9)
//     lwzx   7,9,10            ; fn
//     add    9,9,10
//     lwz    9,4(9)            ; adj
//     mr     8,3               ; this
//     andi.  10,7,0x1          ; virtual-bit test
//     add    3,3,9             ; this + adj
//     beq-   .L2
//     lwzx   9,8,9             ; vtable fetch ...
//     add    9,9,7
//     lwz    7,-1(9)
// .L2:
//     mtctr  7
//     bctr                     ; 16 instructions, 2 dependent loads
//
// The interpreter allocates `Interpreter` with plain `new`, so no handler is
// ever virtual and every PMF carries adj == 0 and an even fn.  The adj path is
// therefore dead weight on *every* instruction.  Flattening the table to a
// matrix of direct function pointers taking `Interpreter*` explicitly gives:
//
//     lis    9,tab@ha
//     rlwinm 8,4,16,20,27
//     rlwinm 10,4,28,28,31
//     la     9,tab@l(9)
//     or     10,8,10
//     addis  9,9,0x1
//     slwi   10,10,2           ; *4   <-- 4-byte stride
//     add    9,9,10
//     lwz    9,-32768(9)
//     mtctr  9
//     bctr                     ; 11 instructions, 1 dependent load
//
// Side benefits that matter on a 750CL with a 32 KiB L1D / 32 KiB L1I:
//   • table stride drops 8 → 4 bytes, so the hot ARM table occupies 16 KiB
//     instead of 32 KiB and the THUMB table 4 KiB instead of 8 KiB.  It now
//     fits the data cache instead of thrashing it.
//   • 32-byte cache lines hold 8 handlers instead of 4, halving the line
//     fetches of a branchy guest working set.
//   • the shared `this` register is materialised once and the callee reads it
//     from r3, so no per-dispatch `mr`/`add` chain on the critical path.
//
// The tables are still *subroutine* threaded (each handler is an independent
// native subroutine called through a table slot and returning its cycle
// count), which is the technique Ertl ranks first for PPC.  Direct threading
// proper would need one contiguous native block per guest opcode; because the
// handlers are ~150 distinct C++ member functions with heavy inlining, doing
// that would mean duplicating the interpreter body per opcode and losing the
// compiler's shared cold paths, so subroutine threading is the right target.
// ─────────────────────────────────────────────────────────────────────────────

#include "core.h"

#if NOODS_THREADED_DISPATCH

#include <cstring>

// Flatten one pointer-to-member-function into a plain function pointer by
// prepending the object pointer as an explicit first argument.
template <typename Ret, typename... Args>
static inline Ret (*flattenPmf(Ret (Interpreter::*pmf)(Args...)))(Interpreter *, Args...) {
    return reinterpret_cast<Ret (*)(Interpreter *, Args...)>(pmf);
}

// The flattened tables are built so the *exact* dispatch could be A/B tested
// from one tree (-DNOODS_THREADED_DISPATCH=0 restores the legacy path).
// THUMB handlers keep their native uint16_t parameter so the truncation the
// interpreter already performed at the call site is preserved bit for bit.
alignas(32)
int (*Interpreter::armInstrsFlat[0x1000])(Interpreter *, uint32_t);

alignas(32)
int (*Interpreter::thumbInstrsFlat[0x400])(Interpreter *, uint16_t);

bool Interpreter::dispatchTablesReady = false;

// Safety net: if the PMF layout ever stops being {fn, adj==0} (e.g. someone
// makes Interpreter polymorphic or allocates it with a virtual base), every
// flat slot is pointed here instead of the flattened handler.  The dispatch
// then costs exactly what the legacy path cost — no hang, no mis-dispatch,
// and no branch added to the hot path.
int Interpreter::armLegacyFallback(Interpreter *cpu, uint32_t opcode) {
    return (cpu->*armInstrs[armIndex(opcode)])(opcode);
}

int Interpreter::thumbLegacyFallback(Interpreter *cpu, uint16_t opcode) {
    return (cpu->*thumbInstrs[thumbIndex(opcode)])(opcode);
}

void Interpreter::buildDispatchTables() {
    if (dispatchTablesReady)
        return;

    // One-time flattening of the 8-byte PMF tables into 4-byte direct
    // function pointers.  4096 + 1024 entries (~20 KiB of .bss); the work is
    // done once at boot and never touched again.
    struct Pmf { uintptr_t fn; ptrdiff_t adj; };
    bool armOk = true, thumbOk = true;

    for (size_t i = 0; i < 0x1000; i++) {
        Pmf p; std::memcpy(&p, &armInstrs[i], sizeof(p));
        if (p.adj != 0 || (p.fn & 1u) != 0) { armOk = false; break; }
        armInstrsFlat[i] = flattenPmf(armInstrs[i]);
    }
    for (size_t i = 0; i < 0x400; i++) {
        Pmf p; std::memcpy(&p, &thumbInstrs[i], sizeof(p));
        if (p.adj != 0 || (p.fn & 1u) != 0) { thumbOk = false; break; }
        thumbInstrsFlat[i] = flattenPmf(thumbInstrs[i]);
    }

    // Degrade gracefully rather than dispatching through a wrong pointer.
    if (!armOk)
        for (size_t i = 0; i < 0x1000; i++) armInstrsFlat[i] = &armLegacyFallback;
    if (!thumbOk)
        for (size_t i = 0; i < 0x400; i++) thumbInstrsFlat[i] = &thumbLegacyFallback;

    dispatchTablesReady = true;
}

#else  // !NOODS_THREADED_DISPATCH

bool Interpreter::dispatchTablesReady = true;
void Interpreter::buildDispatchTables() {}

#endif
