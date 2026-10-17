#!/usr/bin/env python3
"""Thumb SP oracle: actual ARM9 and ARM7 profiles, checking SP adjustments, addresses and preservation."""
import hashlib,json,os,subprocess
from pathlib import Path
from emit_checker import emit
R=Path(__file__).resolve().parents[1];os.chdir(R)
cc=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-g++';results=[]
for profile,arch,models in [('v5','arm946e-s',('arm946',)),('arm7','arm7tdmi',('arm946','ti925t'))]:
 rows=[v for v in json.loads(Path(f'src/generated/{profile}-vectors.json').read_text()) if v.get('verification_scope')=='thumb_sp'];assert len(rows)==192
 decl=[f'extern "C" void {n}(unsigned,unsigned,unsigned,unsigned*);' for n in dict.fromkeys(v['name'] for v in rows)]
 stem='thumb-sp-'+profile
 Path(f'src/qemu/{stem}.cpp').write_text(emit(decl,rows,'thumb_oracle'))
 Path('src/qemu/thumb-sp-main.cpp').write_text('''extern "C" unsigned thumb_oracle_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){unsigned d[6];unsigned bad=thumb_oracle_checks(d);if(d[4]!=192||d[5])bad=1;
semi(4,bad?"FAIL: Thumb SP\\n":"PASS: 192 Thumb SP\\n");unsigned args[2]={0x20026,bad?1u:0u};semi(0x20,args);for(;;){}}
''')
 elf=f'artifacts/{stem}.elf'
 subprocess.run([str(cc),'-mcpu='+arch,'-marm','-O2','-nostdlib','-fno-exceptions','-fno-rtti','-T','src/qemu/link.ld','src/qemu/start.s','src/qemu/runtime.cpp','src/trace.cpp',f'src/qemu/{stem}.cpp','src/qemu/thumb-sp-main.cpp',f'src/generated/{profile}.s','-o',elf],check=True)
 for model in models:
  cmd=['qemu-system-arm','-M','integratorcp','-cpu',model,'-nographic','-monitor','none','-serial','none','-audio','driver=none','-semihosting-config','enable=on,target=native','-kernel',elf]
  try:
   p=subprocess.run(cmd,capture_output=True,text=True,timeout=20);output=p.stdout+p.stderr;status='PASS' if p.returncode==0 and 'PASS: 192 Thumb SP' in output else 'FAIL'
  except subprocess.TimeoutExpired:status='TIMEOUT';output='No terminal result'
  results.append(dict(profile=profile,cpu=model,status=status,output=output,command=cmd,elf_sha256=hashlib.sha256(Path(elf).read_bytes()).hexdigest()))
r={'vectors_per_profile':192,'exact_arm7tdmi':'UNAVAILABLE','ti925t_role':'ARMv4T proxy only','scope':'Thumb SP subset only; no DS BIOS/MMIO/timing claim','results':results}
Path('reports/qemu-thumb-sp.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
raise SystemExit(0 if all(x['status']=='PASS' for x in results) else 1)
