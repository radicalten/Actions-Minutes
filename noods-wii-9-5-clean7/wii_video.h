#ifndef WII_VIDEO_H
#define WII_VIDEO_H

#include <gctypes.h>
#include <ogc/gx.h>
#include <stdint.h>
#include <stdbool.h>

#define NDS_SCREEN_WIDTH  256
#define NDS_SCREEN_HEIGHT 192

#define GBA_SCREEN_WIDTH  240
#define GBA_SCREEN_HEIGHT 160

struct WiiVideoSystem {
    void*    xfbList[2];
    int      currentXfb;
    u8*      texDataTop;
    u8*      texDataBottom;
    GXTexObj texObjTop;
    GXTexObj texObjBottom;
    bool     lastGbaMode;
};

#define DEBUG_OVERLAY_WIDTH  256
#define DEBUG_OVERLAY_HEIGHT  64

struct WiiDebugOverlay {
    u8*      texData;
    GXTexObj texObj;
    bool     enabled;
};

void Wii_VideoInit();

// Takes 16-bit pointers for direct uploading
void Wii_VideoRender(const uint16_t* srcTop, const uint16_t* srcBottom,
                     bool gbaMode = false);

void Wii_VideoFlushAsync();
void Wii_DrawCursor(float x, float y);
void Wii_DebugOverlayInit();
void Wii_DebugOverlayPrint(int line, const char* fmt, ...);
void Wii_DebugOverlaySetEnabled(bool enabled);

#endif // WII_VIDEO_H
