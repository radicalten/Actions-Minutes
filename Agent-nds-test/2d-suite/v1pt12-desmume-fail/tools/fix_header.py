#!/usr/bin/env python3
"""Set friendly homebrew header metadata after the standard ds_rules build."""
import pathlib
import subprocess
import sys

rom = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "nds-2d-agent-suite.nds")
data = bytearray(rom.read_bytes())
data[0x00:0x0C] = b"NDS 2D TEST\0"
data[0x0C:0x10] = b"N2DT"
data[0x10:0x12] = b"00"
rom.write_bytes(data)
subprocess.run(["ndstool", "-f", str(rom)], check=True)
