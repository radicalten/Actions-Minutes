#!/usr/bin/env python3
"""Read-only coverage-first release gate. JSON stdout; exit 0 READY, 2 BLOCKED, 3 ERROR."""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
IMPLEMENTED = {"IMPLEMENTED_DEFINED_MATRIX", "IMPLEMENTED_AND_VALIDATED"}
EXPECTED_PROTOCOLS = {"group_schema": 3, "score_protocol": 3, "trace_protocol": 5}
REQUIRED_MILESTONES = {"verified_standalone_folder"}


class GateInputError(ValueError):
    """An input exists but is malformed or cannot be trusted as gate evidence."""


def _resolve(root: Path, value: str | Path) -> Path:
    path = Path(value)
    return path if path.is_absolute() else root / path


def _display(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path)


def _load_json(
    path: Path,
    *,
    area: str,
    missing_status: str,
    scope: str,
    blockers: list[dict[str, str]],
) -> Any | None:
    if not path.is_file():
        blockers.append({"area": area, "status": missing_status, "scope": f"{scope} ({path})"})
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise GateInputError(f"Malformed JSON in {path}: {exc}") from exc
    except OSError as exc:
        raise GateInputError(f"Cannot read {path}: {exc}") from exc


def _need_object(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise GateInputError(f"{label} must be a JSON object")
    return value


def evaluate(
    root: Path,
    *,
    report: str | Path = "reports/development.json",
    validation: str | Path = "reports/development-validation.json",
    rom: str | Path = "suite.nds",
) -> tuple[dict[str, Any], int]:
    """Evaluate repository inputs without changing approval or milestone state."""
    blockers: list[dict[str, str]] = []
    ledger_path = root / "docs/remaining-cases.json"
    coverage_path = root / "coverage.json"
    plan_path = root / "release/v1.0-plan.json"
    report_path = _resolve(root, report)
    validation_path = _resolve(root, validation)
    rom_path = _resolve(root, rom)

    ledger = _load_json(
        ledger_path,
        area="remaining_case_ledger",
        missing_status="MISSING_INPUT",
        scope="Coverage ledger is required",
        blockers=blockers,
    )
    coverage = _load_json(
        coverage_path,
        area="coverage_map",
        missing_status="MISSING_INPUT",
        scope="CPU uncovered-work map is required",
        blockers=blockers,
    )
    plan = _load_json(
        plan_path,
        area="release_plan",
        missing_status="MISSING_INPUT",
        scope="v1.0 plan and approval evidence are required",
        blockers=blockers,
    )
    evidence = _load_json(
        report_path,
        area="evidence",
        missing_status="MISSING_REPORT",
        scope="Current-ROM development report is required",
        blockers=blockers,
    )
    validation_evidence = _load_json(
        validation_path,
        area="release_regression",
        missing_status="NOT_COMPLETE",
        scope="Full current-ROM release regression/package refresh is required",
        blockers=blockers,
    )

    if ledger is not None:
        ledger = _need_object(ledger, "remaining-cases ledger")
        items = ledger.get("items")
        if not isinstance(items, list):
            raise GateInputError("remaining-cases ledger must contain an items array")
        for item in items:
            if not isinstance(item, dict) or not isinstance(item.get("area"), str):
                raise GateInputError("Each remaining-cases item must have a string area")
            status = item.get("status")
            if not isinstance(status, str) or status not in IMPLEMENTED:
                blockers.append(
                    {
                        "area": item["area"],
                        "status": str(status or "UNSPECIFIED"),
                        "scope": str(item.get("scope", "")),
                    }
                )

    if coverage is not None:
        coverage = _need_object(coverage, "coverage map")
        cpu = coverage.get("cpu")
        if not isinstance(cpu, dict) or not isinstance(cpu.get("uncovered"), list):
            raise GateInputError("coverage map must contain cpu.uncovered as an array")
        for gap in cpu["uncovered"]:
            blockers.append({"area": "cpu_uncovered", "status": "OPEN", "scope": str(gap)})

    if plan is not None:
        plan = _need_object(plan, "release plan")
        milestones = plan.get("milestones")
        if not isinstance(milestones, list):
            raise GateInputError("release plan must contain a milestones array")
        if not milestones:
            blockers.append({"area": "v1_milestones", "status": "MISSING"})
        milestone_ids: set[str] = set()
        for milestone in milestones:
            if not isinstance(milestone, dict) or not isinstance(milestone.get("id"), str):
                raise GateInputError("Each release milestone must have a string id")
            milestone_id = milestone["id"]
            if milestone_id in milestone_ids:
                raise GateInputError(f"Duplicate release milestone id: {milestone_id}")
            milestone_ids.add(milestone_id)
            if milestone.get("status") != "PASS":
                blockers.append(
                    {
                        "area": f"v1_{milestone_id}",
                        "status": str(milestone.get("status") or "UNSPECIFIED"),
                    }
                )
        for missing_id in sorted(REQUIRED_MILESTONES - milestone_ids):
            blockers.append({"area": f"v1_{missing_id}", "status": "MISSING"})

    license_path = root / "LICENSE"
    if plan is None or plan.get("license_approved") is not True or not license_path.is_file():
        blockers.append(
            {
                "area": "original_code_license",
                "status": "NOT_APPROVED",
                "scope": "Owner approval and a root LICENSE are both required; see docs/v1.0-release.md",
            }
        )
    if plan is None or plan.get("redistribution_cleared") is not True:
        blockers.append(
            {
                "area": "upstream_redistribution",
                "status": "BLOCKED",
                "scope": "Pinned upstream source/assets/ROM redistribution permission is unresolved",
            }
        )

    rom_sha256: str | None = None
    if not rom_path.is_file():
        blockers.append(
            {
                "area": "base_rom",
                "status": "MISSING_ROM",
                "scope": f"ROM required to bind validation evidence: {rom_path}",
            }
        )
    else:
        try:
            rom_sha256 = hashlib.sha256(rom_path.read_bytes()).hexdigest()
        except OSError as exc:
            raise GateInputError(f"Cannot read ROM {rom_path}: {exc}") from exc

    if evidence is not None:
        evidence = _need_object(evidence, "development report")
        stale = [
            key for key, expected in EXPECTED_PROTOCOLS.items() if evidence.get(key) != expected
        ]
        if stale:
            blockers.append(
                {
                    "area": "protocol",
                    "status": "STALE_SCHEMA",
                    "scope": "Expected " + ", ".join(f"{key}={EXPECTED_PROTOCOLS[key]}" for key in stale),
                }
            )
        if rom_sha256 is not None and evidence.get("base_rom_sha256") != rom_sha256:
            blockers.append({"area": "evidence", "status": "STALE_ROM_HASH"})
        score = evidence.get("score")
        if not isinstance(score, dict):
            blockers.append({"area": "execution", "status": "MISSING_SCORE"})
        elif score.get("unrun", 1):
            blockers.append({"area": "execution", "status": "UNRESOLVED_GROUPS"})

    if validation_evidence is not None:
        validation_evidence = _need_object(validation_evidence, "development validation")
        if (
            rom_sha256 is None
            or validation_evidence.get("rom_sha256") != rom_sha256
            or validation_evidence.get("full_release_regression") != "PASS"
        ):
            blockers.append(
                {
                    "area": "release_regression",
                    "status": "NOT_COMPLETE",
                    "scope": "Validation must match the current ROM hash and mark full_release_regression PASS",
                }
            )

    status = "BLOCKED" if blockers else "READY"
    result = {
        "schema": 1,
        "status": status,
        "policy": "coverage_first",
        "rom_sha256": rom_sha256,
        "report": _display(report_path, root),
        "score": evidence.get("score") if isinstance(evidence, dict) else None,
        "blockers": blockers,
        "note": "Known emulator test failures are results, not proof of missing suite coverage. READY does not mean all emulators pass.",
    }
    return result, 2 if blockers else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", default="reports/development.json")
    parser.add_argument("--validation", default="reports/development-validation.json")
    parser.add_argument("--rom", default="suite.nds")
    parser.add_argument("--output", default="reports/release-readiness.json")
    args = parser.parse_args(argv)
    output_path = _resolve(ROOT, args.output)

    try:
        result, code = evaluate(ROOT, report=args.report, validation=args.validation, rom=args.rom)
    except (GateInputError, OSError, ValueError) as exc:
        result = {"schema": 1, "status": "ERROR", "error": str(exc)}
        code = 3

    try:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    except OSError as exc:
        result = {"schema": 1, "status": "ERROR", "error": f"Cannot write {output_path}: {exc}"}
        code = 3

    print(json.dumps(result, indent=2))
    return code


if __name__ == "__main__":
    sys.exit(main())
