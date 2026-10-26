// ROM discovery and validation on the SD card, mirroring the checks in the PC Makefile
// (`make verify`): USA release, 32 MiB, CRC1 0x72F70398 / CRC2 0x6556A98B.
#include <cstdio>
#include <cstring>

#include "r3ds.hpp"

namespace r3ds {
namespace {

constexpr uint32_t kExpectedCrc1 = 0x72F70398;
constexpr uint32_t kExpectedCrc2 = 0x6556A98B;
constexpr uint32_t kExpectedSize = 32u * 1024u * 1024u;

inline uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

} // namespace

RomInfo rom_check() {
    RomInfo info;

    // `sdmc:` is the default device on most CFW setups but not all, so try both spellings.
    static const char* candidates[] = {
        "/3ds/racer/baserom.z64",
        "sdmc:/3ds/racer/baserom.z64",
        "/racer/baserom.z64",
        "sdmc:/racer/baserom.z64",
        "/baserom.z64",
        "sdmc:/baserom.z64",
    };

    for (const char* path : candidates) {
        FILE* f = std::fopen(path, "rb");
        if (!f) continue;

        uint8_t header[0x40];
        const size_t got = std::fread(header, 1, sizeof(header), f);
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fclose(f);

        if (got != sizeof(header)) continue;

        info.found = true;
        info.path = path;
        info.size = (uint32_t)(size > 0 ? size : 0);
        info.crc1 = be32(header + 0x10);
        info.crc2 = be32(header + 0x14);
        info.valid = (info.size == kExpectedSize) &&
                     (info.crc1 == kExpectedCrc1) && (info.crc2 == kExpectedCrc2);
        if (info.valid) {
            std::snprintf(info.detail, sizeof(info.detail), "CRC1 0x%08X OK, 32 MiB", (unsigned)info.crc1);
        } else if (info.size != kExpectedSize) {
            std::snprintf(info.detail, sizeof(info.detail), "wrong size: %u MiB (need 32)",
                          (unsigned)(info.size / (1024u * 1024u)));
        } else {
            std::snprintf(info.detail, sizeof(info.detail), "CRC1 0x%08X != 0x%08X",
                          (unsigned)info.crc1, (unsigned)kExpectedCrc1);
        }
        return info;
    }

    std::snprintf(info.detail, sizeof(info.detail), "not found");
    return info;
}

} // namespace r3ds
