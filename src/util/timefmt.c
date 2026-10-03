#include "util/timefmt.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

bool
indigo_time_format_rfc3339(long unix_seconds, char *out, size_t n)
{
    struct tm utc;
    time_t t = (time_t) unix_seconds;
    int len;

    if (!out || n == 0) {
        return false;
    }
    out[0] = '\0';
    /* gmtime, not gmtime_r: the only caller is the session worker, which runs
     * one job at a time, and gmtime_r is not in C11 so the host build's
     * -Wpedantic would object to it. */
    {
        struct tm *p = gmtime(&t);

        if (!p) {
            return false;
        }
        utc = *p;
    }
    len = snprintf(out, n, "%04d-%02d-%02dT%02d:%02d:%02dZ", utc.tm_year + 1900,
                   utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
    if (len < 0 || (size_t) len >= n) {
        out[0] = '\0';
        return false;
    }
    return true;
}