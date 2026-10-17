#!/usr/bin/env python3
"""Regression tests for fail-closed release gating; temporary fixtures are not release evidence."""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GATE = ROOT / "scripts/release_gate.py"


class ReleaseGateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "scripts").mkdir()
        shutil.copy2(GATE, self.root / "scripts/release_gate.py")
        self.rom = self.root / "suite.nds"
        self.rom.write_bytes(b"test-only ROM identity")
        (self.root / "LICENSE").write_text("test-only license fixture\n", encoding="utf-8")
        self.seed_inputs()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def rom_hash(self) -> str:
        import hashlib

        return hashlib.sha256(self.rom.read_bytes()).hexdigest()

    def seed_inputs(self) -> None:
        for directory in ("docs", "release", "reports"):
            (self.root / directory).mkdir(parents=True, exist_ok=True)
        (self.root / "docs/remaining-cases.json").write_text(
            json.dumps(
                {
                    "schema": 1,
                    "items": [
                        {
                            "area": "fixture_defined_matrix",
                            "status": "IMPLEMENTED_DEFINED_MATRIX",
                            "scope": "Synthetic test fixture only",
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        (self.root / "coverage.json").write_text(
            json.dumps({"cpu": {"uncovered": []}}), encoding="utf-8"
        )
        (self.root / "release/v1.0-plan.json").write_text(
            json.dumps(
                {
                    "license_approved": True,
                    "redistribution_cleared": False,
                    "milestones": [
                        {"id": "verified_standalone_folder", "status": "PENDING"},
                        {"id": "coverage", "status": "PASS"},
                    ],
                }
            ),
            encoding="utf-8",
        )
        (self.root / "reports/development.json").write_text(
            json.dumps(
                {
                    "group_schema": 3,
                    "score_protocol": 3,
                    "trace_protocol": 5,
                    "base_rom_sha256": self.rom_hash(),
                    "score": {"unrun": 0, "planned": 1},
                }
            ),
            encoding="utf-8",
        )
        (self.root / "reports/development-validation.json").write_text(
            json.dumps(
                {"rom_sha256": self.rom_hash(), "full_release_regression": "PENDING"}
            ),
            encoding="utf-8",
        )

    def run_gate(self, *extra: str) -> tuple[int, dict[str, object], Path]:
        output = self.root / "nested/output/readiness.json"
        process = subprocess.run(
            [
                sys.executable,
                str(self.root / "scripts/release_gate.py"),
                "--output",
                str(output),
                *extra,
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        try:
            result = json.loads(process.stdout)
        except json.JSONDecodeError as exc:
            self.fail(f"gate did not emit JSON: {process.stdout!r}; stderr={process.stderr!r}: {exc}")
        self.assertTrue(output.is_file(), "gate should create the output parent directory")
        self.assertEqual(json.loads(output.read_text(encoding="utf-8")), result)
        return process.returncode, result, output

    @staticmethod
    def blocker_areas(result: dict[str, object]) -> set[str]:
        blockers = result.get("blockers")
        assert isinstance(blockers, list)
        return {str(item["area"]) for item in blockers if isinstance(item, dict)}

    def test_known_blockers_are_reported_without_rewriting_approval(self) -> None:
        code, result, _ = self.run_gate()
        self.assertEqual(code, 2)
        self.assertEqual(result["status"], "BLOCKED")
        self.assertTrue(
            {
                "upstream_redistribution",
                "release_regression",
                "v1_verified_standalone_folder",
            }
            <= self.blocker_areas(result)
        )
        self.assertNotIn("original_code_license", self.blocker_areas(result))

    def test_missing_clean_checkout_inputs_are_blockers_not_errors(self) -> None:
        for path in (
            "docs/remaining-cases.json",
            "coverage.json",
            "release/v1.0-plan.json",
            "reports/development.json",
            "reports/development-validation.json",
        ):
            (self.root / path).unlink()
        code, result, _ = self.run_gate(
            "--report", "reports/absent.json", "--validation", "reports/absent-validation.json"
        )
        self.assertEqual(code, 2)
        self.assertEqual(result["status"], "BLOCKED")
        self.assertTrue(
            {
                "remaining_case_ledger",
                "coverage_map",
                "release_plan",
                "evidence",
                "release_regression",
                "upstream_redistribution",
            }
            <= self.blocker_areas(result)
        )

    def test_required_standalone_folder_milestone_cannot_be_omitted(self) -> None:
        plan_path = self.root / "release/v1.0-plan.json"
        plan = json.loads(plan_path.read_text(encoding="utf-8"))
        plan["milestones"] = [{"id": "coverage", "status": "PASS"}]
        plan_path.write_text(json.dumps(plan), encoding="utf-8")
        code, result, _ = self.run_gate()
        self.assertEqual(code, 2)
        self.assertIn("v1_verified_standalone_folder", self.blocker_areas(result))

    def test_license_file_is_required_even_when_plan_marks_approval(self) -> None:
        (self.root / "LICENSE").unlink()
        code, result, _ = self.run_gate()
        self.assertEqual(code, 2)
        self.assertIn("original_code_license", self.blocker_areas(result))

    def test_stale_rom_invalidates_both_evidence_sets(self) -> None:
        self.rom.write_bytes(b"different ROM")
        code, result, _ = self.run_gate()
        self.assertEqual(code, 2)
        areas = self.blocker_areas(result)
        self.assertIn("evidence", areas)
        self.assertIn("release_regression", areas)

    def test_malformed_gate_input_is_an_error_with_machine_readable_output(self) -> None:
        (self.root / "coverage.json").write_text("{not json", encoding="utf-8")
        code, result, _ = self.run_gate()
        self.assertEqual(code, 3)
        self.assertEqual(result["status"], "ERROR")
        self.assertIn("Malformed JSON", str(result["error"]))

    def test_ready_requires_all_synthetic_gate_conditions(self) -> None:
        plan_path = self.root / "release/v1.0-plan.json"
        plan = json.loads(plan_path.read_text(encoding="utf-8"))
        plan["redistribution_cleared"] = True
        for milestone in plan["milestones"]:
            milestone["status"] = "PASS"
        plan_path.write_text(json.dumps(plan), encoding="utf-8")
        validation_path = self.root / "reports/development-validation.json"
        validation_path.write_text(
            json.dumps(
                {"rom_sha256": self.rom_hash(), "full_release_regression": "PASS"}
            ),
            encoding="utf-8",
        )
        code, result, _ = self.run_gate()
        self.assertEqual(code, 0)
        self.assertEqual(result["status"], "READY")
        self.assertEqual(result["blockers"], [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
