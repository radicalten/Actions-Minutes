#!/usr/bin/env python3
"""Guard current milestone-10 evidence and bundle ROM/source/support files."""
from pathlib import Path
import hashlib,json,zipfile
from diagnostics import cpu_rows,assertion_rows
R=Path(__file__).resolve().parents[1]
def load(name):return json.loads((R/('reports/'+name+'.json')).read_text())
files={}
for folder in ['src','scripts','vendor','docs']:
 for p in sorted((R/folder).rglob('*')):
  if p.is_file() and '.git' not in p.parts and '__pycache__' not in p.parts:files[str(p.relative_to(R))]=hashlib.sha256(p.read_bytes()).hexdigest()
for name in ['suite.nds','readme.md','handoff.md','coverage.json','Makefile','NOTICE.md','upstream-lock.json','requirements.txt']:
 files[name]=hashlib.sha256((R/name).read_bytes()).hexdigest()
(R/'reports/source-hashes.json').write_text(json.dumps(files,indent=2)+'\n')
report=load('desmume');seq=load('resilient')
for name in ['desmume','resilient','sequential','cpu9','cpu7',*[f'ui-page-{i}' for i in range(12)]]:
 assert load(name)['base_rom_sha256']==files['suite.nds'],f'Stale primary report: {name}'
for cpu,count,bad in [('arm9',6498,320),('arm7',5174,23)]:
 rows=cpu_rows(seq['diagnostics'][cpu]);assert len(rows)==count
 assert sum(r['status']=='FAIL' for r in rows)==bad
 assert all(r['status'] in ('PASS','FAIL') for r in rows)
assert seq['score']['passed']==17 and seq['score']['planned']==25 and seq['score']['unrun']==0
assert len(assertion_rows(seq['diagnostics']['assertions']))==160
for name,count in [('qemu-v5',2012),('qemu-cp15',208)]:
 r=load(name);assert r['status']=='PASS' and r['vectors']==count
r=load('qemu-exceptions');assert (r['status'],r['passed'],r['unsupported'])==('PARTIAL_UNSUPPORTED',128,128)
assert r['mpu_enable_readback']=='ZERO' and len(r['mismatches'])==128
assert load('qemu-operand2')['vectors']==256
assert len(load('operand2-tests')['tests'])==9
assert load('qemu-compare2')['vectors']==256
assert len(load('compare2-tests')['tests'])==9
for name in ['qemu','qemu-privileged','qemu-rock','qemu-operand2','qemu-compare2']:assert all(v['status']=='PASS' for v in load(name)['results'])
for name in ['diagnostic-tests','recovery-tests']:assert all(v=='PASS' for v in load(name).values())
for name in ['arm7-specific-tests','privileged-tests','milestone8-tests','operand2-tests','compare2-tests']:
 r=load(name);assert r['base_rom_sha256']==files['suite.nds'] and all(v=='PASS' for v in r['tests'].values())
