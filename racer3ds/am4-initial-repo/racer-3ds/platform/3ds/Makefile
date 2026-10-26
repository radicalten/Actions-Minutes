#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#   If set to $(BUILD), it will statically link in the converted
#   files as if they were data files.
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# ROMFS is the directory which contains the RomFS, relative to the Makefile (Optional)
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png
#---------------------------------------------------------------------------------
TARGET		:=	racer3ds
APP_TITLE	:=	Racer 3DS
APP_DESCRIPTION	:=	Star Wars Episode I Racer - 3DS target
APP_AUTHOR	:=	racer-3ds
ICON		:=	icon.png

BUILD		:=	build
SOURCES		:=	source thirdparty/libctru-ndsp
DATA		:=
INCLUDES	:=	include
GRAPHICS	:=
GFXBUILD	:=	$(BUILD)
#ROMFS		:=	romfs
#GFXBUILD	:=	$(ROMFS)/gfx

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(CTRULIB)



#---------------------------------------------------------------------------------
# Optional targets
#   make            bring-up app (.3dsx + .smdh) - built without any ROM-derived code
#   make cia        installable .cia (80MB system mode: O3DS extended memory)
#   make runtime-check  cross-compiles the patched N64Recomp runtime for ARMv6K
#   make recompile      PC half of the pipeline: ROM -> RecompiledFuncs/*.c -> ARM objects
#                       (needs --rom; run tools/recompile.sh directly for the first time)
#
# GAME=1 additionally defines R3DS_WITH_GAME/R3DS_LINK_RUNTIME so main.cpp starts the
# recompiled entrypoint instead of the self-test. It needs RecompiledFuncs/ and
# rsp/aspMain.cpp generated from your own baserom.z64 - see BUILDING.md.
#---------------------------------------------------------------------------------
GAME		?=	0
N64RT		?=	$(TOPDIR)/../../deps/N64ModernRuntime

MAKEROM		?=	makerom

ifeq ($(GAME),1)
SOURCES		+=	source-game
CFLAGS		+=	-DR3DS_WITH_GAME -DR3DS_LINK_RUNTIME
# The runtime headers use C++20 (std::span, char8_t, <=>); -std=gnu++20 here overrides the
# gnu++17 set above for the game build only.
CXXFLAGS	+=	-std=gnu++20 -DR3DS_WITH_GAME -DR3DS_LINK_RUNTIME \
			-I$(N64RT)/ultramodern/include -I$(N64RT)/librecomp/include \
			-I$(N64RT)/N64Recomp/include -I$(N64RT)/thirdparty \
			-I$(N64RT)/thirdparty/concurrentqueue \
			-I$(CURDIR)/game
