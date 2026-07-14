//core.h (optimized)
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

extern "C" {
    #include <tuxedo/thread.h>
    #include <tuxedo/tick.h>
    #include <tuxedo/ppc/intrinsics.h>
    #include <tuxedo/ppc/clock.h>
}

#include "action_replay.h"
#include "cartridge.h"
#include "cp15.h"
#include "defines.h"
#include "div_sqrt.h"
#include "dldi.h"
#include "dma.h"
#include "gpu.h"
#include "gpu_2d.h"
#include "gpu_3d.h"
#include "gpu_3d_renderer.h"
#include "hle_arm7.h"
#include "hle_bios.h"
#include "input.h"
#include "interpreter.h"
#include "ipc.h"
#include "memory.h"
#include "rtc.h"
#include "save_states.h"
#include "settings.h"
#include "spi.h"
#include "spu.h"
#include "timers.h"
#include "wifi.h"

// PowerPC optimization macros
#define PPC_LIKELY(x)    __builtin_expect(!!(x), 1)
#define PPC_UNLIKELY(x)  __builtin_expect(!!(x), 0)
#define ALWAYS_INLINE    __attribute__((always_inline)) inline
#define HOT              __attribute__((hot))
#define COLD             __attribute__((cold))
// Correct alignment attribute: applies to the variable, not array elements
#define ALIGNED(n)       __attribute__((aligned(n)))

enum CoreError {
    ERROR_BIOS,
    ERROR_FIRM,
    ERROR_ROM
};

enum SchedTask {
    UPDATE_RUN,
    RESET_CYCLES,
    CART9_WORD_READY,
    CART7_WORD_READY,
    DMA9_TRANSFER0,
    DMA9_TRANSFER1,
    DMA9_TRANSFER2,
    DMA9_TRANSFER3,
    DMA7_TRANSFER0,
    DMA7_TRANSFER1,
    DMA7_TRANSFER2,
    DMA7_TRANSFER3,
    NDS_SCANLINE256,
    NDS_SCANLINE355,
    GBA_SCANLINE240,
    GBA_SCANLINE308,
    GPU3D_COMMANDS,
    ARM9_INTERRUPT,
    ARM7_INTERRUPT,
    NDS_SPU_SAMPLE,
    GBA_SPU_SAMPLE,
    TIMER9_OVERFLOW0,
    TIMER9_OVERFLOW1,
    TIMER9_OVERFLOW2,
    TIMER9_OVERFLOW3,
    TIMER7_OVERFLOW0,
    TIMER7_OVERFLOW1,
    TIMER7_OVERFLOW2,
    TIMER7_OVERFLOW3,
    WIFI_COUNT_MS,
    WIFI_TRANS_REPLY,
    WIFI_TRANS_ACK,
    MAX_TASKS
};

struct SchedEvent {
    uint32_t  cycles;  // Put cycles first for faster comparisons (cache line)
    SchedTask task;

    SchedEvent(SchedTask task, uint32_t cycles): cycles(cycles), task(task) {}

    // Compare only cycles for sorted insertion
    bool operator<(const SchedEvent& e) const { return cycles < e.cycles; }
};

class Core {
public:
    void* operator new  (size_t size);
    void  operator delete  (void* p) noexcept;
    void* operator new[](size_t size);
    void  operator delete[](void* p) noexcept;

    int  id      = 0;
    int  fps     = 0;
    bool arm7Hle = false;
    bool dsiMode = false;
    bool gbaMode = false;

    ActionReplay    actionReplay;
    CartridgeGba    cartridgeGba;
    CartridgeNds    cartridgeNds;
    Cp15            cp15;
    DivSqrt         divSqrt;
    Dldi            dldi;
    Dma             dma[2];
    Gpu             gpu;
    Gpu2D           gpu2D[2];
    Gpu3D           gpu3D;
    Gpu3DRenderer   gpu3DRenderer;
    HleArm7         hleArm7;
    HleBios         hleBios[3];
    Input           input;
    Interpreter     interpreter[2];
    Ipc             ipc;
    Memory          memory;
    Rtc             rtc;
    SaveStates      saveStates;
    Spi             spi;
    Spu             spu;
    Timers          timers[2];
    Wifi            wifi;

    // volatile prevents compiler from caching; guarded by PPCIrqLockByMsr for writes
    volatile uint8_t running;

    // Fixed-capacity event queue: DS never needs more than MAX_TASKS simultaneous events
    // Use a small sorted array instead of std::vector to avoid heap allocation and
    // improve cache locality. MAX_TASKS = 32 => 32 * 8 bytes = 256 bytes
    static constexpr int MAX_EVENTS = MAX_TASKS;
    SchedEvent eventsArr[MAX_EVENTS] = { SchedEvent(MAX_TASKS, 0) };
    int        eventsCount = 0;

    // Keep std::vector for compatibility with existing code that uses events[]
    // but alias through the fixed array for the hot path
    std::vector<SchedEvent> events;

    std::function<void()>   tasks[MAX_TASKS];
    uint32_t                globalCycles = 0;

    Core(std::string ndsRom = "", std::string gbaRom = "", int id = 0,
         int ndsRomFd  = -1, int gbaRomFd  = -1,
         int ndsSaveFd = -1, int gbaSaveFd = -1,
         int ndsStateFd = -1, int gbaStateFd = -1,
         int ndsCheatFd = -1);

    void saveState(FILE* file);
    void loadState(FILE* file);

    void runCore() { (*runFunc)(*this); }
    HOT void schedule(SchedTask task, uint32_t cycles);
    void enterGbaMode();
    void endFrame();

private:
    bool realGbaBios;
    void (*runFunc)(Core&) = &Interpreter::runCoreNds;

    uint64_t lastFpsTimeTicks = 0;
    int      fpsCount         = 0;

    void updateRun();
    void resetCycles();
};
