"""ARM7TDMI-specific execution profile: GBATEK alignment and ARMv4T PC behavior.
Not an exact-QEMU or physical-hardware oracle. Keep undefined MUL carry unasserted.
"""
import json
from pathlib import Path
D=Path(__file__).resolve().parents[1]/'src/generated'
M=0xffffffff
URL='https://mgba-emu.github.io/gbatek/'
def ror(x,n):return ((x>>n)|(x<<(32-n)))&M if n else x
def sign(x,bits):return (x-(1<<bits) if x&(1<<(bits-1)) else x)&M
def generate_arm7(base):
 asm=['.syntax unified','.arch armv4t','.text'];decl=[];rows=[]
 def begin(name):
  decl.append(f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);')
  asm.extend(['.arm','.balign 4',f'.global {name}',f'.type {name}, %function',name+':','push {r4-r8,lr}','mov r4,r3','sub sp,sp,#16'])
 def end():asm.extend(['str r0,[r4]','str r1,[r4,#4]','add sp,sp,#16','pop {r4-r8,lr}','bx lr','.ltorg'])
 def row(name,a,b,result,aux=0,mask=0,kind='not_checked',reference='armcpumemoryalignments',**extra):
  rows.append(dict(id=base+len(rows)+1,name=name,a=a,b=b,initial=0,result=result,flags=aux,mask=mask,aux_kind=kind,source=URL+'#'+reference,architecture='ARM7TDMI execution profile',**extra))
 patterns=[0x00000000,0xffffffff,0x807fff01,0x12a5fe80];guard=0x6d37c2a9
 for state in ('arm','thumb'):
  for op in ('ldr','ldrh','ldrsh','str','strh'):
   name=f'cpu_arm7_{state}_{op}_alignment';begin(name)
   asm+=['mov r5,sp','mov r6,r1',f'ldr r7,={hex(guard)}','str r7,[r5,#4]']
   if op.startswith('str'):asm+=['ldr r7,=0xa5a5a5a5','str r7,[r5]']
   else:asm+=['str r0,[r5]']
   if state=='thumb':asm+=['adr r7,1f','orr r7,r7,#1','adr r8,2f','bx r7','.thumb','.balign 4','1:']
   asm+=[f'{op} r0,[r5,r6]']
   if op.startswith('str'):asm+=['ldr r0,[r5]']
   if state=='thumb':asm+=['bx r8','.arm','.balign 4','2:']
   asm+=['ldr r1,[r5,#4]'];end()
   for a in patterns:
    for offset in range(4):
     half=(a>>((offset&2)*8))&0xffff
     if op=='ldr':expected=ror(a,offset*8)
     elif op=='ldrh':expected=ror(half,8) if offset&1 else half
     elif op=='ldrsh':expected=sign((a>>(offset*8))&255,8) if offset&1 else sign(half,16)
     elif op=='str':expected=a
     else:
      shift=(offset&2)*8;expected=(0xa5a5a5a5&~(0xffff<<shift))|((a&0xffff)<<shift)
     row(name,a,offset,expected,guard,M,'adjacent_word_guard',arm9_semantics_differ=bool(op in ('ldrh','ldrsh') and offset&1))
 name='cpu_arm7_arm_swp_alignment';begin(name)
 asm+=['mov r5,sp','str r0,[r5]','add r6,r5,r1','mvn r7,r0','swp r0,r7,[r6]','ldr r1,[r5]'];end()
 for a in patterns:
  for offset in range(4):row(name,a,offset,ror(a,offset*8),(~a)&M,M,'written_aligned_word')
 for label,ins,delta in [('mov','mov r0,pc',8),('shift_rm','mov r0,pc,lsl r1',12),('shift_rn','add r0,pc,r1,lsl r2',12),('str','str pc,[sp]',12),('stm','stmia r7,{pc}',12)]:
  name='cpu_arm7_arm_pc_'+label;begin(name)
  asm+=['adr r5,1f','mov r1,#0','mov r2,#0','mov r7,sp','1:',ins]
  if label in ('str','stm'):asm+=['ldr r0,[sp]']
  asm+=['sub r0,r0,r5','mov r1,#0'];end()
  row(name,0,0,delta,reference={'str':'armopcodesmemorysingledatatransferldrstrpld','stm':'armopcodesmemoryblockdatatransferldmstm'}.get(label,'armopcodesdataprocessingalu'))
 for op in ('mov','add'):
  for alignment in (0,2):
   name=f'cpu_arm7_thumb_pc_{op}_{alignment}';begin(name)
   asm+=['mov r0,#0','adr r5,2f','adr r6,3f','adr r7,1f','orr r7,r7,#1','bx r7','.thumb','.balign 4','1:']
   if alignment:asm+=['nop']
   asm+=['2:',f'{op} r0,pc','bx r6','.arm','.balign 4','3:','sub r0,r0,r5','mov r1,#0'];end()
   row(name,0,alignment,4,reference='thumbopcodesregisteroperationsalubx')
 for op in ('ldr','ldm'):
  name=f'cpu_arm7_arm_{op}_pc_no_exchange';begin(name)
  asm+=['adr r0,1f','orr r0,r0,#1','str r0,[sp]', 'ldr pc,[sp]' if op=='ldr' else 'ldmia sp,{pc}', '.balign 4','1:','mov r0,#0x79','mrs r1,cpsr','and r1,r1,#0x20'];end()
  row(name,0,1,0x79,0,0x20,'CPSR_T',reference='armopcodesbranchandbranchwithlinkbblbxblxswibkpt',arm9_semantics_differ=True)
 name='cpu_arm7_thumb_pop_pc_no_exchange';begin(name)
 asm+=['mov r5,sp','adr r0,2f','str r0,[sp,#-4]!','adr r6,3f','adr r7,1f','orr r7,r7,#1','bx r7','.thumb','.balign 4','1:','pop {pc}','.balign 4','2:','movs r0,#0x7d','bx r6','.arm','.balign 4','3:','sub r1,sp,r5'];end()
 row(name,0,0,0x7d,0,M,'stack_delta',reference='thumbopcodesmemorymultipleloadstorepushpopandldmstm',arm9_semantics_differ=True)
 from arm7_privileged import extend_privileged
 extend_privileged(asm,decl,rows,base)
 from arm7_irq import extend_irq
 extend_irq(asm,decl,rows,base)
 from arm7_nested import extend_nested
 extend_nested(asm,decl,rows,base)
 from register_operand2 import extend_register_operand2
 extend_register_operand2(asm,decl,rows,base,cpu=7)
 from compare_operand2 import extend_compare_operand2
 extend_compare_operand2(asm,decl,rows,base,cpu=7)
 extend_register_operand2(asm,decl,rows,base,cpu=7,ops=('add','sub','rsb','rsc','orr','bic','mov','mvn'),scope='register_operand2_remaining')
 from thumb_edges import extend_thumb_edges
 extend_thumb_edges(asm,decl,rows,base,cpu=7)
 from thumb_memory import extend_thumb_memory
 extend_thumb_memory(asm,decl,rows,base,cpu=7)
 from thumb_sp import extend_thumb_sp
 extend_thumb_sp(asm,decl,rows,base,cpu=7)
 from thumb_pc import extend_thumb_pc
 extend_thumb_pc(asm,decl,rows,base,cpu=7)
 from thumb_branch import extend_thumb_branch,extend_thumb_branch_sweep
 extend_thumb_branch(asm,decl,rows,base,cpu=7)
 extend_thumb_branch_sweep(asm,decl,rows,base,cpu=7)
 from arm_immediate import extend_arm_immediate
 extend_arm_immediate(asm,decl,rows,base,cpu=7)
 from thumb_immediate import extend_thumb_immediate
 extend_thumb_immediate(asm,decl,rows,base,cpu=7)
 (D/'arm7.s').write_text('\n'.join(asm)+'\n')
 (D/'arm7-vectors.json').write_text(json.dumps(rows,indent=2)+'\n')
 return decl,rows
