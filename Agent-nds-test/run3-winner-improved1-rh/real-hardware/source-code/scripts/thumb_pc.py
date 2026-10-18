"""Thumb PC-relative address/literal matrix; static literals, both instruction alignments."""
FLAGS=(0,0x20000000,0x90000000,0xf0000000)
PATTERNS=(0,0xffffffff,0x80000001,0x12345678)
def forms():
 for op in ('address_pc','literal_ldr'):
  for alignment in (0,2):
   for offset in (0,4,1020):
    for rd in (0,1):
     values=(0,) if op=='address_pc' else (0x46c04728,) if alignment==2 and offset==0 else PATTERNS
     for value in values:yield op,alignment,offset,rd,value

def extend_thumb_pc(asm,decl,rows,base,cpu):
 for op,alignment,offset,rd,value in forms():
  load=op=='literal_ldr';n=f'cpu_arm{cpu}_thumb_pc_{op}_a{alignment}_o{offset}_r{rd}_v{value:08x}'
  code=(0x4800 if load else 0xa000)|(rd<<8)|(offset//4)
  decl.append(f'extern "C" void {n}(unsigned,unsigned,unsigned,unsigned*);')
  asm+=['.arm','.balign 4',f'.global {n}',f'.type {n}, %function',n+':','push {r4-r8,lr}','mov r4,r3','mrs r6,cpsr','mov r7,sp',
   f'ldr r0,=0x{((~value)&0xffffffff):08x}','mov r1,r0',f'ldr r8,={n}_instruction','add r8,r8,#4','bic r8,r8,#3',
   'ldr r5,=2f',f'ldr r3,={n}_instruction+1','msr cpsr_f,r2','bx r3','.thumb','.balign 4']
  if alignment:asm+=['nop']
  asm+=[f'.global {n}_instruction',n+'_instruction:',f'.hword 0x{code:04x}','bx r5']
  if load:
   if alignment==2 and offset==0:asm+=['nop'] # Literal consists of the executed BX followed by NOP.
   else:asm += [f'.space {offset-alignment},0',f'.word 0x{value:08x}'] if offset-alignment else [f'.word 0x{value:08x}']
  asm+=['.arm','.balign 4','2:','mrs r2,cpsr','and r2,r2,#0xf0000000',
   (f'mov r0,r{rd}' if load else f'sub r0,r{rd},r8'),'cmp sp,r7','orrne r2,r2,#1','str r0,[r4]','str r2,[r4,#4]',
   'msr cpsr_f,r6','pop {r4-r8,lr}','bx lr','.ltorg']
  for f in FLAGS:
   rows.append(dict(id=base+len(rows)+1,name=n,a=0,b=value,initial=f,result=value if load else offset,flags=f,mask=0xf0000001,
    aux_kind='NZCV preserved; bit0 SP changed',operation=op,addressing='pc_relative',offset_bytes=offset,destination_register=rd,
    instruction_alignment_mod4=alignment,literal_value=value if load else None,expected_thumb_opcode=code,verification_scope='thumb_pc',
    result_interpretation='literal word' if load else 'destination minus aligned instruction-address-plus-four',
    guard_policy='poisoned destination; unchanged SP; static read-only literal fixture',
    architecture='ARMv4T Thumb.6 literal LDR and Thumb.12 PC address',source='https://mgba-emu.github.io/gbatek/#thumbopcodesmemoryaddressingaddpcsp'))
