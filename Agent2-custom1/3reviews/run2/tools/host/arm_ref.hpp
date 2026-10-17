// arm_ref.hpp -- executable transcription of the NooDS ARM handlers for the
// instruction subset jit3 translates in slice 1.
//
// Every case cites the handler it was copied from (NooDS-Wii/interpreter_alu.cpp,
// interpreter_branch.cpp, interpreter_transfer.cpp).  The point is to have the
// *specification* in executable form: the JIT is then diffed against it for
// thousands of operand/flag combinations instead of against my memory of the code.
//
// Conventions copied from Interpreter::runOpcode():
//   * while a handler runs, registers[15] is the instruction address + 8 (ARM),
//     which is what the handlers use for r15 reads;
//   * a handler returning after a jump leaves registers[15] = target + 4 (ARM),
//     i.e. "next instruction address + 4", because flushPipeline() adds the step
//     after masking.  So between instructions:  registers[15] == nextPc + 4.
//   * step() returns the cycle count the handler returns, or -1 when the case is
//     deliberately not modelled (host-undefined behaviour, SPSR, mode switching).
#ifndef JIT3_ARM_REF_HPP
#define JIT3_ARM_REF_HPP

#include "../../NooDS-Wii/arm7tdmi_mul.hpp"
#include <cstdint>

namespace jit3ref {

#define RBIT(n) (1u << (n))

struct ArmRef {
    uint32_t regs[16] = {};
    uint32_t cpsr = 0x10;                   // System mode, ARM state
    uint8_t* ram = nullptr;                 // guest RAM (little-endian)
    uint32_t base = 0;                      // guest address of ram[0]
    bool arm7 = false;

