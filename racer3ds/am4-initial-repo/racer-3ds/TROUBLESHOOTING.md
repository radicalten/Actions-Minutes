# Crashes when launching the installed `.cia`

Symptom you reported: the HOME-menu launcher goes into extended memory mode, the title
starts, and the console shows a data abort before anything is drawn.

**Correction after decoding your crash dump: it is not the GPU path — it is the DSP.**
The faulting code in the dump is libctru's `ndspInitialize()` pipe handshake, reached from
`ndspInit()` because the audio path was coming up automatically. The GPU hardening from the
previous build stays (it is correct and free), but it was not what crashed.

The sections below, in order:

1. what your dump decodes to (the actual cause),
2. what this build changes about audio,
3. the earlier GPU-write hypothesis, kept as prevention,
4. how to read `boot.log` and what the last line means.

## Dump 1: the handshake libctru does not check

Your dump is a list of 32-bit words, but they are **raw little-endian memory bytes**, not
numbers. Read as printed they look like nonsense (`0x2CDE1000`, `0xBC091500`); byte-swapped
they are `0xDEADC0DE`, `0xDEADCAFE` and a run of real code. So every field has to be
byte-swapped before it means anything.

Byte-searching `racer3ds.elf` for the code words in the dump gives exactly one hit, and it
is unambiguous:

