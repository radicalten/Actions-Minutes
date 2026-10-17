#!/usr/bin/env bash
#
# One command to bring this workspace back to a working state.
#
# The sandbox snapshot only reliably keeps a small workspace, so the big trees
# (devkitPPC, Dolphin's dependency closure, the host test toolchain) are treated
# as disposable: they are deleted between turns and rebuilt here from scripts.
#
#   bash tools/agent_env.sh all         # everything below, in order
#   bash tools/agent_env.sh dkp         # devkitPPC + libogc -> $PREFIX (default /home/user/toolchain)
#   bash tools/agent_env.sh dolphin     # headless Dolphin  -> /home/user/dolphin
#   bash tools/agent_env.sh host        # powerpc-linux-musl + qemu-ppc -> /home/user/.local/tc
#   bash tools/agent_env.sh sdcard      # scratch SD card image/folder for the emulator run
#
set -euo pipefail

DKP_PREFIX=${DKP_PREFIX:-/home/user/dkpopt}   # /home/user/dkpopt/opt/devkitpro when the docker route is used
DOLPHIN_DIR=${DOLPHIN_DIR:-/home/user/dolphin}
HOSTTC=${HOSTTC:-/home/user/.local/tc}

log() { printf '\n=== %s ===\n' "$*"; }

do_dkp() {
    log "devkitPPC + libogc"
    # Preferred: the official packages (works offline from $DKP_PREFIX/pkgs).
    if PREFIX="$DKP_PREFIX" SKIP_BUILD=1 bash "$(dirname "$0")/mk_toolchain.sh" 2>/dev/null; then
        echo "DEVKITPRO=$DKP_PREFIX/dkp/opt/devkitpro"
        return 0
    fi
    # Already fetched by the docker route?  Then there is nothing to do.
    if [ -x "$DKP_PREFIX/opt/devkitpro/devkitPPC/bin/powerpc-eabi-g++" ]; then
        echo "DEVKITPRO=$DKP_PREFIX/opt/devkitpro"
        return 0
    fi
    # pkg.devkitpro.org is Cloudflare-fronted and starts refusing a busy sandbox
    # IP; the same toolchain comes from Docker Hub instead (no docker needed).
    echo "devkitPro CDN unavailable, falling back to Docker Hub" >&2
    echo "DEVKITPRO=$(bash "$(dirname "$0")/fetch_dkp_docker.sh" "" "" "$DKP_PREFIX")"
}

do_host() {
    log "powerpc-linux-musl + qemu-ppc -> $HOSTTC"
    mkdir -p "$HOSTTC"
    if [ ! -x "$HOSTTC/powerpc-linux-musl-cross/bin/powerpc-linux-musl-g++" ]; then
        ( cd "$HOSTTC" && curl -sSL -o musl.tgz https://musl.cc/powerpc-linux-musl-cross.tgz \
          && tar -xzf musl.tgz && rm -f musl.tgz )
    fi
    # Debian splits the static binaries out of qemu-user-static into qemu-user
    if [ ! -x "$HOSTTC/quser/usr/bin/qemu-ppc" ]; then
        mkdir -p "$HOSTTC/qdebs"
        ( cd "$HOSTTC/qdebs" && apt-get download qemu-user qemu-user-static >/dev/null 2>&1 || true )
        for f in "$HOSTTC"/qdebs/*.deb; do dpkg-deb -x "$f" "$HOSTTC/quser"; done
    fi
    "$HOSTTC/quser/usr/bin/qemu-ppc" --version | head -1
    "$HOSTTC/powerpc-linux-musl-cross/bin/powerpc-linux-musl-g++" --version | head -1
}

do_dolphin() {
    log "headless Dolphin -> $DOLPHIN_DIR"
    mkdir -p "$DOLPHIN_DIR/debs"
    # Resolve the full dependency closure as URLs; `apt-get install` itself needs root.
    apt-get install --print-uris -y --no-install-recommends dolphin-emu dolphin-emu-data 2>/dev/null \
        | grep -oE "https?://[^ ']+\.deb" | sed 's/%2b/+/g' > "$DOLPHIN_DIR/urls.txt"
    ( cd "$DOLPHIN_DIR/debs"
      while read -r u; do f=$(basename "$u"); [ -e "$f" ] || curl -sSLO "$u"; done < ../urls.txt )
    ( cd "$DOLPHIN_DIR/debs" && apt-get download dolphin-emu >/dev/null 2>&1 || true )
    bash "$DOLPHIN_DIR/setup.sh"
}

do_sdcard() {
    log "scratch SD card folder -> $DOLPHIN_DIR/sdroot"
    mkdir -p "$DOLPHIN_DIR/sdroot/noods"
    if [ -f /home/user/roms/rockwrestler.nds ]; then
        cp -n /home/user/roms/rockwrestler.nds "$DOLPHIN_DIR/sdroot/noods/"
    fi
    cat > "$DOLPHIN_DIR/sdroot/noods.ini" <<'INI'
directBoot=1
jit3Report=1
autoBootRom=sd:/noods/rockwrestler.nds
emulateAudio=0
INI
    ls -l "$DOLPHIN_DIR/sdroot/noods"
}

case "${1:-all}" in
    dkp)     do_dkp ;;
    host)    do_host ;;
    dolphin) do_dolphin ;;
    sdcard)  do_sdcard ;;
    all)     do_dkp; do_host; do_dolphin; do_sdcard ;;
    *)       echo "usage: $0 [all|dkp|host|dolphin|sdcard]" >&2; exit 2 ;;
esac
echo "agent_env: ${1:-all} done"
