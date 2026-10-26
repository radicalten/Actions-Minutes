// The lower-screen status display, shared with tools/preview so the host-side mockup
// renders exactly the text the console shows.
#include <cstdio>
#include <cstring>
#include <cstdarg>

#include "r3ds.hpp"

// Overridable so tools/preview can print the cross-compiler that actually built the
// 3DS binary instead of the host compiler running the preview tool.
#ifndef R3DS_CC_VERSION
#define R3DS_CC_VERSION __VERSION__
#endif

namespace r3ds {
namespace {

constexpr uint32_t kTitle = 0xE8C547;
constexpr uint32_t kKey   = 0x8FD3FF;
constexpr uint32_t kValue = 0xE6E6E6;
constexpr uint32_t kWarn  = 0xFF7B72;
constexpr uint32_t kOk    = 0x7EE787;
constexpr uint32_t kDim   = 0x8B949E;

struct Writer {
    StatusLine* out;
    int max;
    int count = 0;

    void add(uint32_t rgb, const char* fmt, ...) {
        if (count >= max) return;
        out[count].rgb = rgb;
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(out[count].text, sizeof(out[count].text), fmt, ap);
        va_end(ap);
        count++;
    }
    void rule() { add(kDim, "----------------------------------------"); }
};

} // namespace

int build_status_lines(const StatusInfo& info, StatusLine* out, int max) {
    if (!out || max <= 0) return 0;
    Writer w{out, max};

    // MB with one decimal, computed without pulling float printf into the device build.
    auto mb10 = [](uint32_t bytes) { return (unsigned)((bytes >> 10) * 10u / 1024u); };
    auto mbs = [&](uint32_t bytes) { return (double)mb10(bytes) / 10.0; };

    // Exactly 20 lines of 12 px = the height of the lower screen. Lines are kept under
    // ~36 characters so they fit both the system font on the console and the 8x8 preview.
    w.add(kTitle, "RACER 3DS - PLATFORM BRING-UP");
    w.add(kDim,   "%s  %s", R3DS_CC_VERSION, __DATE__);
    w.rule();

    w.add(kKey,   "MEMORY%s",
          (info.profile_note && *info.profile_note) ? " (64MB profile)" : "");
    w.add(kValue, " app %.1fMB free %.1fMB", mbs(info.app_total), mbs(info.app_free));
    w.add(kValue, " mode %s", info.mode_name);
    w.add(info.largest_probe ? kOk : kWarn, " probe %uMB %s  launch %s",
          (unsigned)(info.largest_probe >> 20), info.largest_probe ? "OK" : "FAIL",
          info.homebrew ? ".3dsx" : ".cia");
    w.rule();

    w.add(kKey,   "ROM");
    w.add(kValue, " %s", info.rom_found ? info.rom_path : "(not found)");
    w.add(info.rom_valid ? kOk : kWarn, " %s", info.rom_detail[0] ? info.rom_detail : "-");
    w.rule();

    w.add(kKey,   "RECOMPILE PLAN");
    w.add(kValue, " %s", info.prepare_note ? info.prepare_note : "-");
    w.rule();

    w.add(kKey,   "GAME CODE");
    if (info.game_linked) {
        w.add(kOk,  " recompiled functions linked");
    } else {
        w.add(kWarn," not linked (no RecompiledFuncs)");
    }
    w.add(kDim,   " tools/recompile.sh, BUILDING.md");

    // (no "PATHS" header: the screen is exactly 20 lines tall)
    w.add(kValue, " renderer PICA200 VI scanout");
    w.add(info.audio_ok ? kOk : kWarn, " audio %s",
          (info.audio_note && *info.audio_note) ? info.audio_note : "off");
    w.add(kValue, " input %s", info.mapping);
    w.add(kDim,   " top: VI pattern f%u %.0ffps",
          (unsigned)info.frame, (double)info.fps);
    w.add(kTitle, "SELECT = exit");
    return w.count;
}

} // namespace r3ds
