//interpreter_alu.cpp (optimized for PowerPC/Wii)
#include "core.h"

// PowerPC assembly helpers for flag computation
// Uses PPC integer instructions directly for speed

// PowerPC CLZ instruction wrapper
static ALWAYS_INLINE uint32_t ppc_clz(uint32_t x) {
    uint32_t result;
    __asm__("cntlzw %0,%1" : "=r"(result) : "r"(x));
    return result;
}

// Multiply helpers using PPC multiply instructions
static ALWAYS_INLINE uint32_t ppc_mulhw(int32_t a, int32_t b) {
    uint32_t result;
    __asm__("mulhw %0,%1,%2" : "=r"(result) : "r"(a), "r"(b));
    return result;
}

static ALWAYS_INLINE uint32_t ppc_mulhwu(uint32_t a, uint32_t b) {
    uint32_t result;
    __asm__("mulhwu %0,%1,%2" : "=r"(result) : "r"(a), "r"(b));
    return result;
}

// Optimized flag computation for subtract operations using PPC
// Returns NZCV flags in bits 31:28 of result
static ALWAYS_INLINE uint32_t ppc_sub_flags(uint32_t op1, uint32_t op2, uint32_t res) {
    // N: bit 31 of result
    // Z: result == 0
    // C: op1 >= op2 (no borrow) -> op1 >= res (unsigned)
    // V: signed overflow: (op2^op1) & ~(res^op2) & BIT(31)
    uint32_t n = res & 0x80000000U;
    uint32_t z = (res == 0) ? 0x40000000U : 0;
    uint32_t c = (op1 >= op2) ? 0x20000000U : 0;
    uint32_t v = ((op2 ^ op1) & ~(res ^ op2) & 0x80000000U) >> 3;
    return n | z | c | v;
}

static ALWAYS_INLINE uint32_t ppc_add_flags(uint32_t op1, uint32_t op2, uint32_t res) {
    uint32_t n = res & 0x80000000U;
    uint32_t z = (res == 0) ? 0x40000000U : 0;
    uint32_t c = (op1 > res) ? 0x20000000U : 0;
    uint32_t v = (~(op2 ^ op1) & (res ^ op2) & 0x80000000U) >> 3;
    return n | z | c | v;
}

// NZ-only flag update
static ALWAYS_INLINE uint32_t ppc_nz_flags(uint32_t res) {
    return (res & 0x80000000U) | ((res == 0) ? 0x40000000U : 0);
}

// ALU dispatch macros - unchanged from original but now using optimized helpers
#define ALU_FUNCS(func, S) \
    int Interpreter::func##Lli(uint32_t opcode) { return func(opcode, lli##S(opcode)); } \
    int Interpreter::func##Llr(uint32_t opcode) { return func(opcode, llr##S(opcode)) + 1; } \
    int Interpreter::func##Lri(uint32_t opcode) { return func(opcode, lri##S(opcode)); } \
    int Interpreter::func##Lrr(uint32_t opcode) { return func(opcode, lrr##S(opcode)) + 1; } \
    int Interpreter::func##Ari(uint32_t opcode) { return func(opcode, ari##S(opcode)); } \
    int Interpreter::func##Arr(uint32_t opcode) { return func(opcode, arr##S(opcode)) + 1; } \
    int Interpreter::func##Rri(uint32_t opcode) { return func(opcode, rri##S(opcode)); } \
    int Interpreter::func##Rrr(uint32_t opcode) { return func(opcode, rrr##S(opcode)) + 1; } \
    int Interpreter::func##Imm(uint32_t opcode) { return func(opcode, imm##S(opcode)); }

ALU_FUNCS(_and,)
ALU_FUNCS(ands, S)
ALU_FUNCS(eor,)
ALU_FUNCS(eors, S)
ALU_FUNCS(sub,)
ALU_FUNCS(subs,)
ALU_FUNCS(rsb,)
ALU_FUNCS(rsbs,)
ALU_FUNCS(add,)
ALU_FUNCS(adds,)
ALU_FUNCS(adc,)
ALU_FUNCS(adcs,)
ALU_FUNCS(sbc,)
ALU_FUNCS(sbcs,)
ALU_FUNCS(rsc,)
ALU_FUNCS(rscs,)
ALU_FUNCS(tst, S)
ALU_FUNCS(teq, S)
ALU_FUNCS(cmp, S)
ALU_FUNCS(cmn, S)
ALU_FUNCS(orr,)
ALU_FUNCS(orrs, S)
ALU_FUNCS(mov,)
ALU_FUNCS(movs, S)
ALU_FUNCS(bic,)
ALU_FUNCS(bics, S)
ALU_FUNCS(mvn,)
ALU_FUNCS(mvns, S)

ALWAYS_INLINE int32_t Interpreter::clampQ(int64_t value) {
    if (PPC_UNLIKELY(value > 0x7FFFFFFFLL)) {
        cpsr |= BIT(27);
        return 0x7FFFFFFF;
    }
    if (PPC_UNLIKELY(value < -0x80000000LL)) {
        cpsr |= BIT(27);
        return (int32_t)0x80000000;
    }
    return (int32_t)value;
}

// ---- Shift operations - optimized with PowerPC rotate instructions ----

ALWAYS_INLINE uint32_t Interpreter::lli(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF];
    uint8_t shift = (opcode >> 7) & 0x1F;
    // PPC: slw handles 0 shift correctly (no-op)
    uint32_t result;
    __asm__("slw %0,%1,%2" : "=r"(result) : "r"(value), "r"(shift));
    return result;
}

ALWAYS_INLINE uint32_t Interpreter::llr(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF] + (((opcode & 0xF) == 0xF) << 2);
    uint8_t shift = *registers[(opcode >> 8) & 0xF];
    uint32_t result;
    __asm__("slw %0,%1,%2" : "=r"(result) : "r"(value), "r"((uint32_t)shift));
    return result;
}

ALWAYS_INLINE uint32_t Interpreter::lri(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF];
    uint8_t shift = (opcode >> 7) & 0x1F;
    if (PPC_UNLIKELY(!shift)) return 0;
    uint32_t result;
    __asm__("srw %0,%1,%2" : "=r"(result) : "r"(value), "r"((uint32_t)shift));
    return result;
}

