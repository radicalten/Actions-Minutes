# AGENTS.md — Headless Wii/NDS emulation sandbox
### Running `rockwrestler.nds` inside NooDS-Wii under Dolphin, fully scripted

This document is a cold-start guide. A new agent in a fresh session should be able to
go from empty sandbox to "rockwrestler boots in NooDS-Wii" by following it in order.
Everything below was verified working end-to-end on 2026-09-30.

---

## 0. Environment facts (measured, not assumed)

| Item | Value |
|---|---|
| OS | Debian 13 (trixie), kernel 6.1.158+, x86_64 |
| CPU | 2 vCPU, Intel Xeon @ 2.60 GHz |
| RAM | **1.9 GiB total, no swap** (budget for llvmpipe + Dolphin) |
| Disk | 25 G root, ~20 G free |
| GPU | **none** — Dolphin must use software GL: `LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe` |
| Sound | none — ALSA "No such file" log lines are harmless; use the **mute** NooDS .dol |
| User | `user` (uid 1000), **passwordless sudo** |
| Preinstalled | python3.13, gcc/g++/make, curl/wget/git/xz/bzip2, ImageMagick `import` |
| Display | none — run `Xvfb :99 -screen 0 1280x720x24` and `export DISPLAY=:99` |

**Persistence rules (critical):**
- Only files under `/home/user` survive between agent turns/sessions.
- **NOT persisted:** apt packages, `/opt`, `/tmp`, running processes, env vars,
  and — observed in practice — even some `/home/user` content can be missing after a
  restore (e.g. `~/.config/dolphin-emu/` and extracted `.dol` files have been pruned).
  ⇒ Treat every session as a cold start: re-install packages and **re-create config
  files from this document**; keep canonical binaries as copies that always exist
  (`nds/apps.zip`, `nds/wii-sd.raw`) and re-extract from them.
- Network: github.com and the devkitPro mirror below work; `*.devkitpro.org` is
  **DNS-blocked** (official dkp-pacman unusable).

---

## Quickstart (after a cold start)

```bash
cd /home/user
./setup-devkitppc.sh          # step 1 (idempotent; ~1 min)
source ./dkp-env.sh
sudo apt-get update -qq && sudo apt-get install -y -qq \
     dolphin-emu xvfb imagemagick mtools libgl1-mesa-dri   # step 2
cd nds
python3 -c "import zipfile; zipfile.ZipFile('apps.zip').extractall('.')"  # step 4 restore
# steps 5.x: rebuild the SD image (~5 s, §5.1) and write the five
# ~/.config/dolphin-emu/*.ini files (§5.2), then:
bash run-headless.sh          # boots NooDS-Wii; DSU server drives the browser
```

Workspace helpers (all in `/home/user`): `setup-devkitppc.sh`, `dkp-env.sh`,
`run-dolphin.sh` (smoke-test rig), `nds/run-headless.sh` (full pipeline),
`nds/dsu_server.py` (virtual gamepad), `nds/RUNBOOK.md` (condensed protocol notes),
`wii-toolchain-test/` (devkitPPC self-test: `make` → `hello.dol`).

---

## 1. Install devkitPPC (Wii toolchain)

The official installer/pacman cannot be used (DNS block). `setup-devkitppc.sh`
downloads verbatim devkitPro packages from the mirror
`https://wii.leseratte10.de/devkitPro` and extracts them to `/opt`:

- devkitppc-gcc 16.1.0, binutils 2.46.0, newlib 4.6.0.20260123-4,
  crtls 2.0.0, rules 1.2.1, libogc 3.1.0, gamecube-tools 1.0.3 (`elf2dol`).

```bash
cd /home/user && ./setup-devkitppc.sh && source ./dkp-env.sh
# verify:
powerpc-eabi-gcc --version | head -1
cd wii-toolchain-test && make && ls -l hello.dol
```
`/opt` is wiped every session — re-run the script after each cold start (it is
idempotent and skips re-download when complete).

## 2. Install Dolphin

```bash
sudo apt-get update -qq
sudo apt-get install -y -qq dolphin-emu xvfb imagemagick mtools libgl1-mesa-dri
```
- Headless binary: `/usr/games/dolphin-emu-nogui` (Dolphin 2503-series in Debian trixie).
- Config dir: `~/.config/dolphin-emu/`  (NOT `~/.local/share/dolphin-emu/` — that one
  holds **data**, e.g. `Load/WiiSD.raw`; ini files placed there are ignored).
- Smoke test: `./run-dolphin.sh <file.dol> 90 out.png` then **read** `out.png`.
  Exit code 137 from the rig is expected (SIGKILL after timeout).

## 3. Download rockwrestler.nds

```bash
curl -fsSL -o nds/rockwrestler.nds \
  https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds
```
Expected: 39,433 bytes, md5 `dfd1770daba69955031c0699d33b1dc6`.
(It is an emulator **test-suite** ROM: grey top screen menu, white bottom screen,
"READY" + green ARM7 light top-right once booted.)

