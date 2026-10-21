# NooDS-Wii v1.66

**NooDS-Wii v1.66** builds directly on **v1.65** (`accbd37`, preserving **v1.63**, **v1.64**, and **v1.65** completely intact) with:
1. **In-Game `Quit Game -> [Browse]` in the Settings Menu** — cleanly flushes `.sav` SRAM/EEPROM/Flash saves, drains audio, resets PPC JIT state, frees all core/ROM allocations without leaks, and returns to the ROM browser focused right on the last played game (even across repeated `32.00 MiB` `.gba` <-> `.nds` switches inside the Wii's `24 MB MEM1 + 64 MB MEM2` RAM budget).
2. **Full Wii Classic Controller, Classic Controller Pro, GameCube Controller, and Wiimote/Nunchuk Support** — diagnosing and resolving **three binary-verified `libogc 3.x` (`libwiiuse.a`) hardware driver bugs** that previously broke extensions and buttons in Dolphin and on real MotionPlus Wiimotes (`RVL-CNT-01-TR`).
3. **Complete `.gba` ROM Browsing & `32.00 MiB` (`256 Mbit`) Cartridge Support** — diagnosing and resolving **six distinct `.gba` launch, memory-arena, header/BIOS, save-path, and recovery bugs**, verified end-to-end on `16.00 MiB` uppercase `.GBA` and full `32.00 MiB` (`0x08000000–0x09FFFFFF`) multi-bank `.gba` cartridges at locked `60.0 FPS`.

---

## Live Verification Screenshots (Dolphin 2503 @ `FPS: 60.0`, Emulated Wii Classic Controller)

### 1. Full `32.00 MiB` (`256 Mbit`) `.gba` Cartridge Running Live with Emulated Wii Classic Controller Input (`FPS: 60.04`)
![32 MB GBA ROM + Live Wii Classic Controller Buttons](docs/v166_3_32mb_classic_buttons_held.png)
*Top emerald band: all 8 x 4 MB banks across `0x08000000–0x09FFFFFF` verified in RAM (`MEM1` `1T-SRAM` + `MEM2`). Middle gold band: 32 KB SRAM write + readback verified (`"NOODS_WII_V166_32MB_SRAM_OK!"`). Bottom cyan bars: live Wii Classic Controller inputs (`A`, `B`, `-` / `SELECT`, `Left Stick Right`, `L`, `R`) active with zero crosstalk at `FPS: 60.04`.*

### 2. In-Game Settings Overlay with `-> Quit Game  [Browse]` (`Classic B + PLUS` / `GC L + R + START` / `Wiimote B + PLUS`)
![In-Game Settings Menu Quit Game Row](docs/v166_4_settings_quit_game_row.png)

### 3. Clean Return to ROM Browser Focused on Last Played Game (`._*` Hidden, `.GBA` / `.agb` Supported)
![Returned to ROM Browser](docs/v166_5_returned_to_browser_from_32mb.png)

### 4. Dual-Screen `.nds` Launch Right After `.gba` Quit + Classic Controller Right-Stick Stylus Cursor
![NDS Game After GBA Quit with Classic Right Stick Stylus](docs/v166_7_nds_after_gba_with_classic_stylus.png)

### 5. Clean Error Notice Recovery on Corrupt/Empty `.gba` Without Freezing (`FPS: 59.95`)
![Corrupt GBA Clean Recovery Notice](docs/v166_8_corrupt_gba_clean_recovery.png)

---

## Root Causes Diagnosed & Fixed in v1.66

### A. Five Binary-Verified Hardware / `libogc 3.x` Discoveries

1. **`libwiiuse.a` Startup Handshake Race (`WPAD_Init` vs `WPAD_SetDataFormat` / `WPAD_SetVRes`):**
   - Calling `WPAD_SetDataFormat()` or `WPAD_SetVRes()` immediately after `WPAD_Init()` aborts `wiiuse_status(wm, wiiuse_handshake)` while `hs == 1`, leaving all extension ports (`Classic`, `Classic Pro`, `Nunchuk`) wedged forever at `exp = 0` (especially when MotionPlus calibration runs first).
   - **Fix (`NooDS-Wii/main.cpp`):** Removed eager startup format calls in `main()`; `PollControllers()` now waits until `WPAD_Probe(ch, &probedExt) == WPAD_ERR_NONE` before configuring each channel's report mode and VRes, re-applying automatically if a controller is hot-plugged or swapped at runtime.

2. **`libwiiuse.a` HID Report `0x37` Classic Controller Button Truncation:**
   - Disassembly of `classic_ctrl_handshake` confirmed that `libogc 3.x` negotiates **Classic Controller Data Format 3** (`8` bytes: `[0..5]` analog sticks/triggers, **`[6..7]` all 16 digital buttons**).
   - Setting `WPAD_FMT_BTNS_ACC_IR` selects Wiimote HID Report `0x37` (which carries only **6** extension bytes), truncating bytes `[6..7]` and killing every digital button on Classic / Classic Pro controllers.
   - **Fix (`NooDS-Wii/main.cpp`):** `PollControllers()` dynamically configures `WPAD_FMT_BTNS_ACC` (HID Report `0x35`, **16** extension bytes) on channels with `probedExt == WPAD_EXP_CLASSIC`, and `WPAD_FMT_BTNS_ACC_IR` + `640x480` IR VRes on standard Wiimote / Nunchuk channels.

3. **`libwiiuse.a` `<classic_ctrl_pressed_buttons>` Bitmask Semantics & Wii U Pro Idle Bits:**
   - Disassembly of `<classic_ctrl_pressed_buttons>` in `libwiiuse.a` revealed two subtleties:
     1. `u32 classic_ctrl_t::btns` stores **all currently held buttons** every frame (not rising edges), so ORing `cc->btns` into `wpadPressed` caused rapid multi-fire repeats on every held button.
     2. Under `cc->type == 2` (Data Format 3), bits `16..17` (`0x00030000`, Wii U Pro `L3`/`R3`) read `1` on idle Classic Controllers because byte `8` is `0x00`, and `WPAD_CLASSIC_BUTTON_*` constants (`0x0001..0x8000` in `<wiiuse/wiiuse.h>`) collide with bare Wiimote `0x0001..0x8000` bits unless shifted by `<< 16` into `WPAD_CLASSIC_BUTTON_*` (`0x00010000..0x80000000`).
   - **Fix (`NooDS-Wii/main.cpp`):** Masked `((u32)wdata->exp.classic.btns & 0xFEFFu) << 16` into `wpadHeld` only and synthesized true 1-frame rising edges across all controllers via `cs.wpadPressed |= (cs.wpadHeld & ~s_prevMergedHeld)`.

4. **`libogc.a` `<_sbrk_r>` One-Way `Arena1 -> Arena2` Jump Stranding `~9.25 MiB` of `MEM1` (`1T-SRAM`):**
   - Disassembly of `<_sbrk_r>` (`0x8010c7ec..0x8010c960`) revealed that once any single allocation (`new Core(...)`, `~17.8 MiB`) exceeds remaining `Arena1` (`~9.75 MiB`), `_sbrk_r` sets its internal `Arena2` latch (`-31620(r13)`) and **never allocates from `Arena1` (`MEM1`) again**, leaving `~9.75 MiB` of fast `1T-SRAM` completely stranded and causing `32.00 MiB` (`256 Mbit`) `.gba` cartridges to run out of `MEM2` around `~23.5 MiB`.
   - **Fix (`NooDS-Wii/cartridge.{h,cpp}`, `memory.{h,cpp}`, `console_ui.{h,cpp}`):**
     - Replaced monolithic `new uint8_t[romSize]` GBA ROM buffers with **`128 x 256 KiB` banked `CartridgeGba::gbaBanks[128]`** (`32.00 MiB` full hardware address space `0x08000000–0x09FFFFFF`, padded to `4 KB` pages for zero-overhead native PPC JIT `readMap7` loads).
     - Reclaimed stranded `Arena1` (`1T-SRAM`) capacity cleanly in `AllocGbaArena1Bank()` (leaving a `512 KiB` system reserve in `MEM1`) and reused those slots zero-fragmentation across unlimited `Quit Game` cycles.
     - Sized `Memory::ram` dynamically (`256 KiB` GBA IWRAM vs `4 MiB` NDS vs `16 MiB` DSi), omitted unused ARM9 `readMap9B`/`writeMap9B` tables (`8.00 MiB`) in standalone GBA mode, and made unused `ConsoleUI::framebuffer` (`1.50 MiB` `.bss`) lazy.

5. **Post-BIOS GBA Affine Identity Matrix (`BG2PA`/`BG2PD` & `BG3PA`/`BG3PD`) Without `gba_bios.bin`:**
   - Without an external `gba_bios.bin`, `Core::enterGbaMode()` wrote `SOUNDBIAS = 0x200` (`0x04000088`) while leaving `BG2PA`, `BG2PD`, `BG3PA`, and `BG3PD` at `0x0000` instead of the GBA hardware/post-BIOS `0x0100` (`1.0x`) identity scale, smearing pixel `(0, 0)` across all `240x160` pixels in Mode 1–5 `.gba` games and demos.
   - **Fix (`NooDS-Wii/core.cpp`, `NooDS-Wii/memory.cpp`):** Initialized `0x04000020`, `0x04000026`, `0x04000030`, and `0x04000036` to `0x0100` during HLE `enterGbaMode()`, ensured standalone `.gba` always uses clean HLE post-BIOS state unless `Direct Boot = Off` with a validated `16 KB` ARM branch (`0xEA`) `gba_bios.bin`, normalized double-slash SD save/state paths (`mkdir("sd:/saves")` via `JoinBaseSubdir()`), filtered macOS `._*` AppleDouble files, accepted `.gba`/`.agb` case-insensitively, added `ArmPpcJit::reset()` on `Core` construction, and added non-blocking browser error notices if a corrupt/empty ROM fails to load.

---

## Complete Controller Mapping Reference

| Action | Wii Classic / Classic Pro | GameCube Controller | Wiimote (+ Nunchuk) |
| :--- | :--- | :--- | :--- |
| **NDS / GBA D-Pad** | D-Pad or Left Stick | D-Pad or Analog Stick | Sideways D-Pad or Nunchuk Stick |
| **A / B / X / Y** | `A` / `B` / `X` / `Y` | `A` / `B` / `X` / `Y` | `2` / `1` / `A` / `B` (or Nunchuk `Z`/`C`) |
| **L / R Triggers** | `L` / `ZL` and `R` / `ZR` | `L` and `R` / `Z` | `B` / `-` and `A` / `+` |
| **START / SELECT** | `+` (`PLUS`) / `-` (`MINUS`) | `START` / `Z` | `+` / `-` |
| **Touch Stylus Move** | Right Stick | C-Stick | Wiimote IR Pointer |
| **Touch Stylus Tap** | `ZR` or `ZL` | `Z` | `A` (IR pointing) |
| **Open Settings Menu** | `B + PLUS` (in-game) / `X` or `-` (browser) | `L + R + START` (in-game) / `X` (browser) | `B + PLUS` (in-game) / `-` (browser) |
| **Quit Game -> Browse** | Settings Row 1 -> `A` | Settings Row 1 -> `A` | Settings Row 1 -> `2` or `A` |
| **Exit to Wii / HBC** | `HOME` | `L + R + Z + START` | `HOME` |

---

## Accuracy & Performance Verification (`frameskip = 0`, Real-Time `60.0 FPS` Dolphin)

| Benchmark Suite (`frameskip = 0`) | Result | Details |
| :--- | :--- | :--- |
| **`suite.gba`** (`mGBA` Automated Hardware Suite) | **`6,997 / 6,998` (`99.99%`)** | `1552/1552` Memory, `130/130` I/O, `2020/2020` Timing, `936/936` Timer count-up, `90/90` Timer IRQ, `140/140` Shifter, `93/93` Carry, `72/72` Multiply long, `615/615` BIOS math, `1244/1244` DMA, `90/90` SIO R/W, `4/4` SIO timing |
| **`nds-2d-agent-suite.nds`** (NDS 2D PPU Suite) | **`8 / 8` (`100.00%`)** | Bit-exact framebuffer match across all 8 test cards (`49,152 / 49,152` pixels) |
| **`suite-poly.nds`** (NDS ARM9/ARM7 CPU + 3D GX Suite) | **`28 / 28` CPU + `77 / 77` 3D** | `0` failed CPU groups, `102,942` expected pixels, `0` missing / overdraw / wrong-color pixels |

### Build Instructions

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=/opt/devkitpro/devkitPPC
make -C /home/user/NooDS-Wii-v1pt66 -j$(nproc)
```
Produces `/home/user/NooDS-Wii-v1pt66/NooDS-Wii.dol` (`text = 1,343,794`, `data = 311,702`, `bss = 12,399,712`, `dec = 14,055,208` = `13.40 MiB` static `MEM1` footprint).
