# N64ModernRuntime / N64Recomp — 3DS (ARMv6K) changes

Eight small edits make the recompilation runtime build with devkitARM for ARMv6K. They are
additive and guarded by `__3DS__` (or `__GNUC__` for the multiply helper), so applying them
does not change the PC builds.

Apply them:

```sh
python3 ../platform/3ds/tools/patch_runtime.py \
    --runtime  <checkout>/N64ModernRuntime \
    --n64recomp <checkout>/N64Recomp
```

or use `../platform/3ds/tools/build-runtime.sh`, which patches a temporary copy and
cross-compiles it. Full patched files are in `files/`, the unified diff in
`n64modernruntime-3ds.patch`, and the resulting ARMv6K objects in
`build-runtime-armv6k.tar.gz`.

| # | File | Change | Why |
|---|------|--------|-----|
| 1 | `thirdparty/concurrentqueue/lightweightsemaphore.h` | `#elif defined(__unix__)` → `… \|\| defined(__3DS__)` (both the include and the class branch) | newlib provides POSIX `sem_*`; without this, moodycamel's semaphore hits `#error Unsupported platform!` |
| 2 | `ultramodern/include/ultramodern/renderer_context.hpp` | new `#elif defined(__3DS__)` branch defining `WindowHandle` (`{ int screen; }`) | the header defines `WindowHandle` per platform; the 3DS has no window system |
| 3 | `ultramodern/src/error_handling.cpp` | `std::quick_exit` → `std::_Exit` on `__3DS__` | newlib has no `std::quick_exit` |
| 4 | `N64Recomp/include/recomp.h` | new `#elif defined(__GNUC__)` branch: `DMULT`/`DMULTU` from 32-bit partial products | ARMv6K has no `__int128`; the header hard-`#error`s otherwise. Verified bit-exact against the `__int128` implementation over 4,000,000 random 64×64 products, including negatives and small values |
| 5 | `librecomp/src/recomp.cpp` | `#else` → `#elif !defined(__3DS__)` around `<sys/mman.h>` | no `<sys/mman.h>` on the 3DS |
| 6 | `librecomp/src/recomp.cpp` | new `#elif defined(__3DS__)` branch: `memalign(0x1000, mem_size)` instead of `mmap(4 GiB, PROT_NONE)` + `mprotect` | the PC build reserves 4 GiB of address space and commits `mem_size`; a 32-bit console has no such address space to reserve |
| 7 | `librecomp/src/recomp.cpp` | `#include <malloc.h>` on `__3DS__` | `memalign` lives there on newlib |
| 8 | `librecomp/include/librecomp/mods.hpp` | `HookDefinition` hash: `if constexpr (sizeof(size_t) == 8)` packing, else a 32-bit mix | the original `static_assert(sizeof(std::size_t) == 8)` fails on every 32-bit host |

## Not patched: `librecomp/src/mods.cpp`

The mod loader loads mods as native shared libraries (`dlfcn.h`, `dlopen`, per-architecture
ELF/Mach-O/PE handling). The 3DS has no dynamic loader for user code, and shipping
host-agnostic mods would need a different mechanism entirely. `build-runtime.sh` skips it.
A 3DS port links mods statically or ships without them.

## Cross-build result

```
$ ./tools/build-runtime.sh
skipping mods.cpp (native mod loader, dlopen)
cross-compiling 43 runtime sources for ARMv6K, 2 at a time
compiled: 43   failed: 0
```

43 objects, ~0.5 MB of `.text` in total (`ultramodern`: 16/16 sources, `librecomp`: 27/27
of the sources it ships with `mods.cpp` excluded). Objects are named
`ultramodern_*.o` / `librecomp_*.o` because both libraries contain `rsp.cpp` and
`extensions.cpp`. Toolchain: devkitARM 16.1.0 (GCC 16.1.0),
libctru 2.7.0, `-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft`.

## Upstreaming

Each change is a few lines and none of them alter PC behaviour, so they are reasonable
upstream PRs: (1) and (4) are pure portability, (2) and (3) are a new platform branch,
(5)-(7) generalize the RDRAM allocation, (8) generalizes a hash. The author of this port
has no relationship with the upstream project; treat them as suggestions.
