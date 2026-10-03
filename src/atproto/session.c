#include "atproto/session.h"

#include "store/session_store.h"
#include "util/log.h"

#include <stdio.h>
#include <string.h>

#if defined(__3DS__) && defined(WOLFRAM_3DS)

#include <3ds.h>
#include <wolfram/3ds.h>
#include <wolfram/agent.h>

#define CA_BUNDLE_PATH "romfs:/cacert.pem"
#define WORKER_STACK 0x20000

typedef enum { JOB_NONE = 0, JOB_LOGIN, JOB_RESUME, JOB_LOGOUT } job_kind;

typedef struct {
    job_kind kind;
    char service[256];
    char identifier[256];
    char password[128];
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

static indigo_failure
classify(wf_status st, const char *server_message)
{
    if (server_message && server_message[0]) {
        if (strstr(server_message, "RateLimit") || strstr(server_message, "rate limit")) {
            return INDIGO_FAIL_RATE_LIMIT;
        }
        /* The server answered and refused: the credentials are the likely cause. */
        return INDIGO_FAIL_BAD_CREDENTIALS;
    }

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

static void
remember(const char *service)
{
    wf_session_data data;

    memset(&s_saved, 0, sizeof s_saved);
    if (wf_agent_get_session_data(s_agent, &data) != WF_OK) {
        indigo_log_warn("session data unavailable; sign-in will not persist");
        return;
    }
    snprintf(s_saved.service, sizeof s_saved.service, "%s", service);
    snprintf(s_saved.handle, sizeof s_saved.handle, "%s", data.handle ? data.handle : "");
    snprintf(s_saved.did, sizeof s_saved.did, "%s", data.did ? data.did : "");
    snprintf(s_saved.pds_url, sizeof s_saved.pds_url, "%s", data.pds_url ? data.pds_url : "");
    snprintf(s_saved.access_jwt, sizeof s_saved.access_jwt, "%s", data.access_jwt ? data.access_jwt : "");
    snprintf(s_saved.refresh_jwt, sizeof s_saved.refresh_jwt, "%s", data.refresh_jwt ? data.refresh_jwt : "");
    wf_agent_session_data_free(&data);

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
        indigo_failure f = classify(st, wf_agent_last_error(s_agent));

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

    memset(&data, 0, sizeof data);
    data.access_jwt = s_saved.access_jwt;
    data.refresh_jwt = s_saved.refresh_jwt;
    data.handle = s_saved.handle;
    data.did = s_saved.did;
    data.pds_url = s_saved.pds_url[0] ? s_saved.pds_url : NULL;
    data.email_confirmed = -1;
    data.email_auth_factor = -1;
    data.active = -1;

    indigo_log_info("resuming saved session for %s", s_saved.handle);
    st = wf_agent_resume(s_agent, &data);
    if (st == WF_OK) {
        /* Confirms the tokens still work; refreshes them if they expired. */
        st = wf_agent_get_session(s_agent);
    }
    if (st != WF_OK) {
        indigo_failure f = classify(st, wf_agent_last_error(s_agent));

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
    if (!s_busy) {
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
indigo_session_has_saved(void)
{
    return false;
}

#endif
