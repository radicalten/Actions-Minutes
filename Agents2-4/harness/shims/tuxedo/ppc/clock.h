// Host (PPC-Linux) shim for libogc "tuxedo/ppc/clock.h".
// PPCGetTickCount returns the PowerPC time base (ticks at 1/4 bus clock; ~40.5
// MHz on Broadway). Only used for diagnostics/timing in the core, so a monotonic
// nanosecond clock scaled to 40.5 MHz preserves semantics well enough.
#pragma once

#include <cstdint>
#include <time.h>

#define NOODS_HOST_TB_HZ 40500000ULL

extern "C" {

static inline uint64_t PPCGetTickCount64(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    return (ns * NOODS_HOST_TB_HZ) / 1000000000ULL;
}

static inline uint32_t PPCGetTickCount(void) {
    return (uint32_t)PPCGetTickCount64();
}

static inline uint64_t PPCTicksToUs(uint64_t ticks) { return (ticks * 1000000ULL) / NOODS_HOST_TB_HZ; }
static inline uint64_t PPCUsToTicks(uint64_t us)    { return (us * NOODS_HOST_TB_HZ) / 1000000ULL; }
static inline uint64_t PPCMsToTicks(uint64_t ms)    { return PPCUsToTicks(ms * 1000ULL); }

} // extern "C"
