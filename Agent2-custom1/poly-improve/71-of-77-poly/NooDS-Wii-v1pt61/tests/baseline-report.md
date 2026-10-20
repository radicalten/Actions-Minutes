# Polygon-suite baseline

This is the **pre-raster-change** measurement for the v1.5 starting source.
It is retained so the later renderer change can be compared against the same
ROM and machine-readable counters.

## Run configuration

- Test ROM: `suite-poly.nds`, data version 2, tag `0x31303242` (`102B`).
- Test DOL: interpreter build (`JIT=0`), `BROWSER=0`, `FULL_RAM_DUMP=1`,
  scripted A pulse, `TEST_FAST=1`.
- Start input: A held on frames 320–321; group 27 advanced automatically after
  the earlier suite groups.
- Stop frame: 2,000. The PLY5 header was **complete**, all 77 scene records
  were finalized, and the checkpoint log reports `skip=0`.
- Full dump: 4,345,856 bytes; result block decoded from file offset `0x415400`.

## Result

| Metric | Baseline |
|---|---:|
| Passing scenes | 43 / 77 (55.84%) |
| Failing scenes | 34 |
| Errored scenes | 0 |
| Expected pixels | 102,942 |
| Missing pixels | 491 |
| Overdraw pixels | 309 |
| Wrong-colour pixels | 185 |
| First failing scene | 2 |
| Group 27 | `FAIL`, code `0x50000002` |

| Result category | Scenes | Pass | Fail | Missing | Overdraw | Wrong-colour |
|---|---:|---:|---:|---:|---:|---:|
| Fill rules, primary | 23 | 15 | 8 | 45 | 168 | 0 |
| Fill rules, unusual | 21 | 6 | 15 | 123 | 20 | 155 |
| Vertical right-edge shift | 9 | 9 | 0 | 0 | 0 | 0 |
| Anti-aliasing | 3 | 2 | 1 | 1 | 3 | 0 |
| Edge marking | 2 | 1 | 1 | 30 | 0 | 30 |
| Clipping | 19 | 10 | 9 | 292 | 118 | 0 |

Failing scene IDs:

- Primary fill rules: `2, 5, 8, 11, 12, 13, 14, 16`.
- Unusual fill rules: `23–30, 32–35, 37, 38, 42`.
- Vertical right-edge shift: none (all 9 pass).
- Anti-aliasing: `55`.
- Edge marking: `57`.
- Clipping: `59, 61, 62, 63, 65, 66, 69, 70, 74`.

The full 77-scene line-by-line output and raw dump are retained outside the
release tree under `/home/user/dolphin-run/out-baseline-2000/` during this
workspace session. `tests/decode_poly.py --scenes` regenerates the table from
any compatible full-RAM dump.
