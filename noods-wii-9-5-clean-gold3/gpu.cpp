// gpu.cpp (fixed: member function definitions restored)
#include <cstring>
#include <algorithm>
#include "core.h"

extern "C" {
    #include <tuxedo/ppc/intrinsics.h>
}

#define PPC_LIKELY(x)   __builtin_expect(!!(x), 1)
#define PPC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define ALWAYS_INLINE   __attribute__((always_inline)) inline
#define HOT             __attribute__((hot))
#define COLD            __attribute__((cold))

// ── Construction / destruction ────────────────────────────────────────────────
Gpu::Gpu(Core *core) : core(core) {
    ready   = false;
    running = false;
    drawing = 0;
}

Gpu::~Gpu() {
    if (gpuThreadStack) {
        running = false;
        KThreadJoin(&gpuThread);
        delete[] gpuThreadStack;
        gpuThreadStack = nullptr;
    }
    while (!framebuffers.empty()) {
        Buffers &b = framebuffers.front();
        delete[] b.framebuffer;
        delete[] b.hiRes3D;
        framebuffers.pop();
    }
}

// ── Thread entry ──────────────────────────────────────────────────────────────
sptr Gpu::GpuThreadEntry(void *arg) {
    Gpu *gpuObj = static_cast<Gpu*>(arg);
    if (gpuObj->core->gbaMode)
        gpuObj->drawGbaThreaded();
    else
        gpuObj->drawThreaded();
    return 0;
}

// ── State serialisation ───────────────────────────────────────────────────────
void Gpu::saveState(FILE *file) {
    fwrite(dispStat,    2, 2,              file);
    fwrite(&vCount,     sizeof(vCount),    1,   file);
    fwrite(&dispCapCnt, sizeof(dispCapCnt),1,   file);
    fwrite(&powCnt1,    sizeof(powCnt1),   1,   file);
}

void Gpu::loadState(FILE *file) {
    fread(dispStat,    2, 2,              file);
    fread(&vCount,     sizeof(vCount),    1,   file);
    fread(&dispCapCnt, sizeof(dispCapCnt),1,   file);
    fread(&powCnt1,    sizeof(powCnt1),   1,   file);
}

// ── Colour conversion – static MEMBER definitions (match gpu.h declarations) ─
//
// Declared as:  static uint32_t rgb5ToRgb8(uint32_t color);
//               static uint32_t rgb6ToRgb8(uint32_t color);
//               static uint16_t rgb6ToRgb5(uint32_t color);
// in gpu.h.  They MUST be defined as Gpu:: members or the linker errors recur.
// ALWAYS_INLINE causes the compiler to inline every call site while still
// emitting the symbol for any TU that takes its address.

