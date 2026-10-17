#!/usr/bin/env python3
"""ROM-level Thumb branch tests, instruction checks, and expectation fault injection."""
from __future__ import annotations

import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
sys.path.insert(0, str(ROOT / "scripts"))
from diagnostics import cpu_rows  # noqa: E402

ROM = Path("suite.nds").read_bytes()
MANIFEST = json.loads(Path("src/generated/debug-manifest.json").read_text(encoding="utf-8"))
GROUPS = json.loads(Path("src/groups.json").read_text(encoding="utf-8"))
NM = Path(os.environ.get("DEVKITARM", "/opt/devkitpro/devkitARM")) / "bin/arm-none-eabi-nm"
REPORTS = Path("reports")
REPORTS.mkdir(exist_ok=True)
CHECKS: dict[str, str] = {}
INJECTIONS: list[dict] = []


def failing(rows: list[dict]) -> set[int]:
    return {row["id"] for row in rows if row["status"] in ("FAIL", "TRACE_INCONSISTENT")}


def run_cpu(cpu: int, image: bytes | bytearray, label: str, group_id: int) -> list[dict]:
    with tempfile.TemporaryDirectory(prefix="thumb-branch-") as directory:
        rom_path = Path(directory) / "fault.nds"
        rom_path.write_bytes(image)
        output = REPORTS / f"thumb-branch-{cpu}-{label}.json"
        process = subprocess.run(
            [
                sys.executable,
                "scripts/run_desmume.py",
                "--child",
                str(group_id),
                "--rom",
                str(rom_path),
                "--output",
                str(output),
            ],
            capture_output=True,
            text=True,
            timeout=90,
            check=False,
        )
        output.with_suffix(".log").write_text(process.stdout + process.stderr, encoding="utf-8")
        if process.returncode != 0 or not output.exists():
            raise RuntimeError(
                f"ARM{cpu} {label}: DeSmuME child harness failed ({process.returncode})\n"
                f"{process.stdout}\n{process.stderr}"
            )
        report = json.loads(output.read_text(encoding="utf-8"))
        if report.get("status") not in ("PASS", "FAIL"):
            raise RuntimeError(f"ARM{cpu} {label}: unexpected ROM result {report.get('status')}")
        return cpu_rows(report["diagnostics"][f"arm{cpu}"])


def symbols_for(cpu: int) -> dict[str, int]:
    elf = Path(f"artifacts/arm{cpu}.elf")
    lines = subprocess.check_output([str(NM), str(elf)], text=True).splitlines()
    return {
        parts[2]: int(parts[0], 16)
        for line in lines
        if len(parts := line.split()) == 3 and parts[0][0] in "0123456789abcdef"
    }


def choose(cases: list[dict], name: str, initial: int | None = None) -> dict:
    matching = [row for row in cases if row["name"] == name]
    if initial is not None:
        matching = [row for row in matching if row["initial"] == initial]
    if len(matching) != 1:
        raise AssertionError(f"expected one vector for {name} with flags {initial}, got {len(matching)}")
    return matching[0]


def check_instruction_layout(cpu: int, cases: list[dict], syms: dict[str, int], pos) -> None:
    helpers = {row["name"] for row in cases}
    assert len(helpers) == 306
    for name in helpers:
        row = next(item for item in cases if item["name"] == name)
        site = syms[name + "_site"]
        target = syms[name + "_target"]
        assert site % 2 == 0 and target % 2 == 0
        assert row["branch_base"] == "instruction_address + 4"
        assert target == site + 4 + row["displacement_bytes"]
        # Read the original NDS image, not the ELF's link-time section bytes.
        opcode = struct.unpack_from("<H", ROM, pos(site))[0]
        assert opcode == row["expected_thumb_opcode"]
        for vector in (item for item in cases if item["name"] == name):
            assert target - (site + 4) == vector["displacement_bytes"]
            if vector["operation"] == "conditional_branch":
                assert opcode & 0xF000 == 0xD000 and ((opcode >> 8) & 0xF) == vector["condition_code"]
                immediate = opcode & 0xFF
                signed_immediate = immediate - 0x100 if immediate & 0x80 else immediate
                assert (immediate, signed_immediate * 2) == (
                    vector["displacement_halfwords"] & 0xFF,
                    vector["displacement_bytes"],
                )
            else:
                assert opcode & 0xF800 == 0xE000
                immediate = opcode & 0x7FF
                signed_immediate = immediate - 0x800 if immediate & 0x400 else immediate
                assert (immediate, signed_immediate * 2) == (
                    vector["displacement_halfwords"] & 0x7FF,
                    vector["displacement_bytes"],
                )


