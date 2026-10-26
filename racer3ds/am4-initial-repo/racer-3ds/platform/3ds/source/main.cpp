// Star Wars Episode I: Racer - 3DS target, application entry point.
//
// Startup is staged and every step is logged to sdmc:/3ds/racer/boot.log (flushed per
// line), because an installed title that dies during GPU or service init shows only a
// data abort, not which call failed. The last line of that file is the answer.
//
//   1. fs / boot log
//   2. input (needed for the safe-mode key check)
//   3. screen (citro2d, or console text if any part of it failed)
//   4. ROM check (reads, never writes)
//   5. memory probe
//   6. audio (ON by default and deferred to frame 2; the firmware is validated before
//      ndspInit() because a bad DSP dump aborted inside ndspInitialize. Hold R to boot
//      without audio, or put "off" in sdmc:/3ds/racer/audio.cfg to make that permanent)
//
// Keys held at launch, all of them opt-OUTs from the defaults:
//   L  safe mode: console text only - no citro3d, no citro2d, no ndsp, no ROM read. If the
//      app works that way the fault is in the GPU/audio path; if it still aborts, the boot
//      log says how far it got.
//   R  no audio for this boot (the DSP path is the one that can crash on a bad firmware).
//   X  64 MB profile: smaller command buffer and a 4 MB allocation probe, so the 80 MB
//      title behaves like the 64 MB one. The system mode itself is an exheader property and
//      cannot be changed here; install bin/racer3ds-64.cia for a real 64 MB title.
#include <3ds.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <malloc.h>

#include "r3ds.hpp"
#include "recomp_plan.h"

using namespace r3ds;

