#!/usr/bin/env bash
set -euo pipefail

PREFIX="${DEVKITPRO:-/opt/devkitpro}"
CACHE_DIR="${DKP_CACHE:-/tmp/dkp_pkgs}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESULT_FILE="${REPO_ROOT}/evidence/toolchain_bootstrap_result.txt"
BASE_URL="https://wii.leseratte10.de/devkitPro"

mkdir -p "$CACHE_DIR" "$PREFIX" "${REPO_ROOT}/evidence"
# (no chmod: tools are invoked via bash; avoids mode drift in git)

declare -a PKGS=(
  "devkitPPC/r50%20%282026-05-03%29/devkitppc-binutils-2.46.0-1-linux_x86_64.pkg.tar.zst|ec39352d27f668d235de9fa69caeee2d753994b695245075a37c4bfc700547d6"
  "devkitPPC/r50%20%282026-05-03%29/devkitppc-gcc-16.1.0-1-linux_x86_64.pkg.tar.zst|656f0cabcd99a1c0d1510d0fccf5556abac26c3c813113175476d315c5b3da7a"
  "devkitPPC/r49%20%282026-01%29/devkitppc-newlib-4.6.0.20260123-4-any.pkg.tar.zst|2c5277c6b07a5558c9fd8e631d958c6d7784860aa55f5c320cb51371bc07e21b"
  "devkitPPC/devkitppc-rules/devkitppc-rules-1.2.1-1-any.pkg.tar.zst|0c3394da451c9dfb3b428d9d61b044eb1eafb947b2b5090ff88525f55c785278"
  "devkitPPC/devkitppc-rules/devkitppc-crtls-2.1.0-1-any.pkg.tar.zst|5a1144d515579eee73bb936ca36a8ea739b00d8cf32af3fa4dda21a039881bcd"
  "libogc/libogc_3.1%20%282026-05-03%29/libogc-3.1.0-1-any.pkg.tar.zst|7c2dba9f4ef8cc496e424cd6208e0eddc893c08fff080016321d777d84c5c083"
  "libfat/libfat_2.1.0/libfat-ogc-2.1.0-4-any.pkg.tar.zst|9fb965672aaa3a82586715aab15ee4a16d3fc8850cbd2bd8cc4868e1c091d1b9"
  "other-stuff/gamecube-tools/gamecube-tools-1.0.7-1-linux_x86_64.pkg.tar.zst|e7dea3d441f3951be336a5b52849bdabf191d7157b765b5b3a603ecf6ba233b4"
  "other-stuff/general-tools/general-tools-1.4.4-1-linux_x86_64.pkg.tar.zst|b6b59bd3d22d4f83a7f0668f749df7cf97b1f05083031e629d43ca4f4377c7b5"
)

for entry in "${PKGS[@]}"; do
  rel="${entry%%|*}"
  expected_sha="${entry##*|}"
  fname="$(basename "$rel")"
  dest="${CACHE_DIR}/${fname}"
  if [ ! -f "$dest" ]; then
    curl -sSfL "${BASE_URL}/${rel}" -o "$dest"
  fi
  actual_sha="$(sha256sum "$dest" | awk '{print $1}')"
  if [ "$actual_sha" != "$expected_sha" ]; then
    echo "STATUS=FAIL (SHA256 mismatch for $fname: got $actual_sha expected $expected_sha)" | tee "$RESULT_FILE"
    exit 1
  fi
  tar --use-compress-program=unzstd -xf "$dest" --strip-components=2 -C "$PREFIX" opt/devkitpro
done

export DEVKITPRO="$PREFIX"
export DEVKITPPC="$PREFIX/devkitPPC"
export PATH="$DEVKITPPC/bin:$PREFIX/tools/bin:$PATH"

SMOKE_DIR="$(mktemp -d)"
trap 'rm -rf "$SMOKE_DIR"' EXIT
cat > "$SMOKE_DIR/smoke.c" << 'EOF'
#include <gccore.h>
int main(void) { VIDEO_Init(); return 0; }
EOF

powerpc-eabi-gcc -mrvl -mcpu=750 -meabi -mhard-float \
  -I"$PREFIX/libogc/include" -L"$PREFIX/libogc/lib/wii" \
  "$SMOKE_DIR/smoke.c" -logc -o "$SMOKE_DIR/smoke.elf"
elf2dol "$SMOKE_DIR/smoke.elf" "$SMOKE_DIR/smoke.dol"

if [ ! -s "$SMOKE_DIR/smoke.dol" ]; then
  echo "STATUS=FAIL (smoke.dol empty)" | tee "$RESULT_FILE"
  exit 1
fi

{
  echo "STATUS=PASS"
  echo "PREFIX=$PREFIX"
  echo "GCC_VERSION=$(powerpc-eabi-g++ --version | head -n1)"
  echo "AS_VERSION=$(powerpc-eabi-as --version | head -n1)"
  echo "ELF2DOL=$(which elf2dol)"
} | tee "$RESULT_FILE"
