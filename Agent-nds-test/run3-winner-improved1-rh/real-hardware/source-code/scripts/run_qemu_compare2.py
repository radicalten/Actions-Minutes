#!/usr/bin/env python3
"""Cross-check the 256 portable register-compare2 vectors; excludes NDS IRQ MMIO and
ARM7 implementation-specific expectations. ti925t remains a proxy, not ARM7TDMI.
"""
import json,os,subprocess
from pathlib import Path
from emit_checker import emit
R=Path(__file__).resolve().parents[1];os.chdir(R)
rows=[v for v in json.loads(Path('src/generated/arm7-vectors.json').read_text()) if v.get('verification_scope')=='compare_operand2_m10']
assert len(rows)==256
names=list(dict.fromkeys(v['name'] for v in rows));decl=[f'extern "C" void {n}(unsigned,unsigned,unsigned,unsigned*);' for n in names]
Path('src/qemu/compare2.cpp').write_text(emit(decl,rows,'compare2'))
Path('src/qemu/compare2-main.cpp').write_text('''extern "C" unsigned compare2_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){unsigned d[6];unsigned bad=compare2_checks(d);
if(d[4]!=256 || d[5])bad=1;
semi(4,bad?"FAIL: compare2 vectors\\n":"PASS: 256 compare2 vectors\\n");
unsigned args[2]={0x20026,bad?1u:0u};semi(0x20,args);for(;;){} }
''')
cc=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-g++'
subprocess.run([str(cc),'-mcpu=arm7tdmi','-marm','-O2','-nostdlib','-fno-exceptions','-fno-rtti','-T','src/qemu/link.ld','src/qemu/start.s','src/qemu/runtime.cpp','src/trace.cpp','src/qemu/compare2.cpp','src/qemu/compare2-main.cpp','src/generated/arm7.s','-o','artifacts/compare2-oracle.elf'],check=True)
results=[]
for cpu in ('arm946','ti925t'):
 cmd=['qemu-system-arm','-M','integratorcp','-cpu',cpu,'-nographic','-monitor','none','-serial','none','-audio','driver=none','-semihosting-config','enable=on,target=native','-kernel','artifacts/compare2-oracle.elf']
 try:
  p=subprocess.run(cmd,capture_output=True,text=True,timeout=20);output=p.stdout+p.stderr
  status='PASS' if p.returncode==0 and 'PASS: 256 compare2 vectors' in output else 'FAIL'
 except subprocess.TimeoutExpired:status='TIMEOUT';output='No terminal oracle result'
 results.append(dict(cpu=cpu,status=status,output=output,command=cmd))
report={'vectors':256,'scope':'register-controlled TST/TEQ/CMP/CMN compare2; low-byte shift counts; NZCV','excluded':'DS MMIO/BIOS and CPU-specific behavior','exact_arm7tdmi':'UNAVAILABLE','ti925t_role':'ARMv4T proxy only','results':results}
Path('reports/qemu-compare2.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
raise SystemExit(0 if all(r['status']=='PASS' for r in results) else 1)