r=load('ui-tests');assert r['both_lcds_verified'] and r['score_preserved'] and len(r['results'])==12 and all(v['status']=='PASS' for v in r['results'])
assert 'Ran 36 tests' in (R/'reports/host-tests.log').read_text() and 'OK' in (R/'reports/host-tests.log').read_text()
assert (R/'reports/rebuild-before.sha256').read_text().split()[0]==files['suite.nds']
summary='''# Verification summary — milestone 10

**Partial coverage, not an exhaustive suite.** See readme.md, coverage.json and
[current compare/test scope](../docs/compare-operand2.md) and
[system/exception oracle limits](../docs/arm9-milestone8.md).

'''
summary+='ROM SHA-256: `'+files['suite.nds']+'`\n\n'
summary+='''| CPU | Total | PASS | FAIL |
|---|---:|---:|---:|
| ARM9 | 6,498 | 6,178 | 320 |
| ARM7 | 5,174 | 5,151 | 23 |

Added256 flag-only register-controlled operand2 cases per CPU; all512 new DS executions PASS. ARM9 failures are 64 CP15 region mirrors,
96 AP aliases,32 Thumb undefined nonentries,128 MPU-abort nonentries. The earlier
23 ARM7 mismatches remain. Expectations were not changed to match the backend.
These are reference discrepancies, not hardware-confirmed emulator findings.

**Independent QEMU evidence:** shared3390 PASS on ARM946 and ti925t proxy;
2012 portable ARM9 PASS;208 ARM946 CP15 aliases PASS;956 portable ARM7 privileged
PASS on both models;11 original CPU groups PASS ARM946. Separate raw-exception
oracle:128 SWI/UDF PASS with explicitly adapted low-RAM vectors,128 MPU-abort
UNSUPPORTED (MPU-enable reads zero). No ITCM enforcement, exact ARM7TDMI or physical
DS claim. Unsupported cases are not counted as PASS. Both the retained256 register-operand2 and new256 flag-only
cases additionally PASS ARM946 and the ti925t proxy; actual helper bytes match on
both DS CPUs and runtime ELF addresses are verified.

[Search all 11,672 vector rows](debugger.html) · [CSV](debugger.csv) ·
[Debugging protocol](../docs/debugging.md)

The full resumed run retains160 assertion events from117 instrumented sites.
Other paths remain group-only/unknown. Historical traces survive native crashes
and are labeled. Original groups are internally fail-fast; zero unrun groups is
not proof every original assertion executed.

'''
for title,r in [('Isolated',report),('Checkpointed sequential',seq)]:
 s=r['score'];summary+=f"**{title}: {s['passed']}/{s['planned']} = {s['percent']:.2f}%**, {s.get('unrun',s['planned']-len(r.get('results',[])))} unrun.\n\n"
summary+='''Six returned failures and two native division crashes; supervised execution is
explicitly resumed, not one uninterrupted boot. Both LCDs show the actual score
and per-case/group details; lower-screen browsing leaves the score unchanged.

![Actual completed score](score-screen.png)

| Group | Result | Assertion / process code |
|---|---|---|
'''
for r in report['results']:summary+=f"| {r['name']} | {r['status']} | {r.get('assertion',r.get('returncode',''))} |\n"
summary+='''
**Regression:**36 host checks;12 real-key lower-screen pages;six ARM7 attribution
checks;five diagnostic scenarios;three recovery scenarios;retained negative and
privileged injections. Nine new compare/test scenarios detect16 primary and16 NZCV
expectation mutations per CPU and212 unintended source-write cases per CPU, with
unchanged flags. The remaining44 opcode-mutated cases coincidentally leave Rn
unchanged and are not claimed as detected faults. Nine retained operand2 scenarios test both CPUs, including16 primary and16 NZCV
expectation mutations per CPU plus64 lost carry-input cases per CPU through actual
ADC-to-ADD/SBC-to-SUB instruction mutations. Retained eight milestone-8 scenarios include220 primary expectation
mutations,a sticky-Q auxiliary mutation,96 missing ARM9 IRQ counters,64 missing
raw SWI counters and32 missing second ARM7 IRQ counters. Injection ROMs were
temporary copies; the distributed ROM has no injected faults or enabled resume seed.
A deterministic rebuild matched the recorded ROM hash.

Diagnostic binaries are deduplicated into byte-hashed `reports/blobs`; JSON paths
and SHA-256 checks preserve every referenced observation. Keep those files together.
'''
(R/'reports/summary.md').write_text(summary)
archive=R.parent/'nds-suite-milestone10.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
 for p in sorted(R.rglob('*')):
  rel=p.relative_to(R)
  if not p.is_file() or any(x in ('.git','work','.venv','__pycache__') for x in rel.parts):continue
  z.write(p,Path('nds-suite')/rel)
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None
 for n in ['suite.nds','readme.md','handoff.md','docs/arm9-milestone8.md','docs/register-operand2.md','docs/compare-operand2.md','reports/debugger.html','src/generated/v5.s']:
  assert 'nds-suite/'+n in z.namelist()
print(archive,archive.stat().st_size,'bytes; integrity and release guards verified')
print('ROM SHA-256',files['suite.nds'])
