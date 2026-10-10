.SUFFIXES:
export DEVKITPRO 	:= /opt/devkitpro
export DEVKITPPC	:= $(DEVKITPRO)/devkitPPC
include $(DEVKITPPC)/wii_rules

JIT    ?= 0
BLOCKS ?= 0

CC      := $(DEVKITPPC)/bin/powerpc-eabi-g++
MACHDEP := -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -fsigned-char -ffast-math -funroll-loops -fauto-inc-dec -finline-functions
INCLUDES += -I/opt/devkitpro/portlibs/wii/include -I/opt/devkitpro/portlibs/ppc/include -I/opt/devkitpro/libogc/include
JIT_DEFS := -DNOODS_JIT=$(JIT) -DNOODS_JIT_BLOCKS=$(BLOCKS)
CXXFLAGS += -O2 -MMD -MP \
-DENDIAN_BIG \
$(JIT_DEFS) \
$(INCLUDES) $(MACHDEP)

CONFIG_TAG := jit$(JIT)_blocks$(BLOCKS)
BUILDDIR   := build_$(CONFIG_TAG)
TARGET     := NooDS-Wii
CONFIG_OUT := $(TARGET)_$(CONFIG_TAG)

LDFLAGS += $(CXXFLAGS) \
-Wl,-Map,$(CONFIG_OUT).elf.map \
-L/opt/devkitpro/portlibs/wii/lib \
-L/opt/devkitpro/portlibs/ppc/lib \
-L/opt/devkitpro/libogc/lib \
-L/opt/devkitpro/libogc/lib/wii \
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
