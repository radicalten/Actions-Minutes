#!/usr/bin/env python3
"""Boundary, corruption and restore preflight checks using actual v4 traces."""
import hashlib,json,struct,tempfile,subprocess
from pathlib import Path
from diagnostics import checked_blob,validate_cpu,restore,MANIFEST
from trace_layout import BASES,CAPACITY,VALUES
report=json.loads(Path('reports/development.json').read_text());entry=report['diagnostics']['arm9'];data=checked_blob(entry)
w=list(validate_cpu(data));assert w[3]==MANIFEST['arm9_count'] and w[3]>6552 and w[8]==1 and w[12]==0
assert BASES['arm9']+64+40*CAPACITY<=BASES['arm7']
assert BASES['arm7']+64+40*CAPACITY<=BASES['assertions']
checks={'actual_trace_exceeds_old_capacity_without_truncation':'PASS','layout_nonoverlap':'PASS'}
for label,field,value in [('version',1,3),('cpu',2,8),('count',3,CAPACITY+1),('width',9,36),('fingerprint',10,w[10]^1),('runtime_error',12,1),('completed_count',4,w[4]-1),('active_id',7,CAPACITY+1),('state',16,5),('state_unknown_bit',16,0x202),('empty_historical',16,0x100),('plan_count',14,w[14]-1),('plan_mask',15,0)]:
 x=w.copy();x[field]=value
 try:validate_cpu(struct.pack('<'+'I'*len(x),*x))
 except ValueError:checks[label+'_rejected']='PASS'
 else:raise AssertionError(label)
for label,d in [('short',b'bad'),('truncated',data[:-40]),('extra',data+b'\0'*40)]:
 try:validate_cpu(d)
 except ValueError:checks[label+'_rejected']='PASS'
 else:raise AssertionError(label)
x=w.copy();x[11]=0;x[16]=(x[16]&255)|0x100
try:validate_cpu(struct.pack('<'+'I'*len(x),*x))
except ValueError:checks['history_without_header_rejected']='PASS'
else:raise AssertionError('history_without_header')
class NeverWrite:
 def __getattr__(self,name):raise AssertionError('Restore touched emulator before validation: '+name)
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'bad.bin';x=w.copy();x[1]=1;p.write_bytes(struct.pack('<'+'I'*len(x),*x))
 bad={'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
 try:restore(NeverWrite(),{'arm9':entry,'arm7':bad})
 except ValueError:checks['entire_restore_validated_before_any_write']='PASS'
 else:raise AssertionError('unsafe restore')
subprocess.run(['g++','-std=c++17','-Wno-int-to-pointer-cast','tests/trace_bounds.cpp','src/trace.cpp','-o','artifacts/trace-bounds'],check=True)
subprocess.run(['artifacts/trace-bounds'],check=True)
checks['actual_runtime_capacity_boundary_guard_page']='PASS'
Path('reports/trace-protocol-tests.json').write_text(json.dumps({'tests':checks},indent=2)+'\n')
for k,v in checks.items():print(v,k)
