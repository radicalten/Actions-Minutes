"""Binary execution evidence. Never synthesize PASS from a static coverage manifest."""
import hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MANIFEST=json.loads((ROOT/'src/generated/debug-manifest.json').read_text())
VECTORS=MANIFEST['vectors']
ASSEMBLY={}
for source in ('src/generated/cpu.s','src/generated/v5.s','src/generated/arm7.s'):
 for line,text in enumerate((ROOT/source).read_text().splitlines(),1):
  if text.startswith('cpu_') and text.endswith(':'):ASSEMBLY[text[:-1]]=f'{source}:{line}'
from trace_layout import BASES,CAPACITY,VERSION
def resolve_path(value):
 path=Path(value);return path if path.is_absolute() else ROOT/path
def read_words(emu,base,count):return emu.memory.read(base,base+4*count,4,False)
def extract(emu,folder):
 folder=Path(folder).resolve();folder.mkdir(parents=True,exist_ok=True);found={}
 for name,base in BASES.items():
  head=read_words(emu,base,16)
  if name=='assertions':
   if head[:2]!=[0x41535235,1] or not 0<=head[3]<=4096:continue
   length=16+5*head[3]
  else:
   if head[0]!=0x43545235 or head[1]!=VERSION or head[9]!=40 or head[10]!=MANIFEST['crc32']:continue
   expected=MANIFEST['arm9_count' if name=='arm9' else 'arm7_count']
   if head[3]!=expected:continue
   if head[3]>CAPACITY or head[12]:raise ValueError('Trace capacity/protocol error')
   length=16+10*head[3]
  words=read_words(emu,base,length);data=struct.pack('<'+'I'*length,*words)
  digest=hashlib.sha256(data).hexdigest();path=folder/(name+'-'+digest+'.bin')
  if not path.exists():
   temp=path.with_suffix('.tmp');temp.write_bytes(data);temp.replace(path)
  found[name]={'path':str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path),'sha256':digest,'header':head}
 return found

