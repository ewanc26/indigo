/* The session without the network: no 3DS or no Wolfram, so nothing can reach
 * a server and every request is refused with "not ready". See session.c for the
 * real one. */

#include "atproto/session.h"

#include <string.h>

#if !(defined(__3DS__) && defined(WOLFRAM_3DS))
static bool s_pending;
static indigo_session_event s_stub;

bool
indigo_session_start(const char *session_path)
{
    (void) session_path;
    return true;
}

void
indigo_session_stop(void)
{
}

static bool
refuse(indigo_session_event_kind kind)
{
    s_stub = (indigo_session_event) {.kind = kind, .failure = WF_FAIL_NOT_READY};
    s_pending = true;
    return true;
}

bool
indigo_session_submit_login(const char *service, const char *identifier,
                            const char *password)
{
    (void) service;
    (void) identifier;
    (void) password;
    return refuse(INDIGO_SESSION_EVENT_SIGN_IN_FAILED);
}

bool
indigo_session_submit_oauth(const char *oauth_node, const char *handle)
{
    (void) oauth_node;
    (void) handle;
    return refuse(INDIGO_SESSION_EVENT_SIGN_IN_FAILED);
}

const char *
indigo_session_pair_url(void)
{
    return "";
}

const char *
indigo_session_pair_code(void)
{
    return "";
}


bool
indigo_session_submit_resume(void)
{
    return false;
}

bool
indigo_session_submit_logout(void)
{
    s_stub = (indigo_session_event) {.kind = INDIGO_SESSION_EVENT_SIGNED_OUT};
    s_pending = true;
    return true;
}

bool
indigo_session_poll(indigo_session_event *out)
{
    if (!s_pending) {
        return false;
    }
    *out = s_stub;
    s_pending = false;
    return true;
}

bool
indigo_session_submit_timeline(const char *cursor)
{
    (void) cursor;
    return false;
}

bool
indigo_session_submit_post_action(indigo_post_action action, const char *post_uri,
                                  const char *post_cid, const char *undo_uri)
{
    (void) action;
    (void) post_uri;
    (void) post_cid;
    (void) undo_uri;
    return false;
}

bool
indigo_session_submit_delete_post(const char *post_uri)
{
    (void) post_uri;
    return false;
}

bool
indigo_session_submit_thread(const char *uri)
{
    (void) uri;
    return false;
}

bool
indigo_session_submit_profile(const char *actor)
{
    (void) actor;
    return false;
}

bool
indigo_session_submit_notifications(void)
{
    return false;
}

bool
indigo_session_submit_publish_thread(const char *const *texts, unsigned count, int reply_gate)
{
    (void) texts;
    (void) count;
    (void) reply_gate;
    return false;
}

bool
indigo_session_submit_publish(indigo_compose_mode mode, const char *text,
                              const char *target_uri, const char *target_cid,
                              const char *root_uri, const char *root_cid, int reply_gate,
                              const char *image_path, const char *image_alt)
{
    (void) image_path;
    (void) image_alt;
    (void) mode;
    (void) text;
    (void) target_uri;
    (void) target_cid;
    (void) root_uri;
    (void) root_cid;
    (void) reply_gate;
    return false;
}

const indigo_profile *
indigo_session_profile(void)
{
    return NULL;
}

const indigo_notification *
indigo_session_notifications(unsigned *count)
{
    *count = 0;
    return NULL;
}

bool
indigo_session_submit_search(const char *query, bool paging)
{
    (void) query;
    (void) paging;
    return false;
}

bool
indigo_session_submit_follow(indigo_follow_action action, const char *did,
                             const char *follow_uri)
{
    (void) action;
    (void) did;
    (void) follow_uri;
    return false;
}

bool
indigo_session_submit_graph(indigo_graph_action action, const char *did,
                            const char *block_uri)
{
    (void) action;
    (void) did;
    (void) block_uri;
    return false;
}

bool
indigo_session_submit_people(indigo_search_kind kind, const char *subject, bool paging)
{
    (void) kind;
    (void) subject;
    (void) paging;
    return false;
}

bool
indigo_session_submit_post_search(const char *query, bool paging)
{
    (void) query;
    (void) paging;
    return false;
}

bool indigo_session_submit_author_feed(const char *actor, int tab,
                                       bool paging) {
  (void)actor;
  (void)tab;
  (void)paging;
  return false;
}

bool
indigo_session_submit_lists(bool paging)
{
    (void) paging;
    return false;
}

bool
indigo_session_submit_list_members(const char *list_uri, bool paging)
{
    (void) list_uri;
    (void) paging;
    return false;
}

bool
indigo_session_submit_feeds(void)
{
    return false;
}

bool
indigo_session_submit_mutes(bool paging)
{
    (void) paging;
    return false;
}

bool
indigo_session_submit_blocks(bool paging)
{
    (void) paging;
    return false;
}

bool
indigo_session_submit_feed(const char *feed_uri, const char *cursor)
{
    (void) feed_uri;
    (void) cursor;
    return false;
}

void
indigo_session_post_search_results(const indigo_post **posts, unsigned *count)
{
    (void) posts;
    (void) count;
}

void
indigo_session_feeds_results(const indigo_list **feeds, unsigned *count)
{
    (void) feeds;
    (void) count;
}

const indigo_actor *
indigo_session_search_results(unsigned *count)
{
    *count = 0;
    return NULL;
}

void
indigo_session_lists_results(const indigo_list **lists, unsigned *count)
{
    (void) lists;
    (void) count;
}

bool
indigo_session_busy(void)
{
    return s_pending;
}

const indigo_post *
indigo_session_page(unsigned *count)
{
    *count = 0;
    return NULL;
}

bool
indigo_session_has_saved(void)
{
    return false;
}

#endif /* no 3DS or no Wolfram */