    uint32_t rd32(uint32_t a) const {
        const uint8_t* p = ram + (a - base);
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    bool condTrue(uint32_t cond) const {
        const bool n = cpsr & RBIT(31), z = cpsr & RBIT(30), c = cpsr & RBIT(29), v = cpsr & RBIT(28);
        switch (cond) {
            case 0x0: return z;
            case 0x1: return !z;
            case 0x2: return c;
            case 0x3: return !c;
            case 0x4: return n;
            case 0x5: return !n;
            case 0x6: return v;
            case 0x7: return !v;
            case 0x8: return c && !z;
            case 0x9: return !(c && !z);
            case 0xA: return n == v;
            case 0xB: return n != v;
            case 0xC: return !z && (n == v);
            case 0xD: return z || (n != v);
            case 0xE: return true;
            default:  return false;             // reserved: handled by the caller
        }
    }

    // ---------------------------------------------------------------- shifters
    // interpreter_alu.cpp, "Define functions for each ARM shift variation"
    uint32_t sh_lli(uint32_t op, bool S) {
        uint32_t value = regs[op & 0xF];
        uint8_t shift = (op >> 7) & 0x1F;
        if (S && shift > 0) cpsr = (cpsr & ~RBIT(29)) | ((!!(value & RBIT(32 - shift))) << 29);
        return value << shift;
    }
    uint32_t sh_lri(uint32_t op, bool S) {
        uint32_t value = regs[op & 0xF];
        uint8_t shift = (op >> 7) & 0x1F;
        if (S) cpsr = (cpsr & ~RBIT(29)) | ((!!(value & RBIT(shift ? (shift - 1) : 31))) << 29);
        return shift ? (value >> shift) : 0;
    }
    uint32_t sh_ari(uint32_t op, bool S) {
        int32_t value = (int32_t)regs[op & 0xF];
        uint8_t shift = (op >> 7) & 0x1F;
        if (S) cpsr = (cpsr & ~RBIT(29)) | ((!!(((uint32_t)value) & RBIT(shift ? (shift - 1) : 31))) << 29);
        return (uint32_t)(value >> (shift ? shift : 31));
    }
    uint32_t sh_rri(uint32_t op, bool S) {
        uint32_t value = regs[op & 0xF];
        uint8_t shift = (op >> 7) & 0x1F;
        uint32_t res = shift ? ((value << (32 - shift)) | (value >> shift))
                             : (((cpsr & RBIT(29)) << 2) | (value >> 1));
        if (S) cpsr = (cpsr & ~RBIT(29)) | ((!!(value & RBIT(shift ? (shift - 1) : 0))) << 29);
        return res;
    }
    uint32_t sh_imm(uint32_t op, bool S) {
        uint32_t value = op & 0xFF;
        uint8_t shift = (op >> 7) & 0x1E;
        if (S && shift > 0) cpsr = (cpsr & ~RBIT(29)) | ((!!(value & RBIT(shift - 1))) << 29);
        return shift ? ((value << (32 - shift)) | (value >> shift)) : value;
    }
    uint32_t sh_llr(uint32_t op, bool S) {
        uint32_t value = regs[op & 0xF] + (((op & 0xF) == 0xF) << 2);
        uint8_t shift = (uint8_t)regs[(op >> 8) & 0xF];
        if (S && shift > 0) cpsr = (cpsr & ~RBIT(29)) | ((!!(shift <= 32 && (value & RBIT(32 - shift)))) << 29);
        return (shift < 32) ? (value << shift) : 0;
    }
    uint32_t sh_lrr(uint32_t op, bool S) {
        uint32_t value = regs[op & 0xF] + (((op & 0xF) == 0xF) << 2);
        uint8_t shift = (uint8_t)regs[(op >> 8) & 0xF];
        if (S && shift > 0) cpsr = (cpsr & ~RBIT(29)) | ((!!(shift <= 32 && (value & RBIT(shift - 1)))) << 29);
        return (shift < 32) ? (value >> shift) : 0;
    }
    uint32_t sh_arr(uint32_t op, bool S) {
        int32_t value = (int32_t)(regs[op & 0xF] + (((op & 0xF) == 0xF) << 2));
        uint8_t shift = (uint8_t)regs[(op >> 8) & 0xF];
        if (S && shift > 0) cpsr = (cpsr & ~RBIT(29)) | ((!!(((uint32_t)value) & RBIT((shift <= 32) ? (shift - 1) : 31))) << 29);
        return (uint32_t)(value >> ((shift < 32) ? shift : 31));
    }
    uint32_t sh_rrr(uint32_t op, bool S) {
        uint32_t value = regs[op & 0xF] + (((op & 0xF) == 0xF) << 2);
        uint8_t shift = (uint8_t)regs[(op >> 8) & 0xF];
        if (S && shift > 0) cpsr = (cpsr & ~RBIT(29)) | ((!!(value & RBIT((shift - 1) & 0x1F))) << 29);
        uint32_t n = shift & 0x1F;
        return n ? ((value >> n) | (value << (32 - n))) : value;    // ARM: Rs == 0 means no shift
    }

    // -------------------------------------------------------------- pipelines
    void flushPipeline(uint32_t target) {
        if (cpsr & RBIT(5)) regs[15] = (target & ~0x1u) + 2;   // THUMB
        else                regs[15] = (target & ~0x3u) + 4;   // ARM
    }

    // ------------------------------------------------------------- instruction
    // Returns cycles, or -1 when the instruction is not modelled by this reference.
    int step(uint32_t pc) {
        const uint32_t op = rd32(pc);
        const uint32_t cond = op >> 28;
        regs[15] = pc + 8;                        // what handlers see while executing
        if (cond == 0xF) return -1;               // reserved encoding: handleReserved
        if (!condTrue(cond)) return 1;            // condition[] table: "false" costs 1
        const uint32_t cls = (op >> 25) & 7;
        const uint32_t dp = (op >> 21) & 0xF;
        const bool s = (op >> 20) & 1;
        const uint32_t rd = (op >> 12) & 0xF;
        const uint32_t rn = (op >> 16) & 0xF;

        // ---- branch and exchange --------------------------------------------
        if (cls == 5) {                           // B / BL
            int32_t off = (int32_t)(op << 8) >> 6;
            if (op & RBIT(24)) regs[14] = regs[15] - 4;      // bl: LR = pc + 4
            regs[15] += (uint32_t)off;                       // b: r15 += offset
            flushPipeline(regs[15]);
            return 3;
        }
        if ((op & 0x0FFFFFF0u) == 0x012FFF10u) {              // BX Rm
            uint32_t t = regs[op & 0xF];
            cpsr |= (t & RBIT(0)) << 5;                       // T is OR-ed in, never cleared
            flushPipeline(t);
            return 3;
        }
        if ((op & 0x0FFFFFF0u) == 0x012FFF30u) {              // BLX Rm
            if (arm7) return 1;                               // ARM9 exclusive
            uint32_t t = regs[op & 0xF];
            cpsr |= (t & RBIT(0)) << 5;
            regs[14] = regs[15] - 4;
            flushPipeline(t);
            return 3;
        }
        // ---- status register transfers ---------------------------------------
        if ((op & 0x0FBF0FFFu) == 0x010F0000u) {              // MRS Rd,CPSR
            if (rd == 15) return -1;
            regs[rd] = cpsr;
            return 2 - (int)arm7;
        }
        if ((op & 0x0FB0FFF0u) == 0x0120F000u) {              // MSR CPSR_<fields>,Rm
            if (op & RBIT(16)) return -1;                     // c byte: setCpsr() side effects
            uint32_t v = regs[op & 0xF];
            for (int i = 1; i < 4; i++)
                if (op & RBIT(16 + i))
                    cpsr = (cpsr & ~(0xFFu << (i << 3))) | (v & (0xFFu << (i << 3)));
            return 1;
        }
        if ((op & 0x0FB0F000u) == 0x0320F000u) {              // MSR CPSR_<fields>,#imm
            if (op & RBIT(16)) return -1;
            uint32_t v = sh_imm(op, false);
            for (int i = 1; i < 4; i++)
                if (op & RBIT(16 + i))
                    cpsr = (cpsr & ~(0xFFu << (i << 3))) | (v & (0xFFu << (i << 3)));
            return 1;
        }
        // ---- multiply --------------------------------------------------------
        // UMULL/SMULL/UMLAL/SMLAL.  The reference has to model what NooDS does (the
        // interpreter is the comparison base for VERIFY), and since the mGBA suite
        // pass the S bit is honored the same way on both sides.
        if ((op & 0x0F8000F0u) == 0x00800090u) {
            const uint32_t rdHi = (op >> 16) & 0xF, rdLo = (op >> 12) & 0xF;
            const uint32_t rs = (op >> 8) & 0xF, rm = op & 0xF;
            if (rdHi == 15 || rdLo == 15 || rs == 15 || rm == 15) return -1;
            const bool s = (op >> 20) & 1, acc = (op >> 21) & 1, uns = (op >> 22) & 1;
            // the operands are captured before the destination registers are written:
            // RdLo/RdHi may alias Rm/Rs, and the flags come from the original values
            const uint32_t rmVal = regs[rm], rsVal = regs[rs];
            const uint64_t addend = acc ? (((uint64_t)regs[rdHi] << 32) | regs[rdLo]) : 0;
            uint64_t res;
            if (uns) res = (uint64_t)rmVal * rsVal;
            else     res = (uint64_t)((int64_t)(int32_t)rmVal * (int32_t)rsVal);
            res += addend;
            regs[rdLo] = (uint32_t)res;
            regs[rdHi] = (uint32_t)(res >> 32);
            if (s) {
                cpsr = (cpsr & ~0xC0000000u) |
                       ((uint32_t)(res >> 32) & RBIT(31)) | ((res == 0) << 30);
                // ARM7 (ARM7TDMI) also records the multiplier's carry-out - the thing
                // the mGBA suite's multiply-long table actually checks.  The ARM9
                // (ARM946E-S) leaves C alone, and so does jit3 there.
                if (arm7) cpsr = (cpsr & ~RBIT(29)) |
                                 (Arm7TdmiMul::longMultiplyCarry(rmVal, rsVal, !uns, addend) << 29);
            }
            if (arm7) {
                // the interpreter's early-termination cycle count
                const int32_t sop = (int32_t)rsVal;
                int m; for (m = 1; (uns ? (rsVal < (uint32_t)(-1 << (m << 3)) ||
                                           rsVal >= (uint32_t)(1 << (m << 3)))
                                      : (sop < (-1 << (m << 3)) || sop >= (1 << (m << 3))))
                              && m < 4; m++);
                return m + 2 + (acc ? 1 : 0);
            }
            return 3;
        }
        if ((op & 0x0FC000F0u) == 0x00000090u) {              // MUL / MLA / variants
            if (arm7) return -1;                              // variable cycle count
            const bool acc = (op >> 21) & 1;
            const uint32_t rs = (op >> 8) & 0xF;
            // NooDS's mul/muls/mlass never flush the pipeline, so a PC destination
            // leaves the stale prefetch in place - deliberately unmodelled.
            if (!acc && ((op >> 16) & 0xF) == 15) return -1;
            uint32_t a = regs[op & 0xF], b = regs[rs];
            regs[rn] = (uint32_t)((int32_t)a * (int32_t)b) + (acc ? regs[rd] : 0u);
            if (rd == 15 || rs == 15 || (op & 0xF) == 15) return -1;    // PC operands
            if (s) cpsr = (cpsr & ~0xC0000000u) | (regs[rn] & RBIT(31)) | ((regs[rn] == 0) << 30);
            return s ? 4 : 2;
        }
        if (cls != 0 && cls != 1) return -1;                  // loads/stores/... slice 2

        // ---- data processing -------------------------------------------------
        if (dp >= 8 && dp <= 11 && !s) return -1;             // test ops need S
        const bool imm = (op >> 25) & 1;
        const bool regShift = !imm && ((op >> 4) & 1);
        uint32_t op2;
        if (imm)                        op2 = sh_imm(op, s);
        else if (regShift)              op2 = ((op >> 5) & 3) == 0 ? sh_llr(op, s)
                                        : ((op >> 5) & 3) == 1 ? sh_lrr(op, s)
                                        : ((op >> 5) & 3) == 2 ? sh_arr(op, s) : sh_rrr(op, s);
        else                            op2 = ((op >> 5) & 3) == 0 ? sh_lli(op, s)
                                        : ((op >> 5) & 3) == 1 ? sh_lri(op, s)
                                        : ((op >> 5) & 3) == 2 ? sh_ari(op, s) : sh_rri(op, s);

        // "When used as Rn when shifting by register, the program counter is read
        //  with +4" -- the (opcode & 0x20F0010) == 0x00F0010 quirk
        const uint32_t pcQuirk = (((op & 0x20F0010u) == 0x00F0010u) << 2);
        const uint32_t op1 = regs[rn] + pcQuirk;    // every handler carries that term

        const bool test = (dp >= 8 && dp <= 11);
        const bool toPc = (rd == 15 && !test);
        if (toPc && s) return -1;                            // S + PC: exception return
        uint32_t* dst = toPc ? &regs[15] : &regs[rd];
        uint32_t res = 0;
        switch (dp) {
            case 0:  res = op1 & op2; break;                              // and
            case 1:  res = op1 ^ op2; break;                              // eor
            case 2:  res = op1 - op2; break;                              // sub
            case 3:  res = op2 - op1; break;                              // rsb
            case 4:  res = op1 + op2; break;                              // add
            case 5:  res = op1 + op2 + ((cpsr & RBIT(29)) >> 29); break;   // adc
            case 6:  res = op1 - op2 - 1 + ((cpsr & RBIT(29)) >> 29); break;  // sbc
            case 7:  res = op2 - op1 - 1 + ((cpsr & RBIT(29)) >> 29); break;  // rsc
            case 8:  res = op1 & op2; break;                              // tst
            case 9:  res = op1 ^ op2; break;                              // teq
            case 10: res = op1 - op2; break;                              // cmp
            case 11: res = op1 + op2; break;                              // cmn
            case 12: res = op1 | op2; break;                              // orr
            case 13: res = op2; break;                                    // mov
            case 14: res = op1 & ~op2; break;                             // bic
            default: res = ~op2; break;                                   // mvn
        }
        if (s || test) {
            const uint32_t R = res;                    // N,Z,C,V exactly as the handlers do
            switch (dp) {
                case 8: case 9:
                    cpsr = (cpsr & ~0xC0000000u) | (R & RBIT(31)) | ((R == 0) << 30); break;
                case 10: case 2:
                    cpsr = (cpsr & ~0xF0000000u) | (R & RBIT(31)) | ((R == 0) << 30) |
                        ((op1 >= R) << 29) | (((op2 ^ op1) & ~(R ^ op2) & RBIT(31)) >> 3); break;
                case 11: case 4:
                    cpsr = (cpsr & ~0xF0000000u) | (R & RBIT(31)) | ((R == 0) << 30) |
                        ((op1 > R) << 29) | ((~(op2 ^ op1) & (R ^ op2) & RBIT(31)) >> 3); break;
                case 5:
                    cpsr = (cpsr & ~0xF0000000u) | (R & RBIT(31)) | ((R == 0) << 30) |
                        ((op1 > R || (op2 == 0xFFFFFFFFu && (cpsr & RBIT(29)))) << 29) |
                        ((~(op2 ^ op1) & (R ^ op2) & RBIT(31)) >> 3); break;
                case 6:
                    cpsr = (cpsr & ~0xF0000000u) | (R & RBIT(31)) | ((R == 0) << 30) |
                        ((op1 >= R && (op2 != 0xFFFFFFFFu || (cpsr & RBIT(29)))) << 29) |
                        (((op2 ^ op1) & ~(R ^ op2) & RBIT(31)) >> 3); break;
                case 3:
                    cpsr = (cpsr & ~0xF0000000u) | (R & RBIT(31)) | ((R == 0) << 30) |
                        ((op2 >= R) << 29) | (((op1 ^ op2) & ~(R ^ op1) & RBIT(31)) >> 3); break;
                case 7:   // rscs replaces all four flags, like the other S forms
                    cpsr = (cpsr & ~0xF0000000u) | (R & RBIT(31)) | ((R == 0) << 30) |
                        ((op2 >= R && (op1 != 0xFFFFFFFFu || (cpsr & RBIT(29)))) << 29) |
                        (((op1 ^ op2) & ~(R ^ op1) & RBIT(31)) >> 3); break;
                default:   // logical S forms: only N and Z
                    cpsr = (cpsr & ~0xC0000000u) | (R & RBIT(31)) | ((R == 0) << 30); break;
            }
        }
        if (test) return 1;
        *dst = res;
        if (toPc) { flushPipeline(regs[15]); return 3; }
        return 1 + (regShift ? 1 : 0);               // ALU_FUNCS adds 1 for the *r forms
    }
};

}  // namespace jit3ref

#endif
