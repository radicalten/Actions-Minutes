#!/usr/bin/env python3
"""Offline regression tests for pinned upstream fetching and safety checks."""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from fetch_upstreams import FetchError, check_dependency, fetch_dependency, load_lock


def git(*args: str, cwd: Path | None = None) -> str:
    result = subprocess.run(
        ["git", *args], cwd=cwd, check=True, text=True, capture_output=True
    )
    return result.stdout.strip()


class UpstreamFetchTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.vendor = self.root / "vendor"
        self.source.mkdir()
        git("init", "--quiet", str(self.source))
        git("-C", str(self.source), "config", "user.name", "Fixture")
        git("-C", str(self.source), "config", "user.email", "fixture@example.invalid")
        (self.source / "required.txt").write_text("first revision\n", encoding="utf-8")
        git("-C", str(self.source), "add", "required.txt")
        git("-C", str(self.source), "commit", "--quiet", "-m", "first")
        self.revision_one = git("-C", str(self.source), "rev-parse", "HEAD")
        (self.source / "required.txt").write_text("second revision\n", encoding="utf-8")
        git("-C", str(self.source), "commit", "--quiet", "-am", "second")
        self.revision_two = git("-C", str(self.source), "rev-parse", "HEAD")
        self.dependency = {
            "name": "fixture",
            "url": str(self.source),
            "revision": self.revision_one,
            "required_paths": ["required.txt"],
        }

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_fetch_is_exact_and_idempotent(self) -> None:
        self.assertTrue(fetch_dependency(self.dependency, self.vendor))
        checkout = self.vendor / "fixture"
        self.assertEqual(git("-C", str(checkout), "rev-parse", "HEAD"), self.revision_one)
        self.assertEqual((checkout / "required.txt").read_text(encoding="utf-8"), "first revision\n")
        check_dependency(self.dependency, self.vendor)
        self.assertFalse(fetch_dependency(self.dependency, self.vendor))

    def test_wrong_revision_and_dirty_checkout_are_rejected(self) -> None:
        fetch_dependency(self.dependency, self.vendor)
        checkout = self.vendor / "fixture"
        git("-C", str(checkout), "fetch", "--quiet", "--depth", "1", "origin", self.revision_two)
        git("-C", str(checkout), "checkout", "--quiet", "--detach", self.revision_two)
        with self.assertRaisesRegex(FetchError, "wrong revision"):
            check_dependency(self.dependency, self.vendor)
        with self.assertRaisesRegex(FetchError, "wrong revision"):
            fetch_dependency(self.dependency, self.vendor)

        git("-C", str(checkout), "checkout", "--quiet", "--detach", self.revision_one)
        (checkout / "required.txt").write_text("local change\n", encoding="utf-8")
        with self.assertRaisesRegex(FetchError, "local changes"):
            check_dependency(self.dependency, self.vendor)

    def test_missing_required_file_leaves_no_partial_checkout(self) -> None:
        broken = dict(self.dependency, required_paths=["not-present.txt"])
        with self.assertRaisesRegex(FetchError, "required upstream path is missing"):
            fetch_dependency(broken, self.vendor)
        self.assertFalse((self.vendor / "fixture").exists())
        self.assertEqual(list(self.vendor.iterdir()), [])

    def test_lock_rejects_path_escape(self) -> None:
        path = self.root / "bad-lock.json"
        path.write_text(
            json.dumps(
                {
                    "schema": 1,
                    "dependencies": [
                        dict(self.dependency, required_paths=["../outside.txt"])
                    ],
                }
            ),
            encoding="utf-8",
        )
        with self.assertRaisesRegex(FetchError, "Unsafe"):
            load_lock(path)

    def test_remote_identity_is_checked(self) -> None:
        fetch_dependency(self.dependency, self.vendor)
        wrong = dict(self.dependency, url=str(self.root / "different-origin"))
        with self.assertRaisesRegex(FetchError, "origin"):
            check_dependency(wrong, self.vendor)


if __name__ == "__main__":
    unittest.main(verbosity=2)
