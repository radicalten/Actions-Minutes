// jit_ppc.h
#pragma once
#include <cstdint>
#include <cstdlib>
#include "core.h"

namespace JitPpc {
    bool initJit(Core* core);   // takes Core* so it can hook runFunc
    void shutdownJit(Core* core);
    void runJitNds(Core& core);
    void runJitGba(Core& core);  // separate GBA entry point
    void invalidateJitRange(uint32_t start, uint32_t end);
    void flushJitCache();
}
