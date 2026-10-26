// Host-side preview for the 3DS target.
//
// It links the very same vi.cpp / status.cpp the console binary uses, renders the VI test
// pattern and the lower-screen status text, and writes a PNG-ready mockup of both screens.
// This is how the layout was checked without a 3DS at hand: same code, same text, same
// colours, just written to an image instead of the PICA200.
//
//   c++ -O2 -std=c++17 -I../../include preview.cpp ../../source/vi.cpp ../../source/status.cpp -o preview
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>

#include "r3ds.hpp"
#include "font8x8.h"

using namespace r3ds;

namespace {

struct Image {
    int w, h;
    std::vector<uint8_t> rgb;
    Image(int w_, int h_) : w(w_), h(h_), rgb((size_t)w_ * h_ * 3, 0) {}
    void set(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) return;
        uint8_t* p = &rgb[((size_t)y * w + x) * 3];
        p[0] = r; p[1] = g; p[2] = b;
    }
    void fill(int x, int y, int ww, int hh, uint8_t r, uint8_t g, uint8_t b) {
        for (int yy = 0; yy < hh; yy++)
            for (int xx = 0; xx < ww; xx++) set(x + xx, y + yy, r, g, b);
    }
};

void draw_text(Image& img, int x, int y, const char* s, uint32_t rgb) {
    const uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    for (int i = 0; s[i]; i++) {
        int code = (unsigned char)s[i];
        if (code < kFontFirst || code > kFontLast) code = '?';
        const uint8_t* glyph = kFont8x8[code - kFontFirst];
        for (int row = 0; row < kFontH; row++)
            for (int col = 0; col < kFontW; col++)
                if (glyph[row] & (0x80 >> col)) img.set(x + i * kFontW + col, y + row, r, g, b);
    }
}

void save_ppm(const char* path, const Image& img) {
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", img.w, img.h);
    std::fwrite(img.rgb.data(), 1, img.rgb.size(), f);
    std::fclose(f);
}

} // namespace

int main() {
    // ---- raw VI output (what the top screen shows, at 2x) ----
    static uint16_t vi[kViW * kViH];
    vi_test_pattern(vi, 1234);

    Image raw(kViW * 2, kViH * 2);
    for (int y = 0; y < kViH * 2; y++)
        for (int x = 0; x < kViW * 2; x++) {
            const uint16_t px = vi[(y / 2) * kViW + (x / 2)];
            raw.set(x, y, (uint8_t)(((px >> 11) & 0x1F) * 255 / 31),
                    (uint8_t)(((px >> 6) & 0x1F) * 255 / 31),
                    (uint8_t)(((px >> 1) & 0x1F) * 255 / 31));
        }
    save_ppm("/tmp/vi_test_pattern.ppm", raw);

    // ---- both screens, as the app lays them out ----
    StatusInfo info;
    info.mode_name  = "HIGH 80MB (O3DS ext)";
    info.app_total  = 80u * 1024u * 1024u;
    info.app_free   = 61u * 1024u * 1024u;
    info.homebrew   = false;
    info.largest_probe = 32u * 1024u * 1024u;
    info.rom_found  = true;
    info.rom_valid  = true;
    info.rom_path   = "/3ds/racer/baserom.z64";
    info.rom_detail = "CRC1 0x72F70398 OK, 32 MiB";
    info.game_linked = false;
    info.audio_ok   = true;
    info.audio_rate = 32000;
    info.audio_note = "on 32000 Hz";
    info.prepare_note = "plan: 906/906 OK";
    info.mapping    = "A A  B B  X Z  Y CUp  D Cbtn";
    info.frame      = 1234;
    info.fps        = 59.0f;

    StatusLine lines[24];
    const int n = build_status_lines(info, lines, 24);

    const int W = 400, H = 240 + 8 + 240;
    Image shot(W, H);
    shot.fill(0, 0, W, H, 24, 24, 28);

    // upper screen (400x240), VI pillarboxed into the middle 320 columns
    shot.fill(0, 0, W, 240, 0, 0, 0);
    for (int y = 0; y < kViH; y++) {
        for (int x = 0; x < kViW; x++) {
            const uint16_t px = vi[y * kViW + x];
            shot.set(40 + x, y, (uint8_t)(((px >> 11) & 0x1F) * 255 / 31),
                     (uint8_t)(((px >> 6) & 0x1F) * 255 / 31),
                     (uint8_t)(((px >> 1) & 0x1F) * 255 / 31));
        }
    }

    // separator between the screens
    shot.fill(0, 240, W, 8, 90, 90, 100);

    // lower screen (320x240, centred)
    const int bx = (W - 320) / 2, by = 248;
    shot.fill(bx, by, 320, 240, 0x0B, 0x0F, 0x14);
    for (int i = 0; i < n; i++)
        draw_text(shot, bx + 4, by + i * 12, lines[i].text, lines[i].rgb);

    save_ppm("/tmp/screens.ppm", shot);

    std::printf("wrote /tmp/vi_test_pattern.ppm and /tmp/screens.ppm\n");
    return 0;
}
