#pragma once

#include <cstdint>

// Optional Wii-side renderer profiler. It is compiled out completely unless
// the Makefile is invoked with PROFILE=1; release builds contain no timing
// reads or global counters.
#ifdef NOODS_V180_PROFILE
#include <tuxedo/ppc/clock.h>

extern volatile uint64_t g_v180Profile[16];

struct V180ProfScope {
    int index;
    uint64_t start;
    explicit V180ProfScope(int idx) : index(idx), start(PPCGetTickCount()) {}
    ~V180ProfScope() { g_v180Profile[index] += PPCGetTickCount() - start; }
};

#define V180_PROFILE_SCOPE(idx) V180ProfScope _v180ProfScope(idx)
#define V180_PROFILE_TICK() PPCGetTickCount()
#define V180_PROFILE_ADD(idx, value) (g_v180Profile[idx] += (value))
#define V180_PROFILE_INC(idx) (g_v180Profile[idx]++)
#else
#define V180_PROFILE_SCOPE(idx) do {} while (0)
#define V180_PROFILE_TICK() UINT64_C(0)
#define V180_PROFILE_ADD(idx, value) do {} while (0)
#define V180_PROFILE_INC(idx) do {} while (0)
#endif
