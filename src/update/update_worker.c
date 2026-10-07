#include "update/update_worker.h"

#include "util/log.h"

#if defined(__3DS__) && defined(WOLFRAM_3DS)

#include "atproto/atproto.h"
#include "update/update_sd.h"
#include "update/update_sig.h"
#include "util/buildinfo.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <wolfram/update.h>
#include <wolfram/xrpc.h>

#define WORKER_STACK 0x20000
/* A GitHub URL is the base of the client, and nothing is ever fetched from it:
 * every request below is an absolute https URL. */
#define WORKER_BASE_URL "https://github.com"
#define MANIFEST_BODY_MAX 4096u

typedef enum { JOB_CHECK, JOB_DOWNLOAD } job_kind;

static LightLock s_lock;
static bool s_inited;
static Thread s_thread;
static bool s_busy;          /* guarded by s_lock */
static job_kind s_job;
static indigo_update_event s_event; /* guarded by s_lock; one slot, one request at a time */
static wf_update_manifest s_manifest;
static bool s_have_manifest;
static indigo_update_paths s_paths;
static bool s_paths_ok;

static void
post(indigo_update_event_kind kind, const char *message)
{
    LightLock_Lock(&s_lock);
    s_event.kind = kind;
    snprintf(s_event.message, sizeof s_event.message, "%s", message ? message : "");
    LightLock_Unlock(&s_lock);
}

static void
fail(const char *message)
{
    indigo_log_warn("update: %s", message);
    post(INDIGO_UPDATE_EVENT_FAILED, message);
}

static wf_xrpc_client *
new_client(void)
{
    wf_xrpc_client *client = wf_xrpc_client_new(WORKER_BASE_URL);

    if (client) {
        wf_xrpc_client_set_ca_bundle(client, INDIGO_CA_BUNDLE_PATH);
    }
    return client;
}

static void
do_check(void)
{
    static const wf_update_policy policy = {INDIGO_UPDATE_MAX_BYTES, "indigo",
                                            "https://github.com/" INDIGO_UPDATE_REPO
                                            "/releases/download/"};
    wf_xrpc_client *client = new_client();
    wf_response resp = {0};
    wf_response sig = {0};
    unsigned char pk[INDIGO_UPDATE_PUBLIC_KEY_LEN];
    wf_update_manifest m;
    char current[INDIGO_UPDATE_VERSION_MAX];
    bool dev = true;
    int err = 0;
    int cmp;

    if (!client) {
        fail("Could not start a connection.");
        return;
    }
    if (wf_http_get_public(client, INDIGO_UPDATE_MANIFEST_URL, MANIFEST_BODY_MAX, &resp) != WF_OK ||
        resp.status != 200 || !resp.body) {
        wf_response_free(&resp);
        wf_xrpc_client_free(client);
        fail("Could not get the latest release from GitHub. Is the console online?");
        return;
    }
    /* The signature is checked on the bytes as downloaded, before anything is
     * parsed. A release with no signature is refused, not trusted on its SHA-256. */
    if (wf_http_get_public(client, INDIGO_UPDATE_SIGNATURE_URL, INDIGO_UPDATE_SIGNATURE_MAX,
                           &sig) != WF_OK ||
        sig.status != 200 || !sig.body) {
        wf_response_free(&resp);
        wf_response_free(&sig);
        wf_xrpc_client_free(client);
        fail("The latest release is not signed, so I did not use it.");
        return;
    }
    wf_xrpc_client_free(client);
    if (!indigo_update_public_key(pk) ||
        !indigo_update_verify_manifest(resp.body, resp.body_len, sig.body, sig.body_len, pk)) {
        wf_response_free(&resp);
        wf_response_free(&sig);
        fail("The latest release is not signed with my key, so I did not use it.");
        return;
    }
    wf_response_free(&sig);
    if (wf_update_parse_manifest(resp.body, resp.body_len, &policy, &m) != WF_OK ||
        !indigo_update_asset_ok(m.version, m.asset.name, m.asset.url)) {
        wf_response_free(&resp);
        fail("The latest release's update information is not something I accept, so I stopped.");
        return;
    }
    wf_response_free(&resp);
    if (!indigo_update_release_of(INDIGO_BUILD_COMMIT, current, sizeof current, &dev) || dev) {
        fail("This build cannot say which release it is.");
        return;
    }
    cmp = wf_update_compare_versions(m.version, current, &err);
    if (err) {
        fail("The latest release has a version number I cannot read.");
        return;
    }
    /* A version that does not fit the event is not one I can show or compare
     * safely: refuse the release instead of cutting it short. */
    if (strlen(m.version) >= sizeof s_event.version) {
        fail("The latest release has a version number I cannot read.");
        return;
    }
    LightLock_Lock(&s_lock);
    s_manifest = m;
    s_have_manifest = cmp > 0;
    s_event.kind = INDIGO_UPDATE_EVENT_CHECKED;
    snprintf(s_event.version, sizeof s_event.version, "%s", m.version);
    s_event.size = m.asset.size;
    s_event.is_update = cmp > 0;
    s_event.message[0] = '\0';
    LightLock_Unlock(&s_lock);
    indigo_log_info("update: latest release is %s, this is %s", m.version, current);
}

