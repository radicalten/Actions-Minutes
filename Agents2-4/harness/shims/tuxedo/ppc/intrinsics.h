// Host (PPC-Linux) shim for libogc "tuxedo/ppc/intrinsics.h".
// The real header pulls in __builtin_ppc_* helpers; the emulator core only needs
// a couple of cache/sync primitives, which the host compiler provides natively.
#pragma once

#include <cstdint>

#define PPC_ICACHE_LINE 32
#define PPC_DCACHE_LINE 32

static inline void ppc_sync(void) { __asm__ __volatile__("sync" ::: "memory"); }
static inline void ppc_isync(void) { __asm__ __volatile__("isync" ::: "memory"); }

static inline void ppc_dcbf(void *p) {
    __asm__ __volatile__("dcbf 0, %0" :: "r"(p) : "memory");
}

static inline void ppc_icbi(void *p) {
    __asm__ __volatile__("icbi 0, %0" :: "r"(p) : "memory");
}
