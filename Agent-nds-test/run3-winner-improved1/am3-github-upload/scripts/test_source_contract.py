#!/usr/bin/env python3
"""Fast source/ROM consistency checks; no emulator or DevkitARM is required."""
from __future__ import annotations

import json
import struct
import sys
import unittest
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from fetch_upstreams import load_lock  # noqa: E402
from trace_layout import BASES, CAPACITY, RECORD_BYTES, RECORD_WORDS, SLOT_BYTES, VERSION  # noqa: E402


class SourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.groups = json.loads((ROOT / "src/groups.json").read_text(encoding="utf-8"))
        cls.manifest = json.loads(
            (ROOT / "src/generated/debug-manifest.json").read_text(encoding="utf-8")
        )
        cls.shared = json.loads((ROOT / "src/generated/vectors.json").read_text(encoding="utf-8"))

    def test_group_registry_is_current_and_contiguous(self) -> None:
        self.assertEqual([group["id"] for group in self.groups], list(range(27)))
        self.assertEqual(
            [(g["id"], g["name"], g.get("cpu"), g.get("isa")) for g in self.groups[:4]],
            [
                (0, "ARM7", 7, "ARM"),
                (1, "ARM7Thumb", 7, "THUMB"),
                (2, "ARM9", 9, "ARM"),
                (3, "ARM9Thumb", 9, "THUMB"),
            ],
        )
        self.assertEqual(len({group["name"] for group in self.groups}), len(self.groups))

    def test_manifest_counts_and_protocols_match_generated_vectors(self) -> None:
        self.assertEqual(self.manifest["schema"], 3)
        self.assertEqual(self.manifest["group_schema"], 3)
        self.assertEqual(VERSION, 5)
        self.assertEqual(RECORD_BYTES, 32)
        self.assertEqual(RECORD_WORDS, 8)
        self.assertEqual(self.manifest["shared_count"], len(self.shared))
        self.assertEqual([row["id"] for row in self.shared], list(range(1, len(self.shared) + 1)))
        for key, count_key in (("vectors", "arm9_count"), ("arm7_vectors", "arm7_count")):
            rows = self.manifest[key]
            self.assertEqual(self.manifest[count_key], len(rows))
            self.assertEqual([row["id"] for row in rows], list(range(1, len(rows) + 1)))
            self.assertLess(len(rows), CAPACITY, f"{key} exceeds trace capacity")

    def test_expanded_trace_slots_preserve_assertion_and_restore_memory(self) -> None:
        self.assertEqual(SLOT_BYTES, 0x6C000)
        self.assertEqual(CAPACITY, 13822)
        self.assertEqual(BASES["arm9"], 0x02300000)
        self.assertEqual(BASES["arm7"], 0x0236C000)
        self.assertEqual(BASES["assertions"], 0x023D8000)
        self.assertEqual(BASES["assertions"] - (BASES["arm7"] + SLOT_BYTES), 0)
        assertion_end = BASES["assertions"] + 64 + 4096 * 20
        self.assertLess(assertion_end, 0x023ED000)
        self.assertEqual(self.manifest["arm9_count"], 13380)
        self.assertEqual(self.manifest["arm7_count"], 12056)
        self.assertEqual(CAPACITY - self.manifest["arm9_count"], 442)
        self.assertEqual(CAPACITY - self.manifest["arm7_count"], 1766)

    def test_every_vector_maps_to_one_registered_cpu_isa_group(self) -> None:
        by_id = {group["id"]: group for group in self.groups}
        for rows, cpu, expected_group_ids in (
            (self.manifest["vectors"], 9, {2, 3}),
            (self.manifest["arm7_vectors"], 7, {0, 1}),
        ):
            observed: Counter[tuple[str, int, str]] = Counter()
            for row in rows:
                group_id = row["group_id"]
                group = by_id[group_id]
                self.assertIn(group_id, expected_group_ids)
                self.assertEqual(row["cpu_group"], group["name"])
                self.assertEqual(row["isa"], group["isa"])
                self.assertEqual(group["cpu"], cpu)
                self.assertIn(row["isa"], {"ARM", "THUMB"})
                observed[(row["cpu_group"], group_id, row["isa"])] += 1
            self.assertEqual(sum(observed.values()), len(rows))
            self.assertGreater(observed[(by_id[min(expected_group_ids)]["name"], min(expected_group_ids), "ARM")], 0)
            self.assertGreater(observed[(by_id[max(expected_group_ids)]["name"], max(expected_group_ids), "THUMB")], 0)

    def test_shared_prefix_remains_identical_for_both_cpu_profiles(self) -> None:
        fields = ("id", "name", "a", "b", "initial", "result", "flags", "mask")
        arm9 = self.manifest["vectors"][: self.manifest["shared_count"]]
        arm7 = self.manifest["arm7_vectors"][: self.manifest["shared_count"]]
        self.assertEqual(len(arm9), len(arm7))
        for left, right in zip(arm9, arm7):
            self.assertEqual({key: left.get(key) for key in fields}, {key: right.get(key) for key in fields})

    def test_thumb_branch_boundary_and_sweep_matrices_are_present_for_both_profiles(self) -> None:
        for key, cpu, group_id in (("vectors", 9, 3), ("arm7_vectors", 7, 1)):
            rows = self.manifest[key]
            branch = [row for row in rows if row.get("verification_scope") == "thumb_branch"]
            sweep = [row for row in rows if row.get("verification_scope") == "thumb_branch_sweep"]
            all_branch = branch + sweep
            self.assertEqual(len(branch), 320)
            self.assertEqual(len(sweep), 1018)
            branch_ids = [row["id"] for row in all_branch]
            self.assertEqual(branch_ids, list(range(branch_ids[0], branch_ids[0] + len(all_branch))))
            immediate = [row for row in rows if row.get("verification_scope") in {
                "arm_immediate_rotate", "thumb_immediate_alu"
            }]
            self.assertTrue(immediate)
            self.assertGreater(min(row["id"] for row in immediate), branch_ids[-1])
            for subset in (branch, sweep):
                self.assertEqual({row["cpu_group"] for row in subset}, {"ARM9Thumb" if cpu == 9 else "ARM7Thumb"})
                self.assertEqual({row["group_id"] for row in subset}, {group_id})
                self.assertTrue(all(row["mask"] == 0xF0000001 for row in subset))
                self.assertTrue(all(row["architecture"] == "ARMv4T Thumb conditional B and unconditional B" for row in subset))
            self.assertEqual(len({row["name"] for row in branch}), 20)
            self.assertEqual(len({row["name"] for row in sweep}), 286)
            self.assertEqual(sum(row["operation"] == "conditional_branch" for row in branch), 256)
            self.assertEqual(sum(row["operation"] == "unconditional_branch" for row in branch), 64)
            self.assertEqual(sum(row["operation"] == "conditional_branch" for row in sweep), 506)
            self.assertEqual(sum(row["operation"] == "unconditional_branch" for row in sweep), 512)

    def test_immediate_alu_matrices_are_append_only_and_fit_v5_capacity(self) -> None:
        for key, cpu in (("vectors", 9), ("arm7_vectors", 7)):
            rows = self.manifest[key]
            arm = [row for row in rows if row.get("verification_scope") == "arm_immediate_rotate"]
            thumb = [row for row in rows if row.get("verification_scope") == "thumb_immediate_alu"]
            self.assertEqual((len(arm), len(thumb)), (528, 2048))
            self.assertEqual({row["cpu_group"] for row in arm}, {f"ARM{cpu}"})
            self.assertEqual({row["cpu_group"] for row in thumb}, {f"ARM{cpu}Thumb"})
            ids = [row["id"] for row in arm + thumb]
            self.assertEqual(ids, list(range(ids[0], ids[0] + len(ids))))
            self.assertEqual(len({row["name"] for row in arm}), 512)
            self.assertEqual(len({row["name"] for row in thumb}), 1024)
            self.assertEqual({row["rotate_field"] for row in arm}, set(range(16)))
            self.assertEqual({row["encoded_imm8"] for row in thumb}, set(range(256)))
            self.assertLessEqual(len(rows), CAPACITY)

    def test_rom_header_and_linker_regions_are_sane(self) -> None:
        rom = (ROOT / "suite.nds").read_bytes()
        self.assertGreaterEqual(len(rom), 0x160)
        for header_offset, expected_address, max_size in (
            (0x20, 0x02000100, 0x1EFF00),
            (0x30, 0x02200000, 0x100000),
        ):
            offset, entry, ram, size = struct.unpack_from("<IIII", rom, header_offset)
            self.assertEqual(entry, expected_address)
            self.assertEqual(ram, expected_address)
            self.assertGreater(size, 0)
            self.assertLess(size, max_size)
            self.assertLessEqual(offset + size, len(rom))

    def test_pinned_upstream_lock_has_required_inputs(self) -> None:
        dependencies = load_lock(ROOT / "upstream-lock.json")
        self.assertEqual({item["name"] for item in dependencies}, {"mgba-suite-auto", "rockwrestler"})
        for dependency in dependencies:
            self.assertEqual(len(dependency["revision"]), 40)
            self.assertTrue(dependency["required_paths"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
