#ifndef INDIGO_SESSION_H
#define INDIGO_SESSION_H

#include "app/timeline.h"
#include "atproto/errors.h"

#include <stdbool.h>

typedef enum {
    INDIGO_SESSION_EVENT_NONE = 0,
    INDIGO_SESSION_EVENT_SIGNED_IN,
    INDIGO_SESSION_EVENT_SIGN_IN_FAILED,
    INDIGO_SESSION_EVENT_SIGNED_OUT,
    INDIGO_SESSION_EVENT_TIMELINE_PAGE,
    INDIGO_SESSION_EVENT_TIMELINE_FAILED,
    INDIGO_SESSION_EVENT_POST_ACTION_DONE,
    INDIGO_SESSION_EVENT_POST_ACTION_FAILED,
} indigo_session_event_kind;

typedef enum {
    INDIGO_POST_ACTION_NONE = 0,
    INDIGO_POST_ACTION_LIKE,
    INDIGO_POST_ACTION_UNLIKE,
    INDIGO_POST_ACTION_REPOST,
    INDIGO_POST_ACTION_UNREPOST,
} indigo_post_action;

/* Posts fetched per timeline request. */
#define INDIGO_PAGE_SIZE 15

typedef struct {
    indigo_session_event_kind kind;
    indigo_failure failure;
    char account[256];
    /* TIMELINE_PAGE: how many posts indigo_session_page() holds, and the
     * cursor for the next page (empty when there are no more). */
    unsigned page_count;
    char cursor[INDIGO_CURSOR_MAX];
    /* POST_ACTION_*: what was attempted, on which post. For LIKE/REPOST,
     * record_uri is the new record; it is empty for UNLIKE/UNREPOST. */
    indigo_post_action action;
    char post_uri[INDIGO_POST_URI_MAX];
    char record_uri[INDIGO_POST_URI_MAX];
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

/* `cursor` NULL or empty fetches the first page. */
bool indigo_session_submit_timeline(const char *cursor);
/* `undo_uri` is the like/repost record to delete for UNLIKE/UNREPOST. */
bool indigo_session_submit_post_action(indigo_post_action action, const char *post_uri,
                                       const char *post_cid, const char *undo_uri);

/* True while a job is running or its event has not been polled. */
bool indigo_session_busy(void);

/* The posts of the last TIMELINE_PAGE event. Valid until the next submit, so
 * read it as soon as the event is polled. */
const indigo_post *indigo_session_page(unsigned *count);

/* Returns true and fills `out` when a job finished since the last poll. */
bool indigo_session_poll(indigo_session_event *out);

/* True if a saved session file exists (cheap; used to pick the first screen). */
bool indigo_session_has_saved(void);

#endif
