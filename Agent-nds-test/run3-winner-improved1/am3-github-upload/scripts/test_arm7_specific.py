#!/usr/bin/env python3
"""Execute and fault-test the original ARM7-specific vectors under the v3 group split."""
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from diagnostics import cpu_rows

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
manifest = json.loads(Path("src/generated/debug-manifest.json").read_text())
vectors = manifest["arm7_vectors"]
original = Path("suite.nds").read_bytes()
nm = Path(os.environ.get("DEVKITARM", "/opt/devkitpro/devkitARM")) / "bin/arm-none-eabi-nm"

# Keep this regression scoped to the original ARM7-only range. Later batches,
# including Thumb memory and the branch sweep, are tested by their own suites.
specific = vectors[3390:5174]
names = {row["name"] for row in specific}
assert len(specific) == 1784 and len(names) == 194
s7 = {
    symbol[2]: int(symbol[0], 16)
    for line in subprocess.check_output([str(nm), "artifacts/arm7.elf"], text=True).splitlines()
    if len(symbol := line.split()) == 3 and symbol[0][0] in "0123456789abcdef"
}
s9 = {
    symbol[2]: int(symbol[0], 16)
    for line in subprocess.check_output([str(nm), "artifacts/arm9.elf"], text=True).splitlines()
    if len(symbol := line.split()) == 3 and symbol[0][0] in "0123456789abcdef"
}
assert all(name in s7 and name not in s9 for name in names)
checks = {"arm7_only_helpers_not_linked_on_arm9": "PASS"}


def run_group(rom: bytes | bytearray, label: str, group_id: int, args=()):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "arm7.nds"
        path.write_bytes(rom)
        output = Path("reports") / f"arm7-specific-{label}-group{group_id}.json"
        process = subprocess.run(
            [
                sys.executable,
                "scripts/run_desmume.py",
                "--rom",
                str(path),
                "--output",
                str(output),
                "--child",
                str(group_id),
                *args,
            ],
            capture_output=True,
            text=True,
            timeout=90,
        )
        output.with_suffix(".log").write_text(process.stdout + process.stderr)
        assert process.returncode == 0, process.stderr
        report = json.loads(output.read_text())
        return report, cpu_rows(report["diagnostics"]["arm7"])


def run_full_profile(rom: bytes | bytearray, label: str, *, arm_args=(), frames=None):
    combined = {}
    reports = {}
    for group_id in (0, 1):
        args = list(arm_args) if group_id == 0 else []
        if frames is not None:
            args += ["--frames", str(frames)]
        report, rows = run_group(rom, label, group_id, args)
        reports[group_id] = report
        for row in rows:
            if row["status"] != "NOT_SELECTED":
                combined[row["id"]] = row
    assert len(combined) == len(vectors)
    return reports, [combined[i] for i in range(1, len(vectors) + 1)]


reports, rows = run_full_profile(
    original,
    "baseline",
    arm_args=("--detail-page", "10", "--debug-keys", "B", "--screenshot", "reports/arm7-specific-screen.png"),
)
assert len(rows) == 12056
assert all(row["function_pc"] == s7[row["name"]] and 0x02200000 <= row["function_pc"] < 0x02300000 for row in rows)
assert all(row["status"] == "PASS" for row in rows[:3390])
failed = [row for row in rows if row["status"] == "FAIL"]
assert len(failed) == 23
assert reports[0]["score"]["cpu7"] == {"passed": 5331, "failed": 13}
assert reports[1]["score"]["cpu7"] == {"passed": 6702, "failed": 10}
assert reports[0]["debug_navigation"][0][2] == 3412
assert any("022" in line for line in reports[0]["bottom_text"])
checks["every_case_matches_arm7_elf_entry_address"] = "PASS"
checks["all_1784_original_arm7_specific_cases_executed_and_23_mismatches_retained"] = "PASS"
checks["arm7_explorer_selects_real_failure"] = "PASS"

# A passing, original ARM7-only vector is deliberately broken in the ARM7 image.
vector = vectors[3390]
pattern = struct.pack(
    "<7I",
    s7[vector["name"]],
    *(vector[key] for key in ("a", "b", "initial", "result", "flags", "mask")),
)
rom = bytearray(original)
assert rom.count(pattern) == 1
position = rom.find(pattern)
offset, _entry, base, size = struct.unpack_from("<4I", rom, 0x30)
assert offset <= position < offset + size
struct.pack_into("<I", rom, position + 16, vector["result"] ^ 1)
mutated_reports, mutated_rows = run_full_profile(rom, "injected")
bad = [row["id"] for row in mutated_rows if row["status"] == "FAIL"]
assert bad == [3391] + [row["id"] for row in failed]
assert mutated_reports[0]["score"]["cpu7"] == {"passed": 5330, "failed": 14}
checks["arm7_payload_fault_injection_detected"] = "PASS"

# Hang the first original ARM7 helper. ARM rows after it remain NOT_RUN, while
# the independent ARM7Thumb run still completes and keeps its own mismatches.
rom = bytearray(original)
struct.pack_into("<I", rom, offset + s7[vector["name"]] - base, 0xEAFFFFFE)
hang_reports, interrupted_rows = run_full_profile(rom, "interrupted", frames=60)
assert hang_reports[0]["status"] in ("ROM_TIMEOUT", "HOST_FRAME_TIMEOUT")
assert all(row["status"] == "PASS" for row in interrupted_rows[:3390])
assert interrupted_rows[3390]["status"] == "INTERRUPTED"
assert all(row["status"] == "NOT_RUN" for row in interrupted_rows[3391:] if row["isa"] == "ARM")
assert all(row["status"] in ("PASS", "FAIL") for row in interrupted_rows if row["isa"] == "THUMB")
checks["arm7_only_hang_is_not_a_pass_and_thumb_group_remains_attributed"] = "PASS"

report = {
    "base_rom_sha256": hashlib.sha256(original).hexdigest(),
    "specific_vectors": len(specific),
    "specific_helpers": len(names),
    "reference_mismatches": len(failed),
    "tests": checks,
    "failures": failed,
    "exact_qemu_arm7tdmi": "UNAVAILABLE",
    "hardware_validation": "NOT_PERFORMED",
}
Path("reports/arm7-specific-tests.json").write_text(json.dumps(report, indent=2) + "\n")
for key, status in checks.items():
    print(status, key)
