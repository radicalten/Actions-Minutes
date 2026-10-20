#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * Unattended DS 2D graphics benchmark. It captures the composed main-engine
 * output, compares it with an independent software rendering of the scene,
 * paints a PolyRaster-style error image, and reports per-card/total scores.
 */
#define HOLD_FRAMES 30
/* Generous guest-cycle budgets for slower emulators; still finite on failure. */
#define CAPTURE_POLL_BUDGET 4000000u
#define FRAME_WAIT_BUDGET 800000u
#define TEST_COUNT 8
#define SCREEN_W 256
#define SCREEN_H 192
#define PIXELS (SCREEN_W * SCREEN_H)
#define BG_MAP_A 16
#define BG_MAP_B 20
#define AFFINE_MAP 16
#define REG_WIN0H (*(vu16 *)0x04000040)
#define REG_WIN0V (*(vu16 *)0x04000044)
#define REG_WININ (*(vu16 *)0x04000048)
#define REG_WINOUT (*(vu16 *)0x0400004A)

static PrintConsole bottomConsole;
static PrintConsole topConsole;
static u16 expected[PIXELS];
static u32 scoreMatches[TEST_COUNT];
static bool captureErrors[TEST_COUNT];
static u16 *spriteGfx[5];
static unsigned frameCounter;

static const char *testNames[TEST_COUNT] = {
    "01 TEXT BG / PALETTE / TILE FLIPS",
    "02 SCROLL OFFSETS / MAP WRAP",
    "03 BG PRIORITY / ALPHA BLEND",
    "04 AFFINE TILE BG / WRAP",
    "05 EXTENDED 16-BIT BITMAP BG",
    "06 AFFINE BITMAP / REPEAT EDGE",
    "07 WINDOW MASK / BG MOSAIC",
    "08 OBJ SIZE / PRIORITY / TRANSPARENCY"
};

static const char *cues[TEST_COUNT] = {
    "4bpp tile colors, palette banks, H/V flips.",
    "Non-tile scroll (13,7) and map wrapping.",
    "Layer priority and 50/50 alpha blending.",
    "Affine 8bpp tiles, rotation, scale, wrap.",
    "Extended RGB555 bitmap and opacity bit.",
    "Affine bitmap transform and repeated edges.",
    "WIN0 layer masks and 4x4 BG mosaic.",
    "OBJ sizes, flips, overlap, transparent pixels."
};

static inline u16 rgb15(int r, int g, int b) {
    return (u16)((r & 31) | ((g & 31) << 5) | ((b & 31) << 10));
}

static u16 colorFor(int n) {
    static const u16 colors[16] = {
        0x0000, 0x001F, 0x03E0, 0x7C00, 0x03FF, 0x7C1F, 0x7FE0, 0x4210,
        0x2108, 0x02BF, 0x7D20, 0x56B5, 0x5294, 0x294A, 0x6318, 0x7FFF
    };
    return colors[n & 15];
}

static void set_palette(int bank, int entry, u16 color) {
    BG_PALETTE[bank * 16 + entry] = color;
}

static void fill_text_bg(int layer, int tileBase, int mapBase,
                         int priority, int paletteBank, int seed) {
    u8 *tiles = (u8 *)BG_TILE_RAM(tileBase);
    u16 *map = BG_MAP_RAM(mapBase);
    volatile u16 *cnt = (volatile u16 *)(0x04000008 + layer * 2);
    int x, y, t;

    memset(tiles, 0, 16 * 32);
    for (t = 1; t < 16; ++t) {
        for (y = 0; y < 8; ++y) for (x = 0; x < 8; ++x) {
            int c = 1 + ((t + (x >> 1) + 2 * (y >> 1) + seed) % 15);
            int pos = t * 32 + y * 4 + (x >> 1);
            if (x & 1) tiles[pos] = (u8)((tiles[pos] & 0x0F) | (c << 4));
            else tiles[pos] = (u8)((tiles[pos] & 0xF0) | c);
        }
    }
    for (y = 0; y < 32; ++y) for (x = 0; x < 32; ++x) {
        u16 entry = (u16)(1 + ((x * 5 + y * 3 + seed) % 15));
        if (((x + y + seed) & 3) == 0) entry |= TILE_FLIP_H;
        if (((x - y + seed) & 7) == 0) entry |= TILE_FLIP_V;
        entry |= (u16)(paletteBank << 12);
        map[y * 32 + x] = entry;
    }
    for (t = 1; t < 16; ++t) for (x = 1; x < 16; ++x)
        set_palette(paletteBank, x, colorFor(x + t + seed));

    *cnt = (u16)(BG_TILE_BASE(tileBase) | BG_MAP_BASE(mapBase) | BG_32x32 |
                 BG_COLOR_16 | BG_PRIORITY(priority));
    if (layer == 0) REG_DISPCNT |= DISPLAY_BG0_ACTIVE;
    if (layer == 1) REG_DISPCNT |= DISPLAY_BG1_ACTIVE;
    if (layer == 2) REG_DISPCNT |= DISPLAY_BG2_ACTIVE;
    if (layer == 3) REG_DISPCNT |= DISPLAY_BG3_ACTIVE;
}