## 4. Download NooDS-Wii v1.5

```bash
curl -fsSL -o nds/apps.zip \
  https://github.com/radicalten/NooDS-Wii/releases/download/v1.5/apps.zip
cd nds && python3 -c "import zipfile; zipfile.ZipFile('apps.zip').extractall('.')"
```
Yields `nds/apps/NooDS-Wii/NooDS-Wii-v1pt5-mute.dol` (1,317,864 B; use this one — no
sound card here) and `NooDS-Wiiv1pt5-sound.dol`. Re-extract whenever the `.dol` is
missing (it has been pruned between sessions before).

## 5. Set up Dolphin to run rockwrestler.nds in NooDS-Wii

### 5.1 Emulated Wii SD card

Canonical recipe (the image is 128 MB, which exceeds the ~128 MB snapshot cap, so
`nds/wii-sd.raw` usually does NOT survive a session — just rebuild it, it is cheap):

```bash
cd nds
truncate -s 128M wii-sd.raw
python3 - <<'EOF'
import struct
mbr = bytearray(512)
mbr[0x1BE:0x1CE] = struct.pack('<B3sB3sII', 0x00, b'\x00\x02\x00', 0x0C,
                               b'\xfe\xff\xff', 2048, 260096)
mbr[510], mbr[511] = 0x55, 0xAA
open('wii-sd.raw','r+b').write(mbr)
EOF
mformat -i wii-sd.raw@@1M ::
mmd     -i wii-sd.raw@@1M ::/noods
mcopy   -i wii-sd.raw@@1M rockwrestler.nds ::/noods/
```
Deploy before every run (the whole `~/.local/share/dolphin-emu` tree may be absent on a
cold start, and Dolphin may rewrite the file):
```bash
mkdir -p ~/.local/share/dolphin-emu/Load
cp nds/wii-sd.raw ~/.local/share/dolphin-emu/Load/WiiSD.raw
```

### 5.2 Dolphin config files — write these on EVERY cold start

`mkdir -p ~/.config/dolphin-emu`, then:

`~/.config/dolphin-emu/Dolphin.ini`
```ini
[Core]
WiiSDCard = True

[Input]
BackgroundInput = True
```

`~/.config/dolphin-emu/DSUClient.ini`  (filename proven by strace; NOT "DualShockUDPClient.ini")
```ini
[Server]
Enabled = True
Entries = local:127.0.0.1:26760;
```

`~/.config/dolphin-emu/WiimoteNew.ini`
```ini
[Wiimote1]
Source = 1
Device = DSUClient/0/local
Buttons/A = `Cross`
D-Pad/Down = `Pad S`
D-Pad/Up = `Pad N`
```

`~/.config/dolphin-emu/GCPadNew.ini`
```ini
[GCPad1]
Device = DSUClient/0/local
Buttons/A = `Cross`
D-Pad/Down = `Pad S`
D-Pad/Up = `Pad N`
```

`~/.config/dolphin-emu/Logger.ini`
```ini
[Options]
WriteToConsole = True
Verbosity = 0

[Logs]
CI = True
Wiimote = True
IOS_SD = True
BOOT = True
COMMON = True
CORE = True
```
⚠️ nogui opens **only** `WiimoteNew.ini`/`GCPadNew.ini` for pads (strace-proven).
Editing plain `Wiimote.ini`/`GCPad.ini` does nothing. With `CI = True` the log line
`Added device: DSUClient/0/local` confirms the virtual pad connected.

### 5.3 Input: do NOT use xdotool — use the DSU server

`xdotool`/XTEST can never work here: headless X servers (Xvfb and Xorg-dummy) emit no
XI2 *raw* events, and Dolphin's XInput2 backend subscribes raw-only (proven with a
minimal XI2 listener). The working route is Dolphin's **Cemuhook DualShockUDPClient**:
run `nds/dsu_server.py`, a tiny UDP server (127.0.0.1:26760) that speaks the exact
Dolphin-2503 protocol (CRC32 = zlib, stored **inside the header at offset 8**;
PortInfo has no name field; device qualifier = `DSUClient/<pad>/<Entries-desc>`).
It presses buttons on a timeline measured from server start:

- t≈20–66 s: three self-healing `A,A,Down,A` sequences drive NooDS-Wii's SD browser.
  Browser facts (NooDS-Wii `src-main.cpp`): starts in `sd:/noods/`, list
  `[.., rockwrestler.nds]`; movement is **edge-triggered** with auto-repeat after 18
  frames ⇒ Down presses must be ≤0.25 s; `A` on a file loads it; the `A,A` prefix
  normalizes the cursor so the sequence is idempotent.
- t≈75–106 s: in-game `Down`/`A` presses (GC A → NDS A; Wiimote 2 → NDS A; Wiimote A →
  NDS X) to demonstrate the game reacting to input.

