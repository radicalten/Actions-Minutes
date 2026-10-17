// ppc_dis.hpp - tiny PowerPC 750CL (Broadway) disassembler for the host harness.
//
// It only needs to cover the subset the JIT can emit plus the couple of
// sequences a human wants to read when a block misbehaves.  Unknown encodings
// are printed as "???" with their primary opcode, which is enough to spot a
// broken encoder.
#ifndef JIT3_PPC_DIS_HPP
#define JIT3_PPC_DIS_HPP

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ppcdis {

// IBM-numbered field masks (bit 0 = MSB), converted to shifts for clarity.
static inline uint32_t f(uint32_t insn, int first, int last) {   // inclusive, IBM numbering
    int width = last - first + 1;
    return (insn >> (31 - last)) & ((width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1));
}
static inline int32_t sx(uint32_t v, int bits) {
    int sh = 32 - bits;
    return (int32_t)(v << sh) >> sh;
}

struct Out {                 // accumulate into a fixed buffer, no allocation
    char buf[128];
    void reset() { buf[0] = 0; }
    void app(const char* fmt, ...) {
        va_list ap; va_start(ap, fmt);
        size_t n = std::strlen(buf);
        std::vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
        va_end(ap);
    }
};

// rlwinm/rlwimi/rlwnm detail
static void disRlw(uint32_t insn, Out& o, const char* mnem) {
    uint32_t rs = f(insn, 6, 10), ra = f(insn, 11, 15), sh = f(insn, 16, 20);
    uint32_t mb = f(insn, 21, 25), me = f(insn, 26, 30), rc = f(insn, 31, 31);
    o.app("%s r%u,r%u,%u,%u,%u%s", mnem, ra, rs, sh, mb, me, rc ? "." : "");
}

static void disX(uint32_t insn, Out& o) {
    // X-form  is  "rA = rS op rB"  (destination in bits 11-15)
    // XO-form is  "rD = rA op rB"  (destination in bits 6-10), so the operand that
    // has to be printed first flips between the two families.
    uint32_t rs = f(insn, 6, 10), ra = f(insn, 11, 15), rb = f(insn, 16, 20);
    const uint32_t rD = rs, rA = ra, rB = rb;
    uint32_t xo = f(insn, 21, 30), rc = f(insn, 31, 31);
    switch (xo) {
        case 0:   o.app("cmpw cr%u,r%u,r%u", f(insn, 6, 8), ra, rb); break;
        case 32:  o.app("cmplw cr%u,r%u,r%u", f(insn, 6, 8), ra, rb); break;
        case 40: o.app("subf%s r%u,r%u,r%u", rc ? "." : "", rD, rA, rB); break;
        case 104: o.app("neg%s r%u,r%u", rc ? "." : "", rD, rA); break;
        case 136: o.app("subfe%s r%u,r%u,r%u", rc ? "." : "", rD, rA, rB); break;
        case 138: o.app("adde%s r%u,r%u,r%u", rc ? "." : "", rD, rA, rB); break;
        case 200: o.app("subfze%s r%u,r%u", rc ? "." : "", rD, rA); break;
        case 202: o.app("addze%s r%u,r%u", rc ? "." : "", rD, rA); break;
        case 8:   o.app("subfc%s r%u,r%u,r%u", rc ? "." : "", ra, rs, rb); break;
        case 10: o.app("addc%s r%u,r%u,r%u", rc ? "." : "", rD, rA, rB); break;
        case 235: o.app("mullw%s r%u,r%u,r%u", rc ? "." : "", rD, rA, rB); break;
        case 266: o.app("add%s r%u,r%u,r%u", rc ? "." : "", rD, rA, rB); break;
        case 28:  o.app("and%s r%u,r%u,r%u", rc ? "." : "", ra, rs, rb); break;
        case 444: o.app("or%s r%u,r%u,r%u", rc ? "." : "", ra, rs, rb); break;
        case 316: o.app("xor%s r%u,r%u,r%u", rc ? "." : "", ra, rs, rb); break;
        case 476: o.app("nand%s r%u,r%u,r%u", rc ? "." : "", ra, rs, rb); break;
        case 124: o.app("nor%s r%u,r%u,r%u", rc ? "." : "", ra, rs, rb); break;
        case 284: o.app("eqv%s r%u,r%u,r%u", rc ? "." : "", ra, rs, rb); break;
        case 60:  o.app("andc%s r%u,r%u,r%u", rc ? "." : "", ra, rs, rb); break;
        case 412: o.app("orc%s r%u,r%u,r%u", rc ? "." : "", ra, rs, rb); break;
        case 24:  o.app("slw%s r%u,r%u,r%u", rc ? "." : "", ra, rs, rb); break;
        case 536: o.app("srw%s r%u,r%u,r%u", rc ? "." : "", ra, rs, rb); break;
        case 792: o.app("sraw%s r%u,r%u,r%u", rc ? "." : "", ra, rs, rb); break;
        case 26:  o.app("cntlzw%s r%u,r%u", rc ? "." : "", ra, rs); break;
        case 922: o.app("extsh%s r%u,r%u", rc ? "." : "", ra, rs); break;
        case 954: o.app("extsb%s r%u,r%u", rc ? "." : "", ra, rs); break;
        case 986: o.app("extsw%s r%u,r%u", rc ? "." : "", ra, rs); break;
        case 23:  o.app("lwzx r%u,r%u,r%u", ra, rs, rb); break;
        case 151: o.app("stwx r%u,r%u,r%u", rs, ra, rb); break;
        case 87:  o.app("lbzx r%u,r%u,r%u", ra, rs, rb); break;
        case 215: o.app("stbx r%u,r%u,r%u", rs, ra, rb); break;
        case 279: o.app("lhzx r%u,r%u,r%u", ra, rs, rb); break;
        case 407: o.app("sthx r%u,r%u,r%u", rs, ra, rb); break;
        case 534: o.app("lwbrx r%u,r%u,r%u", ra, rs, rb); break;
        case 662: o.app("stwbrx r%u,r%u,r%u", rs, ra, rb); break;
        case 790: o.app("lhbrx r%u,r%u,r%u", ra, rs, rb); break;
        case 918: o.app("sthbrx r%u,r%u,r%u", rs, ra, rb); break;
        case 339: o.app("mfspr r%u,spr%u", ra, (f(insn, 16, 20) << 5) | f(insn, 11, 15)); break;
        case 467: o.app("mtspr spr%u,r%u", (f(insn, 16, 20) << 5) | f(insn, 11, 15), rs); break;
        case 19:  switch (f(insn, 6, 10)) {
            case 16: o.app("bclr %u,%u", f(insn, 11, 15), f(insn, 16, 20)); break;
            case 528: o.app("bcctr %u,%u", f(insn, 11, 15), f(insn, 16, 20)); break;
            case 33:  o.app("crnor"); break;
            case 129: o.app("crandc"); break;
            case 193: o.app("crxor"); break;
            case 225: o.app("crnand"); break;
            case 257: o.app("crand"); break;
            case 289: o.app("creqv"); break;
            case 417: o.app("crorc"); break;
            case 449: o.app("cror"); break;
            default:  o.app("op19/%u", f(insn, 6, 10)); break;
          } break;
        default:  o.app("X/%u", xo); break;
    }
}

static void disasm(uint32_t insn, char* out, size_t n) {
    Out o; o.reset();
    uint32_t op = f(insn, 0, 5);
    uint32_t rt = f(insn, 6, 10), ra = f(insn, 11, 15);
    int32_t simm = sx(f(insn, 16, 31), 16);
    switch (op) {
        case 0: o.app("??? 0x%08X", insn); break;
        case 14: o.app("addi r%u,r%u,%d", rt, ra, simm); break;
        case 15: o.app("addis r%u,r%u,%d", rt, ra, simm); break;
        case 24: o.app("ori r%u,r%u,0x%X", ra, rt, f(insn, 16, 31)); break;
        case 25: o.app("oris r%u,r%u,0x%X", ra, rt, f(insn, 16, 31)); break;
        case 26: o.app("xori r%u,r%u,0x%X", ra, rt, f(insn, 16, 31)); break;
        case 27: o.app("xoris r%u,r%u,0x%X", ra, rt, f(insn, 16, 31)); break;
        case 28: o.app("andi%s r%u,r%u,0x%X", f(insn, 31, 31) ? "." : "", ra, rt, f(insn, 16, 31)); break;
        case 29: o.app("andis%s r%u,r%u,0x%X", f(insn, 31, 31) ? "." : "", ra, rt, f(insn, 16, 31)); break;
        case 32: o.app("lwz r%u,%d(r%u)", rt, simm, ra); break;
        case 33: o.app("lwzu r%u,%d(r%u)", rt, simm, ra); break;
        case 34: o.app("lbz r%u,%d(r%u)", rt, simm, ra); break;
        case 35: o.app("lbzu r%u,%d(r%u)", rt, simm, ra); break;
        case 36: o.app("stw r%u,%d(r%u)", rt, simm, ra); break;
        case 37: o.app("stwu r%u,%d(r%u)", rt, simm, ra); break;
        case 38: o.app("stb r%u,%d(r%u)", rt, simm, ra); break;
        case 39: o.app("stbu r%u,%d(r%u)", rt, simm, ra); break;
        case 40: o.app("lhz r%u,%d(r%u)", rt, simm, ra); break;
        case 42: o.app("lha r%u,%d(r%u)", rt, simm, ra); break;
        case 44: o.app("sth r%u,%d(r%u)", rt, simm, ra); break;
        case 46: o.app("lmw r%u,%d(r%u)", rt, simm, ra); break;
        case 47: o.app("stmw r%u,%d(r%u)", rt, simm, ra); break;
        case 16: o.app("bc %u,%u,0x%X", f(insn, 6, 10), f(insn, 11, 15), (uint32_t)sx(f(insn, 16, 29) << 2, 16));
                 break;
        case 18: o.app("b%s 0x%X", f(insn, 31, 31) ? "l" : "", (uint32_t)sx((f(insn, 6, 29) << 2), 26)); break;
        case 19: disX(insn, o); break;
        case 20: disRlw(insn, o, "rlwimi"); break;
        case 21: disRlw(insn, o, "rlwinm"); break;
        case 23: disRlw(insn, o, "rlwnm"); break;
        case 31: disX(insn, o); break;
        default: o.app("op%u 0x%08X", op, insn); break;
    }
    std::snprintf(out, n, "%s", o.buf);
}

}  // namespace ppcdis

#endif
