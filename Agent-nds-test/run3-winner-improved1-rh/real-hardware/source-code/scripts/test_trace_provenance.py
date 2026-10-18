#!/usr/bin/env python3
"""Synthetic partial-plan resume fixture: keep prior ARM and fresh Thumb provenance distinct."""
import hashlib,json,struct,subprocess,sys,tempfile
from pathlib import Path
from diagnostics import checked_blob,cpu_rows,validate_cpu
from trace_layout import RECORD_WORDS,VERSION
R=Path(__file__).resolve().parents[1]
r=json.loads((R/'reports/development.json').read_text());m=json.loads((R/'src/generated/debug-manifest.json').read_text());rom_sha=hashlib.sha256((R/'suite.nds').read_bytes()).hexdigest()
words=list(validate_cpu(checked_blob(r['diagnostics']['arm9'])))
assert words[1]==VERSION
for i,v in enumerate(m['vectors']):
 at=16+RECORD_WORDS*i
 if v['isa']=='THUMB':words[at:at+RECORD_WORDS]=[0]*RECORD_WORDS
 else:words[at]&=255
states=words[16::RECORD_WORDS];words[4]=sum(x in (2,3) for x in states);words[5]=states.count(2);words[6]=states.count(3);words[7]=0;words[8]=0;words[11]=0
validate_cpu(struct.pack('<'+'I'*len(words),*words))
with tempfile.TemporaryDirectory() as d:
 root=Path(d);p=root/'partial.bin';p.write_bytes(struct.pack('<'+'I'*len(words),*words))
 entry={'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'header':words[:16]}
 ledger=[0]*54
 for group in r['groups'][:3]:ledger[2*group['id']]=2;ledger[2*group['id']+1]=group['assertion_or_cause']
 seed=root/'seed.json';seed.write_text(json.dumps({'group_schema':3,'base_rom_sha256':rom_sha,'start':3,'ledger':ledger,'counters':[words[5],words[6],r['score']['cpu7']['passed'],r['score']['cpu7']['failed']],'diagnostics':{'arm9':entry,'arm7':r['diagnostics']['arm7']}}))
 cp=R/'reports/provenance-checkpoint.json';out=root/'result.json'
 cp.unlink(missing_ok=True)
 cmd=[sys.executable,str(R/'scripts/run_desmume.py'),'--child',str(0xffffffff),'--seed',str(seed),'--output',str(out),'--checkpoint',str(cp),'--trace-dir',str(R/'reports/provenance-debug')]
 proc=subprocess.run(cmd,capture_output=True,text=True,timeout=40)
 (R/'reports/provenance-run.log').write_text(proc.stdout+proc.stderr)
 assert proc.returncode<0,'Expected retained native division crash after CPU groups'
 checkpoint=json.loads(cp.read_text());assert checkpoint['active']==22
 rows=cpu_rows(checkpoint['diagnostics']['arm9'])
 assert all(v['status'] in ('PASS','FAIL') for v in rows)
 assert all(v['historical'] for v in rows if v['isa']=='ARM')
 assert all(not v['historical'] for v in rows if v['isa']=='THUMB')
 assert len(rows)==len(m['vectors'])==13380
 assert sum(v['status']=='PASS' for v in rows)==13060
 assert sum(v['status']=='FAIL' for v in rows)==320
report={'base_rom_sha256':rom_sha,'fixture':'Synthetic partial-plan seed derived from actually observed ARM results; not a baseline run','tests':{'restored_arm_records_remain_historical':'PASS','new_thumb_records_are_current_boot':'PASS','all_13060_pass_records_preserved':'PASS','known_320_failures_retained':'PASS'}}
(R/'reports/trace-provenance-tests.json').write_text(json.dumps(report,indent=2)+'\n')
for k,v in report['tests'].items():print(v,k)
