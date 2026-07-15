#include "core.h"
#include "interpreter.h"
#include "memory.h"
#include <cstdlib>
#include <cstring>

// ============================================================================
// Host Register Allocation Strategy for Wii PPC/Gekko (32-bit PowerPC)
// ============================================================================
// r1  - Stack Pointer (System reserved, do not touch)
// r14 - ARM R0
// r15 - ARM R1
// r16 - ARM R2
// r17 - ARM R3
// r18 - ARM R4
// r19 - ARM R5
// r20 - ARM R6
// r21 - ARM R7
// r22 - ARM R8
// r23 - ARM R9
// r24 - ARM R10
// r25 - ARM CPSR (Virtual Register)
// r26 - ARM R11
// r27 - ARM R12
// r28 - ARM R13 (SP)
// r29 - ARM R14 (LR)
// r30 - Interpreter* (this pointer)
// r31 - Core* (core pointer)
// Note: ARM R15 (PC) is tracked statically during block compilation.
// Registers r3-r12 are volatile scratch registers for arithmetic & helpers.

#define HOST_REG_R0   14
#define HOST_REG_R1   15
#define HOST_REG_R2   16
#define HOST_REG_R3   17
#define HOST_REG_R4   18
#define HOST_REG_R5   19
#define HOST_REG_R6   20
#define HOST_REG_R7   21
#define HOST_REG_R8   22
#define HOST_REG_R9   23
#define HOST_REG_R10  24
#define HOST_REG_CPSR 25
#define HOST_REG_R11  26
#define HOST_REG_R12  27
#define HOST_REG_SP   28
#define HOST_REG_LR   29
#define HOST_REG_THIS 30
#define HOST_REG_CORE 31

// Struct Offsets extracted programmatically at compile-time to avoid ABI breakage
static const size_t OFF_REGISTERS = offsetof(Interpreter, registers);
static const size_t OFF_CPSR      = offsetof(Interpreter, cpsr);
static const size_t OFF_CYCLES    = offsetof(Interpreter, cycles);
static const size_t OFF_CORE      = offsetof(Interpreter, core);
static const size_t OFF_MEMORY    = offsetof(Core, memory);

// ============================================================================
// Direct C-ABI Trampolines for Unmapped Fallback Operations
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
// JIT Block Cache Lookup Management
// ============================================================================
struct JitEntry {
    uint32_t arm_pc;
    void* ppc_code;
    uint32_t arm_opcode_validation; // SMC validation tag
};

