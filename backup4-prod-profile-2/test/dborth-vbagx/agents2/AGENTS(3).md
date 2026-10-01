# AGENTS.md — Headless Wii/NDS emulation sandbox
### Running `rockwrestler.nds` in NooDS-Wii under Dolphin

This is a corrected runbook for the Debian 13 / Dolphin 2503 setup described in `AGENTS(2).md`.

**Verification status (2026-09-30):** The ROM download and checksums, NooDS-Wii release archive, relevant upstream Dolphin/NooDS source details, and the corrected SD-image recipe were spot-checked. The full timed Dolphin boot/input sequence was **not** independently rerun during this review. The original guide refers to helper scripts, protocol notes, and captures that were not included with the supplied file; sections depending on those companion files are explicitly marked below. Do not report an end-to-end run as successful without fresh logs and viewed captures.

---

## 0. Environment facts and assumptions

The following measurements were observed in the verification runtime on 2026-09-30. Resource values and persistence behavior are specific to this sandbox and can change between sessions.

| Item | Value |
|---|---|
| OS / kernel | Debian 13.6 (trixie), kernel `6.1.158+`, x86_64 |
| CPU | 2 vCPU |
| RAM | About 1.9 GiB total, no swap |
| Disk | 25 GiB root, about 20 GiB free |
| GPU | The original run notes report no GPU; this review did not independently probe the device. If software rendering is needed, use `LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe`. |
| Sound | The original run notes report no sound device; this review did not independently test audio. Use the mute NooDS DOL for that setup. |
| User | `user` (uid 1000), passwordless sudo was available in the verification runtime |
| Preinstalled | Python 3.13, GCC/G++, make, curl/wget/git/xz/bzip2, ImageMagick `import` |
| Display | No `DISPLAY` was set and Xvfb was not installed initially. The recipe uses Xvfb on `:99`. |

**Persistence / cold-start rules (sandbox-specific):**
- The original run notes report that apt packages, `/opt`, `/tmp`, processes, and environment variables do not survive a cold start. Some files under `/home/user` may also be pruned. Treat those observations as specific to this agent workspace, not as general Linux behavior.
- Recheck required files at the start of a new session. Keep user-owned scripts and inputs under `/home/user`; reinstall OS packages, recreate Dolphin INI files, and rebuild/redeploy the SD image when needed.
- Only `AGENTS(2).md` was supplied for this review. The helper scripts and evidence listed in §7 were not present here. A full run therefore requires those companion files (especially `nds/dsu_server.py` and `nds/run-headless.sh`) to be supplied or independently rebuilt and tested.

**Network note:** The `wii.leseratte10.de/devkitPro/` mirror root and its `devkitPPC/` index were reachable during review. `pkg.devkitpro.org` resolved, but an HTTPS request to `/packages/` returned Cloudflare HTTP 403. Do not describe the current result as a DNS failure or assume network access is stable; test the endpoint and use the mirror fallback if the official service is inaccessible.

---

## Quickstart (after a cold start)

The script-based launch requires the helper files listed in §7 to be supplied separately; they were not included with the source guide attached for this review. The devkitPPC install is optional for running the already-built NooDS DOL; it is needed only for Wii homebrew builds or the toolchain self-test.

```bash
cd /home/user

# Optional: toolchain install/self-test. See §1; not needed just to run NooDS.
# ./setup-devkitppc.sh
# source ./dkp-env.sh
# cd wii-toolchain-test && make && ls -l hello.dol
# cd /home/user

sudo apt-get update -qq
sudo apt-get install -y -qq \
    dolphin-emu xvfb imagemagick mtools libgl1-mesa-dri

mkdir -p nds
curl -fsSL -o nds/rockwrestler.nds \
    https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds
printf '%s  %s\n' 'dfd1770daba69955031c0699d33b1dc6' 'nds/rockwrestler.nds' | md5sum -c -
printf '%s  %s\n' 'f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a' 'nds/rockwrestler.nds' | sha256sum -c -

curl -fsSL -o nds/apps.zip \
    https://github.com/radicalten/NooDS-Wii/releases/download/v1.5/apps.zip
printf '%s  %s\n' 'd676f9315195b509060bc4288e2db0af30a1c99e37bca0b37f37a85c2c893754' 'nds/apps.zip' | sha256sum -c -
(cd nds && python3 -c "import zipfile; zipfile.ZipFile('apps.zip').extractall('.')")
```

