/* Declarations only, for a host type-check of the 3DS-only session code (make
 * typecheck-session). It is not libctru and nothing here runs: it lets the compiler
 * check the session sources against Wolfram's real headers. Add a name only when the
 * type check needs it. */
/* Declarations only, for a type-check of Indigo's session code on the host. Not libctru. */
#ifndef STUB_3DS_H
#define STUB_3DS_H
#include <stdint.h>
#include <stdbool.h>
typedef int32_t Result;
typedef uint32_t Handle;
typedef int64_t s64;
typedef struct { int dummy; } LightLock;
typedef void *Thread;
typedef struct { int dummy; } LightEvent;
void LightLock_Init(LightLock *l);
void LightLock_Lock(LightLock *l);
void LightLock_Unlock(LightLock *l);
Thread threadCreate(void (*entry)(void *), void *arg, size_t stack, int prio, int affinity, bool detached);
void threadJoin(Thread t, uint64_t timeout);
void threadFree(Thread t);
void svcSleepThread(s64 ns);
int svcGetThreadPriority(int32_t *out, Handle h);
#endif
#ifndef STUB_3DS_H2
#define STUB_3DS_H2
#define CUR_THREAD_HANDLE 0u
#define U64_MAX UINT64_MAX
typedef struct { int dummy; } LightSemaphore;
void LightSemaphore_Init(LightSemaphore *s, int32_t initial, int32_t max);
void LightSemaphore_Acquire(LightSemaphore *s, int32_t count);
void LightSemaphore_Release(LightSemaphore *s, int32_t count);
#endif
