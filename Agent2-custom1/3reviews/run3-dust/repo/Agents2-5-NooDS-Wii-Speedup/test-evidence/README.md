# Final speedup candidate test evidence

The current diagnostic test DOL (`../NooDS-Wii-Agents2-5-Speedup-Test.dol`, SHA-256 `53e41b481deada0bd102ca63fa1988933e8b6c1db3dd9dcf54f870328ebe4dda`) was rebuilt from the speedup source and run on a fresh clone of the 64 MiB FAT16 SD image with `suite.nds` (SHA-256 `8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3`), Dolphin `[master] 2503`, headless/Null backend.

- Page 2: frame 650; page-2 input schedule; `avg_frame_us=86062`; 25/27 groups, 0 unrun, ARM9-family 13,252 pass / 128 fail, ARM7-family 12,056 pass / 0 fail.
- Page 3: frame 650; page-3 input schedule; `avg_frame_us=86056`; same CPU counts; groups 14–26 pass.
- `JIT_STATS enabled=0 counters=disabled` appears in both logs. Dolphin's wrapper terminated with status 137 after writing the valid checkpoint; the checkpoint, schedule, and captures were checked.
- Screenshots are upscaled from the captured framebuffer for legibility. Large duplicate RAM/frame dumps were not retained.
