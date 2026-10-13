# Environment notes (harness)

## Snapshot / size
- The platform snapshot of `/home/user` is capped (~128 MB); on 2026-10-13 the cap silently dropped
  whole directories from the change bundle — including `repo/NooDS-Wii/NooDS-Wii/` (the modified
  sources). Cleanup done to prevent recurrence:
  - `devkitpro/` was **moved to `/opt/devkitpro`** — outside `/home/user`, so it is *not* snapshotted
    at all (it never survived snapshots intact before anyway). Re-bootstrap each session; a compiled
    DOL/ELF built from it is kept in the repo (`jit.dol`).
  - Stale harness test binaries removed: `bin_blkdump bin_jit_p bin_t1..t8 bin_trace_i/j/jnb`
    (unrebuildable from the current source) and all `obj/` dirs.
  - Kept (stripped, ~2 MB each): `harness/bin_interp`, `harness/bin_jit`, `harness/bin_jit_hot`.
- A small drop-in copy of the changed sources + patch + rebuilt `jit.dol` lives in `/home/user/handoff/`
  (see `handoff/README.md`) so the sources can never be lost to an omission again.

## Rebuild toolchain
```
bash tools/bootstrap_toolchain.sh            # honours DEVKITPRO; use DEVKITPRO=/opt/devkitpro
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
export PATH=$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH
```
`libogc` etc. come from the same bootstrap. Host tools that are *not* snapshotted and must be
re-installed with apt when missing: `g++-powerpc-linux-gnu` (harness), `qemu-user-static`
(`qemu-ppc-static`), `binutils-arm-linux-gnueabi` (guest disassembly), `binutils-powerpc-linux-gnu`
(strip/objdump), `gdb-multiarch`.

## Harness quick start (no devkitPPC needed)
```
cd /home/user/harness
./build.sh interp            # -> bin_interp/noods_host   (rebuild = ~1 min, needs powerpc g++ + apt pkgs)
./build.sh jit               # -> bin_jit/noods_host
qemu-ppc-static ./bin_jit/noods_host --rom /home/user/repo/NooDS-Wii/nds/rockwrestler.nds \
    --frames 120 --checkpoints 30,60,120 --out runs/x.log --stats
python3 compare.py runs/a runs/b     # interpreter-parity check (expect MISMATCHES=0)
```
Harness timings (~25 ms/f jit, ~31 ms/f interp) are harness-only — never cite as Wii FPS.
