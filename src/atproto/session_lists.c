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

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
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

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
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

    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
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
    char *prefs_json = NULL;
    const char *uris[INDIGO_SEARCH_MAX];
    unsigned n = 0;
    wf_status st;

    (void) j;
    if (!g_session.agent) {
        ev.failure = WF_FAIL_NOT_READY;
        indigo_session_publish_event(&ev);
        return;
    }
    st = wf_agent_get_preferences(g_session.agent, &prefs_json);
    if (st != WF_OK || !prefs_json) {
        indigo_session_publish_failure(&ev, "preferences", st);
        return;
    }
    {
        cJSON *prefs = cJSON_Parse(prefs_json);
        const cJSON *pref = NULL;

        free(prefs_json);
        if (!cJSON_IsArray(prefs)) {
            cJSON_Delete(prefs);
            ev.failure = WF_FAIL_OTHER;
            indigo_log_warn("preferences: not an array");
            indigo_session_publish_event(&ev);
            return;
        }
        cJSON_ArrayForEach(pref, prefs) {
            const cJSON *type = cJSON_GetObjectItemCaseSensitive(pref, "$type");

            if (!cJSON_IsString(type)
                || !strstr(type->valuestring, "savedFeedsPrefV2")) {
                continue;
            }
            {
                const cJSON *items = cJSON_GetObjectItemCaseSensitive(pref, "items");
                const cJSON *it = NULL;

                cJSON_ArrayForEach(it, items) {
                    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(it, "type");
                    const cJSON *value = cJSON_GetObjectItemCaseSensitive(it, "value");

                    if (n < INDIGO_SEARCH_MAX && cJSON_IsString(kind)
                        && cJSON_IsString(value) && !strcmp(kind->valuestring, "feed")
                        && value->valuestring[0]) {
                        uris[n++] = value->valuestring;
                    }
                }
            }
        }
        if (n == 0) {
            /* Older accounts only have the V1 list. */
            cJSON_ArrayForEach(pref, prefs) {
                const cJSON *type = cJSON_GetObjectItemCaseSensitive(pref, "$type");

                if (!cJSON_IsString(type)
                    || !strstr(type->valuestring, "savedFeedsPref")) {
                    continue;
                }
                {
                    const cJSON *saved = cJSON_GetObjectItemCaseSensitive(pref, "saved");
                    const cJSON *it = NULL;

                    cJSON_ArrayForEach(it, saved) {
                        if (n < INDIGO_SEARCH_MAX && cJSON_IsString(it)
                            && it->valuestring[0]) {
                            uris[n++] = it->valuestring;
                        }
                    }
                }
            }
        }
        cJSON_Delete(prefs);
    }

    g_session.feed_count = 0;
    if (n > 0) {
        wf_feedgen_generator_list gens;

        memset(&gens, 0, sizeof gens);
        st = wf_feedgen_get_feed_generators_typed(g_session.agent, uris, n, &gens);
        if (st != WF_OK) {
            /* The URIs alone still make a picker; the record key stands in
             * for the name, the same fallback Cobalt uses. */
            indigo_log_warn("feed generators failed: wolfram status %d", (int) st);
            memset(&gens, 0, sizeof gens);
        }
        for (unsigned i = 0; i < n; i++) {
            const char *name = NULL;
            indigo_list *o = &g_session.feeds[g_session.feed_count];

            for (size_t g = 0; g < gens.generator_count; g++) {
                if (gens.generators[g].uri && !strcmp(gens.generators[g].uri, uris[i])) {
                    name = gens.generators[g].display_name;
                    break;
                }
            }
            if (!name || !name[0]) {
                const char *slash = strrchr(uris[i], '/');

                name = slash ? slash + 1 : uris[i];
            }
            memset(o, 0, sizeof *o);
            indigo_copy_utf8(o->uri, sizeof o->uri, uris[i]);
            indigo_copy_utf8(o->name, sizeof o->name, name);
            indigo_copy_utf8(o->description, sizeof o->description, "Custom feed");
            g_session.feed_count++;
        }
        wf_feedgen_generator_list_free(&gens);
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
