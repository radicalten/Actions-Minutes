// Display path for the 3DS target.
//
// Every GPU/heap call here can fail on an installed title (heavy apps get a different
// memory layout than hbmenu homebrew, and the linear/VRAM pools are not the same size), and
// blindly writing through a failed initialisation is a data abort (write) with no useful
// information. So:
//
//   * every step is checked and logged (sdmc:/3ds/racer/boot.log),
//   * a failure demotes the app to a plain gfx/console text UI instead of crashing,
//   * the citro3d command buffer is retried at smaller sizes before giving up,
//   * drawing functions are all no-ops for the parts that failed to initialise.
#include <3ds.h>
#include <citro2d.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <malloc.h>

#include "r3ds.hpp"

namespace r3ds {

namespace {

C3D_RenderTarget* s_top = nullptr;      // upper screen (VI image + status)
C3D_RenderTarget* s_bottom = nullptr;   // lower screen (status)
C2D_TextBuf s_text_buf = nullptr;

constexpr int kTexW = 512, kTexH = 256; // power-of-two texture holding the 320x240 VI frame
C3D_Tex s_vi_tex;
Tex3DS_SubTexture s_vi_sub;
C2D_Image s_vi_img;
uint16_t* s_vi_upload = nullptr;
bool s_vi_upload_linear = false;        // freed with linearFree vs free

ScreenState s_state{};
bool s_gfx_inited = false;
bool s_c3d_inited = false;
bool s_c2d_inited = false;

inline uint16_t to565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// The command buffer comes out of the linear heap; if that pool is small the default size
// fails, so try smaller ones instead of proceeding with a half-initialised citro3d.
size_t init_c3d_with_retries(size_t* chosen, bool low_mem) {
    static const size_t sizes[] = { C3D_DEFAULT_CMDBUF_SIZE, 0x20000, 0x10000, 0x8000 };
    static const size_t sizes_low[] = { 0x10000, 0x8000 };
    if (low_mem) {
        bootlog("64MB profile: cmdbuf ladder starts at 65536");
        for (size_t size : sizes_low) {
            const size_t free_before = linearSpaceFree();
            if (C3D_Init(size)) {
                *chosen = size;
                bootlog("C3D_Init(%zu) ok, linear free %zu -> %zu", size, free_before, linearSpaceFree());
                return size;
            }
            bootlog("C3D_Init(%zu) FAILED (linear free %zu)", size, free_before);
        }
        *chosen = 0;
        return 0;
    }
    for (size_t size : sizes) {
        const size_t free_before = linearSpaceFree();
        if (C3D_Init(size)) {
            *chosen = size;
            bootlog("C3D_Init(%zu) ok, linear free %zu -> %zu", size, free_before, linearSpaceFree());
            return size;
        }
        bootlog("C3D_Init(%zu) FAILED (linear free %zu)", size, free_before);
    }
    *chosen = 0;
    return 0;
}

} // namespace

const ScreenState& screen_state() { return s_state; }

bool screen_init(bool low_mem) {
    if (s_state.mode == ScreenMode::Citro2d) return true;

    // gfxInitDefault() returns void: it aborts internally if GSP cannot be reached, which
    // is why the boot log line before it is printed unconditionally.
    bootlog("gfxInitDefault");
    gfxInitDefault();
    s_gfx_inited = true;
    bootlog("gfx ok");

    bootlog("linearSpaceFree=%zu vramSpaceFree=%zu", linearSpaceFree(), vramSpaceFree());

    size_t cmdbuf = 0;
    init_c3d_with_retries(&cmdbuf, low_mem);
    if (cmdbuf == 0) {
        bootlog("no usable C3D command buffer, using console text");
        s_state.mode = ScreenMode::Console;
        return false;
    }
    s_c3d_inited = true;
    s_state.cmdbuf_size = (uint32_t)cmdbuf;

    bootlog("C2D_Init(%d)", C2D_DEFAULT_MAX_OBJECTS);
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        bootlog("C2D_Init FAILED, using console text");
        s_state.mode = ScreenMode::Console;
        return false;
    }
    s_c2d_inited = true;

