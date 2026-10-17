// jit3_test.cpp -- differential test: jit3's emitted PowerPC blocks vs. an
// executable transcription of the NooDS ARM handlers.
//
// This file is *cross compiled for PowerPC and run under qemu-ppc*: the emitted
// words are placed in an RWX page and called natively, so the block runs on a real
// PowerPC core with real XER/CR semantics and real big-endian loads.  That is the
// closest thing to the Wii that exists without hardware.
//
//   tools/host/run_tests_ppc.sh
#ifndef JIT3_NO_MAIN
#define JIT3_NO_MAIN
#endif
#include "armjit.hpp"          // NooDS-Wii/jit3
#include "host_env.hpp"
#include "arm_ref.hpp"
#include "ppc_dis.hpp"

#include <cstddef>
#include <cstdio>
#include <string>
#include <cstring>
#include <cstdlib>

using namespace jit3host;
using namespace Jit3;
using jit3ref::ArmRef;

static Env env;
static Cfg cfg;
static const uint32_t PC = GUEST_BASE + 0x100;

static int checks = 0, failures = 0, skipped = 0;
static bool verboseSuite = false;
// failure histogram keyed by the encoding shape, so triage shows patterns
struct Sig { uint32_t key; int count; uint32_t example; };
static Sig sigs[512];
static int sigCount = 0;
static void noteSig(uint32_t key, uint32_t op) {
    for (int i = 0; i < sigCount; i++) if (sigs[i].key == key) { sigs[i].count++; return; }
    if (sigCount < 512) {
        sigs[sigCount].key = key; sigs[sigCount].count = 1; sigs[sigCount].example = op; sigCount++;
        std::fprintf(stderr, "  new failure class, first op 0x%08X\n", op);
    }
}
static void dumpSigs() {
    for (int i = 0; i < sigCount; i++)
        for (int j = i + 1; j < sigCount; j++)
            if (sigs[j].count > sigs[i].count) { Sig t = sigs[i]; sigs[i] = sigs[j]; sigs[j] = t; }
    std::fprintf(stderr, "failure signatures (klass | dp | S | imm | regshift | cond):\n");
    for (int i = 0; i < sigCount && i < 26; i++) {
        uint32_t k = sigs[i].key;
        std::fprintf(stderr, "  %6d  klass=%u dp=%2u S=%u %s %s cond=%X  e.g. op=%08X\n", sigs[i].count,
                     k >> 12, (k >> 4) & 0xF, (k >> 3) & 1, (k >> 2) & 1 ? "imm" : "reg",
                     (k >> 1) & 1 ? "regshift" : "immshift", k & 1 ? 0xE : 0x0, sigs[i].example);
    }
}
static uint32_t curOp = 0;
static const uint32_t* curWords = nullptr;
static uint32_t curCount = 0;

static uint32_t gmRead32(void*, uint32_t a) { return env.rd32(a); }
static uint32_t gmRead16(void*, uint32_t a) { return env.rd16(a); }

static Cfg makeCfg() {
    Cfg c;
    c.arm7 = false;
    c.offRegsUsr  = (int32_t)offsetof(HostCpu, registersUsr);
    c.offRegsPtr  = (int32_t)offsetof(HostCpu, registers);
    c.offCpsr     = (int32_t)offsetof(HostCpu, cpsr);
    c.offHalted   = (int32_t)offsetof(HostCpu, halted);
    c.maxBlockInstrs = 8;
    c.maxBlockWords  = 1200;
    return c;
}

struct Values { uint32_t r[16]; uint32_t cpsr; };

// which host register slot holds guest rN after a run (r8..r12 live in the bank)
static uint32_t gotReg(int i) {
    if (i >= 8 && i <= 12) return env.cpu.registersFiq[i - 8];
    return env.cpu.registersUsr[i];
}

static void report(const char* what, uint32_t pc, const ArmRef& ref, int refCycles,
                   const BlockResult& got) {
    failures++;
    {
        const uint32_t cls = (curOp >> 25) & 7;
        const uint32_t dp = (curOp >> 21) & 0xF;
        const bool sbit = (curOp >> 20) & 1, imm = (curOp >> 25) & 1, rs = (curOp >> 4) & 1;
        noteSig((cls << 12) | (dp << 4) | (sbit << 3) | (imm << 2) | (rs << 1) | ((curOp >> 28) >= 14), curOp);
    }
    if (failures > 3) return;
    std::fprintf(stderr, "FAIL %s op=0x%08X pc=0x%08X\n", what, curOp, pc);
    std::fprintf(stderr, "  cycles got %u want %d   nextPc got 0x%08X want 0x%08X\n",
                 got.cycles, refCycles, got.nextPc, ref.regs[15] - 4);
    for (int i = 0; i < 16; i++)
        if (gotReg(i) != ref.regs[i])
            std::fprintf(stderr, "  r%-2d got 0x%08X want 0x%08X\n", i, gotReg(i), ref.regs[i]);
    if (env.cpu.cpsr != ref.cpsr)
        std::fprintf(stderr, "  cpsr got 0x%08X want 0x%08X\n", env.cpu.cpsr, ref.cpsr);
    std::fprintf(stderr, "  emitted block (%u words):\n", curCount);
    for (uint32_t i = 0; i < curCount; i++) {
        char d[128];
        ppcdis::disasm(curWords[i], d, sizeof d);
        std::fprintf(stderr, "    %3u %04X: %08X  %s\n", i, i * 4, curWords[i], d);
    }
}

