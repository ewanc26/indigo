/* Reading timelines, threads, custom feeds, notifications and post search, and liking and reposting. */

#include "atproto/session_internal.h"

#if defined(__3DS__) && defined(WOLFRAM_3DS)

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
