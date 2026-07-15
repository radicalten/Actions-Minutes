#pragma once

#include <cstdint>
#include <cstring>
#include "defines.h"

// PowerPC byte-swap intrinsics (devkitPPC / GCC)
#ifdef __wii__
#  define LIKELY(x)   __builtin_expect(!!(x), 1)
#  define UNLIKELY(x) __builtin_expect(!!(x), 0)
#  define FORCE_INLINE __attribute__((always_inline)) inline
#  include <stdint.h>
// lwbrx / stwbrx equivalents via GCC builtins
static FORCE_INLINE uint16_t bswap16(uint16_t x) {
    return __builtin_bswap16(x);
}
static FORCE_INLINE uint32_t bswap32(uint32_t x) {
    return __builtin_bswap32(x);
}
#else
static FORCE_INLINE uint16_t bswap16(uint16_t x) { return (x>>8)|(x<<8); }
static FORCE_INLINE uint32_t bswap32(uint32_t x) { return __builtin_bswap32(x); }
#endif

class Core;

// Reads T from a little-endian byte buffer — PowerPC optimized
template<typename T>
static FORCE_INLINE T readLE(const uint8_t *p);

template<> FORCE_INLINE uint8_t readLE<uint8_t>(const uint8_t *p) {
    return *p;
}
template<> FORCE_INLINE uint16_t readLE<uint16_t>(const uint8_t *p) {
    uint16_t v;
    __builtin_memcpy(&v, p, 2);
    return bswap16(v); // PPC is BE, NDS data is LE
}
template<> FORCE_INLINE uint32_t readLE<uint32_t>(const uint8_t *p) {
    uint32_t v;
    __builtin_memcpy(&v, p, 4);
    return bswap32(v);
}

// Writes T to a little-endian byte buffer — PowerPC optimized
template<typename T>
static FORCE_INLINE void writeLE(uint8_t *p, T value);

template<> FORCE_INLINE void writeLE<uint8_t>(uint8_t *p, uint8_t v) {
    *p = v;
}
template<> FORCE_INLINE void writeLE<uint16_t>(uint8_t *p, uint16_t v) {
    v = bswap16(v);
    __builtin_memcpy(p, &v, 2);
}
template<> FORCE_INLINE void writeLE<uint32_t>(uint8_t *p, uint32_t v) {
    v = bswap32(v);
    __builtin_memcpy(p, &v, 4);
}

struct VramMapping {
    uint8_t *mappings[7] = {};
    uint8_t count = 0;

    FORCE_INLINE void add(uint8_t *mapping) {
        mappings[count++] = mapping;
    }

    template <typename T> FORCE_INLINE T read(uint32_t address) const;
    template <typename T> FORCE_INLINE void write(uint32_t address, T value);
};

template<> FORCE_INLINE uint8_t VramMapping::read<uint8_t>(uint32_t address) const {
    uint8_t value = 0;
    for (uint8_t m = 0; m < count; m++)
        value |= mappings[m][address];
    return value;
}
template<> FORCE_INLINE uint16_t VramMapping::read<uint16_t>(uint32_t address) const {
    uint16_t value = 0;
    for (uint8_t m = 0; m < count; m++)
        value |= readLE<uint16_t>(mappings[m] + address);
    return value;
}
template<> FORCE_INLINE uint32_t VramMapping::read<uint32_t>(uint32_t address) const {
    uint32_t value = 0;
    for (uint8_t m = 0; m < count; m++)
        value |= readLE<uint32_t>(mappings[m] + address);
    return value;
}

template<> FORCE_INLINE void VramMapping::write<uint8_t>(uint32_t address, uint8_t value) {
    for (uint8_t m = 0; m < count; m++)
        mappings[m][address] = value;
}
template<> FORCE_INLINE void VramMapping::write<uint16_t>(uint32_t address, uint16_t value) {
    for (uint8_t m = 0; m < count; m++)
        writeLE<uint16_t>(mappings[m] + address, value);
}
template<> FORCE_INLINE void VramMapping::write<uint32_t>(uint32_t address, uint32_t value) {
    for (uint8_t m = 0; m < count; m++)
        writeLE<uint32_t>(mappings[m] + address, value);
}

class Memory {
public:
    // 32-bit address space split into 4KB blocks
    // Use uint32_t offsets instead of pointers to halve cache footprint on 32-bit PPC
    // NULL-equivalent sentinel: 0xFFFFFFFF
    static constexpr uint32_t INVALID_OFFSET = 0xFFFFFFFFu;

    // Raw base pointers for each region — indexed by map entries
    // Maps store byte offsets from base; base is chosen per-region
    // For simplicity keep pointer maps but mark them with FORCE_INLINE accessors

    uint8_t *readMap9A[0x100000];
    uint8_t *readMap9B[0x100000];
    uint8_t *readMap7[0x100000];
    uint8_t *writeMap9A[0x100000];
    uint8_t *writeMap9B[0x100000];
    uint8_t *writeMap7[0x100000];

    uint8_t palette[0x800];
    uint8_t oam[0x800];
    uint8_t *engAExtPal[5];
    uint8_t *engBExtPal[5];
    uint8_t *tex3D[4];
    uint8_t *pal3D[6];

    Memory(Core *core): core(core) {
        // Zero-initialize all maps
        __builtin_memset(readMap9A,  0, sizeof(readMap9A));
        __builtin_memset(readMap9B,  0, sizeof(readMap9B));
        __builtin_memset(readMap7,   0, sizeof(readMap7));
        __builtin_memset(writeMap9A, 0, sizeof(writeMap9A));
        __builtin_memset(writeMap9B, 0, sizeof(writeMap9B));
        __builtin_memset(writeMap7,  0, sizeof(writeMap7));
        __builtin_memset(palette,    0, sizeof(palette));
        __builtin_memset(oam,        0, sizeof(oam));
        __builtin_memset(engAExtPal, 0, sizeof(engAExtPal));
        __builtin_memset(engBExtPal, 0, sizeof(engBExtPal));
        __builtin_memset(tex3D,      0, sizeof(tex3D));
        __builtin_memset(pal3D,      0, sizeof(pal3D));
    }

