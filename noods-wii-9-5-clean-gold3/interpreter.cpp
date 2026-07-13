//interpreter.cpp (optimized)
#include "core.h"
#include <tuxedo/thread.h>

// PowerPC cache line prefetch helper
#define PPC_PREFETCH(addr) __asm__ volatile("dcbt 0,%0" : : "r"(addr))

Interpreter::Interpreter(Core *core, bool arm7): core(core), arm7(arm7) {
    // Initialize the registers for user mode
    // Unrolled for speed on PowerPC
    for (int i = 0; i < 32; i++)
        registers[i] = &registersUsr[i & 0xF];
}

void Interpreter::saveState(FILE *file) {
    fwrite(pipeline,      4, sizeof(pipeline) / 4,      file);
    fwrite(registersUsr,  4, sizeof(registersUsr) / 4,  file);
    fwrite(registersFiq,  4, sizeof(registersFiq) / 4,  file);
    fwrite(registersSvc,  4, sizeof(registersSvc) / 4,  file);
    fwrite(registersAbt,  4, sizeof(registersAbt) / 4,  file);
    fwrite(registersIrq,  4, sizeof(registersIrq) / 4,  file);
    fwrite(registersUnd,  4, sizeof(registersUnd) / 4,  file);
    fwrite(&cpsr,     sizeof(cpsr),     1, file);
    fwrite(&spsrFiq,  sizeof(spsrFiq),  1, file);
    fwrite(&spsrSvc,  sizeof(spsrSvc),  1, file);
    fwrite(&spsrAbt,  sizeof(spsrAbt),  1, file);
    fwrite(&spsrIrq,  sizeof(spsrIrq),  1, file);
    fwrite(&spsrUnd,  sizeof(spsrUnd),  1, file);
    fwrite(&cycles,   sizeof(cycles),   1, file);
    fwrite(&halted,   sizeof(halted),   1, file);
    fwrite(&dsiCycle, sizeof(dsiCycle), 1, file);
    fwrite(&ime,      sizeof(ime),      1, file);
    fwrite(&ie,       sizeof(ie),       1, file);
    fwrite(&irf,      sizeof(irf),      1, file);
    fwrite(&postFlg,  sizeof(postFlg),  1, file);
}

void Interpreter::loadState(FILE *file) {
    fread(pipeline,      4, sizeof(pipeline) / 4,      file);
    fread(registersUsr,  4, sizeof(registersUsr) / 4,  file);
    fread(registersFiq,  4, sizeof(registersFiq) / 4,  file);
    fread(registersSvc,  4, sizeof(registersSvc) / 4,  file);
    fread(registersAbt,  4, sizeof(registersAbt) / 4,  file);
    fread(registersIrq,  4, sizeof(registersIrq) / 4,  file);
    fread(registersUnd,  4, sizeof(registersUnd) / 4,  file);
    fread(&cpsr,     sizeof(cpsr),     1, file);
    fread(&spsrFiq,  sizeof(spsrFiq),  1, file);
    fread(&spsrSvc,  sizeof(spsrSvc),  1, file);
    fread(&spsrAbt,  sizeof(spsrAbt),  1, file);
    fread(&spsrIrq,  sizeof(spsrIrq),  1, file);
    fread(&spsrUnd,  sizeof(spsrUnd),  1, file);
    fread(&cycles,   sizeof(cycles),   1, file);
    fread(&halted,   sizeof(halted),   1, file);
    fread(&dsiCycle, sizeof(dsiCycle), 1, file);
    fread(&ime,      sizeof(ime),      1, file);
    fread(&ie,       sizeof(ie),       1, file);
    fread(&irf,      sizeof(irf),      1, file);
    fread(&postFlg,  sizeof(postFlg),  1, file);

    swapRegisters(cpsr);
    pcData = nullptr;
}

