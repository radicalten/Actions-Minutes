# Harness environment notes

The authoritative setup, reproduction steps, and caveats are in `../AGENTS.md` (§6 environment, §8 harness).

Short version:
- `bin_interp/noods_host` and `bin_jit/noods_host` are rebuilt from the current sources
  (`SRC=<repo>/NooDS-Wii/NooDS-Wii bash build.sh interp|jit bin_interp|bin_jit`). Remove the `obj/`
  directories `build.sh` leaves behind.
- `bin_jit_hot` was removed: it was built from an older tree and its build flags are not recorded.
- `runs/` holds older Stage-1 logs. They are kept as history, not regenerated.
- Run the harness on the rockwrestler and suite ROMs from `../NooDS-Wii/nds` and `../NooDS-Wii/gba`.
- Harness timings are harness-only. Never cite them as Wii FPS.
- Host packages are not kept across sandbox resets. Reinstall them with the command in `AGENTS.md` §6.
