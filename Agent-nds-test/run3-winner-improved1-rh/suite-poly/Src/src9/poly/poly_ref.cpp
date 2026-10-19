// =============================================================================
// poly_ref.cpp — recorded-span reader and framebuffer comparison
//
// This is a direct port of polyrastertest's readData()/test() bit reader. The
// embedded rastertest.data bitstream is read most-significant-bit first through
// the same sliding 32-bit window, so the decode order is bit-exact with
// upstream. Format (DataVersion 2):
//
//   16 bits  data version
//   per scene, per scanline:
//     1 bit   span present
//     8 bits  span start        ) repeated for every span;
//     8 bits  span end          ) a 1 bit means "another span follows"
//     n*5 bit colours           (n = colour mode 1/2/3; omitted when 0)
//
// A scanline with no span means "every pixel must stay void (black)".
// =============================================================================

#include "poly_compat.h"
#include "poly_model.h"
#include "poly_results.h"
#include "poly_gfx.h"
#include "poly_ref.h"

#ifdef POLY_HOST_TEST
// Host self-test: the stream is loaded from disk.
extern "C" const u8 *poly_host_data;
extern "C" unsigned poly_host_size;
#define POLY_DATA_START poly_host_data
#define POLY_DATA_SIZE  poly_host_size
#else
// Embedded by poly_data.s.
extern "C" const u8 poly_data_start[];
extern "C" const u8 poly_data_end[];
#define POLY_DATA_START poly_data_start
#define POLY_DATA_SIZE  ((u32)(poly_data_end - poly_data_start))
#endif

static u32 ref_size;
static u32 ref_ptr;
static u32 ref_buf;
static u8 ref_shift;

static u32 read_data(u32 buffer, u8 bytes)
{
    for (u8 i = 0; i < bytes; i++)
    {
        if (ref_ptr >= ref_size)
        {
            buffer <<= 8;
            continue;
        }
        buffer <<= 8;
        buffer |= POLY_DATA_START[ref_ptr++];
    }
    return buffer;
}

static u8 get_span_point()
{
    u8 r = (u8)((ref_buf >> (24 - ref_shift)) & 0xFF);
    ref_shift += 8;
    return r;
}

static u16 get_color(u8 bits, u16 mask)
{
    u16 r = (u16)((ref_buf >> ((32 - bits) - ref_shift)) & mask);
    ref_shift += bits;
    return r;
}

static void refill_at_span_boundary()
{
    while (17u + ref_shift > 32u)
    {
        ref_buf = read_data(ref_buf, 1);
        ref_shift -= 8;
    }
}

bool poly_ref_begin()
{
    ref_size = POLY_DATA_SIZE;
    ref_ptr = 0;
    ref_shift = 0;
    ref_buf = 0;

    u32 version = read_data(0, 2);
    if (version != 2)
        return false;

    ref_buf = read_data(0, 4);
    return true;
}

// -----------------------------------------------------------------------------
// one scene
// -----------------------------------------------------------------------------

static void compare_pixel(int x, int y, bool in_span, u16 expected, bool colormode,
                          u16 mask, bool compare, bool colorize, PolyStats *st)
{
    u32 offset = (u32)y * POLY_SCREEN_W + x;
    u16 actual = POLY_FB_A[offset] & 0x7FFF;

    if (!compare)
    {
        if (colorize)
            POLY_FB_B[offset] = in_span ? expected : ColorVoid;
        return;
    }

    if (!in_span)
    {
        if (colorize)
            POLY_FB_B[offset] = ColorVoid;

        if (actual != ColorVoid)
        {
            st->overdraw++;
            if (!st->valid)
            {
                st->valid = 1;
                st->first_x = (u16)x;
                st->first_y = (u16)y;
                st->first_kind = POLY_MISMATCH_OVERDRAW;
            }
            if (colorize)
                POLY_FB_A[offset] = ColorOverdraw;
        }
        else if (colorize)
            POLY_FB_A[offset] = ColorVoid;
        return;
    }

    if (colorize)
        POLY_FB_B[offset] = expected;

    if (actual == ColorVoid)
    {
        st->missing++;
        if (!st->valid)
        {
            st->valid = 1;
            st->first_x = (u16)x;
            st->first_y = (u16)y;
            st->first_kind = POLY_MISMATCH_MISSING;
        }
        if (colorize)
            POLY_FB_A[offset] = ColorMissing;
    }
    else if (colormode && (actual & mask) != expected)
    {
        st->wrongcolor++;
        if (!st->valid)
        {
            st->valid = 1;
            st->first_x = (u16)x;
            st->first_y = (u16)y;
            st->first_kind = POLY_MISMATCH_WRONGCOLOR;
        }
        if (colorize)
            POLY_FB_A[offset] = ColorWrongTex;
    }
    else if (colorize)
        POLY_FB_A[offset] = ColorMatch;
}

