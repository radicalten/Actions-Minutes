/*
    Copyright (C) 2019-2025 Hydr8gon
    Copyright (C) 2026 radicalten

    This file is part of NooDS-Wii.

    NooDS-Wii is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    NooDS-Wii is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NooDS-Wii. If not, see <https://www.gnu.org/licenses/>.
*/

#include "core.h"
#include "arm_jit.h"
#include <tuxedo/thread.h>

Interpreter::Interpreter(Core *core, bool arm7): core(core), arm7(arm7) {
    // Initialize the registers for user mode
    for (int i = 0; i < 32; i++)
        registers[i] = &registersUsr[i & 0xF];
}

void Interpreter::saveState(FILE *file) {
    // Write state data to the file
    fwrite(pipeline, 4, sizeof(pipeline) / 4, file);
    fwrite(registersUsr, 4, sizeof(registersUsr) / 4, file);
    fwrite(registersFiq, 4, sizeof(registersFiq) / 4, file);
    fwrite(registersSvc, 4, sizeof(registersSvc) / 4, file);
    fwrite(registersAbt, 4, sizeof(registersAbt) / 4, file);
    fwrite(registersIrq, 4, sizeof(registersIrq) / 4, file);
    fwrite(registersUnd, 4, sizeof(registersUnd) / 4, file);
    fwrite(&cpsr, sizeof(cpsr), 1, file);
    fwrite(&spsrFiq, sizeof(spsrFiq), 1, file);
    fwrite(&spsrSvc, sizeof(spsrSvc), 1, file);
    fwrite(&spsrAbt, sizeof(spsrAbt), 1, file);
    fwrite(&spsrIrq, sizeof(spsrIrq), 1, file);
    fwrite(&spsrUnd, sizeof(spsrUnd), 1, file);
    fwrite(&cycles, sizeof(cycles), 1, file);
    fwrite(&halted, sizeof(halted), 1, file);
    fwrite(&dsiCycle, sizeof(dsiCycle), 1, file);
    fwrite(&ime, sizeof(ime), 1, file);
    fwrite(&ie, sizeof(ie), 1, file);
    fwrite(&irf, sizeof(irf), 1, file);
    fwrite(&postFlg, sizeof(postFlg), 1, file);
}

void Interpreter::loadState(FILE *file) {
    // Read state data from the file
    fread(pipeline, 4, sizeof(pipeline) / 4, file);
    fread(registersUsr, 4, sizeof(registersUsr) / 4, file);
    fread(registersFiq, 4, sizeof(registersFiq) / 4, file);
    fread(registersSvc, 4, sizeof(registersSvc) / 4, file);
    fread(registersAbt, 4, sizeof(registersAbt) / 4, file);
    fread(registersIrq, 4, sizeof(registersIrq) / 4, file);
    fread(registersUnd, 4, sizeof(registersUnd) / 4, file);
    fread(&cpsr, sizeof(cpsr), 1, file);
    fread(&spsrFiq, sizeof(spsrFiq), 1, file);
    fread(&spsrSvc, sizeof(spsrSvc), 1, file);
    fread(&spsrAbt, sizeof(spsrAbt), 1, file);
    fread(&spsrIrq, sizeof(spsrIrq), 1, file);
    fread(&spsrUnd, sizeof(spsrUnd), 1, file);
    fread(&cycles, sizeof(cycles), 1, file);
    fread(&halted, sizeof(halted), 1, file);
    fread(&dsiCycle, sizeof(dsiCycle), 1, file);
    fread(&ime, sizeof(ime), 1, file);
    fread(&ie, sizeof(ie), 1, file);
    fread(&irf, sizeof(irf), 1, file);
    fread(&postFlg, sizeof(postFlg), 1, file);

    // Update mapped registers
    swapRegisters(cpsr);
    pcData = nullptr;
#if NOODS_JIT
    ArmPpcJit::invalidateCpu(arm7);
#endif
}

