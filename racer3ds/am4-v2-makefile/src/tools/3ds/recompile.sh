#!/usr/bin/env bash
# =============================================================================
# Star Wars Episode I: Racer (3DS) - ROM -> generated recompiled sources.
#
#   ./tools/3ds/recompile.sh --rom /path/to/baserom.z64
#
# All configs and fix-up tools are vendored in recomp/ (no network, no clone):
#
#   1. validate the US ROM (magic, 32 MiB, CRC1 0x72F70398)
#   2. N64Recomp            -> generated/RecompiledFuncs/*.c (+ funcs.h, lookup.cpp)
#   3. fix_fallthroughs.py  -> repair split-function continuations
#   4. post_recomp_patches.py -> osPiStartDma_recomp / racer_get_count redirects
#   5. RSPRecomp            -> generated/rsp/aspMain.cpp (stock audio microcode)
#   6. gen_lookup_table.py  -> generated/section_table.cpp
#
# Everything produced is ROM-derived. It lands in platform/3ds/generated/
# (gitignored) and is never committed or redistributed.
# =============================================================================
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
cfg="$repo/recomp"
nr="$repo/lib/N64ModernRuntime/N64Recomp"
gen="$repo/platform/3ds/generated"
work="$repo/build/recomp-work"

ROM=""
while [ $# -gt 0 ]; do
    case "$1" in
        --rom) ROM=$2; shift ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

say() { printf '\n== %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

[ -n "$ROM" ] || die "pass --rom <baserom.z64>"
[ -f "$ROM" ] || die "no such file: $ROM"
[ -x "$nr/build/N64Recomp" ] || die "host tools not built - run tools/3ds/setup.sh first"
[ -f "$cfg/recomp.toml" ] || die "vendored recompiler config missing: $cfg/recomp.toml"

NR_BIN="$nr/build/N64Recomp"
RSP_BIN="$nr/build/RSPRecomp"

# ---------------------------------------------------------------- 1. ROM
say "1/6 ROM validation"
size=$(stat -c%s "$ROM")
[ "$size" = "33554432" ] || die "$ROM is $size bytes; Racer (U) [!] must be 33554432"
magic=$(od -An -tx1 -N4 "$ROM" | tr -d ' \n')
case "$magic" in
    80371240) echo "  byte order: z64 (big endian)" ;;
    *) die "bad ROM magic $magic (need a big-endian .z64; convert .n64/.v64 first)" ;;
esac
crc1=$(od -An -tx4 -j16 -N4 --endian=big "$ROM" | tr -d ' ')
[ "$crc1" = "72f70398" ] || die "CRC1 0x$crc1 != 0x72f70398 (wrong ROM version; need the US NEPE build)"
echo "  CRC1 0x72f70398 OK"

# ------------------------------------------------------ stage the work tree.
# The Python tools locate RecompiledFuncs/ and recomp.toml relative to their
# own location (ROOT = the parent of tools/), so mirror that layout in work/.
rm -rf "$work"
mkdir -p "$work/tools" "$work/src" "$work/rsp"
cp "$ROM" "$work/baserom.z64"
cp "$cfg/recomp.toml" "$cfg/symbols.toml" "$work/"
cp "$cfg/tools/fix_fallthroughs.py" "$cfg/tools/post_recomp_patches.py" \
   "$cfg/tools/gen_lookup_table.py" "$work/tools/"
# Rewrite the RSP config's relative paths for the flat staging layout.
sed -e 's|rom_file_path = "../baserom.z64"|rom_file_path = "baserom.z64"|' \
    -e 's|output_file_path = "aspMain.cpp"|output_file_path = "rsp/aspMain.cpp"|' \
    "$cfg/rsp/aspMain.toml" > "$work/aspMain.toml"

# ---------------------------------------------------------------- 2. N64Recomp
say "2/6 N64Recomp: ROM -> RecompiledFuncs/"
( cd "$work" && "$NR_BIN" recomp.toml )
funcs=$(find "$work/RecompiledFuncs" -name "*.c" | wc -l)
[ "$funcs" -gt 0 ] || die "N64Recomp produced no C files"
[ -f "$work/RecompiledFuncs/lookup.cpp" ] || die "lookup.cpp missing from N64Recomp output"
echo "  $funcs C files + lookup.cpp"

# -------------------------------------------------------- 3. fallthrough fix
say "3/6 fix_fallthroughs.py (split-function continuations)"
( cd "$work" && python3 tools/fix_fallthroughs.py )

# ----------------------------------------------------- 4. post-recomp patches
say "4/6 post_recomp_patches.py (osPiStartDma / racer_osGetCount redirects)"
( cd "$work" && python3 tools/post_recomp_patches.py )

# ---------------------------------------------------------------- 5. RSPRecomp
say "5/6 RSPRecomp: aspMain -> rsp/aspMain.cpp"
( cd "$work" && "$RSP_BIN" aspMain.toml )
grep -q "RspExitReason aspMain(" "$work/rsp/aspMain.cpp" || die "aspMain entry point not found in RSP output"
echo "  aspMain microcode generated"

# -------------------------------------------------------- 6. section table
say "6/6 gen_lookup_table.py -> section_table.cpp"
( cd "$work" && python3 tools/gen_lookup_table.py )
[ -f "$work/src/section_table.cpp" ] || die "section_table.cpp not generated"

# ------------------------------------------------------------- publish
mkdir -p "$gen/rsp"
rm -rf "$gen/RecompiledFuncs"
cp -r "$work/RecompiledFuncs" "$gen/RecompiledFuncs"
cp "$work/rsp/aspMain.cpp" "$gen/rsp/aspMain.cpp"
cp "$work/src/section_table.cpp" "$gen/section_table.cpp"

cat <<EOF

  Generated sources published to $gen:
    RecompiledFuncs/  ($(find "$gen/RecompiledFuncs" -name '*.c' | wc -l | tr -d ' ') C files + lookup.cpp)
    rsp/aspMain.cpp
    section_table.cpp

Next:
  cmake -S platform/3ds -B build-3ds -G Ninja \\
        -DCMAKE_TOOLCHAIN_FILE=lib/rt64-3ds/cmake/devkitARM-3ds.cmake
  cmake --build build-3ds

These files are ROM-derived: do not commit or redistribute them.
EOF
