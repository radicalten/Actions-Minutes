#!/bin/bash
# devkitPro's CDN sits behind Cloudflare and temporarily blocks IPs that burst
# downloads.  Poll politely until it lets us back in, then install the toolchain.
UA="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124 Safari/537.36"
URL="https://pkg.devkitpro.org/packages/dkp-linux/x86_64/general-tools-1.4.4-1-x86_64.pkg.tar.zst"
for i in $(seq 1 ${TRIES:-40}); do
    code=$(curl -sS -A "$UA" -o /dev/null -w "%{http_code}" "$URL" || echo 000)
    echo "$(date +%H:%M:%S) attempt $i: $code"
    if [ "$code" = "200" ]; then
        echo "devkitPro is answering again; installing"
        exec env PREFIX=${PREFIX:-/home/user/toolchain} SKIP_BUILD=1 \
             bash "$(dirname "$0")/mk_toolchain.sh"
    fi
    sleep ${DELAY:-40}
done
echo "still blocked after ${TRIES:-40} attempts"
exit 1
