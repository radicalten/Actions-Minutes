#!/usr/bin/env python3
"""Per-profile SP execution and actual instruction defect injections."""
import hashlib,json,os,struct,subprocess,sys,tempfile
from pathlib import Path
from diagnostics import cpu_rows
R=Path(__file__).resolve().parents[1];os.chdir(R)
original=Path('suite.nds').read_bytes();manifest=json.loads(Path('src/generated/debug-manifest.json').read_text());registry=json.loads(Path('src/groups.json').read_text());checks={};injections=[]
nm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-nm'
def bad(rows):return {v['id'] for v in rows if v['status']=='FAIL'}
for cpu in (7,9):
 group=next(g for g in registry if g['name']==f'ARM{cpu}Thumb');vectors=manifest['arm7_vectors' if cpu==7 else 'vectors'];cases=[v for v in vectors if v.get('verification_scope')=='thumb_pc'];assert len(cases)==216
 syms={s[2]:int(s[0],16) for l in subprocess.check_output([str(nm),f'artifacts/arm{cpu}.elf'],text=True).splitlines() if len(s:=l.split())==3 and s[0][0] in '0123456789abcdef'}
 off,_,base,size=struct.unpack_from('<4I',original,0x30 if cpu==7 else 0x20)
 def pos(address):
  assert base<=address<base+size
  return off+address-base
 def run(rom,label):
  with tempfile.TemporaryDirectory() as d:
   path=Path(d)/'fault.nds';path.write_bytes(rom);out=Path('reports')/f'thumb-pc-{cpu}-{label}.json'
   p=subprocess.run([sys.executable,'scripts/run_desmume.py','--child',str(group['id']),'--rom',str(path),'--output',str(out)],capture_output=True,text=True,timeout=60)
   out.with_suffix('.log').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
   r=json.loads(out.read_text());return cpu_rows(r['diagnostics'][f'arm{cpu}'])
 rows=run(original,'baseline');assert len(rows)==len(vectors) and len(bad(rows))==(10 if cpu==7 else 96)
 assert all(rows[v['id']-1]['status']=='PASS' and rows[v['id']-1]['function_pc']==syms[v['name']] for v in cases)
 assert all(v['status']=='NOT_SELECTED' for v in rows if v['isa']=='ARM')
 baseline_bad=bad(rows);chosen={}
 for v in cases:chosen.setdefault(v['name'],v)
 assert len(chosen)==54
 for n,v in chosen.items():
  address=syms[n+'_instruction'];assert address%4==v['instruction_alignment_mod4']
  assert struct.unpack_from('<2H',original,pos(address))==(v['expected_thumb_opcode'],0x4728)
  if v['operation']=='literal_ldr':assert struct.unpack_from('<I',original,pos(((address+4)&~3)+v['offset_bytes']))[0]==v['literal_value']
 checks[f'arm{cpu}_216_cases_54_encodings_and_literal_fixtures']='PASS'
 def select(op,alignment,offset,rd,value=0):return next(v for v in chosen.values() if (v['operation'],v['instruction_alignment_mod4'],v['offset_bytes'],v['destination_register'],v['b'])==(op,alignment,offset,rd,value))
 def ids(v):return {c['id'] for c in cases if c['name']==v['name']}
 def instruction(rom,v,code,delta=0):struct.pack_into('<H',rom,pos(syms[v['name']+'_instruction']&~1)+delta,code)
 def confirm(rom,label,expected,bit=None,primary_unchanged=False):
  actual=run(rom,label);assert bad(actual)==baseline_bad|expected,(cpu,label,sorted(bad(actual)-baseline_bad),sorted(expected))
  if bit is not None:assert all(actual[i-1]['aux_masked_xor']&bit for i in expected)
  if primary_unchanged:assert all(actual[i-1]['result_xor']==0 for i in expected)
  checks[f'arm{cpu}_{label}']='PASS';injections.append({'cpu':cpu,'scenario':label,'detected_ids':sorted(expected)})
 for field,shift,bit in [('result',16,1),('flags',20,0x10000000)]:
  rom=bytearray(original)
  for v in chosen.values():
   pattern=struct.pack('<7I',syms[v['name']],*[v[k] for k in ('a','b','initial','result','flags','mask')]);assert rom.count(pattern)==1
   struct.pack_into('<I',rom,rom.find(pattern)+shift,v[field]^bit)
  confirm(rom,'54_'+field+'_faults',{v['id'] for v in chosen.values()})
 for label,op,alignment,offset,rd,value,code in [('missing_literal','literal_ldr',0,4,0,0x80000001,0x46c0),('literal_scaled_offset','literal_ldr',2,4,0,0x80000001,0x4802),('literal_wrong_destination','literal_ldr',0,1020,1,0x12345678,0x48ff),('pc_replaced_with_sp','address_pc',2,1020,0,0,0xa8ff)]:
  v=select(op,alignment,offset,rd,value);rom=bytearray(original);instruction(rom,v,code);confirm(rom,label,ids(v))
Path('reports/thumb-pc-tests.json').write_text(json.dumps({'base_rom_sha256':hashlib.sha256(original).hexdigest(),'cases_per_cpu':216,'tests':checks,'injections':injections},indent=2)+'\n')
for k,v in checks.items():print(v,k)
