#pragma once

#include <cstdint>
#include <cstdio>
#include "defines.h"

class Core;

// ---------------------------------------------------------------------------
// Byte-reversed load/store intrinsics (PowerPC Broadway / PPC750)
// ---------------------------------------------------------------------------
FORCE_INLINE uint32_t load32_le(const void* ptr) {
    uint32_t val;
    // lwbrx: Load Word Byte-Reversed Indexed (EA = rA|0 + rB)
    asm volatile("lwbrx %0, 0, %1" : "=r"(val) : "r"(ptr) : "memory");
    return val;
}
FORCE_INLINE uint16_t load16_le(const void* ptr) {
    uint16_t val;
    asm volatile("lhbrx %0, 0, %1" : "=r"(val) : "r"(ptr) : "memory");
    return val;
}
FORCE_INLINE uint8_t load8_le(const void* ptr) {
    return *static_cast<const uint8_t*>(ptr);
}

FORCE_INLINE void store32_le(void* ptr, uint32_t val) {
    asm volatile("stwbrx %1, 0, %0" : : "r"(ptr), "r"(val) : "memory");
}
FORCE_INLINE void store16_le(void* ptr, uint16_t val) {
    asm volatile("sthbrx %1, 0, %0" : : "r"(ptr), "r"(val) : "memory");
}
FORCE_INLINE void store8_le(void* ptr, uint8_t val) {
    *static_cast<uint8_t*>(ptr) = val;
}

// ---------------------------------------------------------------------------
// VRAM Mapping — optimized for single-mapping case (count == 1)
// ---------------------------------------------------------------------------
struct VramMapping {
    uint8_t* single = nullptr;      // Fast path: directly mapped block
    uint8_t* multi[6] = {};         // Slow path: overlapping mappings (capture)
    uint8_t  count = 0;

    FORCE_INLINE void add(uint8_t* mapping) {
        if (count == 0) single = mapping;
        else {
            if (count == 1) multi[0] = single;
            multi[count - 1] = mapping;
        }
        count++;
    }

    template <typename T>
    FORCE_INLINE T read(uint32_t address) const {
        if (__builtin_expect(count == 1, 1)) {
            // Single mapping: direct load
            const uint8_t* ptr = single + address;
            if constexpr (sizeof(T) == 1) return load8_le(ptr);
            if constexpr (sizeof(T) == 2) return load16_le(ptr);
            return load32_le(ptr);
        }
        // Overlapping: OR all mappings (rare)
        T value = 0;
        for (uint8_t m = 0; m < count; m++) {
            const uint8_t* ptr = multi[m] + address;
            if constexpr (sizeof(T) == 1) value |= load8_le(ptr);
            else if constexpr (sizeof(T) == 2) value |= load16_le(ptr);
            else value |= load32_le(ptr);
        }
        return value;
    }

    template <typename T>
    FORCE_INLINE void write(uint32_t address, T value) {
        if (__builtin_expect(count == 1, 1)) {
            uint8_t* ptr = single + address;
            if constexpr (sizeof(T) == 1) store8_le(ptr, value);
            else if constexpr (sizeof(T) == 2) store16_le(ptr, value);
            else store32_le(ptr, value);
            return;
        }
        for (uint8_t m = 0; m < count; m++) {
            uint8_t* ptr = multi[m] + address;
            if constexpr (sizeof(T) == 1) store8_le(ptr, value);
            else if constexpr (sizeof(T) == 2) store16_le(ptr, value);
            else store32_le(ptr, value);
        }
    }
};

class Memory {
public:
    // 32-bit address space, split into 4KB blocks (1M entries = 4MB per map)
    // Total 6 maps = 24MB in MEM2 (allocated via Core::operator new)
    uint8_t* __restrict readMap9A[0x100000] = {};
    uint8_t* __restrict readMap9B[0x100000] = {};
    uint8_t* __restrict readMap7 [0x100000] = {};
    uint8_t* __restrict writeMap9A[0x100000] = {};
    uint8_t* __restrict writeMap9B[0x100000] = {};
    uint8_t* __restrict writeMap7 [0x100000] = {};

    // On-chip memories (aligned for cache-line ops)
    alignas(32) uint8_t palette[0x800] = {};       // 2KB
    alignas(32) uint8_t oam[0x800] = {};           // 2KB
    uint8_t* engAExtPal[5] = {};
    uint8_t* engBExtPal[5] = {};
    uint8_t* tex3D[4] = {};
    uint8_t* pal3D[6] = {};

    Memory(Core* core) : core(core) {}
    void saveState(FILE* file) __attribute__((cold));
    void loadState(FILE* file) __attribute__((cold));

    bool loadBios9() __attribute__((cold));
    bool loadBios7() __attribute__((cold));
    bool loadGbaBios() __attribute__((cold));
    void copyBiosLogo(uint8_t* logo) __attribute__((cold));

    void updateMap9(uint32_t start, uint32_t end, bool tcm = false) __attribute__((hot));
    void updateMap7(uint32_t start, uint32_t end) __attribute__((hot));
    void updateVram() __attribute__((cold));

    // Hot path: memory read/write
    template <typename T> FORCE_INLINE T read(bool arm7, uint32_t address, bool tcm = true) __attribute__((hot));
    template <typename T> FORCE_INLINE void write(bool arm7, uint32_t address, T value, bool tcm = true) __attribute__((hot));

private:
    Core* __restrict core;
    uint32_t gbaBiosAddr = 0;

