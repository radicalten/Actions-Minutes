"""Raw ARM9 low-vector exception entry using a temporary ITCM vector pair.
Preserves control/ITCM registers, vector words, source SPSR/LR and caller state.
SWI uses harmless BIOS Sqrt number if an emulator incorrectly routes it to HLE.
"""
def extend_exceptions(asm,decl,rows,base,kinds=('swi',)):
 for kind in kinds:
  vector,mode={'swi':(8,0x13),'undefined':(4,0x1b),'data_abort':(16,0x17),'prefetch_abort':(12,0x17)}[kind]
  abort=kind.endswith('abort');frame=100 if abort else 44
  for state in ('arm','thumb'):
   for check in ('entry','link_return'):
    n=f'cpu_arm9_raw_{kind}_{state}_{check}';decl.append(f'extern "C" void {n}(unsigned,unsigned,unsigned,unsigned*);')
    asm+=['.arm','.balign 4',f'.global {n}',f'.type {n}, %function',n+':','push {r4-r11,lr}',f'sub sp,sp,#{frame}','mov r7,sp','mov r4,r3','mrs r6,cpsr','str r0,[r7,#8]',
     'orr r2,r6,#0xc0','msr cpsr_c,r2',f'msr cpsr_c,#{0xc0|mode}','mrs r2,spsr','str r2,[r7]','str lr,[r7,#4]',
     'orr r2,r6,#0xc0','msr cpsr_c,r2','mrc p15,0,r8,c1,c0,0','mrc p15,0,r9,c9,c1,1','mov r2,#0x0c','mcr p15,0,r2,c9,c1,1',
     'bic r2,r8,#0x2000','bic r2,r2,#0x1000','bic r2,r2,#5','bic r2,r2,#0x80000','orr r2,r2,#0x40000','mcr p15,0,r2,c1,c0,0',
     f'mov r5,#{vector}','ldr r10,[r5]','ldr r11,[r5,#4]','ldr r2,=0xe51ff004','str r2,[r5]',f'ldr r2,=cpu_arm9_raw_{kind}_handler','str r2,[r5,#4]',
     'mov r2,#0','mcr p15,0,r2,c7,c10,4','mcr p15,0,r2,c7,c5,0','ldr r3,=cpu_arm9_exception_capture','str r2,[r3]',
     'ldr r2,=0xbaadc0de','str r2,[r3,#4]','str r2,[r3,#8]','str r2,[r3,#12]','adr r2,1f','str r2,[r7,#20]','adr r5,2f',
     'ldr r0,[r7,#8]','orr r0,r0,#0xdf','msr cpsr_fsxc,r0']
    if abort:
     # Save both AP views for emulators with broken aliases, and all regions.
     asm+=['ldr r3,=0x021f0000','ldr r2,[r3]','str r2,[r7,#24]','mrc p15,0,r2,c5,c0,2','str r2,[r7,#28]',
      'mrc p15,0,r2,c5,c0,3','str r2,[r7,#32]','mrc p15,0,r2,c5,c0,0','str r2,[r7,#68]','mrc p15,0,r2,c5,c0,1','str r2,[r7,#72]']
     for region in range(8):asm += [f'mrc p15,0,r2,c6,c{region},0',f'str r2,[r7,#{36+4*region}]','mov r2,#0',f'mcr p15,0,r2,c6,c{region},0']
     asm+=['mov r2,#0x3f','mcr p15,0,r2,c6,c0,0','ldr r2,=0x021f0017','mcr p15,0,r2,c6,c7,0',
      'ldr r2,=0x30000003' if kind=='prefetch_abort' else 'mov r2,#3','mcr p15,0,r2,c5,c0,2',
      'mov r2,#3' if kind=='prefetch_abort' else 'ldr r2,=0x30000003','mcr p15,0,r2,c5,c0,3',
      'mov r12,r5','ldr r5,=0x021f0000']
     if kind=='prefetch_abort':
      asm += ['ldr r2,=0xe12fff1c' if state=='arm' else 'ldr r2,=0x46c04760','str r2,[r5]','str r5,[r7,#20]',
       'ldr r3,=cpu_arm9_exception_capture','str r12,[r3,#16]']
     asm += ['mov r2,#0','mcr p15,0,r2,c7,c10,4','mcr p15,0,r2,c7,c5,0','mrc p15,0,r2,c1,c0,0','orr r2,r2,#1','mcr p15,0,r2,c1,c0,0','mrc p15,0,r2,c1,c0,0','str r2,[r7,#76]']
    if state=='thumb' and kind!='prefetch_abort':asm+=['adr r0,0f','orr r0,r0,#1','bx r0','.thumb','.balign 4','0:']
    if kind=='prefetch_abort':
     asm += ['1:']
     if state=='thumb':asm+=['orr r5,r5,#1']
     asm += ['bx r5']
    else:
     ins=('svc #0x0d0000' if state=='arm' else 'svc #0x0d') if kind=='swi' else ('ldr r0,[r5]' if kind=='data_abort' else ('.word 0xe7f000f0' if state=='arm' else '.hword 0xde00'))
     asm+=['1:',ins]
     if state=='thumb':asm+=[f'bx {"r12" if abort else "r5"}',f'bx {"r12" if abort else "r5"}','.arm','.balign 4']
    # All variants land here, including the safe MPU-not-enforced fallback.
    asm+=['2:','mrs r5,cpsr','str r5,[r7,#16]']
    if abort:
     asm += ['mrc p15,0,r2,c1,c0,0','bic r2,r2,#1','mcr p15,0,r2,c1,c0,0','ldr r3,=0x021f0000','ldr r2,[r7,#24]','str r2,[r3]']
     for region in range(8):asm += [f'ldr r2,[r7,#{36+4*region}]',f'mcr p15,0,r2,c6,c{region},0']
     for slot,op in ((68,0),(72,1),(28,2),(32,3)):asm += [f'ldr r2,[r7,#{slot}]',f'mcr p15,0,r2,c5,c0,{op}']

    asm+=[f'mov r5,#{vector}','str r10,[r5]','str r11,[r5,#4]','ldr r0,[r5]','eor r0,r0,r10','ldr r1,[r5,#4]','eor r1,r1,r11','orr r0,r0,r1','str r0,[r7,#24]',
     'mov r2,#0','mcr p15,0,r2,c7,c10,4','mcr p15,0,r2,c7,c5,0','mcr p15,0,r9,c9,c1,1','mcr p15,0,r8,c1,c0,0','mrc p15,0,r2,c1,c0,0','eor r2,r2,r8','str r2,[r7,#12]','mrc p15,0,r2,c9,c1,1','eor r2,r2,r9','ldr r3,[r7,#12]','orr r2,r2,r3','str r2,[r7,#12]',
     f'msr cpsr_c,#{0xc0|mode}','ldr r2,[r7]','msr spsr_fsxc,r2','ldr lr,[r7,#4]','orr r2,r6,#0xc0','msr cpsr_c,r2',
     'ldr r3,=cpu_arm9_exception_capture','ldr r2,=0xf00000ff']
    if check=='entry':asm+=['ldr r0,[r3,#4]','and r0,r0,r2','ldr r1,[r3,#8]','and r1,r1,r2']
    else:asm+=['ldr r0,[r3,#12]','ldr r1,[r7,#20]','sub r0,r0,r1','ldr r1,[r7,#16]','and r1,r1,r2']
    asm+=['ldr r2,[r3]','cmp r2,#1','orrne r1,r1,#0x100','ldr r2,[r7,#12]','cmp r2,#0','orrne r1,r1,#0x400','ldr r2,[r7,#24]','cmp r2,#0','orrne r1,r1,#0x800',*(['ldr r2,[r7,#76]','tst r2,#1','orreq r1,r1,#0x200'] if abort else []),'msr cpsr_fsxc,r6','mov sp,r7','str r0,[r4]','str r1,[r4,#4]',f'add sp,sp,#{frame}','pop {r4-r11,lr}','bx lr','.ltorg']
    for flags in range(16):
     f=flags<<28
     rows.append(dict(id=base+len(rows)+1,name=n,a=f,b=0,initial=0,result=f|0xc0|mode if check=='entry' else (8 if kind=='data_abort' else 4 if kind=='prefetch_abort' or state=='arm' else 2),
      flags=f|(0xff if state=='thumb' and check=='entry' else 0xdf),mask=0xf0000fff,
      aux_kind='SPSR_or_sentinel; count_error_bit8; MPU_not_enabled_bit9; control_restore_bit10; vector_restore_bit11' if check=='entry' else 'returned_CPSR; count_error_bit8; MPU_not_enabled_bit9; control_restore_bit10; vector_restore_bit11',source='https://mgba-emu.github.io/gbatek/#armcpuexceptions',architecture='ARM946E-S low ITCM vectors',verification_scope='real exception entry; BIOS bypass; count error bit8; MPU enable error bit9'))
  asm+=['.arm','.balign 4',f'.global cpu_arm9_raw_{kind}_handler',f'cpu_arm9_raw_{kind}_handler:',
   'ldr r3,=cpu_arm9_exception_capture','mrs r0,cpsr','str r0,[r3,#4]','mrs r0,spsr','str r0,[r3,#8]','str lr,[r3,#12]',
   'ldr r0,[r3]','add r0,r0,#1','str r0,[r3]']
  if kind=='data_abort':
   asm+=['mrs r0,spsr','tst r0,#0x20','subne lr,lr,#6','subeq lr,lr,#4','movs pc,lr']
  elif kind=='prefetch_abort':
   asm+=['mrs r0,spsr','bic r0,r0,#0x20','msr spsr_c,r0','ldr lr,[r3,#16]','movs pc,lr']
  else:asm+=['movs pc,lr']
  asm+=['.ltorg']
 asm+=['.bss','.balign 4','cpu_arm9_exception_capture:','.space 20','.text']
