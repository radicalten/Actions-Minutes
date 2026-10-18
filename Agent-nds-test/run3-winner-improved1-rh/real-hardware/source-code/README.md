# Source snapshot for the real-hardware ROM

This is the source tree used to build `../suite.nds`. It includes all project source and generated build inputs under `src/`, build/test tooling under `scripts/`, standalone tests under `tests/`, the CI workflow under `.github/`, documentation, the pinned-upstream lockfile, and the exact shallow upstream source checkouts under `vendor/`. It also carries a copy of `../suite.nds` at this directory's root because the host source-contract test reads the ROM from the project root; the two ROM copies have identical SHA-256 hashes.

## Rebuild

With a supported Linux system and the required devkitPro/devkitARM, `ndstool`, Python, and requirements installed:

```sh
python3 scripts/fetch_upstreams.py --check
make build
sha256sum suite.nds
make host
```

The supplied binary's SHA-256 is `8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3`; this copied source tree was rebuilt and reproduced that exact checksum. Other toolchains or source changes may produce a different checksum; compare before treating another image as identical. `scripts/setup.sh` documents the project's Debian/Ubuntu setup, but it uses sudo, apt, and network access.

The pinned revisions are recorded in `upstream-lock.json`. This snapshot preserves local Git metadata for those shallow checkouts so `fetch_upstreams.py --check` can validate them. Their presence does not grant redistribution permission. See `NOTICE.md`, `suite-readme.md`, and the parent `../README.md` for license and hardware-test notes.
