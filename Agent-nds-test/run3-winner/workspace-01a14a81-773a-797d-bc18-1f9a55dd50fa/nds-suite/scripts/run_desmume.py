#!/usr/bin/env python3
"""Isolated per-group emulator runs. Never converts missing completion into PASS."""
import argparse,json,os,struct,subprocess,sys,tempfile,time,hashlib,importlib.metadata
import xml.etree.ElementTree as ET
from pathlib import Path
from trace_layout import VERSION
from scoring import decode_score,aggregate
from diagnostics import extract,restore,summarize
ROOT=Path(__file__).resolve().parents[1]
GROUPS=json.loads((ROOT/'src/groups.json').read_text());GROUP_COUNT=len(GROUPS)
def group_selector(value):
 try:
  n=int(value,0)
  if not 0<=n<GROUP_COUNT:raise argparse.ArgumentTypeError('Group ID outside current registry')
  return n
 except ValueError:
  for group in GROUPS:
   if group['name'].lower()==value.lower():return group['id']
  raise argparse.ArgumentTypeError('Unknown group name or numeric ID')
MAGIC=struct.pack('<II',0x53454c31,0x4e445341)
def child(args):
 os.environ.setdefault('SDL_VIDEODRIVER','dummy'); os.environ.setdefault('SDL_AUDIODRIVER','dummy')
 from desmume.emulator import DeSmuME
 data=bytearray(Path(args.rom).read_bytes()); pos=data.find(MAGIC)
 if pos<0 or data.find(MAGIC,pos+1)>=0: raise ValueError('selector marker missing or ambiguous')
 struct.pack_into('<I',data,pos+8,args.child)
 if args.seed:
  seed=json.loads(Path(args.seed).read_text())
  marker=struct.pack('<II',0x52534d35,0x4e445353);off=data.find(marker)
  if off<0 or data.find(marker,off+1)>=0:raise ValueError('Resume marker ambiguous')
  values=[0x52534d35,0x4e445353,seed['start'],1,*seed['counters'],*seed['ledger']]
  if seed.get('group_schema')!=3 or seed.get('base_rom_sha256')!=hashlib.sha256(Path(args.rom).read_bytes()).hexdigest():raise ValueError('Stale/incompatible resume seed')
  if len(values)!=8+2*GROUP_COUNT:raise ValueError('Invalid resume seed length')
  struct.pack_into('<'+'I'*len(values),data,off,*values)
 with tempfile.TemporaryDirectory() as d:
  rom=Path(d)/'suite.nds'; rom.write_bytes(data)
  emu=DeSmuME(); emu.open(str(rom)); emu.volume_set(0)
  result={'group_schema':3,'score_protocol':3,'trace_protocol':VERSION,'id':args.child,'status':'HOST_FRAME_TIMEOUT'}
  last_checkpoint=None
  restored=False;debug_folder=Path(args.trace_dir or str(Path(args.output).with_suffix(''))+'-debug')
  for frame in range(args.frames):
   emu.cycle(False)
   words=emu.memory.read(0x023f0000,0x023f0048,4,False)
   if args.seed and not restored and words[0]==0x4e445354 and words[2]==6:
    saved=json.loads(Path(args.seed).read_text()).get('diagnostics',{})
    result['diagnostic_restore']=restore(emu,saved);restored=True
   if args.checkpoint and words[0]==0x4e445354 and words[1]==3 and words[2]==1 and words[3]<GROUP_COUNT:
    ledger=emu.memory.read(0x023f0100,0x023f0100+8*GROUP_COUNT,4,False);active=words[3]
    states=ledger[::2]
    valid=states[active]==5 and all(x in (1,2,3,6,7) for x in states[:active]) and all(x==0 for x in states[active+1:])
    valid=valid and states.count(1)==words[5] and sum(x in (2,3,6,7) for x in states)==words[8]
    if valid and active!=last_checkpoint:
     snapshot={'active':active,'mailbox':words,'ledger':ledger,'counters':words[14:18],
       'rom_sha256':hashlib.sha256(data).hexdigest(),'diagnostics':extract(emu,debug_folder)}
     dest=Path(args.checkpoint);tmp=dest.with_suffix('.tmp');tmp.write_text(json.dumps(snapshot,indent=2)+'\n');os.replace(tmp,dest)
     last_checkpoint=active
   if words[0]==0x4e445354 and words[1]==3 and words[2] in (3,4,5):
    result.update(status={3:'FAIL',4:'ROM_TIMEOUT',5:'PASS'}[words[2]],assertion=words[4],completed=words[5],frame=frame,mailbox=words)
    break
  result['diagnostics']=extract(emu,debug_folder)
  result['breakdown']=summarize(result['diagnostics'])
  result['rom_sha256']=hashlib.sha256(data).hexdigest()
  result['last_mailbox']=words
  if result['status'] in ('PASS','FAIL','ROM_TIMEOUT'):
   try:
    result['score']=decode_score(words)
    expected=words[6] if args.child==0xffffffff else 1
    if words[7]!=expected or words[6]!=len(json.loads((ROOT/'src/groups.json').read_text())):
     raise ValueError('Wrong test plan size')
    if result['status']=='PASS' and (words[9] or words[8]):result['status']='FAIL'
   except ValueError as e:result.update(status='PROTOCOL_ERROR',protocol_error=str(e))
  if result['status'] in ('PASS','FAIL','ROM_TIMEOUT'):
   # Wait for both physical LCD scanouts, then exercise real DS button input.
   emu.cycle(False);emu.cycle(False)
   if args.detail_page:
    from desmume.controls import Keys,keymask
    for _ in range(args.detail_page):
     emu.input.keypad_add_key(keymask(Keys.KEY_RIGHT));emu.cycle(False);emu.cycle(False)
     emu.input.keypad_rm_key(keymask(Keys.KEY_RIGHT));emu.cycle(False);emu.cycle(False)
   if args.verify_navigation:
    from desmume.controls import Keys,keymask
    trace=[]
    for key,expected_page in [(Keys.KEY_LEFT,(args.detail_page+11)%12),(Keys.KEY_RIGHT,args.detail_page),
                              (Keys.KEY_RIGHT,(args.detail_page+1)%12),(Keys.KEY_LEFT,args.detail_page)]:
     emu.input.keypad_add_key(keymask(key));emu.cycle(False);emu.cycle(False)
     emu.input.keypad_rm_key(keymask(key));emu.cycle(False);emu.cycle(False)
     actual=emu.memory.read(0x023f0088,0x023f0088,4,False)
     if actual!=expected_page:raise RuntimeError('Page navigation mismatch')
     trace.append(actual)
    result['navigation_trace']=trace
   if args.debug_keys:
    from desmume.controls import Keys,keymask
    result['debug_navigation']=[]
    for keyname in args.debug_keys.split(','):
     key={'UP':Keys.KEY_UP,'DOWN':Keys.KEY_DOWN,'A':Keys.KEY_A,'B':Keys.KEY_B}[keyname]
     emu.input.keypad_add_key(keymask(key));emu.cycle(False);emu.cycle(False)
     emu.input.keypad_rm_key(keymask(key));emu.cycle(False);emu.cycle(False)
     result['debug_navigation'].append(emu.memory.read(0x023f0094,0x023f00a4,4,False))
   result['ui']=emu.memory.read(0x023f0080,0x023f0094,4,False)
   tilemap=emu.memory.read(0x0620f800,0x0620fe00,2,False)
   result['bottom_text']=[''.join(chr(c&127) for c in tilemap[y*32:y*32+32]).rstrip() for y in range(24)]
   result['post_ui_mailbox']=emu.memory.read(0x023f0000,0x023f0048,4,False)
   record_words=emu.memory.read(0x023f0100,0x023f0100+8*GROUP_COUNT,4,False)
   result['group_results']=[{'id':i,'state':record_words[2*i],'assertion':record_words[2*i+1]} for i in range(GROUP_COUNT)]
  if args.screenshot:emu.screenshot().save(args.screenshot)
  observations={}
  for n,info in json.loads((ROOT/'artifacts/observations.json').read_text()).items():
   addr,size=info['address'],info['bytes']; width=2 if size==2 else 4
   observations[n]=emu.memory.read(addr,addr+size,width,False)
  observations['arm7_boot']=emu.memory.read(0x02000004,0x02000020,4,False)
  result['startup_observations']=observations
  result['cpu7_mailbox_scope']='last completed ARM7 ISA group; use score.cpu7 for aggregate counters'
  result['cpu7_mailbox']=emu.memory.read(0x023ef000,0x023ef020,4,False)
  result['pc9']=emu.memory.register_arm9[15]
  Path(args.output).write_text(json.dumps(result,indent=2)+'\n')
  emu.destroy()
