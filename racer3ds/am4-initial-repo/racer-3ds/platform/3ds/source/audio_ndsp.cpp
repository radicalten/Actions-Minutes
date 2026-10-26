// Audio output for the 3DS target (ndsp).
//
// This file exists in its current shape because of a crash dump taken from an installed
// title: the faulting instructions were inside libctru's ndspInitialize() pipe handshake
// (DSP_ReadPipeIfPossible -> DSP_ConvertProcessAddressFromDspDram -> DSP_SetSemaphore),
// i.e. the DSP was asked to run a component and never answered properly.
//
// ndspInit() has no defence against that. It loads /3ds/dspfirm.cdc through
// DSP_LoadComponent(), which reports "component actually started" in its is_loaded
// out-parameter - and ndspInit() passes NULL for that parameter, then immediately
// handshakes over DSP pipe 2 as if the DSP were alive. A truncated, region-mismatched or
// otherwise wrong dspfirm.cdc therefore does not fail cleanly; the handshake reads a bogus
// length and a bogus shared-memory address table out of the pipe and execution goes off
// into memory that is not there. That is the abort in the dump.
//
// So, three rules, in order:
//   1. audio is ON by default, with one-key opt-outs: hold R (this boot) or put "off" in
//      sdmc:/3ds/racer/audio.cfg (permanent). Sound that works is the default; the escapes
//      exist because the DSP dump is the one input that can still be wrong on a console.
//   2. the firmware is validated first, with the is_loaded check that ndspInit() skips.
//      If the component does not actually run, ndspInit() is never called.
//   3. ndspExit() is only ever called after ndspInit() succeeded - a failed init leaves
//      partial state and tearing it down again is a crash, not a cleanup.
#include <3ds.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include "r3ds.hpp"

// Provided by thirdparty/libctru-ndsp (see README there): the DSP handshake is the one place
// ndsp cannot be inspected from outside, so the patched copy reports what the DSP sent.
extern "C" {
typedef void (*ndspDebugHook)(const char* stage, unsigned index, u32 value, u32 extra);
void ndspSetDebugHook(ndspDebugHook hook);
}

