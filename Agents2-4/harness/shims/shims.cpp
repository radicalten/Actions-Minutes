// Host (PPC-Linux) implementations for the libogc shims used by the NooDS-Wii
// emulation core, so the interpreter/JIT can be executed (and differentially
// tested) on a big-endian PowerPC Linux host under qemu-ppc.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>
#include <new>

// The Wii build hands MEM2 (0x90000000..0x93400000) to a bump allocator:
// Noods_MEM2_Alloc never frees, and main.cpp's global operator delete skips any
// pointer that lives in MEM2. Reproduce that pairing exactly on the host, over a
// single writable+executable mapping so the JIT code pool is runnable.
static uint8_t *s_mem2Lo = nullptr;
static uint8_t *s_mem2Ptr = nullptr;
static uint8_t *s_mem2Hi = nullptr;

static void mem2Init() {
    if (s_mem2Lo) return;
    const size_t size = 64u * 1024u * 1024u; // covers MEM2 footprint of the Wii build
    void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "shims: MEM2 mmap failed\n");
        abort();
    }
    s_mem2Lo = (uint8_t *)p;
    s_mem2Ptr = s_mem2Lo;
    s_mem2Hi = s_mem2Lo + size;
}

static bool mem2Owns(const void *p) {
    return s_mem2Lo && (const uint8_t *)p >= s_mem2Lo && (const uint8_t *)p < s_mem2Hi;
}

extern "C" {

void *Noods_MEM2_Alloc(size_t size) {
    mem2Init();
    if (size == 0) return nullptr;
    size = (size + 31u) & ~(size_t)31u;
    if (s_mem2Ptr + size > s_mem2Hi) return nullptr;
    void *p = s_mem2Ptr;
    s_mem2Ptr += size;
    return p;
}

void Noods_MEM2_Free(void *ptr) { (void)ptr; }

void DCFlushRange(void *start, uint32_t length) {
    uintptr_t a = (uintptr_t)start & ~(uintptr_t)31;
    uintptr_t e = ((uintptr_t)start + length + 31) & ~(uintptr_t)31;
    __asm__ __volatile__("sync" ::: "memory");
    for (uintptr_t p = a; p < e; p += 32)
        __asm__ __volatile__("dcbst 0, %0" :: "r"(p) : "memory");
    __asm__ __volatile__("sync" ::: "memory");
}

void DCInvalidateRange(void *start, uint32_t length) {
    uintptr_t a = (uintptr_t)start & ~(uintptr_t)31;
    uintptr_t e = ((uintptr_t)start + length + 31) & ~(uintptr_t)31;
    __asm__ __volatile__("sync" ::: "memory");
    for (uintptr_t p = a; p < e; p += 32)
        __asm__ __volatile__("dcbi 0, %0" :: "r"(p) : "memory");
    __asm__ __volatile__("sync" ::: "memory");
}

void ICInvalidateRange(void *start, uint32_t length) {
    uintptr_t a = (uintptr_t)start & ~(uintptr_t)31;
    uintptr_t e = ((uintptr_t)start + length + 31) & ~(uintptr_t)31;
    for (uintptr_t p = a; p < e; p += 32)
        __asm__ __volatile__("dcbst 0, %0" :: "r"(p) : "memory");
    __asm__ __volatile__("sync" ::: "memory");
    for (uintptr_t p = a; p < e; p += 32)
        __asm__ __volatile__("icbi 0, %0" :: "r"(p) : "memory");
    __asm__ __volatile__("sync; isync" ::: "memory");
}

void SYS_Report(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

int fatInitDefault(void) { return 0; }
void fatUnmount(const char *name) { (void)name; }

} // extern "C"


// Mirror main.cpp's global allocation hooks so MEM2-backed objects are never
// handed to the host allocator.
void *operator new(size_t size) {
    void *p = Noods_MEM2_Alloc(size);
    if (!p) p = malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}
void *operator new[](size_t size) {
    void *p = Noods_MEM2_Alloc(size);
    if (!p) p = malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void *p) noexcept { if (p && !mem2Owns(p)) free(p); }
void operator delete[](void *p) noexcept { if (p && !mem2Owns(p)) free(p); }
void operator delete(void *p, size_t) noexcept { if (p && !mem2Owns(p)) free(p); }
void operator delete[](void *p, size_t) noexcept { if (p && !mem2Owns(p)) free(p); }
