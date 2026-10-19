// =============================================================================
// poly_selftest — host-side verification of the polygon comparison engine
//
// The real poly_ref.cpp is compiled unmodified (with POLY_HOST_TEST, which
// swaps the two VRAM framebuffers for plain arrays). The recorded bitstream is
// read from disk exactly as the ROM embeds it.
//
//   ./poly_selftest meta     print "id colormode multispan" for each scene
//   ./poly_selftest verify   run the checks against make_reference.py output
//
// Checks
//   1. Decoder parity: with an all-void framebuffer every expected pixel must
//      be reported missing, and the per-scene counts must equal the counts from
//      the independent Python decoder in make_reference.py.
//   2. Exact match: loading the Python-decoded reference frame must pass every
//      scene with zero missing / overdraw / wrong-colour pixels.
//   3. Overdraw detection: one extra lit pixel must be reported at that exact
//      coordinate with kind=overdraw.
//   4. Missing / wrong-colour detection: removing a lit pixel must give
//      kind=missing; recolouring one in a colour-compared scene must give
//      kind=wrong colour.
//   5. Seek: rewinding to an arbitrary scene must produce identical counts.
// =============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

#include "../../Src/src9/poly/poly_compat.h"
#include "../../Src/src9/poly/poly_model.h"
#include "../../Src/src9/poly/poly_info.h"
#include "../../Src/src9/poly/poly_results.h"

// Framebuffers and stream pointers substituted by POLY_HOST_TEST.
u16 poly_host_fb_a[256 * 192];
u16 poly_host_fb_b[256 * 192];
extern "C" const u8 *poly_host_data = 0;
extern "C" unsigned poly_host_size = 0;

#include "../../Src/src9/poly/poly_ref.cpp"

// Host stand-in for the hardware framebuffer counter (poly_gfx.cpp is on-target
// only because it pokes 3D registers).
u32 poly_count_actual()
{
    u32 n = 0;
    for (u32 i = 0; i < 256u * 192u; i++)
        if ((POLY_FB_A[i] & 0x7FFF) != 0)
            n++;
    return n;
}

static int g_failures = 0;

#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("FAIL: ");                                              \
            std::printf(__VA_ARGS__);                                           \
            std::printf("\n");                                                  \
            g_failures++;                                                       \
        }                                                                       \
    } while (0)

static bool scene_multispan(int i)
{
    return (!(Tests[i].PolyAttr & Opaque)) || (Tests[i].ExtendedTestData > 0);
}

static int g_scenes;
static std::vector<u8> g_data;

