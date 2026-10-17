#!/usr/bin/env python3
"""Execution/attribution and primary, NZCV and carry-in fault injections on both CPUs."""
import hashlib,json,os,struct,subprocess,sys,tempfile
from pathlib import Path
from diagnostics import cpu_rows
R=Path(__file__).resolve().parents[1];os.chdir(R)
original=Path('suite.nds').read_bytes();manifest=json.loads(Path('src/generated/debug-manifest.json').read_text());checks={};bodies={}
nm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-nm'
def run(cpu,rom,label):
 with tempfile.TemporaryDirectory() as d:
  p=Path(d)/'fault.nds';p.write_bytes(rom);out=Path('reports')/f'remaining-{cpu}-{label}.json'
  r=subprocess.run([sys.executable,'scripts/run_desmume.py','--child',str(0 if cpu==7 else 2),'--rom',str(p),'--output',str(out)],capture_output=True,text=True,timeout=60)
  out.with_suffix('.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
  return cpu_rows(json.loads(out.read_text())['diagnostics']['arm'+str(cpu)])
for cpu,first,total in [(9,6499,9466),(7,5175,8142)]:
 vectors=manifest['vectors' if cpu==9 else 'arm7_vectors'];new=vectors[first-1:first+511];assert len(new)==512
 syms={s[2]:int(s[0],16) for l in subprocess.check_output([str(nm),f'artifacts/arm{cpu}.elf'],text=True).splitlines() if len(s:=l.split())==3 and s[0][0] in '0123456789abcdef'}
 off,_,base,size=struct.unpack_from('<4I',original,0x20 if cpu==9 else 0x30)
 def pos(address):
  assert base<=address<base+size
  return off+address-base
 baseline=run(cpu,original,'baseline');assert len(baseline)==total and all(v['status']=='PASS' and v['function_pc']==syms[v['name']] for v in baseline[first-1:first+511])
 baseline_bad={v['id'] for v in baseline if v['status']=='FAIL'};assert len(baseline_bad)==(224 if cpu==9 else 13)
 checks[f'arm{cpu}_all_512_executed_at_correct_elf_addresses']='PASS'
 chosen={}
 for v in new:chosen.setdefault(v['name'],v)
 assert len(chosen)==32
 bodies[cpu]=[original[pos(syms[n]):pos(syms[n])+44] for n in chosen]
 for field,byte_offset,bit in [('primary',16,1),('nzcv',20,0x20000000)]:
  rom=bytearray(original)
  for v in chosen.values():
   pattern=struct.pack('<7I',syms[v['name']],*[v[k] for k in ('a','b','initial','result','flags','mask')]);assert rom.count(pattern)==1
   address=rom.find(pattern);struct.pack_into('<I',rom,address+byte_offset,v['result' if field=='primary' else 'flags']^bit)
  rows=run(cpu,rom,field+'-negative');assert {v['id'] for v in rows if v['status']=='FAIL'}==baseline_bad|{v['id'] for v in chosen.values()}
  checks[f'arm{cpu}_32_{field}_expectation_mutations_detected']='PASS'
 # Replace RSCS by RSBS: only C=0 cases should change, testing reverse borrow.
 rom=bytearray(original)
 for name in chosen:
  if '_rsc_' not in name:continue
  address=pos(syms[name])+16;ins=struct.unpack_from('<I',rom,address)[0]
  assert ((ins>>21)&15)==7 and ins&(1<<20)
  struct.pack_into('<I',rom,address,(ins&~(15<<21))|(3<<21))
 rows=run(cpu,rom,'reverse-borrow-negative')
 expected={v['id'] for v in new if '_rsc_' in v['name'] and not v['initial']&(1<<29)}
 assert len(expected)==32 and {v['id'] for v in rows if v['status']=='FAIL'}==baseline_bad|expected
 checks[f'arm{cpu}_32_reverse_borrow_cases_detected']='PASS'
assert bodies[7]==bodies[9]
checks['portable_helper_machine_code_identical_on_both_cpus']='PASS'
Path('reports/remaining-tests.json').write_text(json.dumps({'base_rom_sha256':hashlib.sha256(original).hexdigest(),'cases_per_cpu':512,'tests':checks},indent=2)+'\n')
for k,v in checks.items():print(v,k)
