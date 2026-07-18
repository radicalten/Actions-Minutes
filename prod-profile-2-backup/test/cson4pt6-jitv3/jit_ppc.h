// ============================================================
// jit_ppc.h (inline header section)
// ============================================================
// In a separate jit_ppc.h file include:

#pragma once
#include <cstdint>
#include <cstdlib>
#include "core.h"

namespace JitPpc {
    bool initJit();
    void shutdownJit();
    void runJitNds(Core& core);
    void invalidateJitRange(uint32_t start, uint32_t end);
    void flushJitCache();
}