namespace {

uint16_t g_vi[kViW * kViH];
StatusLine g_lines[24];
AppMem g_mem;
RomInfo g_rom;
bool g_audio_ok = false;
bool g_safe_mode = false;
bool g_no_audio = false;
bool g_low_mem = false;
bool g_rom_checked = false;
PrepareResult g_prepare;
char g_prepare_note[96] = "not run";

const char* mode_name(uint32_t bytes) {
    if (bytes >= 176u * 1024u * 1024u) return "NEW3DS 178MB (ext)";
    if (bytes >= 120u * 1024u * 1024u) return "NEW3DS 124MB";
    if (bytes >= 76u * 1024u * 1024u)  return "HIGH 80MB (O3DS ext)";
    if (bytes >= 56u * 1024u * 1024u)  return "REGULAR 64MB";
    if (bytes >= 28u * 1024u * 1024u)  return "LOW 32MB";
    return "UNKNOWN/LOW";
}

void fill_status(StatusInfo& info, uint32_t frame, float fps) {
    char mapping[128];
    input_last_mapping(mapping, sizeof(mapping));

    info.mode_name     = g_mem.mode_name;
    info.app_total     = g_mem.app_total;
    info.app_free      = g_mem.app_free;
    info.homebrew      = envIsHomebrew();
    info.largest_probe = g_mem.largest_probe;
    info.rom_found     = g_rom.found;
    info.rom_valid     = g_rom.valid;
    info.rom_path      = g_rom.path;
    info.rom_detail    = g_rom.detail;
#ifdef R3DS_WITH_GAME
    info.game_linked   = true;
#else
    info.game_linked   = false;
#endif
    info.audio_ok      = g_audio_ok;
    info.audio_rate    = 32000;
    info.audio_note    = audio_state_text();
    info.profile_note  = g_low_mem ? "64MB profile" : "";
#ifdef R3DS_WITH_GAME
    if (game_started()) {
        static char gline[96];
        char romline[96];
        static uint32_t last_vi = 0, last_fr = 0;
        if (game_exited()) {
            std::snprintf(gline, sizeof(gline), "game: exited");
        } else {
            const uint32_t vi = game_vi_count(), fr = game_frames();
            if (vi != last_vi || fr != last_fr) {
                last_vi = vi; last_fr = fr;
                std::snprintf(romline, sizeof(romline), "%s", game_rom_path());
                std::snprintf(gline, sizeof(gline), "game: %lu VI %lu frames",
                              (unsigned long)vi, (unsigned long)fr);
            }
        }
        info.prepare_note = gline;
    } else {
        info.prepare_note = "game: starting runtime";
    }
#else
    info.prepare_note  = g_prepare_note;
#endif
    info.mapping       = g_safe_mode ? "safe mode: no input map" : mapping;
    info.frame         = frame;
    info.fps           = fps;
}

// Plain libctru console on both screens: used when citro3d/citro2d is unavailable, when
// safe mode was requested, or after an init failure. No allocation, no GPU beyond the
// framebuffers gfx already owns.
void run_console_ui(const char* why) {
    screen_init_console();
    consoleSelect(consoleGetDefault());

    std::printf("\x1b[1;1H%s\n", "RACER 3DS - SAFE / CONSOLE MODE");
    std::printf("reason: %s\n\n", why);
    std::printf("memory : %s  (%lu MB app, %lu MB free)\n",
                g_mem.mode_name,
                (unsigned long)(g_mem.app_total >> 20),
                (unsigned long)(g_mem.app_free >> 20));
    std::printf("launch : %s\n", envIsHomebrew() ? "hbmenu (.3dsx)" : "installed (.cia)");
    std::printf("cmd buf: %lu bytes\n", (unsigned long)screen_state().cmdbuf_size);
    std::printf("audio  : %s\n", audio_state_text());
    if (g_rom_checked)
        std::printf("rom    : %s\n         %s\n", g_rom.found ? g_rom.path : "(not found)", g_rom.detail);
    std::printf("\nboot log (%s):\n", bootlog_to_file() ? bootlog_path() : "memory only");

    const char* lines[24];
    const int n = bootlog_ring(lines, 24);
    for (int i = 0; i < n && i < 16; i++)
        std::printf(" %s\n", lines[i]);

    std::printf("\nSELECT = exit\n");

    while (aptMainLoop()) {
        hidScanInput();
        const u32 down = hidKeysDown();
        if (down & KEY_SELECT) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    bootlog("console mode: exiting");
}

} // namespace

namespace r3ds {

AppMem app_memory_info(uint32_t probe_bytes) {
    AppMem mi;
    mi.app_total = osGetMemRegionSize(MEMREGION_APPLICATION);
    mi.app_free  = osGetMemRegionFree(MEMREGION_APPLICATION);
    mi.mode_name = mode_name(mi.app_total);
    mi.high_memory = mi.app_total > (64u * 1024u * 1024u);

    // Prove the headroom with a real allocation. Kept modest on purpose: an installed
    // title's heap is smaller than hbmenu's and this must never fail the app.
    void* p = nullptr;
    for (uint32_t bytes = probe_bytes; bytes >= 1u * 1024u * 1024u; bytes /= 2) {
        p = memalign(0x1000, bytes);
        if (p) { mi.largest_probe = bytes; std::free(p); break; }
    }
    return mi;
}

} // namespace r3ds

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    // ---- 1. file system + boot log ---------------------------------------
    const bool fs_ok = R_SUCCEEDED(fsInit());
    bootlog_begin();
    bootlog("racer3ds " __DATE__ " " __TIME__);
    bootlog("fsInit -> %d, homebrew=%d", (int)fs_ok, (int)envIsHomebrew());

