/*
    Copyright (C) 2026 radicalten

    This file is part of NooDS-Wii.

    NooDS-Wii is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch-mechanism microbenchmark (opt-in: -DNOODS_DISPATCH_BENCH=1)
//
// Compares every dispatch technique relevant to this emulator, on the real
// PowerPC 750 target, over an identical opcode stream with identical handler
// bodies.  Reference for the classification: Anton Ertl, "Threading techniques"
// https://www.complang.tuwien.ac.at/forth/threading/
//
//   1. pmf          call through a pointer-to-member table (8-byte entries,
//                   v1.78 baseline)                     -> subroutine threading
//   2. call         call through a flat 4-byte table, return to the dispatcher
//                   (v1.980)                            -> subroutine threading
//   3. switch       replicated nested switch over the 4096-slot map
//   4. goto_call    computed goto to a per-slot label that then CALLS the
//                   handler (a hybrid -- not a pure technique)
//   5. goto_inline  computed goto to a per-slot label with the handler body
//                   inlined at that label, then goto the next slot
//                   -> true direct threading (requires one body copy per slot)
//   6. tail         flat table where the handler's last action is
//                   `return tab[next](...)`, so gcc emits a sibling call and
//                   the dispatcher's return address is reused
//                   -> direct threading with ONE body copy per handler
//
// 5 and 6 differ in code size, which is the whole argument on a 750CL:
// 5 needs one inlined body per *table slot*, 6 needs one body per *handler*.
//
// A "body weight" knob (TINY / MED / HEAVY) is run because the ranking is not
// weight-independent: the cheaper the handler, the more the call/return
// sequence dominates.  The real interpreter's average handler is ~488 bytes of
// PPC code, which brackets HEAVY.
// ─────────────────────────────────────────────────────────────────────────────

#include "core.h"

#if NOODS_DISPATCH_BENCH

#include <cstdio>
#include <cstring>
#include <gccore.h>
extern "C" {
    #include <tuxedo/ppc/clock.h>
}

namespace DispatchBench2 {

// Deliberately small, cache-resident context so the handler bodies are pure
// register/ALU work and only the dispatch varies.
struct Ctx {
    uint32_t r[16];
    uint32_t cpsr;
    uint32_t pc;
    int32_t  credit;      // chained variants: instructions left to run
    const uint32_t *code;
};

enum { NSLOTS = 4096, CODE = 1024 };
enum { ITERS = 1024 * 1024 };

static uint32_t stream[CODE];
static volatile int sink;

// ── handler bodies ──────────────────────────────────────────────────────────
// One "unit" is ~15 PPC instructions of ALU/flag work shaped like an ARM
// data-processing handler.  HEAVY repeats it 5x to reach the real emulator's
// ~488-byte average handler.
#define BODY1(K)                                                              \
    do {                                                                      \
        uint32_t a_ = c->r[(op >> 16) & 0xF] + (uint32_t)(op & 0xFFu) + (K);  \
        uint32_t b_ = c->r[(op >> 12) & 0xF] ^ (a_ << 2);                     \
        uint32_t s_ = a_ + b_;                                                \
        c->r[(op >> 8) & 0xF] = s_;                                           \
        c->cpsr = (c->cpsr & 0x0FFFFFFFu) | (s_ & 0xF0000000u) |              \
                  ((s_ == 0u) ? 0x40000000u : 0u);                            \
    } while (0)

#if BENCH_WEIGHT == 0
    #define BODY(K) BODY1(K)
    #define WEIGHT_NAME "tiny(~15)"
#elif BENCH_WEIGHT == 1
    #define BODY(K) do { BODY1(K); BODY1(K+1); BODY1(K+2); } while (0)
    #define WEIGHT_NAME "med(~45)"
#else
    #define BODY(K) do { BODY1(K); BODY1(K+1); BODY1(K+2); BODY1(K+3);      \
                         BODY1(K+4); BODY1(K+5); BODY1(K+6); BODY1(K+7); } while (0)
    #define WEIGHT_NAME "heavy(~120)"
#endif

#define HANDLER(N)                                                            \
    __attribute__((noinline)) static int h##N(Ctx *c, uint32_t op) {          \
        BODY(N * 16 + 1);                                                     \
        return 1;                                                             \
    }
HANDLER(0) HANDLER(1) HANDLER(2) HANDLER(3) HANDLER(4) HANDLER(5) HANDLER(6) HANDLER(7)

typedef int (*FlatFn)(Ctx *, uint32_t);

// volatile tables: stop gcc from constant-propagating the contents and
// turning any of the indirect calls back into a direct call / switch.
static FlatFn volatile callTab[NSLOTS];
static FlatFn volatile tailTab[NSLOTS];

static inline int idx(uint32_t op) { return ((op >> 16) & 0xFF0) | ((op >> 4) & 0xF); }

// ── 1. pointer-to-member call (v1.78 baseline) ──────────────────────────────
// The PMF receiver *is* the register file, exactly as Interpreter is.
#define MHANDLER(N)                                                           \
    __attribute__((noinline)) int mh##N(uint32_t op) {                        \
        Ctx *c = (Ctx *)this;                                                 \
        BODY(N * 16 + 1);                                                     \
        return 1;                                                             \
    }
class MemObj {
public:
    uint32_t r[16];
    uint32_t cpsr;
    MHANDLER(0) MHANDLER(1) MHANDLER(2) MHANDLER(3)
    MHANDLER(4) MHANDLER(5) MHANDLER(6) MHANDLER(7)
};
static int (MemObj::*volatile pmfTab[NSLOTS])(uint32_t);

// ── 3. replicated nested switch ─────────────────────────────────────────────
static inline int switchDispatch(Ctx *c, uint32_t op) {
    switch ((op >> 20) & 0xFF) {
#define SWLO(HI)                                                              \
        case HI: switch ((op >> 4) & 0xF) {                                   \
            case 0: return h0(c, op);  case 1: return h1(c, op);              \
            case 2: return h2(c, op);  case 3: return h3(c, op);              \
            case 4: return h4(c, op);  case 5: return h5(c, op);              \
            case 6: return h6(c, op);  default: return h7(c, op);             \
        }
    SWLO(0x00) SWLO(0x01) SWLO(0x02) SWLO(0x03) SWLO(0x04) SWLO(0x05) SWLO(0x06) SWLO(0x07)
    SWLO(0x08) SWLO(0x09) SWLO(0x0A) SWLO(0x0B) SWLO(0x0C) SWLO(0x0D) SWLO(0x0E) SWLO(0x0F)
    SWLO(0x10) SWLO(0x11) SWLO(0x12) SWLO(0x13) SWLO(0x14) SWLO(0x15) SWLO(0x16) SWLO(0x17)
    SWLO(0x18) SWLO(0x19) SWLO(0x1A) SWLO(0x1B) SWLO(0x1C) SWLO(0x1D) SWLO(0x1E) SWLO(0x1F)
    SWLO(0x20) SWLO(0x21) SWLO(0x22) SWLO(0x23) SWLO(0x24) SWLO(0x25) SWLO(0x26) SWLO(0x27)
    SWLO(0x28) SWLO(0x29) SWLO(0x2A) SWLO(0x2B) SWLO(0x2C) SWLO(0x2D) SWLO(0x2E) SWLO(0x2F)
    SWLO(0x30) SWLO(0x31) SWLO(0x32) SWLO(0x33) SWLO(0x34) SWLO(0x35) SWLO(0x36) SWLO(0x37)
    SWLO(0x38) SWLO(0x39) SWLO(0x3A) SWLO(0x3B) SWLO(0x3C) SWLO(0x3D) SWLO(0x3E) SWLO(0x3F)
    SWLO(0x40) SWLO(0x41) SWLO(0x42) SWLO(0x43) SWLO(0x44) SWLO(0x45) SWLO(0x46) SWLO(0x47)
    SWLO(0x48) SWLO(0x49) SWLO(0x4A) SWLO(0x4B) SWLO(0x4C) SWLO(0x4D) SWLO(0x4E) SWLO(0x4F)
    SWLO(0x50) SWLO(0x51) SWLO(0x52) SWLO(0x53) SWLO(0x54) SWLO(0x55) SWLO(0x56) SWLO(0x57)
    SWLO(0x58) SWLO(0x59) SWLO(0x5A) SWLO(0x5B) SWLO(0x5C) SWLO(0x5D) SWLO(0x5E) SWLO(0x5F)
    SWLO(0x60) SWLO(0x61) SWLO(0x62) SWLO(0x63) SWLO(0x64) SWLO(0x65) SWLO(0x66) SWLO(0x67)
    SWLO(0x68) SWLO(0x69) SWLO(0x6A) SWLO(0x6B) SWLO(0x6C) SWLO(0x6D) SWLO(0x6E) SWLO(0x6F)
    SWLO(0x70) SWLO(0x71) SWLO(0x72) SWLO(0x73) SWLO(0x74) SWLO(0x75) SWLO(0x76) SWLO(0x77)
    SWLO(0x78) SWLO(0x79) SWLO(0x7A) SWLO(0x7B) SWLO(0x7C) SWLO(0x7D) SWLO(0x7E) SWLO(0x7F)
    SWLO(0x80) SWLO(0x81) SWLO(0x82) SWLO(0x83) SWLO(0x84) SWLO(0x85) SWLO(0x86) SWLO(0x87)
    SWLO(0x88) SWLO(0x89) SWLO(0x8A) SWLO(0x8B) SWLO(0x8C) SWLO(0x8D) SWLO(0x8E) SWLO(0x8F)
    SWLO(0x90) SWLO(0x91) SWLO(0x92) SWLO(0x93) SWLO(0x94) SWLO(0x95) SWLO(0x96) SWLO(0x97)
    SWLO(0x98) SWLO(0x99) SWLO(0x9A) SWLO(0x9B) SWLO(0x9C) SWLO(0x9D) SWLO(0x9E) SWLO(0x9F)
    SWLO(0xA0) SWLO(0xA1) SWLO(0xA2) SWLO(0xA3) SWLO(0xA4) SWLO(0xA5) SWLO(0xA6) SWLO(0xA7)
    SWLO(0xA8) SWLO(0xA9) SWLO(0xAA) SWLO(0xAB) SWLO(0xAC) SWLO(0xAD) SWLO(0xAE) SWLO(0xAF)
    SWLO(0xB0) SWLO(0xB1) SWLO(0xB2) SWLO(0xB3) SWLO(0xB4) SWLO(0xB5) SWLO(0xB6) SWLO(0xB7)
    SWLO(0xB8) SWLO(0xB9) SWLO(0xBA) SWLO(0xBB) SWLO(0xBC) SWLO(0xBD) SWLO(0xBE) SWLO(0xBF)
    SWLO(0xC0) SWLO(0xC1) SWLO(0xC2) SWLO(0xC3) SWLO(0xC4) SWLO(0xC5) SWLO(0xC6) SWLO(0xC7)
    SWLO(0xC8) SWLO(0xC9) SWLO(0xCA) SWLO(0xCB) SWLO(0xCC) SWLO(0xCD) SWLO(0xCE) SWLO(0xCF)
    SWLO(0xD0) SWLO(0xD1) SWLO(0xD2) SWLO(0xD3) SWLO(0xD4) SWLO(0xD5) SWLO(0xD6) SWLO(0xD7)
    SWLO(0xD8) SWLO(0xD9) SWLO(0xDA) SWLO(0xDB) SWLO(0xDC) SWLO(0xDD) SWLO(0xDE) SWLO(0xDF)
    SWLO(0xE0) SWLO(0xE1) SWLO(0xE2) SWLO(0xE3) SWLO(0xE4) SWLO(0xE5) SWLO(0xE6) SWLO(0xE7)
    SWLO(0xE8) SWLO(0xE9) SWLO(0xEA) SWLO(0xEB) SWLO(0xEC) SWLO(0xED) SWLO(0xEE) SWLO(0xEF)
    SWLO(0xF0) SWLO(0xF1) SWLO(0xF2) SWLO(0xF3) SWLO(0xF4) SWLO(0xF5) SWLO(0xF6) SWLO(0xF7)
    SWLO(0xF8) SWLO(0xF9) SWLO(0xFA) SWLO(0xFB) SWLO(0xFC) SWLO(0xFD) SWLO(0xFE) SWLO(0xFF)
#undef SWLO
    }
    return h0(c, op);
}

// ── 6. tail-call chaining: one body per handler, dispatch at the tail ───────
#define THANDLER(N)                                                           \
    __attribute__((noinline)) static int t##N(Ctx *c, uint32_t op) {          \
        BODY(N * 16 + 1);                                                     \
        if (--c->credit <= 0) return 1;                                       \
        uint32_t nop_ = c->code[(c->pc++) & (CODE - 1)];                       \
        return tailTab[idx(nop_)](c, nop_);   /* gcc -> sibling call */       \
    }
