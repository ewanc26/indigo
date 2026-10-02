#---------------------------------------------------------------------------------
# Indigo — native Nintendo 3DS homebrew
#---------------------------------------------------------------------------------

TOPDIR ?= $(CURDIR)

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPro")
endif

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/base_rules

TARGET := indigo
BUILD := build
SOURCES := src src/app src/ui src/input src/net src/atproto src/util
DATA :=
INCLUDES := src

WOLFRAM_ROOT ?= $(TOPDIR)/../wolfram
WOLFRAM_BUILD ?= $(WOLFRAM_ROOT)/build-3ds
WOLFRAM_LIB := $(WOLFRAM_BUILD)/libwolfram.a

LIBCTRU := $(DEVKITPRO)/libctru
PORTLIBS := $(DEVKITPRO)/portlibs/3ds

# Wolfram is optional for the first native shell. Once protocol code starts
# referencing it, the missing library should become a hard build error rather
# than silently producing a client without AT Protocol support.
ifneq ($(wildcard $(WOLFRAM_LIB)),)
  WOLFRAM_CFLAGS := -I$(WOLFRAM_ROOT)/include -I$(WOLFRAM_BUILD)/_deps/cjson-src -DWOLFRAM_3DS
  WOLFRAM_LIBS := $(WOLFRAM_LIB)
else
  WOLFRAM_CFLAGS :=
  WOLFRAM_LIBS :=
endif

CFLAGS := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections \
          $(INCLUDE) $(WOLFRAM_CFLAGS)

CXXFLAGS := $(CFLAGS) -std=gnu++17

ASFLAGS := -g $(ARCH)

LIBS := $(WOLFRAM_LIBS) -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lctru

LIBDIRS := $(LIBCTRU) $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_TITLE := Indigo
export APP_DESCRIPTION := Native AT Protocol / Bluesky client for Nintendo 3DS
export APP_AUTHOR := Ewan C

.PHONY: all clean run

all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).3dsx $(OUTPUT).smdh

run: all
	@echo "Copy $(OUTPUT).3dsx to sd:/3ds/indigo/indigo.3dsx"

else

export LD := $(CC)
export LIBS := $(LIBS)
export LIBPATHS := $(LIBPATHS)

endif

#---------------------------------------------------------------------------------
# 3DSX packaging
#---------------------------------------------------------------------------------

$(TARGET).3dsx: $(OUTPUT).elf
	3dsxtool $< $@

# The base rules provide the ELF link target from OFILES/OUTPUT.
all: $(TARGET).3dsx
