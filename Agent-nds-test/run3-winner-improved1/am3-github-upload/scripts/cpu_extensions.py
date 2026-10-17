"""GBATEK-guided, legal ARMv4T instruction forms; no undefined cases mixed in.
See docs/gbatek-coverage.md for scope and exact source anchors.
"""
M=0xffffffff
BASE='https://mgba-emu.github.io/gbatek/#'

def extend(asm,decl,rows,expect,seeds):
 def addfn(name,body,thumb=False,aux=False,setup=None):
  name='cpu_'+name
  decl.append(f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);')
  asm.extend(['.arm','.align 2',f'.global {name}',f'.type {name}, %function',name+':',
    'push {r4-r8,lr}','mov r4,r3','sub sp,sp,#64','mov r5,sp'])
  asm.extend(setup or ['msr cpsr_f,r2'])
  if thumb:asm.extend(['adr r3,1f+1','bx r3','.thumb','.align 2','1:'])
  asm.extend(body)
  if thumb:
   # Align BX itself, not just the following ARM code. NOP padding preserves flags.
   asm.extend(['.balign 4','bx pc','nop','.arm','.align 2'])
  if not aux:asm.append('mrs r1,cpsr')
  asm.extend(['str r0,[r4]','str r1,[r4,#4]','add sp,sp,#64','pop {r4-r8,lr}','bx lr','.ltorg'])
  return name
 def row(name,a,b,initial,result,flags,mask,anchor,kind='NZCV'):
  rows.append(dict(id=len(rows)+1,name=name,a=a&M,b=b&M,initial=initial&M,result=result&M,flags=flags&M,mask=mask,source=BASE+anchor,aux_kind=kind))
 small=[0,1,0x80000000,M]
 shiftref='armopcodesdataprocessingalu'
 for op in ['lsl','lsr','asr','ror']:
  name=addfn('arm_'+op+'_register',[f'movs r0,r0,{op} r1'])
  for n in [0,1,31,32,33,255,256,257]:
   for c in [0,1]:
    r,f,m=expect(op,0x81234567,n,c);row(name,0x81234567,n,(c<<29)|0x10000000,r,f,m,shiftref)
 for state in ['arm','thumb']:
  for op,amounts in [('lsl',[0,1,31]),('lsr',[1,31,32]),('asr',[1,31,32])]+([('ror',[1,31])] if state=='arm' else []):
   for n in amounts:
    ins=f'movs r0,r0,{op} #{n}' if state=='arm' else f'{op}s r0,r0,#{n}'
    name=addfn(f'{state}_{op}_imm_{n}',[ins],thumb=state=='thumb')
    for a in small:
     for c in [0,1]:
      r,f,m=expect(op,a,n,c);row(name,a,0,(c<<29)|0x10000000,r,f,m,shiftref if state=='arm' else 'thumbinstructionsummary')
 name=addfn('arm_rrx',['movs r0,r0,rrx'])
 for a in small:
  for c in [0,1]:
   r=(c<<31)|(a>>1); f=(r&0x80000000)|(int(r==0)<<30)|((a&1)<<29)|0x10000000
   row(name,a,0,(c<<29)|0x10000000,r,f,0xf0000000,shiftref)

 # Full 64-bit products and separate NZ checks, without making ARM7 C/V promises.
 mulref='armopcodesmultiplyandmultiplyaccumulatemulmla'
 def signed(v):return v if v<0x80000000 else v-2**32
 for op in ['umull','smull','umlal','smlal']:
  for checkflags in [False,True]:
   body=['mov r6,r2','mov r7,#0',f'{op}s r6,r7,r0,r1','mov r0,r6']
   if not checkflags:body+=['mov r1,r7']
   name=addfn('arm_'+op+('_nz' if checkflags else '_64'),body,aux=not checkflags,setup=['mov r8,#0','msr cpsr_f,r8'])
   for a,b in seeds:
    for acc in [0,M]:
     value=((signed(a)*signed(b) if op[0]=='s' else a*b)+(acc if op.endswith('lal') else 0))&((1<<64)-1)
     lo=value&M;hi=value>>32
     flags=(hi&0x80000000)|(int(value==0)<<30)
     row(name,a,b,acc,lo,flags if checkflags else hi,0xc0000000 if checkflags else M,mulref,'NZ' if checkflags else 'product_hi')
 name=addfn('arm_mla',['mlas r6,r0,r1,r2','mov r0,r6'],setup=['mov r8,#0','msr cpsr_f,r8'])
 for a,b in seeds:
  for acc in [0,M]:
   v=(a*b+acc)&M;f=(v&0x80000000)|(int(v==0)<<30)
   row(name,a,b,acc,v,f,0xc0000000,mulref,'NZ')

 # Aligned scratch memory only. Every wrapper restores its own stack and registers.
 # Observe BOTH the loaded/stored value and a writeback delta/guard word.
 memref='armopcodesmemorysingledatatransferldrstrpld'
 pairs=[(0x12345678,0x89abcdef),(0x80008080,0x55aa55aa),(0, M),(M,0)]
 modes=[('pre_plus','[r6,#4]!',4),('pre_minus','[r6,#-4]!',-4),('post_plus','[r6],#4',4),('post_minus','[r6],#-4',-4),('offset','[r6,#4]',0),('reg_shift','[r6,r7,lsl #2]',0)]
 for tag,addr,delta in modes:
  body=['add r6,r5,#16','mov r7,#1','str r0,[r6,#-4]','str r0,[r6]','str r0,[r6,#4]',f'ldr r0,{addr}','sub r1,r6,r5','sub r1,r1,#16']
  name=addfn('arm_ldr_'+tag,body,aux=True)
  for a,b in pairs:row(name,a,b,0,a,delta,M,memref,'base_delta')
  # STR addressing mode, read back at the actual destination and check writeback.
  target=4 if tag in ('pre_plus','offset','reg_shift') else -4 if tag=='pre_minus' else 0
  body=['add r6,r5,#16','mov r7,#1','str r1,[r6,#-4]','str r1,[r6]','str r1,[r6,#4]',f'str r0,{addr}',f'ldr r0,[r5,#{16+target}]','sub r1,r6,r5','sub r1,r1,#16']
  name=addfn('arm_str_'+tag,body,aux=True)
  for a,b in pairs:row(name,a,b,0,a,delta,M,memref,'base_delta')
 for state in ['arm','thumb']:
  for op in ['ldrb','ldrh','ldrsb','ldrsh']:
   signed_load=op in ('ldrsb','ldrsh'); width=8 if op in ('ldrb','ldrsb') else 16
   addr='[r5,r6]' if state=='thumb' else '[r5]'
   body=['str r0,[r5]','str r1,[r5,#4]','movs r6,#0',f'{op} r0,{addr}','ldr r1,[r5,#4]']
   name=addfn(state+'_'+op+'_aligned',body,thumb=state=='thumb',aux=True)
   for a,b in pairs:
    v=a&((1<<width)-1)
    if signed_load and v&(1<<(width-1)):v-=1<<width
    row(name,a,b,0,v,b,M,'armopcodesmemoryhalfworddoublewordandsigneddatatransfer' if width==16 or signed_load else memref,'guard_word')
  for op,offset,width in [('strb',1,8),('strh',2,16)]:
   body=['str r1,[r5]','str r1,[r5,#4]',f'{op} r0,[r5,#{offset}]','ldr r0,[r5]','ldr r1,[r5,#4]']
   name=addfn(state+'_'+op+'_lanes',body,thumb=state=='thumb',aux=True)
   for a,b in pairs:
    mask=((1<<width)-1)<<(8*offset);v=(b&~mask)|((a<<8*offset)&mask)
    row(name,a,b,0,v,b,M,memref,'guard_word')
 for op in ['swp','swpb']:
  name=addfn('arm_'+op,['str r0,[r5]',f'{op} r6,r1,[r5]','mov r0,r6','ldr r1,[r5]'],aux=True)
  for a,b in pairs:
   row(name,a,b,0,a if op=='swp' else a&255,b if op=='swp' else (a&0xffffff00)|(b&255),M,'armopcodesmemorysingledataswapswp','stored_word')
 # Thumb word load/store register and immediate forms. Values are read separately
 # from expected data (never a store/load round-trip as the only check).
 for mode,addr in [('immediate','[r5,#4]'),('register','[r5,r6]')]:
  name=addfn('thumb_ldr_'+mode,['str r0,[r5,#4]','movs r6,#4',f'ldr r0,{addr}','movs r1,#4'],thumb=True,aux=True)
  for a,b in pairs:row(name,a,b,0,a,4,M,'thumbinstructionsummary','offset_marker')
  name=addfn('thumb_str_'+mode,['str r1,[r5]','str r1,[r5,#4]','movs r6,#4',f'str r0,{addr}','ldr r0,[r5,#4]','ldr r1,[r5]'],thumb=True,aux=True)
  for a,b in pairs:row(name,a,b,0,a,b,M,'thumbinstructionsummary','guard_word')
 # Defined block transfer modes, no PC or base-in-list ambiguity.
 for mode,offset in [('ia',0),('ib',4),('da',-4),('db',-8)]:
  name=addfn('arm_ldm_'+mode,['add r6,r5,#16',f'str r0,[r6,#{offset}]',f'str r1,[r6,#{offset+4}]',f'ldm{mode} r6!,{{r0,r1}}'],aux=True)
  for a,b in pairs:row(name,a,b,0,a,b,M,'armopcodesmemoryblockdatatransferldmstm','second_register')
  name=addfn('arm_stm_'+mode,['add r6,r5,#16',f'stm{mode} r6!,{{r0,r1}}',f'ldr r0,[r5,#{16+offset}]',f'ldr r1,[r5,#{20+offset}]'],aux=True)
  for a,b in pairs:row(name,a,b,0,a,b,M,'armopcodesmemoryblockdatatransferldmstm','second_memory_word')
 from thumb_extensions import extend_thumb
 extend_thumb(addfn,row,seeds,expect)
 from arm_extensions import extend_arm
 extend_arm(addfn,row,seeds,expect)