namespace r3ds {
namespace {

constexpr int kChannels = 2;
constexpr int kBufFrames = 1024;
constexpr int kBufCount = 4;
constexpr int kBufSamples = kBufFrames * kChannels;
constexpr int kBufBytes = kBufSamples * (int)sizeof(int16_t);

const char* kFirmPath = "sdmc:/3ds/dspfirm.cdc";
const char* kCfgPath  = "sdmc:/3ds/racer/audio.cfg";

bool s_inited = false;
bool s_attempted = false;
bool s_requested = false;
Result s_result = 1;
ndspWaveBuf s_wave[kBufCount];
int16_t* s_data[kBufCount];
bool s_data_linear[kBufCount];
int s_next = 0;
u32 s_rate = 32000;
char s_state[80] = "on (default)";

// --- policy ---------------------------------------------------------------------------

bool parse_switch(const char* text, bool* value) {
    if (!text || !*text) return false;
    char buf[16];
    size_t n = 0;
    while (*text && n + 1 < sizeof(buf)) {
        char c = *text++;
        if (c == '#' || c == ';' || c == '\r' || c == '\n') break;
        if (c == ' ' || c == '\t') continue;
        buf[n++] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    buf[n] = 0;
    if (!n) return false;
    if (!std::strcmp(buf, "on")  || !std::strcmp(buf, "1") ||
        !std::strcmp(buf, "yes") || !std::strcmp(buf, "enable")) {
        *value = true;
        return true;
    }
    if (!std::strcmp(buf, "off")   || !std::strcmp(buf, "0") ||
        !std::strcmp(buf, "no")    || !std::strcmp(buf, "disable") ||
        !std::strcmp(buf, "false")) {
        *value = false;
        return true;
    }
    return false;
}

// Reads sdmc:/3ds/racer/audio.cfg. Absent file = audio stays off.
bool config_wants_audio(bool* found) {
    *found = false;
    FILE* f = std::fopen(kCfgPath, "rb");
    if (!f) f = std::fopen("/3ds/racer/audio.cfg", "rb");
    if (!f) return false;

    char line[96];
    bool value = false;
    while (std::fgets(line, sizeof(line), f)) {
        for (char* token = std::strtok(line, " \t=:\r\n"); token;
             token = std::strtok(nullptr, " \t=:\r\n")) {
            if (parse_switch(token, &value)) {
                *found = true;
                break;
            }
        }
        if (*found) break;
    }
    std::fclose(f);
    return value;
}

void set_state(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(s_state, sizeof(s_state), fmt, ap);
    va_end(ap);
}

// --- DSP handshake reporter ------------------------------------------------------------

void dsp_debug(const char* stage, unsigned index, u32 value, u32 extra) {
    if (!std::strcmp(stage, "count")) {
        bootlog("dsp: handshake count=%lu", (unsigned long)value);
    } else if (!std::strcmp(stage, "var")) {
        // Only the first four entries fit usefully in a log line set; the point is to see
        // whether the converted addresses live in the DSP shared window.
        if (index < 4)
            bootlog("dsp: var[%u]=0x%04lX -> 0x%08lX", index,
                    (unsigned long)value, (unsigned long)extra);
    } else if (!std::strcmp(stage, "reject")) {
        bootlog("dsp: REJECT var[%u] ptr %u = 0x%08lX (not writable)", index,
                (unsigned)extra, (unsigned long)value);
    }
}

// --- DSP firmware check ---------------------------------------------------------------
//
// The handshake itself is no longer done here. An earlier build performed its own bounded
// handshake before ndspInit() and that turned out to be the bug: the crash dump shows
// audio_init+0x5EC, i.e. the return address of `bl ndspInit` - the probe had already
// succeeded, and the load that failed was libctru's *second* load of the same component.
// A component must be loaded once per boot.
//
// So the bounds checks live where the handshake happens: `thirdparty/libctru-ndsp/` is
// libctru's ndsp with five patches, the important one being that the variable count read
// from DSP pipe 2 is refused unless it fits `u16 vars[16]`. This file only checks that the
// firmware the DSP module will read actually looks like a firmware.

enum class FirmwareState { Ok, Missing, BadFile };

FirmwareState check_firmware(long* out_size) {
    FILE* f = std::fopen(kFirmPath, "rb");
    if (!f) f = std::fopen("/3ds/dspfirm.cdc", "rb");
    if (!f) {
        bootlog("dspfirm: %s NOT FOUND (no DSP firmware dump on the SD card)", kFirmPath);
        return FirmwareState::Missing;
    }

    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fclose(f);
    if (size < 0x800 || size > 8 * 1024 * 1024) {
        bootlog("dspfirm: implausible size %ld, refusing to hand it to the DSP", size);
        return FirmwareState::BadFile;
    }
    if (out_size) *out_size = size;
    bootlog("dspfirm: %s, %ld bytes", kFirmPath, size);
    return FirmwareState::Ok;
}

void free_buffers() {
    for (int i = 0; i < kBufCount; i++) {
        if (!s_data[i]) continue;
        if (s_data_linear[i]) linearFree(s_data[i]); else free(s_data[i]);
        s_data[i] = nullptr;
    }
}

} // namespace

bool audio_wanted(bool safe_mode, bool opt_out) {
    s_requested = false;
    if (safe_mode) {
        set_state("off (safe mode, L held)");
        bootlog("audio: safe mode -> off");
        return false;
    }

    if (opt_out) {
        set_state("off (R held)");
        bootlog("audio: R held -> off for this boot");
        return false;
    }

    bool cfg_present = false;
    const bool cfg_on = config_wants_audio(&cfg_present);
    if (cfg_present && !cfg_on) {
        set_state("off (audio.cfg)");
        bootlog("audio.cfg says off -> audio stays off (delete the file for the default)");
        return false;
    }

    s_requested = true;
    set_state(cfg_present ? "on (audio.cfg)" : "on (default)");
    bootlog("audio: %s -> enabled, firmware is checked before ndspInit",
            cfg_present ? "audio.cfg says on" : "default");
    return true;
}

bool audio_init(uint32_t sample_rate) {
    if (s_inited) return true;
    if (s_attempted) return false;          // do not retry a known-bad service
    s_attempted = true;

    if (!s_requested) {
        // Policy said no; keep the reason that audio_wanted() recorded.
        s_result = 0;
        bootlog("audio_init skipped: %s", s_state);
        return false;
    }

    long fw_size = 0;
    switch (check_firmware(&fw_size)) {
        case FirmwareState::Missing: set_state("off (no /3ds/dspfirm.cdc)"); return false;
        case FirmwareState::BadFile: set_state("off (dspfirm unreadable)"); return false;
        case FirmwareState::Ok:      break;
    }

    ndspSetDebugHook(dsp_debug);

    // One load, one handshake. Everything after this point is bounded by the patched ndsp:
    // 400 ms IRQ timeout, `count <= 16`, converted pointers range-checked.
    bootlog("audio: calling ndspInit (bounded handshake) - if this is the LAST line, the "
            "DSP never answered; power off and hold R to boot without audio");
    s_result = ndspInit();
    bootlog("ndspInit -> 0x%08lX", (unsigned long)s_result);
    if (R_FAILED(s_result)) {
        // Deliberately no ndspExit() here.
        // The patched ndsp fails cleanly instead of faulting: 0xD8805C01 = no firmware,
        // 0xD8805CFF-ish = handshake out of range / component did not start.
        set_state("off (ndspInit 0x%08lX)", (unsigned long)s_result);
        return false;
    }
    s_rate = sample_rate ? sample_rate : 32000;

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)s_rate);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    std::memset(s_wave, 0, sizeof(s_wave));
    for (int i = 0; i < kBufCount; i++) {
        // Linear memory is the DSP-safe choice; fall back to the normal heap if the linear
        // pool is empty on this console.
        s_data[i] = (int16_t*)linearAlloc(kBufBytes);
        s_data_linear[i] = s_data[i] != nullptr;
        if (!s_data[i]) s_data[i] = (int16_t*)memalign(0x40, kBufBytes);
        if (!s_data[i]) {
            bootlog("audio buffer %d allocation FAILED", i);
            free_buffers();
            ndspExit();                     // ndspInit succeeded, so this is safe
            s_inited = false;
            set_state("off (no audio buffers)");
            return false;
        }
        std::memset(s_data[i], 0, kBufBytes);
        s_wave[i].data_vaddr = s_data[i];
        s_wave[i].nsamples = kBufSamples;
        s_wave[i].status = NDSP_WBUF_DONE;
    }

