#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include <cstdint>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cstddef>

// ============================================================================
// Direct Preprocessor Bypass of C++ Class Privacy Boundaries
// ============================================================================
#define private public
#define protected public

#include "core.h"
#include "interpreter.h"
#include "memory.h"

#undef private
#undef protected

// ============================================================================
// Gekko PowerPC Register Mapping Configuration
// ============================================================================
#define PPC_REG_R0     14
#define PPC_REG_R1     15
#define PPC_REG_R2     16
#define PPC_REG_R3     17
#define PPC_REG_R4     18
#define PPC_REG_R5     19
#define PPC_REG_R6     20
#define PPC_REG_R7     21
#define PPC_REG_R8     22
#define PPC_REG_R9     23
#define PPC_REG_R10    24
#define PPC_REG_R11    25
#define PPC_REG_R12    26
#define PPC_REG_SP     27 // ARM R13 (SP)
#define PPC_REG_LR     28 // ARM R14 (LR)
#define PPC_REG_CPSR   29 // ARM CPSR
#define PPC_REG_THIS   30 // Interpreter Pointer ('this')
#define PPC_REG_CORE   31 // Core Pointer

// Static offset configuration
static const size_t OFF_REGISTERS = offsetof(Interpreter, registers);
static const size_t OFF_CPSR      = offsetof(Interpreter, cpsr);
static const size_t OFF_CYCLES    = offsetof(Interpreter, cycles);
static const size_t OFF_CORE      = offsetof(Interpreter, core);
static const size_t OFF_MEMORY    = offsetof(Core, memory);

// ============================================================================
// C-ABI Safe Fallback Hook Definitions
// ============================================================================
extern "C" {
    uint32_t jit_fallback_read32(Interpreter* interp, uint32_t addr, uint32_t arm7) {
        return interp->core->memory.read<uint32_t>(arm7 != 0, addr, true);
    }
    uint16_t jit_fallback_read16(Interpreter* interp, uint32_t addr, uint32_t arm7) {
        return interp->core->memory.read<uint16_t>(arm7 != 0, addr, true);
    }
    uint8_t jit_fallback_read8(Interpreter* interp, uint32_t addr, uint32_t arm7) {
        return interp->core->memory.read<uint8_t>(arm7 != 0, addr, true);
    }
    void jit_fallback_write32(Interpreter* interp, uint32_t addr, uint32_t val, uint32_t arm7) {
        interp->core->memory.write<uint32_t>(arm7 != 0, addr, val, true);
    }
    void jit_fallback_write16(Interpreter* interp, uint32_t addr, uint16_t val, uint32_t arm7) {
        interp->core->memory.write<uint16_t>(arm7 != 0, addr, val, true);
    }
    void jit_fallback_write8(Interpreter* interp, uint32_t addr, uint8_t val, uint32_t arm7) {
        interp->core->memory.write<uint8_t>(arm7 != 0, addr, val, true);
    }
    int jit_fallback_run_opcode(Interpreter* interp) {
        return (interp->*(&Interpreter::runOpcode))();
    }
}

// ============================================================================
// High-Performance Dynamic Cache Allocation
// ============================================================================
struct JitEntry {
    uint32_t arm_pc;
    uint32_t arm_opcode_validation; // SMC validation word
    void* ppc_code;
};

#define JIT_BUFFER_SIZE (6 * 1024 * 1024) // 6MB Executable Translation Buffer
static uint32_t* jit_buffer = nullptr;
static uint32_t  jit_buffer_offset = 0;
static JitEntry  jit_cache[2][65536]; // [0] = ARM9, [1] = ARM7
static bool      jit_initialized = false;

static void jit_init() {
    if (jit_initialized) return;
    jit_buffer = (uint32_t*)malloc(JIT_BUFFER_SIZE);
    memset(jit_cache, 0, sizeof(jit_cache));
    jit_initialized = true;
}

// ============================================================================
// Gekko Native PowerPC Instruction Assembler Emitter
// ============================================================================
class PpcEmitter {
public:
    uint32_t* write_ptr;
    PpcEmitter(uint32_t* dest) : write_ptr(dest) {}

    void emit(uint32_t word) {
        *write_ptr++ = word;
    }

