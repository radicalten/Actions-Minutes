#!/usr/bin/env python3
"""Deterministic ALU smoke vectors; not an exhaustive ISA test generator."""
from pathlib import Path
import json,re,zlib
ROOT=Path(__file__).resolve().parents[1]
D=ROOT/'src/generated'; D.mkdir(parents=True,exist_ok=True)
M=0xffffffff
seeds=[(0,0),(1,1),(M,1),(0x7fffffff,1),(0x80000000,1),(0x80000000,M),(0x12345678,0x87654321),(M,M)]
ops=['and','eor','sub','rsb','add','adc','sbc','rsc','tst','teq','cmp','cmn','orr','mov','bic','mvn','mul']
thumb=['and','eor','sub','add','adc','sbc','tst','cmp','cmn','orr','bic','mvn','mul','neg','lsl','lsr','asr','ror']
def expect(op,a,b,c):
    carry=c; v=1; mask=0xf0000000
    if op in ('add','adc','cmn','sub','cmp','rsb','sbc','rsc','neg'):
        x,y=a,b; k=0
        if op in ('sub','cmp','sbc'): y=(~b)&M; k=c if op=='sbc' else 1
        elif op in ('rsb','rsc'): x=b; y=(~a)&M; k=c if op=='rsc' else 1
        elif op=='neg': x=0; y=(~b)&M; k=1
        elif op=='adc': k=c
        full=x+y+k; r=full&M; carry=int(full>M); v=int(bool((~(x^y)&(x^r))&0x80000000))
    elif op in ('and','tst'): r=a&b
    elif op in ('eor','teq'): r=a^b
    elif op=='orr': r=a|b
    elif op=='bic': r=a&(~b&M)
    elif op=='mvn': r=(~b)&M
    elif op=='mov': r=b
    elif op=='mul': r=(a*b)&M; mask=0xc0000000
    else:
        n=b&255; r=a
        if n:
            if op=='lsl': r=(a<<n)&M if n<32 else 0; carry=(a>>(32-n))&1 if n<=32 else 0
            if op=='lsr': r=a>>n if n<32 else 0; carry=(a>>(n-1))&1 if n<=32 else 0
            if op=='asr': s=a if a<0x80000000 else a-2**32; r=(s>>min(n,32))&M; carry=(a>>(min(n,32)-1))&1
            if op=='ror': n%=32; r=((a>>n)|(a<<(32-n)))&M if n else a; carry=r>>31
    flags=((r>>31)<<31)|(int(r==0)<<30)|(carry<<29)|(v<<28)
    return (a if op in ('tst','teq','cmp','cmn') else r),flags,mask
asm=['.syntax unified','.arch armv4t','.text']
rows=[]; decl=[]
for state,operations in [('arm',ops),('thumb',thumb)]:
 for op in operations:
    name=f'cpu_{state}_{op}'; decl.append(f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);')
    asm+=['.arm','.align 2',f'.global {name}',f'.type {name}, %function',f'{name}:','push {r4,lr}','mov r4,r3','msr cpsr_f,r2']
    if state=='thumb': asm += ['adr r3,1f+1','bx r3','.thumb','.align 2','1:']
    if op in ('tst','teq','cmp','cmn'): ins=f'{op} r0,r1'
    elif op in ('mov','mvn'): ins=f'{op}s r0,r1'
    elif op=='neg': ins='rsbs r0,r1,#0'
    elif op=='mul': ins='muls r0,r1,r0' if state=='thumb' else 'muls r2,r0,r1\nmov r0,r2'
    elif state=='thumb' and op not in ('add','sub'): ins=f'{op}s r0,r1'
    else: ins=f'{op}s r0,r0,r1'
    asm += [ins]
    if state=='thumb': asm+=['nop','bx pc','nop','.arm','.align 2']
    asm+=['mrs r1,cpsr','str r0,[r4]','str r1,[r4,#4]','pop {r4,lr}','bx lr']
    pairs=seeds if op not in ('lsl','lsr','asr','ror') else [(0x81234567,n) for n in (0,1,31,32,33,255,256,257)]
    for a,b in pairs:
     for c in (0,1):
      result,flags,mask=expect(op,a,b,c)
      rows.append(dict(id=len(rows)+1,name=name,a=a,b=b,initial=(c<<29)|0x10000000,result=result,flags=flags,mask=mask))
