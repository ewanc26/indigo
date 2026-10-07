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
#include <wolfram/time.h>
#include <wolfram/3ds.h>
#include <wolfram/actor_prefs_typed.h>
#include <wolfram/actor_typed.h>
#include <wolfram/agent.h>
#include <wolfram/oauth_pairing.h>
#include <wolfram/feed_gen_typed.h>
#include <wolfram/list_typed.h>
#include <wolfram/moderation_typed.h>
#include <wolfram/post_display.h>
#include <wolfram/post_view_typed.h>
#include <wolfram/thread_typed.h>

#define WORKER_STACK 0x20000

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
} job_kind;

typedef struct {
    job_kind kind;
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
    char text[INDIGO_DRAFT_MAX];
    char root_uri[INDIGO_POST_URI_MAX];
    char root_cid[INDIGO_POST_CID_MAX];
    char query[INDIGO_SEARCH_QUERY_MAX];
    indigo_follow_action follow;
    indigo_graph_action graph;
    indigo_search_kind people_kind;
    char actor[INDIGO_PROFILE_DID_MAX];
    char list_uri[INDIGO_POST_URI_MAX];
    /* JOB_FEED's target; the same shape as list_uri, kept separate so the two
     * jobs stay readable. */
    char feed_uri[INDIGO_POST_URI_MAX];
    /* True when this job asks for the next page of the last search rather
     * than a fresh one, so the worker appends instead of resetting. */
    bool paging;
} job;

static char s_path[256];
static Thread s_thread;
static LightLock s_lock;
static LightSemaphore s_wake;
static volatile bool s_quit;
static bool s_started;

/* Guarded by s_lock. */
static job s_job;
static bool s_busy;
static indigo_session_event s_event;
static bool s_event_ready;

/* Worker thread only. */
static wf_agent *s_agent;
static indigo_saved_session s_saved;
static bool s_node_session;
static char s_pair_url[WF_OAUTH_PAIR_URL_MAX];
static char s_pair_code[WF_OAUTH_PAIR_CODE_MAX];
/* Worker-only: the account's muted words and hide-reposts, fetched once per
 * sign-in and applied to every timeline and feed page. */
static indigo_prefs s_prefs;
static bool s_prefs_loaded;

/* Written by the worker before it publishes TIMELINE_PAGE; the main thread
 * reads it after polling that event and before the next submit. */
static indigo_post s_page[INDIGO_THREAD_MAX];
static unsigned s_page_count;
static indigo_profile s_profile;
static indigo_notification s_notes[INDIGO_NOTIFICATION_MAX];
static unsigned s_note_count;
static indigo_actor s_actors[INDIGO_SEARCH_MAX];
static unsigned s_actor_count;
/* Post search keeps its own results: indigo_post is a different type from
 * indigo_actor, and the screen shows one list at a time but still needs both
 * to be valid until the next result arrives. */
static indigo_post s_posts[INDIGO_SEARCH_MAX];
static unsigned s_post_count;
/* Curated lists keep their own array for the same reason the posts do: a
 * list is a third type, and the screen needs the previous list to stay valid
 * until the next result arrives. */
static indigo_list s_lists[INDIGO_SEARCH_MAX];
/* The account's saved feeds: list-shaped rows, so the same type, but their
 * own array so browsing feeds never disturbs a curated-lists view. */
static indigo_list s_feeds[INDIGO_SEARCH_MAX];
static unsigned s_list_count;
static unsigned s_feed_count;

static indigo_failure
classify(wf_status st)
{
    switch (st) {
    case WF_ERR_AUTH:
        return INDIGO_FAIL_BAD_CREDENTIALS;
    case WF_ERR_TIMEOUT:
        return INDIGO_FAIL_TIMEOUT;
    case WF_ERR_CRYPTO:
    case WF_ERR_CONFIG:
        return INDIGO_FAIL_TLS;
    case WF_ERR_RATE_LIMIT:
        return INDIGO_FAIL_RATE_LIMIT;
    case WF_ERR_NETWORK:
    case WF_ERR_DID_RESOLVE:
    case WF_ERR_HANDLE_RESOLVE:
        return INDIGO_FAIL_NETWORK;
    case WF_ERR_PARSE:
        return INDIGO_FAIL_BAD_RESPONSE;
    case WF_ERR_HTTP:
        return INDIGO_FAIL_SERVER;
    default:
        return INDIGO_FAIL_OTHER;
    }
}

static void
publish(indigo_session_event_kind kind, indigo_failure failure, const char *account)
{
    LightLock_Lock(&s_lock);
    memset(&s_event, 0, sizeof s_event);
    s_event.kind = kind;
    s_event.failure = failure;
    if (account) {
        snprintf(s_event.account, sizeof s_event.account, "%s", account);
    }
    s_event_ready = true;
    s_busy = false;
    LightLock_Unlock(&s_lock);
}

static void
publish_event(const indigo_session_event *ev)
{
    LightLock_Lock(&s_lock);
    s_event = *ev;
    s_event_ready = true;
    s_busy = false;
    LightLock_Unlock(&s_lock);
}

static void
drop_agent(void)
{
    if (s_agent) {
        wf_agent_free(s_agent);
        s_agent = NULL;
    }
    s_prefs_loaded = false;
}

static wf_agent *
new_agent(const char *service)
{
    wf_agent *a = wf_agent_new(service);

    if (!a) {
        return NULL;
    }
    if (wf_agent_set_ca_bundle(a, INDIGO_CA_BUNDLE_PATH) != WF_OK) {
        indigo_log_error("could not apply the CA bundle");
        wf_agent_free(a);
        return NULL;
    }
    if (wf_3ds_apply_tls_rng(a) != WF_OK) {
        indigo_log_warn("TLS RNG hook not applied; using the transport default");
    }
    return a;
}

/* The serialised session holds live tokens: scrub before freeing. */
static void
wipe_json(char *json)
{
    if (json) {
        volatile char *p = json;

        for (size_t n = strlen(json); n > 0; n--) {
            *p++ = 0;
        }
        free(json);
    }
}

static void
remember(const char *service)
{
    wf_session_data data = {0};
    wf_status st;

    memset(&s_saved, 0, sizeof s_saved);
    if (wf_agent_get_session_data(s_agent, &data) != WF_OK) {
        indigo_log_warn("session data unavailable; sign-in will not persist");
        return;
    }
    snprintf(s_saved.service, sizeof s_saved.service, "%s", service);

    if (s_node_session) {
        cJSON *root = cJSON_CreateObject();
        if (!root ||
            !cJSON_AddStringToObject(root, "kind", "oauth-node") ||
            !cJSON_AddStringToObject(root, "accessJwt", data.access_jwt) ||
            !cJSON_AddStringToObject(root, "handle", data.handle) ||
            !cJSON_AddStringToObject(root, "did", data.did)) {
            cJSON_Delete(root);
            wf_agent_session_data_free(&data);
            indigo_session_wipe(&s_saved);
            indigo_log_warn("could not encode OAuth-node session");
            return;
        }
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        wf_agent_session_data_free(&data);
        if (!json || strlen(json) >= sizeof s_saved.session) {
            indigo_log_warn("OAuth-node session too large; sign-in will not persist");
            wipe_json(json);
            indigo_session_wipe(&s_saved);
            return;
        }
        memcpy(s_saved.session, json, strlen(json) + 1);
        wipe_json(json);
    } else {
        char *json = NULL;
        st = wf_session_data_to_json(&data, &json);
        wf_agent_session_data_free(&data);
        if (st != WF_OK || !json || strlen(json) >= sizeof s_saved.session) {
            wipe_json(json);
            indigo_log_warn("session too large or unserialisable; sign-in will not persist");
            indigo_session_wipe(&s_saved);
            return;
        }
        memcpy(s_saved.session, json, strlen(json) + 1);
        wipe_json(json);
    }

    if (indigo_session_store_save(s_path, &s_saved) != INDIGO_STORE_OK) {
        indigo_log_warn("could not save the session; you will sign in again next launch");
    } else {
        indigo_log_info("session saved");
    }
}


/* The pairing client is Wolfram's (wolfram#101, include/wolfram/oauth_pairing.h):
 * it owns the begin/poll loop, the 404-is-terminal rule and the expiry. Indigo
 * supplies only the hooks: where to show the code, how to sleep. Every hook
 * runs on this worker thread. Nothing here logs the code or the token. */
static void
pair_on_code(const wf_oauth_pair_begin *begin, void *userdata)
{
    (void) userdata;
    LightLock_Lock(&s_lock);
    snprintf(s_pair_code, sizeof s_pair_code, "%s", begin->pair_code);
    snprintf(s_pair_url, sizeof s_pair_url, "%s", begin->pair_url);
    LightLock_Unlock(&s_lock);
}

static void
pair_sleep(unsigned ms, void *userdata)
{
    (void) userdata;
    svcSleepThread((s64) ms * 1000000LL);
}

