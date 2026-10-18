#!/usr/bin/env python3
"""Host tests for the ARM9 POWCNT1 halfword-access build fixup."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from patch_nds_register_width import NEW, OLD, patch_file, patch_text  # noqa: E402


class RegisterWidthFixTests(unittest.TestCase):
    def test_changes_only_powcnt1_pointer_width(self) -> None:
        before = "vu32* const DISPCNT = (vu32*)0x4000000;\n" + OLD + "\n"
        after = patch_text(before)
        self.assertEqual(after, "vu32* const DISPCNT = (vu32*)0x4000000;\n" + NEW + "\n")

    def test_patch_is_idempotent(self) -> None:
        once = patch_text("prefix\n" + OLD + "\nsuffix\n")
        self.assertEqual(patch_text(once), once)

    def test_rejects_unrecognized_or_duplicate_declaration(self) -> None:
        with self.assertRaises(ValueError):
            patch_text("no POWCNT1 declaration here\n")
        with self.assertRaises(ValueError):
            patch_text(OLD + "\n" + OLD + "\n")

    def test_file_patch_is_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / "inc.h"
            header.write_text(OLD + "\n", encoding="utf-8")
            self.assertTrue(patch_file(header))
            self.assertEqual(header.read_text(encoding="utf-8"), NEW + "\n")
            self.assertFalse(patch_file(header))

    def test_build_patches_only_the_copied_header_before_compilation(self) -> None:
        build_script = (ROOT / "scripts" / "build.sh").read_text(encoding="utf-8")
        copy_index = build_script.index("cp -a vendor/rockwrestler work/rock")
        patch_index = build_script.index(
            "python3 scripts/patch_nds_register_width.py work/rock/src9/inc.h"
        )
        compile_index = build_script.index("arm-none-eabi-g++ -mcpu=arm946e-s")
        self.assertLess(copy_index, patch_index)
        self.assertLess(patch_index, compile_index)
        self.assertNotIn("patch_nds_register_width.py vendor/rockwrestler", build_script)


if __name__ == "__main__":
    unittest.main(verbosity=2)
