# NooDS-Wii-v1pt61 polygon-test build

This is the easy-to-find handoff for the current build. The source tree and test runner are in `/home/user/NooDS-Wii-v1pt61/`.

## Latest tested candidate

- **`NooDS-Wii-poly-test-latest.dol`** — compiled from `gpu_3d_renderer-latest.cpp` with `TEST_JIT=0`.
- **Full-suite result:** tested with the supplied `suite-poly.nds` under headless Dolphin with frameskip 0. All 77 scene records completed: **71/77 pass**, 6 fail, 0 errors. The build is not yet acceptance-ready; the target remains 77/77 scenes and zero pixel mismatches across 102,942 expected pixels.
- Pixel mismatches: 4 missing, 6 overdraw, 155 wrong-colour. Group 27 reports FAIL (`0x50000025`).
- DOL SHA-256: `226d4d7b7bf7f5039782906a70e8187f159dd0d05e06213b8f23c30215ef6a7d`
- Renderer source SHA-256: `c0a9e78a4218a6f0290623b4f7124f2f9ecf42cb1ea9b5bd1a40f481d83954df`
- Remaining failing scenes: **37, 38, 42, 55, 69, 70**.

## Backups

- **`NooDS-Wii-poly-test-previous-70-of-77.dol`** — prior full-suite candidate.
- **`NooDS-Wii-poly-test-previous-69-of-77.dol`** — earlier full-suite candidate.
- **`NooDS-Wii-poly-test-previous-67-of-77.dol`** — earlier candidate.
- **`NooDS-Wii-poly-test-legacy-61-of-77.dol`** — conservative backup.

The `Makefile` is included in this folder.

## Rebuild and test

From the project directory, with devkitPro's Wii toolchain installed:

```sh
source /etc/profile.d/devkit-env.sh && make -j2 poly-test-dol TEST_JIT=0
```

Then run the suite with frameskip 0:

```sh
OUT=/path/to/output MAX_FRAMES=2000 DOLPHIN_TIMEOUT=260s REQUIRE_PASS=0 sh tests/run-poly.sh
```

The runner also accepts `SKIP_BUILD=1` to use an existing test DOL without invoking the Wii toolchain.