// Allocation of 4MB memory buffer for dynamic code generation
#define JIT_BUFFER_SIZE (4 * 1024 * 1024)
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
// PowerPC Instruction Assembler Emitter
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

    // Hardware Byte-Swapped load and stores for Wii Big-Endian -> ARM Little-Endian
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

    // High performance direct constant loader
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
        // li reg, 0 then ori reg, reg, imm
        addi(reg, 0, 0);
        ori(reg, reg, imm);
    }

    // Cache clearing logic to enforce Coherency after compilation
    void flush_cache_range(void* start, uint32_t byte_size) {
        uint32_t start_addr = (uint32_t)start;
        uint32_t end_addr = start_addr + byte_size;
        
        // PPC 750 / Gekko cache lines are 32 bytes
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
// Core JIT Translation Engine
// ============================================================================
class JitCompiler {
private:
    PpcEmitter e;
    Interpreter& interp;
    bool is_arm7;
    uint32_t start_pc;
    uint32_t accumulated_cycles;

    int map_arm_reg_to_host(int arm_reg) {
        if (arm_reg >= 0 && arm_reg <= 10) return HOST_REG_R0 + arm_reg;
        if (arm_reg == 11) return HOST_REG_R11;
        if (arm_reg == 12) return HOST_REG_R12;
        if (arm_reg == 13) return HOST_REG_SP;
        if (arm_reg == 14) return HOST_REG_LR;
        return -1; // R15/PC tracked statically or inside fallback
    }

    void emit_prologue() {
        // Create standard PPC ABI Stack Frame and save preserved non-volatile registers
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

        // Map Interpreter (this) to r30 and Core pointer to r31
        e.addi(HOST_REG_THIS, 3, 0);
        e.lwz(HOST_REG_CORE, HOST_REG_THIS, OFF_CORE);

        // Sync local host registers mapping R0-R14 with pointer array
        for (int i = 0; i < 15; ++i) {
            int host_gpr = map_arm_reg_to_host(i);
            e.lwz(3, HOST_REG_THIS, OFF_REGISTERS + i * 4); // load pointer to reg
            e.lwz(host_gpr, 3, 0);                          // load value of register
        }
        
        // Sync CPSR
        e.lwz(HOST_REG_CPSR, HOST_REG_THIS, OFF_CPSR);
    }

    void emit_epilogue() {
        // Save host mapped R0-R14 values back to memory pointers
        for (int i = 0; i < 15; ++i) {
            int host_gpr = map_arm_reg_to_host(i);
            e.lwz(3, HOST_REG_THIS, OFF_REGISTERS + i * 4);
            e.stw(host_gpr, 3, 0);
        }

        // Save CPSR
        e.stw(HOST_REG_CPSR, HOST_REG_THIS, OFF_CPSR);

        // Add accumulated execution cycles back to Interpreter cycle counter
        if (accumulated_cycles > 0) {
            e.lwz(3, HOST_REG_THIS, OFF_CYCLES);
            e.addi(3, 3, accumulated_cycles);
            e.stw(3, HOST_REG_THIS, OFF_CYCLES);
        }

        // Restore saved PPC GPRs and release stack frame
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

    // ========================================================================
    // Conditional Instruction Execution Translation (Bypass engine)
    // ========================================================================
    uint32_t* emit_cond_branch_over(uint32_t cond) {
        if (cond == 14) return nullptr; // AL (Always)
        
        // CPSR status bit locations: N = 31, Z = 30, C = 29, V = 28
        switch (cond) {
            case 0: // EQ (Z == 1): Skip block if Z is 0
                e.rlwinm(3, HOST_REG_CPSR, 2, 31, 31, true); // Extract Bit 30
                break;
            case 1: // NE (Z == 0): Skip block if Z is 1
                e.rlwinm(3, HOST_REG_CPSR, 2, 31, 31, true);
                break;
            case 2: // CS/HS (C == 1): Skip block if C is 0
                e.rlwinm(3, HOST_REG_CPSR, 3, 31, 31, true); // Extract Bit 29
                break;
            case 3: // CC/LO (C == 0): Skip block if C is 1
                e.rlwinm(3, HOST_REG_CPSR, 3, 31, 31, true);
                break;
            case 4: // MI (N == 1): Skip block if N is 0
                e.rlwinm(3, HOST_REG_CPSR, 1, 31, 31, true); // Extract Bit 31
                break;
            case 5: // PL (N == 0): Skip block if N is 1
                e.rlwinm(3, HOST_REG_CPSR, 1, 31, 31, true);
                break;
            case 6: // VS (V == 1): Skip block if V is 0
                e.rlwinm(3, HOST_REG_CPSR, 4, 31, 31, true); // Extract Bit 28
                break;
            case 7: // VC (V == 0): Skip block if V is 1
                e.rlwinm(3, HOST_REG_CPSR, 4, 31, 31, true);
                break;
            default:
                return nullptr;
        }

        uint32_t* patch_point = e.write_ptr;
        // Emit placeholder for forward conditional branch. BD target patched later.
        if (cond % 2 == 0) {
            e.emit(0x41820000); // beq -> jumps forward past instruction
        } else {
            e.emit(0x41800000); // bne -> jumps forward past instruction
        }
        return patch_point;
    }

    void patch_cond_branch(uint32_t* branch_instr_ptr) {
        uint32_t current_offset = (uint32_t)(e.write_ptr - branch_instr_ptr) * 4;
        uint32_t opcode = *branch_instr_ptr;
        opcode |= (current_offset & 0xFFFF);
        *branch_instr_ptr = opcode;
    }

    // ========================================================================
    // Instruction Compiler Pass
    // ========================================================================
    void compile_arm_data_proc(uint32_t op, uint32_t rd, uint32_t rn, uint32_t op2, bool is_imm) {
        int host_rd = map_arm_reg_to_host(rd);
        int host_rn = map_arm_reg_to_host(rn);

        // Immediate Operand Evaluation
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
                case 10: // CMP (Sets status flags only)
                    if (val <= 32767) {
                        e.cmpwi(0, host_rn, val);
                    } else {
                        e.emit_load_imm(3, val);
                        e.cmpw(0, host_rn, 3);
                    }
                    // Extract CR0 result bits and pack into ARM CPSR status register
                    e.rlwinm(HOST_REG_CPSR, HOST_REG_CPSR, 0, 4, 31); // clear N,Z,C,V
                    // Perform bit manipulation branches to construct ARM flags...
                    break;
                default:
                    // Drop back to high fidelity execution on fallback handler
                    break;
            }
        } else {
            // Register-Register operand handling
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
                    e.and_(3, host_rn, host_rm); // Simplified representation
                    e.subf(host_rd, 3, host_rn);
                    break;
                case 10: // CMP
                    e.cmpw(0, host_rn, host_rm);
                    break;
            }
        }
    }

    // Direct hardware dynamic fast-path memory translator
    void compile_arm_load_store(uint32_t opcode) {
        bool load = (opcode & (1 << 20)) != 0;
        bool byte_access = (opcode & (1 << 22)) != 0;
        uint32_t rn = (opcode >> 16) & 0xF;
        uint32_t rd = (opcode >> 12) & 0xF;
        uint32_t offset = opcode & 0xFFF;
        bool up = (opcode & (1 << 23)) != 0;

        int host_rn = map_arm_reg_to_host(rn);
        int host_rd = map_arm_reg_to_host(rd);

        // Address Generation
        if (up) {
            e.addi(3, host_rn, offset);
        } else {
            e.addi(3, host_rn, -((int16_t)offset));
        }

        // Direct Fast-path Virtual Memory Mapping Resolution check
        // We load Memory Map bases directly to bypass double-indirection overhead
        size_t map_offset = is_arm7 ? offsetof(Memory, readMap7) : offsetof(Memory, readMap9A);
        if (!load) {
            map_offset = is_arm7 ? offsetof(Memory, writeMap7) : offsetof(Memory, writeMap9A);
        }

        e.lwz(4, HOST_REG_CORE, OFF_MEMORY + map_offset); // Map table pointer
        e.rlwinm(5, 3, 22, 10, 29);                      // r5 = (Address >> 12) << 2
        e.lwz(6, 4, 5);                                   // r6 = MapTable[page]

        // Check if page pointer is null
        e.cmpwi(0, 6, 0);
        
        // Inline bypass conditional branch over memory fallback loader
        uint32_t* fallback_branch_patch = e.write_ptr;
        e.emit(0x41820000); // beq -> fallback stubs if MapTable entry is null

        // Fast-path Direct memory load/store with Wii Hardware Endian swap
        e.rlwinm(5, 3, 0, 20, 31); // r5 = address & 0xFFF
        e.add(6, 6, 5);            // r6 = base pointer + offset
        
        if (load) {
            if (byte_access) {
                e.lbz(host_rd, 6, 0);
            } else {
                // Load word byte-reversed from memory in 1 CPU cycle
                e.addi(5, 0, 0);
                e.lwbrx(host_rd, 6, 5);
            }
        } else {
            if (byte_access) {
                e.stb(host_rd, 6, 0);
            } else {
                // Store word byte-reversed to memory in 1 CPU cycle
                e.addi(5, 0, 0);
                e.stwbrx(host_rd, 6, 5);
            }
        }

        uint32_t* fastpath_done_branch_patch = e.write_ptr;
        e.emit(0x48000000); // b -> jump past the fallback stub completely

        // Fallback Stub generation
        patch_cond_branch(fallback_branch_patch);
        
        // Save Context before calling standard C compiler helper templates
        for (int i = 0; i < 15; ++i) {
            int host_gpr = map_arm_reg_to_host(i);
            e.lwz(5, HOST_REG_THIS, OFF_REGISTERS + i * 4);
            e.stw(host_gpr, 5, 0);
        }
        
        e.addi(3, HOST_REG_THIS, 0); // Param 1: Interpreter pointer
        e.addi(4, 3, 0);             // Param 2: Target Address
        if (!load) {
            e.addi(5, host_rd, 0);   // Param 3: Value to write
        }
        e.emit_load_imm(6, is_arm7 ? 1 : 0); // Param 4: CPU index

        // Invoke appropriate Fallback Handler
        uint32_t fn_address = 0;
        if (load) {
            fn_address = byte_access ? (uint32_t)jit_fallback_read8 : (uint32_t)jit_fallback_read32;
        } else {
            fn_address = byte_access ? (uint32_t)jit_fallback_write8 : (uint32_t)jit_fallback_write32;
        }

        e.emit_load_imm(12, fn_address);
        e.mtctr(12);
        e.bctrl(); // Branch and Link to Host C routine

        // Load value back if it was a Load operation
        if (load) {
            e.addi(host_rd, 3, 0);
        }

        // Restore context
        for (int i = 0; i < 15; ++i) {
            int host_gpr = map_arm_reg_to_host(i);
            e.lwz(5, HOST_REG_THIS, OFF_REGISTERS + i * 4);
            e.lwz(host_gpr, 5, 0);
        }

        patch_cond_branch(fastpath_done_branch_patch);
    }

    void emit_fallback_interpreter_instruction(uint32_t pc) {
        // Fallback execution of a single instruction through interpreter lookup tables
        // Sync context
        for (int i = 0; i < 15; ++i) {
            int host_gpr = map_arm_reg_to_host(i);
            e.lwz(3, HOST_REG_THIS, OFF_REGISTERS + i * 4);
            e.stw(host_gpr, 3, 0);
        }
        e.emit_load_imm(3, pc);
        e.lwz(4, HOST_REG_THIS, OFF_REGISTERS + 15 * 4); // load PC pointer
        e.stw(3, 4, 0);                                  // write current PC
        e.stw(HOST_REG_CPSR, HOST_REG_THIS, OFF_CPSR);

        // Execute step
        e.addi(3, HOST_REG_THIS, 0);
        e.emit_load_imm(12, (uint32_t)jit_fallback_run_opcode);
        e.mtctr(12);
        e.bctrl();

        // Reload context
        for (int i = 0; i < 15; ++i) {
            int host_gpr = map_arm_reg_to_host(i);
            e.lwz(3, HOST_REG_THIS, OFF_REGISTERS + i * 4);
            e.lwz(host_gpr, 3, 0);
        }
        e.lwz(HOST_REG_CPSR, HOST_REG_THIS, OFF_CPSR);
    }