static void
clear_pairing(void)
{
    LightLock_Lock(&s_lock);
    s_pair_code[0] = '\0';
    s_pair_url[0] = '\0';
    LightLock_Unlock(&s_lock);
}

static void
do_oauth(const job *j)
{
    static wf_oauth_pair_poll result;
    wf_oauth_pair_hooks hooks = {pair_on_code, NULL, pair_sleep, NULL, NULL};
    wf_xrpc_client *client = wf_xrpc_client_new(j->service);
    wf_status st;

    if (!client) {
        publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, INDIGO_FAIL_TLS, NULL);
        return;
    }
    wf_xrpc_client_set_ca_bundle(client, INDIGO_CA_BUNDLE_PATH);

    st = wf_oauth_pair_run(client, j->identifier, &hooks, &result);
    wf_xrpc_client_free(client);
    clear_pairing();

    if (st == WF_OK) {
        drop_agent();
        s_agent = new_agent(result.service);
        if (s_agent &&
            wf_agent_set_bearer(s_agent, result.token, result.handle, result.did) == WF_OK) {
            char handle[WF_OAUTH_PAIR_HANDLE_MAX];

            snprintf(handle, sizeof handle, "%s", result.handle);
            s_node_session = true;
            remember(result.service);
            wf_oauth_pair_poll_wipe(&result);
            publish(INDIGO_SESSION_EVENT_SIGNED_IN, INDIGO_FAIL_NONE, handle);
            return;
        }
        drop_agent();
        wf_oauth_pair_poll_wipe(&result);
        publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, INDIGO_FAIL_OTHER, NULL);
        return;
    }
    if (st == WF_ERR_AUTH) {
        /* The node's own message, e.g. "This pairing request expired." It is
         * written for a person and carries no credential. */
        indigo_log_warn("OAuth sign-in failed: %s",
                        result.message[0] ? result.message : "unknown");
        wf_oauth_pair_poll_wipe(&result);
        publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, INDIGO_FAIL_SERVER, NULL);
        return;
    }
    wf_oauth_pair_poll_wipe(&result);
    publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED,
            st == WF_ERR_TIMEOUT ? INDIGO_FAIL_TIMEOUT
            : st == WF_ERR_PARSE ? INDIGO_FAIL_BAD_RESPONSE
                                 : classify(st),
            NULL);
}

static void
do_login(const job *j)
{
    wf_status st;
    s_node_session = false;
    const char *who;

    drop_agent();
    s_agent = new_agent(j->service);
    if (!s_agent) {
        publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, INDIGO_FAIL_TLS, NULL);
        return;
    }

    indigo_log_info("sign-in: contacting %s", j->service);
    st = wf_agent_login(s_agent, j->identifier, j->password);
    if (st != WF_OK) {
        indigo_failure f = classify(st);

        indigo_log_warn("sign-in failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(f));
        drop_agent();
        publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, f, NULL);
        return;
    }

    remember(j->service);
    who = wf_agent_get_handle(s_agent);
    indigo_log_info("signed in as %s", who ? who : "(unknown)");
    publish(INDIGO_SESSION_EVENT_SIGNED_IN, INDIGO_FAIL_NONE, who);
}

static void
do_resume(void)
{
    indigo_store_status ss = indigo_session_store_load(s_path, &s_saved);
    wf_session_data data = {0};
    wf_status st;
    const char *who;

    if (ss == INDIGO_STORE_MISSING) {
        publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, INDIGO_FAIL_NONE, NULL);
        return;
    }
    if (ss != INDIGO_STORE_OK) {
        indigo_log_warn("saved session unreadable; kept as .bad");
        publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, INDIGO_FAIL_NONE, NULL);
        return;
    }

    drop_agent();
    s_agent = new_agent(s_saved.service);
    if (!s_agent) {
        indigo_session_wipe(&s_saved);
        publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, INDIGO_FAIL_TLS, NULL);
        return;
    }

    {
        cJSON *saved = cJSON_Parse(s_saved.session);
        const cJSON *kind = saved ? cJSON_GetObjectItemCaseSensitive(saved, "kind") : NULL;
        if (kind && cJSON_IsString(kind) && !strcmp(kind->valuestring, "oauth-node")) {
            const cJSON *token = cJSON_GetObjectItemCaseSensitive(saved, "accessJwt");
            const cJSON *handle = cJSON_GetObjectItemCaseSensitive(saved, "handle");
            const cJSON *did = cJSON_GetObjectItemCaseSensitive(saved, "did");
            if (!cJSON_IsString(token) || !cJSON_IsString(handle) ||
                !cJSON_IsString(did) ||
                wf_agent_set_bearer(s_agent, token->valuestring,
                                     handle->valuestring, did->valuestring) != WF_OK) {
                cJSON_Delete(saved);
                drop_agent();
                indigo_session_wipe(&s_saved);
                indigo_session_store_clear(s_path);
                publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, INDIGO_FAIL_BAD_CREDENTIALS, NULL);
                return;
            }
            s_node_session = true;
            cJSON_Delete(saved);
            who = wf_agent_get_handle(s_agent);
            publish(INDIGO_SESSION_EVENT_SIGNED_IN, INDIGO_FAIL_NONE, who);
            return;
        }
        cJSON_Delete(saved);
    }

    s_node_session = false;
    st = wf_session_data_from_json(s_saved.session, strlen(s_saved.session), &data);
    if (st != WF_OK) {
        indigo_log_warn("saved session unreadable; discarded");
        drop_agent();
        indigo_session_wipe(&s_saved);
        indigo_session_store_clear(s_path);
        publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, INDIGO_FAIL_NONE, NULL);
        return;
    }

    indigo_log_info("resuming saved session for %s", data.handle);
    st = wf_agent_resume(s_agent, &data);
    wf_agent_session_data_free(&data);
    if (st == WF_OK) {
        st = wf_agent_get_session(s_agent);
    }
    if (st != WF_OK) {
        indigo_failure f = classify(st);
        indigo_log_warn("resume failed: wolfram status %d (%s)", (int) st, indigo_failure_tag(f));
        drop_agent();
        indigo_session_wipe(&s_saved);
        if (f == INDIGO_FAIL_BAD_CREDENTIALS) {
            indigo_session_store_clear(s_path);
        }
        publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, f, NULL);
        return;
    }

    {
        char service[256];
        snprintf(service, sizeof service, "%s", s_saved.service);
        indigo_session_wipe(&s_saved);
        remember(service);
    }
    who = wf_agent_get_handle(s_agent);
    indigo_log_info("resumed session for %s", who ? who : "(unknown)");
    publish(INDIGO_SESSION_EVENT_SIGNED_IN, INDIGO_FAIL_NONE, who);
}

static void
do_logout(void)
{
    if (s_agent) {
        /* Best effort: the local session is cleared whatever the server says. */
        (void) wf_agent_logout(s_agent);
        drop_agent();
    }
    indigo_session_store_clear(s_path);
    indigo_log_info("signed out");
    publish(INDIGO_SESSION_EVENT_SIGNED_OUT, INDIGO_FAIL_NONE, NULL);
}

static void
embed_note(const wf_post_display *d, char *dst, size_t cap)
{
    dst[0] = '\0';
    switch (d->embed_kind) {
    case WF_EMBED_IMAGES:
        snprintf(dst, cap, d->image_count == 1 ? "[1 image]" : "[%u images]",
                 (unsigned) d->image_count);
        break;
    case WF_EMBED_VIDEO:
        snprintf(dst, cap, "[video]");
        break;
    case WF_EMBED_EXTERNAL:
        snprintf(dst, cap, "Link: %s", d->external_title && d->external_title[0]
                                           ? d->external_title
                                           : (d->external_uri ? d->external_uri : ""));
        break;
    case WF_EMBED_RECORD:
    case WF_EMBED_RECORD_WITH_MEDIA:
        if (d->quote_uri) {
            snprintf(dst, cap, "Quote @%s: %s",
                     d->quote_author_handle ? d->quote_author_handle : "?",
                     d->quote_text ? d->quote_text : "");
        } else {
            snprintf(dst, cap, "[quoted post unavailable]");
        }
        break;
    case WF_EMBED_UNKNOWN:
        snprintf(dst, cap, "[attachment]");
        break;
    case WF_EMBED_NONE:
        break;
    }
    for (char *c = dst; *c; c++) {
        if (*c == '\n' || *c == '\r') {
            *c = ' ';
        }
    }
}

static unsigned
count_of(int v)
{
    return v > 0 ? (unsigned) v : 0;
}

/* Carry the embed onto the post, for the screens that draw it. The
 * display helper owns the safe text/facet view; the typed embed reader supplies
 * image metadata that the display summary deliberately keeps bounded to a count. */
