#ifndef INDIGO_SESSION_INTERNAL_H
#define INDIGO_SESSION_INTERNAL_H

/*
 * What the session's files share and nothing else may use: the job a request
 * carries in, the one state block (the worker's and the results the main thread
 * reads), and the few helpers every job calls back into. The public interface
 * is atproto/session.h. Everything here exists only in a 3DS build with
 * Wolfram; without one session_stub.c stands in.
 *
 * The session is split by what a job is about: session.c owns the worker
 * thread, the lock, the event hand-off and the queue; session_auth.c signs in
 * and out; session_view.c turns Wolfram's views into Indigo's post, actor and
 * notification types; session_read.c reads timelines, threads, feeds and
 * notifications; session_people.c is people, profiles, follow and block;
 * session_lists.c is curated lists, saved feeds and the muted and blocked
 * lists; session_write.c publishes.
 */

#include "atproto/session.h"
#include "media/cdn_url.h"

#include "atproto/atproto.h"
#include "atproto/prefs.h"

#include "store/session_store.h"
#include "util/log.h"
#include "util/clock.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cJSON.h>

#if defined(__3DS__) && defined(WOLFRAM_3DS)

#include <3ds.h>
#include <wolfram/3ds.h>
#include <wolfram/actor_prefs_typed.h>
#include <wolfram/actor_typed.h>
#include <wolfram/agent.h>
#include <wolfram/attach.h>
#include <wolfram/feed_gen_typed.h>
#include <wolfram/list_typed.h>
#include <wolfram/moderation_typed.h>
#include <wolfram/oauth_pairing.h>
#include <wolfram/post_display.h>
#include <wolfram/post_view_typed.h>
#include <wolfram/profile_tab.h>
#include <wolfram/saved_feeds.h>
#include <wolfram/thread_typed.h>
#include <wolfram/threadgate_postgate.h>
#include <wolfram/time.h>

typedef enum {
    JOB_NONE = 0,
    JOB_LOGIN,
    JOB_OAUTH,
    JOB_RESUME,
    JOB_LOGOUT,
    JOB_TIMELINE,
    JOB_POST_ACTION,
    JOB_THREAD,
    JOB_PROFILE,
    JOB_NOTIFICATIONS,
    JOB_PUBLISH,
    JOB_SEARCH,
    JOB_FOLLOW,
    JOB_GRAPH,
    JOB_PEOPLE,
    JOB_POST_SEARCH,
    JOB_AUTHOR_FEED,
    JOB_LISTS,
    JOB_LIST_MEMBERS,
    JOB_FEEDS,
    JOB_FEED,
    JOB_MUTES,
    JOB_BLOCKS,
} indigo_job_kind;

typedef struct {
    indigo_job_kind kind;
    char service[256];
    char identifier[256];
    char password[128];
    char cursor[INDIGO_CURSOR_MAX];
    indigo_post_action action;
    char post_uri[INDIGO_POST_URI_MAX];
    char post_cid[INDIGO_POST_CID_MAX];
    char undo_uri[INDIGO_POST_URI_MAX];
    indigo_compose_mode mode;
    /* Reply controls for a new top-level post; matches indigo_reply_gate.
     * Ignored for a reply or a quote. */
    int reply_gate;
    /* The image to attach, by path, and its alt text; empty for none. */
    char image_path[INDIGO_IMAGE_PATH_MAX];
    char image_alt[INDIGO_IMAGE_ALT_MAX];
    char text[INDIGO_DRAFT_MAX];
    char root_uri[INDIGO_POST_URI_MAX];
    char root_cid[INDIGO_POST_CID_MAX];
    char query[INDIGO_SEARCH_QUERY_MAX];
    indigo_follow_action follow;
    indigo_graph_action graph;
    indigo_search_kind people_kind;
    char actor[INDIGO_PROFILE_DID_MAX];
    /* JOB_AUTHOR_FEED's wf_profile_tab. */
    int tab;
    char list_uri[INDIGO_POST_URI_MAX];
    /* JOB_FEED's target; the same shape as list_uri, kept separate so the two
     * jobs stay readable. */
    char feed_uri[INDIGO_POST_URI_MAX];
    /* True when this job asks for the next page of the last search rather
     * than a fresh one, so the worker appends instead of resetting. */
    bool paging;
} indigo_job;