// Compile a one-instruction block for `op` at PC and run it on the real core,
// then diff against the reference model.
static uint32_t oneOp = 0xFFFFFFFFu;         // -one <hex>: trace just this encoding
static bool traceOne = false;

static bool tryOne(uint32_t op, const Values& v) {
    if (oneOp != 0xFFFFFFFFu && op != oneOp) return true;
    if (traceOne) {
        const uint32_t cls = (op >> 25) & 7, dp = (op >> 21) & 0xF;
        std::fprintf(stderr, "op=%08X cond=%X cls=%u dp=%u S=%u I=%u rd=%u rn=%u rm=%u rs=%u "
                             "shType=%u shAmt=%u regShift=%u\n", op, op >> 28, cls, dp,
                     (op >> 20) & 1, (op >> 25) & 1, (op >> 12) & 0xF, (op >> 16) & 0xF,
                     op & 0xF, (op >> 8) & 0xF, (op >> 5) & 3, (op >> 7) & 0x1F, (op >> 4) & 1);
        std::fprintf(stderr, "   cpsr=%08X r=", v.cpsr);
        for (int i = 0; i < 16; i++) std::fprintf(stderr, " %08X", v.r[i]);
        std::fprintf(stderr, "\n");
    }
    ArmRef ref;
    ref.ram = env.ram;
    ref.base = GUEST_BASE;
    ref.arm7 = cfg.arm7;
    for (int i = 0; i < 16; i++) ref.regs[i] = v.r[i];
    ref.cpsr = v.cpsr;
    env.wr32(PC, op);
    env.wr32(PC + 4, 0xE1A00000u);            // "mov r0,r0" filler after the instruction

    const int refCycles = ref.step(PC);
    if (refCycles < 0) { skipped++; return true; }

    bool term = false;
    if (!canTranslateOp(cfg, op, term)) { skipped++; return true; }

    uint32_t words[1200]; uint32_t n = 0;
    BlockInfo info;
    Cfg one = cfg;
    one.maxBlockInstrs = 1;                   // exactly this instruction
    GuestMem gm;
    gm.ctx = nullptr;
    gm.read32 = gmRead32;
    gm.read16 = gmRead16;
    if (!compileBlock(one, gm, PC, false, words, 1200, n, info)) { skipped++; return true; }
    if (info.startPc != PC || info.instrCount != 1 || info.exitPc != PC + 4) {
        failures++;
        if (failures <= 12)
            std::fprintf(stderr, "BAD BLOCK INFO op=0x%08X startPc=%X instrs=%u exitPc=%X words=%u\n",
                         op, info.startPc, info.instrCount, info.exitPc, n);
        return false;
    }

    for (int i = 0; i < 16; i++) env.cpu.registersUsr[i] = v.r[i];
    for (int i = 8; i <= 12; i++) env.cpu.registersFiq[i - 8] = v.r[i];
    env.cpu.cpsr = v.cpsr;
    env.cpu.registersUsr[15] = PC + 4;        // boundary convention
    curOp = op; curWords = words; curCount = n;
    BlockResult got = runBlock(env, words, n);
    checks++;

    bool ok = got.cycles == (uint32_t)refCycles;
    ok = ok && got.nextPc == ref.regs[15] - 4;
    for (int i = 0; i < 16 && ok; i++) ok = gotReg(i) == ref.regs[i];
    ok = ok && env.cpu.cpsr == ref.cpsr;
    if (!ok) { report("single", PC, ref, refCycles, got);
               if (traceOne) std::fprintf(stderr, "   ref cpsr=%08X (N=%u Z=%u C=%u V=%u)\n",
                   ref.cpsr, ref.cpsr >> 31, (ref.cpsr >> 30) & 1, (ref.cpsr >> 29) & 1, (ref.cpsr >> 28) & 1); }
    else if (traceOne) std::fprintf(stderr, "  ok\n");
    return ok;
}

// ------------------------------------------------------------------ generators
static uint32_t rngState = 0x12345678u;
static uint32_t rnd() { rngState = rngState * 1103515245u + 12345u; return rngState >> 8; }

