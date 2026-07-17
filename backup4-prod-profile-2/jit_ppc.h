// jit_ppc.h
#pragma once
#include <cstdint>

class Core;

// Defined in jit_trampoline.S
// Calls blockCode via bctrl so JIT block's mflr gets a valid PPC return address
#ifdef __cplusplus
extern "C" {
#endif
    void executeBlock_asm(uint32_t* blockCode);
#ifdef __cplusplus
}
#endif

namespace JitPpc {
    bool initJit(Core* core);
    void shutdownJit(Core* core);
    void runJitNds(Core& core);
    void runJitGba(Core& core);
    void invalidateJitRange(uint32_t start, uint32_t end);
    void flushJitCache();
}
