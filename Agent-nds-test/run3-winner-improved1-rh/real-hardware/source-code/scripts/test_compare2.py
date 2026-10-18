#!/usr/bin/env python3
"""Execution/attribution and primary, NZCV and unintended-source-write fault injections on both CPUs."""
import hashlib,json,os,struct,subprocess,sys,tempfile
from pathlib import Path
from diagnostics import cpu_rows
R=Path(__file__).resolve().parents[1];os.chdir(R)
original=Path('suite.nds').read_bytes();manifest=json.loads(Path('src/generated/debug-manifest.json').read_text());checks={};bodies={}
nm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-nm'
def run(cpu,rom,label):
 with tempfile.TemporaryDirectory() as d:
  p=Path(d)/'fault.nds';p.write_bytes(rom);out=Path('reports')/f'compare2-{cpu}-{label}.json'
  r=subprocess.run([sys.executable,'scripts/run_desmume.py','--child',str(0 if cpu==7 else 2),'--rom',str(p),'--output',str(out)],capture_output=True,text=True,timeout=60)
  out.with_suffix('.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
  return cpu_rows(json.loads(out.read_text())['diagnostics']['arm'+str(cpu)])
for cpu,first,total in [(9,6243,13380),(7,4919,12056)]:
 vectors=manifest['vectors' if cpu==9 else 'arm7_vectors'];new=vectors[first-1:first+255];assert len(new)==256
 syms={s[2]:int(s[0],16) for l in subprocess.check_output([str(nm),f'artifacts/arm{cpu}.elf'],text=True).splitlines() if len(s:=l.split())==3 and s[0][0] in '0123456789abcdef'}
 off,_,base,size=struct.unpack_from('<4I',original,0x20 if cpu==9 else 0x30)
 def pos(address):
  assert base<=address<base+size
  return off+address-base
 baseline=run(cpu,original,'baseline');assert len(baseline)==total and all(baseline[v['id']-1]['status']=='PASS' and baseline[v['id']-1]['function_pc']==syms[v['name']] for v in new)
 baseline_bad={v['id'] for v in baseline if v['status']=='FAIL'};assert len(baseline_bad)==(224 if cpu==9 else 13)
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
 # Replace each test/compare by the equivalent flag-setting, Rn-writing ALU op.
 # Flags remain identical; only unintended source-register writes are detected.
 rom=bytearray(original)
 conversions={'tst':(8,0),'teq':(9,1),'cmp':(10,2),'cmn':(11,4)}
 for name in chosen:
  op=name.split('_')[2];old,new_op=conversions[op]
  address=pos(syms[name])+16;ins=struct.unpack_from('<I',rom,address)[0]
  assert ((ins>>21)&15)==old and ins&(1<<20)
  struct.pack_into('<I',rom,address,(ins&~(15<<21))|(new_op<<21))
 from register_operand2 import shifted
 expected=set()
 for v in new:
  op=v['name'].split('_')[2];kind=v['name'].split('_')[-1]
  a=v['a'];b,_=shifted(v['b'],kind,v['initial'],(v['initial']>>29)&1)
  value={'tst':a&b,'teq':a^b,'cmp':(a-b)&0xffffffff,'cmn':(a+b)&0xffffffff}[op]
  if value!=a:expected.add(v['id'])
 rows=run(cpu,rom,'source-write-negative')
 assert expected and {v['id'] for v in rows if v['status']=='FAIL'}==baseline_bad|expected
 assert all(rows[v['id']-1]['aux_masked_xor']==0 for v in new)
 checks[f'arm{cpu}_source_write_mutations_detected']='PASS'
 print('Unintended source-write failures:',cpu,len(expected))
assert bodies[7]==bodies[9]
checks['portable_helper_machine_code_identical_on_both_cpus']='PASS'
Path('reports/compare2-tests.json').write_text(json.dumps({'base_rom_sha256':hashlib.sha256(original).hexdigest(),'cases_per_cpu':256,'tests':checks},indent=2)+'\n')
for k,v in checks.items():print(v,k)
