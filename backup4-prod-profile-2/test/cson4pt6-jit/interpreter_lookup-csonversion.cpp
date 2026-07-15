/*
 * interpreter_lookup.cpp - NooDS ARM/THUMB Instruction Lookup Tables
 * Replacement for the original interpreter_lookup.cpp
 *
 * Provides:
 *   Interpreter::armInstrs[]   - 4096-entry ARM instruction dispatch table
 *   Interpreter::thumbInstrs[] - 1024-entry THUMB instruction dispatch table
 *   Interpreter::condition[]   - 16x16 condition code truth table
 *   Interpreter::bitCount[]    - 256-entry popcount lookup table
 *
 * Based on NooDS source: https://github.com/Hydr8gon/NooDS
 * ARM opcode map: http://imrannazar.com/ARM-Opcode-Map
 */

#include "interpreter.h"

// ============================================================
//  Condition Code Truth Table
//  condition[cpsr_flags_nibble][arm_cond]
//  cpsr_flags_nibble = (N<<3)|(Z<<2)|(C<<1)|V  (bits 31:28 >> 28)
//  Returns non-zero if the condition is MET
//
//  ARM conditions:
//   0  EQ - Z set
//   1  NE - Z clear
//   2  CS - C set
//   3  CC - C clear
//   4  MI - N set
//   5  PL - N clear
//   6  VS - V set
//   7  VC - V clear
//   8  HI - C set & Z clear
//   9  LS - C clear | Z set
//  10  GE - N == V
//  11  LT - N != V
//  12  GT - Z clear & N == V
//  13  LE - Z set | N != V
//  14  AL - always
//  15  NV - never (ARMv4) / special (ARMv5)
// ============================================================

// Helper macro: extract individual flags from the 4-bit NZCV nibble
#define FLAG_N(x) (((x) >> 3) & 1)
#define FLAG_Z(x) (((x) >> 2) & 1)
#define FLAG_C(x) (((x) >> 1) & 1)
#define FLAG_V(x) (((x) >> 0) & 1)

// Build a single row of the condition table for a given NZCV nibble
#define COND_ROW(nzcv)                                      \
    /* EQ */ FLAG_Z(nzcv),                                  \
    /* NE */ !FLAG_Z(nzcv),                                 \
    /* CS */ FLAG_C(nzcv),                                  \
    /* CC */ !FLAG_C(nzcv),                                 \
    /* MI */ FLAG_N(nzcv),                                  \
    /* PL */ !FLAG_N(nzcv),                                 \
    /* VS */ FLAG_V(nzcv),                                  \
    /* VC */ !FLAG_V(nzcv),                                 \
    /* HI */ (FLAG_C(nzcv) && !FLAG_Z(nzcv)),               \
    /* LS */ (!FLAG_C(nzcv) || FLAG_Z(nzcv)),               \
    /* GE */ (FLAG_N(nzcv) == FLAG_V(nzcv)),                \
    /* LT */ (FLAG_N(nzcv) != FLAG_V(nzcv)),                \
    /* GT */ (!FLAG_Z(nzcv) && FLAG_N(nzcv) == FLAG_V(nzcv)), \
    /* LE */ (FLAG_Z(nzcv) || FLAG_N(nzcv) != FLAG_V(nzcv)), \
    /* AL */ 1,                                             \
    /* NV */ 0

// 16 rows (one per NZCV combination) x 16 columns (one per condition code)
const uint8_t Interpreter::condition[16][16] = {
    { COND_ROW(0x0) },   // NZCV = 0000
    { COND_ROW(0x1) },   // NZCV = 0001
    { COND_ROW(0x2) },   // NZCV = 0010
    { COND_ROW(0x3) },   // NZCV = 0011
    { COND_ROW(0x4) },   // NZCV = 0100
    { COND_ROW(0x5) },   // NZCV = 0101
    { COND_ROW(0x6) },   // NZCV = 0110
    { COND_ROW(0x7) },   // NZCV = 0111
    { COND_ROW(0x8) },   // NZCV = 1000
    { COND_ROW(0x9) },   // NZCV = 1001
    { COND_ROW(0xA) },   // NZCV = 1010
    { COND_ROW(0xB) },   // NZCV = 1011
    { COND_ROW(0xC) },   // NZCV = 1100
    { COND_ROW(0xD) },   // NZCV = 1101
    { COND_ROW(0xE) },   // NZCV = 1110
    { COND_ROW(0xF) },   // NZCV = 1111
};

#undef FLAG_N
#undef FLAG_Z
#undef FLAG_C
#undef FLAG_V
#undef COND_ROW

// ============================================================
//  Bit Count (popcount) Table
//  bitCount[x] = number of set bits in x, for x in [0, 255]
//  Used by LDM/STM to count registers and compute cycle cost
// ============================================================

// Macro to count bits in a nibble
#define B4(n) (((n)>>3)&1) + (((n)>>2)&1) + (((n)>>1)&1) + ((n)&1)

// Macro to count bits in a byte by combining two nibbles
#define B8(n) (B4((n)>>4) + B4((n)&0xF))

// Generate all 256 entries
const uint8_t Interpreter::bitCount[256] = {
    B8(0x00), B8(0x01), B8(0x02), B8(0x03),
    B8(0x04), B8(0x05), B8(0x06), B8(0x07),
    B8(0x08), B8(0x09), B8(0x0A), B8(0x0B),
    B8(0x0C), B8(0x0D), B8(0x0E), B8(0x0F),
    B8(0x10), B8(0x11), B8(0x12), B8(0x13),
    B8(0x14), B8(0x15), B8(0x16), B8(0x17),
    B8(0x18), B8(0x19), B8(0x1A), B8(0x1B),
    B8(0x1C), B8(0x1D), B8(0x1E), B8(0x1F),
    B8(0x20), B8(0x21), B8(0x22), B8(0x23),
    B8(0x24), B8(0x25), B8(0x26), B8(0x27),
    B8(0x28), B8(0x29), B8(0x2A), B8(0x2B),
    B8(0x2C), B8(0x2D), B8(0x2E), B8(0x2F),
    B8(0x30), B8(0x31), B8(0x32), B8(0x33),
    B8(0x34), B8(0x35), B8(0x36), B8(0x37),
    B8(0x38), B8(0x39), B8(0x3A), B8(0x3B),
    B8(0x3C), B8(0x3D), B8(0x3E), B8(0x3F),
    B8(0x40), B8(0x41), B8(0x42), B8(0x43),
    B8(0x44), B8(0x45), B8(0x46), B8(0x47),
    B8(0x48), B8(0x49), B8(0x4A), B8(0x4B),
    B8(0x4C), B8(0x4D), B8(0x4E), B8(0x4F),
    B8(0x50), B8(0x51), B8(0x52), B8(0x53),
    B8(0x54), B8(0x55), B8(0x56), B8(0x57),
    B8(0x58), B8(0x59), B8(0x5A), B8(0x5B),
    B8(0x5C), B8(0x5D), B8(0x5E), B8(0x5F),
    B8(0x60), B8(0x61), B8(0x62), B8(0x63),
    B8(0x64), B8(0x65), B8(0x66), B8(0x67),
    B8(0x68), B8(0x69), B8(0x6A), B8(0x6B),
    B8(0x6C), B8(0x6D), B8(0x6E), B8(0x6F),
    B8(0x70), B8(0x71), B8(0x72), B8(0x73),
    B8(0x74), B8(0x75), B8(0x76), B8(0x77),
    B8(0x78), B8(0x79), B8(0x7A), B8(0x7B),
    B8(0x7C), B8(0x7D), B8(0x7E), B8(0x7F),
    B8(0x80), B8(0x81), B8(0x82), B8(0x83),
    B8(0x84), B8(0x85), B8(0x86), B8(0x87),
    B8(0x88), B8(0x89), B8(0x8A), B8(0x8B),
    B8(0x8C), B8(0x8D), B8(0x8E), B8(0x8F),
    B8(0x90), B8(0x91), B8(0x92), B8(0x93),
    B8(0x94), B8(0x95), B8(0x96), B8(0x97),
    B8(0x98), B8(0x99), B8(0x9A), B8(0x9B),
    B8(0x9C), B8(0x9D), B8(0x9E), B8(0x9F),
    B8(0xA0), B8(0xA1), B8(0xA2), B8(0xA3),
    B8(0xA4), B8(0xA5), B8(0xA6), B8(0xA7),
    B8(0xA8), B8(0xA9), B8(0xAA), B8(0xAB),
    B8(0xAC), B8(0xAD), B8(0xAE), B8(0xAF),
    B8(0xB0), B8(0xB1), B8(0xB2), B8(0xB3),
    B8(0xB4), B8(0xB5), B8(0xB6), B8(0xB7),
    B8(0xB8), B8(0xB9), B8(0xBA), B8(0xBB),
    B8(0xBC), B8(0xBD), B8(0xBE), B8(0xBF),
    B8(0xC0), B8(0xC1), B8(0xC2), B8(0xC3),
    B8(0xC4), B8(0xC5), B8(0xC6), B8(0xC7),
    B8(0xC8), B8(0xC9), B8(0xCA), B8(0xCB),
    B8(0xCC), B8(0xCD), B8(0xCE), B8(0xCF),
    B8(0xD0), B8(0xD1), B8(0xD2), B8(0xD3),
    B8(0xD4), B8(0xD5), B8(0xD6), B8(0xD7),
    B8(0xD8), B8(0xD9), B8(0xDA), B8(0xDB),
    B8(0xDC), B8(0xDD), B8(0xDE), B8(0xDF),
    B8(0xE0), B8(0xE1), B8(0xE2), B8(0xE3),
    B8(0xE4), B8(0xE5), B8(0xE6), B8(0xE7),
    B8(0xE8), B8(0xE9), B8(0xEA), B8(0xEB),
    B8(0xEC), B8(0xED), B8(0xEE), B8(0xEF),
    B8(0xF0), B8(0xF1), B8(0xF2), B8(0xF3),
    B8(0xF4), B8(0xF5), B8(0xF6), B8(0xF7),
    B8(0xF8), B8(0xF9), B8(0xFA), B8(0xFB),
    B8(0xFC), B8(0xFD), B8(0xFE), B8(0xFF),
};

#undef B4
#undef B8

// ============================================================
//  ARM Instruction Dispatch Table
//
//  The table is indexed by bits [27:20] and [7:4] of the
//  instruction word, giving a 12-bit index (4096 entries).
//
//  Index = ((instr >> 16) & 0xFF0) | ((instr >> 4) & 0xF)
//       = (bits[27:20] << 4) | bits[7:4]
//
//  This matches NooDS's interpreter.cpp indexing:
//    armInstrs[((opcode & 0x0FF00000) >> 16) | ((opcode & 0xF0) >> 4)]
//
//  Each entry is a pointer to an Interpreter member function:
//    void (Interpreter::*)(uint32_t)
// ============================================================

// Convenience aliases for interpreter member function pointers
typedef void (Interpreter::*ArmFn)(uint32_t);
typedef void (Interpreter::*ThumbFn)(uint16_t);

// ---- ARM Instruction Handler Forward Declarations ----
// These are defined in interpreter_alu.cpp, interpreter_transfer.cpp, etc.
// We just need their names here to populate the table.

// The exact method names must match those declared in interpreter.h
// Cross-referenced against NooDS source interpreter.h

// Data Processing
// DP ops: AND EOR SUB RSB ADD ADC SBC RSC TST TEQ CMP CMN ORR MOV BIC MVN
// Each exists in: non-S, S, imm, reg-shift, reg variants
// NooDS uses a flat naming scheme based on the encoded opcode bits

// ============================================================
//  ARM opcode table index breakdown:
//
//  Index bits [11:4] = instruction bits [27:20]
//  Index bits [3:0]  = instruction bits [7:4]
//
//  bits[27:25] = group:
//   000 = Data proc / MUL / misc
//   001 = Data proc immediate / MSR immediate
//   010 = LDR/STR immediate
//   011 = LDR/STR register / undefined
//   100 = LDM/STM
//   101 = B/BL
//   110 = CDP/LDC/STC
//   111 = SWI / CDP / MCR / MRC
// ============================================================

// Macro to replicate an entry across 16 low-nibble slots
// (when bits[7:4] don't affect the opcode selection)
#define R16(fn) \
    fn, fn, fn, fn, fn, fn, fn, fn, \
    fn, fn, fn, fn, fn, fn, fn, fn

// Macro for entries where bit4=0 -> fn, bit4=1 -> fn2
// (alternating for even/odd low nibble)
#define A2(fn0, fn1) \
    fn0, fn1, fn0, fn1, fn0, fn1, fn0, fn1, \
    fn0, fn1, fn0, fn1, fn0, fn1, fn0, fn1

// ============================================================
//  Full 4096-entry ARM table
//  Organized as 256 groups of 16 (one group per bits[27:20])
// ============================================================

