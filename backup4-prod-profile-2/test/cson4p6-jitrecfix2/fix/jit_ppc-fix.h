// jit_ppc.h
#pragma once
#include <cstdint>
#include <cstdlib>

class Core;

namespace JitPpc {
    // Core* versions — hook runFunc automatically
    bool initJit(Core* core);
    void shutdownJit(Core* core);

    // No-argument inline wrappers for call sites without a Core* handy.
    // The header-inline definition means jit_ppc.cpp must NOT define
    // no-argument versions of these — they live here only.
    inline bool initJit()     { return initJit(nullptr);  }
    inline void shutdownJit() { shutdownJit(nullptr);      }

    void runJitNds(Core& core);
    void runJitGba(Core& core);
    void invalidateJitRange(uint32_t start, uint32_t end);
    void flushJitCache();
}