    void lis(int rD, uint16_t val) {
        emit((15 << 26) | (rD << 21) | val);
    }

    void ori(int rA, int rS, uint16_t val) {
        emit((24 << 26) | (rS << 21) | (rA << 16) | val);
    }

    void addi(int rD, int rA, int16_t val) {
        emit((14 << 26) | (rD << 21) | (rA << 16) | (uint16_t)val);
    }

    void add(int rD, int rA, int rB, bool rc = false) {
        emit((31 << 26) | (rD << 21) | (rA << 16) | (rB << 11) | (266 << 1) | (rc ? 1 : 0));
    }

    void subf(int rD, int rA, int rB, bool rc = false) {
        emit((31 << 26) | (rD << 21) | (rA << 16) | (rB << 11) | (40 << 1) | (rc ? 1 : 0));
    }

    void lwz(int rD, int rA, int16_t d) {
        emit((32 << 26) | (rD << 21) | (rA << 16) | (uint16_t)d);
    }

    void stw(int rS, int rA, int16_t d) {
        emit((36 << 26) | (rS << 21) | (rA << 16) | (uint16_t)d);
    }

    void lbz(int rD, int rA, int16_t d) {
        emit((34 << 26) | (rD << 21) | (rA << 16) | (uint16_t)d);
    }

    void stb(int rS, int rA, int16_t d) {
        emit((38 << 26) | (rS << 21) | (rA << 16) | (uint16_t)d);
    }

    void lwzx(int rD, int rA, int rB) {
        emit((31 << 26) | (rD << 21) | (rA << 16) | (rB << 11) | (23 << 1));
    }

    // Native PowerPC Hardware Byte-Reversal Load & Stores
    void lwbrx(int rD, int rA, int rB) {
        emit((31 << 26) | (rD << 21) | (rA << 16) | (rB << 11) | (534 << 1));
    }

    void stwbrx(int rS, int rA, int rB) {
        emit((31 << 26) | (rS << 21) | (rA << 16) | (rB << 11) | (662 << 1));
    }

    void lhbrx(int rD, int rA, int rB) {
        emit((31 << 26) | (rD << 21) | (rA << 16) | (rB << 11) | (790 << 1));
    }

    void sthbrx(int rS, int rA, int rB) {
        emit((31 << 26) | (rS << 21) | (rA << 16) | (rB << 11) | (918 << 1));
    }

    void sllw(int rA, int rS, int rB, bool rc = false) {
        emit((31 << 26) | (rS << 21) | (rA << 16) | (rB << 11) | (24 << 1) | (rc ? 1 : 0));
    }

    void srlw(int rA, int rS, int rB, bool rc = false) {
        emit((31 << 26) | (rS << 21) | (rA << 16) | (rB << 11) | (536 << 1) | (rc ? 1 : 0));
    }

    void sraw(int rA, int rS, int rB, bool rc = false) {
        emit((31 << 26) | (rS << 21) | (rA << 16) | (rB << 11) | (792 << 1) | (rc ? 1 : 0));
    }

    void srawi(int rA, int rS, int sh) {
        emit((31 << 26) | (rS << 21) | (rA << 16) | (sh << 11) | (824 << 1));
    }

    void and_(int rA, int rS, int rB, bool rc = false) {
        emit((31 << 26) | (rS << 21) | (rA << 16) | (rB << 11) | (28 << 1) | (rc ? 1 : 0));
    }

    void or_(int rA, int rS, int rB, bool rc = false) {
        emit((31 << 26) | (rS << 21) | (rA << 16) | (rB << 11) | (444 << 1) | (rc ? 1 : 0));
    }

    void xor_(int rA, int rS, int rB, bool rc = false) {
        emit((31 << 26) | (rS << 21) | (rA << 16) | (rB << 11) | (316 << 1) | (rc ? 1 : 0));
    }

    void cmpw(int crD, int rA, int rB) {
        emit((31 << 26) | (crD << 23) | (rA << 16) | (rB << 11) | (0 << 1));
    }

    void cmpwi(int crD, int rA, int16_t simm) {
        emit((11 << 26) | (crD << 23) | (rA << 16) | (uint16_t)simm);
    }

