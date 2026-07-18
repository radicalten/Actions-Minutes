// jit_ppc.h
#pragma once
#include <cstdint>
#include <cstdlib>

class Core;

namespace JitPpc {
    bool initJit(Core* core);
    void shutdownJit(Core* core);

    inline bool initJit()     { return initJit(nullptr);  }
    inline void shutdownJit() { shutdownJit(nullptr);     }

    void runJitNds(Core& core);
    void runJitGba(Core& core);
    void invalidateJitRange(uint32_t start, uint32_t end);
    void flushJitCache();
}