ALWAYS_INLINE uint32_t Interpreter::lrr(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF] + (((opcode & 0xF) == 0xF) << 2);
    uint8_t shift = *registers[(opcode >> 8) & 0xF];
    uint32_t result;
    __asm__("srw %0,%1,%2" : "=r"(result) : "r"(value), "r"((uint32_t)shift));
    return result;
}

ALWAYS_INLINE uint32_t Interpreter::ari(uint32_t opcode) {
    int32_t value = *registers[opcode & 0xF];
    uint8_t shift = (opcode >> 7) & 0x1F;
    // PPC: sraw handles shift=0 -> we need shift of 31 minimum
    uint32_t sh = shift ? shift : 31;
    int32_t result;
    __asm__("sraw %0,%1,%2" : "=r"(result) : "r"(value), "r"(sh));
    return (uint32_t)result;
}

ALWAYS_INLINE uint32_t Interpreter::arr(uint32_t opcode) {
    int32_t value = *registers[opcode & 0xF] + (((opcode & 0xF) == 0xF) << 2);
    uint8_t shift = *registers[(opcode >> 8) & 0xF];
    uint32_t sh = (shift < 32) ? shift : 31;
    int32_t result;
    __asm__("sraw %0,%1,%2" : "=r"(result) : "r"(value), "r"(sh));
    return (uint32_t)result;
}

ALWAYS_INLINE uint32_t Interpreter::rri(uint32_t opcode) {
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

ALWAYS_INLINE uint32_t Interpreter::rrr(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF] + (((opcode & 0xF) == 0xF) << 2);
    uint8_t shift = *registers[(opcode >> 8) & 0xF] & 0x1F;
    if (PPC_UNLIKELY(!shift)) return value;
    uint32_t result;
    __asm__("rlwnm %0,%1,%2,0,31" : "=r"(result) : "r"(value), "r"((uint32_t)(32 - shift)));
    return result;
}

ALWAYS_INLINE uint32_t Interpreter::imm(uint32_t opcode) {
    uint32_t value = opcode & 0xFF;
    uint8_t shift = (opcode >> 7) & 0x1E;
    if (PPC_UNLIKELY(!shift)) return value;
    uint32_t result;
    __asm__("rlwnm %0,%1,%2,0,31" : "=r"(result) : "r"(value), "r"((uint32_t)(32 - shift)));
    return result;
}

// ---- Shift operations with carry (S variants) ----

ALWAYS_INLINE uint32_t Interpreter::lliS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF];
    uint8_t shift = (opcode >> 7) & 0x1F;
    if (shift > 0) {
        // Carry = bit (32-shift) of value
        uint32_t carry = (value >> (32 - shift)) & 1;
        cpsr = (cpsr & ~BIT(29)) | (carry << 29);
    }
    uint32_t result;
    __asm__("slw %0,%1,%2" : "=r"(result) : "r"(value), "r"((uint32_t)shift));
    return result;
}

ALWAYS_INLINE uint32_t Interpreter::llrS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF] + (((opcode & 0xF) == 0xF) << 2);
    uint8_t shift = *registers[(opcode >> 8) & 0xF];
    if (shift > 0) {
        uint32_t carry = (shift <= 32) ? ((value >> (32 - shift)) & 1) : 0;
        cpsr = (cpsr & ~BIT(29)) | (carry << 29);
    }
    uint32_t result;
    __asm__("slw %0,%1,%2" : "=r"(result) : "r"(value), "r"((uint32_t)shift));
    return result;
}

ALWAYS_INLINE uint32_t Interpreter::lriS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF];
    uint8_t shift = (opcode >> 7) & 0x1F;
    // Carry = bit (shift-1) if shift>0, else bit 31
    uint32_t carryBit = shift ? (shift - 1) : 31;
    uint32_t carry = (value >> carryBit) & 1;
    cpsr = (cpsr & ~BIT(29)) | (carry << 29);
    if (PPC_UNLIKELY(!shift)) return 0;
    uint32_t result;
    __asm__("srw %0,%1,%2" : "=r"(result) : "r"(value), "r"((uint32_t)shift));
    return result;
}

ALWAYS_INLINE uint32_t Interpreter::lrrS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF] + (((opcode & 0xF) == 0xF) << 2);
    uint8_t shift = *registers[(opcode >> 8) & 0xF];
    if (shift > 0) {
        uint32_t carry = (shift <= 32) ? ((value >> (shift - 1)) & 1) : 0;
        cpsr = (cpsr & ~BIT(29)) | (carry << 29);
    }
    uint32_t result;
    __asm__("srw %0,%1,%2" : "=r"(result) : "r"(value), "r"((uint32_t)shift));
    return result;
}

ALWAYS_INLINE uint32_t Interpreter::ariS(uint32_t opcode) {
    int32_t value = *registers[opcode & 0xF];
    uint8_t shift = (opcode >> 7) & 0x1F;
    uint32_t carryBit = shift ? (shift - 1) : 31;
    uint32_t carry = ((uint32_t)value >> carryBit) & 1;
    cpsr = (cpsr & ~BIT(29)) | (carry << 29);
    uint32_t sh = shift ? shift : 31;
    int32_t result;
    __asm__("sraw %0,%1,%2" : "=r"(result) : "r"(value), "r"(sh));
    return (uint32_t)result;
}

ALWAYS_INLINE uint32_t Interpreter::arrS(uint32_t opcode) {
    int32_t value = *registers[opcode & 0xF] + (((opcode & 0xF) == 0xF) << 2);
    uint8_t shift = *registers[(opcode >> 8) & 0xF];
    if (shift > 0) {
        uint32_t cb = (shift <= 32) ? (shift - 1) : 31;
        uint32_t carry = ((uint32_t)value >> cb) & 1;
        cpsr = (cpsr & ~BIT(29)) | (carry << 29);
    }
    uint32_t sh = (shift < 32) ? shift : 31;
    int32_t result;
    __asm__("sraw %0,%1,%2" : "=r"(result) : "r"(value), "r"(sh));
    return (uint32_t)result;
}

ALWAYS_INLINE uint32_t Interpreter::rriS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF];
    uint8_t shift = (opcode >> 7) & 0x1F;
    uint32_t res;
    if (PPC_UNLIKELY(!shift)) {
        // RRX with carry
        res = ((cpsr & BIT(29)) << 2) | (value >> 1);
        // Carry = bit 0 of original value
        cpsr = (cpsr & ~BIT(29)) | ((value & 1) << 29);
    } else {
        uint32_t carry = (value >> (shift - 1)) & 1;
        cpsr = (cpsr & ~BIT(29)) | (carry << 29);
        __asm__("rlwnm %0,%1,%2,0,31" : "=r"(res) : "r"(value), "r"((uint32_t)(32 - shift)));
    }
    return res;
}

