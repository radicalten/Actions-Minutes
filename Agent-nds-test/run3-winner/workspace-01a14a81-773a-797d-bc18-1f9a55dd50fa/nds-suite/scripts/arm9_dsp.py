"""ARMv5TE DSP edge matrices with an independent integer reference model."""
M=0xffffffff
PAIRS=[(0,0),(0xffffffff,0xffffffff),(0x7fffffff,1),(0x80000000,0xffffffff),
 (0x7fff8000,0x80007fff),(0x80008000,0x80008000),(0x40000000,0x40000000),(0x1234fedc,0xabcd5678)]
ACC=[0,0x7fffffff,0x80000000,0xffffffff]
FLAGS=[0,0xf0000000,0x08000000,0xf8000000]
def signed(x,bits=32):return x-(1<<bits) if x&(1<<(bits-1)) else x
def half(x,which):return signed((x>>(16 if which=='t' else 0))&65535,16)
def sat(x):return max(-(1<<31),min((1<<31)-1,x))
def extend_dsp(asm,decl,rows,base):
 def begin(name):
  name='cpu_arm9_'+name;decl.append(f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);')
  asm.extend(['.arm','.balign 4',f'.global {name}',f'.type {name}, %function',name+':','push {r4-r8,lr}','mov r4,r3'])
  return name
 def end():asm.extend(['str r0,[r4]','str r1,[r4,#4]','pop {r4-r8,lr}','bx lr','.ltorg'])
 def row(n,a,b,initial,result,aux,kind='NZCVQ',mask=0xf8000000):
  rows.append(dict(id=base+len(rows)+1,name=n,a=a,b=b,initial=initial,result=result&M,flags=aux&M,mask=mask,aux_kind=kind,
   source='https://mgba-emu.github.io/gbatek/#armopcodesmultiplyandmultiplyaccumulatemulmla',architecture='ARMv5TE DSP',verification_scope='portable DSP functional semantics'))
 for op in ('qadd','qsub','qdadd','qdsub'):
  n=begin(op+'_sticky_q');asm+=['msr cpsr_f,r2',f'{op} r0,r0,r1','mrs r1,cpsr'];end()
  for a,b in PAIRS:
   x,y=signed(a),signed(b);d=sat(2*y) if op.startswith('qd') else y
   full=x+d if op in ('qadd','qdadd') else x-d;q=(op.startswith('qd') and d!=2*y) or sat(full)!=full
   for f in FLAGS:row(n,a,b,f,sat(full),f|(0x08000000 if q else 0))
 for x in ('b','t'):
  for y in ('b','t'):
   op='smul'+x+y;n=begin(op+'_flags');asm+=['msr cpsr_f,r2',f'{op} r0,r0,r1','mrs r1,cpsr'];end()
   for a,b in PAIRS:
    for f in FLAGS:row(n,a,b,f,half(a,x)*half(b,y),f)
 for op in ('smla','smlaw'):
  suffixes=[x+y for x in ('b','t') for y in ('b','t')] if op=='smla' else ['b','t']
  for suf in suffixes:
   for oldq in (0,1):
    f=(0xa8000000 if oldq else 0x50000000);n=begin(op+suf+f'_q{oldq}')
    asm+=['mov r5,r2',f'ldr r2,={hex(f)}','msr cpsr_f,r2',f'{op+suf} r0,r0,r1,r5','mrs r1,cpsr'];end()
    for a,b in PAIRS:
     product=half(a,suf[0])*half(b,suf[1]) if op=='smla' else (signed(a)*half(b,suf))>>16
     for acc in ACC:
      full=product+signed(acc);q=not -(1<<31)<=full<(1<<31)
      row(n,a,b,acc,full,f|(0x08000000 if q else 0))
 for suf in ('b','t'):
  n=begin('smulw'+suf+'_flags');asm+=['msr cpsr_f,r2',f'smulw{suf} r0,r0,r1','mrs r1,cpsr'];end()
  for a,b in PAIRS:
   for f in FLAGS:row(n,a,b,f,(signed(a)*half(b,suf))>>16,f)
 for x in ('b','t'):
  for y in ('b','t'):
   for index,acc in enumerate((0,0x7fffffffffffffff,0x8000000000000000,0xffffffffffffffff)):
    for check in ('data','flags'):
     n=begin(f'smlal{x}{y}_acc{index}_{check}')
     asm+=['msr cpsr_f,r2',f'ldr r5,={hex(acc&M)}',f'ldr r6,={hex(acc>>32)}',f'smlal{x}{y} r5,r6,r0,r1','mov r0,r5', 'mov r1,r6' if check=='data' else 'mrs r1,cpsr'];end()
     for a,b in PAIRS:
      result=(acc+half(a,x)*half(b,y))&0xffffffffffffffff
      row(n,a,b,0x58000000,result,result>>32 if check=='data' else 0x58000000,'high_product_word' if check=='data' else 'NZCVQ',M if check=='data' else 0xf8000000)
