#!/usr/bin/env python3
"""Run legal Rockwrestler CPU groups independently; do not QEMU-oracle unpredictable cases."""
import json,os,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1];os.chdir(R)
cc=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-g++'
groups=json.loads((R/'src/groups.json').read_text())[2:13]
results=[]
for index,g in enumerate(groups):
 elf=f'artifacts/rock-oracle-{index:02}.elf'
 compile=[str(cc),'-mcpu=arm946e-s','-marm','-O2','-nostdlib','-fno-exceptions','-fno-rtti',f'-DORACLE_GROUP={index}',
 '-T','src/qemu/link.ld','src/qemu/start.s','src/qemu/runtime.cpp','src/trace.cpp','src/qemu/rock.cpp',
 'vendor/rockwrestler/src9/tests/armv4.s','vendor/rockwrestler/src9/tests/armv5.s',
 'vendor/rockwrestler/src9/framework/interwork.s','-o',elf]
 c=subprocess.run(compile,capture_output=True,text=True)
 (R/f'reports/qemu-rock-compile-{index:02}.log').write_text(c.stdout+c.stderr)
 c.check_returncode()
 cmd=['qemu-system-arm','-M','integratorcp','-cpu','arm946','-nographic','-monitor','none','-serial','none',
 '-audio','driver=none','-semihosting-config','enable=on,target=native','-kernel',elf]
 try:
  p=subprocess.run(cmd,capture_output=True,text=True,timeout=20);text=p.stdout+p.stderr
  result=dict(g,status='PASS' if p.returncode==0 and 'PASS: Rockwrestler CPU group' in text else 'FAIL',returncode=p.returncode,output=text,command=cmd)
 except subprocess.TimeoutExpired:result=dict(g,status='TIMEOUT',command=cmd)
 results.append(result);print(result['name'],result['status'],flush=True)
(R/'reports/qemu-rock.json').write_text(json.dumps({'cpu':'arm946','version':subprocess.check_output(['qemu-system-arm','--version'],text=True),
 'results':results,'excluded':{'group':13,'reason':'contains architecturally unpredictable, chip-specific LDM/STM cases'}},indent=2)+'\n')
raise SystemExit(0 if all(r['status']=='PASS' for r in results) else 1)