def restore(emu,saved):
 # Validate the entire restore set before writing any emulator memory.
 for name,entry in saved.items():
  if name not in BASES:raise ValueError('Unknown diagnostic target')
  data=checked_blob(entry)
  if name=='assertions':
   if len(data)<64 or len(data)%4:raise ValueError('Invalid assertion journal length')
   w=struct.unpack('<'+'I'*(len(data)//4),data)
   if w[0]!=0x41535235 or w[1]!=1 or w[2]!=4096 or w[3]>4096 or len(w)!=16+5*w[3]:raise ValueError('Invalid assertion journal')
  else:
   w=validate_cpu(data)
   if w[2]!=(7 if name=='arm7' else 9):raise ValueError('Trace CPU differs from restore target')
 restored={}
 for name,entry in saved.items():
  data=resolve_path(entry['path']).read_bytes()
  if hashlib.sha256(data).hexdigest()!=entry['sha256']:raise ValueError('Corrupt saved diagnostics')
  words=list(struct.unpack('<'+'I'*(len(data)//4),data));base=BASES[name]
  if name!='assertions':
   validate_cpu(data)
   if words[2]!=(7 if name=='arm7' else 9):raise ValueError('Trace CPU differs from restore target')
   if name=='arm7' and read_words(emu,base,16)[13]!=1:
    restored[name]='not_restored_live_arm7';continue
   words[11]=1 # this trace has imported records; provenance is per record
   for at in range(16,len(words),10):
    if words[at]&255:words[at]|=0x100
  if name=='assertions' and (len(words)<16 or words[0]!=0x41535235 or words[1]!=1 or words[2]!=4096 or words[3]>4096 or len(words)!=16+5*words[3]):raise ValueError('Invalid assertion journal')
  # Emulator is paused between frames, at the explicit RESTORE_WAIT gate.
  emu.memory.write(base,base+len(data),4,words);restored[name]='restored'
 emu.memory.write_long(0x023ed000,0x52455335)
 return restored

def checked_blob(entry):
 data=resolve_path(entry['path']).read_bytes()
 if hashlib.sha256(data).hexdigest()!=entry['sha256']:raise ValueError('Trace SHA256 mismatch')
 return data

def validate_cpu(data):
 if len(data)<64 or len(data)%4:raise ValueError('Invalid CPU trace byte length')
 words=struct.unpack('<'+'I'*(len(data)//4),data);h=words[:16]
 if h[0]!=0x43545235 or h[1]!=VERSION or h[2] not in (7,9) or h[9]!=40 or h[10]!=MANIFEST['crc32']:
  raise ValueError('Unknown CPU trace version/identity')
 expected=MANIFEST['arm7_count' if h[2]==7 else 'arm9_count']
 if h[3]!=expected or h[3]>CAPACITY or len(data)!=64+40*h[3] or h[12]:raise ValueError('Invalid CPU trace bounds/error')
 if h[14]>h[3] or h[15]>3 or h[13]>1:raise ValueError('Invalid trace plan')
 if h[4]!=h[5]+h[6] or h[4]>h[3] or h[7]>h[3] or h[8] not in (0,1) or h[11] not in (0,1):raise ValueError('Invalid CPU trace counters')
 vectors=MANIFEST['arm7_vectors'] if h[2]==7 else MANIFEST['vectors']
 selected=[bool(h[15]&(2 if v['isa']=='THUMB' else 1)) for v in vectors]
 if h[14]!=sum(selected):raise ValueError('Trace ISA plan differs from manifest')
 raw_states=words[16::10]
 if any(x&~0x103 or ((x&0x100) and (not h[11] or not (x&255))) for x in raw_states):raise ValueError('Invalid record state/provenance')
 states=tuple(x&255 for x in raw_states)
 if any(state and not include for state,include in zip(states,selected)):raise ValueError('Executed record outside selected ISA plan')
 if any(x>3 for x in states):raise ValueError('Invalid CPU record state')
 if h[8] and (h[7] or h[4]!=h[14] or states.count(2)!=h[5] or states.count(3)!=h[6]):raise ValueError('Incomplete completed trace')
 return words

def cpu_rows(entry):
 data=checked_blob(entry);words=validate_cpu(data);h=words[:16]
 if h[0]!=0x43545235 or h[10]!=MANIFEST['crc32']:raise ValueError('Unknown CPU trace')
 result=[]
 for i in range(h[3]):
  raw=words[16+10*i:26+10*i];historical=bool(raw[0]&0x100);r=(raw[0]&255,)+raw[1:];v=(MANIFEST['arm7_vectors'] if h[2]==7 else VECTORS)[i]
  status={0:'NOT_RUN',1:'INTERRUPTED',2:'PASS',3:'FAIL'}.get(r[0],'INVALID')
  selected=bool(h[15] & (2 if v['isa']=='THUMB' else 1))
  if not selected and not r[0]:status='NOT_SELECTED'
  item=dict(v,assembly_source=ASSEMBLY.get(v['name'],'unknown'),cpu='ARM'+str(h[2]),status=status,observed=r[1] if r[0] in (2,3) else None,observed_aux=r[2] if r[0] in (2,3) else None,function_pc=r[3] if r[0] else None,historical=historical)
  if r[0]:
   item.update(a=r[4],b=r[5],initial=r[6],result=r[7],flags=r[8],mask=r[9])
   item['runtime_differs_manifest']=any(item[k]!=v[k] for k in ('a','b','initial','result','flags','mask'))
  if r[0] in (2,3):
   item.update(result_xor=r[1]^r[7],aux_masked_xor=(r[2]^r[8])&r[9])
   item['primary_pass']=r[1]==r[7];item['aux_pass']=not item['aux_masked_xor'] if r[9] else None
   if (status=='PASS') != (item['primary_pass'] and not item['aux_masked_xor']):item['status']='TRACE_INCONSISTENT'
  result.append(item)
 return result

def summarize(found):
 output={}
 for name,entry in found.items():
  if name=='assertions':
   h=entry['header'];output[name]={'executed_predicates':h[3],'dropped_events':h[4],'granularity':'117 instrumented C++ sites; assembly/multiline checks not counted'};continue
  rows=cpu_rows(entry);families={}
  for r in rows:
   f=families.setdefault(r['name'],dict(name=r['name'],planned=0,passed=0,failed=0,not_run=0,interrupted=0,not_selected=0))
   f['planned']+=1
   key={'PASS':'passed','FAIL':'failed','NOT_RUN':'not_run','INTERRUPTED':'interrupted','NOT_SELECTED':'not_selected'}.get(r['status'],'failed');f[key]+=1
  output[name]={'selected':sum(r['status']!='NOT_SELECTED' for r in rows),'planned':len(rows),'passed':sum(r['status']=='PASS' for r in rows),'failed':sum(r['status']=='FAIL' for r in rows),
   'not_selected':sum(r['status']=='NOT_SELECTED' for r in rows),'not_run':sum(r['status']=='NOT_RUN' for r in rows),'interrupted':sum(r['status']=='INTERRUPTED' for r in rows),
   'primary_checks_executed':sum(r['status'] in ('PASS','FAIL') for r in rows),
   'aux_checks_executed':sum(r['status'] in ('PASS','FAIL') and bool(r['mask']) for r in rows),
   'failures':[r for r in rows if r['status'] in ('FAIL','TRACE_INCONSISTENT')], 'families':list(families.values())}
 categories={}
 for name in ('arm7','arm9'):
  if name not in found:continue
  for row in cpu_rows(found[name]):
   bucket=categories.setdefault(row['cpu_group'],{'group_id':row['group_id'],'selected':0,'planned':0,'passed':0,'failed':0,'not_run':0,'not_selected':0,'interrupted':0})
   bucket['planned']+=1
   bucket['selected']+=row['status']!='NOT_SELECTED'
   bucket[{'PASS':'passed','FAIL':'failed','NOT_RUN':'not_run','NOT_SELECTED':'not_selected','INTERRUPTED':'interrupted'}.get(row['status'],'failed')]+=1
 output['cpu_groups']=categories
 return output

GROUP_FILES={14:'ipcsync.cpp',15:'ipcfifo.cpp',16:'ipcfifo_irq.cpp',17:'sqrt.cpp',18:'sqrt.cpp',19:'division.cpp',20:'division.cpp',21:'division.cpp',22:'wramcnt.cpp',23:'vramcnt.cpp',24:'tcm.cpp'}
GROUP_FILES={k+2:v for k,v in GROUP_FILES.items()}
def assertion_rows(entry):
 data=checked_blob(entry);w=struct.unpack('<'+'I'*(len(data)//4),data)
 catalog=json.loads((ROOT/'src/generated/assertions.json').read_text())['instrumented']
 sites={(Path(s['source']).name,s['line']):s for s in catalog};rows=[]
 for i in range(w[3]):
  group,identifier,line,condition,status=w[16+5*i:21+5*i]
  site=sites.get((GROUP_FILES.get(group,''),line),{})
  rows.append(dict(event=i+1,group=group,id=identifier,line=line,observed_condition=condition,expected_condition=0,status='PASS' if status==1 else 'FAIL',**{k:v for k,v in site.items() if k!='line'}))
 return rows
