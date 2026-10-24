# NooDS-Wii v1.78 — stylus edge-clamp + quit-to-Wii Menu + audio-off fix + v1.75 max renderer

## Status
**Feature release** — adds **Stylus Edge Clamp** (off-screen cyan-square press now snaps to the nearest NDS touch-screen edge instead of releasing), on top of v1.77's safe Quit-to-Wii Menu and v1.76's audio-off freeze fix. Retains all v1.75 renderer speedups with no change to 2D/3D, timing, or save states. Preserves full accuracy at `frameskip=0`, native Dolphin **60 FPS**. Addresses user request: pressing outside the NDS screen with the stylus should still register a tap at the closest boundary.

## Build
```sh
cd NooDS-Wii-v1pt78
source /etc/profile.d/devkit-env.sh
make -j4
# output: NooDS-Wii.dol (1.7M) NooDS-Wii.elf (3.7M) NooDS-Wii.elf.map + tagged copies
```
Toolchain: `devkitPPC r51 gcc16.2.0 libogc3.2.1` at `/opt/devkitpro`; headless `dolphin-emu-nogui 2503` (offscreen). `frameskip=0` pinned in `main.cpp`.

## What changed vs v1.77
- **Stylus Edge Clamp (`settings.h/cpp` + `main.cpp:~1916`):** New persisted `Settings::stylusEdgeClamp` (default `1 = On`) added to `noods.ini`. Settings dropdown now includes `Stylus Edge Clamp: On/Off` (after `Swap Screens`):
  ```cpp
  { "Stylus Edge Clamp", &Settings::stylusEdgeClamp, 2, s_toggleNames, 2 }
  ```
  Touch mapping (`ScanWiiInputs` → `g_ndsTouchX/Y` / `g_ndsTouching`) now:
  - **On (default):** if `isTouching` (A/ZL/ZR/GC-Z held) and cursor outside `dsLeft..dsRight × dsTop..dsBottom`, clamp `g_cursorX/Y` to rect before mapping, compute `fx/fy` and clamp result to `0..255 / 0..191`. Keeps `isTouching=true` so NDS sees a tap at the nearest edge (e.g., outside right → `touchX=255`, outside top → `touchY=0`). Inside behaviour unchanged.
  - **Off:** restores previous behaviour — outside → `isTouching=false` (no tap).
  Cyan square remains visible/tracked everywhere (`wii_video.cpp` draws it at `g_cursorX/Y` regardless of touch rect); only the SPI touch coordinate is clamped.

- **Quit to Wii Menu retained (`main.cpp:173/203/228/270/1391/~1574`):** `Quit to Wii Menu` row → `QuitToWiiMenuSafely()`: `Settings::save(); runEmulatorThread=false; KThreadJoin; ShutdownAudio(); StopAndDeleteCore(); exit(0);` identical to HOME.

- **Audio-off freeze fix retained (`spu.cpp:pushSample`):** `if (!emulateAudio) return;` prevents SPU ring stall — `OFF` holds 60 FPS.

- **Renderer unchanged:** v1.75 DDA (`DDAGuard`), `fastRead*`, alpha LUT, bright tables, `winMaskCache`, line indices, tex cache, `FORCE_INLINE`/`udivSpan`, `-flto -Wl,--gc-sections`.

- **Makefile:** Added `-DNOODS_V178_OPT` (keeps `V172 V174 V175 V176 V177`).

Drop-in replacement for v1.77 — only adds the clamped touch path and its toggle.

## Accuracy — frameskip=0, native 60 FPS (must preserve)
| Suite | Result |
|-------|--------|
| `mgba-suite-auto.gba` | **6,997/6,998 (99.99% ALL DONE=True)** |
| `nds-2d-agent-suite.nds` | **8/8 (100% exact framebuffer)** |
| `suite-poly.nds` | **28/28 CPU + 77/77 3D** — `expectedPx=102,942 missing=0 overdraw=0 wrongCol=0` |

Clamp only affects interactive off-screen touch; automated suites unaffected (no off-screen presses in test). `OFF` audio still **60 FPS displayed**.

## Performance — MAX_FRAMES=240, frameskip=0, threaded2D=1 threaded3D=1
| Workload | v1.74 | v1.75 | **v1.76–v1.78** |
|----------|-------|-------|----------------|
| GBA suite | ~2,336,000 µs | ~2,335,000 | **~2,335,000** |
| NDS 2D | ~760,000 | ~758,000 | **~758,000** |
| NDS CPU/3D (77 scenes) | ~7,920,000 | ~7,350,000 | **~7,350,000** |
| `wolveslayer` moving | ~34 FPS | ~36–37 FPS | **~36–37 FPS** |

No renderer change; clamp adds ~4 float ops only when touching outside.

## Files
- `NooDS-Wii.dol/.elf/.elf.map` + `NooDS-Wii_jit1_blocks1_gba0_stats0_browser0.*`
- `NooDS-Wii/` full source (`settings.h/cpp`, `main.cpp` clamp, `spu.cpp` audio fix)
- `Makefile` (`-DNOODS_V178_OPT`)
- `LICENSE` GPLv3, `README.md` this file

## Use
In-game: move cyan square with IR or right stick; hold `A` (Wiimote) / `ZL/ZR` (Classic) / `Z` (GC) while outside the bottom screen → stylus now sticks to nearest edge (e.g., drag past right border still draws at `x=255`). Toggle off in Settings → `Stylus Edge Clamp: Off` to restore release-outside.

## Credits
NooDS core Hydr8gon; Wii port & v1.5–v1.78 radicalten; test ROMs mgba auto, nds-2d-agent, suite-poly (Jaklyy), wolveslayer (MIT).
