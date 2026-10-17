"""Thumb SP adjustment and SP-relative address generation, finite immediate matrix."""
FLAGS=(0,0x20000000,0x90000000,0xf0000000)
def forms():
 for op in ('add_sp','sub_sp'):
  for offset in (0,4,508):yield op,offset,13
 for rd in (0,1):
  for offset in (0,4,1020):yield 'address_sp',offset,rd

def extend_thumb_sp(asm,decl,rows,base,cpu):
 for op,offset,rd in forms():
  n=f'cpu_arm{cpu}_thumb_sp_{op}_{offset}_r{rd}'
  code=((0xb080 if op=='sub_sp' else 0xb000)|(offset//4)) if rd==13 else 0xa800|(rd<<8)|(offset//4)
  instruction=f'add r{rd},sp,#{offset}' if rd!=13 else f'{"sub" if op=="sub_sp" else "add"} sp,#{offset}'
  decl.append(f'extern "C" void {n}(unsigned,unsigned,unsigned,unsigned*);')
  asm+=['.arm','.balign 4',f'.global {n}',f'.type {n}, %function',n+':',
   'push {r4-r8,lr}','mov r4,r3','mrs r6,cpsr','mov r7,sp','sub sp,sp,#256','add sp,sp,r0','mov r8,sp',
   'mvn r0,#0','mvn r1,#0','adr r5,2f','adr r3,1f','orr r3,r3,#1','msr cpsr_f,r2','bx r3',
   '.thumb','.balign 4','1:',f'.global {n}_instruction',n+'_instruction:',instruction,'nop','bx r5','nop',
   '.arm','.balign 4','2:','mrs r2,cpsr','and r2,r2,#0xf0000000',f'sub r0,{"sp" if rd==13 else "r"+str(rd)},r8']
  if rd!=13:asm+=['cmp sp,r8','orrne r2,r2,#1']
  asm+=['str r0,[r4]','str r2,[r4,#4]','mov sp,r7','msr cpsr_f,r6','pop {r4-r8,lr}','bx lr','.ltorg']
  for displacement in (0,4,16,124):
   for f in FLAGS:
    rows.append(dict(id=base+len(rows)+1,name=n,a=displacement,b=0,initial=f,result=(-offset if op=='sub_sp' else offset)&0xffffffff,flags=f,mask=0xf0000001,
     aux_kind='NZCV preserved; bit0 SP changed during address generation',operation=op,addressing='sp_relative',offset_bytes=offset,destination_register=rd,
     expected_thumb_opcode=code,verification_scope='thumb_sp',result_interpretation='unsigned destination delta from initial SP',
     guard_policy='SP unchanged for address generation; original stack restored before return; no dereference of adjusted SP',
     architecture='ARMv4T Thumb.12 SP address and Thumb.13 SP adjustment',source='https://mgba-emu.github.io/gbatek/#thumbopcodesmemoryaddressingaddpcsp'))
