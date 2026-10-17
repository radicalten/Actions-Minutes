#!/usr/bin/env python3
"""Cross-check the 956 portable privileged vectors; excludes NDS IRQ MMIO and
ARM7 implementation-specific expectations. ti925t remains a proxy, not ARM7TDMI.
"""
import json,os,subprocess
from pathlib import Path
from emit_checker import emit
R=Path(__file__).resolve().parents[1];os.chdir(R)
rows=[v for v in json.loads(Path('src/generated/arm7-vectors.json').read_text()) if v.get('verification_scope')=='software-prepared state; not exception entry']
assert len(rows)==956
names=list(dict.fromkeys(v['name'] for v in rows));decl=[f'extern "C" void {n}(unsigned,unsigned,unsigned,unsigned*);' for n in names]
Path('src/qemu/privileged.cpp').write_text(emit(decl,rows,'privileged'))
Path('src/qemu/privileged-main.cpp').write_text('''extern "C" unsigned privileged_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){unsigned d[6];unsigned bad=privileged_checks(d);
if(d[4]!=956 || d[5])bad=1;
semi(4,bad?"FAIL: privileged vectors\\n":"PASS: 956 privileged vectors\\n");
unsigned args[2]={0x20026,bad?1u:0u};semi(0x20,args);for(;;){} }
''')
cc=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-g++'
subprocess.run([str(cc),'-mcpu=arm7tdmi','-marm','-O2','-nostdlib','-fno-exceptions','-fno-rtti','-T','src/qemu/link.ld','src/qemu/start.s','src/qemu/runtime.cpp','src/trace.cpp','src/qemu/privileged.cpp','src/qemu/privileged-main.cpp','src/generated/arm7.s','-o','artifacts/privileged-oracle.elf'],check=True)
results=[]
for cpu in ('arm946','ti925t'):
 cmd=['qemu-system-arm','-M','integratorcp','-cpu',cpu,'-nographic','-monitor','none','-serial','none','-audio','driver=none','-semihosting-config','enable=on,target=native','-kernel','artifacts/privileged-oracle.elf']
 try:
  p=subprocess.run(cmd,capture_output=True,text=True,timeout=20);output=p.stdout+p.stderr
  status='PASS' if p.returncode==0 and 'PASS: 956 privileged vectors' in output else 'FAIL'
 except subprocess.TimeoutExpired:status='TIMEOUT';output='No terminal oracle result'
 results.append(dict(cpu=cpu,status=status,output=output,command=cmd))
report={'vectors':956,'scope':'portable software-prepared bank/PSR/return semantics only','excluded':'NDS real IRQ cases; ARM7-specific alignment/PC expectations','exact_arm7tdmi':'UNAVAILABLE','ti925t_role':'ARMv4T proxy only','results':results}
Path('reports/qemu-privileged.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
raise SystemExit(0 if all(r['status']=='PASS' for r in results) else 1)