    bootlog("C2D_Prepare");
    C2D_Prepare();

    s_top    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    s_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    bootlog("screen targets top=%p bottom=%p", (void*)s_top, (void*)s_bottom);
    if (!s_top || !s_bottom) {
        bootlog("C2D_CreateScreenTarget FAILED, using console text");
        s_state.mode = ScreenMode::Console;
        return false;
    }
    s_state.top_ok = true;
    s_state.bottom_ok = true;

    s_text_buf = C2D_TextBufNew(4096);
    bootlog("text buf %p", (void*)s_text_buf);

    // The VI surface texture: 512x256 RGB565 = 256 KiB of VRAM. If VRAM is exhausted the
    // texture has no backing store and uploading would write through NULL, so this is
    // checked before anything uses it.
    bootlog("C3D_TexInit(512x256 RGB565) vram free %zu", vramSpaceFree());
    const bool tex_ok = C3D_TexInit(&s_vi_tex, kTexW, kTexH, GPU_RGB565);
    bootlog("C3D_TexInit -> %d, data=%p, vram free %zu", (int)tex_ok, s_vi_tex.data, vramSpaceFree());
    if (tex_ok && s_vi_tex.data) {
        C3D_TexSetFilter(&s_vi_tex, GPU_NEAREST, GPU_NEAREST);

        // Staging buffer: linear heap first, then the normal heap.
        s_vi_upload = (uint16_t*)linearAlloc(kTexW * kTexH * sizeof(uint16_t));
        s_vi_upload_linear = s_vi_upload != nullptr;
        if (!s_vi_upload) {
            s_vi_upload = (uint16_t*)memalign(0x80, kTexW * kTexH * sizeof(uint16_t));
            bootlog("linearAlloc for staging FAILED, memalign -> %p", (void*)s_vi_upload);
        }
        if (s_vi_upload) {
            std::memset(s_vi_upload, 0, kTexW * kTexH * sizeof(uint16_t));

            s_vi_sub.width  = kViW;
            s_vi_sub.height = kViH;
            s_vi_sub.left   = 0.0f;
            s_vi_sub.top    = 0.0f;
            s_vi_sub.right  = (float)kViW / (float)kTexW;
            s_vi_sub.bottom = (float)kViH / (float)kTexH;
            s_vi_img.tex    = &s_vi_tex;
            s_vi_img.subtex = &s_vi_sub;
            s_state.vi_texture = true;
        }
    }

    s_state.mode = ScreenMode::Citro2d;
    bootlog("screen init done: mode=citro2d cmdbuf=%zu vi_tex=%d",
            cmdbuf, (int)s_state.vi_texture);
    return true;
}

// Drop citro3d/citro2d and everything allocated for them, but keep gfx alive so the
// libctru console can take over the framebuffers. Mixing a live citro3d command context
// with the gfx console is not supported, so a fallback has to go through here first.
void screen_drop_gpu() {
    if (s_vi_upload) {
        if (s_vi_upload_linear) linearFree(s_vi_upload); else free(s_vi_upload);
        s_vi_upload = nullptr;
    }
    if (s_state.vi_texture) { C3D_TexDelete(&s_vi_tex); s_state.vi_texture = false; }
    if (s_text_buf) { C2D_TextBufDelete(s_text_buf); s_text_buf = nullptr; }
    s_top = nullptr;
    s_bottom = nullptr;
    s_state.top_ok = s_state.bottom_ok = false;
    if (s_c2d_inited) { C2D_Fini(); s_c2d_inited = false; }
    if (s_c3d_inited) { C3D_Fini(); s_c3d_inited = false; }
    bootlog("gpu context dropped (citro3d/citro2d)");
}

