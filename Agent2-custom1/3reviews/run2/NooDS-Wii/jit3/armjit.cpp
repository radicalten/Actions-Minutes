/*
    v3-NooDS-Wii: jit3 front-end implementation (slice 1: ARM integer core).

    Design rules (see ../../COMPARISON.md §3):
      R1  one PC convention.  Between instructions registersUsr[15] == nextPc + step
          (step = 4 ARM / 2 Thumb), which is exactly what the NooDS pipeline keeps:
          runOpcode increments *registers[15] before dispatch, and flushPipeline()
          sets *registers[15] = (target & ~3) + 4 on ARM / (target & ~1) + 2 on Thumb.
          The interpreter is only entered at an instruction boundary.
      R2  exact parity with the NooDS handlers: same cycles, same flags, same halt
          behaviour.
      R4  guest r0..r12/sp and the CPSR are resident in PPC registers; only the
          flags a later instruction can observe are materialised (liveness).
      R5  page-mapped memory is accessed inline; the helper is the slow path.
      R6  prove-or-delegate: an instruction is translated completely or handed to
          the interpreter - never partially.

    Slice 1 scope: ARM data processing (incl. S-bit flags), B/BL/BX/BLX(reg),
    MRS/MSR (CPSR), MUL/MLA on ARM9.  Loads/stores, LDM/STM, SWP and the whole
    Thumb ISA are delegated; they arrive in slice 2 on this same machinery.

    Each quirk cites the NooDS handler it mirrors; tools/host re-derives the same
    semantics from those handlers and diffs them against the emitted code.
*/

#include "armjit.hpp"
#include "ppc.hpp"

namespace Jit3 {

using namespace Ppc;

// ---------------------------------------------------------------- registers
static constexpr int RR(int guest) { return 14 + guest; }   // guest r0..r13 -> PPC r14..r27
static constexpr int RCPSR = 28;      // whole CPSR (flags, T, mode, ...)
static constexpr int RDEAD = 29;      // cycle deadline, supplied by the glue
static constexpr int RCYC  = 30;      // accumulated guest cycles (returned in r3)
static constexpr int RCPU  = 31;      // Interpreter* anchor
static constexpr int T0 = 3, T1 = 4, T2 = 5, T3 = 6, T4 = 7, T5 = 8, T6 = 9, T7 = 10;
static constexpr int TCALL = 12;

static constexpr int FRAME = 96, SAVE_OFF = 8, LR_OFF = FRAME + 4;
static constexpr int OFF_R15 = 15 * 4;

static constexpr uint32_t M_REG(int g) { return 1u << g; }
static constexpr uint32_t M_CPSR = 1u << 14;
static constexpr uint32_t M_LR   = 1u << 15;

enum { F_N = 1, F_Z = 2, F_C = 4, F_V = 8, F_ALL = 0xF };
enum { A_N = 31, A_Z = 30, A_C = 29, A_V = 28 };            // CPSR flag bit numbers

static inline uint32_t step(bool thumb) { return thumb ? 2u : 4u; }
static inline uint32_t rorC(uint32_t v, uint32_t r) { r &= 31; return r ? ((v >> r) | (v << (32 - r))) : v; }
static inline uint32_t immValue(uint32_t op) { return rorC(op & 0xFF, ((op >> 8) & 0xF) * 2); }

// ---------------------------------------------------------------- emitter
struct Em {
    const Cfg* cfg = nullptr;
    bool chain = false;        // this block may end by jumping into its successor
    uint32_t chainGen = 0;     // generation the chain probe compares against
    const GuestMem* gm = nullptr;
    bool thumb = false, arm7 = false;
    uint32_t* base = nullptr;
    uint32_t* p = nullptr;
    uint32_t* end = nullptr;
    uint32_t pc = 0;
    uint32_t cyc = 0;
    uint32_t mod = 0;
    bool overflow = false;
    bool stop = false;
    BlockInfo info;
    int carryBit = -1;            // index (in carrySrc) of the shifter carry bit, -1 = unchanged
    int carrySrc = -1;

    void w(uint32_t x) { if (p < end) *p++ = x; else overflow = true; }
    uint32_t idx() const { return (uint32_t)(p - base); }
    void patchB(uint32_t at) { base[at] = b((int32_t)((idx() - at) * 4)); }
    void patchBc(uint32_t at, uint8_t bo, uint8_t bi) {
        base[at] = bc(bo, bi, (int16_t)((idx() - at) * 4));
    }
    void li(int r, uint32_t v) {
        if ((int32_t)v >= -32768 && (int32_t)v <= 32767) { w(Ppc::li(r, (int16_t)v)); return; }
        if ((v & 0xFFFFu) == 0) { w(Ppc::lis(r, (int16_t)(v >> 16))); return; }
        w(Ppc::lis(r, (int16_t)(v >> 16)));
        w(Ppc::ori(r, r, (uint16_t)v));
    }
    void addiC(int r, int32_t imm) { if (imm) w(addi(r, r, (int16_t)imm)); }
    void cycle(int n) { addiC(RCYC, n); cyc += (uint32_t)n; }
    // Copy source bit `srcIdx` into destination bit `dstPPCbit`.  All bit
    // numbers here are PPC/IBM bit numbers (bit 0 = MSB of the 32-bit register),
    // which is the numbering rlwinm/rlwimi use.
    void insertBit(int dst, int src, int dstPPCbit, int srcIdx) {
        w(rlwimi(dst, src, (uint8_t)((srcIdx - dstPPCbit) & 31), (uint8_t)dstPPCbit, (uint8_t)dstPPCbit));
    }
    void markCpsr() { mod |= M_CPSR; }
    // CPSR bit `armBit` = constant 0/1 (used for the constant-immediate carry)
    void setCpsrBitConst(int armBit, int value) {
        const int ibm = 31 - armBit;                     // PPC/IBM bit number of this CPSR bit
        w(rlwinm(RCPSR, RCPSR, 0, (uint8_t)((ibm + 1) & 31), (uint8_t)((ibm + 31) & 31)));   // clear it
        if (!value) return;
        if (ibm < 16) w(ori(RCPSR, RCPSR, (uint16_t)(1u << ibm)));
        else          w(oris(RCPSR, RCPSR, (uint16_t)(1u << (ibm - 16))));
    }

    int getReg(int g, int scratch) {
        if (g <= 13) return RR(g);
        if (g == 14) {                                        // LR lives in memory (both banks)
            w(lwz(T0, RCPU, (int16_t)(cfg->offRegsPtr + 4 * 14)));
            w(lwz(scratch, T0, 0));
            return scratch;
        }
        li(scratch, pc + (thumb ? 4u : 8u));                  // NooDS: *registers[15] during the handler
        return scratch;
    }
    void setReg(int g, int src) {
        if (g <= 13) { if (src != RR(g)) w(mr(RR(g), src)); mod |= M_REG(g); return; }
        if (g == 14) {
            w(lwz(T0, RCPU, (int16_t)(cfg->offRegsPtr + 4 * 14)));
            w(stw(src, T0, 0));
            mod |= M_LR;
        }
    }
    void callHelper(HelperId id) {
        li(TCALL, (uint32_t)cfg->helper[id]);
        w(mtctr(TCALL));
        w(bctrl());
    }

    void prologue() {
        w(stwu(1, 1, -FRAME));
        w(mflr(0));
        w(stw(0, 1, LR_OFF));
        w(stmw(14, 1, SAVE_OFF));
        w(mr(RCPU, 3));
        w(mr(RDEAD, 4));
        w(Ppc::li(RCYC, 0));
        for (int g = 0; g <= 7; g++) w(lwz(RR(g), RCPU, (int16_t)(cfg->offRegsUsr + 4 * g)));
        for (int g = 8; g <= 13; g++) {
            w(lwz(T0, RCPU, (int16_t)(cfg->offRegsPtr + 4 * g)));
            w(lwz(RR(g), T0, 0));
        }
        w(lwz(RCPSR, RCPU, (int16_t)cfg->offCpsr));
        info.warmOffset = (int32_t)idx();                     // entry with all state already resident
    }
    void storeModified() {
        for (int g = 0; g <= 7; g++)
            if (mod & M_REG(g)) w(stw(RR(g), RCPU, (int16_t)(cfg->offRegsUsr + 4 * g)));
        for (int g = 8; g <= 13; g++)
            if (mod & M_REG(g)) {
                w(lwz(T0, RCPU, (int16_t)(cfg->offRegsPtr + 4 * g)));
                w(stw(RR(g), T0, 0));
            }
        if (mod & M_CPSR) w(stw(RCPSR, RCPU, (int16_t)cfg->offCpsr));
    }
    // Every guest register and the CPSR, unconditionally.  A chained exit has to
    // use this instead of storeModified(): the predecessor's dirty registers were
    // *deliberately* left in the registers when it jumped straight into this block,
    // so a partial write-back here would lose them.  One full write-back per chain
    // instead of one per block is the whole point of the exercise.
    void storeAll() {
        for (int g = 0; g <= 7; g++) w(stw(RR(g), RCPU, (int16_t)(cfg->offRegsUsr + 4 * g)));
        for (int g = 8; g <= 13; g++) {
            w(lwz(T0, RCPU, (int16_t)(cfg->offRegsPtr + 4 * g)));
            w(stw(RR(g), T0, 0));
        }
        w(stw(RCPSR, RCPU, (int16_t)cfg->offCpsr));
    }
    void exitStore() { if (chain) storeAll(); else storeModified(); }

    void epilogue() {
        w(mr(3, RCYC));
        w(lmw(14, 1, SAVE_OFF));
        w(lwz(0, 1, LR_OFF));
        w(mtlr(0));
        w(addi(1, 1, FRAME));
        w(blr());
        stop = true;
    }
    // ---- block chaining ---------------------------------------------------
    // A block's exit normally returns to the C++ driver, which looks the successor up
    // and calls it - prologue, epilogue and a table probe per *block*.  Measured on
    // this ROM, that dominates: blocks average ~2 instructions, so most of the time
    // went into the boundary rather than the body.  Here the exit instead probes the
    // driver's own block table and jumps straight to the successor's warm entry.  A
    // miss - not compiled yet, stale generation, or the chain has run long enough -
    // falls through into the ordinary exit below, so the driver stays the safety net.
    //
    // What makes it sound: the probe compares the full PC and the memory generation
    // the code was compiled in (a remap bumps it, so stale probes miss), and the
    // cycle budget keeps the driver in the loop often enough to service interrupts
    // and notice a halt.  The successor's warm entry expects every guest register
    // already resident, which is exactly the state the predecessor leaves behind.
    uint32_t pendMiss[8];
    int npendMiss = 0;
    bool chainEmit(uint32_t constTarget, int regTarget) {
        if (!chain || !cfg->chainTable) return false;
        const uint32_t mask = cfg->chainMask;
        int bits = 0;
        while (((1u << bits) & mask) != 0 && bits < 24) bits++;      // mask is 2^bits - 1
        if (regTarget >= 0) w(mr(T4, regTarget)); else li(T4, constTarget);
        w(srwi(T5, T4, 2));
        w(srwi(T6, T4, 14));
        w(xor_(T5, T5, T6));                                 // same mix as the driver's slotFor()
        w(rlwinm(T5, T5, 0, (uint8_t)(32 - bits), 31));
        w(mulli(T5, T5, (int16_t)cfg->chainStride));
        li(T7, (uint32_t)cfg->chainTable);
        w(add(T7, T7, T5));
        w(lwz(T5, T7, 0));                                   // entry.pc
        w(cmpw(0, T5, T4));
        pendMiss[npendMiss++] = (uint32_t)idx();
        w(bne(0, 0));
        w(lwz(T5, T7, 4));                                   // entry.gen
        li(T6, chainGen);
        w(cmpw(0, T5, T6));
        pendMiss[npendMiss++] = (uint32_t)idx();
        w(bne(0, 0));
        if (cfg->chainBudget > 0) {                          // RCYC < budget, else hand back
            w(cmplwi(0, RCYC, (uint16_t)cfg->chainBudget));
            pendMiss[npendMiss++] = (uint32_t)idx();
            w(bge(0, 0));
        }
        w(lwz(T6, T7, 8));                                   // warm entry, state resident
        w(mr(3, RCPU));                                      // block ABI: r3 = Interpreter*
        if (cfg->chainCounter) {                             // instrumentation, same shape
            li(T7, (uint32_t)(uintptr_t)cfg->chainCounter);
            w(lwz(T5, T7, 0));
            w(addi(T5, T5, 1));
            w(stw(T5, T7, 0));
        }
        w(mtctr(T6));
        w(bctr());
        // closeSkip() re-encodes each pending branch with its *own* condition (it
        // reads the opcode back), which matters because the branches do not share
        // one: the pc/gen tests branch on "not equal" and the budget test on "not
        // less than".
        for (int i = 0; i < npendMiss; i++) closeSkip(pendMiss[i]);
        npendMiss = 0;
        return true;
    }

