"""Read the ROM's canonical trace layout; never maintain a second address map."""
import re
from pathlib import Path
VALUES={k:int(v,0) for k,v in re.findall(r'^#define (\w+) (0x[0-9a-fA-F]+|[0-9]+)$',(Path(__file__).resolve().parents[1]/'src/trace_layout.h').read_text(),re.M)}
CAPACITY=VALUES['TRACE_CAPACITY'];VERSION=VALUES['TRACE_VERSION']
BASES={'arm9':VALUES['TRACE9_BASE'],'arm7':VALUES['TRACE7_BASE'],'assertions':VALUES['ASSERT_BASE']}
assert CAPACITY==(VALUES['TRACE_SLOT_BYTES']-64)//40
assert BASES['arm9']+VALUES['TRACE_SLOT_BYTES']<=BASES['arm7']
assert BASES['arm7']+VALUES['TRACE_SLOT_BYTES']<=BASES['assertions']
assert BASES['assertions']+64+4096*20<0x023ed000
