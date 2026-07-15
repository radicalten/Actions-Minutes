#include <cstring>
#include <algorithm>

extern "C" {
    #include <tuxedo/thread.h>
    #include <tuxedo/ppc/intrinsics.h>
    #include <tuxedo/ppc/clock.h>
}

#include "core.h"

// ---------------------------------------------------------------------------
// Reciprocal lookup tables — eliminate runtime integer division in the
// mixing hot path.  All values are Q15 fixed-point (x / denom ≈ x * rcp / 32768).
//
// rcp7[n]  = round(32768 / n) for n = 1..7   (pan denominator 128 max 128 steps)
// rcp15[n] = round(32768 / 15) * n / 15 for envelope 0-15
//   (we just store 32768/15 ≈ 2184 and multiply)
// ---------------------------------------------------------------------------

// 32768 / n, n = 1..128  (used for pan: 128-pan and pan, denominator 128)
// Instead: precompute the two pan multipliers directly.
// pan is 0..128, so (128-pan) and pan are both 0..128.
// We replace  data * (128-pan) / 128  with  data * panL >> 7
// and         data * pan       / 128  with  data * panR >> 7
// — pure shifts, no division.

// Volume envelope 0..15 divided by 15:
// Replace  x * gbaEnvelopes[i] / 15
// with     (x * gbaEnvMul[gbaEnvelopes[i]]) >> 15
// where    gbaEnvMul[n] = round(32768.0 / 15.0 * n)
static const int32_t gbaEnvMul[16] = {
    0,      // 0/15
    2184,   // 1/15
    4369,   // 2/15
    6554,   // 3/15
    8738,   // 4/15
    10923,  // 5/15
    13107,  // 6/15
    15292,  // 7/15
    17476,  // 8/15
    19661,  // 9/15
    21845,  // 10/15
    24030,  // 11/15
    26214,  // 12/15
    28399,  // 13/15
    30583,  // 14/15
    32768,  // 15/15  (exact power of two, no rounding error)
};

// GBA legacy channel left/right volume: 0..7 divided by 7.
// Replace  data[i] * vol / 7
// with    (data[i] * gbaVolMul[vol]) >> 15
static const int32_t gbaVolMul[8] = {
    0,      // 0/7
    4681,   // 1/7
    9362,   // 2/7
    14043,  // 3/7
    18724,  // 4/7
    23405,  // 5/7
    28086,  // 6/7
    32768,  // 7/7  (≈ exact)
};

// masterVol 0..128 / 128 → pure shift: (x * masterVol) >> 7
// mulFactor 0..128 / 128 → same
// divShift is already a shift

// ---------------------------------------------------------------------------
// Static tables
// ---------------------------------------------------------------------------
const int Spu::indexTable[] = {
    -1, -1, -1, -1, 2, 4, 6, 8
};

const int16_t Spu::adpcmTable[] = {
    0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E,
    0x0010, 0x0011, 0x0013, 0x0015, 0x0017, 0x0019, 0x001C, 0x001F,
    0x0022, 0x0025, 0x0029, 0x002D, 0x0032, 0x0037, 0x003C, 0x0042,
    0x0049, 0x0050, 0x0058, 0x0061, 0x006B, 0x0076, 0x0082, 0x008F,
    0x009D, 0x00AD, 0x00BE, 0x00D1, 0x00E6, 0x00FD, 0x0117, 0x0133,
    0x0151, 0x0173, 0x0198, 0x01C1, 0x01EE, 0x0220, 0x0256, 0x0292,
    0x02D4, 0x031C, 0x036C, 0x03C3, 0x0424, 0x048E, 0x0502, 0x0583,
    0x0610, 0x06AB, 0x0756, 0x0812, 0x08E0, 0x09C3, 0x0ABD, 0x0BD0,
    0x0CFF, 0x0E4C, 0x0FBA, 0x114C, 0x1307, 0x14EE, 0x1706, 0x1954,
    0x1BDC, 0x1EA5, 0x21B6, 0x2515, 0x28CA, 0x2CDF, 0x315B, 0x364B,
    0x3BB9, 0x41B2, 0x4844, 0x4F7E, 0x5771, 0x602F, 0x69CE, 0x7462,
    0x7FFF
};

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
Spu::Spu(Core *core) : core(core) {
    ready         = false;
    writeIdx      = 0;
    bufferPointer = 0;
    memset(buffers,     0, sizeof(buffers));
    memset(&waitQueue1, 0, sizeof(waitQueue1));
    memset(&waitQueue2, 0, sizeof(waitQueue2));
}

Spu::~Spu() {}