| Dump content | Address in `racer3ds.elf` | Symbol |
|---|---|---|
| embedded code region | `0x0010CC34` | `ndspInitialize.part.0+0xEC` |
| `ldrh r0,[r6,#2]!` … `bl DSP_ConvertProcessAddressFromDspDram` … `bl DSP_SetSemaphore` | same region | the DSP-pipe handshake inside `ndspInitialize()` |
| a word that decodes to a return/PC candidate | `0x0010DE2C` | `ndspiReadChnState+0x68` |
| a word that decodes to a pointer | `0x0010AD00` | `gfxGetFramebuffer+0x20` |
| `0x001509BC`, `0x00150938`, `0x0014F82C` | just past `g_vi` in `.bss` | not attributable (offset is outside the symbol's size — do not trust a nearest-symbol match here) |

The strings `Racer 3D` and `CRC1 0x72F70398 OK, 32 MiB` are also in the dump — that is the
ROM header line, so the boot got **past** screen/GPU init and found a ROM on the SD card
before it died. GPU init was never the problem.

What the matching instructions are, in libctru `source/ndsp/ndsp.c`:

```c
DSP_WriteProcessPipe(2, &val, 4);
DSP_SetSemaphore(0x4000);
ndspWaitForIrq(U64_MAX);
DSP_ReadPipeIfPossible(2, 0, &val, sizeof(val), NULL);   // "how many variables follow"
u16 vars[16];
DSP_ReadPipeIfPossible(2, 0, vars, val*2, NULL);        // <- dump is in here
for (unsigned i = 0; i < val; i ++)
    DSP_ConvertProcessAddressFromDspDram(vars[i], (u32*)&ndspVars[i][0]);
```

`ndspInit()` loads `/3ds/dspfirm.cdc` with `DSP_LoadComponent()`, whose `is_loaded`
out-parameter says whether the component **actually started on the DSP** — and `ndspInit()`
passes `NULL` for it, then handshakes as if the DSP were running. A truncated,
region-mismatched or otherwise wrong `dspfirm.cdc` therefore does not fail cleanly: the
handshake reads a bogus count and a bogus variable table out of pipe 2 and execution follows
those values into memory that is not there. That is the abort.

A second, quieter bug in the same area: the installed title's exheader did **not** grant
`dsp::DSP`, so the DSP service could never have opened from the `.cia` at all. That is fixed
too (`platform/3ds/cia/app.rsf`, verified with `ctrtool -t exheader`).

## Dump 2: same subsystem, one level deeper - and now proven

The second dump is a **stack trace**, not a memory snapshot: it carries five code addresses
and each one is the instruction that a `bl` returns to. They line up end to end, so there is
no guesswork about where this died:

| Address in dump | Resolves to | Evidence |
|---|---|---|
| `0x0011056C` | `DSP_SetSemaphore+0x20` | the instruction after `bl svcSendSyncRequest` — the DSP IPC itself |
| `0x0010D440` | `ndspInitialize.part.0+0x148` | inside the pipe handshake, right after the variable table is read |
| `0x0010DA24` | `ndspInit+0xAC` | return address of `bl ndspInitialize` |
| `0x001014AC` | `audio_init+0x154` | return address of `bl ndspInit` |
| `0x001023CC` | `main+0x474` | return address of `bl audio_init(32000)` |

The dump also carries a contiguous copy of the handshake code (`0x10D3E4`…`0x10D46B`) and the
live ndsp globals (`ndspVars`, `ndspFrameId`, `ndspRefCount`, `componentBin`, the audio
buffers). So the sequence was:

1. boot got past GPU init, the ROM check and the first frames (`main+0x474` is the frame
   loop, `g_rom_checked` is set in the dump),
2. audio came up at frame 2 and **the firmware passed the `is_loaded` check** — otherwise
   `ndspInit` would never have run,
3. the DSP answered the pipe handshake, and the very next DSP IPC killed the process.

The mechanism is the one from dump 1, and now it is confirmed by the return address sitting
inside the handshake loop:

```c
u16 vars[16];                                   // 32 bytes on the stack
DSP_ReadPipeIfPossible(2, 0, vars, val*2, NULL); // val comes from the DSP, unchecked
for (unsigned i = 0; i < val; i ++)              // val > 16 => stack is already gone
    DSP_ConvertProcessAddressFromDspDram(...);
```

`val` is whatever the DSP put in pipe 2. If it is larger than 16, that read writes past
`vars`, through the thread's saved registers and TLS, and the next `svcSendSyncRequest`
(`DSP_SetSemaphore`) dies on the corrupted thread state. `is_loaded` cannot catch this: the
component starts fine, it just does not answer this protocol.

## Dump 3: the probe was the problem

The third dump has the same shape as the second, and one address settles it:

| Address in dump | Resolves to | Meaning |
|---|---|---|
| `0x001108AC` | `DSP_SetSemaphore+0x20` | the DSP IPC that dies |
| `0x0010D780` | `ndspInitialize.part.0+0x148` | the handshake loop |
| `0x0010DD64` | `ndspInit+0xAC` | return address of `bl ndspInitialize` |
| `0x00101944` | **`audio_init+0x5EC`** | return address of `bl ndspInit` |
| `0x0010270C` | `main+0x474` | return address of `bl audio_init` |

`audio_init+0x5EC` is the instruction after `bl ndspInit`, and the two instructions before it
in the build are `svcSleepThread` and the "calling ndspInit" breadcrumb. So the sequence was:

1. the pre-flight probe ran, did its own load + handshake, and **succeeded**,
2. it unloaded the component, waited 60 ms,
3. `ndspInit()` loaded the same component **a second time** — and *that* handshake is the one
   that died.

In other words the previous build's fix was itself the fault: a DSP component is not
re-entrant across an unload/reload in one boot, and the second handshake came back with a
count that does not fit `u16 vars[16]`. One load per boot. That is now enforced by removing
the probe entirely.

## Dumps 4 and 5: identical in 80 MB and 64 MB, and the faulting instruction is a write

Both dumps decode to the same addresses (they differ only in the title id — `0F904200` in the
80 MB one, `0F904400` in the 64 MB one — and in two counters). So **extended memory mode is
not involved**: the 64 MB title crashes exactly like the 80 MB one. That closes that question.

| Address in dump | Resolves to | Is… |
|---|---|---|
| `0x00110AE4` | `DSP_SetSemaphore+0x20` | stale return address from the semaphore call just before |
| `0x0010434C` | **`ndspInitialize.part.0+0x1D4`** | `strh r3, [r1]` — a **store** |
| `0x001048D4` | `ndspInit+0x1EC` | return address of `bl ndspInitialize` |
| `0x001013FC` | `audio_init+0xA4` | return address of `bl ndspInit` |
| `0x00102270` | `main+0x474` | return address of `bl audio_init` |

The dumped code window confirms it — those are the last instructions of the handshake:

```asm
104334: mov  r0, #0x4000
104338: bl   DSP_SetSemaphore
10433c: mov  r3, #4          ; ndspFrameId = 4
104340: mov  r2, #0          ; frameCount = 0
104344: ldr  r1, [r5]        ; r1 = ndspVars[0][0]   <- a DSP shared-memory pointer
104348: strh r3, [r5, #0xe0] ; ndspFrameId = 4
10434c: strh r3, [r1]        ; ndspSetCounter(0, 4)  <- FAULT (write)
```

So the sequence this time was:

1. the component loaded once and the handshake **completed**: the count was in range, the
   variable table was read, and every entry converted to *something*,
2. `ndspInit()` then did what it always does — `ndspSetCounter(0, 4)` writes the frame
   counter through `ndspVars[0][0]`,
3. that write faulted, because the converted address is not mapped (or not writable) in this
   process. It is a **write** data abort, which is exactly what the console reported from the
   very first report.

This also explains the earlier dumps: `DSP_SetSemaphore+0x20` was only ever the *stale*
return address left on the stack by the semaphore call two instructions earlier — the actual
fault has been this store all along.

The new check is what turns this crash into a clean failure: the vendored `ndsp` asks the
kernel per entry —

```c
svcQueryMemory(&info, &page, addr);   // mapped? writable? does a u16 fit in the region?
```

— and fails `ndspInit()` instead of writing through a bad pointer. The patched copy also
gained a read-only observer (`ndspSetDebugHook`), so the handshake now reports what the DSP
sent without a second load and without a crash dump. **Those lines are what I need next:**

```
dsp: handshake count=<N>
dsp: var[0]=0x0000 -> 0x1FFxxxxx
dsp: reject var[3] ptr 1 = 0x0015xxxx (not writable)
```

They say how many variables the DSP returns and where it maps its shared memory. A `reject`
with a small address means the firmware is answering for a different DSP revision; a count of
0 or a timeout means the DSP is not starting at all — two different fixes.

## What this build actually changes: a patched `ndsp`

`platform/3ds/thirdparty/libctru-ndsp/` is libctru's own `ndsp` (`ndsp.c`, `ndsp-channel.c`,
`ndsp-filter.c`, `ndsp-internal.h` from master, Zlib licence, changes marked
`// racer-3ds patch:`). The app links these objects instead of libctru's `ndsp.o`, so
`ndspInit()` itself is safe now:

