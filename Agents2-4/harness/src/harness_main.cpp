/*
    Host-side (PPC-Linux) regression harness for the NooDS-Wii ARM->PowerPC JIT.

    Runs the real emulation core (interpreter + JIT, GPU/SPU included) frame by
    frame and emits the same checkpoint records as the Wii G2 harness
    (main.cpp: NoodsTestHarness::onFrameEnd) so interpreter and JIT builds can be
    compared field-by-field.

    Deterministic by construction: audio, threaded 2D/3D and the FPS limiter are
    disabled, so guest state does not depend on host thread scheduling.

    Usage:
      noods_host --rom <nds> [--gba <gba>] --frames N [--checkpoints a,b,c]
                 [--out file] [--pool-words W] [--mem-dump file] [--fb-dump file]
                 [--stats]
*/
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <time.h>

#include "core.h"
#include "defines.h"
#include "settings.h"
#include "arm_jit.h"

// Same geometry the Wii frontend uses for the emulated video output.
#define NDS_SCREEN_WIDTH  256
#define NDS_SCREEN_HEIGHT 192

static uint64_t fnv1a64(const void *data, size_t len, uint64_t h = 1469598103934665603ULL) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t nowUs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

// Memory and Interpreter both declare `friend struct NoodsTestHarness`, which is
// how the Wii build's test harness reads guest state. Mirror that structure here.
struct NoodsTestHarness {
    static void dumpCpuLine(FILE *f, const char *label, Interpreter &cpu) {
        uint32_t spsrVal = cpu.spsr ? *cpu.spsr : 0;
        fprintf(f, "%s cpsr=%08X spsr=%08X pc=%08X pipe0=%08X pipe1=%08X halted=%u ime=%u ie=%08X irf=%08X postFlg=%u cycles=%u",
                label, cpu.cpsr, spsrVal, *cpu.registers[15], cpu.pipeline[0], cpu.pipeline[1],
                (unsigned)cpu.halted, (unsigned)cpu.ime, cpu.ie, cpu.irf, (unsigned)cpu.postFlg, cpu.cycles);
        for (int r = 0; r < 16; r++)
            fprintf(f, " r%d=%08X", r, cpu.registers[r] ? *cpu.registers[r] : cpu.registersUsr[r]);
        for (int r = 0; r < 16; r++)
            fprintf(f, " usr%d=%08X", r, cpu.registersUsr[r]);
        fprintf(f, " svc13=%08X svc14=%08X irq13=%08X irq14=%08X fiq13=%08X fiq14=%08X\n",
                cpu.registersSvc[0], cpu.registersSvc[1],
                cpu.registersIrq[0], cpu.registersIrq[1],
                cpu.registersFiq[5], cpu.registersFiq[6]);
    }

