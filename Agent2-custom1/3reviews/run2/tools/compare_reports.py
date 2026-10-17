#!/usr/bin/env python3
"""
Diff two jit3-report.txt files produced by the same ROM.

The report rows are written by main.cpp (WriteJit3Sample) in both the JIT build
and the interpreter-only build, so the two files are directly comparable:

    frame pc9 pc7 cycles ram fb ms | jit blocks compiles refusals flushes dispatches instrs

Only the semantic columns are compared - frame, both guest PCs, the global cycle
counter and the two FNV hashes.  The trailing stats columns are JIT bookkeeping
and are expected to differ; `ms` is wall-clock and is only reported, since that
is the point of the comparison.

    tools/compare_reports.py report-jit.txt.clean report-interp.txt.clean

Exit status 0 when every common frame agrees, 1 when the first divergence is
found (or when the two runs share no sampled frame at all).
"""
import sys

COLS = ["frame", "pc9", "pc7", "cycles", "ram", "fb", "ms"]
SEMANTIC = ["pc9", "pc7", "cycles", "ram", "fb"]


def rows(path):
    out = {}
    with open(path, "r", errors="replace") as f:
        for line in f:
            line = line.replace("\x00", "").rstrip("\n")
            if not line or line.startswith("#"):
                continue
            f0 = [c for c in line.split("|")[0].split() if c]
            if len(f0) < len(COLS):
                continue
            try:
                frame = int(f0[0])
            except ValueError:
                continue
            out[frame] = dict(zip(COLS, f0))
    return out


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    a, b = rows(sys.argv[1]), rows(sys.argv[2])
    common = sorted(set(a) & set(b))
    print("frames: jit=%d interp=%d common=%d" % (len(a), len(b), len(common)))
    if not common:
        print("FAIL: no frame was sampled by both runs")
        return 1
    bad = 0
    perCol = {c: 0 for c in SEMANTIC}
    firstShown = False
    for f in common:
        diffs = [c for c in SEMANTIC if a[f][c] != b[f][c]]
        for c in diffs:
            perCol[c] += 1
        if diffs:
            bad += 1
            if not firstShown:
                firstShown = True
                print("first divergence at frame %d:" % f)
                for c in SEMANTIC:
                    flag = "  <-- differs" if a[f][c] != b[f][c] else ""
                    print("  %-7s jit=%s interp=%s%s" % (c, a[f][c], b[f][c], flag))
        elif bad == 0:
            print("frame %-4d ok   cycles=%-10s ram=%s fb=%s" % (f, a[f]["cycles"], a[f]["ram"], a[f]["fb"]))
    if bad:
        print("frames with any difference: %d of %d" % (bad, len(common)))
        print("per column: " + ", ".join("%s=%d" % (c, perCol[c]) for c in SEMANTIC))
        if all(perCol[c] == 0 for c in ("cycles", "ram", "fb")):
            print("note: cycles, main-RAM hash and framebuffer hash agree on every "
                  "common frame - the remaining difference is the *sampled PC* only")
        print("FAIL: %d of %d common frames differ" % (bad, len(common)))
        return 1
    f_last = common[-1]
    print("PASS: all %d common frames agree (up to frame %d)" % (len(common), f_last))
    if "ms" in a[f_last] and "ms" in b[f_last]:
        print("speed: frame %d took %s ms with the JIT build and %s ms with the interpreter build (%.1fx)"
              % (f_last, a[f_last].get("ms"), b[f_last].get("ms"),
                 float(b[f_last]["ms"]) / max(1, int(a[f_last]["ms"]))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
