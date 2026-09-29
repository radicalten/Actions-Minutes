# AGENTS.md — devkitPPC (Wii/GameCube) environment

**Do NOT use dkp-pacman**: `pkg.devkitpro.org/packages/` returns HTTP 403 to this sandbox's IP,
so package installs fail. Use the Docker Hub workaround below (the official `devkitpro/devkitppc`
image contains the full toolchain at `/opt/devkitpro`).

## Install (3 commands)

```sh
bash /home/user/dkp-work/fetch-layers.sh    # pulls image layers from Docker Hub, sha256-verified
bash /home/user/dkp-work/extract-layers.sh  # extracts opt/ into dkp-work/stage
bash /home/user/dkp-work/install.sh         # sudo cp -a stage/opt/devkitpro /opt/devkitpro
```

If `dkp-work/` is gone, the steps are: get a pull token from `auth.docker.io`, fetch the
`devkitpro/devkitppc` linux/amd64 manifest from `registry-1.docker.io/v2/`, download each
layer blob, `tar -xzf` only `opt/*` from each in order, copy to `/opt/devkitpro`.

## Environment (every new shell)

```sh
source /home/user/wii-env.sh   # DEVKITPRO, DEVKITPPC, PATH + wiivars.sh flags
```

## Verify

```sh
powerpc-eabi-gcc --version    # expect devkitPPC 16.x
```

## Build a Wii homebrew (output: boot.dol)

```sh
powerpc-eabi-gcc $CFLAGS $CPPFLAGS -DGEKKO -c hello.c -o hello.o
powerpc-eabi-gcc hello.o $LDFLAGS -specs=$DEVKITPRO/libogc/share/rvl.specs \
    -lwiiuse -lbte -logc -lm -o hello.elf
elf2dol hello.elf boot.dol
```

Or use the canonical template (just `make`):
`/opt/devkitpro/examples/wii/templates/makefile/application/`

## Gotchas

- **libogc 3.x needs `-specs=$DEVKITPRO/libogc/share/rvl.specs`** at link time (defines `__bss_end`,
  `__Arena1Lo`, ... via `rvl.ld`). Linking with plain `-logc` fails with undefined references.
- Wiimote code needs `-lwiiuse -lbte`; GameCube builds use `cubevars.sh` + `ogc.specs` instead.
- `/opt` is **not** persisted in workspace snapshots — if the toolchain is missing, re-run the
  3 install commands (needs network to Docker Hub).
- Building works headless; *running* the `.dol` needs a real Wii or `dolphin-emu` (in Debian
  repos, but this sandbox has no display).
- Working test project: `/home/user/wii-test/` (`hello.c` → `boot.dol`, entry 0x80003f00).
