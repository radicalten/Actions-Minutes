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
  p=Path(d)/'fault.nds';p.write_bytes(rom);out=Path('reports')/f'operand2-{cpu}-{label}.json'
  r=subprocess.run([sys.executable,'scripts/run_desmume.py','--child',str(cpu==7 and 1 or 0),'--rom',str(p),'--output',str(out)],capture_output=True,text=True,timeout=60)
  out.with_suffix('.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
  return cpu_rows(json.loads(out.read_text())['diagnostics']['arm'+str(cpu)])
for cpu,first,total in [(9,5987,6498),(7,4663,5174)]:
 vectors=manifest['vectors' if cpu==9 else 'arm7_vectors'];new=vectors[first-1:first+255];assert len(new)==256
 syms={s[2]:int(s[0],16) for l in subprocess.check_output([str(nm),f'artifacts/arm{cpu}.elf'],text=True).splitlines() if len(s:=l.split())==3 and s[0][0] in '0123456789abcdef'}
 off,_,base,size=struct.unpack_from('<4I',original,0x20 if cpu==9 else 0x30)
 def pos(address):
  assert base<=address<base+size
  return off+address-base
 baseline=run(cpu,original,'baseline');assert len(baseline)==total and all(v['status']=='PASS' and v['function_pc']==syms[v['name']] for v in baseline[first-1:])
 baseline_bad={v['id'] for v in baseline if v['status']=='FAIL'};assert len(baseline_bad)==(320 if cpu==9 else 23)
 checks[f'arm{cpu}_all_256_executed_at_correct_elf_addresses']='PASS'
 chosen={}
 for v in new:chosen.setdefault(v['name'],v)
 assert len(chosen)==16
 bodies[cpu]=[original[pos(syms[n]):pos(syms[n])+44] for n in chosen]
 for field,byte_offset,bit in [('primary',16,1),('nzcv',20,0x20000000)]:
  rom=bytearray(original)
  for v in chosen.values():
   pattern=struct.pack('<7I',syms[v['name']],*[v[k] for k in ('a','b','initial','result','flags','mask')]);assert rom.count(pattern)==1
   address=rom.find(pattern);struct.pack_into('<I',rom,address+byte_offset,v['result' if field=='primary' else 'flags']^bit)
  rows=run(cpu,rom,field+'-negative');assert {v['id'] for v in rows if v['status']=='FAIL'}==baseline_bad|{v['id'] for v in chosen.values()}
  checks[f'arm{cpu}_16_{field}_expectation_mutations_detected']='PASS'
 # Replace ADC by ADD and SBC by SUB, retaining S and exact shifted operand.
 rom=bytearray(original)
 for name in chosen:
  if '_adc_' not in name and '_sbc_' not in name:continue
  address=pos(syms[name])+16;ins=struct.unpack_from('<I',rom,address)[0]
  old,new_op=(5,4) if '_adc_' in name else (6,2)
  assert ((ins>>21)&15)==old and ins&(1<<20) and (ins&0xf)==1
  struct.pack_into('<I',rom,address,(ins&~(15<<21))|(new_op<<21))
 rows=run(cpu,rom,'carry-input-negative')
 expected={v['id'] for v in new if ('_adc_' in v['name'] and v['initial']&(1<<29)) or ('_sbc_' in v['name'] and not v['initial']&(1<<29))}
 assert len(expected)==64 and {v['id'] for v in rows if v['status']=='FAIL'}==baseline_bad|expected
 checks[f'arm{cpu}_64_lost_carry_input_cases_detected']='PASS'
assert bodies[7]==bodies[9]
checks['portable_helper_machine_code_identical_on_both_cpus']='PASS'
Path('reports/operand2-tests.json').write_text(json.dumps({'base_rom_sha256':hashlib.sha256(original).hexdigest(),'cases_per_cpu':256,'tests':checks},indent=2)+'\n')
for k,v in checks.items():print(v,k)
