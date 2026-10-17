#!/usr/bin/env python3
"""Four independently executed/scored CPU groups, isolation and interrupted-group recovery."""
import hashlib,json,os,struct,subprocess,sys,tempfile
from pathlib import Path
from diagnostics import cpu_rows
R=Path(__file__).resolve().parents[1];os.chdir(R)
rom=Path('suite.nds').read_bytes();manifest=json.loads(Path('src/generated/debug-manifest.json').read_text());groups=json.loads(Path('src/groups.json').read_text());checks={}
assert [v['name'] for v in groups[:4]]==['ARM7','ARM7Thumb','ARM9','ARM9Thumb'] and len(groups)==27
assert manifest['schema']==3
for cpu,key in [(7,'arm7_vectors'),(9,'vectors')]:
 for v in manifest[key]:
  assert v['cpu_group']==f'ARM{cpu}'+('Thumb' if v['isa']=='THUMB' else '')
  assert groups[v['group_id']]['name']==v['cpu_group']
  if '_return_' in v['name']:assert v['isa']=='ARM'
checks['explicit_complete_cpu_isa_classification']='PASS'
nm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-nm'
syms={cpu:{s[2]:int(s[0],16) for line in subprocess.check_output([str(nm),f'artifacts/arm{cpu}.elf'],text=True).splitlines() if len(s:=line.split())==3 and s[0][0] in '0123456789abcdef'} for cpu in (7,9)}
def execute(data,gid,label,resume=False):
 with tempfile.TemporaryDirectory() as d:
  path=Path(d)/'injected.nds';path.write_bytes(data);out=Path('reports')/('cpu-groups-'+label+'.json')
  cmd=[sys.executable,'scripts/run_resilient.py' if resume else 'scripts/run_desmume.py','--rom',str(path),'--output',str(out)]
  cmd+=['--frames','180'] if resume else ['--child',str(gid)]
  p=subprocess.run(cmd,capture_output=True,text=True,timeout=90);out.with_suffix('.log').write_text(p.stdout+p.stderr)
  assert p.returncode in ((0,1) if resume else (0,)),p.stderr
  report=json.loads(out.read_text());return report
baselines={}
for group in groups[:4]:
 gid=group['id'];cpu=group['cpu'];r=execute(rom,gid,f'{gid}-baseline');baselines[gid]=r
 rows=cpu_rows(r['diagnostics'][f'arm{cpu}']);own=[v for v in rows if v['group_id']==gid]
 assert all(v['status'] in ('PASS','FAIL') for v in own)
 assert all(v['status']=='NOT_SELECTED' for v in rows if v['group_id']!=gid)
 for other in (7,9):
  if other!=cpu and f'arm{other}' in r['diagnostics']:assert all(v['status']=='NOT_SELECTED' for v in cpu_rows(r['diagnostics'][f'arm{other}']))
 assert r['score']['planned']==1 and r['score']['unrun']==0
 assert r['score'][f'cpu{cpu}']=={'passed':sum(v['status']=='PASS' for v in own),'failed':sum(v['status']=='FAIL' for v in own)}
 checks[group['name']+'_independent_plan_and_score']='PASS'
for group in groups[:4]:
 gid=group['id'];cpu=group['cpu'];r=baselines[gid];rows=cpu_rows(r['diagnostics'][f'arm{cpu}'])
 v=next(v for v in rows if v['group_id']==gid and v['status']=='PASS');changed=bytearray(rom)
 pattern=struct.pack('<7I',syms[cpu][v['name']],*[v[k] for k in ('a','b','initial','result','flags','mask')]);assert changed.count(pattern)==1
 struct.pack_into('<I',changed,changed.find(pattern)+16,v['result']^1)
 mutated=execute(changed,gid,f'{gid}-fault');bad=lambda a:{v['id'] for v in a if v['status']=='FAIL'}
 assert bad(cpu_rows(mutated['diagnostics'][f'arm{cpu}']))==bad(rows)|{v['id']}
 opposite=gid^1;outside=execute(changed,opposite,f'{gid}-opposite');otherrows=cpu_rows(outside['diagnostics'][f'arm{cpu}'])
 assert bad(otherrows)==bad(cpu_rows(baselines[opposite]['diagnostics'][f'arm{cpu}'])) and otherrows[v['id']-1]['status']=='NOT_SELECTED'
 checks[group['name']+'_fault_isolated_from_other_isa']='PASS'
# Hard ARM-only hangs must not prevent Thumb execution after a fresh supervised boot.
for cpu,gid,next_gid in [(7,0,1),(9,2,3)]:
 changed=bytearray(rom);offset,_,base,_=struct.unpack_from('<4I',rom,0x30 if cpu==7 else 0x20)
 struct.pack_into('<I',changed,offset+syms[cpu]['cpu_arm_and']-base,0xeafffffe)
 r=execute(changed,gid,f'arm{cpu}-hang',True)
 assert r['score']['complete'] and r['score']['unrun']==0
 assert r['groups'][gid]['status']=='HOST_TIMEOUT'
 thumb=cpu_rows(r['diagnostics'][f'arm{cpu}']);assert all(v['status'] in ('PASS','FAIL') for v in thumb if v['group_id']==next_gid)
 assert r['groups'][next_gid]['status']=='FAIL' # retained reference mismatches, not hang
 checks[f'arm{cpu}_hang_resumes_into_thumb_group']='PASS'
with tempfile.TemporaryDirectory() as d:
 out=Path(d)/'result.json';seed=Path(d)/'v2.json';seed.write_text(json.dumps({'group_schema':2,'start':0,'counters':[0]*4,'ledger':[0]*54}))
 p=subprocess.run([sys.executable,'scripts/run_desmume.py','--child',str(0xffffffff),'--seed',str(seed),'--output',str(out)],capture_output=True,text=True,timeout=30)
 assert p.returncode!=0 and 'Stale/incompatible resume seed' in p.stderr and not out.exists()
 p=subprocess.run([sys.executable,'scripts/run_desmume.py','--group','999','--output',str(out)],capture_output=True,text=True,timeout=30)
 assert p.returncode==2 and not out.exists()
checks['old_seed_and_invalid_public_selector_rejected']='PASS'
r=json.loads(Path('reports/development.json').read_text());assert r['score']['planned']==27 and r['score']['unrun']==0
assert [s['group'] for s in r['segments'] if s['outcome']=='EMULATOR_CRASH']==[22,23]
for cpu in (7,9):assert all(v['status'] in ('PASS','FAIL') and v['historical'] for v in cpu_rows(r['diagnostics'][f'arm{cpu}']))
checks['27_group_resume_preserves_all_cpu_records']='PASS'
Path('reports/cpu-group-tests.json').write_text(json.dumps({'base_rom_sha256':hashlib.sha256(rom).hexdigest(),'group_schema':3,'tests':checks},indent=2)+'\n')
for k,v in checks.items():print(v,k)
