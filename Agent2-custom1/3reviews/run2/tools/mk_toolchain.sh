#!/usr/bin/env bash
#
# Root-free devkitPPC + libogc + elf2dol installer for v3-NooDS-Wii, then build.
#
#   tools/mk_toolchain.sh                 # install (if needed) and build
#   PREFIX=/path SKIP_BUILD=1 ...         # install only
#
# Everything lands under $PREFIX (default ./toolchain) - nothing is written
# outside it.  Re-running is cheap: package archives already in $PKGS are
# reused, so a populated tree rebuilds with no network access at all.
#
# Package names are resolved through devkitPro's pacman databases instead of
# being hardcoded, because the .pkg.tar.zst filenames carry both the version and
# the architecture.  devkitPro splits them over two trees:
#   dkp-libs   (arch any)     - devkitPPC, libogc, libfat-ogc, devkitppc-rules ...
#   dkp-linux  (arch x86_64)  - the cross tools that *run on the build machine*
#                               (binutils, gcc, gamecube-tools/elf2dol)
#
set -euo pipefail
cd "$(dirname "$0")/.."

PREFIX=${PREFIX:-$PWD/toolchain}
PKGS=${PKGS:-$PREFIX/pkgs}
ROOT=${ROOT:-$PREFIX/dkp}          # pacman-style extraction root
DKP=$ROOT/opt/devkitpro            # what the Makefile wants as DEVKITPRO
PKG_REPO=${PKG_REPO:-https://pkg.devkitpro.org/packages}
# devkitPro's CDN answers 403 to non-browser user agents
UA=${UA:-"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124 Safari/537.36"}

TREES=(dkp-libs dkp-linux)
mkdir -p "$PKGS" "$ROOT"

# ---------------------------------------------------------------- zstd
# .pkg.tar.zst needs a zstd binary; GNU tar just shells out to it.
zstd_path() {
    if command -v zstd >/dev/null 2>&1; then command -v zstd; return 0; fi
    local ht=$PREFIX/hosttools
    if [ -x "$ht/usr/bin/zstd" ]; then echo "$ht/usr/bin/zstd"; return 0; fi
    echo "mk_toolchain: fetching zstd" >&2
    ( cd "$PKGS" && apt-get download zstd libzstd1 >/dev/null 2>&1 ) || true
    for d in zstd libzstd1; do
        for f in "$PKGS/$d"_*.deb; do
            [ -e "$f" ] && dpkg-deb -x "$f" "$ht" >/dev/null 2>&1 || true
        done
    done
    [ -x "$ht/usr/bin/zstd" ] && echo "$ht/usr/bin/zstd"
}
ZSTD=$(zstd_path || true)
[ -n "${ZSTD:-}" ] || { echo "mk_toolchain: no zstd available" >&2; exit 1; }
export PATH="$(dirname "$ZSTD"):$PATH"

untar() { tar --use-compress-program="$ZSTD" -xf "$1" -C "$2"; }

# ---------------------------------------------------------------- databases
fetch_db() {  # $1 = tree name
    local db=$PKGS/$1.db
    [ -s "$db" ] && return 0
    for alt in "$PREFIX/pkg/$1.db" "$PREFIX/$1.db"; do
        [ -s "$alt" ] && { cp "$alt" "$db"; return 0; }
    done
    echo "mk_toolchain: fetching $1 database" >&2
    curl -fsSL -A "$UA" -o "$db" "$PKG_REPO/$1.db"
}

# Resolve "package -> tree,arch,filename" out of the pacman databases.  Each db
# is a tar.gz of <pkg>-<ver>/desc files of "%KEY%\nvalue" pairs, so %FILENAME%
# gives the exact archive to fetch.
resolve_packages() {
    python3 - "$PKGS" "${TREES[@]}" <<'PY'
import os, sys, tarfile

pkgs = sys.argv[1]
trees = sys.argv[2:]
want = """devkitPPC devkitppc-binutils devkitppc-crtls devkitppc-gcc devkitppc-newlib
          devkitppc-rules general-tools gamecube-tools libfat-ogc libogc""".split()

def fields(text):
    out, key = {}, None
    for line in text.splitlines():
        if line.startswith("%") and line.endswith("%"):
            key = line
        elif key:
            out[key] = line.strip()
            key = None
    return out

found = {}
for tree in trees:
    db = os.path.join(pkgs, tree + ".db")
    if not os.path.exists(db):
        continue
    with tarfile.open(db, "r:gz") as tf:
        for member in tf.getmembers():
            if not member.name.endswith("/desc"):
                continue
            f = fields(tf.extractfile(member).read().decode("utf-8", "replace"))
            name, fname = f.get("%NAME%"), f.get("%FILENAME%")
            if name in want and fname and name not in found:
                arch = fname.split(".pkg.tar")[0].rsplit("-", 1)[-1]
                found[name] = (tree, arch, fname)

missing = [w for w in want if w not in found]
if missing:
    sys.exit("mk_toolchain: not in the package databases: " + " ".join(missing))
for name in want:
    tree, arch, fname = found[name]
    print("\t".join((name, tree, arch, fname)))
PY
}

for tree in "${TREES[@]}"; do fetch_db "$tree"; done
mapfile -t RESOLVED < <(resolve_packages)

# ---------------------------------------------------------------- packages
for line in "${RESOLVED[@]}"; do
    IFS=$'\t' read -r name tree arch fname <<<"$line"
    compgen -G "$PKGS/$name-*.pkg.tar.zst" >/dev/null && continue
    echo "mk_toolchain: fetching $fname"
    for url in "$PKG_REPO/$tree/$arch/$fname" "$PKG_REPO/$tree/$fname"; do
        curl -fsSL -A "$UA" -o "$PKGS/$fname" "$url" && break
    done
done

for line in "${RESOLVED[@]}"; do
    IFS=$'\t' read -r name tree arch fname <<<"$line"
    for archive in "$PKGS/$name"-*.pkg.tar.zst; do
        [ -e "$archive" ] || continue
        echo "  install $(basename "$archive")"
        untar "$archive" "$ROOT"
    done
done

echo "  toolchain at $DKP"
ls "$DKP/devkitPPC/bin/powerpc-eabi-g++" >/dev/null
ls "$DKP/tools/bin/elf2dol" >/dev/null

if [ "${SKIP_BUILD:-0}" = "1" ]; then
    echo "mk_toolchain: SKIP_BUILD=1, done"
    exit 0
fi

echo "mk_toolchain: building"
make -j"$(nproc 2>/dev/null || echo 2)" DEVKITPRO="$DKP"