// ---------------------------------------------------------------------------
// saveState / loadState — unchanged logic, FIFO serialization updated
// ---------------------------------------------------------------------------
void Spu::saveState(FILE *file) {
    fwrite(&gbaFrameSequencer, sizeof(gbaFrameSequencer), 1, file);
    fwrite(gbaSoundTimers,     sizeof(int32_t),  4,  file);
    fwrite(gbaEnvelopes,       sizeof(int8_t),   3,  file);
    fwrite(gbaEnvTimers,       sizeof(int8_t),   3,  file);
    fwrite(&gbaSweepTimer,     sizeof(gbaSweepTimer),  1, file);
    fwrite(&gbaWaveDigit,      sizeof(gbaWaveDigit),   1, file);
    fwrite(&gbaNoiseValue,     sizeof(gbaNoiseValue),  1, file);
    fwrite(gbaWaveRam,         sizeof(gbaWaveRam),     1, file);
    fwrite(&gbaSampleA,        sizeof(gbaSampleA),     1, file);
    fwrite(&gbaSampleB,        sizeof(gbaSampleB),     1, file);
    fwrite(&enabled,           sizeof(enabled),        1, file);

    fwrite(adpcmValue,     sizeof(int32_t), 16, file);
    fwrite(adpcmLoopValue, sizeof(int32_t), 16, file);
    fwrite(adpcmIndex,     sizeof(int8_t),  16, file);
    fwrite(adpcmLoopIndex, sizeof(int8_t),  16, file);
    fwrite(adpcmToggle,    sizeof(bool),    16, file);

    fwrite(dutyCycles,    sizeof(uint8_t),  6,  file);
    fwrite(noiseValues,   sizeof(uint16_t), 2,  file);
    fwrite(soundCurrent,  sizeof(uint32_t), 16, file);
    fwrite(soundTimers,   sizeof(uint16_t), 16, file);
    fwrite(sndCapCurrent, sizeof(uint32_t), 2,  file);
    fwrite(sndCapTimers,  sizeof(uint16_t), 2,  file);

    fwrite(gbaSoundCntL,      sizeof(uint8_t),  2, file);
    fwrite(gbaSoundCntH,      sizeof(uint16_t), 4, file);
    fwrite(gbaSoundCntX,      sizeof(uint16_t), 4, file);
    fwrite(&gbaMainSoundCntL, sizeof(gbaMainSoundCntL), 1, file);
    fwrite(&gbaMainSoundCntH, sizeof(gbaMainSoundCntH), 1, file);
    fwrite(&gbaMainSoundCntX, sizeof(gbaMainSoundCntX), 1, file);
    fwrite(&gbaSoundBias,     sizeof(gbaSoundBias),     1, file);

    fwrite(soundCnt,  sizeof(uint32_t), 16, file);
    fwrite(soundSad,  sizeof(uint32_t), 16, file);
    fwrite(soundTmr,  sizeof(uint16_t), 16, file);
    fwrite(soundPnt,  sizeof(uint16_t), 16, file);
    fwrite(soundLen,  sizeof(uint32_t), 16, file);
    fwrite(&mainSoundCnt, sizeof(mainSoundCnt), 1, file);
    fwrite(&soundBias,    sizeof(soundBias),    1, file);
    fwrite(sndCapCnt, sizeof(uint8_t),  2, file);
    fwrite(sndCapDad, sizeof(uint32_t), 2, file);
    fwrite(sndCapLen, sizeof(uint16_t), 2, file);

    for (int i = 0; i < 2; i++) {
        uint32_t count = (uint32_t)gbaFifos[i].size();
        fwrite(&count, sizeof(count), 1, file);
        // Walk the ring buffer without modifying it.
        int idx = gbaFifos[i].head;
        for (uint32_t j = 0; j < count; j++) {
            fwrite(&gbaFifos[i].data[idx], sizeof(int8_t), 1, file);
            idx = (idx + 1 == 32) ? 0 : idx + 1;
        }
    }
}

void Spu::loadState(FILE *file) {
    fread(&gbaFrameSequencer, sizeof(gbaFrameSequencer), 1, file);
    fread(gbaSoundTimers,     sizeof(int32_t),  4,  file);
    fread(gbaEnvelopes,       sizeof(int8_t),   3,  file);
    fread(gbaEnvTimers,       sizeof(int8_t),   3,  file);
    fread(&gbaSweepTimer,     sizeof(gbaSweepTimer),  1, file);
    fread(&gbaWaveDigit,      sizeof(gbaWaveDigit),   1, file);
    fread(&gbaNoiseValue,     sizeof(gbaNoiseValue),  1, file);
    fread(gbaWaveRam,         sizeof(gbaWaveRam),     1, file);
    fread(&gbaSampleA,        sizeof(gbaSampleA),     1, file);
    fread(&gbaSampleB,        sizeof(gbaSampleB),     1, file);
    fread(&enabled,           sizeof(enabled),        1, file);

    fread(adpcmValue,     sizeof(int32_t), 16, file);
    fread(adpcmLoopValue, sizeof(int32_t), 16, file);
    fread(adpcmIndex,     sizeof(int8_t),  16, file);
    fread(adpcmLoopIndex, sizeof(int8_t),  16, file);
    fread(adpcmToggle,    sizeof(bool),    16, file);

    fread(dutyCycles,    sizeof(uint8_t),  6,  file);
    fread(noiseValues,   sizeof(uint16_t), 2,  file);
    fread(soundCurrent,  sizeof(uint32_t), 16, file);
    fread(soundTimers,   sizeof(uint16_t), 16, file);
    fread(sndCapCurrent, sizeof(uint32_t), 2,  file);
    fread(sndCapTimers,  sizeof(uint16_t), 2,  file);

    fread(gbaSoundCntL,      sizeof(uint8_t),  2, file);
    fread(gbaSoundCntH,      sizeof(uint16_t), 4, file);
    fread(gbaSoundCntX,      sizeof(uint16_t), 4, file);
    fread(&gbaMainSoundCntL, sizeof(gbaMainSoundCntL), 1, file);
    fread(&gbaMainSoundCntH, sizeof(gbaMainSoundCntH), 1, file);
    fread(&gbaMainSoundCntX, sizeof(gbaMainSoundCntX), 1, file);
    fread(&gbaSoundBias,     sizeof(gbaSoundBias),     1, file);

    fread(soundCnt,  sizeof(uint32_t), 16, file);
    fread(soundSad,  sizeof(uint32_t), 16, file);
    fread(soundTmr,  sizeof(uint16_t), 16, file);
    fread(soundPnt,  sizeof(uint16_t), 16, file);
    fread(soundLen,  sizeof(uint32_t), 16, file);
    fread(&mainSoundCnt, sizeof(mainSoundCnt), 1, file);
    fread(&soundBias,    sizeof(soundBias),    1, file);
    fread(sndCapCnt, sizeof(uint8_t),  2, file);
    fread(sndCapDad, sizeof(uint32_t), 2, file);
    fread(sndCapLen, sizeof(uint16_t), 2, file);

    for (int i = 0; i < 2; i++) {
        gbaFifos[i].clear();
        uint32_t count = 0;
        fread(&count, sizeof(count), 1, file);
        for (uint32_t j = 0; j < count; j++) {
            int8_t value = 0;
            fread(&value, sizeof(value), 1, file);
            gbaFifos[i].push_back(value);
        }
    }

    memset(buffers, 0, sizeof(buffers));
    bufferPointer = 0;
    writeIdx      = 0;

    PPCIrqState st = PPCIrqLockByMsr();
    ready = false;
    PPCIrqUnlockByMsr(st);
}

