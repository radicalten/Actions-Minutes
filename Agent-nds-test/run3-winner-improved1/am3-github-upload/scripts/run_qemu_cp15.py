#!/usr/bin/env python3
"""ARM946 CP15 region/permission aliases; not cache/TCM/MMIO enforcement."""
import json,subprocess,os
from pathlib import Path
from emit_checker import emit
R=Path(__file__).resolve().parents[1];os.chdir(R)
allrows=json.loads(Path('src/generated/v5-vectors.json').read_text())
rows=[v for v in allrows if 5523<=v['id']<=5730]
assert len(rows)==208 and [v['id'] for v in rows]==list(range(5523,5731))
names=list(dict.fromkeys(v['name'] for v in rows));decl=[f'extern "C" void {n}(unsigned,unsigned,unsigned,unsigned*);' for n in names]
Path('src/qemu/cp15-oracle.cpp').write_text(emit(decl,rows,'cp15_oracle'))
Path('src/qemu/cp15main.cpp').write_text('''extern "C" unsigned cp15_oracle_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){unsigned d[6];unsigned bad=cp15_oracle_checks(d);if(d[4]!=208 || d[5])bad=1;
semi(4,bad?"FAIL: CP15 alias vectors\\n":"PASS: 208 CP15 alias vectors\\n");
unsigned args[2]={0x20026,bad?1u:0u};semi(0x20,args);for(;;){} }
''')
cc=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-g++'
subprocess.run([str(cc),'-mcpu=arm946e-s','-marm','-O2','-nostdlib','-fno-exceptions','-fno-rtti','-T','src/qemu/link.ld','src/qemu/start.s','src/qemu/runtime.cpp','src/qemu/cp15main.cpp','src/qemu/cp15-oracle.cpp','src/generated/v5.s','src/trace.cpp','-o','artifacts/cp15-oracle.elf'],check=True)
cmd=['qemu-system-arm','-M','integratorcp','-cpu','arm946','-nographic','-monitor','none','-serial','none','-audio','driver=none','-semihosting-config','enable=on,target=native','-kernel','artifacts/cp15-oracle.elf']
p=subprocess.run(cmd,capture_output=True,text=True,timeout=20);output=p.stdout+p.stderr
report={'cpu':'arm946','vectors':208,'excluded':'No cache bitmap, TCM, IRQ or MPU access-fault claim','status':'PASS' if p.returncode==0 and 'PASS: 208 CP15 alias vectors' in output else 'FAIL','output':output,'command':cmd}
Path('reports/qemu-cp15.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
raise SystemExit(0 if report['status']=='PASS' else 1)
