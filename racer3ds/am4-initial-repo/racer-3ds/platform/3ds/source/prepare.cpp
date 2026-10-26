// The on-console half of the build pipeline.
//
// Recompiling the game (ROM -> C -> ARM) only happens on a PC: N64Recomp reads the ROM and
// writes C, and there is no C compiler on a 3DS. What a console *can* do - and what this file
// does at every boot - is everything up to that point:
//
//   1. identify the ROM (magic, size, internal name, CRC1/CRC2 against the recompiler config),
//   2. walk the project's function map (906 entries, generated from symbols.toml into
//      recomp_plan.h) and verify that every entry really lands inside this ROM's code section
//      and starts with a plausible MIPS instruction,
//   3. write sdmc:/3ds/racer/recompile.txt - a report that says whether this ROM is the one
//      the recompiler expects, and what to run on the PC if it is.
//
// That is the same role the asset step plays in other N64 ports: the console validates and
// prepares, the PC generates code. Dumps 1-5 of this port all came from a console where the
// ROM was fine and the DSP was not; this stage is what tells the two apart in future.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <cstdint>

#include "r3ds.hpp"
#include "recomp_plan.h"

namespace r3ds {
namespace {

const char* kReportPaths[] = {
    "sdmc:/3ds/racer/recompile.txt",
    "/3ds/racer/recompile.txt",
};

// A function entry in the map is only usable if the four bytes it points at look like code:
// not blank, not erased flash. Real validation is the recompiler's job; this catches the
// common failure of pointing an entry map at the wrong ROM.
bool plausible_instruction(uint32_t word) {
    return word != 0x00000000u && word != 0xFFFFFFFFu;
}

// "CRC1 0x…" lines let a later boot skip the scan without trusting a timestamp.
bool report_matches(const char* path, uint32_t crc1, uint32_t crc2) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char line[160];
    bool ok1 = false, ok2 = false;
    while (std::fgets(line, sizeof(line), f)) {
        unsigned a = 0, b = 0;
        if (std::sscanf(line, "CRC1 0x%8X 0x%8X", &a, &b) == 2) {
            ok1 = (a == crc1);
            ok2 = (b == crc2);
        }
    }
    std::fclose(f);
    return ok1 && ok2;
}

bool write_report(const PrepareResult& r, const RomInfo& rom) {
    for (const char* path : kReportPaths) {
        FILE* f = std::fopen(path, "wb");
        if (!f) continue;
        std::fprintf(f, "Racer 3D - recompilation report\n");
        std::fprintf(f, "ROM        : %s (%lu bytes)\n", rom.path, (unsigned long)rom.size);
        std::fprintf(f, "CRC1 0x%08lX 0x%08lX\n", (unsigned long)r.crc1, (unsigned long)r.crc2);
        std::fprintf(f, "expected   : CRC1 0x%08lX 0x%08lX\n",
                     (unsigned long)recomp_plan::kExpectedCrc1,
                     (unsigned long)recomp_plan::kExpectedCrc2);
        std::fprintf(f, "internal   : \"%s\"\n", recomp_plan::kRomInternalName);
        std::fprintf(f, "crc match  : %s\n", r.crc_ok ? "yes" : "NO - the recompiler will refuse this ROM");
        std::fprintf(f, "function map: %u entries, %u verified, %u bad\n",
                     (unsigned)r.funcs_total, (unsigned)r.funcs_ok, (unsigned)r.funcs_bad);
        if (r.funcs_bad)
            std::fprintf(f, "first bad  : index %u vram 0x%08lX first word 0x%08lX\n",
                         (unsigned)r.first_bad_index, (unsigned long)r.first_bad_vram,
                         (unsigned long)r.first_bad_word);
        std::fprintf(f, "\nTo generate the game code on a PC (see BUILDING.md):\n");
        std::fprintf(f, "  tools/recompile.sh --rom <your baserom.z64>\n");
        std::fprintf(f, "  make game            # then copy bin/racer3ds-game.cia to the SD card\n");
        std::fclose(f);
        return true;
    }
    return false;
}

// The recompiler wants .z64 (big-endian). Consoles' SD cards are full of .v64 and .n64
// rips, and a wrong byte order reads as a wrong CRC, which reads as "your ROM is bad". So
// the console canonicalises it here: same job a PC-side ROM swapper does, done once, on the
// device, with the result written back to the card.
enum class RomOrder { Z64, V64, N64, Unknown };

RomOrder detect_order(const uint8_t b[4]) {
    // Read as bytes: the host and the console are both little-endian, so treating the first
    // four bytes as a u32 is what gets this wrong.
    if (b[0] == 0x80 && b[1] == 0x37 && b[2] == 0x12 && b[3] == 0x40) return RomOrder::Z64;
    if (b[0] == 0x37 && b[1] == 0x80 && b[2] == 0x40 && b[3] == 0x12) return RomOrder::V64;
    if (b[0] == 0x40 && b[1] == 0x12 && b[2] == 0x37 && b[3] == 0x80) return RomOrder::N64;
    return RomOrder::Unknown;
}

const char* order_name(RomOrder o) {
    switch (o) {
        case RomOrder::Z64: return "z64 (big endian)";
        case RomOrder::V64: return "v64 (16-bit swapped)";
        case RomOrder::N64: return "n64 (little endian)";
        default:            return "unknown";
    }
}

// Copies `src` to `dst` converting to big-endian. Returns bytes written, or -1.
long convert_to_z64(const char* src, const char* dst, RomOrder order,
                    PrepareProgress progress, void* user, long size) {
    FILE* in = std::fopen(src, "rb");
    if (!in) return -1;
    FILE* out = std::fopen(dst, "wb");
    if (!out) {
        std::fclose(in);
        return -1;
    }
    // Heap, not a static buffer: a 64 KiB .bss block is memory the game's RDRAM needs.
    uint8_t* buf = (uint8_t*)std::malloc(32 * 1024);
    if (!buf) {
        std::fclose(in);
        std::fclose(out);
        return -1;
    }
    long done = 0;
    while (done < size) {
        const size_t cap = 32 * 1024;
        const size_t want = (size_t)((size - done) < (long)cap ? (size - done) : (long)cap);
        const size_t got = std::fread(buf, 1, want, in);
        if (got == 0) break;
        for (size_t i = 0; i + 1 < got; i += 2) {
            if (order == RomOrder::V64) {
                const uint8_t t = buf[i]; buf[i] = buf[i + 1]; buf[i + 1] = t;
            }
        }
        if (order == RomOrder::N64) {
            // little-endian 32-bit words: reverse each 4-byte group as well
            for (size_t i = 0; i + 3 < got; i += 4) {
                const uint8_t a = buf[i], b = buf[i + 1], c = buf[i + 2], d = buf[i + 3];
                buf[i] = d; buf[i + 1] = c; buf[i + 2] = b; buf[i + 3] = a;
            }
        }
        // The N64 checksum block (0x1000-0x101000) is excluded from the 16-bit swap by
        // convention; a straight swap over the header is harmless because the CRCs are data.
        std::fwrite(buf, 1, got, out);
        done += (long)got;
        if (progress) progress((unsigned)(done >> 10), (unsigned)(size >> 10), user);
    }
    std::free(buf);
    std::fclose(in);
    std::fclose(out);
    return done;
}

} // namespace