static const uint32_t kInteresting[10] = {
    0x00000000u, 0x00000001u, 0x00000002u, 0x7FFFFFFFu, 0x80000000u,
    0xFFFFFFFFu, 0xFFFFFFFEu, 0x0000FFFFu, 0x12345678u, 0xDEADBEEFu
};

static Values makeValues() {
    Values v;
    for (int i = 0; i < 16; i++)
        v.r[i] = (rnd() & 3) ? kInteresting[rnd() % 10] : rnd();
    // keep r15 meaningful: PC + 8 boundary value stored as PC + 4 between instructions
    v.r[15] = PC + 4;
    v.cpsr = 0x20000010u | (rnd() & 0xE0000000u);        // C forced set (RRX probes)
    return v;
}

static uint32_t encDP(uint32_t cond, uint32_t dp, bool s, uint32_t rd, uint32_t rn,
                      uint32_t op2) { return (cond << 28) | (dp << 21) | (s << 20) | (rn << 16) | (rd << 12) | op2; }
static uint32_t encImm(uint32_t rot, uint32_t imm8) { return (1u << 25) | ((rot & 0xF) << 8) | (imm8 & 0xFF); }
static uint32_t encSft(uint32_t type, uint32_t amt, uint32_t rm) { return (type << 5) | (amt << 7) | rm; }
static uint32_t encRegSft(uint32_t type, uint32_t rs, uint32_t rm) { return (rs << 8) | (type << 5) | (1u << 4) | rm; }

static void runDPSweep() {
    const uint32_t conds[6] = { 0xE, 0x0, 0x1, 0x8, 0xC, 0xD };
    const uint32_t regs[8] = { 0, 1, 2, 5, 8, 12, 13, 14 };
    for (uint32_t cond : conds)
    for (uint32_t dp = 0; dp < 16; dp++)
    for (uint32_t sv = 0; sv < 2; sv++) {
        if (dp >= 8 && dp <= 11 && !sv) continue;                 // test ops are S-only
        // immediate forms: every rotate, a few immediates
        for (uint32_t rot = 0; rot < 16; rot += (dp & 1) ? 1 : 3)
        for (uint32_t imm8 = 0; imm8 < 256; imm8 += (dp & 2) ? 1 : 85) {
            const uint32_t rd = (dp >= 8 && dp <= 11) ? 0 : regs[rnd() % 8];
            const uint32_t rn = regs[rnd() % 8];
            tryOne(encDP(cond, dp, sv != 0, rd, rn, encImm(rot, imm8)), makeValues());
        }
        // register-shift forms (immediate shifts are covered by the rlwinm path)
        for (uint32_t type = 0; type < 4; type++)
        for (uint32_t amt = 0; amt < 32; amt += 7) {
            const uint32_t rd = (dp >= 8 && dp <= 11) ? 0 : regs[rnd() % 8];
            const uint32_t rn = regs[rnd() % 8];
            const uint32_t rm = regs[rnd() % 8];
            tryOne(encDP(cond, dp, sv != 0, rd, rn, encSft(type, amt, rm)), makeValues());
        }
        // shift-by-register forms (variable shifts, +1 cycle)
        for (uint32_t type = 0; type < 4; type++) {
            const uint32_t rd = (dp >= 8 && dp <= 11) ? 0 : regs[rnd() % 8];
            const uint32_t rn = regs[rnd() % 8];
            const uint32_t rm = regs[rnd() % 8];
            const uint32_t rs = regs[rnd() % 8];
            for (int k = 0; k < 3; k++)
                tryOne(encDP(cond, dp, sv != 0, rd, rn, encRegSft(type, rs, rm)), makeValues());
        }
    }
}

static void runBranchSweep() {
    const uint32_t conds[6] = { 0xE, 0x0, 0x1, 0x8, 0xC, 0xD };
    for (uint32_t cond : conds)
    for (uint32_t lk = 0; lk < 2; lk++)
    for (int32_t off = -8; off <= 8; off += 2)
        tryOne((cond << 28) | (0x5u << 25) | (lk << 24) | ((uint32_t)(off / 4) & 0xFFFFFF), makeValues());
    // BX / BLX register, various registers including PC
    for (uint32_t rm = 0; rm < 16; rm += 3)
    for (uint32_t lk = 0; lk < 2; lk++)
        tryOne(0xE0000000u | 0x012FFF10u | (lk ? 0x20u : 0u) | rm, makeValues());
}

static void runPsrSweep() {
    for (uint32_t rd = 0; rd < 16; rd += 3)
        tryOne(0xE10F0000u | (rd << 12), makeValues());           // MRS Rd,CPSR
    for (uint32_t fields = 0; fields < 16; fields++) {
        if (fields & 1) continue;                                 // c byte is delegated
        for (uint32_t rm = 0; rm < 16; rm += 5)
            tryOne(0xE120F000u | (fields << 16) | (rm), makeValues());
        for (uint32_t rot = 0; rot < 16; rot += 5)
            tryOne(0xE320F000u | (fields << 16) | encImm(rot, 0xA5), makeValues());
    }
}

