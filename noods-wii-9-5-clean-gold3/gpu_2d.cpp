// gpu_2d.cpp (fixed: rgb5ToRgb6 restored as Gpu2D:: member definition)
#include <cstring>
#include <algorithm>
#include "core.h"

#define PPC_LIKELY(x)    __builtin_expect(!!(x), 1)
#define PPC_UNLIKELY(x)  __builtin_expect(!!(x), 0)
#define ALWAYS_INLINE    __attribute__((always_inline)) inline
#define HOT              __attribute__((hot))
#define COLD             __attribute__((cold))

#define PPC_PREFETCH(addr) \
    asm volatile ("dcbt 0,%0" : : "r"(addr) : "memory")

// ── Construction ──────────────────────────────────────────────────────────────
Gpu2D::Gpu2D(Core *core, bool engine) : core(core), engine(engine) {
    bgVramAddr  = 0x6000000 + engine * 0x200000;
    objVramAddr = 0x6400000 + engine * 0x200000;
    palette     = core->memory.palette + engine * 0x400;
    oam         = core->memory.oam     + engine * 0x400;
    extPalettes = engine ? core->memory.engBExtPal : core->memory.engAExtPal;
}

// ── State serialisation ───────────────────────────────────────────────────────
void Gpu2D::saveState(FILE *file) {
    fwrite(winHFlip,      sizeof(bool), 2,  file);
    fwrite(winVFlag,      sizeof(bool), 2,  file);
    fwrite(&dispCnt,      sizeof(dispCnt),     1, file);
    fwrite(bgCnt,         2, 4,  file);
    fwrite(bgHOfs,        2, 4,  file);
    fwrite(bgVOfs,        2, 4,  file);
    fwrite(bgPA,          2, 2,  file);
    fwrite(bgPB,          2, 2,  file);
    fwrite(bgPC,          2, 2,  file);
    fwrite(bgPD,          2, 2,  file);
    fwrite(bgX,           4, 2,  file);
    fwrite(bgY,           4, 2,  file);
    fwrite(winX1,         2, 2,  file);
    fwrite(winX2,         2, 2,  file);
    fwrite(winY1,         2, 2,  file);
    fwrite(winY2,         2, 2,  file);
    fwrite(&winIn,        sizeof(winIn),        1, file);
    fwrite(&winOut,       sizeof(winOut),       1, file);
    fwrite(&bldCnt,       sizeof(bldCnt),       1, file);
    fwrite(&mosaic,       sizeof(mosaic),       1, file);
    fwrite(&bldAlpha,     sizeof(bldAlpha),     1, file);
    fwrite(&bldY,         sizeof(bldY),         1, file);
    fwrite(&masterBright, sizeof(masterBright), 1, file);
}

void Gpu2D::loadState(FILE *file) {
    fread(winHFlip,      sizeof(bool), 2,  file);
    fread(winVFlag,      sizeof(bool), 2,  file);
    fread(&dispCnt,      sizeof(dispCnt),     1, file);
    fread(bgCnt,         2, 4,  file);
    fread(bgHOfs,        2, 4,  file);
    fread(bgVOfs,        2, 4,  file);
    fread(bgPA,          2, 2,  file);
    fread(bgPB,          2, 2,  file);
    fread(bgPC,          2, 2,  file);
    fread(bgPD,          2, 2,  file);
    fread(bgX,           4, 2,  file);
    fread(bgY,           4, 2,  file);
    fread(winX1,         2, 2,  file);
    fread(winX2,         2, 2,  file);
    fread(winY1,         2, 2,  file);
    fread(winY2,         2, 2,  file);
    fread(&winIn,        sizeof(winIn),        1, file);
    fread(&winOut,       sizeof(winOut),       1, file);
    fread(&bldCnt,       sizeof(bldCnt),       1, file);
    fread(&mosaic,       sizeof(mosaic),       1, file);
    fread(&bldAlpha,     sizeof(bldAlpha),     1, file);
    fread(&bldY,         sizeof(bldY),         1, file);
    fread(&masterBright, sizeof(masterBright), 1, file);
}

// ── rgb5ToRgb6 – static MEMBER definition (matches gpu_2d.h declaration) ─────
//
// declared as:  static uint32_t rgb5ToRgb6(uint32_t color);
// in gpu_2d.h.  Must be Gpu2D:: or the linker cannot resolve it.
//
// Converts RGB5 → RGB6: each 5-bit channel is shifted left by 1 to become
// a 6-bit channel.  The upper tracking bits (BIT(25)=semi-transparent,
// BIT(26)=3D pixel, BIT(18..17)=3D alpha, etc.) are preserved unchanged.
// Pure shift arithmetic — zero multiply or divide.

ALWAYS_INLINE uint32_t Gpu2D::rgb5ToRgb6(uint32_t color) {
    //  r: bits [4:0]   → bits [5:1]   (mask 0x0000_003E)
    //  g: bits [9:5]   → bits [11:6]  (mask 0x0000_0FC0)
    //  b: bits [14:10] → bits [17:12] (mask 0x0003_F000)
    uint32_t r = (color <<  1) & 0x0000003Eu;
    uint32_t g = (color >>  4) & 0x00000FC0u;
    uint32_t b = (color >>  9) & 0x0003F000u;
    return (color & 0xFFFC0000u) | b | g | r;
}

// ── reloadRegisters / updateWindows ──────────────────────────────────────────
void Gpu2D::reloadRegisters() {
    internalX[0] = bgX[0];
    internalX[1] = bgX[1];
    internalY[0] = bgY[0];
    internalY[1] = bgY[1];
}

void Gpu2D::updateWindows(int line) {
    for (int i = 0; i < 2; i++) {
        if      (line == winY2[i]) winVFlag[i] = false;
        else if (line == winY1[i]) winVFlag[i] = true;
    }
}

// ── Window-enable query ───────────────────────────────────────────────────────
// Returns the 6-bit enable mask for the window that contains pixel x on
// the given scanline.  Inlined so callers can hoist the (dispCnt & 0xE000)
// guard out of per-pixel loops.
ALWAYS_INLINE uint8_t Gpu2D::getWindowEnabled(int line, int x) const {
    if (PPC_LIKELY(!(dispCnt & 0xE000)))
        return 0x3F; // No windows active → everything enabled

    if ((dispCnt & BIT(13)) && winVFlag[0] &&
        ((x >= winX1[0] && x < winX2[0]) != winHFlip[0]))
        return (uint8_t)(winIn >> 0);   // Window 0

    if ((dispCnt & BIT(14)) && winVFlag[1] &&
        ((x >= winX1[1] && x < winX2[1]) != winHFlip[1]))
        return (uint8_t)(winIn >> 8);   // Window 1

    if ((dispCnt & BIT(15)) && (framebuffer[line * 256 + x] & BIT(24)))
        return (uint8_t)(winOut >> 8);  // Object window

    return (uint8_t)(winOut >> 0);      // Outside all windows
}