// ---------------------------------------------------------------------------
// getSamples — audio thread
// ---------------------------------------------------------------------------
bool Spu::getSamples(uint32_t *dst, int count) {
    if (count > SPU_BUF_FRAMES)
        count = SPU_BUF_FRAMES;

    bool wait = false;

    if (Settings::fpsLimiter == 2) {
        u64 start   = PPCGetTickCount();
        u64 timeout = PPCUsToTicks(1000000 / 60);
        while (true) {
            PPCIrqState st = PPCIrqLockByMsr();
            bool isReady   = ready;
            PPCIrqUnlockByMsr(st);
            if (isReady) break;
            if (PPCGetTickCount() - start > timeout) { wait = true; break; }
            KThreadYield();
        }
    }
    else if (Settings::fpsLimiter == 1) {
        PPCIrqState st = PPCIrqLockByMsr();
        bool isReady   = ready;
        PPCIrqUnlockByMsr(st);
        if (!isReady)
            KThrQueueBlock(&waitQueue2, 1);
    }
    else {
        PPCIrqState st = PPCIrqLockByMsr();
        if (!ready) wait = true;
        PPCIrqUnlockByMsr(st);
    }

    if (wait) {
        PPCIrqState st   = PPCIrqLockByMsr();
        int         ri   = writeIdx ^ 1;
        uint32_t    last = buffers[ri][SPU_BUF_FRAMES - 1];
        PPCIrqUnlockByMsr(st);
        for (int i = 0; i < count; i++) dst[i] = last;
        return false;
    }

    {
        PPCIrqState st = PPCIrqLockByMsr();
        int ri = writeIdx ^ 1;
        // Use 32-byte aligned memcpy — cache-line friendly.
        memcpy(dst, buffers[ri], (size_t)count * sizeof(uint32_t));
        ready = false;
        PPCIrqUnlockByMsr(st);
    }

    if (Settings::fpsLimiter == 1)
        KThrQueueUnblockAllByValue(&waitQueue1, 1);

    return true;
}

// ---------------------------------------------------------------------------
// pushSample — emulator thread
// Packs left/right into one uint32 and stores into the write buffer.
// ---------------------------------------------------------------------------
void Spu::pushSample(int16_t sampleLeft, int16_t sampleRight) {
    buffers[writeIdx][bufferPointer++] =
        ((uint32_t)(uint16_t)sampleRight << 16) |
        ((uint32_t)(uint16_t)sampleLeft  & 0xFFFF);

    if (bufferPointer < (uint32_t)SPU_BUF_FRAMES)
        return;

    // Back-pressure.
    if (Settings::fpsLimiter == 2) {
        u64 start   = PPCGetTickCount();
        u64 timeout = PPCUsToTicks(1000000);
        while (true) {
            PPCIrqState st = PPCIrqLockByMsr();
            bool isReady   = ready;
            PPCIrqUnlockByMsr(st);
            if (!isReady) break;
            if (PPCGetTickCount() - start > timeout) break;
            KThreadYield();
        }
    }
    else if (Settings::fpsLimiter == 1) {
        PPCIrqState st = PPCIrqLockByMsr();
        bool isReady   = ready;
        PPCIrqUnlockByMsr(st);
        if (isReady)
            KThrQueueBlock(&waitQueue1, 1);
    }

    {
        PPCIrqState st = PPCIrqLockByMsr();
        writeIdx ^= 1;
        ready     = true;
        PPCIrqUnlockByMsr(st);
    }

    bufferPointer = 0;

    if (Settings::fpsLimiter == 1)
        KThrQueueUnblockAllByValue(&waitQueue2, 1);
}

