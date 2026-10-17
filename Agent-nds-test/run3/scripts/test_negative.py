#!/usr/bin/env python3
"""Fault injection proves that ROM assertions and host timeout paths are active."""
import json,os,struct,subprocess,sys,tempfile
from pathlib import Path
R=Path(__file__).resolve().parents[1];os.chdir(R)
def run(args,output):
 r=subprocess.run([sys.executable,'scripts/run_desmume.py',*args,'--output',output],capture_output=True,text=True,timeout=30)
 (R/Path(output).with_suffix('.log')).write_text(r.stdout+r.stderr)
 if r.returncode:raise RuntimeError(r.stderr)
 return json.loads(Path(output).read_text())
r=run(['--child','999'],'reports/negative-selector.json')
assert r['status']=='FAIL' and r['assertion']==0xffffffff
r=run(['--child','0','--frames','1'],'reports/negative-frame.json')
assert r['status']=='HOST_FRAME_TIMEOUT'
nm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-nm'
symbols=subprocess.check_output([str(nm),'artifacts/arm9.elf'],text=True)
address=int(next(l.split()[0] for l in symbols.splitlines() if l.endswith(' cpu_arm_and')),16)
pattern=struct.pack('<7I',address,0,0,0x10000000,0,0x50000000,0xf0000000)
rom=bytearray(Path('suite.nds').read_bytes());assert rom.count(pattern)==1
pos=rom.find(pattern);struct.pack_into('<I',rom,pos+16,1)
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'fault.nds';p.write_bytes(rom)
 r=run(['--child','0','--rom',str(p)],'reports/negative-vector.json')
 assert r['status']=='FAIL' and r['assertion']==1
print('PASS: invalid selector, frame timeout, deliberately incorrect CPU expected value')