ALWAYS_INLINE uint32_t Interpreter::rrrS(uint32_t opcode) {
    uint32_t value = *registers[opcode & 0xF] + (((opcode & 0xF) == 0xF) << 2);
    uint8_t shift = *registers[(opcode >> 8) & 0xF];
    if (shift > 0) {
        uint32_t carry = (value >> ((shift - 1) & 0x1F)) & 1;
        cpsr = (cpsr & ~BIT(29)) | (carry << 29);
    }
    uint8_t sh = shift & 0x1F;
    if (PPC_UNLIKELY(!sh)) return value;
    uint32_t result;
    __asm__("rlwnm %0,%1,%2,0,31" : "=r"(result) : "r"(value), "r"((uint32_t)(32 - sh)));
    return result;
}

ALWAYS_INLINE uint32_t Interpreter::immS(uint32_t opcode) {
    uint32_t value = opcode & 0xFF;
    uint8_t shift = (opcode >> 7) & 0x1E;
    if (shift > 0) {
        uint32_t carry = (value >> (shift - 1)) & 1;
        cpsr = (cpsr & ~BIT(29)) | (carry << 29);
    }
    if (PPC_UNLIKELY(!shift)) return value;
    uint32_t result;
    __asm__("rlwnm %0,%1,%2,0,31" : "=r"(result) : "r"(value), "r"((uint32_t)(32 - shift)));
    return result;
}

// ---- ALU operations - optimized flag computation ----

// Helper macro to get Rn with PC+4 correction for register-shifted operands
#define GET_RN(opcode) \
    (*registers[(opcode >> 16) & 0xF] + (((opcode & 0x20F0010) == 0x00F0010) << 2))

