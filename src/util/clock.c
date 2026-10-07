#include "util/clock.h"

#include <time.h>

long long
indigo_time_now(void)
{
    const time_t now = time(NULL);

    /* (time_t) -1 means the clock is unavailable. Callers treat 0 as "no
     * idea", which makes every expiry read as still active rather than as
     * 1970. */
    return (now == (time_t) -1) ? 0 : (long long) now;
}
