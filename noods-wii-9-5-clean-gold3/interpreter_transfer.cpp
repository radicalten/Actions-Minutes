//interpreter_transfer.cpp (optimized for PowerPC/Wii)
#include "core.h"

// PowerPC optimization macros
#define PPC_LIKELY(x)   __builtin_expect(!!(x), 1)
#define PPC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define ALWAYS_INLINE   __attribute__((always_inline)) inline
#define HOT             __attribute__((hot))

// PowerPC rotate helper for misaligned word reads
// Uses rlwnm instruction directly
static ALWAYS_INLINE uint32_t ppc_rotl32(uint32_t value, uint32_t shift) {
    uint32_t result;
    __asm__("rlwnm %0,%1,%2,0,31" : "=r"(result) : "r"(value), "r"(shift));
    return result;
}

// Misaligned word rotate: rotate right by (addr & 3)*8 bits
static ALWAYS_INLINE uint32_t rotateWordMisaligned(uint32_t value, uint32_t addr) {
    uint32_t shift = (addr & 0x3) << 3;
    if (PPC_UNLIKELY(shift)) {
        uint32_t result;
        __asm__("rlwnm %0,%1,%2,0,31" : "=r"(result) : "r"(value), "r"(32 - shift));
        return result;
    }
    return value;
}

// Inline bitCount using PPC cntlzw for popcount
// Uses Brian Kernighan's method, but leverages PPC to avoid branches
static ALWAYS_INLINE uint32_t ppc_popcount16(uint32_t v) {
    // Fast popcount for 16-bit register list using PPC
    // Parallel bit counting
    v = v - ((v >> 1) & 0x5555U);
    v = (v & 0x3333U) + ((v >> 2) & 0x3333U);
    v = (v + (v >> 4)) & 0x0F0FU;
    return (v * 0x0101U) >> 8;
}

// Define functions for each ARM offset variation (half type)
#define HALF_FUNCS(func) \
    int Interpreter::func##Ofrm(uint32_t opcode) { return func##Of(opcode, (uint32_t)(-(int32_t)rp(opcode))); } \
    int Interpreter::func##Ofim(uint32_t opcode) { return func##Of(opcode, (uint32_t)(-(int32_t)ipH(opcode))); } \
    int Interpreter::func##Ofrp(uint32_t opcode) { return func##Of(opcode, rp(opcode)); } \
    int Interpreter::func##Ofip(uint32_t opcode) { return func##Of(opcode, ipH(opcode)); } \
    int Interpreter::func##Prrm(uint32_t opcode) { return func##Pr(opcode, (uint32_t)(-(int32_t)rp(opcode))); } \
    int Interpreter::func##Prim(uint32_t opcode) { return func##Pr(opcode, (uint32_t)(-(int32_t)ipH(opcode))); } \
    int Interpreter::func##Prrp(uint32_t opcode) { return func##Pr(opcode, rp(opcode)); } \
    int Interpreter::func##Prip(uint32_t opcode) { return func##Pr(opcode, ipH(opcode)); } \
    int Interpreter::func##Ptrm(uint32_t opcode) { return func##Pt(opcode, (uint32_t)(-(int32_t)rp(opcode))); } \
    int Interpreter::func##Ptim(uint32_t opcode) { return func##Pt(opcode, (uint32_t)(-(int32_t)ipH(opcode))); } \
    int Interpreter::func##Ptrp(uint32_t opcode) { return func##Pt(opcode, rp(opcode)); } \
    int Interpreter::func##Ptip(uint32_t opcode) { return func##Pt(opcode, ipH(opcode)); }