bool screen_init_console() {
    if (s_state.mode == ScreenMode::Citro2d) return false;   // never mix the two
    screen_drop_gpu();                                       // no-op if nothing came up
    if (!s_gfx_inited) {
        bootlog("gfxInitDefault (console mode)");
        gfxInitDefault();
        s_gfx_inited = true;
    }
    consoleInit(GFX_TOP, nullptr);
    s_state.mode = ScreenMode::Console;
    bootlog("console mode ready");
    return true;
}

void screen_use_console() {
    // Called before anything has been drawn: hand both screens to libctru's console.
    if (s_state.mode == ScreenMode::Citro2d) return;  // never mix the two
    s_state.mode = ScreenMode::Console;
}

void screen_exit() {
    screen_drop_gpu();
    if (s_gfx_inited) { gfxExit(); s_gfx_inited = false; }
    s_state.mode = ScreenMode::Dead;
}

void frame_begin() {
    if (s_state.mode != ScreenMode::Citro2d) return;
    if (s_text_buf) C2D_TextBufClear(s_text_buf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
}

void frame_end() {
    if (s_state.mode != ScreenMode::Citro2d) return;
    C3D_FrameEnd(0);
}

void top_clear(uint32_t rgb) {
    if (s_state.mode != ScreenMode::Citro2d || !s_top) return;
    C2D_TargetClear(s_top, C2D_Color32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0xFF));
    C2D_SceneBegin(s_top);
}

void bot_clear(uint32_t rgb) {
    if (s_state.mode != ScreenMode::Citro2d || !s_bottom) return;
    C2D_TargetClear(s_bottom, C2D_Color32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0xFF));
    C2D_SceneBegin(s_bottom);
}

void draw_vi_image(int x, int y, const uint16_t* rgba5551, int w, int h) {
    if (s_state.mode != ScreenMode::Citro2d || !s_state.vi_texture || !s_vi_upload || !rgba5551)
        return;

    const int cw = (w < kViW) ? w : kViW;
    const int ch = (h < kViH) ? h : kViH;
    for (int row = 0; row < ch; row++) {
        const uint16_t* src = rgba5551 + (size_t)row * (size_t)w;
        uint16_t* dst = s_vi_upload + (size_t)row * (size_t)kTexW;
        for (int col = 0; col < cw; col++) {
            const uint16_t px = src[col];                 // RGBA5551
            const uint8_t r5 = (px >> 11) & 0x1F;
            const uint8_t g5 = (px >> 6) & 0x1F;
            const uint8_t b5 = (px >> 1) & 0x1F;
            dst[col] = to565((uint8_t)(r5 << 3 | r5 >> 2),
                             (uint8_t)(g5 << 3 | g5 >> 2),
                             (uint8_t)(b5 << 3 | b5 >> 2));
        }
    }

    C3D_TexUpload(&s_vi_tex, s_vi_upload);
    C3D_TexFlush(&s_vi_tex);
    C2D_DrawImageAt(s_vi_img, (float)x, (float)y, 0.0f, nullptr, 1.0f, 1.0f);
}

namespace {
void draw_text(C3D_RenderTarget* target, int x, int y, uint32_t rgb, float scale,
               const char* fmt, va_list ap) {
    if (!target || !s_text_buf) return;
    char line[160];
    std::vsnprintf(line, sizeof(line), fmt, ap);

    C2D_Text t;
    C2D_TextParse(&t, s_text_buf, line);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, (float)x, (float)y, 0.5f, scale, scale,
                 C2D_Color32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0xFF));
}
} // namespace

void top_text(int x, int y, uint32_t rgb, const char* fmt, ...) {
    if (s_state.mode != ScreenMode::Citro2d) return;
    va_list ap; va_start(ap, fmt);
    draw_text(s_top, x, y, rgb, 0.42f, fmt, ap);
    va_end(ap);
}

void bot_text(int x, int y, uint32_t rgb, float scale, const char* fmt, ...) {
    if (s_state.mode != ScreenMode::Citro2d) return;
    va_list ap; va_start(ap, fmt);
    draw_text(s_bottom, x, y, rgb, scale, fmt, ap);
    va_end(ap);
}

} // namespace r3ds
