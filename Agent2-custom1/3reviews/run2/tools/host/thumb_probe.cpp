// thumb_probe.cpp -- differential test for the Thumb front end, the way
// jit3_test.cpp is for the ARM one: compile halfwords into a real PowerPC block,
// run it on this core, and compare against a transcription of the NooDS Thumb
// handlers (interpreter_alu.cpp, interpreter_branch.cpp).
//
// It covers the instructions the classifier claims to *compile natively*:
//   formats 1-4 (shift/ALU rows), the hi-register forms of 5, format 12 (add
//   rd, pc/sp, #imm8) and format 13 (add/sub sp, #imm7).  Loads, stores, branches
//   and the delegated forms are not modelled here; they are checked in the emulator
//   by the startup self-test (jit3SelfTest) and by VERIFY=1, because their
//   behaviour depends on the real memory system and the real PC plumbing.
//
//   tools/host/run_thumb_probe.sh            sweep everything
//   tools/host/run_thumb_probe.sh -one 2011  trace one halfword (with disassembly)
#include "host_env.hpp"
#include "armjit.hpp"
#include "ppc_dis.hpp"

#define BIT(i) (1u << (i))
#define BIT31 (1u << 31)
#define BIT29 (1u << 29)
#define BIT5  (1u << 5)

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>

using namespace jit3host;
using namespace Jit3;

static Env env;
static const uint32_t PC = 0x02000100u;          // 4-aligned

// ---- reference: one NooDS Thumb handler, transcribed ------------------------
struct ThumbRef {
    uint32_t r[16];
    uint32_t cpsr;

    // The handlers read/write the *current* register file; this probe runs in
    // System mode, so that is r[] with no banking.
    int step(uint16_t op) {
        const uint32_t t13 = op >> 13;
        if (t13 == 0) return fmt12(op);
        if (t13 == 1) return fmt3(op);
        if ((op & 0xFC00u) == 0x4000u) return aluRow(op);
        if ((op & 0xFC00u) == 0x4400u) return hiReg(op);
        if ((op & 0xF800u) == 0xA000u) return lea(op);      // format 12
        if ((op & 0xFF00u) == 0xB000u) return spAdj(op);    // format 13
        return -1;                                          // not modelled here
    }

    // ADD Rd,PC/SP,#imm8 (addPcT / addSpT): no flags, and PC is masked with ~3.
    int lea(uint16_t op) {
        const bool sp = (op >> 11) & 1;
        const uint32_t rd = (op >> 8) & 7;
        const uint32_t imm = (op & 0xFF) << 2;
        // addPcT: op1 = *registers[15] & ~3, and during the handler registers[15]
        // is the pipeline PC = instrAddr + 4 (the run loop advances it first).
        r[rd] = (sp ? r[13] : ((PC + 4u) & ~3u)) + imm;
        return 1;
    }

    // ADD/SUB SP,#imm7 (addSpImmT)
    int spAdj(uint16_t op) {
        const uint32_t imm = ((op & 0x80u) ? (0u - (op & 0x7Fu)) : (op & 0x7Fu)) << 2;
        r[13] += imm;
        return 1;
    }

    // Formats 1 and 2.
    int fmt12(uint16_t op) {
        const uint32_t sub = (op >> 11) & 3;
        const uint32_t rd = op & 7, rs = (op >> 3) & 7;
        if (sub != 3) {                                     // LSL/LSR/ASR Rd, Rs, #imm5
            const uint32_t op1 = r[rs];
            const uint8_t op2 = (uint8_t)((op >> 6) & 0x1F);
            uint32_t res = 0;
            switch (sub) {
            case 0:                                         // lslImmT
                res = op1 << op2;
                r[rd] = res;
                cpsr = (cpsr & ~0xC0000000u) | (res & BIT31) | ((res == 0) << 30);
                if (op2 > 0) cpsr = (cpsr & ~BIT29) | ((uint32_t)(bool)(op1 & BIT(32 - op2)) << 29);
                return 1;
            case 1:                                         // lsrImmT (0 means 32)
                res = op2 ? (op1 >> op2) : 0;
                r[rd] = res;
                cpsr = (cpsr & ~0xE0000000u) | (res & BIT31) | ((res == 0) << 30) |
                       ((uint32_t)(bool)(op1 & BIT(op2 ? (op2 - 1) : 31)) << 29);
                return 1;
            default:                                        // asrImmT (0 means 32)
                res = op2 ? (uint32_t)((int32_t)op1 >> op2) : ((op1 & BIT31) ? 0xFFFFFFFFu : 0u);
                r[rd] = res;
                cpsr = (cpsr & ~0xE0000000u) | (res & BIT31) | ((res == 0) << 30) |
                       ((uint32_t)(bool)(op1 & BIT(op2 ? (op2 - 1) : 31)) << 29);
                return 1;
            }
        }
        // Format 2: ADD/SUB Rd, Rs, Rn / #imm3 (addRegT, subRegT, addImm3T, subImm3T)
        const bool isSub = (op & 0x200u) != 0;
        const bool isImm = (op & 0x400u) != 0;               // bits 15-9 = 000111x
        const uint32_t op1 = r[rs];
        const uint32_t op2 = isImm ? ((op >> 6) & 7) : r[(op >> 6) & 7];
        const uint32_t res = isSub ? (op1 - op2) : (op1 + op2);
        r[rd] = res;
        if (isSub)
            cpsr = (cpsr & ~0xF0000000u) | (res & BIT31) | ((res == 0) << 30) |
                   ((uint32_t)(op1 >= res) << 29) |
                   (((op2 ^ op1) & ~(res ^ op2) & BIT31) >> 3);
        else
            cpsr = (cpsr & ~0xF0000000u) | (res & BIT31) | ((res == 0) << 30) |
                   ((uint32_t)(op1 > res) << 29) |
                   ((~(op2 ^ op1) & (res ^ op2) & BIT31) >> 3);
        return 1;
    }

