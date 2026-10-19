// =============================================================================
// poly_run.cpp — automated polygon rasterization group
//
// Runs every scene in poly_model.h in order, captures the real 3D output into
// VRAM A, compares it against the hardware-recorded spans in rastertest.data,
// and publishes a machine-readable report at POLY_RESULT_BASE (see
// poly_results.h) plus a compact summary through the suite's own box protocol.
//
// It is deliberately non-fatal: a failing scene is recorded and the run
// continues, so one defective fill rule does not hide the rest of the report.
// The group as a whole is reported to the suite as failed (non-zero return)
// when at least one scene differs.
// =============================================================================

#include "../inc.h"
#include "../framework/bottom.h"

#include "poly_compat.h"
#include "poly_model.h"
#include "poly_info.h"
#include "poly_results.h"
#include "poly_gfx.h"
#include "poly_ref.h"

extern "C" u32 poly_result_checksum(); // defined below, exported for hosts

static vu32 *const poly = (vu32 *)POLY_RESULT_BASE;
static vu32 *const poly_rec = (vu32 *)(POLY_RESULT_BASE + POLY_RESULT_HEADER_W * 4u);
static vu32 *const suite_box = (vu32 *)0x023F0000;

static const int kPolyTests = (int)(sizeof(Tests) / sizeof(Tests[0]));
static const char kVersionString[] = "1.0.2-b";

// -----------------------------------------------------------------------------
// small helpers (no libc in this ROM)
// -----------------------------------------------------------------------------

static int category_index(const char *name)
{
    for (unsigned i = 0; i < sizeof(poly_category_names) / sizeof(poly_category_names[0]); i++)
    {
        const char *a = name;
        const char *b = poly_category_names[i];
        while (*a && *a == *b)
        {
            a++;
            b++;
        }
        if (*a == *b)
            return (int)i;
    }
    return 0;
}

static bool scene_multispan(int i)
{
    return (!(Tests[i].PolyAttr & Opaque)) || (Tests[i].ExtendedTestData > 0);
}

static void publish(u32 state, u32 pass, u32 fail, u32 error, u32 first_fail,
                    u32 last_test, u32 flags)
{
    poly[PH_PASS] = pass;
    poly[PH_FAIL] = fail;
    poly[PH_ERROR] = error;
    poly[PH_FIRST_FAIL] = first_fail;
    poly[PH_LAST_TEST] = last_test;
    poly[PH_FLAGS] = flags;
    poly[PH_STATE] = state; // commit last
}

static void write_record(int i, u32 state, const PolyStats &st)
{
    vu32 *r = poly_rec + i * POLY_RESULT_RECORD_W;
    u32 flags = (u32)Tests[i].ColorMode | ((scene_multispan(i) ? 1u : 0u) << 3);
    u32 mismatch = 0xFFFFFFFFu;
    if (st.valid)
        mismatch = (u32)st.first_x | ((u32)st.first_y << 8) | ((u32)st.first_kind << 16);

    r[PR_STATE] = state;
    r[PR_EXPECTED_PX] = st.expected_px;
    r[PR_ACTUAL_PX] = st.actual_px;
    r[PR_MISSING_PX] = st.missing;
    r[PR_OVERDRAW_PX] = st.overdraw;
    r[PR_WRONGCOLOR_PX] = st.wrongcolor;
    r[PR_FIRST_MISMATCH] = mismatch;
    r[PR_CATEGORY] = (u32)category_index(poly_info[i].category);
    r[PR_FLAGS] = flags;
    r[PR_RESERVED0] = 0;
    r[PR_RESERVED1] = 0;
    r[PR_ID] = (u32)i;
}

// -----------------------------------------------------------------------------
// top-screen human summary (uses the suite's 4-shade text console in bank A)
// -----------------------------------------------------------------------------