    // ---- 2. input (safe-mode key) ---------------------------------------
    // Explicit init: hidScanInput() before gfxInitDefault() is unusual and this removes
    // any doubt about service ordering on an installed title.
    // libctru service inits are refcounted, so an explicit init is safe here and makes the
    // ordering explicit. If HID is unavailable the app still runs (without input).
    const Result hid_res = hidInit();
    bootlog("hidInit -> 0x%08lX, hidSharedMem=%p", (unsigned long)hid_res, (void*)hidSharedMem);
    if (R_SUCCEEDED(hid_res) && hidSharedMem) {
        hidScanInput();
        const u32 held = hidKeysHeld();
        g_safe_mode = (held & KEY_L) != 0;
        g_no_audio  = (held & KEY_R) != 0;
        g_low_mem   = (held & KEY_X) != 0;
    } else {
        bootlog("HID unavailable: input and the safe-mode key are disabled");
    }
    bootlog("keys held: L(safe)=%d R(no audio)=%d X(64MB profile)=%d",
            (int)g_safe_mode, (int)g_no_audio, (int)g_low_mem);

    // Audio policy is decided here, before anything touches the DSP. On by default; R held
    // (or "off" in audio.cfg) is the opt-out.
    g_audio_ok = false;
    const bool want_audio = audio_wanted(g_safe_mode, g_no_audio);
    bootlog("audio policy: %s", audio_state_text());

    std::snprintf(g_prepare_note, sizeof(g_prepare_note), "%s", "plan: no ROM");

    g_mem = app_memory_info((g_safe_mode || g_low_mem) ? 4u * 1024u * 1024u
                                                       : 8u * 1024u * 1024u);
    bootlog("app mem %lu MB free %lu MB, %s, probe %lu MB",
            (unsigned long)(g_mem.app_total >> 20), (unsigned long)(g_mem.app_free >> 20),
            g_mem.mode_name, (unsigned long)(g_mem.largest_probe >> 20));

    // ---- 3. screen ------------------------------------------------------
    uint32_t frame_count = 0;

