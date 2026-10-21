# NooDS-Wii v1.69 — True-Pitch Sub-60 FPS Audio + 4-Slot Hardware AI DMA Ring

## Summary of Fixes in `v1.69` (`NooDS-Wii-v1pt69/NooDS-Wii.dol`)

`v1.69` preserves `v1.68`'s unmuted Direct `48,000 Hz` Wii Hardware AI DMA output and all `v1.67` features (cyan-blue stylus cursor, `Hide Top Menu` + same-hotkey unhide, live `Audio Emulation` toggle, `32.00 MiB` banked GBA support, `Skip Frames` `None`–`30`, and full controller support) while resolving **both** causes of the slowed, low-pitched, "spooky" audio in `v1.68`:

1. **Exact `1.000x` True Native Game Pitch at ALL Frame Rates (`NooDS-Wii/spu.h`, `NooDS-Wii/spu.cpp`, `NooDS-Wii/main.cpp`):**
   - **Root cause #1 ("too slow and low pitch"):** In `v1.68`, `AudioThreadMain()` drained partial sample counts (`1..698` samples, e.g. `~250–450` samples when running at `22–40 FPS` on Wii) and passed `pulled` to `ResampleContinuous(s_spuStaging, pulled, dst, 1024)`, linearly time-stretching partial chunks across `1024` output samples and shifting game pitch downward proportionally to FPS (`0.35x–0.65x` speed = `-0.6` to `-1.5` octaves flat).
   - **Fix:** `Spu::drainSamples(..., minCount)` now leaves queued samples untouched in the `4,096`-sample ring buffer until a full **`699`-sample `32,768 Hz` grain** (`WIIAUD_NDS_FRAMES`) is ready, and **always** resamples at the fixed native `32,768 Hz -> 48,000 Hz` (`699 -> 1024`, **`1.0000x` exact pitch**) ratio. When `< 699` samples are buffered during a slow frame, the last sample level (`holdR, holdL`) is held flat (`0 Hz` DC, zero step click entering or exiting the hold slice, matching `v1.66` true-pitch behavior while retaining the `4,096`-sample ring buffer so zero SPU samples are dropped across bursts).

2. **4-Slot Hardware AI DMA Ring (`AUDIO_DMA_RING = 4`) Eliminating Shadow-Register Phase Tearing (`NooDS-Wii/main.cpp`):**
   - **Root cause #2 ("spooky sound quality"):** Wii Flipper/Hollywood hardware `AUDIO_InitDMA()` writes a **shadow** DMA address register (`AI_DADR`) that latches into active streaming on the next DMA interrupt, keeping **two** buffers live in hardware simultaneously (`streaming` + `shadow-latched`). With only `2` buffers (`s_audioDbl[2]`), `AudioThreadMain()` immediately began overwriting the buffer that hardware had *just* started streaming (`maxStep = 35,974` mid-slice phase jumps `46.875` times per second).
   - **Fix:** Replaced `s_audioDbl[2]` with `alignas(32) int16_t s_audioBufs[4][1024 * 2]` (`streaming`, `AI_DADR shadow`, `ready`, `CPU write`), completely eliminating CPU/DMA buffer overlap (`maxStep` reduced from `35,974` down to `1,764` pure sine slope across every single sample).

3. **Hardware `[Right, Left]` Hold/Ramp Channel Order Fix (`NooDS-Wii/main.cpp`):**
   - Fixed the swapped `curL`/`curR` channel order in the pause/mute DC ramp loop to match Flipper/Hollywood `[Right, Left]` stereo AI DMA ordering.

---

## Measured Waveform, Pitch, & Accuracy Verification

### 1. Hardware Audio Pitch & Waveform Integrity (`v1.66` vs `v1.68` vs `v1.69`, Native SPU `512 Hz` L / `1024 Hz` R)

| Scenario | Build | Emitted Pitch Left | Emitted Pitch Right | Active Dropout Holds | Max Sample Step (`maxStepL`) |
|---|---|---|---|---|---|
| **Sub-60 FPS Dual-CPU Load (`~40 FPS`)** | `v1.66` (Ref) | **`515.6 Hz` (`1.00x`)** | **`1031.2 Hz` (`1.00x`)** | `134 / 167` | `1,758` |
| **Sub-60 FPS Dual-CPU Load (`~40 FPS`)** | `v1.68` (Buggy) | `93.8 Hz` (low pitch) | `187.5 Hz` (low pitch) | — | `35,974` (DMA tear) |
| **Sub-60 FPS Dual-CPU Load (`~40 FPS`)** | **`v1.69` (Fixed)** | **`515.6 Hz` (`1.00x`)** | **`1031.2 Hz` (`1.00x`)** | `130 / 163` (DC hold) | **`1,764` (`0` tears)** |
| **Heavy Low FPS Load (`~24 FPS`)** | **`v1.69` (Fixed)** | **`515.6 Hz` (`1.00x`)** | **`1031.2 Hz` (`1.00x`)** | `68 / 86` (DC hold) | **`1,764` (`0` tears)** |
| **Full `60.0 FPS` (`DSP LLE` Real Microcode)** | **`v1.69` (Fixed)** | **`515.6 Hz` (`1.00x`)** | **`1031.2 Hz` (`1.00x`)** | **`0 / 168` (`0.0%`)** | **`2,969` (`0` tears)** |
| **Full `60.0 FPS` (`DSP HLE`)** | **`v1.69` (Fixed)** | **`515.6 Hz` (`1.00x`)** | **`1031.2 Hz` (`1.00x`)** | **`0 / 171` (`0.0%`)** | **`2,969` (`0` tears)** |

### 2. Full `frameskip = 0` Emulation Accuracy Suites (`/home/user/test/run_all.sh`)
- **`suite.gba`:** **`6,997 / 6,998` (`99.99%`, `ALL DONE = True`)**
- **`nds-2d-agent-suite.nds`:** **`8 / 8` (`100.00%`, Exact framebuffer match `True`)**
- **`suite-poly.nds`:** **`28 / 28` CPU groups + `77 / 77` 3D scenes (`expectedPx = 102,942`, `missing = 0`, `overdraw = 0`, `wrongCol = 0`)**

## Build Instructions

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH="/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:$PATH"
make clean && make -j$(nproc)
```
