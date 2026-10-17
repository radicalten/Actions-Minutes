"""ARM946-only aligned LDRD/STRD forms; never dispatched on ARM7TDMI."""
import json
from pathlib import Path
D=Path(__file__).resolve().parents[1]/'src/generated'
def generate_v5(base):
 asm=['.syntax unified','.arch armv5te','.text'];decl=[];rows=[]
 pairs=[(0,0),(0xffffffff,0x80000000),(0x12345678,0x89abcdef),(0x80008080,0x55aa55aa)]
 modes=[('offset','[r6,#8]',0,8),('pre_plus','[r6,#8]!',8,8),('pre_minus','[r6,#-8]!',-8,-8),('post_plus','[r6],#8',8,0),('post_minus','[r6],#-8',-8,0),('register','[r6,r7]',0,8)]
 for op in ['ldrd','strd']:
  for label,addr,delta,target in modes:
   for check in ['data','writeback']:
    name=f'cpu_arm9_{op}_{label}_{check}'
    decl.append(f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);')
    asm+=['.arm','.align 2',f'.global {name}',f'.type {name}, %function',name+':','push {r4-r8,lr}','mov r4,r3','sub sp,sp,#64','mov r5,sp','add r6,r5,#24','mov r7,#8']
    if op=='ldrd':asm+=[f'str r0,[r5,#{24+target}]',f'str r1,[r5,#{28+target}]']
    else:asm+=['mvn r8,r0',f'str r8,[r5,#{24+target}]','mvn r8,r1',f'str r8,[r5,#{28+target}]']
    asm+=[f'{op} r0,r1,{addr}']
    if check=='data' and op=='strd':asm+=[f'ldr r0,[r5,#{24+target}]',f'ldr r1,[r5,#{28+target}]']
    if check=='writeback':asm+=['sub r0,r6,r5','sub r0,r0,#24',f'ldr r1,[r5,#{24+target}]']
    asm+=['str r0,[r4]','str r1,[r4,#4]','add sp,sp,#64','pop {r4-r8,lr}','bx lr']
    for a,b in pairs:rows.append(dict(id=base+len(rows)+1,name=name,a=a,b=b,initial=0,result=a if check=='data' else delta&0xffffffff,flags=b if check=='data' else a,mask=0xffffffff,aux_kind='second_word' if check=='data' else 'first_memory_word',source='https://mgba-emu.github.io/gbatek/#armopcodesmemoryhalfworddoublewordandsigneddatatransfer',architecture='ARMv5TE only'))
 from arm9_dsp import extend_dsp
 extend_dsp(asm,decl,rows,base)
 from arm7_privileged import extend_privileged
 extend_privileged(asm,decl,rows,base,prefix='cpu_arm9_')
 from arm7_irq import extend_irq
 extend_irq(asm,decl,rows,base,cpu=9)
 from arm9_cp15 import extend_cp15
 extend_cp15(asm,decl,rows,base)
 from arm9_exceptions import extend_exceptions
 extend_exceptions(asm,decl,rows,base,kinds=("swi","undefined","data_abort","prefetch_abort"))
 from register_operand2 import extend_register_operand2
 extend_register_operand2(asm,decl,rows,base,cpu=9)
 from compare_operand2 import extend_compare_operand2
 extend_compare_operand2(asm,decl,rows,base,cpu=9)
 extend_register_operand2(asm,decl,rows,base,cpu=9,ops=('add','sub','rsb','rsc','orr','bic','mov','mvn'),scope='register_operand2_remaining')
 from thumb_edges import extend_thumb_edges
 extend_thumb_edges(asm,decl,rows,base,cpu=9)
 from thumb_memory import extend_thumb_memory
 extend_thumb_memory(asm,decl,rows,base,cpu=9)
 from thumb_sp import extend_thumb_sp
 extend_thumb_sp(asm,decl,rows,base,cpu=9)
 from thumb_pc import extend_thumb_pc
 extend_thumb_pc(asm,decl,rows,base,cpu=9)
 from thumb_branch import extend_thumb_branch,extend_thumb_branch_sweep
 extend_thumb_branch(asm,decl,rows,base,cpu=9)
 extend_thumb_branch_sweep(asm,decl,rows,base,cpu=9)
 from arm_immediate import extend_arm_immediate
 extend_arm_immediate(asm,decl,rows,base,cpu=9)
 from thumb_immediate import extend_thumb_immediate
 extend_thumb_immediate(asm,decl,rows,base,cpu=9)
 (D/'v5.s').write_text('\n'.join(asm)+'\n')
 (D/'v5-vectors.json').write_text(json.dumps(rows,indent=2)+'\n')
 return decl,rows
