// Star Wars Episode I: Racer - 3DS platform layer (bring-up target)
//
// This is the Nintendo 3DS build target for the Racer static recompilation.
// It provides everything the recompilation runtime needs from a host:
//   * a display path  (N64 VI scanout -> PICA200, plus a ROM-less test pattern)
//   * an audio path   (N64 AI stream -> ndsp)
//   * an input path   (3DS buttons + circle pad -> N64 pad)
//   * a ROM path      (sdmc:/3ds/racer/baserom.z64, CRC checked like the PC Makefile)
//   * a renderer stub (ultramodern::renderer::RendererContext implementation)
//
// Built without the ROM-derived code (RecompiledFuncs / rsp/aspMain.cpp) it runs as a
// diagnostic/self-test app: it reports the memory mode it was given, checks the ROM,
// exercises the VI scanout and the input/audio paths. Built with them (GAME=1) the same
// platform layer is what the recompiled game runs on top of.
#pragma once

#include <cstdint>
#include <cstddef>

#if defined(__3DS__)
#include <3ds.h>   // Result
#else
typedef int32_t Result;
#endif

namespace r3ds {

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------
constexpr int kTopW = 400, kTopH = 240;   // upper screen
constexpr int kBotW = 320, kBotH = 240;   // lower screen (touch)

// N64 VI output for this title: 320x240, 16bpp RGBA5551.
constexpr int kViW = 320, kViH = 240;

// How the app ended up talking to the screens. If any part of GPU init fails on the
// running console the app demotes itself to console text instead of crashing.
enum class ScreenMode {
    Uninitialised,
    Citro2d,     // full path: VI image on the upper screen, text on both
    Console,     // fallback: libctru console text on both screens
    Dead,        // gfxInitDefault itself failed
};

struct ScreenState {
    ScreenMode mode = ScreenMode::Uninitialised;
    bool top_ok = false;
    bool bottom_ok = false;
    bool vi_texture = false;
    uint32_t cmdbuf_size = 0;
};

// low_mem (X held) starts the citro3d command-buffer ladder at 64 KiB instead of 256 KiB
// and is paired with a smaller allocation probe: the app behaves like a 64 MB title even
// when it was launched with 80 MB. It cannot change the system mode itself - that is fixed
// in the exheader and the console reboots into it before main() runs.
bool screen_init(bool low_mem = false);   // true if the citro2d path came up
bool screen_init_console();         // gfx + libctru console (fallback/safe mode; drops citro3d)
void screen_drop_gpu();             // tear down citro3d/citro2d, keep gfx
void screen_use_console();          // force the text fallback (safe mode)
void screen_exit();
const ScreenState& screen_state();

// Per-frame: clears both screens.
void frame_begin();
void frame_end();               // presents

// Top screen: draw the 320x240 VI image (letterboxed, 4:3 preserved) at (x,y).
void draw_vi_image(int x, int y, const uint16_t* rgba5551, int w, int h);

// Text drawing. On the top screen we use the built-in 8x8 font so the VI image and the
// overlay share one code path; on the bottom screen we use citro2d's system font.
void top_text(int x, int y, uint32_t rgb, const char* fmt, ...);
void bot_text(int x, int y, uint32_t rgb, float scale, const char* fmt, ...);
void bot_clear(uint32_t rgb);
void top_clear(uint32_t rgb);

// ---------------------------------------------------------------------------
// VI (video interface) emulation
// ---------------------------------------------------------------------------
// The N64 framebuffer as the game leaves it in RDRAM, scanned out the way the VI does:
// 16bpp RGBA5551 or 32bpp RGBA8888, `width` pixels per line, `origin` = byte offset of
// the framebuffer inside RDRAM. `buf` is a 320x240 RGBA5551 destination.
void vi_scanout_rdram(uint16_t* out, const uint8_t* rdram, uint32_t origin,
                      uint32_t width, uint32_t rows, uint32_t status);

// Synthetic "framebuffer" used when there is no game running: colour bars, a gradient,
// a moving pod, and text rendered with the built-in font. Writes 320x240 RGBA5551.
void vi_test_pattern(uint16_t* out, int frame);

// ---------------------------------------------------------------------------
// ROM
// ---------------------------------------------------------------------------
struct RomInfo {
    bool found = false;
    bool valid = false;
    const char* path = "/3ds/racer/baserom.z64";
    char detail[96] = "not checked";
    uint32_t crc1 = 0, crc2 = 0;
    uint32_t size = 0;
};
RomInfo rom_check();                       // looks on the SD card

// ---------------------------------------------------------------------------
// Recompilation plan (the console half of the build pipeline)
// ---------------------------------------------------------------------------
// Recompiling ROM -> C happens on a PC. On the console the same plan is checked against the
// ROM that is actually on the SD card, so a wrong ROM is caught here instead of as a build
// failure later. See source/prepare.cpp.
struct PrepareResult {
    bool ran = false;              // the pass ran at all
    bool rom_ok = false;           // CRCs match and every map entry verified
    bool crc_ok = false;           // header CRCs match recomp.toml
    bool cached = false;           // a previous report already matched this ROM
    bool report_written = false;
    bool converted = false;        // ROM was v64/n64 and was rewritten as baserom.z64
    char canonical_path[64] = "";
    unsigned funcs_total = 0, funcs_ok = 0, funcs_bad = 0;
    unsigned first_bad_index = 0;
    uint32_t first_bad_vram = 0, first_bad_word = 0;
    uint32_t crc1 = 0, crc2 = 0;
    char report_path[64] = "";
};

using PrepareProgress = void (*)(unsigned done, unsigned total, void* user);
PrepareResult prepare_run(PrepareProgress progress, void* user);

// ---------------------------------------------------------------------------
// Game build (only with GAME=1; source/game.cpp)
// ---------------------------------------------------------------------------
// Starts the recompiled game on its own thread and reports what it is doing, so the app can
// draw a status screen around it instead of replacing one.
bool game_run();
bool game_started();
bool game_exited();
uint32_t game_vi_count();      // VI interrupts the runtime has delivered
uint32_t game_frames();
const char* game_rom_path();
void game_count_frame();

// ---------------------------------------------------------------------------
// Audio (ndsp)
// ---------------------------------------------------------------------------
// Audio is ON by default. opt_out (R held) turns it off for this boot, "off" in
// sdmc:/3ds/racer/audio.cfg turns it off permanently, and safe mode always wins.
// audio_init() additionally refuses to touch the DSP unless the firmware passed the
// is_loaded probe that libctru's ndspInit() skips.
bool audio_wanted(bool safe_mode, bool opt_out);
bool audio_init(uint32_t sample_rate);
Result audio_last_error();      // ndspInit() result, for the status screen
const char* audio_state_text(); // one line: why audio is on/off
void audio_exit();
// 16-bit stereo interleaved samples, N64 AI frame size.
void audio_queue(const int16_t* samples, size_t frames);
size_t audio_frames_pending();
void audio_set_frequency(uint32_t sample_rate);

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
struct PadState {
    uint16_t buttons = 0;   // N64 button bitmask (see r3ds::Btn)
    float x = 0.0f, y = 0.0f;  // analog stick, -1..1
};
namespace Btn {  // N64 controller bits
enum : uint16_t {
    A = 0x8000, B = 0x4000, Z = 0x2000, Start = 0x1000,
    DUp = 0x0800, DDown = 0x0400, DLeft = 0x0200, DRight = 0x0100,
    L = 0x0020, R = 0x0010,
    CUp = 0x0008, CDown = 0x0004, CLeft = 0x0002, CRight = 0x0001,
};
}
void input_poll();
PadState input_get(int port);
const char* input_last_mapping(char* line, size_t len);


// ---------------------------------------------------------------------------
// Status screen (shared by the app and by tools/preview, so the preview really is
// what the console shows)
// ---------------------------------------------------------------------------
struct StatusInfo {
    const char* mode_name = "unknown";
    uint32_t app_total = 0;         // bytes
    uint32_t app_free = 0;
    bool homebrew = true;           // launched from hbmenu (.3dsx) vs installed (.cia)
    uint32_t largest_probe = 0;     // biggest successful allocation, bytes
    bool rom_found = false;
    bool rom_valid = false;
    const char* rom_path = "";
    const char* rom_detail = "";
    bool game_linked = false;
    bool audio_ok = false;
    uint32_t audio_rate = 0;
    const char* audio_note = "";    // audio_state_text()
    const char* profile_note = "";  // "64MB profile" when X was held
    const char* prepare_note = "";  // recompilation plan verdict
    const char* mapping = "";
    uint32_t frame = 0;
    float fps = 0.0f;
};

struct StatusLine {
    uint32_t rgb;
    char text[96];
};

// Fills `out` with at most `max` lines; returns how many were written.
int build_status_lines(const StatusInfo& info, StatusLine* out, int max);

// ---------------------------------------------------------------------------
// Display list walking (the seed of a PICA200 RDP interpreter)
// ---------------------------------------------------------------------------
struct DlStats {
    uint32_t commands = 0;
    uint32_t enddl = 0;
    uint32_t bytes = 0;
    uint32_t opcode_hist[256] = {};
};
DlStats dl_walk(const uint8_t* dl, uint32_t size);

// Guest (N64) address -> physical offset inside RDRAM. Replaces the PC runtime's
// `addr - 0xFFFFFFFF80000000` scheme, which needs a 4 GiB virtual reservation.
size_t n64_to_offset(uint64_t addr);

// ---------------------------------------------------------------------------
// Boot trace (sdmc:/3ds/racer/boot.log, one line per startup step, flushed immediately)
// ---------------------------------------------------------------------------
void bootlog_begin();
void bootlog(const char* fmt, ...);
int bootlog_ring(const char** out, int max);
bool bootlog_to_file();
const char* bootlog_path();
void bootlog_close();

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------
struct AppMem {
    uint32_t app_total = 0;    // bytes of application memory the app was given
    uint32_t app_free = 0;
    uint32_t largest_probe = 0;   // biggest successful malloc probe
    bool high_memory = false;
    const char* mode_name = "unknown";
};
AppMem app_memory_info(uint32_t probe_bytes);

} // namespace r3ds