static void set_scroll(int layer, u16 x, u16 y) {
    volatile u16 *h = (volatile u16 *)(0x04000010 + layer * 4);
    volatile u16 *v = h + 1;
    *h = x; *v = y;
}

static void prepare_text_mode(void) {
    videoSetMode(MODE_0_2D);
    REG_BLDCNT = 0;
    REG_MOSAIC = 0;
    REG_DISPCNT &= ~(DISPLAY_WIN0_ON | DISPLAY_WIN1_ON | DISPLAY_SPR_WIN_ON);
    BG_PALETTE[0] = 0;
}

static void case_text_palette(void) {
    prepare_text_mode();
    fill_text_bg(1, 1, BG_MAP_A, 1, 1, 1);
}

static void case_scroll(void) {
    prepare_text_mode();
    fill_text_bg(1, 1, BG_MAP_A, 1, 1, 2);
    set_scroll(1, 13, 7);
}

static void case_priority_blend(void) {
    prepare_text_mode();
    fill_text_bg(3, 1, BG_MAP_A, 2, 2, 3);
    fill_text_bg(2, 2, BG_MAP_B, 1, 3, 8);
    REG_BLDCNT = BLEND_ALPHA | BLEND_SRC_BG2 | BLEND_DST_BG3;
    REG_BLDALPHA = (8 << 8) | 8;
}

static void fill_affine_tiles(void) {
    u8 *tile = (u8 *)BG_TILE_RAM(1);
    u8 *map = (u8 *)BG_MAP_RAM(AFFINE_MAP);
    int i, x, y;
    memset(tile, 0, 16 * 64);
    for (i = 0; i < 16; ++i) for (y = 0; y < 8; ++y) for (x = 0; x < 8; ++x)
        tile[i * 64 + y * 8 + x] = (u8)(1 + ((i + (x >> 1) + 2 * (y >> 1)) % 255));
    /* A 128x128-pixel affine map is 16x16 tile indices, one byte each. */
    for (y = 0; y < 16; ++y) for (x = 0; x < 16; ++x)
        map[y * 16 + x] = (u8)((x * 3 + y * 5) & 15);
    for (i = 0; i < 256; ++i) BG_PALETTE[i] = colorFor(i);
}

static void case_affine_tiles(void) {
    videoSetMode(MODE_2_2D | DISPLAY_BG2_ACTIVE);
    fill_affine_tiles();
    REG_BG2CNT = (u16)(BG_RS_128x128 | BG_COLOR_256 | BG_TILE_BASE(1) | BG_MAP_BASE(AFFINE_MAP) | BG_WRAP_ON | BG_PRIORITY(1));
    REG_BG2PA = 0x00D0; REG_BG2PB = 0x0030;
    REG_BG2PC = (s16)-0x0030; REG_BG2PD = 0x00D0;
    REG_BG2X = 0; REG_BG2Y = 0;
}

static void draw_bitmap(bool rotated) {
    u16 *fb = BG_BMP_RAM(0);
    int x, y;
    for (y = 0; y < 256; ++y) for (x = 0; x < 256; ++x) {
        int band = (x / 32 + y / 32) & 7;
        u16 c = colorFor(band + 1);
        if (((x / 8) ^ (y / 8)) & 1) c = (u16)((c & 0x7BDE) >> 1);
        if (x < 12 || y < 12 || x > 243 || y > 243) c = rgb15(31, 31, 31);
        fb[y * 256 + x] = c | 0x8000;
    }
    REG_BG2CNT = (u16)(BG_BMP16_256x256 | BG_PRIORITY(1) | (rotated ? BG_WRAP_ON : 0));
    REG_BG2PA = rotated ? 0x00D0 : 0x0100;
    REG_BG2PB = rotated ? 0x0030 : 0;
    REG_BG2PC = rotated ? (s16)-0x0030 : 0;
    REG_BG2PD = rotated ? 0x00D0 : 0x0100;
    REG_BG2X = 0; REG_BG2Y = 0;
}

