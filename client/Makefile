#---------------------------------------------------------------------------------
# Dingplay Chat — 3DS / 2DS family homebrew (libctru + citro2d)
#
# Requires devkitPro with the 3ds-dev group installed:
#   dkp-pacman -S 3ds-dev
# Then: make            → dingplay-chat.3dsx (Homebrew Launcher)
#       make cia        → dingplay-chat.cia  (needs makerom + bannertool on PATH)
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
TARGET      := dingplay-chat
BUILD       := build
SOURCES     := source source/screens source/local source/third_party
DATA        := data
INCLUDES    := source source/third_party
GRAPHICS    := gfx
ROMFS       := romfs
GFXBUILD    := $(ROMFS)/gfx

APP_TITLE       := Dingplay Chat
APP_DESCRIPTION := Chat with Dingplay friends online or over Local Wireless
APP_AUTHOR      := Dingplay
ICON            := icon.png

# Unique id for the Local Wireless beacon; also used as the CIA title id low bits.
APP_UNIQUE_ID   := 0xD1A6

#---------------------------------------------------------------------------------
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -Wno-unused-function -O2 -mword-relocations \
           -ffunction-sections -fdata-sections \
           $(ARCH)

CFLAGS  += $(INCLUDE) -D__3DS__ -DAPP_VERSION=\"0.1.0\" -DAPP_UNIQUE_ID=$(APP_UNIQUE_ID)
# cJSON is vendored; keep its own warnings quiet.
CFLAGS  += -DCJSON_HIDE_SYMBOLS=0

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    := -lcitro2d -lcitro3d -lctru -lm
LIBDIRS := $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES    := $(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD := $(CC)

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN     := $(addsuffix .o,$(BINFILES))
export OFILES_SHADERS := $(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o)
export OFILES         := $(OFILES_BIN) $(OFILES_SHADERS) $(OFILES_SOURCES)
export HFILES         := $(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
                         $(addsuffix .h,$(subst .,_,$(BINFILES))) \
                         $(GFXFILES:.t3s=.h)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS := $(if $(NO_SMDH),,$(OUTPUT).smdh)

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

FONT_SRC_DIR := $(TOPDIR)/../fonts/Nunito/static
FONTS := $(ROMFS)/nunito-extrabold.bcfnt $(ROMFS)/nunito-black.bcfnt

.PHONY: all clean fonts cia

#---------------------------------------------------------------------------------
all: $(BUILD) $(GFXBUILD) $(DEPSDIR) $(ROMFS) fonts $(GFXFILES:.t3s=.t3x)
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

$(ROMFS):
	@mkdir -p $@

# Nunito TTF → BCFNT for citro2d. mkbcfnt ships with devkitPro's tex3ds package.
fonts: $(FONTS)

$(ROMFS)/nunito-extrabold.bcfnt: $(FONT_SRC_DIR)/Nunito-ExtraBold.ttf
	@echo mkbcfnt $(notdir $<)
	@mkbcfnt -o $@ -s 24 $<

$(ROMFS)/nunito-black.bcfnt: $(FONT_SRC_DIR)/Nunito-Black.ttf
	@echo mkbcfnt $(notdir $<)
	@mkbcfnt -o $@ -s 24 $<

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).cia $(GFXBUILD) $(FONTS) banner.bnr icon.icn

#---------------------------------------------------------------------------------
%.t3x %.h: %.t3s
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------
# CIA build (installable via FBI). Needs `bannertool` and `makerom` on PATH.
cia: all
	@bannertool makebanner -i banner.png -a banner.wav -o banner.bnr
	@bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $(APP_ICON) -o icon.icn
	@makerom -f cia -o $(TARGET).cia -DAPP_ENCRYPTED=false -rsf tools/app.rsf -target t -exefslogo -elf $(TARGET).elf -icon icon.icn -banner banner.bnr -DAPP_ROMFS=$(ROMFS)
	@echo built ... $(TARGET).cia

#---------------------------------------------------------------------------------
else

DEPENDS := $(OFILES:.o=.d)

#---------------------------------------------------------------------------------
$(OUTPUT).3dsx : $(OUTPUT).elf $(_3DSXDEPS)
$(OFILES_SOURCES) : $(HFILES)
$(OUTPUT).elf : $(OFILES)

#---------------------------------------------------------------------------------
%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS : %.t3x %.shbin
%.t3x.o %_t3x.h : %.t3x
	@echo $(notdir $<)
	@$(bin2o)

%.shbin.o %_shbin.h : %.shbin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
