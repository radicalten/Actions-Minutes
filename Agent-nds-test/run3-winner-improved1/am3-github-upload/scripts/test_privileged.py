#!/usr/bin/env python3
"""Real ARM7 privileged-state and IRQ tests across the separate ARM/Thumb groups."""
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
original = Path("suite.nds").read_bytes()
vectors = json.loads(Path("src/generated/debug-manifest.json").read_text())["arm7_vectors"]
nm = Path(os.environ.get("DEVKITARM", "/opt/devkitpro/devkitARM")) / "bin/arm-none-eabi-nm"
lines = subprocess.check_output([str(nm), "artifacts/arm7.elf"], text=True).splitlines()
syms = {
    symbol[2]: int(symbol[0], 16)
    for line in lines
    if len(symbol := line.split()) == 3 and symbol[0][0] in "0123456789abcdef"
}
offset, _entry, base, size = struct.unpack_from("<4I", original, 0x30)


def position(address):
    assert base <= address < base + size
    return offset + address - base


def run(rom, name):
    reports = {}
    combined = {}
    for group_id in (0, 1):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "injected.nds"
            path.write_bytes(rom)
            output = Path("reports") / f"{name}-group{group_id}.json"
            process = subprocess.run(
                [
                    sys.executable,
                    "scripts/run_desmume.py",
                    "--child",
                    str(group_id),
                    "--rom",
                    str(path),
                    "--output",
                    str(output),
                ],
                capture_output=True,
                text=True,
                timeout=90,
            )
            output.with_suffix(".log").write_text(process.stdout + process.stderr)
            assert process.returncode == 0, process.stderr
            report = json.loads(output.read_text())
            reports[group_id] = report
            for row in cpu_rows(report["diagnostics"]["arm7"]):
                if row["status"] != "NOT_SELECTED":
                    combined[row["id"]] = row
    assert len(combined) == len(vectors)
    return reports, [combined[i] for i in range(1, len(vectors) + 1)]


reports, rows = run(original, "privileged-baseline")
new = rows[3578:4630]
assert len(new) == 1052 and all(row["status"] == "PASS" for row in new)
assert sum(row["name"].startswith("cpu_arm7_irq_entry_") for row in new) == 96
assert reports[0]["score"]["cpu7"] == {"passed": 5331, "failed": 13}
assert reports[1]["score"]["cpu7"] == {"passed": 6702, "failed": 10}
checks = {"all_1052_new_cases_pass_on_arm7": "PASS"}
labels = [
    "bank_sp_", "bank_lr_", "bank_r8_", "spsr_banks_", "spsr_field_",
    "cpsr_mode_", "return_movs_", "return_subs_", "return_ldm_",
    "irq_entry_arm", "irq_entry_thumb", "irq_entry_user",
    "irq_entry_masked_i", "irq_entry_masked_ime", "irq_entry_masked_ie",
]
rom = bytearray(original)
ids = []
for label in labels:
    vector = next(row for row in vectors[3578:] if row["name"].startswith("cpu_arm7_" + label))
    ids.append(vector["id"])
    pattern = struct.pack(
        "<7I", syms[vector["name"]],
        *(vector[key] for key in ("a", "b", "initial", "result", "flags", "mask")),
    )
    assert rom.count(pattern) == 1
    position_in_rom = rom.find(pattern)
    assert offset <= position_in_rom < offset + size
    struct.pack_into("<I", rom, position_in_rom + 16, vector["result"] ^ 1)
reports, rows = run(rom, "privileged-negative")
assert [row["id"] for row in rows[3578:4630] if row["status"] == "FAIL"] == sorted(ids)
checks["fifteen_new_family_expectation_faults_detected"] = "PASS"

# Suppress only the IRQ callback counter, not the actual IRQ or User escape.
rom = bytearray(original)
start = position(syms["cpu_arm7_timer_irq_handler"])
pattern = struct.pack("<I", 0xE2811001)
position_in_rom = rom.find(pattern, start, start + 160)
assert position_in_rom >= 0
struct.pack_into("<I", rom, position_in_rom, 0xE3A01000)
reports, rows = run(rom, "privileged-irq-counter-negative")
bad = [row for row in rows[3578:4630] if row["status"] == "FAIL"]
assert len(bad) == 96 and all(row["name"].startswith("cpu_arm7_irq_entry_") for row in bad)
assert all(row["aux_masked_xor"] & 0x10000 for row in bad)
checks["missing_irq_event_counter_never_passes"] = "PASS"

# Release CPSR.I too early: the pre-release sample catches the premature IRQ.
rom = bytearray(original)
start = position(syms["cpu_arm7_irq_entry_masked_i"])
pattern = struct.pack("<I", 0xE38000DF)
position_in_rom = rom.find(pattern, start, start + 512)
assert position_in_rom >= 0
struct.pack_into("<I", rom, position_in_rom, 0xE380005F)
reports, rows = run(rom, "privileged-irq-early-negative")
bad = [row for row in rows[3578:4630] if row["status"] == "FAIL"]
assert len(bad) == 16 and all(row["name"] == "cpu_arm7_irq_entry_masked_i" for row in bad)
assert all(row["aux_masked_xor"] & 0x40000 for row in bad)
checks["premature_irq_before_I_release_detected"] = "PASS"

Path("reports/privileged-tests.json").write_text(
    json.dumps(
        {
            "base_rom_sha256": hashlib.sha256(original).hexdigest(),
            "new_vectors": 1052,
            "portable_vectors": 956,
            "nds_irq_vectors": 96,
            "tests": checks,
            "injected_case_ids": ids,
        },
        indent=2,
    ) + "\n"
)
for name, status in checks.items():
    print(status, name)
