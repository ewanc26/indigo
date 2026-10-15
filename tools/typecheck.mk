# Type-check the 3DS-only session sources against Wolfram's headers, without libctru.
# Run from the repository root:
#   make -f tools/typecheck.mk WOLFRAM_ROOT=../wolfram
# Only types and calls are checked; nothing is linked or run. The libctru declarations
# are in tools/ctr-stub/3ds.h.
CC ?= cc
WOLFRAM_ROOT ?= ../wolfram
SESSION_TYPECHECK_SRCS = session session_read session_auth session_lists session_people session_view session_write

typecheck-session:
	@for f in $(SESSION_TYPECHECK_SRCS); do \
		echo "typecheck src/atproto/$$f.c"; \
		$(CC) -std=gnu2x -fsyntax-only -D__3DS__ -DWOLFRAM_3DS -Itools/ctr-stub -Isrc -I$(WOLFRAM_ROOT)/include -I/usr/include/cjson src/atproto/$$f.c || exit 1; \
	done
	@echo "typecheck-session: all session sources type-check"

.PHONY: typecheck-session