# ROM-derived objects (tools/recompile.sh) and the cross-compiled runtime
# (tools/build-runtime.sh): both directories must exist before `make game`.
GAME_OBJS	:=	$(wildcard $(CURDIR)/game/build/*.o) \
			$(wildcard $(CURDIR)/game/section_table.o) \
			$(wildcard $(CURDIR)/runtime-build/*.o)
# 3ds_rules links `$(LD) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS)`, so the ROM-derived
# objects and the runtime objects ride in LIBS. Plain .o files are fine there.
LIBS		+=	$(GAME_OBJS)
endif


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
ifeq ($(GFXBUILD),$(BUILD))
#---------------------------------------------------------------------------------
export T3XFILES :=  $(GFXFILES:.t3s=.t3x)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
export ROMFS_T3XFILES	:=	$(patsubst %.t3s, $(GFXBUILD)/%.t3x, $(GFXFILES))
export T3XHFILES		:=	$(patsubst %.t3s, $(BUILD)/%.h, $(GFXFILES))
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(addsuffix .o,$(T3XFILES))

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES))) \
			$(GFXFILES:.t3s=.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean cia cia-64 cia-game runtime-check recompile game

#---------------------------------------------------------------------------------
all: $(BUILD) $(GFXBUILD) $(DEPSDIR) $(ROMFS_T3XFILES) $(T3XHFILES)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

ifneq ($(GFXBUILD),$(BUILD))
$(GFXBUILD):
	@mkdir -p $@
endif

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p $@
endif

#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
# .cia: exheader from cia/app.rsf (SystemMode 80MB, SystemModeExt Legacy)
#---------------------------------------------------------------------------------
cia: $(OUTPUT).cia

$(OUTPUT).cia	:	$(OUTPUT).elf $(OUTPUT).smdh cia/app.rsf
	@echo building ... $(notdir $@)
	$(SILENTCMD)$(MAKEROM) -f cia -o $@ -target t -exefslogo \
		-elf $(OUTPUT).elf -rsf cia/app.rsf -icon $(OUTPUT).smdh \
		-DAPP_SYSTEM_MODE=80MB -DAPP_UNIQUE_ID=0xF9042 \
		-DAPP_VERSION_MAJOR=1 -DAPP_VERSION_MINOR=0 -DAPP_VERSION_MICRO=0

# Same binary, 64 MB system mode: for bisecting "is extended memory mode itself the
# problem?". Different UniqueId so both can be installed at once.
cia-64: $(OUTPUT)-64.cia

$(OUTPUT)-64.cia	:	$(OUTPUT).elf $(OUTPUT).smdh cia/app.rsf
	@echo building ... $(notdir $@)
	$(SILENTCMD)$(MAKEROM) -f cia -o $@ -target t -exefslogo \
		-elf $(OUTPUT).elf -rsf cia/app.rsf -icon $(OUTPUT).smdh \
		-DAPP_SYSTEM_MODE=64MB -DAPP_UNIQUE_ID=0xF9044 \
		-DAPP_VERSION_MAJOR=1 -DAPP_VERSION_MINOR=0 -DAPP_VERSION_MICRO=0

# The playable build: same app, plus the recompiled game and the runtime. Separate title id
# (0xF9046) so it installs next to the bring-up build instead of replacing it.
cia-game: $(OUTPUT).elf $(OUTPUT).smdh cia/app.rsf
	@echo building ... $(TARGET)-game.cia
	$(SILENTCMD)$(MAKEROM) -f cia -o $(TARGET)-game.cia -target t -exefslogo \
		-elf $(OUTPUT).elf -rsf cia/app.rsf -icon $(OUTPUT).smdh \
		-DAPP_SYSTEM_MODE=80MB -DAPP_UNIQUE_ID=0xF9046 \
		-DAPP_VERSION_MAJOR=1 -DAPP_VERSION_MINOR=0 -DAPP_VERSION_MICRO=0

recompile:
	@echo "recompile ..."
	@./tools/recompile.sh

# The game build. Needs game/ (tools/recompile.sh with your ROM) and runtime-build/
# (tools/build-runtime.sh) first; see BUILDING.md, "The game build".
game:
	@test -d game/build -a -n "$$(ls game/build/*.o 2>/dev/null)" || \
		{ echo "game/build is empty - run tools/recompile.sh --rom <your baserom.z64> first" >&2; exit 1; }
	@test -d runtime-build -a -n "$$(ls runtime-build/*.o 2>/dev/null)" || \
		{ echo "runtime-build is empty - run tools/build-runtime.sh first" >&2; exit 1; }
	@rm -f $(TARGET).elf $(TARGET).3dsx
	@$(MAKE) --no-print-directory GAME=1 all

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).cia $(TARGET)-64.cia $(OUTPUT).smdh $(TARGET).elf $(GFXBUILD)

#---------------------------------------------------------------------------------
$(GFXBUILD)/%.t3x	$(BUILD)/%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS	:	%.t3x %.shbin
#---------------------------------------------------------------------------------
%.t3x.o	%_t3x.h :	%.t3x
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

#---------------------------------------------------------------------------------
%.shbin.o %_shbin.h : %.shbin
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