The hashes above match the artifacts fetched during this review. The ROM URL uses the mutable `master` branch: if either ROM hash check fails, stop and inspect the downloaded file rather than silently changing the expected hash. The NooDS release asset is also integrity-checked by its recorded SHA-256.

Next, run the SD-image commands in §5.1 and the INI-writing commands in §5.2. Only after those steps are complete, launch the pipeline (which requires the companion files in §7):

```bash
cd /home/user/nds
bash run-headless.sh
```

---

## 1. Optional: install devkitPPC (Wii toolchain)

This section validates a Wii homebrew toolchain; it is **not a runtime dependency** for loading the prebuilt NooDS `.dol` in Dolphin.

The original run notes say `setup-devkitppc.sh` downloads devkitPro packages from `https://wii.leseratte10.de/devkitPro` and extracts them under `/opt`. The notes list these versions:

- devkitppc-gcc 16.1.0, binutils 2.46.0, newlib 4.6.0.20260123-4,
  crtls 2.0.0, rules 1.2.1, libogc 3.1.0, gamecube-tools 1.0.3 (`elf2dol`).

Those exact package URLs, checksums, and versions were **not** independently checked here because the installer script was not supplied. Review that script before running it; prefer checksum verification for downloaded packages.

```bash
cd /home/user
./setup-devkitppc.sh
source ./dkp-env.sh
powerpc-eabi-gcc --version | head -1
cd wii-toolchain-test && make && ls -l hello.dol
```

Re-run after a cold start if `/opt` was cleared. The installer and environment script are companion files, not included in the supplied `AGENTS(2).md` attachment.

---

## 2. Install Dolphin and host-side tools

```bash
sudo apt-get update -qq
sudo apt-get install -y -qq \
    dolphin-emu xvfb imagemagick mtools libgl1-mesa-dri
```

- On Debian trixie, the package is in the Dolphin 2503 series; the package file list includes `/usr/games/dolphin-emu-nogui`.
- The expected config directory is `~/.config/dolphin-emu/`.
- Dolphin data such as `Load/WiiSD.raw` belongs under `~/.local/share/dolphin-emu/`, not alongside the INI files.
- The original notes mention `./run-dolphin.sh <file.dol> 90 out.png` as a smoke-test rig. That script was not supplied here, so its timeout behavior and exit code cannot be checked from this file alone. Exit 137 means the process was killed by SIGKILL; interpret it as an expected timeout only when the rig’s timeout was actually reached.

---

## 3. Download and verify `rockwrestler.nds`

```bash
mkdir -p /home/user/nds
curl -fsSL -o /home/user/nds/rockwrestler.nds \
    https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds
printf '%s  %s\n' 'dfd1770daba69955031c0699d33b1dc6' \
    '/home/user/nds/rockwrestler.nds' | md5sum -c -
printf '%s  %s\n' 'f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a' \
    '/home/user/nds/rockwrestler.nds' | sha256sum -c -
```

Expected: **39,433 bytes**, MD5 `dfd1770daba69955031c0699d33b1dc6`, SHA-256 `f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a` for the copy checked on 2026-09-30. The upstream project describes Rockwrestler as a work-in-progress NDS emulator test ROM. Its README explains the ARM7 status light and the condition-code test. The ROM branch is mutable, so keep the checksum check.

---

## 4. Download NooDS-Wii v1.5