// ── drawBgPixel ───────────────────────────────────────────────────────────────
ALWAYS_INLINE void Gpu2D::drawBgPixel(int bg, int line, int x, uint32_t pixel) {
    if (PPC_UNLIKELY(dispCnt & 0xE000)) {
        if (!(getWindowEnabled(line, x) & BIT(bg))) return;
    }

    int8_t bgPrio = (int8_t)(bgCnt[bg] & 0x3);

    if (bgPrio <= priorities[0][x]) {
        layers[1][x]     = layers[0][x];
        priorities[1][x] = priorities[0][x];
        blendBits[1][x]  = blendBits[0][x];
        layers[0][x]     = pixel;
        priorities[0][x] = bgPrio;
        blendBits[0][x]  = (int8_t)bg;
    } else if (bgPrio <= priorities[1][x]) {
        layers[1][x]     = pixel;
        priorities[1][x] = bgPrio;
        blendBits[1][x]  = (int8_t)bg;
    }
}

// ── drawObjPixel ──────────────────────────────────────────────────────────────
ALWAYS_INLINE void Gpu2D::drawObjPixel(int line, int x, uint32_t pixel, int8_t priority) {
    if (PPC_UNLIKELY(dispCnt & 0xE000)) {
        if (!(getWindowEnabled(line, x) & BIT(4))) return;
    }

    if (!(layers[0][x] & BIT(15)) || priority < priorities[0][x]) {
        layers[0][x]     = pixel;
        priorities[0][x] = priority;
        blendBits[0][x]  = 4;
    }
}

// ── GBA alpha-blend helper (15-bit channels) ──────────────────────────────────
ALWAYS_INLINE static uint32_t blendAlphaGba(uint32_t top, uint32_t bot,
                                              uint8_t eva, uint8_t evb) {
    uint32_t r = std::min(((top>> 0)&0x1F)*eva + ((bot>> 0)&0x1F)*evb, 31u*16u) / 16u;
    uint32_t g = std::min(((top>> 5)&0x1F)*eva + ((bot>> 5)&0x1F)*evb, 31u*16u) / 16u;
    uint32_t b = std::min(((top>>10)&0x1F)*eva + ((bot>>10)&0x1F)*evb, 31u*16u) / 16u;
    return (b << 10) | (g << 5) | r;
}

// ── NDS alpha-blend helper (18-bit channels) ──────────────────────────────────
ALWAYS_INLINE static uint32_t blendAlphaNds(uint32_t top, uint32_t bot,
                                              uint8_t eva, uint8_t evb) {
    uint32_t r = std::min(((top>> 0)&0x3F)*eva + ((bot>> 0)&0x3F)*evb, 63u*16u) / 16u;
    uint32_t g = std::min(((top>> 6)&0x3F)*eva + ((bot>> 6)&0x3F)*evb, 63u*16u) / 16u;
    uint32_t b = std::min(((top>>12)&0x3F)*eva + ((bot>>12)&0x3F)*evb, 63u*16u) / 16u;
    return (b << 12) | (g << 6) | r;
}

// ── drawGbaScanline ───────────────────────────────────────────────────────────
HOT void Gpu2D::drawGbaScanline(int line) {
    uint32_t backdrop = U8TO16(palette, 0) & ~BIT(15);

    {
        uint32_t *l0 = layers[0];
        uint32_t *l1 = layers[1];
        asm volatile ("dcbz 0,%0" : : "r"(l0)     : "memory");
        asm volatile ("dcbz 0,%0" : : "r"(l0 + 8) : "memory");
        asm volatile ("dcbz 0,%0" : : "r"(l1)     : "memory");
        asm volatile ("dcbz 0,%0" : : "r"(l1 + 8) : "memory");
        for (int i = 0; i < 240; i += 4) {
            l0[i+0]=l0[i+1]=l0[i+2]=l0[i+3]=backdrop;
            l1[i+0]=l1[i+1]=l1[i+2]=l1[i+3]=backdrop;
        }
    }
    memset(priorities, 4, sizeof(priorities));
    memset(blendBits,  5, sizeof(blendBits));

    if (dispCnt & BIT(12)) {
        if (dispCnt & BIT(15)) drawObjects<true>(line, true);
        drawObjects<true>(line, false);
    }

    switch (uint8_t mode = (uint8_t)(dispCnt & 0x7)) {
    case 0:
        if (dispCnt & BIT(11)) drawText<true>(3, line);
        if (dispCnt & BIT(10)) drawText<true>(2, line);
        if (dispCnt & BIT(9))  drawText<true>(1, line);
        if (dispCnt & BIT(8))  drawText<true>(0, line);
        break;
    case 1:
        if (dispCnt & BIT(10)) drawAffine<true>(2, line);
        if (dispCnt & BIT(9))  drawText<true>(1, line);
        if (dispCnt & BIT(8))  drawText<true>(0, line);
        break;
    case 2:
        if (dispCnt & BIT(11)) drawAffine<true>(3, line);
        if (dispCnt & BIT(10)) drawAffine<true>(2, line);
        break;
    case 3: case 4: case 5:
        if (dispCnt & BIT(10)) drawExtendedGba(2, line);
        break;
    default:
        LOG_CRIT("Unknown GBA BG mode: %d\n", mode);
        break;
    }

    const int   blendMode      = (bldCnt >> 6) & 0x3;
    const bool  windowsEnabled = (dispCnt & 0xE000) != 0;
    const uint8_t eva = (uint8_t)std::min((bldAlpha >> 0) & 0x1F, 16);
    const uint8_t evb = (uint8_t)std::min((bldAlpha >> 8) & 0x1F, 16);

    for (int i = 0; i < 240; i++) {
        uint32_t top = layers[0][i];
        int8_t   bb0 = blendBits[0][i];
        int8_t   bb1 = blendBits[1][i];
        bool doAlpha = false;

        if (top & BIT(25)) {
            if      (bldCnt & BIT(8 + bb1))                    doAlpha = true;
            else if (blendMode < 2 || !(bldCnt & BIT(bb0)))    continue;
        } else {
            if (blendMode == 0)                                 continue;
            if (!(bldCnt & BIT(bb0)))                          continue;
            if (blendMode == 1 && !(bldCnt & BIT(8 + bb1)))    continue;
        }

        if (PPC_UNLIKELY(windowsEnabled)) {
            if (!(getWindowEnabled(line, i) & BIT(5))) continue;
        }

        if (doAlpha || blendMode == 1) {
            layers[0][i] = blendAlphaGba(top, layers[1][i], eva, evb);
            continue;
        }
        if (blendMode == 2 && bldY) {
            uint32_t r=(top>> 0)&0x1F; r+=(uint32_t)(31-r)*bldY/16u;
            uint32_t g=(top>> 5)&0x1F; g+=(uint32_t)(31-g)*bldY/16u;
            uint32_t b=(top>>10)&0x1F; b+=(uint32_t)(31-b)*bldY/16u;
            layers[0][i] = (b<<10)|(g<<5)|r;
        } else if (blendMode == 3 && bldY) {
            uint32_t r=(top>> 0)&0x1F; r-=r*bldY/16u;
            uint32_t g=(top>> 5)&0x1F; g-=g*bldY/16u;
            uint32_t b=(top>>10)&0x1F; b-=b*bldY/16u;
            layers[0][i] = (b<<10)|(g<<5)|r;
        }
    }

    memcpy(&framebuffer[line * 256], layers[0], 240 * sizeof(uint32_t));
}

