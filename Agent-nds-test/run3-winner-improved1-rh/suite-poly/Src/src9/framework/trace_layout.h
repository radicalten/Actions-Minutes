#pragma once
// Canonical numeric constants; scripts/trace_layout.py reads this same file.
// The fixed page-aligned slots are unchanged. Protocol v5 uses compact records
// to expand per-CPU capacity without overlapping the assertion journal or UI.
#define TRACE9_BASE 0x02300000
#define TRACE7_BASE 0x0236c000
#define ASSERT_BASE 0x023d8000
#define TRACE_VERSION 5
#define TRACE_SLOT_BYTES 0x6c000
#define TRACE_RECORD_BYTES 32
#define TRACE_RECORD_WORDS 8
#define TRACE_CAPACITY 13822
