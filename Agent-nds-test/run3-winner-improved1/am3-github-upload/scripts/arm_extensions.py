"""Milestone 4: shared ARMv4T conditional execution, operand2 and flags-only PSR access."""
M=0xffffffff
REF='armopcodesdataprocessingalu'
def extend_arm(addfn,row,seeds,expect):
 # Legal conditions, including AL. NV is not a portable never condition on ARMv5.
 conds=['eq','ne','cs','cc','mi','pl','vs','vc','hi','ls','ge','lt','gt','le','al']
 for cond in conds:
  name=addfn('arm_cond_'+cond,['mov r0,#0',f'mov{cond} r0,#1'])
  for nzcv in range(16):
   n,z,c,v=[bool(nzcv&(1<<b)) for b in [3,2,1,0]]
   truth=[z,not z,c,not c,n,not n,v,not v,c and not z,not c or z,n==v,n!=v,not z and n==v,z or n!=v,True]
   row(name,0,0,nzcv<<28,int(truth[conds.index(cond)]),nzcv<<28,0xf0000000,'armcpuflagsconditionfieldcond')
 forms=[('lsl',0),('lsl',1),('lsl',31),('lsr',1),('lsr',32),('asr',1),('asr',32),('ror',1),('ror',31),('rrx',0)]
 for op in ['and','eor','orr','bic','mov','mvn','add','sub','adc','sbc']:
  for shift,n in forms:
   operand='r1,rrx' if shift=='rrx' else f'r1,{shift} #{n}'
   ins=f'{op}s r0,'+('' if op in ('mov','mvn') else 'r0,')+operand
   name=addfn(f'arm_{op}_operand2_{shift}_{n}',[ins])
   for a,b in seeds:
    for carry in [0,1]:
     if shift=='rrx':shifted=(carry<<31)|(b>>1);sc=b&1
     else:
      shifted,sflags,_=expect(shift,b,n,carry);sc=(sflags>>29)&1
     value,flags,mask=expect(op,a,shifted,carry)
     if op in ('and','eor','orr','bic','mov','mvn'):flags=(flags&~(1<<29))|(sc<<29)
     row(name,a,b,(carry<<29)|0x10000000,value,flags,mask,REF)
 # Only CPSR flags are written; verify NZCV and that control bits did not change.
 name=addfn('arm_msr_flags_mrs',['mrs r6,cpsr','msr cpsr_f,r0','mrs r7,cpsr','and r0,r7,#0xf0000000','eor r1,r6,r7','bic r1,r1,#0xf0000000'],aux=True)
 for flags in range(16):
  row(name,flags<<28,0,0,flags<<28,0,M,'armopcodespsrtransfermrsmsr','non_nzcv_change')
