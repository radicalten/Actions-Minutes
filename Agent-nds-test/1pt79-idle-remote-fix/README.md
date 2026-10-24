# NooDS-Wii v1.79 — idle-input fix (5 min disconnect) + stylus clamp + quit-to-Wii Menu

## Status
**Bugfix release** — fixes the idle controller lock after a few minutes without input (game kept running but no controller responded). Root cause: libogc default `__wpad_idletimeout=300` disconnects Wiimotes with `WPAD_DISCON_IDLE_TIMEOUT (0x16)` after 5 min no input; after that the emulator's cached `WPAD_FMT_BTNS_ACC_IR` never re-established cleanly. Retained v1.78 stylus edge-clamp, v1.77 quit-to-Wii Menu, v1.76 audio-off fix, and v1.75 max renderer. Preserves accuracy at `frameskip=0`, native Dolphin **60 FPS**.

## Build
```sh
cd NooDS-Wii-v1pt79
source /etc/profile.d/devkit-env.sh
make -j4
# output: NooDS-Wii.dol (1.7M) NooDS-Wii.elf (3.7M) NooDS-Wii.elf.map + tagged copies
```
Toolchain: `devkitPPC r51 gcc16.2.0 libogc3.2.1` at `/opt/devkitpro`; headless `dolphin-emu-nogui 2503` (offscreen). `frameskip=0` pinned.

## What changed vs v1.78
- **Idle disconnect fix (`main.cpp:1972` + `PollControllers:~1253`):**
  - After `WPAD_Init()` now `WPAD_SetIdleTimeout(3600)` — extends idle power-off from 5 min (libogc default 300s) to 60 min. Idle reading/dialogue no longer kills input; still powers off eventually for battery save. Does **not** affect GameCube (`PAD_ScanPads` has no idle timer — `PAD_Init` only).
  - `PollControllers` now invalidates `s_chanFmt[ch]` on **any** non-`WPAD_ERR_NONE` probe (was only `NO_CONTROLLER`), forcing a clean `WPAD_SetDataFormat/WPAD_SetVRes` re-handshake on next `WPAD_ERR_NONE`. This restores IR + Classic/Nunchuk extension reliably after a wake, unplug/replug, or sync after the rare idle power-off still occurs after 1 h. Never calls `SetDataFormat` inside the `else` (would abort `hs=1` handshake) — just invalidates cached format and resets `s_classicWarmup` on `NO_CONTROLLER`.
  - GameCube pad input (`PAD_ButtonsHeld/Down`, `PAD_StickX/Y`, `PAD_SubStick`, `PAD_TriggerL/R`) merged every frame regardless of Wiimote state — GC never had the idle bug, now explicitly documented.

- **Retained:** `stylusEdgeClamp` (default On, `noods.ini`, clamps off-screen cyan-square press to `0..255/0..191`), `Quit to Wii Menu` (`QuitToWiiMenuSafely` → `Settings::save(); KThreadJoin; ShutdownAudio; StopAndDeleteCore; exit(0)`), audio-off early-return in `spu.cpp:pushSample`, v1.75 DDA renderer (`fastRead*`, alpha LUT, bright tables, `winMaskCache`, line lists, tex cache, `DDAGuard`, `-flto`).

- **Makefile:** Added `-DNOODS_V179_OPT`.

Drop-in replacement for v1.78 — only changes Wiimote idle handling; no 2D/3D, timing, or save-state change.

## Accuracy — frameskip=0, native 60 FPS (must preserve)
| Suite | Result |
|-------|--------|
| `mgba-suite-auto.gba` | **6,997/6,998 (99.99% ALL DONE=True)** |
| `nds-2d-agent-suite.nds` | **8/8 (100% exact framebuffer)** |
| `suite-poly.nds` | **28/28 CPU + 77/77 3D** — `expectedPx=102,942 missing=0 overdraw=0 wrongCol=0` |

Idle fix is input-path only; automated suites unaffected (continuous input, never idle 5 min).

## Performance — MAX_FRAMES=240, frameskip=0, threaded2D=1 threaded3D=1
| Workload | v1.74 | v1.75 | **v1.76–v1.79** |
|----------|-------|-------|----------------|
| GBA suite | ~2,336,000 µs | ~2,335,000 | **~2,335,000** |
| NDS 2D | ~760,000 | ~758,000 | **~758,000** |
| NDS CPU/3D (77 scenes) | ~7,920,000 | ~7,350,000 | **~7,350,000** |
| `wolveslayer` moving | ~34 FPS | ~36–37 FPS | **~36–37 FPS** |

No renderer change.

## Files
- `NooDS-Wii.dol/.elf/.elf.map` + `NooDS-Wii_jit1_blocks1_gba0_stats0_browser0.*`
- `NooDS-Wii/` full source (`main.cpp` idle fix, `settings.h/cpp`, `spu.cpp`)
- `Makefile` (`-DNOODS_V179_OPT`)
- `LICENSE` GPLv3, `README.md` this file

## Use
Leave the game idle as long as you like (pause, cutscene, reading) — Wiimote stays connected for 60 min. After 1 h it will still power off for battery, but on next button press it now reconnects and input resumes (Classic/Nunchuk/Pro re-handshake restored). GameCube controllers unaffected and remain instant.

## Credits
NooDS core Hydr8gon; Wii port & v1.5–v1.79 radicalten; test ROMs mgba auto, nds-2d-agent, suite-poly (Jaklyy), wolveslayer (MIT). Fix validated against libogc `__wpad_idletimeout=300` default and `WPAD_DISCON_IDLE_TIMEOUT=0x16`.
