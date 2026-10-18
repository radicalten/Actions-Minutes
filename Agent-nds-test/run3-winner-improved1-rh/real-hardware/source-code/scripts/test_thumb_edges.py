#!/usr/bin/env python3
"""Actual per-CPU Thumb execution, encoding and primary/flag/carry fault regressions."""
import hashlib,json,os,struct,subprocess,sys,tempfile
from pathlib import Path
from diagnostics import cpu_rows
R=Path(__file__).resolve().parents[1];os.chdir(R)
original=Path('suite.nds').read_bytes();m=json.loads(Path('src/generated/debug-manifest.json').read_text());checks={};bodies={}
nm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-nm'
OP={'lsl':2,'lsr':3,'asr':4,'adc':5,'sbc':6,'ror':7,'neg':9,'mul':13}
def run(cpu,rom,label):
 with tempfile.TemporaryDirectory() as d:
  p=Path(d)/'injected.nds';p.write_bytes(rom);out=Path('reports')/f'thumb-{cpu}-{label}.json'
  proc=subprocess.run([sys.executable,'scripts/run_desmume.py','--child',str(1 if cpu==7 else 3),'--rom',str(p),'--output',str(out)],capture_output=True,text=True,timeout=60)
  out.with_suffix('.log').write_text(proc.stdout+proc.stderr);assert proc.returncode==0,proc.stderr
  return cpu_rows(json.loads(out.read_text())['diagnostics'][f'arm{cpu}'])
def bad(rows):return {v['id'] for v in rows if v['status']=='FAIL'}
for cpu,total in [(9,13380),(7,12056)]:
 vectors=m['vectors' if cpu==9 else 'arm7_vectors'];new=[v for v in vectors if v.get('verification_scope')=='thumb_edges'];assert len(new)==800
 first=new[0]['id'];syms={s[2]:int(s[0],16) for l in subprocess.check_output([str(nm),f'artifacts/arm{cpu}.elf'],text=True).splitlines() if len(s:=l.split())==3 and s[0][0] in '0123456789abcdef'}
 off,_,base,size=struct.unpack_from('<4I',original,0x20 if cpu==9 else 0x30)
 def pos(address):
  assert base<=address<base+size
  return off+address-base
 baseline=run(cpu,original,'baseline');assert len(baseline)==total and len(bad(baseline))==(96 if cpu==9 else 10)
 assert all(v['status']=='PASS' and v['function_pc']==syms[v['name']] for v in baseline[first-1:first+799])
 chosen={}
 for v in new:chosen.setdefault(v['name'],v)
 assert len(chosen)==15
 bodies[cpu]=[]
 for name in chosen:
  op,form=name.split('_')[-2:];address=pos(syms[name+'_instruction']&~1)
  assert address==pos(syms[name])+32
  assert struct.unpack_from('<HH',original,address)==(0x4000|(OP[op]<<6)|(0 if form=='alias' else 8),0x4728)
  bodies[cpu].append(original[pos(syms[name]):pos(syms[name])+60])
 checks[f'arm{cpu}_800_executions_and_15_thumb_encodings']='PASS'
 def patch(rom,v,field,bit):
  pattern=struct.pack('<7I',syms[v['name']],*[v[k] for k in ('a','b','initial','result','flags','mask')]);assert rom.count(pattern)==1
  offset=rom.find(pattern);struct.pack_into('<I',rom,offset+(16 if field=='result' else 20),v[field]^bit)
 for field,bit in [('result',1),('flags',0x80000000)]:
  rom=bytearray(original)
  for v in chosen.values():patch(rom,v,field,bit)
  rows=run(cpu,rom,field+'-negative');assert bad(rows)==bad(baseline)|{v['id'] for v in chosen.values()}
  assert all(rows[v['id']-1]['runtime_differs_manifest'] for v in chosen.values())
  checks[f'arm{cpu}_15_{field}_mutations_detected']='PASS'
 rom=bytearray(original)
 for name in chosen:
  op,form=name.split('_')[-2:]
  if op not in ('adc','sbc'):continue
  # ADDS/SUBS r0,r0,r1 (or r0): same inputs but no incoming C/borrow.
  ins=(0x1800 if op=='adc' else 0x1a00)|(0 if form=='alias' else 0x40)
  struct.pack_into('<H',rom,pos(syms[name+'_instruction']&~1),ins)
 rows=run(cpu,rom,'carry-negative');expected={v['id'] for v in new if ('_adc_' in v['name'] and v['initial']&(1<<29)) or ('_sbc_' in v['name'] and not v['initial']&(1<<29))}
 assert len(expected)==64 and bad(rows)==bad(baseline)|expected
 checks[f'arm{cpu}_64_lost_carry_cases_detected']='PASS'
 rom=bytearray(original);mul=[v for v in chosen.values() if '_mul_' in v['name']]
 for v in mul:patch(rom,v,'flags',0x20000000)
 rows=run(cpu,rom,'mul-carry-mask');assert bad(rows)==bad(baseline)|({v['id'] for v in mul} if cpu==9 else set())
 assert all(rows[v['id']-1]['runtime_differs_manifest'] for v in mul)
 checks[f'arm{cpu}_mul_carry_'+('checked' if cpu==9 else 'excluded_not_asserted')]='PASS'
assert bodies[7]==bodies[9];checks['same_thumb_helper_machine_code_both_cpus']='PASS'
Path('reports/thumb-tests.json').write_text(json.dumps({'base_rom_sha256':hashlib.sha256(original).hexdigest(),'cases_per_cpu':800,'tests':checks},indent=2)+'\n')
for k,v in checks.items():print(v,k)
