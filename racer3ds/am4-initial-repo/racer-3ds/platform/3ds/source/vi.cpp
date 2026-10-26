// N64 VI emulation for the 3DS target.
//
// Two things live here:
//   1. vi_scanout_rdram(): the real scanout path. It reads the framebuffer the game left
//      in RDRAM the way the Video Interface does (16bpp RGBA5551 or 32bpp RGBA8888,
//      `width` pixels per line, byte offset `origin`) and converts it to the 320x240
//      RGBA5551 surface the display layer uploads. When the recompiled game is linked in
//      (`GAME=1`) this is what turns the game's framebuffer into pixels on the 3DS.
//   2. vi_test_pattern(): a synthetic framebuffer used when there is no game running, so
//      the display path can be verified on hardware without a ROM.
#include <cstdio>
#include <cstring>
#include <cmath>

#include "r3ds.hpp"
#include "font8x8.h"

namespace r3ds {
namespace {

inline uint16_t rgba5551(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 1) {
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xF8u) << 3) | ((b & 0xF8u) >> 2) | (a ? 1u : 0u));
}

inline void put(uint16_t* fb, int x, int y, uint16_t c) {
    if ((unsigned)x < (unsigned)kViW && (unsigned)y < (unsigned)kViH)
        fb[(size_t)y * kViW + x] = c;
}

void fill_rect(uint16_t* fb, int x, int y, int w, int h, uint16_t c) {
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++) put(fb, x + col, y + row, c);
}

// Draw a string with the built-in 8x8 font (same glyphs the diagnostic screen uses).
void image_text(uint16_t* fb, int x, int y, const char* s, uint16_t fg, uint16_t bg, bool opaque) {
    for (int i = 0; s[i]; i++) {
        int code = (unsigned char)s[i];
        if (code < kFontFirst || code > kFontLast) code = '?';
        const uint8_t* glyph = kFont8x8[code - kFontFirst];
        for (int row = 0; row < kFontH; row++) {
            const uint8_t bits = glyph[row];
            for (int col = 0; col < kFontW; col++) {
                const bool on = (bits & (0x80 >> col)) != 0;
                if (on) put(fb, x + i * kFontW + col, y + row, fg);
                else if (opaque) put(fb, x + i * kFontW + col, y + row, bg);
            }
        }
    }
}

} // namespace

void vi_scanout_rdram(uint16_t* out, const uint8_t* rdram, uint32_t origin,
                      uint32_t width, uint32_t rows, uint32_t status) {
    if (!out) return;

    // VI_STATUS: bits 1..0 = pixel size, 2 = 16bpp, 3 = 32bpp.
    const uint32_t type = (status >> 1) & 3u;
    if (width == 0) width = kViW;
    if (rows == 0) rows = kViH;

    const uint32_t copy_rows = (rows < (uint32_t)kViH) ? rows : (uint32_t)kViH;
    const uint32_t copy_cols = (width < (uint32_t)kViW) ? width : (uint32_t)kViW;

    for (uint32_t y = 0; y < (uint32_t)kViH; y++) {
        uint16_t* dst = out + (size_t)y * kViW;
        if (y >= copy_rows || !rdram) {
            std::memset(dst, 0, sizeof(uint16_t) * kViW);
            continue;
        }
        for (uint32_t x = 0; x < (uint32_t)kViW; x++) {
            if (x >= copy_cols) { dst[x] = 0; continue; }
            if (type == 2) {
                // 16bpp: one big-endian halfword per pixel.
                const size_t off = (size_t)origin + ((size_t)y * width + x) * 2;
                const uint16_t px = (uint16_t)((rdram[off] << 8) | rdram[off + 1]);
                dst[x] = px;   // already RGBA5551
            } else if (type == 3) {
                // 32bpp: RGBA8888, one big-endian word per pixel.
                const size_t off = (size_t)origin + ((size_t)y * width + x) * 4;
                const uint32_t v = ((uint32_t)rdram[off] << 24) | ((uint32_t)rdram[off + 1] << 16) |
                                   ((uint32_t)rdram[off + 2] << 8) | rdram[off + 3];
                dst[x] = rgba5551((v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF);
            } else {
                dst[x] = 0;
            }
        }
    }
}

void vi_test_pattern(uint16_t* fb, int frame) {
    if (!fb) return;

    // Classic VI test card: eight colour bars, a grey ramp, a framing border and a
    // moving marker so it is obvious the display path is live rather than a frozen image.
    static const uint32_t bars[8][3] = {
        {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
        {255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0},
    };
    const int bar_w = kViW / 8;
    for (int i = 0; i < 8; i++)
        fill_rect(fb, i * bar_w, 0, bar_w, 96, rgba5551(bars[i][0], bars[i][1], bars[i][2]));

    // Grey ramp.
    for (int x = 0; x < kViW; x++) {
        const uint32_t level = (uint32_t)((x * 255) / (kViW - 1));
        const uint16_t c = rgba5551(level, level, level);
        for (int y = 96; y < 120; y++) put(fb, x, y, c);
    }

    // Gradient body + a border one pixel inside the edge.
    for (int y = 120; y < kViH; y++) {
        for (int x = 0; x < kViW; x++) {
            const uint32_t b = (uint32_t)((y - 120) * 255 / (kViH - 120));
            put(fb, x, y, rgba5551(b / 3, b / 2, b));
        }
    }
    for (int x = 0; x < kViW; x++) { put(fb, x, 0, rgba5551(255,255,255)); put(fb, x, kViH - 1, rgba5551(255,255,255)); }
    for (int y = 0; y < kViH; y++) { put(fb, 0, y, rgba5551(255,255,255)); put(fb, kViW - 1, y, rgba5551(255,255,255)); }

    // Crosshair in the middle.
    for (int i = -10; i <= 10; i++) {
        put(fb, kViW / 2 + i, kViH / 2, rgba5551(255, 255, 255));
        put(fb, kViW / 2, kViH / 2 + i, rgba5551(255, 255, 255));
    }

    // Bouncing marker ("pod") - 30x14, moves every frame.
    {
        const int t = frame & 1023;
        const int px = 40 + (int)((sinf(t * 0.031f) * 0.5f + 0.5f) * (kViW - 120));
        const int py = 132 + (int)((sinf(t * 0.047f) * 0.5f + 0.5f) * 70);
        fill_rect(fb, px, py, 30, 14, rgba5551(255, 160, 0));
        fill_rect(fb, px + 4, py + 4, 22, 6, rgba5551(60, 60, 60));
    }

    char line[64];
    // Opaque strip so the labels stay readable over the colour bars.
    fill_rect(fb, 2, 2, 8 * 21 + 4, 20, rgba5551(0, 0, 0));
    image_text(fb, 4, 4, "STAR WARS EP.I RACER", rgba5551(255, 255, 255), 0, false);
    image_text(fb, 4, 14, "VI 320x240 16bpp", rgba5551(180, 220, 255), 0, false);
    std::snprintf(line, sizeof(line), "FRAME %d", frame);
    image_text(fb, 6, 102, line, rgba5551(255,255,0), 0, false);
    image_text(fb, 4, 228, "BRING-UP BUILD - NO GAME CODE", rgba5551(255,255,255), rgba5551(0,0,0), true);
}

} // namespace r3ds
