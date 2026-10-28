/* Reading timelines, threads, custom feeds, notifications and post search, and liking and reposting. */

#include "atproto/session_internal.h"

#if defined(__3DS__) && defined(WOLFRAM_3DS)

/* A playlist for a short clip is a few kilobytes; a feature-length one with
 * ten-second segments is about 900 lines. The cap stops a hostile URL
 * streaming forever. */
#define PLAYLIST_MAX_BYTES (256u * 1024u)
/* A screen of posts is a few videos; past this a new playlist is not fetched,
 * so its poster simply shows no duration. */
#define PLAYLIST_FETCH_PER_PAGE 4

/* The duration of the playlist at `uri`, following a master playlist to one
 * variant and no further. False when the fetch or the parse fails. */
static bool
playlist_seconds(wf_xrpc_client *client, const char *uri, double *seconds)
{
    wf_response response = {0};
    bool found = false;

    if (wf_http_get_public(client, uri, PLAYLIST_MAX_BYTES, &response) == WF_OK &&
        response.body && response.body_len > 0) {
        if (indigo_video_duration_parse(response.body, response.body_len, seconds)) {
            found = true;
        } else {
            char variant[INDIGO_VIDEO_PLAYLIST_MAX];

            if (indigo_video_master_variant(response.body, response.body_len, uri,
                                            variant, sizeof variant)) {
                wf_response variant_response = {0};

                if (wf_http_get_public(client, variant, PLAYLIST_MAX_BYTES,
                                       &variant_response) == WF_OK &&
                    variant_response.body && variant_response.body_len > 0) {
                    found = indigo_video_duration_parse(variant_response.body,
                                                        variant_response.body_len,
                                                        seconds);
                }
                wf_response_free(&variant_response);
            }
        }
    }
    wf_response_free(&response);
    return found;
}

/* Fill the duration text on the video posts of a page before it is published.
 * The fetch runs on the session worker, never on the frame loop; at most
 * PLAYLIST_FETCH_PER_PAGE playlists are tried, so a page of videos cannot turn
 * into a burst of requests. */
static void
fill_video_durations(indigo_post *posts, unsigned count)
{
    wf_xrpc_client *client;
    unsigned fetched = 0;

    if (!g_session.agent || !posts) {
        return;
    }
    client = wf_agent_get_xrpc_client(g_session.agent);
    if (!client) {
        return;
    }
    for (unsigned i = 0; i < count && fetched < PLAYLIST_FETCH_PER_PAGE; i++) {
        double seconds = 0.0;

        if (posts[i].embed_kind != INDIGO_EMBED_VIDEO ||
            !posts[i].embed_playlist[0] || posts[i].embed_duration[0]) {
            continue;
        }
        fetched++;
        if (playlist_seconds(client, posts[i].embed_playlist, &seconds)) {
            indigo_video_duration_format(seconds, posts[i].embed_duration,
                                         sizeof posts[i].embed_duration);
        }
    }
}

/* Fetch the saved preferences once per sign-in. Failure is not fatal: the
 * feed is simply shown unfiltered, and the next page tries again. */
static void
ensure_prefs(void)
{
    wf_actor_preferences p;
    wf_status st;

    if (g_session.prefs_loaded || !g_session.agent) {
        return;
    }
    memset(&p, 0, sizeof p);
    st = wf_agent_get_actor_prefs_typed(g_session.agent, &p);
    if (st != WF_OK) {
        indigo_log_warn("getPreferences failed (%d); feed unfiltered", (int) st);
        indigo_prefs_clear(&g_session.prefs);
        return;
    }
    indigo_prefs_from_wolfram(&g_session.prefs, &p, indigo_time_now());
    wf_actor_preferences_free(&p);
    g_session.prefs_loaded = true;
}

