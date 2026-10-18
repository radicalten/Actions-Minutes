#!/usr/bin/env python3
"""Host-only tests for fail-closed, CPU-qualified emulator-run comparison."""
import unittest

from compare_emulator_runs import (
    _classify_pair,
    compare_runs,
    validate_report_identity,
)
from diagnostics import MANIFEST
from trace_layout import VERSION


ROM = "a" * 64


def case(*, status="PASS", observed=0x1234, observed_aux=0xA, historical=False, mask=0xF):
    return {
        "cpu": "ARM9",
        "id": 7,
        "name": "cpu_arm9_thumb_example",
        "isa": "THUMB",
        "cpu_group": "ARM9Thumb",
        "group_id": 3,
        "architecture": "ARM946E-S",
        "verification_scope": "synthetic test row",
        "source": "test-reference",
        "assembly_source": "src/generated/v5.s:1",
        "a": 1,
        "b": 2,
        "initial": 0,
        "result": 0x1234,
        "flags": 0xA,
        "mask": mask,
        "status": status,
        "observed": observed,
        "observed_aux": observed_aux,
        "function_pc": 0x02001000,
        "historical": historical,
    }


def run(rows, *, plan=2):
    return {
        "label": "test",
        "backend": "synthetic",
        "rom_sha256": ROM,
        "manifest_crc32": int(MANIFEST["crc32"]),
        "selected_isa": {"ARM7": 0, "ARM9": plan},
        "rows": rows,
    }


class EmulatorDiffTests(unittest.TestCase):
    def test_candidate_pass_transition_is_descriptive_not_a_truth_claim(self):
        delta = _classify_pair(
            case(status="FAIL", observed=0),
            case(status="PASS", observed=0x1234),
        )
        self.assertEqual(delta["classification"], "CANDIDATE_PASSES_WHERE_REFERENCE_FAILS")
        self.assertTrue(delta["candidate_matches_registered_expectation"])
        self.assertFalse(delta["reference_matches_registered_expectation"])

    def test_checked_primary_output_divergence_is_reported(self):
        delta = _classify_pair(
            case(status="FAIL", observed=0x1234),
            case(status="FAIL", observed=0x1235),
        )
        self.assertEqual(delta["classification"], "CHECKED_OUTPUT_DIVERGENCE")
        self.assertTrue(delta["primary_changed"])

    def test_masked_auxiliary_difference_is_distinguished_from_raw_difference(self):
        delta = _classify_pair(case(observed_aux=0xA), case(observed_aux=0xB))
        self.assertEqual(delta["classification"], "CHECKED_OUTPUT_DIVERGENCE")
        self.assertTrue(delta["auxiliary_raw_changed"])
        self.assertTrue(delta["auxiliary_masked_changed"])

    def test_unmasked_auxiliary_difference_is_not_called_a_test_failure(self):
        delta = _classify_pair(
            case(observed_aux=0xA, mask=0xF),
            case(observed_aux=0x1A, mask=0xF),
        )
        self.assertEqual(delta["classification"], "UNMASKED_AUXILIARY_DIVERGENCE")
        self.assertTrue(delta["auxiliary_raw_changed"])
        self.assertFalse(delta["auxiliary_masked_changed"])

    def test_restore_provenance_change_is_visible_without_counting_as_behavioral(self):
        delta = _classify_pair(case(historical=True), case(historical=False))
        self.assertEqual(delta["classification"], "PROVENANCE_ONLY")
        self.assertTrue(delta["provenance_changed"])
        result = compare_runs(
            run({("ARM9", 7): case(historical=True)}),
            run({("ARM9", 7): case(historical=False)}),
        )
        self.assertEqual(result["summary"]["behavioral_differences"], 0)
        self.assertEqual(result["summary"]["provenance_only_vectors"], 1)

    def test_unselected_case_is_not_classified_as_a_behavioral_difference(self):
        result = compare_runs(
            run({("ARM9", 7): case()}, plan=2),
            run({}, plan=0),
        )
        self.assertEqual(result["summary"]["compared_executed_vectors"], 0)
        self.assertEqual(result["summary"]["not_comparable_vectors"], 1)
        self.assertEqual(result["not_comparable"][0]["reason"], "not_selected_in_candidate")

    def test_selected_but_absent_trace_record_is_not_comparable(self):
        result = compare_runs(
            run({("ARM9", 7): case()}, plan=2),
            run({}, plan=2),
        )
        self.assertEqual(
            result["not_comparable"][0]["reason"],
            "candidate_trace_missing_but_selected",
        )

    def test_not_run_record_is_never_compared_as_a_pass(self):
        result = compare_runs(
            run({("ARM9", 7): case(status="NOT_RUN", observed=None, observed_aux=None)}),
            run({("ARM9", 7): case()}),
        )
        self.assertEqual(result["summary"]["compared_executed_vectors"], 0)
        self.assertEqual(result["not_comparable"][0]["classification"], "NOT_COMPARABLE")

    def test_different_rom_hashes_are_rejected(self):
        left = run({})
        right = run({})
        right["rom_sha256"] = "b" * 64
        with self.assertRaisesRegex(ValueError, "different ROM SHA-256"):
            compare_runs(left, right)

    def test_stale_report_identity_is_rejected(self):
        report = {
            "base_rom_sha256": "b" * 64,
            "group_schema": MANIFEST["group_schema"],
            "score_protocol": 3,
            "trace_protocol": VERSION,
        }
        with self.assertRaisesRegex(ValueError, "does not match the selected ROM"):
            validate_report_identity(
                report, report_name="stale.json", current_rom_sha256=ROM
            )

    def test_old_trace_protocol_is_rejected(self):
        report = {
            "base_rom_sha256": ROM,
            "group_schema": MANIFEST["group_schema"],
            "score_protocol": 3,
            "trace_protocol": VERSION - 1,
        }
        with self.assertRaisesRegex(ValueError, "expected 5"):
            validate_report_identity(
                report, report_name="old-trace.json", current_rom_sha256=ROM
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