static void case_bitmap(bool rotated) {
    /* A full 256x256x16bpp bitmap uses the whole background bank. */
    videoSetMode(MODE_5_2D | DISPLAY_BG2_ACTIVE);
    draw_bitmap(rotated);
}

static void case_window_mosaic(void) {
    prepare_text_mode();
    fill_text_bg(0, 1, BG_MAP_A, 2, 1, 1);
    fill_text_bg(1, 2, BG_MAP_B, 0, 2, 8);
    REG_MOSAIC = 0x3333;
    REG_BG1CNT |= BG_MOSAIC_ON;
    REG_WIN0H = (48 << 8) | 208;
    REG_WIN0V = (32 << 8) | 160;
    REG_WININ = 0x0003;
    REG_WINOUT = 0x0001;
    REG_DISPCNT |= DISPLAY_WIN0_ON;
}

static const int spriteX[5] = {14, 48, 96, 145, 120};
static const int spriteY[5] = {20, 22, 26, 32, 48};
static const int spriteSize[5] = {8, 16, 32, 64, 32};
static const int spritePriority[5] = {0, 1, 0, 1, 0};
static const bool spriteHFlip[5] = {false, true, false, false, false};
static const bool spriteVFlip[5] = {false, false, true, false, false};

static void fill_sprite_data(u8 *p, int size) {
    int tiles = size / 8;
    int ty, tx, py, px;
    for (ty = 0; ty < tiles; ++ty) for (tx = 0; tx < tiles; ++tx)
        for (py = 0; py < 8; ++py) for (px = 0; px < 8; ++px) {
            int col = (px == 0 || py == 0 || ((px + py + tx + ty) % 11 == 0))
                      ? 0 : 1 + ((tx + ty * 3) % 15);
            int pos = ((ty * tiles + tx) * 32) + py * 4 + px / 2;
            if (px & 1) p[pos] = (u8)((p[pos] & 0x0F) | (col << 4));
            else p[pos] = (u8)((p[pos] & 0xF0) | col);
        }
}

static void case_sprites(void) {
    int i;
    static const SpriteSize sizes[5] = {
        SpriteSize_8x8, SpriteSize_16x16, SpriteSize_32x32,
        SpriteSize_64x64, SpriteSize_32x32
    };
    prepare_text_mode();
    videoSetMode(MODE_0_2D | DISPLAY_BG1_ACTIVE | DISPLAY_SPR_1D);
    fill_text_bg(1, 1, BG_MAP_A, 2, 1, 3);
    vramSetBankE(VRAM_E_MAIN_SPRITE);
    oamInit(&oamMain, SpriteMapping_1D_32, false);
    for (i = 0; i < 5; ++i) {
        spriteGfx[i] = oamAllocateGfx(&oamMain, sizes[i], SpriteColorFormat_16Color);
        if (spriteGfx[i]) fill_sprite_data((u8 *)spriteGfx[i], spriteSize[i]);
    }
    for (i = 0; i < 16; ++i) SPRITE_PALETTE[i] = colorFor(i);
    oamSet(&oamMain, 0, spriteX[0], spriteY[0], spritePriority[0], 0, sizes[0], SpriteColorFormat_16Color, spriteGfx[0], -1, false, false, spriteHFlip[0], spriteVFlip[0], false);
    oamSet(&oamMain, 1, spriteX[1], spriteY[1], spritePriority[1], 0, sizes[1], SpriteColorFormat_16Color, spriteGfx[1], -1, false, false, spriteHFlip[1], spriteVFlip[1], false);
    oamSet(&oamMain, 2, spriteX[2], spriteY[2], spritePriority[2], 0, sizes[2], SpriteColorFormat_16Color, spriteGfx[2], -1, false, false, spriteHFlip[2], spriteVFlip[2], false);
    oamSet(&oamMain, 3, spriteX[3], spriteY[3], spritePriority[3], 0, sizes[3], SpriteColorFormat_16Color, spriteGfx[3], -1, false, false, spriteHFlip[3], spriteVFlip[3], false);
    oamSet(&oamMain, 4, spriteX[4], spriteY[4], spritePriority[4], 0, sizes[4], SpriteColorFormat_16Color, spriteGfx[4], -1, false, false, spriteHFlip[4], spriteVFlip[4], false);
    oamUpdate(&oamMain);
}