// Define functions for each ARM offset variation (full type)
#define FULL_FUNCS(func) \
    int Interpreter::func##Ofim(uint32_t opcode)   { return func##Of(opcode, (uint32_t)(-(int32_t)ip(opcode))); } \
    int Interpreter::func##Ofip(uint32_t opcode)   { return func##Of(opcode, ip(opcode)); } \
    int Interpreter::func##Ofrmll(uint32_t opcode) { return func##Of(opcode, (uint32_t)(-(int32_t)rpll(opcode))); } \
    int Interpreter::func##Ofrmlr(uint32_t opcode) { return func##Of(opcode, (uint32_t)(-(int32_t)rplr(opcode))); } \
    int Interpreter::func##Ofrmar(uint32_t opcode) { return func##Of(opcode, (uint32_t)(-(int32_t)rpar(opcode))); } \
    int Interpreter::func##Ofrmrr(uint32_t opcode) { return func##Of(opcode, (uint32_t)(-(int32_t)rprr(opcode))); } \
    int Interpreter::func##Ofrpll(uint32_t opcode) { return func##Of(opcode, rpll(opcode)); } \
    int Interpreter::func##Ofrplr(uint32_t opcode) { return func##Of(opcode, rplr(opcode)); } \
    int Interpreter::func##Ofrpar(uint32_t opcode) { return func##Of(opcode, rpar(opcode)); } \
    int Interpreter::func##Ofrprr(uint32_t opcode) { return func##Of(opcode, rprr(opcode)); } \
    int Interpreter::func##Prim(uint32_t opcode)   { return func##Pr(opcode, (uint32_t)(-(int32_t)ip(opcode))); } \
    int Interpreter::func##Prip(uint32_t opcode)   { return func##Pr(opcode, ip(opcode)); } \
    int Interpreter::func##Prrmll(uint32_t opcode) { return func##Pr(opcode, (uint32_t)(-(int32_t)rpll(opcode))); } \
    int Interpreter::func##Prrmlr(uint32_t opcode) { return func##Pr(opcode, (uint32_t)(-(int32_t)rplr(opcode))); } \
    int Interpreter::func##Prrmar(uint32_t opcode) { return func##Pr(opcode, (uint32_t)(-(int32_t)rpar(opcode))); } \
    int Interpreter::func##Prrmrr(uint32_t opcode) { return func##Pr(opcode, (uint32_t)(-(int32_t)rprr(opcode))); } \
    int Interpreter::func##Prrpll(uint32_t opcode) { return func##Pr(opcode, rpll(opcode)); } \
    int Interpreter::func##Prrplr(uint32_t opcode) { return func##Pr(opcode, rplr(opcode)); } \
    int Interpreter::func##Prrpar(uint32_t opcode) { return func##Pr(opcode, rpar(opcode)); } \
    int Interpreter::func##Prrprr(uint32_t opcode) { return func##Pr(opcode, rprr(opcode)); } \
    int Interpreter::func##Ptim(uint32_t opcode)   { return func##Pt(opcode, (uint32_t)(-(int32_t)ip(opcode))); } \
    int Interpreter::func##Ptip(uint32_t opcode)   { return func##Pt(opcode, ip(opcode)); } \
    int Interpreter::func##Ptrmll(uint32_t opcode) { return func##Pt(opcode, (uint32_t)(-(int32_t)rpll(opcode))); } \
    int Interpreter::func##Ptrmlr(uint32_t opcode) { return func##Pt(opcode, (uint32_t)(-(int32_t)rplr(opcode))); } \
    int Interpreter::func##Ptrmar(uint32_t opcode) { return func##Pt(opcode, (uint32_t)(-(int32_t)rpar(opcode))); } \
    int Interpreter::func##Ptrmrr(uint32_t opcode) { return func##Pt(opcode, (uint32_t)(-(int32_t)rprr(opcode))); } \
    int Interpreter::func##Ptrpll(uint32_t opcode) { return func##Pt(opcode, rpll(opcode)); } \
    int Interpreter::func##Ptrplr(uint32_t opcode) { return func##Pt(opcode, rplr(opcode)); } \
    int Interpreter::func##Ptrpar(uint32_t opcode) { return func##Pt(opcode, rpar(opcode)); } \
    int Interpreter::func##Ptrprr(uint32_t opcode) { return func##Pt(opcode, rprr(opcode)); }

// Instantiate all variants
HALF_FUNCS(ldrsb)
HALF_FUNCS(ldrsh)
HALF_FUNCS(ldrh)
HALF_FUNCS(strh)
HALF_FUNCS(ldrd)
HALF_FUNCS(strd)

FULL_FUNCS(ldrb)
FULL_FUNCS(strb)
FULL_FUNCS(ldr)
FULL_FUNCS(str)

// ---- Offset helpers ----

ALWAYS_INLINE uint32_t Interpreter::ip(uint32_t opcode) {
    return opcode & 0xFFF;
}

ALWAYS_INLINE uint32_t Interpreter::ipH(uint32_t opcode) {
    return ((opcode >> 4) & 0xF0) | (opcode & 0xF);
}

ALWAYS_INLINE uint32_t Interpreter::rp(uint32_t opcode) {
    return *registers[opcode & 0xF];
}

ALWAYS_INLINE uint32_t Interpreter::rpll(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF];
    uint8_t shift = (opcode >> 7) & 0x1F;
    // PPC slw: if shift >= 32, result is 0 (correct for LSL)
    uint32_t result;
    __asm__("slw %0,%1,%2" : "=r"(result) : "r"(value), "r"((uint32_t)shift));
    return result;
}

ALWAYS_INLINE uint32_t Interpreter::rplr(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF];
    uint8_t shift = (opcode >> 7) & 0x1F;
    if (PPC_UNLIKELY(!shift)) return 0;
    uint32_t result;
    __asm__("srw %0,%1,%2" : "=r"(result) : "r"(value), "r"((uint32_t)shift));
    return result;
}

ALWAYS_INLINE uint32_t Interpreter::rpar(uint32_t opcode) {
    int32_t value = *registers[opcode & 0xF];
    uint8_t shift = (opcode >> 7) & 0x1F;
    uint32_t sh = shift ? shift : 31;
    int32_t result;
    __asm__("sraw %0,%1,%2" : "=r"(result) : "r"(value), "r"(sh));
    return (uint32_t)result;
}

ALWAYS_INLINE uint32_t Interpreter::rprr(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF];
    uint8_t shift = (opcode >> 7) & 0x1F;
    if (PPC_UNLIKELY(!shift)) {
        // RRX: rotate right with carry
        return ((cpsr & BIT(29)) << 2) | (value >> 1);
    }
    uint32_t result;
    __asm__("rlwnm %0,%1,%2,0,31" : "=r"(result) : "r"(value), "r"((uint32_t)(32 - shift)));
    return result;
}

// ---- Return value helpers ----
// Cache these common return values to avoid recomputation
static ALWAYS_INLINE int ldr_cycles(bool arm7) { return (arm7 << 1) + 1; }
static ALWAYS_INLINE int str_cycles(bool arm7) { return arm7 + 1; }

// ---- Single transfer - pre-adjust without writeback ----