THANDLER(0) THANDLER(1) THANDLER(2) THANDLER(3) THANDLER(4) THANDLER(5) THANDLER(6) THANDLER(7)
static inline int t0_(Ctx *c, uint32_t op) { return t0(c, op); }

// ── table setup ─────────────────────────────────────────────────────────────
static void build() {
    FlatFn hs[8] = { h0, h1, h2, h3, h4, h5, h6, h7 };
    for (int i = 0; i < NSLOTS; i++) callTab[i] = hs[i & 7];
    FlatFn ts[8] = { t0, t1, t2, t3, t4, t5, t6, t7 };
    for (int i = 0; i < NSLOTS; i++) tailTab[i] = ts[i & 7];

    int (MemObj::*ms[8])(uint32_t) = {
        &MemObj::mh0, &MemObj::mh1, &MemObj::mh2, &MemObj::mh3,
        &MemObj::mh4, &MemObj::mh5, &MemObj::mh6, &MemObj::mh7 };
    for (int i = 0; i < NSLOTS; i++) pmfTab[i] = ms[i & 7];

    uint32_t s = 0x12345678u;
    for (int i = 0; i < CODE; i++) {
        s = s * 1664525u + 1013904223u;
        uint32_t hi = (s >> 13) & 0xFF, lo = (s >> 7) & 0xF;
        stream[i] = (hi << 20) | (lo << 4) | 0xE0000000u;
    }
}