    static void dumpCheckpoint(FILE *f, Core *core, uint32_t frame, size_t ramLen,
                               uint64_t accUs, uint32_t framesProduced, const uint32_t *fb) {
        Memory &mem = core->memory;
        uint64_t hRam   = fnv1a64(mem.ram, ramLen);
        uint64_t hWram  = fnv1a64(mem.wram, sizeof(mem.wram));
        uint64_t hItcm  = fnv1a64(mem.instrTcm, sizeof(mem.instrTcm));
        uint64_t hDtcm  = fnv1a64(mem.dataTcm, sizeof(mem.dataTcm));
        uint64_t hWram7 = fnv1a64(mem.wram7, sizeof(mem.wram7));
        uint64_t hPal   = fnv1a64(mem.palette, sizeof(mem.palette));
        uint64_t hOam   = fnv1a64(mem.oam, sizeof(mem.oam));
        uint64_t hVram  = fnv1a64(mem.vramA, sizeof(mem.vramA));
        hVram = fnv1a64(mem.vramB, sizeof(mem.vramB), hVram);
        hVram = fnv1a64(mem.vramC, sizeof(mem.vramC), hVram);
        hVram = fnv1a64(mem.vramD, sizeof(mem.vramD), hVram);
        hVram = fnv1a64(mem.vramE, sizeof(mem.vramE), hVram);
        hVram = fnv1a64(mem.vramF, sizeof(mem.vramF), hVram);
        hVram = fnv1a64(mem.vramG, sizeof(mem.vramG), hVram);
        hVram = fnv1a64(mem.vramH, sizeof(mem.vramH), hVram);
        hVram = fnv1a64(mem.vramI, sizeof(mem.vramI), hVram);
        uint64_t hTotalMem = hRam ^ (hWram << 1) ^ (hItcm << 2) ^ (hDtcm << 3) ^
                             (hWram7 << 4) ^ (hPal << 5) ^ (hOam << 6) ^ (hVram << 7);

        size_t scrPixels = (size_t)NDS_SCREEN_WIDTH * NDS_SCREEN_HEIGHT;
        uint64_t hFbTop = fnv1a64(fb, scrPixels * sizeof(uint32_t));
        uint64_t hFbBot = fnv1a64(fb + scrPixels, scrPixels * sizeof(uint32_t));
        uint64_t hFbAll = fnv1a64(fb, scrPixels * 2 * sizeof(uint32_t));

        fprintf(f, "=== CHECKPOINT FRAME %u ===\n", frame);
        fprintf(f, "CYCLES global=%u arm9=%u arm7=%u gbaMode=%d\n",
                core->globalCycles, core->interpreter[0].cycles, core->interpreter[1].cycles, (int)core->gbaMode);
        dumpCpuLine(f, "ARM9", core->interpreter[0]);
        dumpCpuLine(f, "ARM7", core->interpreter[1]);
        fprintf(f, "MEM_HASH ram=%016llX wram=%016llX itcm=%016llX dtcm=%016llX wram7=%016llX pal=%016llX vram=%016llX oam=%016llX total=%016llX\n",
                (unsigned long long)hRam, (unsigned long long)hWram,
                (unsigned long long)hItcm, (unsigned long long)hDtcm,
                (unsigned long long)hWram7, (unsigned long long)hPal,
                (unsigned long long)hVram, (unsigned long long)hOam,
                (unsigned long long)hTotalMem);
        fprintf(f, "FB_HASH top=%016llX bottom=%016llX combined=%016llX\n",
                (unsigned long long)hFbTop, (unsigned long long)hFbBot, (unsigned long long)hFbAll);
        fprintf(f, "PERF frame=%u accum_us=%llu avg_frame_us=%llu frames_produced=%u\n",
                frame, (unsigned long long)accUs, (unsigned long long)(accUs / frame), framesProduced);
#if NOODS_JIT
        ArmPpcJit::dumpStats(f);
#else
        fprintf(f, "JIT_STATS enabled=0\n");
#endif
        fflush(f);
    }

    static void dumpMemory(FILE *mf, Core *core, size_t ramLen) {
        Memory &mem = core->memory;
        fwrite(mem.wram, 1, sizeof(mem.wram), mf);
        fwrite(mem.instrTcm, 1, sizeof(mem.instrTcm), mf);
        fwrite(mem.dataTcm, 1, sizeof(mem.dataTcm), mf);
        fwrite(mem.wram7, 1, sizeof(mem.wram7), mf);
        fwrite(mem.ram, 1, ramLen, mf);
    }
};

struct Opts {
    std::string rom, gba, out, memDump, fbDump;
    uint32_t frames = 0;
    size_t poolWords = 0;
    std::vector<uint32_t> checkpoints;
    bool stats = false;
};

