/*
    Copyright (C) 2026 radicalten

    This file is part of NooDS-Wii.

    NooDS-Wii is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    NooDS-Wii is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NooDS-Wii. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Output parameters (what ASND expects)
// ---------------------------------------------------------------------------
#define WIIAUD_OUT_RATE           48000
#define WIIAUD_FRAMES_PER_BUF     1024
#define WIIAUD_BUF_BYTES_STEREO   (WIIAUD_FRAMES_PER_BUF * 4)   // 1024 * 2ch * 2bytes
#define WIIAUD_BUF_BYTES_MONO     (WIIAUD_FRAMES_PER_BUF * 2)

// ---------------------------------------------------------------------------
// NDS SPU: 32768 Hz, one sample every 512*2 = 1024 ARM cycles.
// At 33.51 MHz ARM7, that is 32730 samples/sec ≈ 32768.
// Frames accumulated per 60 Hz video frame:  32768 / 60 = 546.1
// Use 548 to give the SPU a small surplus so the buffer is always full.
// ---------------------------------------------------------------------------
#define WIIAUD_NDS_RATE              32768
#define WIIAUD_NDS_FRAMES_PER_VFRAME 548
#define WIIAUD_NDS_FRAMES            WIIAUD_NDS_FRAMES_PER_VFRAME

// ---------------------------------------------------------------------------
// GBA SPU: same sample rate, one sample every 512 ARM cycles.
// GBA ARM7 at 16.78 MHz → 32768 samples/sec.
// Frames per 60 Hz frame: 32768 / 60 = 546.1  — use 548 as well so that
// BOTH modes use the SAME buffer size.  This eliminates the reallocation
// that was causing getSamples() to reset ready=false every mode switch.
// ---------------------------------------------------------------------------
#define WIIAUD_GBA_RATE              32768
#define WIIAUD_GBA_FRAMES_PER_VFRAME 548
#define WIIAUD_GBA_FRAMES            WIIAUD_GBA_FRAMES_PER_VFRAME

// Compile-time assert: both paths must use identical frame counts so
// Spu::getSamples() never sees a size change between NDS and GBA mode.
#ifdef __cplusplus
static_assert(WIIAUD_NDS_FRAMES == WIIAUD_GBA_FRAMES,
    "NDS and GBA frame counts must match to avoid SPU buffer reallocation");
#endif

#ifdef __cplusplus
}
#endif
