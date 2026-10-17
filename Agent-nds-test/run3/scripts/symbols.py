#!/usr/bin/env python3
import json,os,subprocess
from pathlib import Path
r=Path(__file__).resolve().parents[1]
nm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-nm'
lines=subprocess.check_output([str(nm),str(r/'artifacts/arm9.elf')],text=True).splitlines()
symbols={s[2]:int(s[0],16) for l in lines if len(s:=l.split())==3 and s[0][0] in '0123456789abcdef'}
sizes={'val_CPSR':4,'val_IPCSYNC':2,'val_IPCFIFOCNT':2,'FIFO_length':2,'val_IME':4,'val_IE':4,'val_IF':4,'val_cp15':72}
(r/'artifacts/observations.json').write_text(json.dumps({n:{'address':symbols[n],'bytes':size} for n,size in sizes.items()},indent=2)+'\n')
