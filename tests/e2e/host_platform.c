/*
 * The platform half of the host end-to-end build: the <3ds.h> calls made by
 * the session worker, backed by pthreads. Nothing here is linked into the
 * console build or the normal host test binary.
 */

#include <3ds.h>

#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include "wolfram/3ds.h"

struct e2e_host_thread {
    pthread_t handle;
    ThreadFunc entrypoint;
    void *arg;
    bool detached;
};

void
LightLock_Init(LightLock *lock)
{
    pthread_mutex_init(&lock->mutex, NULL);
}

void
LightLock_Lock(LightLock *lock)
{
    pthread_mutex_lock(&lock->mutex);
}

void
LightLock_Unlock(LightLock *lock)
{
    pthread_mutex_unlock(&lock->mutex);
}

void
LightSemaphore_Init(LightSemaphore *sem, s32 initial, s32 max)
{
    pthread_mutex_init(&sem->mutex, NULL);
    pthread_cond_init(&sem->cond, NULL);
    sem->count = initial;
    sem->limit = max;
}

void
LightSemaphore_Acquire(LightSemaphore *sem, s32 count)
{
    pthread_mutex_lock(&sem->mutex);
    while (sem->count < count) {
        pthread_cond_wait(&sem->cond, &sem->mutex);
    }
    sem->count -= count;
    pthread_mutex_unlock(&sem->mutex);
}

void
LightSemaphore_Release(LightSemaphore *sem, s32 count)
{
    pthread_mutex_lock(&sem->mutex);
    sem->count += count;
    if (sem->count > sem->limit) {
        sem->count = sem->limit;
    }
    pthread_cond_broadcast(&sem->cond);
    pthread_mutex_unlock(&sem->mutex);
}

static void *
thread_trampoline(void *arg)
{
    struct e2e_host_thread *thread = (struct e2e_host_thread *) arg;

    thread->entrypoint(thread->arg);
    return NULL;
}

Thread
threadCreate(ThreadFunc entrypoint, void *arg, size_t stack_size, int prio,
             int affinity, bool detached)
{
    struct e2e_host_thread *thread;
    pthread_attr_t attr;
    (void) stack_size;
    (void) prio;
    (void) affinity;

    thread = (struct e2e_host_thread *) calloc(1, sizeof(*thread));
    if (!thread) {
        return NULL;
    }
    thread->entrypoint = entrypoint;
    thread->arg = arg;
    thread->detached = detached;

    pthread_attr_init(&attr);
    if (detached) {
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    }
    if (pthread_create(&thread->handle, &attr, thread_trampoline, thread) != 0) {
        pthread_attr_destroy(&attr);
        free(thread);
        return NULL;
    }
    pthread_attr_destroy(&attr);
    return thread;
}

void
threadJoin(Thread thread, u64 timeout)
{
    (void) timeout;

    if (thread && !thread->detached) {
        pthread_join(thread->handle, NULL);
    }
}

void
threadFree(Thread thread)
{
    free(thread);
}

s32
svcGetThreadPriority(s32 *out, Handle thread)
{
    (void) thread;

    if (out) {
        *out = 0;
    }
    return 0;
}

void
svcSleepThread(s64 nanoseconds)
{
    struct timespec ts;

    if (nanoseconds <= 0) {
        return;
    }
    ts.tv_sec = (time_t) (nanoseconds / 1000000000LL);
    ts.tv_nsec = (long) (nanoseconds % 1000000000LL);
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

/* Wolfram's 3DS platform build seeds TLS from the console's hardware RNG. The
 * host library has no such call, and the mock PDS is plain HTTP, so the hook
 * is a no-op here; the agent still runs with its transport defaults. */
wf_status
wf_3ds_apply_tls_rng(struct wf_agent *agent)
{
    (void) agent;
    return WF_OK;
}
