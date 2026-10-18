#!/usr/bin/env python3
"""Actual ARM9 extension and ARM7 nesting execution/fault-injection regression."""
import hashlib,json,os,struct,subprocess,sys,tempfile
from pathlib import Path
from diagnostics import cpu_rows
R=Path(__file__).resolve().parents[1];os.chdir(R)
original=Path('suite.nds').read_bytes();m=json.loads(Path('src/generated/debug-manifest.json').read_text());checks={};selected=[]
nm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-nm'
def symbols(cpu):
 return {s[2]:int(s[0],16) for l in subprocess.check_output([str(nm),f'artifacts/arm{cpu}.elf'],text=True).splitlines() if len(s:=l.split())==3 and s[0][0] in '0123456789abcdef'}
def run(rom,cpu,name,args=()):
 groups=(2,3) if cpu==9 else (0,1)
 combined={};reports=[]
 for index,gid in enumerate(groups):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'injected.nds';p.write_bytes(rom);out=Path('reports')/(name+f'-group{gid}.json')
   extra=list(args) if index==0 else []
   proc=subprocess.run([sys.executable,'scripts/run_desmume.py','--child',str(gid),'--rom',str(p),'--output',str(out),*extra],capture_output=True,text=True,timeout=90)
   out.with_suffix('.log').write_text(proc.stdout+proc.stderr);assert proc.returncode==0,proc.stderr
   report=json.loads(out.read_text());reports.append(report)
   for row in cpu_rows(report['diagnostics']['arm'+str(cpu)]):
    if row['status']!='NOT_SELECTED':combined[row['id']]=row
 assert len(combined)==len(m['vectors' if cpu==9 else 'arm7_vectors'])
 rows=[combined[i] for i in range(1,len(combined)+1)]
 return reports[0],rows
def ids(rows):return {r['id'] for r in rows if r['status']=='FAIL'}
def table_patch(rom,v,cpu,field='result',bit=1):
 syms=symbols(cpu);pattern=struct.pack('<7I',syms[v['name']],*[v[k] for k in ('a','b','initial','result','flags','mask')]);anchor=m['vectors' if cpu==9 else 'arm7_vectors'][3390]
 anchor_pattern=struct.pack('<7I',syms[anchor['name']],*[anchor[k] for k in ('a','b','initial','result','flags','mask')]);assert original.count(anchor_pattern)==1
 pos=original.find(anchor_pattern)+(v['id']-3391)*28;assert rom[pos:pos+28]==pattern
 off,_,_,size=struct.unpack_from('<4I',rom,0x20 if cpu==9 else 0x30);assert off<=pos<off+size
 struct.pack_into('<I',rom,pos+(16 if field=='result' else 20),v[field]^bit)
def instruction_patch(cpu,name,old,new,span):
 rom=bytearray(original);off,_,base,size=struct.unpack_from('<4I',rom,0x20 if cpu==9 else 0x30);start=off+symbols(cpu)[name]-base;assert off<=start<off+size
 pattern=struct.pack('<I',old);assert rom[start:start+span].count(pattern)==1
 pos=rom.find(pattern,start,start+span);struct.pack_into('<I',rom,pos,new);return rom
r,base9=run(original,9,'m8-baseline-arm9',['--detail-page','9','--debug-keys','B','--screenshot','reports/arm9-system-screen.png'])
assert len(base9)==13380 and len(ids(base9))==320 and all(v['status'] in ('PASS','FAIL') for v in base9)
assert all(v['status']=='PASS' for v in base9[3486:5498])
assert sum(v['status']=='FAIL' for v in base9[5498:5730])==160
assert sum(v['status']=='PASS' for v in base9[5730:5986])==96
assert all(not(v['observed_aux']&0xe00) for v in base9[5730:5986]) # MPU enabled and restoration clean on this backend
checks['arm9_full_execution_reference_failures_retained']='PASS'
checks['raw_mpu_enabled_control_itcm_and_vector_restoration']='PASS'
# One primary-expectation mutation per new helper having a passing baseline case.
rom=bytearray(original);seen=set()
for v in base9[3486:5986]:
 if v['status']=='PASS' and v['name'] not in seen:
  seen.add(v['name']);selected.append(v['id']);table_patch(rom,m['vectors'][v['id']-1],9)
r,rows=run(rom,9,'m8-arm9-family-negative')
assert ids(rows)==ids(base9)|set(selected)
assert all(rows[i-1]['runtime_differs_manifest'] and rows[i-1]['result_xor']==1 for i in selected)
checks['every_passing_new_arm9_helper_primary_fault_detected']='PASS'
rom=bytearray(original);v=next(v for v in m['vectors'] if v['name']=='cpu_arm9_qadd_sticky_q');table_patch(rom,v,9,'flags',0x08000000)
r,rows=run(rom,9,'m8-dsp-q-negative');assert ids(rows)==ids(base9)|{v['id']} and rows[v['id']-1]['aux_masked_xor']==0x08000000
checks['sticky_q_auxiliary_fault_detected']='PASS'
rom=instruction_patch(9,'cpu_arm9_timer_irq_handler',0xe2811001,0xe3a01000,160)
r,rows=run(rom,9,'m8-arm9-irq-negative');assert ids(rows)==ids(base9)|set(range(5403,5499))
assert all(v['aux_masked_xor']&0x10000 for v in rows[5402:5498])
checks['arm9_real_irq_missing_counter_detected']='PASS'
rom=instruction_patch(9,'cpu_arm9_raw_swi_handler',0xe2800001,0xe3a00000,100)
r,rows=run(rom,9,'m8-raw-swi-negative');assert ids(rows)==ids(base9)|set(range(5731,5795))
assert all(v['aux_masked_xor']&0x100 for v in rows[5730:5794])
checks['raw_swi_missing_handler_count_detected']='PASS'
r,base7=run(original,7,'m8-baseline-arm7');assert len(base7)==12056 and len(ids(base7))==23 and all(v['status']=='PASS' for v in base7[4630:4662])
checks['all_32_arm7_nested_cases_pass']='PASS'
rom=instruction_patch(7,'cpu_arm7_nested_handler',0xe3a01002,0xe3a01001,10000)
r,rows=run(rom,7,'m8-arm7-nested-negative');assert ids(rows)==ids(base7)|set(range(4631,4663))
assert all(v['aux_masked_xor']&0x100 for v in rows[4630:4662])
checks['arm7_missing_second_irq_count_detected']='PASS'
report={'base_rom_sha256':hashlib.sha256(original).hexdigest(),'tests':checks,'primary_injected_case_ids':selected,'primary_injected_helpers':len(selected)}
Path('reports/milestone8-tests.json').write_text(json.dumps(report,indent=2)+'\n')
for k,v in checks.items():print(v,k)
print('Primary expectation faults:',len(selected))
