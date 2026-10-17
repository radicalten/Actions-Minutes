// host_env.hpp -- a stand-in for the NooDS interpreter's data structures, so the
// emitted PowerPC blocks can be built and *executed natively* on a PowerPC host
// (cross-compiled and run under qemu-ppc for testing).
//
// Layout rules that matter:
//   * registers[] is an array of real uint32_t* -- on PowerPC32 a pointer is 4
//     bytes, exactly like on the Wii, so unlike an x86 host the pointers here can
//     be dereferenced by the emitted code directly.
//   * the guest register file is little-endian in memory, like the GBA/NDS RAM on
//     the real console; the JIT reaches it with lwbrx/stwbrx, so the arena holds
//     plain little-endian bytes and the page map points into it.
//   * the code arena is mmap'd RWX and flushed with __builtin___clear_cache.
#ifndef JIT3_HOST_ENV_HPP
#define JIT3_HOST_ENV_HPP

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

namespace jit3host {

static constexpr uint32_t GUEST_BASE = 0x02000000;   // ARM9 main RAM
static constexpr uint32_t ARENA_SIZE = 0x00100000;   // 1 MiB of guest RAM
static constexpr uint32_t PAGE_SHIFT = 12;
static constexpr uint32_t MAP_ENTRIES = 0x100000;    // same size as NooDS readMap9A

// Stands in for Interpreter: the Cfg offsets are taken with offsetof() on this.
struct HostCpu {
    uint32_t  registersUsr[16];      // current-mode register file
    uint32_t  registersFiq[8];       // bank stand-in for r8..r12 in FIQ mode
    uint32_t* registers[32];         // pointer array the JIT keeps hot registers through
    uint32_t  cpsr;
    uint32_t  halted;
    uint32_t  cycles;                // spin counter used by the delegate test
    uint8_t** readMap;               // page -> host pointer (slice 2)
    uint8_t** writeMap;
};

struct Env {
    HostCpu  cpu;
    uint8_t* ram = nullptr;          // guest RAM, little-endian
    uint8_t** readMap = nullptr;
    uint8_t** writeMap = nullptr;
    uint32_t* code = nullptr;        // RWX code arena
    size_t   codeSize = 0;

    bool init(size_t codeBytes = 1 << 20) {
        ram = (uint8_t*)mmap(nullptr, ARENA_SIZE, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        readMap  = (uint8_t**)mmap(nullptr, (size_t)MAP_ENTRIES * sizeof(uint8_t*),
                                   PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        writeMap = (uint8_t**)mmap(nullptr, (size_t)MAP_ENTRIES * sizeof(uint8_t*),
                                   PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        code = (uint32_t*)mmap(nullptr, codeBytes, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ram == MAP_FAILED || readMap == MAP_FAILED || writeMap == MAP_FAILED || code == MAP_FAILED)
            return false;
        codeSize = codeBytes;
        // Every guest page inside the arena is mapped (main RAM); everything else is
        // left null so the JIT's slow path is the one that has to cope with it.
        for (uint32_t page = 0; page < ARENA_SIZE >> PAGE_SHIFT; page++) {
            uint8_t* p = ram + (page << PAGE_SHIFT);
            readMap[(GUEST_BASE >> PAGE_SHIFT) + page] = p;
            writeMap[(GUEST_BASE >> PAGE_SHIFT) + page] = p;
        }
        cpu.cpsr = 0x00000010;                       // System mode, ARM
        cpu.halted = 0;
        cpu.readMap = readMap;
        cpu.writeMap = writeMap;
        for (int i = 0; i < 32; i++) cpu.registers[i] = &cpu.registersUsr[i];
        for (int i = 8; i <= 12; i++) cpu.registers[i] = &cpu.registersFiq[i - 8];
        return true;
    }

    // guest (little-endian) accessors
    uint32_t rd32(uint32_t a) const {
        const uint8_t* p = ram + (a - GUEST_BASE);
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    void wr32(uint32_t a, uint32_t v) {
        uint8_t* p = ram + (a - GUEST_BASE);
        p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
    }
    uint16_t rd16(uint32_t a) const { return (uint16_t)(rd32(a & ~3u) >> ((a & 2) * 8)); }
    void wr16(uint32_t a, uint16_t v) {
        uint32_t w = rd32(a & ~3u);
        uint32_t sh = (a & 2) * 8;
        w = (w & ~(0xFFFFu << sh)) | ((uint32_t)v << sh);
        wr32(a & ~3u, w);
    }
};

// The block ABI: cycles in r3, exit PC in r4.  On PowerPC32 SysV that is exactly
// how a 64-bit return value is passed (high word in r3, low word in r4), so the
// test can call the block straight through a uint64_t function pointer.
typedef uint64_t (*JitFn)(void* interp, uint32_t deadline);

struct BlockResult { uint32_t cycles, nextPc; };

inline BlockResult runBlock(Env& env, const uint32_t* words, uint32_t count,
                           uint32_t deadline = 1200) {
    std::memcpy(env.code, words, count * 4);
    __builtin___clear_cache((char*)env.code, (char*)env.code + count * 4);
    JitFn fn = (JitFn)env.code;
    uint64_t r = fn(&env.cpu, deadline);
    BlockResult out;
    out.cycles = (uint32_t)(r >> 32);
    out.nextPc = (uint32_t)r;
    return out;
}

}  // namespace jit3host

#endif
