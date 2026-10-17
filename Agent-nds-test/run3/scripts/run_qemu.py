#!/usr/bin/env python3
import json,subprocess,os
from pathlib import Path
R=Path(__file__).resolve().parents[1];os.chdir(R)
cc=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-g++'
subprocess.run([str(cc),'-mcpu=arm7tdmi','-marm','-O2','-nostdlib','-fno-exceptions','-fno-rtti','-T','src/qemu/link.ld','src/qemu/start.s','src/qemu/main.cpp','src/generated/cpu.cpp','src/generated/cpu.s','-o','artifacts/oracle.elf'],check=True)
version=subprocess.check_output(['qemu-system-arm','--version'],text=True)
cpus=subprocess.check_output(['qemu-system-arm','-cpu','help'],text=True)
(R/'reports/qemu-cpus.txt').write_text(cpus)
count=len(json.loads((R/'src/generated/vectors.json').read_text()))
results=[]
for cpu,role in [('arm946','ARM946 CPU functional model; not DS hardware'),('ti925t','ARMv4T proxy only; NOT ARM7TDMI verification')]:
 cmd=['qemu-system-arm','-M','integratorcp','-cpu',cpu,'-nographic','-monitor','none','-serial','none','-audio','driver=none','-semihosting-config','enable=on,target=native','-kernel','artifacts/oracle.elf']
 try:
  r=subprocess.run(cmd,capture_output=True,text=True,timeout=20)
  status='PASS' if r.returncode==0 and f'PASS: {count} CPU vectors' in r.stdout+r.stderr else 'FAIL'
  results.append(dict(cpu=cpu,role=role,status=status,returncode=r.returncode,output=r.stdout+r.stderr,command=cmd))
 except subprocess.TimeoutExpired: results.append(dict(cpu=cpu,role=role,status='TIMEOUT',command=cmd))
report=dict(version=version,exact_arm7tdmi='UNAVAILABLE',vectors=count,results=results)
(R/'reports/qemu.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
raise SystemExit(0 if all(r['status']=='PASS' for r in results) else 1)
