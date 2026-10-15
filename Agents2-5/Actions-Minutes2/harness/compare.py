#!/usr/bin/env python3
"""Compare two harness checkpoint logs (interpreter vs JIT).

Fields compared per checkpoint: CYCLES, ARM9/ARM7 state, MEM_HASH, FB_HASH.
PERF and JIT_STATS lines are ignored (they are timing/diagnostic only).

Usage: compare.py ref.log cand.log [--ignore-cycles]
"""
import re
import sys

STATE_FIELDS = ("cpsr", "spsr", "pc", "halted", "ime", "ie", "irf", "postFlg")


def parse(path):
    checkpoints = {}
    frame = None
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        m = re.match(r"=== CHECKPOINT FRAME (\d+) ===", line)
        if m:
            frame = int(m.group(1))
            checkpoints[frame] = {}
            continue
        if frame is None or not line:
            continue
        if line.startswith(("PERF ", "JIT_STATS", "=== RUN")):
            continue
        if line.startswith(("CYCLES ", "MEM_HASH ", "FB_HASH ")):
            key, _, rest = line.partition(" ")
            checkpoints[frame][key] = rest
        elif line.startswith(("ARM9 ", "ARM7 ")):
            key, _, rest = line.partition(" ")
            fields = {}
            for tok in rest.split():
                if "=" in tok:
                    k, _, v = tok.partition("=")
                    fields[k] = v
            checkpoints[frame][key] = fields
    return checkpoints


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    ignore_cycles = "--ignore-cycles" in sys.argv
    ref, cand = parse(args[0]), parse(args[1])
    frames = sorted(set(ref) & set(cand))
    mismatches = 0
    detail = []
    for fr in frames:
        a, b = ref[fr], cand[fr]
        if not ignore_cycles and a.get("CYCLES") != b.get("CYCLES"):
            mismatches += 1
            detail.append(f"frame {fr}: CYCLES ref={a.get('CYCLES')} cand={b.get('CYCLES')}")
        for h in ("MEM_HASH", "FB_HASH"):
            if a.get(h) != b.get(h):
                mismatches += 1
                detail.append(f"frame {fr}: {h} ref={a.get(h)} cand={b.get(h)}")
        for cpu in ("ARM9", "ARM7"):
            fa, fb = a.get(cpu, {}), b.get(cpu, {})
            for k in sorted(set(fa) | set(fb)):
                if fa.get(k) != fb.get(k):
                    mismatches += 1
                    detail.append(f"frame {fr}: {cpu}.{k} ref={fa.get(k)} cand={fb.get(k)}")
    print(f"checkpoints_ref={len(ref)} checkpoints_cand={len(cand)} common={len(frames)}")
    print(f"MISMATCHES={mismatches}")
    for d in detail[:40]:
        print("  " + d)
    if len(detail) > 40:
        print(f"  ... {len(detail)-40} more")
    print("STATUS=" + ("PASS" if mismatches == 0 and frames else "FAIL"))
    return 0 if mismatches == 0 and frames else 1


if __name__ == "__main__":
    sys.exit(main())
