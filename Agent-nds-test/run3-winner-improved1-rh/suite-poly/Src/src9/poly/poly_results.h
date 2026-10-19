#pragma once
// =============================================================================
// poly_results.h — machine-readable polygon rasterization result block
//
// Fixed address in main RAM, outside every region used by the CPU trace
// protocol (TRACE9 0x02300000, TRACE7 0x0236c000, assertion journal
// 0x023d8000..0x023ec040, ARM7 mailbox 0x023ee000..0x023ef100, suite box
// 0x023f0000, UI mirror 0x023f0080, group records 0x023f0100..0x023f01d8).
//
// POLY_BASE + 0x000 : header, 20 words
// POLY_BASE + 0x050 : 77 records, 12 words each (0x050 .. 0xE9F)
//
// Header words
//   [0]  magic            0x504C5935  ('PLY5')
//   [1]  structure version         1
//   [2]  test count               77
//   [3]  reference data version    2   (upstream DataVersion)
//   [4]  upstream release tag      '102B' = 0x31303242
//   [5]  state        0=not run 1=running 2=complete 3=aborted
//   [6]  passed
//   [7]  failed
//   [8]  errored (no comparable result)
//   [9]  expected pixels (sum of recorded spans)
//   [10] missing pixels  (reference drawn, hardware did not draw)
//   [11] overdraw pixels (hardware drew outside reference)
//   [12] wrong-colour pixels
//   [13] first failing test id, 0xFFFFFFFF if none
//   [14] last test id that produced a result
//   [15] flags: bit0 = 3D engine unavailable, bit1 = partial run
//   [16] FNV-style checksum of all record words
//   [17..19] reserved (zero)
//
// Record words
//   [0] test id
//   [1] state   0=pending 1=pass 2=fail 3=error
//   [2] expected pixels     [3] actual non-void pixels
//   [4] missing             [5] overdraw
//   [6] wrong colour        [7] first mismatch: x | y<<8 | kind<<16
//                                kind 0=missing 1=overdraw 2=wrong colour
//   [8] category index      [9] flags: bit0 colour mode 1, bit1 colour mode 2,
//                                bit2 colour mode 3, bit3 multispan
//   [10] reserved           [11] reserved
//
// A host-side reader should check magic, structure version and state. A
// completed run has state=2; 6+7+8 must equal 2.
// =============================================================================

#define POLY_RESULT_BASE      0x023F0400u
#define POLY_RESULT_MAGIC     0x504C5935u
#define POLY_RESULT_VERSION   1u
#define POLY_RESULT_HEADER_W  20u
#define POLY_RESULT_RECORD_W  12u
#define POLY_RESULT_RECORD(i) (POLY_RESULT_BASE + (POLY_RESULT_HEADER_W + (i) * POLY_RESULT_RECORD_W) * 4u)

// Suite box extension words (published by the main menu's publish()).
#define POLY_BOX_EXT_BASE     18u

// Header field indices
enum
{
    PH_MAGIC = 0,
    PH_VERSION,
    PH_TEST_COUNT,
    PH_DATA_VERSION,
    PH_SOURCE_TAG,
    PH_STATE,
    PH_PASS,
    PH_FAIL,
    PH_ERROR,
    PH_EXPECTED_PX,
    PH_MISSING_PX,
    PH_OVERDRAW_PX,
    PH_WRONGCOLOR_PX,
    PH_FIRST_FAIL,
    PH_LAST_TEST,
    PH_FLAGS,
    PH_CHECKSUM,
    PH_RESERVED0,
    PH_RESERVED1,
    PH_RESERVED2
};

// Record field indices
enum
{
    PR_ID = 0,
    PR_STATE,
    PR_EXPECTED_PX,
    PR_ACTUAL_PX,
    PR_MISSING_PX,
    PR_OVERDRAW_PX,
    PR_WRONGCOLOR_PX,
    PR_FIRST_MISMATCH,
    PR_CATEGORY,
    PR_FLAGS,
    PR_RESERVED0,
    PR_RESERVED1
};

enum
{
    POLY_STATE_PENDING = 0,
    POLY_STATE_PASS = 1,
    POLY_STATE_FAIL = 2,
    POLY_STATE_ERROR = 3
};

enum
{
    POLY_MISMATCH_MISSING = 0,
    POLY_MISMATCH_OVERDRAW = 1,
    POLY_MISMATCH_WRONGCOLOR = 2
};

#define POLY_SOURCE_TAG       0x31303242u // "102B"