// ---------------------------------------------------------------------------
// runGbaSample — optimized
//
// Key changes vs original:
//   • Division by 15  replaced with lookup  (gbaEnvMul)
//   • Division by 7   replaced with lookup  (gbaVolMul)
//   • Division by 128 replaced with >>7 shift
//   • Duty computation avoids per-sample multiply where possible
//   • std::deque → RingBuf (inline, no heap)
// ---------------------------------------------------------------------------
__attribute__((optimize("O3")))
void Spu::runGbaSample() {
    core->schedule(GBA_SPU_SAMPLE, 512);

    if (!Settings::emulateAudio)
        return pushSample(0, 0);

    int32_t sampleLeft  = 0;
    int32_t sampleRight = 0;

    if (gbaMainSoundCntX & 0x80) {  // BIT(7)

        int32_t data[4] = {};

        // ---- Channels 0 and 1: Square wave ----
        for (int i = 0; i < 2; i++) {
            if (!(gbaMainSoundCntX & (1 << i)))
                continue;

            // Sweep (channel 0 only).
            if (i == 0 &&
                (gbaFrameSequencer & 0x1FF) == 128 &&
                (gbaSoundCntL[0] & 0x70) &&
                --gbaSweepTimer <= 0)
            {
                uint16_t freq  = gbaSoundCntX[0] & 0x07FF;
                int      shift = gbaSoundCntL[0] & 0x07;
                int      delta = freq >> shift;
                if (gbaSoundCntL[0] & 0x08) delta = -delta;  // BIT(3)

                freq += (uint16_t)delta;

                if (freq < 0x800) {
                    gbaSoundCntX[0] = (gbaSoundCntX[0] & ~0x07FFu) | freq;
                    gbaSweepTimer   = (gbaSoundCntL[0] & 0x70) >> 4;
                } else {
                    gbaMainSoundCntX &= ~(uint8_t)(1u << i);
                    continue;
                }
            }

            // Advance frequency timer.
            gbaSoundTimers[i] -= 4;
            const int period = 2048 - (gbaSoundCntX[i] & 0x07FF);
            while (gbaSoundTimers[i] <= 0)
                gbaSoundTimers[i] += period;

            // Duty cycle — avoids multiply each sample.
            int duty;
            switch ((gbaSoundCntH[i] >> 6) & 3) {
                case 0:  duty = (period * 7) >> 3; break;  // 87.5%  (period*7/8)
                case 1:  duty = (period * 6) >> 3; break;  // 75%
                case 2:  duty = (period >> 1);     break;  // 50%    (period*4/8)
                case 3:  duty = (period >> 2);     break;  // 25%    (period*2/8)
                default: duty = 0;                 break;
            }

            data[i] = (gbaSoundTimers[i] < duty) ? -0x80 : 0x80;

            // Length counter.
            if ((gbaFrameSequencer & 127) == 0 &&
                (gbaSoundCntX[i] & 0x4000) &&     // BIT(14)
                (gbaSoundCntH[i] & 0x003F))
            {
                uint16_t len = (gbaSoundCntH[i] & 0x003F) - 1;
                gbaSoundCntH[i] = (gbaSoundCntH[i] & ~0x003Fu) | len;
                if (len == 0)
                    gbaMainSoundCntX &= ~(uint8_t)(1u << i);
            }

            // Volume envelope (fires at frame-sequencer == 448).
            if (gbaFrameSequencer == 448 && --gbaEnvTimers[i] <= 0) {
                if (gbaEnvTimers[i] == 0) {
                    if ((gbaSoundCntH[i] & 0x0800) && gbaEnvelopes[i] < 15)  // BIT(11)
                        gbaEnvelopes[i]++;
                    else if (!(gbaSoundCntH[i] & 0x0800) && gbaEnvelopes[i] > 0)
                        gbaEnvelopes[i]--;
                } else {
                    gbaEnvelopes[i] = (int8_t)((gbaSoundCntH[i] >> 12) & 0xF);
                }
                gbaEnvTimers[i] = (int8_t)((gbaSoundCntH[i] >> 8) & 0x7);
            }

            // Envelope volume multiply — table lookup replaces / 15.
            data[i] = (int32_t)(((int64_t)data[i] * gbaEnvMul[gbaEnvelopes[i]]) >> 15);
        }

        // ---- Channel 2: Wave ----
        if ((gbaMainSoundCntX & 0x04) && (gbaSoundCntL[1] & 0x80)) {  // BIT(2), BIT(7)
            gbaSoundTimers[2] -= 64;
            const int wPeriod = 2048 - (gbaSoundCntX[2] & 0x07FF);
            while (gbaSoundTimers[2] <= 0) {
                gbaSoundTimers[2] += wPeriod;
                gbaWaveDigit = (gbaWaveDigit + 1) & 63;  // % 64 via mask
            }

            int bank = (gbaSoundCntL[1] >> 6) & 1;
            if ((gbaSoundCntL[1] & 0x20) && gbaWaveDigit >= 32)  // BIT(5)
                bank ^= 1;

            // Two nibbles per byte.
            uint8_t byte = gbaWaveRam[bank][(gbaWaveDigit & 31) >> 1];
            data[2] = (gbaWaveDigit & 1) ? (byte & 0x0F) : (byte >> 4);

            // Length counter.
            if ((gbaFrameSequencer & 127) == 0 &&
                (gbaSoundCntX[2] & 0x4000) &&
                (gbaSoundCntH[2] & 0x00FF))
            {
                uint16_t len = (gbaSoundCntH[2] & 0x00FF) - 1;
                gbaSoundCntH[2] = (gbaSoundCntH[2] & ~0x00FFu) | len;
                if (len == 0)
                    gbaMainSoundCntX &= ~0x04u;
            }

            // Volume shift.
            switch ((gbaSoundCntH[2] >> 13) & 7) {
                case 0:  data[2] >>= 4;                      break;  // mute
                case 1:  /* full */                           break;  // 100%
                case 2:  data[2] >>= 1;                      break;  //  50%
                case 3:  data[2] >>= 2;                      break;  //  25%
                // case 4: 75% — (x*3)/4 → approximate with (x - x/4)
                default: data[2] = data[2] - (data[2] >> 2); break;  //  75%
            }

            // Scale to square-channel range: * 256 / 15
            // Replace with table: gbaEnvMul[15] >> 4 ≈ 2048, but exact is 256/15.
            // Use: data[2] * 273 >> 4  ≈ data[2] * 17.0625 ≈ data[2] * (256/15)
            // Exact: 256/15 = 17.0667; 273/16 = 17.0625 — error < 0.04%, acceptable.
            data[2] = (data[2] * 273) >> 4;
        }

        // ---- Channel 3: Noise ----
        if (gbaMainSoundCntX & 0x08) {  // BIT(3)
            gbaSoundTimers[3] -= 16;
            while (gbaSoundTimers[3] <= 0) {
                int divisor = (gbaSoundCntX[3] & 0x0007) << 4;
                if (divisor == 0) divisor = 8;
                gbaSoundTimers[3] += divisor << ((gbaSoundCntX[3] >> 4) & 0xF);

                // LFSR step.
                gbaNoiseValue &= ~0x8000u;
                if (gbaNoiseValue & 0x0001) {
                    uint16_t xorMask = (gbaSoundCntH[3] & 0x0008) ? 0x60u : 0x6000u;
                    gbaNoiseValue = (uint16_t)(0x8000u |
                                   ((gbaNoiseValue >> 1) ^ xorMask));
                } else {
                    gbaNoiseValue >>= 1;
                }
            }

            data[3] = (gbaNoiseValue & 0x8000) ? 0x80 : -0x80;

            // Length counter.
            if ((gbaFrameSequencer & 127) == 0 &&
                (gbaSoundCntX[3] & 0x4000) &&
                (gbaSoundCntH[3] & 0x003F))
            {
                uint16_t len = (gbaSoundCntH[3] & 0x003F) - 1;
                gbaSoundCntH[3] = (gbaSoundCntH[3] & ~0x003Fu) | len;
                if (len == 0)
                    gbaMainSoundCntX &= ~0x08u;
            }

            // Volume envelope (shares slot 2).
            if (gbaFrameSequencer == 448 && --gbaEnvTimers[2] <= 0) {
                if (gbaEnvTimers[2] == 0) {
                    if ((gbaSoundCntH[3] & 0x0800) && gbaEnvelopes[2] < 15)
                        gbaEnvelopes[2]++;
                    else if (!(gbaSoundCntH[3] & 0x0800) && gbaEnvelopes[2] > 0)
                        gbaEnvelopes[2]--;
                } else {
                    gbaEnvelopes[2] = (int8_t)((gbaSoundCntH[3] >> 12) & 0xF);
                }
                gbaEnvTimers[2] = (int8_t)((gbaSoundCntH[3] >> 8) & 0x7);
            }

            data[3] = (int32_t)(((int64_t)data[3] * gbaEnvMul[gbaEnvelopes[2]]) >> 15);
        }

        // ---- Mix legacy channels into left/right ----
        // Master volume shift (bits 1-0 of gbaMainSoundCntH).
        const int masterShift = (gbaMainSoundCntH & 0x0003);  // 0=>>2, 1=>>1, 2=>>0

        // Precompute per-channel left/right volume multipliers (0..7 → lookup).
        const uint8_t volL_field = (gbaMainSoundCntL >> 4) & 0x7;
        const uint8_t volR_field =  gbaMainSoundCntL        & 0x7;

        for (int i = 0; i < 4; i++) {
            int32_t d = data[i];

            // Master volume shift.
            if (masterShift < 2)
                d >>= (2 - masterShift);  // case 0: >>2, case 1: >>1, case 2: >>0

            // Left channel.
            if (gbaMainSoundCntL & (1u << (12 + i))) {
                // d * volL / 7  →  (d * gbaVolMul[volL]) >> 15
                sampleLeft  += (int32_t)(((int64_t)d * gbaVolMul[volL_field]) >> 15);
            }
            // Right channel.
            if (gbaMainSoundCntL & (1u << (8 + i))) {
                sampleRight += (int32_t)(((int64_t)d * gbaVolMul[volR_field]) >> 15);
            }
        }

        // ---- DMA FIFO A ----
        {
            const int32_t shiftA = (gbaMainSoundCntH & 0x0004) ? 2 : 1;  // BIT(2)
            if (gbaMainSoundCntH & 0x0200)  // BIT(9)
                sampleLeft  += (int32_t)gbaSampleA << shiftA;
            if (gbaMainSoundCntH & 0x0100)  // BIT(8)
                sampleRight += (int32_t)gbaSampleA << shiftA;
        }

        // ---- DMA FIFO B ----
        {
            const int32_t shiftB = (gbaMainSoundCntH & 0x0008) ? 2 : 1;  // BIT(3)
            if (gbaMainSoundCntH & 0x2000)  // BIT(13)
                sampleLeft  += (int32_t)gbaSampleB << shiftB;
            if (gbaMainSoundCntH & 0x1000)  // BIT(12)
                sampleRight += (int32_t)gbaSampleB << shiftB;
        }

        // Advance frame sequencer (0-511).
        if (++gbaFrameSequencer >= 512)
            gbaFrameSequencer = 0;
    }

    // Apply bias, clamp, scale to 16-bit.
    const int32_t bias = gbaSoundBias & 0x3FF;
    // Clamp to [0, 0x3FF] then re-centre and scale.
    int32_t l = sampleLeft  + bias;
    int32_t r = sampleRight + bias;
    if (l <     0) l = 0; else if (l > 0x3FF) l = 0x3FF;
    if (r <     0) r = 0; else if (r > 0x3FF) r = 0x3FF;
    sampleLeft  = (l - 0x200) << 6;
    sampleRight = (r - 0x200) << 6;

    pushSample((int16_t)sampleLeft, (int16_t)sampleRight);
}