void Interpreter::init() {
    // Prepare to boot the BIOS
    spsrFiq = spsrSvc = spsrAbt = spsrIrq = spsrUnd = 0x10;
    setCpsr(0x000000D3); // Supervisor, interrupts off
    registersUsr[15] = arm7 ? 0x00000000 : 0xFFFF0000;
    flushPipeline();
#if NOODS_JIT
    ArmPpcJit::invalidateCpu(arm7);
#endif

    // Reset the I/O registers
    ime = 0;
    ie = irf = 0;
    postFlg = 0;
}

void Interpreter::directBoot() {
    // Prepare to directly boot a ROM
    spsrFiq = spsrSvc = spsrAbt = spsrIrq = spsrUnd = 0x10;
    setCpsr(0x000000DF); // System, interrupts off
    registersUsr[15] = core->gbaMode ? 0x8000000 : entryAddr;
    registersUsr[14] = registersUsr[15];
    registersUsr[12] = registersUsr[15];
    registersUsr[13] = arm7 ? (core->gbaMode ? 0x3007F00 : 0x380FD80) : 0x3002F7C;
    registersIrq[0] = arm7 ? (core->gbaMode ? 0x3007FA0 : 0x380FF80) : 0x3003F80;
    registersSvc[0] = arm7 ? (core->gbaMode ? 0x3007FE0 : 0x380FFC0) : 0x3003FC0;
    flushPipeline();
#if NOODS_JIT
    ArmPpcJit::invalidateCpu(arm7);
#endif
}

void Interpreter::resetCycles() {
    // Adjust CPU cycles for a global cycle reset
    cycles -= std::min(core->globalCycles, cycles);
}

void Interpreter::runCoreNone(Core &core) {
    // Run the core with no active CPUs
    while (true) {
        PPCIrqState st = PPCIrqLockByMsr();
        bool is_running = core.running;
        core.running = true;
        PPCIrqUnlockByMsr(st);

        if (!is_running) break;

        // Jump to the next task and run all that are scheduled now
        core.globalCycles = core.events[0].cycles;
        while (core.events[0].cycles <= core.globalCycles) {
            {
#if NOODS_JIT && NOODS_JIT_POLL_SKIP
                ArmPpcJit::pollEpoch++;
#endif
            core.tasks[core.events[0].task]();
            }
            core.events.erase(core.events.begin());
        }
    }
}

template void Interpreter::runCoreSingle<false, 0>(Core &core);
template void Interpreter::runCoreSingle<true, 0>(Core &core);
template void Interpreter::runCoreSingle<true, 1>(Core &core);
template <bool _arm7, int shift> void Interpreter::runCoreSingle(Core &core) {
    // Run the core with one active CPU
    Interpreter &arm = core.interpreter[_arm7];
    while (true) {
        PPCIrqState st = PPCIrqLockByMsr();
        bool is_running = core.running;
        core.running = true;
        PPCIrqUnlockByMsr(st);

        if (!is_running) break;

        // Run a CPU until the next scheduled task
        core.globalCycles = std::max(core.globalCycles, arm.cycles);
        while (core.events[0].cycles > arm.cycles)
            arm.cycles = (core.globalCycles += arm.runOpcode() << shift);

        // Jump to the next task and run all that are scheduled now
        core.globalCycles = core.events[0].cycles;
        while (core.events[0].cycles <= core.globalCycles) {
            {
#if NOODS_JIT && NOODS_JIT_POLL_SKIP
                ArmPpcJit::pollEpoch++;
#endif
            core.tasks[core.events[0].task]();
            }
            core.events.erase(core.events.begin());
        }
    }
}

#if NOODS_NDS_TASK_PROFILE
// Diagnostic only: aggregate PPC time in CPU scheduling and each scheduled task.
static uint64_t ndsCpuTicks;
static uint64_t ndsTaskTicks[MAX_TASKS];
static uint32_t ndsTaskCalls[MAX_TASKS];

void Interpreter::dumpNdsTaskProfile(FILE *f) {
    fprintf(f, "NDS_CPU us=%llu\n", (unsigned long long)PPCTicksToUs(ndsCpuTicks));
    for (unsigned i = 0; i < MAX_TASKS; ++i) {
        if (ndsTaskCalls[i])
            fprintf(f, "NDS_TASK id=%u count=%u us=%llu\n", i, ndsTaskCalls[i],
                    (unsigned long long)PPCTicksToUs(ndsTaskTicks[i]));
    }
}
#endif

