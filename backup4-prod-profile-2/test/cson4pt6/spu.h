#pragma once

#include <cstdint>
#include <cstdio>

extern "C" {
    #include <tuxedo/thread.h>
}

class Core;

// ---------------------------------------------------------------------------
// Fixed-size ring buffer — replaces std::deque for GBA FIFOs.
// Avoids heap allocation and pointer chasing entirely.
// ---------------------------------------------------------------------------
template<typename T, int CAP>
struct RingBuf {
    T    data[CAP];
    int  head = 0;   // read index
    int  tail = 0;   // write index
    int  count = 0;

    void clear()           { head = tail = count = 0; }
    bool empty()     const { return count == 0; }
    int  size()      const { return count; }
    bool full()      const { return count == CAP; }

    void push_back(T v) {
        if (count < CAP) {
            data[tail] = v;
            tail = (tail + 1 == CAP) ? 0 : tail + 1;
            ++count;
        }
    }

    T front() const { return data[head]; }

    void pop_front() {
        if (count > 0) {
            head = (head + 1 == CAP) ? 0 : head + 1;
            --count;
        }
    }
};

class Spu {
public:
    explicit Spu(Core *core);
    ~Spu();

    void saveState(FILE *file);
    void loadState(FILE *file);

    bool getSamples(uint32_t *dst, int count);

    void runGbaSample();
    void runSample();
    void gbaFifoTimer(int timer);

    // GBA register accessors
    uint8_t  readGbaSoundCntL(int channel);
    uint16_t readGbaSoundCntH(int channel);
    uint16_t readGbaSoundCntX(int channel);
    uint16_t readGbaMainSoundCntL()  { return gbaMainSoundCntL; }
    uint16_t readGbaMainSoundCntH()  { return gbaMainSoundCntH; }
    uint8_t  readGbaMainSoundCntX()  { return gbaMainSoundCntX; }
    uint16_t readGbaSoundBias()      { return gbaSoundBias;      }
    uint8_t  readGbaWaveRam(int index);

    // NDS register accessors
    uint32_t readSoundCnt(int channel)    { return soundCnt[channel]; }
    uint16_t readMainSoundCnt()           { return mainSoundCnt;      }
    uint16_t readSoundBias()              { return soundBias;          }
    uint8_t  readSndCapCnt(int channel)   { return sndCapCnt[channel]; }
    uint32_t readSndCapDad(int channel)   { return sndCapDad[channel]; }

    // GBA register writers
    void writeGbaSoundCntL(int channel, uint8_t value);
    void writeGbaSoundCntH(int channel, uint16_t mask, uint16_t value);
    void writeGbaSoundCntX(int channel, uint16_t mask, uint16_t value);
    void writeGbaMainSoundCntL(uint16_t mask, uint16_t value);
    void writeGbaMainSoundCntH(uint16_t mask, uint16_t value);
    void writeGbaMainSoundCntX(uint8_t value);
    void writeGbaSoundBias(uint16_t mask, uint16_t value);
    void writeGbaWaveRam(int index, uint8_t value);
    void writeGbaFifoA(uint32_t mask, uint32_t value);
    void writeGbaFifoB(uint32_t mask, uint32_t value);

    // NDS register writers
    void writeSoundCnt(int channel, uint32_t mask, uint32_t value);
    void writeSoundSad(int channel, uint32_t mask, uint32_t value);
    void writeSoundTmr(int channel, uint16_t mask, uint16_t value);
    void writeSoundPnt(int channel, uint16_t mask, uint16_t value);
    void writeSoundLen(int channel, uint32_t mask, uint32_t value);
    void writeMainSoundCnt(uint16_t mask, uint16_t value);
    void writeSoundBias(uint16_t mask, uint16_t value);
    void writeSndCapCnt(int channel, uint8_t value);
    void writeSndCapDad(int channel, uint32_t mask, uint32_t value);
    void writeSndCapLen(int channel, uint16_t mask, uint16_t value);

private:
    Core *core;

    // -----------------------------------------------------------------------
    // Double buffer — 32-byte aligned for cache-line efficiency.
    // Emulator thread writes buffers[writeIdx].
    // Audio  thread reads  buffers[writeIdx ^ 1].
    // -----------------------------------------------------------------------
    static const int SPU_BUF_FRAMES = 548;

    alignas(32) uint32_t buffers[2][SPU_BUF_FRAMES];
    int          writeIdx     = 0;
    uint32_t     bufferPointer = 0;
    volatile bool ready       = false;

    KThrQueue waitQueue1;
    KThrQueue waitQueue2;

    // ---- GBA state ----
    int16_t  gbaFrameSequencer     = 0;
    int32_t  gbaSoundTimers[4]     = {};
    int8_t   gbaEnvelopes[3]       = {};
    int8_t   gbaEnvTimers[3]       = {};
    int8_t   gbaSweepTimer         = 0;
    int8_t   gbaWaveDigit          = 0;
    uint16_t gbaNoiseValue         = 0;

    alignas(4) uint8_t gbaWaveRam[2][16] = {};

    // Fixed-size ring buffers replace std::deque — zero heap allocation.
    RingBuf<int8_t, 32> gbaFifos[2];
    int8_t   gbaSampleA = 0, gbaSampleB = 0;

    uint16_t enabled = 0;

    static const int     indexTable[8];
    static const int16_t adpcmTable[89];

    int32_t adpcmValue[16]     = {};
    int32_t adpcmLoopValue[16] = {};
    int8_t  adpcmIndex[16]     = {};
    int8_t  adpcmLoopIndex[16] = {};
    bool    adpcmToggle[16]    = {};

    uint8_t  dutyCycles[6]     = {};
    uint16_t noiseValues[2]    = {};
    uint32_t soundCurrent[16]  = {};
    uint16_t soundTimers[16]   = {};
    uint32_t sndCapCurrent[2]  = {};
    uint16_t sndCapTimers[2]   = {};

    uint8_t  gbaSoundCntL[2]   = {};
    uint16_t gbaSoundCntH[4]   = {};
    uint16_t gbaSoundCntX[4]   = {};
    uint16_t gbaMainSoundCntL  = 0;
    uint16_t gbaMainSoundCntH  = 0;
    uint8_t  gbaMainSoundCntX  = 0;
    uint16_t gbaSoundBias      = 0;

    uint32_t soundCnt[16]      = {};
    uint32_t soundSad[16]      = {};
    uint16_t soundTmr[16]      = {};
    uint16_t soundPnt[16]      = {};
    uint32_t soundLen[16]      = {};
    uint16_t mainSoundCnt      = 0;
    uint16_t soundBias         = 0;
    uint8_t  sndCapCnt[2]      = {};
    uint32_t sndCapDad[2]      = {};
    uint16_t sndCapLen[2]      = {};

    void pushSample(int16_t sampleLeft, int16_t sampleRight);
    void startChannel(int channel);
};