// ── drawScanline ──────────────────────────────────────────────────────────────
HOT void Gpu2D::drawScanline(int line) {
    uint32_t backdrop = U8TO16(palette, 0) & ~BIT(15);
    {
        uint32_t *l0 = layers[0];
        uint32_t *l1 = layers[1];
        asm volatile ("dcbz 0,%0" : : "r"(l0)      : "memory");
        asm volatile ("dcbz 0,%0" : : "r"(l0 + 8)  : "memory");
        asm volatile ("dcbz 0,%0" : : "r"(l0 + 16) : "memory");
        asm volatile ("dcbz 0,%0" : : "r"(l0 + 24) : "memory");
        asm volatile ("dcbz 0,%0" : : "r"(l1)      : "memory");
        asm volatile ("dcbz 0,%0" : : "r"(l1 + 8)  : "memory");
        asm volatile ("dcbz 0,%0" : : "r"(l1 + 16) : "memory");
        asm volatile ("dcbz 0,%0" : : "r"(l1 + 24) : "memory");
        for (int i = 0; i < 256; i += 8) {
            l0[i+0]=l0[i+1]=l0[i+2]=l0[i+3]=
            l0[i+4]=l0[i+5]=l0[i+6]=l0[i+7]=backdrop;
            l1[i+0]=l1[i+1]=l1[i+2]=l1[i+3]=
            l1[i+4]=l1[i+5]=l1[i+6]=l1[i+7]=backdrop;
        }
    }
    memset(priorities, 4, sizeof(priorities));
    memset(blendBits,  5, sizeof(blendBits));

    if (dispCnt & BIT(12)) {
        if (dispCnt & BIT(15)) drawObjects<false>(line, true);
        drawObjects<false>(line, false);
    }

    switch (uint8_t mode = (uint8_t)(dispCnt & 0x7)) {
    case 0:
        if (dispCnt & BIT(11)) drawText<false>(3, line);
        if (dispCnt & BIT(10)) drawText<false>(2, line);
        if (dispCnt & BIT(9))  drawText<false>(1, line);
        if (dispCnt & BIT(8))  drawText<false>(0, line);
        break;
    case 1:
        if (dispCnt & BIT(11)) drawAffine<false>(3, line);
        if (dispCnt & BIT(10)) drawText<false>(2, line);
        if (dispCnt & BIT(9))  drawText<false>(1, line);
        if (dispCnt & BIT(8))  drawText<false>(0, line);
        break;
    case 2:
        if (dispCnt & BIT(11)) drawAffine<false>(3, line);
        if (dispCnt & BIT(10)) drawAffine<false>(2, line);
        if (dispCnt & BIT(9))  drawText<false>(1, line);
        if (dispCnt & BIT(8))  drawText<false>(0, line);
        break;
    case 3:
        if (dispCnt & BIT(11)) drawExtended(3, line);
        if (dispCnt & BIT(10)) drawText<false>(2, line);
        if (dispCnt & BIT(9))  drawText<false>(1, line);
        if (dispCnt & BIT(8))  drawText<false>(0, line);
        break;
    case 4:
        if (dispCnt & BIT(11)) drawExtended(3, line);
        if (dispCnt & BIT(10)) drawAffine<false>(2, line);
        if (dispCnt & BIT(9))  drawText<false>(1, line);
        if (dispCnt & BIT(8))  drawText<false>(0, line);
        break;
    case 5:
        if (dispCnt & BIT(11)) drawExtended(3, line);
        if (dispCnt & BIT(10)) drawExtended(2, line);
        if (dispCnt & BIT(9))  drawText<false>(1, line);
        if (dispCnt & BIT(8))  drawText<false>(0, line);
        break;
    case 6:
        if (dispCnt & BIT(10)) drawLarge(2, line);
        break;
    default:
        LOG_CRIT("Unknown engine %c BG mode: %d\n", engine ? 'B' : 'A', mode);
        break;
    }

    const int   blendMode      = (bldCnt >> 6) & 0x3;
    const bool  windowsEnabled = (dispCnt & 0xE000) != 0;
    const uint8_t eva = (uint8_t)std::min((bldAlpha >>  0) & 0x1F, 16);
    const uint8_t evb = (uint8_t)std::min((bldAlpha >>  8) & 0x1F, 16);

    for (int i = 0; i < 256; i++) {
        uint32_t top = layers[0][i];
        int8_t   bb0 = blendBits[0][i];
        int8_t   bb1 = blendBits[1][i];
        bool doAlpha = false;

        if (top & BIT(26)) {
            // 3D pixel path
            if (bldCnt & BIT(8 + bb1)) {
                uint32_t alpha3d = ((top >> 18) & 0x3F) + 1;
                if (alpha3d == 64) continue;
                uint32_t bot6  = rgb5ToRgb6(layers[1][i]);
                uint32_t eva3d = alpha3d;
                uint32_t evb3d = 64u - eva3d;
                uint32_t r = std::min(((top>> 0)&0x3F)*eva3d+((bot6>> 0)&0x3F)*evb3d, 63u*64u)/64u;
                uint32_t g = std::min(((top>> 6)&0x3F)*eva3d+((bot6>> 6)&0x3F)*evb3d, 63u*64u)/64u;
                uint32_t b = std::min(((top>>12)&0x3F)*eva3d+((bot6>>12)&0x3F)*evb3d, 63u*64u)/64u;
                layers[0][i] = (b<<12)|(g<<6)|r;
                continue;
            }
            if (blendMode < 2 || !(bldCnt & BIT(bb0))) continue;
            // Brightness blend falls through using current top value
        } else {
            top = rgb5ToRgb6(top);
            layers[0][i] = top;

            if (top & BIT(25)) {
                if      (bldCnt & BIT(8 + bb1))                  doAlpha = true;
                else if (blendMode < 2 || !(bldCnt & BIT(bb0)))  continue;
            } else {
                if (blendMode == 0)                               continue;
                if (!(bldCnt & BIT(bb0)))                         continue;
                if (blendMode == 1 && !(bldCnt & BIT(8 + bb1)))  continue;
            }
        }

        if (PPC_UNLIKELY(windowsEnabled)) {
            if (!(getWindowEnabled(line, i) & BIT(5))) continue;
        }

        if (doAlpha || blendMode == 1) {
            uint32_t bot = (layers[1][i] & BIT(26))
                ? layers[1][i]
                : rgb5ToRgb6(layers[1][i]);
            layers[0][i] = blendAlphaNds(layers[0][i], bot, eva, evb);
            continue;
        }

        top = layers[0][i];
        if (blendMode == 2 && bldY) {
            uint32_t r=(top>> 0)&0x3F; r+=(uint32_t)(63-r)*bldY/16u;
            uint32_t g=(top>> 6)&0x3F; g+=(uint32_t)(63-g)*bldY/16u;
            uint32_t b=(top>>12)&0x3F; b+=(uint32_t)(63-b)*bldY/16u;
            layers[0][i] = (b<<12)|(g<<6)|r;
        } else if (blendMode == 3 && bldY) {
            uint32_t r=(top>> 0)&0x3F; r-=r*bldY/16u;
            uint32_t g=(top>> 6)&0x3F; g-=g*bldY/16u;
            uint32_t b=(top>>12)&0x3F; b-=b*bldY/16u;
            layers[0][i] = (b<<12)|(g<<6)|r;
        }
    }

    // ── Display mode output ───────────────────────────────────────────────────
    uint32_t *fbLine = &framebuffer[line * 256];
    switch ((dispCnt >> 16) & 0x3) {
    case 0:
        memset(fbLine, 0xFF, 256 * sizeof(uint32_t));
        break;
    case 1:
        memcpy(fbLine, layers[0], 256 * sizeof(uint32_t));
        break;
    case 2: {
        uint32_t addr = 0x6800000 + ((dispCnt >> 1) & 0x60000) + (uint32_t)line * 512;
        for (int i = 0; i < 256; i++) {
            if ((i & 7) == 0)
                PPC_PREFETCH(reinterpret_cast<const uint8_t*>(&core->memory) + addr + (i+8)*2);
            fbLine[i] = rgb5ToRgb6(core->memory.read<uint16_t>(0, addr + i * 2));
        }
        break;
    }
    case 3:
        LOG_CRIT("Unimplemented engine %c display mode: display FIFO\n", engine ? 'B' : 'A');
        break;
    }

    // ── Master brightness ─────────────────────────────────────────────────────
    switch ((masterBright >> 14) & 0x3) {
    case 1: {
        uint8_t factor = (uint8_t)std::min((unsigned)(masterBright & 0x1F), 16u);
        if (!factor) break;
        for (int i = 0; i < 256; i += 8) {
            asm volatile ("dcbt 0,%0" : : "r"(&fbLine[i + 8]) : "memory");
            for (int k = 0; k < 8; k++) {
                uint32_t px = fbLine[i+k];
                uint32_t r=(px>> 0)&0x3F; r+=(uint32_t)(63-r)*factor/16u;
                uint32_t g=(px>> 6)&0x3F; g+=(uint32_t)(63-g)*factor/16u;
                uint32_t b=(px>>12)&0x3F; b+=(uint32_t)(63-b)*factor/16u;
                fbLine[i+k] = (b<<12)|(g<<6)|r;
            }
        }
        break;
    }
    case 2: {
        uint8_t factor = (uint8_t)std::min((unsigned)(masterBright & 0x1F), 16u);
        if (!factor) break;
        for (int i = 0; i < 256; i += 8) {
            asm volatile ("dcbt 0,%0" : : "r"(&fbLine[i + 8]) : "memory");
            for (int k = 0; k < 8; k++) {
                uint32_t px = fbLine[i+k];
                uint32_t r=(px>> 0)&0x3F; r-=r*factor/16u;
                uint32_t g=(px>> 6)&0x3F; g-=g*factor/16u;
                uint32_t b=(px>>12)&0x3F; b-=b*factor/16u;
                fbLine[i+k] = (b<<12)|(g<<6)|r;
            }
        }
        break;
    }
    default: break;
    }
}

