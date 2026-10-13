// Host (PPC-Linux) shim for libogc "tuxedo/tick.h".
#pragma once

#include <cstdint>

extern "C" {

static inline uint32_t PPCGetTickCount(void);
static inline uint64_t PPCTicksToUs(uint64_t ticks);

} // extern "C"

#include "ppc/clock.h"
