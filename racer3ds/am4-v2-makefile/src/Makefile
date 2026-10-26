# =============================================================================
# Racer 3DS - convenience wrapper around the CMake cross build.
#
# The real build system is CMake (platform/3ds/CMakeLists.txt); this file just
# saves typing the commands documented in BUILDING.md. It needs a sourced
# devkitPro environment (DEVKITPRO / DEVKITARM) and cmake, ninja (or make),
# python3 on PATH.
#
#   make setup                      build the host N64Recomp/RSPRecomp tools
#   make recompile ROM=baserom.z64  ROM -> platform/3ds/generated/ (your own ROM)
#   make                            configure + build the ELF, .3dsx and .cia
#   make cia                        build only Racer3DS.cia
#   make 3dsx                       build only Racer3DS.3dsx (+ .smdh)
#   make clean                      remove the CMake build tree
#   make distclean                  clean + remove all ROM-derived generated code
#
# Override defaults on the command line, e.g. `make BUILD_DIR=out cia`.
# =============================================================================

BUILD_DIR ?= build-3ds
TOOLCHAIN := lib/rt64-3ds/cmake/devkitARM-3ds.cmake
ROM       ?= baserom.z64
JOBS      ?= $(shell nproc 2>/dev/null || echo 4)
# Ninja when available, else CMake's Unix Makefiles generator.
GENERATOR := $(shell command -v ninja >/dev/null 2>&1 && echo Ninja || echo "Unix Makefiles")

CMAKE      ?= cmake
ELF        := $(BUILD_DIR)/Racer3DS
CIA        := $(BUILD_DIR)/Racer3DS.cia
3DSX       := $(BUILD_DIR)/Racer3DS.3dsx
GENERATED  := platform/3ds/generated

.PHONY: all configure build cia 3dsx setup recompile clean distclean help

# Default: the installable CIA plus the hbmenu .3dsx (when 3dsxtool exists,
# CMake skips that target on toolchains without it; the CIA is always built).
all: cia 3dsx

help:
	@sed -n '2,16p' $(firstword $(MAKEFILE_LIST)) | sed 's/^# \{0,1\}//'

# Friendly failure instead of CMake's cryptic toolchain error.
configure: | check-env
	$(CMAKE) -S platform/3ds -B $(BUILD_DIR) -G "$(GENERATOR)" \
		-DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/$(TOOLCHAIN)

build: configure
	$(CMAKE) --build $(BUILD_DIR) -j$(JOBS)

cia: build
	$(CMAKE) --build $(BUILD_DIR) --target Racer3DS_cia
	@echo "== $(CIA)"

3dsx: build
	$(CMAKE) --build $(BUILD_DIR) --target Racer3DS_3dsx
	@echo "== $(3DSX)"

# Host-side recompilers, built from the vendored N64Recomp tree (no devkitPro
# needed for this step).
setup:
	./tools/3ds/setup.sh --jobs $(JOBS)

# ROM -> generated C/C++. Everything under $(GENERATED) is ROM-derived and is
# gitignored; it is never committed or redistributed.
recompile:
	@test -f "$(ROM)" || { echo "ROM not found: $(ROM) (use: make recompile ROM=/path/to/baserom.z64)"; exit 2; }
	./tools/3ds/recompile.sh --rom "$(ROM)"

check-env:
	@test -n "$(DEVKITPRO)" || { \
		echo "DEVKITPRO is not set. Source the devkitPro environment first,"; \
		echo "e.g.  source /etc/profile.d/devkit-env.sh   (see BUILDING.md)"; \
		exit 2; }

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf $(GENERATED) build/recomp-work
