/* Curated lists, saved feeds, and the muted and blocked account lists. */

#include "atproto/session_internal.h"

#if defined(__3DS__) && defined(WOLFRAM_3DS)

/* The account's curated lists. getLists is paged; the cursor is dropped like
 * actor and post search, because a 3DS list that cannot show page two is not
 * worth paging. */
void
indigo_session_do_lists(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_agent_list_view_list list;
    wf_status st;
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;

    INDIGO_SESSION_AGENT_GUARD(ev);
    /* The signed-in account's own lists: getLists needs an actor, and the
     * agent knows its own handle. */
    {
        const char *who = wf_agent_get_handle(g_session.agent);

        if (!who || !who[0]) {
            ev.failure = WF_FAIL_NOT_READY;
            indigo_session_publish_event(&ev);
            return;
        }
        memset(&list, 0, sizeof list);
        st = wf_agent_get_lists_typed(g_session.agent, who, INDIGO_SEARCH_PAGE, cursor, &list);
    }
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "lists", st);
        return;
    }

    if (!j->paging) {
        g_session.list_count = 0;
    }
    for (size_t i = 0; i < list.list_count && g_session.list_count < INDIGO_SEARCH_MAX; i++) {
        const wf_agent_list_view *l = &list.lists[i];
        indigo_list *o = &g_session.lists[g_session.list_count];

        if (!l->uri || !l->uri[0] || !l->name || !l->name[0]) {
            continue;
        }
        memset(o, 0, sizeof *o);
        indigo_copy_utf8(o->uri, sizeof o->uri, l->uri);
        indigo_copy_utf8(o->name, sizeof o->name, l->name);
        indigo_copy_utf8(o->description, sizeof o->description,
                         l->description ? l->description : "");
        g_session.list_count++;
    }
    ev.kind = INDIGO_SESSION_EVENT_LISTS_PAGE;
    ev.page_count = g_session.list_count;
    indigo_copy_utf8(ev.cursor, sizeof ev.cursor, list.cursor ? list.cursor : "");
    wf_agent_list_view_list_free(&list);
    indigo_log_info("lists: %u", g_session.list_count);
    indigo_session_publish_event(&ev);
}

/* One list's members. getList's items are listItemViews whose subjects are
 * profile views, so they land in the actor array through the same conversion
 * the followers list uses. */
void
indigo_session_do_list_members(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_agent_list_item_list list;
    wf_status st;
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;

    INDIGO_SESSION_AGENT_GUARD(ev);
    memset(&list, 0, sizeof list);
    st = wf_agent_get_list_typed(g_session.agent, j->list_uri, INDIGO_SEARCH_PAGE, cursor, &list);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "list members", st);
        return;
    }

    if (!j->paging) {
        g_session.actor_count = 0;
    }
    for (size_t i = 0; i < list.item_count && g_session.actor_count < INDIGO_SEARCH_MAX; i++) {
        const wf_agent_profile_view *a = &list.items[i].subject;

        if (indigo_session_fill_actor(a, &g_session.actors[g_session.actor_count])) {
            g_session.actor_count++;
        }
    }
    ev.kind = INDIGO_SESSION_EVENT_SEARCH_PAGE;
    ev.page_count = g_session.actor_count;
    indigo_copy_utf8(ev.cursor, sizeof ev.cursor, list.cursor ? list.cursor : "");
    wf_agent_list_item_list_free(&list);
    indigo_log_info("list members '%s': %u", j->list_uri, g_session.actor_count);
    indigo_session_publish_event(&ev);
}

/* The accounts this one has muted or blocked. Indigo could mute and block from
 * a profile but had no way to see the result, so a mis-click was invisible
 * until the person failed to appear somewhere else. Both endpoints return the
 * same actor-list shape as every other list of people, so they fill the same
 * rows and raise the same event; only the request differs. */
void
indigo_session_do_moderation_list(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    const bool blocks = j->kind == JOB_BLOCKS;
    wf_agent_actor_list list;
    wf_status st;
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;

    INDIGO_SESSION_AGENT_GUARD(ev);
    memset(&list, 0, sizeof list);
    if (blocks) {
        st = wf_agent_get_blocks_typed(g_session.agent, INDIGO_SEARCH_PAGE, cursor, &list);
    } else {
        st = wf_agent_get_mutes_typed(g_session.agent, INDIGO_SEARCH_PAGE, cursor, &list);
    }
    if (st != WF_OK) {
        ev.failure = wf_failure_classify(st, 0, NULL);
        indigo_log_warn("%s list failed: wolfram status %d (%s)", blocks ? "blocked" : "muted",
                        (int) st, wf_failure_tag(ev.failure));
        indigo_session_publish_event(&ev);
        return;
    }

    indigo_session_publish_actor_page(j, &list, &ev);
    indigo_log_info("%s accounts: %u", blocks ? "blocked" : "muted", g_session.actor_count);
    indigo_session_publish_event(&ev);
}

/* The account's saved feeds. getPreferences carries the saved feed URIs (V2
 * first, the V1 list as the older fallback); getFeedGenerators turns them
 * into names. The raw preferences JSON is read rather than the typed parse
 * because one preference type the parser rejects must not take the whole
 * picker down with it -- Cobalt hit exactly that on a real account. */
void
indigo_session_do_feeds(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_saved_feed feeds[INDIGO_SEARCH_MAX];
    size_t n = 0;
    wf_status st;

    (void) j;
    INDIGO_SESSION_AGENT_GUARD(ev);
    st = wf_agent_get_saved_feeds(g_session.agent, feeds, INDIGO_SEARCH_MAX, &n);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "preferences", st);
        return;
    }

    g_session.feed_count = 0;
    for (size_t i = 0; i < n; i++) {
        indigo_list *o = &g_session.feeds[g_session.feed_count];

        memset(o, 0, sizeof *o);
        indigo_copy_utf8(o->uri, sizeof o->uri, feeds[i].uri);
        indigo_copy_utf8(o->name, sizeof o->name, feeds[i].name);
        indigo_copy_utf8(o->description, sizeof o->description, "Custom feed");
        g_session.feed_count++;
    }
    ev.kind = INDIGO_SESSION_EVENT_FEEDS_PAGE;
    ev.page_count = g_session.feed_count;
    indigo_log_info("feeds: %u", g_session.feed_count);
    indigo_session_publish_event(&ev);
}

bool
indigo_session_submit_lists(bool paging)
{
    indigo_job j = {.kind = JOB_LISTS};

    j.paging = paging;
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_list_members(const char *list_uri, bool paging)
{
    indigo_job j = {.kind = JOB_LIST_MEMBERS};

    if (!list_uri || !list_uri[0]) {
        return false;
    }
    j.paging = paging;
    indigo_copy_utf8(j.list_uri, sizeof j.list_uri, list_uri);
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_feeds(void)
{
    indigo_job j = {.kind = JOB_FEEDS};

    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_mutes(bool paging)
{
    indigo_job j = {.kind = JOB_MUTES};

    j.paging = paging;
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_blocks(bool paging)
{
    indigo_job j = {.kind = JOB_BLOCKS};

    j.paging = paging;
    return indigo_session_enqueue(&j);
}

void
indigo_session_lists_results(const indigo_list **lists, unsigned *count)
{
    if (lists) {
        *lists = g_session.lists;
    }
    if (count) {
        *count = g_session.list_count;
    }
}

void
indigo_session_feeds_results(const indigo_list **feeds, unsigned *count)
{
    if (feeds) {
        *feeds = g_session.feeds;
    }
    if (count) {
        *count = g_session.feed_count;
    }
}

#endif /* 3DS with Wolfram */
