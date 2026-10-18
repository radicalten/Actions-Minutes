#!/usr/bin/env python3
"""NDS-ROM audits, DeSmuME execution, and expectation fault injection for immediate ALU."""
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
ROM_SHA256 = hashlib.sha256(ROM).hexdigest()
MANIFEST = json.loads(Path("src/generated/debug-manifest.json").read_text(encoding="utf-8"))
GROUPS = json.loads(Path("src/groups.json").read_text(encoding="utf-8"))
NM = Path(os.environ.get("DEVKITARM", "/opt/devkitpro/devkitARM")) / "bin/arm-none-eabi-nm"
REPORTS = Path("reports")
REPORTS.mkdir(exist_ok=True)
SCOPES = {"arm_immediate_rotate", "thumb_immediate_alu"}
FAILURE = {"FAIL", "TRACE_INCONSISTENT"}
RESULTS: dict[str, dict] = {}
INJECTIONS: list[dict] = []


def symbols_for(cpu: int) -> dict[str, int]:
    elf = Path(f"artifacts/arm{cpu}.elf")
    lines = subprocess.check_output([str(NM), str(elf)], text=True).splitlines()
    return {
        parts[2]: int(parts[0], 16)
        for line in lines
        if len(parts := line.split()) == 3 and parts[0][0] in "0123456789abcdef"
    }


def group_id(name: str) -> int:
    matches = [item["id"] for item in GROUPS if item["name"] == name]
    if len(matches) != 1:
        raise AssertionError(f"expected one registered group {name}, got {matches}")
    return matches[0]


def run_cpu(cpu: int, isa: str, image: bytes | bytearray, label: str, selected_group: int) -> list[dict]:
    with tempfile.TemporaryDirectory(prefix="immediate-alu-") as directory:
        rom_path = Path(directory) / "fault.nds"
        rom_path.write_bytes(image)
        output = REPORTS / f"immediate-alu-{cpu}-{isa}-{label}.json"
        process = subprocess.run(
            [
                sys.executable,
                "scripts/run_desmume.py",
                "--child",
                str(selected_group),
                "--rom",
                str(rom_path),
                "--output",
                str(output),
            ],
            capture_output=True,
            text=True,
            timeout=150,
            check=False,
        )
        output.with_suffix(".log").write_text(process.stdout + process.stderr, encoding="utf-8")
        if process.returncode != 0 or not output.exists():
            raise RuntimeError(
                f"ARM{cpu} {isa} {label}: DeSmuME child harness failed ({process.returncode})\n"
                f"{process.stdout}\n{process.stderr}"
            )
        report = json.loads(output.read_text(encoding="utf-8"))
        if report.get("status") not in ("PASS", "FAIL"):
            raise RuntimeError(f"ARM{cpu} {isa} {label}: unexpected result {report.get('status')}")
        if not report.get("rom_sha256"):
            raise AssertionError(f"ARM{cpu} {isa} {label}: child report omitted its executed ROM hash")
        return cpu_rows(report["diagnostics"][f"arm{cpu}"])


def failing(rows: list[dict]) -> set[int]:
    return {row["id"] for row in rows if row["status"] in FAILURE}


def rom_offset_for(cpu: int) -> tuple[int, int, int]:
    header = 0x20 if cpu == 9 else 0x30
    offset, _entry, load_address, load_size = struct.unpack_from("<4I", ROM, header)
    return offset, load_address, load_size


def audit_encodings(cpu: int, vectors: list[dict], symbols: dict[str, int]) -> dict:
    rows = [row for row in vectors if row.get("verification_scope") in SCOPES]
    by_name: dict[str, list[dict]] = {}
    for row in rows:
        by_name.setdefault(row["name"], []).append(row)
    offset, load_address, load_size = rom_offset_for(cpu)

    def rom_position(address: int) -> int:
        if not load_address <= address < load_address + load_size:
            raise AssertionError(f"ARM{cpu}: symbol outside loadable ROM section: {address:#x}")
        return offset + address - load_address

    arm_helpers = thumb_helpers = 0
    for name, cases in by_name.items():
        scope_set = {row["verification_scope"] for row in cases}
        assert len(scope_set) == 1, (name, scope_set)
        scope = next(iter(scope_set))
        symbol = name + "_instruction"
        if symbol not in symbols:
            raise AssertionError(f"ARM{cpu}: instruction label missing from ELF: {symbol}")
        address = symbols[symbol]
        pos = rom_position(address)
        if scope == "arm_immediate_rotate":
            assert address % 4 == 0
            opcodes = {row["expected_arm_opcode"] for row in cases}
            assert len(opcodes) == 1
            actual = struct.unpack_from("<I", ROM, pos)[0]
            expected = next(iter(opcodes))
            assert actual == expected, (cpu, name, hex(actual), hex(expected))
            assert actual >> 28 == 0xE and actual & (1 << 25)
            assert (actual >> 21) & 0xF == cases[0]["expected_arm_opcode"] >> 21 & 0xF
            arm_helpers += 1
        else:
            assert scope == "thumb_immediate_alu"
            assert address % 2 == 0
            opcodes = {row["expected_thumb_opcode"] for row in cases}
            assert len(opcodes) == 1
            actual = struct.unpack_from("<H", ROM, pos)[0]
            expected = next(iter(opcodes))
            assert actual == expected, (cpu, name, hex(actual), hex(expected))
            assert actual & 0xF800 in (0x2000, 0x2800, 0x3000, 0x3800)
            thumb_helpers += 1
    assert (arm_helpers, thumb_helpers) == (512, 1024), (cpu, arm_helpers, thumb_helpers)
    return {"arm_instruction_encodings": arm_helpers, "thumb_instruction_encodings": thumb_helpers}