ALWAYS_INLINE int Interpreter::_and(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    *op0 = GET_RN(opcode) & op2;
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::eor(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    *op0 = GET_RN(opcode) ^ op2;
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::sub(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    *op0 = GET_RN(opcode) - op2;
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::rsb(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    *op0 = op2 - GET_RN(opcode);
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::add(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    *op0 = GET_RN(opcode) + op2;
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::adc(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    *op0 = GET_RN(opcode) + op2 + ((cpsr >> 29) & 1);
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::sbc(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    *op0 = GET_RN(opcode) - op2 - 1 + ((cpsr >> 29) & 1);
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::rsc(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    *op0 = op2 - GET_RN(opcode) - 1 + ((cpsr >> 29) & 1);
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::tst(uint32_t opcode, uint32_t op2) {
    uint32_t op1 = GET_RN(opcode);
    uint32_t res = op1 & op2;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(res);
    return 1;
}

ALWAYS_INLINE int Interpreter::teq(uint32_t opcode, uint32_t op2) {
    uint32_t op1 = GET_RN(opcode);
    uint32_t res = op1 ^ op2;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(res);
    return 1;
}

ALWAYS_INLINE int Interpreter::cmp(uint32_t opcode, uint32_t op2) {
    uint32_t op1 = GET_RN(opcode);
    uint32_t res = op1 - op2;
    cpsr = (cpsr & ~0xF0000000) | ppc_sub_flags(op1, op2, res);
    return 1;
}

ALWAYS_INLINE int Interpreter::cmn(uint32_t opcode, uint32_t op2) {
    uint32_t op1 = GET_RN(opcode);
    uint32_t res = op1 + op2;
    cpsr = (cpsr & ~0xF0000000) | ppc_add_flags(op1, op2, res);
    return 1;
}

ALWAYS_INLINE int Interpreter::orr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    *op0 = GET_RN(opcode) | op2;
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::mov(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    *op0 = op2;
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::bic(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    *op0 = GET_RN(opcode) & ~op2;
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::mvn(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    *op0 = ~op2;
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

// S-flag variants - use optimized flag helpers
ALWAYS_INLINE int Interpreter::ands(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t res = GET_RN(opcode) & op2;
    *op0 = res;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(res);
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::eors(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t res = GET_RN(opcode) ^ op2;
    *op0 = res;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(res);
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::subs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t op1 = GET_RN(opcode);
    uint32_t res = op1 - op2;
    *op0 = res;
    cpsr = (cpsr & ~0xF0000000) | ppc_sub_flags(op1, op2, res);
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::rsbs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t op1 = GET_RN(opcode);
    uint32_t res = op2 - op1;
    *op0 = res;
    // RSB: same as SUB but operands swapped for carry/overflow
    cpsr = (cpsr & ~0xF0000000) | ppc_sub_flags(op2, op1, res);
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::adds(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t op1 = GET_RN(opcode);
    uint32_t res = op1 + op2;
    *op0 = res;
    cpsr = (cpsr & ~0xF0000000) | ppc_add_flags(op1, op2, res);
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::adcs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t op1 = GET_RN(opcode);
    uint32_t carry = (cpsr >> 29) & 1;
    uint32_t res = op1 + op2 + carry;
    *op0 = res;
    // Carry: either op1+op2 overflowed, or (op1+op2)==0xFFFFFFFF and carry==1
    uint32_t n = res & 0x80000000U;
    uint32_t z = (res == 0) ? 0x40000000U : 0;
    uint32_t c = ((op1 > res) || (op2 == 0xFFFFFFFFU && carry)) ? 0x20000000U : 0;
    uint32_t v = (~(op2 ^ op1) & (res ^ op2) & 0x80000000U) >> 3;
    cpsr = (cpsr & ~0xF0000000) | n | z | c | v;
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::sbcs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t op1 = GET_RN(opcode);
    uint32_t carry = (cpsr >> 29) & 1;
    uint32_t res = op1 - op2 - 1 + carry;
    *op0 = res;
    uint32_t n = res & 0x80000000U;
    uint32_t z = (res == 0) ? 0x40000000U : 0;
    uint32_t c = ((op1 >= res) && (op2 != 0xFFFFFFFFU || carry)) ? 0x20000000U : 0;
    uint32_t v = ((op2 ^ op1) & ~(res ^ op2) & 0x80000000U) >> 3;
    cpsr = (cpsr & ~0xF0000000) | n | z | c | v;
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::rscs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t op1 = GET_RN(opcode);
    uint32_t carry = (cpsr >> 29) & 1;
    uint32_t res = op2 - op1 - 1 + carry;
    *op0 = res;
    uint32_t n = res & 0x80000000U;
    uint32_t z = (res == 0) ? 0x40000000U : 0;
    uint32_t c = ((op2 >= res) && (op1 != 0xFFFFFFFFU || carry)) ? 0x20000000U : 0;
    uint32_t v = ((op1 ^ op2) & ~(res ^ op1) & 0x80000000U) >> 3;
    cpsr = (cpsr & ~0xF0000000) | n | z | c | v;
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::orrs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t res = GET_RN(opcode) | op2;
    *op0 = res;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(res);
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::movs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    *op0 = op2;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(op2);
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::bics(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t res = GET_RN(opcode) & ~op2;
    *op0 = res;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(res);
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

ALWAYS_INLINE int Interpreter::mvns(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t res = ~op2;
    *op0 = res;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(res);
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

// ---- Multiply operations - use PPC multiply instructions ----

// Helper: compute timing for ARM7 multiplies
static ALWAYS_INLINE int arm7_mul_cycles(int32_t op, int base) {
    // Count significant bytes using CLZ
    uint32_t uop = (op < 0) ? ~(uint32_t)op : (uint32_t)op;
    uint32_t clz;
    __asm__("cntlzw %0,%1" : "=r"(clz) : "r"(uop | 1)); // |1 avoids clz(0)=32
    // m = number of significant bytes beyond first
    int m = 4 - (clz >> 3); // clz/8 = leading zero bytes
    if (m < 1) m = 1;
    return m + base;
}

int Interpreter::mul(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    uint32_t op1 = *registers[opcode & 0xF];
    int32_t  op2 = *registers[(opcode >> 8) & 0xF];
    *op0 = op1 * (uint32_t)op2;
    if (!arm7) return 2;
    return arm7_mul_cycles(op2, 1);
}

int Interpreter::mla(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    uint32_t op1 = *registers[opcode & 0xF];
    int32_t  op2 = *registers[(opcode >> 8) & 0xF];
    uint32_t op3 = *registers[(opcode >> 12) & 0xF];
    *op0 = op1 * (uint32_t)op2 + op3;
    if (!arm7) return 2;
    return arm7_mul_cycles(op2, 2);
}

int Interpreter::umull(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t  op2 = *registers[opcode & 0xF];
    uint32_t  op3 = *registers[(opcode >> 8) & 0xF];
    // Use PPC mulhwu + mullw for 64-bit result
    uint32_t lo, hi;
    __asm__(
        "mullw  %0,%2,%3\n\t"
        "mulhwu %1,%2,%3"
        : "=&r"(lo), "=r"(hi)
        : "r"(op2), "r"(op3)
    );
    *op0 = lo;
    *op1 = hi;
    if (!arm7) return 3;
    return arm7_mul_cycles((int32_t)op3, 2);
}

int Interpreter::umlal(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t  op2 = *registers[opcode & 0xF];
    uint32_t  op3 = *registers[(opcode >> 8) & 0xF];
    uint32_t lo, hi;
    __asm__(
        "mullw  %0,%2,%3\n\t"
        "mulhwu %1,%2,%3"
        : "=&r"(lo), "=r"(hi)
        : "r"(op2), "r"(op3)
    );
    // Add accumulate
    uint32_t accLo = *op0;
    uint32_t accHi = *op1;
    uint32_t newLo = lo + accLo;
    uint32_t carry = (newLo < lo) ? 1 : 0;
    *op0 = newLo;
    *op1 = hi + accHi + carry;
    if (!arm7) return 3;
    return arm7_mul_cycles((int32_t)op3, 3);
}

int Interpreter::smull(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    int32_t   op2 = *registers[opcode & 0xF];
    int32_t   op3 = *registers[(opcode >> 8) & 0xF];
    uint32_t lo, hi;
    __asm__(
        "mullw  %0,%2,%3\n\t"
        "mulhw  %1,%2,%3"
        : "=&r"(lo), "=r"(hi)
        : "r"(op2), "r"(op3)
    );
    *op0 = lo;
    *op1 = hi;
    if (!arm7) return 3;
    return arm7_mul_cycles(op3, 2);
}

int Interpreter::smlal(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    int32_t   op2 = *registers[opcode & 0xF];
    int32_t   op3 = *registers[(opcode >> 8) & 0xF];
    uint32_t lo, hi;
    __asm__(
        "mullw  %0,%2,%3\n\t"
        "mulhw  %1,%2,%3"
        : "=&r"(lo), "=r"(hi)
        : "r"(op2), "r"(op3)
    );
    uint32_t accLo = *op0, accHi = *op1;
    uint32_t newLo = lo + accLo;
    uint32_t carry = (newLo < lo) ? 1 : 0;
    *op0 = newLo;
    *op1 = hi + accHi + carry;
    if (!arm7) return 3;
    return arm7_mul_cycles(op3, 3);
}

int Interpreter::muls(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    uint32_t op1 = *registers[opcode & 0xF];
    int32_t  op2 = *registers[(opcode >> 8) & 0xF];
    *op0 = op1 * (uint32_t)op2;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(*op0);
    if (!arm7) return 4;
    return arm7_mul_cycles(op2, 1);
}

int Interpreter::mlas(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    uint32_t op1 = *registers[opcode & 0xF];
    int32_t  op2 = *registers[(opcode >> 8) & 0xF];
    uint32_t op3 = *registers[(opcode >> 12) & 0xF];
    *op0 = op1 * (uint32_t)op2 + op3;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(*op0);
    if (!arm7) return 4;
    return arm7_mul_cycles(op2, 2);
}

int Interpreter::umulls(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t  op2 = *registers[opcode & 0xF];
    uint32_t  op3 = *registers[(opcode >> 8) & 0xF];
    uint32_t lo, hi;
    __asm__(
        "mullw  %0,%2,%3\n\t"
        "mulhwu %1,%2,%3"
        : "=&r"(lo), "=r"(hi)
        : "r"(op2), "r"(op3)
    );
    *op0 = lo;
    *op1 = hi;
    uint32_t res64_zero = (lo == 0 && hi == 0);
    cpsr = (cpsr & ~0xC0000000) | (hi & 0x80000000U) | (res64_zero << 30);
    if (!arm7) return 5;
    return arm7_mul_cycles((int32_t)op3, 2);
}

int Interpreter::umlals(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    uint32_t  op2 = *registers[opcode & 0xF];
    uint32_t  op3 = *registers[(opcode >> 8) & 0xF];
    uint32_t lo, hi;
    __asm__(
        "mullw  %0,%2,%3\n\t"
        "mulhwu %1,%2,%3"
        : "=&r"(lo), "=r"(hi)
        : "r"(op2), "r"(op3)
    );
    uint32_t accLo = *op0, accHi = *op1;
    uint32_t newLo = lo + accLo;
    uint32_t carry = (newLo < lo) ? 1 : 0;
    *op0 = newLo;
    *op1 = hi + accHi + carry;
    uint32_t res64_zero = (*op0 == 0 && *op1 == 0);
    cpsr = (cpsr & ~0xC0000000) | (*op1 & 0x80000000U) | (res64_zero << 30);
    if (!arm7) return 5;
    return arm7_mul_cycles((int32_t)op3, 3);
}

int Interpreter::smulls(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    int32_t   op2 = *registers[opcode & 0xF];
    int32_t   op3 = *registers[(opcode >> 8) & 0xF];
    uint32_t lo, hi;
    __asm__(
        "mullw  %0,%2,%3\n\t"
        "mulhw  %1,%2,%3"
        : "=&r"(lo), "=r"(hi)
        : "r"(op2), "r"(op3)
    );
    *op0 = lo;
    *op1 = hi;
    uint32_t res64_zero = (lo == 0 && hi == 0);
    cpsr = (cpsr & ~0xC0000000) | (hi & 0x80000000U) | (res64_zero << 30);
    if (!arm7) return 5;
    return arm7_mul_cycles(op3, 2);
}

int Interpreter::smlals(uint32_t opcode) {
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    int32_t   op2 = *registers[opcode & 0xF];
    int32_t   op3 = *registers[(opcode >> 8) & 0xF];
    uint32_t lo, hi;
    __asm__(
        "mullw  %0,%2,%3\n\t"
        "mulhw  %1,%2,%3"
        : "=&r"(lo), "=r"(hi)
        : "r"(op2), "r"(op3)
    );
    uint32_t accLo = *op0, accHi = *op1;
    uint32_t newLo = lo + accLo;
    uint32_t carry = (newLo < lo) ? 1 : 0;
    *op0 = newLo;
    *op1 = hi + accHi + carry;
    uint32_t res64_zero = (*op0 == 0 && *op1 == 0);
    cpsr = (cpsr & ~0xC0000000) | (*op1 & 0x80000000U) | (res64_zero << 30);
    if (!arm7) return 5;
    return arm7_mul_cycles(op3, 3);
}

// ARM9-exclusive halfword multiplies using PPC mulhw/mullw

int Interpreter::smulbb(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    int16_t op1 = (int16_t)*registers[opcode & 0xF];
    int16_t op2 = (int16_t)*registers[(opcode >> 8) & 0xF];
    *op0 = (uint32_t)((int32_t)op1 * op2);
    return 1;
}

int Interpreter::smulbt(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    int16_t op1 = (int16_t)*registers[opcode & 0xF];
    int16_t op2 = (int16_t)(*registers[(opcode >> 8) & 0xF] >> 16);
    *op0 = (uint32_t)((int32_t)op1 * op2);
    return 1;
}

int Interpreter::smultb(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    int16_t op1 = (int16_t)(*registers[opcode & 0xF] >> 16);
    int16_t op2 = (int16_t)*registers[(opcode >> 8) & 0xF];
    *op0 = (uint32_t)((int32_t)op1 * op2);
    return 1;
}

int Interpreter::smultt(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    int16_t op1 = (int16_t)(*registers[opcode & 0xF] >> 16);
    int16_t op2 = (int16_t)(*registers[(opcode >> 8) & 0xF] >> 16);
    *op0 = (uint32_t)((int32_t)op1 * op2);
    return 1;
}

int Interpreter::smulwb(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    int32_t op1 = *registers[opcode & 0xF];
    int16_t op2 = (int16_t)*registers[(opcode >> 8) & 0xF];
    // (op1 * op2) >> 16 using PPC mulhw trick
    // mulhw gives top 32 bits of 64-bit signed product
    // We want (32x16) >> 16 = top 32 of (op1 * sign_ext(op2))
    int32_t hi;
    __asm__("mulhw %0,%1,%2" : "=r"(hi) : "r"(op1), "r"((int32_t)op2));
    uint32_t lo;
    __asm__("mullw %0,%1,%2" : "=r"(lo) : "r"(op1), "r"((int32_t)op2));
    // result = (hi:lo) >> 16 = (hi << 16) | (lo >> 16)
    *op0 = (uint32_t)((hi << 16) | (lo >> 16));
    return 1;
}

int Interpreter::smulwt(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    int32_t op1 = *registers[opcode & 0xF];
    int16_t op2 = (int16_t)(*registers[(opcode >> 8) & 0xF] >> 16);
    int32_t hi;
    uint32_t lo;
    __asm__("mulhw %0,%1,%2" : "=r"(hi) : "r"(op1), "r"((int32_t)op2));
    __asm__("mullw %0,%1,%2" : "=r"(lo) : "r"(op1), "r"((int32_t)op2));
    *op0 = (uint32_t)((hi << 16) | (lo >> 16));
    return 1;
}

// SMLA variants with Q flag saturation detection
int Interpreter::smlabb(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    int16_t op1 = (int16_t)*registers[opcode & 0xF];
    int16_t op2 = (int16_t)*registers[(opcode >> 8) & 0xF];
    int32_t op3 = *registers[(opcode >> 12) & 0xF];
    int32_t prod = (int32_t)op1 * op2;
    int32_t res32 = prod + op3;
    // Q overflow: if sign of (prod + op3) differs from expected
    // Detect via 64-bit check: (int64_t)prod + op3
    int64_t res64 = (int64_t)prod + op3;
    *op0 = (uint32_t)res32;
    if (PPC_UNLIKELY(res64 != (int64_t)res32)) cpsr |= BIT(27);
    return 1;
}

int Interpreter::smlabt(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    int16_t op1 = (int16_t)*registers[opcode & 0xF];
    int16_t op2 = (int16_t)(*registers[(opcode >> 8) & 0xF] >> 16);
    int32_t op3 = *registers[(opcode >> 12) & 0xF];
    int32_t prod = (int32_t)op1 * op2;
    int64_t res64 = (int64_t)prod + op3;
    *op0 = (uint32_t)(int32_t)res64;
    if (PPC_UNLIKELY(res64 != (int64_t)(int32_t)res64)) cpsr |= BIT(27);
    return 1;
}

int Interpreter::smlatb(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    int16_t op1 = (int16_t)(*registers[opcode & 0xF] >> 16);
    int16_t op2 = (int16_t)*registers[(opcode >> 8) & 0xF];
    int32_t op3 = *registers[(opcode >> 12) & 0xF];
    int32_t prod = (int32_t)op1 * op2;
    int64_t res64 = (int64_t)prod + op3;
    *op0 = (uint32_t)(int32_t)res64;
    if (PPC_UNLIKELY(res64 != (int64_t)(int32_t)res64)) cpsr |= BIT(27);
    return 1;
}

int Interpreter::smlatt(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    int16_t op1 = (int16_t)(*registers[opcode & 0xF] >> 16);
    int16_t op2 = (int16_t)(*registers[(opcode >> 8) & 0xF] >> 16);
    int32_t op3 = *registers[(opcode >> 12) & 0xF];
    int32_t prod = (int32_t)op1 * op2;
    int64_t res64 = (int64_t)prod + op3;
    *op0 = (uint32_t)(int32_t)res64;
    if (PPC_UNLIKELY(res64 != (int64_t)(int32_t)res64)) cpsr |= BIT(27);
    return 1;
}

int Interpreter::smlawb(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    int32_t op1 = *registers[opcode & 0xF];
    int16_t op2 = (int16_t)*registers[(opcode >> 8) & 0xF];
    int32_t op3 = *registers[(opcode >> 12) & 0xF];
    int32_t hi;
    uint32_t lo;
    __asm__("mulhw %0,%1,%2" : "=r"(hi) : "r"(op1), "r"((int32_t)op2));
    __asm__("mullw %0,%1,%2" : "=r"(lo) : "r"(op1), "r"((int32_t)op2));
    int32_t prod16 = (int32_t)((hi << 16) | (lo >> 16));
    int64_t res64 = (int64_t)prod16 + op3;
    *op0 = (uint32_t)(int32_t)res64;
    if (PPC_UNLIKELY(res64 != (int64_t)(int32_t)res64)) cpsr |= BIT(27);
    return 1;
}

int Interpreter::smlawt(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 16) & 0xF];
    int32_t op1 = *registers[opcode & 0xF];
    int16_t op2 = (int16_t)(*registers[(opcode >> 8) & 0xF] >> 16);
    int32_t op3 = *registers[(opcode >> 12) & 0xF];
    int32_t hi;
    uint32_t lo;
    __asm__("mulhw %0,%1,%2" : "=r"(hi) : "r"(op1), "r"((int32_t)op2));
    __asm__("mullw %0,%1,%2" : "=r"(lo) : "r"(op1), "r"((int32_t)op2));
    int32_t prod16 = (int32_t)((hi << 16) | (lo >> 16));
    int64_t res64 = (int64_t)prod16 + op3;
    *op0 = (uint32_t)(int32_t)res64;
    if (PPC_UNLIKELY(res64 != (int64_t)(int32_t)res64)) cpsr |= BIT(27);
    return 1;
}

// SMLAL variants - 64-bit accumulate
int Interpreter::smlalbb(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    int16_t op2 = (int16_t)*registers[opcode & 0xF];
    int16_t op3 = (int16_t)*registers[(opcode >> 8) & 0xF];
    int32_t prod = (int32_t)op2 * op3;
    uint32_t accLo = *op0, accHi = *op1;
    uint32_t newLo = accLo + (uint32_t)prod;
    uint32_t carry = (newLo < accLo) ? 1 : 0;
    *op0 = newLo;
    *op1 = accHi + (uint32_t)((int32_t)prod >> 31) + carry;
    return 2;
}

int Interpreter::smlalbt(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    int16_t op2 = (int16_t)*registers[opcode & 0xF];
    int16_t op3 = (int16_t)(*registers[(opcode >> 8) & 0xF] >> 16);
    int32_t prod = (int32_t)op2 * op3;
    uint32_t accLo = *op0, accHi = *op1;
    uint32_t newLo = accLo + (uint32_t)prod;
    uint32_t carry = (newLo < accLo) ? 1 : 0;
    *op0 = newLo;
    *op1 = accHi + (uint32_t)((int32_t)prod >> 31) + carry;
    return 2;
}

int Interpreter::smlaltb(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    int16_t op2 = (int16_t)(*registers[opcode & 0xF] >> 16);
    int16_t op3 = (int16_t)*registers[(opcode >> 8) & 0xF];
    int32_t prod = (int32_t)op2 * op3;
    uint32_t accLo = *op0, accHi = *op1;
    uint32_t newLo = accLo + (uint32_t)prod;
    uint32_t carry = (newLo < accLo) ? 1 : 0;
    *op0 = newLo;
    *op1 = accHi + (uint32_t)((int32_t)prod >> 31) + carry;
    return 2;
}

int Interpreter::smlaltt(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t *op1 = registers[(opcode >> 16) & 0xF];
    int16_t op2 = (int16_t)(*registers[opcode & 0xF] >> 16);
    int16_t op3 = (int16_t)(*registers[(opcode >> 8) & 0xF] >> 16);
    int32_t prod = (int32_t)op2 * op3;
    uint32_t accLo = *op0, accHi = *op1;
    uint32_t newLo = accLo + (uint32_t)prod;
    uint32_t carry = (newLo < accLo) ? 1 : 0;
    *op0 = newLo;
    *op1 = accHi + (uint32_t)((int32_t)prod >> 31) + carry;
    return 2;
}

int Interpreter::qadd(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    int32_t op1 = *registers[opcode & 0xF];
    int32_t op2 = *registers[(opcode >> 16) & 0xF];
    *op0 = (uint32_t)clampQ((int64_t)op1 + op2);
    return 1;
}

int Interpreter::qsub(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    int32_t op1 = *registers[opcode & 0xF];
    int32_t op2 = *registers[(opcode >> 16) & 0xF];
    *op0 = (uint32_t)clampQ((int64_t)op1 - op2);
    return 1;
}

int Interpreter::qdadd(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    int32_t op1 = *registers[opcode & 0xF];
    int32_t op2 = *registers[(opcode >> 16) & 0xF];
    *op0 = (uint32_t)clampQ((int64_t)op1 + clampQ((int64_t)op2 * 2));
    return 1;
}

int Interpreter::qdsub(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    int32_t op1 = *registers[opcode & 0xF];
    int32_t op2 = *registers[(opcode >> 16) & 0xF];
    *op0 = (uint32_t)clampQ((int64_t)op1 - clampQ((int64_t)op2 * 2));
    return 1;
}

int Interpreter::clz(uint32_t opcode) {
    if (arm7) return 1;
    uint32_t *op0 = registers[(opcode >> 12) & 0xF];
    uint32_t op1 = *registers[opcode & 0xF];
    // Direct PowerPC count leading zeros instruction
    __asm__("cntlzw %0,%1" : "=r"(*op0) : "r"(op1));
    return 1;
}

// ---- THUMB ALU operations ----

int Interpreter::addRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t op1 = *registers[(opcode >> 3) & 0x7];
    uint32_t op2 = *registers[(opcode >> 6) & 0x7];
    uint32_t res = op1 + op2;
    *op0 = res;
    cpsr = (cpsr & ~0xF0000000) | ppc_add_flags(op1, op2, res);
    return 1;
}

int Interpreter::subRegT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t op1 = *registers[(opcode >> 3) & 0x7];
    uint32_t op2 = *registers[(opcode >> 6) & 0x7];
    uint32_t res = op1 - op2;
    *op0 = res;
    cpsr = (cpsr & ~0xF0000000) | ppc_sub_flags(op1, op2, res);
    return 1;
}

int Interpreter::addHT(uint16_t opcode) {
    uint32_t *op0 = registers[((opcode >> 4) & 0x8) | (opcode & 0x7)];
    *op0 += *registers[(opcode >> 3) & 0xF];
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

int Interpreter::cmpHT(uint16_t opcode) {
    uint32_t op1 = *registers[((opcode >> 4) & 0x8) | (opcode & 0x7)];
    uint32_t op2 = *registers[(opcode >> 3) & 0xF];
    uint32_t res = op1 - op2;
    cpsr = (cpsr & ~0xF0000000) | ppc_sub_flags(op1, op2, res);
    return 1;
}

int Interpreter::movHT(uint16_t opcode) {
    uint32_t *op0 = registers[((opcode >> 4) & 0x8) | (opcode & 0x7)];
    *op0 = *registers[(opcode >> 3) & 0xF];
    if (PPC_LIKELY(op0 != registers[15])) return 1;
    flushPipeline();
    return 3;
}

int Interpreter::addPcT(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode >> 8) & 0x7];
    *op0 = (*registers[15] & ~3U) + ((opcode & 0xFF) << 2);
    return 1;
}

int Interpreter::addSpT(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode >> 8) & 0x7];
    *op0 = *registers[13] + ((opcode & 0xFF) << 2);
    return 1;
}

int Interpreter::addSpImmT(uint16_t opcode) {
    uint32_t *op0 = registers[13];
    // Sign-extend and scale
    int32_t off = (opcode & BIT(7)) ? -(int32_t)(opcode & 0x7F) : (int32_t)(opcode & 0x7F);
    *op0 += (uint32_t)(off << 2);
    return 1;
}

int Interpreter::lslImmT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t op1 = *registers[(opcode >> 3) & 0x7];
    uint8_t op2 = (opcode >> 6) & 0x1F;
    *op0 = op1 << op2;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(*op0);
    if (op2 > 0) cpsr = (cpsr & ~BIT(29)) | (((op1 >> (32 - op2)) & 1) << 29);
    return 1;
}

int Interpreter::lsrImmT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t op1 = *registers[(opcode >> 3) & 0x7];
    uint8_t op2 = (opcode >> 6) & 0x1F;
    *op0 = op2 ? (op1 >> op2) : 0;
    uint32_t carryBit = op2 ? (op2 - 1) : 31;
    cpsr = (cpsr & ~0xE0000000) | ppc_nz_flags(*op0) | (((op1 >> carryBit) & 1) << 29);
    return 1;
}

int Interpreter::asrImmT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    int32_t op1 = *registers[(opcode >> 3) & 0x7];
    uint8_t op2 = (opcode >> 6) & 0x1F;
    *op0 = (uint32_t)(op2 ? (op1 >> op2) : ((op1 < 0) ? -1 : 0));
    uint32_t carryBit = op2 ? (op2 - 1) : 31;
    cpsr = (cpsr & ~0xE0000000) | ppc_nz_flags(*op0) | ((((uint32_t)op1 >> carryBit) & 1) << 29);
    return 1;
}

int Interpreter::addImm3T(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t op1 = *registers[(opcode >> 3) & 0x7];
    uint32_t op2 = (opcode >> 6) & 0x7;
    uint32_t res = op1 + op2;
    *op0 = res;
    cpsr = (cpsr & ~0xF0000000) | ppc_add_flags(op1, op2, res);
    return 1;
}

int Interpreter::subImm3T(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t op1 = *registers[(opcode >> 3) & 0x7];
    uint32_t op2 = (opcode >> 6) & 0x7;
    uint32_t res = op1 - op2;
    *op0 = res;
    cpsr = (cpsr & ~0xF0000000) | ppc_sub_flags(op1, op2, res);
    return 1;
}

int Interpreter::addImm8T(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode >> 8) & 0x7];
    uint32_t op1 = *op0;
    uint32_t op2 = opcode & 0xFF;
    *op0 = op1 + op2;
    cpsr = (cpsr & ~0xF0000000) | ppc_add_flags(op1, op2, *op0);
    return 1;
}

int Interpreter::subImm8T(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode >> 8) & 0x7];
    uint32_t op1 = *op0;
    uint32_t op2 = opcode & 0xFF;
    *op0 = op1 - op2;
    cpsr = (cpsr & ~0xF0000000) | ppc_sub_flags(op1, op2, *op0);
    return 1;
}

int Interpreter::cmpImm8T(uint16_t opcode) {
    uint32_t op1 = *registers[(opcode >> 8) & 0x7];
    uint32_t op2 = opcode & 0xFF;
    uint32_t res = op1 - op2;
    cpsr = (cpsr & ~0xF0000000) | ppc_sub_flags(op1, op2, res);
    return 1;
}

int Interpreter::movImm8T(uint16_t opcode) {
    uint32_t *op0 = registers[(opcode >> 8) & 0x7];
    uint32_t op2 = opcode & 0xFF;
    *op0 = op2;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(op2);
    return 1;
}

int Interpreter::lslDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t op1 = *op0;
    uint8_t op2 = *registers[(opcode >> 3) & 0x7];
    *op0 = (op2 < 32) ? (op1 << op2) : 0;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(*op0);
    if (op2 > 0 && op2 <= 32)
        cpsr = (cpsr & ~BIT(29)) | (((op1 >> (32 - op2)) & 1) << 29);
    else if (op2 > 32)
        cpsr &= ~BIT(29);
    return 1;
}

int Interpreter::lsrDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t op1 = *op0;
    uint8_t op2 = *registers[(opcode >> 3) & 0x7];
    *op0 = (op2 < 32) ? (op1 >> op2) : 0;
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(*op0);
    if (op2 > 0 && op2 <= 32)
        cpsr = (cpsr & ~BIT(29)) | (((op1 >> (op2 - 1)) & 1) << 29);
    else if (op2 > 32)
        cpsr &= ~BIT(29);
    return 1;
}