PrepareResult prepare_run(PrepareProgress progress, void* user) {
    PrepareResult r;
    r.ran = true;
    r.rom_ok = false;

    RomInfo rom = rom_check();
    if (!rom.found || !rom.valid) {
        r.ran = false;
        bootlog("prepare: no usable ROM, nothing to plan");
        return r;
    }

    // 1. identity -------------------------------------------------------
    FILE* f = std::fopen(rom.path, "rb");
    if (!f) {
        bootlog("prepare: cannot open %s", rom.path);
        r.ran = false;
        return r;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);

    // Byte order first: everything below assumes big-endian.
    uint8_t head[4] = {0, 0, 0, 0};
    std::fseek(f, 0, SEEK_SET);
    if (std::fread(head, 1, 4, f) != 4) head[0] = 0;
    const RomOrder order = detect_order(head);
    bootlog("prepare: rom order %s (%02X %02X %02X %02X)", order_name(order),
            head[0], head[1], head[2], head[3]);

    if (order == RomOrder::V64 || order == RomOrder::N64) {
        std::fclose(f);
        const char* kCanonical = nullptr;
        for (const char* cand : { "sdmc:/3ds/racer/baserom.z64", "/3ds/racer/baserom.z64",
                                  "baserom.z64" }) {
            FILE* probe = std::fopen(cand, "ab");     // writable?
            if (probe) { std::fclose(probe); kCanonical = cand; break; }
        }
        if (!kCanonical) {
            bootlog("prepare: no writable path for the converted ROM");
            r.ran = false;
            return r;
        }
        bootlog("prepare: converting %s -> %s", order_name(order), kCanonical);
        const long written = convert_to_z64(rom.path, kCanonical, order, progress, user, size);
        if (written != size) {
            bootlog("prepare: conversion failed (%ld/%ld bytes)", written, size);
            r.ran = false;
            return r;
        }
        std::snprintf(r.canonical_path, sizeof(r.canonical_path), "%s", kCanonical);
        r.converted = true;
        bootlog("prepare: wrote canonical ROM %s (%ld bytes)", kCanonical, written);
        rom.path = r.canonical_path;      // carry on with the converted copy (static string)
        f = std::fopen(rom.path, "rb");
        if (!f) {
            bootlog("prepare: cannot reopen the converted ROM");
            r.ran = false;
            return r;
        }
    } else if (order == RomOrder::Unknown) {
        bootlog("prepare: not an N64 ROM (%02X %02X %02X %02X)", head[0], head[1], head[2], head[3]);
    }

    std::fseek(f, 0x10, SEEK_SET);
    uint8_t crc[8] = {0};
    const bool crc_read = std::fread(crc, 1, sizeof(crc), f) == sizeof(crc);
    std::fclose(f);
    r.crc1 = crc_read ? (uint32_t)(crc[0] << 24 | crc[1] << 16 | crc[2] << 8 | crc[3]) : 0;
    r.crc2 = crc_read ? (uint32_t)(crc[4] << 24 | crc[5] << 16 | crc[6] << 8 | crc[7]) : 0;
    r.crc_ok = (r.crc1 == recomp_plan::kExpectedCrc1) && (r.crc2 == recomp_plan::kExpectedCrc2);
    bootlog("prepare: rom %ld bytes, CRC1 0x%08lX CRC2 0x%08lX (%s)",
            size, (unsigned long)r.crc1, (unsigned long)r.crc2,
            r.crc_ok ? "matches recomp.toml" : "does NOT match recomp.toml");

    // 2. cached report? --------------------------------------------------
    for (const char* path : kReportPaths) {
        if (r.crc_ok && report_matches(path, r.crc1, r.crc2)) {
            r.cached = true;
            r.rom_ok = true;
            r.report_written = true;
            std::snprintf(r.report_path, sizeof(r.report_path), "%s", path);
            bootlog("prepare: report %s already matches, scan skipped", path);
            return r;
        }
    }

    // 3. verify the function map against this ROM ------------------------
    r.funcs_total = (unsigned)recomp_plan::kFuncCount;
    f = std::fopen(rom.path, "rb");
    if (!f) {
        bootlog("prepare: cannot reopen %s", rom.path);
        return r;
    }

    const uint32_t sec_rom  = recomp_plan::kCodeSection.rom;
    const uint32_t sec_vram = recomp_plan::kCodeSection.vram;
    const uint32_t sec_size = recomp_plan::kCodeSection.size;

    for (unsigned i = 0; i < r.funcs_total; i++) {
        const auto& fn = recomp_plan::kFuncs[i];
        const uint32_t rel = fn.vram - sec_vram;
        bool ok = fn.vram >= sec_vram && rel + fn.size <= sec_size &&
                  fn.size >= 4 && (long)(sec_rom + rel + 4) <= size;
        uint32_t word = 0;
        if (ok) {
            ok = std::fseek(f, (long)(sec_rom + rel), SEEK_SET) == 0 &&
                 std::fread(&word, 1, 4, f) == 4 && plausible_instruction(word);
            // The ROM is big-endian; the first instruction as printed by the CPU is the
            // byte-swapped value. Accepted either way (both orders are checked for
            // non-blankness only).
        }
        if (ok) {
            r.funcs_ok++;
        } else {
            if (!r.funcs_bad) {
                r.first_bad_index = i;
                r.first_bad_vram = fn.vram;
                r.first_bad_word = word;
            }
            r.funcs_bad++;
        }
        if (progress && ((i & 31u) == 31u || i + 1 == r.funcs_total))
            progress(i + 1, r.funcs_total, user);
    }
    std::fclose(f);

    bootlog("prepare: %u/%u function entries verified (%u bad)",
            r.funcs_ok, r.funcs_total, r.funcs_bad);

    // 4. report ----------------------------------------------------------
    if (r.crc_ok) {
        r.report_written = write_report(r, rom);
        if (r.report_written) {
            std::snprintf(r.report_path, sizeof(r.report_path), "%s", kReportPaths[0]);
            bootlog("prepare: report written to %s", r.report_path);
        } else {
            bootlog("prepare: could not write the report (SD card read-only?)");
        }
    }
    r.rom_ok = r.crc_ok && r.funcs_bad == 0;
    return r;
}

} // namespace r3ds
