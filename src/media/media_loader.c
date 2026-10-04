#include "media/media_loader.h"

#include "util/log.h"

#include <stdlib.h>
#include <string.h>

#if defined(__3DS__) && defined(WOLFRAM_3DS)

#include <3ds.h>
#include <stdio.h>
#include <wolfram/image.h>
#include <wolfram/xrpc.h>

#include "atproto/atproto.h"

/* Decode is CPU-bound and the fetch is round-trip bound, so one thread is
 * enough: a queue of images that arrived while a TLS handshake was in progress
 * is still better than a frame loop that stops. */
#define LOADER_STACK 0x14000
/* URLs waiting to be fetched. Small on purpose -- this is a screen's worth of
 * avatars, not a prefetcher, and a deep queue would just hold stale requests
 * behind the one the reader is actually waiting for. */
#define QUEUE_MAX 12
/* Finished images waiting to be adopted. */
#define RESULT_MAX 4
/* The base URL is never used: every fetch is an absolute CDN URL, and a client
 * of its own means no account credential can reach an image host and no image
 * fetch can disturb a session request's state. */
#define LOADER_BASE_URL "https://bsky.social"

typedef struct {
    char url[INDIGO_MEDIA_URL_MAX];
    int slot;
    unsigned generation;
    /* The longest side the requester will draw at, which is the only thing
     * that decides how big a decode is worth doing. */
    unsigned max_dim;
} pending_fetch;

typedef struct {
    char url[INDIGO_MEDIA_URL_MAX];
    int slot;
    unsigned generation;
    /* Owned until the main thread adopts it. */
    uint8_t *pixels;
    unsigned width;
    unsigned height;
    bool failed;
} finished_fetch;

static Thread s_thread;
static LightLock s_lock;
static LightSemaphore s_wake;
static volatile bool s_quit;
static bool s_started;

static wf_xrpc_client *s_client;

/* Guarded by s_lock. */
static pending_fetch s_queue[QUEUE_MAX];
static unsigned s_queue_head;
static unsigned s_queue_count;
static finished_fetch s_results[RESULT_MAX];
static unsigned s_result_count;

static bool
queued(const char *url)
{
    for (unsigned i = 0; i < s_queue_count; i++) {
        unsigned at = (s_queue_head + i) % QUEUE_MAX;

        if (strcmp(s_queue[at].url, url) == 0) {
            return true;
        }
    }
    return false;
}

static void
load_one(const char *url, int slot, unsigned generation, unsigned max_dim)
{
    wf_response resp = {0};
    wf_image_rgba img = {0};
    finished_fetch done;

    memset(&done, 0, sizeof done);
    snprintf(done.url, sizeof done.url, "%s", url);
    done.slot = slot;
    done.generation = generation;

    /* Public GET: no Authorization header, https-only including redirects, a
     * body cap that aborts the transfer rather than buffering an endless one,
     * and no touch of the client's state. */
    if (wf_http_get_public(s_client, url, INDIGO_MEDIA_DOWNLOAD_MAX, &resp) != WF_OK) {
        indigo_log_warn("image fetch failed: %.80s", url);
        done.failed = true;
    } else if (wf_image_decode_rgba(resp.body, resp.body_len,
                                    max_dim ? max_dim : INDIGO_MEDIA_MAX_DIM,
                                    &img) != WF_OK) {
        indigo_log_warn("image decode failed: %.80s", url);
        done.failed = true;
    } else {
        done.pixels = img.pixels;
        done.width = (unsigned) img.width;
        done.height = (unsigned) img.height;
    }
    wf_response_free(&resp);

    LightLock_Lock(&s_lock);
    if (s_result_count < RESULT_MAX) {
        s_results[s_result_count++] = done;
    } else {
        /* Nobody drained last frame. Dropping the result is correct: the slot
         * stays LOADING until it is evicted, and the image will be requested
         * again if it is still on screen. */
        free(done.pixels);
    }
    LightLock_Unlock(&s_lock);
}

static void
worker(void *unused)
{
    (void) unused;
    for (;;) {
        pending_fetch job;
        bool have = false;

        LightSemaphore_Acquire(&s_wake, 1);
        if (s_quit) {
            return;
        }
        LightLock_Lock(&s_lock);
        if (s_queue_count > 0) {
            job = s_queue[s_queue_head];
            s_queue_head = (s_queue_head + 1) % QUEUE_MAX;
            s_queue_count--;
            have = true;
        }
        LightLock_Unlock(&s_lock);
        if (have) {
            load_one(job.url, job.slot, job.generation, job.max_dim);
        }
    }
}

