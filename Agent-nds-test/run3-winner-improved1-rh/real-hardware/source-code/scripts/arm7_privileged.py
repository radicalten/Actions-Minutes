"""Defined ARMv4T privileged state and synthetic exception-return matrices.
All original state is restored. User-mode escape and real exception ENTRY are
not inferred from these software-prepared return tests.
"""
MODES={'sys':0x1f,'fiq':0x11,'irq':0x12,'svc':0x13,'abt':0x17,'und':0x1b}
EXCEPT={k:v for k,v in MODES.items() if k!='sys'}
PAIRS=[(0,0xffffffff),(0xffffffff,0),(0x12345678,0x89abcdef),(0xa55a5aa5,0x5aa5a55a)]
PSRS=[(0xdf,0xf00000f3),(0xf00000df,0xd2),(0x600000f3,0x900000db),(0x900000d2,0x600000ff)]
MASK=0xf00000ff

def extend_privileged(asm,decl,rows,base,prefix="cpu_arm7_"):
 def begin(label):
  name=prefix+label
  decl.append(f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);')
  asm.extend(['.arm','.balign 4',f'.global {name}',f'.type {name}, %function',name+':',
   'push {r4-r11,lr}','sub sp,sp,#20','mov r7,sp','mov r4,r3','mrs r6,cpsr'])
  return name
 def switch(mode):asm.append(f'msr cpsr_c,#{0xc0|MODES[mode]}')
 def end():asm.extend(['msr cpsr_fsxc,r6','mov sp,r7','str r0,[r4]','str r1,[r4,#4]','add sp,sp,#20','pop {r4-r11,lr}','bx lr','.ltorg'])
 def row(name,a,b,result,aux,kind='second_banked_value',reference='armcpuregisterset',mask=0xffffffff):
  rows.append(dict(id=base+len(rows)+1,name=name,a=a,b=b,initial=0,result=result,flags=aux,mask=mask,aux_kind=kind,
   source='https://mgba-emu.github.io/gbatek/#'+reference,architecture='ARMv4T privileged semantics',verification_scope='software-prepared state; not exception entry'))
 # Every ordered pair of distinct physical SP/LR banks. SYS represents USR bank.
 for left in MODES:
  for right in MODES:
   if left==right:continue
   for reg in ('sp','lr'):
    name=begin(f'bank_{reg}_{left}_{right}');switch(left)
    asm.extend([f'mov r2,{reg}',f'mov {reg},r0']);switch(right)
    asm.extend([f'mov r5,{reg}',f'mov {reg},r1']);switch(left)
    asm.extend([f'mov r0,{reg}',f'mov {reg},r2']);switch(right)
    asm.extend([f'mov r1,{reg}',f'mov {reg},r5']);end()
    for a,b in PAIRS:row(name,a,b,a,b)
 for reg in range(8,13):
  name=begin(f'bank_r{reg}_sys_fiq');switch('sys')
  asm.extend([f'mov r2,r{reg}',f'mov r{reg},r0']);switch('fiq')
  asm.extend([f'mov r5,r{reg}',f'mov r{reg},r1']);switch('sys')
  asm.extend([f'mov r0,r{reg}',f'mov r{reg},r2']);switch('fiq')
  asm.extend([f'mov r1,r{reg}',f'mov r{reg},r5']);end()
  for a,b in PAIRS:row(name,a,b,a,b)
 for left in EXCEPT:
  for right in EXCEPT:
   if left==right:continue
   name=begin(f'spsr_banks_{left}_{right}');switch(left)
   asm.extend(['mrs r2,spsr','msr spsr_fsxc,r0']);switch(right)
   asm.extend(['mrs r5,spsr','msr spsr_fsxc,r1']);switch(left)
   asm.extend(['mrs r0,spsr','msr spsr_fsxc,r2']);switch(right)
   asm.extend(['mrs r1,spsr','msr spsr_fsxc,r5']);end()
   for a,b in PSRS:row(name,a,b,a,b,'second_SPSR',mask=MASK)
 for mode in EXCEPT:
  for field in ('f','c'):
   name=begin(f'spsr_field_{field}_{mode}');switch(mode)
   asm.extend(['mrs r2,spsr','msr spsr_fsxc,r0',f'msr spsr_{field},r1','mrs r0,spsr','msr spsr_fsxc,r2','mrs r1,spsr','eor r1,r1,r2']);end()
   for a,b in PSRS:
    expected=((b&0xf0000000)|(a&0xff)) if field=='f' else ((a&0xf0000000)|(b&0xff))
    row(name,a,b,expected,0,'SPSR_restore_xor',reference='armopcodespsrtransfermrsmsr')
 for mode in MODES:
  name=begin('cpsr_mode_'+mode)
  asm.append('msr cpsr_f,r0');switch(mode)
  asm.extend(['mrs r0,cpsr','ldr r2,=0xf00000ff','and r0,r0,r2','mov r1,#0']);end()
  for flags in range(16):row(name,flags<<28,0,(flags<<28)|0xc0|MODES[mode],0,'not_checked',mask=0,reference='armopcodespsrtransfermrsmsr')
 # Return from each exception bank, through each defined return form, to SYS.
 # ARM/Thumb targets exercise all NZCV states without overwriting them at target.
 for mode in EXCEPT:
  for op in ('movs','subs','ldm'):
   for state in ('arm','thumb'):
    name=begin(f'return_{op}_{mode}_{state}');switch(mode)
    asm.extend(['mrs r2,spsr','str r2,[r7]','str lr,[r7,#4]',f'orr r0,r0,#{0xdf if state=="arm" else 0xff}','msr spsr_fsxc,r0',
      'adr r1,1f','str r1,[r7,#8]','adr r2,2f'])
    if op=='movs':asm.extend(['mov lr,r1','movs pc,lr'])
    elif op=='subs':asm.extend(['add lr,r1,#4','subs pc,lr,#4'])
    else:asm.extend(['str r1,[r7,#12]','add r5,r7,#12','ldmia r5,{pc}^'])
    if state=='thumb':asm.extend(['.thumb','.balign 4','1:','mov r0,pc','bx r2','.arm','.balign 4','2:','ldr r2,[r7,#8]','sub r0,r0,r2'])
    else:asm.extend(['.arm','.balign 4','1:','mov r0,#4','2:'])
    asm.append('mrs r1,cpsr');switch(mode)
    asm.extend(['ldr r2,[r7]','msr spsr_fsxc,r2','ldr lr,[r7,#4]']);end()
    for flags in range(16):row(name,flags<<28,0,4,(flags<<28)|0xdf,'CPSR_after_target_BX_if_Thumb',reference='armcpuexceptions',mask=MASK)
