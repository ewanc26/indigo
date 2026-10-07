/* The session worker: the thread, the lock, the event hand-off and the queue.
 * What the jobs do is in the session_*.c files; see session_internal.h. */

#include "atproto/session_internal.h"

#if defined(__3DS__) && defined(WOLFRAM_3DS)

#define WORKER_STACK 0x20000

indigo_session_state g_session;

void
indigo_session_publish(indigo_session_event_kind kind, wf_failure_kind failure, const char *account)
{
    LightLock_Lock(&g_session.lock);
    memset(&g_session.event, 0, sizeof g_session.event);
    g_session.event.kind = kind;
    g_session.event.failure = failure;
    if (account) {
        snprintf(g_session.event.account, sizeof g_session.event.account, "%s", account);
    }
    g_session.event_ready = true;
    g_session.busy = false;
    LightLock_Unlock(&g_session.lock);
}

void
indigo_session_publish_event(const indigo_session_event *ev)
{
    LightLock_Lock(&g_session.lock);
    g_session.event = *ev;
    g_session.event_ready = true;
    g_session.busy = false;
    LightLock_Unlock(&g_session.lock);
}

/* A call failed: say what kind of failure it was, log it, and tell the app. */
void
indigo_session_publish_failure(indigo_session_event *ev, const char *what, wf_status st)
{
    ev->failure = wf_failure_classify(st, 0, NULL);
    indigo_log_warn("%s failed: wolfram status %d (%s)", what, (int) st,
                    wf_failure_tag(ev->failure));
    indigo_session_publish_event(ev);
}


static void
worker(void *arg)
{
    (void) arg;

    for (;;) {
        indigo_job j;

        LightSemaphore_Acquire(&g_session.wake, 1);
        if (g_session.quit) {
            break;
        }
        LightLock_Lock(&g_session.lock);
        j = g_session.job;
        memset(&g_session.job, 0, sizeof g_session.job);
        LightLock_Unlock(&g_session.lock);

        switch (j.kind) {
        case JOB_LOGIN:
            indigo_session_do_login(&j);
            break;
        case JOB_OAUTH:
            indigo_session_do_oauth(&j);
            break;
        case JOB_RESUME:
            indigo_session_do_resume();
            break;
        case JOB_LOGOUT:
            indigo_session_do_logout();
            break;
        case JOB_TIMELINE:
            indigo_session_do_timeline(&j);
            break;
        case JOB_POST_ACTION:
            indigo_session_do_post_action(&j);
            break;
        case JOB_THREAD:
            indigo_session_do_thread(&j);
            break;
        case JOB_PROFILE:
            indigo_session_do_profile(&j);
            break;
        case JOB_NOTIFICATIONS:
            indigo_session_do_notifications();
            break;
        case JOB_SEARCH:
            indigo_session_do_search(&j);
            break;
        case JOB_FOLLOW:
            indigo_session_do_follow(&j);
            break;
        case JOB_GRAPH:
            indigo_session_do_graph(&j);
            break;
        case JOB_PEOPLE:
            indigo_session_do_people(&j);
            break;
        case JOB_POST_SEARCH:
            indigo_session_do_post_search(&j);
            break;
        case JOB_AUTHOR_FEED:
            indigo_session_do_author_feed(&j);
            break;
        case JOB_LISTS:
            indigo_session_do_lists(&j);
            break;
        case JOB_LIST_MEMBERS:
            indigo_session_do_list_members(&j);
            break;
        case JOB_FEEDS:
            indigo_session_do_feeds(&j);
            break;
        case JOB_FEED:
            indigo_session_do_feed(&j);
            break;
        case JOB_MUTES:
        case JOB_BLOCKS:
            indigo_session_do_moderation_list(&j);
            break;
        case JOB_PUBLISH:
            indigo_session_do_publish(&j);
            break;
        case JOB_NONE:
            break;
        }
        memset(&j, 0, sizeof j);
    }
    indigo_session_drop_agent();
}

bool
indigo_session_start(const char *session_path)
{
    int32_t prio = 0;

    if (g_session.started) {
        return true;
    }
    snprintf(g_session.path, sizeof g_session.path, "%s", session_path);
    LightLock_Init(&g_session.lock);
    LightSemaphore_Init(&g_session.wake, 0, 8);
    g_session.quit = false;
    g_session.busy = false;
    g_session.event_ready = false;

    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    g_session.thread = threadCreate(worker, NULL, WORKER_STACK, prio + 1, -2, false);
    if (!g_session.thread) {
        g_session.thread = threadCreate(worker, NULL, WORKER_STACK, prio + 1, -1, false);
    }
    if (!g_session.thread) {
        indigo_log_error("could not start the network thread");
        return false;
    }
    g_session.started = true;
    return true;
}

void
indigo_session_stop(void)
{
    if (!g_session.started) {
        return;
    }
    g_session.quit = true;
    LightSemaphore_Release(&g_session.wake, 1);
    threadJoin(g_session.thread, U64_MAX);
    threadFree(g_session.thread);
    g_session.thread = NULL;
    g_session.started = false;
    memset(&g_session.job, 0, sizeof g_session.job);
    indigo_session_wipe(&g_session.saved);
}

bool
indigo_session_enqueue(const indigo_job *j)
{
    bool ok = false;

    if (!g_session.started) {
        return false;
    }
    LightLock_Lock(&g_session.lock);
    /* An unpolled event (and the page behind it) must not be overwritten. */
    if (!g_session.busy && !g_session.event_ready) {
        g_session.busy = true;
        g_session.job = *j;
        ok = true;
    }
    LightLock_Unlock(&g_session.lock);
    if (ok) {
        LightSemaphore_Release(&g_session.wake, 1);
    }
    return ok;
}


bool
indigo_session_busy(void)
{
    bool busy;

    if (!g_session.started) {
        return true;
    }
    LightLock_Lock(&g_session.lock);
    busy = g_session.busy || g_session.event_ready;
    LightLock_Unlock(&g_session.lock);
    return busy;
}

const indigo_post *
indigo_session_page(unsigned *count)
{
    *count = g_session.page_count;
    return g_session.page;
}

bool
indigo_session_poll(indigo_session_event *out)
{
    bool ready = false;

    if (!g_session.started) {
        return false;
    }
    LightLock_Lock(&g_session.lock);
    if (g_session.event_ready) {
        *out = g_session.event;
        g_session.event_ready = false;
        ready = true;
    }
    LightLock_Unlock(&g_session.lock);
    return ready;
}

bool
indigo_session_has_saved(void)
{
    FILE *f = fopen(g_session.path, "rb");

    if (!f) {
        return false;
    }
    fclose(f);
    return true;
}

#endif /* 3DS with Wolfram */
