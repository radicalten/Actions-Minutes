/*
    Copyright (C) 2026 NooDS-Wii Contributors

    This file is part of NooDS-Wii.

    NooDS-Wii is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include <cstring>
#include <algorithm>
#ifdef GEKKO
#include <gccore.h>
#endif
#include "arm_jit.h"
#include "core.h"

#if NOODS_JIT_PROFILE
extern "C" {
    #include <tuxedo/ppc/clock.h>
}
// Guest-timebase profiling of dispatch categories. Buckets:
// 0 block, 1 stub, 2 fallback LDR/STR/halfword, 3 fallback LDM/STM, 4 fallback B/BL,
// 5 fallback other ARM, 6 fallback Thumb, 7 fallback ARM9 CP15
static uint64_t s_profTicks[2][16];
static uint64_t s_profCount[2][16];
static int s_profLast = -1;
static const char *const s_profNames[16] = {"block","stub","fb_mem","fb_ldm","fb_branch","fb_arm_other","fb_thumb","fb_cp15",
                                            "block_loadfirst","fb_pcdst","fb_halfword","","","","",""};

struct ProfGuard {
    int cpuIdx; uint32_t opcode; bool thumb; uint32_t t0;
    ProfGuard(bool arm7, uint32_t op, bool th) : cpuIdx(arm7 ? 1 : 0), opcode(op), thumb(th) {
        s_profLast = -1;
        t0 = PPCGetTickCount();
    }
    ~ProfGuard() {
        uint32_t dt = PPCGetTickCount() - t0;
        int b = s_profLast;
        if (b < 0) {
            if (thumb) b = 6;
            else if (cpuIdx == 0 && (opcode & 0x0F000010u) == 0x0E000010u) b = 7;
            else {
                uint32_t c = (opcode >> 25) & 7;
                bool halfword = (c == 0) && (opcode & 0x90u) == 0x90u && (opcode & 0x60u) != 0;
                bool pcDst = ((opcode >> 12) & 0xFu) == 0xFu && c != 5;
                if (pcDst && !halfword) b = 9;
                else if (halfword) b = 10;
                else if ((c == 2) || (c == 3)) b = 2;
                else if (c == 4) b = 3;
                else if (c == 5) b = 4;
                else b = 5;
            }
        }
        s_profTicks[cpuIdx][b] += dt;
        s_profCount[cpuIdx][b]++;
    }
};
#define PROF_GUARD(arm7, op, th) ProfGuard _profGuard((arm7), (op), (th))
#define PROF_MARK(v) (s_profLast = (v))
#else
#define PROF_GUARD(arm7, op, th) ((void)0)
#define PROF_MARK(v) ((void)0)
#endif

void *Noods_MEM2_Alloc(size_t size);

// Small static BSS fallback pool if MEM2 allocation is unavailable
alignas(32) static uint32_t s_bssPoolFallback[16384];

uint32_t *ArmPpcJit::codePool = nullptr;
size_t ArmPpcJit::poolWordsUsed = 0;
size_t ArmPpcJit::poolLimitWords = ArmPpcJit::DEFAULT_POOL_WORDS;
uint32_t ArmPpcJit::currentEpoch = 1;
bool ArmPpcJit::initialized = false;

ArmPpcJit::StubEntry ArmPpcJit::stubTable[2][ArmPpcJit::STUB_TABLE_SIZE];
ArmPpcJit::BlockEntry ArmPpcJit::blockTable[2][ArmPpcJit::BLOCK_TABLE_SIZE];
uint32_t ArmPpcJit::pageEpoch[2][ArmPpcJit::PAGE_EPOCH_SIZE];
uint32_t ArmPpcJit::pageHasBlockBits[2][ArmPpcJit::PAGE_EPOCH_SIZE / 32];
ArmPpcJit::Stats ArmPpcJit::stats = {};

int ArmPpcJit::fallbackArm(Interpreter *cpu, uint32_t opcode) {
    return (cpu->*Interpreter::armInstrs[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode);
}

int ArmPpcJit::fallbackThumb(Interpreter *cpu, uint32_t opcode) {
    return (cpu->*Interpreter::thumbInstrs[(opcode >> 6) & 0x3FF])((uint16_t)opcode);
}

uintptr_t ArmPpcJit::resolveArmHandler(uint32_t opcode) {
    struct Pmf { uintptr_t fn; ptrdiff_t adj; };
    Pmf pmf;
    auto m = Interpreter::armInstrs[((opcode >> 16) & 0xFF0u) | ((opcode >> 4) & 0xFu)];
    std::memcpy(&pmf, &m, sizeof(pmf));
    return ((pmf.fn & 1u) == 0 && pmf.adj == 0) ? pmf.fn : (uintptr_t)&ArmPpcJit::fallbackArm;
}

uintptr_t ArmPpcJit::resolveThumbHandler(uint16_t opcode) {
    struct Pmf { uintptr_t fn; ptrdiff_t adj; };
    Pmf pmf;
    auto m = Interpreter::thumbInstrs[(opcode >> 6) & 0x3FFu];
    std::memcpy(&pmf, &m, sizeof(pmf));
    return ((pmf.fn & 1u) == 0 && pmf.adj == 0) ? pmf.fn : (uintptr_t)&ArmPpcJit::fallbackThumb;
}

int ArmPpcJit::condBranchArm(Interpreter *cpu, uint32_t opcode) {
    switch (Interpreter::condition[((opcode >> 24) & 0xF0) | (cpu->cpsr >> 28)]) {
        case 0: return 1;
        case 2: return cpu->handleReserved(opcode);
        default: return (cpu->*Interpreter::armInstrs[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode);
    }
}

int ArmPpcJit::execBranchArm(Interpreter *cpu, uint32_t opcode, int32_t remDelta, int32_t totalDelta, int32_t prefixCycles) {
    uint32_t oldR15 = cpu->registersUsr[15];
    uint8_t cond = (uint8_t)(opcode >> 28);
    if (cond != 0xEu) {
        uint8_t c = Interpreter::condition[((opcode >> 24) & 0xF0u) | (cpu->cpsr >> 28)];
        if (c == 0) {
            cpu->registersUsr[15] = oldR15 + (uint32_t)remDelta;
            uint8_t *pd = cpu->pcData + totalDelta;
            cpu->pcData = pd;
            cpu->pipeline[0] = U8TO32(pd - 4, 0);
            cpu->pipeline[1] = U8TO32(pd, 0);
            return prefixCycles + 1;
        }
        if (c == 2) {
            cpu->registersUsr[15] = oldR15 + (uint32_t)remDelta;
            uint8_t *pd = cpu->pcData + totalDelta;
            cpu->pcData = pd;
            cpu->pipeline[0] = U8TO32(pd - 4, 0);
            cpu->pipeline[1] = U8TO32(pd, 0);
            return prefixCycles + cpu->handleReserved(opcode);
        }
    }
    if ((opcode & 0x0E000000u) == 0x0A000000u) {
        int32_t offset = (int32_t)(opcode << 8) >> 6;
        uint32_t r15 = oldR15 + (uint32_t)remDelta;
        if (opcode & (1u << 24)) // BL
            *cpu->registers[14] = r15 - 4;
        uint32_t target = (r15 + (uint32_t)offset) & ~3u;
        uint32_t newR15 = target + 4u;
        if ((((target ^ oldR15) | (newR15 ^ oldR15)) >> 12) == 0) {
            uint8_t *pd = cpu->pcData + (int32_t)(newR15 - oldR15);
            cpu->registersUsr[15] = newR15;
            cpu->pcData = pd;
            cpu->pipeline[0] = U8TO32(pd - 4, 0);
            cpu->pipeline[1] = U8TO32(pd, 0);
            return prefixCycles + 3;
        }
        cpu->registersUsr[15] = target;
        cpu->flushPipeline();
        return prefixCycles + 3;
    }
    // BX Rn (0x012FFF10)
    cpu->registersUsr[15] = oldR15 + (uint32_t)remDelta;
    uint32_t target = *cpu->registers[opcode & 0xFu];
    cpu->cpsr |= (target & 1u) << 5;
    cpu->registersUsr[15] = target;
    cpu->flushPipeline();
    return prefixCycles + 3;
}

int ArmPpcJit::execBranchThumb(Interpreter *cpu, uint32_t opcode, int32_t remDelta, int32_t totalDelta, int32_t prefixCycles) {
    uint32_t oldR15 = cpu->registersUsr[15];
    if ((opcode & 0xF000u) == 0xD000u) { // Bcc label
        if (!Interpreter::condition[((opcode >> 4) & 0xF0u) | (cpu->cpsr >> 28)]) {
            cpu->registersUsr[15] = oldR15 + (uint32_t)remDelta;
            uint8_t *pd = cpu->pcData + totalDelta;
            cpu->pcData = pd;
            cpu->pipeline[0] = U8TO16(pd - 2, 0);
            cpu->pipeline[1] = U8TO16(pd, 0);
            return prefixCycles + 1;
        }
        int32_t offset = (int32_t)(int8_t)(opcode & 0xFFu) << 1;
        uint32_t target = (oldR15 + (uint32_t)(remDelta + offset)) & ~1u;
        uint32_t newR15 = target + 2u;
        if ((((target ^ oldR15) | (newR15 ^ oldR15)) >> 12) == 0) {
            uint8_t *pd = cpu->pcData + (int32_t)(newR15 - oldR15);
            cpu->registersUsr[15] = newR15;
            cpu->pcData = pd;
            cpu->pipeline[0] = U8TO16(pd - 2, 0);
            cpu->pipeline[1] = U8TO16(pd, 0);
            return prefixCycles + 3;
        }
        cpu->registersUsr[15] = target;
        cpu->flushPipeline();
        return prefixCycles + 3;
    }
    if ((opcode & 0xF800u) == 0xE000u) { // B label
        int32_t offset = ((int32_t)(int16_t)(opcode << 5)) >> 4;
        uint32_t target = (oldR15 + (uint32_t)(remDelta + offset)) & ~1u;
        uint32_t newR15 = target + 2u;
        if ((((target ^ oldR15) | (newR15 ^ oldR15)) >> 12) == 0) {
            uint8_t *pd = cpu->pcData + (int32_t)(newR15 - oldR15);
            cpu->registersUsr[15] = newR15;
            cpu->pcData = pd;
            cpu->pipeline[0] = U8TO16(pd - 2, 0);
            cpu->pipeline[1] = U8TO16(pd, 0);
            return prefixCycles + 3;
        }
        cpu->registersUsr[15] = target;
        cpu->flushPipeline();
        return prefixCycles + 3;
    }
    if ((opcode & 0xFF00u) == 0x4700u) { // BX Rs or BLX Rs
        cpu->registersUsr[15] = oldR15 + (uint32_t)remDelta;
        uint32_t target = *cpu->registers[(opcode >> 3) & 0xFu];
        if (opcode & 0x0080u) { // BLX Rs (ARM9 only)
            *cpu->registers[14] = cpu->registersUsr[15] - 1;
        }
        cpu->cpsr &= ~((~target & 1u) << 5);
        cpu->registersUsr[15] = target;
        cpu->flushPipeline();
        return prefixCycles + 3;
    }
    // BL / BLX suffix (0xF800..0xFFFF or 0xE800..0xEFFF)
    uint32_t r15 = oldR15 + (uint32_t)remDelta;
    uint32_t offset = (opcode & 0x7FFu) << 1;
    if ((opcode & 0x1000u) == 0) { // BLX suffix (0xE800..0xEFFF, ARM9 only)
        cpu->cpsr &= ~(1u << 5);
    }
    uint32_t ret = r15 - 1;
    cpu->registersUsr[15] = *cpu->registers[14] + offset;
    *cpu->registers[14] = ret;
    cpu->flushPipeline();
    return prefixCycles + 3;
}

extern "C" int jit_fallback_arm(Interpreter *cpu, uint32_t opcode) {
    return ArmPpcJit::fallbackArm(cpu, opcode);
}

extern "C" int jit_fallback_thumb(Interpreter *cpu, uint32_t opcode) {
    return ArmPpcJit::fallbackThumb(cpu, opcode);
}

void ArmPpcJit::flushCodeRange(void *start, size_t byteCount) {
    if (!start || byteCount == 0) return;
    uintptr_t addr = (uintptr_t)start & ~(uintptr_t)31;
    uintptr_t end  = ((uintptr_t)start + byteCount + 31) & ~(uintptr_t)31;
#ifdef GEKKO
    for (uintptr_t p = addr; p < end; p += 32) {
        asm volatile("dcbst 0, %0" :: "r"(p) : "memory");
    }
    asm volatile("sync" ::: "memory");
    for (uintptr_t p = addr; p < end; p += 32) {
        asm volatile("icbi 0, %0" :: "r"(p) : "memory");
    }
    asm volatile("sync; isync" ::: "memory");
#else
    (void)addr;
    (void)end;
#endif
}

void ArmPpcJit::init() {
    if (!codePool) {
        void *mem2 = Noods_MEM2_Alloc(DEFAULT_POOL_BYTES);
        if (mem2) {
            codePool = static_cast<uint32_t *>(mem2);
        } else {
            codePool = s_bssPoolFallback;
            poolLimitWords = sizeof(s_bssPoolFallback) / sizeof(uint32_t);
        }
    }
    poolWordsUsed = 0;
    if (poolLimitWords == 0 || poolLimitWords > DEFAULT_POOL_WORDS)
        poolLimitWords = DEFAULT_POOL_WORDS;
    currentEpoch = 1;
    std::memset(stubTable, 0, sizeof(stubTable));
    std::memset(blockTable, 0, sizeof(blockTable));
    std::memset(pageHasBlockBits, 0, sizeof(pageHasBlockBits));
    for (int c = 0; c < 2; c++) {
        for (size_t i = 0; i < PAGE_EPOCH_SIZE; i++)
            pageEpoch[c][i] = 1;
    }
    std::memset(&stats, 0, sizeof(stats));
    stats.poolLimitWords = poolLimitWords;
    initialized = true;
#ifdef GEKKO
    SYS_Report("[JIT] init codePool=%p poolLimitWords=%zu\n", (void*)codePool, poolLimitWords);
#endif
}

void ArmPpcJit::reset() {
#if NOODS_JIT_POLL_SKIP
    pollEpoch++;
#endif
    if (!initialized) {
        init();
        return;
    }
    size_t savedLimit = poolLimitWords;
    init();
    poolLimitWords = savedLimit;
    stats.poolLimitWords = savedLimit;
}

void ArmPpcJit::setPoolLimitWords(size_t maxWords) {
    if (!initialized) init();
    if (maxWords >= MAX_STUB_WORDS * 2 && maxWords <= DEFAULT_POOL_WORDS) {
        poolLimitWords = maxWords;
        stats.poolLimitWords = maxWords;
    }
}

void ArmPpcJit::wrapPool() {
    poolWordsUsed = 0;
    currentEpoch++;
    if (currentEpoch == 0 || currentEpoch >= 0x3FFFFFFFu) {
        currentEpoch = 1;
        std::memset(stubTable, 0, sizeof(stubTable));
        std::memset(blockTable, 0, sizeof(blockTable));
    }
    std::memset(pageHasBlockBits, 0, sizeof(pageHasBlockBits));
    stats.poolWraps++;
    stats.poolWordsUsed = 0;
}

void ArmPpcJit::ensureSpace(size_t wordsNeeded) {
    if (poolWordsUsed + wordsNeeded > poolLimitWords) {
        wrapPool();
    }
}

static inline void emitLoadImm32(uint32_t *&p, uint8_t rd, uint32_t val) {
    if ((int32_t)val >= -32768 && (int32_t)val <= 32767) {
        *p++ = JitPpc::li(rd, (int16_t)val);
    } else if ((val & 0xFFFFu) == 0) {
        *p++ = JitPpc::lis(rd, (int16_t)(val >> 16));
    } else {
        *p++ = JitPpc::lis(rd, (int16_t)(val >> 16));
        *p++ = JitPpc::ori(rd, rd, (uint16_t)(val & 0xFFFFu));
    }
}

// Insert standard bit srcBitStd (0=LSB..31=MSB) of srcReg into standard bit dstBitStd of dstReg
static inline void emitInsertBit(uint32_t *&p, uint8_t dstReg, uint8_t srcReg, uint8_t dstBitStd, uint8_t srcBitStd) {
    uint8_t sh = (uint8_t)((dstBitStd - srcBitStd) & 31u);
    uint8_t ppcBit = (uint8_t)(31u - dstBitStd);
    *p++ = JitPpc::rlwimi(dstReg, srcReg, sh, ppcBit, ppcBit);
}

void ArmPpcJit::emitLoadGuestReg(uint32_t *&p, uint8_t dstPpc, uint8_t guestReg) {
    const int16_t offRegUsr = (int16_t)offsetof(Interpreter, registersUsr);
    const int16_t offRegs   = (int16_t)offsetof(Interpreter, registers);
    if (guestReg < 8 || guestReg == 15) {
        *p++ = JitPpc::lwz(dstPpc, JitPpc::R31, (int16_t)(offRegUsr + guestReg * 4));
    } else {
        *p++ = JitPpc::lwz(JitPpc::R11, JitPpc::R31, (int16_t)(offRegs + guestReg * 4));
        *p++ = JitPpc::lwz(dstPpc, JitPpc::R11, 0);
    }
}

void ArmPpcJit::emitStoreGuestReg(uint32_t *&p, uint8_t srcPpc, uint8_t guestReg) {
    const int16_t offRegUsr = (int16_t)offsetof(Interpreter, registersUsr);
    const int16_t offRegs   = (int16_t)offsetof(Interpreter, registers);
    if (guestReg < 8 || guestReg == 15) {
        *p++ = JitPpc::stw(srcPpc, JitPpc::R31, (int16_t)(offRegUsr + guestReg * 4));
    } else {
        *p++ = JitPpc::lwz(JitPpc::R11, JitPpc::R31, (int16_t)(offRegs + guestReg * 4));
        *p++ = JitPpc::stw(srcPpc, JitPpc::R11, 0);
    }
}

void ArmPpcJit::emitUpdateNZ(uint32_t *&p, uint8_t resReg) {
    const int16_t offCpsr = (int16_t)offsetof(Interpreter, cpsr);
    *p++ = JitPpc::lwz(JitPpc::R6, JitPpc::R31, offCpsr);
    emitInsertBit(p, JitPpc::R6, resReg, 31, 31);          // N = res[31]
    *p++ = JitPpc::cntlzw(JitPpc::R7, resReg);             // R7 = 32 (bit 5) iff res == 0
    emitInsertBit(p, JitPpc::R6, JitPpc::R7, 30, 5);       // Z = R7[5]
    *p++ = JitPpc::stw(JitPpc::R6, JitPpc::R31, offCpsr);
}

// Requires: R3 = res (from addc R3, R4, R5), R4 = op1, R5 = op2
void ArmPpcJit::emitUpdateAddNZCV(uint32_t *&p) {
    const int16_t offCpsr = (int16_t)offsetof(Interpreter, cpsr);
    *p++ = JitPpc::lwz(JitPpc::R6, JitPpc::R31, offCpsr);
    emitInsertBit(p, JitPpc::R6, JitPpc::R3, 31, 31);      // N = res[31]
    *p++ = JitPpc::cntlzw(JitPpc::R7, JitPpc::R3);
    emitInsertBit(p, JitPpc::R6, JitPpc::R7, 30, 5);       // Z = (res == 0)
    *p++ = JitPpc::mfxer(JitPpc::R7);
    emitInsertBit(p, JitPpc::R6, JitPpc::R7, 29, 29);      // C = XER.CA (standard bit 29)
    *p++ = JitPpc::xor_(JitPpc::R8, JitPpc::R3, JitPpc::R5); // R8 = res ^ op2
    *p++ = JitPpc::eqv(JitPpc::R9, JitPpc::R4, JitPpc::R5);  // R9 = ~(op1 ^ op2)
    *p++ = JitPpc::and_(JitPpc::R8, JitPpc::R8, JitPpc::R9); // R8 = ~(op1 ^ op2) & (res ^ op2)
    emitInsertBit(p, JitPpc::R6, JitPpc::R8, 28, 31);      // V = R8[31]
    *p++ = JitPpc::stw(JitPpc::R6, JitPpc::R31, offCpsr);
}

// Requires: R3 = res (from subfc R3, R5, R4 = R4 - R5), R4 = op1, R5 = op2
void ArmPpcJit::emitUpdateSubNZCV(uint32_t *&p) {
    const int16_t offCpsr = (int16_t)offsetof(Interpreter, cpsr);
    *p++ = JitPpc::lwz(JitPpc::R6, JitPpc::R31, offCpsr);
    emitInsertBit(p, JitPpc::R6, JitPpc::R3, 31, 31);      // N = res[31]
    *p++ = JitPpc::cntlzw(JitPpc::R7, JitPpc::R3);
    emitInsertBit(p, JitPpc::R6, JitPpc::R7, 30, 5);       // Z = (res == 0)
    *p++ = JitPpc::mfxer(JitPpc::R7);
    emitInsertBit(p, JitPpc::R6, JitPpc::R7, 29, 29);      // C = XER.CA (standard bit 29)
    *p++ = JitPpc::xor_(JitPpc::R8, JitPpc::R4, JitPpc::R5); // R8 = op1 ^ op2
    *p++ = JitPpc::eqv(JitPpc::R9, JitPpc::R3, JitPpc::R5);  // R9 = ~(res ^ op2)
    *p++ = JitPpc::and_(JitPpc::R8, JitPpc::R8, JitPpc::R9); // R8 = (op1 ^ op2) & ~(res ^ op2)
    emitInsertBit(p, JitPpc::R6, JitPpc::R8, 28, 31);      // V = R8[31]
    *p++ = JitPpc::stw(JitPpc::R6, JitPpc::R31, offCpsr);
}

bool ArmPpcJit::emitNativeThumb(uint32_t *&p, Interpreter *cpu, uint16_t opcode, int &outCycles, bool emitFlags) {
    (void)cpu;
    const int16_t offCpsr = (int16_t)offsetof(Interpreter, cpsr);
    uint8_t top3 = (uint8_t)(opcode >> 13);

    switch (top3) {
    case 0: { // 000xx: Shift by immediate or add/sub reg/imm3
        uint8_t subOp = (uint8_t)((opcode >> 11) & 0x3);
        uint8_t rd = (uint8_t)(opcode & 0x7);
        uint8_t rs = (uint8_t)((opcode >> 3) & 0x7);
        uint8_t imm5 = (uint8_t)((opcode >> 6) & 0x1F);

        if (subOp == 0) { // LSL Rd, Rs, #imm5
            emitLoadGuestReg(p, JitPpc::R4, rs);
            if (imm5 == 0) {
                *p++ = JitPpc::mr(JitPpc::R3, JitPpc::R4);
                emitStoreGuestReg(p, JitPpc::R3, rd);
                if (emitFlags) emitUpdateNZ(p, JitPpc::R3);
            } else {
                *p++ = JitPpc::slwi(JitPpc::R3, JitPpc::R4, imm5);
                emitStoreGuestReg(p, JitPpc::R3, rd);
                if (emitFlags) {
                    *p++ = JitPpc::lwz(JitPpc::R6, JitPpc::R31, offCpsr);
                    emitInsertBit(p, JitPpc::R6, JitPpc::R3, 31, 31);
                    *p++ = JitPpc::cntlzw(JitPpc::R7, JitPpc::R3);
                    emitInsertBit(p, JitPpc::R6, JitPpc::R7, 30, 5);
                    emitInsertBit(p, JitPpc::R6, JitPpc::R4, 29, (uint8_t)(32 - imm5));
                    *p++ = JitPpc::stw(JitPpc::R6, JitPpc::R31, offCpsr);
                }
            }
            outCycles = 1;
            return true;
        }
        if (subOp == 1) { // LSR Rd, Rs, #imm5
            emitLoadGuestReg(p, JitPpc::R4, rs);
            if (imm5 == 0) {
                *p++ = JitPpc::li(JitPpc::R3, 0);
            } else {
                *p++ = JitPpc::srwi(JitPpc::R3, JitPpc::R4, imm5);
            }
            emitStoreGuestReg(p, JitPpc::R3, rd);
            if (emitFlags) {
                *p++ = JitPpc::lwz(JitPpc::R6, JitPpc::R31, offCpsr);
                emitInsertBit(p, JitPpc::R6, JitPpc::R3, 31, 31);
                *p++ = JitPpc::cntlzw(JitPpc::R7, JitPpc::R3);
                emitInsertBit(p, JitPpc::R6, JitPpc::R7, 30, 5);
                emitInsertBit(p, JitPpc::R6, JitPpc::R4, 29, (uint8_t)(imm5 ? (imm5 - 1) : 31));
                *p++ = JitPpc::stw(JitPpc::R6, JitPpc::R31, offCpsr);
            }
            outCycles = 1;
            return true;
        }
        if (subOp == 2) { // ASR Rd, Rs, #imm5
            emitLoadGuestReg(p, JitPpc::R4, rs);
            *p++ = JitPpc::srawi(JitPpc::R3, JitPpc::R4, (uint8_t)(imm5 ? imm5 : 31));
            emitStoreGuestReg(p, JitPpc::R3, rd);
            if (emitFlags) {
                *p++ = JitPpc::lwz(JitPpc::R6, JitPpc::R31, offCpsr);
                emitInsertBit(p, JitPpc::R6, JitPpc::R3, 31, 31);
                *p++ = JitPpc::cntlzw(JitPpc::R7, JitPpc::R3);
                emitInsertBit(p, JitPpc::R6, JitPpc::R7, 30, 5);
                emitInsertBit(p, JitPpc::R6, JitPpc::R4, 29, (uint8_t)(imm5 ? (imm5 - 1) : 31));
                *p++ = JitPpc::stw(JitPpc::R6, JitPpc::R31, offCpsr);
            }
            outCycles = 1;
            return true;
        }
        // subOp == 3: ADD/SUB reg or imm3
        uint8_t kind = (uint8_t)((opcode >> 9) & 0x3);
        uint8_t rn_or_imm = (uint8_t)((opcode >> 6) & 0x7);
        emitLoadGuestReg(p, JitPpc::R4, rs);
        if (kind & 2) {
            *p++ = JitPpc::li(JitPpc::R5, (int16_t)rn_or_imm);
        } else {
            emitLoadGuestReg(p, JitPpc::R5, rn_or_imm);
        }
        if (kind & 1) { // SUB
            if (emitFlags) {
                *p++ = JitPpc::subfc(JitPpc::R3, JitPpc::R5, JitPpc::R4);
                emitStoreGuestReg(p, JitPpc::R3, rd);
                emitUpdateSubNZCV(p);
            } else {
                *p++ = JitPpc::subf(JitPpc::R3, JitPpc::R5, JitPpc::R4);
                emitStoreGuestReg(p, JitPpc::R3, rd);
            }
        } else { // ADD
            if (emitFlags) {
                *p++ = JitPpc::addc(JitPpc::R3, JitPpc::R4, JitPpc::R5);
                emitStoreGuestReg(p, JitPpc::R3, rd);
                emitUpdateAddNZCV(p);
            } else {
                *p++ = JitPpc::add(JitPpc::R3, JitPpc::R4, JitPpc::R5);
                emitStoreGuestReg(p, JitPpc::R3, rd);
            }
        }
        outCycles = 1;
        return true;
    }

    case 1: { // 001xx: MOV/CMP/ADD/SUB Rd, #imm8
        uint8_t op = (uint8_t)((opcode >> 11) & 0x3);
        uint8_t rd = (uint8_t)((opcode >> 8) & 0x7);
        uint8_t imm8 = (uint8_t)(opcode & 0xFF);
        if (op == 0) { // MOV Rd, #imm8
            *p++ = JitPpc::li(JitPpc::R3, (int16_t)imm8);
            emitStoreGuestReg(p, JitPpc::R3, rd);
            if (emitFlags) emitUpdateNZ(p, JitPpc::R3);
            outCycles = 1;
            return true;
        }
        if (op == 1) { // CMP Rd, #imm8
            if (emitFlags) {
                emitLoadGuestReg(p, JitPpc::R4, rd);
                *p++ = JitPpc::li(JitPpc::R5, (int16_t)imm8);
                *p++ = JitPpc::subfc(JitPpc::R3, JitPpc::R5, JitPpc::R4);
                emitUpdateSubNZCV(p);
            }
            outCycles = 1;
            return true;
        }
        emitLoadGuestReg(p, JitPpc::R4, rd);
        *p++ = JitPpc::li(JitPpc::R5, (int16_t)imm8);
        if (op == 2) { // ADD Rd, #imm8
            if (emitFlags) {
                *p++ = JitPpc::addc(JitPpc::R3, JitPpc::R4, JitPpc::R5);
                emitStoreGuestReg(p, JitPpc::R3, rd);
                emitUpdateAddNZCV(p);
            } else {
                *p++ = JitPpc::add(JitPpc::R3, JitPpc::R4, JitPpc::R5);
                emitStoreGuestReg(p, JitPpc::R3, rd);
            }
        } else { // SUB Rd, #imm8
            if (emitFlags) {
                *p++ = JitPpc::subfc(JitPpc::R3, JitPpc::R5, JitPpc::R4);
                emitStoreGuestReg(p, JitPpc::R3, rd);
                emitUpdateSubNZCV(p);
            } else {
                *p++ = JitPpc::subf(JitPpc::R3, JitPpc::R5, JitPpc::R4);
                emitStoreGuestReg(p, JitPpc::R3, rd);
            }
        }
        outCycles = 1;
        return true;
    }

    case 2: {
        if ((opcode & 0xFC00u) == 0x4000u) { // 010000: Data-processing register
            uint8_t aluOp = (uint8_t)((opcode >> 6) & 0xF);
            uint8_t rd = (uint8_t)(opcode & 0x7);
            uint8_t rs = (uint8_t)((opcode >> 3) & 0x7);
            switch (aluOp) {
            case 0x0: // AND Rd, Rs
                emitLoadGuestReg(p, JitPpc::R4, rd);
                emitLoadGuestReg(p, JitPpc::R5, rs);
                *p++ = JitPpc::and_(JitPpc::R3, JitPpc::R4, JitPpc::R5);
                emitStoreGuestReg(p, JitPpc::R3, rd);
                if (emitFlags) emitUpdateNZ(p, JitPpc::R3);
                outCycles = 1;
                return true;
            case 0x1: // EOR Rd, Rs
                emitLoadGuestReg(p, JitPpc::R4, rd);
                emitLoadGuestReg(p, JitPpc::R5, rs);
                *p++ = JitPpc::xor_(JitPpc::R3, JitPpc::R4, JitPpc::R5);
                emitStoreGuestReg(p, JitPpc::R3, rd);
                if (emitFlags) emitUpdateNZ(p, JitPpc::R3);
                outCycles = 1;
                return true;
            case 0x8: // TST Rd, Rs
                if (emitFlags) {
                    emitLoadGuestReg(p, JitPpc::R4, rd);
                    emitLoadGuestReg(p, JitPpc::R5, rs);
                    *p++ = JitPpc::and_(JitPpc::R3, JitPpc::R4, JitPpc::R5);
                    emitUpdateNZ(p, JitPpc::R3);
                }
                outCycles = 1;
                return true;
            case 0x9: // NEG Rd, Rs (Rd = 0 - Rs)
                *p++ = JitPpc::li(JitPpc::R4, 0);
                emitLoadGuestReg(p, JitPpc::R5, rs);
                if (emitFlags) {
                    *p++ = JitPpc::subfc(JitPpc::R3, JitPpc::R5, JitPpc::R4);
                    emitStoreGuestReg(p, JitPpc::R3, rd);
                    emitUpdateSubNZCV(p);
                } else {
                    *p++ = JitPpc::subf(JitPpc::R3, JitPpc::R5, JitPpc::R4);
                    emitStoreGuestReg(p, JitPpc::R3, rd);
                }
                outCycles = 1;
                return true;
            case 0xA: // CMP Rd, Rs
                if (emitFlags) {
                    emitLoadGuestReg(p, JitPpc::R4, rd);
                    emitLoadGuestReg(p, JitPpc::R5, rs);
                    *p++ = JitPpc::subfc(JitPpc::R3, JitPpc::R5, JitPpc::R4);
                    emitUpdateSubNZCV(p);
                }
                outCycles = 1;
                return true;
            case 0xB: // CMN Rd, Rs
                if (emitFlags) {
                    emitLoadGuestReg(p, JitPpc::R4, rd);
                    emitLoadGuestReg(p, JitPpc::R5, rs);
                    *p++ = JitPpc::addc(JitPpc::R3, JitPpc::R4, JitPpc::R5);
                    emitUpdateAddNZCV(p);
                }
                outCycles = 1;
                return true;
            case 0xC: // ORR Rd, Rs
                emitLoadGuestReg(p, JitPpc::R4, rd);
                emitLoadGuestReg(p, JitPpc::R5, rs);
                *p++ = JitPpc::or_(JitPpc::R3, JitPpc::R4, JitPpc::R5);
                emitStoreGuestReg(p, JitPpc::R3, rd);
                if (emitFlags) emitUpdateNZ(p, JitPpc::R3);
                outCycles = 1;
                return true;
            case 0xE: // BIC Rd, Rs
                emitLoadGuestReg(p, JitPpc::R4, rd);
                emitLoadGuestReg(p, JitPpc::R5, rs);
                *p++ = JitPpc::andc(JitPpc::R3, JitPpc::R4, JitPpc::R5);
                emitStoreGuestReg(p, JitPpc::R3, rd);
                if (emitFlags) emitUpdateNZ(p, JitPpc::R3);
                outCycles = 1;
                return true;
            case 0xF: // MVN Rd, Rs
                emitLoadGuestReg(p, JitPpc::R5, rs);
                *p++ = JitPpc::not_(JitPpc::R3, JitPpc::R5);
                emitStoreGuestReg(p, JitPpc::R3, rd);
                if (emitFlags) emitUpdateNZ(p, JitPpc::R3);
                outCycles = 1;
                return true;
            default:
                return false;
            }
        }
        if ((opcode & 0xFC00u) == 0x4400u) { // 010001: Special data processing (high regs)
            uint8_t op = (uint8_t)((opcode >> 8) & 0x3);
            uint8_t rd = (uint8_t)(((opcode >> 4) & 0x8) | (opcode & 0x7));
            uint8_t rs = (uint8_t)((opcode >> 3) & 0xF);
            if (op == 0 && rd != 15) { // ADD Rd, Rs (no flags)
                emitLoadGuestReg(p, JitPpc::R4, rd);
                emitLoadGuestReg(p, JitPpc::R5, rs);
                *p++ = JitPpc::add(JitPpc::R3, JitPpc::R4, JitPpc::R5);
                emitStoreGuestReg(p, JitPpc::R3, rd);
                outCycles = 1;
                return true;
            }
            if (op == 1) { // CMP Rd, Rs
                if (emitFlags) {
                    emitLoadGuestReg(p, JitPpc::R4, rd);
                    emitLoadGuestReg(p, JitPpc::R5, rs);
                    *p++ = JitPpc::subfc(JitPpc::R3, JitPpc::R5, JitPpc::R4);
                    emitUpdateSubNZCV(p);
                }
                outCycles = 1;
                return true;
            }
            if (op == 2 && rd != 15) { // MOV Rd, Rs (no flags)
                emitLoadGuestReg(p, JitPpc::R3, rs);
                emitStoreGuestReg(p, JitPpc::R3, rd);
                outCycles = 1;
                return true;
            }
        }
        return false;
    }

    case 5: { // 101xx: ADD Rd, PC/SP, #imm8 or ADD SP, #simm7
        if ((opcode & 0xF800u) == 0xA000u) { // ADD Rd, PC, #imm8
            uint8_t rd = (uint8_t)((opcode >> 8) & 0x7);
            uint16_t imm = (uint16_t)((opcode & 0xFFu) << 2);
            emitLoadGuestReg(p, JitPpc::R4, 15);
            *p++ = JitPpc::rlwinm(JitPpc::R4, JitPpc::R4, 0, 0, 29); // & ~3
            *p++ = JitPpc::addi(JitPpc::R3, JitPpc::R4, (int16_t)imm);
            emitStoreGuestReg(p, JitPpc::R3, rd);
            outCycles = 1;
            return true;
        }
        if ((opcode & 0xF800u) == 0xA800u) { // ADD Rd, SP, #imm8
            uint8_t rd = (uint8_t)((opcode >> 8) & 0x7);
            uint16_t imm = (uint16_t)((opcode & 0xFFu) << 2);
            emitLoadGuestReg(p, JitPpc::R4, 13);
            *p++ = JitPpc::addi(JitPpc::R3, JitPpc::R4, (int16_t)imm);
            emitStoreGuestReg(p, JitPpc::R3, rd);
            outCycles = 1;
            return true;
        }
        if ((opcode & 0xFF00u) == 0xB000u) { // ADD SP, #simm7
            int16_t simm = (int16_t)(((opcode & 0x80u) ? -(int32_t)(opcode & 0x7Fu) : (int32_t)(opcode & 0x7Fu)) << 2);
            emitLoadGuestReg(p, JitPpc::R4, 13);
            *p++ = JitPpc::addi(JitPpc::R3, JitPpc::R4, simm);
            emitStoreGuestReg(p, JitPpc::R3, 13);
            outCycles = 1;
            return true;
        }
        return false;
    }

    case 7: {
        if ((opcode & 0xF800u) == 0xF000u) { // BL setup: LR = PC + (sign_ext11 << 12)
            int32_t off = ((int32_t)(int16_t)(opcode << 5) >> 4) << 11;
            emitLoadGuestReg(p, JitPpc::R4, 15);
            emitLoadImm32(p, JitPpc::R5, (uint32_t)off);
            *p++ = JitPpc::add(JitPpc::R3, JitPpc::R4, JitPpc::R5);
            emitStoreGuestReg(p, JitPpc::R3, 14);
            outCycles = 1;
            return true;
        }
        return false;
    }

    default:
        return false;
    }
}

bool ArmPpcJit::emitNativeArm(uint32_t *&p, Interpreter *cpu, uint32_t opcode, int &outCycles, bool emitFlags) {
    // ARM9 CP15 instructions always exit to interpreter
    if (!cpu->arm7 && (opcode & 0x0F000010u) == 0x0E000010u) {
        return false;
    }

    // ARMv5TE CLZ Rd, Rm (0x016F0F10)
    if ((opcode & 0x0FFF0FF0u) == 0x016F0F10u) {
        uint8_t rd = (uint8_t)((opcode >> 12) & 0xF);
        uint8_t rm = (uint8_t)(opcode & 0xF);
        if (rd == 15 || rm == 15) return false;
        if (!cpu->arm7) {
            emitLoadGuestReg(p, JitPpc::R4, rm);
            *p++ = JitPpc::cntlzw(JitPpc::R3, JitPpc::R4);
            emitStoreGuestReg(p, JitPpc::R3, rd);
        }
        outCycles = 1;
        return true;
    }

    // Data processing: bits 27..26 == 00
    if ((opcode & 0x0C000000u) != 0x00000000u) return false;

    bool isImm = (opcode & (1u << 25)) != 0;
    if (!isImm) {
        // Reject multiplies / misc / halfword transfers (bit 4 == 1)
        if ((opcode & (1u << 4)) != 0) return false;
    }

    uint8_t aluOp = (uint8_t)((opcode >> 21) & 0xF);
    bool rawSetFlags = (opcode & (1u << 20)) != 0;
    uint8_t rn = (uint8_t)((opcode >> 16) & 0xF);
    uint8_t rd = (uint8_t)((opcode >> 12) & 0xF);

    // Exclude MSR/MRS encodings (aluOp 8..11 with rawSetFlags == false)
    if (aluOp >= 0x8 && aluOp <= 0xB && !rawSetFlags) return false;
    // Exclude R15 writes (branches/pipeline flushes)
    if (rd == 15) return false;
    // ADCS/SBCS/RSCS (S==1) use interpreter handler to preserve exact upstream flag masks; non-S ADC/SBC/RSC are emitted natively
    if ((aluOp == 0x5 || aluOp == 0x6 || aluOp == 0x7) && rawSetFlags) return false;
    if (!isImm) {
        uint8_t shiftType = (uint8_t)((opcode >> 5) & 0x3);
        uint8_t shiftImm = (uint8_t)((opcode >> 7) & 0x1F);
        // RRX (shiftType == 3 && shiftImm == 0) uses CPSR.C; leave to fallback
        if (shiftType == 3 && shiftImm == 0) return false;
    }

    bool setFlags = rawSetFlags && emitFlags;
    // Test/compare with dead flags is a 1-cycle no-op
    if (aluOp >= 0x8 && aluOp <= 0xB && !setFlags) {
        outCycles = 1;
        return true;
    }

    // Prepare operand 2 in R5 (and track shifter carry-out for logical S==1 ops)
    bool logicalOp = (aluOp == 0x0 || aluOp == 0x1 || aluOp == 0x8 || aluOp == 0x9 ||
                      aluOp == 0xC || aluOp == 0xD || aluOp == 0xE || aluOp == 0xF);
    bool hasShifterCarry = false;
    uint8_t carrySrcReg = 0;
    uint8_t carrySrcBit = 0;

    if (isImm) {
        uint32_t imm8 = opcode & 0xFFu;
        uint8_t rot = (uint8_t)((opcode >> 7) & 0x1Eu);
        uint32_t val = rot ? ((imm8 << (32 - rot)) | (imm8 >> rot)) : imm8;
        emitLoadImm32(p, JitPpc::R5, val);
        if (setFlags && logicalOp && rot != 0) {
            hasShifterCarry = true;
            carrySrcReg = JitPpc::R5;
            carrySrcBit = 31; // bit 31 of rotated val equals imm8[rot - 1]
        }
    } else {
        uint8_t rm = (uint8_t)(opcode & 0xF);
        uint8_t shiftType = (uint8_t)((opcode >> 5) & 0x3);
        uint8_t shiftImm = (uint8_t)((opcode >> 7) & 0x1F);
        // RRX (shiftType == 3 && shiftImm == 0) uses CPSR.C; leave to fallback
        if (shiftType == 3 && shiftImm == 0) return false;

        if (setFlags && logicalOp && !(shiftType == 0 && shiftImm == 0)) {
            hasShifterCarry = true;
            carrySrcReg = JitPpc::R10;
            emitLoadGuestReg(p, JitPpc::R10, rm);
            if (shiftType == 0) { // LSL (shiftImm > 0)
                *p++ = JitPpc::slwi(JitPpc::R5, JitPpc::R10, shiftImm);
                carrySrcBit = (uint8_t)(32 - shiftImm);
            } else if (shiftType == 1) { // LSR
                if (shiftImm == 0) *p++ = JitPpc::li(JitPpc::R5, 0);
                else *p++ = JitPpc::srwi(JitPpc::R5, JitPpc::R10, shiftImm);
                carrySrcBit = (uint8_t)(shiftImm ? (shiftImm - 1) : 31);
            } else if (shiftType == 2) { // ASR
                *p++ = JitPpc::srawi(JitPpc::R5, JitPpc::R10, (uint8_t)(shiftImm ? shiftImm : 31));
                carrySrcBit = (uint8_t)(shiftImm ? (shiftImm - 1) : 31);
            } else { // ROR (shiftImm > 0)
                *p++ = JitPpc::rotrwi(JitPpc::R5, JitPpc::R10, shiftImm);
                carrySrcBit = (uint8_t)(shiftImm - 1);
            }
        } else {
            emitLoadGuestReg(p, JitPpc::R5, rm);
            if (shiftType == 0) { // LSL
                if (shiftImm != 0) *p++ = JitPpc::slwi(JitPpc::R5, JitPpc::R5, shiftImm);
            } else if (shiftType == 1) { // LSR
                if (shiftImm == 0) *p++ = JitPpc::li(JitPpc::R5, 0);
                else *p++ = JitPpc::srwi(JitPpc::R5, JitPpc::R5, shiftImm);
            } else if (shiftType == 2) { // ASR
                *p++ = JitPpc::srawi(JitPpc::R5, JitPpc::R5, (uint8_t)(shiftImm ? shiftImm : 31));
            } else { // ROR (shiftImm != 0)
                *p++ = JitPpc::rotrwi(JitPpc::R5, JitPpc::R5, shiftImm);
            }
        }
    }

    // Load Rn into R4 for binary ops
    if (aluOp != 0xD && aluOp != 0xF) {
        emitLoadGuestReg(p, JitPpc::R4, rn);
    }

    auto emitLogicalFlags = [&](uint8_t resReg) {
        if (!setFlags) return;
        if (!hasShifterCarry) {
            emitUpdateNZ(p, resReg);
        } else {
            const int16_t offCpsr = (int16_t)offsetof(Interpreter, cpsr);
            *p++ = JitPpc::lwz(JitPpc::R6, JitPpc::R31, offCpsr);
            emitInsertBit(p, JitPpc::R6, resReg, 31, 31);
            *p++ = JitPpc::cntlzw(JitPpc::R7, resReg);
            emitInsertBit(p, JitPpc::R6, JitPpc::R7, 30, 5);
            emitInsertBit(p, JitPpc::R6, carrySrcReg, 29, carrySrcBit);
            *p++ = JitPpc::stw(JitPpc::R6, JitPpc::R31, offCpsr);
        }
    };

    switch (aluOp) {
    case 0x0: // AND
        *p++ = JitPpc::and_(JitPpc::R3, JitPpc::R4, JitPpc::R5);
        emitStoreGuestReg(p, JitPpc::R3, rd);
        emitLogicalFlags(JitPpc::R3);
        break;
    case 0x1: // EOR
        *p++ = JitPpc::xor_(JitPpc::R3, JitPpc::R4, JitPpc::R5);
        emitStoreGuestReg(p, JitPpc::R3, rd);
        emitLogicalFlags(JitPpc::R3);
        break;
    case 0x2: // SUB
        if (setFlags) {
            *p++ = JitPpc::subfc(JitPpc::R3, JitPpc::R5, JitPpc::R4);
            emitStoreGuestReg(p, JitPpc::R3, rd);
            emitUpdateSubNZCV(p);
        } else {
            *p++ = JitPpc::subf(JitPpc::R3, JitPpc::R5, JitPpc::R4);
            emitStoreGuestReg(p, JitPpc::R3, rd);
        }
        break;
    case 0x3: // RSB (res = op2 - op1)
        if (setFlags) {
            // Swap R4 and R5 so R4 = op2, R5 = op1 for emitUpdateSubNZCV
            *p++ = JitPpc::mr(JitPpc::R10, JitPpc::R4);
            *p++ = JitPpc::mr(JitPpc::R4, JitPpc::R5);
            *p++ = JitPpc::mr(JitPpc::R5, JitPpc::R10);
            *p++ = JitPpc::subfc(JitPpc::R3, JitPpc::R5, JitPpc::R4);
            emitStoreGuestReg(p, JitPpc::R3, rd);
            emitUpdateSubNZCV(p);
        } else {
            *p++ = JitPpc::subf(JitPpc::R3, JitPpc::R4, JitPpc::R5);
            emitStoreGuestReg(p, JitPpc::R3, rd);
        }
        break;
    case 0x4: // ADD
        if (setFlags) {
            *p++ = JitPpc::addc(JitPpc::R3, JitPpc::R4, JitPpc::R5);
            emitStoreGuestReg(p, JitPpc::R3, rd);
            emitUpdateAddNZCV(p);
        } else {
            *p++ = JitPpc::add(JitPpc::R3, JitPpc::R4, JitPpc::R5);
            emitStoreGuestReg(p, JitPpc::R3, rd);
        }
        break;
    case 0x5: { // ADC (non-S: Rd = op1 + op2 + C)
        const int16_t offCpsr = (int16_t)offsetof(Interpreter, cpsr);
        *p++ = JitPpc::lwz(JitPpc::R6, JitPpc::R31, offCpsr);
        *p++ = JitPpc::rlwinm(JitPpc::R6, JitPpc::R6, 3, 31, 31); // R6 = (cpsr >> 29) & 1
        *p++ = JitPpc::add(JitPpc::R3, JitPpc::R4, JitPpc::R5);
        *p++ = JitPpc::add(JitPpc::R3, JitPpc::R3, JitPpc::R6);
        emitStoreGuestReg(p, JitPpc::R3, rd);
        break;
    }
    case 0x6: { // SBC (non-S: Rd = op1 - op2 - !C)
        const int16_t offCpsr = (int16_t)offsetof(Interpreter, cpsr);
        *p++ = JitPpc::lwz(JitPpc::R6, JitPpc::R31, offCpsr);
        *p++ = JitPpc::rlwinm(JitPpc::R6, JitPpc::R6, 3, 31, 31);
        *p++ = JitPpc::addi(JitPpc::R6, JitPpc::R6, -1); // R6 = C - 1 = -!C
        *p++ = JitPpc::subf(JitPpc::R3, JitPpc::R5, JitPpc::R4);
        *p++ = JitPpc::add(JitPpc::R3, JitPpc::R3, JitPpc::R6);
        emitStoreGuestReg(p, JitPpc::R3, rd);
        break;
    }
    case 0x7: { // RSC (non-S: Rd = op2 - op1 - !C)
        const int16_t offCpsr = (int16_t)offsetof(Interpreter, cpsr);
        *p++ = JitPpc::lwz(JitPpc::R6, JitPpc::R31, offCpsr);
        *p++ = JitPpc::rlwinm(JitPpc::R6, JitPpc::R6, 3, 31, 31);
        *p++ = JitPpc::addi(JitPpc::R6, JitPpc::R6, -1); // R6 = C - 1 = -!C
        *p++ = JitPpc::subf(JitPpc::R3, JitPpc::R4, JitPpc::R5);
        *p++ = JitPpc::add(JitPpc::R3, JitPpc::R3, JitPpc::R6);
        emitStoreGuestReg(p, JitPpc::R3, rd);
        break;
    }
    case 0x8: // TST
        *p++ = JitPpc::and_(JitPpc::R3, JitPpc::R4, JitPpc::R5);
        emitLogicalFlags(JitPpc::R3);
        break;
    case 0x9: // TEQ
        *p++ = JitPpc::xor_(JitPpc::R3, JitPpc::R4, JitPpc::R5);
        emitLogicalFlags(JitPpc::R3);
        break;
    case 0xA: // CMP
        *p++ = JitPpc::subfc(JitPpc::R3, JitPpc::R5, JitPpc::R4);
        emitUpdateSubNZCV(p);
        break;
    case 0xB: // CMN
        *p++ = JitPpc::addc(JitPpc::R3, JitPpc::R4, JitPpc::R5);
        emitUpdateAddNZCV(p);
        break;
    case 0xC: // ORR
        *p++ = JitPpc::or_(JitPpc::R3, JitPpc::R4, JitPpc::R5);
        emitStoreGuestReg(p, JitPpc::R3, rd);
        emitLogicalFlags(JitPpc::R3);
        break;
    case 0xD: // MOV
        *p++ = JitPpc::mr(JitPpc::R3, JitPpc::R5);
        emitStoreGuestReg(p, JitPpc::R3, rd);
        emitLogicalFlags(JitPpc::R3);
        break;
    case 0xE: // BIC
        *p++ = JitPpc::andc(JitPpc::R3, JitPpc::R4, JitPpc::R5);
        emitStoreGuestReg(p, JitPpc::R3, rd);
        emitLogicalFlags(JitPpc::R3);
        break;
    case 0xF: // MVN
        *p++ = JitPpc::not_(JitPpc::R3, JitPpc::R5);
        emitStoreGuestReg(p, JitPpc::R3, rd);
        emitLogicalFlags(JitPpc::R3);
        break;
    default:
        return false;
    }

    outCycles = 1;
    return true;
}

void *ArmPpcJit::compileStub(Interpreter *cpu, uint32_t instrPC, uint32_t opcode, bool thumb) {
    ensureSpace(MAX_STUB_WORDS);
    uint32_t *start = codePool + poolWordsUsed;
    uint32_t *p = start;
    int cpuIdx = cpu->arm7 ? 1 : 0;

#if NOODS_JIT_BLOCKS
    int nativeCycles = 0;
    bool ok = thumb ? emitNativeThumb(p, cpu, (uint16_t)opcode, nativeCycles)
                    : emitNativeArm(p, cpu, opcode, nativeCycles);
    if (ok) {
        *p++ = JitPpc::li(JitPpc::R3, (int16_t)nativeCycles);
        *p++ = JitPpc::lmw(JitPpc::R28, JitPpc::R1, 16);
        *p++ = JitPpc::addi(JitPpc::R1, JitPpc::R1, 32);
        *p++ = JitPpc::blr();

        size_t words = (size_t)(p - start);
        assert(words <= MAX_STUB_WORDS);
        poolWordsUsed += words;
        stats.poolWordsUsed = poolWordsUsed;
        stats.stubCompiles[cpuIdx]++;
        stats.nativeInstrs[cpuIdx]++;
        if (!stats.firstStubAddr) {
            stats.firstStubAddr = start;
            stats.firstStubWords = words;
            stats.firstStubPc = instrPC;
            stats.firstStubOpcode = opcode;
        }
        flushCodeRange(start, words * sizeof(uint32_t));
        return start;
    }
#endif

    // Phase 1 (or Phase 2 non-native fallback) stub:
    //   r3 = r31 (Interpreter *cpu)
    //   r4 = opcode (already fetched in Interpreter::runOpcode)
    //   bctrl resolved Interpreter handler (or fallback helper)
    //   bctr  jit_exit
    *p++ = JitPpc::mr(JitPpc::R3, JitPpc::R31);
    emitLoadImm32(p, JitPpc::R4, thumb ? (uint32_t)(uint16_t)opcode : opcode);
    uintptr_t helperAddr = thumb ? resolveThumbHandler((uint16_t)opcode)
                                 : resolveArmHandler(opcode);
    emitLoadImm32(p, JitPpc::R12, (uint32_t)helperAddr);
    *p++ = JitPpc::mtctr(JitPpc::R12);
    *p++ = JitPpc::bctrl();

    uintptr_t exitAddr = (uintptr_t)&jit_exit;
    emitLoadImm32(p, JitPpc::R12, (uint32_t)exitAddr);
    *p++ = JitPpc::mtctr(JitPpc::R12);
    *p++ = JitPpc::bctr();

    size_t words = (size_t)(p - start);
    assert(words <= MAX_STUB_WORDS);
    poolWordsUsed += words;
    stats.poolWordsUsed = poolWordsUsed;
    stats.stubCompiles[cpuIdx]++;
    stats.fallbackInstrs[cpuIdx]++;
    if (!stats.firstStubAddr) {
        stats.firstStubAddr = start;
        stats.firstStubWords = words;
        stats.firstStubPc = instrPC;
        stats.firstStubOpcode = opcode;
    }
    flushCodeRange(start, words * sizeof(uint32_t));
    return start;
}

static inline bool isTerminatorBranchThumb(bool arm7, uint16_t op) {
    // Conditional branch Bcc (0xD000..0xDDFF), unconditional B (0xE000..0xE7FF),
    // BX Rs (0x4700..0x477F), BLX Rs on ARM9 (0x4780..0x47FF),
    // BL suffix (0xF800..0xFFFF), or BLX suffix on ARM9 (0xE800..0xEFFF)
    if ((op & 0xF000u) == 0xD000u && (op & 0x0F00u) < 0x0E00u) return true;
    if ((op & 0xF800u) == 0xE000u) return true;
    if ((op & 0xFF80u) == 0x4700u) return true;
    if (!arm7 && (op & 0xFF80u) == 0x4780u) return true;
    if ((op & 0xF800u) == 0xF800u) return true;
    if (!arm7 && (op & 0xF800u) == 0xE800u) return true;
    return false;
}

static inline bool isTerminatorBranchArm(uint32_t op) {
    if ((op >> 28) == 0xFu) return false;
    // B / BL (bits 27..25 == 101) or BX (0x012FFF10)
    if ((op & 0x0E000000u) == 0x0A000000u) return true;
    if ((op & 0x0FFFFFF0u) == 0x012FFF10u) return true;
    return false;
}

// Unconditional ARM B/BL (cond == AL). Eligible to START a block (branch-only block).
static inline bool isUncondBranchArm(uint32_t op) {
    return (op >> 28) == 0xEu && (op & 0x0E000000u) == 0x0A000000u;
}

static inline bool isSingleLoadThumb(uint16_t op) {
    if ((op & 0xF800u) == 0x4800u) return true; // LDR Rd, [PC, #imm8]
    if ((op & 0xF000u) == 0x5000u && (op & 0x0E00u) >= 0x0600u) return true; // LDSB/LDR/LDRH/LDRB/LDSH Rd, [Rb, Ro]
    if ((op & 0xF800u) == 0x6800u) return true; // LDR Rd, [Rb, #imm5]
    if ((op & 0xF800u) == 0x7800u) return true; // LDRB Rd, [Rb, #imm5]
    if ((op & 0xF800u) == 0x8800u) return true; // LDRH Rd, [Rb, #imm5]
    if ((op & 0xF800u) == 0x9800u) return true; // LDR Rd, [SP, #imm8]
    return false;
}

static inline bool isSingleLoadArm(uint32_t op) {
    uint8_t rd = (uint8_t)((op >> 12) & 0xF);
    if (rd == 15) return false;
    uint8_t rn = (uint8_t)((op >> 16) & 0xF);
    bool pre = (op & (1u << 24)) != 0;
    bool wb  = (op & (1u << 21)) != 0;
    if (rn == 15 && (!pre || wb)) return false;

    // LDR / LDRB (bits 27..26 == 01, L == 1)
    if ((op & 0x0C100000u) == 0x04100000u) {
        if ((op & (1u << 25)) != 0) { // Register offset
            if ((op & (1u << 4)) != 0) return false;
            if ((op & 0xFu) == 15u) return false;
        }
        return true;
    }
    // LDRH / LDRSB / LDRSH (bits 27..25 == 000, L == 1, bits 7..4 == 1011/1101/1111)
    if ((op & 0x0E100090u) == 0x00100090u && (op & 0x00000060u) != 0) {
        if ((op & (1u << 22)) == 0) { // Register offset
            if ((op & 0x00000F00u) != 0) return false;
            if ((op & 0xFu) == 15u) return false;
        }
        return true;
    }
    return false;
}

static inline bool canTranslateNativeThumb(uint16_t opcode) {
    switch (opcode >> 13) {
    case 0:
    case 1:
        return true;
    case 2:
        if ((opcode & 0xFC00u) == 0x4000u) {
            uint8_t aluOp = (uint8_t)((opcode >> 6) & 0xF);
            return (aluOp == 0x0 || aluOp == 0x1 || aluOp == 0x8 || aluOp == 0x9 || aluOp == 0xA ||
                    aluOp == 0xB || aluOp == 0xC || aluOp == 0xE || aluOp == 0xF);
        }
        if ((opcode & 0xFC00u) == 0x4400u) {
            uint8_t op = (uint8_t)((opcode >> 8) & 0x3);
            uint8_t rd = (uint8_t)(((opcode >> 4) & 0x8) | (opcode & 0x7));
            if (op == 0 && rd != 15) return true;
            if (op == 1) return true;
            if (op == 2 && rd != 15) return true;
        }
        return false;
    case 5:
        return ((opcode & 0xF800u) == 0xA000u ||
                (opcode & 0xF800u) == 0xA800u ||
                (opcode & 0xFF00u) == 0xB000u);
    case 7:
        return ((opcode & 0xF800u) == 0xF000u);
    default:
        return false;
    }
}

static inline bool canTranslateNativeArm(bool arm7, uint32_t opcode) {
    if ((opcode & 0x0C000000u) != 0x00000000u) return false;
    bool isImm = (opcode & (1u << 25)) != 0;
    if (!isImm) {
        if ((opcode & (1u << 4)) != 0) {
            if ((opcode & 0x0FFF0FF0u) == 0x016F0F10u) {
                uint8_t rd = (uint8_t)((opcode >> 12) & 0xF);
                uint8_t rm = (uint8_t)(opcode & 0xF);
                return (rd != 15 && rm != 15);
            }
            return false;
        }
        uint8_t shiftType = (uint8_t)((opcode >> 5) & 0x3);
        uint8_t shiftImm = (uint8_t)((opcode >> 7) & 0x1F);
        if (shiftType == 3 && shiftImm == 0) return false;
    }
    uint8_t rd = (uint8_t)((opcode >> 12) & 0xF);
    if (rd == 15) return false;
    uint8_t aluOp = (uint8_t)((opcode >> 21) & 0xF);
    bool setFlags = (opcode & (1u << 20)) != 0;
    if (aluOp >= 0x8 && aluOp <= 0xB && !setFlags) return false;
    if ((aluOp == 0x5 || aluOp == 0x6 || aluOp == 0x7) && setFlags) return false;
    (void)arm7;
    return true;
}

static inline uint32_t computeTailHash(const uint8_t *pcData0, uint16_t instrCount, bool thumb) {
    uint32_t h = 2166136261u;
    for (uint16_t m = 2; m < instrCount; m++) {
        uint32_t opM = thumb ? (uint32_t)U8TO16(pcData0 + (m - 2) * 2, 0)
                             : (uint32_t)U8TO32(pcData0 + (m - 2) * 4, 0);
        h = (h ^ opM) * 16777619u;
    }
    return h;
}

static inline bool instrReadsPCThumb(uint16_t op) {
    if ((op & 0xF800u) == 0xA000u) return true; // ADD Rd, PC, #imm8
    if ((op & 0xF800u) == 0xF000u) return true; // BL setup
    if ((op & 0xFC00u) == 0x4400u) {            // High register op
        uint8_t rd = (uint8_t)(((op >> 4) & 0x8) | (op & 0x7));
        uint8_t rs = (uint8_t)((op >> 3) & 0xF);
        if (rd == 15 || rs == 15) return true;
    }
    return false;
}

static inline bool instrReadsPCArm(uint32_t op) {
    if ((op & 0x0FFF0FF0u) == 0x016F0F10u) return false; // CLZ has 0xF in bits 19..16
    uint8_t aluOp = (uint8_t)((op >> 21) & 0xF);
    uint8_t rn = (uint8_t)((op >> 16) & 0xF);
    if (aluOp != 0xD && aluOp != 0xF && rn == 15) return true;
    bool isImm = (op & (1u << 25)) != 0;
    if (!isImm && (op & 0xFu) == 15) return true;
    return false;
}

// 4-bit CPSR flag mask: N=8, Z=4, C=2, V=1
static inline uint8_t thumbWrittenFlagsMask(uint16_t opcode) {
    switch (opcode >> 13) {
    case 0: {
        uint8_t subOp = (uint8_t)((opcode >> 11) & 0x3);
        uint8_t imm5  = (uint8_t)((opcode >> 6) & 0x1F);
        if (subOp == 3) return 0xFu; // ADD/SUB sets N,Z,C,V
        if (subOp == 0 && imm5 == 0) return 0xCu; // LSL #0 sets N,Z
        return 0xEu; // LSL/LSR/ASR sets N,Z,C
    }
    case 1: {
        uint8_t op = (uint8_t)((opcode >> 11) & 0x3);
        return (op == 0) ? 0xCu : 0xFu; // MOV sets N,Z; CMP/ADD/SUB sets N,Z,C,V
    }
    case 2:
        if ((opcode & 0xFC00u) == 0x4000u) {
            uint8_t aluOp = (uint8_t)((opcode >> 6) & 0xF);
            if (aluOp == 0x9 || aluOp == 0xA || aluOp == 0xB) return 0xFu; // NEG/CMP/CMN
            return 0xCu; // AND/EOR/TST/ORR/BIC/MVN
        }
        if ((opcode & 0xFC00u) == 0x4400u) {
            uint8_t op = (uint8_t)((opcode >> 8) & 0x3);
            return (op == 1) ? 0xFu : 0x0u; // High-reg CMP sets N,Z,C,V; ADD/MOV set none
        }
        return 0x0u;
    default:
        return 0x0u;
    }
}

static inline uint8_t armWrittenFlagsMask(uint32_t opcode) {
    if ((opcode & 0x0FFF0FF0u) == 0x016F0F10u) return 0x0u; // CLZ
    bool setFlags = (opcode & (1u << 20)) != 0;
    if (!setFlags) return 0x0u;
    uint8_t aluOp = (uint8_t)((opcode >> 21) & 0xF);
    if (aluOp == 0x2 || aluOp == 0x3 || aluOp == 0x4 || aluOp == 0xA || aluOp == 0xB) {
        return 0xFu; // SUBS/RSBS/ADDS/CMP/CMN set N,Z,C,V
    }
    bool isImm = (opcode & (1u << 25)) != 0;
    if (isImm) {
        uint8_t rot = (uint8_t)((opcode >> 7) & 0x1Eu);
        return rot ? 0xEu : 0xCu;
    } else {
        uint8_t shiftType = (uint8_t)((opcode >> 5) & 0x3);
        uint8_t shiftImm  = (uint8_t)((opcode >> 7) & 0x1F);
        return (shiftType == 0 && shiftImm == 0) ? 0xCu : 0xEu;
    }
}

static inline bool armReadsCarry(uint32_t opcode) {
    if ((opcode & 0x0FFF0FF0u) == 0x016F0F10u) return false;
    uint8_t aluOp = (uint8_t)((opcode >> 21) & 0xF);
    return (aluOp == 0x5 || aluOp == 0x6 || aluOp == 0x7);
}

#if NOODS_JIT_POLL_SKIP
#define BLK_N(x) ((x) & 0x7FFF)   // block instruction count (bit 15 = pure flag)
#else
#define BLK_N(x) (x)
#endif

// Purity classification for poll-loop skipping. 1 = pure (register-only), 2 = pure iff the load address is
// side-effect free (checked at runtime), 0 = impure (stores, system/coprocessor, exception return, MSR/MRS, etc.).
static inline int armPurity(uint32_t op) {
    if ((op >> 28) == 0xFu) return 0;
    if ((op & 0x0E000000u) == 0x0A000000u) return 1;             // B / BL
    if ((op & 0x0E000000u) == 0x08000000u) return 0;             // LDM / STM
    if ((op & 0x0C000000u) == 0x0C000000u) return 0;             // coprocessor / SWI
    if ((op & 0x0C000000u) == 0x04000000u) {                     // single data transfer
        if (!(op & 0x00100000u)) return 0;                       // store
        if (op & 0x02000000u) return 0;                          // register offset: not analysed
        return 2;
    }
    if ((op & 0x0E000090u) == 0x00000090u) {                     // multiply / swap / halfword transfer
        if ((op & 0x60u) == 0) {
            if ((op & 0x0FC000F0u) == 0x00000090u) return 1;     // MUL / MLA
            if ((op & 0x0F8000F0u) == 0x00800090u) return 1;     // UMULL .. SMLAL
            return 0;                                            // SWP / SWPB
        }
        if (!(op & 0x00100000u)) return 0;                       // halfword store
        if (!(op & 0x00400000u)) return 0;                       // register offset
        return 2;
    }
    uint32_t dpOp = (op >> 21) & 0xFu;                           // data processing / PSR transfer
    bool s = (op & 0x00100000u) != 0;
    if (dpOp >= 8u && dpOp <= 11u && !s) return 0;               // MRS / MSR / BX / CLZ / ...
    if (s && ((op >> 12) & 0xFu) == 15u) return 0;               // SPSR restore form
    return 1;
}

// Runtime check for class-2 loads: the address must not touch I/O with read side effects.
static inline bool armLoadAddrOk(uint32_t op, uint32_t base) {
    bool halfword = (op & 0x0E000090u) == 0x00000090u;
    uint32_t off = halfword ? (((op >> 4) & 0xF0u) | (op & 0xFu)) : (op & 0xFFFu);
    uint32_t addr = (op & 0x01000000u) ? (((op & 0x00800000u) ? base + off : base - off)) : base;
    switch (addr >> 24) {
        case 0x00: case 0x02: case 0x03: case 0x05: case 0x06: case 0x07:
            return true;
        case 0x04:
            return addr == 0x04000004u || addr == 0x04000006u || addr == 0x04000180u; // DISPSTAT, VCOUNT, IPC_SYNC
        default:
            return false;
    }
}

static inline bool armPure(uint32_t op, uint32_t base) {
    int p = armPurity(op);
    return p == 1 || (p == 2 && armLoadAddrOk(op, base));
}

bool ArmPpcJit::compileBlock(Interpreter *cpu, uint32_t instrPC, uint32_t opcode0, bool thumb, BlockEntry &entry) {
    int cpuIdx = cpu->arm7 ? 1 : 0;
    const int16_t step = thumb ? 2 : 4;
    const int16_t offR15   = (int16_t)((uintptr_t)&cpu->registersUsr[15] - (uintptr_t)cpu);
    const int16_t offPcDat = (int16_t)((uintptr_t)&cpu->pcData - (uintptr_t)cpu);
    const int16_t offPipe0 = (int16_t)((uintptr_t)&cpu->pipeline[0] - (uintptr_t)cpu);
    const int16_t offPipe1 = (int16_t)((uintptr_t)&cpu->pipeline[1] - (uintptr_t)cpu);

    // Pass 1: Scan guest instruction stream before touching codePool
    bool firstIsNative = thumb ? canTranslateNativeThumb((uint16_t)opcode0) : canTranslateNativeArm(cpu->arm7, opcode0);
    bool firstIsLoad   = !firstIsNative && (thumb ? isSingleLoadThumb((uint16_t)opcode0) : isSingleLoadArm(opcode0));
    bool firstIsBranch = !thumb && !firstIsNative && !firstIsLoad && isUncondBranchArm(opcode0);
    if (!firstIsNative && !firstIsLoad && !firstIsBranch) {
        return false;
    }

    uint32_t opcode1 = cpu->pipeline[0];
    uint32_t curPageEpoch = pageEpoch[cpuIdx][(instrPC >> 12) & (PAGE_EPOCH_SIZE - 1)];
    uint32_t r15After0 = cpu->registersUsr[15];
    uint8_t *pcData0 = cpu->pcData;

    uint32_t opList[MAX_BLOCK_INSTRS];
    opList[0] = opcode0;
    uint16_t count = 1;
    bool hasBranchTerm = false;
    uint32_t branchOpcode = 0;
    if (firstIsBranch) {
        // Branch-only block: the branch itself is the terminator; instructions after it never execute.
        hasBranchTerm = true;
        branchOpcode = opcode0;
    }

    for (uint16_t m = 1; m < (firstIsBranch ? 1 : MAX_BLOCK_INSTRS); m++) {
        // Ensure we stay strictly inside the 4 KB page so pcData never crosses a page boundary
        uint32_t simR15 = r15After0 + m * (uint32_t)step;
        if ((simR15 & 0xFFFu) < 8u || (simR15 & 0xFFFu) > 0xFA0u) break;

        uint32_t opM = (m == 1) ? opcode1
                                : (thumb ? (uint32_t)U8TO16(pcData0 + (m - 2) * 2, 0)
                                         : (uint32_t)U8TO32(pcData0 + (m - 2) * 4, 0));

        // Gate G6: Never include ARM9 CP15 instructions inside a block
        if (!thumb && !cpu->arm7 && (opM & 0x0F000010u) == 0x0E000010u) {
            break;
        }

        // In ARM mode, non-initial native instructions in a block must be unconditional (0xE)
        if (!thumb && (opM >> 28) != 0xEu) {
            if (isTerminatorBranchArm(opM)) {
                hasBranchTerm = true;
                branchOpcode = opM;
                count++;
            }
            break;
        }

        if (thumb ? canTranslateNativeThumb((uint16_t)opM) : canTranslateNativeArm(cpu->arm7, opM)) {
            opList[count++] = opM;
            continue;
        }

        if (thumb ? isTerminatorBranchThumb(cpu->arm7, (uint16_t)opM) : isTerminatorBranchArm(opM)) {
            hasBranchTerm = true;
            branchOpcode = opM;
            count++;
        }
        break;
    }

    if (count < 2 && !firstIsBranch) {
        return false;
    }

    // Pass 2: Backward CPSR flag-liveness analysis across the native prefix
    uint16_t nativeCount = hasBranchTerm ? (uint16_t)(count - 1) : count;
    uint16_t nativeStart = firstIsLoad ? 1u : 0u;
    if (nativeCount < nativeStart || (!hasBranchTerm && nativeCount <= nativeStart)) {
        return false;
    }
    bool emitFlagsList[MAX_BLOCK_INSTRS];
    uint8_t liveFlags = 0xFu; // N,Z,C,V are all considered live exiting the native sequence
    for (int m = (int)nativeCount - 1; m >= (int)nativeStart; m--) {
        uint32_t opM = opList[m];
        uint8_t w = thumb ? thumbWrittenFlagsMask((uint16_t)opM) : armWrittenFlagsMask(opM);
        emitFlagsList[m] = ((w & liveFlags) != 0);
        liveFlags &= (uint8_t)~w;
        if (!thumb && armReadsCarry(opM)) {
            liveFlags |= 0x2u;
        }
    }

    // Pass 3: Emit PowerPC code into codePool
    ensureSpace(MAX_STUB_WORDS);
    uint32_t *start = codePool + poolWordsUsed;
    uint32_t *p = start;
    uint16_t prefixCycles = firstIsLoad ? (uint16_t)(cpu->arm7 ? 3 : 1) : 0;
    int16_t appliedPcDelta = 0;

    if (firstIsLoad) {
        *p++ = JitPpc::mr(JitPpc::R3, JitPpc::R31);
        emitLoadImm32(p, JitPpc::R4, thumb ? (uint32_t)(uint16_t)opcode0 : opcode0);
        uintptr_t loadHelper = thumb ? resolveThumbHandler((uint16_t)opcode0)
                                     : resolveArmHandler(opcode0);
        emitLoadImm32(p, JitPpc::R12, (uint32_t)loadHelper);
        *p++ = JitPpc::mtctr(JitPpc::R12);
        *p++ = JitPpc::bctrl();
    }

    for (uint16_t m = nativeStart; m < nativeCount; m++) {
        uint32_t opM = opList[m];
        if (m > 0) {
            int16_t neededDelta = (int16_t)(m * step);
            bool readsPC = thumb ? instrReadsPCThumb((uint16_t)opM) : instrReadsPCArm(opM);
            if (readsPC && appliedPcDelta != neededDelta) {
                *p++ = JitPpc::lwz(JitPpc::R3, JitPpc::R31, offR15);
                *p++ = JitPpc::addi(JitPpc::R3, JitPpc::R3, (int16_t)(neededDelta - appliedPcDelta));
                *p++ = JitPpc::stw(JitPpc::R3, JitPpc::R31, offR15);
                appliedPcDelta = neededDelta;
            }
        }
        int cm = 0;
        bool okM = thumb ? emitNativeThumb(p, cpu, (uint16_t)opM, cm, emitFlagsList[m])
                         : emitNativeArm(p, cpu, opM, cm, emitFlagsList[m]);
        assert(okM);
        (void)okM;
        prefixCycles = (uint16_t)(prefixCycles + cm);
    }

    int16_t totalDelta = (int16_t)((count - 1) * step);
    int16_t remDelta = (int16_t)(totalDelta - appliedPcDelta);

    if (hasBranchTerm) {
        // Tail-call execBranchThumb / execBranchArm directly without touching LR or syncing dead pipeline state on taken branches
        *p++ = JitPpc::mr(JitPpc::R3, JitPpc::R31);
        emitLoadImm32(p, JitPpc::R4, branchOpcode);
        *p++ = JitPpc::li(JitPpc::R5, remDelta);
        *p++ = JitPpc::li(JitPpc::R6, totalDelta);
        *p++ = JitPpc::li(JitPpc::R7, (int16_t)prefixCycles);
        if (firstIsLoad) {
            *p++ = JitPpc::lwz(JitPpc::R0, JitPpc::R1, 36);
            *p++ = JitPpc::mtlr(JitPpc::R0);
        }
        uintptr_t helperAddr = (uintptr_t)(thumb ? (void *)&ArmPpcJit::execBranchThumb
                                                 : (void *)&ArmPpcJit::execBranchArm);
        emitLoadImm32(p, JitPpc::R12, (uint32_t)helperAddr);
        *p++ = JitPpc::mtctr(JitPpc::R12);
        *p++ = JitPpc::lmw(JitPpc::R28, JitPpc::R1, 16);
        *p++ = JitPpc::addi(JitPpc::R1, JitPpc::R1, 32);
        *p++ = JitPpc::bctr();
    } else {
        // Pure native block: synchronize R15, pcData, pipeline[0], pipeline[1] and return prefixCycles directly
        if (remDelta != 0) {
            *p++ = JitPpc::lwz(JitPpc::R3, JitPpc::R31, offR15);
            *p++ = JitPpc::addi(JitPpc::R3, JitPpc::R3, remDelta);
            *p++ = JitPpc::stw(JitPpc::R3, JitPpc::R31, offR15);
        }
        *p++ = JitPpc::lwz(JitPpc::R4, JitPpc::R31, offPcDat);
        *p++ = JitPpc::addi(JitPpc::R4, JitPpc::R4, totalDelta);
        *p++ = JitPpc::stw(JitPpc::R4, JitPpc::R31, offPcDat);
        *p++ = JitPpc::addi(JitPpc::R5, JitPpc::R4, (int16_t)(-step));
        if (thumb) {
            *p++ = JitPpc::lhbrx(JitPpc::R6, 0, JitPpc::R5);
            *p++ = JitPpc::lhbrx(JitPpc::R7, 0, JitPpc::R4);
        } else {
            *p++ = JitPpc::lwbrx(JitPpc::R6, 0, JitPpc::R5);
            *p++ = JitPpc::lwbrx(JitPpc::R7, 0, JitPpc::R4);
        }
        *p++ = JitPpc::stw(JitPpc::R6, JitPpc::R31, offPipe0);
        *p++ = JitPpc::stw(JitPpc::R7, JitPpc::R31, offPipe1);
        *p++ = JitPpc::li(JitPpc::R3, (int16_t)prefixCycles);
        if (firstIsLoad) {
            *p++ = JitPpc::lwz(JitPpc::R0, JitPpc::R1, 36);
            *p++ = JitPpc::mtlr(JitPpc::R0);
        }
        *p++ = JitPpc::lmw(JitPpc::R28, JitPpc::R1, 16);
        *p++ = JitPpc::addi(JitPpc::R1, JitPpc::R1, 32);
        *p++ = JitPpc::blr();
    }

    size_t words = (size_t)(p - start);
    assert(words <= MAX_STUB_WORDS);
    poolWordsUsed += words;
    stats.poolWordsUsed = poolWordsUsed;
    stats.blockCompiles[cpuIdx]++;
    stats.nativeInstrs[cpuIdx] += (nativeCount - nativeStart);
    if (firstIsLoad) stats.fallbackInstrs[cpuIdx]++;
    if (hasBranchTerm) stats.fallbackInstrs[cpuIdx]++;

    if (!stats.firstBlockAddr) {
        stats.firstBlockAddr = start;
        stats.firstBlockWords = words;
        stats.firstBlockPc = instrPC;
    }
    flushCodeRange(start, words * sizeof(uint32_t));

    size_t pageIdx = (instrPC >> 12) & (PAGE_EPOCH_SIZE - 1);
    pageHasBlockBits[cpuIdx][pageIdx >> 5] |= (1u << (pageIdx & 31));

    entry.pc = instrPC;
    entry.opcode0 = opcode0;
    entry.opcode1 = opcode1;
    entry.tailHash = computeTailHash(pcData0, count, thumb);
    entry.epochAndFlags = (currentEpoch << 2) | (thumb ? 3u : 1u);
    entry.pageEpoch = curPageEpoch;
    bool blockPure = false;
#if NOODS_JIT_POLL_SKIP
    if (!thumb) {
        blockPure = !hasBranchTerm || armPurity(branchOpcode) == 1;
        for (uint16_t m = 0; blockPure && m < nativeCount; m++) {
            int pm = armPurity(opList[m]);
            blockPure = (m == 0) ? (pm == 1 || pm == 2) : (pm == 1);
        }
    }
#endif
    entry.instrCount = (uint16_t)(count | (blockPure ? 0x8000u : 0u));
    entry.prefixCycles = hasBranchTerm ? prefixCycles : (uint16_t)(prefixCycles - 1);
    entry.code = start;
    return true;
}

template <bool Track>
FORCE_INLINE int ArmPpcJit::tryExecuteBlock(Interpreter *cpu, uint32_t instrPC, uint32_t opcode, bool thumb, bool &outIsNative0, bool *outPure) {
    if (Track && outPure) *outPure = false;
    if (!cpu->pcData) {
        outIsNative0 = thumb ? canTranslateNativeThumb((uint16_t)opcode) : canTranslateNativeArm(cpu->arm7, opcode);
        return 0;
    }

    int cpuIdx = cpu->arm7 ? 1 : 0;
    size_t bIdx = ((instrPC >> (thumb ? 1 : 2)) ^ (instrPC >> 11)) & (BLOCK_TABLE_SIZE - 1);
    BlockEntry &bEntry = blockTable[cpuIdx][bIdx];
    uint32_t opcode1 = cpu->pipeline[0];
    uint32_t curPageEpoch = pageEpoch[cpuIdx][(instrPC >> 12) & (PAGE_EPOCH_SIZE - 1)];
    uint32_t expectedFlags = (currentEpoch << 2) | (thumb ? 3u : 1u);

    bool hit = (bEntry.pc == instrPC &&
                bEntry.opcode0 == opcode &&
                bEntry.opcode1 == opcode1 &&
                bEntry.epochAndFlags == expectedFlags);

    if (__builtin_expect(hit, 1)) {
        if (__builtin_expect(bEntry.pageEpoch != curPageEpoch, 0)) {
            // Check if actual instruction words I_2..I_{K-1} changed or if the write was to stack/data on the same page
            if (BLK_N(bEntry.instrCount) <= 2 || computeTailHash(cpu->pcData, BLK_N(bEntry.instrCount), thumb) == bEntry.tailHash) {
                bEntry.pageEpoch = curPageEpoch;
            } else {
                hit = false;
            }
        }
    }

    if (__builtin_expect(!hit, 0)) {
        bool firstIsNative = thumb ? canTranslateNativeThumb((uint16_t)opcode) : canTranslateNativeArm(cpu->arm7, opcode);
        outIsNative0 = firstIsNative;
        bool firstIsLoad   = !firstIsNative && (thumb ? isSingleLoadThumb((uint16_t)opcode) : isSingleLoadArm(opcode));
        bool firstIsBranch = !thumb && !firstIsNative && !firstIsLoad && isUncondBranchArm(opcode);
        if (!firstIsNative && !firstIsLoad && !firstIsBranch) return 0;

        uint32_t r15After0 = cpu->registersUsr[15];
        if ((r15After0 & 0xFFFu) < 8u || (r15After0 & 0xFFFu) > 0xF80u) return 0;

        if (thumb) {
            if (!canTranslateNativeThumb((uint16_t)opcode1) && !isTerminatorBranchThumb(cpu->arm7, (uint16_t)opcode1)) return 0;
        } else if (!firstIsBranch) {
            // A branch-first block never executes I_1, so its successor is irrelevant
            if (((opcode1 >> 28) != 0xEu || !canTranslateNativeArm(cpu->arm7, opcode1)) && !isTerminatorBranchArm(opcode1)) return 0;
        }

        if (!compileBlock(cpu, instrPC, opcode, thumb, bEntry)) {
            stats.blockCompileFails[cpuIdx]++;
            return 0;
        }
    }

    // Verify scheduler cycle budget so block never crosses a scheduled hardware event boundary
    Core *core = cpu->core;
    uint32_t nextEventCycles = core->events[0].cycles;
    uint32_t shift = (cpu->arm7 && !core->gbaMode) ? 1u : 0u;
    uint32_t scaledPrefix = (uint32_t)bEntry.prefixCycles << shift;
    if (__builtin_expect(cpu->cycles + scaledPrefix >= nextEventCycles, 0)) {
        outIsNative0 = thumb ? canTranslateNativeThumb((uint16_t)opcode) : canTranslateNativeArm(cpu->arm7, opcode);
        return 0;
    }

    stats.blockHits[cpuIdx]++;
    stats.blockInstrsExecuted[cpuIdx] += BLK_N(bEntry.instrCount);
    PROF_MARK(thumb ? 0 : (isSingleLoadArm(opcode) ? 8 : 0));
    if (Track && outPure) {
        int p = thumb ? 0 : armPurity(opcode);
        *outPure = (bEntry.instrCount & 0x8000) && (p == 1 || (p == 2 && armLoadAddrOk(opcode, cpu->registersUsr[(opcode >> 16) & 0xFu])));
    }
    return jit_enter(bEntry.code, cpu);
}

int ArmPpcJit::executeArm(Interpreter *cpu, uint32_t opcode) {
#if NOODS_JIT_POLL_SKIP
    return pollExecArm(cpu, opcode);
#else
    return executeArmCore<false>(cpu, opcode, nullptr);
#endif
}

template <bool Track>
int ArmPpcJit::executeArmCore(Interpreter *cpu, uint32_t opcode, bool *outPure) {
    if (Track && outPure) *outPure = false;
    PROF_GUARD(cpu->arm7, opcode, false);
    if (!initialized) init();
    int cpuIdx = cpu->arm7 ? 1 : 0;
    stats.dispatches[cpuIdx]++;

#if NOODS_JIT_BLOCKS
    // Instructions with bit 27 == 1 (B/BL, LDM/STM, SWI, Coprocessor/CP15) or Rd == 15 never start a block
    const bool uncondBranch = isUncondBranchArm(opcode);
    if ((opcode & 0x08000000u) != 0 && !uncondBranch) {
        if (!cpu->arm7 && (opcode & 0x0F000010u) == 0x0E000010u) {
            stats.cp15Exits[0]++;
        } else {
            stats.fallbackInstrs[cpuIdx]++;
        }
        if (cpuIdx == 1) { stats.fbOpc7[(opcode >> 20) & 0xFF]++; stats.fbLast7[(opcode >> 20) & 0xFF] = opcode; }
        { if (Track && outPure) *outPure = armPure(opcode, cpu->registersUsr[(opcode >> 16) & 0xFu]); return fallbackArm(cpu, opcode); }
    }
    if ((opcode & 0x0000F000u) == 0x0000F000u && !uncondBranch) {
        stats.fallbackInstrs[cpuIdx]++;
        stats.fbClassArm[cpuIdx][(opcode >> 25) & 7]++;
        if (cpuIdx == 1) { stats.fbOpc7[(opcode >> 20) & 0xFF]++; stats.fbLast7[(opcode >> 20) & 0xFF] = opcode; }
        { if (Track && outPure) *outPure = armPure(opcode, cpu->registersUsr[(opcode >> 16) & 0xFu]); return fallbackArm(cpu, opcode); }
    }
#endif

    uint32_t instrPC = cpu->registersUsr[15] - 8;

#if NOODS_JIT_BLOCKS
    bool isNative0 = false;
    if (int blockCycles = tryExecuteBlock<Track>(cpu, instrPC, opcode, false, isNative0, outPure)) {
        return blockCycles;
    }
    if (!isNative0) {
        stats.fallbackInstrs[cpuIdx]++;
        stats.fbClassArm[cpuIdx][(opcode >> 25) & 7]++;
        if (cpuIdx == 1) { stats.fbOpc7[(opcode >> 20) & 0xFF]++; stats.fbLast7[(opcode >> 20) & 0xFF] = opcode; }
        { if (Track && outPure) *outPure = armPure(opcode, cpu->registersUsr[(opcode >> 16) & 0xFu]); return fallbackArm(cpu, opcode); }
    }
#endif

    size_t idx = ((instrPC >> 2) ^ (instrPC >> 11)) & (STUB_TABLE_SIZE - 1);
    StubEntry &entry = stubTable[cpuIdx][idx];
    uint32_t expectedFlags = (currentEpoch << 2) | 1u;

    if (entry.pc == instrPC && entry.opcode == opcode && entry.epochAndFlags == expectedFlags && entry.code) {
        stats.stubHits[cpuIdx]++;
    } else {
        void *code = compileStub(cpu, instrPC, opcode, false);
        expectedFlags = (currentEpoch << 2) | 1u; // Re-read in case compileStub wrapped the pool
        entry.pc = instrPC;
        entry.opcode = opcode;
        entry.epochAndFlags = expectedFlags;
        entry.code = code;
    }

    if (Track && outPure) *outPure = armPure(opcode, cpu->registersUsr[(opcode >> 16) & 0xFu]);
    PROF_MARK(1);
    return jit_enter(entry.code, cpu);
}

int ArmPpcJit::executeThumb(Interpreter *cpu, uint32_t opcode) {
#if NOODS_JIT_POLL_SKIP
    pollEpoch++;
#endif
    PROF_GUARD(cpu->arm7, opcode, true);
    if (!initialized) init();
    int cpuIdx = cpu->arm7 ? 1 : 0;
    stats.dispatches[cpuIdx]++;

    uint32_t instrPC = cpu->registersUsr[15] - 4;

#if NOODS_JIT_BLOCKS
    bool isNative0 = false;
    if (int blockCycles = tryExecuteBlock(cpu, instrPC, opcode, true, isNative0)) {
        return blockCycles;
    }
    if (!isNative0) {
        stats.fallbackInstrs[cpuIdx]++;
        stats.fbClassThumb[cpuIdx][(opcode >> 13) & 7]++;
        return fallbackThumb(cpu, opcode);
    }
#endif

    size_t idx = ((instrPC >> 1) ^ (instrPC >> 11) ^ 0x1357u) & (STUB_TABLE_SIZE - 1);
    StubEntry &entry = stubTable[cpuIdx][idx];
    uint32_t expectedFlags = (currentEpoch << 2) | 3u;

    if (entry.pc == instrPC && entry.opcode == opcode && entry.epochAndFlags == expectedFlags && entry.code) {
        stats.stubHits[cpuIdx]++;
    } else {
        void *code = compileStub(cpu, instrPC, opcode, true);
        expectedFlags = (currentEpoch << 2) | 3u;
        entry.pc = instrPC;
        entry.opcode = opcode;
        entry.epochAndFlags = expectedFlags;
        entry.code = code;
    }

    PROF_MARK(1);
    return jit_enter(entry.code, cpu);
}

void ArmPpcJit::invalidateAddr(bool arm7, uint32_t addr) {
    if (!initialized) return;
    int cpuIdx = arm7 ? 1 : 0;
    size_t pageIdx = (addr >> 12) & (PAGE_EPOCH_SIZE - 1);
    stats.smcInvalidations[cpuIdx]++;
    if (pageHasBlockBits[cpuIdx][pageIdx >> 5] & (1u << (pageIdx & 31))) {
        pageEpoch[cpuIdx][pageIdx]++;
        if (pageEpoch[cpuIdx][pageIdx] == 0) pageEpoch[cpuIdx][pageIdx] = 1;
    }
}

void ArmPpcJit::invalidateSharedAddr(uint32_t addr) {
    if (!initialized) return;
    size_t pageIdx = (addr >> 12) & (PAGE_EPOCH_SIZE - 1);
    size_t wordIdx = pageIdx >> 5;
    uint32_t bitMask = 1u << (pageIdx & 31);
    for (int c = 0; c < 2; c++) {
        stats.smcInvalidations[c]++;
        if (pageHasBlockBits[c][wordIdx] & bitMask) {
            pageEpoch[c][pageIdx]++;
            if (pageEpoch[c][pageIdx] == 0) pageEpoch[c][pageIdx] = 1;
        }
    }
}

void ArmPpcJit::invalidateRange(bool arm7, uint32_t startAddr, uint32_t endAddr) {
    if (!initialized) return;
    uint32_t startPage = startAddr >> 12;
    uint32_t endPage = endAddr >> 12;
    if (endPage - startPage > 64) {
        invalidateCpu(arm7);
        return;
    }
    for (uint32_t p = startPage; p <= endPage; p++) {
        invalidateSharedAddr(p << 12);
    }
}

void ArmPpcJit::invalidateCpu(bool arm7) {
    if (!initialized) return;
    int cpuIdx = arm7 ? 1 : 0;
    for (size_t i = 0; i < PAGE_EPOCH_SIZE; i++) {
        pageEpoch[cpuIdx][i]++;
        if (pageEpoch[cpuIdx][i] == 0) pageEpoch[cpuIdx][i] = 1;
    }
    std::memset(blockTable[cpuIdx], 0, sizeof(blockTable[cpuIdx]));
    std::memset(pageHasBlockBits[cpuIdx], 0, sizeof(pageHasBlockBits[cpuIdx]));
    stats.mapInvalidations[cpuIdx]++;
}

void ArmPpcJit::invalidateAll() {
    if (!initialized) return;
    invalidateCpu(false);
    invalidateCpu(true);
}


#if NOODS_JIT_POLL_SKIP
uint32_t ArmPpcJit::pollEpoch = 0;
ArmPpcJit::PollTrack ArmPpcJit::pollTrack[2];

void ArmPpcJit::pollSnapshot(PollTrack &t, Interpreter *cpu, uint32_t S) {
    for (int i = 0; i < 16; i++) t.snap[i] = cpu->registersUsr[i];
    t.snap[16] = cpu->cpsr;
    t.snap[17] = cpu->pipeline[0];
    t.snap[18] = cpu->pipeline[1];
    t.snapStart = S;
    t.snapEpoch = pollEpoch;
}

// Fixed-point poll-loop skipping.
//
// A CPU is "looping" when it arrives at the same head PC twice with an identical post-fetch state (registers,
// CPSR, pipeline) and no impure dispatch or event happened in between (pollEpoch unchanged). The iteration
// between two arrivals is then deterministic: every input is register state (identical), or memory (unchanged,
// since memory only changes via impure dispatches or events). So every further iteration repeats exactly, and
// its duration is the measured period.
//
// A joint skip is applied at a closing dispatch (post-dispatch PC = head+4) when the other CPU is also looping
// (or halted) under the same epoch. Both CPUs are then advanced by whole iterations that end at or before the
// next scheduled event. Their registers do not change, and nothing else can change before that event, so cycle
// counts and state match stepping one dispatch at a time. The skip never crosses an event, so partial iterations
// run normally.
int ArmPpcJit::pollExecArm(Interpreter *cpu, uint32_t opcode) {
    Core *core = cpu->core;
    if (core->gbaMode || core->dsiMode) return executeArmCore<false>(cpu, opcode, nullptr);

    const int ci = cpu->arm7 ? 1 : 0;
    PollTrack &t = pollTrack[ci];
    const uint32_t P = cpu->registersUsr[15] - 8u;
    const uint32_t S = core->globalCycles;

    if (t.armed && P == t.head) {
        bool same = (t.snapEpoch == pollEpoch) &&
                    cpu->cpsr == t.snap[16] && cpu->pipeline[0] == t.snap[17] && cpu->pipeline[1] == t.snap[18];
        for (int i = 0; same && i < 16; i++) same = (cpu->registersUsr[i] == t.snap[i]);
        if (same && S > t.snapStart) {
            t.period = S - t.snapStart;
            t.valid = true;
            t.validEpoch = pollEpoch;
        } else {
            t.valid = false;
        }
        pollSnapshot(t, cpu, S);
    } else if (P <= t.prevPC) {
        // A backward or self branch: treat its target as the head of a candidate loop
        t.armed = true;
        t.head = P;
        t.valid = false;
        pollSnapshot(t, cpu, S);
    }
    t.prevPC = P;

    bool pure = false;
    int r = executeArmCore<true>(cpu, opcode, &pure);
    if (!pure) pollEpoch++;

    if (t.valid && t.validEpoch == pollEpoch && r > 0 && cpu->registersUsr[15] == t.head + 4u)
        r = pollJointSkip(cpu, S, r, ci);
    return r;
}

int ArmPpcJit::pollJointSkip(Interpreter *cpu, uint32_t S, int r, int ci) {
    Core *core = cpu->core;
    const int shift = ci ? 1 : 0; // NDS mode only (GBA and DSi excluded by the caller)
    PollTrack &tc = pollTrack[ci];
    Interpreter &o = core->interpreter[ci ^ 1];
    PollTrack &to = pollTrack[ci ^ 1];

    // The other CPU must be halted or looping under the current epoch
    if (!o.halted && !(to.armed && to.valid && to.validEpoch == pollEpoch)) return r;
    if (!tc.period || (!o.halted && !to.period)) return r;

    const uint32_t E = core->events[0].cycles;

    // This CPU: its next iteration starts at tC; skip whole iterations that end at or before E
    const uint32_t tC = S + ((uint32_t)r << shift);
    uint32_t nC = (E > tC) ? (E - tC) / tc.period : 0;
    uint32_t extra = nC * tc.period;
    if (nC && !(shift && (extra & 1u))) {
        tc.snapStart = tC + extra - tc.period;
        r += (int)(extra >> shift);
        stats.pollSkips[ci]++;
        stats.pollSkipIters[ci] += nC;
    }

    // Other CPU: parked at its own head; advance its clock directly (its state is unchanged)
    if (!o.halted) {
        uint32_t tO = o.cycles;
        uint32_t nO = (E > tO) ? (E - tO) / to.period : 0;
        uint32_t addO = nO * to.period;
        if (nO && !((ci ^ 1) && (addO & 1u))) {
            o.cycles = tO + addO;
            to.snapStart = o.cycles - to.period;
            stats.pollSkips[ci ^ 1]++;
            stats.pollSkipIters[ci ^ 1] += nO;
        }
    }
    return r;
}
#endif

void ArmPpcJit::dumpStats(FILE *f) {
    if (!f) return;
    fprintf(f, "JIT_POLL cpu=9 skips=%u iters=%llu cpu=7 skips=%u iters=%llu\n", (unsigned)stats.pollSkips[0], (unsigned long long)stats.pollSkipIters[0], (unsigned)stats.pollSkips[1], (unsigned long long)stats.pollSkipIters[1]);
#if NOODS_JIT_PROFILE
    for (int c = 0; c < 2; c++) {
        for (int b = 0; b < 16; b++) {
            if (!s_profCount[c][b]) continue;
            fprintf(f, "JIT_PROF cpu=%d bucket=%s count=%llu ticks=%llu us=%llu\n",
                    c == 0 ? 9 : 7, s_profNames[b],
                    (unsigned long long)s_profCount[c][b],
                    (unsigned long long)s_profTicks[c][b],
                    (unsigned long long)PPCTicksToUs(s_profTicks[c][b]));
        }
    }
#endif
    fprintf(f,
            "JIT_STATS enabled=1 blocks=%d "
            "disp9=%llu disp7=%llu "
            "stubComp9=%llu stubComp7=%llu "
            "stubHit9=%llu stubHit7=%llu "
            "blkComp9=%llu blkComp7=%llu "
            "blkHit9=%llu blkHit7=%llu "
            "blkInsn9=%llu blkInsn7=%llu "
            "natInsn9=%llu natInsn7=%llu "
            "fbInsn9=%llu fbInsn7=%llu "
            "cp15Exit9=%llu "
            "smcInv9=%llu smcInv7=%llu "
            "mapInv9=%llu mapInv7=%llu "
            "poolUsed=%zu poolLimit=%zu poolWraps=%llu "
            "poolAddr=%p "
            "blkFail7=%llu blkFail9=%llu "
            "fbArm7=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu "
            "fbThumb7=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
            (int)NOODS_JIT_BLOCKS,
            (unsigned long long)stats.dispatches[0],
            (unsigned long long)stats.dispatches[1],
            (unsigned long long)stats.stubCompiles[0],
            (unsigned long long)stats.stubCompiles[1],
            (unsigned long long)stats.stubHits[0],
            (unsigned long long)stats.stubHits[1],
            (unsigned long long)stats.blockCompiles[0],
            (unsigned long long)stats.blockCompiles[1],
            (unsigned long long)stats.blockHits[0],
            (unsigned long long)stats.blockHits[1],
            (unsigned long long)stats.blockInstrsExecuted[0],
            (unsigned long long)stats.blockInstrsExecuted[1],
            (unsigned long long)stats.nativeInstrs[0],
            (unsigned long long)stats.nativeInstrs[1],
            (unsigned long long)stats.fallbackInstrs[0],
            (unsigned long long)stats.fallbackInstrs[1],
            (unsigned long long)stats.cp15Exits[0],
            (unsigned long long)stats.smcInvalidations[0],
            (unsigned long long)stats.smcInvalidations[1],
            (unsigned long long)stats.mapInvalidations[0],
            (unsigned long long)stats.mapInvalidations[1],
            stats.poolWordsUsed,
            stats.poolLimitWords,
            (unsigned long long)stats.poolWraps,
            (void *)codePool,
            (unsigned long long)stats.blockCompileFails[1],
            (unsigned long long)stats.blockCompileFails[0],
            (unsigned long long)stats.fbClassArm[1][0],
            (unsigned long long)stats.fbClassArm[1][1],
            (unsigned long long)stats.fbClassArm[1][2],
            (unsigned long long)stats.fbClassArm[1][3],
            (unsigned long long)stats.fbClassArm[1][4],
            (unsigned long long)stats.fbClassArm[1][5],
            (unsigned long long)stats.fbClassArm[1][6],
            (unsigned long long)stats.fbClassArm[1][7],
            (unsigned long long)stats.fbClassThumb[1][0],
            (unsigned long long)stats.fbClassThumb[1][1],
            (unsigned long long)stats.fbClassThumb[1][2],
            (unsigned long long)stats.fbClassThumb[1][3],
            (unsigned long long)stats.fbClassThumb[1][4],
            (unsigned long long)stats.fbClassThumb[1][5],
            (unsigned long long)stats.fbClassThumb[1][6],
            (unsigned long long)stats.fbClassThumb[1][7]);

    for (unsigned i = 0; i < 256; i++) {
        if (stats.fbOpc7[i] > 1000) fprintf(f, "JIT_FBOPC7 bits27_20=%02X count=%u last=%08X\n", i, stats.fbOpc7[i], stats.fbLast7[i]);
    }

    if (stats.firstStubAddr && stats.firstStubWords > 0) {
        fprintf(f, "JIT_FIRST_STUB addr=%p guest_pc=%08X opcode=%08X words=%zu:",
                (const void *)stats.firstStubAddr, stats.firstStubPc, stats.firstStubOpcode, stats.firstStubWords);
        for (size_t i = 0; i < stats.firstStubWords; i++) {
            fprintf(f, " %08X", stats.firstStubAddr[i]);
        }
        fprintf(f, "\n");
    }
    if (stats.firstBlockAddr && stats.firstBlockWords > 0) {
        fprintf(f, "JIT_FIRST_BLOCK addr=%p guest_pc=%08X words=%zu:",
                (const void *)stats.firstBlockAddr, stats.firstBlockPc, stats.firstBlockWords);
        for (size_t i = 0; i < stats.firstBlockWords; i++) {
            fprintf(f, " %08X", stats.firstBlockAddr[i]);
        }
        fprintf(f, "\n");
    }
}
