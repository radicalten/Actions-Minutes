// Boot trace + crash breadcrumbs.
//
// An installed title that dies before it can draw gives no information on the console: the
// exception screen shows registers, not "which init call failed". So every startup step
// appends one line to sdmc:/3ds/racer/boot.log and flushes it immediately. Whatever the last
// line is, that is the step that killed it. The lines are also kept in a small ring buffer
// so the app can print them on screen without the SD card.
#include <3ds.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>

#include "r3ds.hpp"

namespace r3ds {
namespace {

constexpr int kRingLines = 40;
constexpr int kLineLen = 72;

char s_ring[kRingLines][kLineLen];
int s_count = 0;

FILE* s_file = nullptr;
bool s_tried_open = false;
char s_path[64] = "";

void ring_push(const char* line) {
    if (s_count == kRingLines) {
        std::memmove(s_ring[0], s_ring[1], (size_t)(kRingLines - 1) * kLineLen);
        s_count--;
    }
    std::snprintf(s_ring[s_count], kLineLen, "%s", line);
    s_count++;
}

void write_file(const char* line) {
    if (!s_file) return;
    std::fputs(line, s_file);
    std::fputc('\n', s_file);
    // Flushed every line on purpose: if the next step aborts, this is the last thing on
    // the card.
    std::fflush(s_file);
}

} // namespace

void bootlog_begin() {
    if (s_tried_open) return;
    s_tried_open = true;

    // The sdmc: devoptab is mounted by libctru before main(), so plain paths work; both
    // spellings are tried because they differ between hbmenu and installed titles.
    static const char* candidates[] = {
        "/3ds/racer/boot.log",
        "sdmc:/3ds/racer/boot.log",
        "/boot.log",
    };
    for (const char* path : candidates) {
        s_file = std::fopen(path, "w");   // "w": every boot starts a fresh trace
        if (s_file) {
            std::snprintf(s_path, sizeof(s_path), "%s", path);
            break;
        }
    }
    write_file(s_file ? "boot log opened" : "boot log unavailable (no SD write access)");
}

bool bootlog_to_file() { return s_file != nullptr; }
const char* bootlog_path() { return s_path; }

void bootlog(const char* fmt, ...) {
    char line[kLineLen];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (!s_tried_open) bootlog_begin();
    ring_push(line);
    write_file(line);
}

int bootlog_ring(const char** out, int max) {
    if (!out || max <= 0) return 0;
    const int n = (s_count < max) ? s_count : max;
    for (int i = 0; i < n; i++) out[i] = s_ring[s_count - n + i];
    return n;
}

void bootlog_close() {
    if (s_file) {
        write_file("-- end of boot log --");
        std::fclose(s_file);
        s_file = nullptr;
    }
}

} // namespace r3ds
