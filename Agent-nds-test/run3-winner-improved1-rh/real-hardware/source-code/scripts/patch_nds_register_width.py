#!/usr/bin/env python3
"""Apply project-owned hardware-width fixups to the copied upstream header.

POWCNT1 is a 16-bit ARM9 I/O register. Keep the pinned upstream checkout
untouched; patch only the private work/rock copy used for the NDS build.
"""
from __future__ import annotations

import argparse
from pathlib import Path

OLD = "vu32* const POWCNT1 = (vu32*)0x4000304;"
NEW = "vu16* const POWCNT1 = (vu16*)0x4000304;"


def patch_text(text: str) -> str:
    if NEW in text and OLD not in text:
        return text  # Idempotent for an already-patched build tree.
    count = text.count(OLD)
    if count != 1:
        raise ValueError(f"expected exactly one upstream POWCNT1 declaration; found {count}")
    return text.replace(OLD, NEW, 1)


def patch_file(path: Path) -> bool:
    original = path.read_text(encoding="utf-8")
    updated = patch_text(original)
    if updated == original:
        return False
    path.write_text(updated, encoding="utf-8")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("header", type=Path, help="copied ARM9 inc.h under work/rock")
    args = parser.parse_args()
    changed = patch_file(args.header)
    print(f"POWCNT1 access width: {'patched to 16-bit' if changed else 'already 16-bit'} ({args.header})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
