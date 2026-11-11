/*
 * feeds.c -- custom feed discovery and management.
 *
 * Feed discovery uses app.bsky.unspecced.getSuggestedFeeds (via
 * wf_agent_get_suggested_feeds_typed) to find popular generators.
 * Feed management reads/writes savedFeedsPrefV2 via the raw preferences
 * JSON (wf_agent_get_preferences / wf_agent_put_preferences), not the strict
 * typed parser: one preference type the parser rejects must not take the
 * whole feed list down with it, pinned flags only exist in the raw v2 items,
 * and a putPreferences round-trip must copy unknown preference types
 * verbatim.
 */

#include "atproto/feeds.h"
#include "atproto/session_internal.h"

#include "wolfram/agent.h"
#include "wolfram/unspecced_trends_typed.h"
#include "wolfram/feed_gen_typed.h"
#include "wolfram/feedgen_typed.h"

#include "util/log.h"

#include <cJSON.h>

#include <string.h>
#include <stdlib.h>

/* Session state for feed discovery. */
static indigo_feed_discovery_page s_discovery_page;

/* Session state for saved feeds v2. */
static indigo_saved_feeds_v2 s_saved_feeds_v2;

/* Session state for a single feed generator detail. */
static indigo_feed_generator s_feed_generator;
static bool s_feed_generator_valid = false;

/* ------------------------------------------------------------------ */
/* Feed generator view -> indigo_feed_generator */

static void
copy_generator_from_agent(indigo_feed_generator *out, const wf_agent_generator_view *src)
{
    if (!out || !src) return;
    snprintf(out->uri, sizeof out->uri, "%s", src->uri ? src->uri : "");
    snprintf(out->did, sizeof out->did, "%s", src->did ? src->did : "");
    snprintf(out->display_name, sizeof out->display_name, "%s",
             src->display_name ? src->display_name : "");
    snprintf(out->description, sizeof out->description, "%s",
             src->description ? src->description : "");
    snprintf(out->avatar, sizeof out->avatar, "%s",
             src->avatar ? src->avatar : "");
    out->has_like_count = src->has_like_count != 0;
    out->like_count = src->has_like_count ? (int64_t) src->like_count : 0;
    /* wf_agent_generator_view doesn't have viewer_like or subscriber fields */
    out->has_viewer_like = false;
    out->viewer_like = false;
    out->has_subscriber_count = false;
    out->subscriber_count = 0;
    out->has_viewer_subscribed = false;
    out->viewer_subscribed = false;
}

/* ------------------------------------------------------------------ */
/* Feed discovery: suggested feeds */

void
indigo_session_do_feed_discovery(const indigo_job *j)
{
    (void) j;
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_FEED_DISCOVERY_FAILED};

    INDIGO_SESSION_AGENT_GUARD(ev);

    wf_agent_generator_view_list list = {0};
    const int limit = INDIGO_FEED_DISCOVERY_PAGE;

    const wf_status status =
        wf_agent_get_suggested_feeds_typed(g_session.agent, limit, &list);

    if (status != WF_OK) {
        indigo_log_warn("session: feed discovery failed (%d)", (int) status);
        indigo_session_publish_failure(&ev, "feed discovery", status);
        return;
    }

    LightLock_Lock(&g_session.lock);
    memset(&s_discovery_page, 0, sizeof(s_discovery_page));
    s_discovery_page.count = (int) list.generator_count;
    /* app.bsky.unspecced.getSuggestedFeeds doesn't support cursor pagination,
     * so has_more is always false. */
    s_discovery_page.has_more = false;
    s_discovery_page.cursor[0] = '\0';
    for (size_t i = 0; i < list.generator_count && i < INDIGO_FEED_DISCOVERY_PAGE;
         i++) {
        copy_generator_from_agent(&s_discovery_page.generators[i], &list.generators[i]);
    }
    LightLock_Unlock(&g_session.lock);

    wf_agent_generator_view_list_free(&list);

    indigo_log_info("session: discovered %d feeds", s_discovery_page.count);
    indigo_session_event ev_discovery = { .kind = INDIGO_SESSION_EVENT_FEED_DISCOVERY_PAGE };
    indigo_session_publish_event(&ev_discovery);
}

/* ------------------------------------------------------------------ */
/* Feed generator detail */

