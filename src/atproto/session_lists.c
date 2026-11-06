/* Curated lists, saved feeds, and the muted and blocked account lists. */

#include "atproto/session_internal.h"

#include <wolfram/atproto_lex.h>

#if defined(INDIGO_SESSION_REAL)

/* The account's curated lists. getLists is paged; the cursor is dropped like
 * actor and post search, because a 3DS list that cannot show page two is not
 * worth paging. */
void
indigo_session_do_lists(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_agent_list_view_list list;
    wf_status st;
    const char *cursor = INDIGO_SESSION_PAGE_CURSOR(*j);

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

    /* A fresh fetch replaces the held lists; only a further page appends. The
     * generic page macro resets the actor array, so this one is explicit. */
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
        indigo_copy_utf8(o->purpose, sizeof o->purpose,
                         l->purpose ? l->purpose : INDIGO_LIST_PURPOSE_CURATE);
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
    const char *cursor = INDIGO_SESSION_PAGE_CURSOR(*j);

    INDIGO_SESSION_AGENT_GUARD(ev);
    memset(&list, 0, sizeof list);
    st = wf_agent_get_list_typed(g_session.agent, j->list_uri, INDIGO_SEARCH_PAGE, cursor, &list);
    if (st != WF_OK) {
        indigo_session_publish_failure(&ev, "list members", st);
        return;
    }

    INDIGO_SESSION_PAGE_RESET(j->paging);
    if (!j->paging) {
        memset(g_session.list_member_uris, 0, sizeof g_session.list_member_uris);
    }
    for (size_t i = 0; i < list.item_count && g_session.actor_count < INDIGO_SEARCH_MAX; i++) {
        const wf_agent_profile_view *a = &list.items[i].subject;
        const unsigned row = g_session.actor_count;

        if (indigo_session_fill_actor(a, &g_session.actors[row])) {
            indigo_copy_utf8(g_session.list_member_uris[row],
                             sizeof g_session.list_member_uris[row],
                             list.items[i].uri ? list.items[i].uri : "");
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

/* Create, edit, add or remove one list record. The four writes share this
 * runner because they share the failure and success shapes; only the Wolfram
 * call in the middle differs. */
void
indigo_session_do_list_write(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_LIST_WRITE_FAILED};
    wf_status st;

    INDIGO_SESSION_AGENT_GUARD(ev);

    if (j->kind == JOB_LIST_CREATE) {
        wf_agent_post_result created;

        if (!j->list_name[0] || !j->list_purpose[0]) {
            ev.failure = WF_FAIL_OTHER;
            indigo_copy_utf8(ev.message, sizeof ev.message, "A list needs a name.");
            indigo_session_publish_event(&ev);
            return;
        }
        memset(&created, 0, sizeof created);
        st = wf_agent_graph_create_list(g_session.agent, j->list_purpose,
                                        j->list_name,
                                        j->list_description[0]
                                            ? j->list_description : NULL,
                                        &created);
        if (st != WF_OK) {
            indigo_session_publish_failure(&ev, "create list", st);
            return;
        }
        wf_agent_post_result_free(&created);
        indigo_copy_utf8(ev.message, sizeof ev.message, "List created.");
    } else if (j->kind == JOB_LIST_UPDATE) {
        wf_repo_record record;
        wf_agent_post_result updated;
        const char *slash = strrchr(j->list_uri, '/');
        char *json;

        if (!j->list_name[0] || !slash || !slash[1]) {
            ev.failure = WF_FAIL_OTHER;
            indigo_copy_utf8(ev.message, sizeof ev.message,
                             "That list cannot be edited.");
            indigo_session_publish_event(&ev);
            return;
        }
        memset(&record, 0, sizeof record);
        st = wf_agent_get_record_typed(g_session.agent,
                                       wf_agent_get_did(g_session.agent),
                                       WF_LEX_APP_BSKY_GRAPH_LIST_NSID, slash + 1, NULL,
                                       &record);
        if (st != WF_OK || !record.value || !cJSON_IsObject(record.value)) {
            wf_repo_record_free(&record);
            indigo_session_publish_failure(&ev, "read list", st);
            return;
        }
        cJSON_DeleteItemFromObjectCaseSensitive(record.value, "name");
        cJSON_DeleteItemFromObjectCaseSensitive(record.value, "description");
        cJSON_DeleteItemFromObjectCaseSensitive(record.value, "purpose");
        cJSON_AddStringToObject(record.value, "name", j->list_name);
        if (j->list_description[0]) {
            cJSON_AddStringToObject(record.value, "description",
                                    j->list_description);
        }
        cJSON_AddStringToObject(record.value, "purpose", j->list_purpose);
        json = cJSON_PrintUnformatted(record.value);
        if (!json) {
            wf_repo_record_free(&record);
            ev.failure = WF_FAIL_OTHER;
            indigo_copy_utf8(ev.message, sizeof ev.message,
                             "Could not build the list record.");
            indigo_session_publish_event(&ev);
            return;
        }
        memset(&updated, 0, sizeof updated);
        st = wf_agent_graph_update_list(g_session.agent, slash + 1, json, &updated);
        cJSON_free(json);
        wf_repo_record_free(&record);
        if (st != WF_OK) {
            indigo_session_publish_failure(&ev, "save list", st);
            return;
        }
        wf_agent_post_result_free(&updated);
        indigo_copy_utf8(ev.message, sizeof ev.message, "List updated.");
    } else if (j->kind == JOB_LIST_ADD_MEMBER) {
        wf_agent_post_result item;
        char subject[INDIGO_LIST_SUBJECT_MAX];
        char *resolved = NULL;

        indigo_copy_utf8(subject, sizeof subject, j->list_subject);
        if (subject[0] == '@') {
            memmove(subject, subject + 1, strlen(subject));
        }
        if (!subject[0] || !j->list_uri[0]) {
            ev.failure = WF_FAIL_OTHER;
            indigo_copy_utf8(ev.message, sizeof ev.message,
                             "Choose a list and an account.");
            indigo_session_publish_event(&ev);
            return;
        }
        if (strncmp(subject, "did:", 4) != 0) {
            st = wf_agent_resolve_handle(g_session.agent, subject, &resolved);
            if (st != WF_OK || !resolved || !resolved[0]) {
                free(resolved);
                ev.failure = WF_FAIL_OTHER;
                indigo_copy_utf8(ev.message, sizeof ev.message,
                                 "Could not find that account.");
                indigo_session_publish_event(&ev);
                return;
            }
            indigo_copy_utf8(subject, sizeof subject, resolved);
            free(resolved);
        }
        memset(&item, 0, sizeof item);
        st = wf_agent_graph_create_list_item(g_session.agent, j->list_uri,
                                             subject, &item);
        if (st != WF_OK) {
            indigo_session_publish_failure(&ev, "add list member", st);
            return;
        }
        wf_agent_post_result_free(&item);
        indigo_copy_utf8(ev.message, sizeof ev.message, "Added to the list.");
    } else {
        if (!j->list_item_uri[0]) {
            ev.failure = WF_FAIL_OTHER;
            indigo_copy_utf8(ev.message, sizeof ev.message,
                             "That member has no record to remove.");
            indigo_session_publish_event(&ev);
            return;
        }
        st = wf_agent_graph_delete_list_item(g_session.agent, j->list_item_uri);
        if (st != WF_OK) {
            indigo_session_publish_failure(&ev, "remove list member", st);
            return;
        }
        indigo_copy_utf8(ev.message, sizeof ev.message, "Removed from the list.");
    }

    ev.kind = INDIGO_SESSION_EVENT_LIST_WRITE_DONE;
    indigo_session_publish_event(&ev);
}

/* A starter pack is the pack record plus the first page of its underlying
 * list. A further page reuses the held list URI and asks only for members, so
 * opening a pack is two requests and paging is one. The pack header survives
 * a failed member page: the screen can still show what was opened and say the
 * member list is unavailable. */
void
indigo_session_do_starter_pack(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_STARTER_PACK_FAILED};
    char list_uri[INDIGO_POST_URI_MAX];
    const char *cursor = INDIGO_SESSION_PAGE_CURSOR(*j);

    INDIGO_SESSION_AGENT_GUARD(ev);
    if (!j->paging) {
        wf_graph_starter_pack_view pack;
        wf_status st;

        memset(&pack, 0, sizeof pack);
        st = wf_agent_get_starter_pack_typed(g_session.agent, j->starter_pack_uri,
                                             &pack);
        if (st != WF_OK) {
            indigo_session_publish_failure(&ev, "starter pack", st);
            return;
        }
        indigo_starter_pack_fill_from_wolfram(&g_session.starter_pack, &pack);
        indigo_copy_utf8(list_uri, sizeof list_uri, g_session.starter_pack.list_uri);
        wf_graph_starter_pack_view_free(&pack);
    } else {
        indigo_copy_utf8(list_uri, sizeof list_uri, g_session.starter_pack.list_uri);
    }

    if (!list_uri[0]) {
        g_session.actor_count = 0;
        ev.kind = INDIGO_SESSION_EVENT_STARTER_PACK_PAGE;
        ev.page_count = 0;
        ev.cursor[0] = '\0';
        indigo_session_publish_event(&ev);
        return;
    }

    {
        wf_agent_list_item_list list;
        wf_status st;

        memset(&list, 0, sizeof list);
        st = wf_agent_get_list_typed(g_session.agent, list_uri, INDIGO_SEARCH_PAGE,
                                     cursor, &list);
        if (st != WF_OK) {
            indigo_session_publish_failure(&ev, "starter-pack members", st);
            return;
        }
        if (!j->paging) {
            g_session.actor_count = 0;
        }
        for (size_t i = 0; i < list.item_count && g_session.actor_count < INDIGO_SEARCH_MAX; i++) {
            if (indigo_session_fill_actor(&list.items[i].subject,
                                          &g_session.actors[g_session.actor_count])) {
                g_session.actor_count++;
            }
        }
        ev.kind = INDIGO_SESSION_EVENT_STARTER_PACK_PAGE;
        ev.page_count = g_session.actor_count;
        indigo_copy_utf8(ev.cursor, sizeof ev.cursor, list.cursor ? list.cursor : "");
        wf_agent_list_item_list_free(&list);
    }
    indigo_log_info("starter pack '%s': %u members", j->starter_pack_uri,
                    g_session.actor_count);
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
    const char *cursor = INDIGO_SESSION_PAGE_CURSOR(*j);

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
indigo_session_submit_lists(const char *cursor)
{
    indigo_job j = {.kind = JOB_LISTS};

    if (cursor) {
        snprintf(j.cursor, sizeof j.cursor, "%s", cursor);
    }
    j.paging = j.cursor[0] != '\0';
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_list_members(const char *list_uri, const char *cursor)
{
    indigo_job j = {.kind = JOB_LIST_MEMBERS};

    if (!list_uri || !list_uri[0]) {
        return false;
    }
    if (cursor) {
        snprintf(j.cursor, sizeof j.cursor, "%s", cursor);
    }
    j.paging = j.cursor[0] != '\0';
    indigo_copy_utf8(j.list_uri, sizeof j.list_uri, list_uri);
    return indigo_session_enqueue(&j);
}

static bool
submit_list_write(indigo_job_kind kind, const char *list_uri, const char *name,
                  const char *description, const char *purpose,
                  const char *subject, const char *item_uri)
{
    indigo_job j = {.kind = kind};

    if (list_uri) {
        indigo_copy_utf8(j.list_uri, sizeof j.list_uri, list_uri);
    }
    if (name) {
        indigo_copy_utf8(j.list_name, sizeof j.list_name, name);
    }
    if (description) {
        indigo_copy_utf8(j.list_description, sizeof j.list_description, description);
    }
    if (purpose) {
        indigo_copy_utf8(j.list_purpose, sizeof j.list_purpose, purpose);
    }
    if (subject) {
        indigo_copy_utf8(j.list_subject, sizeof j.list_subject, subject);
    }
    if (item_uri) {
        indigo_copy_utf8(j.list_item_uri, sizeof j.list_item_uri, item_uri);
    }
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_list_create(const char *name, const char *description,
                                  const char *purpose)
{
    if (!name || !name[0]) {
        return false;
    }
    return submit_list_write(JOB_LIST_CREATE, NULL, name, description,
                             purpose && purpose[0] ? purpose
                                                   : INDIGO_LIST_PURPOSE_CURATE,
                             NULL, NULL);
}

bool
indigo_session_submit_list_update(const char *list_uri, const char *name,
                                  const char *description, const char *purpose)
{
    if (!list_uri || !list_uri[0] || !name || !name[0]) {
        return false;
    }
    return submit_list_write(JOB_LIST_UPDATE, list_uri, name, description,
                             purpose && purpose[0] ? purpose
                                                   : INDIGO_LIST_PURPOSE_CURATE,
                             NULL, NULL);
}

bool
indigo_session_submit_list_add_member(const char *list_uri, const char *subject)
{
    if (!list_uri || !list_uri[0] || !subject || !subject[0]) {
        return false;
    }
    return submit_list_write(JOB_LIST_ADD_MEMBER, list_uri, NULL, NULL, NULL,
                             subject, NULL);
}

bool
indigo_session_submit_list_remove_member(const char *list_item_uri)
{
    if (!list_item_uri || !list_item_uri[0]) {
        return false;
    }
    return submit_list_write(JOB_LIST_REMOVE_MEMBER, NULL, NULL, NULL, NULL,
                             NULL, list_item_uri);
}

const char *
indigo_session_list_member_uri(unsigned index)
{
    return index < g_session.actor_count ? g_session.list_member_uris[index] : "";
}

bool
indigo_session_submit_starter_pack(const char *pack_uri, const char *cursor)
{
    indigo_job j = {.kind = JOB_STARTER_PACK};
    char canonical[INDIGO_POST_URI_MAX];

    if (!indigo_starter_pack_uri_from_link(pack_uri, canonical, sizeof canonical)) {
        return false;
    }
    if (cursor) {
        snprintf(j.cursor, sizeof j.cursor, "%s", cursor);
    }
    j.paging = j.cursor[0] != '\0';
    indigo_copy_utf8(j.starter_pack_uri, sizeof j.starter_pack_uri, canonical);
    return indigo_session_enqueue(&j);
}

const indigo_starter_pack *
indigo_session_starter_pack(void)
{
    return &g_session.starter_pack;
}

bool
indigo_session_submit_feeds(void)
{
    indigo_job j = {.kind = JOB_FEEDS};

    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_mutes(const char *cursor)
{
    indigo_job j = {.kind = JOB_MUTES};

    if (cursor) {
        snprintf(j.cursor, sizeof j.cursor, "%s", cursor);
    }
    j.paging = j.cursor[0] != '\0';
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_blocks(const char *cursor)
{
    indigo_job j = {.kind = JOB_BLOCKS};

    if (cursor) {
        snprintf(j.cursor, sizeof j.cursor, "%s", cursor);
    }
    j.paging = j.cursor[0] != '\0';
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

#endif /* INDIGO_SESSION_REAL */