// ── drawText ──────────────────────────────────────────────────────────────────
template <bool gbaMode>
HOT void Gpu2D::drawText(int bg, int line) {
    if (!gbaMode && bg == 0 && (dispCnt & BIT(3))) {
        uint32_t *data     = core->gpu3DRenderer.getLine(line);
        bool      resShift = Settings::highRes3D;
        for (int i = 0; i < 256; i++) {
            uint32_t px = data[i << resShift];
            if (px & 0xFC0000)
                drawBgPixel(bg, line, i, px);
        }
        return;
    }

    const uint32_t tileBaseOrig  = bgVramAddr
                                 + ((dispCnt >> 11) & 0x70000)
                                 + ((bgCnt[bg] <<  3) & 0x0F800);
    const uint32_t indexBaseOrig = bgVramAddr
                                 + ((dispCnt >>  8) & 0x70000)
                                 + ((bgCnt[bg] << 12) & 0x3C000);

    const int mosaicV = (bgCnt[bg] & BIT(6))
        ? (line - (line % (((mosaic >> 4) & 0xF) + 1)))
        : line;
    const int yOffset = (mosaicV + bgVOfs[bg]) & 0x1FF;

    uint32_t tileBase = tileBaseOrig + ((yOffset & 0xF8) << 3);
    if (yOffset >= 256 && (bgCnt[bg] & BIT(15)))
        tileBase += (bgCnt[bg] & BIT(14)) ? 0x1000 : 0x800;

    const int  screenW = gbaMode ? 240 : 256;
    const int  width   = screenW + (bgHOfs[bg] & 0x7);
    const bool flip512 = (bgCnt[bg] & BIT(14)) != 0;

    if (bgCnt[bg] & BIT(7)) {
        // 8bpp
        uint8_t *extPal = (dispCnt & BIT(30))
            ? extPalettes[bg + (bg < 2 && (bgCnt[bg] & BIT(13))) * 2]
            : nullptr;

        for (int i = 0; i < width; i += 8) {
            const int xOffset  = (i + bgHOfs[bg]) & 0x1FF;
            uint32_t  tileAddr = tileBase + ((xOffset & 0xF8) >> 2);
            if (xOffset >= 256 && flip512) tileAddr += 0x800;

            uint16_t  tile    = core->memory.read<uint16_t>(gbaMode, tileAddr);
            uint8_t  *pal     = extPal ? &extPal[(tile >> 3) & 0x1E00] : palette;
            const int tileRow = (tile & BIT(11)) ? (7 - (yOffset & 7)) : (yOffset & 7);
            uint32_t  idxAddr = indexBaseOrig + (tile & 0x3FF) * 64 + tileRow * 8;

            PPC_PREFETCH(reinterpret_cast<const uint8_t*>(&core->memory) + idxAddr + 8);

            uint32_t idxLo = core->memory.read<uint32_t>(gbaMode, idxAddr);
            uint32_t idxHi = core->memory.read<uint32_t>(gbaMode, idxAddr + 4);
            uint64_t indices = (uint64_t)idxLo | ((uint64_t)idxHi << 32);

            const bool hFlip = (tile & BIT(10)) != 0;
            const int  xBase = i - (xOffset & 7);

            for (int p = 0; p < 8 && indices; p++, indices >>= 8) {
                int x = hFlip ? (xBase + 7 - p) : (xBase + p);
                if ((unsigned)x >= (unsigned)screenW) continue;
                uint8_t idx = (uint8_t)(indices & 0xFF);
                if (idx)
                    drawBgPixel(bg, line, x, U8TO16(pal, idx * 2) | BIT(15));
            }
        }
    } else {
        // 4bpp
        for (int i = 0; i < width; i += 8) {
            const int xOffset  = (i + bgHOfs[bg]) & 0x1FF;
            uint32_t  tileAddr = tileBase + ((xOffset & 0xF8) >> 2);
            if (xOffset >= 256 && flip512) tileAddr += 0x800;

            uint16_t  tile    = core->memory.read<uint16_t>(gbaMode, tileAddr);
            uint8_t  *pal     = &palette[(tile & 0xF000) >> 7];
            const int tileRow = (tile & BIT(11)) ? (7 - (yOffset & 7)) : (yOffset & 7);
            uint32_t  idxAddr = indexBaseOrig + (tile & 0x3FF) * 32 + tileRow * 4;

            PPC_PREFETCH(reinterpret_cast<const uint8_t*>(&core->memory) + idxAddr + 4);

            uint32_t   indices = core->memory.read<uint32_t>(gbaMode, idxAddr);
            const bool hFlip   = (tile & BIT(10)) != 0;
            const int  xBase   = i - (xOffset & 7);

            for (int p = 0; p < 8 && indices; p++, indices >>= 4) {
                int x = hFlip ? (xBase + 7 - p) : (xBase + p);
                if ((unsigned)x >= (unsigned)screenW) continue;
                uint8_t idx = (uint8_t)(indices & 0xF);
                if (idx)
                    drawBgPixel(bg, line, x, U8TO16(pal, idx * 2) | BIT(15));
            }
        }
    }
}

