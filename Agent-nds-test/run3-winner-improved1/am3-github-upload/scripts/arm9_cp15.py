"""Non-invasive CP15 register/alias checks. MPU disabled during the check;
these are NOT MPU access-fault, cache-coherence or timing tests.
"""
def extend_cp15(asm,decl,rows,base):
 def begin(label):
  n='cpu_arm9_cp15_'+label;decl.append(f'extern "C" void {n}(unsigned,unsigned,unsigned,unsigned*);')
  asm.extend(['.arm','.balign 4',f'.global {n}',f'.type {n}, %function',n+':','push {r4-r11,lr}','mov r4,r3','mrs r6,cpsr','orr r2,r6,#0xc0','msr cpsr_c,r2',
   'mrc p15,0,r7,c1,c0,0','bic r2,r7,#1','mcr p15,0,r2,c1,c0,0'])
  return n
 def end():asm.extend(['mcr p15,0,r7,c1,c0,0','msr cpsr_fsxc,r6','str r0,[r4]','str r1,[r4,#4]','pop {r4-r11,lr}','bx lr','.ltorg'])
 def row(n,a,result):rows.append(dict(id=base+len(rows)+1,name=n,a=a,b=0,initial=0,result=result,flags=0,mask=0xffffffff,
  aux_kind='restored_registers_xor',source='https://mgba-emu.github.io/gbatek/#armcp15protectionunitpu',architecture='ARM946E-S CP15',verification_scope='register readback/aliases with MPU disabled; no access-permission enforcement claim'))
 for label,cn,op in [('dcacheable',2,0),('icacheable',2,1),('bufferable',3,0)]:
  n=begin(label);asm.extend([f'mrc p15,0,r8,c{cn},c0,{op}',f'mcr p15,0,r0,c{cn},c0,{op}',f'mrc p15,0,r0,c{cn},c0,{op}',f'mcr p15,0,r8,c{cn},c0,{op}',f'mrc p15,0,r1,c{cn},c0,{op}','eor r1,r1,r8','and r1,r1,#0xff','and r0,r0,#0xff']);end()
  for a in (0,1,2,0x80,0x55,0xaa,0x7f,0xff):row(n,a,a)
 for region in range(8):
  for direction in (0,1):
   n=begin(f'region{region}_alias{direction}')
   asm.extend([f'mrc p15,0,r8,c6,c{region},0',f'mrc p15,0,r9,c6,c{region},1',f'mcr p15,0,r0,c6,c{region},{direction}',f'mrc p15,0,r0,c6,c{region},{1-direction}',
    f'mcr p15,0,r8,c6,c{region},0',f'mcr p15,0,r9,c6,c{region},1',f'mrc p15,0,r1,c6,c{region},0','eor r1,r1,r8',f'mrc p15,0,r2,c6,c{region},1','eor r2,r2,r9','orr r1,r1,r2']);end()
   for a in (0,0x02000017,0x0380001f,0x0200002b,0x3f):row(n,a,a)
 for kind,short,extended in [('data',0,2),('code',1,3)]:
  for direction in ('short_to_ext','ext_to_short'):
   n=begin(f'{kind}_ap_{direction}')
   write,read=(short,extended) if direction=='short_to_ext' else (extended,short)
   asm.extend([f'mrc p15,0,r8,c5,c0,{short}',f'mrc p15,0,r9,c5,c0,{extended}','mov r2,#0',f'mcr p15,0,r2,c5,c0,{short}',f'mcr p15,0,r2,c5,c0,{extended}',
    f'mcr p15,0,r0,c5,c0,{write}',f'mrc p15,0,r0,c5,c0,{read}',f'mcr p15,0,r8,c5,c0,{short}',f'mcr p15,0,r9,c5,c0,{extended}',f'mrc p15,0,r1,c5,c0,{short}','eor r1,r1,r8',f'mrc p15,0,r2,c5,c0,{extended}','eor r2,r2,r9','orr r1,r1,r2']);end()
   for region in range(8):
    for ap in range(4):row(n,ap<<(region*(2 if direction=='short_to_ext' else 4)),ap<<(region*(4 if direction=='short_to_ext' else 2)))