/* ---------- Independent CPU reference renderer for the test stimuli. ---------- */
static u16 text_pixel(int layer, int mapBase, int tileBase, int x, int y) {
    u16 *map = BG_MAP_RAM(mapBase);
    u8 *tiles = (u8 *)BG_TILE_RAM(tileBase);
    u16 entry = map[((y & 255) >> 3) * 32 + ((x & 255) >> 3)];
    int tx = x & 7, ty = y & 7;
    int tile = entry & 0x03FF;
    if (entry & TILE_FLIP_H) tx = 7 - tx;
    if (entry & TILE_FLIP_V) ty = 7 - ty;
    u8 packed = tiles[tile * 32 + ty * 4 + tx / 2];
    int color = (tx & 1) ? (packed >> 4) : (packed & 15);
    int pal = (entry >> 12) & 15;
    (void)layer;
    return BG_PALETTE[pal * 16 + color] & 0x7FFF;
}

static u16 affine_tile_pixel(int x, int y) {
    s32 sxFixed = (s32)(0x00D0 * x + 0x0030 * y);
    s32 syFixed = (s32)(-0x0030 * x + 0x00D0 * y);
    int sx = (sxFixed >> 8) & 127;
    int sy = (syFixed >> 8) & 127;
    u8 *map = (u8 *)BG_MAP_RAM(AFFINE_MAP);
    u8 *tiles = (u8 *)BG_TILE_RAM(1);
    int tile = map[(sy >> 3) * 16 + (sx >> 3)];
    u8 index = tiles[tile * 64 + (sy & 7) * 8 + (sx & 7)];
    return BG_PALETTE[index] & 0x7FFF;
}

static u16 affine_bitmap_pixel(int x, int y) {
    int sx = ((0x00D0 * x + 0x0030 * y) >> 8) & 255;
    int sy = (((-0x0030 * x) + 0x00D0 * y) >> 8) & 255;
    return BG_BMP_RAM(0)[sy * 256 + sx] & 0x7FFF;
}

static u16 blend15(u16 a, u16 b) {
    int r = ((((a & 31) * 8) + ((b & 31) * 8)) >> 4);
    int g = (((((a >> 5) & 31) * 8) + (((b >> 5) & 31) * 8)) >> 4);
    int bl = (((((a >> 10) & 31) * 8) + (((b >> 10) & 31) * 8)) >> 4);
    return rgb15(r, g, bl);
}

static u16 sprite_pixel(int sprite, int x, int y) {
    int lx = x - spriteX[sprite], ly = y - spriteY[sprite];
    int size = spriteSize[sprite];
    int tiles = size / 8;
    u8 *p = (u8 *)spriteGfx[sprite];
    if (spriteHFlip[sprite]) lx = size - 1 - lx;
    if (spriteVFlip[sprite]) ly = size - 1 - ly;
    int pos = (((ly >> 3) * tiles + (lx >> 3)) * 32) + (ly & 7) * 4 + ((lx & 7) >> 1);
    u8 packed = p[pos];
    int index = ((lx & 1) ? (packed >> 4) : packed) & 15;
    if (index == 0) return 0;
    return SPRITE_PALETTE[index] & 0x7FFF;
}

static u16 expected_sprite_scene(int x, int y) {
    int best = -1;
    u16 objColor = 0;
    for (int i = 0; i < 5; ++i) {
        if (!spriteGfx[i] || x < spriteX[i] || y < spriteY[i] ||
            x >= spriteX[i] + spriteSize[i] || y >= spriteY[i] + spriteSize[i]) continue;
        u16 c = sprite_pixel(i, x, y);
        if (!c) continue;
        if (best < 0 || spritePriority[i] < spritePriority[best] ||
            (spritePriority[i] == spritePriority[best] && i < best)) {
            best = i;
            objColor = c;
        }
    }
    if (best >= 0) return objColor;
    return text_pixel(1, BG_MAP_A, 1, x, y);
}