static void runMulSweep() {
    for (uint32_t s = 0; s < 2; s++)
    for (uint32_t acc = 0; acc < 2; acc++)
    for (uint32_t rd = 0; rd < 16; rd += 5)
    for (uint32_t rn = 0; rn < 16; rn += 7)
        for (int k = 0; k < 4; k++)
            tryOne(0xE0000090u | (acc << 21) | (s << 20) | (rd << 16) | (rn << 12) |
                   (((rnd() % 15)) << 8) | (rnd() % 15), makeValues());
}


// ------------------------------------------------------------- sequence tests
// A block of n instructions exercises what a single-instruction block cannot:
// fall-through decoding, the flag liveness pass, the conditional-skip patching
// across several instructions, and the cycle total.
static uint32_t seqOps[8];

// Print the reference's per-instruction state - used to locate which instruction
// of a failing sequence is the culprit.
static void traceSeqRef(const uint32_t* ops, int n, const Values& v) {
    ArmRef r;
    r.ram = env.ram; r.base = GUEST_BASE; r.arm7 = cfg.arm7;
    for (int i = 0; i < 16; i++) r.regs[i] = v.r[i];
    r.cpsr = v.cpsr;
    std::fprintf(stderr, "  ref init: cpsr=%08X r=", v.cpsr);
    for (int i = 0; i < 16; i++) std::fprintf(stderr, " %08X", v.r[i]);
    std::fprintf(stderr, "\n");
    uint32_t cur = PC;
    for (int k = 0; k < n; k++) {
        const int c = r.step(cur);
        std::fprintf(stderr, "    step %d pc=%08X op=%08X cyc=%d cpsr=%08X (N%u Z%u C%u V%u) r15=%08X\n",
                     k, cur, ops[k], c, r.cpsr, r.cpsr >> 31, (r.cpsr >> 30) & 1,
                     (r.cpsr >> 29) & 1, (r.cpsr >> 28) & 1, r.regs[15]);
        if (c < 0 || r.regs[15] != cur + 8) break;
        cur += 4;
    }
}

static bool trySeq(const uint32_t* ops, int n, const Values& v) {
    ArmRef ref;
    ref.ram = env.ram; ref.base = GUEST_BASE; ref.arm7 = cfg.arm7;
    for (int i = 0; i < 16; i++) ref.regs[i] = v.r[i];
    ref.cpsr = v.cpsr;
    for (int k = 0; k < n; k++) env.wr32(PC + 4 * k, ops[k]);
    env.wr32(PC + 4 * n, 0xE1A00000u);            // filler the block must not reach

    // Walk the reference the way the interpreter would: a transfer of control ends
    // the block, so the walk stops there instead of running off linearly.  The
    // handler leaves regs[15] either at the mid-handler value (pc + 8, no transfer)
    // or at the flushPipeline value ((target & ~3) + 4, transfer).
    int refCycles = 0, steps = 0;
    uint32_t cur = PC;
    for (int k = 0; k < n; k++) {
        const int c = ref.step(cur);
        if (c < 0) { skipped++; return true; }
        refCycles += c;
        steps++;
        if (ref.regs[15] != cur + 8) break;            // control transferred
        cur += 4;
    }

    uint32_t words[1200]; uint32_t nw = 0;
    BlockInfo info;
    Cfg c = cfg;
    c.maxBlockInstrs = (uint32_t)n;
    GuestMem gm; gm.ctx = nullptr; gm.read32 = gmRead32; gm.read16 = gmRead16;
    if (!compileBlock(c, gm, PC, false, words, 1200, nw, info)) { skipped++; return true; }
    // How many instructions the emitter's scan must have taken: it walks linearly
    // until an untranslatable op or a *terminating* op (BX/BLX-reg/PC write) - a
    // conditional branch is not terminating, it keeps its not-taken continuation.
    int expected = 0;
    for (int k = 0; k < n; k++) {
        bool term = false;
        if (!canTranslateOp(cfg, ops[k], term)) break;
        expected++;
        if (term) {
            // The emitter carries on past a conditional B/BL: the taken path leaves
            // the block, the not-taken path stays.  It only stops there if there is
            // nothing left to continue with (the generator's length, or the 4 KB
            // page - not modelled here, the test's ops never cross one).
            const bool keepsGoing = (k + 1 < n) &&
                                    (ops[k] & 0x0E000000u) == 0x0A000000u && (ops[k] >> 28) < 14;
            if (!keepsGoing) break;
        }
    }
    if (info.startPc != PC || info.instrCount != (uint32_t)expected) {
        failures++;
        if (failures <= 12)
            std::fprintf(stderr, "BAD SEQ BLOCK INFO n=%d steps=%d want=%d startPc=%X instrs=%u exitPc=%X words=%u\n",
                         n, steps, expected, info.startPc, info.instrCount, info.exitPc, nw);
        return false;
    }

    for (int i = 0; i < 16; i++) env.cpu.registersUsr[i] = v.r[i];
    for (int i = 8; i <= 12; i++) env.cpu.registersFiq[i - 8] = v.r[i];
    env.cpu.cpsr = v.cpsr;
    env.cpu.registersUsr[15] = PC + 4;
    for (int k = 0; k < n; k++) seqOps[k] = ops[k];
    curOp = ops[n - 1]; curWords = words; curCount = nw;
    BlockResult got = runBlock(env, words, nw);
    checks++;

    bool ok = got.cycles == (uint32_t)refCycles;
    ok = ok && got.nextPc == ref.regs[15] - 4;
    for (int i = 0; i < 16 && ok; i++) ok = gotReg(i) == ref.regs[i];
    ok = ok && env.cpu.cpsr == ref.cpsr;
    if (!ok) {
        if (failures <= 6) traceSeqRef(ops, n, v);
        std::fprintf(stderr, "FAIL seq n=%d [", n);
        for (int k = 0; k < n; k++) std::fprintf(stderr, " %08X", ops[k]);
        std::fprintf(stderr, " ]\n");
        failures++;                          // report() already counts; keep the detail local
        std::fprintf(stderr, "  cycles got %u want %d   nextPc got 0x%08X want 0x%08X\n",
                     got.cycles, refCycles, got.nextPc, ref.regs[15] - 4);
        for (int i = 0; i < 16; i++)
            if (gotReg(i) != ref.regs[i])
                std::fprintf(stderr, "  r%-2d got 0x%08X want 0x%08X\n", i, gotReg(i), ref.regs[i]);
        if (env.cpu.cpsr != ref.cpsr)
            std::fprintf(stderr, "  cpsr got 0x%08X want 0x%08X\n", env.cpu.cpsr, ref.cpsr);
        for (uint32_t i = 0; i < nw; i++) {
            char d[128];
            ppcdis::disasm(curWords[i], d, sizeof d);
            std::fprintf(stderr, "    %3u %04X: %08X  %s\n", i, i * 4, curWords[i], d);
        }
        return false;
    }
    return true;
}