    void b(int32_t target_offset_bytes) {
        emit((18 << 26) | (((target_offset_bytes) >> 2) & 0x00FFFFFF) << 2);
    }

    void blr() {
        emit(0x4E800020);
    }

    void bctrl() {
        emit(0x4E800421);
    }

    void mtctr(int rS) {
        emit((31 << 26) | (rS << 21) | (9 << 16) | (467 << 1));
    }

    void mtlr(int rS) {
        emit((31 << 26) | (rS << 21) | (8 << 16) | (467 << 1));
    }

    void mflr(int rD) {
        emit((31 << 26) | (rD << 21) | (8 << 16) | (339 << 1));
    }

    void rlwinm(int rA, int rS, int sh, int mb, int me, bool rc = false) {
        emit((21 << 26) | (rS << 21) | (rA << 16) | (sh << 11) | (mb << 6) | (me << 1) | (rc ? 1 : 0));
    }

    void slwi(int rA, int rS, int sh) {
        rlwinm(rA, rS, sh, 0, 31 - sh);
    }

    void emit_load_imm(int reg, uint32_t imm) {
        if ((imm & 0xFFFF0000) == 0) {
            emit_clear_and_ori(reg, imm & 0xFFFF);
        } else if ((imm & 0x0000FFFF) == 0) {
            lis(reg, imm >> 16);
        } else {
            lis(reg, imm >> 16);
            ori(reg, reg, imm & 0xFFFF);
        }
    }

    void emit_clear_and_ori(int reg, uint16_t imm) {
        addi(reg, 0, 0);
        ori(reg, reg, imm);
    }

    // Critical L1 Cache Sync logic to maintain data-instruction coherency
    void flush_cache_range(void* start, uint32_t byte_size) {
        uint32_t start_addr = (uint32_t)start;
        uint32_t end_addr = start_addr + byte_size;
        
        uint32_t addr = start_addr & ~31;
        while (addr < end_addr) {
            asm volatile("dcbst 0, %0" : : "r"(addr));
            addr += 32;
        }
        asm volatile("sync");

        addr = start_addr & ~31;
        while (addr < end_addr) {
            asm volatile("icbi 0, %0" : : "r"(addr));
            addr += 32;
        }
        asm volatile("isync");
    }
};

// ============================================================================
// Core JIT Assembly Parsing Engine
// ============================================================================
class JitCompiler {
private:
    PpcEmitter e;
    Interpreter& interp;
    bool is_arm7;
    uint32_t start_pc;
    uint32_t accumulated_cycles;
    uint32_t* block_start;

    int map_arm_reg_to_host(int arm_reg) {
        if (arm_reg >= 0 && arm_reg <= 10) return PPC_REG_R0 + arm_reg;
        if (arm_reg == 11) return PPC_REG_R11;
        if (arm_reg == 12) return PPC_REG_R12;
        if (arm_reg == 13) return PPC_REG_SP;
        if (arm_reg == 14) return PPC_REG_LR;
        return -1;
    }

    void emit_prologue() {
        // Safe Stack Allocation Frame for Native calling boundaries
        e.addi(1, 1, -80);
        e.stw(31, 1, 76);
        e.stw(30, 1, 72);
        e.stw(29, 1, 68);
        e.stw(28, 1, 64);
        e.stw(27, 1, 60);
        e.stw(26, 1, 56);
        e.stw(25, 1, 52);
        e.stw(24, 1, 48);
        e.stw(23, 1, 44);
        e.stw(22, 1, 40);
        e.stw(21, 1, 36);
        e.stw(20, 1, 32);
        e.stw(19, 1, 28);
        e.stw(18, 1, 24);
        e.stw(17, 1, 20);
        e.stw(16, 1, 16);
        e.stw(15, 1, 12);
        e.stw(14, 1, 8);

        // Map base structures
        e.addi(PPC_REG_THIS, 3, 0);
        e.lwz(PPC_REG_CORE, PPC_REG_THIS, OFF_CORE);

        // Load virtual ARM registers into Gekko hardware GPRs
        for (int i = 0; i < 15; ++i) {
            int host_gpr = map_arm_reg_to_host(i);
            e.lwz(3, PPC_REG_THIS, OFF_REGISTERS + i * 4);
            e.lwz(host_gpr, 3, 0);
        }
        e.lwz(PPC_REG_CPSR, PPC_REG_THIS, OFF_CPSR);
    }

