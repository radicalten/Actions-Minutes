#!/usr/bin/env python3
"""Compare two trace-backed NDS emulator runs of the same current suite ROM.

This is a differential debugger, not an oracle: it reports behavioral deltas
without deciding which emulator or registered expectation is architecturally
correct. QEMU aggregate reports without per-vector trace records are rejected.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any

from diagnostics import MANIFEST, checked_blob, cpu_rows, validate_cpu
from trace_layout import VERSION

ROOT = Path(__file__).resolve().parents[1]
SCORE_PROTOCOL = 3
GROUP_SCHEMA = int(MANIFEST["group_schema"])
EXECUTED = {"PASS", "FAIL", "TRACE_INCONSISTENT"}
DYNAMIC_FIELDS = {
    "status", "observed", "observed_aux", "function_pc", "historical",
    "result_xor", "aux_masked_xor", "primary_pass", "aux_pass",
    "runtime_input_fingerprint", "runtime_input_fingerprint_matches",
    "runtime_differs_manifest",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _project_path(value: str | Path) -> Path:
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


def validate_report_identity(
    report: dict[str, Any], *, report_name: str, current_rom_sha256: str
) -> None:
    """Fail closed on stale ROMs, incompatible schemas, or non-current trace data."""
    if report.get("base_rom_sha256") != current_rom_sha256:
        actual = report.get("base_rom_sha256", "MISSING")
        raise ValueError(
            f"{report_name}: base_rom_sha256 {actual} does not match the selected ROM "
            f"({current_rom_sha256}); rerun this report against that ROM"
        )
    expected = {
        "group_schema": GROUP_SCHEMA,
        "score_protocol": SCORE_PROTOCOL,
        "trace_protocol": VERSION,
    }
    for key, value in expected.items():
        if report.get(key) != value:
            raise ValueError(
                f"{report_name}: {key} is {report.get(key)!r}, expected {value}; "
                "this comparator accepts only current-schema trace reports"
            )


def _diagnostic_batches(report: dict[str, Any]) -> list[tuple[str, dict[str, Any]]]:
    top = report.get("diagnostics")
    if isinstance(top, dict) and any(cpu in top for cpu in ("arm7", "arm9")):
        return [("top-level", top)]

    batches: list[tuple[str, dict[str, Any]]] = []
    results = report.get("results")
    if isinstance(results, list):
        for index, item in enumerate(results):
            if not isinstance(item, dict):
                continue
            nested = item.get("diagnostics")
            if isinstance(nested, dict) and any(cpu in nested for cpu in ("arm7", "arm9")):
                name = item.get("name", item.get("id", index))
                batches.append((f"result:{name}", nested))
    if not batches:
        raise ValueError(
            "report has no CPU trace diagnostics; use trace-backed DeSmuME/compatible "
            "reports, not QEMU aggregate summaries"
        )
    return batches


def _same_static_vector(left: dict[str, Any], right: dict[str, Any]) -> bool:
    left_static = {key: value for key, value in left.items() if key not in DYNAMIC_FIELDS}
    right_static = {key: value for key, value in right.items() if key not in DYNAMIC_FIELDS}
    return left_static == right_static


def _same_observation(left: dict[str, Any], right: dict[str, Any]) -> bool:
    return all(
        left.get(field) == right.get(field)
        for field in ("status", "observed", "observed_aux", "function_pc")
    )


def _row_key(row: dict[str, Any]) -> tuple[str, int]:
    return str(row["cpu"]), int(row["id"])


def _cpu_name(lower_name: str, header_cpu: int) -> str:
    expected = {"arm7": ("ARM7", 7), "arm9": ("ARM9", 9)}[lower_name]
    if header_cpu != expected[1]:
        raise ValueError(f"{lower_name} trace header identifies CPU {header_cpu}")
    return expected[0]


def load_run(
    report_path: str | Path,
    *,
    rom_path: str | Path,
    label: str | None = None,
) -> dict[str, Any]:
    """Load, verify, and flatten selected CPU rows from a current-schema report."""
    source = _project_path(report_path).resolve()
    rom = _project_path(rom_path).resolve()
    try:
        report = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read JSON report {source}: {exc}") from exc
    if not isinstance(report, dict):
        raise ValueError(f"{source}: report root must be a JSON object")

    try:
        current_rom_sha256 = sha256_file(rom)
    except OSError as exc:
        raise ValueError(f"cannot read selected ROM {rom}: {exc}") from exc
    validate_report_identity(
        report, report_name=str(source), current_rom_sha256=current_rom_sha256
    )

    rows: dict[tuple[str, int], dict[str, Any]] = {}
    sources: dict[tuple[str, int], list[str]] = {}
    selected_isa = {"ARM7": 0, "ARM9": 0}
    trace_sha256: dict[str, set[str]] = {"ARM7": set(), "ARM9": set()}
    batch_count = 0

    for batch_name, batch in _diagnostic_batches(report):
        batch_count += 1
        for cpu_key in ("arm7", "arm9"):
            entry = batch.get(cpu_key)
            if entry is None:
                continue
            try:
                blob = checked_blob(entry)
                words = validate_cpu(blob)
            except (KeyError, OSError, ValueError) as exc:
                raise ValueError(f"{source} {batch_name} {cpu_key} trace: {exc}") from exc
            header = list(words[:16])
            if header[1] != VERSION:
                raise ValueError(
                    f"{source} {batch_name} {cpu_key}: trace blob is historical v{header[1]}, "
                    f"not current v{VERSION}"
                )
            if entry.get("header") != header:
                raise ValueError(f"{source} {batch_name} {cpu_key}: JSON header differs from trace blob")

            cpu = _cpu_name(cpu_key, int(header[2]))
            selected_isa[cpu] |= int(header[15])
            trace_sha256[cpu].add(str(entry.get("sha256", "")))
            try:
                parsed_rows = cpu_rows(entry)
            except (KeyError, OSError, ValueError) as exc:
                raise ValueError(f"{source} {batch_name} {cpu_key} rows: {exc}") from exc

            for row in parsed_rows:
                if row.get("status") == "NOT_SELECTED":
                    continue
                key = _row_key(row)
                old = rows.get(key)
                if old is not None:
                    if not _same_static_vector(old, row):
                        raise ValueError(f"{source}: conflicting manifest metadata for {key[0]}:{key[1]}")
                    if not _same_observation(old, row):
                        raise ValueError(
                            f"{source}: overlapping trace batches disagree for {key[0]}:{key[1]}; "
                            "compare one full run or matching CPU/ISA selections"
                        )
                    if old.get("historical") and not row.get("historical"):
                        rows[key] = row
                    sources[key].append(batch_name)
                else:
                    rows[key] = row
                    sources[key] = [batch_name]

    if not rows:
        raise ValueError(f"{source}: no selected per-vector CPU records found")

    return {
        "label": label or report.get("backend") or source.stem,
        "backend": report.get("backend", "unspecified compatible runner"),
        "report_path": str(source),
        "report_sha256": sha256_file(source),
        "rom_path": str(rom),
        "rom_sha256": current_rom_sha256,
        "trace_protocol": VERSION,
        "group_schema": GROUP_SCHEMA,
        "score_protocol": SCORE_PROTOCOL,
        "manifest_crc32": int(MANIFEST["crc32"]),
        "score": report.get("score"),
        "selected_isa": selected_isa,
        "trace_sha256": {cpu: sorted(hashes) for cpu, hashes in trace_sha256.items()},
        "rows": rows,
        "row_sources": sources,
        "diagnostic_batches": batch_count,
    }


def _isa_bit(row: dict[str, Any]) -> int:
    return 2 if str(row.get("isa", "")).upper() == "THUMB" else 1


def _planned_in_run(run: dict[str, Any], row: dict[str, Any]) -> bool:
    return bool(run["selected_isa"].get(str(row["cpu"]), 0) & _isa_bit(row))


def _matches_registered_expectation(row: dict[str, Any]) -> bool | None:
    if row.get("status") not in EXECUTED:
        return None
    observed = row.get("observed")
    observed_aux = row.get("observed_aux")
    if observed is None or observed_aux is None:
        return None
    return (
        int(observed) == int(row["result"])
        and (((int(observed_aux) ^ int(row["flags"])) & int(row["mask"])) == 0)
    )


def _observation_view(row: dict[str, Any] | None, label: str) -> dict[str, Any] | None:
    if row is None:
        return None
    return {
        "label": label,
        "status": row.get("status"),
        "observed": row.get("observed"),
        "observed_aux": row.get("observed_aux"),
        "function_pc": row.get("function_pc"),
        "historical": bool(row.get("historical")),
        "matches_registered_expectation": _matches_registered_expectation(row),
    }


def _case_context(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "cpu": row.get("cpu"),
        "id": row.get("id"),
        "name": row.get("name"),
        "isa": row.get("isa"),
        "cpu_group": row.get("cpu_group"),
        "group_id": row.get("group_id"),
        "architecture": row.get("architecture"),
        "verification_scope": row.get("verification_scope"),
        "source": row.get("source"),
        "assembly_source": row.get("assembly_source"),
        "inputs": {
            "a": row.get("a"),
            "b": row.get("b"),
            "initial": row.get("initial"),
        },
        "expected": {
            "result": row.get("result"),
            "aux": row.get("flags"),
            "aux_mask": row.get("mask"),
        },
    }


def _classify_pair(reference: dict[str, Any], candidate: dict[str, Any]) -> dict[str, Any]:
    if not _same_static_vector(reference, candidate):
        raise ValueError(
            f"incompatible manifest metadata for {reference.get('cpu')}:{reference.get('id')}"
        )
    if reference.get("status") not in EXECUTED or candidate.get("status") not in EXECUTED:
        raise ValueError("_classify_pair requires two executed observations")

    primary_changed = reference.get("observed") != candidate.get("observed")
    reference_aux = reference.get("observed_aux")
    candidate_aux = candidate.get("observed_aux")
    raw_aux_changed = reference_aux != candidate_aux
    mask = int(reference.get("mask", 0))
    masked_aux_changed = (
        reference_aux is not None
        and candidate_aux is not None
        and (((int(reference_aux) ^ int(candidate_aux)) & mask) != 0)
    )
    status_changed = reference.get("status") != candidate.get("status")
    provenance_changed = bool(reference.get("historical")) != bool(candidate.get("historical"))

    if status_changed:
        if reference.get("status") == "PASS" and candidate.get("status") == "FAIL":
            classification = "CANDIDATE_FAILS_WHERE_REFERENCE_PASSES"
        elif reference.get("status") == "FAIL" and candidate.get("status") == "PASS":
            classification = "CANDIDATE_PASSES_WHERE_REFERENCE_FAILS"
        else:
            classification = "STATUS_CHANGED"
    elif primary_changed or masked_aux_changed:
        classification = "CHECKED_OUTPUT_DIVERGENCE"
    elif raw_aux_changed:
        classification = "UNMASKED_AUXILIARY_DIVERGENCE"
    elif provenance_changed:
        classification = "PROVENANCE_ONLY"
    else:
        classification = "UNCHANGED"

    return {
        "classification": classification,
        "status_changed": status_changed,
        "primary_changed": primary_changed,
        "auxiliary_raw_changed": raw_aux_changed,
        "auxiliary_masked_changed": masked_aux_changed,
        "provenance_changed": provenance_changed,
        "reference_matches_registered_expectation": _matches_registered_expectation(reference),
        "candidate_matches_registered_expectation": _matches_registered_expectation(candidate),
    }


def _coverage_summary(run: dict[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for cpu in ("ARM7", "ARM9"):
        manifest_rows = MANIFEST["arm7_vectors"] if cpu == "ARM7" else MANIFEST["vectors"]
        plan = int(run["selected_isa"].get(cpu, 0))
        planned_rows = [
            row for row in manifest_rows
            if plan & _isa_bit(row)
        ]
        rows = [row for (row_cpu, _), row in run["rows"].items() if row_cpu == cpu]
        counts = Counter(row.get("status", "UNKNOWN") for row in rows)
        groups: dict[str, dict[str, int]] = {}
        for row in rows:
            group = str(row.get("cpu_group", "UNKNOWN"))
            bucket = groups.setdefault(group, {"selected": 0, "PASS": 0, "FAIL": 0,
                                                "NOT_RUN": 0, "INTERRUPTED": 0,
                                                "TRACE_INCONSISTENT": 0})
            bucket["selected"] += 1
            status = str(row.get("status", "UNKNOWN"))
            bucket[status] = bucket.get(status, 0) + 1
        result[cpu] = {
            "isa_plan": [name for bit, name in ((1, "ARM"), (2, "THUMB")) if plan & bit],
            "planned": len(planned_rows),
            "selected_records": len(rows),
            "missing_trace_records": max(0, len(planned_rows) - len(rows)),
            "passed": counts["PASS"],
            "failed": counts["FAIL"],
            "not_run": counts["NOT_RUN"],
            "interrupted": counts["INTERRUPTED"],
            "trace_inconsistent": counts["TRACE_INCONSISTENT"],
            "by_cpu_group": groups,
        }
    return result


def _non_comparable(
    key: tuple[str, int],
    reference: dict[str, Any] | None,
    candidate: dict[str, Any] | None,
    reference_run: dict[str, Any],
    candidate_run: dict[str, Any],
) -> dict[str, Any]:
    row = reference or candidate
    assert row is not None
    ref_planned = _planned_in_run(reference_run, row)
    cand_planned = _planned_in_run(candidate_run, row)
    if reference is None:
        reason = "reference_trace_missing_but_selected" if ref_planned else "not_selected_in_reference"
    elif candidate is None:
        reason = "candidate_trace_missing_but_selected" if cand_planned else "not_selected_in_candidate"
    else:
        ref_state = str(reference.get("status"))
        cand_state = str(candidate.get("status"))
        reason = f"not_executed:{ref_state}/{cand_state}"
    return {
        **_case_context(row),
        "classification": "NOT_COMPARABLE",
        "reason": reason,
        "reference": _observation_view(reference, reference_run["label"]),
        "candidate": _observation_view(candidate, candidate_run["label"]),
    }


def compare_runs(reference_run: dict[str, Any], candidate_run: dict[str, Any]) -> dict[str, Any]:
    if reference_run["rom_sha256"] != candidate_run["rom_sha256"]:
        raise ValueError("cannot compare reports with different ROM SHA-256 values")
    if reference_run["manifest_crc32"] != candidate_run["manifest_crc32"]:
        raise ValueError("cannot compare reports with different manifest CRCs")

    reference_rows = reference_run["rows"]
    candidate_rows = candidate_run["rows"]
    differences: list[dict[str, Any]] = []
    non_comparable: list[dict[str, Any]] = []
    classifications: Counter[str] = Counter()
    compared = 0

    for key in sorted(set(reference_rows) | set(candidate_rows)):
        reference = reference_rows.get(key)
        candidate = candidate_rows.get(key)
        if reference is None or candidate is None:
            non_comparable.append(
                _non_comparable(key, reference, candidate, reference_run, candidate_run)
            )
            classifications["NOT_COMPARABLE"] += 1
            continue
        if reference.get("status") not in EXECUTED or candidate.get("status") not in EXECUTED:
            non_comparable.append(
                _non_comparable(key, reference, candidate, reference_run, candidate_run)
            )
            classifications["NOT_COMPARABLE"] += 1
            continue

        compared += 1
        delta = _classify_pair(reference, candidate)
        classification = delta["classification"]
        classifications[classification] += 1
        if classification == "UNCHANGED":
            continue
        differences.append({
            **_case_context(reference),
            "classification": classification,
            "delta": delta,
            "reference": _observation_view(reference, reference_run["label"]),
            "candidate": _observation_view(candidate, candidate_run["label"]),
        })

    behavioral_differences = sum(
        classification not in ("UNCHANGED", "PROVENANCE_ONLY")
        for classification in [row["classification"] for row in differences]
    )
    return {
        "schema": 1,
        "evidence_kind": "trace-backed emulator differential; not an architectural oracle",
        "rom_sha256": reference_run["rom_sha256"],
        "manifest_crc32": reference_run["manifest_crc32"],
        "trace_protocol": VERSION,
        "reference": {key: value for key, value in reference_run.items() if key != "rows" and key != "row_sources"},
        "candidate": {key: value for key, value in candidate_run.items() if key != "rows" and key != "row_sources"},
        "coverage": {
            "reference": _coverage_summary(reference_run),
            "candidate": _coverage_summary(candidate_run),
        },
        "summary": {
            "reference_selected_records": sum(v["selected_records"] for v in _coverage_summary(reference_run).values()),
            "candidate_selected_records": sum(v["selected_records"] for v in _coverage_summary(candidate_run).values()),
            "compared_executed_vectors": compared,
            "behavioral_differences": behavioral_differences,
            "provenance_only_vectors": classifications["PROVENANCE_ONLY"],
            "records_with_any_difference": len(differences),
            "not_comparable_vectors": len(non_comparable),
            "classifications": dict(sorted(classifications.items())),
            "candidate_fails_where_reference_passes": classifications["CANDIDATE_FAILS_WHERE_REFERENCE_PASSES"],
            "candidate_passes_where_reference_fails": classifications["CANDIDATE_PASSES_WHERE_REFERENCE_FAILS"],
        },
        "differences": differences,
        "not_comparable": non_comparable,
        "interpretation": (
            "A candidate PASS/failure transition is only relative to the other run and the "
            "registered expectation. It does not adjudicate architectural correctness. "
            "Check source scope, CPU/ISA, trace provenance, an independent reference, and "
            "hardware evidence separately."
        ),
    }


def _hex(value: Any) -> str:
    return "—" if value is None else f"0x{int(value) & 0xffffffff:08X}"


def _md(value: Any) -> str:
    return str(value if value is not None else "—").replace("|", "\\|").replace("\n", " ")


def render_markdown(result: dict[str, Any], *, limit: int = 100) -> str:
    ref = result["reference"]
    cand = result["candidate"]
    summary = result["summary"]
    lines = [
        "# Emulator run comparison",
        "",
        f"- ROM SHA-256: `{result['rom_sha256']}`",
        f"- Trace protocol: v{result['trace_protocol']} · manifest CRC32: `0x{result['manifest_crc32']:08x}`",
        f"- Reference: **{_md(ref['label'])}** ({_md(ref['backend'])})",
        f"- Candidate: **{_md(cand['label'])}** ({_md(cand['backend'])})",
        "",
        "> Differential observations only—not an architectural oracle. A PASS transition does not prove correctness.",
        "",
        "## Summary",
        "",
        f"- Executed vectors compared: **{summary['compared_executed_vectors']}**",
        f"- Behavioral output/status differences: **{summary['behavioral_differences']}**",
        f"- Provenance-only changes (no output/status change): **{summary['provenance_only_vectors']}**",
        f"- Not comparable (unselected, missing, or not executed): **{summary['not_comparable_vectors']}**",
        f"- Candidate FAIL where reference PASS: **{summary['candidate_fails_where_reference_passes']}**",
        f"- Candidate PASS where reference FAIL: **{summary['candidate_passes_where_reference_fails']}**",
        "",
        "## Selected coverage by CPU/ISA group",
        "",
        "| Group | Reference: selected / PASS / FAIL / NOT_RUN | Candidate: selected / PASS / FAIL / NOT_RUN |",
        "|---|---:|---:|",
    ]
    ref_groups: dict[str, Any] = {}
    cand_groups: dict[str, Any] = {}
    for cpu in ("ARM7", "ARM9"):
        ref_groups.update(result["coverage"]["reference"][cpu]["by_cpu_group"])
        cand_groups.update(result["coverage"]["candidate"][cpu]["by_cpu_group"])
    for group in sorted(set(ref_groups) | set(cand_groups)):
        def values(table: dict[str, Any]) -> str:
            data = table.get(group, {})
            return f"{data.get('selected', 0)} / {data.get('PASS', 0)} / {data.get('FAIL', 0)} / {data.get('NOT_RUN', 0)}"
        lines.append(f"| {group} | {values(ref_groups)} | {values(cand_groups)} |")

    lines.extend(["", "## Divergent executed vectors", ""])
    if not result["differences"]:
        lines.append("No executed-vector differences were recorded.")
    else:
        lines.extend([
            "| CPU | ID | Group / family | Classification | Status (reference → candidate) | Primary observed (reference → candidate) | Source |",
            "|---|---:|---|---|---|---|---|",
        ])
        for row in result["differences"][:limit]:
            left = row.get("reference") or {}
            right = row.get("candidate") or {}
            lines.append(
                f"| {row['cpu']} | {row['id']} | {_md(row.get('cpu_group'))} / {_md(row.get('name'))} "
                f"| {row['classification']} | {left.get('status', '—')} → {right.get('status', '—')} "
                f"| {_hex(left.get('observed'))} → {_hex(right.get('observed'))} "
                f"| {_md(row.get('source'))} |"
            )
        if len(result["differences"]) > limit:
            lines.append("")
            lines.append(
                f"Showing the first {limit} of {len(result['differences'])} differences; "
                "the JSON report contains the complete list."
            )

    lines.extend([
        "",
        "## How to use this result",
        "",
        "Use each `(CPU, ID)` to inspect the input tuple, registered expected values, comparison mask, helper PC, and source in the JSON. Keep ARM7, ARM9, ARM, and Thumb evidence separate. QEMU model/proxy results and physical-hardware observations are different evidence classes.",
        "",
        result["interpretation"],
        "",
    ])
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", help="trace-backed JSON report for the reference run")
    parser.add_argument("candidate", help="trace-backed JSON report for the candidate run")
    parser.add_argument("--rom", default=str(ROOT / "suite.nds"), help="ROM both reports must match")
    parser.add_argument("--reference-label", help="human-readable reference run name")
    parser.add_argument("--candidate-label", help="human-readable candidate run name")
    parser.add_argument("--output", default=str(ROOT / "reports/emulator-diff.json"), help="JSON output")
    parser.add_argument("--markdown", help="Markdown output (default: output path with .md suffix)")
    parser.add_argument("--no-markdown", action="store_true", help="write JSON only")
    parser.add_argument("--markdown-limit", type=int, default=100, help="max differences shown in Markdown")
    args = parser.parse_args(argv)
    if args.markdown_limit < 0:
        parser.error("--markdown-limit must be non-negative")

    try:
        reference = load_run(args.reference, rom_path=args.rom, label=args.reference_label)
        candidate = load_run(args.candidate, rom_path=args.rom, label=args.candidate_label)
        result = compare_runs(reference, candidate)
        output = _project_path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        if not args.no_markdown:
            markdown = _project_path(args.markdown) if args.markdown else output.with_suffix(".md")
            markdown.parent.mkdir(parents=True, exist_ok=True)
            markdown.write_text(render_markdown(result, limit=args.markdown_limit), encoding="utf-8")
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    print(json.dumps({"output": str(output), "summary": result["summary"]}, indent=2))
    if not args.no_markdown:
        print(f"Markdown: {markdown}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
