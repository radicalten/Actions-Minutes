//wii_video.h (updated declarations)
#ifndef WII_VIDEO_H
#define WII_VIDEO_H

#include <gctypes.h>
#include <ogc/gx.h>
#include <stdint.h>
#include <stdbool.h>
#include "console_ui.h"

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
void Wii_VideoRender(const uint16_t* srcTop, const uint16_t* srcBottom,
                     bool gbaMode = false);
void Wii_VideoFlushAsync();
void Wii_DrawCursor(float x, float y);
void Wii_DebugOverlayInit();
void Wii_DebugOverlayPrint(int line, const char* fmt, ...);
void Wii_DebugOverlaySetEnabled(bool enabled);

// ConsoleUI platform hook implementations
// createTexture: emulator framebuffer pixels (RGB5A3 uint16_t)
void* Wii_CreateTexture(uint16_t* data, int width, int height);

// createTextureRGBA8: UI art pixels (RGBA8 uint32_t — BMP icons, font)
void* Wii_CreateTextureRGBA8(uint32_t* data, int width, int height);

void  Wii_DestroyTexture(void* texture);

void* ConsoleUI::createTexture(uint16_t* data, int width, int height) {
    return Wii_CreateTexture(data, width, height);
}

void* ConsoleUI::createTextureRGBA8(uint32_t* data, int width, int height) {
    return Wii_CreateTextureRGBA8(data, width, height);
}

void ConsoleUI::destroyTexture(void* texture) {
    Wii_DestroyTexture(texture);
}

#endif // WII_VIDEO_H