void Interpreter::runCoreNds(Core &core) {
    // Run the core with both CPUs active in NDS mode
    Interpreter &arm9 = core.interpreter[0];
    Interpreter &arm7 = core.interpreter[1];
    while (true) {
        PPCIrqState st = PPCIrqLockByMsr();
        bool is_running = core.running;
        core.running = true;
        PPCIrqUnlockByMsr(st);

        if (!is_running) break;

        // Run the ARM9 and half-speed ARM7 until the next scheduled task
#if NOODS_NDS_TASK_PROFILE
        uint64_t cpuStart = PPCGetTickCount();
#endif
        while (core.events[0].cycles > core.globalCycles) {
            if (core.globalCycles >= arm9.cycles)
                arm9.cycles = core.globalCycles + arm9.runOpcode();
            if (core.globalCycles >= arm7.cycles)
                arm7.cycles = core.globalCycles + (arm7.runOpcode() << 1);
            core.globalCycles = std::min<uint32_t>(arm9.cycles, arm7.cycles);
        }

#if NOODS_NDS_TASK_PROFILE
        ndsCpuTicks += PPCGetTickCount() - cpuStart;
#endif
        // Jump to the next task and run all that are scheduled now
        core.globalCycles = core.events[0].cycles;
        while (core.events[0].cycles <= core.globalCycles) {
            {
#if NOODS_JIT && NOODS_JIT_POLL_SKIP
                ArmPpcJit::pollEpoch++;
#endif
#if NOODS_NDS_TASK_PROFILE
            SchedTask task = core.events[0].task;
            uint64_t taskStart = PPCGetTickCount();
            core.tasks[task]();
            ndsTaskTicks[task] += PPCGetTickCount() - taskStart;
            ndsTaskCalls[task]++;
#else
            core.tasks[core.events[0].task]();
#endif
            }
            core.events.erase(core.events.begin());
        }
    }
}

void Interpreter::runCoreDsi(Core &core) {
    // Run the core in DSi mode
    Interpreter &arm9 = core.interpreter[0];
    Interpreter &arm7 = core.interpreter[1];
    while (true) {
        PPCIrqState st = PPCIrqLockByMsr();
        bool is_running = core.running;
        core.running = true;
        PPCIrqUnlockByMsr(st);

        if (!is_running) break;

        // Run both CPUs until the next scheduled task
        while (core.events[0].cycles > core.globalCycles) {
            // Run the ARM9 twice as fast as usual
            if (core.globalCycles >= arm9.cycles) {
                int cycles = arm9.runOpcode() + arm9.dsiCycle;
                arm9.cycles = core.globalCycles + (cycles >> 1);
                arm9.dsiCycle = (cycles & 0x1);
            }

            // Run the ARM7 at half speed and advance to the next opcode cycle
            if (core.globalCycles >= arm7.cycles)
                arm7.cycles = core.globalCycles + (arm7.runOpcode() << 1);
            core.globalCycles = std::min<uint32_t>(arm9.cycles, arm7.cycles);
        }

        // Jump to the next task and run all that are scheduled now
        core.globalCycles = core.events[0].cycles;
        while (core.events[0].cycles <= core.globalCycles) {
            {
#if NOODS_JIT && NOODS_JIT_POLL_SKIP
                ArmPpcJit::pollEpoch++;
#endif
            core.tasks[core.events[0].task]();
            }
            core.events.erase(core.events.begin());
        }
    }
}

