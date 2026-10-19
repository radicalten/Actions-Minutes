#pragma once
// =============================================================================
// poly_compat.h — minimal libnds-compatible definitions for the polygon module
//
// The suite is bare metal (no libnds). This header supplies exactly the types,
// constants and hardware registers that polyrastertest's test table and drawing
// code rely on. Values are taken from libnds (blocksds) headers so that the
// upstream test definitions keep their original meaning:
//   GL_BLEND     = 1<<3   (DISP3DCNT alpha blending)
//   GL_ANTIALIAS = 1<<4   (DISP3DCNT edge antialiasing)
//   GL_OUTLINE   = 1<<5   (DISP3DCNT edge marking)
//   POLY_CULL_NONE = 3<<6 (POLY_FORMAT cull bits)
//   GL_MAX_DEPTH = 0x7FFF
// =============================================================================

#include "../../common/common.h"

// 4x4 fixed point matrix, identical layout to libnds m4x4.
struct m4x4
{
    s32 m[16];
};

#define POLY_CULL_NONE (3 << 6)
#define GL_MAX_DEPTH   0x7FFF

#define GL_BLEND       (1 << 3)
#define GL_ANTIALIAS   (1 << 4)
#define GL_OUTLINE     (1 << 5)

// GL_GLBEGIN_ENUM values used by the test table.
#define GL_TRIANGLE       0
#define GL_QUAD           1
#define GL_TRIANGLE_STRIP 2
#define GL_QUAD_STRIP     3

// -----------------------------------------------------------------------------
// Display / VRAM
// -----------------------------------------------------------------------------
#define POLY_DISPCNT         (*(vu32 *)0x04000000)
#define POLY_DISPCAPCNT      (*(vu32 *)0x04000064)
#define POLY_DISPSTAT        (*(vu16 *)0x04000004)
#define POLY_POWCNT1         (*(vu16 *)0x04000304)
#define POLY_VRAMCNT_A       (*(vu8  *)0x04000240)
#define POLY_VRAMCNT_B       (*(vu8  *)0x04000241)

// DISPCNT bits (libnds video.h)
#define POLY_DISPLAY_MODE_NORMAL (1u << 16) // 2D/3D normal display
#define POLY_DISPLAY_MODE_FB     (2u << 16) // VRAM framebuffer display
#define POLY_ENABLE_3D           (1u << 3)
// BG0 must be enabled for the 3D layer to be composited and captured: libnds
// MODE_0_3D = MODE_0_2D | DISPLAY_BG0_ACTIVE | ENABLE_3D.
#define POLY_DISPLAY_BG0_ACTIVE  (1u << 8)
#define POLY_MODE_0_3D           (POLY_DISPLAY_MODE_NORMAL | POLY_DISPLAY_BG0_ACTIVE | POLY_ENABLE_3D)

// DISPCAPCNT bits (libnds video.h)
#define POLY_DCAP_ENABLE          (1u << 31)
#define POLY_DCAP_MODE_A          (0u << 29)
#define POLY_DCAP_SRC_A_3DONLY    (1u << 24)
#define POLY_DCAP_SIZE_256x192    (3u << 20)
#define POLY_DCAP_OFFSET_0        (0u << 18)
#define POLY_DCAP_BANK_A          (0u << 16)

// Capture target and the reference image live in VRAM banks A and B. While a
// bank is in LCDC mode (VRAMCNT=0x80) it is reached through the LCD window at
// 0x06800000; that is also where DISPCAPCNT writes and where the suite's own
// text console draws. Bank A = 0x06800000, bank B = 0x06820000.
#ifdef POLY_HOST_TEST
// Host self-test build (tools/poly-selftest): the two framebuffers are plain
// arrays so poly_ref.cpp can be exercised off-target without an emulator.
extern u16 poly_host_fb_a[256 * 192];
extern u16 poly_host_fb_b[256 * 192];
#define POLY_FB_A            poly_host_fb_a
#define POLY_FB_B            poly_host_fb_b
#else
#define POLY_FB_A            ((vu16 *)0x06800000)
#define POLY_FB_B            ((vu16 *)0x06820000)
#endif

#define POLY_SCREEN_W        256
#define POLY_SCREEN_H        192

// -----------------------------------------------------------------------------
// 3D geometry engine registers (libnds video.h)
// -----------------------------------------------------------------------------
#define POLY_GFX_CONTROL      (*(vu16 *)0x04000060)
#define POLY_GFX_STATUS       (*(vu32 *)0x04000600)
#define POLY_GFX_COLOR        (*(vu32 *)0x04000480)
#define POLY_GFX_VERTEX16     (*(vu32 *)0x0400048C)
#define POLY_GFX_POLY_FORMAT  (*(vu32 *)0x040004A4)
#define POLY_GFX_TEX_FORMAT   (*(vu32 *)0x040004A8)
#define POLY_GFX_CLEAR_COLOR  (*(vu32 *)0x04000350)
#define POLY_GFX_CLEAR_DEPTH  (*(vu16 *)0x04000354)
#define POLY_GFX_EDGE_TABLE   ((vu16 *)0x04000330)
#define POLY_GFX_BEGIN        (*(vu32 *)0x04000500)
#define POLY_GFX_END          (*(vu32 *)0x04000504)
#define POLY_GFX_FLUSH        (*(vu32 *)0x04000540)
#define POLY_GFX_VIEWPORT     (*(vu32 *)0x04000580)

// Matrix processor (libnds video.h)
#define POLY_MTX_CONTROL      (*(vu32 *)0x04000440)
#define POLY_MTX_PUSH         (*(vu32 *)0x04000444)
#define POLY_MTX_POP          (*(vu32 *)0x04000448)
#define POLY_MTX_IDENTITY     (*(vu32 *)0x04000454)
#define POLY_MTX_LOAD4x4      (*(vs32 *)0x04000458)

// Matrix modes
#define POLY_MTX_PROJECTION   0
#define POLY_MTX_POSITION     1
#define POLY_MTX_MODELVIEW    2
#define POLY_MTX_TEXTURE      3

// GFX_STATUS bits
#define POLY_GFX_STATUS_MATRIX_STACK_BUSY  (1u << 14)
#define POLY_GFX_STATUS_MATRIX_STACK_ERROR (1u << 15)
#define POLY_GFX_STATUS_BUSY               (1u << 27)
