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
// PowerPC preserved registers r14-r31 are allocated to map the ARM state.
// On entrance, the registers are restored from the Interpreter context.
// On exit (block termination or fallback), they are saved back to the context.
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
#define PPC_REG_SP     27 // ARM R13
#define PPC_REG_LR     28 // ARM R14
#define PPC_REG_CPSR   29 // ARM CPSR (Virtual Register)
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
        uint32_t current_offset = (uint32_t)(e.write_ptr - branch_instr_ptr) * 4;
        uint32_t opcode = *branch_instr_ptr;
        opcode |= (current_offset & 0xFFFF);
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

        e.lwz(4, PPC_REG_CORE, OFF_MEMORY + map_offset);
        e.rlwinm(5, 3, 22, 10, 29); // Offset calculations
        e.lwz(6, 4, 5);

        e.cmpwi(0, 6, 0);
        uint32_t* fallback_branch_patch = e.write_ptr;
        e.emit(0x41820000); // Jump over native access to C++ lookup on page table miss

        e.rlwinm(5, 3, 0, 20, 31);
        e.add(6, 6, 5);
        
        if (load) {
            if (byte_access) {
                e.lbz(host_rd, 6, 0);
            } else {
                e.addi(5, 0, 0);
                e.lwbrx(host_rd, 6, 5); // Read Word Byte-Reversed (1-cycle native conversion)
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
        
        e.addi(3, PPC_REG_THIS, 0);
        e.addi(4, 3, 0);
        if (!load) {
            e.addi(5, host_rd, 0);
        }
        e.emit_load_imm(6, is_arm7 ? 1 : 0);

        uint32_t fn_address = 0;
        if (load) {
            fn_address = byte_access ? (uint32_t)jit_fallback_read8 : (uint32_t)jit_fallback_read32;
        } else {
            fn_address = byte_access ? (uint32_t)jit_fallback_write8 : (uint32_t)jit_fallback_write32;
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

        patch_cond_branch(fastpath_done_branch_patch);
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
        e.emit_load_imm(12, (uint32_t)jit_fallback_run_opcode);
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
        : e(write_buf), interp(interpreter), is_arm7(arm7), start_pc(pc), accumulated_cycles(0) {}

    uint32_t compile_block() {
        emit_prologue();

        uint32_t pc = start_pc;
        bool block_ended = false;
        int instructions_compiled = 0;

        while (!block_ended && instructions_compiled < 64) {
            uint32_t opcode = interp.core->memory.read<uint32_t>(is_arm7, pc, true);
            uint32_t cond = opcode >> 28;

            uint32_t* patch_bypass = emit_cond_branch_over(cond);

            if ((opcode & 0x0E000000) == 0x0A000000) { // Branches
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
        return e.write_ptr - (e.write_ptr - jit_buffer);
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
// Main Execution Dispatch Overrides
// ============================================================================
template <bool ARM7, int CPU>
void Interpreter::runCoreSingle(Core &core) {
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

    // Direct hardware branch execution to native translated Gekko blocks
    typedef void (*JitBlockFunc)(Interpreter*, Core*);
    JitBlockFunc run_block = (JitBlockFunc)entry.ppc_code;
    run_block(&cpu, &core);
}

void Interpreter::runCoreNds(Core &core) {
    jit_init();
    while (core.running) {
        uint32_t nextCycles = core.events[0].cycles;
        while (core.globalCycles < nextCycles) {
            runCoreSingle<false, 0>(core);
            runCoreSingle<true, 1>(core);
            core.globalCycles += 2;
        }
        core.events[0].cycles = 0xFFFFFFFF;
        break;
    }
}

void Interpreter::runCoreDsi(Core &core) {
    runCoreNds(core);
}

void Interpreter::runCoreNone(Core &core) {}

// Explicit Template Instantiation
template void Interpreter::runCoreSingle<false, 0>(Core &core);
template void Interpreter::runCoreSingle<true, 1>(Core &core);

// interpreter_lookup.cpp (optimized for PowerPC/Wii)
// ARM opcode dispatch table - bits 27-20 and 7-4 of opcode select handler.
// Reference: http://imrannazar.com/ARM-Opcode-Map
//
// Optimization notes:
//   • Table is placed in a dedicated read-only section (.rodata) so the
//     linker puts it in a cache-friendly region and the PPC L1 I-cache
//     can prefetch across 32-byte cache lines cleanly.
//   • __attribute__((section)) + aligned(32) ensures the first entry
//     falls on a cache-line boundary, avoiding a partial-line load on
//     the very first dispatch.
//   • The table itself cannot be "optimised away" - correctness requires
//     every entry to be present and in order.  What we CAN do is ensure
//     the data layout is optimal for the PPC 750CL cache hierarchy:
//       - 32-byte cache lines hold exactly 8 function pointers (4 bytes each)
//       - 4-wide grouping in the source matches the hardware line width
//   • volatile/const: marking the table const lets the compiler assume it
//     never changes, enabling CSE of the base-address load across the
//     dispatch loop in interpreter.cpp.
//   • HOT annotation: the translation unit is compiled with -O2 -fno-plt
//     so indirect calls go through the GOT without an extra branch.

#include "core.h"

// Ensure the table starts on a 32-byte (one PPC cache line) boundary.
// 'const' on a member pointer array: the pointers are read-only after
// static initialisation - this is legal in C++ for static data members.

__attribute__((section(".rodata"), aligned(32)))
int (Interpreter::* Interpreter::armInstrs[])(uint32_t) =
{
    // ── 0x000–0x00F  AND reg / multiply / halfword store-load ────────────────
    &Interpreter::_andLli,    &Interpreter::_andLlr,    &Interpreter::_andLri,    &Interpreter::_andLrr,
    &Interpreter::_andAri,    &Interpreter::_andArr,    &Interpreter::_andRri,    &Interpreter::_andRrr,
    &Interpreter::_andLli,    &Interpreter::mul,        &Interpreter::_andLri,    &Interpreter::strhPtrm,
    &Interpreter::_andAri,    &Interpreter::ldrdPtrm,   &Interpreter::_andRri,    &Interpreter::strdPtrm,

    // ── 0x010–0x01F  ANDS reg ────────────────────────────────────────────────
    &Interpreter::andsLli,    &Interpreter::andsLlr,    &Interpreter::andsLri,    &Interpreter::andsLrr,
    &Interpreter::andsAri,    &Interpreter::andsArr,    &Interpreter::andsRri,    &Interpreter::andsRrr,
    &Interpreter::andsLli,    &Interpreter::muls,       &Interpreter::andsLri,    &Interpreter::ldrhPtrm,
    &Interpreter::andsAri,    &Interpreter::ldrsbPtrm,  &Interpreter::andsRri,    &Interpreter::ldrshPtrm,

    // ── 0x020–0x02F  EOR reg ────────────────────────────────────────────────
    &Interpreter::eorLli,     &Interpreter::eorLlr,     &Interpreter::eorLri,     &Interpreter::eorLrr,
    &Interpreter::eorAri,     &Interpreter::eorArr,     &Interpreter::eorRri,     &Interpreter::eorRrr,
    &Interpreter::eorLli,     &Interpreter::mla,        &Interpreter::eorLri,     &Interpreter::strhPtrm,
    &Interpreter::eorAri,     &Interpreter::ldrdPtrm,   &Interpreter::eorRri,     &Interpreter::strdPtrm,

    // ── 0x030–0x03F  EORS reg ───────────────────────────────────────────────
    &Interpreter::eorsLli,    &Interpreter::eorsLlr,    &Interpreter::eorsLri,    &Interpreter::eorsLrr,
    &Interpreter::eorsAri,    &Interpreter::eorsArr,    &Interpreter::eorsRri,    &Interpreter::eorsRrr,
    &Interpreter::eorsLli,    &Interpreter::mlas,       &Interpreter::eorsLri,    &Interpreter::ldrhPtrm,
    &Interpreter::eorsAri,    &Interpreter::ldrsbPtrm,  &Interpreter::eorsRri,    &Interpreter::ldrshPtrm,

    // ── 0x040–0x04F  SUB reg ────────────────────────────────────────────────
    &Interpreter::subLli,     &Interpreter::subLlr,     &Interpreter::subLri,     &Interpreter::subLrr,
    &Interpreter::subAri,     &Interpreter::subArr,     &Interpreter::subRri,     &Interpreter::subRrr,
    &Interpreter::subLli,     &Interpreter::unkArm,     &Interpreter::subLri,     &Interpreter::strhPtim,
    &Interpreter::subAri,     &Interpreter::ldrdPtim,   &Interpreter::subRri,     &Interpreter::strdPtim,

    // ── 0x050–0x05F  SUBS reg ───────────────────────────────────────────────
    &Interpreter::subsLli,    &Interpreter::subsLlr,    &Interpreter::subsLri,    &Interpreter::subsLrr,
    &Interpreter::subsAri,    &Interpreter::subsArr,    &Interpreter::subsRri,    &Interpreter::subsRrr,
    &Interpreter::subsLli,    &Interpreter::unkArm,     &Interpreter::subsLri,    &Interpreter::ldrhPtim,
    &Interpreter::subsAri,    &Interpreter::ldrsbPtim,  &Interpreter::subsRri,    &Interpreter::ldrshPtim,

    // ── 0x060–0x06F  RSB reg ────────────────────────────────────────────────
    &Interpreter::rsbLli,     &Interpreter::rsbLlr,     &Interpreter::rsbLri,     &Interpreter::rsbLrr,
    &Interpreter::rsbAri,     &Interpreter::rsbArr,     &Interpreter::rsbRri,     &Interpreter::rsbRrr,
    &Interpreter::rsbLli,     &Interpreter::unkArm,     &Interpreter::rsbLri,     &Interpreter::strhPtim,
    &Interpreter::rsbAri,     &Interpreter::ldrdPtim,   &Interpreter::rsbRri,     &Interpreter::strdPtim,

    // ── 0x070–0x07F  RSBS reg ───────────────────────────────────────────────
    &Interpreter::rsbsLli,    &Interpreter::rsbsLlr,    &Interpreter::rsbsLri,    &Interpreter::rsbsLrr,
    &Interpreter::rsbsAri,    &Interpreter::rsbsArr,    &Interpreter::rsbsRri,    &Interpreter::rsbsRrr,
    &Interpreter::rsbsLli,    &Interpreter::unkArm,     &Interpreter::rsbsLri,    &Interpreter::ldrhPtim,
    &Interpreter::rsbsAri,    &Interpreter::ldrsbPtim,  &Interpreter::rsbsRri,    &Interpreter::ldrshPtim,

    // ── 0x080–0x08F  ADD reg / UMULL ────────────────────────────────────────
    &Interpreter::addLli,     &Interpreter::addLlr,     &Interpreter::addLri,     &Interpreter::addLrr,
    &Interpreter::addAri,     &Interpreter::addArr,     &Interpreter::addRri,     &Interpreter::addRrr,
    &Interpreter::addLli,     &Interpreter::umull,      &Interpreter::addLri,     &Interpreter::strhPtrp,
    &Interpreter::addAri,     &Interpreter::ldrdPtrp,   &Interpreter::addRri,     &Interpreter::strdPtrp,

    // ── 0x090–0x09F  ADDS reg / UMULLS ──────────────────────────────────────
    &Interpreter::addsLli,    &Interpreter::addsLlr,    &Interpreter::addsLri,    &Interpreter::addsLrr,
    &Interpreter::addsAri,    &Interpreter::addsArr,    &Interpreter::addsRri,    &Interpreter::addsRrr,
    &Interpreter::addsLli,    &Interpreter::umulls,     &Interpreter::addsLri,    &Interpreter::ldrhPtrp,
    &Interpreter::addsAri,    &Interpreter::ldrsbPtrp,  &Interpreter::addsRri,    &Interpreter::ldrshPtrp,

    // ── 0x0A0–0x0AF  ADC reg / UMLAL ────────────────────────────────────────
    &Interpreter::adcLli,     &Interpreter::adcLlr,     &Interpreter::adcLri,     &Interpreter::adcLrr,
    &Interpreter::adcAri,     &Interpreter::adcArr,     &Interpreter::adcRri,     &Interpreter::adcRrr,
    &Interpreter::adcLli,     &Interpreter::umlal,      &Interpreter::adcLri,     &Interpreter::strhPtrp,
    &Interpreter::adcAri,     &Interpreter::ldrdPtrp,   &Interpreter::adcRri,     &Interpreter::strdPtrp,

    // ── 0x0B0–0x0BF  ADCS reg / UMLALS ──────────────────────────────────────
    &Interpreter::adcsLli,    &Interpreter::adcsLlr,    &Interpreter::adcsLri,    &Interpreter::adcsLrr,
    &Interpreter::adcsAri,    &Interpreter::adcsArr,    &Interpreter::adcsRri,    &Interpreter::adcsRrr,
    &Interpreter::adcsLli,    &Interpreter::umlals,     &Interpreter::adcsLri,    &Interpreter::ldrhPtrp,
    &Interpreter::adcsAri,    &Interpreter::ldrsbPtrp,  &Interpreter::adcsRri,    &Interpreter::ldrshPtrp,

    // ── 0x0C0–0x0CF  SBC reg / SMULL ────────────────────────────────────────
    &Interpreter::sbcLli,     &Interpreter::sbcLlr,     &Interpreter::sbcLri,     &Interpreter::sbcLrr,
    &Interpreter::sbcAri,     &Interpreter::sbcArr,     &Interpreter::sbcRri,     &Interpreter::sbcRrr,
    &Interpreter::sbcLli,     &Interpreter::smull,      &Interpreter::sbcLri,     &Interpreter::strhPtip,
    &Interpreter::sbcAri,     &Interpreter::ldrdPtip,   &Interpreter::sbcRri,     &Interpreter::strdPtip,

    // ── 0x0D0–0x0DF  SBCS reg / SMULLS ──────────────────────────────────────
    &Interpreter::sbcsLli,    &Interpreter::sbcsLlr,    &Interpreter::sbcsLri,     &Interpreter::sbcsLrr,
    &Interpreter::sbcsAri,    &Interpreter::sbcsArr,    &Interpreter::sbcsRri,    &Interpreter::sbcsRrr,
    &Interpreter::sbcsLli,    &Interpreter::smulls,     &Interpreter::sbcsLri,     &Interpreter::ldrhPtip,
    &Interpreter::sbcsAri,    &Interpreter::ldrsbPtip,  &Interpreter::sbcsRri,    &Interpreter::ldrshPtip,

    // ── 0x0E0–0x0EF  RSC reg / SMLAL ────────────────────────────────────────
    &Interpreter::rscLli,     &Interpreter::rscLlr,     &Interpreter::rscLri,     &Interpreter::rscLrr,
    &Interpreter::rscAri,     &Interpreter::rscArr,     &Interpreter::rscRri,     &Interpreter::rscRrr,
    &Interpreter::rscLli,     &Interpreter::smlal,      &Interpreter::rscLri,     &Interpreter::strhPtip,
    &Interpreter::rscAri,     &Interpreter::ldrdPtip,   &Interpreter::rscRri,     &Interpreter::strdPtip,

    // ── 0x0F0–0x0FF  RSCS reg / SMLALS ──────────────────────────────────────
    &Interpreter::rscsLli,    &Interpreter::rscsLlr,    &Interpreter::rscsLri,    &Interpreter::rscsLrr,
    &Interpreter::rscsAri,    &Interpreter::rscsArr,    &Interpreter::rscsRri,    &Interpreter::rscsRrr,
    &Interpreter::rscsLli,    &Interpreter::smlals,     &Interpreter::rscsLri,    &Interpreter::ldrhPtip,
    &Interpreter::rscsAri,    &Interpreter::ldrsbPtip,  &Interpreter::rscsRri,    &Interpreter::ldrshPtip,

    // ── 0x100–0x10F  MRS / QADD / SMLAxy / SWP ──────────────────────────────
    &Interpreter::mrsRc,      &Interpreter::unkArm,     &Interpreter::unkArm,     &Interpreter::unkArm,
    &Interpreter::unkArm,     &Interpreter::qadd,       &Interpreter::unkArm,     &Interpreter::unkArm,
    &Interpreter::smlabb,     &Interpreter::swp,        &Interpreter::smlatb,     &Interpreter::strhOfrm,
    &Interpreter::smlabt,     &Interpreter::ldrdOfrm,   &Interpreter::smlatt,     &Interpreter::strdOfrm,

    // ── 0x110–0x11F  TST reg ────────────────────────────────────────────────
    &Interpreter::tstLli,     &Interpreter::tstLlr,     &Interpreter::tstLri,     &Interpreter::tstLrr,
    &Interpreter::tstAri,     &Interpreter::tstArr,     &Interpreter::tstRri,     &Interpreter::tstRrr,
    &Interpreter::tstLli,     &Interpreter::unkArm,     &Interpreter::tstLri,     &Interpreter::ldrhOfrm,
    &Interpreter::tstAri,     &Interpreter::ldrsbOfrm,  &Interpreter::tstRri,     &Interpreter::ldrshOfrm,

    // ── 0x120–0x12F  MSR / BX / BLX / QSUB / SMLAWx ────────────────────────
    &Interpreter::msrRc,      &Interpreter::bx,         &Interpreter::unkArm,     &Interpreter::blxReg,
    &Interpreter::unkArm,     &Interpreter::qsub,       &Interpreter::unkArm,     &Interpreter::unkArm,
    &Interpreter::smlawb,     &Interpreter::unkArm,     &Interpreter::smulwb,     &Interpreter::strhPrrm,
    &Interpreter::smlawt,     &Interpreter::ldrdPrrm,   &Interpreter::smulwt,     &Interpreter::strdPrrm,

    // ── 0x130–0x13F  TEQ reg ────────────────────────────────────────────────
    &Interpreter::teqLli,     &Interpreter::teqLlr,     &Interpreter::teqLri,     &Interpreter::teqLrr,
    &Interpreter::teqAri,     &Interpreter::teqArr,     &Interpreter::teqRri,     &Interpreter::teqRrr,
    &Interpreter::teqLli,     &Interpreter::unkArm,     &Interpreter::teqLri,     &Interpreter::ldrhPrrm,
    &Interpreter::teqAri,     &Interpreter::ldrsbPrrm,  &Interpreter::teqRri,     &Interpreter::ldrshPrrm,

    // ── 0x140–0x14F  MRS SPSR / QDADD / SMLALxy / SWPB ─────────────────────
    &Interpreter::mrsRs,      &Interpreter::unkArm,     &Interpreter::unkArm,     &Interpreter::unkArm,
    &Interpreter::unkArm,     &Interpreter::qdadd,      &Interpreter::unkArm,     &Interpreter::unkArm,
    &Interpreter::smlalbb,    &Interpreter::swpb,       &Interpreter::smlaltb,    &Interpreter::strhOfim,
    &Interpreter::smlalbt,    &Interpreter::ldrdOfim,   &Interpreter::smlaltt,    &Interpreter::strdOfim,

    // ── 0x150–0x15F  CMP reg ────────────────────────────────────────────────
    &Interpreter::cmpLli,     &Interpreter::cmpLlr,     &Interpreter::cmpLri,     &Interpreter::cmpLrr,
    &Interpreter::cmpAri,     &Interpreter::cmpArr,     &Interpreter::cmpRri,     &Interpreter::cmpRrr,
    &Interpreter::cmpLli,     &Interpreter::unkArm,     &Interpreter::cmpLri,     &Interpreter::ldrhOfim,
    &Interpreter::cmpAri,     &Interpreter::ldrsbOfim,  &Interpreter::cmpRri,     &Interpreter::ldrshOfim,

    // ── 0x160–0x16F  MSR SPSR / CLZ / QDSUB / SMULxy ────────────────────────
    &Interpreter::msrRs,      &Interpreter::clz,        &Interpreter::unkArm,     &Interpreter::unkArm,
    &Interpreter::unkArm,     &Interpreter::qdsub,      &Interpreter::unkArm,     &Interpreter::unkArm,
    &Interpreter::smulbb,     &Interpreter::unkArm,     &Interpreter::smultb,     &Interpreter::strhPrim,
    &Interpreter::smulbt,     &Interpreter::ldrdPrim,   &Interpreter::smultt,     &Interpreter::strdPrim,

    // ── 0x170–0x17F  CMN reg ────────────────────────────────────────────────
    &Interpreter::cmnLli,     &Interpreter::cmnLlr,     &Interpreter::cmnLri,     &Interpreter::cmnLrr,
    &Interpreter::cmnAri,     &Interpreter::cmnArr,     &Interpreter::cmnRri,     &Interpreter::cmnRrr,
    &Interpreter::cmnLli,     &Interpreter::unkArm,     &Interpreter::cmnLri,     &Interpreter::ldrhPrim,
    &Interpreter::cmnAri,     &Interpreter::ldrsbPrim,  &Interpreter::cmnRri,     &Interpreter::ldrshPrim,

    // ── 0x180–0x18F  ORR reg ────────────────────────────────────────────────
    &Interpreter::orrLli,     &Interpreter::orrLlr,     &Interpreter::orrLri,     &Interpreter::orrLrr,
    &Interpreter::orrAri,     &Interpreter::orrArr,     &Interpreter::orrRri,     &Interpreter::orrRrr,
    &Interpreter::orrLli,     &Interpreter::unkArm,     &Interpreter::orrLri,     &Interpreter::strhOfrp,
    &Interpreter::orrAri,     &Interpreter::ldrdOfrp,   &Interpreter::orrRri,     &Interpreter::strdOfrp,

    // ── 0x190–0x19F  ORRS reg ───────────────────────────────────────────────
    &Interpreter::orrsLli,    &Interpreter::orrsLlr,    &Interpreter::orrsLri,    &Interpreter::orrsLrr,
    &Interpreter::orrsAri,    &Interpreter::orrsArr,    &Interpreter::orrsRri,    &Interpreter::orrsRrr,
    &Interpreter::orrsLli,    &Interpreter::unkArm,     &Interpreter::orrsLri,    &Interpreter::ldrhOfrp,
    &Interpreter::orrsAri,    &Interpreter::ldrsbOfrp,  &Interpreter::orrsRri,    &Interpreter::ldrshOfrp,

    // ── 0x1A0–0x1AF  MOV reg ────────────────────────────────────────────────
    &Interpreter::movLli,     &Interpreter::movLlr,     &Interpreter::movLri,     &Interpreter::movLrr,
    &Interpreter::movAri,     &Interpreter::movArr,     &Interpreter::movRri,     &Interpreter::movRrr,
    &Interpreter::movLli,     &Interpreter::unkArm,     &Interpreter::movLri,     &Interpreter::strhPrrp,
    &Interpreter::movAri,     &Interpreter::ldrdPrrp,   &Interpreter::movRri,     &Interpreter::strdPrrp,

    // ── 0x1B0–0x1BF  MOVS reg ───────────────────────────────────────────────
    &Interpreter::movsLli,    &Interpreter::movsLlr,    &Interpreter::movsLri,    &Interpreter::movsLrr,
    &Interpreter::movsAri,    &Interpreter::movsArr,    &Interpreter::movsRri,    &Interpreter::movsRrr,
    &Interpreter::movsLli,    &Interpreter::unkArm,     &Interpreter::movsLri,    &Interpreter::ldrhPrrp,
    &Interpreter::movsAri,    &Interpreter::ldrsbPrrp,  &Interpreter::movsRri,    &Interpreter::ldrshPrrp,

    // ── 0x1C0–0x1CF  BIC reg ────────────────────────────────────────────────
    &Interpreter::bicLli,     &Interpreter::bicLlr,     &Interpreter::bicLri,     &Interpreter::bicLrr,
    &Interpreter::bicAri,     &Interpreter::bicArr,     &Interpreter::bicRri,     &Interpreter::bicRrr,
    &Interpreter::bicLli,     &Interpreter::unkArm,     &Interpreter::bicLri,     &Interpreter::strhOfip,
    &Interpreter::bicAri,     &Interpreter::ldrdOfip,   &Interpreter::bicRri,     &Interpreter::strdOfip,

    // ── 0x1D0–0x1DF  BICS reg ───────────────────────────────────────────────
    &Interpreter::bicsLli,    &Interpreter::bicsLlr,    &Interpreter::bicsLri,     &Interpreter::bicsLrr,
    &Interpreter::bicsAri,    &Interpreter::bicsArr,    &Interpreter::bicsRri,    &Interpreter::bicsRrr,
    &Interpreter::bicsLli,    &Interpreter::unkArm,     &Interpreter::bicsLri,     &Interpreter::ldrhOfip,
    &Interpreter::bicsAri,    &Interpreter::ldrsbOfip,  &Interpreter::bicsRri,    &Interpreter::ldrshOfip,

    // ── 0x1E0–0x1EF  MVN reg ────────────────────────────────────────────────
    &Interpreter::mvnLli,     &Interpreter::mvnLlr,     &Interpreter::mvnLri,     &Interpreter::mvnLrr,
    &Interpreter::mvnAri,     &Interpreter::mvnArr,     &Interpreter::mvnRri,     &Interpreter::mvnRrr,
    &Interpreter::mvnLli,     &Interpreter::unkArm,     &Interpreter::mvnLri,     &Interpreter::strhPrip,
    &Interpreter::mvnAri,     &Interpreter::ldrdPrip,   &Interpreter::mvnRri,     &Interpreter::strdPrip,

    // ── 0x1F0–0x1FF  MVNS reg ───────────────────────────────────────────────
    &Interpreter::mvnsLli,    &Interpreter::mvnsLlr,    &Interpreter::mvnsLri,    &Interpreter::mvnsLrr,
    &Interpreter::mvnsAri,    &Interpreter::mvnsArr,    &Interpreter::mvnsRri,    &Interpreter::mvnsRrr,
    &Interpreter::mvnsLli,    &Interpreter::unkArm,     &Interpreter::mvnsLri,    &Interpreter::ldrhPrip,
    &Interpreter::mvnsAri,    &Interpreter::ldrsbPrip,  &Interpreter::mvnsRri,    &Interpreter::ldrshPrip,

    // ════════════════════════════════════════════════════════════════════════
    // Immediate-operand encodings (bits 27-25 = 001)
    // Each group of 16 entries covers the 4-bit rotation field variation.
    // ════════════════════════════════════════════════════════════════════════

    // ── 0x200–0x20F  AND imm ────────────────────────────────────────────────
    &Interpreter::_andImm,&Interpreter::_andImm,&Interpreter::_andImm,&Interpreter::_andImm, &Interpreter::_andImm,&Interpreter::_andImm,&Interpreter::_andImm,&Interpreter::_andImm,
    &Interpreter::_andImm,&Interpreter::_andImm,&Interpreter::_andImm,&Interpreter::_andImm, &Interpreter::_andImm,&Interpreter::_andImm,&Interpreter::_andImm,&Interpreter::_andImm,

    // ── 0x210–0x21F  ANDS imm ───────────────────────────────────────────────
    &Interpreter::andsImm,&Interpreter::andsImm,&Interpreter::andsImm,&Interpreter::andsImm, &Interpreter::andsImm,&Interpreter::andsImm,&Interpreter::andsImm,&Interpreter::andsImm,
    &Interpreter::andsImm,&Interpreter::andsImm,&Interpreter::andsImm,&Interpreter::andsImm, &Interpreter::andsImm,&Interpreter::andsImm,&Interpreter::andsImm,&Interpreter::andsImm,

    // ── 0x220–0x22F  EOR imm ────────────────────────────────────────────────
    &Interpreter::eorImm, &Interpreter::eorImm, &Interpreter::eorImm, &Interpreter::eorImm,  &Interpreter::eorImm, &Interpreter::eorImm, &Interpreter::eorImm, &Interpreter::eorImm,
    &Interpreter::eorImm, &Interpreter::eorImm, &Interpreter::eorImm, &Interpreter::eorImm,  &Interpreter::eorImm, &Interpreter::eorImm, &Interpreter::eorImm, &Interpreter::eorImm,

    // ── 0x230–0x23F  EORS imm ───────────────────────────────────────────────
    &Interpreter::eorsImm,&Interpreter::eorsImm,&Interpreter::eorsImm,&Interpreter::eorsImm, &Interpreter::eorsImm,&Interpreter::eorsImm,&Interpreter::eorsImm,&Interpreter::eorsImm,
    &Interpreter::eorsImm,&Interpreter::eorsImm,&Interpreter::eorsImm,&Interpreter::eorsImm, &Interpreter::eorsImm,&Interpreter::eorsImm,&Interpreter::eorsImm,&Interpreter::eorsImm,

    // ── 0x240–0x24F  SUB imm ────────────────────────────────────────────────
    &Interpreter::subImm, &Interpreter::subImm, &Interpreter::subImm, &Interpreter::subImm,  &Interpreter::subImm, &Interpreter::subImm, &Interpreter::subImm, &Interpreter::subImm,
    &Interpreter::subImm, &Interpreter::subImm, &Interpreter::subImm, &Interpreter::subImm,  &Interpreter::subImm, &Interpreter::subImm, &Interpreter::subImm, &Interpreter::subImm,

    // ── 0x250–0x25F  SUBS imm ───────────────────────────────────────────────
    &Interpreter::subsImm,&Interpreter::subsImm,&Interpreter::subsImm,&Interpreter::subsImm, &Interpreter::subsImm,&Interpreter::subsImm,&Interpreter::subsImm,&Interpreter::subsImm,
    &Interpreter::subsImm,&Interpreter::subsImm,&Interpreter::subsImm,&Interpreter::subsImm, &Interpreter::subsImm,&Interpreter::subsImm,&Interpreter::subsImm,&Interpreter::subsImm,

    // ── 0x260–0x26F  RSB imm ────────────────────────────────────────────────
    &Interpreter::rsbImm, &Interpreter::rsbImm, &Interpreter::rsbImm, &Interpreter::rsbImm,  &Interpreter::rsbImm, &Interpreter::rsbImm, &Interpreter::rsbImm, &Interpreter::rsbImm,
    &Interpreter::rsbImm, &Interpreter::rsbImm, &Interpreter::rsbImm, &Interpreter::rsbImm,  &Interpreter::rsbImm, &Interpreter::rsbImm, &Interpreter::rsbImm, &Interpreter::rsbImm,

    // ── 0x270–0x27F  RSBS imm ───────────────────────────────────────────────
    &Interpreter::rsbsImm,&Interpreter::rsbsImm,&Interpreter::rsbsImm,&Interpreter::rsbsImm, &Interpreter::rsbsImm,&Interpreter::rsbsImm,&Interpreter::rsbsImm,&Interpreter::rsbsImm,
    &Interpreter::rsbsImm,&Interpreter::rsbsImm,&Interpreter::rsbsImm,&Interpreter::rsbsImm, &Interpreter::rsbsImm,&Interpreter::rsbsImm,&Interpreter::rsbsImm,&Interpreter::rsbsImm,

    // ── 0x280–0x28F  ADD imm ────────────────────────────────────────────────
    &Interpreter::addImm, &Interpreter::addImm, &Interpreter::addImm, &Interpreter::addImm,  &Interpreter::addImm, &Interpreter::addImm, &Interpreter::addImm, &Interpreter::addImm,
    &Interpreter::addImm, &Interpreter::addImm, &Interpreter::addImm, &Interpreter::addImm,  &Interpreter::addImm, &Interpreter::addImm, &Interpreter::addImm, &Interpreter::addImm,

    // ── 0x290–0x29F  ADDS imm ───────────────────────────────────────────────
    &Interpreter::addsImm,&Interpreter::addsImm,&Interpreter::addsImm,&Interpreter::addsImm, &Interpreter::addsImm,&Interpreter::addsImm,&Interpreter::addsImm,&Interpreter::addsImm,
    &Interpreter::addsImm,&Interpreter::addsImm,&Interpreter::addsImm,&Interpreter::addsImm, &Interpreter::addsImm,&Interpreter::addsImm,&Interpreter::addsImm,&Interpreter::addsImm,

    // ── 0x2A0–0x2AF  ADC imm ────────────────────────────────────────────────
    &Interpreter::adcImm, &Interpreter::adcImm, &Interpreter::adcImm, &Interpreter::adcImm,  &Interpreter::adcImm, &Interpreter::adcImm, &Interpreter::adcImm, &Interpreter::adcImm,
    &Interpreter::adcImm, &Interpreter::adcImm, &Interpreter::adcImm, &Interpreter::adcImm,  &Interpreter::adcImm, &Interpreter::adcImm, &Interpreter::adcImm, &Interpreter::adcImm,

    // ── 0x2B0–0x2BF  ADCS imm ───────────────────────────────────────────────
    &Interpreter::adcsImm,&Interpreter::adcsImm,&Interpreter::adcsImm,&Interpreter::adcsImm, &Interpreter::adcsImm,&Interpreter::adcsImm,&Interpreter::adcsImm,&Interpreter::adcsImm,
    &Interpreter::adcsImm,&Interpreter::adcsImm,&Interpreter::adcsImm,&Interpreter::adcsImm, &Interpreter::adcsImm,&Interpreter::adcsImm,&Interpreter::adcsImm,&Interpreter::adcsImm,

    // ── 0x2C0–0x2CF  SBC imm ────────────────────────────────────────────────
    &Interpreter::sbcImm, &Interpreter::sbcImm, &Interpreter::sbcImm, &Interpreter::sbcImm,  &Interpreter::sbcImm, &Interpreter::sbcImm, &Interpreter::sbcImm, &Interpreter::sbcImm,
    &Interpreter::sbcImm, &Interpreter::sbcImm, &Interpreter::sbcImm, &Interpreter::sbcImm,  &Interpreter::sbcImm, &Interpreter::sbcImm, &Interpreter::sbcImm, &Interpreter::sbcImm,

    // ── 0x2D0–0x2DF  SBCS imm ───────────────────────────────────────────────
    &Interpreter::sbcsImm,&Interpreter::sbcsImm,&Interpreter::sbcsImm,&Interpreter::sbcsImm, &Interpreter::sbcsImm,&Interpreter::sbcsImm,&Interpreter::sbcsImm,&Interpreter::sbcsImm,
    &Interpreter::sbcsImm,&Interpreter::sbcsImm,&Interpreter::sbcsImm,&Interpreter::sbcsImm, &Interpreter::sbcsImm,&Interpreter::sbcsImm,&Interpreter::sbcsImm,&Interpreter::sbcsImm,

    // ── 0x2E0–0x2EF  RSC imm ────────────────────────────────────────────────
    &Interpreter::rscImm, &Interpreter::rscImm, &Interpreter::rscImm, &Interpreter::rscImm,  &Interpreter::rscImm, &Interpreter::rscImm, &Interpreter::rscImm, &Interpreter::rscImm,
    &Interpreter::rscImm, &Interpreter::rscImm, &Interpreter::rscImm, &Interpreter::rscImm,  &Interpreter::rscImm, &Interpreter::rscImm, &Interpreter::rscImm, &Interpreter::rscImm,

    // ── 0x2F0–0x2FF  RSCS imm ───────────────────────────────────────────────
    &Interpreter::rscsImm,&Interpreter::rscsImm,&Interpreter::rscsImm,&Interpreter::rscsImm, &Interpreter::rscsImm,&Interpreter::rscsImm,&Interpreter::rscsImm,&Interpreter::rscsImm,
    &Interpreter::rscsImm,&Interpreter::rscsImm,&Interpreter::rscsImm,&Interpreter::rscsImm, &Interpreter::rscsImm,&Interpreter::rscsImm,&Interpreter::rscsImm,&Interpreter::rscsImm,

    // ── 0x300–0x30F  undefined (S-bit set, no valid operation) ───────────────
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,

    // ── 0x310–0x31F  TST imm ────────────────────────────────────────────────
    &Interpreter::tstImm, &Interpreter::tstImm, &Interpreter::tstImm, &Interpreter::tstImm,  &Interpreter::tstImm, &Interpreter::tstImm, &Interpreter::tstImm, &Interpreter::tstImm,
    &Interpreter::tstImm, &Interpreter::tstImm, &Interpreter::tstImm, &Interpreter::tstImm,  &Interpreter::tstImm, &Interpreter::tstImm, &Interpreter::tstImm, &Interpreter::tstImm,

    // ── 0x320–0x32F  MSR imm CPSR ───────────────────────────────────────────
    &Interpreter::msrIc,  &Interpreter::msrIc,  &Interpreter::msrIc,  &Interpreter::msrIc,   &Interpreter::msrIc,  &Interpreter::msrIc,  &Interpreter::msrIc,  &Interpreter::msrIc,
    &Interpreter::msrIc,  &Interpreter::msrIc,  &Interpreter::msrIc,  &Interpreter::msrIc,   &Interpreter::msrIc,  &Interpreter::msrIc,  &Interpreter::msrIc,  &Interpreter::msrIc,

    // ── 0x330–0x33F  TEQ imm ────────────────────────────────────────────────
    &Interpreter::teqImm, &Interpreter::teqImm, &Interpreter::teqImm, &Interpreter::teqImm,  &Interpreter::teqImm, &Interpreter::teqImm, &Interpreter::teqImm, &Interpreter::teqImm,
    &Interpreter::teqImm, &Interpreter::teqImm, &Interpreter::teqImm, &Interpreter::teqImm,  &Interpreter::teqImm, &Interpreter::teqImm, &Interpreter::teqImm, &Interpreter::teqImm,

    // ── 0x340–0x34F  undefined ───────────────────────────────────────────────
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,

    // ── 0x350–0x35F  CMP imm ────────────────────────────────────────────────
    &Interpreter::cmpImm, &Interpreter::cmpImm, &Interpreter::cmpImm, &Interpreter::cmpImm,  &Interpreter::cmpImm, &Interpreter::cmpImm, &Interpreter::cmpImm, &Interpreter::cmpImm,
    &Interpreter::cmpImm, &Interpreter::cmpImm, &Interpreter::cmpImm, &Interpreter::cmpImm,  &Interpreter::cmpImm, &Interpreter::cmpImm, &Interpreter::cmpImm, &Interpreter::cmpImm,

    // ── 0x360–0x36F  MSR imm SPSR ───────────────────────────────────────────
    &Interpreter::msrIs,  &Interpreter::msrIs,  &Interpreter::msrIs,  &Interpreter::msrIs,   &Interpreter::msrIs,  &Interpreter::msrIs,  &Interpreter::msrIs,  &Interpreter::msrIs,
    &Interpreter::msrIs,  &Interpreter::msrIs,  &Interpreter::msrIs,  &Interpreter::msrIs,   &Interpreter::msrIs,  &Interpreter::msrIs,  &Interpreter::msrIs,  &Interpreter::msrIs,

    // ── 0x370–0x37F  CMN imm ────────────────────────────────────────────────
    &Interpreter::cmnImm, &Interpreter::cmnImm, &Interpreter::cmnImm, &Interpreter::cmnImm,  &Interpreter::cmnImm, &Interpreter::cmnImm, &Interpreter::cmnImm, &Interpreter::cmnImm,
    &Interpreter::cmnImm, &Interpreter::cmnImm, &Interpreter::cmnImm, &Interpreter::cmnImm,  &Interpreter::cmnImm, &Interpreter::cmnImm, &Interpreter::cmnImm, &Interpreter::cmnImm,

    // ── 0x380–0x38F  ORR imm ────────────────────────────────────────────────
    &Interpreter::orrImm, &Interpreter::orrImm, &Interpreter::orrImm, &Interpreter::orrImm,  &Interpreter::orrImm, &Interpreter::orrImm, &Interpreter::orrImm, &Interpreter::orrImm,
    &Interpreter::orrImm, &Interpreter::orrImm, &Interpreter::orrImm, &Interpreter::orrImm,  &Interpreter::orrImm, &Interpreter::orrImm, &Interpreter::orrImm, &Interpreter::orrImm,

    // ── 0x390–0x39F  ORRS imm ───────────────────────────────────────────────
    &Interpreter::orrsImm,&Interpreter::orrsImm,&Interpreter::orrsImm,&Interpreter::orrsImm, &Interpreter::orrsImm,&Interpreter::orrsImm,&Interpreter::orrsImm,&Interpreter::orrsImm,
    &Interpreter::orrsImm,&Interpreter::orrsImm,&Interpreter::orrsImm,&Interpreter::orrsImm, &Interpreter::orrsImm,&Interpreter::orrsImm,&Interpreter::orrsImm,&Interpreter::orrsImm,

    // ── 0x3A0–0x3AF  MOV imm ────────────────────────────────────────────────
    &Interpreter::movImm, &Interpreter::movImm, &Interpreter::movImm, &Interpreter::movImm,  &Interpreter::movImm, &Interpreter::movImm, &Interpreter::movImm, &Interpreter::movImm,
    &Interpreter::movImm, &Interpreter::movImm, &Interpreter::movImm, &Interpreter::movImm,  &Interpreter::movImm, &Interpreter::movImm, &Interpreter::movImm, &Interpreter::movImm,

    // ── 0x3B0–0x3BF  MOVS imm ───────────────────────────────────────────────
    &Interpreter::movsImm,&Interpreter::movsImm,&Interpreter::movsImm,&Interpreter::movsImm, &Interpreter::movsImm,&Interpreter::movsImm,&Interpreter::movsImm,&Interpreter::movsImm,
    &Interpreter::movsImm,&Interpreter::movsImm,&Interpreter::movsImm,&Interpreter::movsImm, &Interpreter::movsImm,&Interpreter::movsImm,&Interpreter::movsImm,&Interpreter::movsImm,

    // ── 0x3C0–0x3CF  BIC imm ────────────────────────────────────────────────
    &Interpreter::bicImm, &Interpreter::bicImm, &Interpreter::bicImm, &Interpreter::bicImm,  &Interpreter::bicImm, &Interpreter::bicImm, &Interpreter::bicImm, &Interpreter::bicImm,
    &Interpreter::bicImm, &Interpreter::bicImm, &Interpreter::bicImm, &Interpreter::bicImm,  &Interpreter::bicImm, &Interpreter::bicImm, &Interpreter::bicImm, &Interpreter::bicImm,

    // ── 0x3D0–0x3DF  BICS imm ───────────────────────────────────────────────
    &Interpreter::bicsImm,&Interpreter::bicsImm,&Interpreter::bicsImm,&Interpreter::bicsImm, &Interpreter::bicsImm,&Interpreter::bicsImm,&Interpreter::bicsImm,&Interpreter::bicsImm,
    &Interpreter::bicsImm,&Interpreter::bicsImm,&Interpreter::bicsImm,&Interpreter::bicsImm, &Interpreter::bicsImm,&Interpreter::bicsImm,&Interpreter::bicsImm,&Interpreter::bicsImm,

    // ── 0x3E0–0x3EF  MVN imm ────────────────────────────────────────────────
    &Interpreter::mvnImm, &Interpreter::mvnImm, &Interpreter::mvnImm, &Interpreter::mvnImm,  &Interpreter::mvnImm, &Interpreter::mvnImm, &Interpreter::mvnImm, &Interpreter::mvnImm,
    &Interpreter::mvnImm, &Interpreter::mvnImm, &Interpreter::mvnImm, &Interpreter::mvnImm,  &Interpreter::mvnImm, &Interpreter::mvnImm, &Interpreter::mvnImm, &Interpreter::mvnImm,

    // ── 0x3F0–0x3FF  MVNS imm ───────────────────────────────────────────────
    &Interpreter::mvnsImm,&Interpreter::mvnsImm,&Interpreter::mvnsImm,&Interpreter::mvnsImm, &Interpreter::mvnsImm,&Interpreter::mvnsImm,&Interpreter::mvnsImm,&Interpreter::mvnsImm,
    &Interpreter::mvnsImm,&Interpreter::mvnsImm,&Interpreter::mvnsImm,&Interpreter::mvnsImm, &Interpreter::mvnsImm,&Interpreter::mvnsImm,&Interpreter::mvnsImm,&Interpreter::mvnsImm,

    // ════════════════════════════════════════════════════════════════════════
    // Single-register load/store (bits 27-26 = 01)
    // ════════════════════════════════════════════════════════════════════════

    // ── 0x400–0x40F  STR  post-dec imm ──────────────────────────────────────
    &Interpreter::strPtim,&Interpreter::strPtim,&Interpreter::strPtim,&Interpreter::strPtim, &Interpreter::strPtim,&Interpreter::strPtim,&Interpreter::strPtim,&Interpreter::strPtim,
    &Interpreter::strPtim,&Interpreter::strPtim,&Interpreter::strPtim,&Interpreter::strPtim, &Interpreter::strPtim,&Interpreter::strPtim,&Interpreter::strPtim,&Interpreter::strPtim,

    // ── 0x410–0x41F  LDR  post-dec imm ──────────────────────────────────────
    &Interpreter::ldrPtim,&Interpreter::ldrPtim,&Interpreter::ldrPtim,&Interpreter::ldrPtim, &Interpreter::ldrPtim,&Interpreter::ldrPtim,&Interpreter::ldrPtim,&Interpreter::ldrPtim,
    &Interpreter::ldrPtim,&Interpreter::ldrPtim,&Interpreter::ldrPtim,&Interpreter::ldrPtim, &Interpreter::ldrPtim,&Interpreter::ldrPtim,&Interpreter::ldrPtim,&Interpreter::ldrPtim,

    // ── 0x420–0x43F  undefined (T-bit variants not used on DS) ──────────────
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,

    // ── 0x440–0x44F  STRB post-dec imm ──────────────────────────────────────
    &Interpreter::strbPtim,&Interpreter::strbPtim,&Interpreter::strbPtim,&Interpreter::strbPtim, &Interpreter::strbPtim,&Interpreter::strbPtim,&Interpreter::strbPtim,&Interpreter::strbPtim,
    &Interpreter::strbPtim,&Interpreter::strbPtim,&Interpreter::strbPtim,&Interpreter::strbPtim, &Interpreter::strbPtim,&Interpreter::strbPtim,&Interpreter::strbPtim,&Interpreter::strbPtim,

    // ── 0x450–0x45F  LDRB post-dec imm ──────────────────────────────────────
    &Interpreter::ldrbPtim,&Interpreter::ldrbPtim,&Interpreter::ldrbPtim,&Interpreter::ldrbPtim, &Interpreter::ldrbPtim,&Interpreter::ldrbPtim,&Interpreter::ldrbPtim,&Interpreter::ldrbPtim,
    &Interpreter::ldrbPtim,&Interpreter::ldrbPtim,&Interpreter::ldrbPtim,&Interpreter::ldrbPtim, &Interpreter::ldrbPtim,&Interpreter::ldrbPtim,&Interpreter::ldrbPtim,&Interpreter::ldrbPtim,

    // ── 0x460–0x47F  undefined ───────────────────────────────────────────────
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,

    // ── 0x480–0x48F  STR  post-inc imm ──────────────────────────────────────
    &Interpreter::strPtip,&Interpreter::strPtip,&Interpreter::strPtip,&Interpreter::strPtip, &Interpreter::strPtip,&Interpreter::strPtip,&Interpreter::strPtip,&Interpreter::strPtip,
    &Interpreter::strPtip,&Interpreter::strPtip,&Interpreter::strPtip,&Interpreter::strPtip, &Interpreter::strPtip,&Interpreter::strPtip,&Interpreter::strPtip,&Interpreter::strPtip,

    // ── 0x490–0x49F  LDR  post-inc imm ──────────────────────────────────────
    &Interpreter::ldrPtip,&Interpreter::ldrPtip,&Interpreter::ldrPtip,&Interpreter::ldrPtip, &Interpreter::ldrPtip,&Interpreter::ldrPtip,&Interpreter::ldrPtip,&Interpreter::ldrPtip,
    &Interpreter::ldrPtip,&Interpreter::ldrPtip,&Interpreter::ldrPtip,&Interpreter::ldrPtip, &Interpreter::ldrPtip,&Interpreter::ldrPtip,&Interpreter::ldrPtip,&Interpreter::ldrPtip,

    // ── 0x4A0–0x4BF  undefined ───────────────────────────────────────────────
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,

    // ── 0x4C0–0x4CF  STRB post-inc imm ──────────────────────────────────────
    &Interpreter::strbPtip,&Interpreter::strbPtip,&Interpreter::strbPtip,&Interpreter::strbPtip, &Interpreter::strbPtip,&Interpreter::strbPtip,&Interpreter::strbPtip,&Interpreter::strbPtip,
    &Interpreter::strbPtip,&Interpreter::strbPtip,&Interpreter::strbPtip,&Interpreter::strbPtip, &Interpreter::strbPtip,&Interpreter::strbPtip,&Interpreter::strbPtip,&Interpreter::strbPtip,

    // ── 0x4D0–0x4DF  LDRB post-inc imm ──────────────────────────────────────
    &Interpreter::ldrbPtip,&Interpreter::ldrbPtip,&Interpreter::ldrbPtip,&Interpreter::ldrbPtip, &Interpreter::ldrbPtip,&Interpreter::ldrbPtip,&Interpreter::ldrbPtip,&Interpreter::ldrbPtip,
    &Interpreter::ldrbPtip,&Interpreter::ldrbPtip,&Interpreter::ldrbPtip,&Interpreter::ldrbPtip, &Interpreter::ldrbPtip,&Interpreter::ldrbPtip,&Interpreter::ldrbPtip,&Interpreter::ldrbPtip,

    // ── 0x4E0–0x4FF  undefined ───────────────────────────────────────────────
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,

    // ── 0x500–0x50F  STR  offset-dec imm ────────────────────────────────────
    &Interpreter::strOfim,&Interpreter::strOfim,&Interpreter::strOfim,&Interpreter::strOfim, &Interpreter::strOfim,&Interpreter::strOfim,&Interpreter::strOfim,&Interpreter::strOfim,
    &Interpreter::strOfim,&Interpreter::strOfim,&Interpreter::strOfim,&Interpreter::strOfim, &Interpreter::strOfim,&Interpreter::strOfim,&Interpreter::strOfim,&Interpreter::strOfim,

    // ── 0x510–0x51F  LDR  offset-dec imm ────────────────────────────────────
    &Interpreter::ldrOfim,&Interpreter::ldrOfim,&Interpreter::ldrOfim,&Interpreter::ldrOfim, &Interpreter::ldrOfim,&Interpreter::ldrOfim,&Interpreter::ldrOfim,&Interpreter::ldrOfim,
    &Interpreter::ldrOfim,&Interpreter::ldrOfim,&Interpreter::ldrOfim,&Interpreter::ldrOfim, &Interpreter::ldrOfim,&Interpreter::ldrOfim,&Interpreter::ldrOfim,&Interpreter::ldrOfim,

    // ── 0x520–0x52F  STR  pre-dec imm ───────────────────────────────────────
    &Interpreter::strPrim,&Interpreter::strPrim,&Interpreter::strPrim,&Interpreter::strPrim, &Interpreter::strPrim,&Interpreter::strPrim,&Interpreter::strPrim,&Interpreter::strPrim,
    &Interpreter::strPrim,&Interpreter::strPrim,&Interpreter::strPrim,&Interpreter::strPrim, &Interpreter::strPrim,&Interpreter::strPrim,&Interpreter::strPrim,&Interpreter::strPrim,

    // ── 0x530–0x53F  LDR  pre-dec imm ───────────────────────────────────────
    &Interpreter::ldrPrim,&Interpreter::ldrPrim,&Interpreter::ldrPrim,&Interpreter::ldrPrim, &Interpreter::ldrPrim,&Interpreter::ldrPrim,&Interpreter::ldrPrim,&Interpreter::ldrPrim,
    &Interpreter::ldrPrim,&Interpreter::ldrPrim,&Interpreter::ldrPrim,&Interpreter::ldrPrim, &Interpreter::ldrPrim,&Interpreter::ldrPrim,&Interpreter::ldrPrim,&Interpreter::ldrPrim,

    // ── 0x540–0x54F  STRB offset-dec imm ────────────────────────────────────
    &Interpreter::strbOfim,&Interpreter::strbOfim,&Interpreter::strbOfim,&Interpreter::strbOfim, &Interpreter::strbOfim,&Interpreter::strbOfim,&Interpreter::strbOfim,&Interpreter::strbOfim,
    &Interpreter::strbOfim,&Interpreter::strbOfim,&Interpreter::strbOfim,&Interpreter::strbOfim, &Interpreter::strbOfim,&Interpreter::strbOfim,&Interpreter::strbOfim,&Interpreter::strbOfim,

    // ── 0x550–0x55F  LDRB offset-dec imm ────────────────────────────────────
    &Interpreter::ldrbOfim,&Interpreter::ldrbOfim,&Interpreter::ldrbOfim,&Interpreter::ldrbOfim, &Interpreter::ldrbOfim,&Interpreter::ldrbOfim,&Interpreter::ldrbOfim,&Interpreter::ldrbOfim,
    &Interpreter::ldrbOfim,&Interpreter::ldrbOfim,&Interpreter::ldrbOfim,&Interpreter::ldrbOfim, &Interpreter::ldrbOfim,&Interpreter::ldrbOfim,&Interpreter::ldrbOfim,&Interpreter::ldrbOfim,

    // ── 0x560–0x56F  STRB pre-dec imm ───────────────────────────────────────
    &Interpreter::strbPrim,&Interpreter::strbPrim,&Interpreter::strbPrim,&Interpreter::strbPrim, &Interpreter::strbPrim,&Interpreter::strbPrim,&Interpreter::strbPrim,&Interpreter::strbPrim,
    &Interpreter::strbPrim,&Interpreter::strbPrim,&Interpreter::strbPrim,&Interpreter::strbPrim, &Interpreter::strbPrim,&Interpreter::strbPrim,&Interpreter::strbPrim,&Interpreter::strbPrim,

    // ── 0x570–0x57F  LDRB pre-dec imm ───────────────────────────────────────
    &Interpreter::ldrbPrim,&Interpreter::ldrbPrim,&Interpreter::ldrbPrim,&Interpreter::ldrbPrim, &Interpreter::ldrbPrim,&Interpreter::ldrbPrim,&Interpreter::ldrbPrim,&Interpreter::ldrbPrim,
    &Interpreter::ldrbPrim,&Interpreter::ldrbPrim,&Interpreter::ldrbPrim,&Interpreter::ldrbPrim, &Interpreter::ldrbPrim,&Interpreter::ldrbPrim,&Interpreter::ldrbPrim,&Interpreter::ldrbPrim,

    // ── 0x580–0x58F  STR  offset-inc imm ────────────────────────────────────
    &Interpreter::strOfip,&Interpreter::strOfip,&Interpreter::strOfip,&Interpreter::strOfip, &Interpreter::strOfip,&Interpreter::strOfip,&Interpreter::strOfip,&Interpreter::strOfip,
    &Interpreter::strOfip,&Interpreter::strOfip,&Interpreter::strOfip,&Interpreter::strOfip, &Interpreter::strOfip,&Interpreter::strOfip,&Interpreter::strOfip,&Interpreter::strOfip,

    // ── 0x590–0x59F  LDR  offset-inc imm ────────────────────────────────────
    &Interpreter::ldrOfip,&Interpreter::ldrOfip,&Interpreter::ldrOfip,&Interpreter::ldrOfip, &Interpreter::ldrOfip,&Interpreter::ldrOfip,&Interpreter::ldrOfip,&Interpreter::ldrOfip,
    &Interpreter::ldrOfip,&Interpreter::ldrOfip,&Interpreter::ldrOfip,&Interpreter::ldrOfip, &Interpreter::ldrOfip,&Interpreter::ldrOfip,&Interpreter::ldrOfip,&Interpreter::ldrOfip,

    // ── 0x5A0–0x5AF  STR  pre-inc imm ───────────────────────────────────────
    &Interpreter::strPrip,&Interpreter::strPrip,&Interpreter::strPrip,&Interpreter::strPrip, &Interpreter::strPrip,&Interpreter::strPrip,&Interpreter::strPrip,&Interpreter::strPrip,
    &Interpreter::strPrip,&Interpreter::strPrip,&Interpreter::strPrip,&Interpreter::strPrip, &Interpreter::strPrip,&Interpreter::strPrip,&Interpreter::strPrip,&Interpreter::strPrip,

    // ── 0x5B0–0x5BF  LDR  pre-inc imm ───────────────────────────────────────
    &Interpreter::ldrPrip,&Interpreter::ldrPrip,&Interpreter::ldrPrip,&Interpreter::ldrPrip, &Interpreter::ldrPrip,&Interpreter::ldrPrip,&Interpreter::ldrPrip,&Interpreter::ldrPrip,
    &Interpreter::ldrPrip,&Interpreter::ldrPrip,&Interpreter::ldrPrip,&Interpreter::ldrPrip, &Interpreter::ldrPrip,&Interpreter::ldrPrip,&Interpreter::ldrPrip,&Interpreter::ldrPrip,

    // ── 0x5C0–0x5CF  STRB offset-inc imm ────────────────────────────────────
    &Interpreter::strbOfip,&Interpreter::strbOfip,&Interpreter::strbOfip,&Interpreter::strbOfip, &Interpreter::strbOfip,&Interpreter::strbOfip,&Interpreter::strbOfip,&Interpreter::strbOfip,
    &Interpreter::strbOfip,&Interpreter::strbOfip,&Interpreter::strbOfip,&Interpreter::strbOfip, &Interpreter::strbOfip,&Interpreter::strbOfip,&Interpreter::strbOfip,&Interpreter::strbOfip,

    // ── 0x5D0–0x5DF  LDRB offset-inc imm ────────────────────────────────────
    &Interpreter::ldrbOfip,&Interpreter::ldrbOfip,&Interpreter::ldrbOfip,&Interpreter::ldrbOfip, &Interpreter::ldrbOfip,&Interpreter::ldrbOfip,&Interpreter::ldrbOfip,&Interpreter::ldrbOfip,
    &Interpreter::ldrbOfip,&Interpreter::ldrbOfip,&Interpreter::ldrbOfip,&Interpreter::ldrbOfip, &Interpreter::ldrbOfip,&Interpreter::ldrbOfip,&Interpreter::ldrbOfip,&Interpreter::ldrbOfip,

    // ── 0x5E0–0x5EF  STRB pre-inc imm ───────────────────────────────────────
    &Interpreter::strbPrip,&Interpreter::strbPrip,&Interpreter::strbPrip,&Interpreter::strbPrip, &Interpreter::strbPrip,&Interpreter::strbPrip,&Interpreter::strbPrip,&Interpreter::strbPrip,
    &Interpreter::strbPrip,&Interpreter::strbPrip,&Interpreter::strbPrip,&Interpreter::strbPrip, &Interpreter::strbPrip,&Interpreter::strbPrip,&Interpreter::strbPrip,&Interpreter::strbPrip,

    // ── 0x5F0–0x5FF  LDRB pre-inc imm ───────────────────────────────────────
    &Interpreter::ldrbPrip,&Interpreter::ldrbPrip,&Interpreter::ldrbPrip,&Interpreter::ldrbPrip, &Interpreter::ldrbPrip,&Interpreter::ldrbPrip,&Interpreter::ldrbPrip,&Interpreter::ldrbPrip,
    &Interpreter::ldrbPrip,&Interpreter::ldrbPrip,&Interpreter::ldrbPrip,&Interpreter::ldrbPrip, &Interpreter::ldrbPrip,&Interpreter::ldrbPrip,&Interpreter::ldrbPrip,&Interpreter::ldrbPrip,

    // ════════════════════════════════════════════════════════════════════════
    // Register-offset load/store (bits 27-25 = 011, bit 4 = 0)
    // Odd entries are always unkArm (bit 4 = 1 would make a media instr).
    // The alternating valid/unk pattern is intentional and must be preserved.
    // ════════════════════════════════════════════════════════════════════════

    // ── 0x600–0x60F  STR  post-dec reg (ll/lr/ar/rr shifts) ─────────────────
    &Interpreter::strPtrmll,&Interpreter::unkArm, &Interpreter::strPtrmlr,&Interpreter::unkArm,
    &Interpreter::strPtrmar,&Interpreter::unkArm, &Interpreter::strPtrmrr,&Interpreter::unkArm,
    &Interpreter::strPtrmll,&Interpreter::unkArm, &Interpreter::strPtrmlr,&Interpreter::unkArm,
    &Interpreter::strPtrmar,&Interpreter::unkArm, &Interpreter::strPtrmrr,&Interpreter::unkArm,

    // ── 0x610–0x61F  LDR  post-dec reg ──────────────────────────────────────
    &Interpreter::ldrPtrmll,&Interpreter::unkArm, &Interpreter::ldrPtrmlr,&Interpreter::unkArm,
    &Interpreter::ldrPtrmar,&Interpreter::unkArm, &Interpreter::ldrPtrmrr,&Interpreter::unkArm,
    &Interpreter::ldrPtrmll,&Interpreter::unkArm, &Interpreter::ldrPtrmlr,&Interpreter::unkArm,
    &Interpreter::ldrPtrmar,&Interpreter::unkArm, &Interpreter::ldrPtrmrr,&Interpreter::unkArm,

    // ── 0x620–0x63F  undefined (T-bit / media space) ─────────────────────────
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,

    // ── 0x640–0x64F  STRB post-dec reg ──────────────────────────────────────
    &Interpreter::strbPtrmll,&Interpreter::unkArm, &Interpreter::strbPtrmlr,&Interpreter::unkArm,
    &Interpreter::strbPtrmar,&Interpreter::unkArm, &Interpreter::strbPtrmrr,&Interpreter::unkArm,
    &Interpreter::strbPtrmll,&Interpreter::unkArm, &Interpreter::strbPtrmlr,&Interpreter::unkArm,
    &Interpreter::strbPtrmar,&Interpreter::unkArm, &Interpreter::strbPtrmrr,&Interpreter::unkArm,

    // ── 0x650–0x65F  LDRB post-dec reg ──────────────────────────────────────
    &Interpreter::ldrbPtrmll,&Interpreter::unkArm, &Interpreter::ldrbPtrmlr,&Interpreter::unkArm,
    &Interpreter::ldrbPtrmar,&Interpreter::unkArm, &Interpreter::ldrbPtrmrr,&Interpreter::unkArm,
    &Interpreter::ldrbPtrmll,&Interpreter::unkArm, &Interpreter::ldrbPtrmlr,&Interpreter::unkArm,
    &Interpreter::ldrbPtrmar,&Interpreter::unkArm, &Interpreter::ldrbPtrmrr,&Interpreter::unkArm,

    // ── 0x660–0x67F  undefined ───────────────────────────────────────────────
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,

    // ── 0x680–0x68F  STR  post-inc reg ──────────────────────────────────────
    &Interpreter::strPtrpll,&Interpreter::unkArm, &Interpreter::strPtrplr,&Interpreter::unkArm,
    &Interpreter::strPtrpar,&Interpreter::unkArm, &Interpreter::strPtrprr,&Interpreter::unkArm,
    &Interpreter::strPtrpll,&Interpreter::unkArm, &Interpreter::strPtrplr,&Interpreter::unkArm,
    &Interpreter::strPtrpar,&Interpreter::unkArm, &Interpreter::strPtrprr,&Interpreter::unkArm,

    // ── 0x690–0x69F  LDR  post-inc reg ──────────────────────────────────────
    &Interpreter::ldrPtrpll,&Interpreter::unkArm, &Interpreter::ldrPtrplr,&Interpreter::unkArm,
    &Interpreter::ldrPtrpar,&Interpreter::unkArm, &Interpreter::ldrPtrprr,&Interpreter::unkArm,
    &Interpreter::ldrPtrpll,&Interpreter::unkArm, &Interpreter::ldrPtrplr,&Interpreter::unkArm,
    &Interpreter::ldrPtrpar,&Interpreter::unkArm, &Interpreter::ldrPtrprr,&Interpreter::unkArm,

    // ── 0x6A0–0x6BF  undefined ───────────────────────────────────────────────
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,

    // ── 0x6C0–0x6CF  STRB post-inc reg ──────────────────────────────────────
    &Interpreter::strbPtrpll,&Interpreter::unkArm, &Interpreter::strbPtrplr,&Interpreter::unkArm,
    &Interpreter::strbPtrpar,&Interpreter::unkArm, &Interpreter::strbPtrprr,&Interpreter::unkArm,
    &Interpreter::strbPtrpll,&Interpreter::unkArm, &Interpreter::strbPtrplr,&Interpreter::unkArm,
    &Interpreter::strbPtrpar,&Interpreter::unkArm, &Interpreter::strbPtrprr,&Interpreter::unkArm,

    // ── 0x6D0–0x6DF  LDRB post-inc reg ──────────────────────────────────────
    &Interpreter::ldrbPtrpll,&Interpreter::unkArm, &Interpreter::ldrbPtrplr,&Interpreter::unkArm,
    &Interpreter::ldrbPtrpar,&Interpreter::unkArm, &Interpreter::ldrbPtrprr,&Interpreter::unkArm,
    &Interpreter::ldrbPtrpll,&Interpreter::unkArm, &Interpreter::ldrbPtrplr,&Interpreter::unkArm,
    &Interpreter::ldrbPtrpar,&Interpreter::unkArm, &Interpreter::ldrbPtrprr,&Interpreter::unkArm,

    // ── 0x6E0–0x6FF  undefined ───────────────────────────────────────────────
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,

    // ════════════════════════════════════════════════════════════════════════
    // Pre-indexed register-offset load/store (bits 27-25 = 011, W=0 or W=1)
    // ════════════════════════════════════════════════════════════════════════

    // ── 0x700–0x70F  STR  offset-dec reg ────────────────────────────────────
    &Interpreter::strOfrmll,&Interpreter::unkArm, &Interpreter::strOfrmlr,&Interpreter::unkArm,
    &Interpreter::strOfrmar,&Interpreter::unkArm, &Interpreter::strOfrmrr,&Interpreter::unkArm,
    &Interpreter::strOfrmll,&Interpreter::unkArm, &Interpreter::strOfrmlr,&Interpreter::unkArm,
    &Interpreter::strOfrmar,&Interpreter::unkArm, &Interpreter::strOfrmrr,&Interpreter::unkArm,

    // ── 0x710–0x71F  LDR  offset-dec reg ────────────────────────────────────
    &Interpreter::ldrOfrmll,&Interpreter::unkArm, &Interpreter::ldrOfrmlr,&Interpreter::unkArm,
    &Interpreter::ldrOfrmar,&Interpreter::unkArm, &Interpreter::ldrOfrmrr,&Interpreter::unkArm,
    &Interpreter::ldrOfrmll,&Interpreter::unkArm, &Interpreter::ldrOfrmlr,&Interpreter::unkArm,
    &Interpreter::ldrOfrmar,&Interpreter::unkArm, &Interpreter::ldrOfrmrr,&Interpreter::unkArm,

    // ── 0x720–0x72F  STR  pre-dec reg (W=1) ──────────────────────────────────
    &Interpreter::strPrrmll,&Interpreter::unkArm, &Interpreter::strPrrmlr,&Interpreter::unkArm,
    &Interpreter::strPrrmar,&Interpreter::unkArm, &Interpreter::strPrrmrr,&Interpreter::unkArm,
    &Interpreter::strPrrmll,&Interpreter::unkArm, &Interpreter::strPrrmlr,&Interpreter::unkArm,
    &Interpreter::strPrrmar,&Interpreter::unkArm, &Interpreter::strPrrmrr,&Interpreter::unkArm,

    // ── 0x730–0x73F  LDR  pre-dec reg (W=1) ──────────────────────────────────
    &Interpreter::ldrPrrmll,&Interpreter::unkArm, &Interpreter::ldrPrrmlr,&Interpreter::unkArm,
    &Interpreter::ldrPrrmar,&Interpreter::unkArm, &Interpreter::ldrPrrmrr,&Interpreter::unkArm,
    &Interpreter::ldrPrrmll,&Interpreter::unkArm, &Interpreter::ldrPrrmlr,&Interpreter::unkArm,
    &Interpreter::ldrPrrmar,&Interpreter::unkArm, &Interpreter::ldrPrrmrr,&Interpreter::unkArm,

    // ── 0x740–0x74F  STRB offset-dec reg ────────────────────────────────────
    &Interpreter::strbOfrmll,&Interpreter::unkArm, &Interpreter::strbOfrmlr,&Interpreter::unkArm,
    &Interpreter::strbOfrmar,&Interpreter::unkArm, &Interpreter::strbOfrmrr,&Interpreter::unkArm,
    &Interpreter::strbOfrmll,&Interpreter::unkArm, &Interpreter::strbOfrmlr,&Interpreter::unkArm,
    &Interpreter::strbOfrmar,&Interpreter::unkArm, &Interpreter::strbOfrmrr,&Interpreter::unkArm,

    // ── 0x750–0x75F  LDRB offset-dec reg ────────────────────────────────────
    &Interpreter::ldrbOfrmll,&Interpreter::unkArm, &Interpreter::ldrbOfrmlr,&Interpreter::unkArm,
    &Interpreter::ldrbOfrmar,&Interpreter::unkArm, &Interpreter::ldrbOfrmrr,&Interpreter::unkArm,
    &Interpreter::ldrbOfrmll,&Interpreter::unkArm, &Interpreter::ldrbOfrmlr,&Interpreter::unkArm,
    &Interpreter::ldrbOfrmar,&Interpreter::unkArm, &Interpreter::ldrbOfrmrr,&Interpreter::unkArm,

    // ── 0x760–0x76F  STRB pre-dec reg (W=1) ──────────────────────────────────
    &Interpreter::strbPrrmll,&Interpreter::unkArm, &Interpreter::strbPrrmlr,&Interpreter::unkArm,
    &Interpreter::strbPrrmar,&Interpreter::unkArm, &Interpreter::strbPrrmrr,&Interpreter::unkArm,
    &Interpreter::strbPrrmll,&Interpreter::unkArm, &Interpreter::strbPrrmlr,&Interpreter::unkArm,
    &Interpreter::strbPrrmar,&Interpreter::unkArm, &Interpreter::strbPrrmrr,&Interpreter::unkArm,

    // ── 0x770–0x77F  LDRB pre-dec reg (W=1) ──────────────────────────────────
    &Interpreter::ldrbPrrmll,&Interpreter::unkArm, &Interpreter::ldrbPrrmlr,&Interpreter::unkArm,
    &Interpreter::ldrbPrrmar,&Interpreter::unkArm, &Interpreter::ldrbPrrmrr,&Interpreter::unkArm,
    &Interpreter::ldrbPrrmll,&Interpreter::unkArm, &Interpreter::ldrbPrrmlr,&Interpreter::unkArm,
    &Interpreter::ldrbPrrmar,&Interpreter::unkArm, &Interpreter::ldrbPrrmrr,&Interpreter::unkArm,

    // ── 0x780–0x78F  STR  offset-inc reg ────────────────────────────────────
    &Interpreter::strOfrpll,&Interpreter::unkArm, &Interpreter::strOfrplr,&Interpreter::unkArm,
    &Interpreter::strOfrpar,&Interpreter::unkArm, &Interpreter::strOfrprr,&Interpreter::unkArm,
    &Interpreter::strOfrpll,&Interpreter::unkArm, &Interpreter::strOfrplr,&Interpreter::unkArm,
    &Interpreter::strOfrpar,&Interpreter::unkArm, &Interpreter::strOfrprr,&Interpreter::unkArm,

    // ── 0x790–0x79F  LDR  offset-inc reg ────────────────────────────────────
    &Interpreter::ldrOfrpll,&Interpreter::unkArm, &Interpreter::ldrOfrplr,&Interpreter::unkArm,
    &Interpreter::ldrOfrpar,&Interpreter::unkArm, &Interpreter::ldrOfrprr,&Interpreter::unkArm,
    &Interpreter::ldrOfrpll,&Interpreter::unkArm, &Interpreter::ldrOfrplr,&Interpreter::unkArm,
    &Interpreter::ldrOfrpar,&Interpreter::unkArm, &Interpreter::ldrOfrprr,&Interpreter::unkArm,

    // ── 0x7A0–0x7AF  STR  pre-inc reg (W=1) ──────────────────────────────────
    &Interpreter::strPrrpll,&Interpreter::unkArm, &Interpreter::strPrrplr,&Interpreter::unkArm,
    &Interpreter::strPrrpar,&Interpreter::unkArm, &Interpreter::strPrrprr,&Interpreter::unkArm,
    &Interpreter::strPrrpll,&Interpreter::unkArm, &Interpreter::strPrrplr,&Interpreter::unkArm,
    &Interpreter::strPrrpar,&Interpreter::unkArm, &Interpreter::strPrrprr,&Interpreter::unkArm,

    // ── 0x7B0–0x7BF  LDR  pre-inc reg (W=1) ──────────────────────────────────
    &Interpreter::ldrPrrpll,&Interpreter::unkArm, &Interpreter::ldrPrrplr,&Interpreter::unkArm,
    &Interpreter::ldrPrrpar,&Interpreter::unkArm, &Interpreter::ldrPrrprr,&Interpreter::unkArm,
    &Interpreter::ldrPrrpll,&Interpreter::unkArm, &Interpreter::ldrPrrplr,&Interpreter::unkArm,
    &Interpreter::ldrPrrpar,&Interpreter::unkArm, &Interpreter::ldrPrrprr,&Interpreter::unkArm,

    // ── 0x7C0–0x7CF  STRB offset-inc reg ────────────────────────────────────
    &Interpreter::strbOfrpll,&Interpreter::unkArm, &Interpreter::strbOfrplr,&Interpreter::unkArm,
    &Interpreter::strbOfrpar,&Interpreter::unkArm, &Interpreter::strbOfrprr,&Interpreter::unkArm,
    &Interpreter::strbOfrpll,&Interpreter::unkArm, &Interpreter::strbOfrplr,&Interpreter::unkArm,
    &Interpreter::strbOfrpar,&Interpreter::unkArm, &Interpreter::strbOfrprr,&Interpreter::unkArm,

    // ── 0x7D0–0x7DF  LDRB offset-inc reg ────────────────────────────────────
    &Interpreter::ldrbOfrpll,&Interpreter::unkArm, &Interpreter::ldrbOfrplr,&Interpreter::unkArm,
    &Interpreter::ldrbOfrpar,&Interpreter::unkArm, &Interpreter::ldrbOfrprr,&Interpreter::unkArm,
    &Interpreter::ldrbOfrpll,&Interpreter::unkArm, &Interpreter::ldrbOfrplr,&Interpreter::unkArm,
    &Interpreter::ldrbOfrpar,&Interpreter::unkArm, &Interpreter::ldrbOfrprr,&Interpreter::unkArm,

    // ── 0x7E0–0x7EF  STRB pre-inc reg (W=1) ─────────────────────────────────
    &Interpreter::strbPrrpll,&Interpreter::unkArm, &Interpreter::strbPrrplr,&Interpreter::unkArm,
    &Interpreter::strbPrrpar,&Interpreter::unkArm, &Interpreter::strbPrrprr,&Interpreter::unkArm,
    &Interpreter::strbPrrpll,&Interpreter::unkArm, &Interpreter::strbPrrplr,&Interpreter::unkArm,
    &Interpreter::strbPrrpar,&Interpreter::unkArm, &Interpreter::strbPrrprr,&Interpreter::unkArm,

    // ── 0x7F0–0x7FF  LDRB pre-inc reg (W=1) ─────────────────────────────────
    &Interpreter::ldrbPrrpll,&Interpreter::unkArm, &Interpreter::ldrbPrrplr,&Interpreter::unkArm,
    &Interpreter::ldrbPrrpar,&Interpreter::unkArm, &Interpreter::ldrbPrrprr,&Interpreter::unkArm,
    &Interpreter::ldrbPrrpll,&Interpreter::unkArm, &Interpreter::ldrbPrrplr,&Interpreter::unkArm,
    &Interpreter::ldrbPrrpar,&Interpreter::unkArm, &Interpreter::ldrbPrrprr,&Interpreter::unkArm,

    // ════════════════════════════════════════════════════════════════════════
    // Block data transfer (LDM/STM) — bits 27-25 = 100
    // Suffix key:
    //   da=Decrement After   ia=Increment After
    //   db=Decrement Before  ib=Increment Before
    //   W = writeback  U = user-mode registers  UW = both
    // All 16 entries per row are identical (register list in bits 15-0).
    // ════════════════════════════════════════════════════════════════════════

    // ── 0x800–0x80F  STMDA ───────────────────────────────────────────────────
    &Interpreter::stmda,&Interpreter::stmda,&Interpreter::stmda,&Interpreter::stmda, &Interpreter::stmda,&Interpreter::stmda,&Interpreter::stmda,&Interpreter::stmda,
    &Interpreter::stmda,&Interpreter::stmda,&Interpreter::stmda,&Interpreter::stmda, &Interpreter::stmda,&Interpreter::stmda,&Interpreter::stmda,&Interpreter::stmda,

    // ── 0x810–0x81F  LDMDA ───────────────────────────────────────────────────
    &Interpreter::ldmda,&Interpreter::ldmda,&Interpreter::ldmda,&Interpreter::ldmda, &Interpreter::ldmda,&Interpreter::ldmda,&Interpreter::ldmda,&Interpreter::ldmda,
    &Interpreter::ldmda,&Interpreter::ldmda,&Interpreter::ldmda,&Interpreter::ldmda, &Interpreter::ldmda,&Interpreter::ldmda,&Interpreter::ldmda,&Interpreter::ldmda,

    // ── 0x820–0x82F  STMDA (W=1) ─────────────────────────────────────────────
    &Interpreter::stmdaW,&Interpreter::stmdaW,&Interpreter::stmdaW,&Interpreter::stmdaW, &Interpreter::stmdaW,&Interpreter::stmdaW,&Interpreter::stmdaW,&Interpreter::stmdaW,
    &Interpreter::stmdaW,&Interpreter::stmdaW,&Interpreter::stmdaW,&Interpreter::stmdaW, &Interpreter::stmdaW,&Interpreter::stmdaW,&Interpreter::stmdaW,&Interpreter::stmdaW,

    // ── 0x830–0x83F  LDMDA (W=1) ─────────────────────────────────────────────
    &Interpreter::ldmdaW,&Interpreter::ldmdaW,&Interpreter::ldmdaW,&Interpreter::ldmdaW, &Interpreter::ldmdaW,&Interpreter::ldmdaW,&Interpreter::ldmdaW,&Interpreter::ldmdaW,
    &Interpreter::ldmdaW,&Interpreter::ldmdaW,&Interpreter::ldmdaW,&Interpreter::ldmdaW, &Interpreter::ldmdaW,&Interpreter::ldmdaW,&Interpreter::ldmdaW,&Interpreter::ldmdaW,

    // ── 0x840–0x84F  STMDA (U) ───────────────────────────────────────────────
    &Interpreter::stmdaU,&Interpreter::stmdaU,&Interpreter::stmdaU,&Interpreter::stmdaU, &Interpreter::stmdaU,&Interpreter::stmdaU,&Interpreter::stmdaU,&Interpreter::stmdaU,
    &Interpreter::stmdaU,&Interpreter::stmdaU,&Interpreter::stmdaU,&Interpreter::stmdaU, &Interpreter::stmdaU,&Interpreter::stmdaU,&Interpreter::stmdaU,&Interpreter::stmdaU,

    // ── 0x850–0x85F  LDMDA (U) ───────────────────────────────────────────────
    &Interpreter::ldmdaU,&Interpreter::ldmdaU,&Interpreter::ldmdaU,&Interpreter::ldmdaU, &Interpreter::ldmdaU,&Interpreter::ldmdaU,&Interpreter::ldmdaU,&Interpreter::ldmdaU,
    &Interpreter::ldmdaU,&Interpreter::ldmdaU,&Interpreter::ldmdaU,&Interpreter::ldmdaU, &Interpreter::ldmdaU,&Interpreter::ldmdaU,&Interpreter::ldmdaU,&Interpreter::ldmdaU,

    // ── 0x860–0x86F  STMDA (U,W) ─────────────────────────────────────────────
    &Interpreter::stmdaUW,&Interpreter::stmdaUW,&Interpreter::stmdaUW,&Interpreter::stmdaUW, &Interpreter::stmdaUW,&Interpreter::stmdaUW,&Interpreter::stmdaUW,&Interpreter::stmdaUW,
    &Interpreter::stmdaUW,&Interpreter::stmdaUW,&Interpreter::stmdaUW,&Interpreter::stmdaUW, &Interpreter::stmdaUW,&Interpreter::stmdaUW,&Interpreter::stmdaUW,&Interpreter::stmdaUW,

    // ── 0x870–0x87F  LDMDA (U,W) ─────────────────────────────────────────────
    &Interpreter::ldmdaUW,&Interpreter::ldmdaUW,&Interpreter::ldmdaUW,&Interpreter::ldmdaUW, &Interpreter::ldmdaUW,&Interpreter::ldmdaUW,&Interpreter::ldmdaUW,&Interpreter::ldmdaUW,
    &Interpreter::ldmdaUW,&Interpreter::ldmdaUW,&Interpreter::ldmdaUW,&Interpreter::ldmdaUW, &Interpreter::ldmdaUW,&Interpreter::ldmdaUW,&Interpreter::ldmdaUW,&Interpreter::ldmdaUW,

    // ── 0x880–0x88F  STMIA ───────────────────────────────────────────────────
    &Interpreter::stmia,&Interpreter::stmia,&Interpreter::stmia,&Interpreter::stmia, &Interpreter::stmia,&Interpreter::stmia,&Interpreter::stmia,&Interpreter::stmia,
    &Interpreter::stmia,&Interpreter::stmia,&Interpreter::stmia,&Interpreter::stmia, &Interpreter::stmia,&Interpreter::stmia,&Interpreter::stmia,&Interpreter::stmia,

    // ── 0x890–0x89F  LDMIA ───────────────────────────────────────────────────
    &Interpreter::ldmia,&Interpreter::ldmia,&Interpreter::ldmia,&Interpreter::ldmia, &Interpreter::ldmia,&Interpreter::ldmia,&Interpreter::ldmia,&Interpreter::ldmia,
    &Interpreter::ldmia,&Interpreter::ldmia,&Interpreter::ldmia,&Interpreter::ldmia, &Interpreter::ldmia,&Interpreter::ldmia,&Interpreter::ldmia,&Interpreter::ldmia,

    // ── 0x8A0–0x8AF  STMIA (W=1) ─────────────────────────────────────────────
    &Interpreter::stmiaW,&Interpreter::stmiaW,&Interpreter::stmiaW,&Interpreter::stmiaW, &Interpreter::stmiaW,&Interpreter::stmiaW,&Interpreter::stmiaW,&Interpreter::stmiaW,
    &Interpreter::stmiaW,&Interpreter::stmiaW,&Interpreter::stmiaW,&Interpreter::stmiaW, &Interpreter::stmiaW,&Interpreter::stmiaW,&Interpreter::stmiaW,&Interpreter::stmiaW,

    // ── 0x8B0–0x8BF  LDMIA (W=1) ─────────────────────────────────────────────
    &Interpreter::ldmiaW,&Interpreter::ldmiaW,&Interpreter::ldmiaW,&Interpreter::ldmiaW, &Interpreter::ldmiaW,&Interpreter::ldmiaW,&Interpreter::ldmiaW,&Interpreter::ldmiaW,
    &Interpreter::ldmiaW,&Interpreter::ldmiaW,&Interpreter::ldmiaW,&Interpreter::ldmiaW, &Interpreter::ldmiaW,&Interpreter::ldmiaW,&Interpreter::ldmiaW,&Interpreter::ldmiaW,

    // ── 0x8C0–0x8CF  STMIA (U) ───────────────────────────────────────────────
    &Interpreter::stmiaU,&Interpreter::stmiaU,&Interpreter::stmiaU,&Interpreter::stmiaU, &Interpreter::stmiaU,&Interpreter::stmiaU,&Interpreter::stmiaU,&Interpreter::stmiaU,
    &Interpreter::stmiaU,&Interpreter::stmiaU,&Interpreter::stmiaU,&Interpreter::stmiaU, &Interpreter::stmiaU,&Interpreter::stmiaU,&Interpreter::stmiaU,&Interpreter::stmiaU,

    // ── 0x8D0–0x8DF  LDMIA (U) ───────────────────────────────────────────────
    &Interpreter::ldmiaU,&Interpreter::ldmiaU,&Interpreter::ldmiaU,&Interpreter::ldmiaU, &Interpreter::ldmiaU,&Interpreter::ldmiaU,&Interpreter::ldmiaU,&Interpreter::ldmiaU,
    &Interpreter::ldmiaU,&Interpreter::ldmiaU,&Interpreter::ldmiaU,&Interpreter::ldmiaU, &Interpreter::ldmiaU,&Interpreter::ldmiaU,&Interpreter::ldmiaU,&Interpreter::ldmiaU,

    // ── 0x8E0–0x8EF  STMIA (U,W) ─────────────────────────────────────────────
    &Interpreter::stmiaUW,&Interpreter::stmiaUW,&Interpreter::stmiaUW,&Interpreter::stmiaUW, &Interpreter::stmiaUW,&Interpreter::stmiaUW,&Interpreter::stmiaUW,&Interpreter::stmiaUW,
    &Interpreter::stmiaUW,&Interpreter::stmiaUW,&Interpreter::stmiaUW,&Interpreter::stmiaUW, &Interpreter::stmiaUW,&Interpreter::stmiaUW,&Interpreter::stmiaUW,&Interpreter::stmiaUW,

    // ── 0x8F0–0x8FF  LDMIA (U,W) ─────────────────────────────────────────────
    &Interpreter::ldmiaUW,&Interpreter::ldmiaUW,&Interpreter::ldmiaUW,&Interpreter::ldmiaUW, &Interpreter::ldmiaUW,&Interpreter::ldmiaUW,&Interpreter::ldmiaUW,&Interpreter::ldmiaUW,
    &Interpreter::ldmiaUW,&Interpreter::ldmiaUW,&Interpreter::ldmiaUW,&Interpreter::ldmiaUW, &Interpreter::ldmiaUW,&Interpreter::ldmiaUW,&Interpreter::ldmiaUW,&Interpreter::ldmiaUW,

    // ── 0x900–0x90F  STMDB ───────────────────────────────────────────────────
    &Interpreter::stmdb,&Interpreter::stmdb,&Interpreter::stmdb,&Interpreter::stmdb, &Interpreter::stmdb,&Interpreter::stmdb,&Interpreter::stmdb,&Interpreter::stmdb,
    &Interpreter::stmdb,&Interpreter::stmdb,&Interpreter::stmdb,&Interpreter::stmdb, &Interpreter::stmdb,&Interpreter::stmdb,&Interpreter::stmdb,&Interpreter::stmdb,

    // ── 0x910–0x91F  LDMDB ───────────────────────────────────────────────────
    &Interpreter::ldmdb,&Interpreter::ldmdb,&Interpreter::ldmdb,&Interpreter::ldmdb, &Interpreter::ldmdb,&Interpreter::ldmdb,&Interpreter::ldmdb,&Interpreter::ldmdb,
    &Interpreter::ldmdb,&Interpreter::ldmdb,&Interpreter::ldmdb,&Interpreter::ldmdb, &Interpreter::ldmdb,&Interpreter::ldmdb,&Interpreter::ldmdb,&Interpreter::ldmdb,

    // ── 0x920–0x92F  STMDB (W=1) — push ─────────────────────────────────────
    &Interpreter::stmdbW,&Interpreter::stmdbW,&Interpreter::stmdbW,&Interpreter::stmdbW, &Interpreter::stmdbW,&Interpreter::stmdbW,&Interpreter::stmdbW,&Interpreter::stmdbW,
    &Interpreter::stmdbW,&Interpreter::stmdbW,&Interpreter::stmdbW,&Interpreter::stmdbW, &Interpreter::stmdbW,&Interpreter::stmdbW,&Interpreter::stmdbW,&Interpreter::stmdbW,

    // ── 0x930–0x93F  LDMDB (W=1) ─────────────────────────────────────────────
    &Interpreter::ldmdbW,&Interpreter::ldmdbW,&Interpreter::ldmdbW,&Interpreter::ldmdbW, &Interpreter::ldmdbW,&Interpreter::ldmdbW,&Interpreter::ldmdbW,&Interpreter::ldmdbW,
    &Interpreter::ldmdbW,&Interpreter::ldmdbW,&Interpreter::ldmdbW,&Interpreter::ldmdbW, &Interpreter::ldmdbW,&Interpreter::ldmdbW,&Interpreter::ldmdbW,&Interpreter::ldmdbW,

    // ── 0x940–0x94F  STMDB (U) ───────────────────────────────────────────────
    &Interpreter::stmdbU,&Interpreter::stmdbU,&Interpreter::stmdbU,&Interpreter::stmdbU, &Interpreter::stmdbU,&Interpreter::stmdbU,&Interpreter::stmdbU,&Interpreter::stmdbU,
    &Interpreter::stmdbU,&Interpreter::stmdbU,&Interpreter::stmdbU,&Interpreter::stmdbU, &Interpreter::stmdbU,&Interpreter::stmdbU,&Interpreter::stmdbU,&Interpreter::stmdbU,

    // ── 0x950–0x95F  LDMDB (U) ───────────────────────────────────────────────
    &Interpreter::ldmdbU,&Interpreter::ldmdbU,&Interpreter::ldmdbU,&Interpreter::ldmdbU, &Interpreter::ldmdbU,&Interpreter::ldmdbU,&Interpreter::ldmdbU,&Interpreter::ldmdbU,
    &Interpreter::ldmdbU,&Interpreter::ldmdbU,&Interpreter::ldmdbU,&Interpreter::ldmdbU, &Interpreter::ldmdbU,&Interpreter::ldmdbU,&Interpreter::ldmdbU,&Interpreter::ldmdbU,

    // ── 0x960–0x96F  STMDB (U,W) ─────────────────────────────────────────────
    &Interpreter::stmdbUW,&Interpreter::stmdbUW,&Interpreter::stmdbUW,&Interpreter::stmdbUW, &Interpreter::stmdbUW,&Interpreter::stmdbUW,&Interpreter::stmdbUW,&Interpreter::stmdbUW,
    &Interpreter::stmdbUW,&Interpreter::stmdbUW,&Interpreter::stmdbUW,&Interpreter::stmdbUW, &Interpreter::stmdbUW,&Interpreter::stmdbUW,&Interpreter::stmdbUW,&Interpreter::stmdbUW,

    // ── 0x970–0x97F  LDMDB (U,W) ─────────────────────────────────────────────
    &Interpreter::ldmdbUW,&Interpreter::ldmdbUW,&Interpreter::ldmdbUW,&Interpreter::ldmdbUW, &Interpreter::ldmdbUW,&Interpreter::ldmdbUW,&Interpreter::ldmdbUW,&Interpreter::ldmdbUW,
    &Interpreter::ldmdbUW,&Interpreter::ldmdbUW,&Interpreter::ldmdbUW,&Interpreter::ldmdbUW, &Interpreter::ldmdbUW,&Interpreter::ldmdbUW,&Interpreter::ldmdbUW,&Interpreter::ldmdbUW,

    // ── 0x980–0x98F  STMIB ───────────────────────────────────────────────────
    &Interpreter::stmib,&Interpreter::stmib,&Interpreter::stmib,&Interpreter::stmib, &Interpreter::stmib,&Interpreter::stmib,&Interpreter::stmib,&Interpreter::stmib,
    &Interpreter::stmib,&Interpreter::stmib,&Interpreter::stmib,&Interpreter::stmib, &Interpreter::stmib,&Interpreter::stmib,&Interpreter::stmib,&Interpreter::stmib,

    // ── 0x990–0x99F  LDMIB ───────────────────────────────────────────────────
    &Interpreter::ldmib,&Interpreter::ldmib,&Interpreter::ldmib,&Interpreter::ldmib, &Interpreter::ldmib,&Interpreter::ldmib,&Interpreter::ldmib,&Interpreter::ldmib,
    &Interpreter::ldmib,&Interpreter::ldmib,&Interpreter::ldmib,&Interpreter::ldmib, &Interpreter::ldmib,&Interpreter::ldmib,&Interpreter::ldmib,&Interpreter::ldmib,

    // ── 0x9A0–0x9AF  STMIB (W=1) ─────────────────────────────────────────────
    &Interpreter::stmibW,&Interpreter::stmibW,&Interpreter::stmibW,&Interpreter::stmibW, &Interpreter::stmibW,&Interpreter::stmibW,&Interpreter::stmibW,&Interpreter::stmibW,
    &Interpreter::stmibW,&Interpreter::stmibW,&Interpreter::stmibW,&Interpreter::stmibW, &Interpreter::stmibW,&Interpreter::stmibW,&Interpreter::stmibW,&Interpreter::stmibW,

    // ── 0x9B0–0x9BF  LDMIB (W=1) — pop ──────────────────────────────────────
    &Interpreter::ldmibW,&Interpreter::ldmibW,&Interpreter::ldmibW,&Interpreter::ldmibW, &Interpreter::ldmibW,&Interpreter::ldmibW,&Interpreter::ldmibW,&Interpreter::ldmibW,
    &Interpreter::ldmibW,&Interpreter::ldmibW,&Interpreter::ldmibW,&Interpreter::ldmibW, &Interpreter::ldmibW,&Interpreter::ldmibW,&Interpreter::ldmibW,&Interpreter::ldmibW,

    // ── 0x9C0–0x9CF  STMIB (U) ───────────────────────────────────────────────
    &Interpreter::stmibU,&Interpreter::stmibU,&Interpreter::stmibU,&Interpreter::stmibU, &Interpreter::stmibU,&Interpreter::stmibU,&Interpreter::stmibU,&Interpreter::stmibU,
    &Interpreter::stmibU,&Interpreter::stmibU,&Interpreter::stmibU,&Interpreter::stmibU, &Interpreter::stmibU,&Interpreter::stmibU,&Interpreter::stmibU,&Interpreter::stmibU,

    // ── 0x9D0–0x9DF  LDMIB (U) ───────────────────────────────────────────────
    &Interpreter::ldmibU,&Interpreter::ldmiaU,&Interpreter::ldmibU,&Interpreter::ldmibU, &Interpreter::ldmibU,&Interpreter::ldmibU,&Interpreter::ldmibU,&Interpreter::ldmibU,
    &Interpreter::ldmibU,&Interpreter::ldmibU,&Interpreter::ldmibU,&Interpreter::ldmibU, &Interpreter::ldmibU,&Interpreter::ldmibU,&Interpreter::ldmibU,&Interpreter::ldmibU,

    // ── 0x9E0–0x9EF  STMIB (U,W) ─────────────────────────────────────────────
    &Interpreter::stmibUW,&Interpreter::stmibUW,&Interpreter::stmibUW,&Interpreter::stmibUW, &Interpreter::stmibUW,&Interpreter::stmibUW,&Interpreter::stmibUW,&Interpreter::stmibUW,
    &Interpreter::stmibUW,&Interpreter::stmibUW,&Interpreter::stmibUW,&Interpreter::stmibUW, &Interpreter::stmibUW,&Interpreter::stmibUW,&Interpreter::stmibUW,&Interpreter::stmibUW,

    // ── 0x9F0–0x9FF  LDMIB (U,W) ─────────────────────────────────────────────
    &Interpreter::ldmibUW,&Interpreter::ldmibUW,&Interpreter::ldmibUW,&Interpreter::ldmibUW, &Interpreter::ldmibUW,&Interpreter::ldmibUW,&Interpreter::ldmibUW,&Interpreter::ldmibUW,
    &Interpreter::ldmibUW,&Interpreter::ldmibUW,&Interpreter::ldmibUW,&Interpreter::ldmibUW, &Interpreter::ldmibUW,&Interpreter::ldmibUW,&Interpreter::ldmibUW,&Interpreter::ldmibUW,

    // ════════════════════════════════════════════════════════════════════════
    // Branch instructions (bits 27-24 = 1010 / 1011)
    // The 24-bit signed offset is encoded in bits 23-0; every entry in the
    // 0xA00–0xBFF range dispatches to the same handler which extracts it.
    // 256 entries × 2 (B + BL) = 512 total — all identical within each half.
    // ════════════════════════════════════════════════════════════════════════

    // ── 0xA00–0xAFF  B (branch without link) — 256 entries ──────────────────
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xA00–0xA0F
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xA10–0xA1F
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xA20–0xA2F
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xA30–0xA3F
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xA40–0xA4F
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xA50–0xA5F
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xA60–0xA6F
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xA70–0xA7F
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xA80–0xA8F
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xA90–0xA9F
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xAA0–0xAAF
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xAB0–0xABF
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xAC0–0xACF
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xAD0–0xADF
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xAE0–0xAEF
    &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b, &Interpreter::b,&Interpreter::b,&Interpreter::b,&Interpreter::b,   // 0xAF0–0xAFF

    // ── 0xB00–0xBFF  BL (branch with link) — 256 entries ────────────────────
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xB00–0xB0F
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xB10–0xB1F
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xB20–0xB2F
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xB30–0xB3F
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xB40–0xB4F
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xB50–0xB5F
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xB60–0xB6F
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xB70–0xB7F
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xB80–0xB8F
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xB90–0xB9F
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xBA0–0xBAF
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xBB0–0xBBF
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xBC0–0xBCF
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xBD0–0xBDF
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xBE0–0xBEF
    &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl, &Interpreter::bl,&Interpreter::bl,&Interpreter::bl,&Interpreter::bl,   // 0xBF0–0xBFF

    // ════════════════════════════════════════════════════════════════════════
    // Coprocessor load/store (bits 27-24 = 1100/1101/1110/1111)
    // On the NDS/GBA the coprocessor space (0xC00–0xDFF) is entirely
    // undefined — no LDC/STC instructions are used.
    // ════════════════════════════════════════════════════════════════════════

    // ── 0xC00–0xCFF  undefined (coprocessor load/store) ─────────────────────
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xC00–0xC0F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xC10–0xC1F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xC20–0xC2F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xC30–0xC3F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xC40–0xC4F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xC50–0xC5F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xC60–0xC6F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xC70–0xC7F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xC80–0xC8F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xC90–0xC9F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xCA0–0xCAF
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xCB0–0xCBF
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xCC0–0xCCF
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xCD0–0xCDF
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xCE0–0xCEF
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xCF0–0xCFF
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,

    // ── 0xD00–0xDFF  undefined (coprocessor load/store continued) ────────────
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xD00–0xD0F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xD10–0xD1F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xD20–0xD2F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xD30–0xD3F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xD40–0xD4F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xD50–0xD5F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xD60–0xD6F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xD70–0xD7F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xD80–0xD8F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xD90–0xD9F
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xDA0–0xDAF
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xDB0–0xDBF
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xDC0–0xDCF
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xDD0–0xDDF
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xDE0–0xDEF
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,   // 0xDF0–0xDFF
    &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm, &Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,&Interpreter::unkArm,

    // ════════════════════════════════════════════════════════════════════════
    // Coprocessor data operations / register transfers (bits 27-24 = 1110)
    // On the NDS the only live coprocessors are CP14 (debug, ARM9 only) and
    // CP15 (system control, ARM9 only).  Every entry alternates:
    //   even index → CDP / unkArm   (bit 4 = 0: data op, unused on NDS)
    //   odd  index → MCR or MRC     (bit 4 = 1: register transfer)
    // Bits 23-21 select the coprocessor opcode; bits 19-16 select CRn;
    // bits 15-12 select Rd; bits 11-8 select the coprocessor number;
    // bits 7-5 select a secondary opcode; bit 4 distinguishes MCR/MRC.
    // The L bit (bit 20) selects MCR (L=0) vs MRC (L=1).
    // ════════════════════════════════════════════════════════════════════════

    // ── 0xE00–0xE0F  CDP/MCR (L=0, opc1 groups 0–1) ──────────────────────────
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xE00–0xE07
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xE08–0xE0F

    // ── 0xE10–0xE1F  CDP/MRC (L=1, opc1 groups 0–1) ──────────────────────────
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xE10–0xE17
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xE18–0xE1F

    // ── 0xE20–0xE2F  CDP/MCR (opc1 groups 2–3) ───────────────────────────────
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xE20–0xE27
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xE28–0xE2F

    // ── 0xE30–0xE3F  CDP/MRC (opc1 groups 2–3) ───────────────────────────────
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xE30–0xE37
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xE38–0xE3F

    // ── 0xE40–0xE4F  CDP/MCR (opc1 groups 4–5) ───────────────────────────────
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xE40–0xE47
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xE48–0xE4F

    // ── 0xE50–0xE5F  CDP/MRC (opc1 groups 4–5) ───────────────────────────────
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xE50–0xE57
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xE58–0xE5F

    // ── 0xE60–0xE6F  CDP/MCR (opc1 groups 6–7) ───────────────────────────────
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xE60–0xE67
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xE68–0xE6F

    // ── 0xE70–0xE7F  CDP/MRC (opc1 groups 6–7) ───────────────────────────────
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xE70–0xE77
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xE78–0xE7F

    // ── 0xE80–0xE8F  CDP/MCR (opc1 groups 8–9) ───────────────────────────────
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xE80–0xE87
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xE88–0xE8F

    // ── 0xE90–0xE9F  CDP/MRC (opc1 groups 8–9) ───────────────────────────────
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xE90–0xE97
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xE98–0xE9F

    // ── 0xEA0–0xEAF  CDP/MCR (opc1 groups 10–11) ─────────────────────────────
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xEA0–0xEA7
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xEA8–0xEAF

    // ── 0xEB0–0xEBF  CDP/MRC (opc1 groups 10–11) ─────────────────────────────
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xEB0–0xEB7
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xEB8–0xEBF

    // ── 0xEC0–0xECF  CDP/MCR (opc1 groups 12–13) ─────────────────────────────
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xEC0–0xEC7
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xEC8–0xECF

    // ── 0xED0–0xEDF  CDP/MRC (opc1 groups 12–13) ─────────────────────────────
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xED0–0xED7
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xED8–0xEDF

    // ── 0xEE0–0xEEF  CDP/MCR (opc1 groups 14–15) ─────────────────────────────
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xEE0–0xEE7
    &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,  &Interpreter::unkArm,&Interpreter::mcr, &Interpreter::unkArm,&Interpreter::mcr,   // 0xEE8–0xEEF

    // ── 0xEF0–0xEFF  CDP/MRC (opc1 groups 14–15) ─────────────────────────────
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xEF0–0xEF7
    &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,  &Interpreter::unkArm,&Interpreter::mrc, &Interpreter::unkArm,&Interpreter::mrc,   // 0xEF8–0xEFF

    // ════════════════════════════════════════════════════════════════════════
    // Software Interrupt (bits 27-24 = 1111)
    // The 24-bit comment field in the opcode is ignored by hardware;
    // all 256 index positions dispatch to the same SWI handler.
    // ════════════════════════════════════════════════════════════════════════

    // ── 0xF00–0xFFF  SWI — 256 entries ──────────────────────────────────────
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xF00–0xF0F
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xF10–0xF1F
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xF20–0xF2F
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xF30–0xF3F
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xF40–0xF4F
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xF50–0xF5F
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xF60–0xF6F
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xF70–0xF7F
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xF80–0xF8F
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xF90–0xF9F
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xFA0–0xFAF
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xFB0–0xFBF
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xFC0–0xFCF
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xFD0–0xFDF
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xFE0–0xFEF
    &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi, &Interpreter::swi,&Interpreter::swi,&Interpreter::swi,&Interpreter::swi,  // 0xFF0–0xFFF
};

    // ════════════════════════════════════════════════════════════════════════
    // THUMB instruction dispatch table
    // Indexed by bits 15-6 of the 16-bit THUMB opcode (1024 entries).
    // Reference: http://imrannazar.com/ARM-Opcode-Map
    //
    // Layout notes (PowerPC cache optimisation):
    //   • uint16_t handler pointers are 4 bytes on 32-bit PPC.
    //   • 32-byte cache line holds 8 pointers.
    //   • 8-wide rows below align one visual row = one cache line.
    //   • const + aligned(32) keeps the table in .rodata, cache-friendly.
    // ════════════════════════════════════════════════════════════════════════

__attribute__((section(".rodata"), aligned(32)))
int (Interpreter::* Interpreter::thumbInstrs[])(uint16_t) =
{
    // ── 0x000–0x01F  LSL imm5 (32 entries) ───────────────────────────────────
    // bits 15-11 = 00000; 5-bit shift amount in bits 10-6
    &Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT, &Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT,  // 0x000–0x007
    &Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT, &Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT,  // 0x008–0x00F
    &Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT, &Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT,  // 0x010–0x017
    &Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT, &Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT,&Interpreter::lslImmT,  // 0x018–0x01F

    // ── 0x020–0x03F  LSR imm5 ────────────────────────────────────────────────
    &Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT, &Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT,  // 0x020–0x027
    &Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT, &Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT,  // 0x028–0x02F
    &Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT, &Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT,  // 0x030–0x037
    &Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT, &Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT,&Interpreter::lsrImmT,  // 0x038–0x03F

    // ── 0x040–0x05F  ASR imm5 ────────────────────────────────────────────────
    &Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT, &Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT,  // 0x040–0x047
    &Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT, &Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT,  // 0x048–0x04F
    &Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT, &Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT,  // 0x050–0x057
    &Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT, &Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT,&Interpreter::asrImmT,  // 0x058–0x05F

    // ── 0x060–0x07F  ADD/SUB reg and imm3 ────────────────────────────────────
    // 0x060–0x067: ADD reg  (bits 15-9 = 0001100)
    // 0x068–0x06F: SUB reg  (bits 15-9 = 0001101)
    // 0x070–0x077: ADD imm3 (bits 15-9 = 0001110)
    // 0x078–0x07F: SUB imm3 (bits 15-9 = 0001111)
    &Interpreter::addRegT, &Interpreter::addRegT, &Interpreter::addRegT, &Interpreter::addRegT,  &Interpreter::addRegT, &Interpreter::addRegT, &Interpreter::addRegT, &Interpreter::addRegT,  // 0x060–0x067
    &Interpreter::subRegT, &Interpreter::subRegT, &Interpreter::subRegT, &Interpreter::subRegT,  &Interpreter::subRegT, &Interpreter::subRegT, &Interpreter::subRegT, &Interpreter::subRegT,  // 0x068–0x06F
    &Interpreter::addImm3T,&Interpreter::addImm3T,&Interpreter::addImm3T,&Interpreter::addImm3T, &Interpreter::addImm3T,&Interpreter::addImm3T,&Interpreter::addImm3T,&Interpreter::addImm3T, // 0x070–0x077
    &Interpreter::subImm3T,&Interpreter::subImm3T,&Interpreter::subImm3T,&Interpreter::subImm3T, &Interpreter::subImm3T,&Interpreter::subImm3T,&Interpreter::subImm3T,&Interpreter::subImm3T, // 0x078–0x07F

    // ── 0x080–0x09F  MOV imm8 (Rd = bits 10-8, imm = bits 7-0) ──────────────
    &Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T, &Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T, // 0x080–0x087
    &Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T, &Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T, // 0x088–0x08F
    &Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T, &Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T, // 0x090–0x097
    &Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T, &Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T,&Interpreter::movImm8T, // 0x098–0x09F

    // ── 0x0A0–0x0BF  CMP imm8 ────────────────────────────────────────────────
    &Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T, &Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T, // 0x0A0–0x0A7
    &Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T, &Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T, // 0x0A8–0x0AF
    &Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T, &Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T, // 0x0B0–0x0B7
    &Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T, &Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T,&Interpreter::cmpImm8T, // 0x0B8–0x0BF

    // ── 0x0C0–0x0DF  ADD imm8 ────────────────────────────────────────────────
    &Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T, &Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T, // 0x0C0–0x0C7
    &Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T, &Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T, // 0x0C8–0x0CF
    &Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T, &Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T, // 0x0D0–0x0D7
    &Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T, &Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T,&Interpreter::addImm8T, // 0x0D8–0x0DF

    // ── 0x0E0–0x0FF  SUB imm8 ────────────────────────────────────────────────
    &Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T, &Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T, // 0x0E0–0x0E7
    &Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T, &Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T, // 0x0E8–0x0EF
    &Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T, &Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T, // 0x0F0–0x0F7
    &Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T, &Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T,&Interpreter::subImm8T, // 0x0F8–0x0FF

    // ── 0x100–0x10F  ALU ops / high-reg / BX / BLX ───────────────────────────
    // 0x100–0x10F: Data processing (bits 15-10 = 010000, bits 9-6 = opcode)
    // Each of the 16 entries is a distinct operation — no replication here.
    &Interpreter::andDpT,  &Interpreter::eorDpT,  &Interpreter::lslDpT,  &Interpreter::lsrDpT,   // 0x100–0x103  AND EOR LSL LSR
    &Interpreter::asrDpT,  &Interpreter::adcDpT,  &Interpreter::sbcDpT,  &Interpreter::rorDpT,   // 0x104–0x107  ASR ADC SBC ROR
    &Interpreter::tstDpT,  &Interpreter::negDpT,  &Interpreter::cmpDpT,  &Interpreter::cmnDpT,   // 0x108–0x10B  TST NEG CMP CMN
    &Interpreter::orrDpT,  &Interpreter::mulDpT,  &Interpreter::bicDpT,  &Interpreter::mvnDpT,   // 0x10C–0x10F  ORR MUL BIC MVN

    // 0x110–0x11F: Special data / branch-exchange (bits 15-10 = 010001)
    //   010001 00 → ADD Hx       (0x110–0x113)
    //   010001 01 → CMP Hx       (0x114–0x117)
    //   010001 10 → MOV Hx       (0x118–0x11B)
    //   010001 110x → BX         (0x11C–0x11D)
    //   010001 111x → BLX reg    (0x11E–0x11F)
    &Interpreter::addHT,   &Interpreter::addHT,   &Interpreter::addHT,   &Interpreter::addHT,    // 0x110–0x113
    &Interpreter::cmpHT,   &Interpreter::cmpHT,   &Interpreter::cmpHT,   &Interpreter::cmpHT,    // 0x114–0x117
    &Interpreter::movHT,   &Interpreter::movHT,   &Interpreter::movHT,   &Interpreter::movHT,    // 0x118–0x11B
    &Interpreter::bxRegT,  &Interpreter::bxRegT,  &Interpreter::blxRegT, &Interpreter::blxRegT,  // 0x11C–0x11F

    // ── 0x120–0x13F  LDR PC-relative (32 entries) ────────────────────────────
    // bits 15-11 = 01001; Rd in bits 10-8; 8-bit word-aligned offset in 7-0
    &Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT, &Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT,  // 0x120–0x127
    &Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT, &Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT,  // 0x128–0x12F
    &Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT, &Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT,  // 0x130–0x137
    &Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT, &Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT,&Interpreter::ldrPcT,  // 0x138–0x13F

    // ── 0x140–0x17F  Load/store register offset (bits 15-12 = 0101) ──────────
    // Sub-opcode in bits 11-9: 000=STR 001=STRH 010=STRB 011=LDRSB
    //                          100=LDR 101=LDRH 110=LDRB 111=LDRSH
    // Each sub-opcode covers 8 entries (bits 8-6 = Ro register field).
    &Interpreter::strRegT, &Interpreter::strRegT, &Interpreter::strRegT, &Interpreter::strRegT,  &Interpreter::strRegT, &Interpreter::strRegT, &Interpreter::strRegT, &Interpreter::strRegT,  // 0x140–0x147 STR
    &Interpreter::strhRegT,&Interpreter::strhRegT,&Interpreter::strhRegT,&Interpreter::strhRegT, &Interpreter::strhRegT,&Interpreter::strhRegT,&Interpreter::strhRegT,&Interpreter::strhRegT, // 0x148–0x14F STRH
    &Interpreter::strbRegT,&Interpreter::strbRegT,&Interpreter::strbRegT,&Interpreter::strbRegT, &Interpreter::strbRegT,&Interpreter::strbRegT,&Interpreter::strbRegT,&Interpreter::strbRegT, // 0x150–0x157 STRB
    &Interpreter::ldrsbRegT,&Interpreter::ldrsbRegT,&Interpreter::ldrsbRegT,&Interpreter::ldrsbRegT, &Interpreter::ldrsbRegT,&Interpreter::ldrsbRegT,&Interpreter::ldrsbRegT,&Interpreter::ldrsbRegT, // 0x158–0x15F LDRSB
    &Interpreter::ldrRegT, &Interpreter::ldrRegT, &Interpreter::ldrRegT, &Interpreter::ldrRegT,  &Interpreter::ldrRegT, &Interpreter::ldrRegT, &Interpreter::ldrRegT, &Interpreter::ldrRegT,  // 0x160–0x167 LDR
    &Interpreter::ldrhRegT,&Interpreter::ldrhRegT,&Interpreter::ldrhRegT,&Interpreter::ldrhRegT, &Interpreter::ldrhRegT,&Interpreter::ldrhRegT,&Interpreter::ldrhRegT,&Interpreter::ldrhRegT, // 0x168–0x16F LDRH
    &Interpreter::ldrbRegT,&Interpreter::ldrbRegT,&Interpreter::ldrbRegT,&Interpreter::ldrbRegT, &Interpreter::ldrbRegT,&Interpreter::ldrbRegT,&Interpreter::ldrbRegT,&Interpreter::ldrbRegT, // 0x170–0x177 LDRB
    &Interpreter::ldrshRegT,&Interpreter::ldrshRegT,&Interpreter::ldrshRegT,&Interpreter::ldrshRegT, &Interpreter::ldrshRegT,&Interpreter::ldrshRegT,&Interpreter::ldrshRegT,&Interpreter::ldrshRegT, // 0x178–0x17F LDRSH

    // ── 0x180–0x19F  STR  imm5 (word) ───────────────────────────────────────
    &Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T, &Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T, // 0x180–0x187
    &Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T, &Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T, // 0x188–0x18F
    &Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T, &Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T, // 0x190–0x197
    &Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T, &Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T,&Interpreter::strImm5T, // 0x198–0x19F

    // ── 0x1A0–0x1BF  LDR  imm5 (word) ───────────────────────────────────────
    &Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T, &Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T, // 0x1A0–0x1A7
    &Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T, &Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T, // 0x1A8–0x1AF
    &Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T, &Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T, // 0x1B0–0x1B7
    &Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T, &Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T,&Interpreter::ldrImm5T, // 0x1B8–0x1BF

    // ── 0x1C0–0x1DF  STRB imm5 (byte) ───────────────────────────────────────
    &Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T, &Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T, // 0x1C0–0x1C7
    &Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T, &Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T, // 0x1C8–0x1CF
    &Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T, &Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T, // 0x1D0–0x1D7
    &Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T, &Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T,&Interpreter::strbImm5T, // 0x1D8–0x1DF

    // ── 0x1E0–0x1FF  LDRB imm5 (byte) ───────────────────────────────────────
    &Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T, &Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T, // 0x1E0–0x1E7
    &Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T, &Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T, // 0x1E8–0x1EF
    &Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T, &Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T, // 0x1F0–0x1F7
    &Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T, &Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T,&Interpreter::ldrbImm5T, // 0x1F8–0x1FF

    // ── 0x200–0x21F  STRH imm5 (halfword) ───────────────────────────────────
    &Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T, &Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T, // 0x200–0x207
    &Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T, &Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T, // 0x208–0x20F
    &Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T, &Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T, // 0x210–0x217
    &Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T, &Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T,&Interpreter::strhImm5T, // 0x218–0x21F

    // ── 0x220–0x23F  LDRH imm5 (halfword) ───────────────────────────────────
    &Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T, &Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T, // 0x220–0x227
    &Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T, &Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T, // 0x228–0x22F
    &Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T, &Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T, // 0x230–0x237
    &Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T, &Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T,&Interpreter::ldrhImm5T, // 0x238–0x23F

    // ── 0x240–0x25F  STR  SP-relative ────────────────────────────────────────
    &Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT, &Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT,  // 0x240–0x247
    &Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT, &Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT,  // 0x248–0x24F
    &Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT, &Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT,  // 0x250–0x257
    &Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT, &Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT,&Interpreter::strSpT,  // 0x258–0x25F

    // ── 0x260–0x27F  LDR  SP-relative ────────────────────────────────────────
    &Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT, &Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT,  // 0x260–0x267
    &Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT, &Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT,  // 0x268–0x26F
    &Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT, &Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT,  // 0x270–0x277
    &Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT, &Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT,&Interpreter::ldrSpT,  // 0x278–0x27F

    // ── 0x280–0x29F  ADD PC-relative (ADR) ───────────────────────────────────
    &Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT, &Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT,  // 0x280–0x287
    &Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT, &Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT,  // 0x288–0x28F
    &Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT, &Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT,  // 0x290–0x297
    &Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT, &Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT,&Interpreter::addPcT,  // 0x298–0x29F

    // ── 0x2A0–0x2BF  ADD SP-relative ─────────────────────────────────────────
    &Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT, &Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT,  // 0x2A0–0x2A7
    &Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT, &Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT,  // 0x2A8–0x2AF
    &Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT, &Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT,  // 0x2B0–0x2B7
    &Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT, &Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT,&Interpreter::addSpT,  // 0x2B8–0x2BF

    // ── 0x2C0–0x2FF  Miscellaneous 16-bit (bits 15-12 = 1011) ────────────────
    // 0x2C0–0x2C3: ADD SP,#imm7   (bits 9-8 = 00, bit 7 = 0)
    // 0x2C4–0x2CF: undefined
    // 0x2D0–0x2D3: PUSH (no LR)
    // 0x2D4–0x2D7: PUSH (with LR)
    // 0x2D8–0x2EF: undefined
    // 0x2F0–0x2F3: POP  (no PC)
    // 0x2F4–0x2F7: POP  (with PC)
    // 0x2F8–0x2FF: undefined
    &Interpreter::addSpImmT,&Interpreter::addSpImmT,&Interpreter::addSpImmT,&Interpreter::addSpImmT,                                              // 0x2C0–0x2C3
    &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb,                                               // 0x2C4–0x2C7
    &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb,  &Interpreter::unkThumb,&Interpreter::unkThumb,&Interpreter::unkThumb,&Interpreter::unkThumb, // 0x2C8–0x2CF
    &Interpreter::pushT,    &Interpreter::pushT,    &Interpreter::pushT,    &Interpreter::pushT,                                                   // 0x2D0–0x2D3
    &Interpreter::pushLrT,  &Interpreter::pushLrT,  &Interpreter::pushLrT,  &Interpreter::pushLrT,                                                // 0x2D4–0x2D7
    &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb,  &Interpreter::unkThumb,&Interpreter::unkThumb,&Interpreter::unkThumb,&Interpreter::unkThumb, // 0x2D8–0x2DF
    &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb,  &Interpreter::unkThumb,&Interpreter::unkThumb,&Interpreter::unkThumb,&Interpreter::unkThumb, // 0x2E0–0x2E7
    &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb,  &Interpreter::unkThumb,&Interpreter::unkThumb,&Interpreter::unkThumb,&Interpreter::unkThumb, // 0x2E8–0x2EF
    &Interpreter::popT,     &Interpreter::popT,     &Interpreter::popT,     &Interpreter::popT,                                                    // 0x2F0–0x2F3
    &Interpreter::popPcT,   &Interpreter::popPcT,   &Interpreter::popPcT,   &Interpreter::popPcT,                                                 // 0x2F4–0x2F7
    &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb,  &Interpreter::unkThumb,&Interpreter::unkThumb,&Interpreter::unkThumb,&Interpreter::unkThumb, // 0x2F8–0x2FF

    // ── 0x300–0x31F  STMIA (bits 15-11 = 11000) ─────────────────────────────
    &Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT, &Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT,  // 0x300–0x307
    &Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT, &Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT,  // 0x308–0x30F
    &Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT, &Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT,  // 0x310–0x317
    &Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT, &Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT,&Interpreter::stmiaT,  // 0x318–0x31F

    // ── 0x320–0x33F  LDMIA (bits 15-11 = 11001) ─────────────────────────────
    &Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT, &Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT,  // 0x320–0x327
    &Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT, &Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT,  // 0x328–0x32F
    &Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT, &Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT,  // 0x330–0x337
    &Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT, &Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT,&Interpreter::ldmiaT,  // 0x338–0x33F

    // ── 0x340–0x37F  Conditional branch + SWI (bits 15-12 = 1101) ────────────
    // Each condition occupies exactly 4 entries (bits 11-8 = condition code).
    // This is the THUMB branch hot path — most games branch far more often
    // than they execute multiply or coprocessor instructions.
    // Ordering matches ARM condition codes 0–13 plus undefined + SWI.
    &Interpreter::beqT,     &Interpreter::beqT,     &Interpreter::beqT,     &Interpreter::beqT,      // 0x340–0x343  EQ
    &Interpreter::bneT,     &Interpreter::bneT,     &Interpreter::bneT,     &Interpreter::bneT,      // 0x344–0x347  NE
    &Interpreter::bcsT,     &Interpreter::bcsT,     &Interpreter::bcsT,     &Interpreter::bcsT,      // 0x348–0x34B  CS/HS
    &Interpreter::bccT,     &Interpreter::bccT,     &Interpreter::bccT,     &Interpreter::bccT,      // 0x34C–0x34F  CC/LO
    &Interpreter::bmiT,     &Interpreter::bmiT,     &Interpreter::bmiT,     &Interpreter::bmiT,      // 0x350–0x353  MI
    &Interpreter::bplT,     &Interpreter::bplT,     &Interpreter::bplT,     &Interpreter::bplT,      // 0x354–0x357  PL
    &Interpreter::bvsT,     &Interpreter::bvsT,     &Interpreter::bvsT,     &Interpreter::bvsT,      // 0x358–0x35B  VS
    &Interpreter::bvcT,     &Interpreter::bvcT,     &Interpreter::bvcT,     &Interpreter::bvcT,      // 0x35C–0x35F  VC
    &Interpreter::bhiT,     &Interpreter::bhiT,     &Interpreter::bhiT,     &Interpreter::bhiT,      // 0x360–0x363  HI
    &Interpreter::blsT,     &Interpreter::blsT,     &Interpreter::blsT,     &Interpreter::blsT,      // 0x364–0x367  LS
    &Interpreter::bgeT,     &Interpreter::bgeT,     &Interpreter::bgeT,     &Interpreter::bgeT,      // 0x368–0x36B  GE
    &Interpreter::bltT,     &Interpreter::bltT,     &Interpreter::bltT,     &Interpreter::bltT,      // 0x36C–0x36F  LT
    &Interpreter::bgtT,     &Interpreter::bgtT,     &Interpreter::bgtT,     &Interpreter::bgtT,      // 0x370–0x373  GT
    &Interpreter::bleT,     &Interpreter::bleT,     &Interpreter::bleT,     &Interpreter::bleT,      // 0x374–0x377  LE
    &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb, &Interpreter::unkThumb,  // 0x378–0x37B  undefined (cond=1110)
    &Interpreter::swiT,     &Interpreter::swiT,     &Interpreter::swiT,     &Interpreter::swiT,      // 0x37C–0x37F  SWI

    // ── 0x380–0x39F  B unconditional (bits 15-11 = 11100) ────────────────────
    &Interpreter::bT,&Interpreter::bT,&Interpreter::bT,&Interpreter::bT, &Interpreter::bT,&Interpreter::bT,&Interpreter::bT,&Interpreter::bT,  // 0x380–0x387
    &Interpreter::bT,&Interpreter::bT,&Interpreter::bT,&Interpreter::bT, &Interpreter::bT,&Interpreter::bT,&Interpreter::bT,&Interpreter::bT,  // 0x388–0x38F
    &Interpreter::bT,&Interpreter::bT,&Interpreter::bT,&Interpreter::bT, &Interpreter::bT,&Interpreter::bT,&Interpreter::bT,&Interpreter::bT,  // 0x390–0x397
    &Interpreter::bT,&Interpreter::bT,&Interpreter::bT,&Interpreter::bT, &Interpreter::bT,&Interpreter::bT,&Interpreter::bT,&Interpreter::bT,  // 0x398–0x39F

    // ── 0x3A0–0x3BF  BLX offset (bits 15-11 = 11101) ─────────────────────────
    // Second half of a BL/BLX pair: LR ← PC + (imm11 << 1), branch to LR
    &Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT, &Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT, // 0x3A0–0x3A7
    &Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT, &Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT, // 0x3A8–0x3AF
    &Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT, &Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT, // 0x3B0–0x3B7
    &Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT, &Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT,&Interpreter::blxOffT, // 0x3B8–0x3BF

    // ── 0x3C0–0x3DF  BL setup (bits 15-11 = 11110) ───────────────────────────
    // First half of a BL pair: LR ← PC + SignExtend(imm11 << 12)
    &Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT, &Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT, // 0x3C0–0x3C7
    &Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT, &Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT, // 0x3C8–0x3CF
    &Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT, &Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT, // 0x3D0–0x3D7
    &Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT, &Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT,&Interpreter::blSetupT, // 0x3D8–0x3DF

    // ── 0x3E0–0x3FF  BL offset (bits 15-11 = 11111) ─────────────────────────
    // Second half of a BL pair: PC ← LR + (imm11 << 1), LR ← old PC | 1
    &Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT, &Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT,  // 0x3E0–0x3E7
    &Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT, &Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT,  // 0x3E8–0x3EF
    &Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT, &Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT,  // 0x3F0–0x3F7
    &Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT, &Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT,&Interpreter::blOffT,  // 0x3F8–0x3FF
};

// ════════════════════════════════════════════════════════════════════════════
// ARM condition evaluation table
// ════════════════════════════════════════════════════════════════════════════
// Index = (cond << 4) | NZCV   (256 entries total)
// Value:  0 = condition false, 1 = condition true, 2 = always/undefined
//
// PowerPC optimisation:
//   • const + aligned(32): resides in .rodata; fits in 8 PPC cache lines.
//   • The dispatch loop in interpreter.cpp loads a byte with lbzx, so the
//     table must be byte-addressable — uint8_t is correct.
//   • 16-wide rows: each row = exactly one 16-byte quarter cache line,
//     making the per-condition layout visually verifiable at a glance.
// ════════════════════════════════════════════════════════════════════════════

//__attribute__((section(".rodata"), aligned(32)))
const uint8_t Interpreter::condition[256] =
{
    //        NZCV: 0000 0001 0010 0011  0100 0101 0110 0111
    //              0000 0001 0010 0011  0100 0101 0110 0111
    //              1000 1001 1010 1011  1100 1101 1110 1111
    // EQ (Z=1):
    0,0,0,0, 1,1,1,1, 0,0,0,0, 1,1,1,1,   // cond=0x0
    // NE (Z=0):
    1,1,1,1, 0,0,0,0, 1,1,1,1, 0,0,0,0,   // cond=0x1
    // CS/HS (C=1):
    0,0,1,1, 0,0,1,1, 0,0,1,1, 0,0,1,1,   // cond=0x2
    // CC/LO (C=0):
    1,1,0,0, 1,1,0,0, 1,1,0,0, 1,1,0,0,   // cond=0x3
    // MI (N=1):
    0,0,0,0, 0,0,0,0, 1,1,1,1, 1,1,1,1,   // cond=0x4
    // PL (N=0):
    1,1,1,1, 1,1,1,1, 0,0,0,0, 0,0,0,0,   // cond=0x5
    // VS (V=1):
    0,1,0,1, 0,1,0,1, 0,1,0,1, 0,1,0,1,   // cond=0x6
    // VC (V=0):
    1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0,   // cond=0x7
    // HI (C=1 && Z=0):
    0,0,1,1, 0,0,0,0, 0,0,1,1, 0,0,0,0,   // cond=0x8
    // LS (C=0 || Z=1):
    1,1,0,0, 1,1,1,1, 1,1,0,0, 1,1,1,1,   // cond=0x9
    // GE (N=V):
    1,0,1,0, 1,0,1,0, 0,1,0,1, 0,1,0,1,   // cond=0xA
    // LT (N!=V):
    0,1,0,1, 0,1,0,1, 1,0,1,0, 1,0,1,0,   // cond=0xB
    // GT (Z=0 && N=V):
    1,0,1,0, 0,0,0,0, 0,1,0,1, 0,0,0,0,   // cond=0xC
    // LE (Z=1 || N!=V):
    0,1,0,1, 1,1,1,1, 1,0,1,0, 1,1,1,1,   // cond=0xD
    // AL (always):
    1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,   // cond=0xE
    // Reserved (NV — undefined on ARMv5, used for BLX imm on ARMv5T):
    2,2,2,2, 2,2,2,2, 2,2,2,2, 2,2,2,2,   // cond=0xF
};

// ════════════════════════════════════════════════════════════════════════════
// Population-count (bitCount) lookup table
// ════════════════════════════════════════════════════════════════════════════
// bitCount[n] = number of set bits in n, for n in [0, 255].
// Used by LDM/STM to determine the number of registers transferred, which
// directly controls the cycle count and the address increment.
//
// PowerPC optimisation:
//   • 256 bytes fit in exactly 8 PPC cache lines (32 bytes each).
//   • aligned(32): guarantees the first entry is on a cache-line boundary
//     so a single dcbt prefetch warms the whole table.
//   • 16-wide rows: one row = one half-cache-line; easy to verify by
//     checking that row[n>>4] sums match Pascal's triangle coefficients.
//   • Values range 0–8; uint8_t saves space vs uint32_t (4× smaller),
//     keeping the whole table in L1.
//
// Verification: each row is the binomial expansion of (1+1)^4 = 16 entries
// with the expected number of 1-bits for a 4-bit prefix group.
// ════════════════════════════════════════════════════════════════════════════

//__attribute__((section(".rodata"), aligned(32)))
const uint8_t Interpreter::bitCount[256] =
{
    // n:  +0 +1 +2 +3  +4 +5 +6 +7  +8 +9 +A +B  +C +D +E +F
    /* 0x00 */  0, 1, 1, 2,  1, 2, 2, 3,  1, 2, 2, 3,  2, 3, 3, 4,
    /* 0x10 */  1, 2, 2, 3,  2, 3, 3, 4,  2, 3, 3, 4,  3, 4, 4, 5,
    /* 0x20 */  1, 2, 2, 3,  2, 3, 3, 4,  2, 3, 3, 4,  3, 4, 4, 5,
    /* 0x30 */  2, 3, 3, 4,  3, 4, 4, 5,  3, 4, 4, 5,  4, 5, 5, 6,
    /* 0x40 */  1, 2, 2, 3,  2, 3, 3, 4,  2, 3, 3, 4,  3, 4, 4, 5,
    /* 0x50 */  2, 3, 3, 4,  3, 4, 4, 5,  3, 4, 4, 5,  4, 5, 5, 6,
    /* 0x60 */  2, 3, 3, 4,  3, 4, 4, 5,  3, 4, 4, 5,  4, 5, 5, 6,
    /* 0x70 */  3, 4, 4, 5,  4, 5, 5, 6,  4, 5, 5, 6,  5, 6, 6, 7,
    /* 0x80 */  1, 2, 2, 3,  2, 3, 3, 4,  2, 3, 3, 4,  3, 4, 4, 5,
    /* 0x90 */  2, 3, 3, 4,  3, 4, 4, 5,  3, 4, 4, 5,  4, 5, 5, 6,
    /* 0xA0 */  2, 3, 3, 4,  3, 4, 4, 5,  3, 4, 4, 5,  4, 5, 5, 6,
    /* 0xB0 */  3, 4, 4, 5,  4, 5, 5, 6,  4, 5, 5, 6,  5, 6, 6, 7,
    /* 0xC0 */  2, 3, 3, 4,  3, 4, 4, 5,  3, 4, 4, 5,  4, 5, 5, 6,
    /* 0xD0 */  3, 4, 4, 5,  4, 5, 5, 6,  4, 5, 5, 6,  5, 6, 6, 7,
    /* 0xE0 */  3, 4, 4, 5,  4, 5, 5, 6,  4, 5, 5, 6,  5, 6, 6, 7,
    /* 0xF0 */  4, 5, 5, 6,  5, 6, 6, 7,  5, 6, 6, 7,  6, 7, 7, 8,
};
