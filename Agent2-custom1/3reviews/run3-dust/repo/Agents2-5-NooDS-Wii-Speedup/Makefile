.SUFFIXES:
export DEVKITPRO ?= /opt/devkitpro
export DEVKITPPC	:= $(DEVKITPRO)/devkitPPC
include $(DEVKITPPC)/wii_rules

JIT    ?= 0
BLOCKS ?= 0
OPT_LEVEL ?= 3
LTO ?= 0
JIT_PROFILE ?= 0
JIT_POLL_SKIP ?= 0
LOOP_UNROLL ?= 1
FULL_RAM_DUMP ?= 0
G2_SCRIPTED_INPUT ?= 0

# User-facing JIT builds open the SD browser. Set BROWSER=0 for ROM-autoboot regression runs.
ifeq ($(origin BROWSER),undefined)
  ifeq ($(JIT),1)
    BROWSER := 1
  else
    BROWSER := 0
  endif
endif

# Keep detailed counters in diagnostic/autoboot builds; omit hot-path accounting in browser DOLs by default.
ifeq ($(origin JIT_STATS),undefined)
  ifeq ($(BROWSER),1)
    JIT_STATS := 0
  else
    JIT_STATS := 1
  endif
endif

CC      := $(DEVKITPPC)/bin/powerpc-eabi-g++
MACHDEP := -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -fsigned-char -ffast-math -funroll-loops -fauto-inc-dec -finline-functions
INCLUDES += -I$(DEVKITPRO)/portlibs/wii/include -I$(DEVKITPRO)/portlibs/ppc/include -I$(DEVKITPRO)/libogc/include
JIT_DEFS := -DNOODS_JIT=$(JIT) -DNOODS_JIT_BLOCKS=$(BLOCKS) -DNOODS_JIT_STATS=$(JIT_STATS) -DNOODS_JIT_PROFILE=$(JIT_PROFILE) -DNOODS_JIT_POLL_SKIP=$(JIT_POLL_SKIP)
BROWSER_DEFS := -DNOODS_FORCE_BROWSER=$(BROWSER)
TEST_DEFS :=
ifeq ($(FULL_RAM_DUMP),1)
  TEST_DEFS += -DNOODS_FULL_RAM_DUMP=1
endif
ifeq ($(G2_SCRIPTED_INPUT),1)
  TEST_DEFS += -DNOODS_G2_SCRIPTED_INPUT=1
endif
CXXFLAGS += -O$(OPT_LEVEL) -MMD -MP \
-DENDIAN_BIG \
$(JIT_DEFS) \
$(BROWSER_DEFS) \
$(TEST_DEFS) \
$(INCLUDES) $(MACHDEP)
ifeq ($(LTO),1)
  CXXFLAGS += -flto
endif
ifeq ($(LOOP_UNROLL),0)
  CXXFLAGS += -fno-unroll-loops
endif

CONFIG_TAG := jit$(JIT)_blocks$(BLOCKS)_browser$(BROWSER)
ifeq ($(FULL_RAM_DUMP),1)
  CONFIG_TAG := $(CONFIG_TAG)_fullram
endif
ifeq ($(G2_SCRIPTED_INPUT),1)
  CONFIG_TAG := $(CONFIG_TAG)_scriptinput
endif
ifneq ($(OPT_LEVEL),2)
  CONFIG_TAG := $(CONFIG_TAG)_o$(OPT_LEVEL)
endif
ifeq ($(LTO),1)
  CONFIG_TAG := $(CONFIG_TAG)_lto
endif
ifeq ($(JIT_PROFILE),1)
  CONFIG_TAG := $(CONFIG_TAG)_profile
endif
ifeq ($(JIT_POLL_SKIP),1)
  CONFIG_TAG := $(CONFIG_TAG)_pollskip
endif
ifeq ($(JIT_STATS),0)
  CONFIG_TAG := $(CONFIG_TAG)_nostats
endif
ifeq ($(LOOP_UNROLL),0)
  CONFIG_TAG := $(CONFIG_TAG)_nounroll
endif
BUILDDIR   := build_$(CONFIG_TAG)
TARGET     := NooDS-Wii
CONFIG_OUT := $(TARGET)_$(CONFIG_TAG)

LDFLAGS += $(CXXFLAGS) \
-Wl,-Map,$(CONFIG_OUT).elf.map \
-L$(DEVKITPRO)/portlibs/wii/lib \
-L$(DEVKITPRO)/portlibs/ppc/lib \
-L$(DEVKITPRO)/libogc/lib \
-L$(DEVKITPRO)/libogc/lib/wii \
-lasnd -lwiikeyboard -lfat -lwiiuse -lbte -logc -lm

EXCLUDE    :=
SRCDIR     := NooDS-Wii
C_SOURCES  := $(foreach dir, $(SRCDIR), $(filter-out $(EXCLUDE), $(wildcard $(dir)/*.cpp)))
AS_SOURCES := $(foreach dir, $(SRCDIR), $(filter-out $(EXCLUDE), $(wildcard $(dir)/*.S)))
OBJECTS    := $(patsubst %.cpp,$(BUILDDIR)/%.o,$(C_SOURCES)) \
              $(patsubst %.S,$(BUILDDIR)/%.o,$(AS_SOURCES))
DEPS       := $(OBJECTS:.o=.d)

ELF2DOL := elf2dol

all: $(CONFIG_OUT).dol

-include $(DEPS)

$(CONFIG_OUT).dol: $(CONFIG_OUT).elf
	$(ELF2DOL) $< $@
	cp -f $@ $(TARGET).dol
	cp -f $< $(TARGET).elf
	cp -f $(CONFIG_OUT).elf.map $(TARGET).elf.map

$(CONFIG_OUT).elf: $(OBJECTS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CXXFLAGS)

$(BUILDDIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CXXFLAGS)

clean:
	rm -rf $(BUILDDIR) $(CONFIG_OUT).elf $(CONFIG_OUT).dol $(CONFIG_OUT).elf.map $(TARGET).elf $(TARGET).dol $(TARGET).elf.map

clean-all:
	rm -rf build_jit* $(TARGET)_jit*.elf $(TARGET)_jit*.dol $(TARGET)_jit*.elf.map $(TARGET).elf $(TARGET).dol $(TARGET).elf.map $(SRCDIR)/*.o

.PHONY: all clean clean-all test
