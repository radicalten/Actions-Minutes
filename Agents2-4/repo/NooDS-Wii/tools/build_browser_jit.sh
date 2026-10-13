#!/usr/bin/env bash
# Build the regular Wii file-browser DOL with the ARM9/ARM7 block JIT enabled.
# Invoke with: bash tools/build_browser_jit.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"
export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH"

# A separate object directory avoids mixing browser and autoboot build flags.
CXXFLAGS="${CXXFLAGS:-} -DNOODS_FORCE_BROWSER=1" \
    make -j2 JIT=1 BLOCKS=1 CONFIG_TAG=jit_browser
cp -f NooDS-Wii_jit_browser.dol jit.dol
printf 'Browser JIT: %s\n' "$ROOT/jit.dol"
sha256sum jit.dol
