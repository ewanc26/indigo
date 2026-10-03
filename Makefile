#---------------------------------------------------------------------------------
# Indigo — native Nintendo 3DS homebrew
#---------------------------------------------------------------------------------

HOST_GOALS := test warnings snapshots
ifneq ($(filter $(HOST_GOALS),$(MAKECMDGOALS)),)
include mk/host.mk
else
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment")
endif

TOPDIR ?= $(CURDIR)

include $(DEVKITARM)/3ds_rules

TARGET := indigo
BUILD := build

SOURCES := src src/app src/ui src/input src/atproto src/util src/gfx
DATA :=
INCLUDES := src
GRAPHICS :=
GFXBUILD := $(BUILD)

APP_TITLE := Indigo
APP_DESCRIPTION := Native AT Protocol / Bluesky client for Nintendo 3DS
APP_AUTHOR := Ewan C

# devkitPro's 3DS portlibs provide citro2d/citro3d, curl, mbedTLS and zlib.
CTRULIB := $(DEVKITPRO)/libctru
PORTLIBS := $(DEVKITPRO)/portlibs/3ds

# Wolfram — my C AT Protocol SDK, built for the 3DS as a sibling checkout.
WOLFRAM_ROOT ?= $(TOPDIR)/../wolfram
WOLFRAM_BUILD ?= $(WOLFRAM_ROOT)/build-3ds
WOLFRAM_LIB := $(WOLFRAM_BUILD)/libwolfram.a

ifneq ($(wildcard $(WOLFRAM_LIB)),)
  WOLFRAM_CFLAGS := -DWOLFRAM_3DS -I$(WOLFRAM_ROOT)/include -I$(WOLFRAM_BUILD)/_deps/cjson-src
  WOLFRAM_LIBS := $(WOLFRAM_LIB)                   $(WOLFRAM_BUILD)/_deps/cjson-build/libcjson.a                   $(WOLFRAM_BUILD)/_deps/libcbor-build/src/libcbor.a
else
  $(warning Wolfram is not built for 3DS at $(WOLFRAM_LIB); building without protocol support. Run: make wolfram-3ds)
  WOLFRAM_CFLAGS :=
  WOLFRAM_LIBS :=
endif

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS := -g -Wall -Wextra -O2 -mword-relocations           -ffunction-sections -fdata-sections           $(ARCH) $(WOLFRAM_CFLAGS)

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS := -g $(ARCH)

LIBS := $(WOLFRAM_LIBS)         -lcitro2d -lcitro3d         -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz         -lctru -lm

LIBDIRS := $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
# Standard devkitARM recursive build layout
#---------------------------------------------------------------------------------

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))                 $(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir))                 $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
GFXFILES := $(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir))                   $(foreach dir,$(LIBDIRS),-I$(dir)/include)                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifneq ($(strip $(CPPFILES)),)
export LD := $(CXX)
else
export LD := $(CC)
endif

.PHONY: all clean run run-emu wolfram-3ds

# Emulator used by run-emu. Override with EMU=/path/to/emulator.
EMU ?= $(HOME)/Applications/Azahar.app/Contents/MacOS/azahar

all: $(BUILD) $(TARGET).3dsx

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).lst

run: all
	@echo "Copy $(TARGET).3dsx to sd:/3ds/indigo/indigo.3dsx"

# Build, then launch the .3dsx in an emulator (Azahar by default; Citra or
# Lime3DS also work: make run-emu EMU=/path/to/citra). Emulator-only: this
# says nothing about real hardware.
run-emu: all
	@test -x "$(EMU)" || { echo "emulator not found at $(EMU); set EMU=/path/to/azahar"; exit 1; }
	"$(EMU)" "$(CURDIR)/$(TARGET).3dsx"

# Build Wolfram (the sibling checkout) for 3DS using its own toolchain file.
wolfram-3ds:
	cmake -S "$(WOLFRAM_ROOT)" -B "$(WOLFRAM_BUILD)" \
	  -DCMAKE_TOOLCHAIN_FILE="$(WOLFRAM_ROOT)/.devdeps/3ds.cmake" \
	  -DWOLFRAM_BUILD_3DS=ON -DWOLFRAM_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Debug
	cmake --build "$(WOLFRAM_BUILD)"

else

# 3ds_rules supplies the compiler, linker, dependency tracking and 3DSX/SMDH
# packaging rules. Keeping this structure close to devkitPro's application
# template makes Indigo easier to build with current toolchains.

endif

endif
