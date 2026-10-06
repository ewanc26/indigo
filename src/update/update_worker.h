#ifndef INDIGO_UPDATE_WORKER_H
#define INDIGO_UPDATE_WORKER_H

/* The network half of the update screen: one short-lived thread per request,
 * so the frame loop never waits on GitHub. It only talks to the one URL in
 * update.h and the release asset that URL's manifest names (which
 * indigo_update_asset_ok has pinned to this repository's releases), with
 * Wolfram's token-less public GET. No account credential exists on this path,
 * and nothing it logs holds a URL or a response body.
 *
 * Results come back as events, polled once per frame on the main thread, and
 * it is the main thread that moves the state machine and does the swap: the
 * swap needs RomFS closed, which is not a thing to do from a worker. */

#include "update/update.h"

#include <stdbool.h>

typedef enum {
    INDIGO_UPDATE_EVENT_NONE = 0,
    INDIGO_UPDATE_EVENT_CHECKED, /* version, size and is_update are set */
    INDIGO_UPDATE_EVENT_STAGED,  /* verified, written and re-hashed from the card */
    INDIGO_UPDATE_EVENT_FAILED,  /* message says why, written for a person */
} indigo_update_event_kind;

typedef struct {
    indigo_update_event_kind kind;
    char version[INDIGO_UPDATE_VERSION_MAX];
    unsigned long size;
    bool is_update;
    char message[160];
} indigo_update_event;

/* `argv0` is the path the Homebrew Menu launched (for where to stage), or NULL.
 * Call once at start-up. */
void indigo_update_worker_init(const char *argv0, const char *state_path);

/* Start a check, or the download of the release the last check offered. False
 * when a request is already running, or the console build is absent. */
bool indigo_update_worker_check(void);
bool indigo_update_worker_download(void);

/* The next finished event, once; false when there is none. Main thread. */
bool indigo_update_worker_poll(indigo_update_event *ev);

/* Where the staged file is and the swap to run once the state machine says
 * READY. Returns false if nothing is staged. Main thread, RomFS closed. */
bool indigo_update_worker_install(void);

/* Wait for a running request to finish. Call before shutdown. */
void indigo_update_worker_stop(void);

#endif
