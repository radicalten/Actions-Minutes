#!/usr/bin/env python3
"""Apply the 3DS port's changes to copies of N64ModernRuntime and N64Recomp.

Every change is a small, additive, upstream-friendly edit. They are applied to a copy so
the checkouts in deps/ stay pristine; running this twice is a no-op.

  --runtime DIR      path to the N64ModernRuntime copy
  --n64recomp DIR    path to the N64Recomp copy
  --check            only report which patches would apply
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

# (relative path, old text, new text, description, replace-all)
PATCHES: list[tuple[str, str, str, str, bool]] = [
    (
        "thirdparty/concurrentqueue/lightweightsemaphore.h",
        "#elif defined(__unix__)",
        "#elif defined(__unix__) || defined(__3DS__)",
        "concurrentqueue: use the POSIX semaphore path on the 3DS (newlib provides sem_*)",
        True,   # the include guard and the class branch both test __unix__
    ),
    (
        "ultramodern/include/ultramodern/renderer_context.hpp",
        """#elif defined(__APPLE__)
        struct WindowHandle {
            void* window;
            void* view;
            auto operator<=>(const WindowHandle&) const = default;
        };
#endif""",
        """#elif defined(__APPLE__)
        struct WindowHandle {
            void* window;
            void* view;
            auto operator<=>(const WindowHandle&) const = default;
        };
#elif defined(__3DS__)
        // The 3DS has no windowing system: the "window" is one of the console's screens.
        struct WindowHandle {
            int screen;   // 0 = upper, 1 = lower
            auto operator<=>(const WindowHandle&) const = default;
        };
#endif""",
        "ultramodern: give WindowHandle a 3DS definition",
        False,
    ),
    (
        "ultramodern/src/error_handling.cpp",
        """#ifdef __APPLE__
    std::_Exit(exit_status);
#else
    std::quick_exit(exit_status);
#endif""",
        """#if defined(__APPLE__) || defined(__3DS__)
    // newlib (devkitARM) has no std::quick_exit.
    std::_Exit(exit_status);
#else
    std::quick_exit(exit_status);
#endif""",
        "ultramodern: fall back to _Exit on newlib (no std::quick_exit)",
        False,
    ),
    (
        "include/recomp.h",
        """#else
#error "128-bit integer type not found"
#endif""",
        """#elif defined(__GNUC__)
// 32-bit hosts (ARMv6K / Nintendo 3DS) have no __int128: synthesise 64x64 -> 128 with
// 32-bit partial products, which gcc turns into umull/mlal.
// Added for the 3DS build target; identical results to the __int128 branch.
static inline void recomp_umul64(uint64_t a, uint64_t b, uint64_t* lo64, uint64_t* hi64) {
    const uint64_t a0 = (uint32_t)a, a1 = a >> 32;
    const uint64_t b0 = (uint32_t)b, b1 = b >> 32;
    const uint64_t p00 = a0 * b0;
    const uint64_t p01 = a0 * b1;
    const uint64_t p10 = a1 * b0;
    const uint64_t p11 = a1 * b1;
    const uint64_t mid = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
    *lo64 = (uint32_t)p00 | (mid << 32);
    *hi64 = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

static inline void DMULTU(uint64_t a, uint64_t b, uint64_t* lo64, uint64_t* hi64) {
    recomp_umul64(a, b, lo64, hi64);
}

static inline void DMULT(int64_t a, int64_t b, int64_t* lo64, int64_t* hi64) {
    const int negative = (a < 0) != (b < 0);
    const uint64_t ua = (a < 0) ? (uint64_t)(-(a + 1)) + 1 : (uint64_t)a;
    const uint64_t ub = (b < 0) ? (uint64_t)(-(b + 1)) + 1 : (uint64_t)b;
    uint64_t lo, hi;
    recomp_umul64(ua, ub, &lo, &hi);
    if (negative) {
        lo = ~lo + 1;
        hi = ~hi + (lo == 0 ? 1 : 0);
    }
    *lo64 = (int64_t)lo;
    *hi64 = (int64_t)hi;
}

#else
#error "128-bit integer type not found"
#endif""",
        "N64Recomp: 64x64->128 multiply without __int128 (32-bit hosts)",
        False,
    ),
    (
        "librecomp/src/recomp.cpp",
        "#else\n#    include <sys/mman.h>\n#endif",
        "#elif !defined(__3DS__)\n#    include <sys/mman.h>\n#endif",
        "librecomp: no <sys/mman.h> on the 3DS",
        False,
    ),
    (
        "librecomp/src/recomp.cpp",
        """#else
    rdram = (uint8_t*)mmap(NULL, allocation_size, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    alloc_failed = rdram == reinterpret_cast<uint8_t*>(MAP_FAILED);
    if (!alloc_failed) {
        // mprotect returns -1 on failure.
        alloc_failed = (mprotect(rdram, mem_size, PROT_READ | PROT_WRITE) == -1);
        if (alloc_failed) {
            munmap(rdram, allocation_size);
        }
    }
#endif""",
        """#elif defined(__3DS__)
    // The 3DS has no mmap/mprotect and no 4 GiB of address space to reserve, so the
    // "reserve 4 GiB, commit mem_size" trick becomes one committed allocation. Guest
    // addresses still resolve to offsets from the base because the recompiled code does
    // the arithmetic in 64 bits; out-of-range accesses now run past this block instead of
    // into the reserved-but-uncommitted window.
    rdram = (uint8_t*)memalign(0x1000, mem_size);
    alloc_failed = rdram == nullptr;
#else
    rdram = (uint8_t*)mmap(NULL, allocation_size, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    alloc_failed = rdram == reinterpret_cast<uint8_t*>(MAP_FAILED);
    if (!alloc_failed) {
        // mprotect returns -1 on failure.
        alloc_failed = (mprotect(rdram, mem_size, PROT_READ | PROT_WRITE) == -1);
        if (alloc_failed) {
            munmap(rdram, allocation_size);
        }
    }
#endif""",
        "librecomp: replace the 4 GiB PROT_NONE RDRAM reservation with a plain allocation",
        False,
    ),
    (
        "librecomp/src/recomp.cpp",
        "#include <cstdio>",
        "#include <cstdio>\n#if defined(__3DS__)\n#include <malloc.h>   // memalign on newlib\n#endif",
        "librecomp: memalign needs <malloc.h> on newlib",
        False,
    ),
    (
        "librecomp/include/librecomp/mods.hpp",
        """    std::size_t operator()(const recomp::mods::HookDefinition& def) const {
        // This hash packing only works if the resulting value is 64 bits.
        static_assert(sizeof(std::size_t) == 8);
        // Combine the three values into a single 64-bit value.
        // The lower 2 bits of a function address will always be zero, so pack
        // the value of at_return into the lowest bit.
        return (size_t(def.section_rom) << 32) | size_t(def.function_vram) | size_t(def.at_return ? 1 : 0);
    }""",
        """    std::size_t operator()(const recomp::mods::HookDefinition& def) const {
        if constexpr (sizeof(std::size_t) == 8) {
            // The lower 2 bits of a function address will always be zero, so pack
            // the value of at_return into the lowest bit.
            return (size_t(def.section_rom) << 32) | size_t(def.function_vram) | size_t(def.at_return ? 1 : 0);
        } else {
            // 32-bit hosts (e.g. the Nintendo 3DS build target) cannot pack three values
            // into 64 bits, so mix them instead of failing a static_assert.
            uint32_t h = def.section_rom * 0x9E3779B1u;
            h ^= def.function_vram + 0x7F4A7C15u + (h << 6) + (h >> 2);
            h ^= static_cast<uint32_t>(def.at_return) * 0x85EBCA6Bu;
            return static_cast<std::size_t>(h);
        }
    }""",
        "librecomp: 32-bit-safe HookDefinition hash (was a hard 64-bit static_assert)",
        False,
    ),
]

# recomp.h lives in the N64Recomp tree, the rest in the runtime tree.
RUNTIME_PREFIXES = ("thirdparty/", "ultramodern/", "librecomp/")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--runtime", required=True, type=Path)
    ap.add_argument("--n64recomp", required=True, type=Path)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    applied = skipped = missing = 0
    for rel, old, new, why, replace_all in PATCHES:
        base = args.runtime if rel.startswith(RUNTIME_PREFIXES) else args.n64recomp
        path = base / rel
        if not path.is_file():
            print(f"MISSING  {rel}")
            missing += 1
            continue
        text = path.read_text(encoding="utf-8")
        if new in text:
            print(f"already  {rel}: {why}")
            skipped += 1
            continue
        if old not in text:
            print(f"NO MATCH {rel}: {why}")
            missing += 1
            continue
        if not args.check:
            path.write_text(text.replace(old, new) if replace_all else text.replace(old, new, 1),
                            encoding="utf-8")
        print(f"patch    {rel}: {why}")
        applied += 1

    print(f"\n{applied} applied, {skipped} already applied, {missing} not applicable")
    return 0 if missing == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
