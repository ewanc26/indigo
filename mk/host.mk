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

# Sources that never touch libctru/citro2d.
HOST_SRCS := src/app/app.c src/app/search.c src/app/signin.c src/app/social.c src/app/timeline.c src/atproto/errors.c src/store/session_codec.c src/store/session_store.c src/gfx/canvas.c src/ui/layout.c src/ui/wrap.c src/util/log.c src/util/timefmt.c
HOST_SANITIZE := -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1

.PHONY: test warnings snapshots

test: $(HOST_OUT)/tests
	@$(HOST_OUT)/tests

$(HOST_OUT)/tests: tests/tests.c $(HOST_SRCS) $(wildcard src/*/*.h)
	@mkdir -p $(HOST_OUT)
	$(HOST_CC) $(HOST_WARN) $(HOST_SANITIZE) $(HOST_INC) -o $@ tests/tests.c $(HOST_SRCS)

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
