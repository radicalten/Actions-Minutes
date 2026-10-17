#!/usr/bin/env python3
"""Actual multi-failure, ARM7, interrupted-vector, restart and explorer tests."""
import json,os,struct,subprocess,sys,tempfile
from pathlib import Path
from diagnostics import cpu_rows,assertion_rows
R=Path(__file__).resolve().parents[1];os.chdir(R)
manifest=json.loads(Path('src/generated/debug-manifest.json').read_text());vectors=manifest['vectors'];original=Path('suite.nds').read_bytes()
nm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-nm'
def symbols(cpu):
 lines=subprocess.check_output([str(nm),f'artifacts/arm{cpu}.elf'],text=True).splitlines()
 return {s[2]:int(s[0],16) for line in lines if len(s:=line.split())==3 and s[0][0] in '0123456789abcdef'}
def mutate(cpu,ids):
 rom=bytearray(original);syms=symbols(cpu)
 for identifier in ids:
  v=vectors[identifier-1];pattern=struct.pack('<7I',syms[v['name']],*[v[k] for k in ['a','b','initial','result','flags','mask']]);assert rom.count(pattern)==1
  struct.pack_into('<I',rom,rom.find(pattern)+16,v['result']^1)
 return rom
def run(rom,name,args):
 with tempfile.TemporaryDirectory() as d:
  path=Path(d)/'fault.nds';path.write_bytes(rom);out=Path('reports')/(name+'.json')
  p=subprocess.run([sys.executable,'scripts/run_desmume.py','--rom',str(path),'--output',str(out),*args],capture_output=True,text=True,timeout=60)
  out.with_suffix('.log').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
  return json.loads(out.read_text())
results={}
r=run(mutate(9,[1,2,3391]),'debug-multi-failure',['--child','0','--detail-page','9','--debug-keys','B,B','--screenshot','reports/injected-vector-screen.png'])
rows=cpu_rows(r['diagnostics']['arm9']);bad=[v for v in rows if v['status']=='FAIL' and v['id']<=3391]
assert [v['id'] for v in bad]==[1,2,3391]
assert all(v['runtime_differs_manifest'] and v['result_xor']==1 for v in bad)
assert r['score']['cpu9']=={'passed':6175,'failed':323}
assert [step[2] for step in r['debug_navigation']]==[2,3391]
assert any('FAIL' in line for line in r['bottom_text'])
assert r['post_ui_mailbox']==r['last_mailbox'];results['multiple_failures_and_next_failure_ui']='PASS'
r=run(mutate(7,[1,2]),'debug-arm7-failures',['--child','1','--detail-page','10','--debug-keys','B'])
assert [v['id'] for v in cpu_rows(r['diagnostics']['arm7']) if v['status']=='FAIL' and v['id']<=3390]==[1,2]
assert r['score']['cpu7']=={'passed':5149,'failed':25}
assert r['debug_navigation'][0][2]==2;results['arm7_trace_and_explorer']='PASS'
r=run(original,'debug-family-navigation',['--child','0','--detail-page','9','--debug-keys','DOWN,A,DOWN'])
nav=r['debug_navigation'];assert nav[0][1]==1 and nav[0][0]==1
assert nav[1][0]==0 and nav[2][2]==nav[1][2]+1;results['family_and_case_selection']='PASS'
rom=bytearray(original);address=symbols(9)['cpu_arm_add'];off,entry,base,size=struct.unpack_from('<4I',rom,0x20)
struct.pack_into('<I',rom,off+address-base,0xeafffffe)
r=run(rom,'debug-interrupted',['--child','0','--frames','60'])
assert r['status']=='HOST_FRAME_TIMEOUT';rows=cpu_rows(r['diagnostics']['arm9'])
assert sum(v['status']=='INTERRUPTED' for v in rows)==1
assert any(v['status']=='PASS' for v in rows) and any(v['status']=='NOT_RUN' for v in rows)
results['interrupted_and_not_run_distinguished']='PASS'
r=json.loads(Path('reports/negative-recovery.json').read_text())['results'][0]
rows=cpu_rows(r['diagnostics']['arm9']);assert rows[0]['status']=='FAIL' and rows[0]['historical']
assert len([v for v in rows if v['status']=='FAIL'])==321
assert all(v['historical'] for v in cpu_rows(r['diagnostics']['arm7']))
events=assertion_rows(r['diagnostics']['assertions']);assert len(events)==160
assert any(e['group']==15 and e['status']=='FAIL' for e in events)
assert any(e['group']==23 and e['status']=='PASS' for e in events)
assert all(e.get('condition') and e.get('source') for e in events)
results['historical_traces_and_journal_survive_crashes']='PASS'
Path('reports/diagnostic-tests.json').write_text(json.dumps(results,indent=2)+'\n')
for key,status in results.items():print(status,key)