    // Format 3: MOV/CMP/ADD/SUB Rd, #imm8.
    int fmt3(uint16_t op) {
        const uint32_t sub = (op >> 11) & 3;
        const uint32_t rd = (op >> 8) & 7;
        const uint32_t op2 = op & 0xFF;
        const uint32_t op1 = r[rd];
        if (sub == 0) {                                      // movImm8T
            r[rd] = op2;
            cpsr = (cpsr & ~0xC0000000u) | (op2 & BIT31) | ((op2 == 0) << 30);
            return 1;
        }
        const uint32_t res = (sub == 1) ? (op1 - op2) : (sub == 2 ? op1 + op2 : op1 - op2);
        if (sub != 1) r[rd] = res;
        if (sub == 2)                                        // addImm8T
            cpsr = (cpsr & ~0xF0000000u) | (res & BIT31) | ((res == 0) << 30) |
                   ((uint32_t)(op1 > res) << 29) |
                   ((~(op2 ^ op1) & (res ^ op2) & BIT31) >> 3);
        else                                                 // cmpImm8T / subImm8T
            cpsr = (cpsr & ~0xF0000000u) | (res & BIT31) | ((res == 0) << 30) |
                   ((uint32_t)(op1 >= res) << 29) |
                   (((op2 ^ op1) & ~(res ^ op2) & BIT31) >> 3);
        return 1;
    }

