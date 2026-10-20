# NooDS-Wii v1pt61

A Wii port of Hydr8gon's NooDS Nintendo DS emulator, continued from the supplied
`default-NooDS-Wii-v1pt5` source tree. The repository includes the complete Wii
source, build rules, the polygon regression ROM, and the compiled release DOL.

> **Accuracy status:** polygon work is being measured against the included
> `suite-poly.nds` regression suite. See [`tests/README.md`](tests/README.md) for
the controlled headless-Dolphin procedure and result decoder. Do not interpret
an in-progress `PLY5` block as a completed accuracy result.

## Build

Install devkitPro's Wii toolchain (`wii-dev`, libogc and Wii portlibs), then run
from the repository root:

```sh
make
```

The default build enables the ARM JIT and produces `NooDS-Wii-v1pt61.dol`.
To build the interpreter variant instead:

```sh
make JIT=0 BROWSER=0
```

To build the instrumented test DOL (not the release DOL):

```sh
make poly-test-dol        # TEST_JIT=0 by default
make poly-test-dol TEST_JIT=1
```

The test-only switches dump RAM, replay the configured A-button pulse and keep
frameskip pinned to 0. See `tests/README.md` for running the suite ROM in
Dolphin and decoding all 77 scene records.

## Wii setup

NooDS-Wii requires original Nintendo DS BIOS and firmware files; they are not
included. The default paths are:

```text
sd:/noods/bios/bios9.bin
sd:/noods/bios/bios7.bin
sd:/noods/bios/firmware.bin
sd:/noods/bios/gba_bios.bin
```

The emulator starts in `sd:/noods/`. Store ROMs in a preferred subdirectory,
for example `sd:/noods/roms`, `sd:/noods/nds`, or `sd:/noods/gba`.

## Notes and credits

- Based on NooDS by Hydr8gon; Wii port by radicalten.
- Wii Remote, GameCube controller and Wii Classic Controller support are
  inherited from the v1.5 source tree.
- DS and GBA emulation are software-rendered; performance depends on the title
  and Wii model.
- Licensed under GNU GPL version 3 or later; see [`LICENSE`](LICENSE).
