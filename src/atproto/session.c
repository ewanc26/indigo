#include "atproto/session.h"

#include "store/session_store.h"
#include "util/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__3DS__) && defined(WOLFRAM_3DS)

#include <3ds.h>
#include <wolfram/3ds.h>
#include <wolfram/agent.h>
#include <wolfram/post_display.h>

#define CA_BUNDLE_PATH "romfs:/cacert.pem"
#define WORKER_STACK 0x20000

typedef enum {
    JOB_NONE = 0,
    JOB_LOGIN,
    JOB_RESUME,
    JOB_LOGOUT,
    JOB_TIMELINE,
    JOB_POST_ACTION,
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

/* Written by the worker before it publishes TIMELINE_PAGE; the main thread
 * reads it after polling that event and before the next submit. */
static indigo_post s_page[INDIGO_PAGE_SIZE];
static unsigned s_page_count;

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
}

static wf_agent *
new_agent(const char *service)
{
    wf_agent *a = wf_agent_new(service);

    if (!a) {
        return NULL;
    }
    if (wf_agent_set_ca_bundle(a, CA_BUNDLE_PATH) != WF_OK) {
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
    wf_session_data data;
    wf_status st;
    char *json = NULL;

    memset(&s_saved, 0, sizeof s_saved);
    if (wf_agent_get_session_data(s_agent, &data) != WF_OK) {
        indigo_log_warn("session data unavailable; sign-in will not persist");
        return;
    }
    snprintf(s_saved.service, sizeof s_saved.service, "%s", service);
    st = wf_session_data_to_json(&data, &json);
    wf_agent_session_data_free(&data);
    if (st != WF_OK || !json || strlen(json) >= sizeof s_saved.session) {
        indigo_log_warn("session too large or unserialisable; sign-in will not persist");
        wipe_json(json);
        indigo_session_wipe(&s_saved);
        return;
    }
    memcpy(s_saved.session, json, strlen(json) + 1);
    wipe_json(json);

    if (indigo_session_store_save(s_path, &s_saved) != INDIGO_STORE_OK) {
        indigo_log_warn("could not save the session; you will sign in again next launch");
    } else {
        indigo_log_info("session saved");
    }
}

static void
do_login(const job *j)
{
    wf_status st;
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
    wf_session_data data;
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
        /* Confirms the tokens still work; refreshes them if they expired. */
        st = wf_agent_get_session(s_agent);
    }
    if (st != WF_OK) {
        indigo_failure f = classify(st);

        indigo_log_warn("resume failed: wolfram status %d (%s)", (int) st,
                        indigo_failure_tag(f));
        drop_agent();
        indigo_session_wipe(&s_saved);
        /* Offline is not a reason to forget the session; keep the file. */
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

/* False when the item cannot be shown at all (no URI). */
static bool
to_post(const wf_agent_feed_item *item, indigo_post *out)
{
    const wf_agent_post_view *pv = &item->post;
    wf_post_display d;
    char *by = NULL;

    memset(out, 0, sizeof *out);
    if (!pv->uri || !pv->cid || strlen(pv->uri) >= sizeof out->uri ||
        strlen(pv->cid) >= sizeof out->cid) {
        return false;
    }
    snprintf(out->uri, sizeof out->uri, "%s", pv->uri);
    snprintf(out->cid, sizeof out->cid, "%s", pv->cid);
    indigo_copy_utf8(out->handle, sizeof out->handle, pv->author.handle);
    indigo_copy_utf8(out->display_name, sizeof out->display_name, pv->author.display_name);
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
        for (size_t i = 0; i < d.facet_count && out->facet_count < INDIGO_POST_FACETS_MAX; i++) {
            const wf_post_facet *f = &d.facets[i];

            if (f->byte_end > strlen(out->text)) {
                break; /* the text was truncated before this facet */
            }
            out->facets[out->facet_count].kind =
                f->kind == WF_FACET_MENTION ? INDIGO_FACET_MENTION
                : f->kind == WF_FACET_TAG   ? INDIGO_FACET_TAG
                                            : INDIGO_FACET_LINK;
            out->facets[out->facet_count].start = (unsigned) f->byte_start;
            out->facets[out->facet_count].end = (unsigned) f->byte_end;
            out->facet_count++;
        }
        wf_post_display_free(&d);
    }
    if (wf_agent_feed_item_reposted_by(item, &by) == WF_OK && by) {
        indigo_copy_utf8(out->reposted_by, sizeof out->reposted_by, by);
        free(by);
    }
    return true;
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
