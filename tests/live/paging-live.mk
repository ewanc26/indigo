# Runs the request-list paging code in src/atproto/session_read.c against Wolfram's
# library and a local HTTP server. Host only; nothing touches the network.
#   make -f tests/live/paging-live.mk WOLFRAM_ROOT=../wolfram WOLFRAM_HOST_BUILD=/tmp/wf-host
# WOLFRAM_HOST_BUILD is a host build of Wolfram:
#   cmake -S ../wolfram -B /tmp/wf-host -DWOLFRAM_BUILD_TESTS=OFF -DWOLFRAM_BUILD_EXAMPLES=OFF
#   cmake --build /tmp/wf-host --target wolfram
CC ?= cc
WOLFRAM_ROOT ?= ../wolfram
WOLFRAM_HOST_BUILD ?= /tmp/wf-host
GUARD = -D__3DS__ -DWOLFRAM_3DS
INCS = -Isrc -Itools/ctr-stub -I$(WOLFRAM_ROOT)/include -I/usr/include/cjson
BUILD = build-live

paging-live: $(BUILD)/session_paging_live
	./$(BUILD)/session_paging_live

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/ctr_noop.o: tests/live/ctr_noop.c | $(BUILD)
	$(CC) -std=gnu2x -c $(GUARD) -Itools/ctr-stub $< -o $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) -std=gnu2x -c $(GUARD) $(INCS) $< -o $@

$(BUILD)/session_paging_live.o: tests/live/session_paging_live.c | $(BUILD)
	$(CC) -std=gnu2x -c $(GUARD) $(INCS) $< -o $@

# session.c, session_read.c, chat.c, log.c, timeline.c
OBJS = $(BUILD)/atproto/session.o $(BUILD)/atproto/session_read.o $(BUILD)/atproto/chat.o \
       $(BUILD)/util/log.o $(BUILD)/app/timeline.o $(BUILD)/ctr_noop.o $(BUILD)/session_paging_live.o

$(BUILD)/atproto $(BUILD)/util $(BUILD)/app: | $(BUILD)
	mkdir -p $@

$(BUILD)/atproto/%.o: src/atproto/%.c | $(BUILD)/atproto
	$(CC) -std=gnu2x -c $(GUARD) $(INCS) $< -o $@

$(BUILD)/util/%.o: src/util/%.c | $(BUILD)/util
	$(CC) -std=gnu2x -c $(GUARD) $(INCS) $< -o $@

$(BUILD)/app/%.o: src/app/%.c | $(BUILD)/app
	$(CC) -std=gnu2x -c $(GUARD) $(INCS) $< -o $@

$(BUILD)/session_paging_live: $(OBJS)
	$(CC) $(OBJS) -L$(WOLFRAM_HOST_BUILD) -lwolfram -lcjson -lmicrohttpd \
		-Wl,-rpath,$(WOLFRAM_HOST_BUILD) -Wl,--unresolved-symbols=ignore-all -o $@

.PHONY: paging-live
