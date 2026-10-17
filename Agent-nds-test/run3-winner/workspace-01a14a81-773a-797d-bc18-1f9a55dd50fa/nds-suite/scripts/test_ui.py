#!/usr/bin/env python3
"""Actual emulator integration: both LCDs, all detail pages, score invariance."""
import json,subprocess,sys
from pathlib import Path
from PIL import Image
R=Path(__file__).resolve().parents[1]
expected=['CPU GROUP DIAGNOSTICS','GROUPS 00-13','GROUPS 14-26','FAILURES 1-9','FAILURES 10-18','FAILURES 19-27',
 'BOOT SNAPSHOTS / NOT ASSERTIONS','ARM9 CP15 BOOT SNAPSHOT','COVERAGE / EXECUTION LIMITS','ARM9 VECTOR EXPLORER','ARM7 VECTOR EXPLORER','C++ ASSERTION EVENT JOURNAL']
result=[];top=None
for page,title in enumerate(expected):
 path=R/f'reports/ui-page-{page}.json';png=path.with_suffix('.png')
 cmd=[sys.executable,str(R/'scripts/run_resilient.py'),'--verify-navigation','--detail-page',str(page),'--output',str(path),'--screenshot',str(png)]
 p=subprocess.run(cmd,capture_output=True,text=True,timeout=90)
 path.with_suffix('.log').write_text(p.stdout+p.stderr);assert p.returncode in (0,1),p.stderr
 report=json.loads(path.read_text())['results'][0]
 assert report['navigation_trace']==[(page+11)%12,page,(page+1)%12,page]
 assert report['ui']==[0x55493244,1,page,12,1],report['ui']
 assert report['bottom_text'][0]==title,report['bottom_text']
 assert report['post_ui_mailbox']==report['last_mailbox'],'Navigation changed score'
 image=Image.open(png).convert('RGB');assert image.size==(256,384)
 upper=image.crop((0,0,256,192)).tobytes()
 if top is None:top=upper
 assert top==upper,'Top summary changed when paging the lower LCD'
 assert len(set(image.crop((0,192,256,384)).get_flattened_data()))>1,'Lower LCD blank'
 if page==1:assert 'FAIL' in report['bottom_text'][4]
 if page==2:assert 'FAIL' in report['bottom_text'][5]
 if page==3:assert any('00000025' in line for line in report['bottom_text'])
 assert sum(g['state']==1 for g in report['group_results'])==report['score']['passed']
 assert sum(g['state'] in (2,3,6,7) for g in report['group_results'])==report['score']['failed']
 result.append({'page':page,'title':title,'status':'PASS'})
 print('PASS page',page,title)
(R/'reports/ui-tests.json').write_text(json.dumps({'results':result,'score_preserved':True,'both_lcds_verified':True},indent=2)+'\n')