static bool load_file(const char *path, std::vector<u8> &out)
{
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

static void clear_fb()
{
    for (int i = 0; i < 256 * 192; i++)
    {
        poly_host_fb_a[i] = 0;
        poly_host_fb_b[i] = 0;
    }
}

static void load_frame(const std::vector<u16> &frames, int scene)
{
    const u16 *src = &frames[(size_t)scene * 256 * 192];
    for (int i = 0; i < 256 * 192; i++)
        poly_host_fb_a[i] = (src[i] == 0xFFFF) ? 0 : src[i];
}

int main(int argc, char **argv)
{
    g_scenes = (int)(sizeof(Tests) / sizeof(Tests[0]));

    if (argc >= 2 && std::strcmp(argv[1], "meta") == 0)
    {
        for (int i = 0; i < g_scenes; i++)
            std::printf("%d %d %d\n", i, Tests[i].ColorMode, scene_multispan(i) ? 1 : 0);
        return 0;
    }

    const char *dir = (argc >= 3) ? argv[2] : ".";
    std::string data_path = std::string(dir) + "/../../Src/src9/poly/rastertest.data";
    std::string frames_path = std::string(dir) + "/ref_frames.bin";
    std::string counts_path = std::string(dir) + "/ref_counts.txt";
    std::string meta_path = std::string(dir) + "/scene_meta.txt";

    {
        std::vector<u8> raw;
        if (!load_file(data_path.c_str(), raw))
        {
            std::printf("FAIL: cannot read %s\n", data_path.c_str());
            return 1;
        }
        g_data = raw;
    }
    poly_host_data = &g_data[0];
    poly_host_size = (unsigned)g_data.size();

    std::vector<u16> frames;
    {
        std::vector<u8> raw;
        if (!load_file(frames_path.c_str(), raw) ||
            raw.size() != (size_t)g_scenes * 256 * 192 * 2)
        {
            std::printf("FAIL: %s missing or wrong size (run make_reference.py)\n",
                        frames_path.c_str());
            return 1;
        }
        frames.resize(raw.size() / 2);
        std::memcpy(frames.data(), raw.data(), raw.size());
    }

    std::vector<int> ref_counts(g_scenes, -1);
    FILE *cf = std::fopen(counts_path.c_str(), "r");
    if (!cf)
    {
        std::printf("FAIL: cannot read %s\n", counts_path.c_str());
        return 1;
    }
    char line[128];
    int data_version = -1;
    while (std::fgets(line, sizeof(line), cf))
    {
        if (std::strncmp(line, "data_version", 12) == 0)
        {
            std::sscanf(line + 12, "%d", &data_version);
            continue;
        }
        int id, n;
        if (std::sscanf(line, "%d %d", &id, &n) == 2 && id >= 0 && id < g_scenes)
            ref_counts[id] = n;
    }
    std::fclose(cf);

    CHECK(data_version == 2, "expected recorded data version 2, got %d", data_version);

    // ---- 1. decoder parity against the independent Python decoder ----------
    long total_expected = 0;
    clear_fb();
    CHECK(poly_ref_begin(), "embedded stream rejected (version mismatch)");
    for (int i = 0; i < g_scenes; i++)
    {
        PolyStats st;
        poly_check(i, scene_multispan(i), Tests[i].ColorMode, false, &st);
        total_expected += st.expected_px;
        CHECK(st.expected_px == (u32)ref_counts[i],
              "scene %d expected %u px, python decoder says %d",
              i, st.expected_px, ref_counts[i]);
        CHECK(st.missing == st.expected_px,
              "scene %d blank framebuffer: missing %u != expected %u",
              i, st.missing, st.expected_px);
        CHECK(st.overdraw == 0, "scene %d blank framebuffer reported %u overdraw", i, st.overdraw);
    }
    CHECK(total_expected == 102942, "total expected pixels %ld, expected 102942", total_expected);

    // ---- 1b. expected-count pre-pass (used by the ROM for retry logic) -----
    {
        std::vector<u32> pre(g_scenes, 0);
        CHECK(poly_ref_expected_counts(pre.data(), g_scenes), "expected-count pre-pass failed");
        for (int i = 0; i < g_scenes; i++)
            CHECK(pre[i] == (u32)ref_counts[i],
                  "scene %d pre-pass count %u != %d", i, pre[i], ref_counts[i]);
    }

    // ---- 2. exact match ----------------------------------------------------
    CHECK(poly_ref_begin(), "restart failed");
    for (int i = 0; i < g_scenes; i++)
    {
        load_frame(frames, i);
        PolyStats st;
        bool ok = poly_check(i, scene_multispan(i), Tests[i].ColorMode, false, &st);
        CHECK(ok, "scene %d reference frame did not match itself", i);
        CHECK(st.missing == 0 && st.overdraw == 0 && st.wrongcolor == 0,
              "scene %d self-match: m=%u o=%u w=%u", i, st.missing, st.overdraw, st.wrongcolor);
        CHECK(st.actual_px == st.expected_px, "scene %d pixel count %u != %u",
              i, st.actual_px, st.expected_px);
    }

    // ---- 3/4. perturbation detection --------------------------------------
    // The stream is strictly sequential, so each probe rewinds to its scene.
    for (int i = 0; i < g_scenes; i++)
    {
        const u16 *src = &frames[(size_t)i * 256 * 192];

        // First lit pixel of the scene, so the checks are meaningful.
        int first_lit = -1;
        for (int p = 0; p < 256 * 192 && first_lit < 0; p++)
            if (src[p] != 0xFFFF) first_lit = p;

        // Extra lit pixel somewhere that must stay void.
        load_frame(frames, i);
        int extra = -1;
        for (int p = 0; p < 256 * 192 && extra < 0; p++)
            if (src[p] == 0xFFFF) extra = p;
        poly_host_fb_a[extra] = 0x7FFF;
        PolyStats st;
        poly_ref_seek(i);
        poly_check(i, scene_multispan(i), Tests[i].ColorMode, false, &st);
        CHECK(st.overdraw == 1, "scene %d overdraw probe: overdraw=%u", i, st.overdraw);
        CHECK(st.first_kind == POLY_MISMATCH_OVERDRAW && st.valid,
              "scene %d overdraw probe kind %u", i, st.first_kind);
        CHECK((int)st.first_x + (int)st.first_y * 256 == extra,
              "scene %d overdraw probe at (%u,%u), expected (%d,%d)", i,
              st.first_x, st.first_y, extra % 256, extra / 256);

        if (first_lit >= 0)
        {
            // Remove a lit pixel -> missing.
            load_frame(frames, i);
            poly_host_fb_a[first_lit] = 0;
            poly_ref_seek(i);
            poly_check(i, scene_multispan(i), Tests[i].ColorMode, false, &st);
            CHECK(st.missing == 1 && st.first_kind == POLY_MISMATCH_MISSING,
                  "scene %d missing probe: missing=%u kind=%u", i, st.missing, st.first_kind);

            // Recolour a lit pixel in colour-compared scenes -> wrong colour.
            if (Tests[i].ColorMode != 0)
            {
                load_frame(frames, i);
                poly_host_fb_a[first_lit] ^= 0x0001; // flip the lowest compared bit
                poly_ref_seek(i);
                poly_check(i, scene_multispan(i), Tests[i].ColorMode, false, &st);
                CHECK(st.wrongcolor == 1 && st.first_kind == POLY_MISMATCH_WRONGCOLOR,
                      "scene %d colour probe: wrong=%u kind=%u", i, st.wrongcolor, st.first_kind);
            }
        }
        else
        {
            // Colour-mode scenes always have pixels; a fully empty scene is fine.
        }
    }

    // ---- 5. seek -----------------------------------------------------------
    for (int i = 0; i < g_scenes; i += 7)
    {
        load_frame(frames, i);
        CHECK(poly_ref_seek(i), "seek to %d failed", i);
        PolyStats st;
        bool ok = poly_check(i, scene_multispan(i), Tests[i].ColorMode, false, &st);
        CHECK(ok && st.expected_px == (u32)ref_counts[i],
              "scene %d after seek: ok=%d expected=%u (want %d)", i, ok, st.expected_px,
              ref_counts[i]);
    }

    if (g_failures)
    {
        std::printf("SELFTEST FAILED: %d problem(s)\n", g_failures);
        return 1;
    }
    std::printf("SELFTEST PASSED: %d scenes, %ld expected pixels, decoder+comparator verified\n",
                g_scenes, total_expected);
    return 0;
}