    // Main memories (allocated in Core ctor via MEM2, aligned)
    alignas(32) uint8_t ram[0x1000000] = {};        // 16MB Main RAM
    alignas(32) uint8_t wram[0x8000] = {};          // 32KB Shared WRAM
    alignas(32) uint8_t instrTcm[0x8000] = {};      // 32KB I-TCM
    alignas(32) uint8_t dataTcm[0x4000] = {};       // 16KB D-TCM
    alignas(32) uint8_t wram7[0x10000] = {};        // 64KB ARM7 WRAM
    alignas(32) uint8_t wifiRam[0x2000] = {};       // 8KB WiFi RAM

    // VRAM Blocks
    alignas(32) uint8_t vramA[0x20000] = {};
    alignas(32) uint8_t vramB[0x20000] = {};
    alignas(32) uint8_t vramC[0x20000] = {};
    alignas(32) uint8_t vramD[0x20000] = {};
    alignas(32) uint8_t vramE[0x10000] = {};
    alignas(32) uint8_t vramF[0x4000] = {};
    alignas(32) uint8_t vramG[0x4000] = {};
    alignas(32) uint8_t vramH[0x8000] = {};
    alignas(32) uint8_t vramI[0x4000] = {};

    // BIOS (small, stay in class)
    uint8_t bios9[0x8000] = {};
    uint8_t bios7[0x4000] = {};
    uint8_t gbaBios[0x4000] = {};

    // VRAM Mappings
    VramMapping engABg[32];
    VramMapping engBBg[8];
    VramMapping engAObj[16];
    VramMapping engBObj[8];
    VramMapping lcdc[64];
    VramMapping vram7[2];

    uint32_t dmaFill[4] = {};
    uint8_t vramCnt[9] = {};
    uint8_t vramStat = 0;
    uint8_t wramCnt = 0;
    uint8_t haltCnt = 0;

    // Cold fallbacks
    template <typename T> T readFallback(bool arm7, uint32_t address) __attribute__((cold));
    template <typename T> void writeFallback(bool arm7, uint32_t address, T value) __attribute__((cold));

    // I/O Handlers (hot)
    template <typename T> T ioRead9(uint32_t address) __attribute__((hot));
    template <typename T> T ioRead7(uint32_t address) __attribute__((hot));
    template <typename T> T ioReadGba(uint32_t address) __attribute__((hot));
    template <typename T> void ioWrite9(uint32_t address, T value) __attribute__((hot));
    template <typename T> void ioWrite7(uint32_t address, T value) __attribute__((hot));
    template <typename T> void ioWriteGba(uint32_t address, T value) __attribute__((hot));

    // Register accessors
    FORCE_INLINE uint32_t readDmaFill(int ch) { return dmaFill[ch]; }
    FORCE_INLINE uint8_t readVramCnt(int blk) { return vramCnt[blk]; }
    FORCE_INLINE uint8_t readVramStat() { return vramStat; }
    FORCE_INLINE uint8_t readWramCnt() { return wramCnt; }
    FORCE_INLINE uint8_t readHaltCnt() { return haltCnt; }

    void writeDmaFill(int ch, uint32_t mask, uint32_t val) __attribute__((hot));
    void writeVramCnt(int idx, uint8_t val) __attribute__((hot));
    void writeWramCnt(uint8_t val) __attribute__((hot));
    void writeHaltCnt(uint8_t val) __attribute__((hot));
    void writeGbaHaltCnt(uint8_t val) __attribute__((hot));
};

// ---------------------------------------------------------------------------
// Explicit Instantiation & Fast Path Implementation
// ---------------------------------------------------------------------------
template <> FORCE_INLINE uint8_t  Memory::read(bool arm7, uint32_t addr, bool tcm) {
    uint8_t** map = arm7 ? readMap7 : (tcm ? readMap9A : readMap9B);
    if (uint8_t* data = map[addr >> 12]) return load8_le(data + (addr & 0xFFF));
    return readFallback<uint8_t>(arm7, addr);
}
template <> FORCE_INLINE uint16_t Memory::read(bool arm7, uint32_t addr, bool tcm) {
    uint8_t** map = arm7 ? readMap7 : (tcm ? readMap9A : readMap9B);
    if (uint8_t* data = map[addr >> 12]) return load16_le(data + (addr & 0xFFE));
    return readFallback<uint16_t>(arm7, addr);
}
template <> FORCE_INLINE uint32_t Memory::read(bool arm7, uint32_t addr, bool tcm) {
    uint8_t** map = arm7 ? readMap7 : (tcm ? readMap9A : readMap9B);
    if (uint8_t* data = map[addr >> 12]) return load32_le(data + (addr & 0xFFC));
    return readFallback<uint32_t>(arm7, addr);
}

template <> FORCE_INLINE void Memory::write(bool arm7, uint32_t addr, uint8_t val, bool tcm) {
    uint8_t** map = arm7 ? writeMap7 : (tcm ? writeMap9A : writeMap9B);
    if (uint8_t* data = map[addr >> 12]) { store8_le(data + (addr & 0xFFF), val); return; }
    writeFallback(arm7, addr, val);
}
template <> FORCE_INLINE void Memory::write(bool arm7, uint32_t addr, uint16_t val, bool tcm) {
    uint8_t** map = arm7 ? writeMap7 : (tcm ? writeMap9A : writeMap9B);
    if (uint8_t* data = map[addr >> 12]) { store16_le(data + (addr & 0xFFE), val); return; }
    writeFallback(arm7, addr, val);
}
template <> FORCE_INLINE void Memory::write(bool arm7, uint32_t addr, uint32_t val, bool tcm) {
    uint8_t** map = arm7 ? writeMap7 : (tcm ? writeMap9A : writeMap9B);
    if (uint8_t* data = map[addr >> 12]) { store32_le(data + (addr & 0xFFC), val); return; }
    writeFallback(arm7, addr, val);
}