    void saveState(FILE *file);
    void loadState(FILE *file);

    bool loadBios9();
    bool loadBios7();
    bool loadGbaBios();
    void copyBiosLogo(uint8_t *logo);

    void updateMap9(uint32_t start, uint32_t end, bool tcm = false);
    void updateMap7(uint32_t start, uint32_t end);
    void updateVram();

    // Hot path: inline read with bswap instead of byte loop
    template <typename T> FORCE_INLINE T read(bool arm7, uint32_t address, bool tcm = true) {
        uint8_t **map = arm7 ? readMap7 : (tcm ? readMap9A : readMap9B);
        uint8_t *data = map[address >> 12];
        if (LIKELY(data != nullptr)) {
            // Align within the 4KB page (mask off size-1 bits)
            data += address & (0xFFFu & ~(uint32_t)(sizeof(T) - 1));
            return readLE<T>(data);
        }
        return readFallback<T>(arm7, address);
    }

    template <typename T> FORCE_INLINE void write(bool arm7, uint32_t address, T value, bool tcm = true) {
        uint8_t **map = arm7 ? writeMap7 : (tcm ? writeMap9A : writeMap9B);
        uint8_t *data = map[address >> 12];
        if (LIKELY(data != nullptr)) {
            data += address & (0xFFFu & ~(uint32_t)(sizeof(T) - 1));
            writeLE<T>(data, value);
            return;
        }
        writeFallback<T>(arm7, address, value);
    }

private:
    Core *core;
    uint32_t gbaBiosAddr = 0;

    // Keep as zero-initialized arrays
    uint8_t bios9[0x8000]    = {};
    uint8_t bios7[0x4000]    = {};
    uint8_t gbaBios[0x4000]  = {};
    uint8_t ram[0x1000000]   = {};
    uint8_t wram[0x8000]     = {};
    uint8_t instrTcm[0x8000] = {};
    uint8_t dataTcm[0x4000]  = {};
    uint8_t wram7[0x10000]   = {};
    uint8_t wifiRam[0x2000]  = {};

    uint8_t vramA[0x20000] = {};
    uint8_t vramB[0x20000] = {};
    uint8_t vramC[0x20000] = {};
    uint8_t vramD[0x20000] = {};
    uint8_t vramE[0x10000] = {};
    uint8_t vramF[0x4000]  = {};
    uint8_t vramG[0x4000]  = {};
    uint8_t vramH[0x8000]  = {};
    uint8_t vramI[0x4000]  = {};

    VramMapping engABg[32];
    VramMapping engBBg[8];
    VramMapping engAObj[16];
    VramMapping engBObj[8];
    VramMapping lcdc[64];
    VramMapping vram7[2];

    uint32_t dmaFill[4]  = {};
    uint8_t  vramCnt[9]  = {};
    uint8_t  vramStat    = 0;
    uint8_t  wramCnt     = 0;
    uint8_t  haltCnt     = 0;

    template <typename T> T readFallback(bool arm7, uint32_t address);
    template <typename T> void writeFallback(bool arm7, uint32_t address, T value);

    template <typename T> T ioRead9(uint32_t address);
    template <typename T> T ioRead7(uint32_t address);
    template <typename T> T ioReadGba(uint32_t address);
    template <typename T> void ioWrite9(uint32_t address, T value);
    template <typename T> void ioWrite7(uint32_t address, T value);
    template <typename T> void ioWriteGba(uint32_t address, T value);

    uint32_t readDmaFill(int channel)  const { return dmaFill[channel]; }
    uint8_t  readVramCnt(int block)    const { return vramCnt[block]; }
    uint8_t  readVramStat()            const { return vramStat; }
    uint8_t  readWramCnt()             const { return wramCnt; }
    uint8_t  readHaltCnt()             const { return haltCnt; }

    void writeDmaFill(int channel, uint32_t mask, uint32_t value);
    void writeVramCnt(int index, uint8_t value);
    void writeWramCnt(uint8_t value);
    void writeHaltCnt(uint8_t value);
    void writeGbaHaltCnt(uint8_t value);

    // Helper: get VRAM mapping for ARM9 VRAM region
    FORCE_INLINE VramMapping* getVramMapping9(uint32_t address) {
        switch (address & 0xFFE00000) {
            case 0x6000000: return &engABg [(address & 0x7FFFFu) >> 14];
            case 0x6200000: return &engBBg [(address & 0x1FFFFu) >> 14];
            case 0x6400000: return &engAObj[(address & 0x3FFFFu) >> 14];
            case 0x6600000: return &engBObj[(address & 0x1FFFFu) >> 14];
            default:        return &lcdc   [(address & 0xFFFFFu) >> 14];
        }
    }
};

// Explicit instantiation declarations
extern template uint8_t  Memory::read<uint8_t> (bool, uint32_t, bool);
extern template uint16_t Memory::read<uint16_t>(bool, uint32_t, bool);
extern template uint32_t Memory::read<uint32_t>(bool, uint32_t, bool);
extern template void Memory::write<uint8_t> (bool, uint32_t, uint8_t,  bool);
extern template void Memory::write<uint16_t>(bool, uint32_t, uint16_t, bool);
extern template void Memory::write<uint32_t>(bool, uint32_t, uint32_t, bool);