void
indigo_session_do_timeline(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_TIMELINE_FAILED};
    wf_agent_feed_list list;
    wf_status st;

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    ensure_prefs();
    memset(&list, 0, sizeof list);
    st = wf_agent_get_timeline_typed(g_session.agent, INDIGO_PAGE_SIZE,
                                     j->cursor[0] ? j->cursor : NULL, &list);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "timeline", st);
        return;
    }

    g_session.page_count = 0;
    for (size_t i = 0; i < list.item_count && g_session.page_count < INDIGO_PAGE_SIZE; i++) {
        if (indigo_session_to_post(&list.items[i], &g_session.page[g_session.page_count])) {
            g_session.page_count++;
        }
    }
    g_session.page_count -= indigo_prefs_filter_page(&g_session.prefs, g_session.page, g_session.page_count, 0, true);
    fill_video_durations(g_session.page, g_session.page_count);
    ev.kind = INDIGO_SESSION_EVENT_TIMELINE_PAGE;
    ev.page_count = g_session.page_count;
    if (list.cursor && strlen(list.cursor) < sizeof ev.cursor) {
        snprintf(ev.cursor, sizeof ev.cursor, "%s", list.cursor);
    } else if (list.cursor) {
        indigo_log_warn("timeline cursor too long; paging stops here");
    }
    wf_agent_feed_list_free(&list);
    indigo_log_info("timeline: %u posts", g_session.page_count);
    indigo_session_publish_event(&ev);
}

void
indigo_session_do_post_action(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_POST_ACTION_FAILED,
                               .action = j->action};
    wf_agent_post_result res = {0};
    wf_status st = WF_ERR_INVALID_ARG;

    snprintf(ev.post_uri, sizeof ev.post_uri, "%s", j->post_uri);
    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    switch (j->action) {
    case INDIGO_POST_ACTION_LIKE:
        st = wf_agent_like(g_session.agent, j->post_uri, j->post_cid, &res);
        break;
    case INDIGO_POST_ACTION_UNLIKE:
        st = wf_agent_unlike(g_session.agent, j->undo_uri);
        break;
    case INDIGO_POST_ACTION_REPOST:
        st = wf_agent_repost(g_session.agent, j->post_uri, j->post_cid, &res);
        break;
    case INDIGO_POST_ACTION_UNREPOST:
        st = wf_agent_delete_repost(g_session.agent, j->undo_uri);
        break;
    case INDIGO_POST_ACTION_NONE:
        break;
    }
    if (st == WF_OK) {
        ev.kind = INDIGO_SESSION_EVENT_POST_ACTION_DONE;
        if (res.uri && strlen(res.uri) < sizeof ev.record_uri) {
            snprintf(ev.record_uri, sizeof ev.record_uri, "%s", res.uri);
        }
    } else {
        ev.failure = wf_failure_classify(st, 0, NULL);
        indigo_log_warn("post action %d failed: wolfram status %d (%s)", (int) j->action,
                        (int) st, wf_failure_tag(ev.failure));
    }
    wf_agent_post_result_free(&res);
    indigo_session_publish_event(&ev);
}

void
indigo_session_do_thread(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_THREAD_FAILED};
    wf_agent_thread thread;
    const wf_agent_thread_node *chain[INDIGO_THREAD_MAX];
    unsigned parents = 0;
    wf_status st;

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    memset(&thread, 0, sizeof thread);
    st = wf_agent_get_post_thread_typed(g_session.agent, j->post_uri, 6, &thread);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "thread", st);
        return;
    }
    if (thread.root.kind != WF_AGENT_THREAD_KIND_POST) {
        wf_agent_thread_free(&thread);
        ev.failure = WF_FAIL_BAD_RESPONSE;
        indigo_session_publish_event(&ev);
        return;
    }

    /* Oldest ancestor first. Ancestors that are blocked or gone end the chain. */
    for (const wf_agent_thread_node *p = thread.root.parent;
         p && p->kind == WF_AGENT_THREAD_KIND_POST && parents < 8; p = p->parent) {
        chain[parents++] = p;
    }
    g_session.page_count = 0;
    for (unsigned i = parents; i > 0; i--) {
        if (indigo_session_thread_post_to_post(&chain[i - 1]->post, parents - i, &g_session.page[g_session.page_count])) {
            g_session.page_count++;
        }
    }
    ev.focus = g_session.page_count;
    if (indigo_session_thread_post_to_post(&thread.root.post, parents, &g_session.page[g_session.page_count])) {
        g_session.page_count++;
    }
    indigo_session_add_replies(&thread.root, parents + 1);
    wf_agent_thread_free(&thread);
    fill_video_durations(g_session.page, g_session.page_count);

    ev.kind = INDIGO_SESSION_EVENT_THREAD_PAGE;
    ev.page_count = g_session.page_count;
    indigo_log_info("thread: %u posts", g_session.page_count);
    indigo_session_publish_event(&ev);
}

