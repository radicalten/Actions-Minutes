// The ARM7TDMI's multiplier, modelled well enough to get its C flag right.
//
// The ARM ARM says the C (and V) flag is "set to a meaningless value" by a
// multiply, and for years emulators treated it as such.  It is meaningless but
// not random: it is the carry out of the multiplier's final adder, and the mGBA
// test suite's multiply-long table records it per operand pair.  This file is a
// port of the Booth-recoding / carry-save-adder simulation published by zaydlang
// (github.com/zaydlang/multiplication-algorithm, zlib licence, credited in
// NOTES.md), which reproduces every C value the suite records: 72/72 on the
// multiply-long table as checked by tools/host/suite_vectors_arm.txt.
//
// Only the long forms are exposed, and only the ARM7 CPU uses them.  The DS's
// ARM9 is an ARM946E-S with a different multiplier design, and the suite - being
// a GBA suite - says nothing about it, so jit3 and the interpreter leave C alone
// there.  The short MUL/MLA carry is also not modelled: the suite does not test
// it, and NooDS writes N/Z only.
//
// Cost: only S-form long multiplies on the ARM7 CPU reach this, so the ~4
// iterations of the CSA loop never land in a hot path.

#pragma once

#include <cstdint>

namespace Arm7TdmiMul {

struct Result {
    uint64_t output;
    bool carry;
};

namespace Detail {

inline int bit(uint64_t x, int n) { return (x >> n) & 1; }

inline uint64_t signExtend(uint64_t value, int fromSize, int toSize) {
    const uint64_t mask = ((uint64_t(1) << (toSize - fromSize)) - 1) << fromSize;
    return bit(value, fromSize - 1) ? (value | mask) : value;
}

// Arithmetic shift right of a `size`-bit value, the multiplier walks 8 bits a cycle.
inline uint64_t asr(uint64_t value, int shift, int size) {
    value = signExtend(value, size, 64);
    if (bit(value, 63)) value |= ~uint64_t(0) << (64 - shift);
    return (value >> shift) & ((uint64_t(1) << size) - 1);
}

inline void ror128(uint64_t& lo, uint64_t& hi, int shift) {
    const uint64_t nlo = (lo >> shift) | (hi << (64 - shift));
    const uint64_t nhi = (hi >> shift) | (lo << (64 - shift));
    lo = nlo;
    hi = nhi;
}

// Booth-recode one 3-bit chunk of the multiplier into a 34-bit addend.  Cases 4-6
// are negations, done as ones' complement; the +1 is injected as the carry that a
// carry-save adder leaves in the LSB.
inline void boothRecode(uint64_t input, uint32_t chunk, uint64_t& out, bool& carry) {
    switch (chunk) {
        case 0: out = 0; carry = false; break;
        case 1: case 2: out = input; carry = false; break;
        case 3: out = 2 * input; carry = false; break;
        case 4: out = ~(2 * input); carry = true; break;   // not 2 * ~input: see below
        case 5: case 6: out = ~input; carry = true; break;
        default: out = 0; carry = false; break;
    }
    out &= 0x3FFFFFFFFull;
}

struct Csa {
    uint64_t output;
    uint64_t carry;
};

inline Csa performCsa(uint64_t a, uint64_t b, uint64_t c) {
    return { a ^ b ^ c, (a & b) | (b & c) | (c & a) };
}

struct Adder {
    uint32_t output;
    bool carry;
};

inline Adder adder(uint32_t a, uint32_t b, bool carry) {
    const uint32_t output = a + b + carry;
    const uint64_t realOutput = uint64_t(a) + uint64_t(b) + uint64_t(carry);
    return { output, output != realOutput };
}

// One cycle: four recoded addends go through four CSAs down to two 33-bit
// results.  The two bottom bits of each CSA are peeled off into the final result
// (later addends are at least four times as large, so they can never change
// them), and accShiftRegister supplies the accumulate's high bits.
inline Csa csaArray(uint64_t partialSum, uint64_t partialCarry, uint64_t multiplicand,
                    uint64_t multiplier, uint64_t& accShiftRegister) {
    Csa csa { partialSum, partialCarry };
    Csa final { 0, 0 };
    for (int i = 0; i < 4; i++) {
        csa.output &= 0x1FFFFFFFFull;
        csa.carry &= 0x1FFFFFFFFull;

        uint64_t addend; bool addendCarry = false;
        boothRecode(multiplicand, (multiplier >> (2 * i)) & 7, addend, addendCarry);

        Csa result = performCsa(csa.output, addend & 0x1FFFFFFFFull, csa.carry);
        result.carry = (result.carry << 1) | (addendCarry ? 1 : 0);

        final.output |= (result.output & 3) << (2 * i);
        final.carry |= (result.carry & 3) << (2 * i);

        result.output >>= 2;
        result.carry >>= 2;

        // The sign-extension "magic": three bits that stand in for the repeated
        // copies of C[32] / X[33] a full 65-bit datapath would have.
        const uint64_t magic = uint64_t(bit(accShiftRegister, 0)) +
                               uint64_t(!bit(csa.carry, 32)) +
                               uint64_t(!bit(addend, 33));
        result.output |= magic << 31;
        result.carry |= uint64_t(!bit(accShiftRegister, 1)) << 32;
        accShiftRegister >>= 2;

        csa = result;
    }

    final.output |= csa.output << 8;
    final.carry |= csa.carry << 8;
    return final;
}

} // namespace Detail

// rm is the multiplicand, rs the scanned multiplier (the early-termination
// operand), accumulator the 64-bit addend of the accumulate forms.
inline Result longMultiply(uint32_t rm, uint32_t rs, bool isSigned, uint64_t accumulator) {
    uint64_t multiplicand = rm;
    uint64_t multiplier = rs;
    const bool aluCarryIn = multiplier & 1;

    if (isSigned) {
        multiplier = Detail::signExtend(multiplier, 32, 34);
        multiplicand = Detail::signExtend(multiplicand, 32, 34);
    } else {
        multiplier &= 0x1FFFFFFFFull;
        multiplicand &= 0x1FFFFFFFFull;
    }

    Detail::Csa csa;
    csa.carry = (multiplier & 1) ? ~multiplicand : 0;
    csa.output = accumulator;
    uint64_t accShiftRegister = accumulator >> 34;

    uint64_t partialSumLo = 0, partialSumHi = 0, partialCarryLo = 0, partialCarryHi = 0;
    partialSumLo = csa.output & 1;
    partialCarryLo = csa.carry & 1;

    csa.output >>= 1;
    csa.carry >>= 1;
    Detail::ror128(partialSumLo, partialSumHi, 1);
    Detail::ror128(partialCarryLo, partialCarryHi, 1);

    int numIterations = 0;
    for (;;) {
        csa = Detail::csaArray(csa.output, csa.carry, multiplicand, multiplier, accShiftRegister);

        partialSumLo |= csa.output & 0xFF;
        partialCarryLo |= csa.carry & 0xFF;

        csa.output >>= 8;
        csa.carry >>= 8;
        Detail::ror128(partialSumLo, partialSumHi, 8);
        Detail::ror128(partialCarryLo, partialCarryHi, 8);

        multiplier = Detail::asr(multiplier, 8, 33);
        numIterations++;

        // Early termination: the remaining upper bits of the multiplier can no
        // longer change anything.  Signed multiplies also stop on an all-ones
        // remainder, which is what makes them cheaper than unsigned ones.
        const bool done = isSigned ? (multiplier == 0x1FFFFFFFFull || multiplier == 0)
                                   : (multiplier == 0);
        if (done) break;
    }

    partialSumLo |= csa.output;
    partialCarryLo |= csa.carry;

    // The partials above were rotated right by 8 per cycle (plus one); undo it.
    Detail::ror128(partialSumLo, partialSumHi,
                   numIterations == 1 ? 23 : numIterations == 2 ? 15 : numIterations == 3 ? 7 : 31);
    Detail::ror128(partialCarryLo, partialCarryHi,
                   numIterations == 1 ? 23 : numIterations == 2 ? 15 : numIterations == 3 ? 7 : 31);

    // C is the carry out of the final adder, and on long multiplies it is read
    // off the *high* word of the partial carry.
    if (numIterations == 4) {
        const Detail::Adder lo = Detail::adder((uint32_t)partialSumHi, (uint32_t)partialCarryHi, aluCarryIn);
        const Detail::Adder hi = Detail::adder((uint32_t)(partialSumHi >> 32), (uint32_t)(partialCarryHi >> 32), lo.carry);
        return { (uint64_t(hi.output) << 32) | lo.output, ((partialCarryHi >> 63) & 1) != 0 };
    } else {
        const Detail::Adder lo = Detail::adder((uint32_t)(partialSumHi >> 32), (uint32_t)(partialCarryHi >> 32), aluCarryIn);

        const int shiftAmount = 2 + 8 * numIterations;
        partialCarryLo = Detail::signExtend(partialCarryLo, shiftAmount, 64);
        partialSumLo |= accShiftRegister << shiftAmount;

        const Detail::Adder hi = Detail::adder((uint32_t)partialSumLo, (uint32_t)partialCarryLo, lo.carry);
        return { (uint64_t(hi.output) << 32) | lo.output, ((partialCarryHi >> 63) & 1) != 0 };
    }
}

// The only thing NooDS needs: does the multiplier leave C set?
inline bool longMultiplyCarry(uint32_t rm, uint32_t rs, bool isSigned, uint64_t accumulator) {
    return longMultiply(rm, rs, isSigned, accumulator).carry;
}

} // namespace Arm7TdmiMul
