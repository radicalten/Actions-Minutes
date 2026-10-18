"""Real nested ARM7 timer IRQs; outer SPSR and callback LR explicitly stacked.
Tests nested state capture and outer return, not interrupt priority or cycle timing.
"""
def extend_nested(asm,decl,rows,base):
 for state in ('arm','thumb'):
  name='cpu_arm7_irq_nested_'+state
  decl.append(f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);')
  asm+=['.arm','.balign 4',f'.global {name}',f'.type {name}, %function',name+':','push {r4-r11,lr}','sub sp,sp,#36','mov r7,sp','mov r4,r3','mrs r6,cpsr','str r0,[r7,#16]',
   'orr r1,r6,#0xc0','msr cpsr_c,r1','ldr r5,=0x04000200','ldr r2,[r5,#8]','str r2,[r7]','mov r1,#0','str r1,[r5,#8]',
   'ldr r2,[r5,#16]','str r2,[r7,#4]','ldr r3,=0x0380fffc','ldr r2,[r3]','str r2,[r7,#8]','ldr r2,=cpu_arm7_nested_handler','str r2,[r3]',
   'ldr r3,=0x04000108','ldr r2,[r3]','str r2,[r7,#20]','str r1,[r3]','ldr r2,[r3,#4]','str r2,[r7,#12]','str r1,[r3,#4]',
   'ldr r8,=cpu_arm7_nested_capture','str r1,[r8]','ldr r2,=0xbaadc0de','str r2,[r8,#4]','mov r2,#0x60','str r2,[r5,#16]','str r2,[r5,#20]',
   'ldr r0,[r7,#16]','orr r0,r0,#0x5f','msr cpsr_fsxc,r0','mov r1,#1','str r1,[r5,#8]',
   'ldr r3,=0x0400010c','ldr r2,=0x00c0ff80','ldr r8,=2f','b 9f','.ltorg','9:']
  if state=='thumb':asm+=['adr r0,1f','orr r0,r0,#1','bx r0','.thumb','.balign 4','1:']
  asm+=['str r2,[r3]','.rept 4096','nop','.endr']
  if state=='thumb':asm+=['bx r8','.arm','.balign 4']
  asm+=['2:','mrs r8,cpsr','orr r0,r8,#0xc0','msr cpsr_c,r0','mov r1,#0','str r1,[r5,#8]',
   'ldr r3,=0x04000108','str r1,[r3]','str r1,[r3,#4]','mov r1,#0x60','str r1,[r5,#20]',
   'ldr r2,[r7,#20]','str r2,[r3]','ldr r2,[r7,#12]','str r2,[r3,#4]',
   'ldr r3,=0x0380fffc','ldr r2,[r7,#8]','str r2,[r3]','ldr r2,[r7,#4]','str r2,[r5,#16]','ldr r2,[r7]','str r2,[r5,#8]',
   'ldr r3,=cpu_arm7_nested_capture','ldr r0,[r3,#4]','ldr r2,=0xf00000ff','and r0,r0,r2','and r1,r8,r2','ldr r2,[r3]',
   'cmp r2,#2','orrne r1,r1,#0x100','msr cpsr_fsxc,r6','mov sp,r7','str r0,[r4]','str r1,[r4,#4]','add sp,sp,#36','pop {r4-r11,lr}','bx lr','.ltorg']
  for flags in range(16):rows.append(dict(id=base+len(rows)+1,name=name,a=flags<<28,b=0,initial=0,result=(flags<<28)|0x52,flags=(flags<<28)|0x5f,mask=0xf00001ff,
   aux_kind='outer_return_CPSR_after_Thumb_BX; bad_callback_count_bit8',source='https://mgba-emu.github.io/gbatek/#armcpuexceptions',architecture='ARM7TDMI NDS nested IRQ',verification_scope='real timer3 outer/timer2 inner; no priority/timing claim'))
 asm+=['.arm','.balign 4','.global cpu_arm7_nested_handler','cpu_arm7_nested_handler:',
   'ldr r0,=cpu_arm7_nested_capture','ldr r1,[r0]','cmp r1,#0','bne 2f',
   # Outer callback saves callee state and SPSR before allowing nested exception.
   'push {r4-r11,lr}','mrs r4,spsr','mov r5,r0','mov r1,#1','str r1,[r5]',
   'ldr r0,=0x0400010c','mov r1,#0','strh r1,[r0,#2]','ldr r0,=0x04000214','mov r1,#64','str r1,[r0]',
   'and r0,r4,#0xf0000000','orr r0,r0,#0x52','msr cpsr_fsxc,r0','ldr r0,=0x04000108','ldr r1,=0x00c0ff80','b 9f','.ltorg','9:','str r1,[r0]',
   '.rept 2048','nop','.endr','mrs r0,cpsr','orr r0,r0,#0x80','msr cpsr_c,r0','msr spsr_fsxc,r4','pop {r4-r11,lr}','bx lr',
   # Inner callback sees an IRQ-mode source with I clear. BIOS preserves r0-r3.
   '2:','mrs r1,spsr','str r1,[r0,#4]','mov r1,#2','str r1,[r0]','ldr r0,=0x04000108','mov r1,#0','strh r1,[r0,#2]',
   'ldr r0,=0x04000214','mov r1,#32','str r1,[r0]','bx lr','.ltorg','.bss','.balign 4','cpu_arm7_nested_capture:','.space 8','.text']
