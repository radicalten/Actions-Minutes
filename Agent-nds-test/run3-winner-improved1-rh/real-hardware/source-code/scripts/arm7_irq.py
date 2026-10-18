"""Real timer IRQ entry through the NDS ARM7 BIOS callback, not a synthetic mode switch.
Requires the suite's quiescent startup peripherals; restores hook, IE/IME, timer
and CPU state. Pending timer-3 IF is acknowledged, not reconstructible.
"""
def extend_irq(asm,decl,rows,base,cpu=7):
 prefix=f"cpu_arm{cpu}_"
 hook=["ldr r3,=0x0380fffc"] if cpu==7 else ["mrc p15,0,r3,c9,c1,0","mov r3,r3,lsr #12","mov r3,r3,lsl #12","add r3,r3,#0x3f00","add r3,r3,#0xfc"]
 for state in ('arm','thumb','user','masked_i','masked_ime','masked_ie'):
  name=prefix+'irq_entry_'+state
  decl.append(f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);')
  asm+=['.arm','.balign 4',f'.global {name}',f'.type {name}, %function',name+':',
   'push {r4-r11,lr}','sub sp,sp,#36','mov r7,sp','mov r4,r3','mrs r6,cpsr','str r0,[r7,#16]',
   'orr r1,r6,#0xc0','msr cpsr_c,r1','ldr r5,=0x04000200','ldr r2,[r5,#8]','str r2,[r7]',
   'mov r1,#0','str r1,[r5,#8]','ldr r2,[r5,#16]','str r2,[r7,#4]',
   *hook,'ldr r2,[r3]','str r2,[r7,#8]',f'ldr r2,={prefix}timer_irq_handler','str r2,[r3]',
   'ldr r3,=0x0400010c','ldr r2,[r3]','str r2,[r7,#12]','str r1,[r3]',
   f'ldr r8,={prefix}irq_capture','str r1,[r8]','ldr r2,=0xbaadc0de','str r2,[r8,#4]','str r2,[r8,#8]',
   'mov r2,#64','str r2,[r5,#16]','str r2,[r5,#20]',*(['mov r1,#0','str r1,[r5,#16]'] if state=='masked_ie' else []),
   'ldr r0,[r7,#16]','mov r10,r0',*(['eor r0,r0,#0xf0000000'] if state=='user' else []),f'orr r0,r0,#{0xdf if state=="masked_i" else 0x5f}','msr cpsr_fsxc,r0',f'mov r1,#{0 if state=="masked_ime" else 1}','str r1,[r5,#8]',
   'ldr r2,=0x00c0ff80','ldr r8,=2f','b 9f','.ltorg','9:']
  if state=='thumb':asm+=['adr r0,1f','orr r0,r0,#1','bx r0','.thumb','.balign 4','1:']
  if state=='user':asm+=['msr cpsr_c,#0x50','msr cpsr_f,r10','msr cpsr_c,#0xd3','sub r9,sp,r7']
  asm+=['str r2,[r3]','.rept 2048','nop','.endr']
  if state.startswith('masked_'):
   asm+=[f'ldr r0,={prefix}irq_capture','ldr r11,[r0]']
   if state=='masked_i':asm+=['mrs r0,cpsr','bic r0,r0,#0x80','msr cpsr_c,r0']
   elif state=='masked_ime':asm+=['mov r0,#1','str r0,[r5,#8]']
   else:asm+=['mov r0,#64','str r0,[r5,#16]']
   asm+=['b 8f','.ltorg','8:','.rept 2048','nop','.endr']
  if state=='thumb':asm+=['bx r8','.arm','.balign 4']
  asm+=['2:','mrs r0,cpsr','orr r0,r0,#0xc0','msr cpsr_c,r0','mov r1,#0','str r1,[r5,#8]',
    'ldr r3,=0x0400010c','str r1,[r3]','mov r1,#64','str r1,[r5,#20]',
    'ldr r2,[r7,#12]','str r2,[r3]',*hook,'ldr r2,[r7,#8]','str r2,[r3]',
    'ldr r2,[r7,#4]','str r2,[r5,#16]','ldr r2,[r7]','str r2,[r5,#8]',
    f'ldr r3,={prefix}irq_capture','ldr r0,[r3,#8]','ldr r2,=0xf00000ff','and r0,r0,r2',
    'ldr r1,[r3,#4]','and r1,r1,#0xff','ldr r2,[r3]','eor r2,r2,#1','orr r1,r1,r2,lsl #16',
    *(['cmp r9,#0','movne r9,#0x20000','orr r1,r1,r9'] if state=='user' else []),*(['cmp r11,#0','movne r11,#0x40000','orr r1,r1,r11'] if state.startswith('masked_') else []),'msr cpsr_fsxc,r6','mov sp,r7','str r0,[r4]','str r1,[r4,#4]','add sp,sp,#36','pop {r4-r11,lr}','bx lr','.ltorg']
  for flags in range(16):rows.append(dict(id=base+len(rows)+1,name=name,a=flags<<28,b=0,initial=0,
    result=(flags<<28)|({'thumb':0x7f,'user':0x50}.get(state,0x5f)),flags=0xd2,mask=0xffffffff,
    aux_kind='IRQ_control; count_error_shift16; User_SP_error_bit17; early_IRQ_bit18',source='https://mgba-emu.github.io/gbatek/#armcpuexceptions',
    architecture=('ARM7TDMI' if cpu==7 else 'ARM946E-S')+' NDS timer IRQ',verification_scope='real IRQ via BIOS callback; not raw vector/latency/priority coverage'))
 asm+=['.arm','.balign 4',f'.global {prefix}timer_irq_handler',prefix+'timer_irq_handler:',
   'ldr r0,=0x0400010c','mov r1,#0','strh r1,[r0,#2]','ldr r0,=0x04000214','mov r1,#64','str r1,[r0]',
   f'ldr r0,={prefix}irq_capture','mrs r1,cpsr','str r1,[r0,#4]','mrs r1,spsr','str r1,[r0,#8]',
   'and r2,r1,#0x1f','cmp r2,#0x10','bne 1f','bic r1,r1,#0x1f','orr r1,r1,#0x1f','msr spsr_c,r1','1:',
   'ldr r1,[r0]','add r1,r1,#1','str r1,[r0]','bx lr','.ltorg',
   '.bss','.balign 4',prefix+'irq_capture:','.space 12','.text']