    void emit_epilogue() {
        // Flush active Gekko physical registers back to the virtual context
        for (int i = 0; i < 15; ++i) {
            int host_gpr = map_arm_reg_to_host(i);
            e.lwz(3, PPC_REG_THIS, OFF_REGISTERS + i * 4);
            e.stw(host_gpr, 3, 0);
        }
        e.stw(PPC_REG_CPSR, PPC_REG_THIS, OFF_CPSR);

        if (accumulated_cycles > 0) {
            e.lwz(3, PPC_REG_THIS, OFF_CYCLES);
            e.addi(3, 3, accumulated_cycles);
            e.stw(3, PPC_REG_THIS, OFF_CYCLES);
        }

        // Restore active compiler bounds
        e.lwz(31, 1, 76);
        e.lwz(30, 1, 72);
        e.lwz(29, 1, 68);
        e.lwz(28, 1, 64);
        e.lwz(27, 1, 60);
        e.lwz(26, 1, 56);
        e.lwz(25, 1, 52);
        e.lwz(24, 1, 48);
        e.lwz(23, 1, 44);
        e.lwz(22, 1, 40);
        e.lwz(21, 1, 36);
        e.lwz(20, 1, 32);
        e.lwz(19, 1, 28);
        e.lwz(18, 1, 24);
        e.lwz(17, 1, 20);
        e.lwz(16, 1, 16);
        e.lwz(15, 1, 12);
        e.lwz(14, 1, 8);
        e.addi(1, 1, 80);
        e.blr();
    }

    uint32_t* emit_cond_branch_over(uint32_t cond) {
        if (cond == 14) return nullptr; // Always execute

        switch (cond) {
            case 0: // EQ (Z == 1)
                e.rlwinm(3, PPC_REG_CPSR, 2, 31, 31, true); // Extract Bit 30
                break;
            case 1: // NE (Z == 0)
                e.rlwinm(3, PPC_REG_CPSR, 2, 31, 31, true);
                break;
            case 2: // CS/HS (C == 1)
                e.rlwinm(3, PPC_REG_CPSR, 3, 31, 31, true); // Extract Bit 29
                break;
            case 3: // CC/LO (C == 0)
                e.rlwinm(3, PPC_REG_CPSR, 3, 31, 31, true);
                break;
            case 4: // MI (N == 1)
                e.rlwinm(3, PPC_REG_CPSR, 1, 31, 31, true); // Extract Bit 31
                break;
            case 5: // PL (N == 0)
                e.rlwinm(3, PPC_REG_CPSR, 1, 31, 31, true);
                break;
            case 6: // VS (V == 1)
                e.rlwinm(3, PPC_REG_CPSR, 4, 31, 31, true); // Extract Bit 28
                break;
            case 7: // VC (V == 0)
                e.rlwinm(3, PPC_REG_CPSR, 4, 31, 31, true);
                break;
            default:
                return nullptr;
        }

        uint32_t* patch_point = e.write_ptr;
        if (cond % 2 == 0) {
            e.emit(0x41820000); // jump forward over the block if condition fails
        } else {
            e.emit(0x41800000);
        }
        return patch_point;
    }

    void patch_cond_branch(uint32_t* branch_instr_ptr) {
        int32_t word_offset = (int32_t)(e.write_ptr - branch_instr_ptr);
        uint32_t opcode = *branch_instr_ptr;
        opcode &= ~0xFFFC; // Clear old 14-bit BD displacement
        opcode |= (word_offset & 0x3FFF) << 2; // Inject new signed offset
        *branch_instr_ptr = opcode;
    }

    void patch_uncond_branch(uint32_t* branch_instr_ptr) {
        int32_t word_offset = (int32_t)(e.write_ptr - branch_instr_ptr);
        uint32_t opcode = *branch_instr_ptr;
        opcode &= ~0x03FFFFFC; // Clear old 24-bit LI displacement
        opcode |= (word_offset & 0x00FFFFFF) << 2;
        *branch_instr_ptr = opcode;
    }