void
indigo_session_do_post_search(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_agent_post_list list;
    char *next = NULL;
    wf_status st;
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    memset(&list, 0, sizeof list);
    st = wf_agent_search_posts_typed(g_session.agent, j->query, INDIGO_SEARCH_PAGE,
                                      cursor, &list, &next);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "post search", st);
        return;
    }

    if (!j->paging) {
        g_session.post_count = 0;
    }
    for (size_t i = 0; i < list.post_count && g_session.post_count < INDIGO_SEARCH_MAX; i++) {
        if (indigo_session_fill_post(&list.posts[i], &g_session.posts[g_session.post_count])) {
            g_session.post_count++;
        }
    }
    fill_video_durations(g_session.posts, g_session.post_count);
    ev.kind = INDIGO_SESSION_EVENT_POST_SEARCH_PAGE;
    ev.page_count = g_session.post_count;
    indigo_copy_utf8(ev.cursor, sizeof ev.cursor, next ? next : "");
    free(next);
    wf_agent_post_list_free(&list);
    indigo_log_info("post search '%s': %u", j->query, g_session.post_count);
    indigo_session_publish_event(&ev);
}

/* One person's posts. Same result type and same event as post search, so the
 * results land in the same array; only the request differs. to_post is reused
 * so reposts carry their "Reposted by" line here too. */
void
indigo_session_do_author_feed(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_agent_feed_list list;
    wf_status st;
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    memset(&list, 0, sizeof list);
    st = wf_agent_get_profile_tab_typed(g_session.agent, j->actor, j->tab,
                                        INDIGO_SEARCH_PAGE, cursor, &list);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "author feed", st);
        return;
    }

    if (!j->paging) {
        g_session.post_count = 0;
    }
    for (size_t i = 0; i < list.item_count && g_session.post_count < INDIGO_SEARCH_MAX; i++) {
        if (indigo_session_to_post(&list.items[i], &g_session.posts[g_session.post_count])) {
            g_session.post_count++;
        }
    }
    fill_video_durations(g_session.posts, g_session.post_count);
    ev.kind = INDIGO_SESSION_EVENT_POST_SEARCH_PAGE;
    ev.page_count = g_session.post_count;
    indigo_copy_utf8(ev.cursor, sizeof ev.cursor, list.cursor ? list.cursor : "");
    wf_agent_feed_list_free(&list);
    indigo_log_info("author feed '%s': %u", j->actor, g_session.post_count);
    indigo_session_publish_event(&ev);
}

/* One feed's posts. getFeed returns the same feedViewPost items the timeline
 * does, so the conversion is shared with do_timeline. */
void
indigo_session_do_feed(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_TIMELINE_FAILED};
    wf_agent_feed_list list;
    wf_status st;

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    ensure_prefs();
    memset(&list, 0, sizeof list);
    st = wf_agent_get_feed_typed(g_session.agent, j->feed_uri, INDIGO_PAGE_SIZE,
                                 j->cursor[0] ? j->cursor : NULL, &list);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "feed", st);
        return;
    }

    g_session.page_count = 0;
    for (size_t i = 0; i < list.item_count && g_session.page_count < INDIGO_PAGE_SIZE; i++) {
        if (indigo_session_to_post(&list.items[i], &g_session.page[g_session.page_count])) {
            g_session.page_count++;
        }
    }
    /* A custom feed is not the home timeline, so hide_reposts does not apply;
     * the muted words do, because they are about the content, not the feed. */
    g_session.page_count -= indigo_prefs_filter_page(&g_session.prefs, g_session.page, g_session.page_count, 0, false);
    fill_video_durations(g_session.page, g_session.page_count);
    ev.kind = INDIGO_SESSION_EVENT_TIMELINE_PAGE;
    ev.page_count = g_session.page_count;
    if (list.cursor && strlen(list.cursor) < sizeof ev.cursor) {
        snprintf(ev.cursor, sizeof ev.cursor, "%s", list.cursor);
    } else if (list.cursor) {
        indigo_log_warn("feed cursor too long; paging stops here");
    }
    wf_agent_feed_list_free(&list);
    indigo_log_info("feed '%s': %u posts", j->feed_uri, g_session.page_count);
    indigo_session_publish_event(&ev);
}

/* The inbox and the request inbox share one body. They differ only in the list
 * call and in where the rows go; requests use listConvoRequests, which the
 * lexicon prefers over listConvos with a status filter. */