// ── drawAffine ────────────────────────────────────────────────────────────────
template <bool gbaMode>
HOT void Gpu2D::drawAffine(int bg, int line) {
    const uint32_t tileBase  = bgVramAddr + ((bgCnt[bg] << 3) & 0x0F800)
                                          + ((dispCnt >> 11) & 0x70000);
    const uint32_t indexBase = bgVramAddr + ((bgCnt[bg] << 12) & 0x3C000)
                                          + ((dispCnt >>  8) & 0x70000);

    int rotX = internalX[bg - 2] - bgPA[bg - 2];
    int rotY = internalY[bg - 2] - bgPC[bg - 2];

    const int      size     = 128 << ((bgCnt[bg] & 0xC000) >> 14);
    const int      sizeMask = size - 1;
    const bool     wrap     = (bg < 2) || ((bgCnt[bg] & BIT(13)) != 0);
    const int      screenW  = gbaMode ? 240 : 256;
    const int16_t  pa       = bgPA[bg - 2];
    const int16_t  pc       = bgPC[bg - 2];

    for (int i = 0; i < screenW; i++) {
        int x = (rotX += pa) >> 8;
        int y = (rotY += pc) >> 8;

        if (wrap) {
            x &= sizeMask;
            y &= sizeMask;
        } else if ((unsigned)x >= (unsigned)size || (unsigned)y >= (unsigned)size) {
            continue;
        }

        uint8_t tile  = core->memory.read<uint8_t>(gbaMode,
                            tileBase + (y / 8) * (size / 8) + (x / 8));
        uint8_t index = core->memory.read<uint8_t>(gbaMode,
                            indexBase + tile * 64 + (y & 7) * 8 + (x & 7));
        if (index)
            drawBgPixel(bg, line, i, U8TO16(palette, index * 2) | BIT(15));
    }

    internalX[bg - 2] += bgPB[bg - 2];
    internalY[bg - 2] += bgPD[bg - 2];
}

// ── drawExtended ──────────────────────────────────────────────────────────────
HOT void Gpu2D::drawExtended(int bg, int line) {
    int rotX = internalX[bg - 2] - bgPA[bg - 2];
    int rotY = internalY[bg - 2] - bgPC[bg - 2];
    const int16_t pa = bgPA[bg - 2];
    const int16_t pc = bgPC[bg - 2];

    if (bgCnt[bg] & BIT(7)) {
        const uint32_t dataBase = bgVramAddr + ((bgCnt[bg] << 6) & 0x7C000);
        static const uint16_t sizes[] = { 128,128, 256,256, 512,256, 512,512 };
        const uint16_t *sz  = &sizes[((bgCnt[bg] >> 13) & 0x6)];
        const int  sizeX    = sz[0];
        const int  sizeY    = sz[1];
        const bool wrap     = (bgCnt[bg] & BIT(13)) != 0;
        const bool direct   = (bgCnt[bg] & BIT(2))  != 0;

        for (int i = 0; i < 256; i++) {
            int x = (rotX += pa) >> 8;
            int y = (rotY += pc) >> 8;

            if (wrap) { x &= sizeX-1; y &= sizeY-1; }
            else if (x<0||x>=sizeX||y<0||y>=sizeY) continue;

            if (direct) {
                uint16_t px = core->memory.read<uint16_t>(0, dataBase + (y*sizeX+x)*2);
                if (px & BIT(15)) drawBgPixel(bg, line, i, px);
            } else {
                uint8_t idx = core->memory.read<uint8_t>(0, dataBase + y*sizeX+x);
                if (idx) drawBgPixel(bg, line, i, U8TO16(palette, idx*2)|BIT(15));
            }
        }
    } else {
        const uint32_t tileBase  = bgVramAddr + ((bgCnt[bg] <<  3) & 0x0F800)
                                              + ((dispCnt >> 11) & 0x70000);
        const uint32_t indexBase = bgVramAddr + ((bgCnt[bg] << 12) & 0x3C000)
                                              + ((dispCnt >>  8) & 0x70000);
        const int  size     = 128 << ((bgCnt[bg] >> 14) & 0x3);
        const int  sizeMask = size - 1;
        const bool wrap     = (bgCnt[bg] & BIT(13)) != 0;
        const bool useExt   = (dispCnt & BIT(30)) != 0;

        for (int i = 0; i < 256; i++) {
            int x = (rotX += pa) >> 8;
            int y = (rotY += pc) >> 8;

            if (wrap) { x &= sizeMask; y &= sizeMask; }
            else if (x<0||x>=size||y<0||y>=size) continue;

            uint32_t tileAddr = tileBase + ((y/8)*(size/8)+(x/8))*2;
            uint16_t tile     = core->memory.read<uint16_t>(0, tileAddr);

            uint8_t *pal = palette;
            if (useExt) {
                if (!extPalettes[bg]) continue;
                pal = &extPalettes[bg][(tile >> 3) & 0x1E00];
            }

            int tx = ((tile & BIT(10)) ? (7-x) : x) & 7;
            int ty = ((tile & BIT(11)) ? (7-y) : y) & 7;

            uint8_t idx = core->memory.read<uint8_t>(0,
                indexBase + (tile & 0x3FF)*64 + ty*8 + tx);
            if (idx)
                drawBgPixel(bg, line, i, U8TO16(pal, idx*2)|BIT(15));
        }
    }

    internalX[bg - 2] += bgPB[bg - 2];
    internalY[bg - 2] += bgPD[bg - 2];
}