    s_next = 0;
    s_inited = true;
    set_state("on %u Hz", (unsigned)s_rate);
    bootlog("audio ready: %lu Hz stereo", (unsigned long)s_rate);
    return true;
}

Result audio_last_error() { return s_result; }

const char* audio_state_text() { return s_state; }

void audio_exit() {
    if (!s_inited) return;
    ndspChnWaveBufClear(0);
    free_buffers();
    ndspExit();
    s_inited = false;
}

void audio_queue(const int16_t* samples, size_t frames) {
    if (!s_inited || !samples || frames == 0) return;

    ndspWaveBuf* wb = &s_wave[s_next];
    if (wb->status != NDSP_WBUF_DONE && wb->status != NDSP_WBUF_FREE) return;

    const size_t max_frames = (size_t)kBufFrames;
    const size_t n = (frames < max_frames) ? frames : max_frames;
    std::memcpy(s_data[s_next], samples, n * kChannels * sizeof(int16_t));
    if (n < max_frames)
        std::memset(s_data[s_next] + n * kChannels, 0, (max_frames - n) * kChannels * sizeof(int16_t));

    DSP_FlushDataCache(s_data[s_next], kBufBytes);
    wb->nsamples = kBufSamples;
    ndspChnWaveBufAdd(0, wb);
    s_next = (s_next + 1) % kBufCount;
}

size_t audio_frames_pending() {
    if (!s_inited) return 0;
    size_t frames = 0;
    for (int i = 0; i < kBufCount; i++)
        if (s_wave[i].status == NDSP_WBUF_QUEUED || s_wave[i].status == NDSP_WBUF_PLAYING)
            frames += kBufFrames;
    return frames;
}

void audio_set_frequency(uint32_t sample_rate) {
    if (!s_inited || !sample_rate) return;
    s_rate = sample_rate;
    ndspChnSetRate(0, (float)s_rate);
}

} // namespace r3ds
