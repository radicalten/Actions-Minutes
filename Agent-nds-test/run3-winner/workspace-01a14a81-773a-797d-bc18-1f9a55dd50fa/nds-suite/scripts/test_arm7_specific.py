#!/usr/bin/env python3
"""Prove dispatch and attribution on ARM7, without calling known mismatches passes."""
import hashlib,json,os,struct,subprocess,sys,tempfile
from pathlib import Path
from diagnostics import cpu_rows
R=Path(__file__).resolve().parents[1];os.chdir(R)
manifest=json.loads(Path('src/generated/debug-manifest.json').read_text());vectors=manifest['arm7_vectors'];original=Path('suite.nds').read_bytes()
nm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-nm'
def symbols(cpu):
 lines=subprocess.check_output([str(nm),f'artifacts/arm{cpu}.elf'],text=True).splitlines()
 return {s[2]:int(s[0],16) for line in lines if len(s:=line.split())==3 and s[0][0] in '0123456789abcdef'}
s7,s9=symbols(7),symbols(9)
names={v['name'] for v in vectors[3390:]};assert len(names)==194
assert all(n in s7 and n not in s9 for n in names)
checks={'arm7_only_helpers_not_linked_on_arm9':'PASS'}
def run(rom,name,args):
 with tempfile.TemporaryDirectory() as d:
  path=Path(d)/'arm7.nds';path.write_bytes(rom);out=Path('reports')/(name+'.json')
  p=subprocess.run([sys.executable,'scripts/run_desmume.py','--rom',str(path),'--output',str(out),'--child','1',*args],capture_output=True,text=True,timeout=60)
  out.with_suffix('.log').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
  return json.loads(out.read_text())
r=run(original,'arm7-specific-baseline',['--detail-page','10','--debug-keys','B','--screenshot','reports/arm7-specific-screen.png'])
rows=cpu_rows(r['diagnostics']['arm7']);assert len(rows)==5174
assert all(v['function_pc']==s7[v['name']] and 0x02200000<=v['function_pc']<0x02300000 for v in rows)
assert all(v['status']=='PASS' for v in rows[:3390])
failed=[v for v in rows if v['status']=='FAIL'];assert len(failed)==23
assert r['score']['cpu7']=={'passed':5151,'failed':23}
assert r['debug_navigation'][0][2]==3412
assert any('022' in line for line in r['bottom_text'])
checks['every_case_matches_arm7_elf_entry_address']='PASS';checks['all_1784_specific_cases_executed_and_23_mismatches_retained']='PASS';checks['arm7_explorer_selects_real_failure']='PASS'
# A passing new vector is deliberately broken in the ARM7 payload only.
v=vectors[3390];pattern=struct.pack('<7I',s7[v['name']],*[v[k] for k in ('a','b','initial','result','flags','mask')])
rom=bytearray(original);assert rom.count(pattern)==1;pos=rom.find(pattern)
off,entry,base,size=struct.unpack_from('<4I',rom,0x30);assert off<=pos<off+size
struct.pack_into('<I',rom,pos+16,v['result']^1)
r=run(rom,'arm7-specific-injected',[]);bad=[v['id'] for v in cpu_rows(r['diagnostics']['arm7']) if v['status']=='FAIL']
assert bad==[3391]+[v['id'] for v in failed]
assert r['score']['cpu7']=={'passed':5150,'failed':24}
checks['arm7_payload_fault_injection_detected']='PASS'
# Hang only the first new ARM7 helper: shared prefix must be complete, new tail unrun.
rom=bytearray(original);address=s7[v['name']];struct.pack_into('<I',rom,off+address-base,0xeafffffe)
r=run(rom,'arm7-specific-interrupted',['--frames','60']);rows=cpu_rows(r['diagnostics']['arm7'])
assert r['status'] in ('ROM_TIMEOUT','HOST_FRAME_TIMEOUT')
assert all(x['status']=='PASS' for x in rows[:3390])
assert rows[3390]['status']=='INTERRUPTED' and all(x['status']=='NOT_RUN' for x in rows[3391:])
checks['arm7_only_hang_is_not_a_pass']='PASS'
report={'base_rom_sha256':hashlib.sha256(original).hexdigest(),'specific_vectors':1784,'specific_helpers':194,'reference_mismatches':23,'tests':checks,'failures':failed,'exact_qemu_arm7tdmi':'UNAVAILABLE','hardware_validation':'NOT_PERFORMED'}
Path('reports/arm7-specific-tests.json').write_text(json.dumps(report,indent=2)+'\n')
for key,status in checks.items():print(status,key)
