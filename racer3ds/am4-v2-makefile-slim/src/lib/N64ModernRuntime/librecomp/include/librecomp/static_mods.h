#ifndef __RECOMP_STATIC_MODS_H__
#define __RECOMP_STATIC_MODS_H__

// Mods recompiled at build time and linked into the executable (RECOMP_STATIC_MODS),
// for targets that cannot generate or patch code at runtime. A build tool
// (rt64-3ds tools/mod_bake) turns each mod's .nrm into C that defines one
// recomp_static_mod, and instruments every base function a baked mod replaces
// or hooks with a check of its dispatch slot at the function's entry.

#include <stdint.h>
#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

// A base export that takes an extra argument (recomp_func_ext_t, e.g. the
// calling mod's index for recomp_get_config_*): the runtime sets the target,
// and the mod calls it through a generated shim.
typedef struct {
    recomp_func_ext_t* func;
    uintptr_t arg;
} recomp_static_mod_ext_import;

typedef struct {
    const char* mod_id;                     // the manifest's id
    uint64_t binary_hash;                   // recomp_static_mod_hash of the mod_binary.bin it was built from
    uint32_t num_functions;
    recomp_func_t* const* functions;        // by function index in the mod's symbol file
    uint32_t num_imports;
    recomp_func_t** imported_funcs;         // filled by the runtime
    recomp_func_t* const* ext_import_shims; // per import: calls ext_imports[i]
    recomp_static_mod_ext_import* ext_imports;
    uint32_t num_reference_symbols;
    recomp_func_t** reference_symbol_funcs; // filled by the runtime, one per R_MIPS_26 reference reloc
    uint32_t* base_event_index;             // filled by the runtime
    uint32_t num_sections;
    int32_t* section_addresses;             // filled by the runtime: where the mod's sections were placed
} recomp_static_mod;

// A base function that baked mods replace or hook, by the ROM address of its
// section and its vram.
typedef struct {
    uint32_t section_rom;
    uint32_t function_vram;
} recomp_static_dispatch_entry;

// Called from the entry of an instrumented function while its slot is active:
// runs the slot's hooks and replacement, or the original (re-entered once
// with the slot bypassed). Returns non-zero when it handled the call.
int recomp_static_dispatch(uint8_t* rdram, recomp_context* ctx, uint32_t slot);

// FNV-1a, 64 bits: identifies the mod binary a baked mod was built from.
static inline uint64_t recomp_static_mod_hash(const uint8_t* data, size_t size) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < size; i++) {
        h ^= data[i];
        h *= 0x100000001B3ull;
    }
    return h;
}

#ifdef __cplusplus
}
#endif

#endif
