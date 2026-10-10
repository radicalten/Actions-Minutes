#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import sys


def parse_checkpoint_log(path):
    if not os.path.isfile(path) or os.path.getsize(path) == 0:
        return None
    frames = {}
    cur = None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("=== CHECKPOINT FRAME ") and line.endswith(" ==="):
                fnum = int(line.split()[3])
                cur = {"frame": fnum}
                frames[fnum] = cur
            elif cur is not None:
                parts = line.split()
                tag = parts[0]
                kv = {}
                for tok in parts[1:]:
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        kv[k] = v
                cur[tag] = kv
    return frames


def sha256_file(path):
    if not os.path.isfile(path) or os.path.getsize(path) == 0:
        return None, 0
    raw = open(path, "rb").read()
    return hashlib.sha256(raw).hexdigest(), len(raw)


def compare_checkpoints(ref_dir, cand_dir, out_json):
    ref_log = os.path.join(ref_dir, "g2_state.log")
    cand_log = os.path.join(cand_dir, "g2_state.log")
    ref_frames = parse_checkpoint_log(ref_log)
    cand_frames = parse_checkpoint_log(cand_log)

    if not ref_frames or not cand_frames:
        res = {
            "status": "NOT_RUN",
            "reason": "missing_or_empty_checkpoint_log",
            "ref_log_exists": bool(ref_frames),
            "cand_log_exists": bool(cand_frames),
        }
        with open(out_json, "w") as f:
            json.dump(res, f, indent=2)
        print(json.dumps(res, indent=2))
        return 1

    ref_mem_sha, ref_mem_sz = sha256_file(os.path.join(ref_dir, "mem_dump.bin"))
    cand_mem_sha, cand_mem_sz = sha256_file(os.path.join(cand_dir, "mem_dump.bin"))
    ref_fb_sha, ref_fb_sz = sha256_file(os.path.join(ref_dir, "fb_dump.bin"))
    cand_fb_sha, cand_fb_sz = sha256_file(os.path.join(cand_dir, "fb_dump.bin"))

    if not ref_mem_sha or not cand_mem_sha or not ref_fb_sha or not cand_fb_sha:
        res = {
            "status": "NOT_RUN",
            "reason": "missing_binary_dumps",
            "ref_mem_sha": ref_mem_sha,
            "cand_mem_sha": cand_mem_sha,
            "ref_fb_sha": ref_fb_sha,
            "cand_fb_sha": cand_fb_sha,
        }
        with open(out_json, "w") as f:
            json.dump(res, f, indent=2)
        print(json.dumps(res, indent=2))
        return 1

    mismatches = []
    all_fnums = sorted(set(ref_frames.keys()) | set(cand_frames.keys()))
    for fn in all_fnums:
        if fn not in ref_frames or fn not in cand_frames:
            mismatches.append({"frame": fn, "field": "frame_presence"})
            continue
        rf = ref_frames[fn]
        cf = cand_frames[fn]
        for section in ["CYCLES", "ARM9", "ARM7", "MEM_HASH", "FB_HASH"]:
            if rf.get(section) != cf.get(section):
                mismatches.append({
                    "frame": fn,
                    "section": section,
                    "ref": rf.get(section),
                    "cand": cf.get(section),
                })

    if ref_mem_sha != cand_mem_sha:
        mismatches.append({
            "section": "mem_dump.bin",
            "ref": ref_mem_sha,
            "cand": cand_mem_sha,
        })
    if ref_fb_sha != cand_fb_sha:
        mismatches.append({
            "section": "fb_dump.bin",
            "ref": ref_fb_sha,
            "cand": cand_fb_sha,
        })

    last_fn = max(all_fnums)
    status = "PASS" if len(mismatches) == 0 else "FAIL"
    res = {
        "status": status,
        "frames_compared": all_fnums,
        "mismatch_count": len(mismatches),
        "mismatches": mismatches,
        "mem_dump_bytes": ref_mem_sz,
        "mem_dump_sha256_ref": ref_mem_sha,
        "mem_dump_sha256_cand": cand_mem_sha,
        "fb_dump_bytes": ref_fb_sz,
        "fb_dump_sha256_ref": ref_fb_sha,
        "fb_dump_sha256_cand": cand_fb_sha,
        "ref_perf": ref_frames[last_fn].get("PERF", {}),
        "cand_perf": cand_frames[last_fn].get("PERF", {}),
        "cand_jit_stats": cand_frames[last_fn].get("JIT_STATS", {}),
    }
    with open(out_json, "w") as f:
        json.dump(res, f, indent=2)
    print(json.dumps(res, indent=2))
    return 0 if status == "PASS" else 1


def compare_suites(ref_json_path, cand_json_path, out_json):
    if not os.path.isfile(ref_json_path) or not os.path.isfile(cand_json_path):
        res = {"status": "NOT_RUN", "reason": "missing_suite_json"}
        with open(out_json, "w") as f:
            json.dump(res, f, indent=2)
        print(json.dumps(res, indent=2))
        return 1

    ref = json.load(open(ref_json_path))
    cand = json.load(open(cand_json_path))
    if ref.get("status") != "PASS" or cand.get("status") != "PASS":
        res = {
            "status": "FAIL",
            "reason": "input_suite_not_pass",
            "ref_status": ref.get("status"),
            "cand_status": cand.get("status"),
        }
        with open(out_json, "w") as f:
            json.dump(res, f, indent=2)
        print(json.dumps(res, indent=2))
        return 1

    mismatches = []
    if ref.get("skips") != cand.get("skips"):
        mismatches.append({"field": "skips", "ref": ref.get("skips"), "cand": cand.get("skips")})
    ref_suites = {s["suite"]: s for s in ref.get("suites", [])}
    cand_suites = {s["suite"]: s for s in cand.get("suites", [])}
    for name in sorted(set(ref_suites.keys()) | set(cand_suites.keys())):
        rs = ref_suites.get(name)
        cs = cand_suites.get(name)
        if rs != cs:
            mismatches.append({"suite": name, "ref": rs, "cand": cs})

    status = "PASS" if len(mismatches) == 0 else "FAIL"
    res = {
        "status": status,
        "suite_count_ref": ref.get("suite_count"),
        "suite_count_cand": cand.get("suite_count"),
        "skip_count_ref": ref.get("skip_count"),
        "skip_count_cand": cand.get("skip_count"),
        "total_tests_ref": ref.get("total_tests"),
        "total_tests_cand": cand.get("total_tests"),
        "total_passed_ref": ref.get("total_passed"),
        "total_passed_cand": cand.get("total_passed"),
        "total_failed_ref": ref.get("total_failed"),
        "total_failed_cand": cand.get("total_failed"),
        "gbaout_sha256_ref": ref.get("gbaout_sha256"),
        "gbaout_sha256_cand": cand.get("gbaout_sha256"),
        "mismatch_count": len(mismatches),
        "mismatches": mismatches,
    }
    with open(out_json, "w") as f:
        json.dump(res, f, indent=2)
    print(json.dumps(res, indent=2))
    return 0 if status == "PASS" else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["checkpoints", "suites"], required=True)
    ap.add_argument("--ref", required=True)
    ap.add_argument("--cand", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    if args.mode == "checkpoints":
        sys.exit(compare_checkpoints(args.ref, args.cand, args.out))
    else:
        sys.exit(compare_suites(args.ref, args.cand, args.out))


if __name__ == "__main__":
    main()
