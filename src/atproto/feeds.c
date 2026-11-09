/*
 * feeds.c -- custom feed discovery and management.
 *
 * Feed discovery uses app.bsky.unspecced.getSuggestedFeeds (via
 * wf_agent_get_suggested_feeds_typed) to find popular generators.
 * Feed management reads/writes savedFeedsPrefV2 via
 * wf_agent_get_actor_prefs_typed / wf_agent_put_actor_prefs_typed.
 */

#include "atproto/feeds.h"
#include "atproto/session_internal.h"

#include "wolfram/agent.h"
#include "wolfram/unspecced_trends_typed.h"
#include "wolfram/feed_gen_typed.h"
#include "wolfram/feedgen_typed.h"
#include "wolfram/actor_prefs_typed.h"

#include "util/log.h"

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

static void
copy_saved_feed_entry(indigo_saved_feed_entry *out, const wf_actor_pref_saved_feed *src)
{
    if (!out || !src) return;
    snprintf(out->uri, sizeof out->uri, "%s", src->value ? src->value : "");
    /* display_name is not in the pref; we'll fill it from getFeedGenerators */
    out->display_name[0] = '\0';
    out->pinned = src->has_pinned && src->pinned;
}

void
indigo_session_do_saved_feeds_v2(const indigo_job *j)
{
    (void) j;
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SAVED_FEEDS_V2_FAILED};

    INDIGO_SESSION_AGENT_GUARD(ev);

    wf_actor_preferences prefs = {0};
    const wf_status status = wf_agent_get_actor_prefs_typed(g_session.agent, &prefs);

    if (status != WF_OK) {
        indigo_log_warn("session: saved feeds v2 fetch failed (%d)", (int) status);
        indigo_session_publish_failure(&ev, "saved feeds v2", status);
        return;
    }

    LightLock_Lock(&g_session.lock);
    memset(&s_saved_feeds_v2, 0, sizeof(s_saved_feeds_v2));

    /* savedFeedsPrefV2: items[] with type=="feed" */
    for (size_t i = 0; i < prefs.saved_feeds_v2.item_count &&
         s_saved_feeds_v2.count < INDIGO_SAVED_FEEDS_MAX; i++) {
        const wf_actor_pref_saved_feed *src = &prefs.saved_feeds_v2.items[i];
        if (src->type && strcmp(src->type, "feed") == 0) {
            copy_saved_feed_entry(&s_saved_feeds_v2.entries[s_saved_feeds_v2.count],
                                  src);
            s_saved_feeds_v2.count++;
        }
    }
    LightLock_Unlock(&g_session.lock);

    /* Now resolve names via getFeedGenerators for any feed without a name */
    if (s_saved_feeds_v2.count > 0) {
        const char *uris[INDIGO_SAVED_FEEDS_MAX];
        int uri_count = 0;
        for (int i = 0; i < s_saved_feeds_v2.count; i++) {
            if (s_saved_feeds_v2.entries[i].display_name[0] == '\0' &&
                s_saved_feeds_v2.entries[i].uri[0]) {
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
                        if (strcmp(v->uri, s_saved_feeds_v2.entries[i].uri) == 0 &&
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

    wf_actor_preferences_free(&prefs);

    indigo_log_info("session: loaded %d saved feeds", s_saved_feeds_v2.count);
    indigo_session_event ev_saved = { .kind = INDIGO_SESSION_EVENT_SAVED_FEEDS_V2_PAGE };
    indigo_session_publish_event(&ev_saved);
}

/* ------------------------------------------------------------------ */
/* Save/remove/pin feed: write savedFeedsPrefV2 */

/* Build a wf_actor_preferences struct with the modified savedFeedsPrefV2.
 * Returns a struct the caller must free with wf_actor_preferences_free. */
static wf_status
build_saved_feeds_v2_prefs(const char *feed_uri, bool pinned, bool remove,
                           wf_actor_preferences *out)
{
    /* Fetch current preferences to preserve non-feed items */
    wf_actor_preferences prefs = {0};
    const wf_status status = wf_agent_get_actor_prefs_typed(g_session.agent, &prefs);
    if (status != WF_OK) {
        indigo_log_warn("session: could not read preferences for feed save");
        return status;
    }

    /* Build new items array, modifying the target feed */
    size_t new_count = prefs.saved_feeds_v2.item_count + (remove ? 0 : 1);
    wf_actor_pref_saved_feed *new_items =
        calloc(new_count, sizeof(wf_actor_pref_saved_feed));
    if (!new_items) {
        wf_actor_preferences_free(&prefs);
        return WF_ERR_ALLOC;
    }

    size_t out_count = 0;
    bool found = false;

    /* Copy existing items, modifying the target feed */
    for (size_t i = 0; i < prefs.saved_feeds_v2.item_count; i++) {
        const wf_actor_pref_saved_feed *src = &prefs.saved_feeds_v2.items[i];
        if (src->type && src->value) {
            if (strcmp(src->type, "feed") == 0 &&
                strcmp(src->value, feed_uri) == 0) {
                found = true;
                if (remove) {
                    /* Skip: removing this feed */
                    continue;
                }
                /* Update pinned state */
                new_items[out_count].type = strdup(src->type);
                new_items[out_count].value = strdup(src->value);
                if (src->id) new_items[out_count].id = strdup(src->id);
                new_items[out_count].has_pinned = true;
                new_items[out_count].pinned = pinned;
                out_count++;
            } else {
                /* Copy unchanged */
                new_items[out_count].type = strdup(src->type);
                new_items[out_count].value = strdup(src->value);
                if (src->id) new_items[out_count].id = strdup(src->id);
                new_items[out_count].has_pinned = src->has_pinned;
                new_items[out_count].pinned = src->pinned;
                out_count++;
            }
        }
    }

    if (!remove && !found) {
        /* Add new feed entry */
        new_items[out_count].type = strdup("feed");
        new_items[out_count].value = strdup(feed_uri);
        new_items[out_count].id = NULL;
        new_items[out_count].has_pinned = true;
        new_items[out_count].pinned = pinned;
        out_count++;
    }

    /* Initialize output with current prefs but with our new items */
    *out = prefs;
    /* Clear the old items */
    for (size_t i = 0; i < out->saved_feeds_v2.item_count; i++) {
        free(out->saved_feeds_v2.items[i].type);
        free(out->saved_feeds_v2.items[i].value);
        free(out->saved_feeds_v2.items[i].id);
    }
    free(out->saved_feeds_v2.items);
    out->saved_feeds_v2.items = new_items;
    out->saved_feeds_v2.item_count = out_count;

    /* Clear prefs so we can free it without affecting out */
    prefs.saved_feeds_v2.items = NULL;
    prefs.saved_feeds_v2.item_count = 0;
    wf_actor_preferences_free(&prefs);
    return WF_OK;
}

void
indigo_session_do_save_feed(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SAVE_FEED_FAILED};

    INDIGO_SESSION_AGENT_GUARD(ev);

    wf_actor_preferences input = {0};
    const wf_status status = build_saved_feeds_v2_prefs(j->actor, j->paging, false, &input);
    if (status != WF_OK) {
        indigo_log_warn("session: could not prepare feed save");
        ev.failure = WF_FAIL_OTHER;
        indigo_session_publish_event(&ev);
        return;
    }

    const wf_status put_status = wf_agent_put_actor_prefs_typed(g_session.agent, &input);
    wf_actor_preferences_free(&input);

    if (put_status != WF_OK) {
        indigo_log_warn("session: feed save failed (%d)", (int) put_status);
        indigo_session_publish_failure(&ev, "save feed", put_status);
        return;
    }

    indigo_log_info("session: saved feed %s", j->actor);
    indigo_session_event ev_save = { .kind = INDIGO_SESSION_EVENT_SAVE_FEED_DONE };
    indigo_session_publish_event(&ev_save);
}

void
indigo_session_do_remove_feed(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_REMOVE_FEED_FAILED};

    INDIGO_SESSION_AGENT_GUARD(ev);

    wf_actor_preferences input = {0};
    const wf_status status = build_saved_feeds_v2_prefs(j->actor, false, true, &input);
    if (status != WF_OK) {
        indigo_log_warn("session: could not prepare feed removal");
        ev.failure = WF_FAIL_OTHER;
        indigo_session_publish_event(&ev);
        return;
    }

    const wf_status put_status = wf_agent_put_actor_prefs_typed(g_session.agent, &input);
    wf_actor_preferences_free(&input);

    if (put_status != WF_OK) {
        indigo_log_warn("session: feed removal failed (%d)", (int) put_status);
        indigo_session_publish_failure(&ev, "remove feed", put_status);
        return;
    }

    indigo_log_info("session: removed feed %s", j->actor);
    indigo_session_event ev_remove = { .kind = INDIGO_SESSION_EVENT_REMOVE_FEED_DONE };
    indigo_session_publish_event(&ev_remove);
}

void
indigo_session_do_pin_feed(const indigo_job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_PIN_FEED_FAILED};

    INDIGO_SESSION_AGENT_GUARD(ev);

    wf_actor_preferences input = {0};
    const wf_status status = build_saved_feeds_v2_prefs(j->actor, j->paging, false, &input);
    if (status != WF_OK) {
        indigo_log_warn("session: could not prepare feed pin");
        ev.failure = WF_FAIL_OTHER;
        indigo_session_publish_event(&ev);
        return;
    }

    const wf_status put_status = wf_agent_put_actor_prefs_typed(g_session.agent, &input);
    wf_actor_preferences_free(&input);

    if (put_status != WF_OK) {
        indigo_log_warn("session: feed pin failed (%d)", (int) put_status);
        indigo_session_publish_failure(&ev, "pin feed", put_status);
        return;
    }

    indigo_log_info("session: pinned feed %s to %d", j->actor, j->paging);
    indigo_session_event ev_pin = { .kind = INDIGO_SESSION_EVENT_PIN_FEED_DONE };
    indigo_session_publish_event(&ev_pin);
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