int Interpreter::asrDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    int32_t op1 = *op0;
    uint8_t op2 = *registers[(opcode >> 3) & 0x7];
    *op0 = (op2 < 32) ? (uint32_t)(op1 >> op2) : (uint32_t)((op1 < 0) ? -1 : 0);
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(*op0);
    if (op2 > 0) {
        uint32_t cb = (op2 <= 32) ? (op2 - 1) : 31;
        cpsr = (cpsr & ~BIT(29)) | ((((uint32_t)op1 >> cb) & 1) << 29);
    }
    return 1;
}

int Interpreter::rorDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t op1 = *op0;
    uint8_t op2 = *registers[(opcode >> 3) & 0x7];
    uint8_t sh = op2 & 0x1F;
    if (sh)
        *op0 = (op1 << (32 - sh)) | (op1 >> sh);
    // else op0 unchanged
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(*op0);
    if (op2 > 0) {
        uint32_t carry = (op1 >> ((op2 - 1) & 0x1F)) & 1;
        cpsr = (cpsr & ~BIT(29)) | (carry << 29);
    }
    return 1;
}

int Interpreter::andDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    *op0 &= *registers[(opcode >> 3) & 0x7];
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(*op0);
    return 1;
}

int Interpreter::eorDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    *op0 ^= *registers[(opcode >> 3) & 0x7];
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(*op0);
    return 1;
}

