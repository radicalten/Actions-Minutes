/*
    v3-NooDS-Wii: PowerPC 750CL (Broadway) instruction encoder.

    Deliberately free of any NooDS/GEKKO header so that the whole recompiler
    front-end can be compiled and differentially tested on the host (see
    tools/host).  Only the encoders that jit3 actually emits are implemented;
    each one is checked against the PowerPC 750CL / Broadway opcode tables by
    the disassembler in tools/host/ppc_sim.hpp.
*/

#pragma once

#include <cstdint>

namespace Jit3 {
namespace Ppc {

// ---------------------------------------------------------------- D-form
constexpr inline uint32_t D(uint32_t op, uint8_t rt, uint8_t ra, int16_t si) {
    return (op << 26) | ((uint32_t)(rt & 31) << 21) | ((uint32_t)(ra & 31) << 16) | (uint16_t)si;
}
constexpr inline uint32_t li    (uint8_t rt, int16_t si)            { return D(14, rt, 0, si); }
constexpr inline uint32_t lis   (uint8_t rt, int16_t si)            { return D(15, rt, 0, si); }
constexpr inline uint32_t addi  (uint8_t rt, uint8_t ra, int16_t si){ return D(14, rt, ra, si); }
constexpr inline uint32_t addis (uint8_t rt, uint8_t ra, int16_t si){ return D(15, rt, ra, si); }
constexpr inline uint32_t addic (uint8_t rt, uint8_t ra, int16_t si){ return D(12, rt, ra, si); }
constexpr inline uint32_t subfic(uint8_t rt, uint8_t ra, int16_t si){ return D(8,  rt, ra, si); }
constexpr inline uint32_t mulli (uint8_t rt, uint8_t ra, int16_t si){ return D(7,  rt, ra, si); }
constexpr inline uint32_t cmpwi (uint8_t bf, uint8_t ra, int16_t si){
    return (11u << 26) | ((uint32_t)(bf & 7) << 23) | ((uint32_t)(ra & 31) << 16) | (uint16_t)si;
}
constexpr inline uint32_t cmplwi(uint8_t bf, uint8_t ra, uint16_t ui){
    return (10u << 26) | ((uint32_t)(bf & 7) << 23) | ((uint32_t)(ra & 31) << 16) | (uint32_t)ui;
}
constexpr inline uint32_t ori   (uint8_t ra, uint8_t rs, uint16_t ui) { return D(24, rs, ra, (int16_t)ui); }
constexpr inline uint32_t oris  (uint8_t ra, uint8_t rs, uint16_t ui) { return D(25, rs, ra, (int16_t)ui); }
constexpr inline uint32_t andi_ (uint8_t ra, uint8_t rs, uint16_t ui) { return D(28, rs, ra, (int16_t)ui); }
constexpr inline uint32_t xori  (uint8_t ra, uint8_t rs, uint16_t ui) { return D(26, rs, ra, (int16_t)ui); }
constexpr inline uint32_t lwz   (uint8_t rt, uint8_t ra, int16_t d)   { return D(32, rt, ra, d); }
constexpr inline uint32_t lwzu  (uint8_t rt, uint8_t ra, int16_t d)   { return D(33, rt, ra, d); }
constexpr inline uint32_t lbz   (uint8_t rt, uint8_t ra, int16_t d)   { return D(34, rt, ra, d); }
constexpr inline uint32_t lbzu  (uint8_t rt, uint8_t ra, int16_t d)   { return D(35, rt, ra, d); }
constexpr inline uint32_t stw   (uint8_t rs, uint8_t ra, int16_t d)   { return D(36, rs, ra, d); }
constexpr inline uint32_t stwu  (uint8_t rs, uint8_t ra, int16_t d)   { return D(37, rs, ra, d); }
constexpr inline uint32_t stb   (uint8_t rs, uint8_t ra, int16_t d)   { return D(38, rs, ra, d); }
constexpr inline uint32_t stbu  (uint8_t rs, uint8_t ra, int16_t d)   { return D(39, rs, ra, d); }
constexpr inline uint32_t lhz   (uint8_t rt, uint8_t ra, int16_t d)   { return D(40, rt, ra, d); }
constexpr inline uint32_t lha   (uint8_t rt, uint8_t ra, int16_t d)   { return D(42, rt, ra, d); }
constexpr inline uint32_t sth   (uint8_t rs, uint8_t ra, int16_t d)   { return D(44, rs, ra, d); }
constexpr inline uint32_t lmw   (uint8_t rt, uint8_t ra, int16_t d)   { return D(46, rt, ra, d); }
constexpr inline uint32_t stmw  (uint8_t rs, uint8_t ra, int16_t d)   { return D(47, rs, ra, d); }
constexpr inline uint32_t nop   ()                                    { return ori(0, 0, 0); }

// ---------------------------------------------------------------- X-form
constexpr inline uint32_t X(uint32_t xo, uint8_t rt, uint8_t ra, uint8_t rb, bool rc = false) {
    return (31u << 26) | ((uint32_t)(rt & 31) << 21) | ((uint32_t)(ra & 31) << 16) |
           ((uint32_t)(rb & 31) << 11) | ((xo & 0x3FF) << 1) | (rc ? 1u : 0u);
}
constexpr inline uint32_t XO(uint32_t xo, uint8_t rt, uint8_t ra, uint8_t rb, bool oe = false, bool rc = false) {
    return (31u << 26) | ((uint32_t)(rt & 31) << 21) | ((uint32_t)(ra & 31) << 16) |
           ((uint32_t)(rb & 31) << 11) | (oe ? 0x400u : 0u) | ((xo & 0x1FF) << 1) | (rc ? 1u : 0u);
}
// ALU (X-form, opcode 31)
constexpr inline uint32_t cmpw  (uint8_t bf, uint8_t ra, uint8_t rb) { return X(0,   bf << 2, ra, rb); }
constexpr inline uint32_t cmplw (uint8_t bf, uint8_t ra, uint8_t rb) { return X(32,  bf << 2, ra, rb); }
constexpr inline uint32_t and_  (uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false) { return X(28,  rs, ra, rb, rc); }
constexpr inline uint32_t andc  (uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false) { return X(60,  rs, ra, rb, rc); }
constexpr inline uint32_t or_   (uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false) { return X(444, rs, ra, rb, rc); }
constexpr inline uint32_t orc   (uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false) { return X(412, rs, ra, rb, rc); }
constexpr inline uint32_t xor_  (uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false) { return X(316, rs, ra, rb, rc); }
constexpr inline uint32_t nand_ (uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false) { return X(476, rs, ra, rb, rc); }
constexpr inline uint32_t nor_  (uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false) { return X(124, rs, ra, rb, rc); }
constexpr inline uint32_t eqv_  (uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false) { return X(284, rs, ra, rb, rc); }
constexpr inline uint32_t slw   (uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false) { return X(24,  rs, ra, rb, rc); }
constexpr inline uint32_t srw   (uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false) { return X(536, rs, ra, rb, rc); }
constexpr inline uint32_t sraw  (uint8_t ra, uint8_t rs, uint8_t rb, bool rc=false) { return X(792, rs, ra, rb, rc); }
constexpr inline uint32_t srawi (uint8_t ra, uint8_t rs, uint8_t sh, bool rc=false) {
    return (31u << 26) | ((uint32_t)(rs & 31) << 21) | ((uint32_t)(ra & 31) << 16) |
           ((uint32_t)(sh & 31) << 11) | (824u << 1) | (rc ? 1u : 0u);
}
constexpr inline uint32_t cntlzw(uint8_t ra, uint8_t rs, bool rc=false) { return X(26, rs, ra, 0, rc); }
constexpr inline uint32_t extsb (uint8_t ra, uint8_t rs, bool rc=false) { return X(954, rs, ra, 0, rc); }
constexpr inline uint32_t extsh (uint8_t ra, uint8_t rs, bool rc=false) { return X(922, rs, ra, 0, rc); }
constexpr inline uint32_t mfspr (uint8_t rt, uint32_t spr) {
    const uint32_t e = ((spr & 31) << 5) | ((spr >> 5) & 31);
    return (31u << 26) | ((uint32_t)(rt & 31) << 21) | (e << 11) | (339u << 1);
}
constexpr inline uint32_t mtspr (uint32_t spr, uint8_t rs) {
    const uint32_t e = ((spr & 31) << 5) | ((spr >> 5) & 31);
    return (31u << 26) | ((uint32_t)(rs & 31) << 21) | (e << 11) | (467u << 1);
}
constexpr inline uint32_t mfcr  (uint8_t rt) { return (31u << 26) | ((uint32_t)(rt & 31) << 21) | (19u << 1); }
constexpr inline uint32_t mtcrf (uint8_t fxm, uint8_t rs) {
    return (31u << 26) | ((uint32_t)(rs & 31) << 21) | ((uint32_t)fxm << 12) | (144u << 1);
}
constexpr inline uint32_t mflr  (uint8_t rt) { return mfspr(rt, 8); }
constexpr inline uint32_t mtlr  (uint8_t rs) { return mtspr(8, rs); }
constexpr inline uint32_t mfctr (uint8_t rt) { return mfspr(rt, 9); }
constexpr inline uint32_t mtctr (uint8_t rs) { return mtspr(9, rs); }
constexpr inline uint32_t mfxer (uint8_t rt) { return mfspr(rt, 1); }
constexpr inline uint32_t mtxer (uint8_t rs) { return mtspr(1, rs); }
// Carry-less multiply / rotate / byte-reversed memory
constexpr inline uint32_t mulhwu(uint8_t rt, uint8_t ra, uint8_t rb, bool rc=false) { return XO(11, rt, ra, rb, false, rc); }
constexpr inline uint32_t mulhw (uint8_t rt, uint8_t ra, uint8_t rb, bool rc=false) { return XO(75, rt, ra, rb, false, rc); }
constexpr inline uint32_t lwzx  (uint8_t rt, uint8_t ra, uint8_t rb)  { return X(23,  rt, ra, rb); }
constexpr inline uint32_t lwbrx (uint8_t rt, uint8_t ra, uint8_t rb)  { return X(534, rt, ra, rb); }
constexpr inline uint32_t lhbrx (uint8_t rt, uint8_t ra, uint8_t rb)  { return X(790, rt, ra, rb); }
constexpr inline uint32_t stwx  (uint8_t rs, uint8_t ra, uint8_t rb)  { return X(151, rs, ra, rb); }
constexpr inline uint32_t stwbrx(uint8_t rs, uint8_t ra, uint8_t rb)  { return X(662, rs, ra, rb); }
constexpr inline uint32_t sthbrx(uint8_t rs, uint8_t ra, uint8_t rb)  { return X(918, rs, ra, rb); }

// XO-form arithmetic (opcode 31)
constexpr inline uint32_t addc  (uint8_t rt, uint8_t ra, uint8_t rb, bool oe=false, bool rc=false) { return XO(10,  rt, ra, rb, oe, rc); }
constexpr inline uint32_t adde  (uint8_t rt, uint8_t ra, uint8_t rb, bool oe=false, bool rc=false) { return XO(138, rt, ra, rb, oe, rc); }
constexpr inline uint32_t addze (uint8_t rt, uint8_t ra, bool oe=false, bool rc=false)             { return XO(202, rt, ra, 0, oe, rc); }
constexpr inline uint32_t addme (uint8_t rt, uint8_t ra, bool oe=false, bool rc=false)             { return XO(234, rt, ra, 0, oe, rc); }
constexpr inline uint32_t subfc (uint8_t rt, uint8_t ra, uint8_t rb, bool oe=false, bool rc=false) { return XO(8,   rt, ra, rb, oe, rc); }
constexpr inline uint32_t subfe (uint8_t rt, uint8_t ra, uint8_t rb, bool oe=false, bool rc=false) { return XO(136, rt, ra, rb, oe, rc); }
constexpr inline uint32_t subfze(uint8_t rt, uint8_t ra, bool oe=false, bool rc=false)             { return XO(200, rt, ra, 0, oe, rc); }
constexpr inline uint32_t neg   (uint8_t rt, uint8_t ra, bool oe=false, bool rc=false)             { return XO(104, rt, ra, 0, oe, rc); }
constexpr inline uint32_t mullw (uint8_t rt, uint8_t ra, uint8_t rb, bool oe=false, bool rc=false) { return XO(235, rt, ra, rb, oe, rc); }
constexpr inline uint32_t add   (uint8_t rt, uint8_t ra, uint8_t rb, bool oe=false, bool rc=false) { return XO(266, rt, ra, rb, oe, rc); }
constexpr inline uint32_t addic_ (uint8_t rt, uint8_t ra, int16_t si, bool rc=false) { return (13u << 26) | ((uint32_t)(rt & 31) << 21) | ((uint32_t)(ra & 31) << 16) | (uint16_t)si | (rc?1u:0u); }
// Rotate/mask (M-form)
constexpr inline uint32_t rlwinm(uint8_t ra, uint8_t rs, uint8_t sh, uint8_t mb, uint8_t me, bool rc=false) {
    return (21u << 26) | ((uint32_t)(rs & 31) << 21) | ((uint32_t)(ra & 31) << 16) |
           ((uint32_t)(sh & 31) << 11) | ((uint32_t)(mb & 31) << 6) | ((uint32_t)(me & 31) << 1) | (rc?1u:0u);
}
constexpr inline uint32_t rlwimi(uint8_t ra, uint8_t rs, uint8_t sh, uint8_t mb, uint8_t me, bool rc=false) {
    return (20u << 26) | ((uint32_t)(rs & 31) << 21) | ((uint32_t)(ra & 31) << 16) |
           ((uint32_t)(sh & 31) << 11) | ((uint32_t)(mb & 31) << 6) | ((uint32_t)(me & 31) << 1) | (rc?1u:0u);
}
constexpr inline uint32_t rlwnm (uint8_t ra, uint8_t rs, uint8_t rb, uint8_t mb, uint8_t me, bool rc=false) {
    return (23u << 26) | ((uint32_t)(rs & 31) << 21) | ((uint32_t)(ra & 31) << 16) |
           ((uint32_t)(rb & 31) << 11) | ((uint32_t)(mb & 31) << 6) | ((uint32_t)(me & 31) << 1) | (rc?1u:0u);
}
constexpr inline uint32_t slwi(uint8_t ra, uint8_t rs, uint8_t sh, bool rc=false) {
    return rlwinm(ra, rs, sh, 0, (uint8_t)(31 - sh), rc);
}
constexpr inline uint32_t srwi(uint8_t ra, uint8_t rs, uint8_t sh, bool rc=false) {
    return rlwinm(ra, rs, (uint8_t)(32 - sh), sh, 31, rc);
}

constexpr inline uint32_t mr    (uint8_t ra, uint8_t rs)            { return or_(ra, rs, rs); }
constexpr inline uint32_t rotrwi(uint8_t ra, uint8_t rs, uint8_t sh){ return rlwinm(ra, rs, (uint8_t)((32 - (sh & 31)) & 31), 0, 31); }
constexpr inline uint32_t rotrw (uint8_t ra, uint8_t rs, uint8_t rb){ return rlwnm(ra, rs, rb, 0, 31); }
constexpr inline uint32_t extsw (uint8_t ra, uint8_t rs, bool rc=false) { return X(986, rs, ra, 0, rc); }

// ---------------------------------------------------------------- B / XL-form
constexpr inline uint32_t b(int32_t off, bool aa=false, bool lk=false) {
    return (18u << 26) | ((uint32_t)off & 0x03FFFFFCu) | (aa?2u:0u) | (lk?1u:0u);
}
constexpr inline uint32_t bl(int32_t off) { return b(off, false, true); }
constexpr inline uint32_t bc(uint8_t bo, uint8_t bi, int16_t off, bool aa=false, bool lk=false) {
    return (16u << 26) | ((uint32_t)(bo & 31) << 21) | ((uint32_t)(bi & 31) << 16) |
           ((uint32_t)off & 0xFFFCu) | (aa?2u:0u) | (lk?1u:0u);
}
constexpr inline uint32_t blr(bool lk=false)  { return (19u << 26) | (20u << 21) | (16u << 1) | (lk?1u:0u); }
constexpr inline uint32_t bctr(bool lk=false) { return (19u << 26) | (20u << 21) | (528u << 1) | (lk?1u:0u); }
constexpr inline uint32_t bctrl()             { return bctr(true); }
// CR logical ops (XL-form, opcode 19): CRBT = CRBA op CRBB
constexpr inline uint32_t crand (uint8_t bt, uint8_t ba, uint8_t bb) { return (19u<<26) | ((uint32_t)(bt&31)<<21) | ((uint32_t)(ba&31)<<16) | ((uint32_t)(bb&31)<<11) | (257u<<1); }
constexpr inline uint32_t crandc(uint8_t bt, uint8_t ba, uint8_t bb) { return (19u<<26) | ((uint32_t)(bt&31)<<21) | ((uint32_t)(ba&31)<<16) | ((uint32_t)(bb&31)<<11) | (129u<<1); }
constexpr inline uint32_t creqv (uint8_t bt, uint8_t ba, uint8_t bb) { return (19u<<26) | ((uint32_t)(bt&31)<<21) | ((uint32_t)(ba&31)<<16) | ((uint32_t)(bb&31)<<11) | (289u<<1); }
constexpr inline uint32_t crnand(uint8_t bt, uint8_t ba, uint8_t bb) { return (19u<<26) | ((uint32_t)(bt&31)<<21) | ((uint32_t)(ba&31)<<16) | ((uint32_t)(bb&31)<<11) | (225u<<1); }
constexpr inline uint32_t crnor (uint8_t bt, uint8_t ba, uint8_t bb) { return (19u<<26) | ((uint32_t)(bt&31)<<21) | ((uint32_t)(ba&31)<<16) | ((uint32_t)(bb&31)<<11) | (33u<<1); }
constexpr inline uint32_t crorc (uint8_t bt, uint8_t ba, uint8_t bb) { return (19u<<26) | ((uint32_t)(bt&31)<<21) | ((uint32_t)(ba&31)<<16) | ((uint32_t)(bb&31)<<11) | (417u<<1); }
constexpr inline uint32_t cror  (uint8_t bt, uint8_t ba, uint8_t bb) { return (19u<<26) | ((uint32_t)(bt&31)<<21) | ((uint32_t)(ba&31)<<16) | ((uint32_t)(bb&31)<<11) | (449u<<1); }
constexpr inline uint32_t crxor (uint8_t bt, uint8_t ba, uint8_t bb) { return (19u<<26) | ((uint32_t)(bt&31)<<21) | ((uint32_t)(ba&31)<<16) | ((uint32_t)(bb&31)<<11) | (193u<<1); }
constexpr inline uint32_t isync () { return (19u << 26) | (150u << 1); }
constexpr inline uint32_t sync  () { return (31u << 26) | (598u << 1); }
constexpr inline uint32_t dcbst (uint8_t ra, uint8_t rb) { return X(54, 0, ra, rb); }
constexpr inline uint32_t icbi  (uint8_t ra, uint8_t rb) { return X(982, 0, ra, rb); }

// CR field helpers: bo/bi for bc.  bo=12 -> "branch if CR bit set", bo=4 -> "... clear".
constexpr inline uint8_t crbit(int crf, int bit) { return (uint8_t)(crf * 4 + bit); }   // 0=LT,1=GT,2=EQ,3=SO
constexpr inline uint32_t beq  (int16_t off, int crf = 0) { return bc(12, crbit(crf, 2), off); }
constexpr inline uint32_t bne  (int16_t off, int crf = 0) { return bc(4,  crbit(crf, 2), off); }
constexpr inline uint32_t blt  (int16_t off, int crf = 0) { return bc(12, crbit(crf, 0), off); }
constexpr inline uint32_t bge  (int16_t off, int crf = 0) { return bc(4,  crbit(crf, 0), off); }
constexpr inline uint32_t bso  (int16_t off, int crf = 0) { return bc(12, crbit(crf, 3), off); }
constexpr inline uint32_t bns  (int16_t off, int crf = 0) { return bc(4,  crbit(crf, 3), off); }
constexpr inline uint32_t b     (int16_t off, int crf, int bit, bool sense) {
    return bc(sense ? 12 : 4, crbit(crf, bit), off);
}

} // namespace Ppc
} // namespace Jit3
