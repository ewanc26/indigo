# Host-side targets: no devkitARM required.
#
#   make test       build and run unit tests (warnings are errors, ASan + UBSan)
#   make warnings   compile every host-portable source with -Werror and extra flags
#   make snapshots  render layout PNGs into build-host/snapshots

HOST_CC ?= cc
HOST_OUT := build-host
HOST_WARN := -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wstrict-prototypes \
             -Wmissing-prototypes -Wcast-qual -Wundef -Werror
HOST_INC := -Isrc -Itools

# The signature gate on the update manifest (src/update/update_sig.c) calls
# Wolfram's verifier, so its test needs a Wolfram checkout beside this one (CI
# checks out the release named in wolfram.ref). Without one the host build is
# unchanged and that test is skipped, with a notice.
WOLFRAM_ROOT ?= ../wolfram
ifneq ($(wildcard $(WOLFRAM_ROOT)/include/wolfram/update.h),)
HOST_WOLFRAM_SRCS := src/update/update_sig.c
HOST_WOLFRAM_DEFS := -DINDIGO_HOST_WOLFRAM=1 -I$(WOLFRAM_ROOT)/include
# Wolfram's own sources are compiled under the flags Wolfram builds them with,
# not Indigo's warning set, into objects linked into the test binary.
HOST_WOLFRAM_OBJS := $(HOST_OUT)/wf_ed25519.o $(HOST_OUT)/wf_signature.o $(HOST_OUT)/wf_update_core.o
else
HOST_WOLFRAM_SRCS :=
HOST_WOLFRAM_DEFS :=
HOST_WOLFRAM_OBJS :=
endif

# Sources that never touch libctru/citro2d.
HOST_SRCS := src/app/app.c src/app/search.c src/app/signin.c src/app/social.c src/app/timeline.c src/atproto/errors.c src/atproto/prefs.c src/media/media.c src/store/session_codec.c src/store/session_store.c src/store/settings_codec.c src/store/settings_store.c src/store/draft_store.c src/gfx/canvas.c src/ui/layout.c src/ui/wrap.c src/util/log.c src/util/timefmt.c src/update/update.c src/update/updater.c
HOST_SANITIZE := -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1

.PHONY: test warnings snapshots

test: $(HOST_OUT)/tests
	@# The suite runs 70-odd tests in one process and each holds a 565KB
	@# indigo_app, so peak stack is around 17MB -- more than the 8MB a default
	@# shell allows, which made the result depend on what invoked make rather
	@# than on the code. Raised here, and ignored where the hard limit is lower.
	@( ulimit -s 65536 2>/dev/null || true; $(HOST_OUT)/tests )

$(HOST_OUT)/wf_%.o: $(WOLFRAM_ROOT)/src/update/%.c
	@mkdir -p $(HOST_OUT)
	$(HOST_CC) -std=c11 -O1 -g $(HOST_SANITIZE) -I$(WOLFRAM_ROOT)/include -c -o $@ $<

$(HOST_OUT)/tests: tests/tests.c $(HOST_SRCS) $(HOST_WOLFRAM_SRCS) $(HOST_WOLFRAM_OBJS) $(wildcard src/*/*.h)
	@mkdir -p $(HOST_OUT)
	@test -n "$(HOST_WOLFRAM_SRCS)" || echo "tests ... no Wolfram checkout at $(WOLFRAM_ROOT): skipping the update-signature test"
	$(HOST_CC) $(HOST_WARN) $(HOST_SANITIZE) $(HOST_INC) $(HOST_WOLFRAM_DEFS) -o $@ tests/tests.c $(HOST_SRCS) $(HOST_WOLFRAM_SRCS) $(HOST_WOLFRAM_OBJS)

warnings:
	@mkdir -p $(HOST_OUT)
	@for f in $(HOST_SRCS) tests/tests.c; do \
	  echo "CC  $$f"; \
	  $(HOST_CC) $(HOST_WARN) -O2 $(HOST_INC) -c $$f -o $(HOST_OUT)/warn.o || exit 1; \
	done
	@echo "warnings sweep clean"

$(HOST_OUT)/snapshot: tools/snapshot.c tools/snapshot_font.h $(HOST_SRCS) $(wildcard src/*/*.h)
	@mkdir -p $(HOST_OUT)
	$(HOST_CC) -std=c11 -Wall -Wextra -O2 $(HOST_INC) -o $@ tools/snapshot.c $(HOST_SRCS) -lm

snapshots: $(HOST_OUT)/snapshot
	@mkdir -p $(HOST_OUT)/snapshots
	@$(HOST_OUT)/snapshot $(HOST_OUT)/snapshots
