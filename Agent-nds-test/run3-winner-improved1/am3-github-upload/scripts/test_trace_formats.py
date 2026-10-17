#!/usr/bin/env python3
"""Synthetic v4/v5 trace-format tests, including read-only legacy behavior."""
from __future__ import annotations

import hashlib
import struct
import tempfile
from pathlib import Path

from diagnostics import MANIFEST, cpu_rows, fingerprint_inputs, restore, validate_cpu
from trace_layout import LEGACY_RECORD_FORMATS, RECORD_BYTES, RECORD_WORDS, VERSION

MAGIC = 0x43545235


def fixture(version: int) -> tuple[bytes, dict]:
    vectors = MANIFEST["vectors"]
    count = len(vectors)
    vector = vectors[0]
    if version == 5:
        record_bytes, record_words = RECORD_BYTES, RECORD_WORDS
    else:
        record_bytes, record_words = LEGACY_RECORD_FORMATS[version]
    header = [0] * 16
    header[0:4] = [MAGIC, version, 9, count]
    header[4:7] = [1, 1, 0]  # one completed PASS record
    header[9] = record_bytes
    header[10] = MANIFEST["crc32"]
    header[14:16] = [count, 3]  # all vectors, both ARM and Thumb
    words = header + [0] * (record_words * count)
    pc = 0x02001000
    if version == 4:
        record = [
            2,
            vector["result"],
            vector["flags"],
            pc,
            vector["a"],
            vector["b"],
            vector["initial"],
            vector["result"],
            vector["flags"],
            vector["mask"],
        ]
    else:
        record = [
            2,
            vector["result"],
            vector["flags"],
            pc,
            vector["result"],
            vector["flags"],
            vector["mask"],
            fingerprint_inputs(vector["id"], vector["a"], vector["b"], vector["initial"]),
        ]
    words[16 : 16 + record_words] = record
    data = struct.pack("<" + "I" * len(words), *words)
    return data, vector


def entry_for(path: Path, data: bytes) -> dict:
    path.write_bytes(data)
    return {"path": str(path), "sha256": hashlib.sha256(data).hexdigest()}


def main() -> int:
    assert VERSION == 5 and RECORD_BYTES == 32 and RECORD_WORDS == 8
    with tempfile.TemporaryDirectory(prefix="trace-format-") as directory:
        root = Path(directory)
        for version in (4, 5):
            data, vector = fixture(version)
            words = validate_cpu(data)
            assert words[1] == version
            saved = entry_for(root / f"v{version}.bin", data)
            rows = cpu_rows(saved)
            first = rows[0]
            assert first["id"] == vector["id"] and first["status"] == "PASS"
            assert first["observed"] == vector["result"]
            assert first["function_pc"] == 0x02001000
            if version == 4:
                assert first["runtime_differs_manifest"] is False
                assert first["runtime_input_fingerprint_matches"] is None
            else:
                assert first["runtime_differs_manifest"] is False
                assert first["runtime_input_fingerprint_matches"] is True
                assert first["runtime_input_fingerprint"] == fingerprint_inputs(
                    vector["id"], vector["a"], vector["b"], vector["initial"]
                )

        old_data, _ = fixture(4)
        old_entry = entry_for(root / "legacy.bin", old_data)

        class NeverWrite:
            def __getattr__(self, name: str):
                raise AssertionError(f"Legacy restore touched emulator memory: {name}")

        try:
            restore(NeverWrite(), {"arm9": old_entry})
        except ValueError as exc:
            assert "read-only" in str(exc)
        else:
            raise AssertionError("v4 trace was restored into a v5 writer")

    print("PASS: v5 compact records, v4 historical reads, and v4 restore rejection")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