```bash
curl -fsSL -o /home/user/nds/apps.zip \
    https://github.com/radicalten/NooDS-Wii/releases/download/v1.5/apps.zip
printf '%s  %s\n' 'd676f9315195b509060bc4288e2db0af30a1c99e37bca0b37f37a85c2c893754' \
    '/home/user/nds/apps.zip' | sha256sum -c -
cd /home/user/nds
python3 -c "import zipfile; zipfile.ZipFile('apps.zip').extractall('.')"
```

The checked release archive was **1,190,336 bytes** with SHA-256 `d676f9315195b509060bc4288e2db0af30a1c99e37bca0b37f37a85c2c893754`. It contains:

- `apps/NooDS-Wii/NooDS-Wii-v1pt5-mute.dol` — **1,317,864 bytes**; use this one when no sound device is available.
- `apps/NooDS-Wii/NooDS-Wiiv1pt5-sound.dol` — same byte size.

Re-extract from `apps.zip` if the `.dol` is missing. Do not use `extractall` on an untrusted archive; this command is for the checked upstream release asset.

---

## 5. Set up Dolphin to run `rockwrestler.nds` in NooDS-Wii

### 5.1 Build and deploy the emulated Wii SD card

This recipe creates a 128 MiB image with one partition beginning at LBA 2048. On Debian’s mtools 4.0.48, the default `mformat` command formats this 127 MiB partition as **FAT16**. The MBR therefore uses type `0x0E` (FAT16 LBA), not `0x0C` (FAT32 LBA). `-H 2048` also records the partition start in the FAT BPB’s hidden-sector field.

```bash
cd /home/user/nds
truncate -s 128M wii-sd.raw
python3 - <<'EOF'
import struct
mbr = bytearray(512)
# LBA 2048..262143 (260096 sectors). CHS hints use 255 heads / 63 sectors;
# the LBA fields are authoritative for this LBA partition.
mbr[0x1BE:0x1CE] = struct.pack(
    '<B3sB3sII', 0x00, b'\x20\x21\x00', 0x0E,
    b'\x51\x01\x10', 2048, 260096
)
mbr[510], mbr[511] = 0x55, 0xAA
with open('wii-sd.raw', 'r+b') as f:
    f.write(mbr)
EOF

mformat -H 2048 -i wii-sd.raw@@1M ::
mmd     -i wii-sd.raw@@1M ::/noods
mcopy   -i wii-sd.raw@@1M rockwrestler.nds ::/noods/
mdir    -i wii-sd.raw@@1M ::/noods
```

Expected `mdir` output includes `rockwrestler.nds`. The corrected recipe was run during review; the image was 134,217,728 bytes, the partition type was `0x0E`, the BPB hidden-sector count was 2048, and `mdir` listed the ROM. If you intentionally change the filesystem to FAT32, use a FAT32-specific format recipe and verify its BPB and MBR type together; that alternative was not tested here.

Deploy before every run (Dolphin may modify the image, and data files may be absent after a cold start):

```bash
mkdir -p ~/.local/share/dolphin-emu/Load
cp /home/user/nds/wii-sd.raw ~/.local/share/dolphin-emu/Load/WiiSD.raw
```

Inspect the filesystem with `mdir -i wii-sd.raw@@1M ::/noods`. `strings` on a FAT image is not a reliable directory listing.

### 5.2 Write Dolphin configuration files on every cold start

The Dolphin 2503 source names the new-format pad files `WiimoteNew.ini` and `GCPadNew.ini`, and the DSU settings file `DSUClient.ini`. The commands below write all five files into the config directory.