// ── the six loops ───────────────────────────────────────────────────────────
static uint32_t runPmf(Ctx *c) {
    MemObj obj; std::memcpy(obj.r, c->r, sizeof(obj.r)); obj.cpsr = c->cpsr;
    uint64_t t0 = PPCGetTickCount();
    for (int i = 0; i < ITERS; i++) {
        uint32_t op = stream[i & (CODE - 1)];
        sink += (obj.*pmfTab[idx(op)])(op);
    }
    std::memcpy(c->r, obj.r, sizeof(c->r)); c->cpsr = obj.cpsr;
    return (uint32_t)PPCTicksToNs(PPCGetTickCount() - t0);
}

static uint32_t runCall(Ctx *c) {
    uint64_t t0 = PPCGetTickCount();
    for (int i = 0; i < ITERS; i++) {
        uint32_t op = stream[i & (CODE - 1)];
        sink += callTab[idx(op)](c, op);
    }
    return (uint32_t)PPCTicksToNs(PPCGetTickCount() - t0);
}

static uint32_t runSwitch(Ctx *c) {
    uint64_t t0 = PPCGetTickCount();
    for (int i = 0; i < ITERS; i++) {
        uint32_t op = stream[i & (CODE - 1)];
        sink += switchDispatch(c, op);
    }
    return (uint32_t)PPCTicksToNs(PPCGetTickCount() - t0);
}