ALWAYS_INLINE int Interpreter::ldrsbOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t addr = *registers[(opcode >> 16) & 0xF] + op2;
    *op0 = (uint32_t)(int32_t)(int8_t)core->memory.read<uint8_t>(arm7, addr);
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::ldrshOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t addr = *registers[(opcode >> 16) & 0xF] + op2;
    *op0 = (uint32_t)(int32_t)(int16_t)core->memory.read<uint16_t>(arm7, addr);
    // Shift misaligned reads on ARM7
    if (PPC_UNLIKELY(addr & arm7))
        *op0 = (uint32_t)(int32_t)((int16_t)*op0 >> 8);
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::ldrbOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t addr = *registers[(opcode >> 16) & 0xF] + op2;
    *op0 = core->memory.read<uint8_t>(arm7, addr);
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    cpsr |= (*op0 & (uint32_t)!arm7) << 5;
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::strbOf(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *registers[(opcode >> 12) & 0xF] + (((opcode & 0xF000) == 0xF000) << 2);
    uint32_t addr = *registers[(opcode >> 16) & 0xF] + op2;
    core->memory.write<uint8_t>(arm7, addr, op0);
    return str_cycles(arm7);
}

ALWAYS_INLINE int Interpreter::ldrhOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t addr = *registers[(opcode >> 16) & 0xF] + op2;
    *op0 = core->memory.read<uint16_t>(arm7, addr);
    // Rotate misaligned reads on ARM7: rotate right by 8
    if (PPC_UNLIKELY(addr & arm7)) {
        uint32_t v = *op0;
        __asm__("rlwinm %0,%1,24,0,31" : "=r"(*op0) : "r"(v)); // rotate left 24 = rotate right 8
    }
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::strhOf(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *registers[(opcode >> 12) & 0xF] + (((opcode & 0xF000) == 0xF000) << 2);
    uint32_t addr = *registers[(opcode >> 16) & 0xF] + op2;
    core->memory.write<uint16_t>(arm7, addr, op0);
    return str_cycles(arm7);
}

ALWAYS_INLINE int Interpreter::ldrOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t addr = *registers[(opcode >> 16) & 0xF] + op2;
    *op0 = core->memory.read<uint32_t>(arm7, addr);
    *op0 = rotateWordMisaligned(*op0, addr);
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    cpsr |= (*op0 & (uint32_t)!arm7) << 5;
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::strOf(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *registers[(opcode >> 12) & 0xF] + (((opcode & 0xF000) == 0xF000) << 2);
    uint32_t addr = *registers[(opcode >> 16) & 0xF] + op2;
    core->memory.write<uint32_t>(arm7, addr, op0);
    return str_cycles(arm7);
}

ALWAYS_INLINE int Interpreter::ldrdOf(uint32_t opcode, uint32_t op2) {
    uint8_t op0 = (opcode >> 12) & 0xF;
    if (PPC_UNLIKELY(arm7 || op0 == 15)) return 1;
    uint32_t addr = *registers[(opcode >> 16) & 0xF] + op2;
    *registers[op0]     = core->memory.read<uint32_t>(arm7, addr);
    *registers[op0 + 1] = core->memory.read<uint32_t>(arm7, addr + 4);
    return 2;
}

ALWAYS_INLINE int Interpreter::strdOf(uint32_t opcode, uint32_t op2) {
    uint8_t op0 = (opcode >> 12) & 0xF;
    if (PPC_UNLIKELY(arm7 || op0 == 15)) return 1;
    uint32_t addr = *registers[(opcode >> 16) & 0xF] + op2;
    core->memory.write<uint32_t>(arm7, addr,     *registers[op0]);
    core->memory.write<uint32_t>(arm7, addr + 4, *registers[op0 + 1]);
    return 2;
}

// ---- Single transfer - pre-adjust with writeback ----

ALWAYS_INLINE int Interpreter::ldrsbPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t addr = (*op1 += op2);
    *op0 = (uint32_t)(int32_t)(int8_t)core->memory.read<uint8_t>(arm7, addr);
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::ldrshPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t addr = (*op1 += op2);
    *op0 = (uint32_t)(int32_t)(int16_t)core->memory.read<uint16_t>(arm7, addr);
    if (PPC_UNLIKELY(addr & arm7))
        *op0 = (uint32_t)(int32_t)((int16_t)*op0 >> 8);
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::ldrbPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    *op0 = core->memory.read<uint8_t>(arm7, *op1 += op2);
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    cpsr |= (*op0 & (uint32_t)!arm7) << 5;
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::strbPr(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *registers[(opcode >> 12) & 0xF] + (((opcode & 0xF000) == 0xF000) << 2);
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    core->memory.write<uint8_t>(arm7, *op1 += op2, op0);
    return str_cycles(arm7);
}

ALWAYS_INLINE int Interpreter::ldrhPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t addr = (*op1 += op2);
    *op0 = core->memory.read<uint16_t>(arm7, addr);
    if (PPC_UNLIKELY(addr & arm7)) {
        uint32_t v = *op0;
        __asm__("rlwinm %0,%1,24,0,31" : "=r"(*op0) : "r"(v));
    }
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::strhPr(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *registers[(opcode >> 12) & 0xF] + (((opcode & 0xF000) == 0xF000) << 2);
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    core->memory.write<uint16_t>(arm7, *op1 += op2, op0);
    return str_cycles(arm7);
}

ALWAYS_INLINE int Interpreter::ldrPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t addr = (*op1 += op2);
    *op0 = core->memory.read<uint32_t>(arm7, addr);
    *op0 = rotateWordMisaligned(*op0, addr);
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    cpsr |= (*op0 & (uint32_t)!arm7) << 5;
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::strPr(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *registers[(opcode >> 12) & 0xF] + (((opcode & 0xF000) == 0xF000) << 2);
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    core->memory.write<uint32_t>(arm7, *op1 += op2, op0);
    return str_cycles(arm7);
}

