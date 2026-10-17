#!/usr/bin/env python3
"""Separate ARM946 low-vector SWI/undefined/MPU-abort oracle, not DS hardware."""
import json,os,subprocess
from pathlib import Path
from emit_checker import emit
R=Path(__file__).resolve().parents[1];os.chdir(R)
rows=[v for v in json.loads(Path('src/generated/v5-vectors.json').read_text()) if v['name'].startswith('cpu_arm9_raw_')]
assert len(rows)==256
names=list(dict.fromkeys(v['name'] for v in rows));decl=[f'extern "C" void {n}(unsigned,unsigned,unsigned,unsigned*);' for n in names]
code=emit(decl,rows,'exception_oracle')
code='extern \"C\" unsigned semi(unsigned,const void*);\n'+code

code='''extern "C" unsigned semi(unsigned,const void*);
static void val(const char* tag,unsigned n){char t[10];for(unsigned k=0;k<8;k++)t[k]="0123456789abcdef"[(n>>(28-4*k))&15];t[8]=10;t[9]=0;semi(4,tag);semi(4,t);}
'''+code
code=code.replace('if(bad) {++failed;', 'if(bad) {val("BAD ",id);val("GOT ",out[0]);val("EXP ",v.result);val("AUX ",out[1]);val("AEXP ",v.flags);++failed;')
Path('src/qemu/exceptions.cpp').write_text(code)
Path('src/qemu/exceptions-main.cpp').write_text('''extern "C" unsigned exception_oracle_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){unsigned d[6];unsigned bad=exception_oracle_checks(d);if(d[4]!=256 || d[5])bad=1;
semi(4,bad?"FAIL: raw exception vectors\\n":"PASS: 256 raw exception vectors\\n");
unsigned args[2]={0x20026,bad?1u:0u};semi(0x20,args);for(;;){} }
''')
# QEMU ARM946 does not implement the NDS ITCM control register. Use writable
# low RAM for the same vector instructions; do not claim TCM validation.
assembly=Path('src/generated/v5.s').read_text()
assembly=assembly.replace('mrc p15,0,r9,c9,c1,1','mov r9,#0').replace('mrc p15,0,r2,c9,c1,1','mov r2,#0').replace('mcr p15,0,r2,c9,c1,1','nop').replace('mcr p15,0,r9,c9,c1,1','nop')
Path('src/qemu/exceptions-ram.s').write_text(assembly)
cc=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-g++'
subprocess.run([str(cc),'-mcpu=arm946e-s','-marm','-O2','-nostdlib','-fno-exceptions','-fno-rtti','-T','src/qemu/link.ld','src/qemu/start.s','src/qemu/runtime.cpp','src/qemu/exceptions-main.cpp','src/qemu/exceptions.cpp','src/qemu/exceptions-ram.s','src/trace.cpp','-o','artifacts/exceptions-oracle.elf'],check=True)
cmd=['qemu-system-arm','-M','integratorcp','-cpu','arm946','-nographic','-monitor','none','-serial','none','-audio','driver=none','-semihosting-config','enable=on,target=native','-kernel','artifacts/exceptions-oracle.elf']
try:
 p=subprocess.run(cmd,capture_output=True,text=True,timeout=10);output=p.stdout+p.stderr
 status='PASS' if p.returncode==0 and 'PASS: 256 raw exception vectors' in output else 'FAIL'
except subprocess.TimeoutExpired as e:status='TIMEOUT';output=((e.stdout or b'')+(e.stderr or b'')).decode(errors='replace')+'\nOracle did not complete; not PASS'
import re
mismatches=[dict(zip(('id','observed','expected','aux','expected_aux'),[int(x,16) for x in match])) for match in re.findall(r'BAD ([0-9a-f]+)\nGOT ([0-9a-f]+)\nEXP ([0-9a-f]+)\nAUX ([0-9a-f]+)\nAEXP ([0-9a-f]+)',output)]
abort_misses=[m for m in mismatches if m['id']>=5859]
capability_gap=len(abort_misses)==128 and len(mismatches)==128 and all(m['aux']&0x200 for m in abort_misses)
if capability_gap:status='PARTIAL_UNSUPPORTED'
report={'passed':128 if capability_gap else 256 if status=='PASS' else None,'unsupported':128 if capability_gap else 0,'mpu_enable_readback':'ZERO' if capability_gap else 'see observations','mismatches':mismatches,'cpu':'arm946','vectors':256,'status':status,'output':output[-6000:],'output_characters':len(output),'adaptation':'ITCM control register accesses replaced by NOP/mov; vectors use low RAM; no TCM claim','command':cmd,'scope':'ARM946 low-vector exception model; not DS hardware'}
Path('reports/qemu-exceptions.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
raise SystemExit(0 if status=='PASS' else 1)
