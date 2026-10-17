#!/usr/bin/env python3
"""Instrument single-line C++ failure predicates exactly once; never infer unexecuted passes."""
import json,re
from pathlib import Path
R=Path(__file__).resolve().parents[1]
catalog=[]
for p in sorted((R/'work/rock/src9/tests').glob('*.cpp')):
 lines=p.read_text().splitlines()
 for index,line in enumerate(lines):
  m=re.search(r'\bif\s*\((.*)\)\s*fail_test\(([^;]+)\);',line)
  if not m:continue
  expression,identifier=m.groups()
  replacement=f'suite_assert((unsigned)({expression}), {identifier}, {index+1});'
  lines[index]=line[:m.start()]+replacement+line[m.end():]
  catalog.append(dict(source='vendor/rockwrestler/src9/tests/'+p.name,line=index+1,condition=expression,id_expression=identifier,original=line.strip()))
 p.write_text('\n'.join(lines)+'\n')
p=R/'work/rock/src9/inc.h';p.write_text(p.read_text()+'\nextern "C" void suite_assert(unsigned,unsigned,unsigned);\n')
(R/'src/generated/assertions.json').write_text(json.dumps({'schema':1,'instrumented':catalog,'uncovered':'Assembly checks and multiline/direct failure paths remain group-only; no per-assertion PASS inference.'},indent=2)+'\n')
print('Instrumented',len(catalog),'C++ assertion sites')