ALWAYS_INLINE int Interpreter::ldrdPr(uint32_t opcode, uint32_t op2) {
    uint8_t op0 = (opcode >> 12) & 0xF;
    if (PPC_UNLIKELY(arm7 || op0 == 15)) return 1;
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t addr = (*op1 += op2);
    *registers[op0]     = core->memory.read<uint32_t>(arm7, addr);
    *registers[op0 + 1] = core->memory.read<uint32_t>(arm7, addr + 4);
    return 2;
}

ALWAYS_INLINE int Interpreter::strdPr(uint32_t opcode, uint32_t op2) {
    uint8_t op0 = (opcode >> 12) & 0xF;
    if (PPC_UNLIKELY(arm7 || op0 == 15)) return 1;
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t addr = (*op1 += op2);
    core->memory.write<uint32_t>(arm7, addr,     *registers[op0]);
    core->memory.write<uint32_t>(arm7, addr + 4, *registers[op0 + 1]);
    return 2;
}

// ---- Single transfer - post-adjust ----

ALWAYS_INLINE int Interpreter::ldrsbPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t addr = *op1;
    *op1 += op2;
    *op0 = (uint32_t)(int32_t)(int8_t)core->memory.read<uint8_t>(arm7, addr);
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::ldrshPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t addr = *op1;
    *op1 += op2;
    *op0 = (uint32_t)(int32_t)(int16_t)core->memory.read<uint16_t>(arm7, addr);
    if (PPC_UNLIKELY(addr & arm7))
        *op0 = (uint32_t)(int32_t)((int16_t)*op0 >> 8);
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::ldrbPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t addr = *op1;
    *op1 += op2;
    *op0 = core->memory.read<uint8_t>(arm7, addr);
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    cpsr |= (*op0 & (uint32_t)!arm7) << 5;
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::strbPt(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *registers[(opcode >> 12) & 0xF] + (((opcode & 0xF000) == 0xF000) << 2);
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    core->memory.write<uint8_t>(arm7, *op1, op0);
    *op1 += op2;
    return str_cycles(arm7);
}

ALWAYS_INLINE int Interpreter::ldrhPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t addr = *op1;
    *op1 += op2;
    *op0 = core->memory.read<uint16_t>(arm7, addr);
    if (PPC_UNLIKELY(addr & arm7)) {
        uint32_t v = *op0;
        __asm__("rlwinm %0,%1,24,0,31" : "=r"(*op0) : "r"(v));
    }
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::strhPt(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *registers[(opcode >> 12) & 0xF] + (((opcode & 0xF000) == 0xF000) << 2);
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    core->memory.write<uint16_t>(arm7, *op1, op0);
    *op1 += op2;
    return str_cycles(arm7);
}

ALWAYS_INLINE int Interpreter::ldrPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t addr = *op1;
    *op1 += op2;
    *op0 = core->memory.read<uint32_t>(arm7, addr);
    *op0 = rotateWordMisaligned(*op0, addr);
    if (PPC_LIKELY(op0 != registers[15])) return ldr_cycles(arm7);
    cpsr |= (*op0 & (uint32_t)!arm7) << 5;
    flushPipeline();
    return 5;
}

ALWAYS_INLINE int Interpreter::strPt(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *registers[(opcode >> 12) & 0xF] + (((opcode & 0xF000) == 0xF000) << 2);
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    core->memory.write<uint32_t>(arm7, *op1, op0);
    *op1 += op2;
    return str_cycles(arm7);
}

ALWAYS_INLINE int Interpreter::ldrdPt(uint32_t opcode, uint32_t op2) {
    uint8_t op0 = (opcode >> 12) & 0xF;
    if (PPC_UNLIKELY(arm7 || op0 == 15)) return 1;
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t addr = *op1;
    *op1 += op2;
    *registers[op0]     = core->memory.read<uint32_t>(arm7, addr);
    *registers[op0 + 1] = core->memory.read<uint32_t>(arm7, addr + 4);
    return 2;
}

ALWAYS_INLINE int Interpreter::strdPt(uint32_t opcode, uint32_t op2) {
    uint8_t op0 = (opcode >> 12) & 0xF;
    if (PPC_UNLIKELY(arm7 || op0 == 15)) return 1;
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t addr = *op1;
    core->memory.write<uint32_t>(arm7, addr,     *registers[op0]);
    core->memory.write<uint32_t>(arm7, addr + 4, *registers[op0 + 1]);
    *op1 += op2;
    return 2;
}

// ---- Swap ----

int Interpreter::swpb(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t op1 = *registers[opcode & 0xF];
    uint32_t addr = *registers[(opcode >> 16) & 0xF];
    *op0 = core->memory.read<uint8_t>(arm7, addr);
    core->memory.write<uint8_t>(arm7, addr, op1);
    return (arm7 << 1) + 2;
}

int Interpreter::swp(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t op1 = *registers[opcode & 0xF];
    uint32_t addr = *registers[(opcode >> 16) & 0xF];
    *op0 = core->memory.read<uint32_t>(arm7, addr);
    core->memory.write<uint32_t>(arm7, addr, op1);
    *op0 = rotateWordMisaligned(*op0, addr);
    return (arm7 << 1) + 2;
}

// ---- Block transfer helpers ----
// Fast register-list popcount using PPC
static ALWAYS_INLINE uint32_t regListCount(uint32_t opcode) {
    uint32_t rlist = opcode & 0xFFFF;
    return ppc_popcount16(rlist);
}

