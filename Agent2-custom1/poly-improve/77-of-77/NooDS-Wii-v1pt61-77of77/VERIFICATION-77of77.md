# NooDS-Wii v1pt61 — full polygon-suite pass

This snapshot contains the exact source tree and Makefile used for the final candidate, the regular Wii build, and the separate compiled polygon-test DOL used for the acceptance run.

## Acceptance result

- ROM: supplied `tests/suite-poly.nds`
- Result block: **complete**, 77 passed / 77, 0 failed, 0 errors
- Expected pixels: **102,942**; missing 0, overdraw 0, wrong-colour 0
- Group 27: **PASS** (`0x00000000`)
- Frameskip: **0** (`DISPLAY ... skip=0` in the run log)
- Full-run artifacts: `/home/user/dolphin-run/out-aa-scene55-slopecoverage-full-v1/`

## Build outputs

- `NooDS-Wii-v1pt61.dol` — regular Wii build, built with `JIT=1 BLOCKS=0 BROWSER=1`; SHA-256 `4493a939b038057ddd7bbdb6ee66858806cb741d3544f6de9266a6e150daf573`
- `NooDS-Wii-poly-test-77of77.dol` — test-harness build used for the 77/77 acceptance run; SHA-256 `22002c5455c5f67000a37fba6cb986160e32065cc5de397ecd0be7f44f6ce7b8`
- `NooDS-Wii/gpu_3d_renderer.cpp` — SHA-256 `52ffa7a361a7c4713c672a617499d57a999a0ccd9018fe19c439ba5400c77ed2`

The regular Wii build is provided as the playable application. The test-harness DOL is provided separately so the acceptance result can be reproduced. Run `sh tests/run-poly.sh` with the Wii toolchain and Dolphin installed; the runner configures frameskip 0.