// ── drawExtendedGba ───────────────────────────────────────────────────────────
HOT void Gpu2D::drawExtendedGba(int bg, int line) {
    const uint8_t  mode     = (uint8_t)(dispCnt & 0x7);
    const uint32_t dataBase = bgVramAddr + ((bgCnt[bg] << 3) & 0xF800)
                            + ((mode > 3 && (dispCnt & BIT(4))) ? 0xA000 : 0);

    int rotX = internalX[bg - 2] - bgPA[bg - 2];
    int rotY = internalY[bg - 2] - bgPC[bg - 2];
    const int16_t pa = bgPA[bg - 2];
    const int16_t pc = bgPC[bg - 2];

    if (mode == 4) {
        for (int i = 0; i < 240; i++) {
            int x = (rotX += pa) >> 8;
            int y = (rotY += pc) >> 8;
            if (x<0||x>=240||y<0||y>=160) continue;
            uint8_t idx = core->memory.read<uint8_t>(1, dataBase + y*240+x);
            if (idx) drawBgPixel(bg, line, i, U8TO16(palette,idx*2)|BIT(15));
        }
    } else {
        const int sizeX = (mode==5) ? 160 : 240;
        const int sizeY = (mode==5) ? 128 : 160;
        for (int i = 0; i < 240; i++) {
            int x = (rotX += pa) >> 8;
            int y = (rotY += pc) >> 8;
            if (x<0||x>=sizeX||y<0||y>=sizeY) continue;
            uint16_t px = core->memory.read<uint16_t>(1, dataBase+(y*sizeX+x)*2);
            drawBgPixel(bg, line, i, px|BIT(15));
        }
    }

    internalX[bg - 2] += bgPB[bg - 2];
    internalY[bg - 2] += bgPD[bg - 2];
}

// ── drawLarge ─────────────────────────────────────────────────────────────────
HOT void Gpu2D::drawLarge(int bg, int line) {
    int rotX = internalX[bg - 2] - bgPA[bg - 2];
    int rotY = internalY[bg - 2] - bgPC[bg - 2];
    const int16_t pa = bgPA[bg - 2];
    const int16_t pc = bgPC[bg - 2];

    const int  sizeX   = ((bgCnt[bg] >> 14) & 0x3) ? 1024 : 512;
    const int  sizeY   = ((bgCnt[bg] >> 14) & 0x3) ?  512 : 1024;
    const bool wrap    = (bgCnt[bg] & BIT(13)) != 0;
    const bool engineB = (engine == 1);
    const int  yWrap   = sizeY / 4;

    for (int i = 0; i < 256; i++) {
        int x = (rotX += pa) >> 8;
        int y = (rotY += pc) >> 8;

        if (wrap) { x &= sizeX-1; y &= sizeY-1; }
        else if (x<0||x>=sizeX||y<0||y>=sizeY) continue;

        if (engineB) y &= yWrap - 1;

        uint8_t idx = core->memory.read<uint8_t>(0, bgVramAddr + y*sizeX + x);
        if (idx) drawBgPixel(bg, line, i, U8TO16(palette,idx*2)|BIT(15));
    }

    internalX[bg - 2] += bgPB[bg - 2];
    internalY[bg - 2] += bgPD[bg - 2];
}

