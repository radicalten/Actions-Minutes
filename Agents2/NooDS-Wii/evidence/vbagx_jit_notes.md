# vbagx JIT Reference Notes (Commit `4d5b9984e9b7457c9146e37dace5b95fa04b73ef`)

Reference repository: `https://github.com/dborth/vbagx.git`
Verified commit SHA: `4d5b9984e9b7457c9146e37dace5b95fa04b73ef` (`evidence/vbagx_ref_commit.txt`)

## 1. Dispatch Unit and Trace Terminators

- **Scope & Dispatch Unit (`source/vba/gba/JIT.h:11-28,69,110`, `source/vba/gba/GBA-thumb.cpp:1377-1461`):**
  - The vbagx JIT is a micro-JIT solely for 16-bit GBA **THUMB** traces (`JITCompileThumbTrace(u32 startPC, JITCache& cache)` at `JIT.h:110`, `JITCompiler.cpp:104`). It does not compile 32-bit ARM instructions (`JIT.h:15-16`).
  - Maximum trace length is `JIT_TRACE_MAX_INSTRUCTIONS` = 42 Thumb instructions (`JIT.h:69`, loop bound `instrCount < JIT_TRACE_MAX_INSTRUCTIONS` at `JITCompiler.cpp:395`).
- **Trace Terminators (`source/vba/gba/JITCompiler.cpp:395-2266`):**
  1. Reaching `JIT_TRACE_MAX_INSTRUCTIONS` (42 instructions) or exceeding the scratch buffer watermark (`emitPtr - scratchBuffer > (MAX_WORDS - 64)` at `JITCompiler.cpp:398-401`).
  2. Unfetchable or unmapped guest PC page (`readTable->pages[pageBank]` null or out of range at `JITCompiler.cpp:414-421`).
  3. Conditional branch Format 16 (`B<cond>`, opcode `0xD0..0xDF`, `JITCompiler.cpp:1936-2158`) when not a forward local jump within the trace; `SWI` (`0xDF`) bails out immediately (`BAILOUT_SWI_OR_UND`).
  4. Unconditional branch Format 18 (`B label`, `JITCompiler.cpp:2161-2191`) — emits direct linker-stub branch or returns and sets `endBlock = true; blockTerminatedEarly = true`.
  5. Long branch with link Format 19 (`BL`, `JITCompiler.cpp:2194-2253`) — emits two-halfword `BL` and terminates the block (`endBlock = true; blockTerminatedEarly = true`), or bails if suffix is not `0xF800`.
  6. Format 5 `BX`/`ADD Rd, PC`/`MOV PC, Rs` (`JITCompiler.cpp:964-1089`) and `POP {..., PC}` (`JITCompiler.cpp:1878-1933`) — any write to guest R15 or mode switch ends the trace (`endBlock = true; blockTerminatedEarly = true`).
  7. Unsupported Thumb opcodes (`default:` at `JITCompiler.cpp:2255-2259` → `BAILOUT_UNSUPPORTED_OPCODE`, `endBlock = true`), or instructions whose operands/conditions fail static guards (e.g., PC-dest ALU, complex LDM/STM edge cases).

## 2. Hash Input and Collision Policy vs Arena Exhaustion

- **Hash Function (`source/vba/gba/JITCache.h:113-123`, `source/vba/gba/JITCache.cpp:113-115,192-201`):**
  - `u32 index = ((pc >> 1) ^ (pc >> 13)) & (HASH_TABLE_SIZE - 1);` where `HASH_TABLE_SIZE` is `65536` on Wii (`JITCache.h:60`).
  - Only the 32-bit guest `pc` is hashed (because vbagx only has one CPU and only JITs Thumb mode).
