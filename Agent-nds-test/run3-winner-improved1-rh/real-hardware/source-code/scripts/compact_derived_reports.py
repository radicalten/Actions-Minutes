#!/usr/bin/env python3
"""Remove replayable CPU detail arrays from secondary reports, never raw observations.

Full primary development report is retained. Reconstruct a secondary report's
breakdown with diagnostics.summarize(report['diagnostics']) using matching source.
"""
import json
from pathlib import Path
R=Path(__file__).resolve().parents[1]
changed=[];saved=0
for p in (R/'reports').rglob('*.json'):
 if p.name=='development.json':continue
 before=p.stat().st_size;obj=json.loads(p.read_text());touched=False
 def visit(d):
  global touched
  if isinstance(d,dict):
   if isinstance(d.get('diagnostics'),dict) and isinstance(d.get('breakdown'),dict):
    for cpu in ('arm7','arm9'):
     if cpu not in d['diagnostics']:continue
     b=d['breakdown'].get(cpu,{})
     for field in ('families','failures'):
      if field in b:del b[field];touched=True
    if touched:d['derived_detail_storage']='CPU family/failure arrays omitted; rebuild via diagnostics.summarize(diagnostics). Hashed raw traces and aggregate outcomes retained.'
   for v in list(d.values()):visit(v)
  elif isinstance(d,list):
   for v in d:visit(v)
 visit(obj)
 if touched:
  p.write_text(json.dumps(obj,separators=(',',':'))+'\n');saved+=before-p.stat().st_size;changed.append(str(p.relative_to(R)))
(R/'reports/derived-storage-compaction.json').write_text(json.dumps({'bytes_saved':saved,'files':changed,'method':'Only replayable family/failure arrays removed from secondary reports; primary development report and raw traces unchanged'},indent=2)+'\n')
print(f'{len(changed)} secondary reports compacted; {saved} bytes saved')