static uint32_t rndReg() {
    static const uint32_t rp[8] = { 0, 1, 2, 5, 8, 12, 13, 14 };
    return rp[rnd() % 8];
}

// A random translatable instruction; `allowTerm` permits B/BL/BX/BLX as the last
// instruction of a block.
static uint32_t genOp(bool allowTerm) {
    static const uint32_t conds[6] = { 0xE, 0x0, 0x1, 0x8, 0xC, 0xD };
    for (int tries = 0; tries < 24; tries++) {
        const uint32_t cond = conds[rnd() % 6];
        uint32_t op;
        switch (rnd() % 5) {
        case 0:                                   // test op (TST/TEQ/CMP/CMN)
            op = encDP(cond, 8 + rnd() % 4, true, 0, rndReg(), encImm(rnd() % 16, rnd() % 256));
            break;
        case 1:                                   // immediate data processing
            op = encDP(cond, rnd() % 16, rnd() % 2, rndReg(), rndReg(), encImm(rnd() % 16, rnd() % 256));
            break;
        case 2:                                   // shift by immediate
            op = encDP(cond, rnd() % 16, rnd() % 2, rndReg(), rndReg(), encSft(rnd() % 4, rnd() % 32, rndReg()));
            break;
        case 3:                                   // shift by register
            op = encDP(cond, rnd() % 16, rnd() % 2, rndReg(), rndReg(), encRegSft(rnd() % 4, rndReg(), rndReg()));
            break;
        default:                                  // transfer of control
            if (!allowTerm) continue;
            if (rnd() % 2) {
                const uint32_t off = (uint32_t)((int32_t)(rnd() % 17) - 8);
                op = (cond << 28) | (0x5u << 25) | ((rnd() % 2) << 24) | (off & 0xFFFFFFu);
            } else {
                op = (cond << 28) | 0x012FFF10u | ((rnd() % 2) ? 0x20u : 0u) | rndReg();   // BLX only on ARM9
            }
            break;
        }
        bool term = false;
        if (!canTranslateOp(cfg, op, term)) continue;
        if (term && !allowTerm) continue;
        return op;
    }
    return 0xE1A00000u;                            // mov r0,r0
}

