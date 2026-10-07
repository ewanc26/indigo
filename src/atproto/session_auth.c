/* Signing in, resuming and signing out, and the Wolfram agent they build and drop. */

#include "atproto/session_internal.h"

#include "app/app.h"

#if defined(__3DS__) && defined(WOLFRAM_3DS)

void
indigo_session_drop_agent(void)
{
    if (g_session.agent) {
        wf_agent_free(g_session.agent);
        g_session.agent = NULL;
    }
    g_session.prefs_loaded = false;
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

    memset(&g_session.saved, 0, sizeof g_session.saved);
    if (wf_agent_get_session_data(g_session.agent, &data) != WF_OK) {
        indigo_log_warn("session data unavailable; sign-in will not persist");
        return;
    }
    snprintf(g_session.saved.service, sizeof g_session.saved.service, "%s", service);

    if (g_session.node_session) {
        cJSON *root = cJSON_CreateObject();
        if (!root ||
            !cJSON_AddStringToObject(root, "kind", "oauth-node") ||
            !cJSON_AddStringToObject(root, "accessJwt", data.access_jwt) ||
            !cJSON_AddStringToObject(root, "handle", data.handle) ||
            !cJSON_AddStringToObject(root, "did", data.did)) {
            cJSON_Delete(root);
            wf_agent_session_data_free(&data);
            indigo_session_wipe(&g_session.saved);
            indigo_log_warn("could not encode OAuth-node session");
            return;
        }
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        wf_agent_session_data_free(&data);
        if (!json || strlen(json) >= sizeof g_session.saved.session) {
            indigo_log_warn("OAuth-node session too large; sign-in will not persist");
            wipe_json(json);
            indigo_session_wipe(&g_session.saved);
            return;
        }
        memcpy(g_session.saved.session, json, strlen(json) + 1);
        wipe_json(json);
    } else {
        char *json = NULL;
        st = wf_session_data_to_json(&data, &json);
        wf_agent_session_data_free(&data);
        if (st != WF_OK || !json || strlen(json) >= sizeof g_session.saved.session) {
            wipe_json(json);
            indigo_log_warn("session too large or unserialisable; sign-in will not persist");
            indigo_session_wipe(&g_session.saved);
            return;
        }
        memcpy(g_session.saved.session, json, strlen(json) + 1);
        wipe_json(json);
    }

    if (indigo_session_store_save(g_session.path, &g_session.saved) != INDIGO_STORE_OK) {
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
    LightLock_Lock(&g_session.lock);
    snprintf(g_session.pair_code, sizeof g_session.pair_code, "%s", begin->pair_code);
    snprintf(g_session.pair_url, sizeof g_session.pair_url, "%s", begin->pair_url);
    LightLock_Unlock(&g_session.lock);
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
    LightLock_Lock(&g_session.lock);
    g_session.pair_code[0] = '\0';
    g_session.pair_url[0] = '\0';
    LightLock_Unlock(&g_session.lock);
}

void
indigo_session_do_oauth(const indigo_job *j)
{
    static wf_oauth_pair_poll result;
    wf_oauth_pair_hooks hooks = {pair_on_code, NULL, pair_sleep, NULL, NULL};
    wf_xrpc_client *client = wf_xrpc_client_new(j->service);
    wf_status st;

    if (!client) {
        indigo_session_publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, WF_FAIL_TLS, NULL);
        return;
    }
    wf_xrpc_client_set_ca_bundle(client, INDIGO_CA_BUNDLE_PATH);

    st = wf_oauth_pair_run(client, j->identifier, &hooks, &result);
    wf_xrpc_client_free(client);
    clear_pairing();

    if (st == WF_OK) {
        indigo_session_drop_agent();
        g_session.agent = new_agent(result.service);
        if (g_session.agent &&
            wf_agent_set_bearer(g_session.agent, result.token, result.handle, result.did) == WF_OK) {
            char handle[WF_OAUTH_PAIR_HANDLE_MAX];

            snprintf(handle, sizeof handle, "%s", result.handle);
            g_session.node_session = true;
            remember(result.service);
            wf_oauth_pair_poll_wipe(&result);
            indigo_session_publish(INDIGO_SESSION_EVENT_SIGNED_IN, WF_FAIL_NONE, handle);
            return;
        }
        indigo_session_drop_agent();
        wf_oauth_pair_poll_wipe(&result);
        indigo_session_publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, WF_FAIL_OTHER, NULL);
        return;
    }
    if (st == WF_ERR_AUTH) {
        /* The node's own message, e.g. "This pairing request expired." It is
         * written for a person and carries no credential. */
        indigo_log_warn("OAuth sign-in failed: %s",
                        result.message[0] ? result.message : "unknown");
        wf_oauth_pair_poll_wipe(&result);
        indigo_session_publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, WF_FAIL_SERVER, NULL);
        return;
    }
    wf_oauth_pair_poll_wipe(&result);
    indigo_session_publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED,
            wf_failure_classify(st, 0, NULL), NULL);
}