    void compile_arm_data_proc(uint32_t op, uint32_t rd, uint32_t rn, uint32_t op2, bool is_imm) {
        int host_rd = map_arm_reg_to_host(rd);
        int host_rn = map_arm_reg_to_host(rn);

        if (is_imm) {
            uint32_t rot = (op2 >> 8) * 2;
            uint32_t val = op2 & 0xFF;
            if (rot != 0) {
                val = (val >> rot) | (val << (32 - rot));
            }

            switch (op) {
                case 4: // ADD
                    if (val <= 32767) {
                        e.addi(host_rd, host_rn, val);
                    } else {
                        e.emit_load_imm(3, val);
                        e.add(host_rd, host_rn, 3);
                    }
                    break;
                case 2: // SUB
                    if (val <= 32767) {
                        e.addi(host_rd, host_rn, -((int16_t)val));
                    } else {
                        e.emit_load_imm(3, val);
                        e.subf(host_rd, 3, host_rn);
                    }
                    break;
                case 13: // MOV
                    e.emit_load_imm(host_rd, val);
                    break;
                case 10: // CMP
                    if (val <= 32767) {
                        e.cmpwi(0, host_rn, val);
                    } else {
                        e.emit_load_imm(3, val);
                        e.cmpw(0, host_rn, 3);
                    }
                    e.rlwinm(PPC_REG_CPSR, PPC_REG_CPSR, 0, 4, 31);
                    break;
                default:
                    break;
            }
        } else {
            int host_rm = map_arm_reg_to_host(op2 & 0xF);
            switch (op) {
                case 4: // ADD
                    e.add(host_rd, host_rn, host_rm);
                    break;
                case 2: // SUB
                    e.subf(host_rd, host_rm, host_rn);
                    break;
                case 13: // MOV
                    e.addi(host_rd, host_rm, 0);
                    break;
                case 1: // EOR
                    e.xor_(host_rd, host_rn, host_rm);
                    break;
                case 12: // ORR
                    e.or_(host_rd, host_rn, host_rm);
                    break;
                case 14: // BIC
                    e.and_(3, host_rn, host_rm);
                    e.subf(host_rd, 3, host_rn);
                    break;
                case 10: // CMP
                    e.cmpw(0, host_rn, host_rm);
                    break;
            }
        }
    }

    void compile_arm_load_store(uint32_t opcode) {
        bool load = (opcode & (1 << 20)) != 0;
        bool byte_access = (opcode & (1 << 22)) != 0;
        uint32_t rn = (opcode >> 16) & 0xF;
        uint32_t rd = (opcode >> 12) & 0xF;
        uint32_t offset = opcode & 0xFFF;
        bool up = (opcode & (1 << 23)) != 0;

        int host_rn = map_arm_reg_to_host(rn);
        int host_rd = map_arm_reg_to_host(rd);

        if (up) {
            e.addi(3, host_rn, offset);
        } else {
            e.addi(3, host_rn, -((int16_t)offset));
        }

        size_t map_offset = is_arm7 ? offsetof(Memory, readMap7) : offsetof(Memory, readMap9A);
        if (!load) {
            map_offset = is_arm7 ? offsetof(Memory, writeMap7) : offsetof(Memory, writeMap9A);
        }

        uint32_t total_offset = OFF_MEMORY + map_offset;
        e.emit_load_imm(4, total_offset);
        e.add(4, 4, PPC_REG_CORE); // r4 = &core->memory.readMap

        e.srawi(5, 3, 12); // r5 = Address >> 12
        e.slwi(5, 5, 2);   // r5 = index * 4
        e.lwzx(6, 4, 5);   // r6 = MapTable[index]

        e.cmpwi(0, 6, 0);
        uint32_t* fallback_branch_patch = e.write_ptr;
        e.emit(0x41820000); // Jump over native access on page pointer miss

        e.rlwinm(5, 3, 0, 20, 31); // r5 = Address & 0xFFF
        e.add(6, 6, 5);            // r6 = host pointer
        
        if (load) {
            if (byte_access) {
                e.lbz(host_rd, 6, 0);
            } else {
                e.addi(5, 0, 0);
                e.lwbrx(host_rd, 6, 5); // Read Word Byte-Reversed
            }
        } else {
            if (byte_access) {
                e.stb(host_rd, 6, 0);
            } else {
                e.addi(5, 0, 0);
                e.stwbrx(host_rd, 6, 5); // Store Word Byte-Reversed
            }
        }

        uint32_t* fastpath_done_branch_patch = e.write_ptr;
        e.emit(0x48000000);

        patch_cond_branch(fallback_branch_patch);
        
        for (int i = 0; i < 15; ++i) {
            int host_gpr = map_arm_reg_to_host(i);
            e.lwz(5, PPC_REG_THIS, OFF_REGISTERS + i * 4);
            e.stw(host_gpr, 5, 0);
        }
        
        e.addi(4, 3, 0);             // Param 2: Target Address
        e.addi(3, PPC_REG_THIS, 0); // Param 1: Interpreter*
        if (!load) {
            e.addi(5, host_rd, 0);   // Param 3: Write Value
        }
        e.emit_load_imm(6, is_arm7 ? 1 : 0); // Param 4: CPU Type

        uint32_t fn_address = 0;
        if (load) {
            fn_address = (uint32_t)(uintptr_t)(byte_access ? jit_fallback_read8 : jit_fallback_read32);
        } else {
            fn_address = (uint32_t)(uintptr_t)(byte_access ? jit_fallback_write8 : jit_fallback_write32);
        }

        e.emit_load_imm(12, fn_address);
        e.mtctr(12);
        e.bctrl();

        if (load) {
            e.addi(host_rd, 3, 0);
        }

        for (int i = 0; i < 15; ++i) {
            int host_gpr = map_arm_reg_to_host(i);
            e.lwz(5, PPC_REG_THIS, OFF_REGISTERS + i * 4);
            e.lwz(host_gpr, 5, 0);
        }

        patch_uncond_branch(fastpath_done_branch_patch);
    }

