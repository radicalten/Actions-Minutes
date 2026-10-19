#pragma once
// Bare-metal port of the drawing/capture path used by polyrastertest.

#include "poly_compat.h"

// Power the 3D engine, reset its state the same way libnds glInit() does, and
// put VRAM banks A/B into LCDC mode ready for capture/comparison.
void poly_gfx_init();

// Draw one scene (upstream drawPoly). The 3D output is composited at the next
// frame boundary; capture it with poly_capture_begin/VBlank/poly_capture_end.
bool poly_draw_test(int index);

// Frame-aligned display capture into VRAM bank A. Requests the capture during
// vblank and waits for all scanlines to be written. Returns false on timeout.
bool poly_capture_frame();

// Number of non-void pixels in the capture framebuffer.
u32 poly_count_actual();

// Display selectors for the human-facing part of the runner.
void poly_show_capture();    // show VRAM A (raw or colour-coded diff)
void poly_show_3d();         // show the live 3D layer
