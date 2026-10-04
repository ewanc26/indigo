#---------------------------------------------------------------------------------
# Indigo — native Nintendo 3DS homebrew
#---------------------------------------------------------------------------------

# Project root, independent of CURDIR (which flips between outer and inner make).
PROJECT_ROOT := $(abspath $(dir $(abspath $(firstword $(MAKEFILE_LIST)))))

# Build identity, stamped by the buildinfo target below. This target runs in the
# outer make and does not require devkitARM.
BUILDINFO := $(PROJECT_ROOT)/src/util/buildinfo_gen.h

.PHONY: buildinfo

buildinfo:
	@c=$$(git -C $(PROJECT_ROOT) describe --tags --always --dirty 2>/dev/null || echo unknown); \
	n=$$(git -C $(PROJECT_ROOT) rev-list --count HEAD 2>/dev/null || echo 0); \
	d=$$(date +%Y-%m-%d); \
	printf '#pragma once\n#define INDIGO_BUILD_COMMIT "%s"\n#define INDIGO_BUILD_NUMBER %s\n#define INDIGO_BUILD_DATE "%s"\n' "$$c" "$$n" "$$d" > $(BUILDINFO).tmp; \
	if cmp -s $(BUILDINFO).tmp $(BUILDINFO); then rm $(BUILDINFO).tmp; else mv $(BUILDINFO).tmp $(BUILDINFO); echo "buildinfo ... $$n $$c"; fi

HOST_GOALS := test warnings snapshots buildinfo
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

SOURCES := src src/app src/ui src/input src/atproto src/store src/util src/gfx src/media
DATA :=
ROMFS := romfs
INCLUDES := src
GRAPHICS :=
GFXBUILD := $(BUILD)

APP_TITLE := Indigo
APP_DESCRIPTION := Native Bluesky client for Nintendo 3DS
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
CFLAGS += $(INCLUDE) -D__3DS__
ifneq ($(strip $(DEV_AUTOFILL)),)
  CFLAGS += -DINDIGO_DEV_AUTOFILL
endif

ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS := $(WOLFRAM_LIBS)         -lcitro2d -lcitro3d         -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz         -lctru -lm -lstdc++

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

ifneq ($(strip $(ROMFS)),)
  export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

ifneq ($(strip $(CPPFILES)),)
export LD := $(CXX)
else
export LD := $(CC)
endif

.PHONY: all clean run run-emu wolfram-3ds build-3ds test warnings snapshots

# Emulator used by run-emu. Override with EMU=/path/to/emulator.
EMU ?= $(HOME)/Applications/Azahar.app/Contents/MacOS/azahar

# The recursive make owns dependency tracking (objects, ELF, 3DSX), so it runs
# on every build. Depending on $(TARGET).3dsx here instead would let a stale
# executable survive a source change: the outer make has no rule for it.
#
# Bare `make` has to build the 3DSX. `buildinfo` is the first real rule in this
# file, so without an explicit .DEFAULT_GOAL the default goal is `buildinfo`,
# `make` stamps the header, and nothing is compiled.
#
# devkitPro's 3ds_rules/base_rules define no `all` of their own, so each half of
# the recursion has to supply one. This wrapper is the outer half; the inner
# half is in the else branch at the end of this file.
.DEFAULT_GOAL := all

all: buildinfo
	@if [ -z "$(WOLFRAM_LIBS)" ]; then \
		echo "all ... REFUSED: Wolfram is not linked — every ATProto/Bluesky call in this build would fail." >&2; \
		echo "all ... Build it first (see the Wolfram comment in the Makefile), then re-run make." >&2; \
		exit 1; \
	fi
	@mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile all

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).lst

run: all
	@echo "Copy $(TARGET).3dsx to sd:/3ds/indigo/indigo.3dsx"

# Build, then launch the .3dsx in an emulator (Azahar by default; Citra or
# Lime3DS also work: make run-emu EMU=/path/to/citra). Emulator-only: this
# says nothing about real hardware.
#
# Launching is not uniform. Azahar ignores a .3dsx passed on its own command
# line and just sits on its HOME menu, so when the emulator is a macOS app
# bundle the file is opened through LaunchServices instead. EMU_APP overrides
# the guess if the bundle does not follow the Contents/MacOS layout.
EMU_APP ?= $(shell _e=$(EMU); _d=$$(dirname $$_e); _d=$$(dirname $$_d); _d=$$(dirname $$_d); \
	   [ -f "$$_d/Contents/Info.plist" ] && echo "$$_d")

run-emu: build-3ds
	@test -x "$(EMU)" || { echo "emulator not found at $(EMU); set EMU=/path/to/azahar"; exit 1; }
	@if [ -n "$(EMU_APP)" ] && [ -d "$(EMU_APP)" ]; then \
		open -a "$(EMU_APP)" "$(CURDIR)/$(TARGET).3dsx"; \
	else \
		"$(EMU)" "$(CURDIR)/$(TARGET).3dsx"; \
	fi

# Build Wolfram (the sibling checkout) for 3DS using its own toolchain file.
wolfram-3ds:
	cmake -S "$(WOLFRAM_ROOT)" -B "$(WOLFRAM_BUILD)" \
	  -DCMAKE_TOOLCHAIN_FILE="$(WOLFRAM_ROOT)/.devdeps/3ds.cmake" \
	  -DWOLFRAM_BUILD_3DS=ON -DWOLFRAM_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Debug
	cmake --build "$(WOLFRAM_BUILD)"

# Linking the 3DSX needs the devkitPro 3DS portlibs (curl, mbedtls, zlib). They
# ship in the devkitPro image that CI builds in, not in a plain host devkitPro,
# so local 3DS builds go through the same image. The parent directory is
# mounted because Wolfram is a sibling checkout.
#
#   make            host build: correct in CI, and on a host with the portlibs
#   make build-3ds  container build: the local default
#
# Always starting from a clean build keeps the generated .d files pointing at
# one tree; they record absolute paths and are meaningless in the other.
EMU_CONTAINER ?= docker run --rm --user $$(id -u):$$(id -g) \
	  -e DEVKITPRO=/opt/devkitpro -e DEVKITARM=/opt/devkitpro/devkitARM \
	  -v $(CURDIR)/..:/work -w /work/$(notdir $(CURDIR)) devkitpro/devkitarm:latest

build-3ds:
	$(EMU_CONTAINER) sh -c 'make clean && make'

else

# The recursive make in build/. devkitPro supplies no `all`, so it is defined
# here: the goal the outer wrapper passes down.
all: $(OUTPUT).3dsx

# 3ds_rules supplies the compiler, linker and 3DSX/SMDH packaging rules. The
# two prerequisites below are what connect the object files to the final
# executable; without them the recursive make has no targets.
$(OUTPUT).3dsx : $(OUTPUT).elf $(_3DSXDEPS)

$(OUTPUT).elf : $(OFILES)

-include $(DEPSDIR)/*.d

endif

endif
