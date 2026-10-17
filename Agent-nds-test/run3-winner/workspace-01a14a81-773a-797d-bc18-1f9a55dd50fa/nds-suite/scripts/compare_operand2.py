"""Flag-only register-shift operand2; source register must be preserved."""
from register_operand2 import COUNTS,shifted,reference
def extend_compare_operand2(asm,decl,rows,base,cpu):
 for op in ('tst','teq','cmp','cmn'):
  for kind in ('lsl','lsr','asr','ror'):
   name=f'cpu_arm{cpu}_{op}_register_operand2_{kind}'
   decl.append(f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);')
   asm+=['.arm','.balign 4',f'.global {name}',f'.type {name}, %function',name+':',
    'push {r4-r6,lr}','mov r4,r3','mrs r6,cpsr','msr cpsr_f,r2',
    f'{op} r0,r1,{kind} r2','mrs r1,cpsr','str r0,[r4]','str r1,[r4,#4]',
    'msr cpsr_f,r6','pop {r4-r6,lr}','bx lr']
   for i,count in enumerate(COUNTS):
    for carry in (0,1):
     a,b=0x7fffffff,0x80000001;old_v=i&1
     shifted_b,sc=shifted(b,kind,count,carry)
     if op in ('tst','teq'):
      value=a&shifted_b if op=='tst' else a^shifted_b
      flags=(value&0x80000000)|(int(value==0)<<30)|(sc<<29)|(old_v<<28)
     else:
      _,flags=reference('sbc' if op=='cmp' else 'adc',a,shifted_b,'lsl',0,1 if op=='cmp' else 0,old_v)
     result=a # Test/compare must not write Rn.
     rows.append(dict(id=base+len(rows)+1,name=name,a=a,b=b,
      initial=0xc0000000|(old_v<<28)|(carry<<29)|count,result=result,flags=flags,mask=0xf0000000,
      aux_kind='NZCV; initial also supplies Rs, whose low eight bits select shift count',
      shift_count=count,architecture='ARMv4T legal register-controlled operand2',
      verification_scope='compare_operand2_m10',
      source='https://mgba-emu.github.io/gbatek/#armopcodesdataprocessingalu'))
