#include <cassert>
#include <cstdio>
#include "o1heap/o1heap.h"

#include "librecomp/addresses.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/helpers.hpp"
#include "ultramodern/error_handling.hpp"

static uint32_t heap_offset;

static inline O1HeapInstance* get_heap(uint8_t* rdram) {
    return reinterpret_cast<O1HeapInstance*>(&rdram[heap_offset]);
}

extern "C" void recomp_alloc(uint8_t* rdram, recomp_context* ctx) {
    uint8_t* mem = reinterpret_cast<uint8_t*>(recomp::alloc(rdram, ctx->r4));
    // NULL for NULL, which the caller can check (not an address below RDRAM).
    ctx->r2 = mem == nullptr ? 0 : (uint32_t)(mem - rdram) + 0xFFFFFFFF80000000ULL;
}

extern "C" void recomp_free(uint8_t* rdram, recomp_context* ctx) {
    PTR(void) to_free = _arg<0, PTR(void)>(rdram, ctx);
    // Handle NULL frees.
    if (to_free == NULLPTR) {
        return;
    }
    recomp::free(rdram, TO_PTR(void, to_free));
}

// Where the heap starts in RDRAM once initialized (above the mods loaded at
// start): what a game's own allocator may treat as the mods' data end.
uint32_t recomp::heap_rdram_address() {
    return 0x80000000u + heap_offset;
}

void recomp::register_heap_exports() {
    recomp::overlays::register_base_export("recomp_alloc", recomp_alloc);
    recomp::overlays::register_base_export("recomp_free", recomp_free);
}

void recomp::init_heap(uint8_t* rdram, uint32_t address) {
    // Align the heap start to 16 bytes.
    address = (address + 15U) & ~15U;

    // Calculate the offset of the heap from the start of rdram and the size of the heap.
    heap_offset = address - 0x80000000U;
    assert(heap_offset < recomp::mem_size && "recomp heap starts outside RDRAM");
    size_t heap_size = recomp::mem_size - heap_offset;

    printf("Initializing recomp heap at offset 0x%08X with size 0x%08X\n", heap_offset, static_cast<uint32_t>(heap_size));

    // o1heap refuses an arena that isn't aligned to O1HEAP_ALIGNMENT; every
    // allocation would then return NULL.
    if (o1heapInit(&rdram[heap_offset], heap_size) == nullptr) {
        ultramodern::error_handling::message_box("Failed to initialize the recomp heap (misaligned RDRAM?).");
        ULTRAMODERN_QUICK_EXIT();
    }
}

void* recomp::alloc(uint8_t* rdram, size_t size) {
    void* ret = o1heapAllocate(get_heap(rdram), size);
    if (ret == nullptr && size != 0) {
        // Worth a line in the log: a caller that uses the block unchecked
        // writes through a null pointer next.
        const O1HeapDiagnostics d = o1heapGetDiagnostics(get_heap(rdram));
        fprintf(stderr, "recomp heap: %u bytes not available, %u of %u in use (peak %u)\n", (unsigned)size,
                (unsigned)d.allocated, (unsigned)d.capacity, (unsigned)d.peak_allocated);
    }
    return ret;
}

void recomp::free(uint8_t* rdram, void* mem) {
    o1heapFree(get_heap(rdram), mem);
}