const ArmFn Interpreter::armInstrs[4096] = {

// ============================================================
// Group 0x00: AND  Rd,Rn,<shift>  (bits[27:20] = 0000 0000)
// 0x000: AND  rd,rn,rm  (no shift, bit4=0)
// 0x001: AND  rd,rn,rm  (shift by reg, bit4=1, bit7=0 -> AND)
// ============================================================

// --- 0x00x: AND reg ---
// bits[27:20]=0x00, bits[7:4]=0..15
// bit4=0: AND rReg (LSL imm)
// bit4=1, bit7=0: AND rReg (shift by reg)
// bit4=1, bit7=1: MUL / misc
    &Interpreter::andLli,   // 0x000  AND Rd,Rn,Rm LSL #imm
    &Interpreter::andLlr,   // 0x001  AND Rd,Rn,Rm LSL Rs
    &Interpreter::andLri,   // 0x002  AND Rd,Rn,Rm LSR #imm
    &Interpreter::andLrr,   // 0x003  AND Rd,Rn,Rm LSR Rs
    &Interpreter::andAri,   // 0x004  AND Rd,Rn,Rm ASR #imm
    &Interpreter::andArr,   // 0x005  AND Rd,Rn,Rm ASR Rs
    &Interpreter::andRri,   // 0x006  AND Rd,Rn,Rm ROR #imm
    &Interpreter::andRrr,   // 0x007  AND Rd,Rn,Rm ROR Rs
    &Interpreter::andLli,   // 0x008  AND (bit3=1 -> same for non-mul)
    &Interpreter::mul,      // 0x009  MUL Rd,Rm,Rs (bit[7:4]=1001)
    &Interpreter::andLri,   // 0x00A
    &Interpreter::undefined, // 0x00B
    &Interpreter::andAri,   // 0x00C
    &Interpreter::undefined, // 0x00D
    &Interpreter::andRri,   // 0x00E
    &Interpreter::undefined, // 0x00F

// --- 0x01x: ANDS reg ---
    &Interpreter::andsLli,  // 0x010
    &Interpreter::andsLlr,  // 0x011
    &Interpreter::andsLri,  // 0x012
    &Interpreter::andsLrr,  // 0x013
    &Interpreter::andsAri,  // 0x014
    &Interpreter::andsArr,  // 0x015
    &Interpreter::andsRri,  // 0x016
    &Interpreter::andsRrr,  // 0x017
    &Interpreter::andsLli,  // 0x018
    &Interpreter::muls,     // 0x019  MULS
    &Interpreter::andsLri,  // 0x01A
    &Interpreter::undefined, // 0x01B
    &Interpreter::andsAri,  // 0x01C
    &Interpreter::undefined, // 0x01D
    &Interpreter::andsRri,  // 0x01E
    &Interpreter::undefined, // 0x01F

// --- 0x02x: EOR reg ---
    &Interpreter::eorLli,   // 0x020
    &Interpreter::eorLlr,   // 0x021
    &Interpreter::eorLri,   // 0x022
    &Interpreter::eorLrr,   // 0x023
    &Interpreter::eorAri,   // 0x024
    &Interpreter::eorArr,   // 0x025
    &Interpreter::eorRri,   // 0x026
    &Interpreter::eorRrr,   // 0x027
    &Interpreter::eorLli,   // 0x028
    &Interpreter::mla,      // 0x029  MLA
    &Interpreter::eorLri,   // 0x02A
    &Interpreter::undefined, // 0x02B
    &Interpreter::eorAri,   // 0x02C
    &Interpreter::undefined, // 0x02D
    &Interpreter::eorRri,   // 0x02E
    &Interpreter::undefined, // 0x02F

// --- 0x03x: EORS reg ---
    &Interpreter::eorsLli,  // 0x030
    &Interpreter::eorsLlr,  // 0x031
    &Interpreter::eorsLri,  // 0x032
    &Interpreter::eorsLrr,  // 0x033
    &Interpreter::eorsAri,  // 0x034
    &Interpreter::eorsArr,  // 0x035
    &Interpreter::eorsRri,  // 0x036
    &Interpreter::eorsRrr,  // 0x037
    &Interpreter::eorsLli,  // 0x038
    &Interpreter::mlas,     // 0x039  MLAS
    &Interpreter::eorsLri,  // 0x03A
    &Interpreter::undefined, // 0x03B
    &Interpreter::eorsAri,  // 0x03C
    &Interpreter::undefined, // 0x03D
    &Interpreter::eorsRri,  // 0x03E
    &Interpreter::undefined, // 0x03F

// --- 0x04x: SUB reg ---
    &Interpreter::subLli,   // 0x040
    &Interpreter::subLlr,   // 0x041
    &Interpreter::subLri,   // 0x042
    &Interpreter::subLrr,   // 0x043
    &Interpreter::subAri,   // 0x044
    &Interpreter::subArr,   // 0x045
    &Interpreter::subRri,   // 0x046
    &Interpreter::subRrr,   // 0x047
    &Interpreter::subLli,   // 0x048
    &Interpreter::undefined, // 0x049
    &Interpreter::subLri,   // 0x04A
    &Interpreter::strh,     // 0x04B  STRH (bit[7:4]=1011)
    &Interpreter::subAri,   // 0x04C
    &Interpreter::undefined, // 0x04D
    &Interpreter::subRri,   // 0x04E
    &Interpreter::undefined, // 0x04F

// --- 0x05x: SUBS reg ---
    &Interpreter::subsLli,  // 0x050
    &Interpreter::subsLlr,  // 0x051
    &Interpreter::subsLri,  // 0x052
    &Interpreter::subsLrr,  // 0x053
    &Interpreter::subsAri,  // 0x054
    &Interpreter::subsArr,  // 0x055
    &Interpreter::subsRri,  // 0x056
    &Interpreter::subsRrr,  // 0x057
    &Interpreter::subsLli,  // 0x058
    &Interpreter::undefined, // 0x059
    &Interpreter::subsLri,  // 0x05A
    &Interpreter::ldrh,     // 0x05B  LDRH
    &Interpreter::subsAri,  // 0x05C
    &Interpreter::ldrsb,    // 0x05D  LDRSB
    &Interpreter::subsRri,  // 0x05E
    &Interpreter::ldrsh,    // 0x05F  LDRSH

// --- 0x06x: RSB reg ---
    &Interpreter::rsbLli,   // 0x060
    &Interpreter::rsbLlr,   // 0x061
    &Interpreter::rsbLri,   // 0x062
    &Interpreter::rsbLrr,   // 0x063
    &Interpreter::rsbAri,   // 0x064
    &Interpreter::rsbArr,   // 0x065
    &Interpreter::rsbRri,   // 0x066
    &Interpreter::rsbRrr,   // 0x067
    &Interpreter::rsbLli,   // 0x068
    &Interpreter::undefined, // 0x069
    &Interpreter::rsbLri,   // 0x06A
    &Interpreter::undefined, // 0x06B
    &Interpreter::rsbAri,   // 0x06C
    &Interpreter::undefined, // 0x06D
    &Interpreter::rsbRri,   // 0x06E
    &Interpreter::undefined, // 0x06F

// --- 0x07x: RSBS reg ---
    &Interpreter::rsbsLli,  // 0x070
    &Interpreter::rsbsLlr,  // 0x071
    &Interpreter::rsbsLri,  // 0x072
    &Interpreter::rsbsLrr,  // 0x073
    &Interpreter::rsbsAri,  // 0x074
    &Interpreter::rsbsArr,  // 0x075
    &Interpreter::rsbsRri,  // 0x076
    &Interpreter::rsbsRrr,  // 0x077
    &Interpreter::rsbsLli,  // 0x078
    &Interpreter::undefined, // 0x079
    &Interpreter::rsbsLri,  // 0x07A
    &Interpreter::undefined, // 0x07B
    &Interpreter::rsbsAri,  // 0x07C
    &Interpreter::undefined, // 0x07D
    &Interpreter::rsbsRri,  // 0x07E
    &Interpreter::undefined, // 0x07F

// --- 0x08x: ADD reg ---
    &Interpreter::addLli,   // 0x080
    &Interpreter::addLlr,   // 0x081
    &Interpreter::addLri,   // 0x082
    &Interpreter::addLrr,   // 0x083
    &Interpreter::addAri,   // 0x084
    &Interpreter::addArr,   // 0x085
    &Interpreter::addRri,   // 0x086
    &Interpreter::addRrr,   // 0x087
    &Interpreter::addLli,   // 0x088
    &Interpreter::umull,    // 0x089  UMULL
    &Interpreter::addLri,   // 0x08A
    &Interpreter::strhPr,   // 0x08B  STRH post-reg
    &Interpreter::addAri,   // 0x08C
    &Interpreter::undefined, // 0x08D
    &Interpreter::addRri,   // 0x08E
    &Interpreter::undefined, // 0x08F

// --- 0x09x: ADDS reg ---
    &Interpreter::addsLli,  // 0x090
    &Interpreter::addsLlr,  // 0x091
    &Interpreter::addsLri,  // 0x092
    &Interpreter::addsLrr,  // 0x093
    &Interpreter::addsAri,  // 0x094
    &Interpreter::addsArr,  // 0x095
    &Interpreter::addsRri,  // 0x096
    &Interpreter::addsRrr,  // 0x097
    &Interpreter::addsLli,  // 0x098
    &Interpreter::umulls,   // 0x099  UMULLS
    &Interpreter::addsLri,  // 0x09A
    &Interpreter::ldrhPr,   // 0x09B  LDRH post-reg
    &Interpreter::addsAri,  // 0x09C
    &Interpreter::ldrsbPr,  // 0x09D  LDRSB post-reg
    &Interpreter::addsRri,  // 0x09E
    &Interpreter::ldrshPr,  // 0x09F  LDRSH post-reg

// --- 0x0Ax: ADC reg ---
    &Interpreter::adcLli,   // 0x0A0
    &Interpreter::adcLlr,   // 0x0A1
    &Interpreter::adcLri,   // 0x0A2
    &Interpreter::adcLrr,   // 0x0A3
    &Interpreter::adcAri,   // 0x0A4
    &Interpreter::adcArr,   // 0x0A5
    &Interpreter::adcRri,   // 0x0A6
    &Interpreter::adcRrr,   // 0x0A7
    &Interpreter::adcLli,   // 0x0A8
    &Interpreter::umlal,    // 0x0A9  UMLAL
    &Interpreter::adcLri,   // 0x0AA
    &Interpreter::undefined, // 0x0AB
    &Interpreter::adcAri,   // 0x0AC
    &Interpreter::undefined, // 0x0AD
    &Interpreter::adcRri,   // 0x0AE
    &Interpreter::undefined, // 0x0AF

// --- 0x0Bx: ADCS reg ---
    &Interpreter::adcsLli,  // 0x0B0
    &Interpreter::adcsLlr,  // 0x0B1
    &Interpreter::adcsLri,  // 0x0B2
    &Interpreter::adcsLrr,  // 0x0B3
    &Interpreter::adcsAri,  // 0x0B4
    &Interpreter::adcsArr,  // 0x0B5
    &Interpreter::adcsRri,  // 0x0B6
    &Interpreter::adcsRrr,  // 0x0B7
    &Interpreter::adcsLli,  // 0x0B8
    &Interpreter::umlals,   // 0x0B9  UMLALS
    &Interpreter::adcsLri,  // 0x0BA
    &Interpreter::undefined, // 0x0BB
    &Interpreter::adcsAri,  // 0x0BC
    &Interpreter::undefined, // 0x0BD
    &Interpreter::adcsRri,  // 0x0BE
    &Interpreter::undefined, // 0x0BF

// --- 0x0Cx: SBC reg ---
    &Interpreter::sbcLli,   // 0x0C0
    &Interpreter::sbcLlr,   // 0x0C1
    &Interpreter::sbcLri,   // 0x0C2
    &Interpreter::sbcLrr,   // 0x0C3
    &Interpreter::sbcAri,   // 0x0C4
    &Interpreter::sbcArr,   // 0x0C5
    &Interpreter::sbcRri,   // 0x0C6
    &Interpreter::sbcRrr,   // 0x0C7
    &Interpreter::sbcLli,   // 0x0C8
    &Interpreter::smull,    // 0x0C9  SMULL
    &Interpreter::sbcLri,   // 0x0CA
    &Interpreter::undefined, // 0x0CB
    &Interpreter::sbcAri,   // 0x0CC
    &Interpreter::undefined, // 0x0CD
    &Interpreter::sbcRri,   // 0x0CE
    &Interpreter::undefined, // 0x0CF

// --- 0x0Dx: SBCS reg ---
    &Interpreter::sbcsLli,  // 0x0D0
    &Interpreter::sbcsLlr,  // 0x0D1
    &Interpreter::sbcsLri,  // 0x0D2
    &Interpreter::sbcsLrr,  // 0x0D3
    &Interpreter::sbcsAri,  // 0x0D4
    &Interpreter::sbcsArr,  // 0x0D5
    &Interpreter::sbcsRri,  // 0x0D6
    &Interpreter::sbcsRrr,  // 0x0D7
    &Interpreter::sbcsLli,  // 0x0D8
    &Interpreter::smulls,   // 0x0D9  SMULLS
    &Interpreter::sbcsLri,  // 0x0DA
    &Interpreter::undefined, // 0x0DB
    &Interpreter::sbcsAri,  // 0x0DC
    &Interpreter::undefined, // 0x0DD
    &Interpreter::sbcsRri,  // 0x0DE
    &Interpreter::undefined, // 0x0DF

// --- 0x0Ex: RSC reg ---
    &Interpreter::rscLli,   // 0x0E0
    &Interpreter::rscLlr,   // 0x0E1
    &Interpreter::rscLri,   // 0x0E2
    &Interpreter::rscLrr,   // 0x0E3
    &Interpreter::rscAri,   // 0x0E4
    &Interpreter::rscArr,   // 0x0E5
    &Interpreter::rscRri,   // 0x0E6
    &Interpreter::rscRrr,   // 0x0E7
    &Interpreter::rscLli,   // 0x0E8
    &Interpreter::smlal,    // 0x0E9  SMLAL
    &Interpreter::rscLri,   // 0x0EA
    &Interpreter::undefined, // 0x0EB
    &Interpreter::rscAri,   // 0x0EC
    &Interpreter::undefined, // 0x0ED
    &Interpreter::rscRri,   // 0x0EE
    &Interpreter::undefined, // 0x0EF

// --- 0x0Fx: RSCS reg ---
    &Interpreter::rscsLli,  // 0x0F0
    &Interpreter::rscsLlr,  // 0x0F1
    &Interpreter::rscsLri,  // 0x0F2
    &Interpreter::rscsLrr,  // 0x0F3
    &Interpreter::rscsAri,  // 0x0F4
    &Interpreter::rscsArr,  // 0x0F5
    &Interpreter::rscsRri,  // 0x0F6
    &Interpreter::rscsRrr,  // 0x0F7
    &Interpreter::rscsLli,  // 0x0F8
    &Interpreter::smlals,   // 0x0F9  SMLALS
    &Interpreter::rscsLri,  // 0x0FA
    &Interpreter::undefined, // 0x0FB
    &Interpreter::rscsAri,  // 0x0FC
    &Interpreter::undefined, // 0x0FD
    &Interpreter::rscsRri,  // 0x0FE
    &Interpreter::undefined, // 0x0FF

// ============================================================
// Group 0x10x: MRS CPSR / TST / MSR / SWP
// bits[27:20] = 0001 0000 = 0x10
// ============================================================

// --- 0x10x: MRS / TST / MSR / misc ---
    &Interpreter::mrsCpsr,  // 0x100  MRS Rd,CPSR
    &Interpreter::undefined, // 0x101
    &Interpreter::undefined, // 0x102
    &Interpreter::undefined, // 0x103
    &Interpreter::undefined, // 0x104
    &Interpreter::undefined, // 0x105
    &Interpreter::undefined, // 0x106
    &Interpreter::undefined, // 0x107
    &Interpreter::undefined, // 0x108
    &Interpreter::undefined, // 0x109
    &Interpreter::undefined, // 0x10A
    &Interpreter::undefined, // 0x10B  (STRH with specific encoding)
    &Interpreter::undefined, // 0x10C
    &Interpreter::undefined, // 0x10D
    &Interpreter::undefined, // 0x10E
    &Interpreter::undefined, // 0x10F

// --- 0x11x: TSTS / TST reg ---
    &Interpreter::tstLli,   // 0x110
    &Interpreter::tstLlr,   // 0x111
    &Interpreter::tstLri,   // 0x112
    &Interpreter::tstLrr,   // 0x113
    &Interpreter::tstAri,   // 0x114
    &Interpreter::tstArr,   // 0x115
    &Interpreter::tstRri,   // 0x116
    &Interpreter::tstRrr,   // 0x117
    &Interpreter::tstLli,   // 0x118
    &Interpreter::undefined, // 0x119
    &Interpreter::tstLri,   // 0x11A
    &Interpreter::ldrhOf,   // 0x11B  LDRH offset
    &Interpreter::tstAri,   // 0x11C
    &Interpreter::ldrsbOf,  // 0x11D  LDRSB offset
    &Interpreter::tstRri,   // 0x11E
    &Interpreter::ldrshOf,  // 0x11F  LDRSH offset

// --- 0x12x: MSR CPSR / BX ---
    &Interpreter::msrRc,    // 0x120  MSR CPSR,Rm
    &Interpreter::bx,       // 0x121  BX Rn
    &Interpreter::undefined, // 0x122
    &Interpreter::blxReg,   // 0x123  BLX Rn
    &Interpreter::undefined, // 0x124
    &Interpreter::undefined, // 0x125
    &Interpreter::undefined, // 0x126
    &Interpreter::undefined, // 0x127
    &Interpreter::undefined, // 0x128
    &Interpreter::undefined, // 0x129
    &Interpreter::undefined, // 0x12A
    &Interpreter::strhPof,  // 0x12B  STRH pre-offset
    &Interpreter::undefined, // 0x12C
    &Interpreter::undefined, // 0x12D
    &Interpreter::undefined, // 0x12E
    &Interpreter::undefined, // 0x12F

// --- 0x13x: TEQ reg ---
    &Interpreter::teqLli,   // 0x130
    &Interpreter::teqLlr,   // 0x131
    &Interpreter::teqLri,   // 0x132
    &Interpreter::teqLrr,   // 0x133
    &Interpreter::teqAri,   // 0x134
    &Interpreter::teqArr,   // 0x135
    &Interpreter::teqRri,   // 0x136
    &Interpreter::teqRrr,   // 0x137
    &Interpreter::teqLli,   // 0x138
    &Interpreter::undefined, // 0x139
    &Interpreter::teqLri,   // 0x13A
    &Interpreter::ldrhPof,  // 0x13B  LDRH pre-offset
    &Interpreter::teqAri,   // 0x13C
    &Interpreter::ldrsbPof, // 0x13D  LDRSB pre-offset
    &Interpreter::teqRri,   // 0x13E
    &Interpreter::ldrshPof, // 0x13F  LDRSH pre-offset

// --- 0x14x: MRS SPSR / SWP ---
    &Interpreter::mrsSpsr,  // 0x140  MRS Rd,SPSR
    &Interpreter::undefined, // 0x141
    &Interpreter::undefined, // 0x142
    &Interpreter::undefined, // 0x143
    &Interpreter::undefined, // 0x144
    &Interpreter::undefined, // 0x145
    &Interpreter::undefined, // 0x146
    &Interpreter::undefined, // 0x147
    &Interpreter::undefined, // 0x148
    &Interpreter::swp,      // 0x149  SWP
    &Interpreter::undefined, // 0x14A
    &Interpreter::undefined, // 0x14B
    &Interpreter::undefined, // 0x14C
    &Interpreter::undefined, // 0x14D
    &Interpreter::undefined, // 0x14E
    &Interpreter::undefined, // 0x14F

// --- 0x15x: CMP reg ---
    &Interpreter::cmpLli,   // 0x150
    &Interpreter::cmpLlr,   // 0x151
    &Interpreter::cmpLri,   // 0x152
    &Interpreter::cmpLrr,   // 0x153
    &Interpreter::cmpAri,   // 0x154
    &Interpreter::cmpArr,   // 0x155
    &Interpreter::cmpRri,   // 0x156
    &Interpreter::cmpRrr,   // 0x157
    &Interpreter::cmpLli,   // 0x158
    &Interpreter::undefined, // 0x159
    &Interpreter::cmpLri,   // 0x15A
    &Interpreter::ldrhPrW,  // 0x15B  LDRH pre-reg writeback
    &Interpreter::cmpAri,   // 0x15C
    &Interpreter::ldrsbPrW, // 0x15D  LDRSB pre-reg writeback
    &Interpreter::cmpRri,   // 0x15E
    &Interpreter::ldrshPrW, // 0x15F  LDRSH pre-reg writeback

// --- 0x16x: MSR SPSR / CLZ ---
    &Interpreter::msrRs,    // 0x160  MSR SPSR,Rm
    &Interpreter::undefined, // 0x161
    &Interpreter::undefined, // 0x162
    &Interpreter::undefined, // 0x163
    &Interpreter::undefined, // 0x164
    &Interpreter::undefined, // 0x165
    &Interpreter::undefined, // 0x166
    &Interpreter::undefined, // 0x167
    &Interpreter::undefined, // 0x168
    &Interpreter::undefined, // 0x169
    &Interpreter::undefined, // 0x16A
    &Interpreter::strhPoW,  // 0x16B  STRH post-offset writeback
    &Interpreter::undefined, // 0x16C
    &Interpreter::undefined, // 0x16D
    &Interpreter::undefined, // 0x16E
    &Interpreter::undefined, // 0x16F

// --- 0x17x: CMN reg ---
    &Interpreter::cmnLli,   // 0x170
    &Interpreter::cmnLlr,   // 0x171
    &Interpreter::cmnLri,   // 0x172
    &Interpreter::cmnLrr,   // 0x173
    &Interpreter::cmnAri,   // 0x174
    &Interpreter::cmnArr,   // 0x175
    &Interpreter::cmnRri,   // 0x176
    &Interpreter::cmnRrr,   // 0x177
    &Interpreter::cmnLli,   // 0x178
    &Interpreter::undefined, // 0x179
    &Interpreter::cmnLri,   // 0x17A
    &Interpreter::ldrhPoW,  // 0x17B  LDRH post-offset writeback
    &Interpreter::cmnAri,   // 0x17C
    &Interpreter::ldrsbPoW, // 0x17D  LDRSB post-offset writeback
    &Interpreter::cmnRri,   // 0x17E
    &Interpreter::ldrshPoW, // 0x17F  LDRSH post-offset writeback

// --- 0x18x: ORR reg ---
    &Interpreter::orrLli,   // 0x180
    &Interpreter::orrLlr,   // 0x181
    &Interpreter::orrLri,   // 0x182
    &Interpreter::orrLrr,   // 0x183
    &Interpreter::orrAri,   // 0x184
    &Interpreter::orrArr,   // 0x185
    &Interpreter::orrRri,   // 0x186
    &Interpreter::orrRrr,   // 0x187
    &Interpreter::orrLli,   // 0x188
    &Interpreter::undefined, // 0x189
    &Interpreter::orrLri,   // 0x18A
    &Interpreter::strhPrW,  // 0x18B  STRH pre-reg writeback
    &Interpreter::orrAri,   // 0x18C
    &Interpreter::undefined, // 0x18D
    &Interpreter::orrRri,   // 0x18E
    &Interpreter::undefined, // 0x18F

// --- 0x19x: ORRS reg ---
    &Interpreter::orrsLli,  // 0x190
    &Interpreter::orrsLlr,  // 0x191
    &Interpreter::orrsLri,  // 0x192
    &Interpreter::orrsLrr,  // 0x193
    &Interpreter::orrsAri,  // 0x194
    &Interpreter::orrsArr,  // 0x195
    &Interpreter::orrsRri,  // 0x196
    &Interpreter::orrsRrr,  // 0x197
    &Interpreter::orrsLli,  // 0x198
    &Interpreter::undefined, // 0x199
    &Interpreter::orrsLri,  // 0x19A
    &Interpreter::ldrhPrW,  // 0x19B  (reuse pre-reg writeback)
    &Interpreter::orrsAri,  // 0x19C
    &Interpreter::ldrsbPrW, // 0x19D
    &Interpreter::orrsRri,  // 0x19E
    &Interpreter::ldrshPrW, // 0x19F

// --- 0x1Ax: MOV reg ---
    &Interpreter::movLli,   // 0x1A0
    &Interpreter::movLlr,   // 0x1A1
    &Interpreter::movLri,   // 0x1A2
    &Interpreter::movLrr,   // 0x1A3
    &Interpreter::movAri,   // 0x1A4
    &Interpreter::movArr,   // 0x1A5
    &Interpreter::movRri,   // 0x1A6
    &Interpreter::movRrr,   // 0x1A7
    &Interpreter::movLli,   // 0x1A8
    &Interpreter::undefined, // 0x1A9
    &Interpreter::movLri,   // 0x1AA
    &Interpreter::undefined, // 0x1AB
    &Interpreter::movAri,   // 0x1AC
    &Interpreter::undefined, // 0x1AD
    &Interpreter::movRri,   // 0x1AE
    &Interpreter::undefined, // 0x1AF

// --- 0x1Bx: MOVS reg ---
    &Interpreter::movsLli,  // 0x1B0
    &Interpreter::movsLlr,  // 0x1B1
    &Interpreter::movsLri,  // 0x1B2
    &Interpreter::movsLrr,  // 0x1B3
    &Interpreter::movsAri,  // 0x1B4
    &Interpreter::movsArr,  // 0x1B5
    &Interpreter::movsRri,  // 0x1B6
    &Interpreter::movsRrr,  // 0x1B7
    &Interpreter::movsLli,  // 0x1B8
    &Interpreter::undefined, // 0x1B9
    &Interpreter::movsLri,  // 0x1BA
    &Interpreter::undefined, // 0x1BB
    &Interpreter::movsAri,  // 0x1BC
    &Interpreter::undefined, // 0x1BD
    &Interpreter::movsRri,  // 0x1BE
    &Interpreter::undefined, // 0x1BF

// --- 0x1Cx: BIC reg ---
    &Interpreter::bicLli,   // 0x1C0
    &Interpreter::bicLlr,   // 0x1C1
    &Interpreter::bicLri,   // 0x1C2
    &Interpreter::bicLrr,   // 0x1C3
    &Interpreter::bicAri,   // 0x1C4
    &Interpreter::bicArr,   // 0x1C5
    &Interpreter::bicRri,   // 0x1C6
    &Interpreter::bicRrr,   // 0x1C7
    &Interpreter::bicLli,   // 0x1C8
    &Interpreter::undefined, // 0x1C9
    &Interpreter::bicLri,   // 0x1CA
    &Interpreter::strhPoWx, // 0x1CB  STRH post-offset writeback variant
    &Interpreter::bicAri,   // 0x1CC
    &Interpreter::undefined, // 0x1CD
    &Interpreter::bicRri,   // 0x1CE
    &Interpreter::undefined, // 0x1CF

// --- 0x1Dx: BICS reg ---
    &Interpreter::bicsLli,  // 0x1D0
    &Interpreter::bicsLlr,  // 0x1D1
    &Interpreter::bicsLri,  // 0x1D2
    &Interpreter::bicsLrr,  // 0x1D3
    &Interpreter::bicsAri,  // 0x1D4
    &Interpreter::bicsArr,  // 0x1D5
    &Interpreter::bicsRri,  // 0x1D6
    &Interpreter::bicsRrr,  // 0x1D7
    &Interpreter::bicsLli,  // 0x1D8
    &Interpreter::undefined, // 0x1D9
    &Interpreter::bicsLri,  // 0x1DA
    &Interpreter::ldrhPoWx, // 0x1DB
    &Interpreter::bicsAri,  // 0x1DC
    &Interpreter::ldrsbPoWx,// 0x1DD
    &Interpreter::bicsRri,  // 0x1DE
    &Interpreter::ldrshPoWx,// 0x1DF

// --- 0x1Ex: MVN reg ---
    &Interpreter::mvnLli,   // 0x1E0
    &Interpreter::mvnLlr,   // 0x1E1
    &Interpreter::mvnLri,   // 0x1E2
    &Interpreter::mvnLrr,   // 0x1E3
    &Interpreter::mvnAri,   // 0x1E4
    &Interpreter::mvnArr,   // 0x1E5
    &Interpreter::mvnRri,   // 0x1E6
    &Interpreter::mvnRrr,   // 0x1E7
    &Interpreter::mvnLli,   // 0x1E8
    &Interpreter::undefined, // 0x1E9
    &Interpreter::mvnLri,   // 0x1EA
    &Interpreter::undefined, // 0x1EB
    &Interpreter::mvnAri,   // 0x1EC
    &Interpreter::undefined, // 0x1ED
    &Interpreter::mvnRri,   // 0x1EE
    &Interpreter::undefined, // 0x1EF

// --- 0x1Fx: MVNS reg ---
    &Interpreter::mvnsLli,  // 0x1F0
    &Interpreter::mvnsLlr,  // 0x1F1
    &Interpreter::mvnsLri,  // 0x1F2
    &Interpreter::mvnsLrr,  // 0x1F3
    &Interpreter::mvnsAri,  // 0x1F4
    &Interpreter::mvnsArr,  // 0x1F5
    &Interpreter::mvnsRri,  // 0x1F6
    &Interpreter::mvnsRrr,  // 0x1F7
    &Interpreter::mvnsLli,  // 0x1F8
    &Interpreter::undefined, // 0x1F9
    &Interpreter::mvnsLri,  // 0x1FA
    &Interpreter::undefined, // 0x1FB
    &Interpreter::mvnsAri,  // 0x1FC
    &Interpreter::undefined, // 0x1FD
    &Interpreter::mvnsRri,  // 0x1FE
    &Interpreter::undefined, // 0x1FF

// ============================================================
// Group 0x200-0x3FF: Data Processing Immediate (bit25=1)
// bits[27:25] = 001
// bits[27:20] = 0x20..0x3F -> table indices 0x200..0x3FF
// All 16 low-nibble slots are the same (immediate, no shift)
// ============================================================

// 0x200-0x20F: AND imm
    R16(&Interpreter::andImm),
// 0x210-0x21F: ANDS imm
    R16(&Interpreter::andsImm),
// 0x220-0x22F: EOR imm
    R16(&Interpreter::eorImm),
// 0x230-0x23F: EORS imm
    R16(&Interpreter::eorsImm),
// 0x240-0x24F: SUB imm
    R16(&Interpreter::subImm),
// 0x250-0x25F: SUBS imm
    R16(&Interpreter::subsImm),
// 0x260-0x26F: RSB imm
    R16(&Interpreter::rsbImm),
// 0x270-0x27F: RSBS imm
    R16(&Interpreter::rsbsImm),
// 0x280-0x28F: ADD imm
    R16(&Interpreter::addImm),
// 0x290-0x29F: ADDS imm
    R16(&Interpreter::addsImm),
// 0x2A0-0x2AF: ADC imm
    R16(&Interpreter::adcImm),
// 0x2B0-0x2BF: ADCS imm
    R16(&Interpreter::adcsImm),
// 0x2C0-0x2CF: SBC imm
    R16(&Interpreter::sbcImm),
// 0x2D0-0x2DF: SBCS imm
    R16(&Interpreter::sbcsImm),
// 0x2E0-0x2EF: RSC imm
    R16(&Interpreter::rscImm),
// 0x2F0-0x2FF: RSCS imm
    R16(&Interpreter::rscsImm),

// 0x300-0x30F: undefined (TST imm without S is undefined encoding)
    R16(&Interpreter::undefined),
// 0x310-0x31F: TST imm
    R16(&Interpreter::tstImm),
// 0x320-0x32F: MSR CPSR imm
    R16(&Interpreter::msrIc),
// 0x330-0x33F: TEQ imm
    R16(&Interpreter::teqImm),
// 0x340-0x34F: undefined
    R16(&Interpreter::undefined),
// 0x350-0x35F: CMP imm
    R16(&Interpreter::cmpImm),
// 0x360-0x36F: MSR SPSR imm
    R16(&Interpreter::msrIs),
// 0x370-0x37F: CMN imm
    R16(&Interpreter::cmnImm),
// 0x380-0x38F: ORR imm
    R16(&Interpreter::orrImm),
// 0x390-0x39F: ORRS imm
    R16(&Interpreter::orrsImm),
// 0x3A0-0x3AF: MOV imm
    R16(&Interpreter::movImm),
// 0x3B0-0x3BF: MOVS imm
    R16(&Interpreter::movsImm),
// 0x3C0-0x3CF: BIC imm
    R16(&Interpreter::bicImm),
// 0x3D0-0x3DF: BICS imm
    R16(&Interpreter::bicsImm),
// 0x3E0-0x3EF: MVN imm
    R16(&Interpreter::mvnImm),
// 0x3F0-0x3FF: MVNS imm
    R16(&Interpreter::mvnsImm),

// ============================================================
// Group 0x400-0x5FF: Single Data Transfer Immediate (bit25=0,bit26=1)
// LDR/STR with immediate offset
// bits[27:20]: bit24=P, bit23=U, bit22=B, bit21=W, bit20=L
// ============================================================

// 0x400-0x40F: STR  post-dec imm  (P=0,U=0,B=0,W=0,L=0)
    R16(&Interpreter::strPt),
// 0x410-0x41F: LDR  post-dec imm
    R16(&Interpreter::ldrPt),
// 0x420-0x42F: STRT post-dec imm (W=1 with P=0 = user mode)
    R16(&Interpreter::strPtW),
// 0x430-0x43F: LDRT post-dec imm
    R16(&Interpreter::ldrPtW),
// 0x440-0x44F: STRB post-dec imm
    R16(&Interpreter::strbPt),
// 0x450-0x45F: LDRB post-dec imm
    R16(&Interpreter::ldrbPt),
// 0x460-0x46F: STRBT post-dec imm
    R16(&Interpreter::strbPtW),
// 0x470-0x47F: LDRBT post-dec imm
    R16(&Interpreter::ldrbPtW),
// 0x480-0x48F: STR  post-inc imm  (U=1)
    R16(&Interpreter::strPtU),
// 0x490-0x49F: LDR  post-inc imm
    R16(&Interpreter::ldrPtU),
// 0x4A0-0x4AF: STRT post-inc imm
    R16(&Interpreter::strPtUW),
// 0x4B0-0x4BF: LDRT post-inc imm
    R16(&Interpreter::ldrPtUW),
// 0x4C0-0x4CF: STRB post-inc imm
    R16(&Interpreter::strbPtU),
// 0x4D0-0x4DF: LDRB post-inc imm
    R16(&Interpreter::ldrbPtU),
// 0x4E0-0x4EF: STRBT post-inc imm
    R16(&Interpreter::strbPtUW),
// 0x4F0-0x4FF: LDRBT post-inc imm
    R16(&Interpreter::ldrbPtUW),
// 0x500-0x50F: STR  pre-dec imm   (P=1)
    R16(&Interpreter::strOf),
// 0x510-0x51F: LDR  pre-dec imm
    R16(&Interpreter::ldrOf),
// 0x520-0x52F: STR  pre-dec imm writeback
    R16(&Interpreter::strPr),
// 0x530-0x53F: LDR  pre-dec imm writeback
    R16(&Interpreter::ldrPr),
// 0x540-0x54F: STRB pre-dec imm
    R16(&Interpreter::strbOf),
// 0x550-0x55F: LDRB pre-dec imm
    R16(&Interpreter::ldrbOf),
// 0x560-0x56F: STRB pre-dec imm writeback
    R16(&Interpreter::strbPr),
// 0x570-0x57F: LDRB pre-dec imm writeback
    R16(&Interpreter::ldrbPr),
// 0x580-0x58F: STR  pre-inc imm   (U=1,P=1)
    R16(&Interpreter::strOfU),
// 0x590-0x59F: LDR  pre-inc imm
    R16(&Interpreter::ldrOfU),
// 0x5A0-0x5AF: STR  pre-inc imm writeback
    R16(&Interpreter::strPrU),
// 0x5B0-0x5BF: LDR  pre-inc imm writeback
    R16(&Interpreter::ldrPrU),
// 0x5C0-0x5CF: STRB pre-inc imm
    R16(&Interpreter::strbOfU),
// 0x5D0-0x5DF: LDRB pre-inc imm
    R16(&Interpreter::ldrbOfU),
// 0x5E0-0x5EF: STRB pre-inc imm writeback
    R16(&Interpreter::strbPrU),
// 0x5F0-0x5FF: LDRB pre-inc imm writeback
    R16(&Interpreter::ldrbPrU),

// ============================================================
// Group 0x600-0x7FF: Single Data Transfer Register (bit25=1,bit26=1)
// LDR/STR with register offset / shifted register offset
// bit4=1 in this region is UNDEFINED
// ============================================================

// 0x600-0x60F: STR  post-dec reg
    A2(&Interpreter::strPtR, &Interpreter::undefined),
// 0x610-0x61F: LDR  post-dec reg
    A2(&Interpreter::ldrPtR, &Interpreter::undefined),
// 0x620-0x62F: STRT post-dec reg
    A2(&Interpreter::strPtRW, &Interpreter::undefined),
// 0x630-0x63F: LDRT post-dec reg
    A2(&Interpreter::ldrPtRW, &Interpreter::undefined),
// 0x640-0x64F: STRB post-dec reg
    A2(&Interpreter::strbPtR, &Interpreter::undefined),
// 0x650-0x65F: LDRB post-dec reg
    A2(&Interpreter::ldrbPtR, &Interpreter::undefined),
// 0x660-0x66F: STRBT post-dec reg
    A2(&Interpreter::strbPtRW, &Interpreter::undefined),
// 0x670-0x67F: LDRBT post-dec reg
    A2(&Interpreter::ldrbPtRW, &Interpreter::undefined),
// 0x680-0x68F: STR  post-inc reg
    A2(&Interpreter::strPtRU, &Interpreter::undefined),
// 0x690-0x69F: LDR  post-inc reg
    A2(&Interpreter::ldrPtRU, &Interpreter::undefined),
// 0x6A0-0x6AF: STRT post-inc reg
    A2(&Interpreter::strPtRUW, &Interpreter::undefined),
// 0x6B0-0x6BF: LDRT post-inc reg
    A2(&Interpreter::ldrPtRUW, &Interpreter::undefined),
// 0x6C0-0x6CF: STRB post-inc reg
    A2(&Interpreter::strbPtRU, &Interpreter::undefined),
// 0x6D0-0x6DF: LDRB post-inc reg
    A2(&Interpreter::ldrbPtRU, &Interpreter::undefined),
// 0x6E0-0x6EF: STRBT post-inc reg
    A2(&Interpreter::strbPtRUW, &Interpreter::undefined),
// 0x6F0-0x6FF: LDRBT post-inc reg
    A2(&Interpreter::ldrbPtRUW, &Interpreter::undefined),
// 0x700-0x70F: STR  pre-dec reg
    A2(&Interpreter::strOfR, &Interpreter::undefined),
// 0x710-0x71F: LDR  pre-dec reg
    A2(&Interpreter::ldrOfR, &Interpreter::undefined),
// 0x720-0x72F: STR  pre-dec reg writeback
    A2(&Interpreter::strPrR, &Interpreter::undefined),
// 0x730-0x73F: LDR  pre-dec reg writeback
    A2(&Interpreter::ldrPrR, &Interpreter::undefined),
// 0x740-0x74F: STRB pre-dec reg
    A2(&Interpreter::strbOfR, &Interpreter::undefined),
// 0x750-0x75F: LDRB pre-dec reg
    A2(&Interpreter::ldrbOfR, &Interpreter::undefined),
// 0x760-0x76F: STRB pre-dec reg writeback
    A2(&Interpreter::strbPrR, &Interpreter::undefined),
// 0x770-0x77F: LDRB pre-dec reg writeback
    A2(&Interpreter::ldrbPrR, &Interpreter::undefined),
// 0x780-0x78F: STR  pre-inc reg
    A2(&Interpreter::strOfRU, &Interpreter::undefined),
// 0x790-0x79F: LDR  pre-inc reg
    A2(&Interpreter::ldrOfRU, &Interpreter::undefined),
// 0x7A0-0x7AF: STR  pre-inc reg writeback
    A2(&Interpreter::strPrRU, &Interpreter::undefined),
// 0x7B0-0x7BF: LDR  pre-inc reg writeback
    A2(&Interpreter::ldrPrRU, &Interpreter::undefined),
// 0x7C0-0x7CF: STRB pre-inc reg
    A2(&Interpreter::strbOfRU, &Interpreter::undefined),
// 0x7D0-0x7DF: LDRB pre-inc reg
    A2(&Interpreter::ldrbOfRU, &Interpreter::undefined),
// 0x7E0-0x7EF: STRB pre-inc reg writeback
    A2(&Interpreter::strbPrRU, &Interpreter::undefined),
// 0x7F0-0x7FF: LDRB pre-inc reg writeback / CLZ
    &Interpreter::ldrbPrRU, // 0x7F0
    &Interpreter::clz,       // 0x7F1  CLZ Rd,Rm (bits[27:20]=0111 1111, bits[7:4]=0001)
    &Interpreter::ldrbPrRU, // 0x7F2
    &Interpreter::undefined, // 0x7F3
    &Interpreter::ldrbPrRU, // 0x7F4
    &Interpreter::undefined, // 0x7F5
    &Interpreter::ldrbPrRU, // 0x7F6
    &Interpreter::undefined, // 0x7F7
    &Interpreter::ldrbPrRU, // 0x7F8
    &Interpreter::undefined, // 0x7F9
    &Interpreter::ldrbPrRU, // 0x7FA
    &Interpreter::undefined, // 0x7FB
    &Interpreter::ldrbPrRU, // 0x7FC
    &Interpreter::undefined, // 0x7FD
    &Interpreter::ldrbPrRU, // 0x7FE
    &Interpreter::undefined, // 0x7FF

// ============================================================
// Group 0x800-0x9FF: Block Data Transfer (LDM/STM)
// bits[27:25] = 100
// bits[27:20]: bit24=P, bit23=U, bit22=S, bit21=W, bit20=L
// All 16 low-nibble slots identical (register list in bits[15:0])
// ============================================================

// 0x800-0x80F: STMDA  (P=0,U=0,S=0,W=0,L=0) - Store Multiple Dec After
    R16(&Interpreter::stmda),
// 0x810-0x81F: LDMDA  (L=1)
    R16(&Interpreter::ldmda),
// 0x820-0x82F: STMDA writeback
    R16(&Interpreter::stmdaW),
// 0x830-0x83F: LDMDA writeback
    R16(&Interpreter::ldmdaW),
// 0x840-0x84F: STMDA user registers (S=1)
    R16(&Interpreter::stmdaU),
// 0x850-0x85F: LDMDA user registers / restore CPSR
    R16(&Interpreter::ldmdaU),
// 0x860-0x86F: STMDA user writeback
    R16(&Interpreter::stmdaUW),
// 0x870-0x87F: LDMDA user writeback
    R16(&Interpreter::ldmdaUW),
// 0x880-0x88F: STMIA  (U=1) - Store Multiple Inc After
    R16(&Interpreter::stmia),
// 0x890-0x89F: LDMIA
    R16(&Interpreter::ldmia),
// 0x8A0-0x8AF: STMIA writeback
    R16(&Interpreter::stmiaW),
// 0x8B0-0x8BF: LDMIA writeback
    R16(&Interpreter::ldmiaW),
// 0x8C0-0x8CF: STMIA user
    R16(&Interpreter::stmiaU),
// 0x8D0-0x8DF: LDMIA user
    R16(&Interpreter::ldmiaU),
// 0x8E0-0x8EF: STMIA user writeback
    R16(&Interpreter::stmiaUW),
// 0x8F0-0x8FF: LDMIA user writeback
    R16(&Interpreter::ldmiaUW),
// 0x900-0x90F: STMDB  (P=1,U=0) - Store Multiple Dec Before
    R16(&Interpreter::stmdb),
// 0x910-0x91F: LDMDB
    R16(&Interpreter::ldmdb),
// 0x920-0x92F: STMDB writeback
    R16(&Interpreter::stmdbW),
// 0x930-0x93F: LDMDB writeback
    R16(&Interpreter::ldmdbW),
// 0x940-0x94F: STMDB user
    R16(&Interpreter::stmdbU),
// 0x950-0x95F: LDMDB user
    R16(&Interpreter::ldmdbU),
// 0x960-0x96F: STMDB user writeback
    R16(&Interpreter::stmdbUW),
// 0x970-0x97F: LDMDB user writeback
    R16(&Interpreter::ldmdbUW),
// 0x980-0x98F: STMIB  (P=1,U=1) - Store Multiple Inc Before
    R16(&Interpreter::stmib),
// 0x990-0x99F: LDMIB
    R16(&Interpreter::ldmib),
// 0x9A0-0x9AF: STMIB writeback
    R16(&Interpreter::stmibW),
// 0x9B0-0x9BF: LDMIB writeback
    R16(&Interpreter::ldmibW),
// 0x9C0-0x9CF: STMIB user
    R16(&Interpreter::stmibU),
// 0x9D0-0x9DF: LDMIB user
    R16(&Interpreter::ldmibU),
// 0x9E0-0x9EF: STMIB user writeback
    R16(&Interpreter::stmibUW),
// 0x9F0-0x9FF: LDMIB user writeback
    R16(&Interpreter::ldmibUW),

// ============================================================
// Group 0xA00-0xBFF: Branch / Branch-with-Link
// bits[27:25] = 101
// bit24 = L (link)
// All 256 low-byte slots identical per L value
// ============================================================

// 0xA00-0xAFF: B (L=0)
    R16(&Interpreter::b),   // 0xA00
    R16(&Interpreter::b),   // 0xA10
    R16(&Interpreter::b),   // 0xA20
    R16(&Interpreter::b),   // 0xA30
    R16(&Interpreter::b),   // 0xA40
    R16(&Interpreter::b),   // 0xA50
    R16(&Interpreter::b),   // 0xA60
    R16(&Interpreter::b),   // 0xA70
    R16(&Interpreter::b),   // 0xA80
    R16(&Interpreter::b),   // 0xA90
    R16(&Interpreter::b),   // 0xAA0
    R16(&Interpreter::b),   // 0xAB0
    R16(&Interpreter::b),   // 0xAC0
    R16(&Interpreter::b),   // 0xAD0
    R16(&Interpreter::b),   // 0xAE0
    R16(&Interpreter::b),   // 0xAF0

// 0xB00-0xBFF: BL (L=1)
    R16(&Interpreter::bl),  // 0xB00
    R16(&Interpreter::bl),  // 0xB10
    R16(&Interpreter::bl),  // 0xB20
    R16(&Interpreter::bl),  // 0xB30
    R16(&Interpreter::bl),  // 0xB40
    R16(&Interpreter::bl),  // 0xB50
    R16(&Interpreter::bl),  // 0xB60
    R16(&Interpreter::bl),  // 0xB70
    R16(&Interpreter::bl),  // 0xB80
    R16(&Interpreter::bl),  // 0xB90
    R16(&Interpreter::bl),  // 0xBA0
    R16(&Interpreter::bl),  // 0xBB0
    R16(&Interpreter::bl),  // 0xBC0
    R16(&Interpreter::bl),  // 0xBD0
    R16(&Interpreter::bl),  // 0xBE0
    R16(&Interpreter::bl),  // 0xBF0

// ============================================================
// Group 0xC00-0xDFF: Coprocessor LDC/STC
// bits[27:25] = 110
// ============================================================

    R16(&Interpreter::undefined), // 0xC00
    R16(&Interpreter::undefined), // 0xC10
    R16(&Interpreter::undefined), // 0xC20
    R16(&Interpreter::undefined), // 0xC30
    R16(&Interpreter::undefined), // 0xC40
    R16(&Interpreter::undefined), // 0xC50
    R16(&Interpreter::undefined), // 0xC60
    R16(&Interpreter::undefined), // 0xC70
    R16(&Interpreter::undefined), // 0xC80
    R16(&Interpreter::undefined), // 0xC90
    R16(&Interpreter::undefined), // 0xCA0
    R16(&Interpreter::undefined), // 0xCB0
    R16(&Interpreter::undefined), // 0xCC0
    R16(&Interpreter::undefined), // 0xCD0
    R16(&Interpreter::undefined), // 0xCE0
    R16(&Interpreter::undefined), // 0xCF0
    R16(&Interpreter::undefined), // 0xD00
    R16(&Interpreter::undefined), // 0xD10
    R16(&Interpreter::undefined), // 0xD20
    R16(&Interpreter::undefined), // 0xD30
    R16(&Interpreter::undefined), // 0xD40
    R16(&Interpreter::undefined), // 0xD50
    R16(&Interpreter::undefined), // 0xD60
    R16(&Interpreter::undefined), // 0xD70
    R16(&Interpreter::undefined), // 0xD80
    R16(&Interpreter::undefined), // 0xD90
    R16(&Interpreter::undefined), // 0xDA0
    R16(&Interpreter::undefined), // 0xDB0
    R16(&Interpreter::undefined), // 0xDC0
    R16(&Interpreter::undefined), // 0xDD0
    R16(&Interpreter::undefined), // 0xDE0
    R16(&Interpreter::undefined), // 0xDF0

// ============================================================
// Group 0xE00-0xEFF: Coprocessor CDP / MCR / MRC
// bits[27:25] = 111, bit24=0
// bit4=0 -> CDP, bit4=1 -> MCR/MRC
// ============================================================

    A2(&Interpreter::cdp, &Interpreter::mcr), // 0xE00
    A2(&Interpreter::cdp, &Interpreter::mrc), // 0xE10
    A2(&Interpreter::cdp, &Interpreter::mcr), // 0xE20
    A2(&Interpreter::cdp, &Interpreter::mrc), // 0xE30
    A2(&Interpreter::cdp, &Interpreter::mcr), // 0xE40
    A2(&Interpreter::cdp, &Interpreter::mrc), // 0xE50
    A2(&Interpreter::cdp, &Interpreter::mcr), // 0xE60
    A2(&Interpreter::cdp, &Interpreter::mrc), // 0xE70
    A2(&Interpreter::cdp, &Interpreter::mcr), // 0xE80
    A2(&Interpreter::cdp, &Interpreter::mrc), // 0xE90
    A2(&Interpreter::cdp, &Interpreter::mcr), // 0xEA0
    A2(&Interpreter::cdp, &Interpreter::mrc), // 0xEB0
    A2(&Interpreter::cdp, &Interpreter::mcr), // 0xEC0
    A2(&Interpreter::cdp, &Interpreter::mrc), // 0xED0
    A2(&Interpreter::cdp, &Interpreter::mcr), // 0xEE0
    A2(&Interpreter::cdp, &Interpreter::mrc), // 0xEF0

// ============================================================
// Group 0xF00-0xFFF: SWI (Software Interrupt)
// bits[27:25] = 111, bit24=1
// ============================================================

    R16(&Interpreter::swi), // 0xF00
    R16(&Interpreter::swi), // 0xF10
    R16(&Interpreter::swi), // 0xF20
    R16(&Interpreter::swi), // 0xF30
    R16(&Interpreter::swi), // 0xF40
    R16(&Interpreter::swi), // 0xF50
    R16(&Interpreter::swi), // 0xF60
    R16(&Interpreter::swi), // 0xF70
    R16(&Interpreter::swi), // 0xF80
    R16(&Interpreter::swi), // 0xF90
    R16(&Interpreter::swi), // 0xFA0
    R16(&Interpreter::swi), // 0xFB0
    R16(&Interpreter::swi), // 0xFC0
    R16(&Interpreter::swi), // 0xFD0
    R16(&Interpreter::swi), // 0xFE0
    R16(&Interpreter::swi), // 0xFF0
};