def patch_expectation(image: bytearray, syms: dict[str, int], row: dict, field: str, xor: int) -> None:
    pattern = struct.pack(
        "<7I",
        syms[row["name"]],
        *(row[key] for key in ("a", "b", "initial", "result", "flags", "mask")),
    )
    at = image.find(pattern)
    assert at >= 0 and image.count(pattern) == 1, (row["id"], row["name"], field)
    offset = 16 if field == "result" else 20
    struct.pack_into("<I", image, at + offset, row[field] ^ xor)


def confirm_injection(
    cpu: int,
    label: str,
    group_id: int,
    baseline_bad: set[int],
    expected: set[int],
    image: bytes | bytearray,
    *,
    manifest_mutation: bool = False,
) -> None:
    actual = run_cpu(cpu, image, label, group_id)
    new_bad = failing(actual) - baseline_bad
    assert new_bad == expected, (cpu, label, sorted(new_bad), sorted(expected))
    assert failing(actual) - expected == baseline_bad
    if manifest_mutation:
        assert all(actual[vector_id - 1]["runtime_differs_manifest"] for vector_id in expected)
    INJECTIONS.append({"cpu": cpu, "scenario": label, "detected_ids": sorted(expected)})
    CHECKS[f"arm{cpu}_{label}"] = "PASS"