void
indigo_session_do_feed_generator(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_FEED_GENERATOR_FAILED};

    INDIGO_SESSION_AGENT_GUARD(ev);

    wf_agent_generator_detail detail = {0};
    const wf_status status =
        wf_agent_get_feed_generator_typed(g_session.agent, j->actor, &detail);

    if (status != WF_OK) {
        indigo_log_warn("session: feed generator fetch failed (%d)", (int) status);
        indigo_session_publish_failure(&ev, "feed generator", status);
        return;
    }

    LightLock_Lock(&g_session.lock);
    copy_generator_from_agent(&s_feed_generator, &detail.view);
    s_feed_generator_valid = true;
    LightLock_Unlock(&g_session.lock);

    wf_agent_generator_detail_free(&detail);

    indigo_log_info("session: loaded feed generator %s", j->actor);
    indigo_session_event ev_generator = { .kind = INDIGO_SESSION_EVENT_FEED_GENERATOR_PAGE };
    indigo_session_publish_event(&ev_generator);
}

/* ------------------------------------------------------------------ */
/* Saved feeds v2: read savedFeedsPrefV2 */

/* The items array of the savedFeedsPrefV2 preference inside a raw preferences
 * array, duplicated; NULL when there is none. */
static cJSON *
prefs_saved_feeds_v2_items(const cJSON *prefs)
{
    if (!prefs || !cJSON_IsArray(prefs)) {
        return NULL;
    }
    for (int i = 0; i < cJSON_GetArraySize(prefs); i++) {
        const cJSON *it = cJSON_GetArrayItem(prefs, i);
        const cJSON *ty = cJSON_GetObjectItemCaseSensitive(it, "$type");
        if (ty && cJSON_IsString(ty) && ty->valuestring &&
            strstr(ty->valuestring, "savedFeedsPrefV2")) {
            const cJSON *items = cJSON_GetObjectItemCaseSensitive(it, "items");
            if (cJSON_IsArray(items)) {
                return cJSON_Duplicate(items, true);
            }
            return NULL;
        }
    }
    return NULL;
}

void
indigo_session_do_saved_feeds_v2(const indigo_job *j)
{
    (void) j;
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SAVED_FEEDS_V2_FAILED};

    INDIGO_SESSION_AGENT_GUARD(ev);

    char *cur_json = NULL;
    const wf_status status = wf_agent_get_preferences(g_session.agent, &cur_json);
    if (status != WF_OK || !cur_json) {
        indigo_log_warn("session: saved feeds v2 fetch failed (%d)", (int) status);
        indigo_session_publish_failure(&ev, "saved feeds v2", status);
        return;
    }
    cJSON *prefs = cJSON_Parse(cur_json);
    free(cur_json);
    if (!prefs || !cJSON_IsArray(prefs)) {
        if (prefs) cJSON_Delete(prefs);
        prefs = NULL;
    }

    LightLock_Lock(&g_session.lock);
    memset(&s_saved_feeds_v2, 0, sizeof(s_saved_feeds_v2));
    if (prefs) {
        cJSON *items = prefs_saved_feeds_v2_items(prefs);
        if (items) {
            for (int i = 0; i < cJSON_GetArraySize(items) &&
                            s_saved_feeds_v2.count < INDIGO_SAVED_FEEDS_MAX; i++) {
                const cJSON *it = cJSON_GetArrayItem(items, i);
                const cJSON *ty = cJSON_GetObjectItemCaseSensitive(it, "type");
                const cJSON *val = cJSON_GetObjectItemCaseSensitive(it, "value");
                const cJSON *pin = cJSON_GetObjectItemCaseSensitive(it, "pinned");
                if (!ty || !val || !cJSON_IsString(ty) || !cJSON_IsString(val) ||
                    strcmp(ty->valuestring, "feed") != 0 || !val->valuestring[0]) {
                    continue;
                }
                indigo_saved_feed_entry *e =
                    &s_saved_feeds_v2.entries[s_saved_feeds_v2.count++];
                snprintf(e->uri, sizeof e->uri, "%s", val->valuestring);
                const char *key = strrchr(val->valuestring, '/');
                snprintf(e->display_name, sizeof e->display_name, "%s",
                         key ? key + 1 : val->valuestring);
                e->pinned = pin && cJSON_IsTrue(pin);
            }
            cJSON_Delete(items);
        }
        cJSON_Delete(prefs);
    }
    LightLock_Unlock(&g_session.lock);

    /* Resolve display names via getFeedGenerators; a failure there leaves the
     * record keys in place, which are still a usable picker. */
    if (s_saved_feeds_v2.count > 0) {
        const char *uris[INDIGO_SAVED_FEEDS_MAX];
        int uri_count = 0;
        for (int i = 0; i < s_saved_feeds_v2.count; i++) {
            if (s_saved_feeds_v2.entries[i].uri[0]) {
                uris[uri_count++] = s_saved_feeds_v2.entries[i].uri;
            }
        }
        if (uri_count > 0) {
            wf_feedgen_generator_list gens = {0};
            const wf_status st =
                wf_feedgen_get_feed_generators_typed(g_session.agent, uris, uri_count,
                                                     &gens);
            if (st == WF_OK) {
                LightLock_Lock(&g_session.lock);
                for (size_t g = 0; g < gens.generator_count; g++) {
                    const wf_feedgen_generator_view *v = &gens.generators[g];
                    for (int i = 0; i < s_saved_feeds_v2.count; i++) {
                        if (v->uri &&
                            strcmp(v->uri, s_saved_feeds_v2.entries[i].uri) == 0 &&
                            v->display_name && v->display_name[0]) {
                            snprintf(s_saved_feeds_v2.entries[i].display_name,
                                     sizeof(s_saved_feeds_v2.entries[i].display_name),
                                     "%s", v->display_name);
                        }
                    }
                }
                LightLock_Unlock(&g_session.lock);
                wf_feedgen_generator_list_free(&gens);
            }
        }
    }

    indigo_log_info("session: loaded %d saved feeds", s_saved_feeds_v2.count);
    indigo_session_event ev_saved = { .kind = INDIGO_SESSION_EVENT_SAVED_FEEDS_V2_PAGE };
    indigo_session_publish_event(&ev_saved);
}

