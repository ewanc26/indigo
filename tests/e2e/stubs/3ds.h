/*
 * A libctru stand-in for the host end-to-end build. Only the pieces the
 * session worker uses are here: the lock, semaphore and thread calls, and the
 * clock sleep. The implementations live in host_platform.c and are backed by
 * pthreads, so the real worker and its queue run unchanged on the host.
 *
 * This header is on the include path only for `make e2e`; the console build
 * sees devkitARM's <3ds.h>.
 */

#ifndef INDIGO_E2E_3DS_STUB_H
#define INDIGO_E2E_3DS_STUB_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int32_t s32;
typedef int64_t s64;
typedef uint64_t u64;
typedef int Handle;

#define CUR_THREAD_HANDLE ((Handle) 0)
#define U64_MAX UINT64_MAX

typedef struct {
    pthread_mutex_t mutex;
} LightLock;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    s32 count;
    s32 limit;
} LightSemaphore;

typedef void (*ThreadFunc)(void *arg);
typedef struct e2e_host_thread *Thread;

void LightLock_Init(LightLock *lock);
void LightLock_Lock(LightLock *lock);
void LightLock_Unlock(LightLock *lock);

void LightSemaphore_Init(LightSemaphore *sem, s32 initial, s32 max);
void LightSemaphore_Acquire(LightSemaphore *sem, s32 count);
void LightSemaphore_Release(LightSemaphore *sem, s32 count);

Thread threadCreate(ThreadFunc entrypoint, void *arg, size_t stack_size,
                    int prio, int affinity, bool detached);
void threadJoin(Thread thread, u64 timeout);
void threadFree(Thread thread);

s32 svcGetThreadPriority(s32 *out, Handle thread);
void svcSleepThread(s64 nanoseconds);

#endif /* INDIGO_E2E_3DS_STUB_H */