static void runSeqSweep() {
    for (int iter = 0; iter < 3000; iter++) {
        const int n = 2 + (int)(rnd() % 7);
        uint32_t ops[8];
        bool ok = true;
        for (int k = 0; k < n; k++) {
            ops[k] = genOp(k == n - 1);
            bool term = false;
            if (!canTranslateOp(cfg, ops[k], term) || (term && k != n - 1)) { ok = false; break; }
        }
        if (ok) { for (int k = 0; k < n; k++) seqOps[k] = ops[k]; trySeq(seqOps, n, makeValues()); }
    }
}

// Hand-picked interaction patterns: flag producers feeding consumers, shifter
// carries feeding ADC/SBC, conditionals on the previous instruction's flags.
static void runSeqTargeted() {
    static const uint32_t cases[][2][8] = {
        // adds r0,r1,r2 ; adc r3,r4,r5 ; sbcs r6,r0,r3
        {{0xE0910002u, 0xE0A43005u, 0xE0D06003u}},
        // movs r0,r1,lsr #1 ; adc r2,r3,r4  (shifter carry is ADC's carry-in)
        {{0xE1B000A1u, 0xE0A23004u}},
        // cmp r0,r1 ; movgt r2,#7 ; movle r3,#9
        {{0xE1500001u, 0xC3A02007u, 0xD3A03009u}},
        // tst r0,#1 ; addeq r2,r2,r2 ; adc r3,r3,r3   (TST must not touch C)
        {{0xE3100001u, 0x00A22002u, 0xE0A33003u}},
        // cmn r0,#0 ; rsbs r1,r0,#0 ; adc r2,r2,r2
        {{0xE3700000u, 0xE2700000u, 0xE0A22002u}},
        // movs r0,r0 ; bne +8 ; add r1,r1,#1
        {{0xE1B00000u, 0x1A000000u, 0xE2811001u}},
        // msr cpsr_f,r0 ; adc r1,r2,r3 ; mrs r4,cpsr
        {{0xE128F000u, 0xE0A13002u, 0xE10F4000u}},
        // tst r0,r1,lsl r2 ; adc r3,r3,r3
        {{0xE1100211u, 0xE0A33003u}},
        // muls r0,r1,r2 ; addcs r3,r4,#1 ; mla r5,r6,r7,r0
        {{0xE0100291u, 0x22834001u, 0xE0260791u}},
        // sub r0,r1,r2 ; subs r1,r0,r1 ; rsc r2,r3,r4 ; rscs r5,r6,r7
        {{0xE0410002u, 0xE0510001u, 0xE0632004u, 0xE0775006u}},
        // eors r0,r0,r0 (Z=1) ; bne -4 ; mov r1,#1
        {{0xE0200000u, 0x1AFFFFFEu, 0xE3A01001u}},
        // movs r0,#0x80000000 ; adc r1,r1,r1 ; adcs r2,r2,r2 ; mov r3,r3
        {{0xE3B00001u, 0xE0A11001u, 0xE0B22002u, 0xE1A03003u}},
    };
    for (auto& c : cases) {
        const uint32_t* seq = c[0];
        int n = 0;
        while (n < 8 && seq[n] != 0) n++;
        for (int variant = 0; variant < 6; variant++) {
            for (int k = 0; k < n; k++) seqOps[k] = seq[k];
            trySeq(seqOps, n, makeValues());
        }
    }
}


// ---------------------------------------------------------------------------
// -suite <file>: vectors derived from the mGBA test suite's own tables
// (tools/host/gen_suite_vectors.py).  Unlike the random sweeps these are hardware
// expectations, checked in the shape the suite checks them: one instruction, given
// registers and CPSR in, then the named registers and the whole CPSR out.
//
// Each row carries a core tag:
//   any  - architectural, so the ARM9 path (the one jit3 compiles) is compared in
//          full: registers and CPSR.
//   arm7 - the expectation is ARM7TDMI-specific.  The multiply-long table's C flag
//          is the ARM7TDMI multiplier's carry-out; nothing in a GBA suite speaks
//          for the DS's ARM946E-S, so these rows are run twice:
//            * on the ARM9 path for the result registers and N/Z only (C masked),
//              since that is the code jit3 actually executes them with;
//            * on the ARM7 path in full - and there jit3 refuses long multiplies
//              (variable cycle count) and the interpreter runs them, so the
//              interpreter's verdict is the shipped one and fails this run.
//
// Line: core|name|op|r0|r1|r2|r3|r8|r9|cpsr_in|checks|note
// ---------------------------------------------------------------------------
static bool canCompile(const Cfg& c, uint32_t op, bool* delegates) {
    bool term = false;
    if (!canTranslateOp(c, op, term)) return false;
    uint32_t words[1200]; uint32_t n = 0; BlockInfo info;
    Cfg one = c; one.maxBlockInstrs = 1;
    GuestMem gm; gm.ctx = nullptr; gm.read32 = gmRead32; gm.read16 = gmRead16;
    if (!compileBlock(one, gm, PC, false, words, 1200, n, info)) return false;
    if (delegates) *delegates = info.delegInstrs != 0;
    return true;
}