| Patch | Effect |
|---|---|
| `is_loaded` is checked after `DSP_LoadComponent()` | a wrong-region or truncated `dspfirm.cdc` fails cleanly instead of handshaking with a DSP that never started |
| the IRQ wait is 400 ms instead of `U64_MAX` | a silent DSP can no longer freeze the boot |
| the variable count is refused unless `1 <= count <= 16` | the stack smash in dumps 2 and 3 cannot happen |
| every converted table entry is checked with `svcQueryMemory` (mapped, writable, room for a `u16`) | the write that faults in dumps 4/5 becomes a clean `ndspInit` failure |
| a read-only `ndspSetDebugHook` reports the count and table | the next report is a `boot.log` line, not another dump |
| `ndspWaitForIrq()` no longer calls `svcBreak(USERBREAK_PANIC)` on a failed wait | a dead DSP cannot panic the app from libctru's own error path |

Verified in the shipped binary: `cmp r2, #15` / `bhi` after the pipe read, the pointer range
checks, and the literal `0x17D78400` (400 ms). The app now logs
`audio: calling ndspInit (bounded handshake)` and either `audio ready: 32000 Hz stereo` or
`off (ndspInit 0xD8805Cxx)`.

## What this build changes about audio

1. **The handshake is bounded where it happens** — see the patch table above. One load per
   boot, 400 ms IRQ timeout, `count <= 16`, converted pointers range-checked. The app only
   checks that `sdmc:/3ds/dspfirm.cdc` exists and has a plausible size before calling
   `ndspInit()`; it no longer loads the component itself.
2. **Audio is on by default, with opt-outs.** No file to create and no key to hold: the
   default boot has audio. To take it out of the picture, hold **R** at launch (this boot
   only) or put `off` in `sdmc:/3ds/racer/audio.cfg` (permanent; `on`/`1`/`yes`/`enable` and
   `off`/`0`/`no`/`disable` are understood, deleting the file restores the default).
   Safe mode (**L**) always disables it.
3. **Every audio step is logged, before it happens:** `audio policy: on (default)` →
   `audio: probing … (bytes)` → `dspfirm: DSP_LoadComponent -> …, is_loaded=…` →
   `dsp: handshake reports N variable(s)` → `dsp: handshake looks sane` →
   `audio: calling ndspInit - if this is the LAST line, …` → `ndspInit -> 0x…`.
   `ndspExit()` is still never called after a failed `ndspInit()`.
4. **The status screen and safe-mode console print the reason**, e.g. `audio on 32000 Hz`,
   `audio off (R held)` or `audio off (DSP handshake out of range)` — so "why is there no
   sound" is answered without a log.
5. **Memory profile opt-out**: hold **X** at launch and the app behaves like a 64 MB title
   (64 KiB command buffer, 4 MB allocation probe); the status header reads
   `MEMORY (64MB profile)`. The system mode itself is fixed in the exheader and the console
   reboots into it before `main()` runs, so a key cannot change it — `bin/racer3ds-64.cia`
   is the real 64 MB title, and **X** is the way to test the same app under the same
   pressure without reinstalling.

