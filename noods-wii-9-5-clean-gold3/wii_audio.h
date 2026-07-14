// wii_audio.h (optimized for PowerPC/Wii)
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Output configuration ──────────────────────────────────────────────────────
// 48 kHz stereo output via ASND, 1024-frame double-buffered DMA.
// Buffer sizes are compile-time constants so the compiler can fold them
// into immediate operands on PowerPC (no load-from-memory overhead).

#define WIIAUD_OUT_RATE             48000
#define WIIAUD_FRAMES_PER_BUF       1024

// Stereo: 2 channels × 2 bytes (int16) = 4 bytes/frame
// Mono:   1 channel  × 2 bytes (int16) = 2 bytes/frame
// Written as shifts so the compiler emits a single slwi instruction.
#define WIIAUD_BUF_BYTES_STEREO     (WIIAUD_FRAMES_PER_BUF << 2)   // × 4
#define WIIAUD_BUF_BYTES_MONO       (WIIAUD_FRAMES_PER_BUF << 1)   // × 2

// ── NDS source timing ─────────────────────────────────────────────────────────
// NDS SPU runs at 32,768 Hz.
// At 60 fps: 32768 / 60 ≈ 546.1 frames/vframe.
// noods uses 548 to keep the ring buffer from underrunning;
// we expose both the rate and the per-vframe count for the resampler.
#define WIIAUD_NDS_RATE                 32768
#define WIIAUD_NDS_FRAMES_PER_VFRAME    548
#define WIIAUD_NDS_FRAMES               WIIAUD_NDS_FRAMES_PER_VFRAME

// ── GBA source timing ─────────────────────────────────────────────────────────
// GBA SPU also runs at 32,768 Hz but the vframe period is slightly longer
// (GBA scanline = 308 dots × 4 cycles vs NDS 355 × 6), giving 549 frames.
#define WIIAUD_GBA_RATE                 32768
#define WIIAUD_GBA_FRAMES_PER_VFRAME    549
#define WIIAUD_GBA_FRAMES               WIIAUD_GBA_FRAMES_PER_VFRAME

// ── Derived constants (used by the resampler and DMA fill loops) ──────────────
// Maximum source frames in a single pull (GBA > NDS, so GBA wins).
// Used to size the static staging buffer in PullSpuSamples() without a branch.
#if WIIAUD_GBA_FRAMES > WIIAUD_NDS_FRAMES
#   define WIIAUD_MAX_SRC_FRAMES    WIIAUD_GBA_FRAMES
#else
#   define WIIAUD_MAX_SRC_FRAMES    WIIAUD_NDS_FRAMES
#endif

// Number of uint32_t words in the largest possible source pull.
// Handy for staging-buffer declarations: uint32_t buf[WIIAUD_MAX_SRC_FRAMES]
// (already used in main.cpp PullSpuSamples; centralised here for consistency).
#define WIIAUD_MAX_SRC_WORDS        WIIAUD_MAX_SRC_FRAMES

// Total byte count of the staging buffer (for cache-flush helpers).
#define WIIAUD_STAGING_BYTES        (WIIAUD_MAX_SRC_FRAMES * 4)

// ── Volume / gain headroom ────────────────────────────────────────────────────
// ASND MAX_VOLUME = 255.  Keeping it at full avoids a multiply in the driver.
#ifndef WIIAUD_MASTER_VOLUME
#   define WIIAUD_MASTER_VOLUME     255
#endif

#ifdef __cplusplus
}
#endif
