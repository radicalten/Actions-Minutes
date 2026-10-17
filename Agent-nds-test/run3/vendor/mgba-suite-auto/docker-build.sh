#!/bin/bash
set -e
BASE=$(realpath $(dirname $0))
# Pinned rather than `latest`. The suite's expected constants are measured on
# hardware against a specific build: `Update hblankBit test with modern gcc
# values` (mgba-emu/suite@a58437f) and `Update DMA latch test...` (@8c97f2c)
# were both made on 2026-05-31, when `devkitpro/devkitarm:latest` was the
# 20260221 image. The 20260610 image lays hblankBit out differently - three
# extra one-cycle `movs` after the second Halt, before the timer read - so a
# `latest` build disagrees with its own expected values by three cycles on
# Hblank and by 1-16 on the six Flip results, for reasons that have nothing
# to do with the emulator under test. Bump this only alongside constants
# re-measured on hardware with the new toolchain.
IMAGE=devkitpro/devkitarm:20260221

if [ "$(git rev-parse --is-shallow-repository)" = "true" ]; then
	# Needed so `git rev-list --count` below sees the full history
	git fetch --unshallow
fi
docker run --rm -v $BASE:/root/suite -w /root/suite "$IMAGE" make
mkdir -p deploy
zip deploy/suite-v0-r$(git rev-list --count HEAD).zip suite.gba
docker run --rm -v $BASE:/root/suite -w /root/suite "$IMAGE" make clean