If `nds/dsu_server.py` is missing, rebuild it from the protocol facts in
`nds/RUNBOOK.md` (header layout, PortInfo/PadDataResponse field order, 100-byte pad
packet) — a Python round-trip test against itself catches layout errors cheaply.

### 5.4 Run

```bash
cd nds && bash run-headless.sh
# --- manual equivalent of the script, if preferred:
mkdir -p ~/.local/share/dolphin-emu/Load && cp wii-sd.raw ~/.local/share/dolphin-emu/Load/WiiSD.raw
(setsid python3 dsu_server.py >/tmp/dsu.log 2>&1 &)
pgrep -x Xvfb >/dev/null || { Xvfb :99 -screen 0 1280x720x24 & sleep 2; }
export DISPLAY=:99
timeout -s KILL 215 env LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  /usr/games/dolphin-emu-nogui -p x11 -v OGL -a HLE \
  -e apps/NooDS-Wii/NooDS-Wii-v1pt5-mute.dol
```
Timeline (wall seconds from server start): boot ~5–15 s, browser visible ~30–50 s,
ROM load triggered ~60–70 s, game running by ~75 s. Exit 137 is expected.
Pacing is far below real time (2 vCPU + llvmpipe) — never trust A/V timing.

### 5.5 What success looks like (read the captures)

`import -window root cap.png` at the times above; then **view** the PNG:
1. Browser: black screen, `Dir: sd:/noods/`, `-> rockwrestler.nds`,
   `A=Load B=Back B++=Settings` (cursor moved off `..` ⇒ input works).
2. Booted: NooDS HUD `FPS: 61.6 / HOME=Quit B++=Settings`, grey top screen with
   `READY` + green battery/ARM7 light and `● CONDITION CODES`, white bottom screen.
3. Later captures show a different game screen ⇒ in-game input works.
Reference captures: `nds/cap21-a.png` (browser), `nds/cap21-c.png` (booted),
`nds/cap22-a.png` (in-game reaction).

## 6. Other necessary steps & pitfalls (all paid for once — don't pay again)

1. **Re-install apt packages every session** (`dolphin-emu xvfb imagemagick mtools
   libgl1-mesa-dri`; add `strace xdotool xinput` only when debugging).
2. **Re-run `setup-devkitppc.sh` and `source dkp-env.sh` every session** (/opt wiped).
3. **Re-create the five ini files every session** (§5.2) — they have been pruned before.
4. SD image: restore with `cp` before each run; inspect with
   `mdir -i wii-sd.raw@@1M ::/noods` (`strings` on FAT gives false negatives).
5. `pkill -f` self-matches your own shell when the pattern text appears in the command
   (e.g. via a start command). Use bracket tricks (`pkill -f 'dsu[_]server'`) in a
   command that contains no other literal match, or PID files.
6. DSU details that cost days once: CRC32 goes at header byte 8 (not appended); response
   payloads repeat `msg_type`; 2503 `PortInfo` = id,state,model,conn,mac,batt,pad (no
   name — the name comes from `Entries` description); server must *stream* PadData at
   ~120 Hz after registration; `Entries` format `desc:addr:port;`.
7. NooDS-Wii browser input is edge-triggered; holds >18 frames auto-repeat and wrap the
   2-entry list. In-game key map: Wiimote 2→A, 1→B, A→X, B→Y, +→START, −→SELECT;
   GC A→A, B→B, X→X, Y→Y, Start→START.
8. raw.githubusercontent paths for Dolphin 2503: `Source/Core/Common/Config/Config.cpp`
   (no ConfigLoaders dir), DSU at `InputCommon/ControllerInterface/DualShockUDPClient/`.
9. Never present an unverified run as done: view the captures, check the `Added device`
   log line, and confirm the screen changed between captures.

## 7. File map

```
/home/user/AGENTS.md               this guide
/home/user/setup-devkitppc.sh      step 1 (mirror install, idempotent)
/home/user/dkp-env.sh              source before Wii builds
/home/user/run-dolphin.sh          step 2 smoke-test rig
/home/user/wii-toolchain-test/     devkitPPC compile test (make -> hello.dol)
/home/user/nds/rockwrestler.nds    step 3 (md5 dfd1770daba69955031c0699d33b1dc6)
/home/user/nds/apps.zip            step 4 canonical release zip
/home/user/nds/apps/NooDS-Wii/     extracted .dol files (re-extract if missing)
/home/user/nds/wii-sd.raw          SD image (rebuild per §5.1; too big to persist)
/home/user/nds/dsu_server.py       step 5.3 virtual DS4 (Cemuhook DSU)
/home/user/nds/run-headless.sh     step 5.4 one-shot pipeline
/home/user/nds/RUNBOOK.md          condensed DSU protocol + browser facts
/home/user/nds/src-*.cpp           NooDS-Wii v1.5 sources (input-map reference)
/home/user/nds/cap*.png            evidence captures
```
