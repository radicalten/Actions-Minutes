"""Milestone 3: defined Thumb control-flow, high-register and stack/address forms.
Shared ARMv4T cases; CPU-specific invalid encodings are intentionally excluded.
"""
M=0xffffffff
REF='thumbinstructionsummary'
def extend_thumb(addfn,row,seeds,expect):
 def add(name,body,aux=False,setup=None):
  return addfn('thumb_'+name,body,thumb=True,aux=aux,setup=setup)
 def emit(name,a,b,initial,v,aux=0,mask=0,kind='ignored'):
  row(name,a,b,initial,v,aux,mask,REF,kind)
 # All 14 legal conditional branch conditions, all 16 NZCV combinations.
 conditions=['eq','ne','cs','cc','mi','pl','vs','vc','hi','ls','ge','lt','gt','le']
 for cond in conditions:
  name=add('branch_'+cond,[f'b{cond} 2f','movs r0,#0','b 3f','2:','movs r0,#1','3:'])
  for nzcv in range(16):
   n,z,c,v=[bool(nzcv&(1<<b)) for b in [3,2,1,0]]
   truth=[z,not z,c,not c,n,not n,v,not v,c and not z,not c or z,n==v,n!=v,not z and n==v,z or n!=v]
   emit(name,0,0,nzcv<<28,int(truth[conditions.index(cond)]))
 # High-register operations: MOV/ADD must preserve NZCV; CMP must update it.
 for label,body in [
  ('mov_high_roundtrip',['mov r8,r0','mov r0,r8']),
  ('add_high_destination',['mov r8,r0','add r8,r1','mov r0,r8']),
  ('add_high_source',['mov r8,r1','add r0,r8']),
  ('cmp_high_destination',['mov r8,r0','cmp r8,r1']),
  ('cmp_high_source',['mov r8,r1','cmp r0,r8'])]:
  name=add(label,body)
  for a,b in seeds:
   initial=0xa0000000
   if label.startswith('cmp'):v,f,m=expect('cmp',a,b,1)
   else:v=a if label.startswith('mov') else (a+b)&M;f=initial;m=0xf0000000
   emit(name,a,b,initial,v,f,m,'NZCV')
 # BL covers its LR Thumb bit as well as return to the next instruction.
 for direction,body in [
  ('forward',['bl 2f','b 3f','2:','adds r0,#1','mov r1,lr','movs r2,#1','ands r1,r2','bx lr','3:']),
  ('backward',['b 3f','2:','adds r0,#1','mov r1,lr','movs r2,#1','ands r1,r2','bx lr','3:','bl 2b'])]:
  name=add('bl_'+direction,body,aux=True)
  for a,b in seeds:emit(name,a,b,0,(a+1)&M,1,M,'lr_thumb_bit')
 name=add('b_forward_backward',['b 3f','2:','adds r0,#1','b 4f','3:','b 2b','4:'])
 for a,b in seeds:
  v,f,m=expect('add',a,1,0);emit(name,a,b,0,v,f,m,'NZCV')
 # PC-relative literal load with literal pool located inside the helper.
 name=add('ldr_literal',['ldr r0,2f','b 3f','.balign 4','2: .word 0x89abcdef','3:'])
 for flags in [0,0x20000000,0x90000000,0xf0000000]:emit(name,0,0,flags,0x89abcdef,flags,M&0xf0000000,'NZCV')
 # ADR result compared to an ARM-calculated address of the same Thumb label.
 name=add('adr_pc',['adr r0,2f','subs r0,r0,r6','b 3f','.balign 4','2: .word 0','3:'],
   setup=['msr cpsr_f,r2','adr r6,2f'])
 emit(name,0,0,0,0)
 name=add('add_sp_address',['add r0,sp,#12','mov r1,sp','subs r0,r0,r1'])
 emit(name,0,0,0,12)
 for direction,amount in [('plus',508),('minus',-508)]:
  op='add' if amount>0 else 'sub';undo='sub' if amount>0 else 'add'
  name=add('adjust_sp_'+direction,[f'{op} sp,#508','mov r0,sp','subs r0,r0,r5',f'{undo} sp,#508'])
  emit(name,0,0,0,amount&M)
 name=add('ldr_sp',['ldr r0,[sp,#12]'],setup=['msr cpsr_f,r2','str r0,[sp,#12]'])
 for a,b in seeds:emit(name,a,b,0xa0000000,a,0xa0000000,0xf0000000,'NZCV')
 name=add('str_sp',['str r0,[sp,#12]','ldr r0,[r5,#12]','ldr r1,[sp,#8]'],aux=True,
   setup=['msr cpsr_f,r2','str r1,[sp,#8]','str r1,[sp,#12]'])
 for a,b in seeds:emit(name,a,b,0,a,b,M,'guard_word')
 for label,body in [
  ('push_low',['push {r0,r1}','ldr r0,[sp]','ldr r1,[sp,#4]','add sp,#8']),
  ('pop_low',['str r0,[sp]','str r1,[sp,#4]','pop {r0,r1}','sub sp,#8']),
  ('push_lr',['mov lr,r0','push {r1,lr}','ldr r0,[sp,#4]','ldr r1,[sp]','add sp,#8'])]:
  name=add(label,body,aux=True)
  for a,b in seeds:emit(name,a,b,0,a,b,M,'second_stack_word')
 name=add('pop_pc',['pop {pc}','movs r0,#0','b 4f','3:','movs r0,#1','4:','sub sp,#4','mov r1,sp','subs r1,r1,r5'],aux=True,
  setup=['msr cpsr_f,r2','adr r6,3f+1','str r6,[sp]'])
 emit(name,0,0,0,1,0,M,'restored_sp_delta')
 for op in ['ldmia','stmia']:
  for check in ['data','writeback']:
   body=['movs r6,r5']
   if op=='ldmia':body+=['str r0,[r5]','str r1,[r5,#4]']
   body+=[f'{op} r6!,{{r0,r1}}']
   if op=='stmia' and check=='data':body+=['ldr r0,[r5]','ldr r1,[r5,#4]']
   if check=='writeback':body+=['subs r0,r6,r5','movs r1,#0']
   name=add(op+'_'+check,body,aux=True)
   for a,b in seeds[:4]:emit(name,a,b,0,a if check=='data' else 8,b if check=='data' else 0,M,'second_word' if check=='data' else 'base_delta')