int Interpreter::adcDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t op1 = *op0;
    uint32_t op2 = *registers[(opcode >> 3) & 0x7];
    uint32_t carry = (cpsr >> 29) & 1;
    *op0 = op1 + op2 + carry;
    uint32_t n = *op0 & 0x80000000U;
    uint32_t z = (*op0 == 0) ? 0x40000000U : 0;
    uint32_t c = ((op1 > *op0) || (op2 == 0xFFFFFFFFU && carry)) ? 0x20000000U : 0;
    uint32_t v = (~(op2 ^ op1) & (*op0 ^ op2) & 0x80000000U) >> 3;
    cpsr = (cpsr & ~0xF0000000) | n | z | c | v;
    return 1;
}

int Interpreter::sbcDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t op1 = *op0;
    uint32_t op2 = *registers[(opcode >> 3) & 0x7];
    uint32_t carry = (cpsr >> 29) & 1;
    *op0 = op1 - op2 - 1 + carry;
    uint32_t n = *op0 & 0x80000000U;
    uint32_t z = (*op0 == 0) ? 0x40000000U : 0;
    uint32_t c = ((op1 >= *op0) && (op2 != 0xFFFFFFFFU || carry)) ? 0x20000000U : 0;
    uint32_t v = ((op2 ^ op1) & ~(*op0 ^ op2) & 0x80000000U) >> 3;
    cpsr = (cpsr & ~0xF0000000) | n | z | c | v;
    return 1;
}