// ============================================================
//  THUMB Instruction Dispatch Table
//
//  THUMB instructions are 16-bit.
//  Index = bits[15:6] of the THUMB opcode (10 bits = 1024 entries)
//
//  NooDS indexing: thumbInstrs[(opcode >> 6) & 0x3FF]
//
//  THUMB opcode groups (bits[15:12]):
//   0000: LSL Rd,Rs,#Imm5     (shift=00)
//   0001: LSR/ASR             (shift=01/10)
//   0010: ADD/SUB reg/imm     (shift=11)
//   001x: MOV/CMP/ADD/SUB imm
//   0100: ALU / HI reg ops / BX / load PC
//   0101: LDR/STR reg offset
//   011x: LDR/STR imm offset
//   1000: LDRH/STRH
//   1001: LDR/STR SP relative
//   1010: ADD PC/SP
//   1011: PUSH/POP / misc
//   1100: STMIA/LDMIA
//   1101: Conditional branch / SWI
//   1110: B
//   1111: BL/BLX (two instruction sequence)
// ============================================================

const ThumbFn Interpreter::thumbInstrs[1024] = {

// ============================================================
// Bits[15:11] = 00000: LSL Rd,Rs,#imm (shift=0..31)
// Index bits[9:0] = instr[15:6]
// 0x000-0x03F: LSL with various immediates
// ============================================================

// bits[15:11]=00000 -> LSL: indices 0x000-0x01F (imm=0..31 in bits[10:6])
// NooDS index = instr[15:6]: for LSL, bits[15:11]=00000, bits[10:6]=imm5
// So indices 0x000 (imm=0) through 0x01F (imm=31): all map to lslI
    &Interpreter::lslI,  // 0x000 LSL #0 (MOV Rd,Rm)
    &Interpreter::lslI,  // 0x001
    &Interpreter::lslI,  // 0x002
    &Interpreter::lslI,  // 0x003
    &Interpreter::lslI,  // 0x004
    &Interpreter::lslI,  // 0x005
    &Interpreter::lslI,  // 0x006
    &Interpreter::lslI,  // 0x007
    &Interpreter::lslI,  // 0x008
    &Interpreter::lslI,  // 0x009
    &Interpreter::lslI,  // 0x00A
    &Interpreter::lslI,  // 0x00B
    &Interpreter::lslI,  // 0x00C
    &Interpreter::lslI,  // 0x00D
    &Interpreter::lslI,  // 0x00E
    &Interpreter::lslI,  // 0x00F
    &Interpreter::lslI,  // 0x010
    &Interpreter::lslI,  // 0x011
    &Interpreter::lslI,  // 0x012
    &Interpreter::lslI,  // 0x013
    &Interpreter::lslI,  // 0x014
    &Interpreter::lslI,  // 0x015
    &Interpreter::lslI,  // 0x016
    &Interpreter::lslI,  // 0x017
    &Interpreter::lslI,  // 0x018
    &Interpreter::lslI,  // 0x019
    &Interpreter::lslI,  // 0x01A
    &Interpreter::lslI,  // 0x01B
    &Interpreter::lslI,  // 0x01C
    &Interpreter::lslI,  // 0x01D
    &Interpreter::lslI,  // 0x01E
    &Interpreter::lslI,  // 0x01F

// bits[15:11]=00001: LSR imm (indices 0x020-0x03F)
    &Interpreter::lsrI,  // 0x020
    &Interpreter::lsrI,  // 0x021
    &Interpreter::lsrI,  // 0x022
    &Interpreter::lsrI,  // 0x023
    &Interpreter::lsrI,  // 0x024
    &Interpreter::lsrI,  // 0x025
    &Interpreter::lsrI,  // 0x026
    &Interpreter::lsrI,  // 0x027
    &Interpreter::lsrI,  // 0x028
    &Interpreter::lsrI,  // 0x029
    &Interpreter::lsrI,  // 0x02A
    &Interpreter::lsrI,  // 0x02B
    &Interpreter::lsrI,  // 0x02C
    &Interpreter::lsrI,  // 0x02D
    &Interpreter::lsrI,  // 0x02E
    &Interpreter::lsrI,  // 0x02F
    &Interpreter::lsrI,  // 0x030
    &Interpreter::lsrI,  // 0x031
    &Interpreter::lsrI,  // 0x032
    &Interpreter::lsrI,  // 0x033
    &Interpreter::lsrI,  // 0x034
    &Interpreter::lsrI,  // 0x035
    &Interpreter::lsrI,  // 0x036
    &Interpreter::lsrI,  // 0x037
    &Interpreter::lsrI,  // 0x038
    &Interpreter::lsrI,  // 0x039
    &Interpreter::lsrI,  // 0x03A
    &Interpreter::lsrI,  // 0x03B
    &Interpreter::lsrI,  // 0x03C
    &Interpreter::lsrI,  // 0x03D
    &Interpreter::lsrI,  // 0x03E
    &Interpreter::lsrI,  // 0x03F

// bits[15:11]=00010: ASR imm (indices 0x040-0x05F)
    &Interpreter::asrI,  // 0x040
    &Interpreter::asrI,  &Interpreter::asrI,  &Interpreter::asrI,
    &Interpreter::asrI,  &Interpreter::asrI,  &Interpreter::asrI,
    &Interpreter::asrI,  &Interpreter::asrI,  &Interpreter::asrI,
    &Interpreter::asrI,  &Interpreter::asrI,  &Interpreter::asrI,
    &Interpreter::asrI,  &Interpreter::asrI,  &Interpreter::asrI,
    &Interpreter::asrI,  &Interpreter::asrI,  &Interpreter::asrI,
    &Interpreter::asrI,  &Interpreter::asrI,  &Interpreter::asrI,
    &Interpreter::asrI,  &Interpreter::asrI,  &Interpreter::asrI,
    &Interpreter::asrI,  &Interpreter::asrI,  &Interpreter::asrI,
    &Interpreter::asrI,  &Interpreter::asrI,  &Interpreter::asrI, // 0x05F

// bits[15:11]=00011: ADD/SUB (indices 0x060-0x07F)
// bits[10:9]: 00=ADD reg, 01=SUB reg, 10=ADD imm3, 11=SUB imm3
    &Interpreter::addR,  // 0x060 ADD Rd,Rs,Rn
    &Interpreter::addR,  // 0x061
    &Interpreter::addR,  // 0x062
    &Interpreter::addR,  // 0x063
    &Interpreter::addR,  // 0x064
    &Interpreter::addR,  // 0x065
    &Interpreter::addR,  // 0x066
    &Interpreter::addR,  // 0x067
    &Interpreter::subR,  // 0x068 SUB Rd,Rs,Rn
    &Interpreter::subR,  // 0x069
    &Interpreter::subR,  // 0x06A
    &Interpreter::subR,  // 0x06B
    &Interpreter::subR,  // 0x06C
    &Interpreter::subR,  // 0x06D
    &Interpreter::subR,  // 0x06E
    &Interpreter::subR,  // 0x06F
    &Interpreter::add3I, // 0x070 ADD Rd,Rs,#imm3
    &Interpreter::add3I, // 0x071
    &Interpreter::add3I, // 0x072
    &Interpreter::add3I, // 0x073
    &Interpreter::add3I, // 0x074
    &Interpreter::add3I, // 0x075
    &Interpreter::add3I, // 0x076
    &Interpreter::add3I, // 0x077
    &Interpreter::sub3I, // 0x078 SUB Rd,Rs,#imm3
    &Interpreter::sub3I, // 0x079
    &Interpreter::sub3I, // 0x07A
    &Interpreter::sub3I, // 0x07B
    &Interpreter::sub3I, // 0x07C
    &Interpreter::sub3I, // 0x07D
    &Interpreter::sub3I, // 0x07E
    &Interpreter::sub3I, // 0x07F

// bits[15:13]=001: MOV/CMP/ADD/SUB immediate (indices 0x080-0x0FF)
// bits[12:11]: 00=MOV, 01=CMP, 10=ADD, 11=SUB
// bits[10:8]: Rd/Rn register; bits[7:0]: imm8
    &Interpreter::movI,  // 0x080 MOV R0,#imm8
    &Interpreter::movI,  // 0x081 MOV R0,#imm8 (different imm)
    &Interpreter::movI,  // 0x082
    &Interpreter::movI,  // 0x083
    &Interpreter::movI,  // 0x084
    &Interpreter::movI,  // 0x085
    &Interpreter::movI,  // 0x086
    &Interpreter::movI,  // 0x087
    &Interpreter::movI,  // 0x088 MOV R1,#imm8
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  // 0x08F
    &Interpreter::movI,  // 0x090 MOV R2,#imm8
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  // 0x097
    &Interpreter::movI,  // 0x098 MOV R3,#imm8
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  // 0x09F
    &Interpreter::movI,  // 0x0A0 MOV R4,#imm8
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  // 0x0A7
    &Interpreter::movI,  // 0x0A8 MOV R5,#imm8
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  // 0x0AF
    &Interpreter::movI,  // 0x0B0 MOV R6,#imm8
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  // 0x0B7
    &Interpreter::movI,  // 0x0B8 MOV R7,#imm8
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  &Interpreter::movI,  &Interpreter::movI,
    &Interpreter::movI,  // 0x0BF

    &Interpreter::cmpI,  // 0x0C0 CMP R0,#imm8
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  // 0x0C7
    &Interpreter::cmpI,  // 0x0C8 CMP R1,#imm8
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  // 0x0CF
    &Interpreter::cmpI,  // 0x0D0 CMP R2,#imm8
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  // 0x0D7
    &Interpreter::cmpI,  // 0x0D8 CMP R3,#imm8
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  // 0x0DF
    &Interpreter::cmpI,  // 0x0E0 CMP R4,#imm8
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  // 0x0E7
    &Interpreter::cmpI,  // 0x0E8 CMP R5,#imm8
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  // 0x0EF
    &Interpreter::cmpI,  // 0x0F0 CMP R6,#imm8
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  // 0x0F7
    &Interpreter::cmpI,  // 0x0F8 CMP R7,#imm8
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  &Interpreter::cmpI,  &Interpreter::cmpI,
    &Interpreter::cmpI,  // 0x0FF

// bits[15:13]=010: ALU ops / HI reg / BX / LDR PC
// 0x100-0x13F: ALU operations (bits[15:10]=010000)
    &Interpreter::andDp, // 0x100 AND
    &Interpreter::eorDp, // 0x101 EOR
    &Interpreter::lslDp, // 0x102 LSL
    &Interpreter::lsrDp, // 0x103 LSR
    &Interpreter::asrDp, // 0x104 ASR
    &Interpreter::adcDp, // 0x105 ADC
    &Interpreter::sbcDp, // 0x106 SBC
    &Interpreter::rorDp, // 0x107 ROR
    &Interpreter::tstDp, // 0x108 TST
    &Interpreter::negDp, // 0x109 NEG
    &Interpreter::cmpDp, // 0x10A CMP
    &Interpreter::cmnDp, // 0x10B CMN
    &Interpreter::orrDp, // 0x10C ORR
    &Interpreter::mulDp, // 0x10D MUL
    &Interpreter::bicDp, // 0x10E BIC
    &Interpreter::mvnDp, // 0x10F MVN

// 0x110-0x12F: HI register operations (bits[15:10]=010001)
// bits[9:8]: 00=ADD, 01=CMP, 10=MOV, 11=BX/BLX
    &Interpreter::addH,  // 0x110 ADD (HI regs)
    &Interpreter::addH,  // 0x111
    &Interpreter::addH,  // 0x112
    &Interpreter::addH,  // 0x113
    &Interpreter::cmpH,  // 0x114 CMP (HI regs)
    &Interpreter::cmpH,  // 0x115
    &Interpreter::cmpH,  // 0x116
    &Interpreter::cmpH,  // 0x117
    &Interpreter::movH,  // 0x118 MOV (HI regs)
    &Interpreter::movH,  // 0x119
    &Interpreter::movH,  // 0x11A
    &Interpreter::movH,  // 0x11B
    &Interpreter::bxReg, // 0x11C BX Rs
    &Interpreter::bxReg, // 0x11D
    &Interpreter::blxReg,// 0x11E BLX Rs
    &Interpreter::blxReg,// 0x11F

// 0x120-0x13F: LDR PC-relative (bits[15:11]=01001, Rd in bits[10:8])
    &Interpreter::ldrPc, // 0x120 LDR R0,[PC,#imm8*4]
    &Interpreter::ldrPc, &Interpreter::ldrPc, &Interpreter::ldrPc,
    &Interpreter::ldrPc, &Interpreter::ldrPc, &Interpreter::ldrPc,
    &Interpreter::ldrPc, // 0x127
    &Interpreter::ldrPc, // 0x128 LDR R1,[PC,...]
    &Interpreter::ldrPc, &Interpreter::ldrPc, &Interpreter::ldrPc,
    &Interpreter::ldrPc, &Interpreter::ldrPc, &Interpreter::ldrPc,
    &Interpreter::ldrPc, // 0x12F
    &Interpreter::ldrPc, // 0x130 LDR R2
    &Interpreter::ldrPc, &Interpreter::ldrPc, &Interpreter::ldrPc,
    &Interpreter::ldrPc, &Interpreter::ldrPc, &Interpreter::ldrPc,
    &Interpreter::ldrPc, // 0x137
    &Interpreter::ldrPc, // 0x138 LDR R3
    &Interpreter::ldrPc, &Interpreter::ldrPc, &Interpreter::ldrPc,
    &Interpreter::ldrPc, &Interpreter::ldrPc, &Interpreter::ldrPc,
    &Interpreter::ldrPc, // 0x13F

// 0x140-0x15F: LDR/STR reg offset (bits[15:12]=0101)
// bits[11:9]: 000=STR, 001=STRH, 010=STRB, 011=LDSB, 100=LDR, 101=LDRH, 110=LDRB, 111=LDSH
    &Interpreter::strR,  // 0x140 STR Rd,[Rb,Ro]
    &Interpreter::strR,  &Interpreter::strR,  &Interpreter::strR,
    &Interpreter::strR,  &Interpreter::strR,  &Interpreter::strR,
    &Interpreter::strR,  // 0x147
    &Interpreter::strhR, // 0x148 STRH
    &Interpreter::strhR, &Interpreter::strhR, &Interpreter::strhR,
    &Interpreter::strhR, &Interpreter::strhR, &Interpreter::strhR,
    &Interpreter::strhR, // 0x14F
    &Interpreter::strbR, // 0x150 STRB
    &Interpreter::strbR, &Interpreter::strbR, &Interpreter::strbR,
    &Interpreter::strbR, &Interpreter::strbR, &Interpreter::strbR,
    &Interpreter::strbR, // 0x157
    &Interpreter::ldsbR, // 0x158 LDSB (signed byte)
    &Interpreter::ldsbR, &Interpreter::ldsbR, &Interpreter::ldsbR,
    &Interpreter::ldsbR, &Interpreter::ldsbR, &Interpreter::ldsbR,
    &Interpreter::ldsbR, // 0x15F
    &Interpreter::ldrR,  // 0x160 LDR
    &Interpreter::ldrR,  &Interpreter::ldrR,  &Interpreter::ldrR,
    &Interpreter::ldrR,  &Interpreter::ldrR,  &Interpreter::ldrR,
    &Interpreter::ldrR,  // 0x167
    &Interpreter::ldrhR, // 0x168 LDRH
    &Interpreter::ldrhR, &Interpreter::ldrhR, &Interpreter::ldrhR,
    &Interpreter::ldrhR, &Interpreter::ldrhR, &Interpreter::ldrhR,
    &Interpreter::ldrhR, // 0x16F
    &Interpreter::ldrbR, // 0x170 LDRB
    &Interpreter::ldrbR, &Interpreter::ldrbR, &Interpreter::ldrbR,
    &Interpreter::ldrbR, &Interpreter::ldrbR, &Interpreter::ldrbR,
    &Interpreter::ldrbR, // 0x177
    &Interpreter::ldshR, // 0x178 LDSH (signed halfword)
    &Interpreter::ldshR, &Interpreter::ldshR, &Interpreter::ldshR,
    &Interpreter::ldshR, &Interpreter::ldshR, &Interpreter::ldshR,
    &Interpreter::ldshR, // 0x17F

// 0x180-0x1FF: LDR/STR immediate offset (bits[15:13]=011)
// bit12=B, bit11=L
// bits[10:6] = imm5 (offset)
    &Interpreter::strI,  // 0x180 STR Rd,[Rb,#imm5]
    &Interpreter::strI,  &Interpreter::strI,  &Interpreter::strI,
    &Interpreter::strI,  &Interpreter::strI,  &Interpreter::strI,
    &Interpreter::strI,  &Interpreter::strI,  &Interpreter::strI,
    &Interpreter::strI,  &Interpreter::strI,  &Interpreter::strI,
    &Interpreter::strI,  &Interpreter::strI,  &Interpreter::strI,
    &Interpreter::strI,  &Interpreter::strI,  &Interpreter::strI,
    &Interpreter::strI,  &Interpreter::strI,  &Interpreter::strI,
    &Interpreter::strI,  &Interpreter::strI,  &Interpreter::strI,
    &Interpreter::strI,  &Interpreter::strI,  &Interpreter::strI,
    &Interpreter::strI,  &Interpreter::strI,  &Interpreter::strI,
    &Interpreter::strI,  // 0x19F

    &Interpreter::ldrI,  // 0x1A0 LDR Rd,[Rb,#imm5]
    &Interpreter::ldrI,  &Interpreter::ldrI,  &Interpreter::ldrI,
    &Interpreter::ldrI,  &Interpreter::ldrI,  &Interpreter::ldrI,
    &Interpreter::ldrI,  &Interpreter::ldrI,  &Interpreter::ldrI,
    &Interpreter::ldrI,  &Interpreter::ldrI,  &Interpreter::ldrI,
    &Interpreter::ldrI,  &Interpreter::ldrI,  &Interpreter::ldrI,
    &Interpreter::ldrI,  &Interpreter::ldrI,  &Interpreter::ldrI,
    &Interpreter::ldrI,  &Interpreter::ldrI,  &Interpreter::ldrI,
    &Interpreter::ldrI,  &Interpreter::ldrI,  &Interpreter::ldrI,
    &Interpreter::ldrI,  &Interpreter::ldrI,  &Interpreter::ldrI,
    &Interpreter::ldrI,  &Interpreter::ldrI,  &Interpreter::ldrI,
    &Interpreter::ldrI,  // 0x1BF

    &Interpreter::strbI, // 0x1C0 STRB Rd,[Rb,#imm5]
    &Interpreter::strbI, &Interpreter::strbI, &Interpreter::strbI,
    &Interpreter::strbI, &Interpreter::strbI, &Interpreter::strbI,
    &Interpreter::strbI, &Interpreter::strbI, &Interpreter::strbI,
    &Interpreter::strbI, &Interpreter::strbI, &Interpreter::strbI,
    &Interpreter::strbI, &Interpreter::strbI, &Interpreter::strbI,
    &Interpreter::strbI, &Interpreter::strbI, &Interpreter::strbI,
    &Interpreter::strbI, &Interpreter::strbI, &Interpreter::strbI,
    &Interpreter::strbI, &Interpreter::strbI, &Interpreter::strbI,
    &Interpreter::strbI, &Interpreter::strbI, &Interpreter::strbI,
    &Interpreter::strbI, &Interpreter::strbI, &Interpreter::strbI,
    &Interpreter::strbI, // 0x1DF

    &Interpreter::ldrbI, // 0x1E0 LDRB Rd,[Rb,#imm5]
    &Interpreter::ldrbI, &Interpreter::ldrbI, &Interpreter::ldrbI,
    &Interpreter::ldrbI, &Interpreter::ldrbI, &Interpreter::ldrbI,
    &Interpreter::ldrbI, &Interpreter::ldrbI, &Interpreter::ldrbI,
    &Interpreter::ldrbI, &Interpreter::ldrbI, &Interpreter::ldrbI,
    &Interpreter::ldrbI, &Interpreter::ldrbI, &Interpreter::ldrbI,
    &Interpreter::ldrbI, &Interpreter::ldrbI, &Interpreter::ldrbI,
    &Interpreter::ldrbI, &Interpreter::ldrbI, &Interpreter::ldrbI,
    &Interpreter::ldrbI, &Interpreter::ldrbI, &Interpreter::ldrbI,
    &Interpreter::ldrbI, &Interpreter::ldrbI, &Interpreter::ldrbI,
    &Interpreter::ldrbI, &Interpreter::ldrbI, &Interpreter::ldrbI,
    &Interpreter::ldrbI, // 0x1FF

// 0x200-0x23F: STRH/LDRH immediate (bits[15:12]=1000)
    &Interpreter::strhI, // 0x200 STRH
    &Interpreter::strhI, &Interpreter::strhI, &Interpreter::strhI,
    &Interpreter::strhI, &Interpreter::strhI, &Interpreter::strhI,
    &Interpreter::strhI, &Interpreter::strhI, &Interpreter::strhI,
    &Interpreter::strhI, &Interpreter::strhI, &Interpreter::strhI,
    &Interpreter::strhI, &Interpreter::strhI, &Interpreter::strhI,
    &Interpreter::strhI, &Interpreter::strhI, &Interpreter::strhI,
    &Interpreter::strhI, &Interpreter::strhI, &Interpreter::strhI,
    &Interpreter::strhI, &Interpreter::strhI, &Interpreter::strhI,
    &Interpreter::strhI, &Interpreter::strhI, &Interpreter::strhI,
    &Interpreter::strhI, &Interpreter::strhI, &Interpreter::strhI,
    &Interpreter::strhI, // 0x21F

    &Interpreter::ldrhI, // 0x220 LDRH
    &Interpreter::ldrhI, &Interpreter::ldrhI, &Interpreter::ldrhI,
    &Interpreter::ldrhI, &Interpreter::ldrhI, &Interpreter::ldrhI,
    &Interpreter::ldrhI, &Interpreter::ldrhI, &Interpreter::ldrhI,
    &Interpreter::ldrhI, &Interpreter::ldrhI, &Interpreter::ldrhI,
    &Interpreter::ldrhI, &Interpreter::ldrhI, &Interpreter::ldrhI,
    &Interpreter::ldrhI, &Interpreter::ldrhI, &Interpreter::ldrhI,
    &Interpreter::ldrhI, &Interpreter::ldrhI, &Interpreter::ldrhI,
    &Interpreter::ldrhI, &Interpreter::ldrhI, &Interpreter::ldrhI,
    &Interpreter::ldrhI, &Interpreter::ldrhI, &Interpreter::ldrhI,
    &Interpreter::ldrhI, &Interpreter::ldrhI, &Interpreter::ldrhI,
    &Interpreter::ldrhI, // 0x23F

// 0x240-0x27F: STR/LDR SP-relative (bits[15:12]=1001)
    &Interpreter::strSpI, // 0x240 STR Rd,[SP,#imm8]
    &Interpreter::strSpI, &Interpreter::strSpI, &Interpreter::strSpI,
    &Interpreter::strSpI, &Interpreter::strSpI, &Interpreter::strSpI,
    &Interpreter::strSpI, &Interpreter::strSpI, &Interpreter::strSpI,
    &Interpreter::strSpI, &Interpreter::strSpI, &Interpreter::strSpI,
    &Interpreter::strSpI, &Interpreter::strSpI, &Interpreter::strSpI,
    &Interpreter::strSpI, &Interpreter::strSpI, &Interpreter::strSpI,
    &Interpreter::strSpI, &Interpreter::strSpI, &Interpreter::strSpI,
    &Interpreter::strSpI, &Interpreter::strSpI, &Interpreter::strSpI,
    &Interpreter::strSpI, &Interpreter::strSpI, &Interpreter::strSpI,
    &Interpreter::strSpI, &Interpreter::strSpI, &Interpreter::strSpI,
    &Interpreter::strSpI, // 0x25F

    &Interpreter::ldrSpI, // 0x260 LDR Rd,[SP,#imm8]
    &Interpreter::ldrSpI, &Interpreter::ldrSpI, &Interpreter::ldrSpI,
    &Interpreter::ldrSpI, &Interpreter::ldrSpI, &Interpreter::ldrSpI,
    &Interpreter::ldrSpI, &Interpreter::ldrSpI, &Interpreter::ldrSpI,
    &Interpreter::ldrSpI, &Interpreter::ldrSpI, &Interpreter::ldrSpI,
    &Interpreter::ldrSpI, &Interpreter::ldrSpI, &Interpreter::ldrSpI,
    &Interpreter::ldrSpI, &Interpreter::ldrSpI, &Interpreter::ldrSpI,
    &Interpreter::ldrSpI, &Interpreter::ldrSpI, &Interpreter::ldrSpI,
    &Interpreter::ldrSpI, &Interpreter::ldrSpI, &Interpreter::ldrSpI,
    &Interpreter::ldrSpI, &Interpreter::ldrSpI, &Interpreter::ldrSpI,
    &Interpreter::ldrSpI, &Interpreter::ldrSpI, &Interpreter::ldrSpI,
    &Interpreter::ldrSpI, // 0x27F

// 0x280-0x2BF: ADD PC/SP (bits[15:12]=1010)
// bit11: 0=PC, 1=SP
    &Interpreter::addPc, // 0x280 ADD Rd,PC,#imm8
    &Interpreter::addPc, &Interpreter::addPc, &Interpreter::addPc,
    &Interpreter::addPc, &Interpreter::addPc, &Interpreter::addPc,
    &Interpreter::addPc, &Interpreter::addPc, &Interpreter::addPc,
    &Interpreter::addPc, &Interpreter::addPc, &Interpreter::addPc,
    &Interpreter::addPc, &Interpreter::addPc, &Interpreter::addPc,
    &Interpreter::addPc, &Interpreter::addPc, &Interpreter::addPc,
    &Interpreter::addPc, &Interpreter::addPc, &Interpreter::addPc,
    &Interpreter::addPc, &Interpreter::addPc, &Interpreter::addPc,
    &Interpreter::addPc, &Interpreter::addPc, &Interpreter::addPc,
    &Interpreter::addPc, &Interpreter::addPc, &Interpreter::addPc,
    &Interpreter::addPc, // 0x29F

    &Interpreter::addSp, // 0x2A0 ADD Rd,SP,#imm8
    &Interpreter::addSp, &Interpreter::addSp, &Interpreter::addSp,
    &Interpreter::addSp, &Interpreter::addSp, &Interpreter::addSp,
    &Interpreter::addSp, &Interpreter::addSp, &Interpreter::addSp,
    &Interpreter::addSp, &Interpreter::addSp, &Interpreter::addSp,
    &Interpreter::addSp, &Interpreter::addSp, &Interpreter::addSp,
    &Interpreter::addSp, &Interpreter::addSp, &Interpreter::addSp,
    &Interpreter::addSp, &Interpreter::addSp, &Interpreter::addSp,
    &Interpreter::addSp, &Interpreter::addSp, &Interpreter::addSp,
    &Interpreter::addSp, &Interpreter::addSp, &Interpreter::addSp,
    &Interpreter::addSp, &Interpreter::addSp, &Interpreter::addSp,
    &Interpreter::addSp, // 0x2BF

// 0x2C0-0x2FF: Miscellaneous (bits[15:12]=1011)
// ADD/SUB SP, PUSH, POP, BKPT
    &Interpreter::addSpI,    // 0x2C0 ADD SP,#imm7
    &Interpreter::addSpI,    &Interpreter::addSpI,    &Interpreter::addSpI,
    &Interpreter::addSpI,    &Interpreter::addSpI,    &Interpreter::addSpI,
    &Interpreter::addSpI,    // 0x2C7
    &Interpreter::subSpI,    // 0x2C8 SUB SP,#imm7
    &Interpreter::subSpI,    &Interpreter::subSpI,    &Interpreter::subSpI,
    &Interpreter::subSpI,    &Interpreter::subSpI,    &Interpreter::subSpI,
    &Interpreter::subSpI,    // 0x2CF
    &Interpreter::undefined, // 0x2D0
    &Interpreter::undefined, &Interpreter::undefined, &Interpreter::undefined,
    &Interpreter::undefined, &Interpreter::undefined, &Interpreter::undefined,
    &Interpreter::undefined, // 0x2D7
    &Interpreter::undefined, // 0x2D8
    &Interpreter::undefined, &Interpreter::undefined, &Interpreter::undefined,
    &Interpreter::undefined, &Interpreter::undefined, &Interpreter::undefined,
    &Interpreter::undefined, // 0x2DF
    &Interpreter::push,      // 0x2E0 PUSH {rlist}
    &Interpreter::push,      &Interpreter::push,      &Interpreter::push,
    &Interpreter::push,      &Interpreter::push,      &Interpreter::push,
    &Interpreter::push,      &Interpreter::push,      &Interpreter::push,
    &Interpreter::push,      &Interpreter::push,      &Interpreter::push,
    &Interpreter::push,      &Interpreter::push,      &Interpreter::push,
    &Interpreter::pushLr,    // 0x2F0 PUSH {rlist,LR}
    &Interpreter::pushLr,    &Interpreter::pushLr,    &Interpreter::pushLr,
    &Interpreter::pushLr,    &Interpreter::pushLr,    &Interpreter::pushLr,
    &Interpreter::pushLr,    &Interpreter::pushLr,    &Interpreter::pushLr,
    &Interpreter::pushLr,    &Interpreter::pushLr,    &Interpreter::pushLr,
    &Interpreter::pushLr,    &Interpreter::pushLr,    &Interpreter::pushLr, // 0x2FF

// 0x300-0x33F: STMIA/LDMIA (bits[15:12]=1100)
    &Interpreter::stmia,  // 0x300 STMIA Rb!,{rlist}
    &Interpreter::stmia,  &Interpreter::stmia,  &Interpreter::stmia,
    &Interpreter::stmia,  &Interpreter::stmia,  &Interpreter::stmia,
    &Interpreter::stmia,  &Interpreter::stmia,  &Interpreter::stmia,
    &Interpreter::stmia,  &Interpreter::stmia,  &Interpreter::stmia,
    &Interpreter::stmia,  &Interpreter::stmia,  &Interpreter::stmia,
    &Interpreter::stmia,  &Interpreter::stmia,  &Interpreter::stmia,
    &Interpreter::stmia,  &Interpreter::stmia,  &Interpreter::stmia,
    &Interpreter::stmia,  &Interpreter::stmia,  &Interpreter::stmia,
    &Interpreter::stmia,  &Interpreter::stmia,  &Interpreter::stmia,
    &Interpreter::stmia,  &Interpreter::stmia,  &Interpreter::stmia,
    &Interpreter::stmia,  // 0x31F

    &Interpreter::ldmia,  // 0x320 LDMIA Rb!,{rlist}
    &Interpreter::ldmia,  &Interpreter::ldmia,  &Interpreter::ldmia,
    &Interpreter::ldmia,  &Interpreter::ldmia,  &Interpreter::ldmia,
    &Interpreter::ldmia,  &Interpreter::ldmia,  &Interpreter::ldmia,
    &Interpreter::ldmia,  &Interpreter::ldmia,  &Interpreter::ldmia,
    &Interpreter::ldmia,  &Interpreter::ldmia,  &Interpreter::ldmia,
    &Interpreter::ldmia,  &Interpreter::ldmia,  &Interpreter::ldmia,
    &Interpreter::ldmia,  &Interpreter::ldmia,  &Interpreter::ldmia,
    &Interpreter::ldmia,  &Interpreter::ldmia,  &Interpreter::ldmia,
    &Interpreter::ldmia,  &Interpreter::ldmia,  &Interpreter::ldmia,
    &Interpreter::ldmia,  &Interpreter::ldmia,  &Interpreter::ldmia,
    &Interpreter::ldmia,  // 0x33F

// 0x340-0x37F: Conditional branch / SWI (bits[15:12]=1101)
// bits[11:8] = condition code (0-14: Bcc, 15: SWI)
    &Interpreter::beq,       // 0x340 BEQ
    &Interpreter::beq,       &Interpreter::beq,       &Interpreter::beq,
    &Interpreter::bne,       // 0x344 BNE
    &Interpreter::bne,       &Interpreter::bne,       &Interpreter::bne,
    &Interpreter::bcs,       // 0x348 BCS
    &Interpreter::bcs,       &Interpreter::bcs,       &Interpreter::bcs,
    &Interpreter::bcc,       // 0x34C BCC
    &Interpreter::bcc,       &Interpreter::bcc,       &Interpreter::bcc,
    &Interpreter::bmi,       // 0x350 BMI
    &Interpreter::bmi,       &Interpreter::bmi,       &Interpreter::bmi,
    &Interpreter::bpl,       // 0x354 BPL
    &Interpreter::bpl,       &Interpreter::bpl,       &Interpreter::bpl,
    &Interpreter::bvs,       // 0x358 BVS
    &Interpreter::bvs,       &Interpreter::bvs,       &Interpreter::bvs,
    &Interpreter::bvc,       // 0x35C BVC
    &Interpreter::bvc,       &Interpreter::bvc,       &Interpreter::bvc,
    &Interpreter::bhi,       // 0x360 BHI
    &Interpreter::bhi,       &Interpreter::bhi,       &Interpreter::bhi,
    &Interpreter::bls,       // 0x364 BLS
    &Interpreter::bls,       &Interpreter::bls,       &Interpreter::bls,
    &Interpreter::bge,       // 0x368 BGE
    &Interpreter::bge,       &Interpreter::bge,       &Interpreter::bge,
    &Interpreter::blt,       // 0x36C BLT
    &Interpreter::blt,       &Interpreter::blt,       &Interpreter::blt,
    &Interpreter::bgt,       // 0x370 BGT
    &Interpreter::bgt,       &Interpreter::bgt,       &Interpreter::bgt,
    &Interpreter::ble,       // 0x374 BLE
    &Interpreter::ble,       &Interpreter::ble,       &Interpreter::ble,
    &Interpreter::undefined, // 0x378 (condition 0xE = AL in THUMB is undefined encoding)
    &Interpreter::undefined, &Interpreter::undefined, &Interpreter::undefined,
    &Interpreter::swi,       // 0x37C SWI
    &Interpreter::swi,       &Interpreter::swi,       &Interpreter::swi, // 0x37F

// 0x380-0x39F: B unconditional (bits[15:11]=11100)
    &Interpreter::b,     // 0x380
    &Interpreter::b,     &Interpreter::b,     &Interpreter::b,
    &Interpreter::b,     &Interpreter::b,     &Interpreter::b,
    &Interpreter::b,     &Interpreter::b,     &Interpreter::b,
    &Interpreter::b,     &Interpreter::b,     &Interpreter::b,
    &Interpreter::b,     &Interpreter::b,     &Interpreter::b,
    &Interpreter::b,     &Interpreter::b,     &Interpreter::b,
    &Interpreter::b,     &Interpreter::b,     &Interpreter::b,
    &Interpreter::b,     &Interpreter::b,     &Interpreter::b,
    &Interpreter::b,     &Interpreter::b,     &Interpreter::b,
    &Interpreter::b,     &Interpreter::b,     &Interpreter::b,
    &Interpreter::b,     // 0x39F

// 0x3A0-0x3BF: BLX suffix (bits[15:11]=11101, ARMv5)
    &Interpreter::blxOff,// 0x3A0
    &Interpreter::blxOff,&Interpreter::blxOff,&Interpreter::blxOff,
    &Interpreter::blxOff,&Interpreter::blxOff,&Interpreter::blxOff,
    &Interpreter::blxOff,&Interpreter::blxOff,&Interpreter::blxOff,
    &Interpreter::blxOff,&Interpreter::blxOff,&Interpreter::blxOff,
    &Interpreter::blxOff,&Interpreter::blxOff,&Interpreter::blxOff,
    &Interpreter::blxOff,&Interpreter::blxOff,&Interpreter::blxOff,
    &Interpreter::blxOff,&Interpreter::blxOff,&Interpreter::blxOff,
    &Interpreter::blxOff,&Interpreter::blxOff,&Interpreter::blxOff,
    &Interpreter::blxOff,&Interpreter::blxOff,&Interpreter::blxOff,
    &Interpreter::blxOff,&Interpreter::blxOff,&Interpreter::blxOff,
    &Interpreter::blxOff,// 0x3BF

// 0x3C0-0x3FF: BL prefix / BL suffix (bits[15:11]=11110/11111)
    &Interpreter::blSetup, // 0x3C0  BL setup (high offset)
    &Interpreter::blSetup, &Interpreter::blSetup, &Interpreter::blSetup,
    &Interpreter::blSetup, &Interpreter::blSetup, &Interpreter::blSetup,
    &Interpreter::blSetup, &Interpreter::blSetup, &Interpreter::blSetup,
    &Interpreter::blSetup, &Interpreter::blSetup, &Interpreter::blSetup,
    &Interpreter::blSetup, &Interpreter::blSetup, &Interpreter::blSetup,
    &Interpreter::blSetup, &Interpreter::blSetup, &Interpreter::blSetup,
    &Interpreter::blSetup, &Interpreter::blSetup, &Interpreter::blSetup,
    &Interpreter::blSetup, &Interpreter::blSetup, &Interpreter::blSetup,
    &Interpreter::blSetup, &Interpreter::blSetup, &Interpreter::blSetup,
    &Interpreter::blSetup, &Interpreter::blSetup, &Interpreter::blSetup,
    &Interpreter::blSetup, // 0x3DF

    &Interpreter::blOff,   // 0x3E0  BL offset (low offset, final branch)
    &Interpreter::blOff,   &Interpreter::blOff,   &Interpreter::blOff,
    &Interpreter::blOff,   &Interpreter::blOff,   &Interpreter::blOff,
    &Interpreter::blOff,   &Interpreter::blOff,   &Interpreter::blOff,
    &Interpreter::blOff,   &Interpreter::blOff,   &Interpreter::blOff,
    &Interpreter::blOff,   &Interpreter::blOff,   &Interpreter::blOff,
    &Interpreter::blOff,   &Interpreter::blOff,   &Interpreter::blOff,
    &Interpreter::blOff,   &Interpreter::blOff,   &Interpreter::blOff,
    &Interpreter::blOff,   &Interpreter::blOff,   &Interpreter::blOff,
    &Interpreter::blOff,   &Interpreter::blOff,   &Interpreter::blOff,
    &Interpreter::blOff,   &Interpreter::blOff,   &Interpreter::blOff,
    &Interpreter::blOff,   // 0x3FF
};

#undef R16
#undef A2
