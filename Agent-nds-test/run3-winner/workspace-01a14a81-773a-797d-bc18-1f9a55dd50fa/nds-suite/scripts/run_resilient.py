#!/usr/bin/env python3
"""Checkpointed sequential execution; native crashes/hangs remain non-pass results.
Never suppresses faulting tests. Restarts a fresh emulator at the NEXT group,
restoring the accumulated ledger into an explicit, visibly labeled resume seed.
"""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
from trace_layout import VERSION
import xml.etree.ElementTree as ET
R=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--rom',default=str(R/'suite.nds'));p.add_argument('--frames',type=int,default=600);p.add_argument('--timeout',type=float,default=30)
 p.add_argument('--output',default=str(R/'reports/resilient.json'));p.add_argument('--screenshot');p.add_argument('--detail-page',type=int,choices=range(12),default=0);p.add_argument('--verify-navigation',action='store_true')
 a=p.parse_args();dest=Path(a.output);folder=dest.with_suffix('');folder.mkdir(parents=True,exist_ok=True)
 dest.unlink(missing_ok=True);dest.with_suffix('.xml').unlink(missing_ok=True)
 if a.screenshot:Path(a.screenshot).unlink(missing_ok=True)
 groups=json.loads((R/'src/groups.json').read_text());group_count=len(groups)
 seed=None;segments=[];final=None;minimum=0
 for boot in range(group_count+1):
  out=folder/f'boot-{boot:02}.json';cp=folder/f'checkpoint-{boot:02}.json';log=folder/f'boot-{boot:02}.log'
  # No stale checkpoint may authorize attributing a new crash to an old group.
  out.unlink(missing_ok=True);cp.unlink(missing_ok=True)
  cmd=[sys.executable,str(R/'scripts/run_desmume.py'),'--child',str(0xffffffff),'--rom',a.rom,'--frames',str(a.frames),'--output',str(out),'--checkpoint',str(cp),'--detail-page',str(a.detail_page)]
  if seed:cmd+=['--seed',str(seed)]
  if a.screenshot:cmd+=['--screenshot',a.screenshot]
  if a.verify_navigation:cmd+=['--verify-navigation']
  try:
   proc=subprocess.run(cmd,capture_output=True,text=True,timeout=a.timeout);log.write_text(proc.stdout+proc.stderr)
   code=proc.returncode
   result=json.loads(out.read_text()) if code==0 and out.exists() else {}
   if result.get('score',{}).get('complete') and result.get('status') in ('PASS','FAIL','ROM_TIMEOUT'):
    final=result;segments.append({'boot':boot,'outcome':'COMPLETED','worker':str(out.relative_to(R)) if out.is_relative_to(R) else str(out)});break
   if code<0:outcome='EMULATOR_CRASH';cause=-code
   elif result.get('status')=='HOST_FRAME_TIMEOUT':outcome='HOST_FRAME_TIMEOUT';cause=1
   else:raise RuntimeError(f'Worker/harness failure; not a recoverable test outcome: {code} {result.get("status")}')
  except subprocess.TimeoutExpired as e:
   parts=[x.encode() if isinstance(x,str) else (x or b'') for x in (e.stdout,e.stderr)];log.write_bytes(b''.join(parts))
   outcome='HOST_WALL_TIMEOUT';cause=2
  if not cp.exists():raise RuntimeError('No durable BEGIN checkpoint: cannot safely attribute or resume this failure')
  state=json.loads(cp.read_text());active=state['active']
  if not minimum<=active<group_count or state['ledger'][2*active]!=5:raise RuntimeError('Invalid/non-progressing checkpoint')
  state['ledger'][2*active]=6 if outcome=='EMULATOR_CRASH' else 7
  state['ledger'][2*active+1]=cause
  segments.append({'boot':boot,'outcome':outcome,'group':active,'cause':cause,'checkpoint':str(cp),'rom_sha256':state['rom_sha256']})
  minimum=active+1;seed=folder/f'seed-{boot+1:02}.json'
  seed.write_text(json.dumps({'group_schema':3,'base_rom_sha256':hashlib.sha256(Path(a.rom).read_bytes()).hexdigest(),'start':minimum,'ledger':state['ledger'],'counters':state['counters'],'diagnostics':{k:v for k,v in state.get('diagnostics',{}).items() if k!='arm7' or all(state['ledger'][2*i] in (1,2,3,6,7) for i in (0,1))}},indent=2)+'\n')
  print(f'{outcome}: group {active}; resuming at {minimum}',flush=True)
 if final is None:raise RuntimeError('Recovery limit exhausted')
 groups=json.loads((R/'src/groups.json').read_text());states={1:'PASS',2:'FAIL',3:'ROM_TIMEOUT',6:'EMULATOR_CRASH',7:'HOST_TIMEOUT'}
 ledger=[]
 for r,g in zip(final['group_results'],groups):ledger.append(dict(g,status=states.get(r['state'],'UNRESOLVED'),assertion_or_cause=r['assertion']))
 if any(r['status']=='UNRESOLVED' for r in ledger):raise RuntimeError('Terminal screen has unresolved ledger entries')
 report={'group_schema':3,'score_protocol':3,'trace_protocol':VERSION,'base_rom_sha256':hashlib.sha256(Path(a.rom).read_bytes()).hexdigest(),'execution':'sequential_with_checkpoint_restart','score':final['score'],
 'segments':segments,'groups':ledger,'diagnostics':final.get('diagnostics',{}),'breakdown':final.get('breakdown',{}),'results':[final],'backend':'py-desmume 0.0.9'}
 dest.write_text(json.dumps(report,indent=2)+'\n')
 root=ET.Element('testsuite',name='suite.nds.resilient',tests=str(group_count),failures=str(sum(g['status']=='FAIL' for g in ledger)),errors=str(sum(g['status'] not in ('PASS','FAIL') for g in ledger)))
 for g in ledger:
  e=ET.SubElement(root,'testcase',name=g['name'],classname='nds')
  if g['status']!='PASS':ET.SubElement(e,'failure' if g['status']=='FAIL' else 'error',type=g['status']).text=json.dumps(g)
 ET.ElementTree(root).write(str(dest.with_suffix('.xml')),encoding='utf-8',xml_declaration=True)
 print('FINAL SCORE:',json.dumps(final['score']),flush=True)
 return 0 if all(g['status']=='PASS' for g in ledger) else 1
if __name__=='__main__':sys.exit(main())
