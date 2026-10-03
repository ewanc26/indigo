#ifndef INDIGO_SESSION_H
#define INDIGO_SESSION_H

#include "atproto/errors.h"

#include <stdbool.h>

typedef enum {
    INDIGO_SESSION_EVENT_NONE = 0,
    INDIGO_SESSION_EVENT_SIGNED_IN,
    INDIGO_SESSION_EVENT_SIGN_IN_FAILED,
    INDIGO_SESSION_EVENT_SIGNED_OUT,
} indigo_session_event_kind;

typedef struct {
    indigo_session_event_kind kind;
    indigo_failure failure;
    char account[256];
} indigo_session_event;

/*
 * Network work happens on one worker thread so the frame loop never blocks.
 * The main thread submits a job and polls for its event; at most one job runs
 * at a time. `session_path` is where the resumable session is kept.
 */
bool indigo_session_start(const char *session_path);
void indigo_session_stop(void);

/* Each returns false if a job is already running. Strings are copied. */
bool indigo_session_submit_login(const char *service, const char *identifier,
                                 const char *password);
bool indigo_session_submit_resume(void);
bool indigo_session_submit_logout(void);

/* Returns true and fills `out` when a job finished since the last poll. */
bool indigo_session_poll(indigo_session_event *out);

/* True if a saved session file exists (cheap; used to pick the first screen). */
bool indigo_session_has_saved(void);

#endif
