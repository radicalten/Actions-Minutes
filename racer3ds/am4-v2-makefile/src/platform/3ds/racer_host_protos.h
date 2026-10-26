// Star Wars Episode I: Racer - 3DS host prototypes.
//
// Force-included (-include) into every generated RecompiledFuncs/*.c translation
// unit only. N64Recomp already emits the declarations it needs for the runtime's
// reimplemented libultra functions (the *_recomp family), and
// tools/post_recomp_patches.py inserts a local prototype above each redirect it
// makes (osPiStartDma_recomp, racer_get_count). This header documents the two
// host functions this game provides and gives the C++ side a single place to
// keep their C linkage correct. Racer has no RecompiledPatches, so unlike
// Banjo's bk_host_protos.h there is no RECOMP_FUNC weak redefine here.
#ifndef RACER_HOST_PROTOS_H
#define RACER_HOST_PROTOS_H

#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

// Game's osGetCount (symbols.toml's racer_osGetCount), redirected by
// post_recomp_patches.py: returns the runtime's virtual OS count.
void racer_get_count(uint8_t* rdram, recomp_context* ctx);

// Game's __osSiRawStartDma (COP0/MMIO, cannot be recompiled): synthesizes the
// PIF controller status block on a read and raises the SI completion message.
void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx);

#ifdef __cplusplus
}
#endif

#endif  // RACER_HOST_PROTOS_H
