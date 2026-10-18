"""Legal register-controlled operand2: shifter carry vs arithmetic carry.
Reference is integer arithmetic, independently exercised under QEMU and DS.
"""
M=0xffffffff
COUNTS=(0,1,31,32,33,255,256,257)
def signed(x):return x-(1<<32) if x&(1<<31) else x
def shifted(x,kind,count,carry):
 n=count&255
 if not n:return x,carry
 if kind=='lsl':return ((x<<n)&M if n<32 else 0),((x>>(32-n))&1 if n<=32 else 0)
 if kind=='lsr':return (x>>n if n<32 else 0),((x>>(n-1))&1 if n<=32 else 0)
 if kind=='asr':return (signed(x)>>min(n,32))&M,(x>>min(n-1,31))&1
 n%=32
 return ((x>>n)|(x<<(32-n)))&M if n else x,(x>>((n-1)%32))&1

def reference(op,a,b,kind,count,carry,old_v):
 b,sc=shifted(b,kind,count,carry)
 if op in ('adc','add'):
  c_in=carry if op=='adc' else 0
  full=a+b+c_in;r=full&M;c=int(full>M);v=int(not -(1<<31)<=signed(a)+signed(b)+c_in<(1<<31))
 elif op in ('sbc','sub','rsb','rsc'):
  x,y=(b,a) if op in ('rsb','rsc') else (a,b)
  c_in=carry if op in ('sbc','rsc') else 1
  full=x-y-(1-c_in);r=full&M;c=int(full>=0);v=int(not -(1<<31)<=signed(x)-signed(y)-(1-c_in)<(1<<31))
 else:
  r={'and':a&b,'eor':a^b,'orr':a|b,'bic':a&(~b&M),'mov':b,'mvn':~b&M}[op];c=sc;v=old_v
 return r,(r&0x80000000)|(int(r==0)<<30)|(c<<29)|(v<<28)

def extend_register_operand2(asm,decl,rows,base,cpu,ops=('adc','sbc','and','eor'),scope='register_operand2_m9'):
 for op in ops:
  for kind in ('lsl','lsr','asr','ror'):
   name=f'cpu_arm{cpu}_{op}_register_operand2_{kind}'
   decl.append(f'extern "C" void {name}(unsigned,unsigned,unsigned,unsigned*);')
   asm+=['.arm','.balign 4',f'.global {name}',f'.type {name}, %function',name+':',
    'push {r4-r6,lr}','mov r4,r3','mrs r6,cpsr','msr cpsr_f,r2',
    f'{op}s r0,'+('' if op in ('mov','mvn') else 'r0,')+f'r1,{kind} r2','mrs r1,cpsr','str r0,[r4]','str r1,[r4,#4]',
    'msr cpsr_f,r6','pop {r4-r6,lr}','bx lr']
   for i,count in enumerate(COUNTS):
    for carry in (0,1):
     a,b=0x7fffffff,0x80000001;old_v=i&1
     result,flags=reference(op,a,b,kind,count,carry,old_v)
     rows.append(dict(id=base+len(rows)+1,name=name,a=a,b=b,
      initial=0xc0000000|(old_v<<28)|(carry<<29)|count,result=result,flags=flags,mask=0xf0000000,
      aux_kind='NZCV; initial also supplies Rs, whose low eight bits select shift count',
      shift_count=count,architecture='ARMv4T legal register-controlled operand2',
      verification_scope=scope,
      source='https://mgba-emu.github.io/gbatek/#armopcodesdataprocessingalu'))