    // Format 4: the ALU row.  rd is always the first source *and* the destination
    // (except for the test ops and NEG/MVN/MUL, which is where the encodings differ).
    int aluRow(uint16_t op) {
        const uint32_t aop = (op >> 6) & 0xF;
        const uint32_t rd = op & 7, rs = (op >> 3) & 7;
        const uint32_t op1 = r[rd], op2 = r[rs];
        const auto setNZ = [&](uint32_t res) {
            cpsr = (cpsr & ~0xC0000000u) | (res & BIT31) | ((res == 0) << 30);
        };
        switch (aop) {
        case 0:  r[rd] = op1 & op2;  setNZ(r[rd]); return 1;                   // AND
        case 1:  r[rd] = op1 ^ op2;  setNZ(r[rd]); return 1;                   // EOR
        case 2: {                                                             // LSL Rd, Rs
            const uint8_t s = (uint8_t)op2;
            const uint32_t res = (s < 32) ? (op1 << s) : 0;
            r[rd] = res;
            cpsr = (cpsr & ~0xC0000000u) | (res & BIT31) | ((res == 0) << 30);
            if (s > 0) cpsr = (cpsr & ~BIT29) | ((uint32_t)(s <= 32 && (op1 & BIT(32 - s))) << 29);
            return 1;
        }
        case 3: {                                                             // LSR Rd, Rs
            const uint8_t s = (uint8_t)op2;
            const uint32_t res = (s < 32) ? (op1 >> s) : 0;
            r[rd] = res;
            cpsr = (cpsr & ~0xC0000000u) | (res & BIT31) | ((res == 0) << 30);
            if (s > 0) cpsr = (cpsr & ~BIT29) | ((uint32_t)(s <= 32 && (op1 & BIT(s - 1))) << 29);
            return 1;
        }
        case 4: {                                                             // ASR Rd, Rs
            const uint8_t s = (uint8_t)op2;
            const uint32_t res = (s < 32) ? (uint32_t)((int32_t)op1 >> s)
                                          : ((op1 & BIT31) ? 0xFFFFFFFFu : 0u);
            r[rd] = res;
            cpsr = (cpsr & ~0xC0000000u) | (res & BIT31) | ((res == 0) << 30);
            // NooDS: `(bool)(op1 & BIT(...)) << 29` - the cast is load-bearing, since
            // shifting the mask itself (0x40000000 << 29) is 0 in 32 bits.
            if (s > 0) cpsr = (cpsr & ~BIT29) |
                              ((uint32_t)((op1 & BIT((s <= 32) ? (s - 1) : 31)) != 0) << 29);
            return 1;
        }
        case 5:                                                               // ADC
            r[rd] = op1 + op2 + ((cpsr & BIT29) >> 29);
            cpsr = (cpsr & ~0xF0000000u) | (r[rd] & BIT31) | ((r[rd] == 0) << 30) |
                   ((uint32_t)(op1 > r[rd] || (op2 == 0xFFFFFFFFu && (cpsr & BIT29))) << 29) |
                   ((~(op2 ^ op1) & (r[rd] ^ op2) & BIT31) >> 3);
            return 1;
        case 6:                                                               // SBC
            r[rd] = op1 - op2 - 1 + ((cpsr & BIT29) >> 29);
            cpsr = (cpsr & ~0xF0000000u) | (r[rd] & BIT31) | ((r[rd] == 0) << 30) |
                   ((uint32_t)(op1 >= r[rd] && (op2 != 0xFFFFFFFFu || (cpsr & BIT29))) << 29) |
                   (((op2 ^ op1) & ~(r[rd] ^ op2) & BIT31) >> 3);
            return 1;
        case 7: {                                                             // ROR Rd, Rs
            const uint8_t s = (uint8_t)op2;
            const uint8_t n = (uint8_t)(s & 0x1F);
            const uint32_t res = (op1 << (32 - n)) | (op1 >> n);   // rotate by (op2 & 31)
            r[rd] = res;
            cpsr = (cpsr & ~0xC0000000u) | (res & BIT31) | ((res == 0) << 30);
            if (s > 0) cpsr = (cpsr & ~BIT29) |
                              ((uint32_t)(bool)(op1 & BIT((s - 1) & 0x1F)) << 29);
            return 1;
        }
        case 8: {                                                             // TST Rd, Rs
            const uint32_t res = op1 & op2;
            cpsr = (cpsr & ~0xC0000000u) | (res & BIT31) | ((res == 0) << 30);
            return 1;
        }
        case 9:                                                               // NEG Rd, Rs
            r[rd] = (uint32_t)(0u - op2);
            cpsr = (cpsr & ~0xF0000000u) | (r[rd] & BIT31) | ((r[rd] == 0) << 30) |
                   ((uint32_t)(r[rd] == 0) << 29);
            return 1;
        case 10: {                                                            // CMP Rd, Rs
            const uint32_t res = op1 - op2;
            cpsr = (cpsr & ~0xF0000000u) | (res & BIT31) | ((res == 0) << 30) |
                   ((uint32_t)(op1 >= res) << 29) |
                   (((op2 ^ op1) & ~(res ^ op2) & BIT31) >> 3);
            return 1;
        }
        case 11: {                                                            // CMN Rd, Rs
            const uint32_t res = op1 + op2;
            cpsr = (cpsr & ~0xF0000000u) | (res & BIT31) | ((res == 0) << 30) |
                   ((uint32_t)(op1 > res) << 29) |
                   ((~(op2 ^ op1) & (res ^ op2) & BIT31) >> 3);
            return 1;
        }
        case 12: r[rd] = op1 | op2;  setNZ(r[rd]); return 1;                   // ORR
        case 13:                                                              // MUL Rd, Rs
            // mulDpT: N,Z only, and *four* cycles on the ARM9 (the ARM7 formula
            // depends on the operand width, and the ARM7 form is delegated).
            r[rd] = (uint32_t)((int32_t)op2 * (int32_t)op1);
            setNZ(r[rd]);
            return 4;
        case 14: r[rd] = op1 & ~op2; setNZ(r[rd]); return 1;                    // BIC
        default: r[rd] = ~op2;       setNZ(r[rd]); return 1;                    // MVN
        }
    }