- **Collision Policy (`source/vba/gba/JITCache.h:116-122`, `source/vba/gba/JITCache.cpp:116-167`):**
  - Direct-mapped table (`BasicBlock blockTable[HASH_TABLE_SIZE]`, 16-byte aligned `{startPC, length, execute, nextSMC}`, `JITCache.h:78-83`).
  - On lookup (`getBlock(pc)`), if `block->startPC == pc`, it is a hit; otherwise it returns `nullptr` (miss), triggering compilation.
  - On registration (`registerBlock(pc, length, execute)`), a hash collision overwrites the bucket in place after unlinking the evicted `BasicBlock` from its `smcRegistry` linked list (`JITCache.cpp:121-147`). The evicted block's machine code remains orphaned in the bump arena until the arena wraps.

## 3. Arena Allocation and Exhaustion

- **Arena Sizing (`source/vba/gba/JITCache.h:55-61`, `Makefile.wii:53-59`):**
  - On Wii (`HW_RVL`), `JIT_ARENA_MB` defaults to 16 MiB (`JIT_ARENA_SIZE = 1024 * 1024 * JIT_ARENA_MB`), kept `<= 32 MiB` so PowerPC relative branch `b` (±32 MiB displacement) can reach across the arena (`JITCache.h:59`).
- **Bump Allocation & Exhaustion (`source/vba/gba/JITCache.cpp:89-111,170-259`):**
  - `allocateJITMemory(size_t numBytes)` rounds `numBytes` up to a 32-byte cacheline multiple (`(numBytes + 31) & ~31`).
  - If `arenaOffset + numBytes > JIT_ARENA_SIZE`, it calls `flushCache()` (`JITCache.cpp:93-95`), which resets `arenaOffset = 0`, zeroes `blockTable`, `smcRegistry`, and `smcPageFlags`, and re-emits the shared linker stub at the start of `jitArena` (`JITCache.cpp:178-257`).
  - `JITCompileThumbTrace` emits into a stack scratch buffer first (`u32 scratchBuffer[MAX_WORDS]` at `JITCompiler.cpp:106-107`) and only allocates exact emitted bytes from `cache.allocateJITMemory(actualBytes)` after compilation succeeds (`JITCompiler.cpp:2345-2361`).

## 4. Self-Modifying Code (SMC) Tracking and Invalidation

- **Tables & Granularity (`source/vba/gba/JITCache.h:15-19,61,90,99`):**
  - `SMC_MAP_SIZE = 65536` covers 64 MiB at 1 KiB page granularity (`page = (address >> 10) & 0xFFFF`).
  - `u8 smcPageFlags[SMC_MAP_SIZE]` is a fast byte array indicating whether any compiled block starts on that 1 KiB page.
  - `BasicBlock* smcRegistry[SMC_MAP_SIZE]` holds the head of a singly linked intrusive list (`BasicBlock::nextSMC`) of blocks residing on that page.
- **Registration (`source/vba/gba/JITCache.cpp:154-164`):**
  - When `registerBlock` inserts a compiled block in GBA bank 2 (EWRAM `0x02xxxxxx`) or bank 3 (IWRAM `0x03xxxxxx`), it sets `smcPageFlags[startPage] = 1` and prepends `block` to `smcRegistry[startPage]`.
- **Write Hooks (`source/vba/gba/JITCache.h:130-140`, `source/vba/gba/JITCompiler.cpp:345-391`):**
  - Interpreter/C++ writes invoke `JIT_SMC_GUARD(address, pageIdx)` which checks `if ((pageIdx == 2) | (pageIdx == 3))` and `jitCache.smcPageFlags[page]`, calling `jitCache.invalidateSMCTarget(address)`.
  - JIT-emitted store instructions emit an inline SMC page-flag check (`EmitSMCWriteCheck` at `JITCompiler.cpp:345-391`) that bails out with `result.smcHit = 1` and `result.smcAddress = writtenEA` (`GBA-thumb.cpp:1429-1433`).
- **Invalidation & Patching (`source/vba/gba/JITCache.cpp:262-321`):**
  - `invalidateSMCTarget(targetEA)` scans pages from `targetEA - (JIT_TRACE_MAX_INSTRUCTIONS + 1) * 2` to `targetEA + 36`.
  - For any overlapping block (`targetEA < blockEndPC && endEA > blockStartPC`), it overwrites the first instruction of `curr->execute` with an unconditional branch `PPC_B(branchOffset)` to `linkerReturnAddress`, flushes the 4-byte patch via `DCStoreRange(codePtr, 4); ICInvalidateRange(codePtr, 4);` (so any already-chained caller safely exits), sets `curr->execute = nullptr`, and unlinks `curr` from `smcRegistry[page]`.