static void decimal(int x, int y, unsigned value)
{
    char buf[12];
    unsigned at = 11;
    buf[at--] = 0;
    do
    {
        buf[at--] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    draw_string(x, y, &buf[at + 1]);
}

static void show_summary_screen(unsigned pass, unsigned fail, unsigned error)
{
    clear_screen();
    draw_string(0, 0, "POLY RASTERIZATION SUITE");
    draw_string(0, 2, "POLYRASTERTEST");
    draw_string(15, 2, kVersionString);
    draw_string(0, 3, "DATA VERSION");
    draw_string(13, 3, "2");

    draw_string(0, 5, "SCENES PASSED");
    decimal(14, 5, pass);
    draw_string(0, 6, "SCENES FAILED");
    decimal(14, 6, fail);
    draw_string(0, 7, "SCENES ERROR");
    decimal(14, 7, error);
    draw_string(0, 9, "RESULT MAP BELOW");
    draw_string(0, 10, "LEFT/RIGHT: POLY PAGES");
    draw_string(0, 12, "PASS = FRAME MATCHES");
    draw_string(0, 13, "HARDWARE RECORDING");
    poly_show_capture(); // display the text console in VRAM bank A
}

// -----------------------------------------------------------------------------
// group entry point
// -----------------------------------------------------------------------------

extern "C" unsigned run_poly_group()
{
    // Start with a clean, known display state: the framework reset the IO state
    // before calling us. Set up the secondary LCD text console first because
    // bottom_setup() rewrites POWCNT1, then power the 3D engine.
    bottom_setup();
    poly_gfx_init();

    for (u32 i = 0; i < POLY_RESULT_HEADER_W; i++)
        poly[i] = 0;
    for (u32 i = 0; i < (u32)kPolyTests * POLY_RESULT_RECORD_W; i++)
        poly_rec[i] = 0;

    poly[PH_MAGIC] = POLY_RESULT_MAGIC;
    poly[PH_VERSION] = POLY_RESULT_VERSION;
    poly[PH_TEST_COUNT] = (u32)kPolyTests;
    poly[PH_DATA_VERSION] = 2;
    poly[PH_SOURCE_TAG] = POLY_SOURCE_TAG;
    poly[PH_STATE] = 1;

    bool ref_ok = poly_ref_begin();
    if (!ref_ok)
    {
        // The embedded stream is missing or has an unexpected version.
        for (int i = 0; i < kPolyTests; i++)
        {
            PolyStats empty = {0, 0, 0, 0, 0, 0, 0, 0, 0};
            write_record(i, POLY_STATE_ERROR, empty);
        }
        publish(3, 0, 0, (u32)kPolyTests, 0xFFFFFFFFu, (u32)kPolyTests, 0);
        suite_box[18] = POLY_RESULT_BASE;
        suite_box[19] = 0;
        suite_box[20] = 0;
        suite_box[21] = (u32)kPolyTests;
        suite_box[22] = 0xFFFFFFFFu;
        suite_box[23] = 3;
        suite_box[24] = (u32)kPolyTests;
        suite_box[25] = 2;
        bottom_render(12);
        show_summary_screen(0, 0, (unsigned)kPolyTests);
        return 0xFFFFFF02u;
    }

    // Expected pixel count per scene, decoded once up front. Used to tell a
    // genuinely empty scene from a failed capture without guessing.
    static u32 expected_px[sizeof(Tests) / sizeof(Tests[0])];
    poly_ref_expected_counts(expected_px, kPolyTests);
    poly_ref_begin();

    unsigned pass = 0, fail = 0, error = 0;
    unsigned first_fail = 0xFFFFFFFFu;
    u32 flags = 0;
    unsigned capture_failures = 0;
    u32 sum_expected = 0, sum_missing = 0, sum_overdraw = 0, sum_wrong = 0;

    for (int i = 0; i < kPolyTests; i++)
    {
        PolyStats st = {0, 0, 0, 0, 0, 0, 0, 0, 0};

        // Upstream draws each scene twice after a VBlank and captures the next
        // frame. Keep that frame alignment; retry once if the capture came back
        // completely empty while the recording expects pixels (start-up frame
        // or a skipped frame).
        bool captured = true;
        for (int attempt = 0; attempt < 2; attempt++)
        {
            wait_vblank();
            captured = poly_draw_test(i);
            if (captured)
                captured = poly_draw_test(i);
            if (captured)
                captured = poly_capture_frame();
            if (!captured)
                break;
            if (poly_count_actual() != 0 || expected_px[i] == 0)
                break;
        }

        if (!captured)
        {
            capture_failures++;
            st.valid = 0;
            poly[PH_LAST_TEST] = (u32)i;
            write_record(i, POLY_STATE_ERROR, st);
            // Advance the sequential reference stream past this scene.
            poly_check(i, scene_multispan(i), Tests[i].ColorMode, false, 0);
            error++;
            publish(1, pass, fail, error, first_fail, (u32)i, flags);
            if (i == 2 && capture_failures == 3)
            {
                // 3D engine unavailable: do not spend minutes timing out.
                flags |= 1;
                for (int j = i + 1; j < kPolyTests; j++)
                {
                    PolyStats empty = {0, 0, 0, 0, 0, 0, 0, 0, 0};
                    poly[PH_LAST_TEST] = (u32)j;
                    write_record(j, POLY_STATE_ERROR, empty);
                    error++;
                }
                publish(3, pass, fail, error, first_fail, (u32)(kPolyTests - 1), flags);
                goto finish_aborted;
            }
            continue;
        }

        bool capture_empty = (poly_count_actual() == 0) && (expected_px[i] != 0);
        bool ok = poly_check(i, scene_multispan(i), Tests[i].ColorMode, false, &st);
        if (capture_empty)
        {
            // The recorder produced pixels but the capture is blank: this is a
            // capture problem, not a rasterization difference. Keep the decoded
            // pixel counts but report the scene as an error.
            ok = false;
            error++;
            write_record(i, POLY_STATE_ERROR, st);
            publish(1, pass, fail, error, first_fail, (u32)i, flags);
            bottom_render(12);
            continue;
        }
        sum_expected += st.expected_px;
        sum_missing += st.missing;
        sum_overdraw += st.overdraw;
        sum_wrong += st.wrongcolor;
        write_record(i, ok ? POLY_STATE_PASS : POLY_STATE_FAIL, st);
        if (ok)
            pass++;
        else
        {
            if (first_fail == 0xFFFFFFFFu)
                first_fail = (u32)i;
            fail++;
        }

        publish(1, pass, fail, error, first_fail, (u32)i, flags);

        // Live progress on the secondary LCD, final per-scene table at the end.
        bottom_render(12);
    }

    poly[PH_EXPECTED_PX] = sum_expected;
    poly[PH_MISSING_PX] = sum_missing;
    poly[PH_OVERDRAW_PX] = sum_overdraw;
    poly[PH_WRONGCOLOR_PX] = sum_wrong;
    {
        u32 checksum = 0;
        for (int i = 0; i < kPolyTests * (int)POLY_RESULT_RECORD_W; i++)
            checksum = (checksum * 16777619u) ^ poly_rec[i];
        poly[PH_FLAGS] = flags;
        poly[PH_CHECKSUM] = checksum;
    }
    publish(2, pass, fail, error, first_fail, (u32)(kPolyTests - 1), flags);

    // Top-screen text summary; numbers/counters live in the result block and
    // on the secondary LCD pages 13-14.
    show_summary_screen(pass, fail, error);

    // Publish the poly summary in the suite's box extension so a single host
    // poll can read CPU groups and polygon results.
    suite_box[18] = POLY_RESULT_BASE;
    suite_box[19] = pass;
    suite_box[20] = fail;
    suite_box[21] = error;
    suite_box[22] = first_fail;
    suite_box[23] = 2;
    suite_box[24] = (u32)kPolyTests;
    suite_box[25] = 2;

    bottom_render(12);

    // Report the group itself through the suite protocol. fail_test() unwinds
    // back to the menu, which marks group 27 as a nonpass and records the code
    // below; the polygon result block stays intact for host readers.
    if (fail || error)
        fail_test(0x50000000u | ((first_fail != 0xFFFFFFFFu ? first_fail : (unsigned)kPolyTests) & 0xFFu));
    return 0;

finish_aborted:
    suite_box[18] = POLY_RESULT_BASE;
    suite_box[19] = pass;
    suite_box[20] = fail;
    suite_box[21] = error;
    suite_box[22] = first_fail;
    suite_box[23] = 3;
    suite_box[24] = (u32)kPolyTests;
    suite_box[25] = 2;
    bottom_render(12);
    show_summary_screen(pass, fail, error);
    return 0xFFFFFF01u;
}

extern "C" u32 poly_result_checksum()
{
    u32 checksum = 0;
    for (int i = 0; i < kPolyTests * (int)POLY_RESULT_RECORD_W; i++)
        checksum = (checksum * 16777619u) ^ poly_rec[i];
    return checksum;
}