    // Format 5: ADD/CMP/MOV with a high register (no PC operands here).
    int hiReg(uint16_t op) {
        const uint32_t kind = (op >> 8) & 3;
        const uint32_t rd = ((op >> 4) & 8) | (op & 7);
        const uint32_t rs = (op >> 3) & 0xF;
        if (rd == 15 || rs == 15) return -1;                 // PC forms: not modelled here
        switch (kind) {
        case 0:                                               // addHT
            r[rd] += r[rs];
            return 1;
        case 1: {                                             // cmpHT
            const uint32_t op1 = r[rd], op2 = r[rs];
            const uint32_t res = op1 - op2;
            cpsr = (cpsr & ~0xF0000000u) | (res & BIT31) | ((res == 0) << 30) |
                   ((uint32_t)(op1 >= res) << 29) |
                   (((op2 ^ op1) & ~(res ^ op2) & BIT31) >> 3);
            return 1;
        }
        case 2:                                               // movHT
            r[rd] = r[rs];
            return 1;
        default: return -1;                                   // BX / BLX
        }
    }
};

// ---- harness ----------------------------------------------------------------
static int checks = 0, failures = 0, skipped = 0;
static uint16_t curOp = 0;
static const uint32_t* curWords = nullptr;
static uint32_t curCount = 0;
static uint32_t oneOp = 0xFFFFu;                     // -one <hex4>
static bool traceOne = false;
static int shownFails = 0;
static bool verbose = false;
static const char* suiteFile = nullptr;
static uint32_t forceCpsr = 0; static bool hasForceCpsr = false;

static uint32_t gmRead32(void*, uint32_t a) { return env.rd32(a); }
static uint32_t gmRead16(void*, uint32_t a) { return env.rd16(a); }

static Cfg makeCfg() {
    Cfg c;
    c.arm7 = false;
    c.offRegsUsr  = (int32_t)offsetof(HostCpu, registersUsr);
    c.offRegsPtr  = (int32_t)offsetof(HostCpu, registers);
    c.offCpsr     = (int32_t)offsetof(HostCpu, cpsr);
    c.offHalted   = (int32_t)offsetof(HostCpu, halted);
    c.maxBlockInstrs = 1;                     // exactly the instruction under test
    c.maxBlockWords  = 1200;
    return c;
}
static Cfg cfg = makeCfg();

static void dumpBlock() {
    std::fprintf(stderr, "  emitted block (%u words):\n", curCount);
    for (uint32_t i = 0; i < curCount && i < 120; i++) {
        char d[128];
        ppcdis::disasm(curWords[i], d, sizeof d);
        std::fprintf(stderr, "    %3u %04X: %08X  %s\n", i, i * 4, curWords[i], d);
    }
}

static const uint32_t kInteresting[10] = {
    0x00000000u, 0x00000001u, 0x00000002u, 0x7FFFFFFFu, 0x80000000u,
    0xFFFFFFFFu, 0xFFFFFFFEu, 0x0000FFFFu, 0x12345678u, 0xDEADBEEFu
};
static uint32_t rngState = 0x2468ACE1u;
static uint32_t rnd() { rngState = rngState * 1103515245u + 12345u; return rngState >> 8; }

struct Values { uint32_t r[16]; uint32_t cpsr; };