/* ------------------------------------------------------------------ */
/* Save/remove/pin feed: write savedFeedsPrefV2 */

/* Build the v2 saved-feeds items array from the raw current preferences,
 * copied verbatim, with the target feed added, re-pinned or removed.
 * Returns a cJSON array the caller must free. */
static cJSON *
build_saved_feeds_v2_items(const char *feed_uri, bool pinned, bool remove)
{
    char *cur_json = NULL;
    cJSON *prefs = NULL;
    if (wf_agent_get_preferences(g_session.agent, &cur_json) == WF_OK && cur_json) {
        prefs = cJSON_Parse(cur_json);
        free(cur_json);
    }
    if (!prefs || !cJSON_IsArray(prefs)) {
        if (prefs) cJSON_Delete(prefs);
        prefs = cJSON_CreateArray();
    }

    cJSON *items = cJSON_CreateArray();
    if (!items) {
        cJSON_Delete(prefs);
        return NULL;
    }

    bool seen = false;
    cJSON *existing = prefs_saved_feeds_v2_items(prefs);
    if (existing) {
        for (int i = 0; i < cJSON_GetArraySize(existing); i++) {
            cJSON *src = cJSON_GetArrayItem(existing, i);
            const cJSON *ty = cJSON_GetObjectItemCaseSensitive(src, "type");
            const cJSON *val = cJSON_GetObjectItemCaseSensitive(src, "value");
            const bool is_target = ty && val && cJSON_IsString(ty) &&
                                   cJSON_IsString(val) && val->valuestring &&
                                   strcmp(ty->valuestring, "feed") == 0 &&
                                   strcmp(val->valuestring, feed_uri) == 0;
            if (is_target) {
                seen = true;
                if (remove) {
                    continue;   /* drop the item entirely */
                }
                cJSON *item = cJSON_Duplicate(src, true);
                cJSON *pin = cJSON_GetObjectItemCaseSensitive(item, "pinned");
                if (pin) {
                    cJSON_ReplaceItemInObject(item, "pinned",
                                              cJSON_CreateBool(pinned));
                } else {
                    cJSON_AddBoolToObject(item, "pinned", pinned);
                }
                cJSON_AddItemToArray(items, item);
            } else {
                cJSON_AddItemToArray(items, cJSON_Duplicate(src, true));
            }
        }
        cJSON_Delete(existing);
    }

    if (!remove && !seen) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "feed");
        cJSON_AddStringToObject(item, "value", feed_uri);
        cJSON_AddBoolToObject(item, "pinned", pinned);
        cJSON_AddItemToArray(items, item);
    }

    cJSON_Delete(prefs);
    return items;
}

/* putPreferences replaces the whole preferences array, so round-trip the
 * current one and swap in the modified v2 saved-feeds preference. */
