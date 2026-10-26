// Host test for source/prepare.cpp: build a synthetic 32 MiB ROM with the header the
// recompiler expects, run the plan pass, print the report. No 3DS involved.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <utility>

#include "r3ds.hpp"

// --- stubs for the pieces prepare.cpp expects from the platform layer ---
static std::string g_rom_path;
namespace r3ds {
RomInfo rom_check() {
    RomInfo r;
    r.found = true; r.valid = true;
    r.path = g_rom_path.c_str();
    r.size = 32u * 1024u * 1024u;
    std::snprintf(r.detail, sizeof(r.detail), "synthetic");
    return r;
}
void bootlog(const char*, ...) {}
} // namespace r3ds

#include "../../source/prepare.cpp"

static void be32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    v[off] = x >> 24; v[off + 1] = x >> 16; v[off + 2] = x >> 8; v[off + 3] = x;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "/tmp/prepare-test.z64";
    std::vector<uint8_t> rom(32u * 1024u * 1024u, 0);
    // N64 header: magic, name at 0x20, CRC1/CRC2 at 0x10/0x14.
    const uint32_t magic = 0x80371240;
    be32(rom, 0, magic);
    std::memcpy(&rom[0x20], "STAR WARS EP1 RACER", 19);
    be32(rom, 0x10, recomp_plan::kExpectedCrc1);
    be32(rom, 0x14, recomp_plan::kExpectedCrc2);
    // Fill the code section with plausible big-endian MIPS words.
    for (uint32_t off = recomp_plan::kCodeSection.rom;
         off < recomp_plan::kCodeSection.rom + recomp_plan::kCodeSection.size; off += 4)
        be32(rom, off, 0x27BDFFE0u); // addiu sp, sp, -0x20
    FILE* f = std::fopen(path, "wb");
    std::fwrite(rom.data(), 1, rom.size(), f);
    std::fclose(f);

    g_rom_path = path;
    r3ds::PrepareResult r = r3ds::prepare_run(nullptr, nullptr);
    std::printf("ran=%d rom_ok=%d crc_ok=%d funcs %u/%u (bad %u) report=%d %s\n",
                (int)r.ran, (int)r.rom_ok, (int)r.crc_ok, r.funcs_ok, r.funcs_total,
                r.funcs_bad, (int)r.report_written, r.report_path);

    // v64 case: the same ROM 16-bit byte-swapped, as a bad rip would be. The pass must
    // convert it to baserom.z64 and then validate it.
    {
        std::vector<uint8_t> v64 = rom;
        for (size_t i = 0; i + 1 < v64.size(); i += 2) std::swap(v64[i], v64[i + 1]);
        std::string v64_path = std::string(path) + ".v64";
        FILE* vf = std::fopen(v64_path.c_str(), "wb");
        std::fwrite(v64.data(), 1, v64.size(), vf);
        std::fclose(vf);
        g_rom_path = v64_path;
        std::remove("baserom.z64");
        r3ds::PrepareResult c = r3ds::prepare_run(nullptr, nullptr);
        std::printf("v64: converted=%d canonical=\"%s\" crc_ok=%d funcs %u/%u\n",
                    (int)c.converted, c.canonical_path, (int)c.crc_ok, c.funcs_ok, c.funcs_total);
        bool same = false;
        FILE* a = std::fopen(path, "rb");
        FILE* b = std::fopen("baserom.z64", "rb");
        if (a && b) {
            int ca, cb; same = true;
            do { ca = std::fgetc(a); cb = std::fgetc(b); if (ca != cb) { same = false; break; } }
            while (ca != EOF);
        }
        if (a) std::fclose(a);
        if (b) std::fclose(b);
        std::printf("v64: converted file is byte-identical to the z64 original: %s\n",
                    same ? "yes" : "NO");
        if (!c.converted || !same || c.funcs_bad) return 2;
        g_rom_path = path;
    }

    // Negative case: wrong CRC and a hole of zeroes in the middle of the code section.
    be32(rom, 0x10, 0xDEADBEEF);
    for (uint32_t off = 0x40000; off < 0x40200; off += 4) be32(rom, off, 0);
    f = std::fopen(path, "wb");
    std::fwrite(rom.data(), 1, rom.size(), f);
    std::fclose(f);
    r3ds::PrepareResult bad = r3ds::prepare_run(nullptr, nullptr);
    std::printf("bad: crc_ok=%d funcs %u/%u (bad %u) first bad vram=0x%08X word=0x%08X\n",
                (int)bad.crc_ok, bad.funcs_ok, bad.funcs_total, bad.funcs_bad,
                bad.first_bad_vram, bad.first_bad_word);
    return (r.rom_ok && bad.funcs_bad > 0 && !bad.crc_ok && !bad.report_written) ? 0 : 1;
}