// One halfword at PC: compile it in Thumb mode, run it, and compare.
static bool tryOneHalf(uint16_t op, const Values& v) {
    if (oneOp != 0xFFFFu && op != oneOp) return true;
    if (traceOne) {
        std::fprintf(stderr, "op=%04X r=", op);
        for (int i = 0; i < 16; i++) std::fprintf(stderr, " %08X", v.r[i]);
        std::fprintf(stderr, " cpsr=%08X (N=%u Z=%u C=%u V=%u)\n", v.cpsr,
                     v.cpsr >> 31, (v.cpsr >> 30) & 1, (v.cpsr >> 29) & 1, (v.cpsr >> 28) & 1);
    }

    ThumbRef ref;
    for (int i = 0; i < 16; i++) ref.r[i] = v.r[i];
    ref.cpsr = v.cpsr;
    const int refCycles = ref.step(op);
    if (refCycles < 0) { skipped++; return true; }

    env.wr16(PC, op);
    env.wr16(PC + 2, 0x46C0u);                // "mov r8, r8": filler, never reached

    uint32_t words[1200]; uint32_t n = 0;
    BlockInfo info;
    GuestMem gm;
    gm.ctx = nullptr;
    gm.read32 = gmRead32;
    gm.read16 = gmRead16;

    // The classifier is private to the emitter, so ask the compiler itself: a
    // block that does not compile is a refusal, not a failure.
    if (!compileBlock(cfg, gm, PC, true, words, 1200, n, info)) { skipped++; return true; }
    // A block that hands an instruction to the interpreter has no helper table here
    // (this probe links only the emitter), and it is not what is under test anyway.
    if (info.delegInstrs) { skipped++; return true; }
    if (info.instrCount != 1) { skipped++; return true; }

    env.cpu.registersUsr[13] = v.r[13];
    for (int i = 0; i < 16; i++) {
        env.cpu.registersUsr[i] = v.r[i];
        env.cpu.registers[i] = &env.cpu.registersUsr[i];      // System mode: no banking
    }
    env.cpu.cpsr = v.cpsr;
    env.cpu.registersUsr[15] = PC + 2;        // the interpreter's pipeline PC in Thumb
    env.cpu.halted = 0;
    curOp = op; curWords = words; curCount = n;
    if (verbose) { std::fprintf(stderr, "  ... op=%04X words=%u\n", op, n); std::fflush(stderr); }

    BlockResult got = runBlock(env, words, n);
    checks++;

    // The driver's exit convention: kPcFromCpu (-1 in the high word) means "the CPU
    // holds the PC"; anything else is the logical PC the block resumed at.
    const uint32_t gotPc = (got.nextPc == 0xFFFF0000u) ? (env.cpu.registersUsr[15] - 2)
                                                       : (got.nextPc & ~1u);
    const uint32_t refPc = PC + 2;            // this subset never moves the PC

    bool ok = got.cycles == (uint32_t)refCycles && gotPc == refPc;
    for (int i = 0; i < 16 && ok; i++)
        if (i != 15) ok = env.cpu.registersUsr[i] == ref.r[i];
    ok = ok && env.cpu.cpsr == ref.cpsr;
    if (!ok) {
        failures++;
        if (shownFails < 6) {
            shownFails++;
            std::fprintf(stderr, "FAIL op=%04X %s\n", op, oneOp != 0xFFFFu ? "(traced)" : "");
            std::fprintf(stderr, "  in cpsr=%08X r=", v.cpsr);
            for (int i = 0; i < 16; i++) std::fprintf(stderr, " %08X", v.r[i]);
            std::fprintf(stderr, "\n");
            std::fprintf(stderr, "  cyc got %u want %d   pc got %08X want %08X\n",
                         got.cycles, refCycles, gotPc, refPc);
            for (int i = 0; i < 16; i++)
                if (i != 15 && env.cpu.registersUsr[i] != ref.r[i])
                    std::fprintf(stderr, "  r%-2d got %08X want %08X\n",
                                 i, env.cpu.registersUsr[i], ref.r[i]);
            if (env.cpu.cpsr != ref.cpsr)
                std::fprintf(stderr, "  cpsr got %08X (N=%u Z=%u C=%u V=%u) want %08X (N=%u Z=%u C=%u V=%u)\n",
                             env.cpu.cpsr, env.cpu.cpsr >> 31, (env.cpu.cpsr >> 30) & 1,
                             (env.cpu.cpsr >> 29) & 1, (env.cpu.cpsr >> 28) & 1,
                             ref.cpsr, ref.cpsr >> 31, (ref.cpsr >> 30) & 1,
                             (ref.cpsr >> 29) & 1, (ref.cpsr >> 28) & 1);
            dumpBlock();
        }
    } else if (traceOne) {
        std::fprintf(stderr, "  ok: cyc=%u pc=%08X cpsr=%08X\n", got.cycles, gotPc, env.cpu.cpsr);
        dumpBlock();
    }
    return ok;
}

static Values makeValues() {
    Values v;
    for (int i = 0; i < 16; i++)
        v.r[i] = (rnd() & 3) ? kInteresting[rnd() % 10] : rnd();
    v.r[13] = kInteresting[rnd() % 10];
    v.r[15] = PC + 2;                          // pipeline PC, never used by this subset
    // System mode with the T bit set, and all four flags in every combination
    v.cpsr = 0x0000001Fu | BIT5 | ((rnd() & 0xF) << 28);
    return v;
}