```bash
mkdir -p ~/.config/dolphin-emu

cat > ~/.config/dolphin-emu/Dolphin.ini <<'EOF'
[Core]
WiiSDCard = True

[Input]
BackgroundInput = True
EOF

cat > ~/.config/dolphin-emu/DSUClient.ini <<'EOF'
[Server]
Enabled = True
Entries = local:127.0.0.1:26760;
EOF

cat > ~/.config/dolphin-emu/WiimoteNew.ini <<'EOF'
[Wiimote1]
Source = 1
Device = DSUClient/0/local
Buttons/A = `Cross`
D-Pad/Down = `Pad S`
D-Pad/Up = `Pad N`
EOF

cat > ~/.config/dolphin-emu/GCPadNew.ini <<'EOF'
[GCPad1]
Device = DSUClient/0/local
Buttons/A = `Cross`
D-Pad/Down = `Pad S`
D-Pad/Up = `Pad N`
EOF

cat > ~/.config/dolphin-emu/Logger.ini <<'EOF'
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
EOF
```

`Dolphin.ini` belongs in `~/.config/dolphin-emu/`, not `~/.local/share/dolphin-emu/`. The latter is the data directory (including `Load/WiiSD.raw`). For Dolphin 2503, the DSU INI filename is `DSUClient.ini`, not `DualShockUDPClient.ini`. The no-GUI build uses the `WiimoteNew.ini` / `GCPadNew.ini` pad files; editing the legacy `Wiimote.ini` / `GCPad.ini` files will not configure these pads. These filenames and relevant config keys were checked against the Dolphin 2503 source; this review did not launch Dolphin to verify the resulting runtime log.

### 5.3 Input: prefer the DSU server for this recorded headless setup

The original run notes report that `xdotool`/XTEST did not drive Dolphin’s XInput2 raw-input path under Xvfb or Xorg-dummy. That is an environment-specific test result, not a universal statement about every X server or Dolphin backend. Use the DSU route below unless you independently validate another input route.

Dolphin’s Cemuhook DualShock UDP client is configured for a server at `127.0.0.1:26760`. The original workspace used `nds/dsu_server.py`, a small UDP server that sends virtual pad state on a timeline. **That server and its `RUNBOOK.md` were not supplied for this review.** Without them, the custom input timeline cannot be reproduced just from this guide; obtain the original files or implement and test a DSU server before claiming success.

Facts checked against Dolphin 2503 source:

- The message header is 16 bytes; CRC32 is at byte offset 8. Dolphin calculates it with zlib CRC32.
- The 32-bit `message_type` follows the header at the start of each packet’s message body. `PortInfo` carries id/state/model/connection/MAC/battery/padding fields and has no name field.
- The DSU server entry format is `description:address:port;`; the device name comes from the description, hence `DSUClient/0/local` for the sample config.
- The `PadDataResponse` structure is 100 bytes in Dolphin 2503. A server must answer registration and provide pad state; the original notes report streaming around 120 Hz, but that rate and the missing server implementation were not independently tested here.

NooDS-Wii v1.5 source confirms its file browser starts at `sd:/noods/`, inserts `..` before directory entries, and uses an 18-frame initial held-direction repeat delay. With only the `noods` directory and ROM created by §5.1, the intended browser list is `[.., rockwrestler.nds]`. Short button pulses avoid holding Down long enough to trigger a repeat. The original sequence `A, A, Down, A` uses the first two A presses to return to the SD root and re-enter its only `noods` directory, then selects and loads the ROM. This relies on the clean SD image containing no other root entries.

Original timeline (wall seconds from DSU server start; **reported by the previous run, not revalidated here**):

- About 20–66 s: three `A, A, Down, A` sequences drive the browser. Keep Down pulses brief; the 18-frame repeat threshold is a frame count, not a universal wall-clock duration.
- About 75–106 s: in-game Down/A presses are intended to show input response.
- The original notes report GC A → NDS A; Wii Remote 2 → NDS A; Wii Remote A → NDS X. The source also maps Wii Remote 1 → B, B → Y, `+` → START, and `−` → SELECT; GameCube face buttons map directly.

### 5.4 Run

