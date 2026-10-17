#!/usr/bin/env python3
"""Run immediate-ALU vectors on QEMU as a functional cross-check, not an NDS model."""
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
SCOPES = {"arm_immediate_rotate", "thumb_immediate_alu"}
EXPECTED = 2576


def main() -> int:
    results: list[dict] = []
    (ROOT / "artifacts").mkdir(exist_ok=True)
    (ROOT / "reports").mkdir(exist_ok=True)
    for profile, architecture, models in PROFILES:
        vectors = json.loads((ROOT / f"src/generated/{profile}-vectors.json").read_text())
        rows = [row for row in vectors if row.get("verification_scope") in SCOPES]
        counts = {
            scope: sum(row.get("verification_scope") == scope for row in rows)
            for scope in SCOPES
        }
        if len(rows) != EXPECTED or counts != {
            "arm_immediate_rotate": 528,
            "thumb_immediate_alu": 2048,
        }:
            raise SystemExit(f"{profile}: unexpected immediate-ALU coverage {counts}")
        if [row["id"] for row in rows] != list(range(rows[0]["id"], rows[0]["id"] + EXPECTED)):
            raise SystemExit(f"{profile}: immediate-ALU IDs are not one append-only range")

        stem = f"immediate-alu-{profile}"
        cpp = ROOT / f"artifacts/{stem}.cpp"
        decl = [
            f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);'
            for name in dict.fromkeys(row["name"] for row in rows)
        ]
        cpp.write_text(emit(decl, rows, "immediate_alu_oracle"), encoding="utf-8")
        main_cpp = ROOT / f"artifacts/{stem}-main.cpp"
        main_cpp.write_text(
            '''extern "C" unsigned immediate_alu_oracle_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){
 unsigned detail[6];
 unsigned bad=immediate_alu_oracle_checks(detail);
 if(detail[4]!=2576 || detail[5]!=0) bad=1;
 semi(4,bad?"FAIL: immediate ALU\\n":"PASS: 2576 immediate ALU cases\\n");
 unsigned args[2]={0x20026,bad?1u:0u};
 semi(0x20,args);
 for(;;){}
}
''',
            encoding="utf-8",
        )
        elf = ROOT / f"artifacts/{stem}.elf"
        compile_command = [
            str(CC), f"-mcpu={architecture}", "-marm", "-O2", "-nostdlib",
            "-fno-exceptions", "-fno-rtti", "-T", "src/qemu/link.ld",
            "src/qemu/start.s", "src/qemu/runtime.cpp", "src/trace.cpp",
            str(cpp), str(main_cpp), f"src/generated/{profile}.s", "-o", str(elf),
        ]
        subprocess.run(compile_command, check=True)
        for model in models:
            command = [
                "qemu-system-arm", "-M", "integratorcp", "-cpu", model,
                "-nographic", "-monitor", "none", "-serial", "none",
                "-audio", "driver=none", "-semihosting-config",
                "enable=on,target=native", "-kernel", str(elf),
            ]
            try:
                process = subprocess.run(command, capture_output=True, text=True, timeout=45, check=False)
                output = process.stdout + process.stderr
                status = (
                    "PASS" if process.returncode == 0
                    and "PASS: 2576 immediate ALU cases" in output else "FAIL"
                )
                returncode = process.returncode
            except subprocess.TimeoutExpired as exc:
                output = (exc.stdout or "") + (exc.stderr or "")
                status = "TIMEOUT"
                returncode = None
            results.append({
                "profile": profile,
                "cpu_model": model,
                "status": status,
                "returncode": returncode,
                "output": output,
                "command": command,
                "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
                "selected_vectors": len(rows),
                "scope_counts": counts,
            })

    report = {
        "vectors_per_profile": EXPECTED,
        "arm_immediate_rotate": 528,
        "thumb_immediate_alu": 2048,
        "exact_arm7tdmi": "UNAVAILABLE",
        "ti925t_role": "ARMv4T proxy only; not exact ARM7TDMI verification",
        "scope": "Functional immediate-opcode comparison only; no DS BIOS/MMIO/timing or hardware claim",
        "results": results,
    }
    output = ROOT / "reports/qemu-immediate-alu.json"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if results and all(row["status"] == "PASS" for row in results) else 1


if __name__ == "__main__":
    sys.exit(main())