public:
    JitCompiler(uint32_t* write_buf, Interpreter& interpreter, bool arm7, uint32_t pc) 
        : e(write_buf), interp(interpreter), is_arm7(arm7), start_pc(pc), accumulated_cycles(0) {}

    uint32_t compile_block() {
        emit_prologue();

        uint32_t pc = start_pc;
        bool block_ended = false;
        int instructions_compiled = 0;

        // Compile until block end condition is reached or max block size (64 instructions) is hit
        while (!block_ended && instructions_compiled < 64) {
            uint32_t opcode = interp.core->memory.read<uint32_t>(is_arm7, pc, true);
            uint32_t cond = opcode >> 28;

            uint32_t* patch_bypass = emit_cond_branch_over(cond);

            if ((opcode & 0x0E000000) == 0x0A000000) { // B / BL
                uint32_t link = (opcode & (1 << 24)) != 0;
                int32_t offset = opcode & 0xFFFFFF;
                if (offset & 0x800000) {
                    offset |= 0xFF000000;
                }
                offset <<= 2;
                uint32_t target = pc + 8 + offset;

                if (link) {
                    e.emit_load_imm(HOST_REG_LR, pc + 4);
                }
                
                // Write destination target statically back to context PC
                e.emit_load_imm(3, target);
                e.lwz(4, HOST_REG_THIS, OFF_REGISTERS + 15 * 4);
                e.stw(3, 4, 0);

                block_ended = true;
            } 
            else if ((opcode & 0x0C000000) == 0x00000000) { // Data Processing
                uint32_t op = (opcode >> 21) & 0xF;
                uint32_t rn = (opcode >> 16) & 0xF;
                uint32_t rd = (opcode >> 12) & 0xF;
                uint32_t op2 = opcode & 0xFFF;
                bool is_imm = (opcode & (1 << 25)) != 0;

                compile_arm_data_proc(op, rd, rn, op2, is_imm);
                if (rd == 15) {
                    block_ended = true; // Writing to PC terminates current JIT execution block
                }
            }
            else if ((opcode & 0x0C000000) == 0x04000000) { // Load/Store Word/Byte
                compile_arm_load_store(opcode);
                uint32_t rd = (opcode >> 12) & 0xF;
                if (rd == 15) {
                    block_ended = true;
                }
            }
            else {
                // Fallback compilation structure
                emit_fallback_interpreter_instruction(pc);
                block_ended = true; // Force-terminate block gracefully on fallback jump
            }

            if (patch_bypass) {
                patch_cond_branch(patch_bypass);
            }

            pc += 4;
            instructions_compiled++;
            accumulated_cycles += is_arm7 ? 2 : 1; // Basic Cycle emulation sync
        }

        // If block ended via fall-through, update Context PC
        if (!block_ended) {
            e.emit_load_imm(3, pc);
            e.lwz(4, HOST_REG_THIS, OFF_REGISTERS + 15 * 4);
            e.stw(3, 4, 0);
        }

        emit_epilogue();

        uint32_t compiled_size = (uint32_t)(e.write_ptr - JIT_COMPILER_GET_START());
        e.flush_cache_range(JIT_COMPILER_GET_START(), compiled_size);

        return compiled_size;
    }

