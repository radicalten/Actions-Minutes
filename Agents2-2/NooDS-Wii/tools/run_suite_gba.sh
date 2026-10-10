#!/usr/bin/env bash
set -euo pipefail

export PATH="$PATH:/usr/sbin:/sbin:/usr/games"

if [ $# -lt 3 ]; then
  echo "Usage: $0 DOL LABEL OUT_DIR [TIMEOUT_SECS]" >&2
  exit 2
fi

DOL="$(realpath "$1")"
LABEL="$2"
OUT_DIR="$(realpath -m "$3")"
SECS="${4:-180}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$OUT_DIR"

SD_IMG="${OUT_DIR}/sd_${LABEL}.img"
GBAOUT_LOG="${OUT_DIR}/gbaout_${LABEL}.log"
SUMMARY_JSON="${OUT_DIR}/mgba_summary_${LABEL}.json"
DOLPHIN_DIR="${OUT_DIR}/dolphin_${LABEL}"

rm -f "$SD_IMG" "$GBAOUT_LOG" "$SUMMARY_JSON"

if [ ! -f "$DOL" ]; then
  python3 - "$SUMMARY_JSON" "$LABEL" "missing_dol" << 'PY'
import json, sys
out, label, reason = sys.argv[1], sys.argv[2], sys.argv[3]
with open(out, "w") as f:
    json.dump({"status": "NOT_RUN", "label": label, "reason": reason}, f, indent=2)
PY
  exit 1
fi

"${REPO_ROOT}/tools/build_sd_image.sh" "$SD_IMG" --autoboot "sd:/gba/suite.gba" >/dev/null

set +e
"${REPO_ROOT}/tools/run_dolphin.sh" "$DOL" "$SD_IMG" "$SECS" "$DOLPHIN_DIR" "ALL DONE"
DRC=$?
set -e

if ! mcopy -i "$SD_IMG" ::/gbaout.log "$GBAOUT_LOG" 2>/dev/null; then
  rm -f "$SD_IMG"
  python3 - "$SUMMARY_JSON" "$LABEL" "missing_gbaout_log" "$DRC" << 'PY'
import json, sys
out, label, reason, drc = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
with open(out, "w") as f:
    json.dump({"status": "FAIL", "label": label, "reason": reason, "dolphin_rc": drc}, f, indent=2)
PY
  exit 1
fi

rm -f "$SD_IMG"

python3 - "$GBAOUT_LOG" "$SUMMARY_JSON" "$LABEL" "$DOLPHIN_DIR/result.txt" << 'PY'
import hashlib, json, re, sys

log_path, json_path, label, res_path = sys.argv[1:5]
raw = open(log_path, "rb").read()
sha256 = hashlib.sha256(raw).hexdigest()
lines = [ln.strip() for ln in raw.decode("utf-8", errors="replace").splitlines() if ln.strip()]

res_kv = {}
try:
    for ln in open(res_path):
        if "=" in ln:
            k, v = ln.strip().split("=", 1)
            res_kv[k] = v
except Exception:
    pass

suites = []
current_suite = None
current_fails = []
skips = []
all_done = False

for ln in lines:
    if ln.startswith("BEGIN: "):
        current_suite = ln[len("BEGIN: "):].strip()
        current_fails = []
    elif ln.startswith("SKIP: "):
        skips.append(ln[len("SKIP: "):].strip())
    elif ln.startswith("END: "):
        m = re.match(r"^END:\s*(\d+)/(\d+)$", ln)
        if m and current_suite is not None:
            failed = int(m.group(1))
            total = int(m.group(2))
            suites.append({
                "suite": current_suite,
                "failed": failed,
                "total": total,
                "passed": total - failed,
                "failure_lines": current_fails,
            })
            current_suite = None
            current_fails = []
    elif ln == "ALL DONE":
        all_done = True
    else:
        if current_suite is not None and ("FAIL" in ln):
            current_fails.append(ln)

total_tests = sum(s["total"] for s in suites)
total_failed = sum(s["failed"] for s in suites)
total_passed = sum(s["passed"] for s in suites)

status = "PASS" if (all_done and len(suites) == 13 and len(skips) == 1) else "FAIL"

summary = {
    "status": status,
    "label": label,
    "all_done": all_done,
    "suite_count": len(suites),
    "skip_count": len(skips),
    "skips": skips,
    "total_tests": total_tests,
    "total_passed": total_passed,
    "total_failed": total_failed,
    "gbaout_bytes": len(raw),
    "gbaout_sha256": sha256,
    "elapsed_seconds": int(res_kv.get("ELAPSED_SECONDS", -1)),
    "suites": suites,
}

with open(json_path, "w") as f:
    json.dump(summary, f, indent=2)

print(json.dumps({k: v for k, v in summary.items() if k != "suites"}, indent=2))
if status != "PASS":
    sys.exit(1)
PY