// ARM9 writeback condition check: load WB value if not last or only register
static ALWAYS_INLINE bool arm9NeedsWriteback(uint32_t opcode, uint8_t op0) {
    uint32_t rlist = opcode & 0xFFFF;
    // Higher registers than op0 set, OR op0 is the only register
    return (rlist & ~((uint32_t)(BIT(op0 + 1) - 1))) || (rlist == (uint32_t)BIT(op0));
}

// ARM7 STM writeback: store WB if op0 is not the first listed register
static ALWAYS_INLINE bool arm7StmNeedsEarlyWb(uint32_t opcode, uint8_t op0) {
    return (opcode & ((uint32_t)(BIT(op0 + 1) - 1))) > (uint32_t)BIT(op0);
}

// ---- Block loads/stores - no writeback ----

// Macro to reduce repetition in block transfer implementations
// DA = decrement after: base = Rn - m*4, then read from base+4, base+8...
// IA = increment after: base = Rn, read from base, base+4...
// DB = decrement before: base = Rn - m*4, read from base, base+4...
// IB = increment before: base = Rn, read from base+4, base+8...

int Interpreter::ldmda(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF] - (m << 2);
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *registers[i] = core->memory.read<uint32_t>(arm7, op0 += 4);
    }
    if (PPC_LIKELY(opcode & BIT(15))) {
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmda(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF] - (m << 2);
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, op0 += 4, *registers[i]);
    }
    return m + (arm7 || m < 2);
}

int Interpreter::ldmia(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *registers[i] = core->memory.read<uint32_t>(arm7, op0);
        op0 += 4;
    }
    if (PPC_LIKELY(opcode & BIT(15))) {
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmia(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, op0, *registers[i]);
        op0 += 4;
    }
    return m + (arm7 || m < 2);
}

int Interpreter::ldmdb(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF] - (m << 2);
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *registers[i] = core->memory.read<uint32_t>(arm7, op0);
        op0 += 4;
    }
    if (PPC_LIKELY(opcode & BIT(15))) {
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmdb(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF] - (m << 2);
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, op0, *registers[i]);
        op0 += 4;
    }
    return m + (arm7 || m < 2);
}

int Interpreter::ldmib(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *registers[i] = core->memory.read<uint32_t>(arm7, op0 += 4);
    }
    if (PPC_LIKELY(opcode & BIT(15))) {
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmib(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, op0 += 4, *registers[i]);
    }
    return m + (arm7 || m < 2);
}

// ---- Block loads/stores - with writeback ----

int Interpreter::ldmdaW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = (*registers[op0] -= (m << 2));
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *registers[i] = core->memory.read<uint32_t>(arm7, address += 4);
    }
    if (!arm7 && arm9NeedsWriteback(opcode, op0))
        *registers[op0] = address - (m << 2);
    if (PPC_LIKELY(opcode & BIT(15))) {
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmdaW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = *registers[op0] - (m << 2);
    if (arm7 && arm7StmNeedsEarlyWb(opcode, op0))
        *registers[op0] = address;
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, address += 4, *registers[i]);
    }
    *registers[op0] = address - (m << 2);
    return m + (arm7 || m < 2);
}

int Interpreter::ldmiaW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = (*registers[op0] += (m << 2)) - (m << 2);
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *registers[i] = core->memory.read<uint32_t>(arm7, address);
        address += 4;
    }
    if (!arm7 && arm9NeedsWriteback(opcode, op0))
        *registers[op0] = address;
    if (PPC_LIKELY(opcode & BIT(15))) {
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmiaW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = *registers[op0];
    if (arm7 && arm7StmNeedsEarlyWb(opcode, op0))
        *registers[op0] = address + (m << 2);
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, address, *registers[i]);
        address += 4;
    }
    *registers[op0] = address;
    return m + (arm7 || m < 2);
}

int Interpreter::ldmdbW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = (*registers[op0] -= (m << 2));
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *registers[i] = core->memory.read<uint32_t>(arm7, address);
        address += 4;
    }
    if (!arm7 && arm9NeedsWriteback(opcode, op0))
        *registers[op0] = address - (m << 2);
    if (PPC_LIKELY(opcode & BIT(15))) {
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmdbW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = *registers[op0] - (m << 2);
    if (arm7 && arm7StmNeedsEarlyWb(opcode, op0))
        *registers[op0] = address;
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, address, *registers[i]);
        address += 4;
    }
    *registers[op0] = address - (m << 2);
    return m + (arm7 || m < 2);
}

int Interpreter::ldmibW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = (*registers[op0] += (m << 2)) - (m << 2);
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *registers[i] = core->memory.read<uint32_t>(arm7, address += 4);
    }
    if (!arm7 && arm9NeedsWriteback(opcode, op0))
        *registers[op0] = address;
    if (PPC_LIKELY(opcode & BIT(15))) {
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmibW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = *registers[op0];
    if (arm7 && arm7StmNeedsEarlyWb(opcode, op0))
        *registers[op0] = address + (m << 2);
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, address += 4, *registers[i]);
    }
    *registers[op0] = address;
    return m + (arm7 || m < 2);
}

// ---- User-mode block transfers (no writeback) ----

int Interpreter::ldmdaU(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF] - (m << 2);
    // Use normal registers when PC is in list (branching), else user registers
    bool useNormal = (opcode & BIT(15)) != 0;
    uint32_t **regs = useNormal ? registers : &registers[16]; // index offset trick from original
    // Original: &registers[(~opcode & BIT(15)) >> 11]
    // ~opcode & BIT(15): if bit15 set in opcode -> 0 -> &registers[0] (normal)
    // if bit15 clear -> BIT(15) -> >>11 -> 16 -> &registers[16] (user)
    // We replicate this exactly:
    uint32_t **r = &registers[(~opcode & BIT(15)) >> 11];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *r[i] = core->memory.read<uint32_t>(arm7, op0 += 4);
    }
    if (PPC_LIKELY(opcode & BIT(15))) {
        if (spsr) setCpsr(*spsr);
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmdaU(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF] - (m << 2);
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, op0 += 4, registersUsr[i]);
    }
    return m + (arm7 || m < 2);
}

