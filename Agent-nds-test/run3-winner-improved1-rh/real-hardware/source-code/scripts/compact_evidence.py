#!/usr/bin/env python3
"""Deduplicate immutable diagnostic blobs, retaining every JSON-referenced byte."""
import hashlib,json,os
from pathlib import Path
R=Path(__file__).resolve().parents[1];os.chdir(R)
pool=Path('reports/blobs');pool.mkdir(exist_ok=True);used=set();changed=0

def visit(x):
 global changed
 if isinstance(x,dict):
  for k,v in x.items():
   if k=='path' and isinstance(v,str) and v.endswith('.bin'):
    p=Path(v);assert p.is_file(),v
    data=p.read_bytes();digest=hashlib.sha256(data).hexdigest()
    if 'sha256' in x:assert x['sha256']==digest,(v,'hash mismatch')
    dest=pool/(digest+'.bin')
    if dest.exists():assert dest.read_bytes()==data
    else:dest.write_bytes(data)
    used.add(dest.resolve());x[k]=str(dest);changed+=v!=str(dest)
   else:visit(v)
 elif isinstance(x,list):
  for v in x:visit(v)
files=[]
for p in Path('reports').rglob('*.json'):
 obj=json.loads(p.read_text());visit(obj);files.append((p,obj))
for p,obj in files:p.write_text(json.dumps(obj,indent=2)+'\n')
removed=0
for p in Path('reports').rglob('*.bin'):
 if p.resolve() not in used:p.unlink();removed+=1
print(f'{changed} references pooled; {len(used)} unique blobs retained; {removed} redundant/unreferenced blobs removed')
