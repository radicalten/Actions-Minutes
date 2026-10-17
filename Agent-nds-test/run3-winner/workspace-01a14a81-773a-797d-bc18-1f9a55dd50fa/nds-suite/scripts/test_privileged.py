#!/usr/bin/env python3
"""Real ARM7 positive and negative coverage for privileged state and IRQ gates."""
import hashlib,json,os,struct,subprocess,sys,tempfile
from pathlib import Path
from diagnostics import cpu_rows
R=Path(__file__).resolve().parents[1];os.chdir(R)
original=Path('suite.nds').read_bytes();vectors=json.loads(Path('src/generated/debug-manifest.json').read_text())['arm7_vectors']
nm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-nm'
lines=subprocess.check_output([str(nm),'artifacts/arm7.elf'],text=True).splitlines();syms={s[2]:int(s[0],16) for l in lines if len(s:=l.split())==3 and s[0][0] in '0123456789abcdef'}
off,entry,base,size=struct.unpack_from('<4I',original,0x30)
def position(address):
 assert base<=address<base+size
 return off+address-base
def run(rom,name):
 with tempfile.TemporaryDirectory() as d:
  path=Path(d)/'injected.nds';path.write_bytes(rom);out=Path('reports')/(name+'.json')
  p=subprocess.run([sys.executable,'scripts/run_desmume.py','--child','1','--rom',str(path),'--output',str(out)],capture_output=True,text=True,timeout=60)
  out.with_suffix('.log').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
  r=json.loads(out.read_text());return r,cpu_rows(r['diagnostics']['arm7'])
r,rows=run(original,'privileged-baseline')
new=rows[3578:4630];assert len(new)==1052 and all(v['status']=='PASS' for v in new)
assert sum(v['name'].startswith('cpu_arm7_irq_entry_') for v in new)==96
assert r['score']['cpu7']=={'passed':5151,'failed':23}
checks={'all_1052_new_cases_pass_on_arm7':'PASS'}
labels=['bank_sp_','bank_lr_','bank_r8_','spsr_banks_','spsr_field_','cpsr_mode_','return_movs_','return_subs_','return_ldm_',
 'irq_entry_arm','irq_entry_thumb','irq_entry_user','irq_entry_masked_i','irq_entry_masked_ime','irq_entry_masked_ie']
rom=bytearray(original);ids=[]
for label in labels:
 v=next(v for v in vectors[3578:] if v['name'].startswith('cpu_arm7_'+label));ids.append(v['id'])
 pattern=struct.pack('<7I',syms[v['name']],*[v[k] for k in ('a','b','initial','result','flags','mask')]);assert rom.count(pattern)==1
 pos=rom.find(pattern);assert off<=pos<off+size;struct.pack_into('<I',rom,pos+16,v['result']^1)
r,rows=run(rom,'privileged-negative');assert [v['id'] for v in rows[3578:] if v['status']=='FAIL']==sorted(ids)
checks['fifteen_new_family_expectation_faults_detected']='PASS'
# Suppress only the IRQ callback's event counter, not the actual IRQ or User escape.
rom=bytearray(original);start=position(syms['cpu_arm7_timer_irq_handler']);pattern=struct.pack('<I',0xe2811001)
pos=rom.find(pattern,start,start+160);assert pos>=0
struct.pack_into('<I',rom,pos,0xe3a01000)
r,rows=run(rom,'privileged-irq-counter-negative');bad=[v for v in rows[3578:] if v['status']=='FAIL']
assert len(bad)==96 and all(v['name'].startswith('cpu_arm7_irq_entry_') for v in bad)
assert all(v['aux_masked_xor'] & 0x10000 for v in bad)
checks['missing_irq_event_counter_never_passes']='PASS'
# Release CPSR.I too early: the pre-release sample must catch the premature IRQ.
rom=bytearray(original);start=position(syms['cpu_arm7_irq_entry_masked_i']);pattern=struct.pack('<I',0xe38000df)
pos=rom.find(pattern,start,start+512);assert pos>=0
struct.pack_into('<I',rom,pos,0xe380005f)
r,rows=run(rom,'privileged-irq-early-negative');bad=[v for v in rows[3578:] if v['status']=='FAIL']
assert len(bad)==16 and all(v['name']=='cpu_arm7_irq_entry_masked_i' for v in bad)
assert all(v['aux_masked_xor'] & 0x40000 for v in bad)
checks['premature_irq_before_I_release_detected']='PASS'
Path('reports/privileged-tests.json').write_text(json.dumps({'base_rom_sha256':hashlib.sha256(original).hexdigest(),'new_vectors':1052,'portable_vectors':956,'nds_irq_vectors':96,'tests':checks,'injected_case_ids':ids},indent=2)+'\n')
for name,status in checks.items():print(status,name)