static void
fill_embed(const wf_post_display *d, const cJSON *raw_embed, indigo_post *out)
{
    wf_post_embed embed = {0};

    out->embed_count =
        d->image_count > INDIGO_EMBED_IMAGES_MAX ? INDIGO_EMBED_IMAGES_MAX
                                                  : (unsigned char) d->image_count;

    if (wf_post_embed_from_json(raw_embed, &embed) != WF_OK) {
        return;
    }

    if (embed.image_count > 0 && embed.images) {
        for (size_t i = 0; i < embed.image_count; i++) {
            const wf_post_embed_image *img = &embed.images[i];
            if (!img->thumb || !img->thumb[0]) {
                continue;
            }
            if (out->embed_kind == INDIGO_EMBED_NONE) {
                out->embed_kind = INDIGO_EMBED_IMAGE;
                indigo_media_cdn_url(out->embed_thumb, sizeof out->embed_thumb, img->thumb, INDIGO_CDN_THUMBNAIL);
                indigo_copy_utf8(out->embed_alt, sizeof out->embed_alt,
                                 img->alt ? img->alt : "");
                if (img->width > 0 && img->height > 0) {
                    unsigned w = (unsigned) img->width;
                    unsigned h = (unsigned) img->height;
                    while (w > 255 || h > 255) {
                        w /= 2;
                        h /= 2;
                    }
                    out->embed_w = (unsigned char) (w ? w : 1);
                    out->embed_h = (unsigned char) (h ? h : 1);
                }
                break;
            }
        }
    }

    if (out->embed_kind == INDIGO_EMBED_NONE && embed.has_external &&
        embed.external_uri) {
        out->embed_kind = INDIGO_EMBED_LINK;
        indigo_copy_utf8(out->embed_title, sizeof out->embed_title,
                         embed.external_title ? embed.external_title : "");
        indigo_copy_utf8(out->embed_uri, sizeof out->embed_uri, embed.external_uri);
        indigo_media_cdn_url(out->embed_thumb, sizeof out->embed_thumb, embed.external_thumb,
                             INDIGO_CDN_THUMBNAIL);
    }

    wf_post_embed_free(&embed);
}

/* False when the post cannot be shown at all (no URI). */
static bool
fill_post(const wf_agent_post_view *pv, indigo_post *out)
{
    wf_post_display d;

    memset(out, 0, sizeof *out);
    if (!pv->uri || !pv->cid || strlen(pv->uri) >= sizeof out->uri ||
        strlen(pv->cid) >= sizeof out->cid) {
        return false;
    }
    snprintf(out->uri, sizeof out->uri, "%s", pv->uri);
    snprintf(out->cid, sizeof out->cid, "%s", pv->cid);
    indigo_copy_utf8(out->handle, sizeof out->handle, pv->author.handle);
    indigo_copy_utf8(out->display_name, sizeof out->display_name, pv->author.display_name);
    indigo_media_cdn_url(out->avatar, sizeof out->avatar, pv->author.avatar, INDIGO_CDN_AVATAR);
    out->like_count = count_of(pv->like_count);
    out->repost_count = count_of(pv->repost_count);
    out->reply_count = count_of(pv->reply_count);
    if (pv->viewer.like && strlen(pv->viewer.like) < sizeof out->like_uri) {
        snprintf(out->like_uri, sizeof out->like_uri, "%s", pv->viewer.like);
    }
    if (pv->viewer.repost && strlen(pv->viewer.repost) < sizeof out->repost_uri) {
        snprintf(out->repost_uri, sizeof out->repost_uri, "%s", pv->viewer.repost);
    }

    if (wf_agent_post_view_display(pv, &d) == WF_OK) {
        indigo_copy_utf8(out->text, sizeof out->text, d.text);
        out->is_reply = d.is_reply != 0;
        {
            char note[256];

            embed_note(&d, note, sizeof note);
            indigo_copy_utf8(out->embed_note, sizeof out->embed_note, note);
        }
        fill_embed(&d, pv->embed, out);
        for (size_t i = 0; i < d.facet_count && out->facet_count < INDIGO_POST_FACETS_MAX; i++) {
            const wf_display_facet *f = &d.facets[i];

            if (f->byte_end > strlen(out->text)) {
                break; /* the text was truncated before this facet */
            }
            out->facets[out->facet_count].kind =
                f->kind == WF_FACET_MENTION ? INDIGO_FACET_MENTION
                : f->kind == WF_FACET_TAG   ? INDIGO_FACET_TAG
                                            : INDIGO_FACET_LINK;
            out->facets[out->facet_count].start = (unsigned) f->byte_start;
            out->facets[out->facet_count].end = (unsigned) f->byte_end;
            indigo_copy_utf8(out->facets[out->facet_count].target,
                             sizeof out->facets[out->facet_count].target,
                             f->target ? f->target : "");
            out->facet_count++;
        }
        wf_post_display_free(&d);
    }
    return true;
}

static bool
to_post(const wf_agent_feed_item *item, indigo_post *out)
{
    char *by = NULL;

    if (!fill_post(&item->post, out)) {
        return false;
    }
    if (wf_agent_feed_item_reposted_by(item, &by) == WF_OK && by) {
        indigo_copy_utf8(out->reposted_by, sizeof out->reposted_by, by);
        free(by);
    }
    return true;
}

/* Fetch the saved preferences once per sign-in. Failure is not fatal: the
 * feed is simply shown unfiltered, and the next page tries again. */
static void
ensure_prefs(void)
{
    wf_actor_preferences p;
    wf_status st;

    if (s_prefs_loaded || !s_agent) {
        return;
    }
    memset(&p, 0, sizeof p);
    st = wf_agent_get_actor_prefs_typed(s_agent, &p);
    if (st != WF_OK) {
        indigo_log_warn("getPreferences failed (%d); feed unfiltered", (int) st);
        indigo_prefs_clear(&s_prefs);
        return;
    }
    indigo_prefs_from_wolfram(&s_prefs, &p, indigo_time_now());
    wf_actor_preferences_free(&p);
    s_prefs_loaded = true;
}

static void
do_timeline(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_TIMELINE_FAILED};
    wf_agent_feed_list list;
    wf_status st;

    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    ensure_prefs();
    memset(&list, 0, sizeof list);
    st = wf_agent_get_timeline_typed(s_agent, INDIGO_PAGE_SIZE,
                                     j->cursor[0] ? j->cursor : NULL, &list);
    if (st != WF_OK) {
        ev.failure = classify(st);
        indigo_log_warn("timeline failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(ev.failure));
        publish_event(&ev);
        return;
    }

    s_page_count = 0;
    for (size_t i = 0; i < list.item_count && s_page_count < INDIGO_PAGE_SIZE; i++) {
        if (to_post(&list.items[i], &s_page[s_page_count])) {
            s_page_count++;
        }
    }
    s_page_count -= indigo_prefs_filter_page(&s_prefs, s_page, s_page_count, 0, true);
    ev.kind = INDIGO_SESSION_EVENT_TIMELINE_PAGE;
    ev.page_count = s_page_count;
    if (list.cursor && strlen(list.cursor) < sizeof ev.cursor) {
        snprintf(ev.cursor, sizeof ev.cursor, "%s", list.cursor);
    } else if (list.cursor) {
        indigo_log_warn("timeline cursor too long; paging stops here");
    }
    wf_agent_feed_list_free(&list);
    indigo_log_info("timeline: %u posts", s_page_count);
    publish_event(&ev);
}

static void
do_post_action(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_POST_ACTION_FAILED,
                               .action = j->action};
    wf_agent_post_result res = {0};
    wf_status st = WF_ERR_INVALID_ARG;

    snprintf(ev.post_uri, sizeof ev.post_uri, "%s", j->post_uri);
    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    switch (j->action) {
    case INDIGO_POST_ACTION_LIKE:
        st = wf_agent_like(s_agent, j->post_uri, j->post_cid, &res);
        break;
    case INDIGO_POST_ACTION_UNLIKE:
        st = wf_agent_unlike(s_agent, j->undo_uri);
        break;
    case INDIGO_POST_ACTION_REPOST:
        st = wf_agent_repost(s_agent, j->post_uri, j->post_cid, &res);
        break;
    case INDIGO_POST_ACTION_UNREPOST:
        st = wf_agent_delete_repost(s_agent, j->undo_uri);
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
        ev.failure = classify(st);
        indigo_log_warn("post action %d failed: wolfram status %d (%s)", (int) j->action,
                        (int) st, indigo_failure_tag(ev.failure));
    }
    wf_agent_post_result_free(&res);
    publish_event(&ev);
}

/* A thread post is a postView without the bookmark fields: borrow its
 * pointers (nothing here is freed) and reuse the one display path. */