static bool runSuiteVector(const Cfg& c, const Values& v, uint32_t op, uint32_t* outRegs, uint32_t* outCpsr) {
    bool term = false;
    if (!canTranslateOp(c, op, term)) return false;
    env.wr32(PC, op);
    env.wr32(PC + 4, 0xE1A00000u);
    uint32_t words[1200]; uint32_t n = 0; BlockInfo info;
    Cfg one = c; one.maxBlockInstrs = 1;
    GuestMem gm; gm.ctx = nullptr; gm.read32 = gmRead32; gm.read16 = gmRead16;
    if (!compileBlock(one, gm, PC, false, words, 1200, n, info) || info.delegInstrs) return false;
    for (int i = 0; i < 16; i++) {
        env.cpu.registersUsr[i] = v.r[i];
        if (i >= 8 && i <= 12) env.cpu.registersFiq[i - 8] = v.r[i];
    }
    env.cpu.cpsr = v.cpsr;
    env.cpu.registersUsr[15] = PC + 4;
    curOp = op; curWords = words; curCount = n;
    runBlock(env, words, n);
    for (int i = 0; i < 16; i++) outRegs[i] = gotReg(i);
    *outCpsr = env.cpu.cpsr;
    return true;
}

static int runSuite(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return 1000; }
    char line[512];
    int jitFail = 0, delegFail = 0, interpFail = 0, maskedRows = 0;
    int wasSkipped = 0, total = 0, arm7Rows = 0, shown = 0;
    std::fprintf(stderr, "  %-30s %-8s %-7s %-7s %s\n", "case", "op", "jit", "interp", "note");
    while (std::fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* nl = std::strchr(line, '\n'); if (nl) *nl = 0;
        char* field[12] = {0}; int nf = 0; char* q = line;
        for (; nf < 12 && q; nf++) { field[nf] = q; char* bar = std::strchr(q, '|');
                                     if (bar) { *bar = 0; q = bar + 1; } else q = nullptr; }
        if (nf < 11) continue;
        total++;
        const bool arm7Row = !std::strcmp(field[0], "arm7");
        if (arm7Row) arm7Rows++;
        const char* name = field[1];
        const uint32_t op = (uint32_t)std::strtoul(field[2], nullptr, 16);
        Values v{};
        v.r[0] = (uint32_t)std::strtoul(field[3], nullptr, 16);
        v.r[1] = (uint32_t)std::strtoul(field[4], nullptr, 16);
        v.r[2] = (uint32_t)std::strtoul(field[5], nullptr, 16);
        v.r[3] = (uint32_t)std::strtoul(field[6], nullptr, 16);
        v.r[8] = (uint32_t)std::strtoul(field[7], nullptr, 16);
        v.r[9] = (uint32_t)std::strtoul(field[8], nullptr, 16);
        v.cpsr = (uint32_t)std::strtoul(field[9], nullptr, 16);
        v.r[15] = PC + 4;

        // the checks string is parsed into a local copy: strtok writes into its input
        char checks[160];
        std::snprintf(checks, sizeof checks, "%s", field[10]);
        uint32_t wantReg[16]; bool haveReg[16] = {false};
        uint32_t wantCpsr = 0; bool wantCpsrSet = false;
        for (char* chk = std::strtok(checks, ","); chk; chk = std::strtok(nullptr, ",")) {
            if (!std::strncmp(chk, "cpsr=", 5)) {
                wantCpsr = (uint32_t)std::strtoul(chk + 5, nullptr, 16); wantCpsrSet = true;
            } else if (chk[0] == 'r') {
                const int idx = std::atoi(chk + 1);
                const char* eq = std::strchr(chk, '=');
                if (idx >= 0 && idx < 16 && eq) {
                    wantReg[idx] = (uint32_t)std::strtoul(eq + 1, nullptr, 16);
                    haveReg[idx] = true;
                }
            }
        }

        // ---- the jit3 verdict, always on the ARM9 path (the one jit3 compiles)
        //      for ARM7TDMI-specific rows only the result registers and N/Z are
        //      meaningful; C is ARM7TDMI hardware and is masked out.
        Cfg jitCfg = cfg; jitCfg.arm7 = false;
        uint32_t got[16] = {0}, gotCpsr = 0;
        bool jitRan = runSuiteVector(jitCfg, v, op, got, &gotCpsr);
        bool jitOk = true, interpOk = true;
        if (jitRan) {
            const uint32_t mask = arm7Row ? ~0x20000000u : ~0u;
            if (wantCpsrSet && (gotCpsr & mask) != (wantCpsr & mask)) jitOk = false;
            for (int i = 0; i < 16; i++)
                if (haveReg[i] && got[i] != wantReg[i]) jitOk = false;
            if (!jitOk) jitFail++;
            if (arm7Row) maskedRows++;
        }

        // ---- the interpreter (as modelled here) on the row's own core
        env.wr32(PC, op);
        ArmRef ref; ref.ram = env.ram; ref.base = GUEST_BASE; ref.arm7 = arm7Row;
        for (int i = 0; i < 16; i++) ref.regs[i] = v.r[i];
        ref.cpsr = v.cpsr;
        const int rc = ref.step(PC);
        if (rc < 0) { wasSkipped++; }
        else {
            if (wantCpsrSet && ref.cpsr != wantCpsr) interpOk = false;
            for (int i = 0; i < 16; i++)
                if (haveReg[i] && ref.regs[i] != wantReg[i]) interpOk = false;
            if (!interpOk) interpFail++;
            // Where jit3 will not compile the row on its own core it is the
            // interpreter that runs the instruction on that core in the shipped
            // build, so this verdict is the one that fails the run.
            bool ownCompiled = canCompile(([&]{ Cfg c = cfg; c.arm7 = arm7Row; return c; })(), op, nullptr);
            if (!ownCompiled && !interpOk) delegFail++;
        }

        if (!jitOk || !interpOk) {
            if (++shown <= 10) {
                std::fprintf(stderr, "  %-30s %08X %-7s %-7s %s\n", name, op,
                             jitRan ? (jitOk ? "ok" : "FAIL") : "refused",
                             interpOk ? "ok" : "FAIL", field[11]);
                for (int i = 0; i < 16; i++) {
                    if (!haveReg[i] || (ref.regs[i] == wantReg[i] && (!jitRan || got[i] == wantReg[i]))) continue;
                    std::fprintf(stderr, "      r%-2d jit=%08X interp=%08X want=%08X%s\n",
                                 i, got[i], ref.regs[i], wantReg[i],
                                 got[i] == ref.regs[i] ? "" : "   (jit3 and the interpreter disagree)");
                }
                if (wantCpsrSet && (ref.cpsr != wantCpsr || (jitRan && gotCpsr != wantCpsr)))
                    std::fprintf(stderr, "      cpsr jit=%08X interp=%08X want=%08X  (N=%u Z=%u C=%u V=%u)\n",
                                 gotCpsr, ref.cpsr, wantCpsr, wantCpsr >> 31, (wantCpsr >> 30) & 1,
                                 (wantCpsr >> 29) & 1, (wantCpsr >> 28) & 1);
            }
        } else if (verboseSuite) {
            std::fprintf(stderr, "  %-30s %08X ok      ok\n", name, op);
        }
    }
    std::fclose(f);
    std::fprintf(stderr, "suite: %d vectors (%d arm7-specific), jit vs suite FAIL=%d "
                         "(C masked on %d of them), interpreter vs suite FAIL=%d, "
                         "shipped-on-own-core FAIL=%d, skipped=%d\n",
                 total, arm7Rows, jitFail, maskedRows, interpFail, delegFail, wasSkipped);
    return jitFail || delegFail;
}


