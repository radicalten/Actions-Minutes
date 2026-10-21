# NooDS-Wii v1.71 — conservative JIT memory-lookup optimization

## Status
This is a small optimization candidate, not a solution to the reported lack of a visible JIT benefit in games. Previous releases are preserved. Audio, video pacing, input and UI source files are unchanged from v1.70 (apart from the JIT implementation and Makefile).

## Changes
- Specialize eight generated memory-map lookups with the stable Core/page-table base address rather than loading Core and adding the same map offset repeatedly. Page-table contents are still read dynamically; mapped/unmapped guards and interpreter fallbacks remain.
- Fuse nine `(address >> 12) * 4` calculations into one PowerPC rotate-and-mask instruction.
- Include GBA_JIT and JIT_STATS in build-directory names so switching these options cannot silently reuse incompatible objects.
- Expose the previously undefined GBA_JIT build option, default **0**. The runtime toggle still does not enable GBA recompilation in this default build. An experimental GBA_JIT=1 build retained the GBA suite score but was slower on the measured workload, so it was not shipped as the default.

## Results and limitations
All suites passed with runtime ARM JIT both On and Off, frameskip=0: GBA 6997/6998 (the existing one failure remains); NDS 2D exact match to the verified 8/8 baseline; NDS CPU 28/28; NDS 3D 77/77 with zero missing/extra/wrong-color pixels. Logs are in validation/.

The fixed-240-frame diagnostic benchmark used Dolphin EmulationSpeed=1.0 and frameskip=0. Its PERF counters measure **emulated PPC timebase**, not independently measured host wall-clock time. Compared to v1.70, the candidate's diagnostic times were:

| Suite | v1.70 JIT On | v1.71 JIT On | Reduction |
|---|---:|---:|---:|
| GBA | 2340175 us | 2340174 us | unchanged |
| NDS 2D | 895458 us | 889399 us | 0.68% |
| NDS CPU/3D | 9556431 us | 9499082 us | 0.60% |

These small diagnostic gains do not establish an observable commercial-game or real-Wii improvement. The benchmark MAX_FRAMES harness disables the internal frame limiter; therefore these numbers are not paced gameplay FPS. The Null-backend accuracy runs also cannot visually establish native Dolphin FPS throughout execution.

Separate live OpenGL verification omitted MAX_FRAMES and used the normal limiter. validation/live-pacing.png shows native Dolphin **59.92 FPS** while internal emulation is **16 FPS** on the 3D suite. This verifies that the two rates remain separate in that captured session, not that every workload is permanently locked to 60.

The standalone-branch expansion and the larger register-pinned dispatcher experiment were not retained. No guest cycle-accounting, instruction coverage, frameskip, audio or rendering shortcuts were introduced in this build.

## Build
Requires devkitPPC/libogc in /opt/devkitpro:

```sh
make -j8
```

Default binary: NooDS-Wii.dol. Settings > ARM JIT retains v1.70's live NDS On/Off behavior and persistence.

For meaningful further profiling, use the specific games and scenes where FPS is below full speed, with identical settings and runtime JIT On/Off comparisons. A game already reaching its native frame rate will not show a higher FPS from faster CPU execution.
