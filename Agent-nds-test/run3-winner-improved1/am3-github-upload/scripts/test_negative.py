#!/usr/bin/env python3
"""Fault injection proves that ROM assertions and host timeout paths are active."""
import json,os,struct,subprocess,sys,tempfile
from pathlib import Path
R=Path(__file__).resolve().parents[1];os.chdir(R)
manifest=json.loads(Path('src/generated/debug-manifest.json').read_text());vectors=manifest['vectors'];count=manifest['arm9_count']
arm9_arm_total=sum(v['group_id']==2 for v in vectors);arm9_thumb_total=sum(v['group_id']==3 for v in vectors)
def run(args,output):
 r=subprocess.run([sys.executable,'scripts/run_desmume.py',*args,'--output',output],capture_output=True,text=True,timeout=30)
 (R/Path(output).with_suffix('.log')).write_text(r.stdout+r.stderr)
 if r.returncode:raise RuntimeError(r.stderr)
 return json.loads(Path(output).read_text())
r=run(['--child','999'],'reports/negative-selector.json')
assert r['status']=='FAIL' and r['assertion']==0xffffffff
r=run(['--child','0','--frames','1'],'reports/negative-frame.json')
assert r['status']=='HOST_FRAME_TIMEOUT'
nm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-nm'
symbols=subprocess.check_output([str(nm),'artifacts/arm9.elf'],text=True)
address=int(next(l.split()[0] for l in symbols.splitlines() if l.endswith(' cpu_arm_and')),16)
pattern=struct.pack('<7I',address,0,0,0x10000000,0,0x50000000,0xf0000000)
rom=bytearray(Path('suite.nds').read_bytes());assert rom.count(pattern)==1
pos=rom.find(pattern);struct.pack_into('<I',rom,pos+16,1)
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'fault.nds';p.write_bytes(rom)
 r=run(['--child','2','--rom',str(p)],'reports/negative-vector.json')
 assert r['status']=='FAIL' and r['assertion']==1
 assert r['score']['cpu9']=={'passed':arm9_arm_total-225,'failed':225}
 assert r['score']['passed']==0 and r['score']['percent']==0
 output='reports/negative-recovery.json'
 proc=subprocess.run([sys.executable,'scripts/run_resilient.py','--rom',str(p),'--output',output],capture_output=True,text=True,timeout=90)
 Path('reports/negative-recovery.log').write_text(proc.stdout+proc.stderr)
 assert proc.returncode in (0,1) and Path(output).exists()
 r=json.loads(Path(output).read_text())['results'][0]
 assert r['score']['cpu9']=={'passed':count-321,'failed':321}
 assert r['last_mailbox'][3]>0, 'CPU failure prevented later groups from running'
 assert r['score']['passed']+r['score']['failed']+r['score']['unrun']==r['score']['planned']
print('PASS: invalid selector, frame timeout, incorrect CPU expectation, CPU continuation and score accounting')
# Prove execution of new control-flow/stack cases, not just the original ALU table.
for op in ['branch_eq','push_lr','pop_pc']:
 v=next(v for v in vectors if v['name']=='cpu_thumb_'+op)
 address=int(next(l.split()[0] for l in symbols.splitlines() if l.endswith(' '+v['name'])),16)
 pattern=struct.pack('<7I',address,*[v[k] for k in ['a','b','initial','result','flags','mask']])
 rom=bytearray(Path('suite.nds').read_bytes());assert rom.count(pattern)==1
 struct.pack_into('<I',rom,rom.find(pattern)+16,v['result']^1)
 with tempfile.TemporaryDirectory() as d:
  p=Path(d)/'thumb-fault.nds';p.write_bytes(rom)
  r=run(['--child','3','--rom',str(p)],'reports/negative-thumb-'+op+'.json')
  assert r['status']=='FAIL' and r['assertion']==v['id']
  assert r['score']['cpu9']=={'passed':arm9_thumb_total-97,'failed':97}
 print('PASS: fault injection for',op)
for label,name in [('condition','cpu_arm_cond_eq'),('operand2','cpu_arm_adc_operand2_rrx_0'),('psr','cpu_arm_msr_flags_mrs')]:
 v=next(v for v in vectors if v['name']==name)
 address=int(next(l.split()[0] for l in symbols.splitlines() if l.endswith(' '+name)),16)
 pattern=struct.pack('<7I',address,*[v[k] for k in ['a','b','initial','result','flags','mask']])
 rom=bytearray(Path('suite.nds').read_bytes());assert rom.count(pattern)==1
 struct.pack_into('<I',rom,rom.find(pattern)+16,v['result']^1)
 with tempfile.TemporaryDirectory() as d:
  path=Path(d)/'arm-fault.nds';path.write_bytes(rom)
  r=run(['--child','2','--rom',str(path)],'reports/negative-arm-'+label+'.json')
  assert r['status']=='FAIL' and r['assertion']==v['id']
  assert r['score']['cpu9']=={'passed':arm9_arm_total-225,'failed':225}
 print('PASS: fault injection for',label)