If all companion files are present:

```bash
cd /home/user/nds
bash run-headless.sh
```

Manual equivalent of the original helper flow:

```bash
cd /home/user/nds
mkdir -p ~/.local/share/dolphin-emu/Load
cp wii-sd.raw ~/.local/share/dolphin-emu/Load/WiiSD.raw
(setsid python3 dsu_server.py >/tmp/dsu.log 2>&1 &)
pgrep -x Xvfb >/dev/null || { Xvfb :99 -screen 0 1280x720x24 & sleep 2; }
export DISPLAY=:99
timeout -s KILL 215 env LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  /usr/games/dolphin-emu-nogui -p x11 -v OGL -a HLE \
  -e apps/NooDS-Wii/NooDS-Wii-v1pt5-mute.dol
```

The Dolphin 2503 no-GUI source supports the listed `-p`, `-v`, `-a`, and `-e` options. Exit 137 is expected only if `timeout` reaches 215 seconds and sends SIGKILL; it is not, by itself, proof that the game booted. The original notes report slow pacing under 2 vCPU plus llvmpipe, so use the log and screenshots rather than assuming emulated frame timing equals wall-clock timing.

`pkill -f` can match the shell command that contains its own pattern. Prefer a PID file or a carefully chosen pattern such as `pkill -f 'dsu[_]server'`, while ensuring the literal pattern does not also occur elsewhere in the command line.

### 5.5 Verify the screen and logs; do not infer success from exit status

Capture the Xvfb root window at the relevant times and view the PNGs:

```bash
import -window root cap.png
```

Expected indicators reported by the original run:

1. Browser: `Dir: sd:/noods/`, `-> rockwrestler.nds`, and `A=Load B=Back B++=Settings`; the cursor should move off `..` after Down.
2. Booted ROM: NooDS HUD such as `FPS: 61.6 / HOME=Quit B++=Settings`, grey top screen with `READY`, a green ARM7 status light, and the condition-code test; white bottom screen.
3. Later capture: a changed game screen after in-game input.

The original file names were `nds/cap21-a.png` (browser), `nds/cap21-c.png` (booted), and `nds/cap22-a.png` (in-game reaction). These captures were not included with the supplied guide, so they are reference names, not evidence available to this review. For any claimed successful run, retain fresh captures, the Dolphin log line `Added device: DSUClient/0/local`, and evidence that the screen changes between captures.

---

## 6. Session checklist and pitfalls

1. Reinstall OS packages after a cold start if they are absent: `dolphin-emu xvfb imagemagick mtools libgl1-mesa-dri`.
2. Re-run the devkitPPC setup only when compiling Wii homebrew or running the toolchain self-test; it is not needed to launch the prebuilt NooDS DOL. `/opt` may be cleared between sessions.
3. Recreate the five Dolphin INI files from §5.2 when the config directory is missing or reset.
4. Rebuild the 128 MiB SD image with the corrected FAT16-LBA type (`0x0E`) and `mformat -H 2048`, then copy it into `~/.local/share/dolphin-emu/Load/WiiSD.raw` before each run.
5. Use `mdir -i wii-sd.raw@@1M ::/noods` to inspect FAT contents; `strings` is not a reliable FAT directory check.
6. `pkill -f` can self-match; use a PID file or a pattern verified against the actual process list.
7. DSU specifics worth preserving: CRC32 at header offset 8 (not appended); `message_type` follows the 16-byte header; `PortInfo` has no name field; 100-byte `PadDataResponse`; entry format `description:address:port;`. Verify the actual server implementation before relying on the input timeline.
8. NooDS browser Down repeats after 18 held frames; use short pulses. Browser A is separate from the in-game mapping (Wiimote A maps to NDS X in-game).
9. For Dolphin 2503, use `WiimoteNew.ini`, `GCPadNew.ini`, and `DSUClient.ini`. Do not substitute the legacy pad INIs or the incorrect DSU filename.
10. The official devkitPro package endpoint may be blocked at DNS, HTTP, or TLS layers depending on the sandbox. Test access before diagnosing it; the listed mirror was reachable at review time.
11. Never present an unverified run as complete. Check the DSU device log, inspect captures, verify that the ROM is visible and loaded, and confirm an in-game screen change.