void Interpreter::init() {
    setCpsr(0x000000D3);
    registersUsr[15] = arm7 ? 0x00000000 : 0xFFFF0000;
    flushPipeline();
    ime = 0;
    ie = irf = 0;
    postFlg = 0;
}

void Interpreter::directBoot() {
    setCpsr(0x000000DF);
    uint32_t entry = core->gbaMode ? 0x8000000 : entryAddr;
    registersUsr[15] = entry;
    registersUsr[14] = entry;
    registersUsr[12] = entry;
    registersUsr[13] = arm7 ? (core->gbaMode ? 0x3007F00 : 0x380FD80) : 0x3002F7C;
    registersIrq[0]  = arm7 ? (core->gbaMode ? 0x3007FA0 : 0x380FF80) : 0x3003F80;
    registersSvc[0]  = arm7 ? (core->gbaMode ? 0x3007FE0 : 0x380FFC0) : 0x3003FC0;
    flushPipeline();
}

void Interpreter::resetCycles() {
    cycles -= std::min(core->globalCycles, cycles);
}

void Interpreter::runCoreNone(Core &core) {
    while (true) {
        PPCIrqState st = PPCIrqLockByMsr();
        bool is_running = core.running;
        core.running = true;
        PPCIrqUnlockByMsr(st);

        if (PPC_UNLIKELY(!is_running)) break;

        core.globalCycles = core.events[0].cycles;
        while (core.events[0].cycles <= core.globalCycles) {
            core.tasks[core.events[0].task]();
            core.events.erase(core.events.begin());
        }
    }
}

template void Interpreter::runCoreSingle<false, 0>(Core &core);
template void Interpreter::runCoreSingle<true, 0>(Core &core);
template void Interpreter::runCoreSingle<true, 1>(Core &core);

template <bool _arm7, int shift>
void Interpreter::runCoreSingle(Core &core) {
    Interpreter &arm = core.interpreter[_arm7];

    while (true) {
        PPCIrqState st = PPCIrqLockByMsr();
        bool is_running = core.running;
        core.running = true;
        PPCIrqUnlockByMsr(st);

        if (PPC_UNLIKELY(!is_running)) break;

        // Cache event cycle threshold to avoid repeated dereference
        uint32_t eventCycles = core.events[0].cycles;
        core.globalCycles = std::max(core.globalCycles, arm.cycles);

        while (PPC_LIKELY(eventCycles > arm.cycles)) {
            uint32_t gc = core.globalCycles + (arm.runOpcode() << shift);
            arm.cycles = gc;
            core.globalCycles = gc;
            eventCycles = core.events[0].cycles;
        }

        core.globalCycles = eventCycles;
        while (core.events[0].cycles <= core.globalCycles) {
            core.tasks[core.events[0].task]();
            core.events.erase(core.events.begin());
        }
    }
}

void Interpreter::runCoreNds(Core &core) {
    Interpreter &arm9 = core.interpreter[0];
    Interpreter &arm7 = core.interpreter[1];

    while (true) {
        PPCIrqState st = PPCIrqLockByMsr();
        bool is_running = core.running;
        core.running = true;
        PPCIrqUnlockByMsr(st);

        if (PPC_UNLIKELY(!is_running)) break;

        uint32_t eventCycles = core.events[0].cycles;

        while (PPC_LIKELY(eventCycles > core.globalCycles)) {
            uint32_t gc = core.globalCycles;

            if (PPC_LIKELY(gc >= arm9.cycles))
                arm9.cycles = gc + arm9.runOpcode();

            if (PPC_LIKELY(gc >= arm7.cycles))
                arm7.cycles = gc + (arm7.runOpcode() << 1);

            // Use PowerPC min trick - avoid branch with conditional move pattern
            uint32_t a9c = arm9.cycles;
            uint32_t a7c = arm7.cycles;
            core.globalCycles = (a9c < a7c) ? a9c : a7c;
            eventCycles = core.events[0].cycles;
        }

        core.globalCycles = eventCycles;
        while (core.events[0].cycles <= core.globalCycles) {
            core.tasks[core.events[0].task]();
            core.events.erase(core.events.begin());
        }
    }
}