// ── drawObjects ───────────────────────────────────────────────────────────────
template <bool gbaMode>
HOT void Gpu2D::drawObjects(int line, bool window) {
    const int screenW = gbaMode ? 240 : 256;

    for (int i = 0; i < 128; i++) {
        uint8_t byte1 = oam[i * 8 + 1];
        uint8_t type  = (byte1 >> 2) & 0x3;
        if ((byte1 & 0x3) == 2 || (type == 2) != window) continue;

        uint16_t object[3] = {
            U8TO16(oam, i*8+0),
            U8TO16(oam, i*8+2),
            U8TO16(oam, i*8+4)
        };

        static const uint8_t sizes[] = {
             8, 8, 16,16, 32,32, 64,64,
            16, 8, 32, 8, 32,16, 64,32,
             8,16,  8,32, 16,32, 32,64,
             0, 0,  0, 0,  0, 0,  0, 0
        };
        const uint8_t *sz    = &sizes[((object[0]>>11)&0x18)|((object[1]>>13)&0x6)];
        const uint8_t  width  = sz[0];
        const uint8_t  height = sz[1];

        const bool shift   = ((object[0] & 0x300) == 0x300);
        const int  width2  = width  << (int)shift;
        const int  height2 = height << (int)shift;

        int y = object[0] & 0xFF;
        if (y >= 192) y -= 256;

        const int spriteY = ((object[0] & BIT(12))
            ? (line - (line % (((mosaic>>12)&0xF)+1)))
            : line) - y;
        if (spriteY < 0 || spriteY >= height2) continue;

        int x = object[1] & 0x1FF;
        if (x >= 256) x -= 512;

        const int8_t priority = (int8_t)(((object[2]>>10)&0x3) - 1);

        // ── Bitmap objects (NDS) ──────────────────────────────────────────────
        if (!gbaMode && type == 3) {
            uint32_t dataBase;
            int      bitmapWidth;

            if (dispCnt & BIT(6)) {
                dataBase    = objVramAddr + (object[2]&0x3FF)*((dispCnt&BIT(22))?256:128);
                bitmapWidth = width;
            } else {
                uint8_t xMask = (dispCnt&BIT(5)) ? 0x1F : 0x0F;
                dataBase      = objVramAddr
                              + (object[2]&xMask)*0x10
                              + (object[2]&0x3FF&~xMask)*0x80;
                bitmapWidth   = (dispCnt&BIT(5)) ? 256 : 128;
            }

            if (object[0] & BIT(8)) {
                const int rsIdx = (object[1]>>9)&0x1F;
                const int16_t params[4] = {
                    (int16_t)U8TO16(oam, rsIdx*0x20+0x06),
                    (int16_t)U8TO16(oam, rsIdx*0x20+0x0E),
                    (int16_t)U8TO16(oam, rsIdx*0x20+0x16),
                    (int16_t)U8TO16(oam, rsIdx*0x20+0x1E)
                };
                const int hw2=width2/2, hh2=height2/2;
                for (int j = 0; j < width2; j++) {
                    int offset = x + j;
                    if ((unsigned)offset >= (unsigned)screenW) continue;
                    int rx=((params[0]*(j-hw2)+params[1]*(spriteY-hh2))>>8)+width/2;
                    if ((unsigned)rx>=(unsigned)width) continue;
                    int ry=((params[2]*(j-hw2)+params[3]*(spriteY-hh2))>>8)+height/2;
                    if ((unsigned)ry>=(unsigned)height) continue;
                    uint16_t px=core->memory.read<uint16_t>(0,dataBase+(ry*bitmapWidth+rx)*2);
                    if (px&BIT(15)) drawObjPixel(line,offset,px,priority);
                }
            } else {
                int vRow=(object[1]&BIT(13))?(height-spriteY-1):spriteY;
                dataBase+=vRow*bitmapWidth*2;
                for (int j=0; j<width; j++) {
                    int offset=(object[1]&BIT(12))?(x+width-j-1):(x+j);
                    if ((unsigned)offset>=(unsigned)screenW) continue;
                    uint16_t px=core->memory.read<uint16_t>(0,dataBase+j*2);
                    if (px&BIT(15)) drawObjPixel(line,offset,px,priority);
                }
            }
            continue;
        }

        // ── Tile base ─────────────────────────────────────────────────────────
        uint32_t tileBase;
        if (gbaMode) {
            tileBase = 0x6010000 + (object[2]&0x3FF)*32;
        } else {
            uint16_t bound=(dispCnt&BIT(4))?(uint16_t)(32<<((dispCnt>>20)&0x3)):32;
            tileBase=objVramAddr+(object[2]&0x3FF)*bound;
        }

        if (object[0] & BIT(8)) {
            // Rotscale tile sprite
            const int rsIdx=(object[1]>>9)&0x1F;
            const int16_t params[4]={
                (int16_t)U8TO16(oam,rsIdx*0x20+0x06),
                (int16_t)U8TO16(oam,rsIdx*0x20+0x0E),
                (int16_t)U8TO16(oam,rsIdx*0x20+0x16),
                (int16_t)U8TO16(oam,rsIdx*0x20+0x1E)
            };
            const int hw2=width2/2, hh2=height2/2;

            if (object[0] & BIT(13)) {
                // 8bpp rotscale
                const int mapWidth=(dispCnt&BIT(gbaMode?6:4))?width:128;
                uint8_t *pal;
                if (dispCnt&BIT(31)) {
                    if (!extPalettes[4]) continue;
                    pal=&extPalettes[4][(object[2]&0xF000)>>3];
                } else pal=&palette[0x200];

                for (int j=0; j<width2; j++) {
                    int offset=x+j;
                    if ((unsigned)offset>=(unsigned)screenW) continue;
                    int rx=((params[0]*(j-hw2)+params[1]*(spriteY-hh2))>>8)+width/2;
                    if ((unsigned)rx>=(unsigned)width) continue;
                    int ry=((params[2]*(j-hw2)+params[3]*(spriteY-hh2))>>8)+height/2;
                    if ((unsigned)ry>=(unsigned)height) continue;
                    uint8_t idx=core->memory.read<uint8_t>(gbaMode,
                        tileBase+((ry/8)*mapWidth+ry%8)*8+(rx/8)*64+rx%8);
                    if (idx&&type==2) framebuffer[line*256+offset]|=BIT(24);
                    else {
                        if (idx) drawObjPixel(line,offset,((type==1)<<25)|BIT(15)|U8TO16(pal,idx*2),priority);
                        if (gbaMode&&priority<priorities[0][offset]&&blendBits[0][offset]==4)
                            priorities[0][offset]=priority;
                    }
                }
            } else {
                // 4bpp rotscale
                const int mapWidth=(dispCnt&BIT(gbaMode?6:4))?width:256;
                uint8_t *pal=&palette[0x200+((object[2]&0xF000)>>12)*32];

                for (int j=0; j<width2; j++) {
                    int offset=x+j;
                    if ((unsigned)offset>=(unsigned)screenW) continue;
                    int rx=((params[0]*(j-hw2)+params[1]*(spriteY-hh2))>>8)+width/2;
                    if ((unsigned)rx>=(unsigned)width) continue;
                    int ry=((params[2]*(j-hw2)+params[3]*(spriteY-hh2))>>8)+height/2;
                    if ((unsigned)ry>=(unsigned)height) continue;
                    uint8_t raw=core->memory.read<uint8_t>(gbaMode,
                        tileBase+((ry/8)*mapWidth+ry%8)*4+(rx/8)*32+(rx%8)/2);
                    uint8_t idx=(rx&1)?(raw>>4):(raw&0xF);
                    if (idx&&type==2) framebuffer[line*256+offset]|=BIT(24);
                    else {
                        if (idx) drawObjPixel(line,offset,((type==1)<<25)|BIT(15)|U8TO16(pal,idx*2),priority);
                        if (gbaMode&&priority<priorities[0][offset]&&blendBits[0][offset]==4)
                            priorities[0][offset]=priority;
                    }
                }
            }
        } else if (object[0] & BIT(13)) {
            // 8bpp non-rotscale
            const int  mapWidth=(dispCnt&BIT(gbaMode?6:4))?width:128;
            const bool vFlip=(object[1]&BIT(13))!=0;
            const bool hFlip=(object[1]&BIT(12))!=0;

            if (vFlip)
                tileBase+=(7-(spriteY%8)+((height-1-spriteY)/8)*mapWidth)*8;
            else
                tileBase+=((spriteY%8)+(spriteY/8)*mapWidth)*8;

            uint8_t *pal;
            if (dispCnt&BIT(31)) {
                if (!extPalettes[4]) continue;
                pal=&extPalettes[4][(object[2]&0xF000)>>3];
            } else pal=&palette[0x200];

            for (int j=0; j<width; j++) {
                int offset=hFlip?(x+width-j-1):(x+j);
                if ((unsigned)offset>=(unsigned)screenW) continue;
                if ((j&7)==0)
                    PPC_PREFETCH(reinterpret_cast<const uint8_t*>(&core->memory)
                                 + tileBase+(j/8)*64+(j&7)+8);
                uint8_t idx=core->memory.read<uint8_t>(gbaMode,tileBase+(j/8)*64+j%8);
                if (idx&&type==2) framebuffer[line*256+offset]|=BIT(24);
                else {
                    if (idx) drawObjPixel(line,offset,((type==1)<<25)|BIT(15)|U8TO16(pal,idx*2),priority);
                    if (gbaMode&&priority<priorities[0][offset]&&blendBits[0][offset]==4)
                        priorities[0][offset]=priority;
                }
            }
        } else {
            // 4bpp non-rotscale
            const int  mapWidth=(dispCnt&BIT(gbaMode?6:4))?width:256;
            const bool vFlip=(object[1]&BIT(13))!=0;
            const bool hFlip=(object[1]&BIT(12))!=0;

            if (vFlip)
                tileBase+=(7-(spriteY%8)+((height-1-spriteY)/8)*mapWidth)*4;
            else
                tileBase+=((spriteY%8)+(spriteY/8)*mapWidth)*4;

            uint8_t *pal=&palette[0x200+((object[2]&0xF000)>>12)*32];

            for (int j=0; j<width; j++) {
                int offset=hFlip?(x+width-j-1):(x+j);
                if ((unsigned)offset>=(unsigned)screenW) continue;
                if ((j&1)==0)
                    PPC_PREFETCH(reinterpret_cast<const uint8_t*>(&core->memory)
                                 + tileBase+(j/8)*32+(j%8)/2+4);
                uint8_t raw=core->memory.read<uint8_t>(gbaMode,tileBase+(j/8)*32+(j%8)/2);
                uint8_t idx=(j&1)?(raw>>4):(raw&0xF);
                if (idx&&type==2) framebuffer[line*256+offset]|=BIT(24);
                else {
                    if (idx) drawObjPixel(line,offset,((type==1)<<25)|BIT(15)|U8TO16(pal,idx*2),priority);
                    if (gbaMode&&priority<priorities[0][offset]&&blendBits[0][offset]==4)
                        priorities[0][offset]=priority;
                }
            }
        }
    }
}