static bool
thread_post_to_post(const wf_agent_thread_post *tp, unsigned depth, indigo_post *out)
{
    wf_agent_post_view pv;

    memset(&pv, 0, sizeof pv);
    pv.uri = tp->uri;
    pv.cid = tp->cid;
    pv.author = tp->author;
    pv.record = tp->record;
    pv.embed = tp->embed;
    pv.reply_count = tp->reply_count;
    pv.repost_count = tp->repost_count;
    pv.like_count = tp->like_count;
    pv.quote_count = tp->quote_count;
    pv.indexed_at = tp->indexed_at;
    pv.viewer.like = tp->viewer_like;
    pv.viewer.repost = tp->viewer_repost;
    if (!fill_post(&pv, out)) {
        return false;
    }
    out->depth = (unsigned char) (depth > 6 ? 6 : depth);
    return true;
}

static void
add_replies(const wf_agent_thread_node *n, unsigned depth)
{
    for (size_t i = 0; i < n->replies_count && s_page_count < INDIGO_THREAD_MAX; i++) {
        const wf_agent_thread_node *r = &n->replies[i];

        if (r->kind != WF_AGENT_THREAD_KIND_POST) {
            continue;
        }
        if (thread_post_to_post(&r->post, depth, &s_page[s_page_count])) {
            s_page_count++;
            add_replies(r, depth + 1);
        }
    }
}

static void
do_thread(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_THREAD_FAILED};
    wf_agent_thread thread;
    const wf_agent_thread_node *chain[INDIGO_THREAD_MAX];
    unsigned parents = 0;
    wf_status st;

    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    memset(&thread, 0, sizeof thread);
    st = wf_agent_get_post_thread_typed(s_agent, j->post_uri, 6, &thread);
    if (st != WF_OK) {
        ev.failure = classify(st);
        indigo_log_warn("thread failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(ev.failure));
        publish_event(&ev);
        return;
    }
    if (thread.root.kind != WF_AGENT_THREAD_KIND_POST) {
        wf_agent_thread_free(&thread);
        ev.failure = INDIGO_FAIL_BAD_RESPONSE;
        publish_event(&ev);
        return;
    }

    /* Oldest ancestor first. Ancestors that are blocked or gone end the chain. */
    for (const wf_agent_thread_node *p = thread.root.parent;
         p && p->kind == WF_AGENT_THREAD_KIND_POST && parents < 8; p = p->parent) {
        chain[parents++] = p;
    }
    s_page_count = 0;
    for (unsigned i = parents; i > 0; i--) {
        if (thread_post_to_post(&chain[i - 1]->post, parents - i, &s_page[s_page_count])) {
            s_page_count++;
        }
    }
    ev.focus = s_page_count;
    if (thread_post_to_post(&thread.root.post, parents, &s_page[s_page_count])) {
        s_page_count++;
    }
    add_replies(&thread.root, parents + 1);
    wf_agent_thread_free(&thread);

    ev.kind = INDIGO_SESSION_EVENT_THREAD_PAGE;
    ev.page_count = s_page_count;
    indigo_log_info("thread: %u posts", s_page_count);
    publish_event(&ev);
}

static void
do_profile(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_PROFILE_FAILED};
    wf_agent_profile p;
    wf_status st;

    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    memset(&p, 0, sizeof p);
    st = wf_agent_get_profile(s_agent, j->post_uri, &p);
    if (st != WF_OK) {
        ev.failure = classify(st);
        indigo_log_warn("profile failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(ev.failure));
        publish_event(&ev);
        return;
    }
    memset(&s_profile, 0, sizeof s_profile);
    indigo_copy_utf8(s_profile.handle, sizeof s_profile.handle, p.handle);
    indigo_copy_utf8(s_profile.display_name, sizeof s_profile.display_name, p.display_name);
    indigo_media_cdn_url(s_profile.avatar, sizeof s_profile.avatar, p.avatar, INDIGO_CDN_AVATAR);
    indigo_copy_utf8(s_profile.bio, sizeof s_profile.bio, p.description);
    indigo_copy_utf8(s_profile.did, sizeof s_profile.did, p.did);
    indigo_copy_utf8(s_profile.follow_uri, sizeof s_profile.follow_uri, p.following);
    indigo_copy_utf8(s_profile.block_uri, sizeof s_profile.block_uri, p.blocking);
    indigo_copy_utf8(s_profile.pinned_uri, sizeof s_profile.pinned_uri, p.pinned_post_uri);
    s_profile.muted = p.muted;
    s_profile.blocked = p.blocking != NULL;
    s_profile.followers = count_of(p.followers_count);
    s_profile.follows = count_of(p.follows_count);
    s_profile.posts = count_of(p.posts_count);
    s_profile.following = p.following != NULL;
    s_profile.loaded = true;
    wf_agent_profile_free(&p);
    ev.kind = INDIGO_SESSION_EVENT_PROFILE_LOADED;
    publish_event(&ev);
}

/* Every list of people -- search, followers, following, list members, mutes,
 * blocks -- arrives as the same Wolfram actor view, so they all fill the same
 * row through this one function rather than six copies of it. The avatar URL
 * rides along because the row has somewhere to show it. */
static bool
fill_actor(const wf_agent_profile_view *a, indigo_actor *o)
{
    if (!a->handle || !a->handle[0]) {
        /* A profile with no handle cannot be opened, and a row that does
         * nothing is worse than a missing one. */
        return false;
    }
    memset(o, 0, sizeof *o);
    indigo_copy_utf8(o->handle, sizeof o->handle, a->handle);
    indigo_copy_utf8(o->display_name, sizeof o->display_name,
                     a->display_name ? a->display_name : "");
    indigo_copy_utf8(o->did, sizeof o->did, a->did ? a->did : "");
    indigo_media_cdn_url(o->avatar, sizeof o->avatar, a->avatar, INDIGO_CDN_AVATAR);
    return true;
}

static indigo_note_kind
note_kind(const char *reason)
{
    if (!reason) {
        return INDIGO_NOTE_OTHER;
    }
    if (strcmp(reason, "like") == 0) {
        return INDIGO_NOTE_LIKE;
    }
    if (strcmp(reason, "repost") == 0) {
        return INDIGO_NOTE_REPOST;
    }
    if (strcmp(reason, "follow") == 0) {
        return INDIGO_NOTE_FOLLOW;
    }
    if (strcmp(reason, "reply") == 0) {
        return INDIGO_NOTE_REPLY;
    }
    if (strcmp(reason, "mention") == 0) {
        return INDIGO_NOTE_MENTION;
    }
    if (strcmp(reason, "quote") == 0) {
        return INDIGO_NOTE_QUOTE;
    }
    return INDIGO_NOTE_OTHER;
}