# Import all upstream carry table rows without GBA UI/interrupt assumptions.
# Only NZCV is compared: mode/IRQ control bits are platform startup state.
source=(ROOT/'vendor/mgba-suite-auto/src/carry.c').read_text()
table=source.split('static const struct CarryTest carryTests[] = {',1)[1].split('\n};',1)[0]
carry_count=0
for label,values in re.findall(r'\{ "([^"]+)", \{ ([^}]+) \} \}',table):
 values=[int(x.strip(),0)&M for x in values.split(',')]
 assert len(values)==9
 a,b,initial=values[:3]
 for j,op in enumerate(('adc','sbc','rsc')):
  result,flags=values[3+2*j:5+2*j]
  rows.append(dict(id=len(rows)+1,name='cpu_arm_'+op,a=a,b=b,initial=initial,result=result,flags=flags,mask=0xf0000000,source='mgba-suite-auto/carry.c',upstream_case=label))
  carry_count+=1
assert carry_count==93, f'Upstream carry table changed: {carry_count}'
from cpu_extensions import extend
extend(asm,decl,rows,expect,seeds)
counts='#define CPU_VECTOR_COUNT "'+str(len(rows))+'"\n#define CPU_VECTOR_TOTAL '+str(len(rows))+'\n'
for state in ['arm','thumb']:
 subset=[v for v in rows if v['name'].startswith('cpu_'+state+'_')]
 counts+='#define CPU_'+state.upper()+'_VECTOR_TOTAL '+str(len(subset))+'\n'
 counts+='#define CPU_'+state.upper()+'_FUNCTION_TOTAL '+str(len({v['name'] for v in subset}))+'\n'
from generate_v5 import generate_v5
from generate_arm7 import generate_arm7
from emit_checker import emit
v5decl,v5rows=generate_v5(len(rows))
a7decl,a7rows=generate_arm7(len(rows))
from cpu_groups import classify,category,NAMES
for v in rows+v5rows+a7rows:
 v['isa'],v['isa_basis']=classify(v)
arm7rows=[dict(v,cpu_group=category(7,v['isa']),group_id=NAMES.index(category(7,v['isa']))) for v in rows+a7rows]
allrows=[dict(v,cpu_group=category(9,v['isa']),group_id=NAMES.index(category(9,v['isa']))) for v in rows+v5rows]
(D/'v5-vectors.json').write_text(json.dumps(allrows[len(rows):],indent=2)+'\n')
(D/'arm7-vectors.json').write_text(json.dumps(arm7rows[len(rows):],indent=2)+'\n')
from trace_layout import CAPACITY
assert max(len(allrows),len(arm7rows))<=CAPACITY, 'Trace slots exceed reserved region'
fingerprint=zlib.crc32(json.dumps({"arm9":allrows,"arm7":arm7rows},sort_keys=True).encode())
for cpu,items in [(7,arm7rows),(9,allrows)]:
 for isa in ('ARM','THUMB'):counts+=f'#define CPU{cpu}_{isa}_TOTAL {sum(v["isa"]==isa for v in items)}\n'
counts+='#define CPU9_VECTOR_TOTAL '+str(len(allrows))+'\n#define CPU7_VECTOR_TOTAL '+str(len(arm7rows))+'\n'
counts+='#define CPU_V5_VECTOR_TOTAL '+str(len(v5rows))+'\n#define CPU_MANIFEST_CRC '+hex(fingerprint)+'u\n'
(D/'count.h').write_text(counts)
(D/'cpu.s').write_text('\n'.join(asm)+'\n')
(D/'cpu.cpp').write_text(emit(decl,rows))
(D/'v5.cpp').write_text(emit(v5decl,v5rows,'v5'))
a7code=emit(a7decl,a7rows,'arm7')
(D/'arm7.cpp').write_text(a7code)
# ARM9 gets metadata only, with null helper pointers: ARM7 helpers are NOT linked there.
meta=a7code.split('extern "C" void cpu_trace_begin')[0]+a7code[a7code.index('extern "C" unsigned arm7_family_count'):]
for name in {v['name'] for v in a7rows}:meta=meta.replace('{'+name+',','{0,')
(D/'arm7meta.cpp').write_text(meta)
(D/'vectors.json').write_text(json.dumps(rows,indent=2)+'\n')
(D/'debug-manifest.json').write_text(json.dumps({'schema':3,'group_schema':3,'crc32':fingerprint,'shared_count':len(rows),'arm9_count':len(allrows),'arm7_count':len(arm7rows),'vectors':allrows,'arm7_vectors':arm7rows},indent=2)+'\n')
print(f'{len(rows)} shared + {len(v5rows)} ARM9-only + {len(a7rows)} ARM7-specific vectors generated')