int main(int argc, char** argv) {
    if (!env.init()) { std::fprintf(stderr, "env init failed\n"); return 2; }
    cfg = makeCfg();
    const bool quick = (argc > 1 && std::string(argv[1]) == "-q");
    const char* suiteFile = nullptr;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "-suite") && i + 1 < argc) suiteFile = argv[++i];
        if (!std::strcmp(argv[i], "-v")) verboseSuite = true;
    }
    if (suiteFile) return runSuite(suiteFile) ? 1 : 0;
    std::string one;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "-one" && i + 1 < argc) { one = argv[i + 1]; oneOp = (uint32_t)strtoul(argv[i + 1], nullptr, 16); traceOne = true; }
    }
    if (failures > 3) failures = failures;

    runDPSweep();
    std::fprintf(stderr, "after DP:   checks=%d fail=%d skip=%d\n", checks, failures, skipped);
    runBranchSweep();
    std::fprintf(stderr, "after B:    checks=%d fail=%d skip=%d\n", checks, failures, skipped);
    runPsrSweep();
    std::fprintf(stderr, "after PSR:  checks=%d fail=%d skip=%d\n", checks, failures, skipped);
    if (!quick) runMulSweep();
    std::fprintf(stderr, "after MUL:  checks=%d fail=%d skip=%d\n", checks, failures, skipped);
    runSeqTargeted();
    std::fprintf(stderr, "after SEQ*: checks=%d fail=%d skip=%d\n", checks, failures, skipped);
    if (!quick) runSeqSweep();
    std::fprintf(stderr, "after SEQR: checks=%d fail=%d skip=%d\n", checks, failures, skipped);
    dumpSigs();
    std::printf("%s: %d checked, %d failed, %d not modelled\n",
                failures ? "FAIL" : "OK", checks, failures, skipped);
    return failures ? 1 : 0;
}
