# Dual-screen console and result ledger

Milestone 6 uses both physical Nintendo DS LCDs. This is implemented in the ROM, not composed by the host screenshot tool.

## User controls

The top screen remains the score summary. At a completed or stopped run, press **Left/Right** to move between 12 lower-screen pages, wrapping at either end:

| Page shown | Contents |
|---|---|
| 1/12 | CPU vector counts and first-failure diagnostics for ARM9 and ARM7 |
| 2/12 | Registered groups 00–12 and their statuses |
| 3/12 | Registered groups 13–24 and their statuses |
| 4/12 | Non-pass outcomes 1–9, with assertion/cause IDs |
| 5/12 | Non-pass outcomes 10–18 |
| 6/12 | Non-pass outcomes 19–25 |
| 7/12 | ARM9 and ARM7 startup IPC/IRQ/CPSR snapshots side by side |
| 8/12 | Eighteen captured ARM9 CP15 values |
| 9/12 | Vector/function counts, coverage and verification limitations |
| 10/12 | ARM9 family/case explorer |
| 11/12 | ARM7 family/case explorer |
| 12/12 | Executed C++ assertion journal |

CPU vector IDs are one-based and CPU-qualified. Schema-2 `src/generated/debug-manifest.json` has separate full ARM9 and ARM7 lists; IDs above the shared prefix have different meanings. Explorer controls: Up/Down selects family/case, A toggles selection mode, B selects the next failed case. The journal uses Up/Down for events and B for the next failure. See [debugging.md](debugging.md). Hardware assertion IDs are displayed in hexadecimal. An auxiliary CPU value can be CPSR flags, a high product word, a guard value, a base delta, or another result, as specified by the vector's `aux_kind`. Diagnostic fields are not evidence of a failure when the first-failure ID is zero.

Group statuses:

- **WAIT:** selected but not run.
- **PASS:** group returned successfully.
- **FAIL:** an assertion failed.
- **TIME:** ROM-detected timeout.
- **SKIP:** outside the selected test plan; not a pass.
- **RUN:** currently executing.
- **CRSH:** host-observed native emulator signal.
- **HTIM:** host-observed hard hang / frame or wall timeout.

Three non-pass pages hold up to nine outcomes each, covering all 25 groups. The second value is an assertion ID for FAIL/TIME, signal number for CRSH, or timeout cause for HTIM. Invalid selectors are configuration errors on the top screen, not registered-group entries.

On supervised recovery the top screen says RESUMED. Full vector traces and executed C++ events are restored through an explicit host barrier; historical CPU records are labeled PRIOR BOOT. The first-failure summary is derived from those traces. Counts and per-group outcomes survive. Boot snapshots belong to the fresh boot, not the pre-crash state. See [recovery.md](recovery.md).

The lower console is active during the pure CPU groups and at terminal reporting. It is deliberately disabled during hardware groups. This avoids using an actively rendered VRAM bank while testing memory/VRAM/IPC hardware. Hardware failures that leave the CPU unable to execute reporting code, and native emulator crashes, cannot display a final page; the host must report their timeout/crash status.

## Hardware configuration

`src/bottom.cpp` implements a small 4bpp text console without libnds:

- POWCNT1 `0x8203`: LCDs enabled; main 2D engine routed to upper LCD and sub engine to lower LCD. [POWCNT1 at `0x04000304` is a 16-bit register](https://mgba-emu.github.io/gbatek/); the build now narrows the copied upstream declaration to `vu16*` so hardware accesses use halfwords rather than 32-bit writes.
- VRAM bank C control `0x84`: maps bank C to sub BG memory at `0x06200000`.
- Sub-engine display: normal graphics mode with BG0 enabled.
- BG0 font graphics: `0x06200000`, 128 glyphs converted from the retained upstream font.
- BG0 map: `0x0620f800`, 32×32 tile entries; 32×24 visible.
- Sub BG palette: `0x05000400`, light text on a dark background.
- Blending/scroll/master brightness reset when initializing the console.

Font data and result text are reinitialized after hardware tests; never assume they survived VRAM mapping tests. The top score renderer remains the upstream main-engine VRAM renderer. `bottom_idle()` polls KEYINPUT rather than relying on VBlank interrupts that an interrupt test may have changed.

The 16-bit POWCNT1 access correction is a hardware-compatibility fixup applied only to the private `work/rock` copy during `scripts/build.sh`; the pinned vendor checkout remains untouched. Five host regression tests cover the patcher and build wiring. The rebuilt ROM uses a halfword `STRH` for the write at `0x04000304`; it is packaged as `real-hardware/suite.nds` (SHA-256 `8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3`). This confirms the fix is in the binary, **not** that it resolves the user’s white screens: the new ROM still needs testing on the reported DSi/3DS + TWiLight Menu++/nds-bootstrap setup.

Paging edits only the lower tilemap and UI page field. It does not rerun tests, change scores, alter group results, or pretend a partial run is complete.

## Additional reporting blocks

The existing score mailbox remains **protocol v2** and keeps its original layout at `0x023f0000`.

### Optional UI metadata: `0x023f0080`

Nine little-endian u32 words:

| Word | Meaning |
|---|---|
| 0 | magic `0x55493244` |
| 1 | UI protocol version 1 |
| 2 | current zero-based page, written after drawing |
| 3 | number of pages, currently 12 |
| 4 | console configured/enabled: 1; disabled during hardware groups: 0 |
| 5–7 | explorer family-mode flag, selected zero-based family, one-based case ID |
| 8 | selected zero-based C++ event |

### Per-group ledger: `0x023f0100`

25 entries, each two little-endian u32 words:

1. status: 0 WAIT, 1 PASS, 2 FAIL, 3 TIME, 4 SKIP, 5 RUN, 6 CRSH, 7 HTIM;
2. first failing assertion ID or host cause, or zero when there is no failure.

A zero assertion is a valid hardware failure ID: inspect the status, not whether the number is nonzero. These additional addresses, the score mailbox, and CPU diagnostic areas must remain reserved when changing memory layout.

The host runner exports `ui`, `bottom_text`, `group_results` and `post_ui_mailbox` for terminal results. These are diagnostics, not another scoring mechanism.

## Automated verification

```sh
python3 scripts/test_ui.py
python3 scripts/run_desmume.py --sequential \
  --detail-page 2 --verify-navigation \
  --output reports/lower-groups.json --screenshot reports/lower-groups.png
```

`test_ui.py` executes 12 supervised sequential runs of the actual ROM and uses the emulator's DS keypad API. It verifies:

- both LCDs are represented in the 256×384 capture;
- lower-screen pixels are nonblank;
- every page's tilemap title and metadata match the requested page;
- Left/Right work, including page 0↔11 wraparound;
- the top screen is byte-identical across paging operations;
- the score mailbox does not change after navigation;
- the group ledger matches the score counts;
- known failure IDs are present on the failure page.

The runner uses `emu.cycle(False)` so an uninitialized joystick subsystem does not overwrite injected DS keypad input. It advances two idle frames after terminal reporting to allow both LCD scanouts to finish before capture.

Actual captures are retained as `reports/ui-page-0.png` through `reports/ui-page-11.png`. No mocked screens or post-composed lower panels are used. Verification currently covers the tested emulator, not physical DS hardware.
