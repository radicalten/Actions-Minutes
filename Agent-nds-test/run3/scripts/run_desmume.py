#!/usr/bin/env python3
"""Isolated per-group emulator runs. Never converts missing completion into PASS."""
import argparse,json,os,struct,subprocess,sys,tempfile,time,hashlib,importlib.metadata
import xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MAGIC=struct.pack('<II',0x53454c31,0x4e445341)
def child(args):
 os.environ.setdefault('SDL_VIDEODRIVER','dummy'); os.environ.setdefault('SDL_AUDIODRIVER','dummy')
 from desmume.emulator import DeSmuME
 data=bytearray(Path(args.rom).read_bytes()); pos=data.find(MAGIC)
 if pos<0 or data.find(MAGIC,pos+1)>=0: raise ValueError('selector marker missing or ambiguous')
 struct.pack_into('<I',data,pos+8,args.child)
 with tempfile.TemporaryDirectory() as d:
  rom=Path(d)/'suite.nds'; rom.write_bytes(data)
  emu=DeSmuME(); emu.open(str(rom)); emu.volume_set(0)
  result={'id':args.child,'status':'HOST_FRAME_TIMEOUT'}
  for frame in range(args.frames):
   emu.cycle()
   words=emu.memory.read(0x023f0000,0x023f001c,4,False)
   if words[0]==0x4e445354 and words[1]==1 and words[2] in (3,4,5):
    result.update(status={3:'FAIL',4:'ROM_TIMEOUT',5:'PASS'}[words[2]],assertion=words[4],completed=words[5],frame=frame,mailbox=words)
    break
  result['rom_sha256']=hashlib.sha256(data).hexdigest()
  result['last_mailbox']=words
  if result['status']=='PASS' and (words[5]!=1 or words[6]!=25):
   result['status']='PROTOCOL_ERROR'
  observations={}
  for n,info in json.loads((ROOT/'artifacts/observations.json').read_text()).items():
   addr,size=info['address'],info['bytes']; width=2 if size==2 else 4
   observations[n]=emu.memory.read(addr,addr+size,width,False)
  observations['arm7_boot']=emu.memory.read(0x02000004,0x02000020,4,False)
  result['startup_observations']=observations
  result['cpu7_mailbox']=emu.memory.read(0x023ef000,0x023ef018,4,False)
  result['pc9']=emu.memory.register_arm9[15]
  Path(args.output).write_text(json.dumps(result,indent=2)+'\n')
  emu.destroy()
def main():
 p=argparse.ArgumentParser();p.add_argument('--rom',default=str(ROOT/'suite.nds'));p.add_argument('--frames',type=int,default=600);p.add_argument('--timeout',type=float,default=30);p.add_argument('--child',type=int);p.add_argument('--group',type=int);p.add_argument('--output',default=str(ROOT/'reports/desmume.json'));a=p.parse_args()
 if a.child is not None: child(a);return
 groups=json.loads((ROOT/'src/groups.json').read_text());results=[]
 for g in groups:
  if a.group is not None and g['id']!=a.group:continue
  with tempfile.TemporaryDirectory() as d:
   output=Path(d)/'result.json'
   cmd=[sys.executable,__file__,'--child',str(g['id']),'--rom',a.rom,'--frames',str(a.frames),'--output',str(output)]
   try:
    r=subprocess.run(cmd,capture_output=True,text=True,timeout=a.timeout)
    (ROOT/f"reports/desmume-{g['id']:02}.log").write_text(r.stdout+r.stderr)
    result=json.loads(output.read_text()) if r.returncode==0 and output.exists() else {'status':'EMULATOR_CRASH' if r.returncode<0 else 'HARNESS_ERROR','returncode':r.returncode}
   except subprocess.TimeoutExpired:result={'status':'HOST_WALL_TIMEOUT'}
   result.update(g);results.append(result);print(g['id'],g['name'],result['status'],flush=True)
 Path(a.output).write_text(json.dumps({'backend':'py-desmume '+importlib.metadata.version('py-desmume'),'base_rom_sha256':hashlib.sha256(Path(a.rom).read_bytes()).hexdigest(),'isolated_boot':True,'results':results},indent=2)+'\n')
 root=ET.Element('testsuite',name='suite.nds',tests=str(len(results)),failures=str(sum(r['status']!='PASS' for r in results)))
 for r in results:
  case=ET.SubElement(root,'testcase',name=r['name'],classname='nds')
  if r['status']!='PASS': ET.SubElement(case,'failure',type=r['status']).text=json.dumps(r)
 ET.ElementTree(root).write(str(Path(a.output).with_suffix('.xml')),encoding='utf-8',xml_declaration=True)
 sys.exit(0 if results and all(r['status']=='PASS' for r in results) else 1)
if __name__=='__main__':main()