int Interpreter::tstDpT(uint16_t opcode) {
    uint32_t res = *registers[opcode & 0x7] & *registers[(opcode >> 3) & 0x7];
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(res);
    return 1;
}

int Interpreter::cmpDpT(uint16_t opcode) {
    uint32_t op1 = *registers[opcode & 0x7];
    uint32_t op2 = *registers[(opcode >> 3) & 0x7];
    uint32_t res = op1 - op2;
    cpsr = (cpsr & ~0xF0000000) | ppc_sub_flags(op1, op2, res);
    return 1;
}

int Interpreter::cmnDpT(uint16_t opcode) {
    uint32_t op1 = *registers[opcode & 0x7];
    uint32_t op2 = *registers[(opcode >> 3) & 0x7];
    uint32_t res = op1 + op2;
    cpsr = (cpsr & ~0xF0000000) | ppc_add_flags(op1, op2, res);
    return 1;
}

int Interpreter::orrDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    *op0 |= *registers[(opcode >> 3) & 0x7];
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(*op0);
    return 1;
}

int Interpreter::bicDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    *op0 &= ~*registers[(opcode >> 3) & 0x7];
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(*op0);
    return 1;
}

int Interpreter::mvnDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    *op0 = ~*registers[(opcode >> 3) & 0x7];
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(*op0);
    return 1;
}

int Interpreter::negDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    uint32_t op2 = *registers[(opcode >> 3) & 0x7];
    uint32_t res = (uint32_t)(-(int32_t)op2);
    *op0 = res;
    // NEG = 0 - op2
    cpsr = (cpsr & ~0xF0000000) | ppc_sub_flags(0, op2, res);
    return 1;
}

int Interpreter::mulDpT(uint16_t opcode) {
    uint32_t *op0 = registers[opcode & 0x7];
    int32_t op1 = *registers[(opcode >> 3) & 0x7];
    int32_t op2 = *op0;
    *op0 = (uint32_t)((int32_t)op1 * op2);
    cpsr = (cpsr & ~0xC0000000) | ppc_nz_flags(*op0);
    if (!arm7) return 4;
    return arm7_mul_cycles(op2, 1);
}
