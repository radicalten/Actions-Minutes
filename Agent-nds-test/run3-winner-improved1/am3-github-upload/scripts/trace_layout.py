"""Read the ROM's canonical trace layout; never maintain a second address map."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VALUES = {
    key: int(value, 0)
    for key, value in re.findall(
        r"^#define (\w+) (0x[0-9a-fA-F]+|[0-9]+)$",
        (ROOT / "src/trace_layout.h").read_text(encoding="utf-8"),
        re.M,
    )
}
HEADER_BYTES = 64
RECORD_BYTES = VALUES["TRACE_RECORD_BYTES"]
RECORD_WORDS = VALUES["TRACE_RECORD_WORDS"]
LEGACY_RECORD_FORMATS = {4: (40, 10)}
ASSERTION_CAPACITY = 4096
ASSERTION_RECORD_BYTES = 20
RESTORE_GATE = 0x023ED000

CAPACITY = VALUES["TRACE_CAPACITY"]
VERSION = VALUES["TRACE_VERSION"]
SLOT_BYTES = VALUES["TRACE_SLOT_BYTES"]
BASES = {
    "arm9": VALUES["TRACE9_BASE"],
    "arm7": VALUES["TRACE7_BASE"],
    "assertions": VALUES["ASSERT_BASE"],
}

assert RECORD_WORDS * 4 == RECORD_BYTES
assert CAPACITY == (SLOT_BYTES - HEADER_BYTES) // RECORD_BYTES
assert HEADER_BYTES + CAPACITY * RECORD_BYTES == SLOT_BYTES
assert SLOT_BYTES % 0x1000 == 0
assert all(base % 0x1000 == 0 for base in BASES.values())
assert BASES["arm9"] + SLOT_BYTES <= BASES["arm7"]
assert BASES["arm7"] + SLOT_BYTES <= BASES["assertions"]
assert BASES["assertions"] + HEADER_BYTES + ASSERTION_CAPACITY * ASSERTION_RECORD_BYTES < RESTORE_GATE