static uint32_t runGotoCall(Ctx *c) {
    static void *lab[NSLOTS];
    for (int i = 0; i < NSLOTS; i++) {
        switch (i & 7) {
            case 0: lab[i] = &&C0; break;  case 1: lab[i] = &&C1; break;
            case 2: lab[i] = &&C2; break;  case 3: lab[i] = &&C3; break;
            case 4: lab[i] = &&C4; break;  case 5: lab[i] = &&C5; break;
            case 6: lab[i] = &&C6; break;  default: lab[i] = &&C7; break;
        }
    }
    uint64_t t0 = PPCGetTickCount();
    int i = 0;
    uint32_t op;
    goto d;
C0: sink += h0(c, op); i++; goto d;
C1: sink += h1(c, op); i++; goto d;
C2: sink += h2(c, op); i++; goto d;
C3: sink += h3(c, op); i++; goto d;
C4: sink += h4(c, op); i++; goto d;
C5: sink += h5(c, op); i++; goto d;
C6: sink += h6(c, op); i++; goto d;
C7: sink += h7(c, op); i++; goto d;
d:
    if (i >= ITERS) goto e;
    op = stream[i & (CODE - 1)];
    goto *lab[idx(op)];
e:
    return (uint32_t)PPCTicksToNs(PPCGetTickCount() - t0);
}

