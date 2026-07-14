// wii_video.h (optimized)
#ifndef WII_VIDEO_H
#define WII_VIDEO_H

#include <gctypes.h>
#include <ogc/gx.h>
#include <stdint.h>
#include <stdbool.h>

extern "C" {
    #include <tuxedo/ppc/intrinsics.h>
}

#define NDS_SCREEN_WIDTH  256
#define NDS_SCREEN_HEIGHT 192
#define GBA_SCREEN_WIDTH  240
#define GBA_SCREEN_HEIGHT 160

#define DEBUG_OVERLAY_WIDTH  256
#define DEBUG_OVERLAY_HEIGHT  64

// Cache line size on Wii/GameCube Broadway CPU
#define PPC_CACHE_LINE 32

// Alignment helpers
#define ALIGNED32 __attribute__((aligned(32)))
#define HOT_FN    __attribute__((hot))
#define COLD_FN   __attribute__((cold))
#define ALWAYS_INLINE __attribute__((always_inline)) inline

struct ALIGNED32 WiiVideoSystem {
    void*    xfbList[2];
    int      currentXfb;
    u8*      texDataTop;
    u8*      texDataBottom;
    GXTexObj texObjTop;
    GXTexObj texObjBottom;
    bool     lastGbaMode;
};

struct ALIGNED32 WiiDebugOverlay {
    u8*      texData;
    GXTexObj texObj;
    bool     enabled;
};

void Wii_VideoInit();
void Wii_VideoRender(const uint32_t* srcTop, const uint32_t* srcBottom,
                     bool gbaMode = false);
void Wii_VideoFlushAsync();
void Wii_DrawCursor(float x, float y);
void Wii_DebugOverlayInit();
void Wii_DebugOverlayPrint(int line, const char* fmt, ...);
void Wii_DebugOverlaySetEnabled(bool enabled);

#endif // WII_VIDEO_H
