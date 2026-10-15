# NooDS-Wii Dolphin test builds

These DOLs were built with devkitPPC 16.1.0 from `Conservative-JIT-v3-NooDS-Wii/` and rerun in headless Dolphin 2503. All three produced matching ARM9/ARM7 CPU state, guest cycles, memory hashes, and framebuffer hashes at the three synthetic test checkpoints. The full test record is `../DOLPHIN-SMOKE-TEST.md`.

## DOL variants

- `NooDS-Wii_interpreter.dol` — `JIT=0 NATIVE=0 BLOCKS=0`; interpreter baseline.
- `NooDS-Wii_jit-bridge.dol` — `JIT=1 NATIVE=0 BLOCKS=0`; generated stubs use the interpreter through C-ABI bridges.
- `NooDS-Wii_jit-native.dol` — `JIT=1 NATIVE=1 BLOCKS=0`; opt-in, narrow ARM/Thumb native whitelist. **Experimental.**

All builds keep multi-instruction blocks and poll-loop skipping disabled.

## Quick smoke test

The included `jit-thumb-smoke.nds` is a tiny copyright-free test ROM. Copy it to `sd:/noods/jit-smoke.nds` on a virtual/physical Wii SD card, and copy `autoboot.txt` to the SD root. Then boot any DOL. It runs a three-frame test and writes `g2_state.log`, `mem_dump.bin`, and `fb_dump.bin` to the SD card. The test ROM exercises ARM-to-Thumb switching on both ARM9 and ARM7; it is not a game or broad compatibility test.

For Dolphin setup commands, virtual-SD creation, and checkpoint comparison, see `../Conservative-JIT-v3-NooDS-Wii/tests/dolphin_smoke/README.md`.

## SHA-256

```text
d676c31fbc77305e866db9b9486dd037937bbd97e34e178cb5fbc203d3b3304c  NooDS-Wii_interpreter.dol
9554b492d00a50f365fc3604169d0f25fd881e2ee41509e093df75d41f1643e9  NooDS-Wii_jit-bridge.dol
020c859c5e82566a167c966d302e8b78eab0a892c17480b78bdef85b4ea4fc1b  NooDS-Wii_jit-native.dol
a6077445225cc88a9d827256ff76a6e219eedfca51591de7fb989ff68633dbb9  jit-thumb-smoke.nds
```

Native mode remains experimental: matching this synthetic ROM does not establish retail-game compatibility, physical Wii correctness, or performance improvement.
