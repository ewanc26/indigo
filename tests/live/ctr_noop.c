/* Single-threaded host stand-ins for the libctru lock and semaphore calls. */
#include <stddef.h>
#include "3ds.h"
void LightLock_Init(LightLock *l) { (void) l; }
void LightLock_Lock(LightLock *l) { (void) l; }
void LightLock_Unlock(LightLock *l) { (void) l; }
void LightSemaphore_Init(LightSemaphore *s, int32_t i, int32_t m) { (void) s; (void) i; (void) m; }
void LightSemaphore_Acquire(LightSemaphore *s, int32_t c) { (void) s; (void) c; }
void LightSemaphore_Release(LightSemaphore *s, int32_t c) { (void) s; (void) c; }