static void
run_chat_list(bool requests, const char *cursor)
{
    indigo_session_event ev = {.kind = requests ? INDIGO_SESSION_EVENT_CHAT_REQUESTS_FAILED
                                                : INDIGO_SESSION_EVENT_CHAT_FAILED};
    indigo_chat_inbox *dst = requests ? &g_session.requests : &g_session.chat;
    /* A cursor means the next page of requests: those rows are appended, not
     * replacing the ones already shown. */
    const bool append = requests && cursor && cursor[0] != '\0';
    wf_chat_convo_list list;
    wf_status st;

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    memset(&list, 0, sizeof list);
    if (requests) {
        st = wf_agent_chat_list_convo_requests_typed(g_session.agent, INDIGO_CHAT_CONVOS_MAX,
                                                     append ? cursor : NULL, &list);
    } else {
        st = wf_agent_chat_list_convos(g_session.agent, INDIGO_CHAT_CONVOS_MAX, NULL, &list);
    }
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, requests ? "message requests" : "messages", st);
        return;
    }

    if (!append) {
        indigo_chat_inbox_clear(dst);
    }
    indigo_chat_inbox_append_from_wolfram(dst, &list, wf_agent_get_did(g_session.agent));
    wf_chat_convo_list_free(&list);

    ev.kind = requests ? INDIGO_SESSION_EVENT_CHAT_REQUESTS_PAGE : INDIGO_SESSION_EVENT_CHAT_PAGE;
    ev.page_count = dst->count;
    indigo_session_publish_event(&ev);
}

void
indigo_session_do_chat_inbox(void)
{
    run_chat_list(false, NULL);
}

void
indigo_session_do_chat_requests(const char *cursor)
{
    run_chat_list(true, cursor);
}

void
indigo_session_do_chat_messages(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_CHAT_THREAD_FAILED};
    wf_chat_message_list list;
    wf_status st;

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    memset(&list, 0, sizeof list);
    st = wf_agent_chat_get_messages(g_session.agent, j->post_uri, INDIGO_CHAT_MESSAGES_MAX,
                                    NULL, &list);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "messages", st);
        return;
    }

    /* An id that does not fit is refused, not cut: a cut id would open another conversation. */
    if (strlen(j->post_uri) >= sizeof g_session.chat_thread.convo_id) {
        ev.failure = WF_FAIL_OTHER;
        wf_chat_message_list_free(&list);
        indigo_session_publish_event(&ev);
        return;
    }
    memcpy(g_session.chat_thread.convo_id, j->post_uri, strlen(j->post_uri) + 1);
    indigo_chat_thread_fill_from_wolfram(&g_session.chat_thread, &list,
                                         wf_agent_get_did(g_session.agent));
    wf_chat_message_list_free(&list);

    ev.kind = INDIGO_SESSION_EVENT_CHAT_THREAD_PAGE;
    ev.page_count = g_session.chat_thread.count;
    indigo_session_publish_event(&ev);
}

void
indigo_session_do_chat_send(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_CHAT_SEND_FAILED};
    wf_chat_message sent;
    wf_status st;

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    memset(&sent, 0, sizeof sent);
    st = wf_agent_chat_send_message(g_session.agent, j->post_uri, j->text, NULL, &sent);
    wf_chat_message_reset(&sent);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "send", st);
        return;
    }

    ev.kind = INDIGO_SESSION_EVENT_CHAT_SEND_DONE;
    indigo_session_publish_event(&ev);
}

/* Accept or decline one message request. Both come through here: the call
 * differs, and the list change is the same. */
static void
chat_request_decision(const indigo_job *j, bool accept)
{
    indigo_session_event ev = {.kind = accept ? INDIGO_SESSION_EVENT_CHAT_ACCEPT_FAILED
                                              : INDIGO_SESSION_EVENT_CHAT_DECLINE_FAILED};
    char id[INDIGO_CHAT_ID_MAX];
    wf_status st;

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    if (!indigo_chat_convo_id_copy(id, sizeof id, j->post_uri)) {
        ev.failure = WF_FAIL_OTHER;
        indigo_session_publish_event(&ev);
        return;
    }

    /* Accepting answers with a rev or nothing, and declining with the convo
     * ref. Neither body is needed, so the status-only calls are used: the
     * typed wrapper for accept parses a convoView, which the server does not
     * send. */
    st = accept ? wf_agent_chat_accept_convo(g_session.agent, id)
                : wf_agent_chat_leave_convo(g_session.agent, id);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, accept ? "accept request" : "decline request", st);
        return;
    }

    /* Accepted, the conversation joins the inbox, copied from the request list
     * so no fetch is needed. Only when the inbox is already loaded: an empty
     * one is fetched when Messages is opened, and one row would hide the rest. */
    const indigo_chat_convo *row = indigo_chat_inbox_find(&g_session.requests, id);
    if (row) {
        indigo_chat_convo moved = *row;

        indigo_chat_inbox_remove(&g_session.requests, id);
        if (accept && g_session.chat.count > 0) {
            indigo_chat_inbox_add_front(&g_session.chat, &moved);
        }
    }

    ev.kind = accept ? INDIGO_SESSION_EVENT_CHAT_ACCEPT_DONE
                     : INDIGO_SESSION_EVENT_CHAT_DECLINE_DONE;
    indigo_session_publish_event(&ev);
}

