#!/usr/bin/env python3
"""Execute real assertion-continuation, native crash, and hang recovery scenarios.
All injection ROMs are temporary copies; distributed ROM expectations unchanged.
"""
import json,os,struct,subprocess,sys,tempfile
from pathlib import Path
R=Path(__file__).resolve().parents[1];os.chdir(R)
nm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-nm'
lines=subprocess.check_output([str(nm),'artifacts/arm9.elf'],text=True).splitlines()
symbols={s[2]:int(s[0],16) for l in lines if len(s:=l.split())==3 and s[0][0] in '0123456789abcdef'}
original=bytearray(Path('suite.nds').read_bytes());off,entry,base,size=struct.unpack_from('<4I',original,0x20)
def rompos(address):
 assert base<=address<base+size
 return off+address-base
def fail_patch(rom,symbol):
 address=symbols[symbol];target=symbols['fail_test']
 struct.pack_into('<II',rom,rompos(address),0xe3a00055,0xea000000|(((target-(address+4)-8)//4)&0xffffff))
def execute(rom,name,resilient=False):
 with tempfile.TemporaryDirectory() as d:
  path=Path(d)/'injected.nds';path.write_bytes(rom)
  output=Path('reports')/(name+'.json');output.unlink(missing_ok=True)
  script='scripts/run_resilient.py' if resilient else 'scripts/run_desmume.py'
  cmd=[sys.executable,script,'--rom',str(path),'--output',str(output)]
  if not resilient:cmd+=['--child',str(0xffffffff)]
  p=subprocess.run(cmd,capture_output=True,text=True,timeout=120)
  output.with_suffix('.log').write_text(p.stdout+p.stderr)
  assert p.returncode in (0,1) and output.exists(),p.stderr
  report=json.loads(output.read_text());return report['results'][0] if resilient else report
# Test-only early assertions replace the two instructions that crash this native backend.
# This demonstrates the standalone ROM completes in one boot after ALL returned failures.
rom=bytearray(original)
for function in ['run_tests_ds_maths_div_64_32','run_tests_ds_maths_div_64_64']:
 # Resolve by function suffix without assuming C++ mangled length.
 match=next(n for n in symbols if function+'v' in n)
 fail_patch(rom,match)
r=execute(rom,'recovery-oneboot-assertions')
assert r['score']['complete'] and r['score']['unrun']==0
assert r['group_results'][17]['state']==2 and r['group_results'][18]['state']==1
assert r['group_results'][22]=={'id':22,'state':2,'assertion':0x55}
assert r['group_results'][23]=={'id':23,'state':2,'assertion':0x55}
assert r['group_results'][25]['state']==1 and r['group_results'][26]['state']==2
print('PASS: single boot continues returned IPC/math/TCM assertions')
# Force WRAM's first successful check to fail AFTER ARM7 entered its RW service.
wram=symbols[next(n for n in symbols if 'run_tests_wramcnt' in n)]
patched=bytearray(rom);branch=None
# Replace the instruction immediately before WRAM's first suite_assert call
# with MOV r0,#1. Its failure predicate is normally zero; ARM7 is already in RW.
for address in range(wram,wram+512,4):
 opcode=struct.unpack_from('<I',patched,rompos(address))[0]
 if opcode&0xff000000==0xeb000000:
  delta=(opcode&0xffffff);delta=delta-(1<<24) if delta&(1<<23) else delta
  if address+8+delta*4==symbols['suite_assert']:
   branch=address;struct.pack_into('<I',patched,rompos(address-4),0xe3a00001);break
assert branch is not None
r=execute(patched,'recovery-arm7-rw')
assert r['group_results'][24]['state']==2 and r['group_results'][24]['assertion']==0
assert r['group_results'][25]['state']==1,'ARM7 left stuck in RW mode after failure'
assert r['score']['complete']
print('PASS: cooperative ARM7 reset from abandoned WRAM/RW test')
# An infinite ARM9 loop is not a returned assertion. Host must retain and skip it as TIME.
rom=bytearray(original);struct.pack_into('<I',rom,rompos(wram),0xeafffffe)
r=execute(rom,'recovery-host-hang',True)
assert r['group_results'][24]['state']==7
assert r['group_results'][25]['state']==1 and r['group_results'][26]['state']==2
assert r['score']['complete'] and r['score']['unrun']==0
print('PASS: host checkpoints survive two native crashes and an injected hard hang')
Path('reports/recovery-tests.json').write_text(json.dumps({'single_boot_assertions':'PASS','arm7_rw_reset':'PASS','native_crash_and_hang_resume':'PASS'},indent=2)+'\n')
