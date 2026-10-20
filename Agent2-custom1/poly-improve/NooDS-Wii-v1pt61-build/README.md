# NooDS-Wii-v1pt61 polygon-test builds

This folder is the easy-to-find handoff for the current `.dol` builds.

## Latest build

- **`NooDS-Wii-poly-test-latest.dol`** — built from the active source with `TEST_JIT=0`.
- **Status:** the build succeeded, but this newest thin-triangle change has **not yet had a full 77-scene test run**. Treat it as a test candidate, not a verified release.
- SHA-256: `0613d86710f6e417f2370f4da5a5076c5d6cb72bd41cd3a3583c79c948031723`

## Last fully verified build

- **`NooDS-Wii-poly-test-verified-61-of-77.dol`** — last archived candidate that completed the full suite with frameskip 0.
- Result: **61/77 pass**, 16 fail, 0 errors; 102,942 expected pixels; 112 missing, 17 overdraw, 155 wrong-colour.
- SHA-256: `93abc16c1d6b7b3a2a632cffa217d283b8916e6fbf872daadcc4e3b75b09dd25`

## Source and rebuild

`gpu_3d_renderer-latest.cpp` is the renderer source used for the latest build. `Makefile` is copied from the project. The complete source/build tree is `/home/user/NooDS-Wii-v1pt61/`.

From that project directory, rebuild with:

```sh
source /etc/profile.d/devkit-env.sh && make -j2 poly-test-dol TEST_JIT=0
```

The final 77/77 acceptance run and release DOL are still pending.