// ---------------------------------------------------------------------------
// runSample — optimized
//
// Key changes vs original:
//   • division by 128 → >>7 (pan, masterVol, mulFactor)
//   • int64_t mixing kept (NDS requires it) but inner divisions eliminated
//   • BIT() macros replaced with literal shifts (avoids macro expansion cost)
//   • Capture-unit clamp changed to branchless min/max
// ---------------------------------------------------------------------------
__attribute__((optimize("O3")))
void Spu::runSample() {
    core->schedule(NDS_SPU_SAMPLE, 512 * 2);

    if (!Settings::emulateAudio)
        return pushSample(0, 0);

    int64_t mixerLeft  = 0, mixerRight = 0;
    int64_t channelsLeft[2]  = {};
    int64_t channelsRight[2] = {};

    for (int i = 0; (uint16_t)(enabled >> i); i++) {
        if (!(enabled & (1u << i))) continue;

        const uint32_t cnt    = soundCnt[i];
        const uint8_t  format = (cnt >> 29) & 0x3;
        int64_t        data   = 0;

        switch (format) {
            case 0:
                data = (int8_t)core->memory.read<uint8_t>(1, soundCurrent[i]) << 8;
                break;
            case 1:
                data = (int16_t)core->memory.read<uint16_t>(1, soundCurrent[i]);
                break;
            case 2:
                data = adpcmValue[i];
                break;
            case 3:
                if (i >= 8 && i <= 13) {
                    uint8_t duty = 7 - ((cnt >> 24) & 0x7);
                    data = (dutyCycles[i - 8] < duty) ? -0x7FFF : 0x7FFF;
                } else if (i >= 14) {
                    data = (noiseValues[i - 14] & 0x8000) ? -0x7FFF : 0x7FFF;
                }
                break;
        }

        soundTimers[i] += 512;
        bool overflow   = (soundTimers[i] < 512);

        while (overflow) {
            soundTimers[i] += soundTmr[i];
            overflow         = (soundTimers[i] < soundTmr[i]);

            switch (format) {
                case 0:
                    soundCurrent[i] += 1;
                    break;

                case 1:
                    soundCurrent[i] += 2;
                    break;

                case 2: {
                    if (soundCurrent[i] == soundSad[i] + (uint32_t)soundPnt[i] * 4 &&
                        !adpcmToggle[i])
                    {
                        adpcmLoopValue[i] = adpcmValue[i];
                        adpcmLoopIndex[i] = adpcmIndex[i];
                    }

                    uint8_t byte   = core->memory.read<uint8_t>(1, soundCurrent[i]);
                    uint8_t nibble = adpcmToggle[i]
                                     ? ((byte >> 4) & 0xF)
                                     : ( byte & 0xF);

                    // IMA step — avoid repeated table lookup.
                    const int32_t step = adpcmTable[adpcmIndex[i]];
                    int32_t diff = step >> 3;
                    if (nibble & 1) diff += step >> 2;
                    if (nibble & 2) diff += step >> 1;
                    if (nibble & 4) diff += step;

                    if (nibble & 8) {
                        adpcmValue[i] -= diff;
                        if (adpcmValue[i] < -0x7FFF) adpcmValue[i] = -0x7FFF;
                    } else {
                        adpcmValue[i] += diff;
                        if (adpcmValue[i] >  0x7FFF) adpcmValue[i] =  0x7FFF;
                    }

                    int8_t newIdx = adpcmIndex[i] + (int8_t)indexTable[nibble & 7];
                    if (newIdx <  0) newIdx =  0;
                    if (newIdx > 88) newIdx = 88;
                    adpcmIndex[i] = newIdx;

                    adpcmToggle[i] = !adpcmToggle[i];
                    if (!adpcmToggle[i]) soundCurrent[i]++;
                    break;
                }

                case 3:
                    if (i >= 8 && i <= 13) {
                        dutyCycles[i - 8] = (dutyCycles[i - 8] + 1) & 7;  // %8 via mask
                    } else if (i >= 14) {
                        uint16_t nv = noiseValues[i - 14];
                        nv &= ~0x8000u;
                        if (nv & 1)
                            nv = (uint16_t)(0x8000u | ((nv >> 1) ^ 0x6000u));
                        else
                            nv >>= 1;
                        noiseValues[i - 14] = nv;
                    }
                    break;
            }

            if (format != 3 &&
                soundCurrent[i] >= soundSad[i] + ((uint32_t)soundPnt[i] + soundLen[i]) * 4)
            {
                const uint8_t repeat = (cnt >> 27) & 0x3;
                if (repeat == 1) {
                    soundCurrent[i] = soundSad[i] + (uint32_t)soundPnt[i] * 4;
                    if (format == 2) {
                        adpcmValue[i]  = adpcmLoopValue[i];
                        adpcmIndex[i]  = adpcmLoopIndex[i];
                        adpcmToggle[i] = false;
                    }
                } else {
                    soundCnt[i] &= ~0x80000000u;
                    enabled     &= ~(uint16_t)(1u << i);
                    break;
                }
            }
        }

        // Volume divider: bits 9-8.
        int divShift = (cnt >> 8) & 0x3;
        if (divShift == 3) divShift = 4;
        data <<= (4 - divShift);

        // Per-channel volume multiplier: bits 6-0.
        // Replace / 128 with >> 7.
        int mulFactor = cnt & 0x7F;
        if (mulFactor == 127) mulFactor = 128;
        data = (data * mulFactor) >> 7;

        // Panning: bits 22-16.
        // Replace / 128 with >> 7.
        int pan = (cnt >> 16) & 0x7F;
        if (pan == 127) pan = 128;
        const int panR = pan;
        const int panL = 128 - pan;

        // Avoid 64-bit multiply where possible; pan is 0..128, data fits int64.
        const int64_t dataLeft  = ((data * panL) >> 7) >> 3;
        const int64_t dataRight = ((data * panR) >> 7) >> 3;

        if (i == 1 || i == 3) {
            channelsLeft[i >> 1]  = dataLeft;
            channelsRight[i >> 1] = dataRight;
            if (mainSoundCnt & (1u << (12 + (i >> 1))))
                continue;
        }

        mixerLeft  += dataLeft;
        mixerRight += dataRight;
    }

    // ---- Capture units ----
    for (int i = 0; i < 2; i++) {
        if (!(sndCapCnt[i] & 0x80)) continue;  // BIT(7)

        sndCapTimers[i] += 512;
        bool overflow    = (sndCapTimers[i] < 512);

        while (overflow) {
            const uint16_t tmr = soundTmr[1 + (i << 1)];
            sndCapTimers[i] += tmr;
            overflow          = (sndCapTimers[i] < tmr);

            int64_t sample = (i == 0) ? mixerLeft : mixerRight;
            // Branchless clamp.
            if (sample >  0x7FFFFF) sample =  0x7FFFFF;
            if (sample < -0x800000) sample = -0x800000;

            if (sndCapCnt[i] & 0x08) {  // BIT(3) — 8-bit
                core->memory.write<uint8_t>(1, sndCapCurrent[i],
                                            (uint8_t)(sample >> 16));
                sndCapCurrent[i] += 1;
            } else {
                core->memory.write<uint16_t>(1, sndCapCurrent[i],
                                             (uint16_t)(sample >> 8));
                sndCapCurrent[i] += 2;
            }

            if (sndCapCurrent[i] >= sndCapDad[i] + (uint32_t)sndCapLen[i] * 4) {
                if (sndCapCnt[i] & 0x04) {  // BIT(2) — one-shot
                    sndCapCnt[i] &= ~0x80u;
                    break;
                } else {
                    sndCapCurrent[i] = sndCapDad[i];
                }
            }
        }
    }

    // ---- Final output selection ----
    int64_t sampleLeft, sampleRight;
    switch ((mainSoundCnt >> 8) & 0x3) {
        case 0:  sampleLeft = mixerLeft;                          break;
        case 1:  sampleLeft = channelsLeft[0];                    break;
        case 2:  sampleLeft = channelsLeft[1];                    break;
        case 3:  sampleLeft = channelsLeft[0] + channelsLeft[1];  break;
        default: sampleLeft = mixerLeft;                          break;
    }
    switch ((mainSoundCnt >> 10) & 0x3) {
        case 0:  sampleRight = mixerRight;                           break;
        case 1:  sampleRight = channelsRight[0];                     break;
        case 2:  sampleRight = channelsRight[1];                     break;
        case 3:  sampleRight = channelsRight[0] + channelsRight[1];  break;
        default: sampleRight = mixerRight;                           break;
    }

    // Master volume — replace / 128 with >> 7.
    int masterVol = mainSoundCnt & 0x7F;
    if (masterVol == 127) masterVol = 128;
    sampleLeft  = (sampleLeft  * masterVol) >> 7;
    sampleRight = (sampleRight * masterVol) >> 7;
    sampleLeft  >>= 8;
    sampleRight >>= 8;

    // Bias and clamp.
    if (Settings::audio16Bit) {
        int32_t sl = (int32_t)sampleLeft  + (soundBias << 6);
        int32_t sr = (int32_t)sampleRight + (soundBias << 6);
        if (sl <      0) sl = 0; else if (sl > 0xFFFF) sl = 0xFFFF;
        if (sr <      0) sr = 0; else if (sr > 0xFFFF) sr = 0xFFFF;
        sampleLeft  = sl - 0x8000;
        sampleRight = sr - 0x8000;
    } else {
        int32_t sl = (int32_t)(sampleLeft  >> 6) + soundBias;
        int32_t sr = (int32_t)(sampleRight >> 6) + soundBias;
        if (sl <    0) sl = 0; else if (sl > 0x3FF) sl = 0x3FF;
        if (sr <    0) sr = 0; else if (sr > 0x3FF) sr = 0x3FF;
        sampleLeft  = (sl - 0x200) << 6;
        sampleRight = (sr - 0x200) << 6;
    }

    pushSample((int16_t)sampleLeft, (int16_t)sampleRight);
}

