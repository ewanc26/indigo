/* Runs Indigo's real request-list paging (session_read.c) against a local server.
 *
 * Built for the host with the 3DS guard set (see tests/live/paging-live.mk). The session
 * code is single-threaded here (the libctru locks are no-ops in ctr_noop.c), and the
 * server is an MHD daemon that records the query string, so the cursor is checked on the
 * wire. The link tolerates the other session modules this path never calls; a change that
 * makes it call one will fail at run time, not link time. */
#include <stdio.h>
#include <string.h>
#include <microhttpd.h>
#include "atproto/session_internal.h"

extern indigo_session_state g_session;
void indigo_session_do_chat_requests(const char *cursor);

static int failures = 0;
static int checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static char last_query[512];
static int requests_seen = 0;

#define PAGE1 "{\"requests\":[{\"id\":\"c1\",\"members\":[{\"did\":\"did:plc:me\"},{\"did\":\"did:plc:alice\",\"handle\":\"alice.test\",\"displayName\":\"Alice\"}],\"lastMessage\":{\"text\":\"hi\"},\"unreadCount\":0,\"muted\":false,\"status\":\"request\"}],\"cursor\":\"page2\"}"
#define PAGE2 "{\"requests\":[{\"id\":\"c2\",\"members\":[{\"did\":\"did:plc:me\"},{\"did\":\"did:plc:bob\",\"handle\":\"bob.test\",\"displayName\":\"Bob\"}],\"lastMessage\":{\"text\":\"yo\"},\"unreadCount\":0,\"muted\":false,\"status\":\"request\"}]}"

static enum MHD_Result
serve(void *cls, struct MHD_Connection *conn, const char *url, const char *method,
      const char *version, const char *upload, size_t *upload_size, void **con_cls)
{
    const char *q;
    const char *body;
    struct MHD_Response *resp;
    enum MHD_Result ok;
    (void) cls; (void) method; (void) version; (void) upload; (void) con_cls;
    if (*upload_size != 0) { *upload_size = 0; return MHD_YES; }
    requests_seen++;
    last_query[0] = '\0';
    if (strstr(url, "getMessages") != NULL) {
        static const char msgs[] = "{\"messages\":[]}";
        resp = MHD_create_response_from_buffer(strlen(msgs), (void *) msgs, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "content-type", "application/json");
        ok = MHD_queue_response(conn, MHD_HTTP_OK, resp);
        MHD_destroy_response(resp);
        return ok;
    }
    q = strstr(url, "listConvoRequests");
    (void) q;
    {
        const char *qs = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "cursor");
        if (qs) snprintf(last_query, sizeof last_query, "%s", qs);
        body = qs ? PAGE2 : PAGE1;
    }
    resp = MHD_create_response_from_buffer(strlen(body), (void *) body, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(resp, "content-type", "application/json");
    ok = MHD_queue_response(conn, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    return ok;
}

int
main(void)
{
    struct MHD_Daemon *d = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, 0, NULL, NULL, &serve, NULL, MHD_OPTION_END);
    const union MHD_DaemonInfo *info;
    char svc[64];
    wf_agent *agent;
    int count_after_first;

    CHECK(d != NULL);
    if (d == NULL) return 1;
    info = MHD_get_daemon_info(d, MHD_DAEMON_INFO_BIND_PORT);
    snprintf(svc, sizeof svc, "http://127.0.0.1:%d", (int) info->port);
    agent = wf_agent_new(svc);
    CHECK(agent != NULL);
    CHECK(wf_agent_set_bearer(agent, "tok", "me.test", "did:plc:me") == WF_OK);
    g_session.agent = agent;

    /* First page: a fresh load replaces the list and reports the server's cursor. */
    memset(&g_session.requests, 0, sizeof g_session.requests);
    indigo_session_do_chat_requests(NULL);
    CHECK(g_session.event.kind == INDIGO_SESSION_EVENT_CHAT_REQUESTS_PAGE);
    CHECK(g_session.requests.count == 1);
    CHECK(strcmp(g_session.requests.items[0].name, "Alice") == 0);
    CHECK(g_session.requests.has_more);
    CHECK(strcmp(g_session.requests.cursor, "page2") == 0);
    CHECK(last_query[0] == '\0');                       /* the first request has no cursor */
    count_after_first = g_session.requests.count;

    /* Next page: sent with the cursor, appended after the rows already there. */
    indigo_session_do_chat_requests(g_session.requests.cursor);
    CHECK(g_session.event.kind == INDIGO_SESSION_EVENT_CHAT_REQUESTS_PAGE);
    CHECK(strcmp(last_query, "page2") == 0);
    CHECK(g_session.requests.count == count_after_first + 1);
    CHECK(strcmp(g_session.requests.items[0].name, "Alice") == 0);
    CHECK(strcmp(g_session.requests.items[1].name, "Bob") == 0);
    CHECK(!g_session.requests.has_more);

    /* A conversation id that fits loads the thread and records its id. */
    {
        indigo_job j;
        memset(&j, 0, sizeof j);
        snprintf(j.post_uri, sizeof j.post_uri, "%s", "convo-ok");
        memset(g_session.chat_thread.convo_id, 0, sizeof g_session.chat_thread.convo_id);
        indigo_session_do_chat_messages(&j);
        CHECK(g_session.event.kind == INDIGO_SESSION_EVENT_CHAT_THREAD_PAGE);
        CHECK(strcmp(g_session.chat_thread.convo_id, "convo-ok") == 0);
    }

    /* An id longer than the bridge accepts is refused: no thread, and the id is not cut. */
    {
        indigo_job j;
        char longid[300];
        memset(&j, 0, sizeof j);
        memset(longid, 'y', 199);
        longid[199] = '\0';
        snprintf(j.post_uri, sizeof j.post_uri, "%s", longid);
        memset(g_session.chat_thread.convo_id, 0, sizeof g_session.chat_thread.convo_id);
        indigo_session_do_chat_messages(&j);
        CHECK(g_session.event.kind == INDIGO_SESSION_EVENT_CHAT_THREAD_FAILED);
        CHECK(g_session.chat_thread.convo_id[0] == '\0');
    }

    /* A failing server: the failure event is published and the list is kept. */
    MHD_stop_daemon(d);
    indigo_session_do_chat_requests(NULL);
    CHECK(g_session.event.kind == INDIGO_SESSION_EVENT_CHAT_REQUESTS_FAILED);
    CHECK(g_session.requests.count == 2);

    wf_agent_free(agent);
    if (failures) {
        fprintf(stderr, "session_paging_live: %d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("session_paging_live: %d checks passed\n", checks);
    return 0;
}