int main(int argc, char **argv) {
    Opts o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--rom") o.rom = next();
        else if (a == "--gba") o.gba = next();
        else if (a == "--frames") o.frames = (uint32_t)strtoul(next().c_str(), nullptr, 10);
        else if (a == "--out") o.out = next();
        else if (a == "--pool-words") o.poolWords = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--mem-dump") o.memDump = next();
        else if (a == "--fb-dump") o.fbDump = next();
        else if (a == "--stats") o.stats = true;
        else if (a == "--checkpoints") {
            std::string s = next(), cur;
            for (char c : s + ",") {
                if (c == ',') { if (!cur.empty()) o.checkpoints.push_back((uint32_t)strtoul(cur.c_str(), nullptr, 10)); cur.clear(); }
                else cur += c;
            }
        } else {
            fprintf(stderr, "unknown arg %s\n", a.c_str());
            return 2;
        }
    }
    if (o.out.empty()) o.out = "/dev/stdout";

    // Determinism: no audio thread, no per-frame render threads, no FPS limiter.
    Settings::emulateAudio = 0;
    Settings::threaded2D  = 0;
    Settings::threaded3D  = 0;
    Settings::fpsLimiter  = 0;
    Settings::frameskip   = 0;
    Settings::highRes3D   = 0;

#if NOODS_JIT
    ArmPpcJit::init();
    if (o.poolWords) ArmPpcJit::setPoolLimitWords(o.poolWords);
#endif

    FILE *f = fopen(o.out.c_str(), "wb");
    if (!f) { fprintf(stderr, "HARNESS=FAIL cannot_open_out\n"); return 1; }

    Core *core = nullptr;
    try {
        core = new Core(o.rom, o.gba);
    } catch (...) {
        fprintf(stderr, "HARNESS=FAIL rom_load\n");
        return 1;
    }

    static uint32_t tempBuffer[NDS_SCREEN_WIDTH * NDS_SCREEN_HEIGHT * 2];
    memset(tempBuffer, 0, sizeof(tempBuffer));

    fprintf(stderr, "HARNESS=START rom=%s gba=%s frames=%u jit=%d blocks=%d pool=%zu\n",
            o.rom.c_str(), o.gba.c_str(), o.frames, (int)NOODS_JIT, (int)NOODS_JIT_BLOCKS, o.poolWords);

    uint64_t t0 = nowUs(), accUs = 0;
    uint32_t frame = 0, framesProduced = 0, lastFrame = 0;
    size_t ramLen = core->gbaMode ? 0x40000 : 0x400000;

    for (frame = 1; o.frames == 0 || frame <= o.frames; frame++) {
        uint64_t f0 = nowUs();
        core->runCore();
        bool gotFrame = core->gpu.getFrame(tempBuffer, core->gbaMode);
        uint64_t f1 = nowUs();
        accUs += f1 - f0;
        if (gotFrame) framesProduced++;
        lastFrame = frame;

        if (std::find(o.checkpoints.begin(), o.checkpoints.end(), frame) != o.checkpoints.end())
            NoodsTestHarness::dumpCheckpoint(f, core, frame, ramLen, accUs, framesProduced, tempBuffer);

        if (o.frames && frame == o.frames)
            break;
    }

    if (!o.memDump.empty()) {
        if (FILE *mf = fopen(o.memDump.c_str(), "wb")) {
            NoodsTestHarness::dumpMemory(mf, core, ramLen);
            fclose(mf);
        }
    }
    if (!o.fbDump.empty()) {
        if (FILE *bf = fopen(o.fbDump.c_str(), "wb")) {
            fwrite(tempBuffer, 1, (size_t)NDS_SCREEN_WIDTH * NDS_SCREEN_HEIGHT * 2 * sizeof(uint32_t), bf);
            fclose(bf);
        }
    }

    uint64_t totalUs = nowUs() - t0;
    fprintf(f, "=== RUN SUMMARY ===\n");
    fprintf(f, "FRAMES=%u EMITTED=%u TOTAL_US=%llu AVG_US=%llu\n",
            lastFrame, framesProduced, (unsigned long long)totalUs,
            (unsigned long long)(totalUs / (lastFrame ? lastFrame : 1)));
#if NOODS_JIT
    if (o.stats) ArmPpcJit::dumpStats(f);
#endif
    fclose(f);

    fprintf(stderr, "HARNESS=DONE frames=%u emitted=%u total_us=%llu avg_us=%llu\n",
            lastFrame, framesProduced, (unsigned long long)totalUs,
            (unsigned long long)(totalUs / (lastFrame ? lastFrame : 1)));
    delete core;
    fprintf(stderr, "STATUS=PASS\n");
    return 0;
}