def choose(rows: list[dict], scope: str, op: str, *, immediate: int | None = None,
           rotate: int | None = None, initial: int | None = None) -> dict:
    selected = [
        row for row in rows
        if row.get("verification_scope") == scope and row.get("operation") == op
    ]
    if immediate is not None:
        selected = [row for row in selected if row.get("encoded_imm8") == immediate]
    if rotate is not None:
        selected = [row for row in selected if row.get("rotate_field") == rotate]
    if initial is not None:
        selected = [row for row in selected if row["initial"] == initial]
    if not selected:
        raise AssertionError((scope, op, immediate, rotate, initial))
    return selected[0]


def patch_expectation(image: bytearray, symbols: dict[str, int], row: dict, field: str, xor: int) -> None:
    pattern = struct.pack(
        "<7I",
        symbols[row["name"]],
        *(row[key] for key in ("a", "b", "initial", "result", "flags", "mask")),
    )
    at = image.find(pattern)
    assert at >= 0 and image.count(pattern) == 1, (row["id"], row["name"], field)
    byte_offset = 16 if field == "result" else 20
    struct.pack_into("<I", image, at + byte_offset, row[field] ^ xor)


def main() -> int:
    per_cpu: dict[int, dict] = {}
    for cpu in (7, 9):
        key = "arm7_vectors" if cpu == 7 else "vectors"
        vectors = MANIFEST[key]
        family_rows = [row for row in vectors if row.get("verification_scope") in SCOPES]
        arm_rows = [row for row in family_rows if row.get("verification_scope") == "arm_immediate_rotate"]
        thumb_rows = [row for row in family_rows if row.get("verification_scope") == "thumb_immediate_alu"]
        assert (len(arm_rows), len(thumb_rows), len(family_rows)) == (528, 2048, 2576)
        symbols = symbols_for(cpu)
        layout = audit_encodings(cpu, vectors, symbols)
        per_cpu[cpu] = {"vectors": vectors, "family_rows": family_rows, "symbols": symbols, "layout": layout}

        for isa, cases, expected_count in (("ARM", arm_rows, 528), ("Thumb", thumb_rows, 2048)):
            name = f"ARM{cpu}" if isa == "ARM" else f"ARM{cpu}Thumb"
            selected_group = group_id(name)
            observed = run_cpu(cpu, isa, ROM, "baseline", selected_group)
            assert len(observed) == len(vectors)
            for row in cases:
                result = observed[row["id"] - 1]
                assert result["status"] == "PASS", (
                    cpu, isa, row["id"], row["name"], result["status"],
                    result.get("observed"), row["result"], result.get("observed_aux"), row["flags"],
                )
                assert result["function_pc"] == symbols[row["name"]]
                assert result["runtime_input_fingerprint_matches"] is True
            other_scope = "thumb_immediate_alu" if isa == "ARM" else "arm_immediate_rotate"
            assert all(
                observed[row["id"] - 1]["status"] == "NOT_SELECTED"
                for row in family_rows if row.get("verification_scope") == other_scope
            )
            prior_bad = failing(observed)
            assert prior_bad.isdisjoint(row["id"] for row in cases)
            per_cpu[cpu][f"{isa.lower()}_baseline_failures"] = sorted(prior_bad)
            per_cpu[cpu][f"{isa.lower()}_passed"] = expected_count

            result_case = (
                choose(family_rows, "arm_immediate_rotate", "adc", rotate=5, initial=0x30000000)
                if isa == "ARM"
                else choose(family_rows, "thumb_immediate_alu", "adds", immediate=1)
            )
            flags_case = (
                choose(family_rows, "arm_immediate_rotate", "tst", rotate=0, initial=0x10000000)
                if isa == "ARM"
                else choose(family_rows, "thumb_immediate_alu", "movs", immediate=0, initial=0x10000000)
            )
            for field, case, xor in (("result", result_case, 1), ("flags", flags_case, 0x10000000)):
                fault = bytearray(ROM)
                patch_expectation(fault, symbols, case, field, xor)
                after = run_cpu(cpu, isa, fault, f"{field}-mutation", selected_group)
                new_bad = failing(after) - prior_bad
                assert new_bad == {case["id"]}, (cpu, isa, field, sorted(new_bad), case["id"])
                assert failing(after) - {case["id"]} == prior_bad
                assert after[case["id"] - 1]["runtime_differs_manifest"] is True
                INJECTIONS.append({"cpu": cpu, "isa": isa, "field": field, "detected_id": case["id"]})

    report = {
        "rom_sha256": ROM_SHA256,
        "matrix_per_cpu": {"arm_immediate_rotate": 528, "thumb_immediate_alu": 2048, "total": 2576},
        "helpers_per_cpu": {"ARM": 512, "Thumb": 1024},
        "encoding_audits": {f"ARM{cpu}": data["layout"] for cpu, data in per_cpu.items()},
        "focused_desmume": {
            f"ARM{cpu}": {
                "ARM_vectors_passed": data["arm_passed"],
                "Thumb_vectors_passed": data["thumb_passed"],
                "preexisting_ARM_failures": len(data["arm_baseline_failures"]),
                "preexisting_Thumb_failures": len(data["thumb_baseline_failures"]),
            }
            for cpu, data in per_cpu.items()
        },
        "fault_injections": INJECTIONS,
        "exact_arm7tdmi_external_oracle": "UNAVAILABLE",
        "scope_note": "DeSmuME runs execute the NDS CPU/ISA groups; QEMU is the independent functional comparator, with ti925t proxy-only. No physical DS validation is claimed.",
    }
    output = REPORTS / "immediate-alu-rom-tests.json"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
