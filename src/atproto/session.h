#ifndef INDIGO_SESSION_H
#define INDIGO_SESSION_H

#include "app/search.h"
#include "app/social.h"
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
    INDIGO_SESSION_EVENT_THREAD_PAGE,
    INDIGO_SESSION_EVENT_THREAD_FAILED,
    INDIGO_SESSION_EVENT_PROFILE_LOADED,
    INDIGO_SESSION_EVENT_PROFILE_FAILED,
    INDIGO_SESSION_EVENT_NOTIFICATIONS_PAGE,
    INDIGO_SESSION_EVENT_NOTIFICATIONS_FAILED,
    INDIGO_SESSION_EVENT_PUBLISHED,
    INDIGO_SESSION_EVENT_PUBLISH_FAILED,
    INDIGO_SESSION_EVENT_SEARCH_PAGE,
    INDIGO_SESSION_EVENT_SEARCH_FAILED,
    INDIGO_SESSION_EVENT_POST_SEARCH_PAGE,
    INDIGO_SESSION_EVENT_LISTS_PAGE,
    INDIGO_SESSION_EVENT_FEEDS_PAGE,
    INDIGO_SESSION_EVENT_FOLLOW_DONE,
    INDIGO_SESSION_EVENT_FOLLOW_FAILED,
    INDIGO_SESSION_EVENT_GRAPH_DONE,
    INDIGO_SESSION_EVENT_GRAPH_FAILED,
} indigo_session_event_kind;

/* Follow and unfollow are separate actions rather than a toggle with one
 * entry point: unfollow deletes a record by URI, and there is no URI to
 * derive from a handle. */
typedef enum {
    INDIGO_FOLLOW_NONE = 0,
    INDIGO_FOLLOW,
    INDIGO_UNFOLLOW,
} indigo_follow_action;

typedef enum {
    INDIGO_POST_ACTION_NONE = 0,
    INDIGO_POST_ACTION_LIKE,
    INDIGO_POST_ACTION_UNLIKE,
    INDIGO_POST_ACTION_REPOST,
    INDIGO_POST_ACTION_UNREPOST,
} indigo_post_action;

/* Posts fetched per timeline request. */
#define INDIGO_PAGE_SIZE 15
/* Posts kept from a thread: ancestors, the post, then replies in order. */
#define INDIGO_THREAD_MAX 40

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
    /* THREAD_PAGE: index in indigo_session_page() of the post that was asked
     * for. NOTIFICATIONS_PAGE: page_count counts notifications. */
    unsigned focus;
    /* PUBLISHED: what was published (the app refreshes the right view). */
    indigo_compose_mode compose_mode;
    /* FOLLOW_*: what was attempted. `actor` is the subject did; a follow puts
     * its new record URI in `record_uri`, while an unfollow deletes the URI
     * the app already had and so reports none. */
    indigo_follow_action follow;
    char actor[INDIGO_PROFILE_DID_MAX];
    /* GRAPH_*: what was attempted. A block reports its new record URI in
     * `record_uri`; mute, unmute and unblock report none. */
    indigo_graph_action graph;
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

/* Replies, ancestors and the post itself; `uri` is an at:// post URI. */
bool indigo_session_submit_thread(const char *uri);
/* `actor` is a handle or DID. */
bool indigo_session_submit_profile(const char *actor);
bool indigo_session_submit_notifications(void);
/* Publish a post, reply or quote. For a reply, `root_*` is the thread root
 * and `target_*` the post being answered; for a quote, `target_*` is quoted. */
bool indigo_session_submit_publish(indigo_compose_mode mode, const char *text,
                                   const char *target_uri, const char *target_cid,
                                   const char *root_uri, const char *root_cid);
/* Find people by name or handle. Results are bounded to INDIGO_SEARCH_MAX;
 * there is no paging, because a 3DS list that cannot show page two is not a
 * list worth paging. */
bool indigo_session_submit_search(const char *query);
/* Post search. Results are indigo_post, so this has its own result array and
 * its own event rather than sharing the actor search's. */
bool indigo_session_submit_post_search(const char *query);
/* One person's posts. Shares post search's result array and event, the way the
 * people lists share the actor search's. */
bool indigo_session_submit_author_feed(const char *actor);
/* The signed-in account's curated lists, then one list's members. The members
 * reuse the actor search's result array and events, the way the people lists
 * do; the lists themselves have their own. */
bool indigo_session_submit_lists(void);
bool indigo_session_submit_list_members(const char *list_uri);
void indigo_session_post_search_results(const indigo_post **posts, unsigned *count);
/* The account's curated lists. A list is a third result type, so it has its
 * own array and its own event rather than sharing the actor search's. */
void indigo_session_lists_results(const indigo_list **lists, unsigned *count);
/* The account's saved feeds, and one feed's posts. The feeds are list-shaped
 * (a name with a URI to open), so they share the lists' result type but have
 * their own array and event; the feed posts reuse the timeline's, because
 * getFeed returns the same shape getTimeline does. */
bool indigo_session_submit_feeds(void);
bool indigo_session_submit_feed(const char *feed_uri, const char *cursor);
void indigo_session_feeds_results(const indigo_list **feeds, unsigned *count);
/* Fetch one person's followers or following. Reuses the actor search's result
 * array and its events, because the result type is the same. */
bool indigo_session_submit_people(indigo_search_kind kind, const char *subject);
/* Follow or unfollow `did`. `follow_uri` is required for an unfollow and
 * ignored for a follow, which creates the record and reports its URI back. */
bool indigo_session_submit_follow(indigo_follow_action action, const char *did,
                                  const char *follow_uri);
/* Mute/unmute/block/unblock `did`. `block_uri` is required to unblock and
 * ignored otherwise. */
bool indigo_session_submit_graph(indigo_graph_action action, const char *did,
                                 const char *block_uri);

/* True while a job is running or its event has not been polled. */
bool indigo_session_busy(void);

/* The posts of the last TIMELINE_PAGE event. Valid until the next submit, so
 * read it as soon as the event is polled. */
const indigo_post *indigo_session_page(unsigned *count);

/* Results of the last PROFILE_LOADED / NOTIFICATIONS_PAGE event; same
 * lifetime rule as indigo_session_page(). */
const indigo_profile *indigo_session_profile(void);
const indigo_notification *indigo_session_notifications(unsigned *count);
const indigo_actor *indigo_session_search_results(unsigned *count);

/* Returns true and fills `out` when a job finished since the last poll. */
bool indigo_session_poll(indigo_session_event *out);

/* True if a saved session file exists (cheap; used to pick the first screen). */
bool indigo_session_has_saved(void);

#endif
