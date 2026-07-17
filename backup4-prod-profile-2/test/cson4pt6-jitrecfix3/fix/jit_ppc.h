#pragma once
#include <cstdint>

class Core;

// Declared here so jit_ppc.cpp can use it; defined in jit_trampoline.S
extern "C" void executeBlock_asm(uint32_t* blockCode);

namespace JitPpc {
    bool initJit(Core* core);
    void shutdownJit(Core* core);
    void runJitNds(Core& core);
    void runJitGba(Core& core);
    void invalidateJitRange(uint32_t start, uint32_t end);
    void flushJitCache();
}