typedef struct {
    char path[256];
    Thread thread;
    LightLock lock;
    LightSemaphore wake;
    volatile bool quit;
    bool started;

/* Guarded by s_lock. */
    indigo_job job;
    bool busy;
    indigo_session_event event;
    bool event_ready;

/* Worker thread only. */
    wf_agent *agent;
    indigo_saved_session saved;
    bool node_session;
    char pair_url[WF_OAUTH_PAIR_URL_MAX];
    char pair_code[WF_OAUTH_PAIR_CODE_MAX];
/* Worker-only: the account's muted words and hide-reposts, fetched once per
 * sign-in and applied to every timeline and feed page. */
    indigo_prefs prefs;
    bool prefs_loaded;

/* Written by the worker before it publishes TIMELINE_PAGE; the main thread
 * reads it after polling that event and before the next submit. */
    indigo_post page[INDIGO_THREAD_MAX];
    unsigned page_count;
    indigo_profile profile;
    indigo_notification notes[INDIGO_NOTIFICATION_MAX];
    unsigned note_count;
    indigo_actor actors[INDIGO_SEARCH_MAX];
    unsigned actor_count;
/* Post search keeps its own results: indigo_post is a different type from
 * indigo_actor, and the screen shows one list at a time but still needs both
 * to be valid until the next result arrives. */
    indigo_post posts[INDIGO_SEARCH_MAX];
    unsigned post_count;
/* Curated lists keep their own array for the same reason the posts do: a
 * list is a third type, and the screen needs the previous list to stay valid
 * until the next result arrives. */
    indigo_list lists[INDIGO_SEARCH_MAX];
/* The account's saved feeds: list-shaped rows, so the same type, but their
 * own array so browsing feeds never disturbs a curated-lists view. */
    indigo_list feeds[INDIGO_SEARCH_MAX];
    unsigned list_count;
    unsigned feed_count;
} indigo_session_state;

extern indigo_session_state g_session;

/* Publish WF_FAIL_NOT_READY and return when no agent is live. Every job
 * function that needs the agent must be guarded against a race with logout
 * or a failed resume; this macro is the canonical form. `_ev` is an already-
 * initialised indigo_session_event whose kind is the caller's failure kind. */
#define INDIGO_SESSION_AGENT_GUARD(_ev) \
    do { \
        if (!g_session.agent) { \
            (_ev).failure = WF_FAIL_NOT_READY; \
            indigo_session_publish_event(&(_ev)); \
            return; \
        } \
    } while (0)

/* session.c */
void indigo_session_publish(indigo_session_event_kind kind, wf_failure_kind failure, const char *account);
void indigo_session_publish_event(const indigo_session_event *ev);
void indigo_session_publish_failure(indigo_session_event *ev, const char *what, wf_status st);
bool indigo_session_enqueue(const indigo_job *j);

/* session_auth.c */
void indigo_session_drop_agent(void);
void indigo_session_do_login(const indigo_job *j);
void indigo_session_do_logout(void);
void indigo_session_do_oauth(const indigo_job *j);
void indigo_session_do_resume(void);

/* session_view.c */
bool indigo_session_fill_actor(const wf_agent_profile_view *a, indigo_actor *o);
unsigned indigo_session_count_of(int v);
void indigo_session_add_replies(const wf_agent_thread_node *n, unsigned depth);
bool indigo_session_fill_post(const wf_agent_post_view *pv, indigo_post *out);
indigo_note_kind indigo_session_note_kind(const char *reason);
bool indigo_session_thread_post_to_post(const wf_agent_thread_post *tp, unsigned depth, indigo_post *out);
bool indigo_session_to_post(const wf_agent_feed_item *item, indigo_post *out);

/* session_read.c */
void indigo_session_do_author_feed(const indigo_job *j);
void indigo_session_do_feed(const indigo_job *j);
void indigo_session_do_notifications(void);
void indigo_session_do_post_action(const indigo_job *j);
void indigo_session_do_post_search(const indigo_job *j);
void indigo_session_do_thread(const indigo_job *j);
void indigo_session_do_timeline(const indigo_job *j);

/* session_people.c */
void indigo_session_do_follow(const indigo_job *j);
void indigo_session_do_graph(const indigo_job *j);
void indigo_session_do_people(const indigo_job *j);
void indigo_session_do_profile(const indigo_job *j);
void indigo_session_do_search(const indigo_job *j);
void indigo_session_publish_actor_page(const indigo_job *j, wf_agent_actor_list *list, indigo_session_event *ev);

/* session_lists.c */
void indigo_session_do_feeds(const indigo_job *j);
void indigo_session_do_list_members(const indigo_job *j);
void indigo_session_do_lists(const indigo_job *j);
void indigo_session_do_moderation_list(const indigo_job *j);

/* session_write.c */
void indigo_session_do_publish(const indigo_job *j);

#endif /* 3DS with Wolfram */

#endif
