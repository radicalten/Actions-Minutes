#pragma once
#include "poly_compat.h"

struct PolyStats
{
    u32 expected_px;
    u32 actual_px;
    u32 missing;
    u32 overdraw;
    u32 wrongcolor;
    u16 first_x;
    u16 first_y;
    u8 first_kind;
    u8 valid;
};

// Rewind to the first scene and validate the embedded data version.
bool poly_ref_begin();

// Compare one scene against the recorded spans. The stream is sequential, so
// tests must be checked in order (or seeked with poly_ref_seek first).
// colorize=true paints VRAM A with polyrastertest's error colours.
bool poly_check(int index, bool multispan, u8 colormode, bool colorize, PolyStats *out);

// Restart and skip to a given scene (used to re-display an earlier failure).
bool poly_ref_seek(int index);

// Decode the whole stream once and report the expected pixel count of each
// scene. Consumes the stream, so call poly_ref_begin() again afterwards.
bool poly_ref_expected_counts(u32 *counts, int n);
