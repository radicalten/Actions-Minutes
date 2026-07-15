//interpreter_branch.cpp (optimized for PowerPC/Wii)
#include "core.h"

int Interpreter::bx(uint32_t opcode) {
    uint32_t op0 = *registers[opcode & 0xF];
    // Set THUMB bit if target bit 0 set
    cpsr = (cpsr & ~BIT(5)) | ((op0 & 1) << 5);
    *registers[15] = op0 & ~1U;
    flushPipeline();
    return 3;
}

int Interpreter::blxReg(uint32_t opcode) {
    if (PPC_UNLIKELY(arm7)) return 1;
    uint32_t op0 = *registers[opcode & 0xF];
    cpsr = (cpsr & ~BIT(5)) | ((op0 & 1) << 5);
    *registers[14] = *registers[15] - 4;
    *registers[15] = op0 & ~1U;
    flushPipeline();
    return 3;
}

int Interpreter::b(uint32_t opcode) {
    int32_t op0 = (int32_t)(opcode << 8) >> 6;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bl(uint32_t opcode) {
    int32_t op0 = (int32_t)(opcode << 8) >> 6;
    *registers[14] = *registers[15] - 4;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::blx(uint32_t opcode) {
    if (PPC_UNLIKELY(arm7)) return 1;
    int32_t op0 = ((int32_t)(opcode << 8) >> 6) | ((opcode & BIT(24)) >> 23);
    cpsr |= BIT(5);
    *registers[14] = *registers[15] - 4;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

COLD int Interpreter::swi(uint32_t opcode) {
    LOG_INFO("Triggering ARM%d software interrupt: 0x%X\n", arm7 ? 7 : 9, opcode & 0xFFFFFF);
    *registers[15] -= 4;
    return exception(0x08);
}

int Interpreter::bxRegT(uint16_t opcode) {
    uint32_t op0 = *registers[(opcode >> 3) & 0xF];
    // Clear THUMB if target bit 0 clear
    cpsr = (cpsr & ~BIT(5)) | ((op0 & 1) << 5);
    *registers[15] = op0;
    flushPipeline();
    return 3;
}

int Interpreter::blxRegT(uint16_t opcode) {
    if (PPC_UNLIKELY(arm7)) return 1;
    uint32_t op0 = *registers[(opcode >> 3) & 0xF];
    cpsr = (cpsr & ~BIT(5)) | ((op0 & 1) << 5);
    *registers[14] = *registers[15] - 1;
    *registers[15] = op0;
    flushPipeline();
    return 3;
}

// Optimized conditional branches - use PPC branch prediction hints
// Most branches are taken in tight loops, so we hint "taken"

int Interpreter::beqT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    if (PPC_UNLIKELY(!(cpsr & BIT(30)))) return 1; // Z clear = not equal
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bneT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    if (PPC_UNLIKELY(cpsr & BIT(30))) return 1; // Z set = equal
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bcsT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    if (PPC_UNLIKELY(!(cpsr & BIT(29)))) return 1;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bccT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    if (PPC_UNLIKELY(cpsr & BIT(29))) return 1;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bmiT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    if (PPC_UNLIKELY(!(cpsr & BIT(31)))) return 1;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bplT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    if (PPC_UNLIKELY(cpsr & BIT(31))) return 1;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bvsT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    if (PPC_UNLIKELY(!(cpsr & BIT(28)))) return 1;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bvcT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    if (PPC_UNLIKELY(cpsr & BIT(28))) return 1;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bhiT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    // C set and Z clear
    if (PPC_UNLIKELY((cpsr & 0x60000000) != 0x20000000)) return 1;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::blsT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    // C clear or Z set
    if (PPC_UNLIKELY((cpsr & 0x60000000) == 0x20000000)) return 1;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bgeT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    // N == V: (cpsr ^ (cpsr<<3)) bit31 == 0
    if (PPC_UNLIKELY((cpsr ^ (cpsr << 3)) & BIT(31))) return 1;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bltT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    // N != V
    if (PPC_UNLIKELY(!((cpsr ^ (cpsr << 3)) & BIT(31)))) return 1;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bgtT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    // Z==0 and N==V
    if (PPC_UNLIKELY(((cpsr ^ (cpsr << 3)) | (cpsr << 1)) & BIT(31))) return 1;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bleT(uint16_t opcode) {
    int32_t op0 = (int8_t)opcode << 1;
    // Z==1 or N!=V
    if (PPC_UNLIKELY(!(((cpsr ^ (cpsr << 3)) | (cpsr << 1)) & BIT(31)))) return 1;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::bT(uint16_t opcode) {
    int32_t op0 = (int16_t)(opcode << 5) >> 4;
    *registers[15] += op0;
    flushPipeline();
    return 3;
}

int Interpreter::blSetupT(uint16_t opcode) {
    int32_t op0 = (int16_t)(opcode << 5) >> 4;
    *registers[14] = *registers[15] + (op0 << 11);
    return 1;
}

int Interpreter::blOffT(uint16_t opcode) {
    uint32_t op0 = (opcode & 0x7FF) << 1;
    uint32_t ret = *registers[15] - 1;
    *registers[15] = *registers[14] + op0;
    *registers[14] = ret;
    flushPipeline();
    return 3;
}

int Interpreter::blxOffT(uint16_t opcode) {
    if (PPC_UNLIKELY(arm7)) return 1;
    uint32_t op0 = (opcode & 0x7FF) << 1;
    cpsr &= ~BIT(5);
    uint32_t ret = *registers[15] - 1;
    *registers[15] = *registers[14] + op0;
    *registers[14] = ret;
    flushPipeline();
    return 3;
}

COLD int Interpreter::swiT(uint16_t opcode) {
    LOG_INFO("Triggering ARM%d software interrupt: 0x%X\n", arm7 ? 7 : 9, opcode & 0xFF);
    *registers[15] -= 4;
    return exception(0x08);
}