---

## 7. File map and companion-artifact status

```text
/home/user/AGENTS.md                         this corrected runbook
/home/user/uploads/AGENTS(2).md              supplied source guide
/home/user/setup-devkitppc.sh                 optional toolchain helper; not supplied here
/home/user/dkp-env.sh                         optional toolchain environment; not supplied here
/home/user/run-dolphin.sh                     optional smoke-test rig; not supplied here
/home/user/wii-toolchain-test/                optional make -> hello.dol self-test; not supplied here
/home/user/nds/rockwrestler.nds               step 3 download; verified hashes in §3
/home/user/nds/apps.zip                       step 4 release archive; verified SHA-256 in §4
/home/user/nds/apps/NooDS-Wii/                extracted release DOLs
/home/user/nds/wii-sd.raw                     generated by §5.1; rebuild/deploy per session
/home/user/nds/dsu_server.py                  DSU server required by §5.3/§5.4; not supplied here
/home/user/nds/run-headless.sh                 full helper pipeline; not supplied here
/home/user/nds/RUNBOOK.md                     DSU implementation notes; not supplied here
/home/user/nds/src-*.cpp                      source snapshots mentioned by the original guide
/home/user/nds/cap*.png                        evidence captures mentioned by the original guide
```

A genuinely cold-startable bundle must include the missing helper files or document their complete contents and dependencies. Do not assume the file map means those files were present or validated.

## 8. Upstream references

- [Rockwrestler repository and README](https://github.com/RockPolish/rockwrestler) · [ROM download](https://github.com/RockPolish/rockwrestler/raw/master/rockwrestler.nds)
- [NooDS-Wii v1.5 release](https://github.com/radicalten/NooDS-Wii/releases/tag/v1.5) · [v1.5 browser/input source](https://github.com/radicalten/NooDS-Wii/blob/v1.5/NooDS-Wii/main.cpp)
- [Debian trixie Dolphin package file list](https://packages.debian.org/trixie/amd64/dolphin-emu/filelist)
- [Dolphin 2503 config filenames](https://raw.githubusercontent.com/dolphin-emu/dolphin/2503/Source/Core/Common/CommonPaths.h) · [settings sections/keys](https://raw.githubusercontent.com/dolphin-emu/dolphin/2503/Source/Core/Core/Config/MainSettings.cpp) · [no-GUI options](https://raw.githubusercontent.com/dolphin-emu/dolphin/2503/Source/Core/DolphinNoGUI/MainNoGUI.cpp) · [CLI options](https://raw.githubusercontent.com/dolphin-emu/dolphin/2503/Source/Core/UICommon/CommandLineParse.cpp)
- [Dolphin 2503 DSU protocol structures](https://raw.githubusercontent.com/dolphin-emu/dolphin/2503/Source/Core/InputCommon/ControllerInterface/DualShockUDPClient/DualShockUDPProto.h) · [DSU client configuration/parser](https://raw.githubusercontent.com/dolphin-emu/dolphin/2503/Source/Core/InputCommon/ControllerInterface/DualShockUDPClient/DualShockUDPClient.cpp) · [CRC32 helper](https://raw.githubusercontent.com/dolphin-emu/dolphin/2503/Source/Core/Common/Hash.cpp)
- [GNU mtools manual](https://www.gnu.org/software/mtools/manual/mtools.html) · [devkitPro libfat partition code](https://github.com/devkitPro/libfat/blob/master/source/partition.c) · [devkitPro package mirror](https://wii.leseratte10.de/devkitPro/)