void Interpreter::runCoreDsi(Core &core) {
    Interpreter &arm9 = core.interpreter[0];
    Interpreter &arm7 = core.interpreter[1];

    while (true) {
        PPCIrqState st = PPCIrqLockByMsr();
        bool is_running = core.running;
        core.running = true;
        PPCIrqUnlockByMsr(st);

        if (PPC_UNLIKELY(!is_running)) break;

        uint32_t eventCycles = core.events[0].cycles;

        while (PPC_LIKELY(eventCycles > core.globalCycles)) {
            uint32_t gc = core.globalCycles;

            if (PPC_LIKELY(gc >= arm9.cycles)) {
                int c = arm9.runOpcode() + arm9.dsiCycle;
                arm9.cycles = gc + (c >> 1);
                arm9.dsiCycle = c & 1;
            }

            if (PPC_LIKELY(gc >= arm7.cycles))
                arm7.cycles = gc + (arm7.runOpcode() << 1);

            uint32_t a9c = arm9.cycles;
            uint32_t a7c = arm7.cycles;
            core.globalCycles = (a9c < a7c) ? a9c : a7c;
            eventCycles = core.events[0].cycles;
        }

        core.globalCycles = eventCycles;
        while (core.events[0].cycles <= core.globalCycles) {
            core.tasks[core.events[0].task]();
            core.events.erase(core.events.begin());
        }
    }
}