int Interpreter::ldmiaU(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF];
    uint32_t **r = &registers[(~opcode & BIT(15)) >> 11];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *r[i] = core->memory.read<uint32_t>(arm7, op0);
        op0 += 4;
    }
    if (PPC_LIKELY(opcode & BIT(15))) {
        if (spsr) setCpsr(*spsr);
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmiaU(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, op0, registersUsr[i]);
        op0 += 4;
    }
    return m + (arm7 || m < 2);
}

int Interpreter::ldmdbU(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF] - (m << 2);
    uint32_t **r = &registers[(~opcode & BIT(15)) >> 11];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *r[i] = core->memory.read<uint32_t>(arm7, op0);
        op0 += 4;
    }
    if (PPC_LIKELY(opcode & BIT(15))) {
        if (spsr) setCpsr(*spsr);
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmdbU(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF] - (m << 2);
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, op0, registersUsr[i]);
        op0 += 4;
    }
    return m + (arm7 || m < 2);
}

int Interpreter::ldmibU(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF];
    uint32_t **r = &registers[(~opcode & BIT(15)) >> 11];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *r[i] = core->memory.read<uint32_t>(arm7, op0 += 4);
    }
    if (PPC_LIKELY(opcode & BIT(15))) {
        if (spsr) setCpsr(*spsr);
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmibU(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint32_t op0 = *registers[(opcode >> 16) & 0xF];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, op0 += 4, registersUsr[i]);
    }
    return m + (arm7 || m < 2);
}

// ---- User-mode block transfers with writeback ----

int Interpreter::ldmdaUW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = (*registers[op0] -= (m << 2));
    uint32_t **r = &registers[(~opcode & BIT(15)) >> 11];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *r[i] = core->memory.read<uint32_t>(arm7, address += 4);
    }
    if (!arm7 && arm9NeedsWriteback(opcode, op0))
        *registers[op0] = address - (m << 2);
    if (PPC_LIKELY(opcode & BIT(15))) {
        if (spsr) setCpsr(*spsr);
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmdaUW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = *registers[op0] - (m << 2);
    if (arm7 && arm7StmNeedsEarlyWb(opcode, op0))
        *registers[op0] = address;
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, address += 4, registersUsr[i]);
    }
    *registers[op0] = address - (m << 2);
    return m + (arm7 || m < 2);
}

int Interpreter::ldmiaUW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = (*registers[op0] += (m << 2)) - (m << 2);
    uint32_t **r = &registers[(~opcode & BIT(15)) >> 11];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *r[i] = core->memory.read<uint32_t>(arm7, address);
        address += 4;
    }
    if (!arm7 && arm9NeedsWriteback(opcode, op0))
        *registers[op0] = address;
    if (PPC_LIKELY(opcode & BIT(15))) {
        if (spsr) setCpsr(*spsr);
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmiaUW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = *registers[op0];
    if (arm7 && arm7StmNeedsEarlyWb(opcode, op0))
        *registers[op0] = address + (m << 2);
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, address, registersUsr[i]);
        address += 4;
    }
    *registers[op0] = address;
    return m + (arm7 || m < 2);
}

int Interpreter::ldmdbUW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = (*registers[op0] -= (m << 2));
    uint32_t **r = &registers[(~opcode & BIT(15)) >> 11];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *r[i] = core->memory.read<uint32_t>(arm7, address);
        address += 4;
    }
    if (!arm7 && arm9NeedsWriteback(opcode, op0))
        *registers[op0] = address - (m << 2);
    if (PPC_LIKELY(opcode & BIT(15))) {
        if (spsr) setCpsr(*spsr);
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmdbUW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = *registers[op0] - (m << 2);
    if (arm7 && arm7StmNeedsEarlyWb(opcode, op0))
        *registers[op0] = address;
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, address, registersUsr[i]);
        address += 4;
    }
    *registers[op0] = address - (m << 2);
    return m + (arm7 || m < 2);
}

int Interpreter::ldmibUW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = (*registers[op0] += (m << 2)) - (m << 2);
    uint32_t **r = &registers[(~opcode & BIT(15)) >> 11];
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *r[i] = core->memory.read<uint32_t>(arm7, address += 4);
    }
    if (!arm7 && arm9NeedsWriteback(opcode, op0))
        *registers[op0] = address;
    if (PPC_LIKELY(opcode & BIT(15))) {
        if (spsr) setCpsr(*spsr);
        cpsr |= (*registers[15] & (uint32_t)!arm7) << 5;
        flushPipeline();
        return m + 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmibUW(uint32_t opcode) {
    uint32_t m = regListCount(opcode);
    uint8_t op0 = (opcode >> 16) & 0xF;
    uint32_t address = *registers[op0];
    if (arm7 && arm7StmNeedsEarlyWb(opcode, op0))
        *registers[op0] = address + (m << 2);
    for (int i = 0; i < 16; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, address += 4, registersUsr[i]);
    }
    *registers[op0] = address;
    return m + (arm7 || m < 2);
}

// ---- Status register transfers ----