__attribute__((optimize("O3")))
uint32_t Gpu::rgb5ToRgb8(uint32_t color)
{
    // 5-bit -> 8-bit via bit replication: (v<<3)|(v>>2)
    // exact for all 32 values: 0->0, 31->255
    const uint32_t r5 = (color >>  0) & 0x1Fu;
    const uint32_t g5 = (color >>  5) & 0x1Fu;
    const uint32_t b5 = (color >> 10) & 0x1Fu;
    const uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2));
    const uint8_t g = (uint8_t)((g5 << 3) | (g5 >> 2));
    const uint8_t b = (uint8_t)((b5 << 3) | (b5 >> 2));
    // Output 0xAABBGGRR
    return (0xFFu << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

__attribute__((optimize("O3")))
uint32_t Gpu::rgb6ToRgb8(uint32_t color)
{
    // 6-bit -> 8-bit via bit replication: (v<<2)|(v>>4)
    // exact for all 64 values: 0->0, 63->255
    const uint32_t r6 = (color >>  0) & 0x3Fu;
    const uint32_t g6 = (color >>  6) & 0x3Fu;
    const uint32_t b6 = (color >> 12) & 0x3Fu;
    const uint8_t r = (uint8_t)((r6 << 2) | (r6 >> 4));
    const uint8_t g = (uint8_t)((g6 << 2) | (g6 >> 4));
    const uint8_t b = (uint8_t)((b6 << 2) | (b6 >> 4));
    // Output 0xAABBGGRR
    return (0xFFu << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

__attribute__((optimize("O3")))
uint16_t Gpu::rgb6ToRgb5(uint32_t color)
{
    // Input:  bits[5:0]=r6,  bits[11:6]=g6,  bits[17:12]=b6
    // Output: bits[4:0]=r5,  bits[9:5]=g5,   bits[14:10]=b5
    // Each channel: drop LSB (right-shift 1) then repack
    const uint16_t r = (uint16_t)((color >>  1) & 0x1Fu);
    const uint16_t g = (uint16_t)((color >>  7) & 0x1Fu);
    const uint16_t b = (uint16_t)((color >> 13) & 0x1Fu);
    return (uint16_t)(BIT(15) | (b << 10) | (g << 5) | r);
}

// ── getFrame ──────────────────────────────────────────────────────────────────
HOT bool Gpu::getFrame(uint32_t *out, bool gbaCrop) {
    if (PPC_UNLIKELY(!ready))
        return false;

    Buffers &buffers = framebuffers.front();

    if (gbaCrop) {
        if (Settings::highRes3D || Settings::screenFilter == 1) {
            for (int y = 0; y < 160; y++) {
                uint32_t *line = &out[y * 240 * 4];
                const uint32_t *src = &buffers.framebuffer[y * 256];
                for (int x = 0; x < 240; x++) {
                    if ((x & 3) == 0)
                        asm volatile ("dcbt 0,%0" : : "r"(src + x + 4) : "memory");
                    uint32_t px = rgb5ToRgb8(src[x]);
                    line[x * 2]     = px;
                    line[x * 2 + 1] = px;
                }
                memcpy(&line[240 * 2], line, 240 * 8);
            }
        } else {
            for (int y = 0; y < 160; y++) {
                const uint32_t *src = &buffers.framebuffer[y * 256];
                uint32_t *dst = &out[y * 240];
                for (int x = 0; x < 240; x++) {
                    if ((x & 7) == 0)
                        asm volatile ("dcbt 0,%0" : : "r"(src + x + 8) : "memory");
                    dst[x] = rgb5ToRgb8(src[x]);
                }
            }
        }
    }
    else if (core->gbaMode) {
        int      offset = (powCnt1 & BIT(15)) ? 0 : (256 * 192);
        uint32_t base   = 0x6800000 + (uint32_t)gbaBlock * 0x20000;
        gbaBlock = !gbaBlock;

        if (Settings::highRes3D || Settings::screenFilter == 1) {
            for (int y = 0; y < 192; y++) {
                uint32_t *line = &out[(offset + y * 256) * 4];
                for (int x = 0; x < 256; x++) {
                    bool inGba = (x >= 8 && x < 248 && y >= 16 && y < 176);
                    uint32_t px = inGba
                        ? rgb5ToRgb8(buffers.framebuffer[(y - 16) * 256 + x - 8])
                        : rgb5ToRgb8(core->memory.read<uint16_t>(0, base + (y * 256 + x) * 2));
                    line[x * 2]     = px;
                    line[x * 2 + 1] = px;
                }
                memcpy(&line[256 * 2], line, 256 * 8);
            }
            memset(&out[(256 * 192 - offset) * 4], 0,
                   256 * 192 * 4 * sizeof(uint32_t));
        } else {
            for (int y = 0; y < 192; y++) {
                uint32_t *row = &out[offset + y * 256];
                for (int x = 0; x < 256; x++) {
                    bool inGba = (x >= 8 && x < 248 && y >= 16 && y < 176);
                    row[x] = inGba
                        ? rgb5ToRgb8(buffers.framebuffer[(y - 16) * 256 + x - 8])
                        : rgb5ToRgb8(core->memory.read<uint16_t>(0, base + (y * 256 + x) * 2));
                }
            }
            memset(&out[256 * 192 - offset], 0, 256 * 192 * sizeof(uint32_t));
        }
    }
    else {
        if (Settings::highRes3D || Settings::screenFilter == 1) {
            if (buffers.hiRes3D) {
                for (int y = 0; y < 192 * 2; y++) {
                    const uint32_t *srcRow = &buffers.framebuffer[y * 256];
                    for (int x = 0; x < 256; x++) {
                        if ((x & 3) == 0)
                            asm volatile ("dcbt 0,%0" : : "r"(srcRow + x + 4) : "memory");
                        uint32_t value = srcRow[x];
                        int i = (y * 2) * (256 * 2) + (x * 2);
                        if (value & BIT(26)) {
                            auto pickHi = [&](int idx) -> uint32_t {
                                uint32_t v2 = buffers.hiRes3D[idx % (256 * 192 * 4)];
                                return rgb6ToRgb8((v2 & 0xFC0000) ? v2 : value);
                            };
                            out[i + 0]   = pickHi(i + 0);
                            out[i + 1]   = pickHi(i + 1);
                            out[i + 512] = pickHi(i + 512);
                            out[i + 513] = pickHi(i + 513);
                        } else {
                            uint32_t c = rgb6ToRgb8(value);
                            out[i + 0] = out[i + 1] = c;
                            out[i + 512] = out[i + 513] = c;
                        }
                    }
                }
            } else {
                for (int y = 0; y < 192 * 2; y++) {
                    uint32_t *line = &out[y * 256 * 4];
                    const uint32_t *src = &buffers.framebuffer[y * 256];
                    for (int x = 0; x < 256; x++) {
                        if ((x & 7) == 0)
                            asm volatile ("dcbt 0,%0" : : "r"(src + x + 8) : "memory");
                        uint32_t px = rgb6ToRgb8(src[x]);
                        line[x * 2]     = px;
                        line[x * 2 + 1] = px;
                    }
                    memcpy(&line[256 * 2], line, 256 * 8);
                }
            }
        } else {
            const int total        = 256 * 192 * 2;
            const uint32_t *src    = buffers.framebuffer;
            int i = 0;
            for (; i + 8 <= total; i += 8) {
                asm volatile ("dcbt 0,%0" : : "r"(src + i + 16) : "memory");
                out[i+0] = rgb6ToRgb8(src[i+0]);
                out[i+1] = rgb6ToRgb8(src[i+1]);
                out[i+2] = rgb6ToRgb8(src[i+2]);
                out[i+3] = rgb6ToRgb8(src[i+3]);
                out[i+4] = rgb6ToRgb8(src[i+4]);
                out[i+5] = rgb6ToRgb8(src[i+5]);
                out[i+6] = rgb6ToRgb8(src[i+6]);
                out[i+7] = rgb6ToRgb8(src[i+7]);
            }
            for (; i < total; i++)
                out[i] = rgb6ToRgb8(src[i]);
        }
    }

    // ── Screen ghosting ───────────────────────────────────────────────────────
    if (PPC_UNLIKELY(Settings::screenGhost)) {
        static uint32_t prev[256 * 192 * 8];
        const uint32_t width  = (gbaCrop ? 240u : 256u)
                              << (uint32_t)(Settings::highRes3D || Settings::screenFilter == 1);
        const uint32_t height = (gbaCrop ? 160u : 384u)
                              << (uint32_t)(Settings::highRes3D || Settings::screenFilter == 1);
        const uint32_t size   = width * height;

        uint32_t i = 0;
        for (; i + 4 <= size; i += 4) {
            asm volatile ("dcbt 0,%0" : : "r"(&out[i + 8]) : "memory");
            for (int k = 0; k < 4; k++) {
                uint32_t p = out[i+k], q = prev[i+k];
                uint8_t r = (uint8_t)(((p & 0xFF)       + (q & 0xFF))       >> 1);
                uint8_t g = (uint8_t)((((p>>8) & 0xFF)  + ((q>>8) & 0xFF))  >> 1);
                uint8_t b = (uint8_t)((((p>>16) & 0xFF) + ((q>>16) & 0xFF)) >> 1);
                prev[i+k] = p;
                out[i+k]  = 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
            }
        }
        for (; i < size; i++) {
            uint32_t p = out[i], q = prev[i];
            uint8_t r = (uint8_t)(((p & 0xFF)       + (q & 0xFF))       >> 1);
            uint8_t g = (uint8_t)((((p>>8) & 0xFF)  + ((q>>8) & 0xFF))  >> 1);
            uint8_t b = (uint8_t)((((p>>16) & 0xFF) + ((q>>16) & 0xFF)) >> 1);
            prev[i] = p;
            out[i]  = 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
        }
    }

    delete[] buffers.framebuffer;
    delete[] buffers.hiRes3D;

    PPCIrqState st = PPCIrqLockByMsr();
    framebuffers.pop();
    ready = !framebuffers.empty();
    PPCIrqUnlockByMsr(st);
    return true;
}

// ── GBA scanline callbacks ────────────────────────────────────────────────────
void Gpu::gbaScanline240() {
    if (vCount < 160) {
        if (PPC_LIKELY(gpuThreadStack)) {
            while (drawing != 0)
                KThreadYield();
        } else if (frames == 0) {
            core->gpu2D[0].drawGbaScanline(vCount);
        }
        core->dma[1].trigger(2);
    }

    dispStat[1] |= BIT(1);
    if (dispStat[1] & BIT(4))
        core->interpreter[1].sendInterrupt(1);

    core->schedule(GBA_SCANLINE240, 308 * 4);
}

void Gpu::gbaScanline308() {
    dispStat[1] &= ~BIT(1);

    switch (++vCount) {
    case 160:
        if (gpuThreadStack) {
            running = false;
            KThreadJoin(&gpuThread);
            delete[] gpuThreadStack;
            gpuThreadStack = nullptr;
        }

        dispStat[1] |= BIT(0);
        if (dispStat[1] & BIT(3))
            core->interpreter[1].sendInterrupt(0);

        core->dma[1].trigger(1);

        if (frames == 0 && framebuffers.size() < 2) {
            Buffers buffers;
            buffers.framebuffer = new uint32_t[256 * 160];
            memcpy(buffers.framebuffer,
                   core->gpu2D[0].getFramebuffer(),
                   256 * 160 * sizeof(uint32_t));

            PPCIrqState st = PPCIrqLockByMsr();
            framebuffers.push(buffers);
            ready = true;
            PPCIrqUnlockByMsr(st);
        }

        if (frames++ >= Settings::frameskip)
            frames = 0;

        core->endFrame();
        break;

    case 227:
        dispStat[1] &= ~BIT(0);
        break;

    case 228:
        vCount = 0;
        core->gpu2D[0].reloadRegisters();

        if (Settings::threaded2D && frames == 0 && !gpuThreadStack) {
            running        = true;
            gpuThreadStack = new u8[16384];
            KThreadPrepare(&gpuThread, GpuThreadEntry, this,
                           gpuThreadStack + 16384, 15);
            KThreadResume(&gpuThread);
        }
        break;
    }

    core->gpu2D[0].updateWindows(vCount);

    if (vCount < 160 && gpuThreadStack)
        drawing = 1;

    if (vCount == (dispStat[1] >> 8)) {
        dispStat[1] |= BIT(2);
        if (dispStat[1] & BIT(5))
            core->interpreter[1].sendInterrupt(2);
    } else if (dispStat[1] & BIT(2)) {
        dispStat[1] &= ~BIT(2);
    }

    core->schedule(GBA_SCANLINE308, 308 * 4);
}

// ── NDS scanline callbacks ────────────────────────────────────────────────────
void Gpu::scanline256() {
    if (vCount < 192) {
        if (PPC_LIKELY(gpuThreadStack)) {
            while (drawing == 1)
                KThreadYield();

            PPCIrqState st          = PPCIrqLockByMsr();
            int         cur_drawing = drawing;
            drawing                 = 3;
            PPCIrqUnlockByMsr(st);

            switch (cur_drawing) {
            case 2:
                core->gpu2D[1].drawScanline(vCount);
                // fall through
            case 3:
                while (drawing != 0)
                    KThreadYield();
                break;
            }
        } else if (frames == 0) {
            core->gpu2D[0].drawScanline(vCount);
            core->gpu2D[1].drawScanline(vCount);
        }

        core->dma[0].trigger(2);

        if (vCount == 0 && (dispCapCnt & BIT(31)))
            displayCapture = true;

        if (PPC_UNLIKELY(displayCapture)) {
            static const uint16_t sizes[] = {
                128,128, 256,64, 256,128, 256,192
            };
            const uint16_t *size   = &sizes[(dispCapCnt >> 19) & 0x6];
            const uint16_t  width  = size[0];
            const uint16_t  height = size[1];

            uint32_t base        = 0x6800000 + ((dispCapCnt & 0x00030000) >> 16) * 0x20000;
            uint32_t writeOffset = ((dispCapCnt & 0x000C0000) >> 3) + vCount * width * 2;

            switch ((dispCapCnt & 0x60000000) >> 29) {
            case 0: {
                uint32_t *source   = (dispCapCnt & BIT(24))
                    ? core->gpu3DRenderer.getLine(vCount)
                    : core->gpu2D[0].getRawLine();
                bool resShift = (Settings::highRes3D && (dispCapCnt & BIT(24)));
                for (int i = 0; i < width; i++)
                    core->memory.write<uint16_t>(0,
                        base + ((writeOffset + i * 2) & 0x1FFFF),
                        rgb6ToRgb5(source[i << resShift]));
                break;
            }
            case 1: {
                if (dispCapCnt & BIT(25)) {
                    LOG_CRIT("Unimplemented display capture: display FIFO\n");
                    break;
                }
                uint32_t readOffset = ((dispCapCnt & 0x0C000000) >> 11) + vCount * width * 2;
                for (int i = 0; i < width; i++) {
                    uint16_t color = core->memory.read<uint16_t>(0,
                        base + ((readOffset + i * 2) & 0x1FFFF));
                    core->memory.write<uint16_t>(0,
                        base + ((writeOffset + i * 2) & 0x1FFFF), color);
                }
                break;
            }
            default: {
                if (dispCapCnt & BIT(25)) {
                    LOG_CRIT("Unimplemented display capture: display FIFO\n");
                    break;
                }
                uint32_t *source = (dispCapCnt & BIT(24))
                    ? core->gpu3DRenderer.getLine(vCount)
                    : core->gpu2D[0].getRawLine();
                bool resShift    = (Settings::highRes3D && (dispCapCnt & BIT(24)));
                uint32_t readOffset = ((dispCapCnt & 0x0C000000) >> 11) + vCount * width * 2;
                uint8_t eva = (uint8_t)std::min((dispCapCnt >>  0) & 0x1Fu, 16u);
                uint8_t evb = (uint8_t)std::min((dispCapCnt >>  8) & 0x1Fu, 16u);

                for (int i = 0; i < width; i++) {
                    uint16_t c1 = rgb6ToRgb5(source[i << resShift]);
                    uint16_t c2 = core->memory.read<uint16_t>(0,
                        base + ((readOffset + i * 2) & 0x1FFFF));
                    uint8_t r = (uint8_t)std::min(
                        (((c1>>0)&0x1F)*eva + ((c2>>0)&0x1F)*evb) / 16u, 31u);
                    uint8_t g = (uint8_t)std::min(
                        (((c1>>5)&0x1F)*eva + ((c2>>5)&0x1F)*evb) / 16u, 31u);
                    uint8_t b = (uint8_t)std::min(
                        (((c1>>10)&0x1F)*eva + ((c2>>10)&0x1F)*evb) / 16u, 31u);
                    uint16_t color = (uint16_t)(BIT(15)|(b<<10)|(g<<5)|r);
                    core->memory.write<uint16_t>(0,
                        base + ((writeOffset + i * 2) & 0x1FFFF), color);
                }
                break;
            }}

            if (vCount + 1 == height) {
                displayCapture = false;
                dispCapCnt    &= ~BIT(31);
            }
        }
    }

    if (frames == 0 && dirty3D &&
        (core->gpu2D[0].readDispCnt() & BIT(3)) &&
        ((vCount + 48) % 263) < 192)
    {
        if (vCount == 215) dirty3D  = BIT(1);
        core->gpu3DRenderer.drawScanline((vCount + 48) % 263);
        if (vCount == 143) dirty3D &= ~BIT(1);
    }

    for (int i = 0; i < 2; i++) {
        dispStat[i] |= BIT(1);
        if (dispStat[i] & BIT(4))
            core->interpreter[i].sendInterrupt(1);
    }

    core->schedule(NDS_SCANLINE256, 355 * 6);
}

void Gpu::scanline355() {
    switch (++vCount) {
    case 192: {
        if (gpuThreadStack) {
            running = false;
            KThreadJoin(&gpuThread);
            delete[] gpuThreadStack;
            gpuThreadStack = nullptr;
        }

        for (int i = 0; i < 2; i++) {
            dispStat[i] |= BIT(0);
            if (dispStat[i] & BIT(3))
                core->interpreter[i].sendInterrupt(0);
            core->dma[i].trigger(1);
        }

        if (core->gpu3D.shouldSwap())
            core->gpu3D.swapBuffers();

        if (frames == 0 && framebuffers.size() < 2) {
            Buffers buffers;
            buffers.framebuffer = new uint32_t[256 * 192 * 2];
            buffers.hiRes3D     = nullptr;

            if (powCnt1 & BIT(0)) {
                const uint32_t *top = (powCnt1 & BIT(15))
                    ? core->gpu2D[0].getFramebuffer()
                    : core->gpu2D[1].getFramebuffer();
                const uint32_t *bot = (powCnt1 & BIT(15))
                    ? core->gpu2D[1].getFramebuffer()
                    : core->gpu2D[0].getFramebuffer();
                memcpy(&buffers.framebuffer[0],         top, 256*192*sizeof(uint32_t));
                memcpy(&buffers.framebuffer[256 * 192], bot, 256*192*sizeof(uint32_t));
            } else {
                memset(buffers.framebuffer, 0, 256*192*2*sizeof(uint32_t));
            }

            if (Settings::highRes3D && (core->gpu2D[0].readDispCnt() & BIT(3))) {
                buffers.hiRes3D = new uint32_t[256 * 192 * 4];
                memcpy(buffers.hiRes3D,
                       core->gpu3DRenderer.getLine(0),
                       256 * 192 * 4 * sizeof(uint32_t));
                buffers.top3D = (powCnt1 & BIT(15));
            }

            PPCIrqState st = PPCIrqLockByMsr();
            framebuffers.push(buffers);
            ready = true;
            PPCIrqUnlockByMsr(st);
        }

        if (frames++ >= Settings::frameskip)
            frames = 0;

        core->actionReplay.applyCheats();
        core->endFrame();
        break;
    }
    case 262:
        dispStat[0] &= ~BIT(0);
        dispStat[1] &= ~BIT(0);
        break;

    case 263:
        vCount = 0;
        core->gpu2D[0].reloadRegisters();
        core->gpu2D[1].reloadRegisters();

        if (Settings::threaded2D && frames == 0 && !gpuThreadStack) {
            running        = true;
            gpuThreadStack = new u8[16384];
            KThreadPrepare(&gpuThread, GpuThreadEntry, this,
                           gpuThreadStack + 16384, 15);
            KThreadResume(&gpuThread);
        }
        break;
    }

    core->gpu2D[0].updateWindows(vCount);
    core->gpu2D[1].updateWindows(vCount);

    if (vCount < 192 && gpuThreadStack)
        drawing = 1;

    for (int i = 0; i < 2; i++) {
        uint16_t vcmp = (uint16_t)((dispStat[i] >> 8) | ((dispStat[i] & BIT(7)) << 1));
        if (vCount == vcmp) {
            dispStat[i] |= BIT(2);
            if (dispStat[i] & BIT(5))
                core->interpreter[i].sendInterrupt(2);
        } else if (dispStat[i] & BIT(2)) {
            dispStat[i] &= ~BIT(2);
        }
        dispStat[i] &= ~BIT(1);
    }

    core->schedule(NDS_SCANLINE355, 355 * 6);
}

// ── GPU thread workers ────────────────────────────────────────────────────────
void Gpu::drawGbaThreaded() {
    while (PPC_LIKELY(running)) {
        while (drawing != 1) {
            if (PPC_UNLIKELY(!running)) return;
            KThreadYield();
        }
        core->gpu2D[0].drawGbaScanline(vCount);
        drawing = 0;
    }
}

void Gpu::drawThreaded() {
    while (PPC_LIKELY(running)) {
        while (drawing != 1) {
            if (PPC_UNLIKELY(!running)) return;
            KThreadYield();
        }

        drawing = 2;
        core->gpu2D[0].drawScanline(vCount);

        PPCIrqState st = PPCIrqLockByMsr();
        bool doSecond  = (drawing == 2);
        if (doSecond) drawing = 3;
        PPCIrqUnlockByMsr(st);

        if (doSecond)
            core->gpu2D[1].drawScanline(vCount);

        drawing = 0;
    }
}

// ── Register writes ───────────────────────────────────────────────────────────
void Gpu::writeDispStat(bool cpu, uint16_t mask, uint16_t value) {
    mask &= 0xFFB8;
    dispStat[cpu] = (dispStat[cpu] & ~mask) | (value & mask);
}

void Gpu::writeDispCapCnt(uint32_t mask, uint32_t value) {
    mask &= 0xEF3F1F1F;
    dispCapCnt = (dispCapCnt & ~mask) | (value & mask);
}

void Gpu::writePowCnt1(uint16_t mask, uint16_t value) {
    mask &= 0x820F;
    powCnt1 = (powCnt1 & ~mask) | (value & mask);
}