## 5. PowerPC GPR Map, Allocation, Spill/Reload, and Fixed Registers

- **Fixed & Reserved Host Registers (`source/vba/gba/JITPPCEmitter.h:48-107`, `source/vba/gba/JITTrampoline.S:53-70`):**
  - `r0`: Hardware zero in D-form base addressing (`RA=0`) and LR scratch in prologue/epilogue; never used as a general scratch base register (`JITPPCEmitter.h:51`).
  - `r1`: Host stack pointer (`SP`); holds the 128-byte trampoline frame (`JITPPCEmitter.h:52`, `JITTrampoline.S:79`).
  - `r2`, `r13`: ABI system reserved (`SDA2` / `SDA`), never touched (`JITPPCEmitter.h:53-54`).
  - `r3`: Accumulates guest elapsed cycles during trace execution (`JITPPCEmitter.h:57`, `JITTrampoline.S:101,116`).
  - `r4`: Holds `nextPC` on trace exit and acts as local scratch (`JITPPCEmitter.h:58`, `JITTrampoline.S:117`).
  - `r5`: Holds live `busPrefetchCount` across the trace (`JITPPCEmitter.h:59`, `JITTrampoline.S:97,112`).
  - `r6`: `PPC_REG_FLAGS` — holds packed N/Z/C/V flags (`JITPPCEmitter.h:60-64`).
  - `r7`–`r12`, `r31`: Volatile/instruction-level scratch registers (`JITPPCEmitter.h:65-70,106`).
  - `r14`: Fixed base pointer to GBA `reg[0..15]` array (`PPC_REG_GBA_REGS_PTR`, `JITTrampoline.S:89`).
  - `r29`: Eagerly loaded/flushed guest `R15` (PC) (`PPC_REG_PC`, `JITTrampoline.S:90,121`).
  - `r30`: Fixed base pointer to `gbaReadTable` (`PPC_R30_TABLE`, `JITTrampoline.S:93`).
- **Lazy LRU Guest Register Allocation (`source/vba/gba/JITCompiler.cpp:249-342`):**
  - Guest `R0`–`R14` are lazily mapped on demand into host non-volatile registers `r15`–`r28` (14 host slots for 15 guest registers, tracked via `RegState regCache[15]` and `allocatedPoolMask`).
  - `FindOrAllocateHostReg` (`JITCompiler.cpp:249-305`) allocates a free host register from `r15..r28`, or if all 14 are occupied, evicts the least-recently-used register (`lastUsed` timestamp) that is not in `lockedMask`, emitting `PPC_STW(victimHostReg, 14, victimGba * 4)` if dirty, and loading the new guest register via `PPC_LWZ(chosenHostReg, 14, gbaReg * 4)` when `loadValue` is true.
  - `FlushDirtyRegisters` / `EmitEagerStateFlush` (`JITCompiler.cpp:320-333`) writes all dirty allocated guest registers back to `0..56(r14)` before exits or guard checks.

## 6. NZCV Flag Computation and Laziness