def main() -> int:
    rom_sha256 = hashlib.sha256(ROM).hexdigest()
    per_cpu: dict[int, dict] = {}
    for cpu in (7, 9):
        key = "arm7_vectors" if cpu == 7 else "vectors"
        vectors = MANIFEST[key]
        edge_cases = [row for row in vectors if row.get("verification_scope") == "thumb_branch"]
        sweep_cases = [row for row in vectors if row.get("verification_scope") == "thumb_branch_sweep"]
        cases = edge_cases + sweep_cases
        assert len(edge_cases) == 320 and len(sweep_cases) == 1018 and len(cases) == 1338
        group = next(item for item in GROUPS if item["name"] == f"ARM{cpu}Thumb")
        syms = symbols_for(cpu)
        header = 0x30 if cpu == 7 else 0x20
        rom_offset, _entry, load_address, load_size = struct.unpack_from("<4I", ROM, header)

        def pos(
            address: int,
            _offset: int = rom_offset,
            _base: int = load_address,
            _size: int = load_size,
        ) -> int:
            assert _base <= address < _base + _size, hex(address)
            return _offset + address - _base

        check_instruction_layout(cpu, cases, syms, pos)
        rows = run_cpu(cpu, ROM, "baseline", group["id"])
        assert len(rows) == len(vectors)
        assert all(
            rows[row["id"] - 1]["status"] == "PASS"
            and rows[row["id"] - 1]["function_pc"] == syms[row["name"]]
            for row in cases
        )
        assert all(row["status"] == "NOT_SELECTED" for row in rows if row["isa"] == "ARM")
        baseline_bad = failing(rows)
        assert baseline_bad.isdisjoint(row["id"] for row in cases)
        thumb_failures = [row for row in rows if row["isa"] == "THUMB" and row["status"] == "FAIL"]
        per_cpu[cpu] = {
            "group_id": group["id"],
            "selected_thumb": sum(row["isa"] == "THUMB" for row in vectors),
            "branch_passed": len(cases),
            "preexisting_thumb_failures": len(thumb_failures),
            "baseline_failed_ids": sorted(baseline_bad),
            "symbols": syms,
            "cases": cases,
            "vectors": vectors,
            "baseline_bad": baseline_bad,
            "pos": pos,
        }
        CHECKS[f"arm{cpu}_1338_rom_executions_and_306_encodings_targets"] = "PASS"

    for cpu in (7, 9):
        state = per_cpu[cpu]
        cases, syms, group_id = state["cases"], state["symbols"], state["group_id"]
        baseline_bad = state["baseline_bad"]
        # Exercise both result and NZCV comparator fields using rows from all
        # three path layouts (conditional backward/forward and unconditional).
        chosen = [
            choose(cases, f"cpu_arm{cpu}_thumb_branch_cond_eq_dm256", 0x40000000),
            choose(cases, f"cpu_arm{cpu}_thumb_branch_cond_ne_dp254", 0x40000000),
            choose(cases, f"cpu_arm{cpu}_thumb_branch_b_dm2048", 0x90000000),
            choose(cases, f"cpu_arm{cpu}_thumb_branch_b_dp2046", 0xF0000000),
        ]
        for field, xor in (("result", 1), ("flags", 0x10000000)):
            image = bytearray(ROM)
            for row in chosen:
                patch_expectation(image, syms, row, field, xor)
            confirm_injection(
                cpu,
                f"four_{field}_expectations_rejected",
                group_id,
                baseline_bad,
                {row["id"] for row in chosen},
                image,
                manifest_mutation=True,
            )

        # Change EQ to NE at the -256 site. Every NZCV state must invert the
        # target/fallthrough outcome and all 16 rows should be diagnosed.
        image = bytearray(ROM)
        eq_name = f"cpu_arm{cpu}_thumb_branch_cond_eq_dm256"
        eq_site = state["pos"](syms[eq_name + "_site"])
        original_eq_opcode = struct.unpack_from("<H", ROM, eq_site)[0]
        assert original_eq_opcode & 0xFF00 == 0xD000
        struct.pack_into("<H", image, eq_site, original_eq_opcode ^ 0x0100)

        # The maximum positive B encoding is +2046. Decrementing its immediate
        # by one halfword lands exactly on the final guard BX, not the target.
        b_name = f"cpu_arm{cpu}_thumb_branch_b_dp2046"
        b_site = state["pos"](syms[b_name + "_site"])
        original_b_opcode = struct.unpack_from("<H", ROM, b_site)[0]
        assert original_b_opcode == 0xE3FF
        struct.pack_into("<H", image, b_site, original_b_opcode ^ 1)

        # At displacement -2, EQ's target is exactly the fallthrough address.
        # Changing it to 0 makes the taken row skip the marker and fail.
        alias_name = f"cpu_arm{cpu}_thumb_branch_sweep_eq_dm2"
        alias_row = choose(cases, alias_name, 0x40000000)
        alias_site = state["pos"](syms[alias_name + "_site"])
        alias_opcode = struct.unpack_from("<H", ROM, alias_site)[0]
        assert alias_opcode == 0xD0FF and alias_row["target_equals_fallthrough"]
        struct.pack_into("<H", image, alias_site, alias_opcode & 0xFF00)

        expected = {
            row["id"]
            for row in cases
            if row["name"] in (eq_name, b_name)
        } | {alias_row["id"]}
        assert len(expected) == 33 and expected.issubset({row["id"] for row in cases})
        confirm_injection(
            cpu,
            "condition_range_and_sweep_mutations_rejected",
            group_id,
            baseline_bad,
            expected,
            image,
        )

    report = {
        "base_rom_sha256": rom_sha256,
        "rom_sha256": rom_sha256,
        "branch_cases_per_cpu": 1338,
        "edge_cases_per_cpu": 320,
        "sweep_cases_per_cpu": 1018,
        "unique_helpers_per_cpu": 306,
        "actual_rom_encoding_and_target_checks": 612,
        "baseline": {
            f"arm{cpu}": {
                "group_id": state["group_id"],
                "thumb_vectors_selected": state["selected_thumb"],
                "branch_vectors_passed": state["branch_passed"],
                "preexisting_thumb_failures_unchanged_by_branch_addition": state["preexisting_thumb_failures"],
            }
            for cpu, state in per_cpu.items()
        },
        "tests": CHECKS,
        "fault_injections": INJECTIONS,
        "evidence_scope": "DeSmuME NDS ROM execution; not physical hardware and not an exact ARM7TDMI oracle",
    }
    output = REPORTS / "thumb-branch-tests.json"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