void
indigo_session_do_chat_accept(const indigo_job *j)
{
    chat_request_decision(j, true);
}

void
indigo_session_do_chat_decline(const indigo_job *j)
{
    chat_request_decision(j, false);
}

void
indigo_session_do_chat_start(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_CHAT_START_FAILED};
    char target[INDIGO_CHAT_TARGET_MAX];
    char did[INDIGO_CHAT_TARGET_MAX];
    indigo_chat_target_kind kind;

    g_session.chat_started[0] = '\0';
    if (!g_session.agent || !wf_agent_get_did(g_session.agent)) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }

    kind = indigo_chat_target_normalise(j->post_uri, target, sizeof target)
               ? indigo_chat_target_kind_of(target)
               : INDIGO_CHAT_TARGET_INVALID;
    if (kind == INDIGO_CHAT_TARGET_INVALID) {
        ev.failure = WF_FAIL_OTHER;
        indigo_copy_utf8(ev.message, sizeof ev.message, "Enter a handle or DID to message.");
        indigo_session_publish_event(&ev);
        return;
    }

    if (kind == INDIGO_CHAT_TARGET_HANDLE) {
        char *resolved = NULL;
        wf_status st = wf_agent_resolve_handle(g_session.agent, target, &resolved);
        const bool fits = resolved && strlen(resolved) < sizeof did;

        if (st == WF_OK && fits) {
            indigo_copy_utf8(did, sizeof did, resolved);
        }
        free(resolved);
        if (st != WF_OK || !fits) {
            ev.failure = WF_FAIL_OTHER;
            indigo_copy_utf8(ev.message, sizeof ev.message,
                             st == WF_OK || st == WF_ERR_HTTP
                                 ? "No account has that handle."
                                 : "Could not look up that handle.");
            indigo_session_publish_event(&ev);
            return;
        }
    } else {
        indigo_copy_utf8(did, sizeof did, target);
    }

    if (indigo_chat_is_self(wf_agent_get_did(g_session.agent), did)) {
        ev.failure = WF_FAIL_OTHER;
        indigo_copy_utf8(ev.message, sizeof ev.message, "That is your own account.");
        indigo_session_publish_event(&ev);
        return;
    }

    /* getConvoForMembers takes every member, the viewer included. */
    const char *members[2] = {wf_agent_get_did(g_session.agent), did};
    wf_chat_convo convo;
    wf_status st;

    memset(&convo, 0, sizeof convo);
    st = wf_agent_chat_get_convo_for_members_typed(g_session.agent, members, 2, &convo);
    if (st != WF_OK || !indigo_chat_convo_id_copy(g_session.chat_started,
                                                  sizeof g_session.chat_started, convo.id)) {
        wf_chat_convo_free(&convo);
        if (st != WF_OK) {
            indigo_session_publish_failure(&ev, "start conversation", st);
        } else {
            ev.failure = WF_FAIL_OTHER;
            indigo_copy_utf8(ev.message, sizeof ev.message,
                             "The server did not return a conversation.");
            indigo_session_publish_event(&ev);
        }
        return;
    }
    wf_chat_convo_free(&convo);

    ev.kind = INDIGO_SESSION_EVENT_CHAT_START_DONE;
    indigo_session_publish_event(&ev);
}