def main():
 p=argparse.ArgumentParser();p.add_argument('--list-groups',action='store_true');p.add_argument('--trace-dir');p.add_argument('--debug-keys',default='');p.add_argument('--no-restart',action='store_true');p.add_argument('--seed');p.add_argument('--checkpoint');p.add_argument('--verify-navigation',action='store_true');p.add_argument('--detail-page',type=int,choices=range(12),default=0);p.add_argument('--sequential',action='store_true');p.add_argument('--screenshot');p.add_argument('--rom',default=str(ROOT/'suite.nds'));p.add_argument('--frames',type=int,default=600);p.add_argument('--timeout',type=float,default=30);p.add_argument('--child',type=int);p.add_argument('--group',type=group_selector);p.add_argument('--output',default=str(ROOT/'reports/desmume.json'));a=p.parse_args()
 if a.list_groups:
  print(json.dumps({'schema':3,'groups':GROUPS},indent=2));return
 if a.child is not None: child(a);return
 if a.sequential and a.group is not None:p.error('--sequential and --group cannot be combined')
 if a.sequential and not a.no_restart:
  cmd=[sys.executable,str(ROOT/'scripts/run_resilient.py'),'--rom',a.rom,'--frames',str(a.frames),'--timeout',str(a.timeout),'--output',a.output,'--detail-page',str(a.detail_page)]
  if a.screenshot:cmd+=['--screenshot',a.screenshot]
  if a.verify_navigation:cmd+=['--verify-navigation']
  sys.exit(subprocess.call(cmd))
 groups=json.loads((ROOT/'src/groups.json').read_text());results=[]
 if a.sequential:groups=[{'id':0xffffffff,'name':'Sequential ROM run'}]
 if a.screenshot and not (a.sequential or a.group is not None):p.error('--screenshot requires --group or --sequential')
 for g in groups:
  if a.group is not None and g['id']!=a.group:continue
  with tempfile.TemporaryDirectory() as d:
   output=Path(d)/'result.json'
   cmd=[sys.executable,__file__,'--child',str(g['id']),'--rom',a.rom,'--frames',str(a.frames),'--output',str(output)]
   cmd+=['--detail-page',str(a.detail_page),'--trace-dir',str(Path(a.output).resolve().with_suffix('')/f'group-{g["id"]}-debug')]
   if a.verify_navigation:cmd+=['--verify-navigation']
   if a.screenshot:cmd+=['--screenshot',a.screenshot]
   try:
    r=subprocess.run(cmd,capture_output=True,text=True,timeout=a.timeout)
    (ROOT/f"reports/desmume-{g['id']:02}.log").write_text(r.stdout+r.stderr)
    result=json.loads(output.read_text()) if r.returncode==0 and output.exists() else {'status':'EMULATOR_CRASH' if r.returncode<0 else 'HARNESS_ERROR','returncode':r.returncode}
   except subprocess.TimeoutExpired:result={'status':'HOST_WALL_TIMEOUT'}
   result.update(g);results.append(result);print(g['id'],g['name'],result['status'],flush=True)
 score=results[0].get('score',{}) if a.sequential and results else aggregate(results)
 print('SCORE:',json.dumps(score),flush=True)
 Path(a.output).write_text(json.dumps({'group_schema':3,'score_protocol':3,'trace_protocol':VERSION,'score':score,'backend':'py-desmume '+importlib.metadata.version('py-desmume'),'base_rom_sha256':hashlib.sha256(Path(a.rom).read_bytes()).hexdigest(),'isolated_boot':not a.sequential,'results':results},indent=2)+'\n')
 root=ET.Element('testsuite',name='suite.nds',tests=str(len(results)),failures=str(sum(r['status']!='PASS' for r in results)))
 for r in results:
  case=ET.SubElement(root,'testcase',name=r['name'],classname='nds')
  if r['status']!='PASS': ET.SubElement(case,'failure',type=r['status']).text=json.dumps(r)
 ET.ElementTree(root).write(str(Path(a.output).with_suffix('.xml')),encoding='utf-8',xml_declaration=True)
 sys.exit(0 if results and all(r['status']=='PASS' for r in results) else 1)
if __name__=='__main__':main()