- **Packed Flag Representation (`source/vba/gba/JITPPCEmitter.h:60-64,109-145`, `source/vba/gba/JITCompiler.cpp:153-246`):**
  - All 4 condition flags (`N`, `Z`, `C`, `V`) are packed into bits 0..3 (IBM MSB bit numbering, i.e., bits 31..28) of `r6` (`PPC_REG_FLAGS`) using `rlwimi` (`PPC_MERGE_FLAG_BIT`) and extracted as 0/1 using `rlwinm` (`PPC_EXTRACT_FLAG_BIT`).
  - Flags are **lazily loaded** into `r6` on the first instruction in a block that reads or writes any flag (`EnsureFlagsLoaded` at `JITCompiler.cpp:158-166` loads the `flags` pointer from `84(r1)` and merges the 4 discrete flag words into `r6`).
  - Flag values themselves are **eagerly computed** whenever a Thumb instruction defines them (not deferred as lazy operands):
    - `N` and `Z`: `EmitNZFlags(sourceReg)` (`JITCompiler.cpp:236-240`) merges the sign bit (`sh=1`) into `FLAG_N` and uses `cntlzw` (`PPC_CNTLZW(PPC_R8, sourceReg)`) shifted by `sh=27` to merge `(sourceReg == 0)` into `FLAG_Z`.
    - `C` and `V`: `EmitCVFlagsFromXER(scratchReg)` (`JITCompiler.cpp:242-246`) executes `mfxer scratchReg` right after `addco.`/`subfco.`/`addc`/`adde` and merges `XER[CA]` (`sh=3`) and `XER[OV]` (`sh=2`) into `r6`.
  - Dirty flags (`flagsDirty`) are unpacked back to the 4 memory words at `84(r1)` via `FlushDirtyFlags` / `EmitDirtyFlagFlush` (`JITCompiler.cpp:206-228`) before block exit or bailout.

## 7. Direct Block Linking and Post-Patch Coherency

- **Linker Stub (`source/vba/gba/JITCache.cpp:184-257`):**
  - At the beginning of `jitArena`, `flushCache()` emits a shared native lookup/linker routine at `linkerStubAddress` (with miss/exit target `linkerReturnAddress`).
  - A compiled block ending with a static branch loads the target guest PC into `r4` and calls `bl linkerStubAddress` (setting `LR` to `branchInsn + 4`).
  - The linker stub hashes `r4` (`srwi`/`xor`/`rlwinm`), indexes `blockTable`, checks `block->startPC == r4` (`cmpw` + `bne missTarget`), loads `block->execute` (`lwz r12, 8(r11)`), and checks `r12 != nullptr` (`cmpwi` + `beq missTarget`).
- **Self-Patching & Coherency (`source/vba/gba/JITCache.cpp:218-236`):**
  - On a hit, the stub reads the caller's branch address via `mflr r10; addi r10, r10, -4`, computes the byte displacement `subf r11, r10, r12`, masks it into a PowerPC relative branch instruction (`rlwinm r11, r11, 0, 6, 29; oris r11, r11, 0x4800`), and overwrites the caller's `bl` instruction (`stw r11, 0(r10)`).
  - Immediately after storing the patched branch word, it executes the 5-instruction Broadway cache coherency sequence on `r10`:
    `dcbst 0, r10; sync; icbi 0, r10; sync; isync` (`JITCache.cpp:227-231`), then jumps to the target block via `mtctr r12; bctr`.

## 8. Trampoline Stack Frame, LR, and Non-Volatile Saves

- **Entry (`source/vba/gba/JITTrampoline.S:73-103`):**
  - `ExecuteJITTrace` saves the caller's Link Register in the caller frame (`mflr 0; stw 0, 4(1)`), allocates a 128-byte aligned stack frame (`stwu 1, -128(1)`), and saves all 18 PowerPC non-volatile GPRs `r14`–`r31` at offsets `8(r1)..76(r1)` (`stmw 14, 8(1)`).
  - It spills incoming pointer arguments (`gbaRegs` `r6` → `80(r1)`, `flags` `r7` → `84(r1)`, `outResult` `r4` → `88(r1)`, `busPrefetchCount` `r5` → `92(r1)`), initializes `r14 = gbaRegs`, `r29 = gbaRegs[15]`, `r30 = readTable`, `r5 = *busPrefetchCount`, zeroes cycle counter `r3 = 0`, and enters the trace via `mtctr 3; bctr`.
- **Exit (`source/vba/gba/JITTrampoline.S:107-131`):**
  - `ExecuteJITTrace_Return` stores `r5` back to `*busPrefetchCount`, writes `r3` (`cycles`) and `r4` (`nextPC`) into `outResult` (`0(r10)` and `4(r10)`), stores `r29` back to `gbaRegs[15]` (`60(r10)`), restores `r14`–`r31` (`lmw 14, 8(1)`), restores `LR` (`lwz 0, 132(1); mtlr 0`), deallocates the frame (`addi 1, 1, 128`), and returns (`blr`).

