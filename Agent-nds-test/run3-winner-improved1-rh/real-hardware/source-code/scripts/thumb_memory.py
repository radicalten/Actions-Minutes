"""Aligned Thumb memory addressing, legal load aliases, flags and guard checks."""
M=0xffffffff
GUARD=0xa5a5a5a5
PATTERNS=(0x80017f80,0xff007fff,0x12345678,0x80008000)
FLAGS=(0,0x20000000,0x90000000,0xf0000000)
WIDTH={'ldr':4,'str':4,'ldrb':1,'strb':1,'ldrh':2,'strh':2,'ldrsb':1,'ldrsh':2}
REG={'str':0x5000,'strh':0x5200,'strb':0x5400,'ldrsb':0x5600,'ldr':0x5800,'ldrh':0x5a00,'ldrb':0x5c00,'ldrsh':0x5e00}
IMM={'str':0x6000,'ldr':0x6800,'strb':0x7000,'ldrb':0x7800,'strh':0x8000,'ldrh':0x8800}
def forms():
 for op in ('ldr','ldrb','ldrh','str','strb','strh'):
  for offset in (0,WIDTH[op],31*WIDTH[op]):yield op,'immediate',offset,0
 for op in ('ldr','str'):
  for offset in (0,4,1020):yield op,'sp',offset,0
 for op in ('ldr','ldrb','ldrh','ldrsb','ldrsh'):
  offsets=(0,4,128) if WIDTH[op]==4 else (0,1,3) if WIDTH[op]==1 else (0,2,62)
  for offset in offsets:
   for rd in (0,1,2):yield op,'register',offset,rd
 for op in ('str','strb','strh'):
  for offset in ((0,4,128) if WIDTH[op]==4 else (0,1,3) if WIDTH[op]==1 else (0,2,62)):yield op,'register',offset,0

def expected(op,a,offset):
 shift=(offset&3)*8;bits=WIDTH[op]*8;mask=(1<<bits)-1
 if op.startswith('str'):return ((GUARD&~(mask<<shift))|((a&mask)<<shift))&M
 value=(a>>shift)&mask
 if op in ('ldrsb','ldrsh') and value&(1<<(bits-1)):value-=1<<bits
 return value&M

def extend_thumb_memory(asm,decl,rows,base,cpu):
 for op,mode,offset,rd in forms():
  n=f'cpu_arm{cpu}_thumb_mem_{op}_{mode}_{offset}_r{rd}';load=op.startswith('ldr');aligned=offset&~3
  decl.append(f'extern "C" void {n}(unsigned,unsigned,unsigned,unsigned*);')
  operand=f'[sp,#{offset}]' if mode=='sp' else '[r1,r2]' if mode=='register' else f'[r1,#{offset}]'
  opcode=(REG[op]|(2<<6)|(1<<3)|rd) if mode=='register' else ((0x9800 if load else 0x9000)|(offset//4)) if mode=='sp' else IMM[op]|((offset//WIDTH[op])<<6)|(1<<3)|rd
  asm+=['.arm','.balign 4',f'.global {n}',f'.type {n}, %function',n+':',
   'push {r4-r10,lr}','mov r4,r3','mrs r6,cpsr','mov r8,r2','mov r9,r0','mov r10,r1','sub sp,sp,#1152',
   'mov r0,sp','mov r1,r10','mov r2,#288','9:','str r1,[r0],#4','subs r2,r2,#1','bne 9b','add sp,sp,#16','mov r7,sp']
  if load:asm+=[f'str r9,[r7,#{aligned}]']
  asm+=[('mvn r0,r9' if load and rd==0 else 'mov r0,r9'),'mov r1,r7',f'ldr r2,={offset}','adr r5,2f','adr r3,1f','orr r3,r3,#1','msr cpsr_f,r8','bx r3',
   '.thumb','.balign 4','1:',f'.global {n}_instruction',n+'_instruction:',f'{op} r{rd},{operand}','nop','bx r5','nop','.arm','.balign 4','2:',
   'mrs r3,cpsr','and r8,r3,#0xf0000000']
  asm+=([f'mov r9,r{rd}'] if load else [f'ldr r9,[r7,#{aligned}]'])
  if not(load and rd==1):asm+=['cmp r1,r7','orrne r8,r8,#2']
  if not(load and rd==2):asm+=[f'ldr r3,={offset}','cmp r2,r3','orrne r8,r8,#4']
  asm+=['cmp sp,r7','orrne r8,r8,#8',f'ldr r0,[r7,#{aligned-4}]','cmp r0,r10','orrne r8,r8,#1',
   f'ldr r0,[r7,#{aligned+4}]','cmp r0,r10','orrne r8,r8,#1','str r9,[r4]','str r8,[r4,#4]',
   'mov sp,r7','sub sp,sp,#16','add sp,sp,#1152','msr cpsr_f,r6','pop {r4-r10,lr}','bx lr','.ltorg']
  for a in PATTERNS:
   for f in FLAGS:
    rows.append(dict(id=base+len(rows)+1,name=n,a=a,b=GUARD,initial=f,result=expected(op,a,offset),flags=f,mask=0xf000000f,
     aux_kind='NZCV; bit0 adjacent guard changed; bit1 base changed; bit2 offset changed; bit3 SP changed',
     operation=op,addressing=mode,offset_bytes=offset,access_bytes=WIDTH[op],destination_register=rd,
     alias='destination_base' if rd==1 else 'destination_offset' if rd==2 else 'none',
     expected_thumb_opcode=opcode,load_destination_poisoned=bool(load and rd==0),guard_policy='aligned target word neighbors; overwritten load destination exempt from register preservation',
     architecture='ARMv4T Thumb aligned scratch accesses',verification_scope='thumb_memory',
     source='https://mgba-emu.github.io/gbatek/#thumbopcodesmemoryloadstoreldrstr'))