static void
do_download(void)
{
    wf_update_manifest m;
    wf_update_verify v;
    wf_xrpc_client *client;
    wf_response resp = {0};
    indigo_update_result r;

    LightLock_Lock(&s_lock);
    m = s_manifest;
    LightLock_Unlock(&s_lock);
    if (!s_have_manifest || !s_paths_ok) {
        fail("There is nothing to download.");
        return;
    }
    client = new_client();
    if (!client) {
        fail("Could not start a connection.");
        return;
    }
    /* The ceiling is the manifest's own size: a body past it aborts the
     * transfer rather than filling memory. */
    if (wf_http_get_public(client, m.asset.url, m.asset.size, &resp) != WF_OK || resp.status != 200 ||
        !resp.body) {
        wf_response_free(&resp);
        wf_xrpc_client_free(client);
        fail("The download failed. Nothing on the card changed.");
        return;
    }
    wf_xrpc_client_free(client);
    wf_update_verify_init(&v, &m.asset);
    if (wf_update_verify_feed(&v, resp.body, resp.body_len) != WF_OK ||
        wf_update_verify_final(&v) != WF_OK) {
        wf_response_free(&resp);
        fail("The download was not the size and SHA-256 the release states, so I threw it away. "
             "Nothing on the card changed.");
        return;
    }
    if (!indigo_update_sd_write_file(s_paths.staged, resp.body, resp.body_len)) {
        wf_response_free(&resp);
        fail("Could not write the download to the SD card. Is it full or locked?");
        return;
    }
    wf_response_free(&resp);
    /* Re-hashed from the card: a write that succeeded and the right bytes on
     * the card are different claims. */
    r = indigo_update_stage(indigo_update_sd_fs(), &s_paths, m.version, m.asset.sha256);
    if (r != INDIGO_UPDATE_OK) {
        fail("What was written to the card did not read back correctly, so I removed it. "
             "Nothing else changed.");
        return;
    }
    indigo_log_info("update: %s downloaded, checked and staged", m.version);
    post(INDIGO_UPDATE_EVENT_STAGED, "");
}

static void
worker(void *unused)
{
    (void) unused;
    if (s_job == JOB_CHECK) {
        do_check();
    } else {
        do_download();
    }
    LightLock_Lock(&s_lock);
    s_busy = false;
    LightLock_Unlock(&s_lock);
}

static bool
start(job_kind job)
{
    int32_t prio = 0;

    if (!s_inited) {
        return false;
    }
    LightLock_Lock(&s_lock);
    if (s_busy) {
        LightLock_Unlock(&s_lock);
        return false;
    }
    s_busy = true;
    LightLock_Unlock(&s_lock);
    if (s_thread) {
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }
    s_job = job;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    s_thread = threadCreate(worker, NULL, WORKER_STACK, prio + 1, -2, false);
    if (!s_thread) {
        LightLock_Lock(&s_lock);
        s_busy = false;
        LightLock_Unlock(&s_lock);
        return false;
    }
    return true;
}

void
indigo_update_worker_init(const char *argv0, const char *state_path)
{
    LightLock_Init(&s_lock);
    memset(&s_event, 0, sizeof s_event);
    s_paths_ok = argv0 && indigo_update_paths_from(argv0, state_path, &s_paths);
    s_inited = true;
}

bool
indigo_update_worker_check(void)
{
    return start(JOB_CHECK);
}

bool
indigo_update_worker_download(void)
{
    return start(JOB_DOWNLOAD);
}

bool
indigo_update_worker_poll(indigo_update_event *ev)
{
    bool have = false;

    if (!s_inited) {
        return false;
    }
    LightLock_Lock(&s_lock);
    if (s_event.kind != INDIGO_UPDATE_EVENT_NONE) {
        *ev = s_event;
        s_event.kind = INDIGO_UPDATE_EVENT_NONE;
        have = true;
    }
    LightLock_Unlock(&s_lock);
    return have;
}

bool
indigo_update_worker_install(void)
{
    if (!s_paths_ok) {
        return false;
    }
    return indigo_update_install(indigo_update_sd_fs(), &s_paths) == INDIGO_UPDATE_OK;
}

void
indigo_update_worker_stop(void)
{
    if (s_thread) {
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }
}

#else

void
indigo_update_worker_init(const char *argv0, const char *state_path)
{
    (void) argv0;
    (void) state_path;
}

bool
indigo_update_worker_check(void)
{
    return false;
}

bool
indigo_update_worker_download(void)
{
    return false;
}

bool
indigo_update_worker_poll(indigo_update_event *ev)
{
    (void) ev;
    return false;
}

bool
indigo_update_worker_install(void)
{
    return false;
}

void
indigo_update_worker_stop(void)
{
}

#endif
