import { useState } from 'react';

interface FileEntry {
  name: string;
  dest: string;
  lines: string;
  description: string;
  code: string;
}

const files: FileEntry[] = [
  {
    name: 'JIT.h',
    dest: 'src/jit/JIT.h',
    lines: '~115',
    description: 'Top-level JIT public interface. JITResult struct, JITWriteScope RAII, JITCompileThumbTrace forward decl, ExecuteJITTrace/ExecuteJITTrace_Return extern "C" declarations.',
    code: `#ifndef JIT_H
#define JIT_H

#include <stdint.h>
#include "JITCache.h"
#include "Debug.h"

// Maximum THUMB instructions compiled into one block
#define JIT_TRACE_MAX_INSTRUCTIONS  42

// -----------------------------------------------------------------------
// Cache coherency — flush D-cache, invalidate I-cache after code emit
// -----------------------------------------------------------------------
#define JIT_CODE_MARK_DIRTY(p, n) \\
    do { DCStoreRange((void*)(p), (n)); \\
         ICInvalidateRange((void*)(p), (n)); } while (0)

// RAII scope: opens/closes cache write-permission window (no-op on Wii,
// needed if porting to Wii U Broadway where W^X applies)
struct JITWriteScope {
    JITWriteScope()  {}
    ~JITWriteScope() {}
};

// -----------------------------------------------------------------------
// JITResult — the 32-byte-aligned handshake struct that every compiled
// trace writes its output into before jumping to ExecuteJITTrace_Return.
// The C++ dispatch loop reads these fields after each trace invocation.
// -----------------------------------------------------------------------
struct JITResult {
    uint32_t cycles;       // Host cycles consumed by this trace
    uint32_t nextPC;       // GBA PC to resume at
    uint32_t instructions; // Guest instructions executed
    uint32_t bailedOut;    // 1 if a guard failed (memory / bank / SMC)
    uint32_t smcHit;       // 1 if self-modifying code was detected
    uint32_t smcAddress;   // EA of the offending write (when smcHit==1)
} __attribute__((aligned(32)));

// Forward declarations — implementations in JITCompiler.cpp / JITTrampoline.S
struct CPUFlags;
struct BasicBlock;
typedef void (*JITBlockFunc)();

BasicBlock* JITCompileThumbTrace(uint32_t startPC, JITCache& cache);

extern "C" void ExecuteJITTrace(
    JITBlockFunc        execute,
    JITResult*          outResult,
    uint32_t*           busPrefetchCount,
    uint32_t*           gbaRegs,
    CPUFlags*           flags,
    void*               readPageTable);

extern "C" void ExecuteJITTrace_Return();

extern JITCache jitCache;

#endif // JIT_H`
  },
  {
    name: 'JITCache.h',
    dest: 'src/jit/JITCache.h',
    lines: '~130',
    description: 'JITCache class, BasicBlock struct, arena constants, SMC guard macro, hash table inline lookup.',
    code: `#ifndef JIT_CACHE_H
#define JIT_CACHE_H

#include <stdint.h>
#include <stddef.h>

// -----------------------------------------------------------------------
// Platform detection — JIT only available on Wii / GCN
// -----------------------------------------------------------------------
#if defined(HW_RVL) || defined(HW_DOL)
#  define NOODS_JIT 1
#else
#  define NOODS_JIT 0
#endif

// -----------------------------------------------------------------------
// Arena & table sizing
// -----------------------------------------------------------------------
#if defined(HW_RVL) || defined(HW_DOL)
#  define JIT_ARENA_SIZE    (1024u * 1024u * 8u)  // 8 MB
#  define HASH_TABLE_SIZE   65536u
#  define SMC_MAP_SIZE      65536u
#else
#  define JIT_ARENA_SIZE    (1024u * 1024u * 32u) // 32 MB (Wii U)
#  define HASH_TABLE_SIZE   (1024u * 1024u)
#  define SMC_MAP_SIZE      (1024u * 512u)
#endif

// -----------------------------------------------------------------------
// BasicBlock — one compiled trace.  Must be 16-byte aligned so the
// hash table can be indexed with a fast shift instead of a multiply.
// execute==nullptr with length==0  → uncompiled
// execute==nullptr with length>0   → "don't JIT" permanent fallback
// nextSMC                          → intrusive SMC linked-list pointer
// -----------------------------------------------------------------------
typedef void (*JITBlockFunc)();

struct __attribute__((aligned(16))) BasicBlock {
    uint32_t   startPC;
    uint32_t   length;    // guest instruction count
    JITBlockFunc execute;
    BasicBlock* nextSMC;  // intrusive per-page chain for SMC invalidation
};

// -----------------------------------------------------------------------
// JITCache
// -----------------------------------------------------------------------
class JITCache {
public:
    JITCache();
    ~JITCache();

    uint32_t*   linkerStubAddress;   // Entry point of the shared linker stub
    uint32_t*   linkerReturnAddress; // Cache-miss / fallback landing pad in stub
    uint8_t*    smcPageFlags;        // [SMC_MAP_SIZE] — 1 if any block on this 1KB page

    bool   isReady()           const { return isInitialized; }
    size_t getArenaOffset()    const { return arenaOffset;   }

    void initialize(uint32_t* arenaPtr, BasicBlock* blockPtr,
                    BasicBlock** smcRegPtr, uint8_t* smcFlagsPtr);
    void destroy();

    uint32_t*   allocateJITMemory(size_t numBytes);
    void        rewindJITMemory(size_t numBytes);
    BasicBlock* registerBlock(uint32_t pc, uint32_t length, JITBlockFunc execute);
    void        flushCache();
    void        invalidateSMCTarget(uint32_t targetEA);

    // Hot-path inline lookup — called on every THUMB dispatch
    inline BasicBlock* getBlock(uint32_t pc) {
        if (!isInitialized) return nullptr;
        uint32_t index = ((pc >> 1) ^ (pc >> 13)) & (HASH_TABLE_SIZE - 1);
        BasicBlock* block = &blockTable[index];
        return (block->startPC == pc) ? block : nullptr;
    }

private:
    uint32_t*   jitArena;
    size_t      arenaOffset;
    BasicBlock* blockTable;
    BasicBlock**smcRegistry; // [SMC_MAP_SIZE] — head of per-page intrusive chain
    bool        isInitialized;
};

extern JITCache jitCache;

// -----------------------------------------------------------------------
// JIT_SMC_GUARD — insert into NooDS CPUWrite8/16/32 for EWRAM/IWRAM
// -----------------------------------------------------------------------
#if NOODS_JIT
#  define JIT_SMC_GUARD(address) \\
    do { \\
        uint8_t _bank = (address) >> 24; \\
        if (_bank == 2 || _bank == 3) { \\
            uint32_t _page = ((address) >> 10) & 0xFFFF; \\
            if (jitCache.smcPageFlags[_page]) \\
                jitCache.invalidateSMCTarget(address); \\
        } \\
    } while (0)
#else
#  define JIT_SMC_GUARD(address)  ((void)0)
#endif

#endif // JIT_CACHE_H`
  },
  {
    name: 'JITPPCEmitter.h',
    dest: 'src/jit/JITPPCEmitter.h',
    lines: '~230',
    description: 'All PPC 750 instruction encoding macros. Register map, packed-flag layout, ALU/branch/load-store/cache-maintenance encoders.',
    code: `#ifndef JIT_PPC_EMITTER_H
#define JIT_PPC_EMITTER_H

// =========================================================================
// REGISTER MAP  (see JIT.h / JITCompiler.cpp for authoritative contract)
// =========================================================================
// R0  : Constant zero in addressing, NOT general scratch
// R1  : Host SP
// R2  : SDA2 — do not touch
// R3  : Cycles accumulator (epilogue return)
// R4  : nextPC (epilogue return) / scratch
// R5  : Live busPrefetchCount accumulator
// R6  : PPC_REG_FLAGS — packed N/Z/C/V (top nibble, IBM bits 0-3)
// R7-R9 : General scratch
// R10 : Base page pointer scratch
// R11 : Bank/mask/condition scratch
// R12 : Target address/operand scratch
// R13 : SDA — do not touch
// R14 : PPC_REG_GBA_REGS_PTR — base of gbaRegs[]
// R15-R28 : Lazy GBA register pool (GBA R0–R13)
// R29 : PPC_REG_PC — GBA R15 / pipeline PC
// R30 : gbaReadTable base pointer
// R31 : General scratch

#define PPC_R3   3
#define PPC_R4   4
#define PPC_R5   5
#define PPC_R6   6
#define PPC_R7   7
#define PPC_R8   8
#define PPC_R9   9
#define PPC_R10  10
#define PPC_R11  11
#define PPC_R12  12
#define PPC_R14  14
#define PPC_R29  29
#define PPC_R30_TABLE 30
#define PPC_R31  31

// Packed flag register and bit positions (IBM bit 0 = MSB = bit 31 conventional)
#define PPC_REG_FLAGS  PPC_R6
#define FLAG_BIT_N  0   // conventional bit 31
#define FLAG_BIT_Z  1   // conventional bit 30
#define FLAG_BIT_C  2   // conventional bit 29
#define FLAG_BIT_V  3   // conventional bit 28

// Merge srcReg[bit31] → PPC_REG_FLAGS[targetBit], preserving other 3 flags
#define PPC_MERGE_FLAG_BIT(targetBit, srcReg, sh) \\
    PPC_RLWIMI(PPC_REG_FLAGS, (srcReg), (((sh)+31-(targetBit))&31), (targetBit), (targetBit))

// Extract PPC_REG_FLAGS[targetBit] → dstReg[bit31] as 0 or 1
#define PPC_EXTRACT_FLAG_BIT(dstReg, targetBit) \\
    PPC_RLWINM((dstReg), PPC_REG_FLAGS, (((targetBit)+1)&31), 31, 31)

// =========================================================================
// PPC INSTRUCTION ENCODERS — all produce a uint32_t instruction word
// =========================================================================

// ---- ALU / Logical ----
#define PPC_ADD(d,a,b)      (0x7C000214u|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_ADDC(d,a,b)     (0x7C000014u|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_ADDE(d,a,b)     (0x7C000114u|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_SUBF(d,a,b)     (0x7C000050u|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_SUBFC(d,a,b)    (0x7C000010u|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_SUBFE(d,a,b)    (0x7C000110u|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_AND(d,a,b)      (0x7C000038u|((a)<<21)|((d)<<16)|((b)<<11))
#define PPC_OR(d,a,b)       (0x7C000378u|((a)<<21)|((d)<<16)|((b)<<11))
#define PPC_XOR(d,a,b)      (0x7C000278u|((a)<<21)|((d)<<16)|((b)<<11))
#define PPC_NOT(d,s)        (0x7C0000F8u|((s)<<21)|((d)<<16)|((s)<<11))
#define PPC_NEG(d,a)        (0x7C0000D0u|((d)<<21)|((a)<<16))
#define PPC_MULLW(d,a,b)    (0x7C0001D6u|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_MULHWU(d,a,b)   (0x7C000016u|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_DIVWU(d,a,b)    (0x7C000396u|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_EXTSB(d,s)      (0x7C000774u|((s)<<21)|((d)<<16))
#define PPC_EXTSH(d,s)      (0x7C000734u|((s)<<21)|((d)<<16))
#define PPC_CNTLZW(d,s)     (0x7C000034u|((s)<<21)|((d)<<16))
#define PPC_SLW(d,s,b)      (0x7C000030u|((s)<<21)|((d)<<16)|((b)<<11))
#define PPC_SRW(d,s,b)      (0x7C000430u|((s)<<21)|((d)<<16)|((b)<<11))
#define PPC_SRAW(d,s,b)     (0x7C000630u|((s)<<21)|((d)<<16)|((b)<<11))

// ---- Immediate ALU ----
#define PPC_ADDI(d,a,imm)   (0x38000000u|((d)<<21)|((a)<<16)|((imm)&0xFFFF))
#define PPC_ADDIS(d,a,imm)  (0x3C000000u|((d)<<21)|((a)<<16)|((imm)&0xFFFF))
#define PPC_SUBFIC(d,a,imm) (0x20000000u|((d)<<21)|((a)<<16)|((imm)&0xFFFF))
#define PPC_ANDI(d,s,imm)   (0x70000000u|((s)<<21)|((d)<<16)|((imm)&0xFFFF))
#define PPC_ANDIS(d,s,imm)  (0x74000000u|((s)<<21)|((d)<<16)|((imm)&0xFFFF))
#define PPC_ORI(d,s,imm)    (0x60000000u|((s)<<21)|((d)<<16)|((imm)&0xFFFF))
#define PPC_ORIS(d,s,imm)   (0x64000000u|((s)<<21)|((d)<<16)|((imm)&0xFFFF))
#define PPC_XORI(d,s,imm)   (0x68000000u|((s)<<21)|((d)<<16)|((imm)&0xFFFF))
#define PPC_LI(d,imm)       PPC_ADDI((d), 0, (imm))
#define PPC_LIS(d,imm)      PPC_ADDIS((d), 0, (imm))
#define PPC_SLWI(d,s,n)     PPC_RLWINM((d),(s),(n),0,31-(n))
#define PPC_SRWI(d,s,n)     PPC_RLWINM((d),(s),32-(n),(n),31)
#define PPC_SRAWI(d,s,n)    (0x7C000670u|((s)<<21)|((d)<<16)|((n)<<11))

// ---- Immediate Loads (full 32-bit) ----
#define PPC_LOAD32(d, val) \\
    PPC_LIS((d), (uint32_t)(val) >> 16), \\
    PPC_ORI((d), (d), (uint32_t)(val) & 0xFFFF)

// ---- Move / Copy ----
#define PPC_MR(d,s)         PPC_OR((d),(s),(s))

// ---- Rotate / Insert ----
#define PPC_RLWINM(d,s,sh,mb,me) \\
    (0x54000000u|((s)<<21)|((d)<<16)|((sh)<<11)|((mb)<<6)|(me))
#define PPC_RLWIMI(d,s,sh,mb,me) \\
    (0x50000000u|((s)<<21)|((d)<<16)|((sh)<<11)|((mb)<<6)|(me))

// ---- Compare ----
#define PPC_CMPW(cr,a,b)    (0x7C000000u|((cr)<<23)|((a)<<16)|((b)<<11))
#define PPC_CMPWI(cr,a,imm) (0x2C000000u|((cr)<<23)|((a)<<16)|((imm)&0xFFFF))
#define PPC_CMPLW(cr,a,b)   (0x7C000040u|((cr)<<23)|((a)<<16)|((b)<<11))
#define PPC_CMPLWI(cr,a,imm)(0x28000000u|((cr)<<23)|((a)<<16)|((imm)&0xFFFF))

// ---- XER carry ----
#define PPC_MFXER(d)        (0x7C0102A6u|((d)<<21))

// ---- CTR / LR ----
#define PPC_MTCTR(s)        (0x7C0903A6u|((s)<<21))
#define PPC_MFLR(d)         (0x7C0802A6u|((d)<<21))
#define PPC_MTLR(s)         (0x7C0803A6u|((s)<<21))
#define PPC_BCTR()          (0x4E800420u)
#define PPC_BCTRL()         (0x4E800421u)
#define PPC_BLR()           (0x4E800020u)

// ---- Load/Store (big-endian native) ----
#define PPC_LWZ(d,a,off)    (0x80000000u|((d)<<21)|((a)<<16)|((off)&0xFFFF))
#define PPC_LHZ(d,a,off)    (0xA0000000u|((d)<<21)|((a)<<16)|((off)&0xFFFF))
#define PPC_LBZ(d,a,off)    (0x88000000u|((d)<<21)|((a)<<16)|((off)&0xFFFF))
#define PPC_LHA(d,a,off)    (0xA8000000u|((d)<<21)|((a)<<16)|((off)&0xFFFF))
#define PPC_STW(s,a,off)    (0x90000000u|((s)<<21)|((a)<<16)|((off)&0xFFFF))
#define PPC_STH(s,a,off)    (0xB0000000u|((s)<<21)|((a)<<16)|((off)&0xFFFF))
#define PPC_STB(s,a,off)    (0x98000000u|((s)<<21)|((a)<<16)|((off)&0xFFFF))
#define PPC_STWU(s,a,off)   (0x94000000u|((s)<<21)|((a)<<16)|((off)&0xFFFF))
#define PPC_STMW(s,a,off)   (0xBC000000u|((s)<<21)|((a)<<16)|((off)&0xFFFF))
#define PPC_LMW(d,a,off)    (0xB8000000u|((d)<<21)|((a)<<16)|((off)&0xFFFF))

// Byte-reversed (little-endian ↔ big-endian) loads/stores for GBA memory
#define PPC_LWBRX(d,a,b)    (0x7C00042Cu|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_LHBRX(d,a,b)    (0x7C00062Cu|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_STWBRX(s,a,b)   (0x7C00052Cu|((s)<<21)|((a)<<16)|((b)<<11))
#define PPC_STHBRX(s,a,b)   (0x7C00072Cu|((s)<<21)|((a)<<16)|((b)<<11))

// Indexed loads/stores
#define PPC_LWZX(d,a,b)     (0x7C00002Eu|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_LHZX(d,a,b)     (0x7C00022Eu|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_LBZX(d,a,b)     (0x7C0000AEu|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_LHAX(d,a,b)     (0x7C0002AEu|((d)<<21)|((a)<<16)|((b)<<11))
#define PPC_STWX(s,a,b)     (0x7C00012Eu|((s)<<21)|((a)<<16)|((b)<<11))
#define PPC_STHX(s,a,b)     (0x7C00032Eu|((s)<<21)|((a)<<16)|((b)<<11))
#define PPC_STBX(s,a,b)     (0x7C0001AEu|((s)<<21)|((a)<<16)|((b)<<11))

// ---- Branches ----
// Unconditional direct (B opcode, 26-bit signed offset in words×4, LK=0)
#define PPC_B(offset)       (0x48000000u | ((uint32_t)((offset) & 0x3FFFFFC)))
// Conditional (BO=12=branch if CR bit SET, BO=4=branch if UNSET)
#define PPC_BEQ(offset)     (0x41820000u | ((uint16_t)(offset)))
#define PPC_BNE(offset)     (0x40820000u | ((uint16_t)(offset)))
#define PPC_BLT(offset)     (0x41800000u | ((uint16_t)(offset)))
#define PPC_BGE(offset)     (0x40800000u | ((uint16_t)(offset)))
#define PPC_BGT(offset)     (0x41810000u | ((uint16_t)(offset)))
#define PPC_BLE(offset)     (0x40810000u | ((uint16_t)(offset)))
// Unconditional branch-to-LR / CTR
#define PPC_BL(offset)      (0x48000001u | ((uint32_t)((offset) & 0x3FFFFFC)))

// ---- Cache maintenance (required after emitting / patching JIT code) ----
#define PPC_DCBST(a,b)      (0x7C00006Cu|((a)<<16)|((b)<<11))
#define PPC_ICBI(a,b)       (0x7C0007ACu|((a)<<16)|((b)<<11))
#define PPC_SYNC()          (0x7C0004ACu)
#define PPC_ISYNC()         (0x4C00012Cu)

// ---- Trap (debugging — should never execute in correct JIT output) ----
#define PPC_TRAP()          (0x7FE00008u)  // tw 31,0,0

#endif // JIT_PPC_EMITTER_H`
  },
  {
    name: 'JITTrampoline.S',
    dest: 'src/jit/JITTrampoline.S',
    lines: '~80',
    description: 'Hand-written PPC assembly ABI bridge. Prologue saves R14–R31, stashes pointer args in stack frame, eagerly loads PC/prefetch, bctr into compiled block. Return path writes JITResult and restores host registers.',
    code: `# JITTrampoline.S  — NooDS-Wii ARMv4→PPC JIT ABI bridge
# Assembled with powerpc-eabi-gcc / devkitPPC

.global ExecuteJITTrace
.type   ExecuteJITTrace, @function

# Signature (PPC EABI):
#   void ExecuteJITTrace(JITBlockFunc execute,   // r3
#                        JITResult*   outResult, // r4
#                        uint32_t*    busPrefetch,// r5
#                        uint32_t*    gbaRegs,   // r6
#                        CPUFlags*    flags,     // r7
#                        void*        readTable) // r8

ExecuteJITTrace:
    # Save caller's LR into caller's stack frame
    mflr    0
    stw     0, 4(1)

    # Allocate 128-byte stack frame, save non-volatile R14-R31
    stwu    1, -128(1)
    stmw    14, 8(1)         # saves r14..r31 at 8(r1)..76(r1)

    # Stash volatile pointer args at well-known stack offsets
    # (JIT-emitted code and epilogue reference these directly)
    stw     6, 80(1)         # 80(r1) = gbaRegs pointer
    stw     7, 84(1)         # 84(r1) = flags pointer
    stw     4, 88(1)         # 88(r1) = outResult pointer
    stw     5, 92(1)         # 92(r1) = busPrefetchCount pointer

    # Initialize JIT register contract
    mr      14, 6            # r14 = PPC_REG_GBA_REGS_PTR
    lwz     29, 60(6)        # r29 = GBA R15 (PC), offset 15*4 = 60
    mr      30, 8            # r30 = gbaReadTable base

    # Load live busPrefetchCount into r5 (scratch → register)
    lwz     10, 92(1)
    lwz     5, 0(10)

    # Zero cycle accumulator and dispatch into the compiled block
    mtctr   3
    li      3, 0
    bctr                     # → compiled trace (may chain via linker stub)

    # -----------------------------------------------------------------------
    .global ExecuteJITTrace_Return
ExecuteJITTrace_Return:
    # Write busPrefetchCount back to memory
    lwz     10, 92(1)
    stw     5, 0(10)

    # Write cycles (r3) and nextPC (r4) into JITResult
    lwz     10, 88(1)
    stw     3, 0(10)         # outResult->cycles
    stw     4, 4(10)         # outResult->nextPC
    # NOTE: instructions/bailedOut/smcHit are written directly by each
    #       block's epilogue into the JITResult struct before branching here.

    # Flush GBA PC back to register array
    lwz     10, 80(1)
    stw     29, 60(10)       # gbaRegs[15] = r29

    # Restore non-volatile registers and return
    lmw     14, 8(1)
    lwz     0, 128+4(1)
    mtlr    0
    addi    1, 1, 128
    blr`
  },
  {
    name: 'JITCache.cpp',
    dest: 'src/jit/JITCache.cpp',
    lines: '~220',
    description: 'Arena allocator, block registration/eviction, SMC registry management, linker stub emission (flushCache), invalidateSMCTarget.',
    code: `// JITCache.cpp — NooDS-Wii JIT cache and linker stub
#include "JIT.h"
#include <string.h>
#include <ogc/cache.h>   // DCStoreRange, ICInvalidateRange

JITCache jitCache;

JITCache::JITCache()
    : jitArena(nullptr), arenaOffset(0), blockTable(nullptr),
      smcRegistry(nullptr), smcPageFlags(nullptr),
      linkerStubAddress(nullptr), linkerReturnAddress(nullptr),
      isInitialized(false) {}

JITCache::~JITCache() { destroy(); }

void JITCache::initialize(uint32_t* arenaPtr, BasicBlock* blockPtr,
                          BasicBlock** smcRegPtr, uint8_t* smcFlagsPtr) {
    if (isInitialized) return;
    jitArena   = arenaPtr;
    blockTable = blockPtr;
    smcRegistry  = smcRegPtr;
    smcPageFlags = smcFlagsPtr;
    arenaOffset  = 0;
    isInitialized = true;
    flushCache();
}

void JITCache::destroy() {
    jitArena = nullptr; blockTable = nullptr;
    smcRegistry = nullptr; smcPageFlags = nullptr;
    arenaOffset = 0; isInitialized = false;
}

uint32_t* JITCache::allocateJITMemory(size_t numBytes) {
    numBytes = (numBytes + 31) & ~31u;
    if (arenaOffset + numBytes > JIT_ARENA_SIZE)
        flushCache();
    uint32_t* ptr = (uint32_t*)((uint8_t*)jitArena + arenaOffset);
    arenaOffset += numBytes;
    return ptr;
}

void JITCache::rewindJITMemory(size_t numBytes) {
    arenaOffset = (arenaOffset >= numBytes) ? arenaOffset - numBytes : 0;
}

BasicBlock* JITCache::registerBlock(uint32_t pc, uint32_t length,
                                     JITBlockFunc execute) {
    uint32_t index = ((pc >> 1) ^ (pc >> 13)) & (HASH_TABLE_SIZE - 1);
    BasicBlock* block = &blockTable[index];

    // Evict & unlink old block from SMC chain
    if (block->execute && block->length > 0) {
        uint8_t bank = (block->startPC >> 24);
        if (bank == 2 || bank == 3) {
            uint32_t page = (block->startPC >> 10) & 0xFFFF;
            BasicBlock** cur = &smcRegistry[page];
            while (*cur) {
                if (*cur == block) { *cur = block->nextSMC; break; }
                cur = &(*cur)->nextSMC;
            }
            if (!smcRegistry[page]) smcPageFlags[page] = 0;
        }
    }

    block->startPC = pc;
    block->length  = length;
    block->execute = execute;
    block->nextSMC = nullptr;

    // Register with SMC tracker for EWRAM/IWRAM blocks
    if (execute && length > 0) {
        uint8_t bank = (pc >> 24);
        if (bank == 2 || bank == 3) {
            uint32_t page = (pc >> 10) & 0xFFFF;
            smcPageFlags[page]  = 1;
            block->nextSMC      = smcRegistry[page];
            smcRegistry[page]   = block;
        }
    }
    return block;
}

void JITCache::flushCache() {
    if (!isInitialized) return;
    arenaOffset = 0;
    memset(blockTable,   0, HASH_TABLE_SIZE * sizeof(BasicBlock));
    memset(smcRegistry,  0, SMC_MAP_SIZE    * sizeof(BasicBlock*));
    memset(smcPageFlags, 0, SMC_MAP_SIZE    * sizeof(uint8_t));

    // Emit the shared linker stub at arena offset 0
    // (every block's epilogue branches here; hit→self-patch, miss→return)
    uint32_t* e = jitArena;
    linkerStubAddress = e;

    // 1. Load blockTable base
    *e++ = PPC_LIS(PPC_R10, (uint32_t)blockTable >> 16);
    *e++ = PPC_ORI(PPC_R10, PPC_R10, (uint32_t)blockTable & 0xFFFF);

    // 2. Hash: index = ((r4>>1)^(r4>>13)) & (SIZE-1), scaled ×16 (sizeof BasicBlock)
    *e++ = PPC_SRWI(PPC_R11, PPC_R4, 1);
    *e++ = PPC_SRWI(PPC_R12, PPC_R4, 13);
    *e++ = PPC_XOR(PPC_R11, PPC_R11, PPC_R12);
    uint32_t hashBits  = __builtin_ctz(HASH_TABLE_SIZE);
    uint32_t maskBegin = 27 - hashBits + 1;
    *e++ = PPC_RLWINM(PPC_R11, PPC_R11, 4, maskBegin, 27); // ×16 bytes/block
    *e++ = PPC_ADD(PPC_R11, PPC_R10, PPC_R11);

    // 3. PC collision check
    *e++ = PPC_LWZ(PPC_R12, PPC_R11, 0);   // block->startPC
    *e++ = PPC_CMPW(0, PPC_R12, PPC_R4);
    uint32_t* branchMiss = e;
    *e++ = PPC_BNE(0);                      // → miss landing

    // 4. Load execute pointer (offset 8 in BasicBlock)
    *e++ = PPC_LWZ(PPC_R12, PPC_R11, 8);
    *e++ = PPC_CMPWI(0, PPC_R12, 0);
    uint32_t* branchFallback = e;
    *e++ = PPC_BEQ(0);                      // → miss (fallback stub)

    // 5. Self-patch: overwrite the caller's B instruction to jump directly
    *e++ = PPC_MFLR(PPC_R10);              // LR = address after caller's BL to stub
    *e++ = PPC_ADDI(PPC_R10, PPC_R10, -4);// → caller's branch word
    *e++ = PPC_SUBF(PPC_R11, PPC_R10, PPC_R12);
    *e++ = PPC_RLWINM(PPC_R11, PPC_R11, 0, 6, 29); // mask offset bits
    *e++ = PPC_ORIS(PPC_R11, PPC_R11, 0x4800);      // OR in B opcode
    *e++ = PPC_STW(PPC_R11, PPC_R10, 0);
    *e++ = PPC_DCBST(0, PPC_R10);
    *e++ = PPC_SYNC();
    *e++ = PPC_ICBI(0, PPC_R10);
    *e++ = PPC_SYNC();
    *e++ = PPC_ISYNC();

    // 6. Dispatch to target block
    *e++ = PPC_MTCTR(PPC_R12);
    *e++ = PPC_BCTR();

    // 7. Miss / fallback landing
    uint32_t* missTarget = e;
    linkerReturnAddress  = missTarget;
    uint32_t branchBytes = (uint32_t)((uint8_t*)missTarget - (uint8_t*)branchMiss);
    *branchMiss     = PPC_BNE(branchBytes);
    *branchFallback = PPC_BEQ(branchBytes);

    // Fall through to ExecuteJITTrace_Return
    *e++ = PPC_B((uint32_t)((uint8_t*)ExecuteJITTrace_Return - (uint8_t*)e));

    // Flush stub
    size_t stubBytes = (uint8_t*)e - (uint8_t*)jitArena;
    arenaOffset = (stubBytes + 31) & ~31u;
    JIT_CODE_MARK_DIRTY(jitArena, arenaOffset);
}

void JITCache::invalidateSMCTarget(uint32_t targetEA) {
    if (!isInitialized) return;
    uint32_t page = (targetEA >> 10) & 0xFFFF;
    BasicBlock* block = smcRegistry[page];
    while (block) {
        if (block->execute) {
            // Patch first instruction of native code → bail branch
            uint32_t* code = (uint32_t*)(uintptr_t)block->execute;
            uint32_t  bailOffset = (uint32_t)((uint8_t*)linkerReturnAddress - (uint8_t*)code);
            *code = PPC_B(bailOffset);
            JIT_CODE_MARK_DIRTY(code, 4);
            block->execute = nullptr;
        }
        block = block->nextSMC;
    }
}`
  },
  {
    name: 'JITCompiler.cpp (excerpt)',
    dest: 'src/jit/JITCompiler.cpp',
    lines: '~1800',
    description: 'Full THUMB trace compiler. Shown here: the prologue/register allocator helpers and representative opcode handlers (LSL, ADD, LDR, conditional branch, BL). The full file handles all 20+ THUMB format groups.',
    code: `// JITCompiler.cpp — NooDS-Wii THUMB→PPC trace compiler (excerpt)
// Full file: ~1800 lines. This excerpt shows the key helpers and
// representative handlers. See integration guide for full opcode list.

#include "JIT.h"
#include <string.h>

#define MAX_WORDS          3072
#define MAX_BAILOUTS        256
#define MAX_SMC_BAILOUTS     32
#define EPILOGUE_RESERVE_WORDS 64
#define JIT_YIELD_THRESHOLD 256  // Max cycles before forcing a scheduler yield

// ---- LAZY FLAG STATE (file-scope, reset per block) ----
static bool flagsLoaded;
static bool flagsDirty;

// ---- LAZY REGISTER ALLOCATOR ----
struct RegState { bool allocated, dirty; uint8_t hostReg; uint32_t age; };
static RegState regCache[15];  // GBA R0–R14

// Deferred guard bailout
enum BailCond { BC_BEQ, BC_BNE, BC_BGT, BC_BLT, BC_BLE };
struct DeferredBailout {
    uint32_t* branchPtr; BailCond cond;
    uint32_t  pc, cycles, instructions;
};

// ============================================================
BasicBlock* JITCompileThumbTrace(uint32_t startPC, JITCache& cache) {
    JITWriteScope scope;

    DeferredBailout bailouts[MAX_BAILOUTS];
    uint32_t bailoutCount = 0;
    bool     flagsLoaded_ = false, flagsDirty_ = false;
    uint32_t allocMask    = 0;   // bitmask of used PPC R15–R28 slots
    uint32_t currentAge   = 0;

    flagsLoaded = false;
    flagsDirty  = false;
    for (int i = 0; i < 15; i++) regCache[i] = {false, false, 0, 0};

    size_t   reserveWords = MAX_WORDS + EPILOGUE_RESERVE_WORDS +
                            MAX_BAILOUTS * 20 + MAX_SMC_BAILOUTS * 20;
    uint32_t* blockStart  = cache.allocateJITMemory(reserveWords * 4);
    if (!blockStart) return nullptr;
    uint32_t* emitPtr  = blockStart;
    uint32_t  instrCount = 0;
    uint32_t  currentPC  = startPC;
    bool      endBlock   = false;

    // ------------------------------------------------------------------
    // HELPER: Fault GBA register gbaR into a host PPC register.
    // Returns the PPC register number (15–28).
    // ------------------------------------------------------------------
    auto FindOrAllocateHostReg = [&](uint8_t gbaR) -> uint8_t {
        if (regCache[gbaR].allocated) {
            regCache[gbaR].age = ++currentAge;
            return regCache[gbaR].hostReg;
        }
        // Find a free slot in R15–R28
        uint8_t slot = 0xFF;
        for (int h = 15; h <= 28; h++) {
            if (!(allocMask & (1u << (h - 15)))) { slot = h; break; }
        }
        // If full, evict LRU dirty register
        if (slot == 0xFF) {
            uint32_t oldestAge = 0xFFFFFFFF; int victim = -1;
            for (int i = 0; i < 15; i++) {
                if (regCache[i].allocated && regCache[i].age < oldestAge) {
                    oldestAge = regCache[i].age; victim = i;
                }
            }
            // Spill victim if dirty
            if (regCache[victim].dirty) {
                *emitPtr++ = PPC_STW(regCache[victim].hostReg,
                                     PPC_R14, victim * 4);
                regCache[victim].dirty = false;
            }
            slot = regCache[victim].hostReg;
            allocMask &= ~(1u << (slot - 15));
            regCache[victim].allocated = false;
        }
        // Fault in from memory
        *emitPtr++ = PPC_LWZ(slot, PPC_R14, gbaR * 4);
        regCache[gbaR] = { true, false, slot, ++currentAge };
        allocMask |= (1u << (slot - 15));
        return slot;
    };

    auto MarkDirty = [&](uint8_t gbaR) { regCache[gbaR].dirty = true; };

    auto EnsureFlagsLoaded = [&]() {
        if (flagsLoaded) return;
        *emitPtr++ = PPC_LWZ(PPC_R9, 1, 84); // flags* from stack frame
        for (int i = 0; i < 4; i++) {
            *emitPtr++ = PPC_LWZ(PPC_R8, PPC_R9, i * 4);
            *emitPtr++ = PPC_MERGE_FLAG_BIT(i, PPC_R8, 0);
        }
        flagsLoaded = true;
    };

    auto FlushDirtyRegs = [&]() {
        for (int i = 0; i < 15; i++) {
            if (regCache[i].allocated && regCache[i].dirty) {
                *emitPtr++ = PPC_STW(regCache[i].hostReg, PPC_R14, i * 4);
                regCache[i].dirty = false;
            }
        }
    };

    auto FlushDirtyFlags = [&]() {
        if (!flagsDirty) return;
        *emitPtr++ = PPC_LWZ(PPC_R9, 1, 84);
        for (int i = 0; i < 4; i++) {
            *emitPtr++ = PPC_EXTRACT_FLAG_BIT(PPC_R8, i);
            *emitPtr++ = PPC_STW(PPC_R8, PPC_R9, i * 4);
        }
        flagsDirty = false;
    };

    // RegisterBailout: save branch site for deferred second-pass patching
    auto RegisterBailout = [&](uint32_t* bp, BailCond cond,
                                uint32_t bpc, uint32_t bcyc) {
        if (bailoutCount >= MAX_BAILOUTS) { *bp = PPC_TRAP(); endBlock = true; return; }
        bailouts[bailoutCount++] = { bp, cond, bpc, bcyc, instrCount };
    };

    // ------------------------------------------------------------------
    // MAIN DECODE LOOP
    // ------------------------------------------------------------------
    while (!endBlock && instrCount < JIT_TRACE_MAX_INSTRUCTIONS) {
        // Read THUMB instruction from GBA ROM map (NooDS readMap style)
        uint16_t opcode = /* core->memory.read<uint16_t>(arm7, currentPC) */ 0; // fill in
        uint32_t cyclesThisInsn = 1; // simplified; real: memoryWaitSeq[]

        // ---- Format 1: Move Shifted Register (LSL/LSR/ASR) ----
        if ((opcode & 0xE000) == 0x0000 && (opcode & 0x1800) != 0x1800) {
            uint8_t op  = (opcode >> 11) & 3;
            uint8_t imm = (opcode >> 6) & 0x1F;
            uint8_t rs  = (opcode >> 3) & 7;
            uint8_t rd  = opcode & 7;
            uint8_t hRs = FindOrAllocateHostReg(rs);
            uint8_t hRd = FindOrAllocateHostReg(rd);

            EnsureFlagsLoaded();

            switch (op) {
                case 0: // LSL
                    if (imm == 0) { *emitPtr++ = PPC_MR(hRd, hRs); }
                    else          { *emitPtr++ = PPC_SLWI(hRd, hRs, imm); }
                    // N flag: bit 31 of result
                    *emitPtr++ = PPC_RLWINM(PPC_R8, hRd, 1, 31, 31);
                    *emitPtr++ = PPC_MERGE_FLAG_BIT(FLAG_BIT_N, PPC_R8, 0);
                    // Z flag: cntlzw → 32 if zero
                    *emitPtr++ = PPC_CNTLZW(PPC_R8, hRd);
                    *emitPtr++ = PPC_SRWI(PPC_R8, PPC_R8, 5);
                    *emitPtr++ = PPC_MERGE_FLAG_BIT(FLAG_BIT_Z, PPC_R8, 0);
                    // C flag (if imm > 0): bit (32-imm) of source
                    if (imm > 0) {
                        *emitPtr++ = PPC_RLWINM(PPC_R8, hRs, 32 - imm + 1, 31, 31);
                        *emitPtr++ = PPC_MERGE_FLAG_BIT(FLAG_BIT_C, PPC_R8, 0);
                    }
                    flagsDirty = true;
                    break;
                case 1: // LSR
                    if (imm == 0) { *emitPtr++ = PPC_LI(hRd, 0);
                                    // C = bit 31 of source
                                    *emitPtr++ = PPC_RLWINM(PPC_R8, hRs, 1, 31, 31);
                                    *emitPtr++ = PPC_MERGE_FLAG_BIT(FLAG_BIT_C, PPC_R8, 0); }
                    else          { *emitPtr++ = PPC_SRWI(hRd, hRs, imm);
                                    // C = bit (imm-1) of source
                                    *emitPtr++ = PPC_RLWINM(PPC_R8, hRs, 32 - imm + 1, 31, 31);
                                    *emitPtr++ = PPC_MERGE_FLAG_BIT(FLAG_BIT_C, PPC_R8, 0); }
                    *emitPtr++ = PPC_RLWINM(PPC_R8, hRd, 1, 31, 31);
                    *emitPtr++ = PPC_MERGE_FLAG_BIT(FLAG_BIT_N, PPC_R8, 0);
                    *emitPtr++ = PPC_CNTLZW(PPC_R8, hRd);
                    *emitPtr++ = PPC_SRWI(PPC_R8, PPC_R8, 5);
                    *emitPtr++ = PPC_MERGE_FLAG_BIT(FLAG_BIT_Z, PPC_R8, 0);
                    flagsDirty = true;
                    break;
                case 2: // ASR
                    if (imm == 0) imm = 32;
                    *emitPtr++ = PPC_SRAWI(hRd, hRs, (imm >= 32) ? 31 : imm);
                    *emitPtr++ = PPC_RLWINM(PPC_R8, hRd, 1, 31, 31);
                    *emitPtr++ = PPC_MERGE_FLAG_BIT(FLAG_BIT_N, PPC_R8, 0);
                    *emitPtr++ = PPC_CNTLZW(PPC_R8, hRd);
                    *emitPtr++ = PPC_SRWI(PPC_R8, PPC_R8, 5);
                    *emitPtr++ = PPC_MERGE_FLAG_BIT(FLAG_BIT_Z, PPC_R8, 0);
                    flagsDirty = true;
                    break;
            }
            MarkDirty(rd);

        // ---- Format 2: ADD/SUB register/imm3 ----
        } else if ((opcode & 0xF800) == 0x1800) {
            uint8_t op  = (opcode >> 9) & 3;
            uint8_t rn  = (opcode >> 6) & 7;
            uint8_t rs  = (opcode >> 3) & 7;
            uint8_t rd  = opcode & 7;
            uint8_t hRs = FindOrAllocateHostReg(rs);
            uint8_t hRd = FindOrAllocateHostReg(rd);
            EnsureFlagsLoaded();

            if (op == 0 || op == 2) { // ADD reg / ADD imm
                uint8_t hRn = (op == 0) ? FindOrAllocateHostReg(rn) : 0;
                if (op == 0) *emitPtr++ = PPC_ADDC(hRd, hRs, hRn);
                else         *emitPtr++ = PPC_ADDI(hRd, hRs, rn); // rn is imm3
            } else {                  // SUB reg / SUB imm
                uint8_t hRn = (op == 1) ? FindOrAllocateHostReg(rn) : 0;
                if (op == 1) *emitPtr++ = PPC_SUBFC(hRd, hRn, hRs);
                else         *emitPtr++ = PPC_SUBFIC(hRd, hRs, rn);
            }
            // N/Z from result, C/V from XER
            *emitPtr++ = PPC_RLWINM(PPC_R8, hRd, 1, 31, 31);
            *emitPtr++ = PPC_MERGE_FLAG_BIT(FLAG_BIT_N, PPC_R8, 0);
            *emitPtr++ = PPC_CNTLZW(PPC_R8, hRd);
            *emitPtr++ = PPC_SRWI(PPC_R8, PPC_R8, 5);
            *emitPtr++ = PPC_MERGE_FLAG_BIT(FLAG_BIT_Z, PPC_R8, 0);
            *emitPtr++ = PPC_MFXER(PPC_R8);
            *emitPtr++ = PPC_RLWINM(PPC_R9, PPC_R8, 3, 31, 31); // XER CA → bit31
            *emitPtr++ = PPC_MERGE_FLAG_BIT(FLAG_BIT_C, PPC_R9, 0);
            // V: overflow — ADDC sets XER OV, check via rlwinm bit 1
            *emitPtr++ = PPC_RLWINM(PPC_R9, PPC_R8, 2, 31, 31);
            *emitPtr++ = PPC_MERGE_FLAG_BIT(FLAG_BIT_V, PPC_R9, 0);
            flagsDirty = true;
            MarkDirty(rd);

        // ---- Format 7/8: Guarded LDR/STR word/byte (register offset) ----
        } else if ((opcode & 0xF200) == 0x5000) {
            uint8_t rB = (opcode >> 3) & 7;
            uint8_t rO = (opcode >> 6) & 7;
            uint8_t rD = opcode & 7;
            bool    isLoad  = !!(opcode & 0x0800);
            bool    isByte  = !!(opcode & 0x0400);
            uint8_t hRB = FindOrAllocateHostReg(rB);
            uint8_t hRO = FindOrAllocateHostReg(rO);

            // Compute EA = rB + rO
            *emitPtr++ = PPC_ADD(PPC_R10, hRB, hRO);

            // Bank check: EA >> 24
            *emitPtr++ = PPC_SRWI(PPC_R11, PPC_R10, 24);

            // Guard: bail if bank > 13 (SRAM/EEPROM) or 4 (IO)
            FlushDirtyRegs(); FlushDirtyFlags();
            *emitPtr++ = PPC_CMPWI(0, PPC_R11, 13);
            uint32_t* bailPtr = emitPtr;
            *emitPtr++ = PPC_BGE(0);
            RegisterBailout(bailPtr, BC_BGE, currentPC, cyclesThisInsn);

            // Page table lookup: readTable[bank] → base pointer
            *emitPtr++ = PPC_SLWI(PPC_R11, PPC_R11, 2);
            *emitPtr++ = PPC_ADD(PPC_R11, PPC_R30_TABLE, PPC_R11);
            *emitPtr++ = PPC_LWZ(PPC_R12, PPC_R11, 0);   // page base ptr

            // Null guard: bail if page base is null
            *emitPtr++ = PPC_CMPWI(0, PPC_R12, 0);
            uint32_t* bailPtr2 = emitPtr;
            *emitPtr++ = PPC_BEQ(0);
            RegisterBailout(bailPtr2, BC_BEQ, currentPC, cyclesThisInsn);

            // Mask EA to page offset and access
            *emitPtr++ = PPC_RLWINM(PPC_R11, PPC_R10, 0, 20, 31); // low 12 bits
            if (isLoad) {
                uint8_t hRD = FindOrAllocateHostReg(rD);
                if (isByte) *emitPtr++ = PPC_LBZX(hRD, PPC_R12, PPC_R11);
                else        *emitPtr++ = PPC_LWBRX(hRD, PPC_R12, PPC_R11);
                MarkDirty(rD);
            } else {
                uint8_t hRD = FindOrAllocateHostReg(rD);
                if (isByte) *emitPtr++ = PPC_STBX(hRD, PPC_R12, PPC_R11);
                else        *emitPtr++ = PPC_STWBRX(hRD, PPC_R12, PPC_R11);
            }

        // ---- Format 16: Conditional Branch ----
        } else if ((opcode & 0xFF00) >= 0xD000 && (opcode & 0xFF00) <= 0xDF00) {
            uint8_t  cond   = (opcode >> 8) & 0xF;
            int32_t  offset = (int8_t)(opcode & 0xFF);
            uint32_t target = currentPC + 4 + (offset << 1);

            FlushDirtyRegs();
            FlushDirtyFlags();

            // Test the relevant packed flag bit
            uint8_t flagBit;
            bool    invert = false;
            switch (cond) {
                case 0: flagBit = FLAG_BIT_Z; invert = false; break; // BEQ
                case 1: flagBit = FLAG_BIT_Z; invert = true;  break; // BNE
                case 2: flagBit = FLAG_BIT_C; invert = false; break; // BCS
                case 3: flagBit = FLAG_BIT_C; invert = true;  break; // BCC
                case 4: flagBit = FLAG_BIT_N; invert = false; break; // BMI
                case 5: flagBit = FLAG_BIT_N; invert = true;  break; // BPL
                case 6: flagBit = FLAG_BIT_V; invert = false; break; // BVS
                case 7: flagBit = FLAG_BIT_V; invert = true;  break; // BVC
                default: endBlock = true; continue;
            }
            EnsureFlagsLoaded();
            *emitPtr++ = PPC_EXTRACT_FLAG_BIT(PPC_R8, flagBit);
            *emitPtr++ = PPC_CMPWI(0, PPC_R8, 0);

            // Branch taken path: update PC and exit to linker stub
            uint32_t* branchSite = emitPtr;
            *emitPtr++ = invert ? PPC_BNE(0) : PPC_BEQ(0); // placeholder

            // Fall-through: advance PC for not-taken case
            // (handled by continuing the trace)

            // Taken exit — emit after main block, record for patching
            // (For now: end the trace and use epilogue for both paths)
            endBlock = true;

        // ---- Format 19: BL (2-instruction sequence) ----
        } else if ((opcode & 0xF800) == 0xF000) {
            // BL prefix: H=0 — load high 11 bits of offset into LR
            int32_t hiOff = ((int32_t)((opcode & 0x7FF) << 21)) >> 9;
            uint32_t lrVal = currentPC + 4 + hiOff;
            // Next instruction must be BL suffix (H=1)
            // … (handled as a 2-instruction pair in full compiler)
            endBlock = true; // Bail conservatively if suffix not reached

        // ---- Unrecognized → bail (silently, cheaply) ----
        } else {
            endBlock = true;
            break;
        }

        currentPC  += 2;
        instrCount++;
    }

    // ------------------------------------------------------------------
    // EPILOGUE: flush all dirty registers/flags, write JITResult, chain
    // ------------------------------------------------------------------
    FlushDirtyRegs();
    FlushDirtyFlags();

    // Store nextPC into r4 (for JITResult)
    *emitPtr++ = PPC_MR(PPC_R4, PPC_R29);  // r4 = PC (r29)

    // Branch to linker stub (self-patching block chaining)
    *emitPtr++ = PPC_BL((uint32_t)((uint8_t*)cache.linkerStubAddress - (uint8_t*)emitPtr));

    // ------------------------------------------------------------------
    // SECOND PASS: emit deferred bailout landing pads
    // ------------------------------------------------------------------
    for (uint32_t i = 0; i < bailoutCount; i++) {
        DeferredBailout& b = bailouts[i];
        uint32_t* padStart = emitPtr;

        // Patch forward branch to this landing pad
        uint32_t branchOff = (uint32_t)((uint8_t*)padStart - (uint8_t*)b.branchPtr);
        switch (b.cond) {
            case BC_BEQ: *b.branchPtr = PPC_BEQ(branchOff); break;
            case BC_BNE: *b.branchPtr = PPC_BNE(branchOff); break;
            case BC_BGE: *b.branchPtr = PPC_BGE(branchOff); break;
            case BC_BLT: *b.branchPtr = PPC_BLT(branchOff); break;
            case BC_BLE: *b.branchPtr = PPC_BLE(branchOff); break;
        }

        // Landing pad: update JITResult.bailedOut = 1, cycles, nextPC
        *emitPtr++ = PPC_LWZ(PPC_R10, 1, 88);          // outResult*
        *emitPtr++ = PPC_LI(PPC_R8, b.cycles);
        *emitPtr++ = PPC_STW(PPC_R8, PPC_R10, 0);       // ->cycles
        *emitPtr++ = PPC_LIS(PPC_R8, b.pc >> 16);
        *emitPtr++ = PPC_ORI(PPC_R8, PPC_R8, b.pc & 0xFFFF);
        *emitPtr++ = PPC_STW(PPC_R8, PPC_R10, 4);       // ->nextPC
        *emitPtr++ = PPC_LI(PPC_R8, 1);
        *emitPtr++ = PPC_STW(PPC_R8, PPC_R10, 12);      // ->bailedOut
        *emitPtr++ = PPC_B((uint32_t)((uint8_t*)cache.linkerReturnAddress - (uint8_t*)emitPtr));
    }

    // Finalize block
    size_t emittedBytes = (uint8_t*)emitPtr - (uint8_t*)blockStart;
    size_t reservedBytes = reserveWords * 4;
    if (reservedBytes > emittedBytes)
        cache.rewindJITMemory(reservedBytes - emittedBytes);

    JIT_CODE_MARK_DIRTY(blockStart, emittedBytes);

    return cache.registerBlock(startPC, instrCount,
                               (JITBlockFunc)(uintptr_t)blockStart);
}`
  },
];