void
indigo_session_do_login(const indigo_job *j)
{
    wf_status st;
    g_session.node_session = false;
    const char *who;
    char *pds = NULL;

    indigo_session_drop_agent();
    g_session.agent = new_agent(j->service);
    if (!g_session.agent) {
        indigo_session_publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, WF_FAIL_TLS, NULL);
        return;
    }

    /* The handle or DID resolves to the account's PDS, which the agent is
     * pointed at before the login is sent. The user never types that host. */
    indigo_log_info("sign-in: contacting %s", j->service);
    st = wf_agent_login_discovered(g_session.agent, j->identifier, j->password, &pds);
    if (st != WF_OK) {
        wf_failure_kind f = wf_failure_classify(st, 0, NULL);

        indigo_log_warn("sign-in failed: wolfram status %d (%s)", (int) st,
                        wf_failure_tag(f));
        free(pds);
        indigo_session_drop_agent();
        indigo_session_publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, f, NULL);
        return;
    }

    /* The saved session keeps the discovered PDS, so a resume goes straight
     * there. Fall back to the starting host only if none was reported. */
    remember(pds ? pds : j->service);
    free(pds);
    who = wf_agent_get_handle(g_session.agent);
    indigo_log_info("signed in as %s", who ? who : "(unknown)");
    indigo_session_publish(INDIGO_SESSION_EVENT_SIGNED_IN, WF_FAIL_NONE, who);
}