void
indigo_session_do_chat_join_requests(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_CHAT_JOIN_REQUESTS_FAILED};
    char id[INDIGO_CHAT_ID_MAX];
    char cursor_buf[INDIGO_CURSOR_MAX];
    const char *cursor = NULL;
    wf_chat_join_requests list;
    wf_status st;

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    if (!indigo_chat_convo_id_copy(id, sizeof id, j->post_uri)) {
        ev.failure = WF_FAIL_OTHER;
        indigo_session_publish_event(&ev);
        return;
    }
    if (j->paging) {
        if (!g_session.join_requests.cursor[0]) {
            ev.kind = INDIGO_SESSION_EVENT_CHAT_JOIN_REQUESTS_PAGE;
            ev.page_count = g_session.join_requests.count;
            indigo_session_publish_event(&ev);
            return;
        }
        indigo_copy_utf8(cursor_buf, sizeof cursor_buf, g_session.join_requests.cursor);
        cursor = cursor_buf;
    }

    memset(&list, 0, sizeof list);
    st = wf_agent_chat_list_join_requests_typed(g_session.agent, id,
                                                INDIGO_CHAT_JOIN_REQUESTS_MAX, cursor, &list);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "join requests", st);
        return;
    }

    if (!j->paging) {
        indigo_chat_join_requests_reset(&g_session.join_requests);
        indigo_copy_utf8(g_session.join_requests.convo_id,
                         sizeof g_session.join_requests.convo_id, id);
    }
    const int added = indigo_chat_join_requests_append_from_wolfram(&g_session.join_requests,
                                                                    &list);
    if (j->paging && added == 0) {
        g_session.join_requests.has_more = false;
        g_session.join_requests.cursor[0] = '\0';
    }
    wf_chat_join_requests_free(&list);

    ev.kind = INDIGO_SESSION_EVENT_CHAT_JOIN_REQUESTS_PAGE;
    ev.page_count = g_session.join_requests.count;
    indigo_session_publish_event(&ev);
}

/* Approve or reject one join request. Both come through here: the call differs,
 * and the list change is the same. */
static void
chat_join_decision(const indigo_job *j, bool approve)
{
    indigo_session_event ev = {.kind = approve ? INDIGO_SESSION_EVENT_CHAT_JOIN_APPROVE_FAILED
                                               : INDIGO_SESSION_EVENT_CHAT_JOIN_REJECT_FAILED};
    char id[INDIGO_CHAT_ID_MAX];
    wf_status st;

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    if (!indigo_chat_convo_id_copy(id, sizeof id, j->post_uri) ||
        indigo_chat_target_kind_of(j->member_did) != INDIGO_CHAT_TARGET_DID) {
        ev.failure = WF_FAIL_OTHER;
        indigo_session_publish_event(&ev);
        return;
    }

    if (approve) {
        wf_chat_convo convo;

        memset(&convo, 0, sizeof convo);
        st = wf_agent_chat_approve_join_request_typed(g_session.agent, id, j->member_did,
                                                      &convo);
        wf_chat_convo_free(&convo);
    } else {
        wf_chat_ok ok;

        memset(&ok, 0, sizeof ok);
        st = wf_agent_chat_reject_join_request_typed(g_session.agent, id, j->member_did, &ok);
        wf_chat_ok_free(&ok);
    }
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, approve ? "approve join request" : "reject join request",
                                       st);
        return;
    }

    /* Settled, so the row leaves the loaded list without a refetch. */
    indigo_chat_join_requests_remove(&g_session.join_requests, j->member_did);

    ev.kind = approve ? INDIGO_SESSION_EVENT_CHAT_JOIN_APPROVE_DONE
                      : INDIGO_SESSION_EVENT_CHAT_JOIN_REJECT_DONE;
    indigo_session_publish_event(&ev);
}

void
indigo_session_do_chat_join_approve(const indigo_job *j)
{
    chat_join_decision(j, true);
}

void
indigo_session_do_chat_join_reject(const indigo_job *j)
{
    chat_join_decision(j, false);
}