static void render_reference(int test) {
    u16 *bitmap = BG_BMP_RAM(0);
    for (int y = 0; y < SCREEN_H; ++y) for (int x = 0; x < SCREEN_W; ++x) {
        u16 c = 0;
        switch (test) {
            case 0:
                c = text_pixel(1, BG_MAP_A, 1, x, y);
                break;
            case 1:
                c = text_pixel(1, BG_MAP_A, 1, x + 13, y + 7);
                break;
            case 2: {
                u16 top = text_pixel(2, BG_MAP_B, 2, x, y);
                u16 under = text_pixel(3, BG_MAP_A, 1, x, y);
                c = blend15(top, under);
                break;
            }
            case 3:
                c = affine_tile_pixel(x, y);
                break;
            case 4:
                c = bitmap[y * 256 + x] & 0x7FFF;
                break;
            case 5:
                c = affine_bitmap_pixel(x, y);
                break;
            case 6:
                if (x >= 48 && x < 208 && y >= 32 && y < 160)
                    c = text_pixel(1, BG_MAP_B, 2, x & ~3, y & ~3);
                else
                    c = text_pixel(0, BG_MAP_A, 1, x, y);
                break;
            case 7:
                c = expected_sprite_scene(x, y);
                break;
        }
        expected[y * SCREEN_W + x] = c & 0x7FFF;
    }
}

static unsigned channel_delta(u16 a, u16 b, int shift) {
    int ca = (a >> shift) & 31;
    int cb = (b >> shift) & 31;
    return (unsigned)(ca > cb ? ca - cb : cb - ca);
}

/* Avoid blocking forever when an emulator's scanline clock stops advancing. */
static bool wait_for_next_frame(void) {
    u16 previous = REG_VCOUNT;
    for (volatile u32 budget = FRAME_WAIT_BUDGET; budget > 0; --budget) {
        u16 current = REG_VCOUNT;
        if (current < previous) return true; /* scanline counter wrapped */
        previous = current;
    }
    return false;
}

static void show_capture_timeout(void) {
    u16 *fb = VRAM_A;
    for (int y = 0; y < SCREEN_H; ++y) for (int x = 0; x < SCREEN_W; ++x)
        fb[y * SCREEN_W + x] = (((x + y) & 15) < 2) ? rgb15(31, 0, 0) : rgb15(8, 0, 0);
    videoSetMode(MODE_FB0);
}

static bool capture_compare(int test, u32 *scoreOut) {
    /* The capture unit is frame-synchronized itself; don't busy-wait for an
       extra VCOUNT wrap before arming it (some slower emulators stall here). */
    REG_DISPCAPCNT = DCAP_MODE_A |
                     DCAP_SRC_A(DCAP_SRC_A_COMPOSITED) |
                     DCAP_SIZE(DCAP_SIZE_256x192) |
                     DCAP_BANK(DCAP_BANK_VRAM_A);
    REG_DISPCAPCNT |= DCAP_ENABLE;
    bool completed = false;
    for (volatile u32 budget = CAPTURE_POLL_BUDGET; budget > 0; --budget) {
        if (!(REG_DISPCAPCNT & DCAP_ENABLE)) {
            completed = true;
            break;
        }
    }
    if (!completed) {
        /* A broken/unsupported capture path must not wedge the whole ROM. */
        REG_DISPCAPCNT = 0;
        show_capture_timeout();
        return false;
    }

    REG_DISPCAPCNT = 0;
    render_reference(test);
    u16 *actual = VRAM_A;
    u32 matches = 0;
    for (int i = 0; i < PIXELS; ++i) {
        u16 got = actual[i] & 0x7FFF;
        u16 want = expected[i];
        bool match = channel_delta(got, want, 0) <= 1 &&
                     channel_delta(got, want, 5) <= 1 &&
                     channel_delta(got, want, 10) <= 1;
        if (match) {
            ++matches;
            actual[i] = rgb15(0, 31, 0);
        } else if (got == 0 && want != 0) {
            actual[i] = rgb15(31, 0, 0);       /* missing */
        } else if (got != 0 && want == 0) {
            actual[i] = rgb15(31, 0, 31);      /* overdraw */
        } else {
            actual[i] = rgb15(0, 0, 31);       /* wrong color/position */
        }
    }
    videoSetMode(MODE_FB0);
    *scoreOut = matches;
    return true;
}

static void print_pct(u32 matches, u32 total) {
    u32 basis = (matches * 10000u) / total;
    iprintf("%u.%02u%%", (unsigned)(basis / 100), (unsigned)(basis % 100));
}

