#!/usr/bin/env python3
"""Compile and run the Thumb branch boundary/sweep matrices on documented QEMU models."""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

from emit_checker import emit

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
CC = Path(os.environ.get("DEVKITARM", "/opt/devkitpro/devkitARM")) / "bin/arm-none-eabi-g++"
PROFILES = (
    ("v5", "arm946e-s", ("arm946",)),
    ("arm7", "arm7tdmi", ("arm946", "ti925t")),
)


def main() -> int:
    results = []
    (ROOT / "artifacts").mkdir(exist_ok=True)
    (ROOT / "reports").mkdir(exist_ok=True)

    for profile, architecture, models in PROFILES:
        manifest_path = ROOT / f"src/generated/{profile}-vectors.json"
        vectors = json.loads(manifest_path.read_text(encoding="utf-8"))
        scopes = {"thumb_branch", "thumb_branch_sweep"}
        rows = [row for row in vectors if row.get("verification_scope") in scopes]
        edge_count = sum(row.get("verification_scope") == "thumb_branch" for row in rows)
        sweep_count = sum(row.get("verification_scope") == "thumb_branch_sweep" for row in rows)
        if (edge_count, sweep_count, len(rows)) != (320, 1018, 1338):
            raise SystemExit(
                f"{profile}: expected 320 edge + 1,018 sweep = 1,338 vectors, "
                f"got {edge_count} + {sweep_count} = {len(rows)}"
            )

        stem = f"thumb-branch-{profile}"
        cpp = ROOT / f"artifacts/{stem}.cpp"
        cpp.write_text(
            emit(
                [
                    f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);'
                    for name in dict.fromkeys(row["name"] for row in rows)
                ],
                rows,
                "thumb_branch_oracle",
            ),
            encoding="utf-8",
        )
        main_cpp = ROOT / "artifacts/thumb-branch-main.cpp"
        main_cpp.write_text(
            '''extern "C" unsigned thumb_branch_oracle_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){
 unsigned detail[6];
 unsigned bad=thumb_branch_oracle_checks(detail);
 if(detail[4]!=1338 || detail[5]!=0) bad=1;
 semi(4,bad?"FAIL: 1338 Thumb branch cases\\n":"PASS: 1338 Thumb branch cases\\n");
 unsigned args[2]={0x20026,bad?1u:0u};
 semi(0x20,args);
 for(;;){}
}
''',
            encoding="utf-8",
        )
        elf = ROOT / f"artifacts/{stem}.elf"
        compile_command = [
            str(CC),
            f"-mcpu={architecture}",
            "-marm",
            "-O2",
            "-nostdlib",
            "-fno-exceptions",
            "-fno-rtti",
            "-T",
            "src/qemu/link.ld",
            "src/qemu/start.s",
            "src/qemu/runtime.cpp",
            "src/trace.cpp",
            str(cpp),
            str(main_cpp),
            f"src/generated/{profile}.s",
            "-o",
            str(elf),
        ]
        subprocess.run(compile_command, check=True)

        for model in models:
            command = [
                "qemu-system-arm",
                "-M",
                "integratorcp",
                "-cpu",
                model,
                "-nographic",
                "-monitor",
                "none",
                "-serial",
                "none",
                "-audio",
                "driver=none",
                "-semihosting-config",
                "enable=on,target=native",
                "-kernel",
                str(elf),
            ]
            try:
                process = subprocess.run(
                    command, capture_output=True, text=True, timeout=30, check=False
                )
                output = process.stdout + process.stderr
                status = (
                    "PASS"
                    if process.returncode == 0 and "PASS: 1338 Thumb branch cases" in output
                    else "FAIL"
                )
                returncode = process.returncode
            except subprocess.TimeoutExpired as exc:
                output = (exc.stdout or "") + (exc.stderr or "")
                status = "TIMEOUT"
                returncode = None
            results.append(
                {
                    "profile": profile,
                    "cpu_model": model,
                    "status": status,
                    "returncode": returncode,
                    "output": output,
                    "command": command,
                    "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
                }
            )

    report = {
        "vectors_per_profile": 1338,
        "edge_cases_per_profile": 320,
        "sweep_cases_per_profile": 1018,
        "helpers_per_profile": 306,
        "exact_arm7tdmi": "UNAVAILABLE",
        "ti925t_role": "ARMv4T proxy only; not exact ARM7TDMI verification",
        "scope": "All conditional-B offsets with safe runtime states, plus boundary and stratified unconditional-B offsets; no DS BIOS/MMIO/timing claim",
        "results": results,
    }
    output_path = ROOT / "reports/qemu-thumb-branch.json"
    output_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if results and all(item["status"] == "PASS" for item in results) else 1


if __name__ == "__main__":
    sys.exit(main())
