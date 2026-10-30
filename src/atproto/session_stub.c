/* The session without the network: no 3DS or no Wolfram, so nothing can reach
 * a server and every request is refused with "not ready". See session.c for the
 * real one. */

#include "atproto/session.h"

#include <string.h>

#if !defined(INDIGO_SESSION_HOST) && !(defined(__3DS__) && defined(WOLFRAM_3DS))
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
indigo_session_submit_notifications_more(const char *cursor)
{
    (void) cursor;
    return false;
}

bool
indigo_session_submit_chat_inbox(void)
{
    return false;
}

const indigo_chat_inbox *
indigo_session_chat_inbox(void)
{
    static const indigo_chat_inbox empty;

    return &empty;
}

bool
indigo_session_submit_chat_requests(void)
{
    return false;
}

bool
indigo_session_submit_chat_requests_more(const char *cursor)
{
    (void) cursor;
    return false;
}

const indigo_chat_inbox *
indigo_session_chat_requests(void)
{
    static const indigo_chat_inbox empty;

    return &empty;
}

bool
indigo_session_submit_chat_messages(const char *convo_id)
{
    (void) convo_id;
    return false;
}

const indigo_chat_thread *
indigo_session_chat_thread(void)
{
    static const indigo_chat_thread empty;

    return &empty;
}

bool
indigo_session_submit_chat_send(const char *convo_id, const char *text)
{
    (void) convo_id;
    (void) text;
    return false;
}

bool
indigo_session_submit_chat_accept(const char *convo_id)
{
    (void) convo_id;
    return false;
}

bool
indigo_session_submit_chat_decline(const char *convo_id)
{
    (void) convo_id;
    return false;
}

bool
indigo_session_submit_chat_start(const char *handle_or_did)
{
    (void) handle_or_did;
    return false;
}

const char *
indigo_session_chat_started_convo(void)
{
    return "";
}

bool
indigo_session_submit_chat_join_requests(const char *convo_id, bool paging)
{
    (void) convo_id;
    (void) paging;
    return false;
}

const indigo_chat_join_requests *
indigo_session_chat_join_requests(void)
{
    static const indigo_chat_join_requests empty;

    return &empty;
}

bool
indigo_session_submit_chat_join_approve(const char *convo_id, const char *did)
{
    (void) convo_id;
    (void) did;
    return false;
}

bool
indigo_session_submit_chat_join_reject(const char *convo_id, const char *did)
{
    (void) convo_id;
    (void) did;
    return false;
}

bool
indigo_session_submit_chat_group_info(const char *convo_id)
{
    (void) convo_id;
    return false;
}

const indigo_chat_group *
indigo_session_chat_group(void)
{
    static const indigo_chat_group empty;

    return &empty;
}

bool
indigo_session_submit_chat_group_create(const char *name, const char *members)
{
    (void) name;
    (void) members;
    return false;
}

const char *
indigo_session_chat_group_started(void)
{
    return "";
}

bool
indigo_session_submit_chat_group_rename(const char *convo_id, const char *name)
{
    (void) convo_id;
    (void) name;
    return false;
}

bool
indigo_session_submit_chat_group_add(const char *convo_id, const char *members)
{
    (void) convo_id;
    (void) members;
    return false;
}

bool
indigo_session_submit_chat_group_remove(const char *convo_id, const char *did)
{
    (void) convo_id;
    (void) did;
    return false;
}

bool
indigo_session_submit_chat_join_link_create(const char *convo_id)
{
    (void) convo_id;
    return false;
}

bool
indigo_session_submit_chat_join_link_toggle(const char *convo_id, bool disable)
{
    (void) convo_id;
    (void) disable;
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
indigo_session_submit_search(const char *query, const char *cursor)
{
    (void) query;
    (void) cursor;
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
indigo_session_submit_people(indigo_search_kind kind, const char *subject,
                              const char *cursor)
{
    (void) kind;
    (void) subject;
    (void) cursor;
    return false;
}

bool
indigo_session_submit_post_search(const char *query, const char *cursor)
{
    (void) query;
    (void) cursor;
    return false;
}

bool
indigo_session_submit_author_feed(const char *actor, int tab, const char *cursor)
{
    (void) actor;
    (void) tab;
    (void) cursor;
    return false;
}

bool
indigo_session_submit_lists(const char *cursor)
{
    (void) cursor;
    return false;
}

bool
indigo_session_submit_list_members(const char *list_uri, const char *cursor)
{
    (void) list_uri;
    (void) cursor;
    return false;
}

bool
indigo_session_submit_feeds(void)
{
    return false;
}

bool
indigo_session_submit_mutes(const char *cursor)
{
    (void) cursor;
    return false;
}

bool
indigo_session_submit_blocks(const char *cursor)
{
    (void) cursor;
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

#endif /* no 3DS, no Wolfram, and not the host end-to-end build */