// The hot path - optimized heavily for PowerPC
// Key optimizations:
//  1. Cache pipeline[0] in local var to avoid pointer re-read
//  2. Avoid redundant register[15] dereferences  
//  3. Use __builtin_expect for branch prediction
HOT ALWAYS_INLINE int Interpreter::runOpcode() {
    // Advance pipeline - keep in local regs for speed
    uint32_t opcode = pipeline[0];
    pipeline[0] = pipeline[1];

    if (PPC_UNLIKELY(cpsr & BIT(5))) {
        // THUMB mode
        uint32_t pc = (*registers[15] += 2);
        // Fast path: use pcData pointer if valid and aligned
        if (PPC_LIKELY((pc & 0xFFE) && pcData)) {
            pcData += 2;
            pipeline[1] = (uint16_t)(*(uint16_t*)pcData);
        } else {
            pipeline[1] = getOpcode16();
        }
        return (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
    } else {
        // ARM mode - most common path
        uint32_t pc = (*registers[15] += 4);
        if (PPC_LIKELY((pc & 0xFFC) && pcData)) {
            pcData += 4;
            // Use unaligned load helper for big-endian safety
            pipeline[1] = U8TO32(pcData, 0);
        } else {
            pipeline[1] = getOpcode32();
        }

        // Check condition - use precomputed table
        uint8_t cond = condition[((opcode >> 24) & 0xF0) | (cpsr >> 28)];
        if (PPC_LIKELY(cond == 1)) {
            // Execute - most opcodes are unconditional
            return (this->*armInstrs[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode);
        } else if (PPC_UNLIKELY(cond == 0)) {
            return 1; // Condition false
        } else {
            return handleReserved(opcode); // Reserved
        }
    }
}

uint16_t Interpreter::getOpcode16() {
    uint32_t pc = *registers[15];
    uint8_t **map = arm7 ? core->memory.readMap7 : core->memory.readMap9A;
    pcData = map[pc >> 12];
    if (PPC_UNLIKELY(!pcData))
        return core->memory.read<uint16_t>(arm7, pc);
    pcData += (pc & 0xFFE);
    return U8TO16(pcData, 0);
}

uint32_t Interpreter::getOpcode32() {
    uint32_t pc = *registers[15];
    uint8_t **map = arm7 ? core->memory.readMap7 : core->memory.readMap9A;
    pcData = map[pc >> 12];
    if (PPC_UNLIKELY(!pcData))
        return core->memory.read<uint32_t>(arm7, pc);
    pcData += (pc & 0xFFC);
    return U8TO32(pcData, 0);
}

void Interpreter::halt(int bit) {
    bool before = halted;
    halted |= BIT(bit);
    if (PPC_UNLIKELY(before)) return;
    core->schedule(UPDATE_RUN, 0);
    cycles = 0xFFFFFFFF;
}

void Interpreter::unhalt(int bit) {
    bool before = halted;
    halted &= ~BIT(bit);
    if (PPC_UNLIKELY(!before)) return;
    core->schedule(UPDATE_RUN, 0);
    cycles = 0;
}

void Interpreter::sendInterrupt(int bit) {
    irf |= BIT(bit);
    if (PPC_LIKELY(ie & irf)) {
        if (ime && !(cpsr & BIT(7)))
            core->schedule(SchedTask(ARM9_INTERRUPT + arm7), (arm7 && !core->gbaMode) + 1);
        else if (ime || arm7)
            unhalt(0);
    }
}

void Interpreter::interrupt() {
    if (ime && (ie & irf) && !(cpsr & BIT(7))) {
        exception(0x18);
        unhalt(0);
    }
}

int Interpreter::exception(uint8_t vector) {
    if (PPC_UNLIKELY(bios && (arm7 || core->cp15.exceptionAddr)))
        return bios->execute(vector, registers);

    static const uint8_t modes[] = { 0x13, 0x1B, 0x13, 0x17, 0x17, 0x13, 0x12, 0x11 };
    setCpsr((cpsr & ~0x3F) | BIT(7) | modes[vector >> 2], true);
    *registers[14] = *registers[15] + ((*spsr & BIT(5)) >> 4);
    *registers[15] = (arm7 ? 0 : core->cp15.exceptionAddr) + vector;
    flushPipeline();
    return 3;
}

void Interpreter::flushPipeline() {
    if (cpsr & BIT(5)) {
        *registers[15] = (*registers[15] & ~0x1) + 2;
        pipeline[0] = core->memory.read<uint16_t>(arm7, *registers[15] - 2);
        pipeline[1] = getOpcode16();
    } else {
        *registers[15] = (*registers[15] & ~0x3) + 4;
        pipeline[0] = core->memory.read<uint32_t>(arm7, *registers[15] - 4);
        pipeline[1] = getOpcode32();
    }
}

// Optimized register swap - unrolled for PowerPC
void Interpreter::swapRegisters(uint32_t value) {
    switch (value & 0x1F) {
    case 0x10: // User
    case 0x1F: // System
        registers[8]  = &registersUsr[8];
        registers[9]  = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersUsr[13];
        registers[14] = &registersUsr[14];
        spsr = nullptr;
        break;

    case 0x11: // FIQ
        registers[8]  = &registersFiq[0];
        registers[9]  = &registersFiq[1];
        registers[10] = &registersFiq[2];
        registers[11] = &registersFiq[3];
        registers[12] = &registersFiq[4];
        registers[13] = &registersFiq[5];
        registers[14] = &registersFiq[6];
        spsr = &spsrFiq;
        break;

    case 0x12: // IRQ
        registers[8]  = &registersUsr[8];
        registers[9]  = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersIrq[0];
        registers[14] = &registersIrq[1];
        spsr = &spsrIrq;
        break;

    case 0x13: // Supervisor
        registers[8]  = &registersUsr[8];
        registers[9]  = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersSvc[0];
        registers[14] = &registersSvc[1];
        spsr = &spsrSvc;
        break;

    case 0x17: // Abort
        registers[8]  = &registersUsr[8];
        registers[9]  = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersAbt[0];
        registers[14] = &registersAbt[1];
        spsr = &spsrAbt;
        break;

    case 0x1B: // Undefined
        registers[8]  = &registersUsr[8];
        registers[9]  = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersUnd[0];
        registers[14] = &registersUnd[1];
        spsr = &spsrUnd;
        break;

    default:
        LOG_CRIT("Unknown ARM%d CPU mode: 0x%X\n", arm7 ? 7 : 9, value & 0x1F);
        break;
    }
}

void Interpreter::setCpsr(uint32_t value, bool save) {
    if (PPC_UNLIKELY((value & 0x1F) != (cpsr & 0x1F)))
        swapRegisters(value);

    if (save && spsr) *spsr = cpsr;
    cpsr = value;

    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(SchedTask(ARM9_INTERRUPT + arm7), (arm7 && !core->gbaMode) + 1);
}

int Interpreter::handleReserved(uint32_t opcode) {
    if ((opcode & 0xE000000) == 0xA000000)
        return blx(opcode);

    if (PPC_UNLIKELY(bios && opcode == 0xFF000000))
        return finishHleIrq();

    if (core->dldi.isPatched()) {
        uint32_t **r = registers;
        switch (opcode) {
            case DLDI_START:  *r[0] = core->dldi.startup(); break;
            case DLDI_INSERT: *r[0] = core->dldi.isInserted(); break;
            case DLDI_READ:   *r[0] = core->dldi.readSectors(arm7, *r[0], *r[1], *r[2]); break;
            case DLDI_WRITE:  *r[0] = core->dldi.writeSectors(arm7, *r[0], *r[1], *r[2]); break;
            case DLDI_CLEAR:  *r[0] = core->dldi.clearStatus(); break;
            case DLDI_STOP:   *r[0] = core->dldi.shutdown(); break;
        }
        return bx(14);
    }

    return unkArm(opcode);
}

int Interpreter::handleHleIrq() {
    setCpsr((cpsr & ~0x3F) | BIT(7) | 0x12, true);
    *registers[14] = *registers[15] + ((*spsr & BIT(5)) ? 2 : 0);
    stmdbW((13 << 16) | BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(12) | BIT(14));
    *registers[14] = arm7 ? 0x00000000 : 0xFFFF0000;
    *registers[15] = core->memory.read<uint32_t>(arm7,
        arm7 ? 0x3FFFFFC : (core->cp15.dtcmAddr + 0x3FFC));
    flushPipeline();
    return 3;
}

int Interpreter::finishHleIrq() {
    if (bios->shouldCheck())
        bios->checkWaitFlags();
    ldmiaW((13 << 16) | BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(12) | BIT(14));
    *registers[15] = *registers[14] - 4;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

COLD int Interpreter::unkArm(uint32_t opcode) {
    LOG_CRIT("Unknown ARM%d ARM opcode: 0x%X\n", arm7 ? 7 : 9, opcode);
    return 1;
}

COLD int Interpreter::unkThumb(uint16_t opcode) {
    LOG_CRIT("Unknown ARM%d THUMB opcode: 0x%X\n", arm7 ? 7 : 9, opcode);
    return 1;
}

void Interpreter::writeIme(uint8_t value) {
    ime = value & 0x01;
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(SchedTask(ARM9_INTERRUPT + arm7), (arm7 && !core->gbaMode) + 1);
}

void Interpreter::writeIe(uint32_t mask, uint32_t value) {
    mask &= (arm7 ? (core->gbaMode ? 0x3FFF : 0x01FF3FFF) : 0x003F3F7F);
    ie = (ie & ~mask) | (value & mask);
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(SchedTask(ARM9_INTERRUPT + arm7), (arm7 && !core->gbaMode) + 1);
}

void Interpreter::writeIrf(uint32_t mask, uint32_t value) {
    irf &= ~(value & mask);
}

void Interpreter::writePostFlg(uint8_t value) {
    postFlg |= value & 0x01;
    if (!arm7) postFlg = (postFlg & ~0x02) | (value & 0x02);
}