void
indigo_session_do_resume(void)
{
    indigo_store_status ss = indigo_session_store_load(g_session.path, &g_session.saved);
    wf_session_data data = {0};
    wf_status st;
    const char *who;

    if (ss == INDIGO_STORE_MISSING) {
        indigo_session_publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, WF_FAIL_NONE, NULL);
        return;
    }
    if (ss != INDIGO_STORE_OK) {
        indigo_log_warn("saved session unreadable; kept as .bad");
        indigo_session_publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, WF_FAIL_NONE, NULL);
        return;
    }

    indigo_session_drop_agent();
    g_session.agent = new_agent(g_session.saved.service);
    if (!g_session.agent) {
        indigo_session_wipe(&g_session.saved);
        indigo_session_publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, WF_FAIL_TLS, NULL);
        return;
    }

    {
        cJSON *saved = cJSON_Parse(g_session.saved.session);
        const cJSON *kind = saved ? cJSON_GetObjectItemCaseSensitive(saved, "kind") : NULL;
        if (kind && cJSON_IsString(kind) && !strcmp(kind->valuestring, "oauth-node")) {
            const cJSON *token = cJSON_GetObjectItemCaseSensitive(saved, "accessJwt");
            const cJSON *handle = cJSON_GetObjectItemCaseSensitive(saved, "handle");
            const cJSON *did = cJSON_GetObjectItemCaseSensitive(saved, "did");
            if (!cJSON_IsString(token) || !cJSON_IsString(handle) ||
                !cJSON_IsString(did) ||
                wf_agent_set_bearer(g_session.agent, token->valuestring,
                                     handle->valuestring, did->valuestring) != WF_OK) {
                cJSON_Delete(saved);
                indigo_session_drop_agent();
                indigo_session_wipe(&g_session.saved);
                indigo_session_store_clear(g_session.path);
                indigo_session_publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, WF_FAIL_BAD_CREDENTIALS, NULL);
                return;
            }
            g_session.node_session = true;
            cJSON_Delete(saved);
            who = wf_agent_get_handle(g_session.agent);
            indigo_session_publish(INDIGO_SESSION_EVENT_SIGNED_IN, WF_FAIL_NONE, who);
            return;
        }
        cJSON_Delete(saved);
    }

    g_session.node_session = false;
    st = wf_session_data_from_json(g_session.saved.session, strlen(g_session.saved.session), &data);
    if (st != WF_OK) {
        indigo_log_warn("saved session unreadable; discarded");
        indigo_session_drop_agent();
        indigo_session_wipe(&g_session.saved);
        indigo_session_store_clear(g_session.path);
        indigo_session_publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, WF_FAIL_NONE, NULL);
        return;
    }

    indigo_log_info("resuming saved session for %s", data.handle);
    st = wf_agent_resume(g_session.agent, &data);
    wf_agent_session_data_free(&data);
    if (st == WF_OK) {
        st = wf_agent_get_session(g_session.agent);
    }
    if (st != WF_OK) {
        wf_failure_kind f = wf_failure_classify(st, 0, NULL);
        indigo_log_warn("resume failed: wolfram status %d (%s)", (int) st, wf_failure_tag(f));
        indigo_session_drop_agent();
        indigo_session_wipe(&g_session.saved);
        if (f == WF_FAIL_BAD_CREDENTIALS) {
            indigo_session_store_clear(g_session.path);
        }
        indigo_session_publish(INDIGO_SESSION_EVENT_SIGN_IN_FAILED, f, NULL);
        return;
    }

    {
        char service[256];
        snprintf(service, sizeof service, "%s", g_session.saved.service);
        indigo_session_wipe(&g_session.saved);
        remember(service);
    }
    who = wf_agent_get_handle(g_session.agent);
    indigo_log_info("resumed session for %s", who ? who : "(unknown)");
    indigo_session_publish(INDIGO_SESSION_EVENT_SIGNED_IN, WF_FAIL_NONE, who);
}

void
indigo_session_do_logout(void)
{
    if (g_session.agent) {
        /* Best effort: the local session is cleared whatever the server says. */
        (void) wf_agent_logout(g_session.agent);
        indigo_session_drop_agent();
    }
    indigo_session_store_clear(g_session.path);
    indigo_log_info("signed out");
    indigo_session_publish(INDIGO_SESSION_EVENT_SIGNED_OUT, WF_FAIL_NONE, NULL);
}

bool
indigo_session_submit_login(const char *service, const char *identifier,
                            const char *password)
{
    indigo_job j = {.kind = JOB_LOGIN};
    bool ok;

    /* The password path starts from the default host when no Service is set;
     * the login then moves to the account's own PDS. */
    snprintf(j.service, sizeof j.service, "%s",
             service && service[0] ? service : INDIGO_DEFAULT_SERVICE);
    snprintf(j.identifier, sizeof j.identifier, "%s", identifier);
    snprintf(j.password, sizeof j.password, "%s", password);
    ok = indigo_session_enqueue(&j);
    memset(&j, 0, sizeof j);
    return ok;
}

bool
indigo_session_submit_oauth(const char *oauth_node, const char *handle)
{
    if (!oauth_node || !oauth_node[0] || !handle || !handle[0]) {
        return false;
    }
    indigo_job j = {.kind = JOB_OAUTH};
    snprintf(j.service, sizeof j.service, "%s", oauth_node);
    snprintf(j.identifier, sizeof j.identifier, "%s", handle);
    return indigo_session_enqueue(&j);
}

const char *
indigo_session_pair_url(void)
{
    return g_session.pair_url;
}

const char *
indigo_session_pair_code(void)
{
    return g_session.pair_code;
}

bool
indigo_session_submit_resume(void)
{
    indigo_job j = {.kind = JOB_RESUME};

    return indigo_session_enqueue(&j);
}

bool
indigo_session_submit_logout(void)
{
    indigo_job j = {.kind = JOB_LOGOUT};

    return indigo_session_enqueue(&j);
}

#endif /* 3DS with Wolfram */