## 9. Unsupported-Instruction Handoff and Cycle/Resume-PC Reporting

- **Compile-Time Unsupported First Instruction (`source/vba/gba/JITCompiler.cpp:2268-2272`, `source/vba/gba/GBA-thumb.cpp:1396,1463-1498`):**
  - If the very first instruction at `startPC` is unsupported (`instrCount == 0`), `JITCompileThumbTrace` records a sentinel block `cache.registerBlock(startPC, 0, nullptr)` without touching the code arena. The dispatch loop sees `block->execute == nullptr` and falls straight through to the C++ single-instruction interpreter (`GBA-thumb.cpp:1463+`).
- **Mid-Trace Unsupported Instruction or Guard Failure (`source/vba/gba/JITCompiler.cpp:2255-2259,2280-2339`, `source/vba/gba/GBA-thumb.cpp:1419-1452`):**
  - If a trace stops at an unsupported instruction after compiling `instrCount > 0` instructions, the normal epilogue flushes dirty registers/flags, sets `r3` to accumulated cycles, sets `r4` to `currentPC` (the unsupported instruction's address), writes `JITResult` metadata (`instructions = instrCount`, `bailedOut = 0`), and branches to `linkerReturnAddress` → `ExecuteJITTrace_Return`.
  - If a runtime guard (memory bank/page miss, alignment, or SMC hit) triggers mid-trace, it branches to a deferred bailout stub (`JITCompiler.cpp:2283-2307`) where dirty state was already flushed before the guard; the stub adds cycles up to the bailing instruction (`PPC_ADDI(PPC_R3, PPC_R3, bailouts[i].cycles)`), records `bailedOut = 1` and `instructions = bailouts[i].instructions`, loads the bailing instruction's PC into `r4`, and jumps to `linkerReturnAddress`.
  - Back in `GBA-thumb.cpp:1419-1452`, `cpuTotalTicks += result.cycles` is added, the 2-stage Thumb prefetch pipeline is re-primed at `armNextPC = result.nextPC` (`reg[15].I = armNextPC + 2; cpuPrefetch[0] = CPUReadHalfWord(armNextPC); cpuPrefetch[1] = CPUReadHalfWord(armNextPC + 2);`), and if `result.bailedOut` is set, `useJIT = false` forces the C++ interpreter to execute the bailing instruction on the very next pass.

## 10. Code Flush and I-Cache Maintenance

- **Macro & API (`source/vba/gba/JIT.h:62,78-79`):**
  - On Wii (`<ogc/cache.h>`), `JIT_CODE_MARK_DIRTY(p, n)` calls `DCStoreRange((void*)(p), (n)); ICInvalidateRange((void*)(p), (n));`.
- **All Three Flush Sites:**
  1. **Trace Compilation (`source/vba/gba/JITCompiler.cpp:2356-2358`):** After `memcpy(codeBlock, scratchBuffer, actualBytes)` into `jitArena`, calls `JIT_CODE_MARK_DIRTY(codeBlock, actualBytes)` (`DCStoreRange` + `ICInvalidateRange`).
  2. **Cache Flush / Linker Stub Emission (`source/vba/gba/JITCache.cpp:253-256`):** After writing the linker stub at the start of `jitArena`, calls `DCStoreRange((void*)jitArena, stubSize); ICInvalidateRange((void*)jitArena, stubSize);`.
  3. **SMC Entry Patch & Dynamic Branch Linking (`source/vba/gba/JITCache.cpp:227-231,294-295`):**
     - `invalidateSMCTarget` patches 4 bytes at `*codePtr` and calls `DCStoreRange(codePtr, 4); ICInvalidateRange(codePtr, 4);`.
     - The native linker stub patches the caller's `bl` instruction in-situ and executes inline `dcbst 0, r10; sync; icbi 0, r10; sync; isync`.