FORCE_INLINE int Interpreter::runOpcode() {
    // Push the next opcode through the pipeline
    uint32_t opcode = pipeline[0];
    pipeline[0] = pipeline[1];

    // Execute an instruction
    if (cpsr & BIT(5)) { // THUMB mode
        // Increment the program counter and fill the pipeline from pointer or fallback
        pipeline[1] = (((*registers[15] += 2) & 0xFFE) && pcData) ? U8TO16(pcData += 2, 0) : getOpcode16();

        // Execute a THUMB instruction
#if NOODS_JIT
        return ArmPpcJit::executeThumb(this, opcode);
#else
        return (this->*thumbInstrs[(opcode >> 6) & 0x3FF])(opcode);
#endif
    }
    else { // ARM mode
        // Increment the program counter and fill the pipeline from pointer or fallback
        pipeline[1] = (((*registers[15] += 4) & 0xFFC) && pcData) ? U8TO32(pcData += 4, 0) : getOpcode32();

        // Execute an ARM instruction based on its condition
        switch (condition[((opcode >> 24) & 0xF0) | (cpsr >> 28)]) {
            case 0: return 1; // False
            case 2: return handleReserved(opcode); // Reserved
#if NOODS_JIT
            default: return ArmPpcJit::executeArm(this, opcode);
#else
            default: return (this->*armInstrs[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode);
#endif
        }
    }
}

uint16_t Interpreter::getOpcode16() {
    // Set the opcode pointer or fall back to a regular 16-bit opcode read
    if (!(pcData = (arm7 ? core->memory.readMap7 : core->memory.readMap9A)[*registers[15] >> 12]))
        return core->memory.read<uint16_t>(arm7, *registers[15]);
    pcData += (*registers[15] & 0xFFE);
    return U8TO16(pcData, 0);
}

uint32_t Interpreter::getOpcode32() {
    // Set the opcode pointer or fall back to a regular 32-bit opcode read
    if (!(pcData = (arm7 ? core->memory.readMap7 : core->memory.readMap9A)[*registers[15] >> 12]))
        return core->memory.read<uint32_t>(arm7, *registers[15]);
    pcData += (*registers[15] & 0xFFC);
    return U8TO32(pcData, 0);
}

void Interpreter::halt(int bit) {
    // Set a halt bit and disable the CPU if newly halted
    bool before = halted;
    halted |= BIT(bit);
    if (before) return;
    core->schedule(UPDATE_RUN, 0);
    cycles = 0xFFFFFFFF;
}

void Interpreter::unhalt(int bit) {
    // Clear a halt bit and enable the CPU if newly halted
    bool before = halted;
    halted &= ~BIT(bit);
    if (!before) return;
    core->schedule(UPDATE_RUN, 0);
    cycles = 0;
}

void Interpreter::sendInterrupt(int bit) {
    // Set the interrupt's request bit
    irf |= BIT(bit);

    // Trigger an interrupt if the conditions are met, or unhalt the CPU even if interrupts are disabled
    // The ARM9 additionally needs IME to be set for it to unhalt, but the ARM7 doesn't care
    if (ie & irf) {
        if (ime && !(cpsr & BIT(7)))
            core->schedule(SchedTask(ARM9_INTERRUPT + arm7), (arm7 && !core->gbaMode) + 1);
        else if (ime || arm7)
            unhalt(0);
    }
}

void Interpreter::interrupt() {
    // Trigger an interrupt and unhalt the CPU if the conditions still hold
    if (ime && (ie & irf) && !(cpsr & BIT(7))) {
        exception(0x18);
        unhalt(0);
    }
}

int Interpreter::exception(uint8_t vector) {
    // Forward the call to HLE BIOS if enabled, unless on ARM9 with the exception address changed
    if (bios && (arm7 || core->cp15.exceptionAddr))
        return bios->execute(vector, registers);

    // Switch the CPU mode, save the return address, and jump to the exception vector
    static const uint8_t modes[] = { 0x13, 0x1B, 0x13, 0x17, 0x17, 0x13, 0x12, 0x11 };
    setCpsr((cpsr & ~0x3F) | BIT(7) | modes[vector >> 2], true); // ARM, interrupts off, new mode
    *registers[14] = *registers[15] + ((*spsr & BIT(5)) >> 4);
    *registers[15] = (arm7 ? 0 : core->cp15.exceptionAddr) + vector;
    flushPipeline();
    return 3;
}

int Interpreter::undefinedException(bool thumb) {
    // Match Dust's ARM7/ARM9 undefined-instruction entry: preserve the old CPSR
    // in the Undefined SPSR bank, clear Thumb state, disable IRQs, and branch to +4.
    setCpsr((cpsr & ~0x3F) | BIT(7) | 0x1B, true);
    *registers[14] = *registers[15] - (thumb ? 2 : 4);
    *registers[15] = (arm7 ? 0 : core->cp15.exceptionAddr) | 0x04;
    flushPipeline();
    return 3;
}

void Interpreter::flushPipeline() {
    // Adjust the program counter and refill the pipeline after a jump
    if (cpsr & BIT(5)) { // THUMB mode
        *registers[15] = (*registers[15] & ~0x1) + 2;
        pipeline[0] = core->memory.read<uint16_t>(arm7, *registers[15] - 2);
        pipeline[1] = getOpcode16();
    }
    else { // ARM mode
        *registers[15] = (*registers[15] & ~0x3) + 4;
        pipeline[0] = core->memory.read<uint32_t>(arm7, *registers[15] - 4);
        pipeline[1] = getOpcode32();
    }
}

void Interpreter::swapRegisters(uint32_t value) {
    // Swap banked registers based on a CPU mode value
    switch (value & 0x0F) {
    case 0x00: // User
    case 0x04: // Reserved: System register bank, Abort SPSR bank
    case 0x05:
    case 0x06:
    case 0x08: // Reserved: System register bank, Undefined SPSR bank
    case 0x09:
    case 0x0A:
    case 0x0C: // Reserved and System modes share the System register bank
    case 0x0D:
    case 0x0E:
    case 0x0F: // System
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersUsr[13];
        registers[14] = &registersUsr[14];
        if ((value & 0x0F) >= 0x04 && (value & 0x0F) <= 0x06)
            spsr = &spsrAbt;
        else if ((value & 0x0F) >= 0x08 && (value & 0x0F) <= 0x0A)
            spsr = &spsrUnd;
        else
            spsr = nullptr;
        break;

    case 0x01: // FIQ (mode field bits 3:0)
        registers[8] = &registersFiq[0];
        registers[9] = &registersFiq[1];
        registers[10] = &registersFiq[2];
        registers[11] = &registersFiq[3];
        registers[12] = &registersFiq[4];
        registers[13] = &registersFiq[5];
        registers[14] = &registersFiq[6];
        spsr = &spsrFiq;
        break;

    case 0x02: // IRQ (mode field bits 3:0)
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersIrq[0];
        registers[14] = &registersIrq[1];
        spsr = &spsrIrq;
        break;

    case 0x03: // Supervisor (mode field bits 3:0)
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersSvc[0];
        registers[14] = &registersSvc[1];
        spsr = &spsrSvc;
        break;

    case 0x07: // Abort (mode field bits 3:0)
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersAbt[0];
        registers[14] = &registersAbt[1];
        spsr = &spsrAbt;
        break;

    case 0x0B: // Undefined (mode field bits 3:0)
        registers[8] = &registersUsr[8];
        registers[9] = &registersUsr[9];
        registers[10] = &registersUsr[10];
        registers[11] = &registersUsr[11];
        registers[12] = &registersUsr[12];
        registers[13] = &registersUnd[0];
        registers[14] = &registersUnd[1];
        spsr = &spsrUnd;
        break;

    default:
        LOG_CRIT("Unknown ARM%d CPU mode: 0x%X\n", arm7 ? 7 : 9, value & 0x0F);
        break;
    }
}

void Interpreter::setCpsr(uint32_t value, bool save) {
    // Update registers if the CPU mode changed
    if ((value & 0x0F) != (cpsr & 0x0F))
        swapRegisters(value);

    // Set the CPSR, saving the old value if requested
    if (save && spsr) *spsr = cpsr;
    cpsr = value;

    // Trigger an interrupt if the conditions are met
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(SchedTask(ARM9_INTERRUPT + arm7), (arm7 && !core->gbaMode) + 1);
}

int Interpreter::handleReserved(uint32_t opcode) {
    // The ARM9-exclusive BLX instruction uses the reserved condition code, so let it run
    if ((opcode & 0xE000000) == 0xA000000)
        return blx(opcode); // BLX label

    // If the special HLE BIOS opcode was jumped to, return from an HLE interrupt
    if (bios && opcode == 0xFF000000)
        return finishHleIrq();

    // If a DLDI function was jumped to, HLE it and return
    if (core->dldi.isPatched()) {
        uint32_t **r = registers;
        switch (opcode) {
            case DLDI_START: *r[0] = core->dldi.startup(); break;
            case DLDI_INSERT: *r[0] = core->dldi.isInserted(); break;
            case DLDI_READ: *r[0] = core->dldi.readSectors(arm7, *r[0], *r[1], *r[2]); break;
            case DLDI_WRITE: *r[0] = core->dldi.writeSectors(arm7, *r[0], *r[1], *r[2]); break;
            case DLDI_CLEAR: *r[0] = core->dldi.clearStatus(); break;
            case DLDI_STOP: *r[0] = core->dldi.shutdown(); break;
        }
        return bx(14);
    }

    // Treat anything else as an unknown opcode
    return unkArm(opcode);
}

int Interpreter::handleHleIrq() {
    // Switch to IRQ mode, save the return address, and push registers to the stack
    setCpsr((cpsr & ~0x3F) | BIT(7) | 0x12, true);
    *registers[14] = *registers[15] + ((*spsr & BIT(5)) ? 2 : 0);
    stmdbW((13 << 16) | BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(12) | BIT(14));

    // Set the return address to the special HLE BIOS opcode amd jump to the interrupt handler
    *registers[14] = arm7 ? 0x00000000 : 0xFFFF0000;
    *registers[15] = core->memory.read<uint32_t>(arm7, arm7 ? 0x3FFFFFC : (core->cp15.dtcmAddr + 0x3FFC));
    flushPipeline();
    return 3;
}

int Interpreter::finishHleIrq() {
    // Update the wait flags if in the middle of an HLE IntrWait function
    if (bios->shouldCheck())
        bios->checkWaitFlags();

    // Pop registers from the stack, jump to the return address, and restore the mode
    ldmiaW((13 << 16) | BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(12) | BIT(14));
    *registers[15] = *registers[14] - 4;
    if (spsr) setCpsr(*spsr);
    flushPipeline();
    return 3;
}

int Interpreter::unkArm(uint32_t opcode) {
    // Unknown ARM encodings enter Undefined mode rather than acting as NOPs.
    (void)opcode;
    return undefinedException(false);
}

int Interpreter::unkThumb(uint16_t opcode) {
    // Unknown Thumb encodings enter the same Undefined vector with a Thumb LR offset.
    (void)opcode;
    return undefinedException(true);
}

void Interpreter::writeIme(uint8_t value) {
    // Write to the IME register
    ime = value & 0x01;

    // Trigger an interrupt if the conditions are met
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(SchedTask(ARM9_INTERRUPT + arm7), (arm7 && !core->gbaMode) + 1);
}

void Interpreter::writeIe(uint32_t mask, uint32_t value) {
    // Write to the IE register
    mask &= (arm7 ? (core->gbaMode ? 0x3FFF : 0x01FF3FFF) : 0x003F3F7F);
    ie = (ie & ~mask) | (value & mask);

    // Trigger an interrupt if the conditions are met
    if (ime && (ie & irf) && !(cpsr & BIT(7)))
        core->schedule(SchedTask(ARM9_INTERRUPT + arm7), (arm7 && !core->gbaMode) + 1);
}

void Interpreter::writeIrf(uint32_t mask, uint32_t value) {
    // Write to the IF register
    // Setting a bit actually clears it to acknowledge an interrupt
    irf &= ~(value & mask);
}

void Interpreter::writePostFlg(uint8_t value) {
    // Write to the POSTFLG register
    // The first bit can be set, but never cleared
    // For some reason, the second bit is writable on the ARM9
    postFlg |= value & 0x01;
    if (!arm7) postFlg = (postFlg & ~0x02) | (value & 0x02);
}