    void exitPcConst(uint32_t exitPc) {
        chainEmit(exitPc, -1);
        li(T0, exitPc + step(thumb));
        w(stw(T0, RCPU, (int16_t)(cfg->offRegsUsr + OFF_R15)));
        li(4, exitPc);
        exitStore();
        epilogue();
    }
    void exitPcReg(int pcReg) {                               // pcReg: target, low bits already aligned
        chainEmit(0, pcReg);
        w(addi(T0, pcReg, (int16_t)step(thumb)));
        w(stw(T0, RCPU, (int16_t)(cfg->offRegsUsr + OFF_R15)));
        w(mr(4, pcReg));
        exitStore();
        epilogue();
    }
    // Exit to a register target whose *mode* may change (BX/BLX): step and
    // alignment mask follow the new T bit, exactly like flushPipeline().
    //
    // Two things this used to get wrong, both caught by VERIFY on the Thumb
    // scratch programs and both invisible to the fast path (the driver rewrites
    // registersUsr[15] from the returned exit PC, so only the verifier's register
    // comparison sees what the block actually left there):
    //   * the step lived in T1 = r4, and `mr(4, T4)` below overwrites r4 with the
    //     exit PC - so `add(T0, T4, T1)` added the target to itself, leaving
    //     registersUsr[15] = 2 * target.  The step is in T2 = r5 now.
    //   * the step was built as (T + 1) << 1, which is 4 in Thumb and 2 in ARM -
    //     inverted.  It is (~T & 1) + 1) << 1: 2 in Thumb, 4 in ARM.  (rlwinm's
    //     rotate is numeric, so moving CPSR bit 5 down to bit 0 is a shift of 27 -
    //     the extraction itself was right.)
    // Both mattered for a BX to a 2-aligned Thumb address; ARM's own bx was wrong
    // in the register it left behind in the same way.
    void exitPcRegDyn(int targetReg) {
        w(rlwinm(T2, RCPSR, 27, 31, 31));                     // CPSR.T -> bit 0
        w(xori(T2, T2, 1));                                   // 0 in Thumb, 1 in ARM
        w(addi(T2, T2, 1));                                   // 1 / 2
        w(slwi(T2, T2, 1));                                   // step = 2 / 4
        w(addi(T3, T2, -1));                                  // step - 1
        w(nor_(T3, T3, T3));                                  // ~1 / ~3
        w(and_(T4, targetReg, T3));                           // aligned next PC (T4: probe-proof)
        chainEmit(0, T4);                                     // chain on the aligned target
        w(mr(4, T4));
        w(add(T0, T4, T2));                                   // registersUsr[15] = nextPc + step
        w(stw(T0, RCPU, (int16_t)(cfg->offRegsUsr + OFF_R15)));
        exitStore();
        epilogue();
    }
    // Instruction the emitter does not model (loads, stores, LDM/STM, coprocessor):
    // run it with the interpreter's own handler *inside* the block.  The guest
    // state is written back, the handler runs with the pipeline built for this
    // instruction, and everything is reloaded afterwards, so the block carries on
    // with the next instruction instead of ending here.
    //
    // This is the difference between one dispatch per memory access and one
    // dispatch per block: with every load ending a block, blocks averaged two
    // instructions and the dispatch, the negative-cache probe and flushPipeline()
    // were paid per instruction - which is why the recompiler measured 1.4x
    // *slower* than the plain interpreter.
    //
    // The condition is deliberately not tested here: runOpcode() evaluates it (and
    // charges its own 1 cycle when false), so the handler's return value is the
    // instruction's exact cycle cost on every path.
    void delegateInline(uint32_t instrPc) {
        for (int g = 0; g <= 13; g++) {
            if (g <= 7) w(stw(RR(g), RCPU, (int16_t)(cfg->offRegsUsr + 4 * g)));
            else { w(lwz(T0, RCPU, (int16_t)(cfg->offRegsPtr + 4 * g))); w(stw(RR(g), T0, 0)); }
        }
        w(stw(RCPSR, RCPU, (int16_t)cfg->offCpsr));
        w(mr(3, RCPU));
        li(4, instrPc);
        callHelper(H_InterpOne);              // leaves the instruction's cycles in r3
        w(add(RCYC, RCYC, 3));
        // The handler may have changed any register, the flags, or the bank the
        // pointers array points at, so reload everything it could have touched.
        for (int g = 0; g <= 7; g++) w(lwz(RR(g), RCPU, (int16_t)(cfg->offRegsUsr + 4 * g)));
        for (int g = 8; g <= 13; g++) {
            w(lwz(T0, RCPU, (int16_t)(cfg->offRegsPtr + 4 * g)));
            w(lwz(RR(g), T0, 0));
        }
        w(lwz(RCPSR, RCPU, (int16_t)cfg->offCpsr));
        mod = 0;                              // freshly loaded: nothing to write back
    }
    // Same, for the instructions that can move the PC (load into r15, LDM with
    // r15, SVC, coprocessor): the glue takes the new PC from the CPU, as
    // exitDelegate() does.
    void exitDelegateNow(uint32_t instrPc) {
        delegateInline(instrPc);
        w(lis(4, -1));                        // 0xFFFF0000: "PC is in the CPU"
        storeModified();
        epilogue();
    }

    // One instruction handed to the interpreter; r4 = 0xFFFF0000 tells the glue to
    // take the new PC from the CPU (the instruction may have changed it).
    void exitDelegate(uint32_t instrPc) {
        for (int g = 0; g <= 13; g++) {
            if (g <= 7) w(stw(RR(g), RCPU, (int16_t)(cfg->offRegsUsr + 4 * g)));
            else { w(lwz(T0, RCPU, (int16_t)(cfg->offRegsPtr + 4 * g))); w(stw(RR(g), T0, 0)); }
        }
        w(stw(RCPSR, RCPU, (int16_t)cfg->offCpsr));
        w(mr(3, RCPU));
        li(4, instrPc);
        callHelper(H_InterpOne);
        w(add(RCYC, RCYC, 3));
        w(lis(4, -1));                                        // 0xFFFF0000
        epilogue();
    }

    // ---- conditional execution ---------------------------------------
    // CR0 = (LT,GT,EQ,SO) from the low nibble of the CPSR (ARM V,N,Z,C order),
    // then a single CR bit branch tells the caller whether to skip.
    uint32_t emitCondTest(uint32_t cond) {
        w(mtcrf(0x80, RCPSR));
        // CR0 is now (LT,GT,EQ,SO) <- CPSR (N,Z,C,V).  The caller jumps *over* the
        // instruction body, so the branch has to be taken when the ARM condition is
        // FALSE.  bc's BO=4 branches on "CR bit clear", BO=12 on "CR bit set", so for
        // conditions 0..7 the raw flag bit needs no extra work - only the polarity of
        // the test changes (cond odd = the negated form).
        if (cond < 8) {
            int bit;                                   // CR0 bit holding the raw flag
            switch (cond) {
                default: bit = 1; break;               // EQ / NE -> Z
                case 2: case 3: bit = 2; break;        // CS / CC -> C
                case 4: case 5: bit = 0; break;        // MI / PL -> N
                case 6: case 7: bit = 3; break;        // VS / VC -> V
            }
            uint32_t at = idx();
            w(bc((cond & 1) ? 12 : 4, crbit(0, bit), 0));
            return at;
        }
        // 8..13 have no single flag: build the condition's *result* in CR bit 4
        // (CR1.LT) with CR logical ops, then skip when it came out clear.
        switch (cond) {
            case 8:  w(crandc(4, 2, 1)); break;                    // HI: C && !Z
            case 9:  w(crorc(4, 1, 2));  break;                    // LS: Z || !C
            case 10: w(creqv(4, 0, 3));  break;                    // GE: N == V
            case 11: w(crxor(4, 0, 3));  break;                    // LT: N != V
            case 12: w(creqv(4, 0, 3)); w(crandc(4, 4, 1)); break; // GT: (N == V) && !Z
            default: w(crxor(4, 0, 3)); w(cror(4, 4, 1));  break;  // LE: (N != V) || Z
        }
        uint32_t at = idx();
        w(bc(4, crbit(0, 4), 0));
        return at;
    }
    void closeSkip(uint32_t at) {
        if (at == UINT32_MAX) return;
        patchBc(at, (uint8_t)((base[at] >> 21) & 31), (uint8_t)((base[at] >> 16) & 31));
    }

    // ---- flags -------------------------------------------------------
    void setNZ(int res, uint8_t mask) {
        if (mask & F_N) insertBit(RCPSR, res, 0, 0);          // CPSR.N <- res MSB (IBM bit 0)
        // cntlzw(res) == 32 exactly when res == 0, and 32's only set bit is value
        // bit 5 -> PPC/IBM bit 26 (bit numbers here are MSB-first)
        if (mask & F_Z) { w(cntlzw(T1, res)); insertBit(RCPSR, T1, 1, 26); }
    }
    void setCFromXer(uint8_t mask) {
        if (mask & F_C) { w(mfxer(T1)); insertBit(RCPSR, T1, 2, 2); }   // XER.CA (IBM bit 2) -> CPSR.C
    }
    // (NooDS's rscs handler used to merge its flags rather than replace them - it
    // masked with ~0xC0000000 and OR-ed the computed C and V in - so the emitter
    // mirrored that with an OR-ed carry and an OR-ed overflow.  The mGBA suite's
    // carry table disagrees: RSCS replaces all four flags.  Both sides were fixed
    // and the two OR helpers removed.  Same shape of bug as the multiply C flag -
    // an interpreter quirk the emitter was faithfully copying.)
    void setVAdd(int res, int a, int b) {                     // V = (a^res) & (b^res) sign
        w(xor_(T2, res, b)); w(eqv_(T3, a, b)); w(and_(T2, T2, T3)); w(srwi(T2, T2, 31));
        insertBit(RCPSR, T2, 3, 31);
    }
    void setVSub(int res, int x, int y) {                     // V = (x^y) & ~(res^y) sign, x - y
        w(xor_(T2, x, y)); w(eqv_(T3, res, y)); w(and_(T2, T2, T3)); w(srwi(T2, T2, 31));
        insertBit(RCPSR, T2, 3, 31);
    }
    void setCFromBit(int src, int bitIdx) { insertBit(RCPSR, src, 2, bitIdx); }

    // ---- shifter -----------------------------------------------------
    // dst = shift(src, amount).  Returns the index in `src` of the carry-out bit
    // (NooDS lliS/lriS/ariS/rriS/immS), or -1 when the carry is unchanged.
    int shiftImm(int dst, int src, int type, int amount) {
        switch (type) {
        case 0:                                               // LSL #n: carry = src[32-n]
            if (amount == 0) { if (dst != src) w(mr(dst, src)); return -1; }
            w(slwi(dst, src, (uint8_t)amount));
            return amount - 1;
        case 1:                                               // LSR #n (0 -> 32): carry = src[n-1]
            if (amount == 0) { w(Ppc::li(dst, 0)); return 0; }
            w(srwi(dst, src, (uint8_t)amount));
            return 32 - amount;
        case 2:                                               // ASR #n (0 -> 31): carry = src[n-1]
            if (amount == 0) { w(srawi(dst, src, 31)); return 0; }
            w(srawi(dst, src, (uint8_t)amount));
            return 32 - amount;
        default:                                              // ROR #n (0 -> RRX)
            if (amount == 0) {
                w(rlwinm(T4, RCPSR, 3, 31, 31));              // CPSR.C (IBM bit 2) -> value bit 0
                w(srwi(dst, src, 1));
                w(or_(dst, dst, T4));
                return 31;                                    // carry = src bit 0
            }
            w(rotrwi(dst, src, (uint8_t)amount));
            return 32 - amount;
        }
    }
    // Shift by register.  S-forms that need the shifter's carry are delegated, so
    // this only ever produces the value (NooDS llr/lrr/arr/rrr value rules).
    void shiftReg(int dst, int src, int type, int rs) {
        w(rlwinm(T4, rs, 0, 24, 31));                         // n = Rs & 0xFF
        if (type == 2) {                                      // ASR: n >= 32 -> 31
            w(cmpwi(0, T4, 32));
            uint32_t b = idx(); w(bc(12, crbit(0, 0), 0));   // n < 32: skip the clamp
            w(Ppc::li(T4, 31));
            patchBc(b, 12, crbit(0, 0));
            w(sraw(dst, src, T4));
            return;
        }
        if (type == 3) {                                      // ROR: n & 31 (PPC masks the same way)
            w(neg(T5, T4));
            w(rlwnm(dst, src, T5, 0, 31));
            return;
        }
        w(cmpwi(0, T4, 32));
        uint32_t at = idx(); w(bc(12, crbit(0, 0), 0));       // n < 32 -> shift
        w(Ppc::li(dst, 0));
        uint32_t done = idx(); w(b(0));
        patchBc(at, 12, crbit(0, 0));
        w(type == 1 ? srw(dst, src, T4) : slw(dst, src, T4));
        patchB(done);
    }