static void
do_search(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_agent_actor_list list;
    wf_status st;
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;

    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    if (!j->query[0]) {
        /* The app refuses to submit an empty query, so reaching this is a bug
         * rather than something a person did. */
        ev.failure = INDIGO_FAIL_OTHER;
        publish_event(&ev);
        return;
    }
    memset(&list, 0, sizeof list);
    st = wf_agent_search_actors_typed(s_agent, j->query, INDIGO_SEARCH_PAGE,
                                      cursor, &list);
    if (st != WF_OK) {
        ev.failure = classify(st);
        indigo_log_warn("search failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(ev.failure));
        publish_event(&ev);
        return;
    }

    if (!j->paging) {
        s_actor_count = 0;
    }
    for (size_t i = 0; i < list.actor_count && s_actor_count < INDIGO_SEARCH_MAX; i++) {
        const wf_agent_profile_view *a = &list.actors[i];

        if (fill_actor(a, &s_actors[s_actor_count])) {
            s_actor_count++;
        }
    }
    ev.kind = INDIGO_SESSION_EVENT_SEARCH_PAGE;
    ev.page_count = s_actor_count;
    indigo_copy_utf8(ev.cursor, sizeof ev.cursor, list.cursor ? list.cursor : "");
    wf_agent_actor_list_free(&list);
    indigo_log_info("search '%s': %u", j->query, s_actor_count);
    publish_event(&ev);
}

/* Followers and following return the same actor view as searchActors, and the
 * screen shows one list at a time, so the results land in the same array and
 * report through the same events. Only the request differs. */
static void
do_people(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_agent_actor_list list;
    wf_status st = WF_ERR_INVALID_ARG;
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;

    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    memset(&list, 0, sizeof list);
    if (j->people_kind == INDIGO_SEARCH_LIKED_BY) {
        wf_agent_like_list likes;

        memset(&likes, 0, sizeof likes);
        st = wf_feedgen_get_likes_typed(s_agent, j->list_uri, NULL, INDIGO_SEARCH_PAGE,
                                        cursor, &likes);
        if (st != WF_OK) {
            ev.failure = classify(st);
            indigo_log_warn("liked-by failed: wolfram status %d (%s)", (int) st,
                            indigo_failure_tag(ev.failure));
            publish_event(&ev);
            return;
        }
        if (!j->paging) {
            s_actor_count = 0;
        }
        for (size_t i = 0; i < likes.like_count && s_actor_count < INDIGO_SEARCH_MAX; i++) {
            if (fill_actor(&likes.likes[i].actor, &s_actors[s_actor_count])) {
                s_actor_count++;
            }
        }
        ev.kind = INDIGO_SESSION_EVENT_SEARCH_PAGE;
        ev.page_count = s_actor_count;
        indigo_copy_utf8(ev.cursor, sizeof ev.cursor, likes.cursor ? likes.cursor : "");
        wf_agent_like_list_free(&likes);
        indigo_log_info("liked-by: %u", s_actor_count);
        publish_event(&ev);
        return;
    }
    if (j->people_kind == INDIGO_SEARCH_REPOSTED_BY) {
        st = wf_feedgen_get_reposted_by_typed(s_agent, j->list_uri, NULL,
                                              INDIGO_SEARCH_PAGE, cursor, &list);
    } else if (j->people_kind == INDIGO_SEARCH_FOLLOWERS) {
        st = wf_agent_get_followers_typed(s_agent, j->actor, INDIGO_SEARCH_PAGE,
                                           cursor, &list);
    } else {
        st = wf_agent_get_follows_typed(s_agent, j->actor, INDIGO_SEARCH_PAGE,
                                         cursor, &list);
    }
    if (st != WF_OK) {
        ev.failure = classify(st);
        indigo_log_warn("people failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(ev.failure));
        publish_event(&ev);
        return;
    }

    if (!j->paging) {
        s_actor_count = 0;
    }
    for (size_t i = 0; i < list.actor_count && s_actor_count < INDIGO_SEARCH_MAX; i++) {
        const wf_agent_profile_view *a = &list.actors[i];

        if (fill_actor(a, &s_actors[s_actor_count])) {
            s_actor_count++;
        }
    }
    ev.kind = INDIGO_SESSION_EVENT_SEARCH_PAGE;
    ev.page_count = s_actor_count;
    indigo_copy_utf8(ev.cursor, sizeof ev.cursor, list.cursor ? list.cursor : "");
    wf_agent_actor_list_free(&list);
    indigo_log_info("people %d '%s': %u", (int) j->people_kind, j->actor, s_actor_count);
    publish_event(&ev);
}

static void
do_post_search(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_agent_post_list list;
    char *next = NULL;
    wf_status st;
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;

    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    memset(&list, 0, sizeof list);
    st = wf_agent_search_posts_typed(s_agent, j->query, INDIGO_SEARCH_PAGE,
                                      cursor, &list, &next);
    if (st != WF_OK) {
        ev.failure = classify(st);
        indigo_log_warn("post search failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(ev.failure));
        publish_event(&ev);
        return;
    }

    if (!j->paging) {
        s_post_count = 0;
    }
    for (size_t i = 0; i < list.post_count && s_post_count < INDIGO_SEARCH_MAX; i++) {
        if (fill_post(&list.posts[i], &s_posts[s_post_count])) {
            s_post_count++;
        }
    }
    ev.kind = INDIGO_SESSION_EVENT_POST_SEARCH_PAGE;
    ev.page_count = s_post_count;
    indigo_copy_utf8(ev.cursor, sizeof ev.cursor, next ? next : "");
    free(next);
    wf_agent_post_list_free(&list);
    indigo_log_info("post search '%s': %u", j->query, s_post_count);
    publish_event(&ev);
}

/* One person's posts. Same result type and same event as post search, so the
 * results land in the same array; only the request differs. to_post is reused
 * so reposts carry their "Reposted by" line here too. */
static void
do_author_feed(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_agent_feed_list list;
    wf_status st;
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;

    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    memset(&list, 0, sizeof list);
    st = wf_agent_get_author_feed_typed(s_agent, j->actor, INDIGO_SEARCH_PAGE, cursor,
                                        /* No filter: posts_with_replies would mix
                                         * replies into a person's own posts, and
                                         * replies_filter removes everything but
                                         * them. The bare call is their posts. */
                                        NULL, &list);
    if (st != WF_OK) {
        ev.failure = classify(st);
        indigo_log_warn("author feed failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(ev.failure));
        publish_event(&ev);
        return;
    }

    if (!j->paging) {
        s_post_count = 0;
    }
    for (size_t i = 0; i < list.item_count && s_post_count < INDIGO_SEARCH_MAX; i++) {
        if (to_post(&list.items[i], &s_posts[s_post_count])) {
            s_post_count++;
        }
    }
    ev.kind = INDIGO_SESSION_EVENT_POST_SEARCH_PAGE;
    ev.page_count = s_post_count;
    indigo_copy_utf8(ev.cursor, sizeof ev.cursor, list.cursor ? list.cursor : "");
    wf_agent_feed_list_free(&list);
    indigo_log_info("author feed '%s': %u", j->actor, s_post_count);
    publish_event(&ev);
}

/* The account's curated lists. getLists is paged; the cursor is dropped like
 * actor and post search, because a 3DS list that cannot show page two is not
 * worth paging. */
static void
do_lists(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_agent_list_view_list list;
    wf_status st;
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;

    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    /* The signed-in account's own lists: getLists needs an actor, and the
     * agent knows its own handle. */
    {
        const char *who = wf_agent_get_handle(s_agent);

        if (!who || !who[0]) {
            ev.failure = INDIGO_FAIL_NOT_READY;
            publish_event(&ev);
            return;
        }
        memset(&list, 0, sizeof list);
        st = wf_agent_get_lists_typed(s_agent, who, INDIGO_SEARCH_PAGE, cursor, &list);
    }
    if (st != WF_OK) {
        ev.failure = classify(st);
        indigo_log_warn("lists failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(ev.failure));
        publish_event(&ev);
        return;
    }

    if (!j->paging) {
        s_list_count = 0;
    }
    for (size_t i = 0; i < list.list_count && s_list_count < INDIGO_SEARCH_MAX; i++) {
        const wf_agent_list_view *l = &list.lists[i];
        indigo_list *o = &s_lists[s_list_count];

        if (!l->uri || !l->uri[0] || !l->name || !l->name[0]) {
            continue;
        }
        memset(o, 0, sizeof *o);
        indigo_copy_utf8(o->uri, sizeof o->uri, l->uri);
        indigo_copy_utf8(o->name, sizeof o->name, l->name);
        indigo_copy_utf8(o->description, sizeof o->description,
                         l->description ? l->description : "");
        s_list_count++;
    }
    ev.kind = INDIGO_SESSION_EVENT_LISTS_PAGE;
    ev.page_count = s_list_count;
    indigo_copy_utf8(ev.cursor, sizeof ev.cursor, list.cursor ? list.cursor : "");
    wf_agent_list_view_list_free(&list);
    indigo_log_info("lists: %u", s_list_count);
    publish_event(&ev);
}

/* One list's members. getList's items are listItemViews whose subjects are
 * profile views, so they land in the actor array through the same conversion
 * the followers list uses. */
static void
do_list_members(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    wf_agent_list_item_list list;
    wf_status st;
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;

    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    memset(&list, 0, sizeof list);
    st = wf_agent_get_list_typed(s_agent, j->list_uri, INDIGO_SEARCH_PAGE, cursor, &list);
    if (st != WF_OK) {
        ev.failure = classify(st);
        indigo_log_warn("list members failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(ev.failure));
        publish_event(&ev);
        return;
    }

    if (!j->paging) {
        s_actor_count = 0;
    }
    for (size_t i = 0; i < list.item_count && s_actor_count < INDIGO_SEARCH_MAX; i++) {
        const wf_agent_profile_view *a = &list.items[i].subject;

        if (fill_actor(a, &s_actors[s_actor_count])) {
            s_actor_count++;
        }
    }
    ev.kind = INDIGO_SESSION_EVENT_SEARCH_PAGE;
    ev.page_count = s_actor_count;
    indigo_copy_utf8(ev.cursor, sizeof ev.cursor, list.cursor ? list.cursor : "");
    wf_agent_list_item_list_free(&list);
    indigo_log_info("list members '%s': %u", j->list_uri, s_actor_count);
    publish_event(&ev);
}

/* The accounts this one has muted or blocked. Indigo could mute and block from
 * a profile but had no way to see the result, so a mis-click was invisible
 * until the person failed to appear somewhere else. Both endpoints return the
 * same actor-list shape as every other list of people, so they fill the same
 * rows and raise the same event; only the request differs. */
static void
do_moderation_list(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    const bool blocks = j->kind == JOB_BLOCKS;
    wf_agent_actor_list list;
    wf_status st;
    const char *cursor = j->paging && j->cursor[0] ? j->cursor : NULL;

    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    memset(&list, 0, sizeof list);
    if (blocks) {
        st = wf_agent_get_blocks_typed(s_agent, INDIGO_SEARCH_PAGE, cursor, &list);
    } else {
        st = wf_agent_get_mutes_typed(s_agent, INDIGO_SEARCH_PAGE, cursor, &list);
    }
    if (st != WF_OK) {
        ev.failure = classify(st);
        indigo_log_warn("%s list failed: wolfram status %d (%s)", blocks ? "blocked" : "muted",
                        (int) st, indigo_failure_tag(ev.failure));
        publish_event(&ev);
        return;
    }

    if (!j->paging) {
        s_actor_count = 0;
    }
    for (size_t i = 0; i < list.actor_count && s_actor_count < INDIGO_SEARCH_MAX; i++) {
        const wf_agent_profile_view *a = &list.actors[i];

        if (fill_actor(a, &s_actors[s_actor_count])) {
            s_actor_count++;
        }
    }
    ev.kind = INDIGO_SESSION_EVENT_SEARCH_PAGE;
    ev.page_count = s_actor_count;
    indigo_copy_utf8(ev.cursor, sizeof ev.cursor, list.cursor ? list.cursor : "");
    wf_agent_actor_list_free(&list);
    indigo_log_info("%s accounts: %u", blocks ? "blocked" : "muted", s_actor_count);
    publish_event(&ev);
}

/* The account's saved feeds. getPreferences carries the saved feed URIs (V2
 * first, the V1 list as the older fallback); getFeedGenerators turns them
 * into names. The raw preferences JSON is read rather than the typed parse
 * because one preference type the parser rejects must not take the whole
 * picker down with it -- Cobalt hit exactly that on a real account. */
static void
do_feeds(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_SEARCH_FAILED};
    char *prefs_json = NULL;
    const char *uris[INDIGO_SEARCH_MAX];
    unsigned n = 0;
    wf_status st;

    (void) j;
    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    st = wf_agent_get_preferences(s_agent, &prefs_json);
    if (st != WF_OK || !prefs_json) {
        ev.failure = classify(st);
        indigo_log_warn("preferences failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(ev.failure));
        publish_event(&ev);
        return;
    }
    {
        cJSON *prefs = cJSON_Parse(prefs_json);
        const cJSON *pref = NULL;

        free(prefs_json);
        if (!cJSON_IsArray(prefs)) {
            cJSON_Delete(prefs);
            ev.failure = INDIGO_FAIL_OTHER;
            indigo_log_warn("preferences: not an array");
            publish_event(&ev);
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

    s_feed_count = 0;
    if (n > 0) {
        wf_feedgen_generator_list gens;

        memset(&gens, 0, sizeof gens);
        st = wf_feedgen_get_feed_generators_typed(s_agent, uris, n, &gens);
        if (st != WF_OK) {
            /* The URIs alone still make a picker; the record key stands in
             * for the name, the same fallback Cobalt uses. */
            indigo_log_warn("feed generators failed: wolfram status %d", (int) st);
            memset(&gens, 0, sizeof gens);
        }
        for (unsigned i = 0; i < n; i++) {
            const char *name = NULL;
            indigo_list *o = &s_feeds[s_feed_count];

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
            s_feed_count++;
        }
        wf_feedgen_generator_list_free(&gens);
    }
    ev.kind = INDIGO_SESSION_EVENT_FEEDS_PAGE;
    ev.page_count = s_feed_count;
    indigo_log_info("feeds: %u", s_feed_count);
    publish_event(&ev);
}

/* One feed's posts. getFeed returns the same feedViewPost items the timeline
 * does, so the conversion is shared with do_timeline. */
static void
do_feed(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_TIMELINE_FAILED};
    wf_agent_feed_list list;
    wf_status st;

    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    ensure_prefs();
    memset(&list, 0, sizeof list);
    st = wf_agent_get_feed_typed(s_agent, j->feed_uri, INDIGO_PAGE_SIZE,
                                 j->cursor[0] ? j->cursor : NULL, &list);
    if (st != WF_OK) {
        ev.failure = classify(st);
        indigo_log_warn("feed failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(ev.failure));
        publish_event(&ev);
        return;
    }

    s_page_count = 0;
    for (size_t i = 0; i < list.item_count && s_page_count < INDIGO_PAGE_SIZE; i++) {
        if (to_post(&list.items[i], &s_page[s_page_count])) {
            s_page_count++;
        }
    }
    /* A custom feed is not the home timeline, so hide_reposts does not apply;
     * the muted words do, because they are about the content, not the feed. */
    s_page_count -= indigo_prefs_filter_page(&s_prefs, s_page, s_page_count, 0, false);
    ev.kind = INDIGO_SESSION_EVENT_TIMELINE_PAGE;
    ev.page_count = s_page_count;
    if (list.cursor && strlen(list.cursor) < sizeof ev.cursor) {
        snprintf(ev.cursor, sizeof ev.cursor, "%s", list.cursor);
    } else if (list.cursor) {
        indigo_log_warn("feed cursor too long; paging stops here");
    }
    wf_agent_feed_list_free(&list);
    indigo_log_info("feed '%s': %u posts", j->feed_uri, s_page_count);
    publish_event(&ev);
}

static void
do_follow(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_FOLLOW_FAILED,
                               .follow = j->follow};
    wf_agent_post_result res = {0};
    wf_status st = WF_ERR_INVALID_ARG;

    snprintf(ev.actor, sizeof ev.actor, "%s", j->actor);
    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    switch (j->follow) {
    case INDIGO_FOLLOW:
        st = wf_agent_follow(s_agent, j->actor, &res);
        break;
    case INDIGO_UNFOLLOW:
        if (!j->undo_uri[0]) {
            /* The app only offers unfollow while it holds the record URI, so
             * an empty one here is a bug rather than a person tapping it. */
            ev.failure = INDIGO_FAIL_OTHER;
            break;
        }
        st = wf_agent_unfollow(s_agent, j->undo_uri);
        break;
    case INDIGO_FOLLOW_NONE:
        break;
    }
    if (st == WF_OK) {
        ev.kind = INDIGO_SESSION_EVENT_FOLLOW_DONE;
        if (res.uri && strlen(res.uri) < sizeof ev.record_uri) {
            snprintf(ev.record_uri, sizeof ev.record_uri, "%s", res.uri);
        }
        /* The server owns the follower count; the app adjusts its copy from
         * here rather than assuming the write already landed. */
        if (j->follow == INDIGO_FOLLOW && s_profile.followers < UINT_MAX) {
            s_profile.followers++;
        } else if (j->follow == INDIGO_UNFOLLOW && s_profile.followers > 0) {
            s_profile.followers--;
        }
    } else {
        ev.failure = classify(st);
        indigo_log_warn("follow %d failed: wolfram status %d (%s)", (int) j->follow, (int) st,
                        indigo_failure_tag(ev.failure));
    }
    wf_agent_post_result_free(&res);
    publish_event(&ev);
}

static void
do_graph(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_GRAPH_FAILED, .graph = j->graph};
    wf_agent_post_result res = {0};
    wf_status st = WF_ERR_INVALID_ARG;

    snprintf(ev.actor, sizeof ev.actor, "%s", j->actor);
    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    switch (j->graph) {
    case INDIGO_GRAPH_MUTE:
        st = wf_agent_mute_actor(s_agent, j->actor);
        break;
    case INDIGO_GRAPH_UNMUTE:
        st = wf_agent_unmute_actor(s_agent, j->actor);
        break;
    case INDIGO_GRAPH_BLOCK:
        st = wf_agent_block(s_agent, j->actor, &res);
        break;
    case INDIGO_GRAPH_UNBLOCK:
        /* Block is a repo record, so unblocking deletes by URI and there is
         * no handle to resolve one from. */
        if (!j->undo_uri[0]) {
            ev.failure = INDIGO_FAIL_OTHER;
            break;
        }
        st = wf_agent_unblock(s_agent, j->undo_uri);
        break;
    case INDIGO_GRAPH_NONE:
        break;
    }
    if (st == WF_OK) {
        ev.kind = INDIGO_SESSION_EVENT_GRAPH_DONE;
        if (res.uri && strlen(res.uri) < sizeof ev.record_uri) {
            snprintf(ev.record_uri, sizeof ev.record_uri, "%s", res.uri);
        }
    } else {
        ev.failure = classify(st);
        indigo_log_warn("graph action %d failed: wolfram status %d (%s)", (int) j->graph,
                        (int) st, indigo_failure_tag(ev.failure));
    }
    wf_agent_post_result_free(&res);
    publish_event(&ev);
}

static void
do_notifications(void)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_NOTIFICATIONS_FAILED};
    wf_agent_notification_list list;
    wf_status st;

    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    memset(&list, 0, sizeof list);
    st = wf_agent_list_notifications_typed(s_agent, INDIGO_NOTIFICATION_MAX, NULL, &list);
    if (st != WF_OK) {
        ev.failure = classify(st);
        indigo_log_warn("notifications failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(ev.failure));
        publish_event(&ev);
        return;
    }

    s_note_count = 0;
    for (size_t i = 0; i < list.notification_count && s_note_count < INDIGO_NOTIFICATION_MAX;
         i++) {
        const wf_agent_notification *n = &list.notifications[i];
        indigo_notification *o = &s_notes[s_note_count];

        memset(o, 0, sizeof *o);
        o->kind = note_kind(n->reason);
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
        s_note_count++;
    }
    wf_agent_notification_list_free(&list);
    /* Mark everything up to now seen. This is always a top-of-list fetch --
     * notifications are never paged -- so unlike Cobalt there is no paging
     * case to exclude. A failure is logged, not surfaced: the notifications
     * arrived, and an error about a badge on another client is noise. */
    if (s_note_count > 0) {
        char seen_at[32];
        /* Nothing initialises a clock on the 3DS, so time() can return a
         * negative value. Sending "1970-01-01T00:00:00Z" as seenAt would be
         * worse than not marking anything, so the clock decides. */
        const long long now = indigo_time_now();

        if (now > 0 && wf_time_format_rfc3339((int64_t) now, seen_at, sizeof seen_at) == WF_OK) {
            if (wf_agent_update_seen_notifications(s_agent, seen_at) != WF_OK) {
                indigo_log_warn("updateSeen failed: the unread badge may linger on other "
                                "clients");
            }
        }
    }
    ev.kind = INDIGO_SESSION_EVENT_NOTIFICATIONS_PAGE;
    ev.page_count = s_note_count;
    indigo_log_info("notifications: %u", s_note_count);
    publish_event(&ev);
}

static void
do_publish(const job *j)
{
    indigo_session_event ev = {.kind = INDIGO_SESSION_EVENT_PUBLISH_FAILED,
                               .compose_mode = j->mode};
    wf_agent_post_result res = {0};
    wf_status st = WF_ERR_INVALID_ARG;

    if (!s_agent) {
        ev.failure = INDIGO_FAIL_NOT_READY;
        publish_event(&ev);
        return;
    }
    switch (j->mode) {
    case INDIGO_COMPOSE_POST:
        st = wf_agent_post(s_agent, j->text, &res);
        break;
    case INDIGO_COMPOSE_REPLY:
        st = wf_agent_reply_refs(s_agent, j->text, j->root_uri, j->root_cid, j->post_uri,
                                 j->post_cid, &res);
        break;
    case INDIGO_COMPOSE_QUOTE:
        st = wf_agent_quote(s_agent, j->text, j->post_uri, j->post_cid, &res);
        break;
    }
    if (st == WF_OK) {
        ev.kind = INDIGO_SESSION_EVENT_PUBLISHED;
        if (res.uri && strlen(res.uri) < sizeof ev.record_uri) {
            snprintf(ev.record_uri, sizeof ev.record_uri, "%s", res.uri);
        }
        indigo_log_info("published (mode %d)", (int) j->mode);
        /* Reply gate, new top-level posts only: a threadgate attaches to the
         * post it names, and gating a reply separately from its thread is not
         * something the official client offers either. Gate 0 writes nothing,
         * since no threadgate at all already means everyone may reply.
         *
         * A failure here does not roll the post back. It exists, ungated,
         * which is the safer outcome than dropping something the user can see
         * themselves have posted. */
        if (j->mode == INDIGO_COMPOSE_POST && j->reply_gate > 0 && res.uri && res.uri[0]) {
            /* Indexed by reply_gate: 1 allows follows and mentions, 2 is an
             * empty rule set, so nobody may reply. */
            static const char *const allow[] = {
                NULL,
                "[{\"$type\":\"app.bsky.feed.threadgate#followingRule\"},"
                "{\"$type\":\"app.bsky.feed.threadgate#mentionRule\"}]",
                "[]",
            };
            const int gate = j->reply_gate;

            if (gate < (int) (sizeof allow / sizeof allow[0])) {
                wf_agent_post_result gated = {0};
                wf_status gst =
                    wf_agent_create_threadgate(s_agent, res.uri, allow[gate], NULL, 0, &gated);

                if (gst != WF_OK) {
                    indigo_log_warn("reply gate failed (wolfram status %d) for %s", (int) gst,
                                    res.uri);
                } else {
                    indigo_log_info("reply gate set on %s", res.uri);
                }
                wf_agent_post_result_free(&gated);
            } else {
                indigo_log_warn("reply gate %d out of range, posted ungated", gate);
            }
        }
    } else {
        ev.failure = classify(st);
        indigo_log_warn("publish failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(ev.failure));
    }
    wf_agent_post_result_free(&res);
    publish_event(&ev);
}

static void
worker(void *arg)
{
    (void) arg;

    for (;;) {
        job j;

        LightSemaphore_Acquire(&s_wake, 1);
        if (s_quit) {
            break;
        }
        LightLock_Lock(&s_lock);
        j = s_job;
        memset(&s_job, 0, sizeof s_job);
        LightLock_Unlock(&s_lock);

        switch (j.kind) {
        case JOB_LOGIN:
            do_login(&j);
            break;
        case JOB_OAUTH:
            do_oauth(&j);
            break;
        case JOB_RESUME:
            do_resume();
            break;
        case JOB_LOGOUT:
            do_logout();
            break;
        case JOB_TIMELINE:
            do_timeline(&j);
            break;
        case JOB_POST_ACTION:
            do_post_action(&j);
            break;
        case JOB_THREAD:
            do_thread(&j);
            break;
        case JOB_PROFILE:
            do_profile(&j);
            break;
        case JOB_NOTIFICATIONS:
            do_notifications();
            break;
        case JOB_SEARCH:
            do_search(&j);
            break;
        case JOB_FOLLOW:
            do_follow(&j);
            break;
        case JOB_GRAPH:
            do_graph(&j);
            break;
        case JOB_PEOPLE:
            do_people(&j);
            break;
        case JOB_POST_SEARCH:
            do_post_search(&j);
            break;
        case JOB_AUTHOR_FEED:
            do_author_feed(&j);
            break;
        case JOB_LISTS:
            do_lists(&j);
            break;
        case JOB_LIST_MEMBERS:
            do_list_members(&j);
            break;
        case JOB_FEEDS:
            do_feeds(&j);
            break;
        case JOB_FEED:
            do_feed(&j);
            break;
        case JOB_MUTES:
        case JOB_BLOCKS:
            do_moderation_list(&j);
            break;
        case JOB_PUBLISH:
            do_publish(&j);
            break;
        case JOB_NONE:
            break;
        }
        memset(&j, 0, sizeof j);
    }
    drop_agent();
}

bool
indigo_session_start(const char *session_path)
{
    int32_t prio = 0;

    if (s_started) {
        return true;
    }
    snprintf(s_path, sizeof s_path, "%s", session_path);
    LightLock_Init(&s_lock);
    LightSemaphore_Init(&s_wake, 0, 8);
    s_quit = false;
    s_busy = false;
    s_event_ready = false;

    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    s_thread = threadCreate(worker, NULL, WORKER_STACK, prio + 1, -2, false);
    if (!s_thread) {
        s_thread = threadCreate(worker, NULL, WORKER_STACK, prio + 1, -1, false);
    }
    if (!s_thread) {
        indigo_log_error("could not start the network thread");
        return false;
    }
    s_started = true;
    return true;
}

void
indigo_session_stop(void)
{
    if (!s_started) {
        return;
    }
    s_quit = true;
    LightSemaphore_Release(&s_wake, 1);
    threadJoin(s_thread, U64_MAX);
    threadFree(s_thread);
    s_thread = NULL;
    s_started = false;
    memset(&s_job, 0, sizeof s_job);
    indigo_session_wipe(&s_saved);
}

static bool
submit(const job *j)
{
    bool ok = false;

    if (!s_started) {
        return false;
    }
    LightLock_Lock(&s_lock);
    /* An unpolled event (and the page behind it) must not be overwritten. */
    if (!s_busy && !s_event_ready) {
        s_busy = true;
        s_job = *j;
        ok = true;
    }
    LightLock_Unlock(&s_lock);
    if (ok) {
        LightSemaphore_Release(&s_wake, 1);
    }
    return ok;
}

bool
indigo_session_submit_login(const char *service, const char *identifier,
                            const char *password)
{
    job j = {.kind = JOB_LOGIN};
    bool ok;

    snprintf(j.service, sizeof j.service, "%s", service);
    snprintf(j.identifier, sizeof j.identifier, "%s", identifier);
    snprintf(j.password, sizeof j.password, "%s", password);
    ok = submit(&j);
    memset(&j, 0, sizeof j);
    return ok;
}

bool
indigo_session_submit_oauth(const char *oauth_node, const char *handle)
{
    if (!oauth_node || !oauth_node[0] || !handle || !handle[0]) {
        return false;
    }
    job j = {.kind = JOB_OAUTH};
    snprintf(j.service, sizeof j.service, "%s", oauth_node);
    snprintf(j.identifier, sizeof j.identifier, "%s", handle);
    return submit(&j);
}

const char *
indigo_session_pair_url(void)
{
    return s_pair_url;
}

const char *
indigo_session_pair_code(void)
{
    return s_pair_code;
}


bool
indigo_session_submit_resume(void)
{
    job j = {.kind = JOB_RESUME};

    return submit(&j);
}

bool
indigo_session_submit_logout(void)
{
    job j = {.kind = JOB_LOGOUT};

    return submit(&j);
}

bool
indigo_session_submit_timeline(const char *cursor)
{
    job j = {.kind = JOB_TIMELINE};

    if (cursor) {
        snprintf(j.cursor, sizeof j.cursor, "%s", cursor);
    }
    return submit(&j);
}

bool
indigo_session_submit_post_action(indigo_post_action action, const char *post_uri,
                                  const char *post_cid, const char *undo_uri)
{
    job j = {.kind = JOB_POST_ACTION, .action = action};

    snprintf(j.post_uri, sizeof j.post_uri, "%s", post_uri);
    snprintf(j.post_cid, sizeof j.post_cid, "%s", post_cid);
    snprintf(j.undo_uri, sizeof j.undo_uri, "%s", undo_uri);
    return submit(&j);
}

bool
indigo_session_submit_thread(const char *uri)
{
    job j = {.kind = JOB_THREAD};

    snprintf(j.post_uri, sizeof j.post_uri, "%s", uri);
    return submit(&j);
}

bool
indigo_session_submit_profile(const char *actor)
{
    job j = {.kind = JOB_PROFILE};

    snprintf(j.post_uri, sizeof j.post_uri, "%s", actor);
    return submit(&j);
}

bool
indigo_session_submit_notifications(void)
{
    job j = {.kind = JOB_NOTIFICATIONS};

    return submit(&j);
}

bool
indigo_session_submit_publish(indigo_compose_mode mode, const char *text,
                              const char *target_uri, const char *target_cid,
                              const char *root_uri, const char *root_cid, int reply_gate)
{
    job j = {.kind = JOB_PUBLISH, .mode = mode, .reply_gate = reply_gate};

    snprintf(j.text, sizeof j.text, "%s", text);
    snprintf(j.post_uri, sizeof j.post_uri, "%s", target_uri ? target_uri : "");
    snprintf(j.post_cid, sizeof j.post_cid, "%s", target_cid ? target_cid : "");
    snprintf(j.root_uri, sizeof j.root_uri, "%s", root_uri ? root_uri : "");
    snprintf(j.root_cid, sizeof j.root_cid, "%s", root_cid ? root_cid : "");
    return submit(&j);
}

const indigo_profile *
indigo_session_profile(void)
{
    return &s_profile;
}

bool
indigo_session_submit_search(const char *query, bool paging)
{
    job j = {.kind = JOB_SEARCH};

    if (!query) {
        return false;
    }
    j.paging = paging;
    indigo_copy_utf8(j.query, sizeof j.query, query);
    return submit(&j);
}

bool
indigo_session_submit_post_search(const char *query, bool paging)
{
    job j = {.kind = JOB_POST_SEARCH};

    if (!query) {
        return false;
    }
    j.paging = paging;
    indigo_copy_utf8(j.query, sizeof j.query, query);
    return submit(&j);
}

bool
indigo_session_submit_author_feed(const char *actor, bool paging)
{
    job j = {.kind = JOB_AUTHOR_FEED};

    if (!actor || !actor[0]) {
        return false;
    }
    j.paging = paging;
    indigo_copy_utf8(j.actor, sizeof j.actor, actor);
    return submit(&j);
}

bool
indigo_session_submit_lists(bool paging)
{
    job j = {.kind = JOB_LISTS};

    j.paging = paging;
    return submit(&j);
}

bool
indigo_session_submit_list_members(const char *list_uri, bool paging)
{
    job j = {.kind = JOB_LIST_MEMBERS};

    if (!list_uri || !list_uri[0]) {
        return false;
    }
    j.paging = paging;
    indigo_copy_utf8(j.list_uri, sizeof j.list_uri, list_uri);
    return submit(&j);
}

bool
indigo_session_submit_feeds(void)
{
    job j = {.kind = JOB_FEEDS};

    return submit(&j);
}

bool
indigo_session_submit_mutes(bool paging)
{
    job j = {.kind = JOB_MUTES};

    j.paging = paging;
    return submit(&j);
}

bool
indigo_session_submit_blocks(bool paging)
{
    job j = {.kind = JOB_BLOCKS};

    j.paging = paging;
    return submit(&j);
}

bool
indigo_session_submit_feed(const char *feed_uri, const char *cursor)
{
    job j = {.kind = JOB_FEED};

    if (!feed_uri || !feed_uri[0]) {
        return false;
    }
    indigo_copy_utf8(j.feed_uri, sizeof j.feed_uri, feed_uri);
    if (cursor) {
        snprintf(j.cursor, sizeof j.cursor, "%s", cursor);
    }
    return submit(&j);
}

bool
indigo_session_submit_follow(indigo_follow_action action, const char *did,
                             const char *follow_uri)
{
    job j = {.kind = JOB_FOLLOW, .follow = action};

    if (action == INDIGO_FOLLOW_NONE || !did || !did[0]) {
        return false;
    }
    if (action == INDIGO_UNFOLLOW && (!follow_uri || !follow_uri[0])) {
        return false;
    }
    indigo_copy_utf8(j.actor, sizeof j.actor, did);
    indigo_copy_utf8(j.undo_uri, sizeof j.undo_uri, follow_uri ? follow_uri : "");
    return submit(&j);
}

bool
indigo_session_submit_graph(indigo_graph_action action, const char *did,
                            const char *block_uri)
{
    job j = {.kind = JOB_GRAPH, .graph = action};

    if (action == INDIGO_GRAPH_NONE || !did || !did[0]) {
        return false;
    }
    if (action == INDIGO_GRAPH_UNBLOCK && (!block_uri || !block_uri[0])) {
        return false;
    }
    indigo_copy_utf8(j.actor, sizeof j.actor, did);
    indigo_copy_utf8(j.undo_uri, sizeof j.undo_uri, block_uri ? block_uri : "");
    return submit(&j);
}

bool
indigo_session_submit_people(indigo_search_kind kind, const char *subject, bool paging)
{
    job j = {.kind = JOB_PEOPLE, .people_kind = kind};

    if (kind == INDIGO_SEARCH_PEOPLE || !subject || !subject[0]) {
        return false;
    }
    j.paging = paging;
    /* A post's URI is longer than a handle, so the two kinds that take one
     * keep it in the URI-sized field rather than truncating it into actor. */
    if (kind == INDIGO_SEARCH_LIKED_BY || kind == INDIGO_SEARCH_REPOSTED_BY) {
        indigo_copy_utf8(j.list_uri, sizeof j.list_uri, subject);
    } else {
        indigo_copy_utf8(j.actor, sizeof j.actor, subject);
    }
    return submit(&j);
}

void
indigo_session_post_search_results(const indigo_post **posts, unsigned *count)
{
    if (posts) {
        *posts = s_posts;
    }
    if (count) {
        *count = s_post_count;
    }
}

void
indigo_session_lists_results(const indigo_list **lists, unsigned *count)
{
    if (lists) {
        *lists = s_lists;
    }
    if (count) {
        *count = s_list_count;
    }
}

void
indigo_session_feeds_results(const indigo_list **feeds, unsigned *count)
{
    if (feeds) {
        *feeds = s_feeds;
    }
    if (count) {
        *count = s_feed_count;
    }
}

const indigo_actor *
indigo_session_search_results(unsigned *count)
{
    *count = s_actor_count;
    return s_actors;
}

const indigo_notification *
indigo_session_notifications(unsigned *count)
{
    *count = s_note_count;
    return s_notes;
}

bool
indigo_session_busy(void)
{
    bool busy;

    if (!s_started) {
        return true;
    }
    LightLock_Lock(&s_lock);
    busy = s_busy || s_event_ready;
    LightLock_Unlock(&s_lock);
    return busy;
}

const indigo_post *
indigo_session_page(unsigned *count)
{
    *count = s_page_count;
    return s_page;
}

bool
indigo_session_poll(indigo_session_event *out)
{
    bool ready = false;

    if (!s_started) {
        return false;
    }
    LightLock_Lock(&s_lock);
    if (s_event_ready) {
        *out = s_event;
        s_event_ready = false;
        ready = true;
    }
    LightLock_Unlock(&s_lock);
    return ready;
}

bool
indigo_session_has_saved(void)
{
    FILE *f = fopen(s_path, "rb");

    if (!f) {
        return false;
    }
    fclose(f);
    return true;
}

#else /* no 3DS or no Wolfram: nothing can reach the network */

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
    s_stub = (indigo_session_event) {.kind = kind, .failure = INDIGO_FAIL_NOT_READY};
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
indigo_session_submit_publish(indigo_compose_mode mode, const char *text,
                              const char *target_uri, const char *target_cid,
                              const char *root_uri, const char *root_cid, int reply_gate)
{
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

bool
indigo_session_submit_author_feed(const char *actor, bool paging)
{
    (void) actor;
    (void) paging;
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

#endif
