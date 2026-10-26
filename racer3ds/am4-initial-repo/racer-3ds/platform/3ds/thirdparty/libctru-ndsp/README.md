# Vendored libctru `ndsp` — with the DSP handshake bounds-checked

`ndsp.c`, `ndsp-channel.c`, `ndsp-filter.c` and `ndsp-internal.h` are taken verbatim from
**libctru master** (`libctru/source/ndsp/`, fetched 2026-10-26) and patched. libctru is
Zlib-licensed; see `LICENSE`.

Why they are here: `ndspInit()` is the one libctru entry point in this port that can take the
whole process down on a console whose DSP firmware answers differently than expected. Two
crash dumps from an O3DS installed title land inside `ndspInitialize()`'s pipe handshake, and
one lands on the first channel-state read afterwards. Both are the same defect: values that
come *from the DSP* are used without validation.

The app links these objects **instead of** libctru's `ndsp.o` (the linker only pulls that
archive member when a symbol is still undefined, so defining all of them here wins). Nothing
else in libctru is replaced.

## The patches

| # | Where | What libctru does | What this copy does |
|---|---|---|---|
| 1 | `ndspWaitForIrq()` | `svcBreak(USERBREAK_PANIC)` if the wait fails for any reason other than a clean timeout — the "invalid handle" case its own comment calls common | returns `false`; a dead DSP is not a panic |
| 2 | `ndspInitialize()` | waits for the pipe interrupt with `U64_MAX` — forever | 400 ms, then fails the init |
| 3 | `ndspInitialize()` | reads a count from DSP pipe 2 and then reads `count*2` bytes into `u16 vars[16]` **without checking the count** | refuses `count == 0 || count > 16`; that read is the stack smash in both dumps |
| 4 | `ndspInitialize()` | stores the converted DSP variable table and trusts it | each converted pointer is range-checked (non-null, below `0x40000000`), so a junk table fails here instead of faulting in the ndsp thread |
| 5 | `ndspInit()` | passes `NULL` for `DSP_LoadComponent()`'s `is_loaded` and handshakes anyway | passes `&componentStarted` and fails cleanly when the DSP did not start the component |

Every patched line is marked `// racer-3ds patch:` in the sources.

## Why this instead of a pre-flight probe

An earlier build ran its own bounded handshake *before* `ndspInit()`. The third crash dump
shows that this is what broke it: `audio_init+0x5EC` is the return address of `bl ndspInit`,
i.e. the probe had already succeeded, and the handshake that crashed was libctru's *second*
load of the same component. One load, done safely, is the fix — not two loads.

Keeping this in-tree also means the fix travels with the source instead of requiring a
rebuilt devkitPro archive.