    if (g_safe_mode) {
        bootlog("safe mode: skipping citro3d/citro2d");
        run_console_ui("safe mode (L held at launch)");
    } else if (!screen_init(g_low_mem)) {
        bootlog("screen_init failed -> console fallback");
        run_console_ui("GPU init failed, see log");
    } else {
        bootlog("screen ready");

        // ---- 4. ROM -----------------------------------------------------
        if (fs_ok) {
            g_rom = rom_check();
            g_rom_checked = true;
            bootlog("rom: found=%d valid=%d path=%s (%s)",
                    (int)g_rom.found, (int)g_rom.valid, g_rom.path, g_rom.detail);
        } else {
            bootlog("fsInit failed: ROM not checked");
        }

        // ---- 4b. recompilation plan -------------------------------------
        // The console half of the build pipeline: identify the ROM against recomp.toml and
        // verify every function in the recompiler's map lands in it. Progress is drawn here
        // because the pass touches the SD card ~900 times.
        if (g_rom.found && g_rom.valid) {
            const char* kStage[] = { "reading ROM header", "verifying 906 function entries",
                                     "writing re.compile report" };
            int stage = 0;
            unsigned last_pct = 0xFFFFFFFFu;
            auto draw_progress = [&](unsigned done, unsigned total) {
                const unsigned pct = total ? done * 100u / total : 0;
                if (pct == last_pct) return;
                last_pct = pct;
                frame_begin();
                top_clear(0x000000);
                draw_vi_image((kTopW - kViW) / 2, 0, g_vi, kViW, kViH);
                bot_clear(0x0B0F14);
                bot_text(4,   0, 0xE8C547, 0.45f, "RACER 3DS - RECOMPILATION PLAN");
                bot_text(4,  16, 0x8B949E, 0.40f, "the console half of the pipeline");
                bot_text(4,  36, 0x8FD3FF, 0.40f, "stage: %s", kStage[stage < 3 ? stage : 2]);
                bot_text(4,  52, 0xE6E6E6, 0.40f, "%u / %u functions  (%u%%)", done, total, pct);
                // 40-char progress bar
                char bar[48];
                const unsigned filled = pct * 40u / 100u;
                for (unsigned i = 0; i < 40; i++) bar[i] = (i < filled) ? '#' : '-';
                bar[40] = 0;
                bot_text(4,  72, 0x7EE787, 0.40f, "%s", bar);
                bot_text(4,  96, 0x8B949E, 0.38f, "ROM  CRC1 0x%08lX", (unsigned long)g_rom.crc1);
                bot_text(4, 112, 0x8B949E, 0.38f, "want CRC1 0x%08X", recomp_plan::kExpectedCrc1);
                frame_end();
            };
            auto on_progress = [](unsigned done, unsigned total, void* user) {
                (*static_cast<decltype(&draw_progress)>(user))(done, total);
            };
            draw_progress(0, (unsigned)recomp_plan::kFuncCount);
            stage = 1;
            g_prepare = prepare_run(on_progress, &draw_progress);
            stage = 2;
            last_pct = 0xFFFFFFFFu;
            draw_progress(g_prepare.funcs_total, g_prepare.funcs_total);
            bootlog("prepare: %s", g_prepare.rom_ok ? "plan OK" : "plan FAILED");
            if (!g_rom.found || !g_rom.valid)
                std::snprintf(g_prepare_note, sizeof(g_prepare_note), "plan: no ROM");
            else if (g_prepare.crc_ok && g_prepare.funcs_bad == 0)
                std::snprintf(g_prepare_note, sizeof(g_prepare_note),
                              "plan: %u/%u OK%s", g_prepare.funcs_ok, g_prepare.funcs_total,
                              g_prepare.cached ? " (cached)" : "");
            else if (!g_prepare.crc_ok)
                std::snprintf(g_prepare_note, sizeof(g_prepare_note), "plan: ROM CRC mismatch");
            else
                std::snprintf(g_prepare_note, sizeof(g_prepare_note),
                              "plan: %u bad entries", g_prepare.funcs_bad);
        }

#ifdef R3DS_WITH_GAME
        // ---- 4c. start the game -------------------------------------------
        // The recompiled game runs on its own thread; this loop keeps drawing the status
        // screen and pumping the pad. If the runtime cannot start (no ROM, no runtime
        // objects linked) the app stays a bring-up build instead of dying.
        if (!game_run())
            bootlog("game: could not start the runtime");
        else
            bootlog("game: runtime thread started");
#endif

        // ---- 5/6. frames, then audio ------------------------------------
        u64 last_tick = svcGetSystemTick();
        float fps = 0.0f;

        while (aptMainLoop()) {
            input_poll();
            if (hidKeysDown() & KEY_SELECT) break;

            // Audio comes up lazily, after the app has proven it can draw: a console
            // without DSP firmware must not take the screen down with it.
            if (want_audio && !g_audio_ok && frame_count == 2) {
                g_audio_ok = audio_init(32000);
                bootlog("audio_init -> %d (0x%08lX)", (int)g_audio_ok, (unsigned long)audio_last_error());
            }

            vi_test_pattern(g_vi, (int)frame_count);

            StatusInfo info;
            fill_status(info, frame_count, fps);
            const int lines = build_status_lines(info, g_lines, 24);

            frame_begin();
            top_clear(0x000000);
            draw_vi_image((kTopW - kViW) / 2, 0, g_vi, kViW, kViH);
            bot_clear(0x0B0F14);
            for (int i = 0; i < lines; i++)
                bot_text(4, i * 12, g_lines[i].rgb, 0.40f, "%s", g_lines[i].text);
            frame_end();

            frame_count++;
            const u64 now = svcGetSystemTick();
            if (now - last_tick > (SYSCLOCK_ARM11 / 2)) {
                fps = (float)frame_count * (float)SYSCLOCK_ARM11 / (float)(now - last_tick);
                frame_count = 0;
                last_tick = now;
            }
        }
    }

    bootlog("exiting after %u frames", (unsigned)frame_count);
    screen_exit();
    audio_exit();
    bootlog_close();
    if (fs_ok) fsExit();
    return 0;
}
