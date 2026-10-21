#!/bin/bash
# Build a FAT32 SD image from the stage/ directory contents.
# Usage: ./make-sd.sh [size_MB]
set -e
SIZE=${1:-256}
SD=/home/user/test/sd.img
STAGE=/home/user/test/stage

rm -f "$SD"
truncate -s ${SIZE}M "$SD"
sudo /usr/sbin/mkfs.fat -F 32 -n NOODS "$SD" > /dev/null
# Copy everything from stage/ into the image root
cd "$STAGE"
find . -type d -printf '%P\n' | grep -v '^$' | while read -r d; do
    mmd -i "$SD" "::/$d"
done
find . -type f -printf '%P\n' | while read -r f; do
    mcopy -i "$SD" "$f" "::$f"
done
echo "SD image built: $SD ($(du -h "$SD" | cut -f1))"
mlabel -i "$SD" -s
