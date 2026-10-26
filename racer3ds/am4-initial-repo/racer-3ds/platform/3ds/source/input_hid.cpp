// Input path for the 3DS target: 3DS buttons + circle pad -> N64 controller state.
//
// The default mapping is the one that makes sense for Racer (accelerate/brake on the face
// buttons, C-buttons on the D-pad, air brakes on the shoulders); the recompiled game
// receives exactly what n64frontend's get_input callback would have produced on PC.
#include <3ds.h>
#include <cstdio>
#include <cstring>

#include "r3ds.hpp"

namespace r3ds {
namespace {

PadState s_pad[4];
char s_mapping[128] = "A A  B B  X Z  Y CUp  D Cbtn";

// Circle pad is -156..156; convert to -1..1 with a small deadzone.
float axis(s16 v) {
    const float f = (float)v / 156.0f;
    if (f > -0.10f && f < 0.10f) return 0.0f;
    return f > 1.0f ? 1.0f : (f < -1.0f ? -1.0f : f);
}

} // namespace

void input_poll() {
    hidScanInput();

    const u32 held = hidKeysHeld();
    PadState& p = s_pad[0];

    p.buttons = 0;
    if (held & KEY_A)      p.buttons |= Btn::A;
    if (held & KEY_B)      p.buttons |= Btn::B;
    if (held & KEY_X)      p.buttons |= Btn::Z;
    if (held & KEY_Y)      p.buttons |= Btn::CUp;
    if (held & KEY_L)      p.buttons |= Btn::L;
    if (held & KEY_R)      p.buttons |= Btn::R;
    if (held & KEY_START)  p.buttons |= Btn::Start;
    if (held & KEY_DUP)    p.buttons |= Btn::CUp;
    if (held & KEY_DDOWN)  p.buttons |= Btn::CDown;
    if (held & KEY_DLEFT)  p.buttons |= Btn::CLeft;
    if (held & KEY_DRIGHT) p.buttons |= Btn::CRight;
    // New 3DS: the C-stick is reported as digital buttons by libctru, and does the same
    // job as the D-pad here (C buttons).
    if (held & KEY_CSTICK_UP)    p.buttons |= Btn::CUp;
    if (held & KEY_CSTICK_DOWN)  p.buttons |= Btn::CDown;
    if (held & KEY_CSTICK_LEFT)  p.buttons |= Btn::CLeft;
    if (held & KEY_CSTICK_RIGHT) p.buttons |= Btn::CRight;

    circlePosition circle;
    std::memset(&circle, 0, sizeof(circle));
    hidCircleRead(&circle);
    p.x = axis(circle.dx);
    p.y = axis(circle.dy);

    for (int i = 1; i < 4; i++) s_pad[i] = PadState{};
}

PadState input_get(int port) {
    if (port < 0 || port >= 4) return PadState{};
    return s_pad[port];
}

const char* input_last_mapping(char* line, size_t len) {
    std::snprintf(line, len, "%s", s_mapping);
    return line;
}

} // namespace r3ds