    void emit_fallback_interpreter_instruction(uint32_t pc) {
        for (int i = 0; i < 15; ++i) {
            int host_gpr = map_arm_reg_to_host(i);
            e.lwz(3, PPC_REG_THIS, OFF_REGISTERS + i * 4);
            e.stw(host_gpr, 3, 0);
        }
        e.emit_load_imm(3, pc);
        e.lwz(4, PPC_REG_THIS, OFF_REGISTERS + 15 * 4);
        e.stw(3, 4, 0);
        e.stw(PPC_REG_CPSR, PPC_REG_THIS, OFF_CPSR);

        e.addi(3, PPC_REG_THIS, 0);
        e.emit_load_imm(12, (uint32_t)(uintptr_t)jit_fallback_run_opcode);
        e.mtctr(12);
        e.bctrl();

        for (int i = 0; i < 15; ++i) {
            int host_gpr = map_arm_reg_to_host(i);
            e.lwz(3, PPC_REG_THIS, OFF_REGISTERS + i * 4);
            e.lwz(host_gpr, 3, 0);
        }
        e.lwz(PPC_REG_CPSR, PPC_REG_THIS, OFF_CPSR);
    }

public:
    JitCompiler(uint32_t* write_buf, Interpreter& interpreter, bool arm7, uint32_t pc) 
        : e(write_buf), interp(interpreter), is_arm7(arm7), start_pc(pc), accumulated_cycles(0), block_start(write_buf) {}