void
indigo_session_do_notifications(void)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_NOTIFICATIONS_FAILED};
    wf_agent_notification_list list;
    wf_status st;

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    memset(&list, 0, sizeof list);
    st = wf_agent_list_notifications_typed(g_session.agent, INDIGO_NOTIFICATION_MAX, NULL, &list);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "notifications", st);
        return;
    }

    g_session.note_count = 0;
    for (size_t i = 0; i < list.notification_count && g_session.note_count < INDIGO_NOTIFICATION_MAX;
         i++) {
        const wf_agent_notification *n = &list.notifications[i];
        indigo_notification *o = &g_session.notes[g_session.note_count];

        memset(o, 0, sizeof *o);
        o->kind = indigo_session_note_kind(n->reason);
        indigo_copy_utf8(o->handle, sizeof o->handle, n->author.handle);
        indigo_media_cdn_url(o->avatar, sizeof o->avatar, n->author.avatar, INDIGO_CDN_AVATAR);
        indigo_copy_utf8(o->name, sizeof o->name,
                         n->author.display_name && n->author.display_name[0]
                             ? n->author.display_name
                             : n->author.handle);
        o->unread = !n->is_read;
        if (o->kind == INDIGO_NOTE_LIKE || o->kind == INDIGO_NOTE_REPOST) {
            if (n->reason_subject && strlen(n->reason_subject) < sizeof o->target_uri) {
                snprintf(o->target_uri, sizeof o->target_uri, "%s", n->reason_subject);
            }
        } else if (o->kind != INDIGO_NOTE_FOLLOW && o->kind != INDIGO_NOTE_OTHER) {
            wf_agent_post_view pv;
            wf_post_display d;

            if (n->uri && strlen(n->uri) < sizeof o->target_uri) {
                snprintf(o->target_uri, sizeof o->target_uri, "%s", n->uri);
            }
            memset(&pv, 0, sizeof pv);
            pv.uri = n->uri;
            pv.cid = n->cid;
            pv.record = n->record;
            if (wf_agent_post_view_display(&pv, &d) == WF_OK) {
                indigo_copy_utf8(o->text, sizeof o->text, d.text);
                wf_post_display_free(&d);
            }
        }
        g_session.note_count++;
    }
    wf_agent_notification_list_free(&list);
    /* Mark everything up to now seen. This is always a top-of-list fetch --
     * notifications are never paged -- so unlike Cobalt there is no paging
     * case to exclude. A failure is logged, not surfaced: the notifications
     * arrived, and an error about a badge on another client is noise. */
    if (g_session.note_count > 0) {
        char seen_at[32];
        /* Nothing initialises a clock on the 3DS, so time() can return a
         * negative value. Sending "1970-01-01T00:00:00Z" as seenAt would be
         * worse than not marking anything, so the clock decides. */
        const long long now = indigo_time_now();

        if (now > 0 && wf_time_format_rfc3339((int64_t) now, seen_at, sizeof seen_at) == WF_OK) {
            if (wf_agent_update_seen_notifications(g_session.agent, seen_at) != WF_OK) {
                indigo_log_warn("updateSeen failed: the unread badge may linger on other "
                                "clients");
            }
        }
    }
    ev.kind = INDIGO_SESSION_EVENT_NOTIFICATIONS_PAGE;
    ev.page_count = g_session.note_count;
    indigo_log_info("notifications: %u", g_session.note_count);
    indigo_session_publish_event(&ev);
}

