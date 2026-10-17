// ppc_sim.hpp -- a small PowerPC 750CL (Broadway) interpreter for host testing.
//
// The emitted blocks are real PowerPC machine code, so they cannot be executed on
// an x86 host.  Instead this file runs them on a *functional model* of the CPU:
// one step per instruction, big-endian memory, IBM numbering throughout (bit 0 =
// MSB), and a fault report instead of a crash whenever a test goes wrong.
//
// It deliberately covers only what jit3 emits, and it decodes from the manuals
// rather than reusing jit3/ppc.hpp, so an encoder bug shows up as a value mismatch
// instead of being mirrored by a matching bug on the model side.
#ifndef JIT3_PPC_SIM_HPP
#define JIT3_PPC_SIM_HPP

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace ppcsim {

// ------------------------------------------------------------------ helpers
// IBM-numbered field extraction: field(first,last) with bit 0 = MSB.
static inline uint32_t fld(uint32_t insn, int first, int last) {
    int w = last - first + 1;
    return (insn >> (31 - last)) & ((w >= 32) ? 0xFFFFFFFFu : ((1u << w) - 1));
}
static inline int32_t sext(uint32_t v, int bits) { int sh = 32 - bits; return (int32_t)(v << sh) >> sh; }

struct Sim {
    // ---- architectural state ------------------------------------------------
    uint32_t gpr[32] = {};
    uint32_t lr = 0, ctr = 0, xer = 0, cr = 0;
    // xer is kept in IBM bit layout: bit 0 = SO, 1 = OV, 2 = CA
    static constexpr uint32_t XER_CA = 1u << 29;   // IBM bit 2
    static constexpr uint32_t XER_OV = 1u << 30;   // IBM bit 1
    static constexpr uint32_t XER_SO = 1u << 31;   // IBM bit 0 (sticky)

    uint8_t* mem = nullptr;
    size_t   memSize = 0;

    uint64_t steps = 0, stepLimit = 100000;
    bool stopped = false;              // blr with LR == sentinel
    bool fault_ = false;
    char faultBuf[160] = {};

    void fault(const char* what, uint32_t addr) {
        if (fault_) return;                       // report the first one only
        fault_ = true;
        std::snprintf(faultBuf, sizeof faultBuf, "%s at 0x%08X", what, addr);
    }

    // ---- memory ------------------------------------------------------------
    bool inRange(uint32_t a, size_t n) const {
        return (size_t)a + n <= memSize;
    }
    bool rd8(uint32_t a, uint8_t& v)  { if (!inRange(a, 1)) { fault("load8", a); return false; } v = mem[a]; return true; }
    bool wr8(uint32_t a, uint8_t v)   { if (!inRange(a, 1)) { fault("store8", a); return false; } mem[a] = v; return true; }
    bool rd16(uint32_t a, uint16_t& v) {
        if (!inRange(a, 2)) { fault("load16", a); return false; }
        v = (uint16_t)((mem[a] << 8) | mem[a + 1]); return true;      // big-endian
    }
    bool wr16(uint32_t a, uint16_t v) {
        if (!inRange(a, 2)) { fault("store16", a); return false; }
        mem[a] = (uint8_t)(v >> 8); mem[a + 1] = (uint8_t)v; return true;
    }
    bool rd32(uint32_t a, uint32_t& v) {
        if (!inRange(a, 4)) { fault("load32", a); return false; }
        v = ((uint32_t)mem[a] << 24) | ((uint32_t)mem[a + 1] << 16) | ((uint32_t)mem[a + 2] << 8) | mem[a + 3];
        return true;
    }
    bool wr32(uint32_t a, uint32_t v) {
        if (!inRange(a, 4)) { fault("store32", a); return false; }
        mem[a] = (uint8_t)(v >> 24); mem[a + 1] = (uint8_t)(v >> 16);
        mem[a + 2] = (uint8_t)(v >> 8); mem[a + 3] = (uint8_t)v; return true;
    }
    // byte-reversed forms (little-endian guest memory)
    bool rd32br(uint32_t a, uint32_t& v) { uint32_t t; if (!rd32(a, t)) return false; v = __builtin_bswap32(t); return true; }
    bool rd16br(uint32_t a, uint16_t& v) { uint16_t t; if (!rd16(a, t)) return false; v = (uint16_t)__builtin_bswap16(t); return true; }

    // ---- CR helpers --------------------------------------------------------
    // CR layout: field crf occupies IBM bits crf*4 .. crf*4+3 == LT,GT,EQ,SO.
    bool crb(int bit) const { return (cr & (0x80000000u >> (bit & 31))) != 0; }
    void setCrb(int bit, bool v) {
        uint32_t m = 0x80000000u >> (bit & 31);
        cr = v ? (cr | m) : (cr & ~m);
    }
    void setCRField(int crf, uint32_t value) {
        uint32_t shift = 28 - crf * 4;
        cr = (cr & ~(0xFu << shift)) | ((value & 0xF) << shift);
    }
    // compare: 0b1000 = LT, 0b0100 = GT, 0b0010 = EQ, plus SO copied from XER
    void compare(int crf, uint32_t a, uint32_t b, bool sign) {
        int32_t sa = sign ? (int32_t)a : (int32_t)(a & 0x7FFFFFFF);
        int32_t sb = sign ? (int32_t)b : (int32_t)(b & 0x7FFFFFFF);
        uint32_t v = 0;
        if (sa < sb) v = 0x8; else if (sa > sb) v = 0x4; else v = 0x2;
        if (xer & XER_SO) v |= 0x1;
        setCRField(crf, v);
    }

    // ---- execution ---------------------------------------------------------
    void run(uint32_t entry, uint32_t sentinelLr) {
        uint32_t pc = entry;
        while (!fault_ && !stopped && steps < stepLimit) {
            if (steps++ > 20000000u) { fault("step limit", pc); return; }
            uint32_t insn = 0;
            if (!rd32(pc, insn)) return;
            uint32_t next = pc + 4;
            bool br = stepOne(pc, insn, next);
            if (stopped || fault_) return;
            if (!br) pc = next;
            else pc = next;                       // stepOne already set `next` via ctr/lr
        }
    }

    // returns true when the instruction was an unconditional jump that the caller
    // should honour through `jumpTarget`
    bool stepOne(uint32_t pc, uint32_t insn, uint32_t& nextPc) {
        const uint32_t op = fld(insn, 0, 5);
        const int rt = (int)fld(insn, 6, 10), ra = (int)fld(insn, 11, 15);
        const uint32_t d = fld(insn, 16, 31);
        const int32_t sd = sext(d, 16);
        switch (op) {
        case 7:  gpr[rt] = (uint32_t)(int32_t)(int8_t)(uint8_t)fld(insn, 16, 20); break;   // mulli
        case 8:  gpr[rt] = (uint32_t)(sd & ~3); break;                                    // subfic
        case 10: { uint64_t t = (uint64_t)d + (uint64_t)~(uint64_t)gpr[ra] + 1;
                   gpr[rt] = (uint32_t)t; setCa(t >> 32); if (fld(insn,31,31)) setCr0((int32_t)gpr[rt]); break; }  // subfc... (unused)
        case 11: { uint64_t t = (uint64_t)d + (uint64_t)~gpr[ra] + ((xer & XER_CA) ? 1u : 0u) + 1u;
                   uint32_t res = (uint32_t)t;
                   setOvOverflow(!((gpr[ra] & 0x80000000u) && !(res & 0x80000000u)));
                   setCa(t >> 32); gpr[rt] = res; if (fld(insn,31,31)) setCr0((int32_t)res); break; }  // subfe
        case 12: { int32_t r = (int32_t)gpr[ra] + (int32_t)d;
                   setOv(!(((gpr[ra] ^ d) & 0x80000000u) == 0 && ((gpr[ra] ^ (uint32_t)r) & 0x80000000u) != 0));
                   gpr[rt] = (uint32_t)r; if (fld(insn,31,31)) setCr0(r); break; }         // addic/subic form
        case 13: { int32_t r = (int32_t)gpr[ra] + sd;
                   bool ov = ((int32_t)gpr[ra] > 0 && sd > 0 && r < 0) || ((int32_t)gpr[ra] < 0 && sd < 0 && r >= 0);
                   setOv(ov); gpr[rt] = (uint32_t)r; if (fld(insn,31,31)) setCr0(r); break; }  // addic.
        case 14: gpr[rt] = (ra ? gpr[ra] : 0u) + sd; break;                               // addi / li
        case 15: gpr[rt] = (ra ? gpr[ra] : 0u) + (d << 16); break;                        // addis / lis
        case 16: { uint32_t bo = fld(insn, 6, 10), bi = fld(insn, 11, 15);
                   if (conditionOk(bo, bi)) nextPc = (fld(insn,31,31) ? 0u : pc) + sext(fld(insn, 16, 29) << 2, 16);
                   break; }
        case 18: { if (fld(insn,31,31)) lr = pc + 4;
                   nextPc = (fld(insn,31,31) ? 0u : pc) + sext(fld(insn, 6, 29) << 2, 26);
                   break; }
        case 19: stepXl(insn, nextPc); break;
        case 20: case 21: case 23: stepRlw(insn, op == 20); break;
        case 24: { uint32_t v = d; gpr[ra] = gpr[rt] | v; break; }                         // ori (nop = ori 0,0,0)
        case 25: { uint32_t v = d << 16; gpr[ra] = gpr[rt] | v; break; }                   // oris
        case 26: gpr[ra] = gpr[rt] ^ d; break;                                             // xori
        case 27: gpr[ra] = gpr[rt] ^ (d << 16); break;                                     // xoris
        case 28: { uint32_t r = gpr[rt] & d; gpr[ra] = r; setCr0((int32_t)r); break; }      // andi.
        case 29: { uint32_t r = gpr[rt] & (d << 16); gpr[ra] = r; setCr0((int32_t)r); break; }  // andis.
        case 31: stepX(insn, nextPc); break;
        case 32: { uint32_t a = (ra ? gpr[ra] : 0) + sd, v; if (rd32(a, v)) gpr[rt] = v; break; }   // lwz
        case 33: { uint32_t a = (ra ? gpr[ra] : 0) + sd, v; if (rd32(a, v)) { gpr[rt] = v; gpr[ra] = a; } break; }
        case 34: { uint8_t v; if (rd8((ra ? gpr[ra] : 0) + sd, v)) gpr[rt] = v; break; }    // lbz
        case 35: { uint8_t v; uint32_t a = (ra ? gpr[ra] : 0) + sd; if (rd8(a, v)) { gpr[rt] = v; gpr[ra] = a; } break; }
        case 36: if (!wr32((ra ? gpr[ra] : 0) + sd, gpr[rt])) return false; break;          // stw
        case 37: { uint32_t a = gpr[ra] + sd; if (!wr32(a, gpr[rt])) return false; gpr[ra] = a; break; }  // stwu
        case 38: if (!wr8((ra ? gpr[ra] : 0) + sd, (uint8_t)gpr[rt])) return false; break;  // stb
        case 39: { uint32_t a = gpr[ra] + sd; if (!wr8(a, (uint8_t)gpr[rt])) return false; gpr[ra] = a; break; }
        case 40: { uint16_t v; if (rd16((ra ? gpr[ra] : 0) + sd, v)) gpr[rt] = v; break; }  // lhz
        case 42: { uint16_t v; if (rd16((ra ? gpr[ra] : 0) + sd, v)) gpr[rt] = (uint32_t)(int32_t)(int16_t)v; break; }  // lha
        case 44: if (!wr16((ra ? gpr[ra] : 0) + sd, (uint16_t)gpr[rt])) return false; break;  // sth
        case 46: { uint32_t a = (ra ? gpr[ra] : 0) + sd;              // lmw
                   for (int r = rt; r < 32; r++) { uint32_t v; if (!rd32(a, v)) return false; gpr[r] = v; a += 4; }
                   break; }
        case 47: { uint32_t a = (ra ? gpr[ra] : 0) + sd;              // stmw
                   for (int r = rt; r < 32; r++) { if (!wr32(a, gpr[r])) return false; a += 4; }
                   break; }
        default: fault("unsupported primary opcode", pc); return false;
        }
        return true;
    }

    void setCr0(int32_t v) {                        // used by andi./addic. and X-form Rc
        uint32_t f = 0;
        if (v < 0) f |= 0x8; else if (v > 0) f |= 0x4; else f |= 0x2;
        if (xer & XER_SO) f |= 0x1;
        setCRField(0, f);
    }
    void setCa(bool c) { xer = c ? (xer | XER_CA) : (xer & ~XER_CA); }
    void setOv(bool o) {
        if (o) xer |= XER_OV; else xer &= ~XER_OV;
        if (o) xer |= XER_SO;                          // sticky summary
    }
    void setOvOverflow(bool o) { setOv(o); }

    bool conditionOk(uint32_t bo, uint32_t bi) {
        bool ctrOk = true, condOk = true;
        if ((bo & 0x10) == 0) {                        // bit 3 (0x10) set = ignore CTR
            ctr--;
            bool ctrZero = (ctr == 0);
            ctrOk = (bo & 0x08) ? ctrZero : !ctrZero;
        }
        if ((bo & 0x04) == 0) {                        // bit 2 (0x04) set = ignore CR
            bool bit = crb((int)bi);
            condOk = (bo & 0x10) ? bit : !bit;         // BO bit 4 = "branch if true"
        }
        return ctrOk && condOk;
    }

    void stepXl(uint32_t insn, uint32_t& nextPc) {
        const uint32_t xo = fld(insn, 21, 30);
        const uint32_t bt = fld(insn, 6, 10), ba = fld(insn, 11, 15), bb = fld(insn, 16, 20);
        const uint32_t bo = fld(insn, 6, 10), bi = fld(insn, 11, 15);
        switch (xo) {
        case 0:   gpr[(int)fld(insn,6,10)] = cr; break;                         // mcrf (unused here)
        case 19:  break;                                                        // mfcr: handled below
        case 150: break;                                                        // isync
        case 16:  if (conditionOk(bo, bi)) nextPc = lr & ~3u; break;            // bclr / blr
        case 528: if (fld(insn,31,31)) lr = nextPc;                             // bcctr / bctrl
                  if (conditionOk(bo, bi)) nextPc = ctr & ~3u; break;
        case 33: case 129: case 193: case 225: case 257: case 289: case 417: case 449: {
            bool a = crb((int)ba), b = crb((int)bb), r;
            switch (xo) {
                case 33:  r = !(a | b); break;                                  // crnor
                case 129: r = a & !b; break;                                    // crandc
                case 193: r = a ^ b; break;                                     // crxor
                case 225: r = !(a & b); break;                                  // crnand
                case 257: r = a & b; break;                                     // crand
                case 289: r = !(a ^ b); break;                                  // creqv
                case 417: r = a | !b; break;                                    // crorc
                default:  r = a | b; break;                                     // cror
            }
            setCrb((int)bt, r);
            break; }
        default: fault("unsupported opcode 19", (uint32_t)xo); break;
        }
    }

    void stepRlw(uint32_t insn, bool isImi) {
        const int rs = (int)fld(insn, 6, 10), ra = (int)fld(insn, 11, 15);
        const uint32_t sh = fld(insn, 16, 20), mb = fld(insn, 21, 25), me = fld(insn, 26, 30);
        const bool rc = fld(insn, 31, 31) != 0;
        uint32_t mask = 0;
        // rlwinm mask: bits MB..ME of the *rotated* value (rotating the mask with it)
        uint32_t rot = sh & 31;
        uint32_t m = 0;
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (i + rot) & 31;                 // position in the unrotated word
            bool inRange = (mb <= me) ? (pos >= mb && pos <= me) : (pos >= mb || pos <= me);
            if (inRange) m |= 0x80000000u >> i;
        }
        (void)mask;
        uint32_t rol = (gpr[rs] << rot) | (rot ? (gpr[rs] >> (32 - rot)) : 0);
        uint32_t res;
        if (isImi && insn >> 26 == 20) {                   // rlwimi: insert
            res = (gpr[ra] & ~m) | (rol & m);
        } else {
            res = rol & m;
        }
        gpr[ra] = res;
        if (rc) setCr0((int32_t)res);
    }

    void stepX(uint32_t insn, uint32_t& nextPc) {
        const uint32_t xo = fld(insn, 21, 30);
        const int rt = (int)fld(insn, 6, 10), ra = (int)fld(insn, 11, 15), rb = (int)fld(insn, 16, 20);
        const bool rc = fld(insn, 31, 31) != 0;
        const uint32_t a = gpr[ra], b = gpr[rb], s = gpr[rt];
        uint32_t r = 0; bool write = true, ov = false;
        switch (xo) {
        case 0:   compare((int)fld(insn, 6, 8), a, b, true); write = false; break;      // cmp
        case 32:  compare((int)fld(insn, 6, 8), a, b, false); write = false; break;     // cmpl
        case 40:  { uint32_t t = ~a + b + 1; r = t; setCa((uint64_t)(~a) + b + 1 > 0xFFFFFFFFu); write = false;
                    gpr[ra] = r; write = true; break; }                                  // subf rA = rB - rA
        case 8:   { uint64_t t = (uint64_t)~a + b + 1; r = (uint32_t)t; setCa(t >> 32); gpr[ra] = r; write = false; break; }  // subfc rA = rB - rA
        case 104: r = (uint32_t)(-(int32_t)a); ov = (a == 0x80000000u); gpr[ra] = r; write = false; break;   // neg
        case 136: { uint64_t t = (uint64_t)~a + b + ((xer & XER_CA) ? 1u : 0u) + 1u; r = (uint32_t)t;
                    setCa(t >> 32);
                    ov = ((a & 0x80000000u) == (b & 0x80000000u)) && ((r & 0x80000000u) != (b & 0x80000000u));
                    gpr[ra] = r; write = false; break; }                                 // subfe rA = ~rA + rB + CA
        case 138: { uint64_t t = (uint64_t)a + b + ((xer & XER_CA) ? 1u : 0u); r = (uint32_t)t;
                    setCa(t >> 32);
                    ov = ((a & 0x80000000u) == (b & 0x80000000u)) && ((r & 0x80000000u) != (a & 0x80000000u));
                    gpr[ra] = r; write = false; break; }                                 // adde  rA = rA + rB + CA
        case 10:  { uint64_t t = (uint64_t)a + b; r = (uint32_t)t; setCa(t >> 32); gpr[ra] = r; write = false; break; }  // addc
        case 200: { uint64_t t = (uint64_t)(~a) + ((xer & XER_CA) ? 1u : 0u) + 1u; r = (uint32_t)t; setCa(t >> 32);
                    gpr[ra] = r; write = false; break; }                                 // subfze
        case 202: { uint64_t t = (uint64_t)a + ((xer & XER_CA) ? 1u : 0u); r = (uint32_t)t; setCa(t >> 32);
                    gpr[ra] = r; write = false; break; }                                 // addze
        case 235: r = (uint32_t)((int32_t)a * (int32_t)b); gpr[ra] = r; write = false; break;      // mullw
        case 266: r = a + b; ov = ((a ^ r) & (b ^ r) & 0x80000000u) != 0; gpr[ra] = r; write = false; break;  // add
        case 28:  r = s & b; gpr[ra] = r; write = false; break;                          // and
        case 444: r = s | b; gpr[ra] = r; write = false; break;                          // or
        case 316: r = s ^ b; gpr[ra] = r; write = false; break;                          // xor
        case 476: r = ~(s & b); gpr[ra] = r; write = false; break;                       // nand
        case 124: r = ~(s | b); gpr[ra] = r; write = false; break;                       // nor
        case 284: r = ~(s ^ b); gpr[ra] = r; write = false; break;                       // eqv
        case 60:  r = s & ~b; gpr[ra] = r; write = false; break;                         // andc
        case 412: r = s | ~b; gpr[ra] = r; write = false; break;                         // orc
        case 24:  r = (b & 32) ? 0 : (s << (b & 31)); gpr[ra] = r; write = false; break;  // slw
        case 536: r = (b & 32) ? 0 : (s >> (b & 31)); gpr[ra] = r; write = false; break;  // srw
        case 792: { uint32_t n = b & 63;                                                  // sraw
                    r = (n > 31) ? ((s & 0x80000000u) ? 0xFFFFFFFFu : 0u) : (uint32_t)((int32_t)s >> n);
                    gpr[ra] = r; write = false; break; }
        case 824: { uint32_t n = fld(insn, 16, 20); r = (uint32_t)((int32_t)s >> n); if (s & 0x80000000u) r |= (0xFFFFFFFFu << (32 - n)); gpr[ra] = r; write = false; break; }  // srawi
        case 26:  r = (uint32_t)__builtin_clz(s); gpr[ra] = r; write = false; break;      // cntlzw
        case 922: r = (uint32_t)(int32_t)(int16_t)s; gpr[ra] = r; write = false; break;   // extsh
        case 954: r = (uint32_t)(int32_t)(int8_t)s; gpr[ra] = r; write = false; break;    // extsb
        case 986: r = s; gpr[ra] = r; write = false; break;                               // extsw
        case 19:  gpr[rt] = cr; write = false; break;                                     // mfcr
        case 144: { uint32_t fxm = fld(insn, 12, 19);                                     // mtcrf
                    for (int i = 0; i < 8; i++)
                        if (fxm & (0x80u >> i)) {
                            uint32_t sh = (uint32_t)(28 - i * 4);
                            cr = (cr & ~(0xFu << sh)) | (((s >> sh) & 0xFu) << sh);
                        }
                    write = false; break; }
        case 339: { uint32_t spr = (fld(insn, 16, 20) << 5) | fld(insn, 11, 15);          // mfspr
                    uint32_t v = 0; if (!getSpr(spr, v)) return; gpr[rt] = v; write = false; break; }
        case 467: { uint32_t spr = (fld(insn, 16, 20) << 5) | fld(insn, 11, 15);          // mtspr
                    setSpr(spr, s); write = false; break; }
        case 23:  { uint32_t addr = (ra ? a : 0u) + b, v; if (rd32(addr, v)) gpr[rt] = v; write = false; break; }   // lwzx
        case 87:  { uint8_t v; if (rd8((ra ? a : 0u) + b, v)) gpr[rt] = v; write = false; break; }                  // lbzx
        case 279: { uint16_t v; if (rd16((ra ? a : 0u) + b, v)) gpr[rt] = v; write = false; break; }                // lhzx
        case 151: { wr32((ra ? a : 0u) + b, s); write = false; break; }                                             // stwx
        case 215: { wr8((ra ? a : 0u) + b, (uint8_t)s); write = false; break; }                                     // stbx
        case 407: { wr16((ra ? a : 0u) + b, (uint16_t)s); write = false; break; }                                   // sthx
        case 534: { uint32_t v; if (rd32((ra ? a : 0u) + b, v)) gpr[rt] = __builtin_bswap32(v); write = false; break; }   // lwbrx
        case 790: { uint16_t v; if (rd16((ra ? a : 0u) + b, v)) gpr[rt] = __builtin_bswap16(v); write = false; break; }   // lhbrx
        case 662: { wr32((ra ? a : 0u) + b, __builtin_bswap32(s)); write = false; break; }                          // stwbrx
        case 918: { wr16((ra ? a : 0u) + b, (uint16_t)__builtin_bswap16((uint16_t)s)); write = false; break; }      // sthbrx
        case 598: break;                                                                  // sync
        default:  fault("unsupported opcode 31", (uint32_t)xo); write = false; break;
        }
        if (write && (rc || xo == 9)) setCr0((int32_t)gpr[ra]);    // XO-form Rc updates CR0
        if (rc) setCr0((int32_t)r);
        if (ov) { xer |= XER_OV; xer |= XER_SO; }
    }

    bool getSpr(uint32_t spr, uint32_t& v) {
        switch (spr) {
            case 1:  v = xer; return true;
            case 8:  v = lr;  return true;
            case 9:  v = ctr; return true;
            case 268: v = 0; return true;                       // TB (unused)
            default: fault("unsupported mfspr", spr); return false;
        }
    }
    void setSpr(uint32_t spr, uint32_t v) {
        switch (spr) {
            case 1:  xer = v; return;
            case 8:  lr = v;  return;
            case 9:  ctr = v; return;
            default: fault("unsupported mtspr", spr); return;
        }
    }
};

}  // namespace ppcsim

#endif