static uint32_t runGotoInline(Ctx *c) {
    // Body inlined at every label: no call, no return.
    static void *lab[NSLOTS];
    for (int i = 0; i < NSLOTS; i++) {
        switch (i & 7) {
            case 0: lab[i] = &&I0; break;  case 1: lab[i] = &&I1; break;
            case 2: lab[i] = &&I2; break;  case 3: lab[i] = &&I3; break;
            case 4: lab[i] = &&I4; break;  case 5: lab[i] = &&I5; break;
            case 6: lab[i] = &&I6; break;  default: lab[i] = &&I7; break;
        }
    }
    uint64_t t0 = PPCGetTickCount();
    int i = 0;
    uint32_t op;
    goto d;
    // Each label: inline body + count + next dispatch.
#define INL(N) I##N: { BODY(N * 16 + 1); i++; goto d; }
    INL(0) INL(1) INL(2) INL(3) INL(4) INL(5) INL(6) INL(7)
#undef INL
d:
    if (i >= ITERS) goto e;
    op = stream[i & (CODE - 1)];
    goto *lab[idx(op)];
e:
    return (uint32_t)PPCTicksToNs(PPCGetTickCount() - t0);
}

static uint32_t runTail(Ctx *c) {
    // Entered exactly once; every subsequent instruction is reached by the
    // sibling call at the tail of the previous handler.  No shared dispatcher.
    uint64_t t0 = PPCGetTickCount();
    uint32_t op = stream[0];
    c->code = stream;
    c->pc = 1;
    c->credit = ITERS - 1;
    sink += t0_(c, op);
    return (uint32_t)PPCTicksToNs(PPCGetTickCount() - t0);
}

struct Result { const char *name; uint32_t ns; };

void run() {
    build();
    Ctx c;
    std::memset(&c, 0, sizeof(c));
    c.code = stream;

    // Warm up every path once so first-run code paging is excluded.
    runPmf(&c); runCall(&c); runSwitch(&c); runGotoCall(&c); runGotoInline(&c);
    c.credit = 4096; runTail(&c);

    Result r[6];
    r[0] = { "pmf_call",        runPmf(&c) };
    r[1] = { "flat_call",       runCall(&c) };
    r[2] = { "repl_switch",     runSwitch(&c) };
    r[3] = { "goto_then_call",  runGotoCall(&c) };
    r[4] = { "goto_inline",     runGotoInline(&c) };
    r[5] = { "tailcall_chain",  runTail(&c) };

    FILE *f = fopen("sd:/dispatch_bench.log", "w");
    for (int pass = 0; pass < 2; pass++) {
        FILE *o = pass ? stdout : f;
        if (!o) continue;
        fprintf(o, "[DISPATCH_BENCH] weight=%d iters=%d\n", (int)BENCH_WEIGHT, (int)ITERS);
        for (int i = 0; i < 6; i++)
            fprintf(o, "[DISPATCH_BENCH] %-16s ns=%u ns_per_dispatch=%.2f\n",
                    r[i].name, r[i].ns, (double)r[i].ns / ITERS);
        fflush(o);
    }
    if (f) fclose(f);

    for (int i = 0; i < 6; i++)
        SYS_Report("[DISPATCH_BENCH] w=%d %s ns=%.2f\n",
                   (int)BENCH_WEIGHT, r[i].name, (double)r[i].ns / ITERS);
}

}

void NoodsRunDispatchBench() { DispatchBench2::run(); }

#else
void NoodsRunDispatchBench() {}
#endif