bool
indigo_session_submit_timeline(const char *cursor)
{
    indigo_job j = {.kind = JOB_TIMELINE};

    if (cursor) {
        snprintf(j.cursor, sizeof j.cursor, "%s", cursor);
    }
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_post_action(indigo_post_action action, const char *post_uri,
                                  const char *post_cid, const char *undo_uri)
{
    indigo_job j = {.kind = JOB_POST_ACTION, .action = action};

    snprintf(j.post_uri, sizeof j.post_uri, "%s", post_uri);
    snprintf(j.post_cid, sizeof j.post_cid, "%s", post_cid);
    snprintf(j.undo_uri, sizeof j.undo_uri, "%s", undo_uri);
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_thread(const char *uri)
{
    indigo_job j = {.kind = JOB_THREAD};

    snprintf(j.post_uri, sizeof j.post_uri, "%s", uri);
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_notifications(void)
{
    indigo_job j = {.kind = JOB_NOTIFICATIONS};

    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_chat_inbox(void)
{
    indigo_job j = {.kind = JOB_CHAT_INBOX};

    return indigo_session_enqueue(&j);
}

const indigo_chat_inbox *
indigo_session_chat_inbox(void)
{
    return &g_session.chat;
}

bool
indigo_session_submit_chat_requests(void)
{
    indigo_job j = {.kind = JOB_CHAT_REQUESTS};

    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_chat_requests_more(const char *cursor)
{
    indigo_job j = {.kind = JOB_CHAT_REQUESTS};

    if (!cursor || cursor[0] == '\0') {
        return false;
    }
    snprintf(j.cursor, sizeof j.cursor, "%s", cursor);
    return indigo_session_enqueue(&j);
}

const indigo_chat_inbox *
indigo_session_chat_requests(void)
{
    return &g_session.requests;
}

bool
indigo_session_submit_chat_messages(const char *convo_id)
{
    indigo_job j = {.kind = JOB_CHAT_MESSAGES};

    if (!convo_id || !convo_id[0]) {
        return false;
    }
    snprintf(j.post_uri, sizeof j.post_uri, "%s", convo_id);
    return indigo_session_enqueue(&j);
}

const indigo_chat_thread *
indigo_session_chat_thread(void)
{
    return &g_session.chat_thread;
}

bool
indigo_session_submit_chat_send(const char *convo_id, const char *text)
{
    indigo_job j = {.kind = JOB_CHAT_SEND};

    if (!convo_id || !convo_id[0] || !indigo_chat_reply_valid(text)) {
        return false;
    }
    snprintf(j.post_uri, sizeof j.post_uri, "%s", convo_id);
    snprintf(j.text, sizeof j.text, "%s", text);
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_chat_accept(const char *convo_id)
{
    indigo_job j = {.kind = JOB_CHAT_ACCEPT};

    if (!indigo_chat_convo_id_copy(j.post_uri, sizeof j.post_uri, convo_id)) {
        return false;
    }
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_chat_decline(const char *convo_id)
{
    indigo_job j = {.kind = JOB_CHAT_DECLINE};

    if (!indigo_chat_convo_id_copy(j.post_uri, sizeof j.post_uri, convo_id)) {
        return false;
    }
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_chat_start(const char *handle_or_did)
{
    indigo_job j = {.kind = JOB_CHAT_START};
    char target[INDIGO_CHAT_TARGET_MAX];

    if (!indigo_chat_target_normalise(handle_or_did, target, sizeof target) ||
        indigo_chat_target_kind_of(target) == INDIGO_CHAT_TARGET_INVALID) {
        return false;
    }
    indigo_copy_utf8(j.post_uri, sizeof j.post_uri, target);
    return indigo_session_enqueue(&j);
}

const char *
indigo_session_chat_started_convo(void)
{
    return g_session.chat_started;
}

bool
indigo_session_submit_chat_join_requests(const char *convo_id, bool paging)
{
    indigo_job j = {.kind = JOB_CHAT_JOIN_REQUESTS};

    if (!indigo_chat_convo_id_copy(j.post_uri, sizeof j.post_uri, convo_id)) {
        return false;
    }
    j.paging = paging;
    return indigo_session_enqueue(&j);
}

const indigo_chat_join_requests *
indigo_session_chat_join_requests(void)
{
    return &g_session.join_requests;
}

/* Both decisions name the group and the requester, so they share the guard. */
static bool
submit_join_decision(const char *convo_id, const char *did, bool approve)
{
    indigo_job j = {.kind = approve ? JOB_CHAT_JOIN_APPROVE : JOB_CHAT_JOIN_REJECT};

    if (!indigo_chat_convo_id_copy(j.post_uri, sizeof j.post_uri, convo_id) ||
        indigo_chat_target_kind_of(did) != INDIGO_CHAT_TARGET_DID) {
        return false;
    }
    indigo_copy_utf8(j.member_did, sizeof j.member_did, did);
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_chat_join_approve(const char *convo_id, const char *did)
{
    return submit_join_decision(convo_id, did, true);
}

bool
indigo_session_submit_chat_join_reject(const char *convo_id, const char *did)
{
    return submit_join_decision(convo_id, did, false);
}

bool
indigo_session_submit_post_search(const char *query, bool paging)
{
    indigo_job j = {.kind = JOB_POST_SEARCH};

    if (!query) {
        return false;
    }
    j.paging = paging;
    indigo_copy_utf8(j.query, sizeof j.query, query);
    return indigo_session_enqueue(&j);
}

bool indigo_session_submit_author_feed(const char *actor, int tab,
                                       bool paging) {
  indigo_job j = {.kind = JOB_AUTHOR_FEED};

  if (!actor || !actor[0]) {
    return false;
  }
  j.paging = paging;
  j.tab = tab;
  indigo_copy_utf8(j.actor, sizeof j.actor, actor);
  return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_feed(const char *feed_uri, const char *cursor)
{
    indigo_job j = {.kind = JOB_FEED};

    if (!feed_uri || !feed_uri[0]) {
        return false;
    }
    indigo_copy_utf8(j.feed_uri, sizeof j.feed_uri, feed_uri);
    if (cursor) {
        snprintf(j.cursor, sizeof j.cursor, "%s", cursor);
    }
    return indigo_session_enqueue(&j);
}

void
indigo_session_post_search_results(const indigo_post **posts, unsigned *count)
{
    if (posts) {
        *posts = g_session.posts;
    }
    if (count) {
        *count = g_session.post_count;
    }
}

const indigo_notification *
indigo_session_notifications(unsigned *count)
{
    *count = g_session.note_count;
    return g_session.notes;
}

#endif /* 3DS with Wolfram */
