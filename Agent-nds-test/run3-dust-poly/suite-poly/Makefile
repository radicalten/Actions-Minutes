# =============================================================================
# suite-poly.nds — self-contained automated NDS test suite
#
# One ROM, one folder. Everything needed is in here:
#   Src/                  ARM9 + ARM7 sources (CPU groups 0-26 + POLY RASTER 27)
#   Src/src9/poly/        polygon rasterization module (polyrastertest port)
#   Src/src9/poly/rastertest.data   hardware recording, embedded with .incbin
#   tools/poly-selftest/  host-side verification of the comparison engine
#
# No network, no vendor fetch, no patch steps.
#
# Toolchain: devkitARM (arm-none-eabi-g++/objcopy/objdump) and ndstool.
#   make DEVKITARM=/usr                  # Debian toolchain + ndstool on PATH
#   make DEVKITPRO=/opt/devkitpro        # default devkitPro layout
#
# Usage:
#   make            build suite-poly.nds (this is the only deliverable)
#   make selftest   verify the polygon decoder/comparator on the host
#   make disasm     also write build/arm{7,9}.disasm (objdump listings)
#   make clean      remove build outputs and suite-poly.nds
#
# Output: suite-poly.nds, ARM9 image < 2,027,264 B (MPU scratch at 0x021F0000).
# =============================================================================

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
PATH := $(DEVKITARM)/bin:$(DEVKITPRO)/tools/bin:$(PATH)
export PATH

CXX     := arm-none-eabi-g++
OBJCOPY := arm-none-eabi-objcopy
OBJDUMP := arm-none-eabi-objdump
NDSTOOL := ndstool

# Exact flags from the original suite build; the poly module is compiled in with
# SUITE_POLY so the CPU groups and group 27 share one image.
FLAGS := -O2 -marm -nostartfiles -fno-exceptions -fno-rtti -Wno-narrowing \
         -fno-delete-null-pointer-checks -fno-strict-aliasing \
         -fno-unwind-tables -fno-asynchronous-unwind-tables

SOURCES := $(shell find Src -type f 2>/dev/null)

.PHONY: all selftest disasm clean
all: suite-poly.nds

build:
	mkdir -p build

# Compiled from inside Src/ so relative paths (font.bin, rastertest.data)
# resolve exactly as in the original build. Source order matters: it decides
# the layout of the image.
build/arm9.elf: $(SOURCES) $(MAKEFILE_LIST) | build
	cd Src && $(CXX) -mcpu=arm946e-s $(FLAGS) -DSUITE_POLY -o ../build/arm9.elf \
	  src9/entry.s src9/*.cpp src9/framework/*.cpp src9/framework/*.s \
	  src9/tests/*.cpp src9/tests/*.s src9/poly/*.cpp src9/poly/*.s \
	  common/*.cpp common/*.s \
	  -T linker9.ld -Wl,-Map,../build/arm9.map

build/arm7.elf: $(SOURCES) $(MAKEFILE_LIST) | build
	cd Src && $(CXX) -mcpu=arm7tdmi $(FLAGS) -o ../build/arm7.elf \
	  src7/entry.s src7/*.cpp src7/framework/*.cpp src7/framework/*.s \
	  src7/tests/*.cpp common/*.cpp common/*.s \
	  -T linker7.ld -Wl,-Map,../build/arm7.map

build/arm9.bin: build/arm9.elf
	$(OBJCOPY) -O binary $< $@

build/arm7.bin: build/arm7.elf
	$(OBJCOPY) -O binary $< $@

suite-poly.nds: build/arm9.bin build/arm7.bin
	@# Memory-layout guards from the original build:
	@#   ARM7 code/data live in reserved main RAM (< 0x100000),
	@#   ARM9 image must stop before MPU scratch at 0x021f0000 (< 0x1eff00).
	@test $$(wc -c < build/arm7.bin) -lt 1048576 \
	  || { echo 'ARM7 image exceeds reserved main RAM' >&2; exit 1; }
	@test $$(wc -c < build/arm9.bin) -lt 2027264 \
	  || { echo 'ARM9 image encroaches on MPU scratch at 021f0000' >&2; exit 1; }
	$(NDSTOOL) -c $@ \
	  -r9 0x02000100 -e9 0x02000100 \
	  -r7 0x02200000 -e7 0x02200000 \
	  -9 build/arm9.bin -7 build/arm7.bin
	@echo "  PHASE   CPU groups 0-26 + POLY RASTER group 27"
	@ls -l $@
	sha256sum $@

# Host-side self-test: builds the real poly_ref.cpp for the host with
# POLY_HOST_TEST, generates reference frames with an independent Python
# decoder, and cross-checks every scene (decoder parity, exact match,
# overdraw / missing / wrong-colour detection, seek, retry pre-pass).
selftest:
	$(MAKE) -C tools/poly-selftest run

disasm: build/arm9.elf build/arm7.elf
	$(OBJDUMP) -d build/arm9.elf > build/arm9.disasm
	$(OBJDUMP) -d build/arm7.elf > build/arm7.disasm

clean:
	rm -rf build suite-poly.nds
