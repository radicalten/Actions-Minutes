# =============================================================================
# Minimal suite.nds build — Nintendo DS CPU test-suite ROM
#
# Everything needed to build suite.nds lives in Src/ (pre-merged, pre-patched).
# No Python, no vendor fetch, no patch steps: the POWCNT1 16-bit width fix,
# the ARM7 service_poll7 hooks, the instrumented assertions, the generated
# trace tables (13,380 ARM9 / 12,056 ARM7 vectors) and the fixed build-time
# stamp ("AUTOMATED BASELINE", kept for bit-exact reproducibility) are all
# already baked into Src/.
#
# Toolchain (versions used for the reference build, BUILD-INFO.json):
#   devkitARM r68 — arm-none-eabi-g++ 16.1.0, binutils 2.46.0,
#                   newlib 4.6.0.20260123-4
#   ndstool 2.3.1
# Set DEVKITPRO=/your/path if devkitPro is not in /opt/devkitpro.
#
# Usage:
#   make            build suite.nds (plus build/arm{7,9}.elf/.bin/.map)
#   make disasm     also write build/arm{7,9}.disasm (objdump listings)
#   make clean      remove build outputs and suite.nds
#
# Verified output (bit-exact with the published real-hardware ROM):
#   suite.nds  2,036,233 bytes
#   SHA-256    8259d5b024014bd24b6fa2dc669c7fd7e1793d9f2e90a3158b553010475872f3
#   ndstool -i: Header CRC 0x6601 (OK), Logo CRC 0x9E1A (OK)
# Do not change the flags, source order or ndstool arguments below — the
# bit-exact hash depends on all of them.
# =============================================================================

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
PATH := $(DEVKITARM)/bin:$(DEVKITPRO)/tools/bin:$(PATH)
export PATH

CXX     := arm-none-eabi-g++
OBJCOPY := arm-none-eabi-objcopy
OBJDUMP := arm-none-eabi-objdump
NDSTOOL := ndstool

# Exact flags from the original scripts/build.sh.
FLAGS := -O2 -marm -nostartfiles -fno-exceptions -fno-rtti -Wno-narrowing \
         -fno-delete-null-pointer-checks -fno-strict-aliasing \
         -fno-unwind-tables -fno-asynchronous-unwind-tables

SOURCES := $(shell find Src -type f 2>/dev/null)

.PHONY: all disasm clean
all: suite.nds

build:
	mkdir -p build

# Compiled from inside Src/ so that relative paths (incl. font.s's
# ".incbin src9/framework/font.bin") resolve exactly as in the original build.
build/arm9.elf: $(SOURCES) $(MAKEFILE_LIST) | build
	cd Src && $(CXX) -mcpu=arm946e-s $(FLAGS) -o ../build/arm9.elf \
	  src9/entry.s src9/*.cpp src9/framework/*.cpp src9/framework/*.s \
	  src9/tests/*.cpp src9/tests/*.s common/*.cpp common/*.s \
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

suite.nds: build/arm9.bin build/arm7.bin
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
	sha256sum $@

disasm: build/arm9.elf build/arm7.elf
	$(OBJDUMP) -d build/arm9.elf > build/arm9.disasm
	$(OBJDUMP) -d build/arm7.elf > build/arm7.disasm

clean:
	rm -rf build suite.nds