static void run_case(int index) {
    /* The sub engine is initialized once for the status console and is never
       reconfigured by a top-screen test. */
    consoleSelect(&bottomConsole);
    iprintf("\x1b[2J");
    iprintf("NDS 2D RASTER SCORE\n");
    iprintf("%s\n", testNames[index]);
    iprintf("%s\n\n", cues[index]);
    iprintf("Configuring main-engine scene...\n");

    switch (index) {
        case 0: case_text_palette(); break;
        case 1: case_scroll(); break;
        case 2: case_priority_blend(); break;
        case 3: case_affine_tiles(); break;
        case 4: case_bitmap(false); break;
        case 5: case_bitmap(true); break;
        case 6: case_window_mosaic(); break;
        case 7: case_sprites(); break;
    }

    consoleSelect(&bottomConsole);
    iprintf("Scene ready; capturing/comparing...\n");
    captureErrors[index] = !capture_compare(index, &scoreMatches[index]);
    consoleSelect(&bottomConsole);
    if (captureErrors[index]) {
        scoreMatches[index] = 0;
        iprintf("FRAME/CAPTURE TIMEOUT: skipped;\ncontinuing to next card.\n\n");
    } else {
        iprintf("PIXEL MATCH: ");
        print_pct(scoreMatches[index], PIXELS);
        iprintf(" (%lu / %u)\n\n", (unsigned long)scoreMatches[index], (unsigned)PIXELS);
        iprintf("GREEN = match\nRED = missing pixels\nPINK = extra pixels\nBLUE = wrong color/position\n");
    }
    iprintf("Next card in %u VBlanks.\n", HOLD_FRAMES);
}

static void print_summary(PrintConsole *console) {
    u32 sum = 0;
    int valid = 0, errors = 0;
    consoleSelect(console);
    iprintf("\x1b[2JNDS 2D RASTER RESULTS\n\nPER-TEST PIXEL MATCH\n");
    for (int i = 0; i < TEST_COUNT; ++i) {
        iprintf("%02d  ", i + 1);
        if (captureErrors[i]) {
            iprintf("FRAME/CAPTURE TIMEOUT\n");
            ++errors;
        } else {
            print_pct(scoreMatches[i], PIXELS);
            iprintf("\n");
            sum += scoreMatches[i];
            ++valid;
        }
    }
    iprintf("\nTOTAL: ");
    if (valid) print_pct(sum, (u32)PIXELS * (u32)valid);
    else iprintf("N/A");
    iprintf("\nValid tests: %d/%d  Capture errors: %d\n", valid, TEST_COUNT, errors);
    iprintf("GREEN match | RED missing | PINK extra\nBLUE wrong color/position\n");
    iprintf("Press START to rerun the suite.\n");
}

static void show_summary(void) {
    /* Reuse the now-idle main BG memory for a top-screen results console. */
    videoSetMode(MODE_0_2D | DISPLAY_BG0_ACTIVE);
    consoleInit(&topConsole, 0, BgType_Text4bpp, BgSize_T_256x256,
                31, 0, true, true);
    print_summary(&topConsole);
    print_summary(&bottomConsole);
}

static void run_suite(void) {
    memset(scoreMatches, 0, sizeof(scoreMatches));
    memset(captureErrors, 0, sizeof(captureErrors));
    for (int test = 0; test < TEST_COUNT; ++test) {
        run_case(test);
        for (frameCounter = 0; frameCounter < HOLD_FRAMES; ++frameCounter)
            if (!wait_for_next_frame()) break;
    }
    show_summary();
}

int main(void) {
    /* Bank A is the display-capture target; bank B backs main-engine BG VRAM. */
    vramSetBankA(VRAM_A_LCD);
    vramSetBankB(VRAM_B_MAIN_BG_0x06000000);
    vramSetBankC(VRAM_C_SUB_BG);
    vramSetBankE(VRAM_E_MAIN_SPRITE);
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    consoleInit(&bottomConsole, 0, BgType_Text4bpp, BgSize_T_256x256,
                31, 0, false, true);
    consoleSelect(&bottomConsole);
    iprintf("NDS 2D RASTER SCORE\nStarting automatically...\n");

    run_suite();
    /* Keep the results screen responsive; START reruns the complete suite. */
    while (1) {
        scanKeys();
        if (keysDown() & KEY_START) run_suite();
        (void)wait_for_next_frame();
    }
    return 0;
}
