#!/bin/bash
# Archive the whole source tree at each checkpoint, or re-extract it.
#
#   tools/guard.sh check      compare the working tree against the newest snapshot
#   tools/guard.sh restore    re-extract the newest snapshot over the tree
#   tools/guard.sh snapshot   write a fresh snapshot from the current tree
#
# Why the whole tree: silent workspace reverts have rolled this repo back six times,
# and one of them wiped two helper scripts that a hand-written file list had never
# covered (and, earlier, an entire directory - the drift check skips directories, so
# naming `NooDS-Wii/jit3` made every recompiler edit invisible).  Archiving "." with
# build outputs excluded has neither failure mode.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BK=/home/user/v3src-backup
EXCL="--exclude=*.o --exclude=*.d --exclude=.jit-flag --exclude=__pycache__ --exclude=*~"
# by NAME, not mtime: a silent revert can hand files (and tarballs) older timestamps
newest() { ls -1 $BK/v3-src-*-final.tar.gz $BK/v3-thumb-*.tar.gz 2>/dev/null | sort | tail -1; }

case "${1:-check}" in
check)
    t=$(newest); [ -n "$t" ] || { echo "no snapshot"; exit 1; }
    rm -rf /tmp/guard && mkdir -p /tmp/guard && tar xzf "$t" -C /tmp/guard || exit 1
    bad=0
    # every file in the snapshot must be present and identical, and no file that the
    # snapshot knows about may be missing from the tree
    ( cd /tmp/guard && find . -type f | sed 's|^\./||' ) | while read -r f; do
        [ -e "$ROOT/$f" ] || { echo "MISSING: $f"; echo x >> /tmp/guard.miss; }
        cmp -s "/tmp/guard/$f" "$ROOT/$f" 2>/dev/null || { echo "DRIFT: $f"; echo x >> /tmp/guard.miss; }
    done
    if [ -s /tmp/guard.miss ]; then rm -f /tmp/guard.miss; echo "tree has drifted from $(basename "$t")"; else
        rm -f /tmp/guard.miss; echo "tree matches $(basename "$t")"; fi
    ;;
restore)  t=$(newest); echo "restoring from $(basename "$t")"; tar xzf "$t" -C "$ROOT" ; cp "$BK/$(basename "$t")" "$t" 2>/dev/null || true ;;
snapshot) t=$BK/v3-thumb-$(date +%H%M).tar.gz
          tar czf "$t" $EXCL -C "$ROOT" . && echo "$t" || { rm -f "$t"; echo "snapshot FAILED"; } ;;
*) echo "usage: guard.sh [check|restore|snapshot]" ;;
esac