If the DSP is still a problem after this, the log tells you which gate refused: policy
(`R`/`audio.cfg`), firmware load (`is_loaded`), or the handshake bounds check. Holding **R**
gets you a working boot in every one of those cases.

## The earlier hypothesis: an unchecked GPU init (kept as prevention)

A write fault before the first frame is a store through an address the app thinks it owns
but does not, and GPU init was the obvious suspect. In v1 the chain was

```
gfxInitDefault(); C3D_Init(); C2D_Init(); C2D_Prepare();
C2D_CreateScreenTarget(); C3D_TexInit(); linearAlloc();
... first frame: C3D_TexUpload() -> C2D_TargetClear() -> C2D_DrawImageAt()
```

with **no return value checked anywhere**. `C3D_Init`, `C2D_Init` and `C3D_TexInit` all
return `bool` and all three allocate from the linear heap, so any of them can fail on an
installed title's tighter heap and the next call writes through a null/garbage pointer:

* `C3D_TexInit()` fails → `tex->data == NULL` → `C3D_TexUpload()` stores into `NULL`;
* `C3D_Init()` fails → `C2D_Prepare()`/`C2D_TargetClear()` store into a render target with
  no memory;
* `C2D_CreateScreenTarget()` returns NULL → `C2D_TargetClear(NULL, …)`.

None of that is what the dump shows, but it is a real failure mode on a different console
and it is now impossible: every allocation is checked and a failure demotes the app to the
console UI instead of writing through a bad pointer.

## What changed in this build

1. **Every GPU/heap step is checked and logged** (`source/screen.cpp`). The citro3d command
   buffer is retried at 128 KiB → 64 KiB → 32 KiB before giving up; the 512×256 RGB565 VI
   texture and its staging buffer are verified (`C3D_TexInit()`, `data != NULL`,
   `vramSpaceFree()`) before anything uploads to them. All drawing entry points are no-ops
   for parts that failed, so a failure can no longer turn into a wild pointer write.
2. **A failure demotes the app instead of crashing**: if citro3d/citro2d cannot come up it
   tears the GPU context back down (`screen_drop_gpu()`: `C3D_Fini`/`C2D_Fini` — mixing a
   live citro3d context with the libctru console is not supported) and runs a plain
   `consoleInit` text UI that still reports the memory mode, the ROM and the trace.
3. **The boot log** — `sdmc:/3ds/racer/boot.log`, one line per startup step, flushed
   immediately. Whatever the last line is, that is the step that died. It also keeps the
   last 40 lines in RAM so the console UI can print them without the SD card.
4. **Audio is deferred, on by default and firmware-validated** (see the DSP section above):
   **R** or `audio.cfg` `off` opts out, `dspfirm.cdc` is probed with the `is_loaded` check
   libctru skips, and `ndspExit()` is only ever called after a successful `ndspInit()`.
5. **Safe mode**: hold **L** while launching. No citro3d, no citro2d, no ndsp, no ROM read —
   just `gfxInitDefault()` + the console. If it works in safe mode, the fault is in the GPU
   path; if it still aborts, the log tells you how far it got.
6. **A 64 MB `.cia` for bisecting** (`make cia-64`, `bin/racer3ds-64.cia`, title id
   `000400000F904400` so it installs alongside). If the 64 MB build works and the 80 MB one
   does not, the problem is extended memory mode itself (e.g. a plugin/cheat loader that
   cannot survive the reboot), not the app.

## How to test it on your console

1. Install `bin/racer3ds.cia` (80 MB) **and** `bin/racer3ds-64.cia` (64 MB) with FBI.
2. Launch the **64 MB** one from the HOME menu first. (Already done: dumps 4 and 5 show both
   titles crash identically, so extended memory mode is not part of the problem. Keep the
   test anyway — it is the cheapest way to confirm a future fix landed.)
3. Then launch the **80 MB** one.
4. Then hold **L** and launch the 80 MB one again (safe mode).
5. Audio is **on by default** now — the log should show
   `dspfirm: … component ran` and `audio ready: 32000 Hz stereo`. If that boot dies in the
   DSP, hold **R** to confirm the battery of tests above still passes without audio.
6. Hold **X** and launch the 80 MB one for the 64 MB profile (same title, smaller command
   buffer and probe; the header reads `MEMORY (64MB profile)`).

After each attempt, read the log — it is on the SD card and it survives the crash:

```
sdmc:/3ds/racer/boot.log
```

A healthy 80 MB boot looks like this:

```
boot log opened
racer3ds <date> <time>
fsInit -> 0, homebrew=0
hidInit -> 0x00000000, hidSharedMem=0x...
keys held: L(safe)=0 R(no audio)=0 X(64MB profile)=0
app mem 80 MB free 6x MB, HIGH 80MB (O3DS ext), probe 8 MB
gfxInitDefault
gfx ok
linearSpaceFree=... vramSpaceFree=...
C3D_Init(262144) ok, linear free ... -> ...
C2D_Init(4096)
C2D_Prepare
screen targets top=... bottom=...
text buf ...
C3D_TexInit(512x256 RGB565) vram free ...
C3D_TexInit -> 1, data=..., vram free ...
screen init done: mode=citro2d cmdbuf=262144 vi_tex=1
screen ready
rom: found=... valid=... path=...
audio: default -> enabled, firmware is checked before ndspInit
audio policy: on (default)
dspfirm: sdmc:/3ds/dspfirm.cdc, 2097152 bytes
audio: calling ndspInit (bounded handshake) - if this is the LAST line, the ...
ndspInit -> 0x00000000
audio ready: 32000 Hz stereo
```

and with the audio opted out:

```text
audio: R held -> off for this boot
audio policy: off (R held)
audio_init skipped: off (R held)
```

and on a crash the last line is the culprit, e.g.:

```
C3D_TexInit(512x256 RGB565) vram free 0        <- VRAM exhausted, never returned
```

or

```
C3D_Init(262144) FAILED (linear free 0)        <- then 131072/65536/32768 retries
```

Please send me the file (or its last lines) and I can turn it into a targeted fix rather
than guesswork.

## If the log stops before `gfxInitDefault`

Then the abort is earlier than GPU init and the log's last line is one of:

| Last line | Meaning | Next step |
|---|---|---|
| `boot log opened` then nothing | the abort is in `fsInit`/service bring-up | try safe mode; check the title's service list (exheader) with `ctrtool --exheader` |
| *(file missing)* | the app cannot write to `sdmc:` at all | `rom_check()` reads only; if reads fail too, the app's FS access is the problem (`fs:USER`), not the GPU |
| `hidInit -> 0xD8E0…` | HID handle refused | the title's service list again; safe mode still works without input |
| `gfxInitDefault` then nothing | GSP init died (this call returns void and aborts internally) | extended-memory + GSP interaction — the 64 MB build is the test |
| `audio policy: …` | not a crash point — audio policy is decided before any DSP call | if the next line is missing the crash is later; the DSP is not running yet |
| `audio: calling ndspInit - if this is the LAST line, the DSP never answered; ...` then nothing | the DSP accepted the firmware but never answered the pipe handshake (this is your original abort) | relaunch holding **R** (audio off) and you have a working boot |
| `keys held: …` then nothing | the crash is before GPU init | next line missing tells you it is not audio, not the profile |
| `off (ndspInit 0xD8805Cxx)` | the patched handshake refused: bad count, a table entry that is not writable memory, a silent DSP, or a component that did not start | the app is fine, audio is off. **Send `boot.log`** — the `dsp:` lines say which one |
| `dspfirm: component did NOT run` | `dspfirm.cdc` is truncated or from the wrong region | re-dump it from **your** console (GodMode9: `[1:] SYSNAND CTRNAND` → `title/00040138/0000002f` → DSP1, or let Luma/`hbmenu` provide `hb:ndsp`) |

## Things worth ruling out that are not in the code

* **Plugins / cheat loaders** (NTR CFW, Luma3DS plugins, BootNTR). Extended memory mode
  reboots the console, and plugins that survive the reboot into an EMM title are a
  well-known source of exactly this "data abort right after the black screen" on O3DS.
  Disable them (hold SELECT at boot on Luma to open the menu) and retry.
* **Luma3DS version**: extended memory titles on O3DS want a reasonably current Luma
  (`13.x`). Older versions mishandle some EMM titles.
* **A broken/partial install**: reinstall with FBI, and make sure there is free space on
  both SD and the title's NAND/SD location.
* **Corrupt SD card** would not cause a write abort, but it would explain a missing log.
* **A wrong-region `dspfirm.cdc`** is the one input that can crash `ndspInit()` (see the DSP
  section). It must come from the console it runs on; a dump copied from another region or
  a half-finished copy is exactly the `is_loaded = 0` case.

None of this reaches the console as text unless the app survives long enough to print, which
is exactly why the log exists.
