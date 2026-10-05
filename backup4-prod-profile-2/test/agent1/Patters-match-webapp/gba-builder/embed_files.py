#!/usr/bin/env python3
"""
embed_files.py — make index.html self-contained
===============================================

Injects the builder scripts + game databases + builder_core.py into the
"embedded-files" block of index.html, so the page can run fully standalone
in a browser (via Pyodide) with no server and no other files.

Run this after changing builder_core.py or updating the
gba-emu-compilation-builders repo:

    python3 embed_files.py
"""

import base64
import json
import os
import re

APP_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.join(os.path.dirname(APP_DIR), "gba-emu-compilation-builders")
INDEX = os.path.join(APP_DIR, "index.html")

# builder scripts + databases + the bundled HVCA exit subroutine
EMBED_FILES = [
    "pocketnes_compile.py", "goomba_compile.py", "snesadvance_compile.py",
    "snezziboy_compile.py", "pceadvance_compile.py", "smsadvance_compile.py",
    "hvca_compile.py", "cologne_compile.py", "msxadvance_compile.py",
    "ngpgba_compile.py", "wasabi_compile.py", "zxadvance_compile.py",
    "pnesmmw.mdb", "snesadvance.dat", "snezzi.dat", "flash_ez4_ezo.sub",
]


def b64(path):
    with open(path, "rb") as fh:
        return base64.b64encode(fh.read()).decode("ascii")


def main():
    files = {}
    for name in EMBED_FILES:
        path = os.path.join(REPO_DIR, name)
        if not os.path.exists(path):
            raise SystemExit(f"missing: {path}")
        files[name] = b64(path)

    payload = {
        "core": b64(os.path.join(APP_DIR, "builder_core.py")),
        "files": files,
    }

    with open(INDEX, "r", encoding="utf-8") as fh:
        html = fh.read()

    new_html, n = re.subn(
        r'(<script id="embedded-files" type="application/json">).*?(</script>)',
        lambda m: m.group(1) + json.dumps(payload) + m.group(2),
        html, flags=re.S,
    )
    if n != 1:
        raise SystemExit("embedded-files block not found in index.html")

    with open(INDEX, "w", encoding="utf-8") as fh:
        fh.write(new_html)
    print(f"embedded {len(files)} repo files + builder_core.py")
    print(f"index.html is now {os.path.getsize(INDEX)/1024:.0f} KB and fully self-contained")


if __name__ == "__main__":
    main()
