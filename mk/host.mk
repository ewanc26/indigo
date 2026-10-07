# Host-side targets: no devkitARM required.
#
#   make test       build and run unit tests (warnings are errors, ASan + UBSan)
#   make warnings   compile every host-portable source with -Werror and extra flags
#   make snapshots  render layout PNGs into build-host/snapshots

HOST_CC ?= cc
HOST_CXX ?= c++
# Wolfram's datetime check is C++ (cpp/wolfram/syntax.cpp), so the binaries need the C++ runtime.
HOST_CXXLIB ?= $(if $(filter Darwin,$(shell uname -s)),-lc++,-lstdc++)
HOST_OUT := build-host
HOST_WARN := -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wstrict-prototypes \
             -Wmissing-prototypes -Wcast-qual -Wundef -Werror
HOST_INC := -Isrc -Itools

# Indigo's protocol-adjacent logic is Wolfram's: muted-word matching, the update
# signature check, the CDN image URLs. The host build therefore needs a Wolfram
# checkout beside this one (CI checks out the release named in wolfram.ref) and
# cJSON's headers (Wolfram's preference types use them). Wolfram's own sources
# are compiled under the flags Wolfram uses, not Indigo's warning set, into
# objects linked into every host binary.
WOLFRAM_ROOT ?= ../wolfram
ifeq ($(wildcard $(WOLFRAM_ROOT)/include/wolfram/update.h),)
$(error The host build needs a Wolfram checkout at $(WOLFRAM_ROOT) (the release in wolfram.ref); set WOLFRAM_ROOT)
endif
HOST_CJSON_CFLAGS ?= $(shell pkg-config --cflags libcjson 2>/dev/null)
HOST_CJSON_LIBS ?= $(shell pkg-config --libs libcjson 2>/dev/null || echo -lcjson)
HOST_WOLFRAM_INC := -I$(WOLFRAM_ROOT)/include $(HOST_CJSON_CFLAGS)
HOST_WOLFRAM_FILES := src/agent/muted_words.c src/agent/moderation.c src/failure.c src/time.c src/cdn.c src/attach.c src/update/ed25519.c \
                      src/update/signature.c src/update/update_core.c
HOST_WOLFRAM_CXX_FILES := cpp/wolfram/syntax.cpp
HOST_WOLFRAM_OBJS := $(addprefix $(HOST_OUT)/wf_,$(subst /,_,$(HOST_WOLFRAM_FILES:.c=.o))) \
                     $(addprefix $(HOST_OUT)/wf_,$(subst /,_,$(HOST_WOLFRAM_CXX_FILES:.cpp=.o)))

# Sources that never touch libctru/citro2d.
HOST_SRCS := src/app/app.c src/app/search.c src/app/signin.c src/app/social.c src/app/timeline.c src/atproto/errors.c src/atproto/prefs.c src/media/media.c src/store/session_codec.c src/store/session_store.c src/store/settings_codec.c src/store/settings_store.c src/store/draft_store.c src/store/file.c src/gfx/canvas.c src/ui/layout.c src/ui/layout_widgets.c src/ui/layout_post.c src/ui/layout_top.c src/ui/layout_bottom.c src/ui/wrap.c src/util/log.c src/util/clock.c src/update/update.c src/update/updater.c src/update/update_sig.c src/media/cdn_url.c
HOST_SANITIZE := -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1

.PHONY: test warnings snapshots

test: $(HOST_OUT)/tests
	@# The suite runs 70-odd tests in one process and each holds a 565KB
	@# indigo_app, so peak stack is around 17MB -- more than the 8MB a default
	@# shell allows, which made the result depend on what invoked make rather
	@# than on the code. Raised here, and ignored where the hard limit is lower.
	@( ulimit -s 65536 2>/dev/null || true; $(HOST_OUT)/tests )

# One rule per Wolfram source: build-host/wf_update_ed25519.o from src/update/ed25519.c.
define WOLFRAM_OBJ_RULE
$(HOST_OUT)/wf_$(subst /,_,$(1:.c=.o)): $(WOLFRAM_ROOT)/$(1)
	@mkdir -p $(HOST_OUT)
	$(HOST_CC) -std=c11 -O1 -g $(HOST_SANITIZE) $(HOST_WOLFRAM_INC) -c -o $$@ $$<
endef
$(foreach f,$(HOST_WOLFRAM_FILES),$(eval $(call WOLFRAM_OBJ_RULE,$(f))))

define WOLFRAM_CXX_OBJ_RULE
$(HOST_OUT)/wf_$(subst /,_,$(1:.cpp=.o)): $(WOLFRAM_ROOT)/$(1)
	@mkdir -p $(HOST_OUT)
	$(HOST_CXX) -std=c++17 -O1 -g $(HOST_SANITIZE) -I$(WOLFRAM_ROOT)/include -c -o $$@ $$<
endef
$(foreach f,$(HOST_WOLFRAM_CXX_FILES),$(eval $(call WOLFRAM_CXX_OBJ_RULE,$(f))))

$(HOST_OUT)/tests: tests/tests.c $(HOST_SRCS) $(HOST_WOLFRAM_OBJS) $(wildcard src/*/*.h)
	@mkdir -p $(HOST_OUT)
	$(HOST_CC) $(HOST_WARN) $(HOST_SANITIZE) $(HOST_INC) $(HOST_WOLFRAM_INC) -o $@ tests/tests.c $(HOST_SRCS) $(HOST_WOLFRAM_OBJS) $(HOST_CXXLIB) $(HOST_CJSON_LIBS)

warnings:
	@mkdir -p $(HOST_OUT)
	@for f in $(HOST_SRCS) tests/tests.c; do \
	  echo "CC  $$f"; \
	  $(HOST_CC) $(HOST_WARN) -O2 $(HOST_INC) $(HOST_WOLFRAM_INC) -c $$f -o $(HOST_OUT)/warn.o || exit 1; \
	done
	@echo "warnings sweep clean"

$(HOST_OUT)/snapshot: tools/snapshot.c tools/snapshot_font.h $(HOST_SRCS) $(HOST_WOLFRAM_OBJS) $(wildcard src/*/*.h)
	@mkdir -p $(HOST_OUT)
	$(HOST_CC) -std=c11 -Wall -Wextra -O2 $(HOST_SANITIZE) $(HOST_INC) $(HOST_WOLFRAM_INC) -o $@ tools/snapshot.c $(HOST_SRCS) $(HOST_WOLFRAM_OBJS) $(HOST_CXXLIB) $(HOST_CJSON_LIBS) -lm

snapshots: $(HOST_OUT)/snapshot
	@mkdir -p $(HOST_OUT)/snapshots
	@$(HOST_OUT)/snapshot $(HOST_OUT)/snapshots
