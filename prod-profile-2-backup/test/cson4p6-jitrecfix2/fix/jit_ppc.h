// jit_ppc.h
#pragma once
#include <cstdint>
#include <cstdlib>

class Core;

namespace JitPpc {
    // Core* overloads (preferred — hook runFunc automatically)
    bool initJit(Core* core);
    void shutdownJit(Core* core);

    // No-argument overloads for call sites that don't have a Core* handy.
    // These skip the runFunc hook; caller must call core->setRunFunc() separately.
    inline bool initJit()      { return initJit(nullptr); }
    inline void shutdownJit()  { shutdownJit(nullptr);    }

    void runJitNds(Core& core);
    void runJitGba(Core& core);
    void invalidateJitRange(uint32_t start, uint32_t end);
    void flushJitCache();
}
