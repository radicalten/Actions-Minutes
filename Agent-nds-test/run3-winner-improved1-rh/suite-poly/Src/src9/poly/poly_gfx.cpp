// =============================================================================
// poly_gfx.cpp — bare-metal 3D setup + polygon submission + DISPCAPCNT capture
//
// This reproduces, register for register, what polyrastertest gets from libnds:
//   glInit()            poly_gfx_init()
//   glBegin/glVertex3v16/glEnd/glFlush/glPolyFmt/glLoadMatrix4x4
//                       poly_submit()
//   DCAP setup + enable  poly_gfx_init() / poly_capture()
//
// libnds reference values (video.h / videoGL.h):
//   GFX_VIEWPORT = x1 | y1<<8 | x2<<16 | y2<<24
//   GFX_VERTEX16 = (y<<16) | (x & 0xFFFF), then a second write with z
//   glClearColor(r,g,b,a) = RGB15 | (alpha<<16), glClearPolyID(id) = id<<24
//   glFlush(mode) writes GFX_FLUSH
// =============================================================================

#include "poly_compat.h"
#include "poly_model.h"
#include "poly_gfx.h"

// -----------------------------------------------------------------------------
// helpers
// -----------------------------------------------------------------------------

static void wait_status_clear(u32 mask, u32 spin_limit)
{
    while ((POLY_GFX_STATUS & mask) && spin_limit)
        spin_limit--;
}

// Equivalent of libnds glResetMatrixStack(): drain pending pushes/pops, pop
// both stacks back to the top and load identity into projection/modelview.
static void reset_matrix_stack()
{
    u32 guard = 1u << 20;
    while ((POLY_GFX_STATUS & POLY_GFX_STATUS_MATRIX_STACK_BUSY) && guard)
    {
        POLY_GFX_STATUS |= POLY_GFX_STATUS_MATRIX_STACK_ERROR;
        guard--;
    }

    if (POLY_GFX_STATUS & (1u << 13))
    {
        POLY_MTX_CONTROL = POLY_MTX_PROJECTION;
        POLY_MTX_POP = 1;
    }

    POLY_MTX_CONTROL = POLY_MTX_MODELVIEW;
    POLY_MTX_POP = (POLY_GFX_STATUS >> 8) & 0x1F;

    POLY_MTX_CONTROL = POLY_MTX_MODELVIEW;
    POLY_MTX_IDENTITY = 0;
    POLY_MTX_CONTROL = POLY_MTX_PROJECTION;
    POLY_MTX_IDENTITY = 0;
    POLY_MTX_CONTROL = POLY_MTX_POSITION;
    POLY_MTX_IDENTITY = 0;
    POLY_MTX_CONTROL = POLY_MTX_TEXTURE;
    POLY_MTX_IDENTITY = 0;
}

static inline void submit_vertex(s16 x, s16 y, s16 z)
{
    POLY_GFX_VERTEX16 = ((u32)(u16)y << 16) | ((u16)x & 0xFFFF);
    POLY_GFX_VERTEX16 = (u32)(u16)z;
}

static void submit_polygon(const s16 verts[NumVerts][NumAxis], const u16 colors[4])
{
    int n;
    if (verts[3][0] != -512)
    {
        n = 4;
        POLY_GFX_BEGIN = GL_QUAD;
    }
    else
    {
        n = 3;
        POLY_GFX_BEGIN = GL_TRIANGLE;
    }

    for (int i = 0; i < n; i++)
    {
        POLY_GFX_COLOR = colors[i];
        submit_vertex(verts[i][0], verts[i][1], verts[i][2]);
    }
}

// -----------------------------------------------------------------------------
// capture
// -----------------------------------------------------------------------------

bool poly_capture_frame()
{
    // Align to a vblank, request the capture, then let two full frames pass:
    // the request is latched at the end of vblank and every scanline of the
    // following frame is written to bank A. Two vblanks make this independent
    // of where inside vblank we happen to be.
    while ((POLY_DISPSTAT & 1) == 1) { } // leave any vblank in progress
    while ((POLY_DISPSTAT & 1) == 0) { } // vblank start
    POLY_DISPCAPCNT |= POLY_DCAP_ENABLE;

    for (int frame = 0; frame < 2; frame++)
    {
        while ((POLY_DISPSTAT & 1) == 1) { }
        while ((POLY_DISPSTAT & 1) == 0) { }
    }

    POLY_DISPCAPCNT &= ~POLY_DCAP_ENABLE; // idempotent; hardware clears it too
    return true;
}