// ---------------------------------------------------------------------------
// -suite <file>: the external oracle.
//
// Each line of the file is a vector derived (by tools/host/gen_suite_vectors.py)
// from the mGBA test suite's CPU-core tables - the expectations in it are the
// suite's own, and the suite is the accuracy reference the GBA emulator community
// runs.  A vector is one Thumb halfword, the registers and CPSR to enter it with,
// and the result and CPSR the *hardware* leaves behind:
//
//   name|op|r0|r1|cpsr_in|dst|exp_reg|exp_cpsr|note
//
// Two verdicts per vector, and they answer different questions:
//   * jit vs suite - does the code jit3 emits do what ARM7TDMI does?  This is the
//     verdict that can fail because of a jit3 bug, and the exit status counts them.
//   * model vs suite - does the semantics NooDS itself implements (as this probe
//     models it) match ARM7TDMI?  jit3 reproduces NooDS on purpose, so a failure
//     here is a NooDS finding, not a jit3 one - reported, never "fixed" silently.
//
// The two answers are compared against each other as well: jit3 and the model
// disagreeing is a jit3 bug even if the suite happens to agree with one of them.
// ---------------------------------------------------------------------------
static int runSuite(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return 1000; }
    char line[512];
    int jitFail = 0, refFail = 0, jitRefMismatch = 0, skipped = 0, total = 0, shown = 0;
    std::fprintf(stderr, "  %-34s %-6s %-9s %-9s %s\n",
                 "case", "op", "jit", "model", "note");
    while (std::fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* nl = std::strchr(line, '\n');
        if (nl) *nl = 0;
        char* field[9] = {0};
        int nf = 0;
        char* p = line;
        for (; nf < 9 && p; nf++) {
            field[nf] = p;
            char* bar = std::strchr(p, '|');
            if (bar) { *bar = 0; p = bar + 1; } else p = nullptr;
        }
        if (nf < 8) continue;
        const uint16_t op = (uint16_t)std::strtoul(field[1], nullptr, 16);
        Values v{};
        v.r[0] = (uint32_t)std::strtoul(field[2], nullptr, 16);
        v.r[1] = (uint32_t)std::strtoul(field[3], nullptr, 16);
        v.cpsr  = (uint32_t)std::strtoul(field[4], nullptr, 16);
        const int dst = std::atoi(field[5]);
        const uint32_t expReg = (uint32_t)std::strtoul(field[6], nullptr, 16);
        const uint32_t expCpsr = (uint32_t)std::strtoul(field[7], nullptr, 16);
        const char* note = field[8] ? field[8] : "";
        v.r[15] = PC + 2;
        total++;

        // The model first: it decides whether the vector is even in the probe's
        // universe (step() returns < 0 for the instructions it does not model).
        ThumbRef ref;
        for (int i = 0; i < 16; i++) ref.r[i] = v.r[i];
        ref.cpsr = v.cpsr;
        if (ref.step(op) < 0) {
            skipped++;
            if (verbose) std::fprintf(stderr, "  %-34s %04X  skipped: the probe's model does not model it\n", field[0], op);
            continue;
        }

        env.wr16(PC, op);
        env.wr16(PC + 2, 0x46C0u);
        uint32_t words[1200]; uint32_t n = 0; BlockInfo info;
        GuestMem gm; gm.ctx = nullptr; gm.read32 = gmRead32; gm.read16 = gmRead16;
        Cfg vecCfg = cfg;
        vecCfg.maxBlockInstrs = 1;
        vecCfg.chain = false;
        if (!compileBlock(vecCfg, gm, PC, true, words, 1200, n, info) || info.delegInstrs ||
            info.instrCount != 1) {
            skipped++;                                // refused or delegated: not our case
            if (verbose) std::fprintf(stderr, "  %-34s %04X  skipped: jit3 refuses or delegates it\n", field[0], op);
            continue;
        }
        for (int i = 0; i < 16; i++) {
            env.cpu.registersUsr[i] = v.r[i];
            env.cpu.registers[i] = &env.cpu.registersUsr[i];
        }
        env.cpu.cpsr = v.cpsr;
        env.cpu.registersUsr[15] = PC + 2;
        env.cpu.halted = 0;
        curOp = op; curWords = words; curCount = n;
        runBlock(env, words, n);

        const uint32_t gotReg = env.cpu.registersUsr[dst], gotCpsr = env.cpu.cpsr;
        const bool jitOk = gotReg == expReg && gotCpsr == expCpsr;
        const bool refOk = ref.r[dst] == expReg && ref.cpsr == expCpsr;
        const bool agree = gotReg == ref.r[dst] && gotCpsr == ref.cpsr;
        if (!jitOk) jitFail++;
        if (!refOk) refFail++;
        if (!agree) jitRefMismatch++;
        if (!jitOk || !refOk) {
            shown++;
            std::fprintf(stderr, "  %-34s %04X  %-9s %-9s %s\n", field[0], op,
                         jitOk ? "ok" : "FAIL", refOk ? "ok" : "FAIL", note);
            std::fprintf(stderr, "      in  r0=%08X r1=%08X cpsr=%08X   want rd=%08X cpsr=%08X\n",
                         v.r[0], v.r[1], v.cpsr, expReg, expCpsr);
            std::fprintf(stderr, "      jit rd=%08X cpsr=%08X (N=%u Z=%u C=%u V=%u)%s%s\n",
                         gotReg, gotCpsr, gotCpsr >> 31, (gotCpsr >> 30) & 1,
                         (gotCpsr >> 29) & 1, (gotCpsr >> 28) & 1,
                         agree ? "" : "  (jit3 and the NooDS model DISAGREE)",
                         (jitOk || refOk) ? "" : "");
            if (!agree) {
                std::fprintf(stderr, "      model rd=%08X cpsr=%08X (N=%u Z=%u C=%u V=%u)\n",
                             ref.r[dst], ref.cpsr, ref.cpsr >> 31, (ref.cpsr >> 30) & 1,
                             (ref.cpsr >> 29) & 1, (ref.cpsr >> 28) & 1);
                if (shown <= 6) dumpBlock();
            }
        } else if (verbose) {
            std::fprintf(stderr, "  %-34s %04X  ok       ok\n", field[0], op);
        }
    }
    std::fclose(f);
    std::fprintf(stderr,
                 "suite: %d vectors, jit vs suite FAIL=%d, NooDS-model vs suite FAIL=%d, "
                 "jit-vs-model mismatches=%d, skipped=%d\n",
                 total, jitFail, refFail, jitRefMismatch, skipped);
    return jitFail + jitRefMismatch;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "-v")) { verbose = true; continue; }
        if (!std::strcmp(argv[i], "-suite") && i + 1 < argc) { suiteFile = argv[++i]; continue; }
        if (!std::strcmp(argv[i], "-csr") && i + 1 < argc) { forceCpsr = (uint32_t)std::strtoul(argv[i + 1], nullptr, 16); hasForceCpsr = true; continue; }
        if (!std::strcmp(argv[i], "-one") && i + 1 < argc) {
            oneOp = (uint16_t)std::strtoul(argv[++i], nullptr, 16);
            traceOne = true;
        }
    }
    if (!env.init()) { std::fprintf(stderr, "env init failed\n"); return 2; }

    const int NSTATES = 24;

    if (suiteFile) return runSuite(suiteFile);

    // -seq <hex4>[,<hex4>...]: compile and run a whole block with no reference at
    // all, and print what it emitted and what it left behind.  For the instructions
    // this probe does not model (branches, delegates) this is the instrument: the
    // static cycle charge is printed next to the runtime result, so a charge that
    // is emitted on the wrong path shows up immediately.
    for (int a = 1; a < argc; a++) {
        if (std::strcmp(argv[a], "-seq") || a + 1 >= argc) continue;
        uint32_t seqCpsr = 0x0000001Fu | BIT5 | BIT29;    // T set, C set, Z clear
        for (int q = a + 2; q < argc; q++)
            if (!std::strncmp(argv[q], "-csr", 4) && q + 1 < argc) seqCpsr = (uint32_t)std::strtoul(argv[q + 1], nullptr, 16);
        uint16_t seq[8]; int nseq = 0;
        for (char* tok = std::strtok(argv[a + 1], ","); tok && nseq < 8; tok = std::strtok(nullptr, ","))
            seq[nseq++] = (uint16_t)std::strtoul(tok, nullptr, 16);
        for (int i = 0; i < nseq; i++) env.wr16(PC + 2 * i, seq[i]);
        uint32_t words[1200]; uint32_t n = 0; BlockInfo info;
        GuestMem gm; gm.ctx = nullptr; gm.read32 = gmRead32; gm.read16 = gmRead16;
        Cfg seqCfg = cfg;
        seqCfg.maxBlockInstrs = (uint32_t)nseq;
        Values v;
        for (int i = 0; i < 16; i++) v.r[i] = kInteresting[i % 10];
        v.r[15] = PC + 2;
        v.cpsr = seqCpsr;
        std::fprintf(stderr, "seq:");
        for (int i = 0; i < nseq; i++) std::fprintf(stderr, " %04X", seq[i]);
        std::fprintf(stderr, "  (T bit set, CPSR=%08X)\n", v.cpsr);
        std::fprintf(stderr, "  in  r0..r7 =");
        for (int i = 0; i < 8; i++) std::fprintf(stderr, " %08X", v.r[i]);
        std::fprintf(stderr, "\n  in r8..r15 =");
        for (int i = 8; i < 16; i++) std::fprintf(stderr, " %08X", v.r[i]);
        std::fprintf(stderr, "\n");
        if (!compileBlock(seqCfg, gm, PC, true, words, 1200, n, info)) {
            std::fprintf(stderr, "  refused\n");
            return 1;
        }
        std::fprintf(stderr, "  block: instrs=%u exitPc=%08X staticCycles=%u words=%u\n",
                     info.instrCount, info.exitPc, info.cycles, n);
        curWords = words; curCount = n;
        dumpBlock();
        for (int i = 0; i < 16; i++) {
            env.cpu.registersUsr[i] = v.r[i];
            env.cpu.registers[i] = &env.cpu.registersUsr[i];
        }
        env.cpu.cpsr = v.cpsr;
        env.cpu.registersUsr[15] = PC + 2;
        BlockResult got = runBlock(env, words, n);
        std::fprintf(stderr, "  run: cycles=%u rawPc=%08X cpsr=%08X regsUsr[15]=%08X\n",
                     got.cycles, got.nextPc, env.cpu.cpsr, env.cpu.registersUsr[15]);
        std::fprintf(stderr, "  r0..r3 = %08X %08X %08X %08X\n",
                     env.cpu.registersUsr[0], env.cpu.registersUsr[1],
                     env.cpu.registersUsr[2], env.cpu.registersUsr[3]);
        return 0;
    }

    if (oneOp != 0xFFFFu) {
        // Deterministic: every interesting operand in r0..r3, and all 16 flag
        // combinations, so a disagreement can be reproduced by hand.
        for (int f = 0; f < 16; f++) {
            for (int k = 0; k < 10; k++) {
                Values v;
                for (int i = 0; i < 16; i++) v.r[i] = kInteresting[(k + i) % 10];
                v.r[15] = PC + 2;
                v.cpsr = 0x0000001Fu | BIT5 | ((uint32_t)f << 28);
                tryOneHalf(oneOp, v);
            }
        }
        std::fprintf(stderr, "traced: checks=%d failures=%d\n", checks, failures);
        return failures ? 1 : 0;
    }

    // Formats 1 and 2 (shift by immediate, add/sub register and imm3)
    for (uint32_t enc = 0; enc < 4; enc++)
        for (uint32_t rd = 0; rd < 8; rd++)
            for (uint32_t rs = 0; rs < 8; rs++)
                for (uint32_t amt = 0; amt < 32; amt++)
                    for (int s = 0; s < NSTATES; s++)
                        tryOneHalf((uint16_t)((enc << 11) | (amt << 6) | (rs << 3) | rd), makeValues());
    for (uint32_t add = 0; add < 2; add++)
        for (uint32_t imm = 0; imm < 2; imm++)
            for (uint32_t rn = 0; rn < 8; rn++)
                for (uint32_t rs = 0; rs < 8; rs++)
                    for (uint32_t rd = 0; rd < 8; rd++)
                        for (int s = 0; s < NSTATES; s++)
                            tryOneHalf((uint16_t)(0x1800u | (add << 9) | (imm << 10) |
                                                  (rn << 6) | (rs << 3) | rd), makeValues());

    // Format 3 (MOV/CMP/ADD/SUB Rd, #imm8)
    for (uint32_t kind = 0; kind < 4; kind++)
        for (uint32_t rd = 0; rd < 8; rd++)
            for (uint32_t imm = 0; imm < 256; imm++)
                for (int s = 0; s < NSTATES; s++)
                    tryOneHalf((uint16_t)(0x2000u | (kind << 11) | (rd << 8) | imm), makeValues());

    // Format 4 (the ALU row, all 16 ops)
    for (uint32_t aop = 0; aop < 16; aop++)
        for (uint32_t rd = 0; rd < 8; rd++)
            for (uint32_t rs = 0; rs < 8; rs++)
                for (int s = 0; s < NSTATES; s++)
                    tryOneHalf((uint16_t)(0x4000u | (aop << 6) | (rs << 3) | rd), makeValues());

    // Format 5 (hi-register ADD/CMP/MOV), formats 12 and 13
    for (uint32_t kind = 0; kind < 3; kind++)
        for (uint32_t rd = 0; rd < 16; rd++)
            for (uint32_t rs = 0; rs < 16; rs++) {
                if (rd == 15 || rs == 15) continue;
                for (int s = 0; s < NSTATES; s++)
                    tryOneHalf((uint16_t)(0x4400u | (kind << 8) | ((rd >> 3) << 7) |
                                          ((rs >> 3) << 6) | ((rs & 7) << 3) | (rd & 7)), makeValues());
            }
    for (uint32_t sp = 0; sp < 2; sp++)
        for (uint32_t rd = 0; rd < 8; rd++)
            for (uint32_t imm = 0; imm < 256; imm++)
                for (int s = 0; s < NSTATES; s++)
                    tryOneHalf((uint16_t)(0xA000u | (sp << 11) | (rd << 8) | imm), makeValues());
    for (uint32_t imm = 0; imm < 128; imm++)
        for (int s = 0; s < NSTATES; s++) {
            tryOneHalf((uint16_t)(0xB000u | imm), makeValues());
            tryOneHalf((uint16_t)(0xB080u | imm), makeValues());
        }

    std::fprintf(stderr, "%s: %d checked, %d failed, %d not modelled\n",
                 failures ? "FAIL" : "OK", checks, failures, skipped);
    return failures ? 1 : 0;
}