// Helper: apply immediate rotation (same as ARM imm encoding)
static ALWAYS_INLINE uint32_t msr_imm_rotate(uint32_t opcode) {
    uint32_t value = opcode & 0xFF;
    uint8_t shift = (opcode >> 7) & 0x1E;
    if (PPC_UNLIKELY(!shift)) return value;
    uint32_t result;
    __asm__("rlwnm %0,%1,%2,0,31" : "=r"(result) : "r"(value), "r"((uint32_t)(32 - shift)));
    return result;
}

// Helper: apply MSR mask update (4 x 8-bit blocks)
static ALWAYS_INLINE void apply_msr_mask(uint32_t &dest, uint32_t src, uint32_t opcode, int startBit) {
    // Process 4 byte fields
    for (int i = 0; i < 4; i++) {
        if (opcode & BIT(startBit + i)) {
            uint32_t mask = 0xFF << (i << 3);
            dest = (dest & ~mask) | (src & mask);
        }
    }
}

int Interpreter::msrRc(uint32_t opcode) {
    uint32_t op1 = *registers[opcode & 0xF];
    if (opcode & BIT(16)) {
        uint8_t mask = ((cpsr & 0x1F) == 0x10) ? 0xE0 : 0xFF;
        setCpsr((cpsr & ~mask) | (op1 & mask));
    }
    for (int i = 1; i < 4; i++) {
        if (opcode & BIT(16 + i)) {
            uint32_t mask = 0xFF << (i << 3);
            cpsr = (cpsr & ~mask) | (op1 & mask);
        }
    }
    return 1;
}

int Interpreter::msrRs(uint32_t opcode) {
    if (PPC_UNLIKELY(!spsr)) return 1;
    uint32_t op1 = *registers[opcode & 0xF];
    apply_msr_mask(*spsr, op1, opcode, 16);
    return 1;
}

int Interpreter::msrIc(uint32_t opcode) {
    uint32_t op1 = msr_imm_rotate(opcode);
    if (opcode & BIT(16)) {
        uint8_t mask = ((cpsr & 0x1F) == 0x10) ? 0xE0 : 0xFF;
        setCpsr((cpsr & ~mask) | (op1 & mask));
    }
    for (int i = 1; i < 4; i++) {
        if (opcode & BIT(16 + i)) {
            uint32_t mask = 0xFF << (i << 3);
            cpsr = (cpsr & ~mask) | (op1 & mask);
        }
    }
    return 1;
}

int Interpreter::msrIs(uint32_t opcode) {
    if (PPC_UNLIKELY(!spsr)) return 1;
    uint32_t op1 = msr_imm_rotate(opcode);
    apply_msr_mask(*spsr, op1, opcode, 16);
    return 1;
}

int Interpreter::mrsRc(uint32_t opcode) {
    *registers[(opcode >> 12) & 0xF] = cpsr;
    return 2 - arm7;
}

int Interpreter::mrsRs(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    if (spsr) *op0 = *spsr;
    return 2 - arm7;
}

int Interpreter::mrc(uint32_t opcode) {
    if (PPC_UNLIKELY(arm7)) return 1;
    uint32_t *op2 = registers[(opcode >> 12) & 0xF];
    *op2 = core->cp15.read((opcode >> 16) & 0xF, opcode & 0xF, (opcode >> 5) & 0x7);
    return 1;
}

int Interpreter::mcr(uint32_t opcode) {
    if (PPC_UNLIKELY(arm7)) return 1;
    core->cp15.write((opcode >> 16) & 0xF, opcode & 0xF, (opcode >> 5) & 0x7,
                     *registers[(opcode >> 12) & 0xF]);
    return 1;
}

// ---- THUMB transfer instructions ----

int Interpreter::ldrsbRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + *registers[(opcode >> 6) & 0x7];
    *op0 = (uint32_t)(int32_t)(int8_t)core->memory.read<uint8_t>(arm7, addr);
    return ldr_cycles(arm7);
}

int Interpreter::ldrshRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + *registers[(opcode >> 6) & 0x7];
    *op0 = (uint32_t)(int32_t)(int16_t)core->memory.read<uint16_t>(arm7, addr);
    if (PPC_UNLIKELY(addr & arm7))
        *op0 = (uint32_t)(int32_t)((int16_t)*op0 >> 8);
    return ldr_cycles(arm7);
}

int Interpreter::ldrbRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + *registers[(opcode >> 6) & 0x7];
    *op0 = core->memory.read<uint8_t>(arm7, addr);
    return ldr_cycles(arm7);
}

int Interpreter::strbRegT(uint16_t opcode) {
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + *registers[(opcode >> 6) & 0x7];
    core->memory.write<uint8_t>(arm7, addr, *registers[opcode & 0x7]);
    return str_cycles(arm7);
}

int Interpreter::ldrhRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + *registers[(opcode >> 6) & 0x7];
    *op0 = core->memory.read<uint16_t>(arm7, addr);
    if (PPC_UNLIKELY(addr & arm7)) {
        uint32_t v = *op0;
        __asm__("rlwinm %0,%1,24,0,31" : "=r"(*op0) : "r"(v));
    }
    return ldr_cycles(arm7);
}

int Interpreter::strhRegT(uint16_t opcode) {
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + *registers[(opcode >> 6) & 0x7];
    core->memory.write<uint16_t>(arm7, addr, *registers[opcode & 0x7]);
    return str_cycles(arm7);
}

int Interpreter::ldrRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + *registers[(opcode >> 6) & 0x7];
    *op0 = core->memory.read<uint32_t>(arm7, addr);
    *op0 = rotateWordMisaligned(*op0, addr);
    return ldr_cycles(arm7);
}

