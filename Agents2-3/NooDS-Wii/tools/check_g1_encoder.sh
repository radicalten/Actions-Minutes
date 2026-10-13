#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPPC="$DEVKITPRO/devkitPPC"
export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:/usr/sbin:/usr/games:$PATH"

OUT_LOG="evidence/g1_encoder_oracle.log"
TMP_DIR="$(mktemp -d /tmp/noods_g1.XXXXXX)"
trap 'rm -rf "$TMP_DIR"' EXIT

g++ -O2 -std=c++17 tools/test_encoder.cpp -o "$TMP_DIR/test_encoder"
"$TMP_DIR/test_encoder" > "$TMP_DIR/expected.txt"
"$TMP_DIR/test_encoder" --asm > "$TMP_DIR/ref.s"

powerpc-eabi-as -mgekko -mbig "$TMP_DIR/ref.s" -o "$TMP_DIR/ref.o"
powerpc-eabi-objdump -d -M gekko "$TMP_DIR/ref.o" > "$TMP_DIR/objdump.txt"

python3 - "$TMP_DIR/expected.txt" "$TMP_DIR/objdump.txt" "$OUT_LOG" <<'PY'
import re, sys

exp_path, dump_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

expected = []
with open(exp_path) as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        parts = line.split(" ", 3)
        expected.append((parts[0], parts[1], parts[2].lower(), parts[3]))

actual = []
pat = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{2})\s+([0-9a-f]{2})\s+([0-9a-f]{2})\s+([0-9a-f]{2})\s+(.*)$")
with open(dump_path) as f:
    for line in f:
        m = pat.match(line)
        if m:
            word = "0x" + m.group(2) + m.group(3) + m.group(4) + m.group(5)
            dis = re.sub(r"\s+", " ", m.group(6).strip())
            actual.append((word.lower(), dis))

if len(expected) != len(actual):
    print(f"STATUS=FAIL count mismatch expected={len(expected)} actual={len(actual)}")
    sys.exit(1)

passed = 0
lines = []
lines.append("Gate G1 — PowerPC 750CL / Broadway Encoder Oracle vs powerpc-eabi-as + powerpc-eabi-objdump")
lines.append("=" * 94)
lines.append(f"{'Idx':<4} {'Name':<14} {'JitPpc':<12} {'GNU as':<12} {'Result':<6} {'Source Asm':<26} {'Objdump Disassembly'}")
lines.append("-" * 94)

for (idx, name, exp_word, src_asm), (act_word, dis) in zip(expected, actual):
    ok = (exp_word == act_word)
    if ok:
        passed += 1
    status = "PASS" if ok else "FAIL"
    lines.append(f"{idx:<4} {name:<14} {exp_word:<12} {act_word:<12} {status:<6} {src_asm:<26} {dis}")

lines.append("-" * 94)
overall = "PASS" if passed == len(expected) and passed >= 44 else "FAIL"
lines.append(f"FORMS_MATCHED={passed}/{len(expected)}")
lines.append(f"REQUIRED_MINIMUM=44/44")
lines.append(f"STATUS={overall}")

with open(out_path, "w") as f:
    f.write("\n".join(lines) + "\n")

print("\n".join(lines))
sys.exit(0 if overall == "PASS" else 1)
PY