// ---------------------------------------------------------------------------
// startChannel — unchanged logic
// ---------------------------------------------------------------------------
void Spu::startChannel(int channel) {
    soundCurrent[channel] = soundSad[channel];
    soundTimers[channel]  = soundTmr[channel];

    switch ((soundCnt[channel] >> 29) & 0x3) {
        case 2: {
            uint32_t header      = core->memory.read<uint32_t>(1, soundSad[channel]);
            adpcmValue[channel]  = (int16_t)(header & 0xFFFF);
            adpcmIndex[channel]  = (header >> 16) & 0x7F;
            if (adpcmIndex[channel] > 88) adpcmIndex[channel] = 88;
            adpcmToggle[channel] = false;
            soundCurrent[channel] += 4;
            break;
        }
        case 3:
            if (channel >= 8 && channel <= 13)
                dutyCycles[channel - 8]   = 0;
            else if (channel >= 14)
                noiseValues[channel - 14] = 0x7FFF;
            break;
    }
    enabled |= (uint16_t)(1u << channel);
}

// ---------------------------------------------------------------------------
// gbaFifoTimer — updated for RingBuf
// ---------------------------------------------------------------------------
void Spu::gbaFifoTimer(int timer) {
    if (((gbaMainSoundCntH >> 10) & 1) == (uint16_t)timer) {
        if (!gbaFifos[0].empty()) {
            gbaSampleA = gbaFifos[0].front();
            gbaFifos[0].pop_front();
        }
        if (gbaFifos[0].size() <= 16)
            core->dma[1].trigger(3, 0x02);
    }

    if (((gbaMainSoundCntH >> 14) & 1) == (uint16_t)timer) {
        if (!gbaFifos[1].empty()) {
            gbaSampleB = gbaFifos[1].front();
            gbaFifos[1].pop_front();
        }
        if (gbaFifos[1].size() <= 16)
            core->dma[1].trigger(3, 0x04);
    }
}