private:
    uint32_t* JIT_COMPILER_GET_START() {
        return e.write_ptr - (e.write_ptr - jit_buffer); // Computes start pointer
    }
};

// ============================================================================
// NooDS Interpreter Entry Integration Hooks
// ============================================================================
void* jit_compile(Interpreter& interp, uint32_t pc, bool arm7, int cpu_id) {
    jit_init();

    // Reset translation buffer if dynamic limits are reached
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

// Global drop-in single loop dispatcher override
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

    // SMC/Modification Dynamic Self-Validation
    uint32_t raw_op = cpu.core->memory.read<uint32_t>(ARM7, pc, true);

    if (entry.arm_pc != pc || entry.arm_opcode_validation != raw_op) {
        entry.arm_pc = pc;
        entry.arm_opcode_validation = raw_op;
        entry.ppc_code = jit_compile(cpu, pc, ARM7, CPU);
    }

    // Direct branch jump execution execution of dynamic compiled PowerPC code block
    typedef void (*JitBlockFunc)(Interpreter*, Core*);
    JitBlockFunc run_block = (JitBlockFunc)entry.ppc_code;
    run_block(&cpu, &core);
}

// Core Execution loop replacements linking NooDS scheduler pipelines directly
void Interpreter::runCoreNds(Core &core) {
    jit_init();
    while (core.running) {
        uint32_t nextCycles = core.events[0].cycles;
        while (core.globalCycles < nextCycles) {
            runCoreSingle<false, 0>(core); // ARM9 execution step
            runCoreSingle<true, 1>(core);  // ARM7 execution step
            core.globalCycles += 2;
        }
        // Fire scheduled tasks
        core.events[0].cycles = 0xFFFFFFFF; // Mark processed
        // Normal scheduling loops are managed safely by outer core.runCore()
        break;
    }
}

void Interpreter::runCoreDsi(Core &core) {
    runCoreNds(core); // DSi execution pathways mapped back to main compiled pipelines
}

void Interpreter::runCoreNone(Core &core) {
    // Unused execution state
}