// ── Register write handlers ───────────────────────────────────────────────────
void Gpu2D::writeDispCnt(uint32_t mask, uint32_t value) {
    mask   &= (engine == 0) ? 0xFFFFFFFF : 0xC0B1FFF7;
    dispCnt = (dispCnt & ~mask) | (value & mask);
    if (core->gbaMode) dispCnt &= 0xFFFF;
}
void Gpu2D::writeBgCnt(int bg, uint16_t mask, uint16_t value) {
    if (core->gbaMode && bg < 2) mask &= 0xDFFF;
    bgCnt[bg] = (bgCnt[bg] & ~mask) | (value & mask);
}
void Gpu2D::writeBgHOfs(int bg, uint16_t mask, uint16_t value) {
    mask &= 0x01FF;
    bgHOfs[bg] = (bgHOfs[bg] & ~mask) | (value & mask);
}
void Gpu2D::writeBgVOfs(int bg, uint16_t mask, uint16_t value) {
    mask &= 0x01FF;
    bgVOfs[bg] = (bgVOfs[bg] & ~mask) | (value & mask);
}
void Gpu2D::writeBgPA(int bg, uint16_t mask, uint16_t value) {
    bgPA[bg-2] = (int16_t)((bgPA[bg-2] & ~mask) | (value & mask));
}
void Gpu2D::writeBgPB(int bg, uint16_t mask, uint16_t value) {
    bgPB[bg-2] = (int16_t)((bgPB[bg-2] & ~mask) | (value & mask));
}
void Gpu2D::writeBgPC(int bg, uint16_t mask, uint16_t value) {
    bgPC[bg-2] = (int16_t)((bgPC[bg-2] & ~mask) | (value & mask));
}
void Gpu2D::writeBgPD(int bg, uint16_t mask, uint16_t value) {
    bgPD[bg-2] = (int16_t)((bgPD[bg-2] & ~mask) | (value & mask));
}
void Gpu2D::writeBgX(int bg, uint32_t mask, uint32_t value) {
    mask &= 0x0FFFFFFF;
    bgX[bg-2] = (bgX[bg-2] & ~mask) | (value & mask);
    if (bgX[bg-2] & BIT(27)) bgX[bg-2] |=  0xF0000000;
    else                      bgX[bg-2] &= ~0xF0000000;
    internalX[bg-2] = bgX[bg-2];
}
void Gpu2D::writeBgY(int bg, uint32_t mask, uint32_t value) {
    mask &= 0x0FFFFFFF;
    bgY[bg-2] = (bgY[bg-2] & ~mask) | (value & mask);
    if (bgY[bg-2] & BIT(27)) bgY[bg-2] |=  0xF0000000;
    else                      bgY[bg-2] &= ~0xF0000000;
    internalY[bg-2] = bgY[bg-2];
}
void Gpu2D::writeWinH(int win, uint16_t mask, uint16_t value) {
    if (mask & 0x00FF) winX2[win] = (uint16_t)((value & 0x00FF));
    if (mask & 0xFF00) winX1[win] = (uint16_t)((value & 0xFF00) >> 8);
    winHFlip[win] = (winX1[win] > winX2[win]);
    if (winHFlip[win]) SWAP(winX1[win], winX2[win]);
}
void Gpu2D::writeWinV(int win, uint16_t mask, uint16_t value) {
    if (mask & 0x00FF) winY2[win] = (uint16_t)((value & 0x00FF));
    if (mask & 0xFF00) winY1[win] = (uint16_t)((value & 0xFF00) >> 8);
}
void Gpu2D::writeWinIn(uint16_t mask, uint16_t value) {
    mask &= 0x3F3F;
    winIn = (winIn & ~mask) | (value & mask);
}
void Gpu2D::writeWinOut(uint16_t mask, uint16_t value) {
    mask &= 0x3F3F;
    winOut = (winOut & ~mask) | (value & mask);
}
void Gpu2D::writeMosaic(uint16_t mask, uint16_t value) {
    mosaic = (mosaic & ~mask) | (value & mask);
}
void Gpu2D::writeBldCnt(uint16_t mask, uint16_t value) {
    mask &= 0x3FFF;
    bldCnt = (bldCnt & ~mask) | (value & mask);
}
void Gpu2D::writeBldAlpha(uint16_t mask, uint16_t value) {
    mask &= 0x1F1F;
    bldAlpha = (bldAlpha & ~mask) | (value & mask);
}
void Gpu2D::writeBldY(uint8_t value) {
    bldY = value & 0x1F;
    if (bldY > 16) bldY = 16;
}
void Gpu2D::writeMasterBright(uint16_t mask, uint16_t value) {
    mask &= 0xC01F;
    masterBright = (masterBright & ~mask) | (value & mask);
}