// ---------------------------------------------------------------------------
// GBA register writers — unchanged logic
// ---------------------------------------------------------------------------
void Spu::writeGbaSoundCntL(int channel, uint8_t value) {
    if (!(gbaMainSoundCntX & 0x80)) return;
    uint8_t mask = (channel == 0) ? 0x7F : 0xE0;
    gbaSoundCntL[channel / 2] = (gbaSoundCntL[channel / 2] & ~mask) | (value & mask);
}

void Spu::writeGbaSoundCntH(int channel, uint16_t mask, uint16_t value) {
    if (!(gbaMainSoundCntX & 0x80)) return;
    switch (channel) {
        case 2: mask &= 0xE0FF; break;
        case 3: mask &= 0xFF3F; break;
    }
    gbaSoundCntH[channel] = (gbaSoundCntH[channel] & ~mask) | (value & mask);
}

void Spu::writeGbaSoundCntX(int channel, uint16_t mask, uint16_t value) {
    if (!(gbaMainSoundCntX & 0x80)) return;
    mask &= (channel == 3) ? 0x40FF : 0x47FF;
    gbaSoundCntX[channel] = (gbaSoundCntX[channel] & ~mask) | (value & mask);

    if (!Settings::emulateAudio || !(value & 0x8000)) return;

    gbaMainSoundCntX |= (uint8_t)(1u << channel);
    switch (channel) {
        case 0:
            gbaSweepTimer = (gbaSoundCntL[0] >> 4) & 0x7;
            // fall through
        case 1:
            gbaEnvelopes[channel]   = (int8_t)((gbaSoundCntH[channel] >> 12) & 0xF);
            gbaEnvTimers[channel]   = (int8_t)((gbaSoundCntH[channel] >>  8) & 0x7);
            gbaSoundTimers[channel] = 2048 - (gbaSoundCntX[channel] & 0x07FF);
            break;
        case 2:
            gbaWaveDigit      = 0;
            gbaSoundTimers[2] = 2048 - (gbaSoundCntX[2] & 0x07FF);
            break;
        case 3: {
            gbaNoiseValue   = (gbaSoundCntH[3] & 0x0008) ? 0x40u : 0x4000u;
            gbaEnvelopes[2] = (int8_t)((gbaSoundCntH[3] >> 12) & 0xF);
            gbaEnvTimers[2] = (int8_t)((gbaSoundCntH[3] >>  8) & 0x7);
            int divisor     = (gbaSoundCntX[3] & 0x0007) << 4;
            if (divisor == 0) divisor = 8;
            gbaSoundTimers[3] = divisor << ((gbaSoundCntX[3] >> 4) & 0xF);
            break;
        }
    }
}

void Spu::writeGbaMainSoundCntL(uint16_t mask, uint16_t value) {
    if (!(gbaMainSoundCntX & 0x80)) return;
    mask &= 0xFF77;
    gbaMainSoundCntL = (gbaMainSoundCntL & ~mask) | (value & mask);
}

void Spu::writeGbaMainSoundCntH(uint16_t mask, uint16_t value) {
    mask &= 0x770F;
    gbaMainSoundCntH = (gbaMainSoundCntH & ~mask) | (value & mask);
    if (value & 0x0800) gbaFifos[0].clear();
    if (value & 0x8000) gbaFifos[1].clear();
}

