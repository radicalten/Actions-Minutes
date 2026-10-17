#!/usr/bin/env bash
#
# Fetch a devkitPro toolchain without pkg.devkitpro.org.
#
# Why this exists: the official packages live behind Cloudflare, which starts
# answering 403 to a busy sandbox IP, and that takes the whole build with it.
# Docker Hub serves the same toolchain - the devkitpro/devkitppc image contains
# /opt/devkitpro/devkitPPC (with wii_rules), libogc, portlibs and tools/elf2dol -
# and the registry API needs nothing but curl/python, no docker daemon.
#
#   tools/fetch_dkp_docker.sh [image] [tag] [dest]
#     image  default devkitpro/devkitppc
#     tag    default latest
#     dest   default /home/user/dkpopt  (must NOT be /tmp: that is a ~1 GB
#            tmpfs and a full build tree there makes make() die silently)
#
# Prints the directory to pass as DEVKITPRO on success.
set -euo pipefail

IMAGE=${1:-devkitpro/devkitppc}
TAG=${2:-latest}
DEST=${3:-/home/user/dkpopt}
LAYERS=${DKP_LAYERS:-/tmp/dkp-layers}   # tarballs only; they are re-downloadable
DKP=$DEST/opt/devkitpro

if [ -x "$DKP/devkitPPC/bin/powerpc-eabi-g++" ] && [ -f "$DKP/tools/bin/elf2dol" ]; then
    echo "$DKP"
    exit 0
fi

mkdir -p "$LAYERS" "$DEST"

python3 - "$IMAGE" "$TAG" "$LAYERS" <<'PY'
import json, os, sys, urllib.request

image, tag, out = sys.argv[1], sys.argv[2], sys.argv[3]

def get(url, token=None, accept=None, raw=False):
    req = urllib.request.Request(url)
    if token:  req.add_header("Authorization", "Bearer " + token)
    if accept: req.add_header("Accept", accept)
    with urllib.request.urlopen(req, timeout=180) as r:
        data = r.read()
    return data if raw else json.loads(data)

tok = get(f"https://auth.docker.io/token?service=registry.docker.io"
          f"&scope=repository:{image}:pull")["token"]
A_INDEX = ("application/vnd.docker.distribution.manifest.list.v2+json,"
           "application/vnd.oci.image.index.v1+json")
A_MAN = ("application/vnd.docker.distribution.manifest.v2+json,"
         "application/vnd.oci.image.manifest.v1+json")
base = f"https://registry-1.docker.io/v2/{image}"

man = get(f"{base}/manifests/{tag}", tok, A_INDEX)
digest = next(m["digest"] for m in man["manifests"]
              if m.get("platform", {}).get("architecture") in ("amd64", "x86_64")
              and m.get("platform", {}).get("os") == "linux")
img = get(f"{base}/manifests/{digest}", tok, A_MAN)

for i, layer in enumerate(img["layers"]):
    path = os.path.join(out, f"layer{i:02d}.tar.gz")
    if os.path.exists(path) and os.path.getsize(path) == layer["size"]:
        continue
    sys.stderr.write(f"fetching layer {i} ({layer['size'] // 1048576} MB)\n")
    open(path, "wb").write(get(f"{base}/blobs/{layer['digest']}", tok, None, raw=True))
PY

for layer in "$LAYERS"/layer*.tar.gz; do
    tar -xzf "$layer" -C "$DEST" 2>/dev/null || true
done

test -x "$DKP/devkitPPC/bin/powerpc-eabi-g++"
test -f "$DKP/libogc/lib/wii/libogc.a"
test -f "$DKP/devkitPPC/wii_rules"
test -x "$DKP/tools/bin/elf2dol"
echo "$DKP"