bool
indigo_media_loader_start(void)
{
    int32_t prio = 0;

    if (s_started) {
        return true;
    }
    s_client = wf_xrpc_client_new(LOADER_BASE_URL);
    if (!s_client) {
        indigo_log_error("media loader: no client");
        return false;
    }
    /* Verified per client rather than globally, because a missing trust store
     * fails every fetch with a TLS error that looks like a network fault. */
    wf_xrpc_client_set_ca_bundle(s_client, INDIGO_CA_BUNDLE_PATH);
    LightLock_Init(&s_lock);
    LightSemaphore_Init(&s_wake, 0, QUEUE_MAX);
    s_quit = false;
    s_queue_head = 0;
    s_queue_count = 0;
    s_result_count = 0;
    memset(s_queue, 0, sizeof s_queue);
    memset(s_results, 0, sizeof s_results);

    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    s_thread = threadCreate(worker, NULL, LOADER_STACK, prio + 1, -2, false);
    if (!s_thread) {
        s_thread = threadCreate(worker, NULL, LOADER_STACK, prio + 1, -1, false);
    }
    if (!s_thread) {
        indigo_log_error("media loader: could not start the thread");
        wf_xrpc_client_free(s_client);
        s_client = NULL;
        return false;
    }
    s_started = true;
    return true;
}

void
indigo_media_loader_stop(void)
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
    /* Anything the thread decoded but nobody adopted. */
    for (unsigned i = 0; i < RESULT_MAX; i++) {
        free(s_results[i].pixels);
    }
    memset(s_results, 0, sizeof s_results);
    s_result_count = 0;
    memset(s_queue, 0, sizeof s_queue);
    s_queue_head = 0;
    s_queue_count = 0;
    wf_xrpc_client_free(s_client);
    s_client = NULL;
}

bool
indigo_media_loader_request(indigo_media_cache *c, const char *url, unsigned max_dim)
{
    unsigned generation = 0;
    int slot;
    bool ok = false;

    if (!s_started || !c || !url || url[0] == '\0') {
        return false;
    }
    /* The cache decides whether this URL needs fetching at all, so a repeated
     * request for a URL already loading, ready or failed costs one lookup. */
    slot = indigo_media_claim(c, url, max_dim, &generation);
    if (slot < 0) {
        return false;
    }
    LightLock_Lock(&s_lock);
    if (s_queue_count < QUEUE_MAX && !queued(url)) {
        unsigned at = (s_queue_head + s_queue_count) % QUEUE_MAX;

        snprintf(s_queue[at].url, sizeof s_queue[at].url, "%s", url);
        s_queue[at].slot = slot;
        s_queue[at].generation = generation;
        s_queue[at].max_dim = max_dim;
        s_queue_count++;
        ok = true;
    }
    LightLock_Unlock(&s_lock);
    if (!ok) {
        /* The slot was claimed but never queued, so nothing will ever finish
         * it. Put it back to a state the cache will reconsider. */
        indigo_media_fail(c, slot, generation);
        return false;
    }
    LightSemaphore_Release(&s_wake, 1);
    return true;
}

void
indigo_media_loader_drain(indigo_media_cache *c)
{
    if (!s_started || !c) {
        return;
    }
    LightLock_Lock(&s_lock);
    for (unsigned i = 0; i < s_result_count; i++) {
        finished_fetch *f = &s_results[i];

        if (f->failed) {
            indigo_media_fail(c, f->slot, f->generation);
        } else if (!indigo_media_publish(c, f->slot, f->generation, f->pixels,
                                         f->width, f->height)) {
            /* Stale or over budget: publish did not take the pixels. */
            free(f->pixels);
        }
        f->pixels = NULL;
    }
    s_result_count = 0;
    LightLock_Unlock(&s_lock);
}

bool
indigo_media_loader_running(void)
{
    return s_started;
}

#else /* the host build: the pipeline exists, the network does not */

bool
indigo_media_loader_start(void)
{
    return false;
}

void
indigo_media_loader_stop(void)
{
}

bool
indigo_media_loader_request(indigo_media_cache *c, const char *url, unsigned max_dim)
{
    (void) c;
    (void) url;
    (void) max_dim;
    return false;
}

void
indigo_media_loader_drain(indigo_media_cache *c)
{
    (void) c;
}

bool
indigo_media_loader_running(void)
{
    return false;
}

#endif