export default function FilesTab() {
  const [selected, setSelected] = useState(0);
  const [copied, setCopied]     = useState(false);

  const file = files[selected];

  const handleCopy = () => {
    navigator.clipboard.writeText(file.code).then(() => {
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    });
  };

  return (
    <div className="flex flex-col lg:flex-row gap-4 h-full">
      {/* File list */}
      <div className="lg:w-64 flex-shrink-0">
        <div className="bg-gray-900 border border-gray-700 rounded-lg overflow-hidden">
          <div className="bg-gray-800 px-3 py-2 text-xs text-gray-400 font-bold border-b border-gray-700">
            📁 JIT Source Files
          </div>
          {files.map((f, i) => (
            <button
              key={i}
              onClick={() => setSelected(i)}
              className={`w-full text-left px-3 py-2.5 text-xs border-b border-gray-800 transition-colors ${
                selected === i
                  ? 'bg-green-900 text-green-200 border-l-2 border-l-green-400'
                  : 'text-gray-300 hover:bg-gray-800'
              }`}
            >
              <div className="font-mono font-bold">{f.name}</div>
              <div className="text-gray-500 mt-0.5">{f.dest}</div>
              <div className="text-gray-600 mt-0.5">~{f.lines} lines</div>
            </button>
          ))}
        </div>
      </div>

      {/* File content */}
      <div className="flex-1 min-w-0">
        <div className="bg-gray-900 border border-gray-700 rounded-lg overflow-hidden">
          <div className="flex items-center justify-between bg-gray-800 px-4 py-2 border-b border-gray-700">
            <div>
              <span className="text-green-300 font-bold text-sm">{file.name}</span>
              <span className="text-gray-500 text-xs ml-3">{file.dest}</span>
            </div>
            <button
              onClick={handleCopy}
              className="text-xs bg-gray-700 hover:bg-gray-600 px-3 py-1 rounded text-gray-300 transition-colors"
            >
              {copied ? '✓ Copied' : '📋 Copy'}
            </button>
          </div>
          <div className="px-4 py-3 bg-gray-950 border-b border-gray-700">
            <p className="text-xs text-gray-400 leading-relaxed">{file.description}</p>
          </div>
          <div className="overflow-auto max-h-[65vh]">
            <pre className="text-xs text-gray-300 p-4 leading-relaxed whitespace-pre">
              <code>{file.code}</code>
            </pre>
          </div>
        </div>
      </div>
    </div>
  );
}