// -----------------------------------------------------------------------------
// public entry points
// -----------------------------------------------------------------------------

void poly_gfx_init()
{
    // libnds glInit(): powerOn(POWER_3D_CORE | POWER_MATRIX)
    // POWCNT1 bit 2 = 3D matrix, bit 3 = main 3D core. Missing either one
    // leaves the geometry engine unpowered and every capture comes back empty.
    POLY_POWCNT1 |= (1u << 2) | (1u << 3);

    reset_matrix_stack();

    // Clear FIFO (libnds glInit: GFX_STATUS |= 1<<29).
    POLY_GFX_STATUS |= (1u << 29);
    wait_status_clear(POLY_GFX_STATUS_BUSY, 1u << 20);

    // Prime the vertex/polygon buffers like glInit() does.
    POLY_GFX_FLUSH = 0;
    POLY_GFX_CONTROL = 0;

    // Rear plane: black, alpha 31, polygon id 0.
    POLY_GFX_CLEAR_COLOR = (u32)0 | (31u << 16);
    POLY_GFX_CLEAR_COLOR &= 0xC0FFFFFFu;
    POLY_GFX_CLEAR_DEPTH = GL_MAX_DEPTH;

    POLY_GFX_TEX_FORMAT = 0;
    POLY_GFX_POLY_FORMAT = 0;

    // Capture target and reference image banks must be LCDC-addressable.
    POLY_VRAMCNT_A = 0x80;
    POLY_VRAMCNT_B = 0x80;

    // libnds MODE_0_3D: normal display, BG0 active and pointing at the 3D
    // layer. Without BG0 the 3D layer is neither composited nor captured.
    POLY_DISPCNT = POLY_MODE_0_3D;

    // Mode A, source A = 3D only, 256x192, offset 0, target bank A.
    POLY_DISPCAPCNT = POLY_DCAP_MODE_A | POLY_DCAP_SRC_A_3DONLY |
                      POLY_DCAP_SIZE_256x192 | POLY_DCAP_OFFSET_0 |
                      POLY_DCAP_BANK_A;
}

bool poly_draw_test(int index)
{
    const TestData &t = Tests[index];

    POLY_GFX_CLEAR_COLOR = (u32)(t.RearID & 0x3F) << 24 | (31u << 16);
    POLY_GFX_CLEAR_DEPTH = t.RearDepth;

    for (int i = 0; i < 8; i++)
        POLY_GFX_EDGE_TABLE[i] = t.OutlineColors[i];

    POLY_GFX_VIEWPORT = t.Viewport;

    POLY_MTX_CONTROL = POLY_MTX_PROJECTION;
    for (int i = 0; i < 16; i++)
        POLY_MTX_LOAD4x4 = t.Projection.m[i];

    POLY_MTX_CONTROL = POLY_MTX_MODELVIEW;
    POLY_MTX_IDENTITY = 0;

    POLY_GFX_POLY_FORMAT = t.PolyAttr;
    POLY_GFX_CONTROL = (u16)t.Disp3DCnt;

    submit_polygon(t.Vertices, t.VertexColors);

    if (t.ExtendedTestData != 0)
    {
        const ExtTestData &ext = ExtendedTests[t.ExtendedTestData - 1];
        for (u32 p = 0; p < ext.NumPolygons; p++)
            submit_polygon(ext.Polygons[p].Vertices, ext.Polygons[p].VertexColors);
    }

    // Note: upstream never issues glEnd(); the vertex list is terminated by the
    // flush/swap. Replicating that is important for bit-identical rasterization.
    POLY_GFX_FLUSH = 0;
    return true;
}

u32 poly_count_actual()
{
    u32 n = 0;
    for (u32 i = 0; i < (u32)POLY_SCREEN_W * POLY_SCREEN_H; i++)
        if ((POLY_FB_A[i] & 0x7FFF) != 0)
            n++;
    return n;
}

void poly_show_capture()
{
    POLY_DISPCNT = POLY_DISPLAY_MODE_FB | (0u << 18); // VRAM bank A
}

void poly_show_3d()
{
    POLY_DISPCNT = POLY_MODE_0_3D;
}
