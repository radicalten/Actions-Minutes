"""Defined Thumb ALU edges and Rd==Rs aliases, including CPU-specific MUL flags."""
from register_operand2 import shifted,reference
M=0xffffffff
FLAGS=(0,0x20000000,0x90000000,0xf0000000)
PAIRS=((0,0),(1,1),(M,1),(0x7fffffff,1),(0x80000000,1),(0x80000000,M),(0x12345678,0x87654321),(M,M))
VALUES=(0,1,0x80000001,M)
COUNTS=(0,1,31,32,33,255,256,257)
def nz(value):return (value&0x80000000)|(int(value==0)<<30)
def extend_thumb_edges(asm,decl,rows,base,cpu):
 def helper(op,alias):
  n=f'cpu_arm{cpu}_thumb_edges_{op}_'+('alias' if alias else 'distinct')
  decl.append(f'extern "C" void {n}(unsigned,unsigned,unsigned,unsigned*);')
  rs='r0' if alias else 'r1'
  instruction=f'{op}s r0,{rs}'
  if op=='neg':instruction='rsbs r0,r1,#0'
  if op=='mul':instruction=f'muls r0,{rs},r0'
  asm.extend(['.arm','.balign 4',f'.global {n}',f'.type {n}, %function',n+':',
   'push {r4-r8,lr}','mov r4,r3','mrs r6,cpsr','adr r5,2f','adr r7,1f','orr r7,r7,#1',
   'msr cpsr_f,r2','bx r7','.thumb','.balign 4','1:',f'.global {n}_instruction',n+'_instruction:',
   instruction,'bx r5','.arm','.balign 4','2:','mrs r1,cpsr','str r0,[r4]','str r1,[r4,#4]',
   'msr cpsr_f,r6','pop {r4-r8,lr}','bx lr'])
  return n
 def row(n,a,b,f,result,aux,mask=0xf0000000):
  rows.append(dict(id=base+len(rows)+1,name=n,a=a,b=b,initial=f,result=result,flags=aux,mask=mask,
   aux_kind='NZCV' if mask==0xf0000000 else 'NZV; ARM7 MUL carry deliberately excluded',
   architecture='ARMv4T Thumb; ARM9 MUL preserves C, ARM7 MUL C unspecified',
   verification_scope='thumb_edges',source='https://mgba-emu.github.io/gbatek/#thumbopcodesregisteroperationsalubx'))
 for op in ('lsl','lsr','asr','ror'):
  n=helper(op,False)
  for a in VALUES:
   for b in COUNTS:
    for f in FLAGS:
     result,c=shifted(a,op,b,(f>>29)&1);row(n,a,b,f,result,nz(result)|(c<<29)|(f&0x10000000))
  n=helper(op,True)
  for a in (0,1,32,257):
   for f in FLAGS:
    result,c=shifted(a,op,a,(f>>29)&1);row(n,a,0,f,result,nz(result)|(c<<29)|(f&0x10000000))
 for op in ('adc','sbc'):
  for alias in (False,True):
   n=helper(op,alias)
   for a,b in PAIRS:
    for f in FLAGS:
     result,aux=reference(op,a,a if alias else b,'lsl',0,(f>>29)&1,(f>>28)&1);row(n,a,b,f,result,aux)
 n=helper('neg',False)
 for a,b in PAIRS:
  for f in FLAGS:
   result,aux=reference('sub',0,b,'lsl',0,1,0);row(n,a,b,f,result,aux)
 for alias in (False,True):
  n=helper('mul',alias)
  for a,b in PAIRS:
   for f in FLAGS:
    result=(a*(a if alias else b))&M;mask=0xf0000000 if cpu==9 else 0xd0000000
    row(n,a,b,f,result,(nz(result)|(f&0x30000000))&mask,mask)