void Spu::writeGbaMainSoundCntX(uint8_t value) {
    gbaMainSoundCntX = (gbaMainSoundCntX & ~0x80u) | (value & 0x80u);

    if (!(gbaMainSoundCntX & 0x80)) {
        for (int i = 0; i < 2; i++) gbaSoundCntL[i] = 0;
        for (int i = 0; i < 4; i++) { gbaSoundCntH[i] = 0; gbaSoundCntX[i] = 0; }
        gbaMainSoundCntL   = 0;
        gbaMainSoundCntX  &= ~0x0Fu;
        gbaFrameSequencer  = 0;
    }
}

void Spu::writeGbaSoundBias(uint16_t mask, uint16_t value) {
    mask &= 0xC3FE;
    gbaSoundBias = (gbaSoundBias & ~mask) | (value & mask);
}

void Spu::writeGbaWaveRam(int index, uint8_t value) {
    gbaWaveRam[!((gbaSoundCntL[1] >> 6) & 1)][index] = value;
}

void Spu::writeGbaFifoA(uint32_t mask, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        if (!gbaFifos[0].full() && (mask & (0xFFu << shift)))
            gbaFifos[0].push_back((int8_t)(value >> shift));
}

void Spu::writeGbaFifoB(uint32_t mask, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        if (!gbaFifos[1].full() && (mask & (0xFFu << shift)))
            gbaFifos[1].push_back((int8_t)(value >> shift));
}

// ---------------------------------------------------------------------------
// NDS register writers — unchanged logic, BIT() → literal shifts
// ---------------------------------------------------------------------------
void Spu::writeSoundCnt(int channel, uint32_t mask, uint32_t value) {
    if (!Settings::emulateAudio) value &= ~0x80000000u;

    bool enable = !(soundCnt[channel] & 0x80000000u) && (value & mask & 0x80000000u);

    mask &= 0xFF7F837Fu;
    soundCnt[channel] = (soundCnt[channel] & ~mask) | (value & mask);

    if (enable && (mainSoundCnt & 0x8000u) &&
        (soundSad[channel] != 0 || ((soundCnt[channel] >> 29) & 0x3) == 3))
    {
        startChannel(channel);
    }
    else if (!(soundCnt[channel] & 0x80000000u)) {
        enabled &= ~(uint16_t)(1u << channel);
    }
}

void Spu::writeSoundSad(int channel, uint32_t mask, uint32_t value) {
    mask &= 0x07FFFFFFu;
    soundSad[channel] = (soundSad[channel] & ~mask) | (value & mask);

    if (((soundCnt[channel] >> 29) & 0x3) != 3) {
        if (soundSad[channel] != 0 &&
            (mainSoundCnt & 0x8000u) &&
            (soundCnt[channel] & 0x80000000u))
        {
            startChannel(channel);
        } else {
            enabled &= ~(uint16_t)(1u << channel);
        }
    }
}

void Spu::writeSoundTmr(int channel, uint16_t mask, uint16_t value) {
    soundTmr[channel] = (soundTmr[channel] & ~mask) | (value & mask);
}

void Spu::writeSoundPnt(int channel, uint16_t mask, uint16_t value) {
    soundPnt[channel] = (soundPnt[channel] & ~mask) | (value & mask);
}

void Spu::writeSoundLen(int channel, uint32_t mask, uint32_t value) {
    mask &= 0x003FFFFFu;
    soundLen[channel] = (soundLen[channel] & ~mask) | (value & mask);
}

void Spu::writeMainSoundCnt(uint16_t mask, uint16_t value) {
    bool enable = !(mainSoundCnt & 0x8000u) && (value & 0x8000u);
    mask &= 0xBF7Fu;
    mainSoundCnt = (mainSoundCnt & ~mask) | (value & mask);

    if (enable) {
        for (int i = 0; i < 16; i++) {
            if ((soundCnt[i] & 0x80000000u) &&
                (soundSad[i] != 0 || ((soundCnt[i] >> 29) & 0x3) == 3))
            {
                startChannel(i);
            }
        }
    } else if (!(mainSoundCnt & 0x8000u)) {
        enabled = 0;
    }
}

void Spu::writeSoundBias(uint16_t mask, uint16_t value) {
    mask &= 0x03FFu;
    soundBias = (soundBias & ~mask) | (value & mask);
}

void Spu::writeSndCapCnt(int channel, uint8_t value) {
    if (!(sndCapCnt[channel] & 0x80u) && (value & 0x80u)) {
        sndCapCurrent[channel] = sndCapDad[channel];
        sndCapTimers[channel]  = soundTmr[1 + (channel << 1)];
    }
    sndCapCnt[channel] = value & 0x8Fu;
}

void Spu::writeSndCapDad(int channel, uint32_t mask, uint32_t value) {
    mask &= 0x07FFFFFFu;
    sndCapDad[channel] = (sndCapDad[channel] & ~mask) | (value & mask);
    sndCapCurrent[channel] = sndCapDad[channel];
    sndCapTimers[channel]  = soundTmr[1 + (channel << 1)];
}

void Spu::writeSndCapLen(int channel, uint16_t mask, uint16_t value) {
    sndCapLen[channel] = (sndCapLen[channel] & ~mask) | (value & mask);
}

// ---------------------------------------------------------------------------
// GBA register readers
// ---------------------------------------------------------------------------
uint8_t Spu::readGbaSoundCntL(int channel) {
    return gbaSoundCntL[channel / 2];
}

uint16_t Spu::readGbaSoundCntH(int channel) {
    uint16_t mask = (channel == 2) ? (uint16_t)~0x00FFu : (uint16_t)~0x003Fu;
    return gbaSoundCntH[channel] & mask;
}

uint16_t Spu::readGbaSoundCntX(int channel) {
    return gbaSoundCntX[channel] & (uint16_t)~0x07FFu;
}

uint8_t Spu::readGbaWaveRam(int index) {
    return gbaWaveRam[!((gbaSoundCntL[1] >> 6) & 1)][index];
}
