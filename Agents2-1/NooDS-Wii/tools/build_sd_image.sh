#!/usr/bin/env bash
set -euo pipefail

export PATH="$PATH:/usr/sbin:/sbin:/usr/games"

if [ $# -lt 1 ]; then
  echo "Usage: $0 IMG [--autoboot PATH] [--no-suite] [--no-nds]" >&2
  exit 2
fi

IMG="$1"
shift

AUTOBOOT=""
WITH_SUITE=1
WITH_NDS=1

while [ $# -gt 0 ]; do
  case "$1" in
    --autoboot)
      AUTOBOOT="$2"
      shift 2
      ;;
    --no-suite)
      WITH_SUITE=0
      shift
      ;;
    --no-nds)
      WITH_NDS=0
      shift
      ;;
    *)
      echo "Unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

if [ -e "$IMG" ]; then
  echo "ERROR: $IMG already exists (refusing to overwrite)" >&2
  exit 1
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDS_ROM="${REPO_ROOT}/nds/rockwrestler.nds"
GBA_ROM="${REPO_ROOT}/gba/suite.gba"

NDS_EXP_SHA="f905e16510b00500d432b62dbfafc33e9a7179187475981fe74d9e691a96069a"
GBA_EXP_SHA="63f8c6b10135f91cc643e6197d9f2fd6c7abba4454571b9aa4039f7db3870495"

if [ "$WITH_NDS" -eq 1 ]; then
  if [ ! -f "$NDS_ROM" ]; then
    echo "ERROR: missing $NDS_ROM" >&2
    exit 1
  fi
  sha="$(sha256sum "$NDS_ROM" | awk '{print $1}')"
  if [ "$sha" != "$NDS_EXP_SHA" ]; then
    echo "ERROR: SHA mismatch on $NDS_ROM ($sha != $NDS_EXP_SHA)" >&2
    exit 1
  fi
fi

if [ "$WITH_SUITE" -eq 1 ]; then
  if [ ! -f "$GBA_ROM" ]; then
    echo "ERROR: missing $GBA_ROM" >&2
    exit 1
  fi
  sha="$(sha256sum "$GBA_ROM" | awk '{print $1}')"
  if [ "$sha" != "$GBA_EXP_SHA" ]; then
    echo "ERROR: SHA mismatch on $GBA_ROM ($sha != $GBA_EXP_SHA)" >&2
    exit 1
  fi
fi

mkdir -p "$(dirname "$IMG")"
dd if=/dev/zero of="$IMG" bs=1M count=128 status=none
mkfs.vfat -F 16 "$IMG" >/dev/null

for d in ::/nds ::/gba ::/noods ::/noods/bios ::/noods/gba ::/noods/nds; do
  mmd -i "$IMG" "$d"
done

if [ "$WITH_SUITE" -eq 1 ]; then
  for dst in ::/gba/suite.gba ::/noods/suite.gba ::/noods/gba/suite.gba ::/suite.gba ::/noods/suite-auto.gba; do
    mcopy -i "$IMG" "$GBA_ROM" "$dst"
  done
fi

if [ "$WITH_NDS" -eq 1 ]; then
  for dst in ::/nds/rockwrestler.nds ::/noods/rockwrestler.nds ::/noods/nds/rockwrestler.nds ::/rockwrestler.nds; do
    mcopy -i "$IMG" "$NDS_ROM" "$dst"
  done
fi

if [ -n "$AUTOBOOT" ]; then
  TMP_AB="$(mktemp)"
  printf '%s\n' "$AUTOBOOT" > "$TMP_AB"
  mcopy -i "$IMG" "$TMP_AB" ::/autoboot.txt
  mcopy -i "$IMG" "$TMP_AB" ::/noods/autoboot.txt
  rm -f "$TMP_AB"
fi

mdir -i "$IMG" ::/ ::/noods ::/gba ::/nds