    uint32_t compile_block() {
        emit_prologue();

        uint32_t pc = start_pc;
        bool block_ended = false;
        int instructions_compiled = 0;

        while (!block_ended && instructions_compiled < 64) {
            uint32_t opcode = interp.core->memory.read<uint32_t>(is_arm7, pc, true);
            uint32_t cond = opcode >> 28;

            uint32_t* patch_bypass = emit_cond_branch_over(cond);

            if ((opcode & 0x0E000000) == 0x0A000000) { // Branch Operations
                uint32_t link = (opcode & (1 << 24)) != 0;
                int32_t offset = opcode & 0xFFFFFF;
                if (offset & 0x800000) {
                    offset |= 0xFF000000;
                }
                offset <<= 2;
                uint32_t target = pc + 8 + offset;

                if (link) {
                    e.emit_load_imm(PPC_REG_LR, pc + 4);
                }
                
                e.emit_load_imm(3, target);
                e.lwz(4, PPC_REG_THIS, OFF_REGISTERS + 15 * 4);
                e.stw(3, 4, 0);

                block_ended = true;
            } 
            else if ((opcode & 0x0C000000) == 0x00000000) { // Data operations
                uint32_t op = (opcode >> 21) & 0xF;
                uint32_t rn = (opcode >> 16) & 0xF;
                uint32_t rd = (opcode >> 12) & 0xF;
                uint32_t op2 = opcode & 0xFFF;
                bool is_imm = (opcode & (1 << 25)) != 0;

                compile_arm_data_proc(op, rd, rn, op2, is_imm);
                if (rd == 15) {
                    block_ended = true;
                }
            }
            else if ((opcode & 0x0C000000) == 0x04000000) { // Load/Stores
                compile_arm_load_store(opcode);
                uint32_t rd = (opcode >> 12) & 0xF;
                if (rd == 15) {
                    block_ended = true;
                }
            }
            else {
                emit_fallback_interpreter_instruction(pc);
                block_ended = true;
            }

            if (patch_bypass) {
                patch_cond_branch(patch_bypass);
            }

            pc += 4;
            instructions_compiled++;
            accumulated_cycles += is_arm7 ? 2 : 1;
        }

        if (!block_ended) {
            e.emit_load_imm(3, pc);
            e.lwz(4, PPC_REG_THIS, OFF_REGISTERS + 15 * 4);
            e.stw(3, 4, 0);
        }

        emit_epilogue();

        uint32_t compiled_size = (uint32_t)(e.write_ptr - JIT_COMPILER_GET_START());
        e.flush_cache_range(JIT_COMPILER_GET_START(), compiled_size);

        return compiled_size;
    }

private:
    uint32_t* JIT_COMPILER_GET_START() {
        return block_start;
    }
};

// ============================================================================
// Core JIT Translation Engine Interface Hooks
// ============================================================================
void* jit_compile(Interpreter& interp, uint32_t pc, bool arm7, int cpu_id) {
    jit_init();

    if (jit_buffer_offset >= (JIT_BUFFER_SIZE / 4) - 2048) {
        jit_buffer_offset = 0;
        memset(jit_cache, 0, sizeof(jit_cache));
    }

    uint32_t* compile_dest = &jit_buffer[jit_buffer_offset];
    JitCompiler compiler(compile_dest, interp, arm7, pc);
    
    uint32_t compiled_bytes = compiler.compile_block();
    jit_buffer_offset += (compiled_bytes / 4) + 4;

    return (void*)compile_dest;
}

// ============================================================================
// Main JIT Hook Override
// ============================================================================
template <bool ARM7, int CPU>
void Interpreter::runCoreSingle(Core &core) {
    jit_init(); // Safe execution guard

    Interpreter &cpu = core.interpreter[CPU];
    if (cpu.halted) {
        cpu.cycles += cpu.dsiCycle ? 4 : 2;
        return;
    }

    uint32_t pc = *cpu.registers[15];
    uint32_t hash = (pc >> 2) & 0xFFFF;
    JitEntry &entry = jit_cache[CPU][hash];

    uint32_t raw_op = cpu.core->memory.read<uint32_t>(ARM7, pc, true);

    if (entry.arm_pc != pc || entry.arm_opcode_validation != raw_op) {
        entry.arm_pc = pc;
        entry.arm_opcode_validation = raw_op;
        entry.ppc_code = jit_compile(cpu, pc, ARM7, CPU);
    }

    typedef void (*JitBlockFunc)(Interpreter*, Core*);
    JitBlockFunc run_block = (JitBlockFunc)entry.ppc_code;
    run_block(&cpu, &core);
}

// Explicit template instantiations to resolve link requirements
template void Interpreter::runCoreSingle<false, 0>(Core &core);
template void Interpreter::runCoreSingle<true, 1>(Core &core);

#pragma GCC diagnostic pop

// ============================================================================
// Append Original Interpreter Tables for Unmapped Fallback Lookup Resolution
// ============================================================================
#define runCoreSingle dummy_runCoreSingle
#include "interpreter_lookup.cpp.original"
#undef runCoreSingle