static void scan_scene(bool multispan, u8 colormode, bool compare, bool colorize, PolyStats *st)
{
    u16 mask;
    u8 bits;
    switch (colormode)
    {
    case 1: mask = 0x1F;   bits = 5;  break;
    case 2: mask = 0x3FF;  bits = 10; break;
    case 3: mask = 0x7FFF; bits = 15; break;
    default: mask = 0; bits = 0; break;
    }

    for (int y = 0; y < POLY_SCREEN_H; y++)
    {
        while (ref_shift >= 8)
        {
            ref_buf = read_data(ref_buf, 1);
            ref_shift -= 8;
        }

        if (ref_buf & (1u << (31 - ref_shift)))
        {
            ref_shift++;
            u8 startspan = get_span_point();
            u8 endspan = get_span_point();
            bool done = false;

            for (int x = 0;; x++)
            {
                if (multispan && x > endspan && !done)
                {
                    refill_at_span_boundary();
                    if (ref_buf & (1u << (31 - ref_shift)))
                    {
                        ref_shift++;
                        startspan = get_span_point();
                        endspan = get_span_point();
                    }
                    else
                    {
                        ref_shift++;
                        done = true;
                    }
                }

                if (x >= POLY_SCREEN_W)
                    break;

                bool in_span = (x >= startspan && x <= endspan);
                u16 expected = ColorVoid;

                if (in_span)
                    st->expected_px++;

                if (in_span)
                {
                    if (colormode != 0)
                    {
                        while (bits + ref_shift > 32)
                        {
                            ref_buf = read_data(ref_buf, 1);
                            ref_shift -= 8;
                        }
                        expected = get_color(bits, mask);
                    }
                    else
                        expected = ColorStock;
                }

                compare_pixel(x, y, in_span, expected, colormode != 0, mask,
                              compare, colorize, st);
            }
        }
        else
        {
            ref_shift++;
            for (int x = 0; x < POLY_SCREEN_W; x++)
            {
                compare_pixel(x, y, false, ColorVoid, false, 0, compare, colorize, st);
            }
        }
    }
}

bool poly_check(int index, bool multispan, u8 colormode, bool colorize, PolyStats *out)
{
    (void)index;

    PolyStats st;
    st.expected_px = 0;
    st.actual_px = 0;
    st.missing = 0;
    st.overdraw = 0;
    st.wrongcolor = 0;
    st.first_x = 0;
    st.first_y = 0;
    st.first_kind = 0;
    st.valid = 0;

    // Count non-void pixels exactly once, before any colourisation.
    st.actual_px = poly_count_actual();

    scan_scene(multispan, colormode, true, colorize, &st);

    if (out)
        *out = st;

    // A scene fails if any pixel differs from the recording.
    return st.missing == 0 && st.overdraw == 0 && st.wrongcolor == 0;
}

bool poly_ref_expected_counts(u32 *counts, int n)
{
    if (!poly_ref_begin())
        return false;

    PolyStats st;
    for (int i = 0; i < n; i++)
    {
        st.expected_px = 0;
        st.actual_px = 0;
        st.missing = 0;
        st.overdraw = 0;
        st.wrongcolor = 0;
        st.first_x = 0;
        st.first_y = 0;
        st.first_kind = 0;
        st.valid = 0;

        // Decode-only pass: the framebuffer is irrelevant here.
        scan_scene((!(Tests[i].PolyAttr & Opaque)) || Tests[i].ExtendedTestData > 0,
                   Tests[i].ColorMode, false, false, &st);
        counts[i] = st.expected_px;
    }
    return true;
}

bool poly_ref_seek(int index)
{
    if (!poly_ref_begin())
        return false;

    PolyStats dummy;
    dummy.expected_px = 0;
    dummy.actual_px = 0;
    dummy.missing = 0;
    dummy.overdraw = 0;
    dummy.wrongcolor = 0;
    dummy.first_x = 0;
    dummy.first_y = 0;
    dummy.first_kind = 0;
    dummy.valid = 0;

    for (int i = 0; i < index; i++)
    {
        bool multispan = (!(Tests[i].PolyAttr & Opaque)) || Tests[i].ExtendedTestData > 0;
        scan_scene(multispan, Tests[i].ColorMode, false, false, &dummy);
    }
    return true;
}