int Interpreter::strRegT(uint16_t opcode) {
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + *registers[(opcode >> 6) & 0x7];
    core->memory.write<uint32_t>(arm7, addr, *registers[opcode & 0x7]);
    return str_cycles(arm7);
}

int Interpreter::ldrbImm5T(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + ((opcode >> 6) & 0x1F);
    *op0 = core->memory.read<uint8_t>(arm7, addr);
    return ldr_cycles(arm7);
}

int Interpreter::strbImm5T(uint16_t opcode) {
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + ((opcode >> 6) & 0x1F);
    core->memory.write<uint8_t>(arm7, addr, *registers[opcode & 0x7]);
    return str_cycles(arm7);
}

int Interpreter::ldrhImm5T(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + ((opcode >> 5) & 0x3E);
    *op0 = core->memory.read<uint16_t>(arm7, addr);
    if (PPC_UNLIKELY(addr & arm7)) {
        uint32_t v = *op0;
        __asm__("rlwinm %0,%1,24,0,31" : "=r"(*op0) : "r"(v));
    }
    return ldr_cycles(arm7);
}

int Interpreter::strhImm5T(uint16_t opcode) {
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + ((opcode >> 5) & 0x3E);
    core->memory.write<uint16_t>(arm7, addr, *registers[opcode & 0x7]);
    return str_cycles(arm7);
}

int Interpreter::ldrImm5T(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + ((opcode >> 4) & 0x7C);
    *op0 = core->memory.read<uint32_t>(arm7, addr);
    *op0 = rotateWordMisaligned(*op0, addr);
    return ldr_cycles(arm7);
}

int Interpreter::strImm5T(uint16_t opcode) {
    uint32_t addr = *registers[(opcode >> 3) & 0x7] + ((opcode >> 4) & 0x7C);
    core->memory.write<uint32_t>(arm7, addr, *registers[opcode & 0x7]);
    return str_cycles(arm7);
}

int Interpreter::ldrPcT(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode >> 8) & 0x7];
    uint32_t addr = (*registers[15] & ~3U) + ((opcode & 0xFF) << 2);
    *op0 = core->memory.read<uint32_t>(arm7, addr);
    *op0 = rotateWordMisaligned(*op0, addr);
    return ldr_cycles(arm7);
}

int Interpreter::ldrSpT(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode >> 8) & 0x7];
    uint32_t addr = *registers[13] + ((opcode & 0xFF) << 2);
    *op0 = core->memory.read<uint32_t>(arm7, addr);
    *op0 = rotateWordMisaligned(*op0, addr);
    return ldr_cycles(arm7);
}

int Interpreter::strSpT(uint16_t opcode) {
    uint32_t addr = *registers[13] + ((opcode & 0xFF) << 2);
    core->memory.write<uint32_t>(arm7, addr, *registers[(opcode >> 8) & 0x7]);
    return str_cycles(arm7);
}

// ---- THUMB block transfers ----

int Interpreter::ldmiaT(uint16_t opcode) {
    uint8_t m = ppc_popcount16(opcode & 0xFF);
    uint32_t *op0 = registers[(opcode >> 8) & 0x7];
    uint32_t address = (*op0 += (m << 2)) - (m << 2);
    for (int i = 0; i < 8; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *registers[i] = core->memory.read<uint32_t>(arm7, address);
        address += 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::stmiaT(uint16_t opcode) {
    uint8_t m = ppc_popcount16(opcode & 0xFF);
    uint8_t op0 = (opcode >> 8) & 0x7;
    uint32_t address = *registers[op0];
    if (arm7 && arm7StmNeedsEarlyWb(opcode, op0))
        *registers[op0] = address + (m << 2);
    for (int i = 0; i < 8; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, address, *registers[i]);
        address += 4;
    }
    *registers[op0] = address;
    return m + (arm7 || m < 2);
}

int Interpreter::popT(uint16_t opcode) {
    uint8_t m = ppc_popcount16(opcode & 0xFF);
    for (int i = 0; i < 8; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *registers[i] = core->memory.read<uint32_t>(arm7, *registers[13]);
        *registers[13] += 4;
    }
    return m + (arm7 ? 2 : (m < 2));
}

int Interpreter::pushT(uint16_t opcode) {
    uint8_t m = ppc_popcount16(opcode & 0xFF);
    uint32_t address = (*registers[13] -= (m << 2));
    for (int i = 0; i < 8; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, address, *registers[i]);
        address += 4;
    }
    return m + (arm7 || m < 2);
}

int Interpreter::popPcT(uint16_t opcode) {
    uint8_t m = ppc_popcount16(opcode & 0xFF) + 1;
    for (int i = 0; i < 8; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        *registers[i] = core->memory.read<uint32_t>(arm7, *registers[13]);
        *registers[13] += 4;
    }
    *registers[15] = core->memory.read<uint32_t>(arm7, *registers[13]);
    *registers[13] += 4;
    // Clear THUMB bit if bit0 of loaded PC is clear and not ARM7
    cpsr &= ~((~(*registers[15]) & (uint32_t)!arm7) << 5);
    flushPipeline();
    return m + 4;
}

int Interpreter::pushLrT(uint16_t opcode) {
    uint8_t m = ppc_popcount16(opcode & 0xFF) + 1;
    uint32_t address = (*registers[13] -= (m << 2));
    for (int i = 0; i < 8; i++) {
        if (PPC_LIKELY(!(opcode & BIT(i)))) continue;
        core->memory.write<uint32_t>(arm7, address, *registers[i]);
        address += 4;
    }
    core->memory.write<uint32_t>(arm7, address, *registers[14]);
    return m + (arm7 || m < 2);
}
