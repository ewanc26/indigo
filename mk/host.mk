# Host-side targets: no devkitARM required.
#
#   make test       build and run unit tests (warnings are errors, ASan + UBSan)
#   make warnings   compile every host-portable source with -Werror and extra flags
#   make snapshots  render layout PNGs into build-host/snapshots
#   make e2e        drive the real session worker and a mock PDS on the host

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
HOST_WOLFRAM_FILES := src/agent/muted_words.c src/agent/profile_tab.c src/agent/moderation.c src/failure.c src/time.c src/cdn.c src/attach.c src/util.c src/update/ed25519.c \
                      src/update/signature.c src/update/update_core.c src/qr.c
HOST_WOLFRAM_CXX_FILES := cpp/wolfram/syntax.cpp
HOST_WOLFRAM_OBJS := $(addprefix $(HOST_OUT)/wf_,$(subst /,_,$(HOST_WOLFRAM_FILES:.c=.o))) \
                     $(addprefix $(HOST_OUT)/wf_,$(subst /,_,$(HOST_WOLFRAM_CXX_FILES:.cpp=.o)))

# Sources that never touch libctru/citro2d.
HOST_SRCS := src/app/app.c src/app/credits.c src/app/app_posts.c src/app/app_screens.c src/app/app_results.c src/app/app_messages.c src/app/timeline_cache.c src/app/post_queue.c src/app/app_queue.c src/store/cache_codec.c src/store/cache_store.c src/store/queue_codec.c src/store/queue_store.c src/app/search.c src/app/signin.c src/app/social.c src/app/timeline.c src/atproto/errors.c src/atproto/prefs.c src/atproto/chat.c src/atproto/video.c src/media/media.c src/store/session_codec.c src/store/session_store.c src/store/settings_codec.c src/store/settings_store.c src/store/draft_store.c src/store/file.c src/gfx/canvas.c src/ui/layout.c src/ui/layout_widgets.c src/ui/layout_post.c src/ui/layout_top.c src/ui/layout_bottom.c src/ui/wrap.c src/util/log.c src/util/clock.c src/update/update.c src/update/updater.c src/update/update_sig.c src/media/cdn_url.c
HOST_SANITIZE := -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1

.PHONY: test warnings snapshots e2e

# The end-to-end build compiles the real session worker with a stub <3ds.h>
# from tests/e2e/stubs (see tests/e2e/host_platform.c) and runs it against a
# loopback mock PDS. It needs a *host* build of Wolfram, which the ordinary
# host targets do not: they compile the handful of Wolfram sources they link.
HOST_SOEXT := $(if $(filter Darwin,$(shell uname -s)),dylib,so)
E2E_WOLFRAM_BUILD ?= $(WOLFRAM_ROOT)/build-host
HOST_WOLFRAM_LIB := $(firstword $(wildcard \
    $(E2E_WOLFRAM_BUILD)/libwolfram.$(HOST_SOEXT) \
    $(E2E_WOLFRAM_BUILD)/libwolfram.so \
    $(E2E_WOLFRAM_BUILD)/libwolfram.dylib))
HOST_E2E_SESSION_SRCS := src/atproto/session.c src/atproto/session_auth.c \
                         src/atproto/session_read.c src/atproto/session_view.c \
                         src/atproto/session_people.c src/atproto/session_lists.c \
                         src/atproto/session_write.c
HOST_E2E_SRCS := tests/e2e_main.c tests/e2e/mock_pds.c tests/e2e/host_platform.c \
                 $(HOST_SRCS) $(HOST_E2E_SESSION_SRCS)
# glibc hides usleep/nanosleep under -std=c11 unless a POSIX feature level is
# asked for; the harness and its pthread <3ds.h> stub need both.
HOST_E2E_CFLAGS := -DINDIGO_SESSION_HOST -DINDIGO_SESSION_REAL -D_DEFAULT_SOURCE \
                   -D_POSIX_C_SOURCE=200809L -Itests -Itests/e2e/stubs

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

# The e2e binary is not part of `make test`: it needs Wolfram built for the
# host with its full agent/chat surface, not just the sources the unit tests
# link. With no such build the target says how to make one instead of
# silently skipping the coverage.
e2e: $(HOST_OUT)/e2e
	@# The harness starts the worker, so its session file lives in a scratch
	@# directory. `rm` rather than trusting a previous run's state.
	@mkdir -p $(HOST_OUT)/e2e-data
	@rm -f $(HOST_OUT)/e2e-data/session.dat
	@LSAN_OPTIONS=suppressions=$(CURDIR)/tests/e2e/lsan.supp \
	  ./$(HOST_OUT)/e2e $(HOST_OUT)/e2e-data

$(HOST_OUT)/e2e: $(HOST_E2E_SRCS) $(HOST_WOLFRAM_LIB) \
                  $(wildcard src/*/*.h tests/e2e/*.h tests/e2e/stubs/*.h)
ifeq ($(HOST_WOLFRAM_LIB),)
	@echo "error: make e2e needs a host Wolfram build at $(E2E_WOLFRAM_BUILD)" >&2
	@echo "       cmake -S $(WOLFRAM_ROOT) -B $(E2E_WOLFRAM_BUILD) \\" >&2
	@echo "         -DWOLFRAM_BUILD_TESTS=OFF -DWOLFRAM_BUILD_EXAMPLES=OFF" >&2
	@echo "       cmake --build $(E2E_WOLFRAM_BUILD) -j4 --target wolfram" >&2
	@exit 1
else
	@mkdir -p $(HOST_OUT)
	$(HOST_CC) $(HOST_WARN) $(HOST_SANITIZE) $(HOST_INC) $(HOST_WOLFRAM_INC) \
	  $(HOST_E2E_CFLAGS) -o $@ $(HOST_E2E_SRCS) $(HOST_WOLFRAM_LIB) \
	  $(HOST_CJSON_LIBS) -lm -lpthread \
	  -Wl,-rpath,$(abspath $(E2E_WOLFRAM_BUILD)) \
	  -Wl,-rpath,$(abspath $(E2E_WOLFRAM_BUILD))/_deps/cjson-build
endif