    // Register-specified shift *whose carry is part of the result*: Thumb's
    // LSL/LSR/ASR/ROR Rd, Rs are the only flag source of the ALU row that writes C
    // this way, so unlike ARM's plain S-forms (delegated, see shiftReg's comment)
    // the carry has to be computed.  Value rules are shiftReg()'s; the carry-out is
    // left in bit 31 of `carryOut` and, when the amount is zero, the *current*
    // CPSR.C is copied there instead - NooDS gates every one of these carry writes
    // on `op2 > 0`, so amount 0 preserves the flag rather than clearing it.
    //
    //   LSL: carry = src[(32-n) & 31], 0 for n > 32      (lslDpT)
    //   LSR: carry = src[(n-1) & 31],  0 for n > 32      (lsrDpT)
    //   ASR: carry = src[(n-1) & 31], src[31] for n > 32 (asrDpT)
    //   ROR: carry = src[(n-1) & 31]                     (rorDpT, n & 31 rotates)
    //
    // with n = Rs & 0xFF (NooDS reads the amount as a uint8_t).  `wantCarry` false
    // skips the whole computation: the flag liveness pass has proven C dead here.
    void shiftRegCarry(int dst, int src, int type, int rs, int carryOut, bool wantCarry) {
        shiftReg(dst, src, type, rs);                         // the value (and it reuses T4)
        if (!wantCarry) return;
        w(rlwinm(T4, rs, 0, 24, 31));                         // n = Rs & 0xFF
        // rotate-left amount that lands the carry bit in bit 31: LSL wants
        // src[32-n] (so n-1), the other three want src[n-1] (so 32-n).
        if (type == 0) w(addi(T6, T4, -1));
        else           w(subfic(T6, T4, 32));
        w(rlwinm(T6, T6, 0, 27, 31));                         // & 31
        // rlwnm's mask fields count from value bit 31 down (mb=0 is value bit 31,
        // mb=31 is value bit 0 - measured, not assumed), and the carry has to be
        // parked in value bit 31 for carryBit 0 to name it.
        w(rlwnm(carryOut, src, T6, 0, 0));                    // candidate carry -> value bit 31
        if (type == 2) {                                      // ASR: n > 32 -> src[31]
            w(addi(T6, T4, -33));
            w(srawi(T6, T6, 31));                             // ~0 while n <= 32
            w(and_(carryOut, carryOut, T6));
            w(nor_(T6, T6, T6));                              // ~0 while n > 32
            w(and_(T6, src, T6));
            w(rlwinm(T6, T6, 0, 0, 0));                       // keep bit 31 only
            w(or_(carryOut, carryOut, T6));
        } else if (type != 3) {                               // LSL/LSR: n > 32 -> 0
            w(addi(T6, T4, -33));
            w(srawi(T6, T6, 31));                             // ~0 while n <= 32, else 0
            w(and_(carryOut, carryOut, T6));
        }
        // amount 0: C is preserved, so pick the live flag over the candidate.
        // T4 (n) is free once the mask is built, and T6/T4 are the only scratches
        // this far into operand-2 setup that the rest of emitDP does not need.
        w(addi(T6, T4, -1));
        w(srawi(T6, T6, 31));                                 // ~0 iff n == 0
        w(rlwinm(T4, RCPSR, 2, 0, 0));                        // CPSR.C -> value bit 31
        w(andc(carryOut, carryOut, T6));                      // candidate where n != 0
        w(and_(T4, T4, T6));                                  // live flag where n == 0
        w(or_(carryOut, carryOut, T4));
    }
};

// ------------------------------------------------- flags bookkeeping
static bool isTestOp(uint32_t dp) { return dp == 8 || dp == 9 || dp == 10 || dp == 11; }
static bool isMulOp(uint32_t op) { return (op & 0x0FC000F0u) == 0x00000090u; }
// UMULL/SMULL/UMLAL/SMLAL: 0000 1UAS RdHi RdLo Rs 1001 Rm - the same bits 27-25 = 000
// space as data processing, which is why it has to be tested *before* the DP path
// (before this, 0xE0810092 was decoded as "subs r0, r1, r2, lsl r0").
static bool isMulLongOp(uint32_t op) { return (op & 0x0F8000F0u) == 0x00800090u; }
static bool isPsrOp(uint32_t op) { return (op & 0x0F800000u) == 0x01000000u; }
static bool isMrsOp(uint32_t op) { return (op & 0x0FBF0FFFu) == 0x010F0000u; }
static bool isMsrRegOp(uint32_t op) { return (op & 0x0FB0FFF0u) == 0x0120F000u; }
static bool isMsrImmOp(uint32_t op) { return (op & 0x0FB0F000u) == 0x0320F000u; }
static bool isBranchOp(uint32_t op) { return (op & 0x0E000000u) == 0x0A000000u; }
// A conditional B/BL whose *not-taken* path can stay inside the block.  Most of the
// guest's control flow is this shape - "cmp; bne" around a short body - and every
// one of them used to end the block, so the not-taken path paid a full chain probe
// (table lookup, generation check, budget check, indirect jump) to reach the very
// next instruction.
static bool isCondBranchCont(uint32_t op) {
    return isBranchOp(op) && (op >> 28) < 14;
}
static bool isBxOp(uint32_t op) { return (op & 0x0FFFFFF0u) == 0x012FFF10u; }
static bool isBlxRegOp(uint32_t op) { return (op & 0x0FFFFFF0u) == 0x012FFF30u; }

// isPsrOp()'s pattern (bits 27..23 = 00010) is not "PSR only".  With bit 25 clear
// it also matches opcodes 8..15 in their *register-operand* form - TST/TEQ/CMP/CMN
// and ORR/MOV/BIC/MVN - and it is where BX/BLX(register), MRS and MSR live too.
// This predicate picks out the second group in its simplest shape: "cmp r1, r3",
// "mov r0, r1", "tst r8, r14" - the one the emitter has actually been checked
// against, and the one that matters here, because "cmp rX, rY" and "bx lr" are
// what the guest's inner loops are made of.
//
// The exclusions are not decoration.  MRS/MSR, BX/BLX and the multiplies all
// satisfy the plain shape's bits and each has its own (very different) emitter
// path; without them "mrs r0, cpsr" reads as "tst r0, r0" and the block emits the
// wrong instruction.  RRX is excluded because it rotates the *carry* in, which is
// the one shifted form whose flag handling the differential sweep still disagrees
// with (see armCanTranslate).
// The *miscellaneous* load/store group (bits 27..25 = 000 with bits 7 and 4 set -
// which is what separates it from a data-processing register shift).  It holds
// LDRH/STRH/LDRSB/LDRSH, the two-word forms (LDRD/STRD) and SWP.  Only the four
// half/byte forms are compiled here: they are what this ROM's guest code is full of
// (the entry-weighted delegate census put 2.5M of 2.8M delegated instructions in
// this class), and they need nothing the byte/word slice does not already have
// except a narrower access and, for the signed forms, a sign extension the helper
// does in one instruction.
static bool isCompiledMiscTransfer(uint32_t op) {
    if ((op >> 25) & 1) return false;                      // bits 27..25 = 000
    if (!((op >> 7) & 1) || !((op >> 4) & 1)) return false;// the "extra" marker
    const bool h   = (op >> 5) & 1;                        // H
    const bool s   = (op >> 6) & 1;                        // S
    if (!h && !s) return false;                            // LDRD/STRD/SWP: not this slice
    const bool reg = !((op >> 22) & 1);                    // I: 0 = register offset
    const bool pre = (op >> 24) & 1;                       // P
    const bool wb  = (op >> 21) & 1;                       // W
    const bool load = (op >> 20) & 1;                      // L
    const uint32_t rd = (op >> 12) & 0xF;
    const uint32_t rn = (op >> 16) & 0xF;
    if (load && rd == 15) return false;                    // load into PC
    if (!load && rd == 15) return false;                   // store of PC
    if (wb && rd == rn) return false;                      // unpredictable with write-back
    if (wb && rn == 15) return false;                      // write-back into the PC
    if (reg && (op & 0xF) == 15) return false;             // Rm = PC
    (void)pre;
    return true;
}

static bool isPlainRegDp(uint32_t op) {
    if ((op >> 25) & 1) return false;                                      // must be the register form
    if (((op >> 21) & 0xF) < 8) return false;                              // opcodes 8..15 only
    if ((op >> 4) & 1) return false;                                       // no register-specified shift
    if (((op >> 7) & 0x1F) != 0) return false;                             // no immediate shift either
    if (((op >> 5) & 3) == 3) return false;                                // no RRX (ROR with amount 0)
    if (isMrsOp(op) || isMsrRegOp(op) || isMsrImmOp(op)) return false;
    if (isBxOp(op) || isBlxRegOp(op)) return false;
    if (isMulOp(op)) return false;
    return true;
}

// Single data transfer (LDR/STR/LDRB/STRB) in the shapes this emitter compiles:
// no PC operand (a load into r15 refills the pipeline, a store of r15 reads pc+12),
// no write-back into the register the value also lands in, and - in the
// register-offset form - no shifted offset, which is the one part of the address
// arithmetic worth leaving to the interpreter for now.  Everything else in these
// two encoding classes stays a delegate, so a refused shape costs what it always
// did, and the extra load/store forms (LDRH/STRH/LDRD/STRD/SWP) live in class 0
// and are untouched.
static bool isCompiledTransfer(uint32_t op) {
    const uint32_t cls = (op >> 25) & 7;                   // 2 = immediate offset, 3 = register
    const bool pre     = (op >> 24) & 1;                   // P
    const bool wb      = (op >> 21) & 1;                   // W
    const bool load    = (op >> 20) & 1;                   // L
    const uint32_t rd  = (op >> 12) & 0xF;
    const uint32_t rn  = (op >> 16) & 0xF;
    if (cls == 3) {
        if ((op >> 4) & 1) return false;                   // the extra load/store forms
        if (((op >> 5) & 3) != 0) return false;            // only "rd,[rn,rm]": no LSR/ASR/ROR
        if (((op >> 7) & 0x1F) != 0) return false;         // ... and no shift amount
        if ((op & 0xF) == 15) return false;                // Rm = PC
    }
    // A PC base is fine, and worth having: "ldr r2,[pc,#0x4E0]" is how every literal
    // pool in this ROM is read.  NooDS's handlers use *registers[15], which during an
    // instruction is the instruction address + 8, and Em::getReg() reproduces exactly
    // that - so the address comes out right with no special case.
    if (load && rd == 15) return false;                    // load into PC: flushPipeline()
    if (!load && rd == 15) return false;                   // store of PC reads pc+12, not pc+8
    if (wb && rd == rn) return false;                      // unpredictable with write-back
    if (wb && rn == 15) return false;                      // write-back to the PC: a jump
    // Note what is *not* refused: P=0,W=0 ("str r3,[r1],#4" as some assemblers encode
    // it).  The ARM ARM calls it unpredictable, but NooDS does not: its 256-entry
    // handler table routes P=0,W=0 to the very same post-index handler as P=0,W=1 -
    // verified in interpreter_lookup.cpp, where both rows read strPtip/ldrPtip - so
    // the guest is relying on a write-back, and that is what gets emitted.
    return true;
}

// Does the barrel shifter of this instruction update the carry flag?  lliS/lriS/
// ariS/rriS and immS write C when the shift amount is non-zero; the by-register
// forms llrS/lrrS/arrS/rrrS write it when 0 < n <= 32 (assume written).
static bool shifterWritesC(uint32_t op) {
    if ((op >> 25) & 1) return ((op >> 8) & 0xF) != 0;                     // immediate: rotate != 0
    if ((op >> 4) & 1) return true;                                        // by register
    // Only lliS guards on the amount: LSL #0 never writes C, while LSR #0/ASR #0
    // mean #32 and ROR #0 is RRX (C = the source's LSB) - lriS/ariS/rriS all write it.
    const uint32_t type = (op >> 5) & 3, amt = (op >> 7) & 0x1F;
    return (type == 0) ? (amt != 0) : true;
}

// The flags an instruction *may modify*, exactly - this feeds the liveness pass,
// and an over-approximation there is not safe: it lets a later "writer" kill a
// previous instruction's still-observable flag.  In NooDS terms this is which
// handlers use "cpsr & ~0xC0000000" (N,Z only, plus the shifter's C) versus
// "cpsr & ~0xF0000000" (all four).
static uint8_t armFlagsWritten(uint32_t op) {
    if (isMulOp(op)) return (op >> 20) & 1 ? (uint8_t)(F_N | F_Z) : 0;      // muls: N,Z only
    // Long multiplies with the S bit set write N and Z from the *64-bit* result and
    // leave C and V alone.  That is the ARM7TDMI behavior the mGBA suite's
    // multiply-long table records; NooDS wrote no flags at all until the interpreter
    // was corrected along with this emitter.
    if (isMulLongOp(op)) return (op >> 20) & 1 ? (uint8_t)(F_N | F_Z) : 0;
    if (isMrsOp(op)) return 0;                                             // MRS writes no flags
    if (isMsrImmOp(op) || isMsrRegOp(op)) return (op & 0x80000u) ? F_ALL : 0;   // MSR of the "f" byte
    const uint32_t cls = (op >> 25) & 7;
    if (cls != 0 && cls != 1) return 0;                                    // branches move no flags
    const uint32_t dp = (op >> 21) & 0xF;
    if (dp == 8 || dp == 9) {                                              // TST/TEQ: V preserved
        uint8_t w = (uint8_t)(F_N | F_Z);
        if (shifterWritesC(op)) w |= F_C;
        return w;
    }
    if (dp == 10 || dp == 11) return F_ALL;                                // CMP/CMN replace all four
    if (!((op >> 20) & 1)) return 0;
    if (dp >= 2 && dp <= 7) return F_ALL;                                  // arithmetic S: N,Z,C,V
    uint8_t w = (uint8_t)(F_N | F_Z);                                      // logical S: N,Z + shifter C
    if (shifterWritesC(op)) w |= F_C;
    return w;
}
static uint8_t armFlagsRead(uint32_t op) {
    uint8_t r = 0;
    if ((op >> 28) < 14) r |= F_ALL;                                       // the condition reads all four
    if (isMrsOp(op)) r |= F_ALL;                                           // MRS Rd, CPSR reads everything
    if (isPsrOp(op) && ((op >> 12) & 0xF) == 15 && !((op >> 25) & 1)) {
        // MSR CPSR_c (mode byte): setCpsr() consults I/F and the IRQ state
        if (op & 0x10000u) r |= F_ALL;
    }
    const uint32_t cls = (op >> 25) & 7;
    if (cls == 0 || cls == 1) {
        const uint32_t dp = (op >> 21) & 0xF;
        if (dp == 5 || dp == 6 || dp == 7) r |= F_C;                       // ADC/SBC/RSC read C
        // ROR #0 is RRX: it rotates the old carry into the top bit, so the shifter
        // itself consumes C (no by-register form can be RRX).
        if (!((op >> 25) & 1) && !((op >> 4) & 1) && ((op >> 5) & 3) == 3 && ((op >> 7) & 0x1F) == 0)
            r |= F_C;
    }
    return r;
}

// ------------------------------------------------- single data transfer
// LDR/STR/LDRB/STRB: compute the address here, call the memory helper for the
// access, keep the guest register file resident.  What the helper does *not* do is
// anything that touches a guest register - the write-back, the destination and the
// base all live in the block's own registers, so nothing has to be written out and
// read back around the call.  That marshalling is exactly what made a delegated
// load more expensive than interpreting it.
static void emitTransfer(Em& e, uint32_t op) {
    const uint32_t cond = op >> 28;
    const bool pre    = (op >> 24) & 1;                 // P
    const bool up     = (op >> 23) & 1;                 // U
    const bool byte   = (op >> 22) & 1;                 // B
    const bool wb     = (op >> 21) & 1;                 // W
    const bool load   = (op >> 20) & 1;                 // L
    const uint32_t rd = (op >> 12) & 0xF;
    const uint32_t rn = (op >> 16) & 0xF;
    const bool regOff = ((op >> 25) & 7) == 3;

    uint32_t skip = UINT32_MAX;
    if (cond < 14) {
        e.cycle(1);                                     // condition false: 1 cycle
        skip = e.emitCondTest(cond);
    }

    // T2 = the offset operand the interpreter's handlers would call op2.
    if (regOff) {
        const int src = e.getReg((int)(op & 0xF), T4);
        if (src != T2) e.w(mr(T2, src));
        if (!up) e.w(subfic(T2, T2, 0));                // "!U": subtract, so op1 + op2 is always an add
    } else {
        const uint32_t off = op & 0xFFFu;
        e.li(T2, up ? off : (uint32_t)(0u - off));
    }

    // A store's value is read before anything else can touch the base register
    // ("str r0, [r0, #4]!" stores the old r0) and before the call, which may clobber
    // every volatile register.  T5 is the one temp the address arithmetic below does
    // not use - and it must not be T2, which holds the offset.
    if (!load) e.w(mr(T5, e.getReg((int)rd, T4)));   // T5: the store's value, staged

    const int base = e.getReg((int)rn, T4);
    if (pre) e.w(add(T3, base, T2));                    // effective address
    else if (base != T3) e.w(mr(T3, base));

    // The write-back, and it has to happen before the call: T3 does not survive it.
    // Pre-index writes back the effective address itself; post-index ("ldr r0,[r1],#4")
    // writes back the *unmodified* base plus the offset - which is the whole point of
    // the form, and getting it wrong does not just corrupt a register, it leaves the
    // guest's pointer unbumped and loops for ever.  T6 stages that sum because
    // setReg() reaches for T0 when the base is the link register.
    if (pre) { if (wb) e.setReg((int)rn, T3); }
    else {
        // Post-index always writes back, W bit or not - see isCompiledTransfer: NooDS
        // sends both P=0 encodings to the post-index-with-write-back handler, so the
        // guest's pointer advances either way.
        e.w(add(T6, T3, T2));
        e.setReg((int)rn, T6);
    }

    e.w(mr(3, RCPU));
    e.w(mr(4, T3));
    // r5 is the helper's value argument - and it is also T2, the offset register, so
    // it can only be loaded once the offset is no longer needed (i.e. after the
    // write-back above).
    if (!load) e.w(mr(5, T5));
    e.callHelper(load ? (byte ? H_Load8 : H_Load32) : (byte ? H_Store8 : H_Store32));
    // The cycle charge is the handler's own formula, known at compile time: NooDS's
    // ldr/ldrb handlers return (arm7 << 1) + 1 and str/strb return arm7 + 1, so a
    // load is 1 cycle on the ARM9 and 3 on the ARM7, and a store 1 and 2.  Nothing
    // has to come back from the helper but the loaded value, which is in r3 - which
    // is the point: a two-word struct return would land in r3:r4 only under one
    // ABI, and under the hidden-pointer convention it lands on the load address
    // instead (measured, on this very instruction: the helper was handed the
    // return buffer in r3 and read the address out of the offset register).
    e.cycle(e.arm7 ? (load ? 3 : 2) : 1);
    if (load) {
        // r3 holds the value, but it is also T0 - which setReg() reaches for when the
        // destination has no home register (LR lives in memory).  Stage the value
        // first so its own address computation cannot clobber it.
        if (rd == 14) { e.w(mr(T4, 3)); e.setReg(14, T4); }
        else e.setReg((int)rd, 3);
    }

    if (skip != UINT32_MAX) e.closeSkip(skip);
}

// LDRH/STRH/LDRSB/LDRSH.  Same shape as emitTransfer - address in T3, one helper
// call, no register-file marshalling - with the offset in the split nibble form
// this encoding uses, and a sign-extended result where the opcode asks for one.
static void emitMiscTransfer(Em& e, uint32_t op) {
    const uint32_t cond = op >> 28;
    const bool pre    = (op >> 24) & 1;                    // P
    const bool up     = (op >> 23) & 1;                    // U
    const bool regOff = !((op >> 22) & 1);                 // I
    const bool wb     = (op >> 21) & 1;                    // W
    const bool load   = (op >> 20) & 1;                    // L
    const uint32_t rd = (op >> 12) & 0xF;
    const uint32_t rn = (op >> 16) & 0xF;
    const bool hBit   = (op >> 5) & 1;                     // H
    const bool sBit   = (op >> 6) & 1;                     // S

    int helper;
    if (load)  helper = hBit ? (sBit ? H_LoadS16 : H_Load16) : H_LoadS8;   // LDRSH / LDRH / LDRSB
    else       helper = H_Store16;                                          // STRH only

    uint32_t skip = UINT32_MAX;
    if (cond < 14) {
        e.cycle(1);
        skip = e.emitCondTest(cond);
    }

    if (regOff) {
        const int src = e.getReg((int)(op & 0xF), T4);
        if (src != T2) e.w(mr(T2, src));
        if (!up) e.w(subfic(T2, T2, 0));
    } else {
        const uint32_t off = (uint32_t)((((op >> 8) & 0xF) << 4) | (op & 0xF));   // imm4H:imm4L
        e.li(T2, up ? off : (uint32_t)(0u - off));
    }

    if (!load) e.w(mr(T5, e.getReg((int)rd, T4)));         // the store's value, before the call

    const int base = e.getReg((int)rn, T4);
    if (pre) e.w(add(T3, base, T2));
    else if (base != T3) e.w(mr(T3, base));
    if (pre) { if (wb) e.setReg((int)rn, T3); }
    else { e.w(add(T6, T3, T2)); e.setReg((int)rn, T6); }  // post always writes back

    e.w(mr(3, RCPU));
    e.w(mr(4, T3));
    if (!load) e.w(mr(5, T5));
    e.callHelper((HelperId)helper);
    e.cycle(e.arm7 ? (load ? 3 : 2) : 1);
    if (load) {
        if (rd == 14) { e.w(mr(T4, 3)); e.setReg(14, T4); }
        else e.setReg((int)rd, 3);
    }
    if (skip != UINT32_MAX) e.closeSkip(skip);
}

// ------------------------------------------------- Thumb front-end (slice 1)
//
// Thumb is a different ISA - 16-bit encodings, its own decoder, and flag rules
// fixed by format instead of by an S bit - but the *host* code it needs is exactly
// what the ARM slice already provides: the register-resident block ABI, the memory
// helpers, the exits, the chain probe.  This section therefore only has to decode
// halfwords, decide what is compiled, and emit.
//
// Compiled: formats 1-4 (imm shifts, ADD/SUB, MOV/CMP/ADD/SUB imm8, the ALU row),
// format 5 (hi-register ADD/CMP/MOV and BX), the whole load/store group (register
// offset, imm5, PC-relative, SP-relative, the signed forms), formats 12/13 (address
// arithmetic), conditional and unconditional branches, and BL pairs fused into one
// compiled instruction.
//
// Delegated (run by the interpreter from inside the block): register-shifted ALU
// ops 2/3/4/7 (their carry rules are not the ARM shifter's), MUL on ARM7 (variable
// timing), PUSH/POP, LDMIA/STMIA, and hi-register forms with an r15 operand.
// Refused - the block ends and the driver interprets the halfword - for SWI and the
// undefined 0xDExx encodings, BLX (it switches to ARM), and a stray BL offset
// halfword.
//
// NooDS fidelity, quirks included; the interpreter is the reference:
//   * registers[15] holds "instruction address + 2" in Thumb mode, and NooDS's
//     PC-relative handlers use `*registers[15] & ~3` as the base, i.e. (addr + 2)
//     & ~3 - *not* the ARM ARM's (addr + 4) & ~3.  thumbPcBase() reproduces NooDS.
//   * cycle charges come off the handlers: 1 for the ALU rows, 3 for a taken branch
//     (1 for a not-taken conditional branch), (arm7 << 1) + 1 for loads and
//     arm7 + 1 for stores.  The transfer formulas are the same ones the ARM path
//     charges, which is why emitThumbMem() charges them identically.
//   * a BL pair is *two* interpreter steps (1 + 3 cycles) with a visible intermediate
//     LR, so a block that fuses it reports two instructions and charges 4 cycles -
//     which is exactly what the shadow verifier replays.

// emitDP() and emitMul() are defined below this section (with the ARM emitters);
// the Thumb ALU rows go through them rather than duplicating the flag work.
static void emitDP(Em& e, uint32_t op, uint8_t flagMask, bool thumbRegShift = false);
static void emitMul(Em& e, uint32_t op, uint8_t flagMask);
static void emitMulLong(Em& e, uint32_t op, uint8_t flagMask);

// NooDS advances registers[15] *before* executing each instruction, so during the
// handler the pipeline PC reads as instrAddr + 4 (ARM: +8) - the same bias the ARM
// side handles by refusing every PC operand.  Both Thumb PC-relative forms mask the
// low bits: addPcT has `*registers[15] & ~3`, ldrPcT the same.
static uint32_t thumbPcBase(uint32_t addr) { return (addr + 4u) & ~3u; }  // NooDS: *registers[15] & ~3

// A Thumb data-processing instruction, expressed as the ARM encoding emitDP() takes.
// Thumb has no condition field and no S bit: every ALU instruction here is
// unconditional, and the format decides whether flags are written.
// Assemble an ARM data-processing word for emitDP().  `op2` is used verbatim in
// bits 11-0, so it has to be a *properly encoded* operand-2 field:
//   * a register operand is the register number, optionally with a shift
//     (bits 11-7 amount, 6-5 type, 4 = 0 for an immediate shift),
//   * an immediate operand is bit 25 | (rotate << 8) | value.
// And `rn` is ARM's operand 1, which the Thumb rows do not always put where the
// encoding fields suggest: "and rd, rs" is ARM's "and rd, rd, rs", so the *same*
// register appears as both destination and operand 1.
static uint32_t thumbDpOp(uint32_t dp, bool s, uint32_t rd, uint32_t rn, uint32_t op2) {
    return (14u << 28) | (dp << 21) | (s ? (1u << 20) : 0u) | (rn << 16) | (rd << 12) | op2;
}
static constexpr uint32_t kImm25 = 1u << 25;      // ARM marking for an immediate operand 2

// Which flags the instruction writes - read straight off the handlers in
// interpreter_alu.cpp (thumb*).
static uint8_t thumbWrittenFlags(uint16_t op) {
    if ((op >> 13) == 0) {                                  // formats 1 and 2
        return ((op >> 11) & 3) == 3 ? (uint8_t)F_ALL : (uint8_t)(F_N | F_Z | F_C);
    }
    if ((op >> 13) == 1) {                                  // format 3
        return ((op >> 11) & 3) == 0 ? (uint8_t)(F_N | F_Z) : (uint8_t)F_ALL;
    }
    if ((op & 0xFC00u) == 0x4000u) {                        // format 4 (ALU row)
        const uint32_t a = (op >> 6) & 0xF;
        if (a == 5 || a == 6 || a == 9 || a == 10 || a == 11) return (uint8_t)F_ALL;  // ADC/SBC/NEG/CMP/CMN
        // The register shifts (2/3/4/7) write C as well - lslDpT and friends set it
        // whenever the amount is non-zero - which the delegated path used to hide
        // behind F_ALL.  The other logical rows (AND/EOR/ORR/BIC/MVN/MUL) write N,Z
        // only.
        if (a == 2 || a == 3 || a == 4 || a == 7) return (uint8_t)(F_N | F_Z | F_C);
        return (uint8_t)(F_N | F_Z);                         // logical row and MUL
    }
    if ((op & 0xFC00u) == 0x4400u && ((op >> 8) & 3) == 1) return (uint8_t)F_ALL;     // CMP Rd, Rs (hi)
    return 0;
}

static uint8_t thumbReadFlags(uint16_t op) {
    if ((op >> 12) == 13) return (uint8_t)F_ALL;                                     // conditional branch
    if ((op & 0xFC00u) == 0x4000u) {
        const uint32_t a = (op >> 6) & 0xF;
        if (a == 5 || a == 6) return (uint8_t)F_C;                                   // ADC/SBC carry in
    }
    if ((op & 0xFC00u) == 0x4400u && ((op >> 8) & 3) == 1) return (uint8_t)F_ALL;     // CMP Rd, Rs (hi)
    return 0;
}

// ---------------------------------------------------------------- decoding

// The ALU rows (formats 1-4 and the hi-register forms of 5) as the ARM operation
// each one behaves like.  One table, used by both the classifier and the emitter, so
// "what is compiled" and "what is emitted" cannot drift apart.
struct ThumbAlu {
    bool     ok   = false;     // false: this op is not compiled
    uint32_t dp   = 0;         // ARM operation field for emitDP()
    bool     s    = true;      // does it write flags
    uint32_t rd = 0, rn = 0, op2 = 0;
    uint8_t  flags = 0;
    uint8_t  reads = 0;
    bool     thumbRegShift = false;  // the ALU row's register shifts: 1 cycle flat,
                                     // and the shifter's carry is a real flag write
};

static ThumbAlu thumbAlu(const Em& e, uint16_t op) {
    ThumbAlu a;
    a.flags = thumbWrittenFlags(op);
    a.reads = thumbReadFlags(op);
    const uint32_t t13 = op >> 13;

    if (t13 == 0) {                                          // format 1 or 2
        const uint32_t sub = (op >> 11) & 3;
        const uint32_t rd = op & 7, rs = (op >> 3) & 7;
        if (sub != 3) {                                      // format 1: LSL/LSR/ASR Rd, Rs, #imm5
            // as an ARM "MOV Rd, Rs, <type> #amt": the shifter then produces the
            // carry exactly as the Thumb handler does (amount 0 = shift by 32 for
            // LSR/ASR, and "no carry" for LSL, both of which are ARM's rules too).
            a.ok = true; a.dp = 13; a.rd = rd; a.rn = 0;
            a.op2 = (((op >> 6) & 0x1F) << 7) | (sub << 5) | rs;
        } else {                                             // format 2: ADD/SUB Rd, Rs, Rm/#imm3
            // addRegT: rd = Rs + Rn (Rn is the field at 8-6, read as a register);
            // addImm3T: rd = Rs + imm3.  Bit 10 is the I bit that separates them.
            const bool immForm = (op & 0x400u) != 0;
            a.ok = true; a.dp = (op & 0x200u) ? 2u : 4u;      // bit 9 = subtract
            a.rd = rd; a.rn = rs;
            a.op2 = immForm ? (kImm25 | ((op >> 6) & 7)) : (op >> 6) & 7;
        }
        return a;
    }
    if (t13 == 1) {                                          // format 3: MOV/CMP/ADD/SUB imm8
        const uint32_t sub = (op >> 11) & 3;
        const uint32_t rd = (op >> 8) & 7;
        const uint32_t imm = op & 0xFF;
        a.ok = true;
        const uint32_t immOp = kImm25 | imm;                 // an ARM 8-bit immediate
        switch (sub) {
        case 0:  a.dp = 13; a.rd = rd;             a.op2 = immOp; break; // MOV Rd,#imm8
        case 1:  a.dp = 10; a.rd = 0;  a.rn = rd;  a.op2 = immOp; break; // CMP Rd,#imm8
        case 2:  a.dp = 4;  a.rd = rd; a.rn = rd;  a.op2 = immOp; break; // ADD Rd,#imm8
        default: a.dp = 2;  a.rd = rd; a.rn = rd;  a.op2 = immOp; break; // SUB Rd,#imm8
        }
        return a;
    }
    if ((op & 0xFC00u) == 0x4000u) {                         // format 4: the ALU row
        const uint32_t aop = (op >> 6) & 0xF;
        const uint32_t rd = op & 7, rs = (op >> 3) & 7;
        // Every handler here reads operand 1 from the *first* register field
        // (opcode & 7 = Rd) and operand 2 from (opcode >> 3) & 7 = Rs, which is where
        // the rows differ from the ARM encodings they are mapped onto:
        //   and/eor/orr/bic/adc/sbc: rd = Rd op Rs   -> ARM "op rd, Rd, Rs"
        //   tst/cmp/cmn:             test Rd, Rs     -> ARM "test Rd, Rs"
        //   neg:                     rd = 0 - Rs     -> ARM "rsb rd, Rs, #0"
        //   mvn:                     rd = ~Rs        -> ARM "mvn rd, Rs"
        a.rd = rd; a.rn = rd; a.op2 = rs;
        switch (aop) {
        case 0:  a.dp = 0;  break;                           // AND
        case 1:  a.dp = 1;  break;                           // EOR
        case 2: case 3: case 4: case 7: {                    // LSL/LSR/ASR/ROR Rd, Rs
            // lslDpT/lsrDpT/asrDpT/rorDpT: rd = rd <shift> Rs, N,Z and C written (C
            // is *preserved* when the amount is 0), amount = Rs & 0xFF.  The ARM
            // encoding that means exactly that is "MOVS rd, rd, <type> Rs": bit 4
            // set selects a shift by register, the amount register sits in bits
            // 11:8 and the type in 6:5.  The type is not the row number: the row
            // order is LSL/LSR/ASR/ROR = 2/3/4/7 but the ARM shift types are
            // 0/1/2/3, and ROR is 3 in both only because it happens to be last.
            const uint32_t type = (aop == 2) ? 0u : (aop == 3) ? 1u : (aop == 4) ? 2u : 3u;
            a.dp = 13; a.rd = rd; a.rn = 0;
            a.op2 = (rs << 8) | (type << 5) | 0x10u | rd;
            a.thumbRegShift = true;
            break;
        }
        case 5:  a.dp = 5;  break;                           // ADC
        case 6:  a.dp = 6;  break;                           // SBC
        case 8:  a.dp = 8;  a.rd = 0; break;                 // TST (no destination)
        case 9:  a.dp = 3;  a.rn = rs; a.op2 = kImm25; break; // NEG = RSB Rd, Rs, #0
        case 10: a.dp = 10; a.rd = 0; break;                 // CMP (no destination)
        case 11: a.dp = 11; a.rd = 0; break;                 // CMN (no destination)
        case 12: a.dp = 12; break;                           // ORR
        case 13: if (e.arm7) return a; a.dp = 0; break;      // MUL (ARM9 only): emitMul path
        case 14: a.dp = 14; break;                           // BIC
        default: a.dp = 15; a.rn = 0; break;                 // MVN (~Rs; no operand 1)
        }
        a.ok = true;
        return a;
    }
    if ((op & 0xFC00u) == 0x4400u) {                         // format 5: hi-register ops
        const uint32_t sub = (op >> 8) & 3;
        const uint32_t rd = ((op >> 4) & 8) | (op & 7);
        const uint32_t rs = (op >> 3) & 0xF;
        if (sub == 3) return a;                               // BX/BLX: handled in the branch path
        if (rd == 15 || rs == 15) return a;                   // PC operands: delegated
        if (sub == 0)      { a.ok = true; a.dp = 4;  a.s = false; a.rd = rd; a.rn = rd; a.op2 = rs; }  // ADD
        else if (sub == 1) { a.ok = true; a.dp = 10; a.s = true;  a.rd = 0;  a.rn = rd; a.op2 = rs; }  // CMP
        else if (sub == 2) { a.ok = true; a.dp = 13; a.s = false; a.rd = rd;              a.op2 = rs; }  // MOV
        return a;                                             // sub == 3 is BX (above)
    }
    return a;
}

// A single memory access.  Every Thumb load/store compiled here has r0-r7 (or SP)
// as its base and its destination, so no write-back and no r15 special case is
// involved - which is what makes this so much smaller than the ARM transfer path.
struct ThumbMem {
    bool     ok = false;
    HelperId helper = H_Load32;
    bool     load = false;
    bool     baseConst = false;      // PC-relative: base = thumbPcBase(instrAddr)
    uint8_t  baseReg = 0;
    bool     hasIndex = false;
    uint8_t  indexReg = 0;
    uint32_t offset = 0;
    uint8_t  rd = 0;
};

static ThumbMem thumbMem(uint16_t op) {
    ThumbMem m;
    const uint32_t t13 = op >> 13;
    const uint32_t rd = op & 7;
    if (t13 == 3) {                                          // format 9: LDR/STR/LDRB/STRB, imm5
        const bool byte = (op >> 12) & 1;
        const bool load = (op >> 11) & 1;
        m.ok = true; m.load = load; m.rd = (uint8_t)rd;
        m.baseReg = (uint8_t)((op >> 3) & 7);
        m.offset = ((op >> 6) & 0x1F) << (byte ? 0 : 2);
        m.helper = load ? (byte ? H_Load8 : H_Load32) : (byte ? H_Store8 : H_Store32);
        return m;
    }
    if (t13 == 4) {
        if ((op >> 12) & 1) {                                // format 11: LDR/STR Rd,[SP,#imm8]
            const bool load = (op >> 11) & 1;
            m.ok = true; m.load = load; m.rd = (uint8_t)((op >> 8) & 7);
            m.baseReg = 13;
            m.offset = (op & 0xFF) << 2;
            m.helper = load ? H_Load32 : H_Store32;
        } else {                                             // format 10: LDRH/STRH Rd,[Rb,#imm5]
            const bool load = (op >> 11) & 1;
            m.ok = true; m.load = load; m.rd = (uint8_t)rd;
            m.baseReg = (uint8_t)((op >> 3) & 7);
            m.offset = ((op >> 6) & 0x1F) << 1;
            m.helper = load ? H_Load16 : H_Store16;
        }
        return m;
    }
    if ((op & 0xF000u) == 0x5000u) {                         // format 7/8: register offset
        const uint32_t sel = (op >> 9) & 7;
        m.ok = true; m.rd = (uint8_t)rd;
        m.baseReg = (uint8_t)((op >> 3) & 7);
        m.hasIndex = true; m.indexReg = (uint8_t)((op >> 6) & 7);
        switch (sel) {
        case 0: m.load = false; m.helper = H_Store32; break;
        case 1: m.load = false; m.helper = H_Store16; break;
        case 2: m.load = false; m.helper = H_Store8;  break;
        case 3: m.load = true;  m.helper = H_LoadS8;  break;
        case 4: m.load = true;  m.helper = H_Load32;  break;
        case 5: m.load = true;  m.helper = H_Load16;  break;
        case 6: m.load = true;  m.helper = H_Load8;   break;
        default:m.load = true;  m.helper = H_LoadS16; break;
        }
        return m;
    }
    if ((op & 0xF800u) == 0x4800u) {                         // format 6: LDR Rd,[PC,#imm8]
        m.ok = true; m.load = true; m.rd = (uint8_t)((op >> 8) & 7);
        m.baseConst = true;
        m.offset = (op & 0xFF) << 2;
        m.helper = H_Load32;
        return m;
    }
    return m;
}

// ---------------------------------------------------------------- classifier

static bool thumbIsBx(uint16_t op)     { return (op & 0xFF80u) == 0x4700u; }   // BX Rs (bit 7 = 0)
static bool thumbIsBlxReg(uint16_t op) { return (op & 0xFF80u) == 0x4780u; }   // BLX Rs (bit 7 = 1, ARM9)
static bool thumbIsBCond(uint16_t op) { return (op >> 12) == 13 && ((op >> 8) & 0xF) < 14; }
static bool thumbIsB(uint16_t op)     { return (op & 0xF800u) == 0xE000u; }    // unconditional B
static bool thumbIsBLSetup(uint16_t op) { return (op & 0xF800u) == 0xF000u; }  // BL high halfword
static bool thumbIsBLOff(uint16_t op)   { return (op & 0xF800u) == 0xF800u; }  // BL low halfword
static bool thumbIsPushPop(uint16_t op) { return (op & 0xF600u) == 0xB400u; }
static bool thumbIsLdmStm(uint16_t op)  { return (op >> 12) == 12; }
static bool thumbIsSwi(uint16_t op)     { return (op & 0xFF00u) == 0xDF00u; }
static bool thumbIsSpAdj(uint16_t op)   { return (op >> 8) == 0xB0u; }
static bool thumbIsLea(uint16_t op)     { return (op >> 12) == 10; }
static bool thumbIsHi(uint16_t op)      { return (op & 0xFC00u) == 0x4400u; }

// Instructions that are correct but not compiled yet: the interpreter runs them
// *inside* the block (delegateInline) or, when they can move the PC, at the end of
// it (exitDelegateNow).  `terminates` is what the block structure needs to know.
static bool thumbIsDelegate(uint16_t op, bool* terminates) {
    *terminates = false;
    if (thumbIsPushPop(op)) {                                // PUSH/POP
        const bool load = (op >> 11) & 1;                    // POP
        if (load && ((op >> 8) & 1)) *terminates = true;      // POP {..., pc}: the PC moves
        return true;
    }
    if (thumbIsLdmStm(op)) return true;                      // LDMIA/STMIA (no PC forms)
    if ((op & 0xFC00u) == 0x4400u && ((op >> 8) & 3) != 3) {  // hi-register ops with r15
        const uint32_t rd = ((op >> 4) & 8) | (op & 7);
        const uint32_t rs = (op >> 3) & 0xF;
        if (rd == 15) { *terminates = true; return true; }    // mov/add pc, Rs
        if (rs == 15) return true;
    }
    return false;
}

static bool thumbCanTranslate(const Em& e, uint16_t op, bool* terminates) {
    *terminates = false;
    if ((op >> 12) == 13) {                                   // 0xD000-0xDFFF
        if (thumbIsBCond(op)) { *terminates = true; return true; }   // Bcc
        return false;                                         // SWI #imm8 / undefined cond=14
    }
    if (thumbIsBx(op)) { *terminates = true; return ((op >> 3) & 0xF) != 15; }
    if (thumbIsBlxReg(op)) return false;                      // mode switch: refused
    if (thumbIsB(op)) { *terminates = true; return true; }
    if (thumbIsBLSetup(op)) { *terminates = true; return true; }   // paired or standalone
    if (thumbIsBLOff(op)) return false;                       // only valid after a setup
    if (thumbIsLdmStm(op) || thumbIsPushPop(op)) return false;     // delegated
    if (thumbIsLea(op)) return true;                          // ADD Rd, PC/SP, #imm8
    if (thumbIsSpAdj(op)) return true;                        // ADD/SUB SP, #imm
    if (thumbMem(op).ok) return true;                          // the load/store group
    return thumbAlu(e, op).ok;                                 // formats 1-4, hi-register forms
}

// ---------------------------------------------------------------- emitters

// One memory access through the same helpers the ARM path calls.  r3 = cpu,
// r4 = address, r5 = value (stores); the loaded value comes back in r3 and the
// destination is always a resident low register, so it lands there directly.
static void emitThumbMem(Em& e, uint16_t op, uint32_t instrAddr) {
    const ThumbMem m = thumbMem(op);
    if (!m.ok) return;
    if (!m.load) e.w(mr(5, e.getReg((int)m.rd, T5)));         // stage the value before anything clobbers it
    if (m.baseConst) {
        e.li(4, thumbPcBase(instrAddr) + m.offset);
    } else {
        const int base = e.getReg((int)m.baseReg, T4);
        if (m.hasIndex) {
            const int idx = e.getReg((int)m.indexReg, T3);
            e.w(add(4, base, idx));
        } else if (m.offset) {
            e.w(addi(4, base, (int16_t)m.offset));
        } else {
            e.w(mr(4, base));
        }
    }
    e.w(mr(3, RCPU));
    e.callHelper(m.helper);
    e.cycle(e.arm7 ? (m.load ? 3 : 2) : 1);                   // the handler's own formula
    if (m.load) e.setReg((int)m.rd, 3);                       // r0-r7 are resident: no staging needed
}

// Bcc: the taken path leaves the block (exactly like a normal branch), the
// not-taken path falls through into the next instruction of this same block.
static void emitThumbCondBranchExit(Em& e, uint16_t op, uint32_t instrAddr) {
    const uint32_t cond = (op >> 8) & 0xF;
    const int32_t off = (int32_t)(int8_t)(op & 0xFF) << 1;
    e.cycle(1);                                               // not taken: 1 cycle
    const uint32_t skip = e.emitCondTest(cond);
    e.cycle(2);                                               // taken: 3
    e.exitPcConst(instrAddr + 4u + (uint32_t)off);
    e.closeSkip(skip);
}

static void emitThumbB(Em& e, uint16_t op, uint32_t instrAddr) {
    const int32_t off = ((int32_t)(int16_t)(op << 5)) >> 4;    // NooDS bT: sign-extended 11 bits, <<1
    e.cycle(3);
    e.exitPcConst(instrAddr + 4u + (uint32_t)off);
}

// BL, both halfwords at once.  NooDS runs them as two instructions:
//   blSetupT: LR = registers[15] + (offset11 << 12), where registers[15] = hiAddr + 4
//   blOffT:   target = LR + (offset_lo << 1), LR = registers[15] - 1, and there
//             registers[15] is the offset halfword's address + 4
// so the fused form has to leave LR = hiAddr + 5 (the return address, T bit included -
// the pair's two steps advance registers[15] past the whole pair) and jump to
// hiAddr + 4 + (offset11 << 12) + (offset_lo << 1).
static void emitThumbBL(Em& e, uint16_t hi, uint16_t lo, uint32_t hiAddr) {
    const int32_t s = ((int32_t)(int16_t)(hi << 5)) >> 5;      // sign-extended 11-bit field
    const uint32_t target = hiAddr + 4u + (uint32_t)(s << 12) + ((uint32_t)(lo & 0x7FF) << 1);
    e.cycle(4);                                               // 1 + 3
    e.li(T3, hiAddr + 5u);                                    // NooDS's return address, T bit included
    e.setReg(14, T3);
    e.exitPcConst(target);
}

// A BL setup halfword on its own (no offset halfword follows): NooDS leaves LR as
// an intermediate value and the next halfword runs separately, so the block ends
// here and the driver picks up at addr + 2.
static void emitThumbBLSetup(Em& e, uint16_t hi, uint32_t hiAddr) {
    const int32_t s = ((int32_t)(int16_t)(hi << 5)) >> 5;
    e.cycle(1);
    e.li(T3, hiAddr + 4u + (uint32_t)(s << 12));
    e.setReg(14, T3);
    e.exitPcConst(hiAddr + 2u);
}

// BX Rs: unlike the ARM-mode BX handler (which only ever *sets* CPSR.T), bxRegT
// clears T when bit 0 of the target is clear, so both directions have to be emitted.
static void emitThumbBx(Em& e, uint16_t op) {
    const uint32_t rs = (op >> 3) & 0xF;
    e.cycle(3);
    const int target = e.getReg((int)rs, T5);
    e.w(Ppc::rlwinm(T6, target, 5, 26, 26));                  // (target & 1) -> a T-bit mask
    e.li(T7, 0xFFFFFFDFu);                                    // ~BIT(5)
    e.w(and_(RCPSR, RCPSR, T7));
    e.w(or_(RCPSR, RCPSR, T6));
    e.markCpsr();
    e.exitPcRegDyn(target);
}

// ADD Rd, PC/SP, #imm8 (format 12).  PC-relative uses NooDS's base, see above.
static void emitThumbLea(Em& e, uint16_t op, uint32_t instrAddr) {
    const bool sp = (op >> 11) & 1;
    const uint32_t rd = (op >> 8) & 7;
    const uint32_t imm = (op & 0xFF) << 2;
    e.cycle(1);
    if (sp) {
        e.w(addi(RR((int)rd), RR(13), (int16_t)imm));
        e.setReg((int)rd, RR((int)rd));                       // marks the register dirty
    } else {
        e.li(T3, thumbPcBase(instrAddr) + imm);
        e.setReg((int)rd, T3);
    }
}

// ADD/SUB SP, #imm7 (format 13).
static void emitThumbSpAdj(Em& e, uint16_t op) {
    const uint32_t imm = ((op & 0x80u) ? (0u - (op & 0x7Fu)) : (op & 0x7Fu)) << 2;
    e.cycle(1);
    e.w(addi(RR(13), RR(13), (int16_t)imm));
    e.setReg(13, RR(13));
}

// The ALU rows, through the ARM emitters (see thumbAlu).
static void emitThumbAlu(Em& e, uint16_t op, uint8_t flagMask) {
    const ThumbAlu a = thumbAlu(e, op);
    if (!a.ok) return;
    if ((op & 0xFC00u) == 0x4000u && ((op >> 6) & 0xF) == 13) {   // MUL Rd, Rs
        // ARM MUL: A=0, S=1, Rd = bits 19-16, Rs = 11-8, Rm = 3-0 (multiply is
        // commutative, and NooDS's thumb mulT sets N,Z - armFlagsWritten agrees).
        const uint32_t armOp = (14u << 28) | (1u << 20) |
                               ((uint32_t)(op & 7) << 16) | (((uint32_t)((op >> 3) & 7)) << 8) |
                               (uint32_t)(op & 7);
        emitMul(e, armOp, (uint8_t)(flagMask & (F_N | F_Z)));
        return;
    }
    const bool isNeg = (op & 0xFC00u) == 0x4000u && ((op >> 6) & 0xF) == 9;
    // negDpT writes N, Z and C (= result == 0) and *clears* V: its mask is
    // ~0xF0000000 with only three bits set again.  The ARM RSB it is mapped onto
    // produces exactly that C (0 >= res, i.e. res == 0), but it would also compute
    // the signed-overflow flag - so V is dropped from the mask and written as a
    // constant zero instead.
    uint8_t mask = a.s ? (uint8_t)(flagMask & a.flags) : 0;
    if (isNeg) mask = (uint8_t)(mask & (F_N | F_Z | F_C));
    emitDP(e, thumbDpOp(a.dp, a.s, a.rd, a.rn, a.op2), mask, a.thumbRegShift);
    if (isNeg && mask) {
        e.setCpsrBitConst(28, 0);                             // CPSR.V = 0
        e.markCpsr();
    }
}

// One Thumb instruction: the same order of decisions as thumbCanTranslate().
static void thumbEmitOne(Em& e, uint16_t op, uint16_t op2, bool fused, uint32_t instrAddr,
                         uint8_t flagMask, bool condExit) {
    if (fused)                                          { emitThumbBL(e, op, op2, instrAddr); return; }
    if (condExit || thumbIsBCond(op))                   { emitThumbCondBranchExit(e, op, instrAddr); return; }
    if (thumbIsBx(op))                                  { emitThumbBx(e, op); return; }
    if (thumbIsB(op))                                   { emitThumbB(e, op, instrAddr); return; }
    if (thumbIsBLSetup(op))                             { emitThumbBLSetup(e, op, instrAddr); return; }
    if (thumbIsLea(op))                                 { emitThumbLea(e, op, instrAddr); return; }
    if (thumbIsSpAdj(op))                               { emitThumbSpAdj(e, op); return; }
    if (thumbMem(op).ok)                                { emitThumbMem(e, op, instrAddr); return; }
    emitThumbAlu(e, op, flagMask);
}

// ---------------------------------------------------------------- block driver

// The Thumb twin of compileBlock(): same structure, halfword decode, one 4KB page,
// one interpreter step per instruction (a fused BL counts as two, because that is
// what the interpreter does and what the shadow verifier replays).
static bool compileThumbBlock(const Cfg& cfg, const GuestMem& mem, uint32_t pc,
                              uint32_t* out, uint32_t cap, uint32_t& outWords, BlockInfo& info) {
    Em e;
    e.cfg = &cfg; e.gm = &mem; e.thumb = true; e.arm7 = cfg.arm7;
    e.base = out; e.p = out; e.end = out + cap; e.pc = pc;
    e.info = BlockInfo();
    e.chain = cfg.chain; e.chainGen = cfg.chainGen;
    e.info.startPc = pc;
    e.info.thumb = true;

    uint16_t ops[32], ops2[32];
    uint32_t addr[32];
    uint8_t written[32], readF[32], emitMask[32], deleg[32], dterm[32], condExit[32], fused[32];
    uint32_t count = 0, curPc = pc, steps = 0;
    for (; count < cfg.maxBlockInstrs && count < 32; ) {
        const uint16_t op = (uint16_t)mem.u16(curPc);
        bool term = false, isDeleg = false, isFused = false;
        uint16_t op2 = 0;
        uint32_t half = 1;

        if (thumbIsBLSetup(op)) {
            // Fuse with the offset halfword when both fit in this block and page.
            const uint32_t nxt = curPc + 2u;
            const bool room = (count + 1 < cfg.maxBlockInstrs) && (count + 1 < 32) &&
                              (!cfg.maxBlockInstrs || cfg.maxBlockInstrs > 1) &&
                              (((nxt ^ pc) >> 12) == 0);
            const uint16_t cand = room ? (uint16_t)mem.u16(nxt) : 0;
            if (room && thumbIsBLOff(cand) && !thumbIsBlxReg(cand)) { op2 = cand; half = 2; isFused = true; }
            term = true;                                    // paired or standalone: the block ends here
        } else if (!thumbCanTranslate(e, op, &term)) {
            if (!thumbIsDelegate(op, &term)) break;          // refused: the block ends before it
            isDeleg = true;
        }

        ops[count] = op; ops2[count] = op2; addr[count] = curPc;
        deleg[count] = isDeleg ? 1 : 0; dterm[count] = term ? 1 : 0;
        condExit[count] = 0; fused[count] = isFused ? 1 : 0;
        if (isDeleg) { written[count] = F_ALL; readF[count] = F_ALL; }
        else         { written[count] = thumbWrittenFlags(op); readF[count] = thumbReadFlags(op); }

        if (term) {
            const uint32_t nxt = curPc + half * 2u;
            const bool room = (count + 1 < cfg.maxBlockInstrs) && (count + 1 < 32);
            if (!isDeleg && !isFused && thumbIsBCond(op) && room && (((nxt ^ pc) >> 12) == 0)) {
                condExit[count] = 1;                          // both exits; the fall-through continues
                count++; curPc = nxt; steps++;
                continue;
            }
            count++; steps += half;
            break;
        }
        count++; steps += half;
        curPc += half * 2u;
        if (((curPc ^ pc) >> 12) != 0) break;                 // one 4KB page per block
    }
    if (count == 0) return false;
    {   // flag liveness, same pass as the ARM side (Thumb has no conditional ALU ops,
        // so every write here is an always-executed write)
        uint8_t live = F_ALL;
        for (int k = (int)count - 1; k >= 0; k--) {
            emitMask[k] = (uint8_t)(written[k] & live);
            live = (uint8_t)((live & ~written[k]) | readF[k]);
        }
    }

    e.prologue();
    e.info.bodyOffset = (int32_t)e.idx();
    for (uint32_t k = 0; k < count && !e.overflow; k++) {
        e.pc = addr[k];
        const uint16_t op = ops[k];
        if (deleg[k]) {   // the interpreter runs this one instruction
            if (dterm[k]) e.exitDelegateNow(addr[k]); else e.delegateInline(addr[k]);
        }
        else if (condExit[k] || (dterm[k] && !deleg[k] && !fused[k] && thumbIsBCond(op))) {
            thumbEmitOne(e, op, ops2[k], false, addr[k], F_ALL, true);
            // exitPcConst() "ended" the block as far as Em is concerned; the not-taken
            // path has not, in fact, ended.  It continues at the next instruction's
            // code in this block, or - when the branch is the last instruction - it
            // falls out of the loop and exits through the tail exit below.
            e.stop = false;
        }
        else                                                thumbEmitOne(e, op, ops2[k], fused[k] != 0, addr[k],
                                                                        emitMask[k], false);
        if (e.overflow) return false;
    }
    if (!e.stop) e.exitPcConst(pc + 2u * steps);
    if (e.overflow) return false;
    outWords = e.idx();
    info = e.info;
    info.instrCount = steps;
    {   uint32_t nd = 0;
        for (uint32_t k = 0; k < count; k++)
            if (deleg[k]) {
                nd++;
                if (info.delegWordCount < 8) info.delegWords[info.delegWordCount++] = ops[k];
            }
        info.delegInstrs = nd;
    }
    info.cycles = e.cyc;
    info.exitPc = pc + 2u * steps;
    info.endsWithBranch = true;
    return true;
}

// ------------------------------------------------- classification (R6)
// Instructions the emitter does not model, handed to the interpreter inside the
// block instead of ending it (see Em::delegateInline).  `terminates` says whether
// the instruction can move the PC, which is the only thing the *block structure*
// has to know about it - everything else is the interpreter's business, PC
// operands and IO side effects included.
static bool armIsDelegate(const Em& e, uint32_t op, bool* terminates) {
    const uint32_t cls = (op >> 25) & 7;
    if ((op >> 28) == 15) return false;                        // BLX imm / PLD / reserved
    if (isBxOp(op) || isBlxRegOp(op) || isBranchOp(op)) return false;   // translated directly
    if (isPsrOp(op) || isMulOp(op) || isMulLongOp(op)) return false;   // translated or refused
    *terminates = false;
    const bool load = (op >> 20) & 1;
    const bool wb   = (op >> 21) & 1;
    const uint32_t rd = (op >> 12) & 0xF;
    const uint32_t rn = (op >> 16) & 0xF;
    (void)e;

    if (cls == 2 || cls == 3) {                                // LDR/STR/LDRB/STRB
        if (load && rd == 15) *terminates = true;              // jumps
        if (wb && rn == 15) *terminates = true;                // writeback of the PC
        return true;
    }
    if (cls == 0 && !((op >> 25) & 1) && ((op >> 4) & 1) && ((op >> 7) & 1)) {
        // extra load/store, SWP and the other miscellaneous forms
        const bool h = (op >> 5) & 1;                          // H
        const bool sBit = (op >> 6) & 1;                       // S
        if (load && (rd == 15 || (h && sBit && rd == 14))) *terminates = true;
        if (wb && rn == 15) *terminates = true;
        return true;
    }
    if (cls == 4) {                                            // LDM/STM
        if (op & 0x8000u) *terminates = true;                  // register list holds r15
        return true;
    }
    if (cls == 6 || cls == 7 || (op & 0x0F000000u) == 0x0F000000u) {
        *terminates = true;                                    // SVC / coprocessor
        return true;
    }
    return false;
}

static bool armCanTranslate(const Em& e, uint32_t op, bool* terminates) {
    const uint32_t cond = op >> 28;
    const uint32_t cls  = (op >> 25) & 7;
    const uint32_t dp   = (op >> 21) & 0xF;
    const bool s        = (op >> 20) & 1;
    const uint32_t rd   = (op >> 12) & 0xF;
    const uint32_t rn   = (op >> 16) & 0xF;
    const uint32_t rm   = op & 0xF;
    const bool imm      = (op >> 25) & 1;
    const bool regShift = !imm && ((op >> 4) & 1);
    *terminates = false;

    if (cond == 15) return false;                                          // BLX imm / PLD / reserved
    if (isPsrOp(op)) {                                                     // NooDS 0x1xx row
        if (isMrsOp(op)) return rd != 15;                                  // MRS Rd, CPSR
        if (isMsrRegOp(op) || isMsrImmOp(op)) {
            if (op & 0x10000u) return false;                               // mode byte: setCpsr() side effects
            return true;                                                   // f/s/x bytes only
        }
        // TST/TEQ/CMP/CMN share this encoding space in their register-operand form
        // (bits 27..23 = 00010 with bit 25 clear - exactly what isPsrOp() looks at).
        // They are not PSR transfers at all, and were being refused: the block broke
        // in front of every "cmp r1, r3" and the driver had to dispatch it.  That is
        // ~98k refusals in a 60 s run of this ROM, one C++ dispatch each.
        if (isPlainRegDp(op)) return true;                                 // TST/TEQ/CMP/CMN, unshifted
        // Everything else that lands in this pattern - BX/BLX(register), the SPSR
        // forms, QADD, SWP, and the shifted or register-shifted register-operand
        // data-processing shapes - stays refused.  BX is the awkward one: its exit
        // switches the T bit and re-aligns the target to the new step, and the
        // emitted version of that (exitPcRegDyn) is one of the differential sweep's
        // failures.  Refusing is correct and costs 147 dispatched instructions in
        // 60 s, against 98k for the compares.
        return false;
    }
    if (isBranchOp(op) || cls == 5) {                                      // B / BL
        // A branch always ends the block: both paths have their exit emitted
        // (the taken one to the target, the not-taken one to pc+4).  Leaving
        // *terminates false here made every block keep decoding - and emitting -
        // the words *after* the jump as dead code: a 24-instruction block for a
        // two-instruction call stub, three times the compile work, three times
        // the arena, and a self-test walk that ran inside a block it should have
        // bounced out of.
        *terminates = true;
        return true;
    }
    if (isBxOp(op)) { *terminates = true; return rm != 15; }               // BX Rm
    if (isBlxRegOp(op)) { *terminates = true; return !e.arm7 && rm != 15; } // BLX Rm (ARM9 only)
    if (isMulOp(op)) {                                                     // MUL / MLA
        if (e.arm7) return false;                                          // variable cycle count
        if (rd == 15 || rm == 15) return false;
        // (op>>16)&0xF is the MUL destination.  NooDS's mul/muls/mlass have no
        // "Handle pipelining" tail (interpreter_alu.cpp:688), so a write to the PC
        // does *not* flush the pipeline: the stale prefetched instruction still
        // executes.  Not reproducible in a block -> hand it to the interpreter.
        if (!((op >> 21) & 1) && ((op >> 16) & 0xF) == 15) return false;    // mul r15,...
        if (((op >> 8) & 0xF) == 15) return false;                         // Rs
        if (((op >> 21) & 1) && rn == 15) return false;                    // MLA accumulate
        if (((op >> 21) & 1) && s) return false;                           // no such NooDS handler
        return true;
    }
    if (isMulLongOp(op)) {                                                 // UMULL/SMULL/UMLAL/SMLAL
        if (e.arm7) return false;                                          // variable cycle count
        if (rd == 15 || rn == 15) return false;                            // RdLo / RdHi
        if (rm == 15 || ((op >> 8) & 0xF) == 15) return false;             // Rm / Rs
        return true;
    }
    // bits 27-25 = 000 with bit 7 set is the *miscellaneous* group - the extra
    // load/store forms (STRH/LDRH/LDRSB/LDRSH/LDRD/STRD/SWP) - not data
    // processing.  A data-processing register shift has bit 4 set and bit 7
    // clear, so testing bit 7 is what separates the two; without it "strh r14,
    // [r1, #0x84]" is read as "add r14, r1, r0", which writes the base address
    // into the guest's link register and drops the store.
    if (cls == 0 && !imm && ((op >> 4) & 1) && ((op >> 7) & 1)) {           // extra load/store
        *terminates = false;
        return isCompiledMiscTransfer(op);
    }
    if (cls == 2 || cls == 3) {                                            // single data transfer
        // These used to be handed to the interpreter inline - a delegate - and on
        // this ROM that is where the hot loop lives: the guest's inner loop is one
        // load or store per block, and each one paid the thirty register moves of
        // the delegate wrapper plus a full fetch/decode/dispatch, for an access the
        // emitted code can make itself.
        *terminates = false;
        return isCompiledTransfer(op);
    }
    if (cls != 0 && cls != 1) return false;                                // loads/stores/... slice 3
    // ---- data processing
    if (isTestOp(dp) && !s) return false;                                  // unpredictable encoding
    if (rd == 15) {
        if (s || isTestOp(dp)) return false;                               // exception return / SPC
        *terminates = true;                                                // computed branch
    }
    // RRX (shift type ROR with a zero amount) rotates the carry into the operand and
    // writes a new one out of the LSB, and the flag pass here does not model that
    // ordering: it is the sweep's one remaining sequence failure ("movs r0, r13, rrx"
    // two instructions before the mismatch).  Nothing in the refusal census or the
    // compile trace uses it, so it is refused rather than got wrong.
    if (!imm && ((op >> 5) & 3) == 3 && ((op >> 7) & 0x1F) == 0) return false;
    if (regShift) {
        if (rm == 15 || rn == 15) return false;                            // the "+4" PC quirks
        if (((op >> 8) & 0xF) == 15) return false;                         // Rs == PC
        if (s && (dp == 0 || dp == 1 || dp == 5 || dp == 6 || dp == 7 ||
                  dp == 8 || dp == 9 || dp == 12 || dp == 13 || dp == 14 || dp == 15))
            return false;                                                  // shifter carry / XER order
    }
    if (isTestOp(dp) && rn == 15) return false;                            // test op with PC operand
    return true;
}

bool canTranslateOp(const Cfg& cfg, uint32_t op, bool& terminates) {
    Em e;
    e.cfg = &cfg;
    e.arm7 = cfg.arm7;
    return armCanTranslate(e, op, &terminates);
}

// ------------------------------------------------- DP emitter
// Hand one instruction to the interpreter from inside the block.
static void emitDelegate(Em& e, uint32_t instrPc, bool terminates) {
    if (terminates) e.exitDelegateNow(instrPc);
    else            e.delegateInline(instrPc);
}

static void emitDP(Em& e, uint32_t op, uint8_t flagMask, bool thumbRegShift) {
    const uint32_t cond = op >> 28;
    const uint32_t dp   = (op >> 21) & 0xF;
    const bool s        = (op >> 20) & 1;
    const uint32_t rd   = (op >> 12) & 0xF;
    const uint32_t rn   = (op >> 16) & 0xF;
    const uint32_t rm   = op & 0xF;
    const uint32_t rs   = (op >> 8) & 0xF;
    const bool imm      = (op >> 25) & 1;
    const bool regShift = !imm && ((op >> 4) & 1);
    const uint32_t shiftType = (op >> 5) & 3;
    const uint32_t shiftAmt  = (op >> 7) & 0x1F;
    const bool test = isTestOp(dp);
    // ARM charges an extra cycle when the shift amount comes from a register; the
    // Thumb ALU row charges a flat 1 (lslDpT etc. return 1), so that penalty is
    // suppressed for the Thumb reg-shift rows.
    const int instrCycles = (rd == 15 ? 3 : 1) + ((regShift && !thumbRegShift) ? 1 : 0);

    uint32_t skip = UINT32_MAX;
    if (cond < 14) {
        e.cycle(1);                                                        // false condition = 1 cycle
        skip = e.emitCondTest(cond);                                       // skips the body when false
        e.cycle(instrCycles - 1);
    } else {
        e.cycle(instrCycles);
    }

    // Arithmetic destination forms: their C is the carry-out of the add/sub, so it
    // comes from XER; 10/11 (CMP/CMN) are arithmetic tests, 8/9 (TST/TEQ) logical.
    const bool arithOp = (dp >= 2 && dp <= 7) || dp == 10 || dp == 11;
    // operand 2 (always lands in T5)
    if (imm) {
        e.li(T5, immValue(op));
        // immS: C = the last bit rotated out = MSB of the rotated value = IBM bit 0
        e.carryBit = ((op >> 8) & 0xF) ? 0 : -1;
        e.carrySrc = T5;
    } else {
        int src = e.getReg((int)rm, T2);
        if (regShift) {
            const int amt = e.getReg((int)rs, T3);        // LR/PC are not resident either
            if (thumbRegShift) {
                // The Thumb row *is* the flag write, so the shifter's carry is part
                // of the result; T7 survives to the flag tail (this path is always a
                // logical MOVS with rd < 14, where the tail's `res` is the guest
                // register, not T7).
                e.shiftRegCarry(T5, src, (int)shiftType, amt, T7, (flagMask & F_C) != 0);
                // carryBit is in IBM numbering (bit k = value bit 31-k): the
                // candidate was built in bit 31, which is IBM bit 0.
                e.carrySrc = T7; e.carryBit = 0;
            } else {
                e.shiftReg(T5, src, (int)shiftType, amt);
                e.carryBit = -1; e.carrySrc = -1;
            }
        }
        else {
            e.carryBit = e.shiftImm(T5, src, (int)shiftType, (int)shiftAmt);
            e.carrySrc = src;
            // The destination may be the same guest register as the shifted source
            // (eg. "ands r3, r0, r3, lsr #2"), and the ALU write would destroy the
            // carry bit the shifter just produced: park it in a scratch first.
            if (e.carryBit >= 0 && !arithOp) {
                // rlwinm rotates *left*, and rotated bit 31 comes from source bit
                // (31 + SH) & 31, so extracting bit k into the LSB needs SH = k + 1.
                e.w(rlwinm(T3, src, (uint8_t)((e.carryBit + 1) & 31), 31, 31));
                e.carrySrc = T3; e.carryBit = 31;
            }
        }
    }
    int op1 = e.getReg((int)rn, T6);
    // only r0..r13 are resident; r14 (LR) and r15 (PC) are not, so anything that
    // writes them has to go through a scratch and setReg()
    const bool resScratch = (test || rd >= 14);
    int res = resScratch ? T7 : RR((int)rd);
    if (!resScratch && res == op1) {       // "subs r3, r3, ...": V/C need the old op1
        e.w(mr(T6, op1));
        op1 = T6;
    }

    switch (dp) {
    case 0: case 8:  e.w(and_(res, op1, T5)); break;                          // AND / TST
    case 1: case 9:  e.w(xor_(res, op1, T5)); break;                          // EOR / TEQ
    case 4: case 11: e.w(addc(res, op1, T5)); break;                          // ADD / CMN: op1 + op2
    case 2: case 10: e.w(subfc(res, T5, op1)); break;                         // SUB / CMP: op1 - op2
    case 3:          e.w(subfc(res, op1, T5)); break;                         // RSB (op2 - op1)
    case 5: case 6: case 7: {                                               // ADC / SBC / RSC
        // NooDS runs the shifter (which writes C) *before* the handler reads the
        // carry-in, so with a shifting operand the C used by ADC/SBC/RSC is the
        // shifter's carry, not the incoming one.  Same order here.
        if (s && e.carryBit >= 0) e.setCFromBit(e.carrySrc, e.carryBit);
        e.w(rlwinm(T2, RCPSR, 0, 2, 2));                                      // CPSR.C -> XER.CA (IBM bit 2)
        e.w(mtxer(T2));
        if (dp == 5)      e.w(adde(res, op1, T5));
        else if (dp == 6) e.w(subfe(res, T5, op1));
        else              e.w(subfe(res, op1, T5));
        break;
    }
    case 12: e.w(or_(res, op1, T5)); break;                                   // ORR
    case 13: if (res != T5) e.w(mr(res, T5)); break;                          // MOV
    case 14: e.w(andc(res, op1, T5)); break;                                  // BIC
    default: e.w(nor_(res, T5, T5)); break;                                   // MVN
    }

    if (s || test) {
        const uint8_t m = (uint8_t)(test ? (uint8_t)F_ALL : flagMask);
        e.setNZ(res, m);
        if (arithOp) {
            e.setCFromXer(m);
            if (m & F_V) {
                switch (dp) {
                case 4: case 11: case 5: e.setVAdd(res, op1, T5); break;    // ADD/CMN/ADC: op1 + op2
                case 2: case 10: e.setVSub(res, op1, T5); break;            // SUB / CMP: op1 - op2
                case 3:          e.setVSub(res, T5, op1); break;            // RSB: op2 - op1
                case 6:          e.setVSub(res, op1, T5); break;            // SBC
                default:         e.setVSub(res, T5, op1); break;            // RSC: op2 - op1
                }
            }
        } else if ((m & F_C) && e.carryBit >= 0) {
            e.setCFromBit(e.carrySrc, e.carryBit);                          // shifter carry (imm/reg imm-shift)
        }
        e.markCpsr();
    }

    if (rd == 15) {
        e.w(rlwinm(T3, res, 0, 0, 29));                                       // flushPipeline masks to ~3
        e.exitPcReg(T3);
    } else if (!test) {
        e.setReg((int)rd, res);
    }
    e.closeSkip(skip);
}

// ------------------------------------------------- branch emitter
static void emitBranchOrX(Em& e, uint32_t op) {
    const uint32_t cond = op >> 28;
    if (isBranchOp(op)) {                                                   // B / BL
        const uint32_t lk = (op >> 24) & 1;
        int32_t off = (int32_t)(op << 8) >> 6;
        uint32_t target = (e.pc + 8u + (uint32_t)off) & ~3u;
        uint32_t skip = UINT32_MAX;
        if (cond < 14) {
            e.cycle(1);                                  // condition false: 1 cycle, fall through
            skip = e.emitCondTest(cond);
            e.cycle(2);                                  // taken: 3 total
        } else {
            e.cycle(3);
        }
        if (lk) { e.li(T3, e.pc + 4u); e.setReg(14, T3); }                  // BL: LR = instrPC + 4
        e.exitPcConst(target);
        if (skip != UINT32_MAX) {
            e.closeSkip(skip);                           // not taken: leave the block at pc+4
            e.exitPcConst(e.pc + 4u);
        }
        return;
    }
    // BX Rm / BLX Rm
    const uint32_t rm = op & 0xF;
    const bool link = isBlxRegOp(op);
    uint32_t skip = UINT32_MAX;
    if (cond < 14) {
        e.cycle(1);
        skip = e.emitCondTest(cond);
        e.cycle(2);
    } else {
        e.cycle(3);
    }
    int target = e.getReg((int)rm, T5);
    if (link) { e.li(T3, e.pc + 4u); e.setReg(14, T3); }                    // BLX Rm: LR = instrPC + 4
    e.w(rlwinm(T6, target, 5, 26, 26));                                     // (Rm & 1) -> CPSR.T (IBM bit 26)
    e.w(or_(RCPSR, RCPSR, T6));                                             // NooDS bx: cpsr |= bit (T is never cleared)
    e.markCpsr();
    e.exitPcRegDyn(target);
    if (skip != UINT32_MAX) {
        e.closeSkip(skip);
        e.exitPcConst(e.pc + 4u);
    }
}

// "Exit if taken": the taken path leaves the block exactly as a normal branch would
// (chained, or through the driver), and the not-taken path falls through into the
// next instruction *inside this same block*.  The block therefore keeps running
// without a probe, a prologue or a write-back.
static void emitCondBranchExit(Em& e, uint32_t op) {
    const uint32_t lk = (op >> 24) & 1;
    int32_t off = (int32_t)(op << 8) >> 6;
    uint32_t target = (e.pc + 8u + (uint32_t)off) & ~3u;
    e.cycle(1);                                          // condition false: 1 cycle
    uint32_t skip = e.emitCondTest(op >> 28);
    e.cycle(2);                                          // taken: 3 total
    if (lk) { e.li(T3, e.pc + 4u); e.setReg(14, T3); }   // BL: LR = instrPC + 4
    e.exitPcConst(target);
    e.closeSkip(skip);                                   // not taken: carry on inline
}

// MRS Rd,CPSR  /  MSR CPSR_<f|s|x>, Rm|#imm  (NooDS mrsRc / msrRc / msrIc)
static void emitPsr(Em& e, uint32_t op, uint8_t flagMask) {
    (void)flagMask;
    const uint32_t cond = op >> 28;
    const uint32_t rd = (op >> 12) & 0xF;
    const int msrCycles = 1;
    const int mrsCycles = e.arm7 ? 1 : 2;                                   // mrsRc: 2 - arm7
    const bool isMRS = (rd != 15);
    const int cyc = isMRS ? mrsCycles : msrCycles;
    uint32_t skip = UINT32_MAX;
    if (cond < 14) { e.cycle(1); skip = e.emitCondTest(cond); e.cycle(cyc - 1); }
    else e.cycle(cyc);

    if (isMRS) {
        e.setReg((int)rd, RCPSR);
    } else {
        const bool immForm = ((op >> 25) & 1) != 0;
        const uint32_t val = immValue(op);
        if (immForm) e.li(T5, val);                       // NooDS msrIc rotates the immediate
        int msg = immForm ? T5 : e.getReg((int)(op & 0xF), T5);
        for (int lane = 1; lane < 4; lane++) {            // f, s, x  (the c byte is delegated)
            if (!(op & (1u << (16 + lane)))) continue;
            const uint8_t hi = (uint8_t)(31 - (8 * lane + 7));   // PPC bit of the lane's MSB
            const uint8_t lo = (uint8_t)(31 - (8 * lane));       // ... and of its LSB
            e.w(rlwinm(RCPSR, RCPSR, 0, (uint8_t)((lo + 1) & 31), (uint8_t)((hi + 31) & 31)));  // clear lane
            if (immForm && ((val >> (8 * lane)) & 0xFF) == 0) continue;
            e.w(rlwinm(T6, msg, 0, hi, lo));              // CPSR<lane> <- source<lane>
            e.w(or_(RCPSR, RCPSR, T6));
        }
        e.markCpsr();
    }
    e.closeSkip(skip);
}

// Write a value that lives in a scratch into a guest register, with the optional
// N,Z update the S forms of MUL/MLA do.
static void setRegAny(Em& e, uint32_t rd, int scratch, uint8_t flagMask, bool s) {
    if (s) { e.setNZ(scratch, (uint8_t)(flagMask & (F_N | F_Z))); e.markCpsr(); }
    e.setReg((int)rd, scratch);
}

// MUL / MLA (ARM9: 2 cycles, 4 with S; muls sets N,Z only)
static void emitMul(Em& e, uint32_t op, uint8_t flagMask) {
    const uint32_t cond = op >> 28;
    const bool s = (op >> 20) & 1;
    const bool acc = (op >> 21) & 1;
    const uint32_t rd = (op >> 16) & 0xF, rn = (op >> 12) & 0xF;
    const uint32_t rs = (op >> 8) & 0xF, rm = op & 0xF;
    const int cyc = s ? 4 : 2;
    uint32_t skip = UINT32_MAX;
    if (cond < 14) { e.cycle(1); skip = e.emitCondTest(cond); e.cycle(cyc - 1); }
    else e.cycle(cyc);
    const int a = e.getReg((int)rm, T2);
    const int b = e.getReg((int)rs, T3);
    e.w(mullw(T7, a, b));
    if (acc) {
        const int c = e.getReg((int)rn, T4);
        e.w(add(T7, T7, c));
    }
    setRegAny(e, rd, T7, flagMask, s);
    e.closeSkip(skip);
}

// UMULL / SMULL / UMLAL / SMLAL (ARM9 only: ARM7's cycle count depends on the
// multiplier, so the driver hands those to the interpreter like it does for MUL).
// The product is 64-bit: mullw + (mulhwu|mulhw) on PowerPC, and the accumulate
// forms add the RdHi:RdLo pair through the XER carry.
//
// The S bit sets N and Z from the 64-bit result - N is bit 63, Z is the whole
// 64-bit value being zero - and C and V keep whatever they held.  This is the
// behavior the mGBA suite's multiply-long table records for hardware; NooDS's
// interpreter had no flag handling here at all (see the note in NOTES.md), and both
// sides were corrected together so that VERIFY keeps comparing like with like.
static void emitMulLong(Em& e, uint32_t op, uint8_t flagMask) {
    const uint32_t cond = op >> 28;
    const bool s = (op >> 20) & 1, acc = (op >> 21) & 1, uns = (op >> 22) & 1;
    const uint32_t rdHi = (op >> 16) & 0xF, rdLo = (op >> 12) & 0xF;
    const uint32_t rs = (op >> 8) & 0xF, rm = op & 0xF;
    const int cyc = 3;                                        // NooDS: all four return 3 on ARM9
    uint32_t skip = UINT32_MAX;
    if (cond < 14) { e.cycle(1); skip = e.emitCondTest(cond); e.cycle(cyc - 1); }
    else e.cycle(cyc);

    const int a = e.getReg((int)rm, T2);
    const int b = e.getReg((int)rs, T3);
    e.w(mullw(T5, a, b));                                     // low word
    e.w(uns ? mulhwu(T6, a, b) : mulhw(T6, a, b));            // high word
    if (acc) {
        const int lo = e.getReg((int)rdLo, T7);
        const int hi = e.getReg((int)rdHi, T4);
        e.w(addc(T5, T5, lo));                                // XER.CA = carry out of the low word
        e.w(adde(T6, T6, hi));
    }
    if (s) {
        // N is the top of the high word; Z needs the pair OR-ed together, so the
        // zero test runs on the OR and the sign test on the high word itself.
        if (flagMask & F_Z) {
            e.w(or_(T4, T5, T6));
            e.w(cntlzw(T1, T4));
            e.insertBit(RCPSR, T1, 1, 26);
        }
        if (flagMask & F_N) e.insertBit(RCPSR, T6, 0, 0);
        e.markCpsr();
    }
    e.setReg((int)rdLo, T5);
    e.setReg((int)rdHi, T6);
    e.closeSkip(skip);
}

// ------------------------------------------------- block driver
bool compileBlock(const Cfg& cfg, const GuestMem& mem, uint32_t pc, bool thumb,
                  uint32_t* out, uint32_t cap, uint32_t& outWords, BlockInfo& info) {
    Em e;
    e.cfg = &cfg; e.gm = &mem; e.thumb = thumb; e.arm7 = cfg.arm7;
    e.base = out; e.p = out; e.end = out + cap; e.pc = pc;
    e.info = BlockInfo();
    // Chaining is only sound when the probe can see the whole table this block
    // would jump into, i.e. when the process-wide generation has not moved since
    // this compilation started.  The driver re-reads the memory generation
    // immediately before every compileBlock() call, so within one compilation the
    // generation is fixed and the value it hands us is trustworthy.
    e.chain = cfg.chain;
    e.chainGen = cfg.chainGen;

    e.info.startPc = pc;
    e.info.thumb = thumb;
    if (thumb) return compileThumbBlock(cfg, mem, pc, out, cap, outWords, info);

    uint32_t ops[32]; uint8_t written[32], readF[32], emitMask[32];
    uint8_t deleg[32], dterm[32], condExit[32];
    uint32_t count = 0, curPc = pc;
    for (; count < cfg.maxBlockInstrs && count < 32; ) {
        const uint32_t op = mem.u32(curPc);
        bool term = false;
        bool isDeleg = false;
        if (!armCanTranslate(e, op, &term)) {
            if (!armIsDelegate(e, op, &term)) break;
            isDeleg = true;
        }
        ops[count] = op;
        deleg[count] = isDeleg;
        dterm[count] = term;
        condExit[count] = 0;
        if (isDeleg) {
            // The interpreter does whatever this instruction does, flags included.
            written[count] = F_ALL;
            readF[count] = F_ALL;
        } else {
            written[count] = armFlagsWritten(op);
            readF[count] = armFlagsRead(op);
        }
        if (term) {
            // A conditional branch does not have to end the block.  If there is room
            // and the fall-through stays in the same 4KB page (the page pointer the
            // block was compiled against is what makes a fetch safe), the taken path
            // exits and the fall-through becomes the next instruction of *this* block.
            // Only non-delegated branches qualify: a delegated instruction leaves
            // through the interpreter and has to end the block.
            const uint32_t nxt = curPc + 4;
            const bool room = (count + 1 < cfg.maxBlockInstrs) && (count + 1 < 32);
            if (!isDeleg && isCondBranchCont(op) && room && ((nxt ^ pc) >> 12) == 0) {
                condExit[count] = 1;
                count++;
                curPc = nxt;
                continue;
            }
            count++;
            break;
        }
        count++;
        curPc += 4;
        if (((curPc ^ pc) >> 12) != 0) break;                 // one 4KB page per block
    }
    if (count == 0) return false;
    {   // flag liveness: at the exit every flag is observable, earlier writers may
        // only materialise what someone downstream still reads.
        uint8_t live = F_ALL;
        for (int k = (int)count - 1; k >= 0; k--) {
            emitMask[k] = (uint8_t)(written[k] & live);
            // A conditional instruction may not execute at all, so it cannot make an
            // earlier writer's flags dead - only an always-executed write can.
            const uint8_t w = ((ops[k] >> 28) >= 14) ? written[k] : 0;
            live = (uint8_t)((live & ~w) | readF[k]);
        }
    }

    e.prologue();
    e.info.bodyOffset = (int32_t)e.idx();
    for (uint32_t k = 0; k < count && !e.overflow; k++) {
        e.pc = pc + 4 * k;
        const uint32_t op = ops[k];
        if (deleg[k])                                       emitDelegate(e, pc + 4 * k, dterm[k]);
        else if (condExit[k])                             { emitCondBranchExit(e, op);
                                                            // exitPcConst() ends the block
                                                            // as far as Em is concerned;
                                                            // the not-taken path means it
                                                            // has not, in fact, ended.
                                                            e.stop = false; }
        else if (isBranchOp(op) || isBxOp(op) || isBlxRegOp(op)) emitBranchOrX(e, op);
        // Mirrors the gate: inside isPsrOp()'s pattern the only things that are
        // really PSR transfers are MRS and the MSR forms; the register-operand test
        // ops that also match go down the data-processing path.  Without this the
        // block emitted an MRS (CPSR into the destination register) for every
        // "cmp rX, rY" - measured directly: after "cmp r0, r1" the guest's r0 held
        // the CPSR and the flags were stale.
        else if (isPsrOp(op)) {
            if (isPlainRegDp(op)) emitDP(e, op, emitMask[k]);
            else                  emitPsr(e, op, F_ALL);
        }
        else if (isMulOp(op))                               emitMul(e, op, emitMask[k]);
        else if (isMulLongOp(op))                           emitMulLong(e, op, emitMask[k]);
        else if (((op >> 25) & 7) == 2 || ((op >> 25) & 7) == 3) emitTransfer(e, op);
        else if (((op >> 25) & 7) == 0 && ((op >> 4) & 1) && ((op >> 7) & 1) &&
                 ((op >> 5) & 1 || (op >> 6) & 1))               emitMiscTransfer(e, op);
        else                                                emitDP(e, op, emitMask[k]);
        if (e.overflow) return false;
    }
    if (!e.stop) e.exitPcConst(pc + 4 * count);               // fall-through exit
    if (e.overflow) return false;
    outWords = e.idx();
    info = e.info;                       // startPc/thumb/bodyOffset/warmOffset/...
    info.instrCount = count;
    // Count the words handed to the interpreter.  With loads and stores delegated
    // this was half of every block's instructions on this ROM, and each delegate
    // costs a full interpreter call plus the register-file marshalling - so the
    // figure is worth reporting per frame, next to the instruction count.
    {   uint32_t nd = 0;
        for (uint32_t k = 0; k < count; k++)
            if (deleg[k]) {
                nd++;
                if (info.delegWordCount < 8) info.delegWords[info.delegWordCount++] = ops[k];
            }
        info.delegInstrs = nd;
    }
    info.cycles = e.cyc;
    info.exitPc = pc + 4 * count;
    info.endsWithBranch = true;
    return true;
}

} // namespace Jit3