static wf_status
put_saved_feeds_v2(cJSON *items)
{
    char *cur_json = NULL;
    cJSON *prefs = NULL;
    if (wf_agent_get_preferences(g_session.agent, &cur_json) == WF_OK && cur_json) {
        prefs = cJSON_Parse(cur_json);
        free(cur_json);
    }
    if (!prefs || !cJSON_IsArray(prefs)) {
        if (prefs) cJSON_Delete(prefs);
        prefs = cJSON_CreateArray();
    }
    cJSON *v2 = cJSON_CreateObject();
    cJSON_AddItemToObject(v2, "$type",
                          cJSON_CreateString("app.bsky.actor.defs#savedFeedsPrefV2"));
    cJSON_AddItemToObject(v2, "items", items);
    for (int i = cJSON_GetArraySize(prefs) - 1; i >= 0; i--) {
        cJSON *it = cJSON_GetArrayItem(prefs, i);
        cJSON *ty = cJSON_GetObjectItemCaseSensitive(it, "$type");
        if (ty && cJSON_IsString(ty) && ty->valuestring &&
            strstr(ty->valuestring, "savedFeedsPrefV2")) {
            cJSON_DeleteItemFromArray(prefs, i);
        }
    }
    cJSON_AddItemToArray(prefs, v2);

    char *json = cJSON_PrintUnformatted(prefs);
    cJSON_Delete(prefs);
    if (!json) {
        return WF_ERR_ALLOC;
    }
    wf_response res = {0};
    const wf_status status = wf_agent_put_preferences(g_session.agent, json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

/* Shared save/remove/pin body: build the items, write them, publish the
 * event. */
static void
do_feed_mutation(const indigo_job *j, bool remove,
                 indigo_session_event_kind done_kind,
                 indigo_session_event_kind failed_kind, const char *what)
{
    indigo_session_event ev = {.kind = failed_kind};

    INDIGO_SESSION_AGENT_GUARD(ev);

    cJSON *items = build_saved_feeds_v2_items(j->actor, j->paging != 0, remove);
    if (!items) {
        indigo_log_warn("session: could not prepare %s", what);
        ev.failure = WF_FAIL_OTHER;
        indigo_session_publish_event(&ev);
        return;
    }

    const wf_status put_status = put_saved_feeds_v2(items);
    if (put_status != WF_OK) {
        indigo_log_warn("session: %s failed (%d)", what, (int) put_status);
        indigo_session_publish_failure(&ev, what, put_status);
        return;
    }

    /* Invalidate the cached list so the UI reloads. */
    LightLock_Lock(&g_session.lock);
    memset(&s_saved_feeds_v2, 0, sizeof(s_saved_feeds_v2));
    LightLock_Unlock(&g_session.lock);

    indigo_log_info("session: %s %s", what, j->actor);
    indigo_session_event ev_done = { .kind = done_kind };
    indigo_session_publish_event(&ev_done);
}

void
indigo_session_do_save_feed(const indigo_job *j)
{
    do_feed_mutation(j, false, INDIGO_SESSION_EVENT_SAVE_FEED_DONE,
                     INDIGO_SESSION_EVENT_SAVE_FEED_FAILED, "saved feed");
}

void
indigo_session_do_remove_feed(const indigo_job *j)
{
    do_feed_mutation(j, true, INDIGO_SESSION_EVENT_REMOVE_FEED_DONE,
                     INDIGO_SESSION_EVENT_REMOVE_FEED_FAILED, "removed feed");
}

void
indigo_session_do_pin_feed(const indigo_job *j)
{
    do_feed_mutation(j, false, INDIGO_SESSION_EVENT_PIN_FEED_DONE,
                     INDIGO_SESSION_EVENT_PIN_FEED_FAILED, "pinned feed");
}

/* ------------------------------------------------------------------ */
/* Public accessors */

const indigo_feed_discovery_page *
indigo_session_feed_discovery_results(void)
{
    return &s_discovery_page;
}

const indigo_feed_generator *
indigo_session_feed_generator_result(void)
{
    return &s_feed_generator;
}

bool
indigo_session_feed_generator_valid(void)
{
    return s_feed_generator_valid;
}

const indigo_saved_feeds_v2 *
indigo_session_saved_feeds_v2_results(void)
{
    return &s_saved_feeds_v2;
}

/* ------------------------------------------------------------------ */
/* Submission functions */

bool
indigo_session_submit_feed_discovery(const char *cursor)
{
    indigo_job j = {.kind = JOB_FEED_DISCOVERY};
    if (cursor && cursor[0]) {
        snprintf(j.cursor, sizeof j.cursor, "%s", cursor);
        j.paging = true;
    }
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_feed_generator(const char *feed_uri)
{
    indigo_job j = {.kind = JOB_FEED_GENERATOR};
    snprintf(j.actor, sizeof j.actor, "%s", feed_uri);
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_saved_feeds_v2(void)
{
    indigo_job j = {.kind = JOB_SAVED_FEEDS_V2};
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_save_feed(const char *feed_uri, bool pinned)
{
    indigo_job j = {.kind = JOB_SAVE_FEED};
    snprintf(j.actor, sizeof j.actor, "%s", feed_uri);
    j.paging = pinned;
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_remove_feed(const char *feed_uri)
{
    indigo_job j = {.kind = JOB_REMOVE_FEED};
    snprintf(j.actor, sizeof j.actor, "%s", feed_uri);
    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_pin_feed(const char *feed_uri, bool pinned)
{
    indigo_job j = {.kind = JOB_PIN_FEED};
    snprintf(j.actor, sizeof j.actor, "%s", feed_uri);
    j.paging = pinned;
    return indigo_session_enqueue(&j);
}
