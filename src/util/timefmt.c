#include "util/timefmt.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Read exactly `digits` decimal digits. Returns false on anything else, so a
 * malformed field fails the parse instead of silently reading a short value. */
static bool
take_digits(const char **cursor, int digits, int *out)
{
    int value = 0;

    for (int i = 0; i < digits; i++) {
        char c = (*cursor)[i];

        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    *cursor += digits;
    *out = value;
    return true;
}

static bool
take_literal(const char **cursor, char expected)
{
    if (**cursor != expected) {
        return false;
    }
    (*cursor)++;
    return true;
}

/* Days from 1970-01-01 to y-m-d, proleptic Gregorian. Howard Hinnant's
 * days_from_civil: it shifts the year to start in March so the leap day lands
 * at the end of the cycle, which removes every special case for February.
 * Written arithmetically rather than through timegm, which is not portable
 * to devkitARM. */
static long long
days_from_civil(long long y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned) (y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;

    return era * 146097 + (long long) doe - 719468;
}

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

bool
indigo_time_parse_rfc3339(const char *text, long long *out_epoch)
{
    if (!text || !out_epoch) {
        return false;
    }

    const char *c = text;
    int year, month, day, hour, minute, second;

    if (!take_digits(&c, 4, &year) || !take_literal(&c, '-') ||
        !take_digits(&c, 2, &month) || !take_literal(&c, '-') ||
        !take_digits(&c, 2, &day)) {
        return false;
    }

    /* ATProto uses 'T'; tolerate the lowercase form RFC 3339 also permits. */
    if (*c != 'T' && *c != 't') {
        return false;
    }
    c++;

    if (!take_digits(&c, 2, &hour) || !take_literal(&c, ':') ||
        !take_digits(&c, 2, &minute) || !take_literal(&c, ':') ||
        !take_digits(&c, 2, &second)) {
        return false;
    }

    /* Fractional seconds are present on most PDS output and carry no
     * information a feed needs, so they are skipped rather than parsed. */
    if (*c == '.') {
        c++;
        if (*c < '0' || *c > '9') {
            return false;
        }
        while (*c >= '0' && *c <= '9') {
            c++;
        }
    }

    /* UTC only -- see the header. A numeric offset is rejected rather than
     * being read as if it were Zulu. */
    if (*c != 'Z' && *c != 'z') {
        return false;
    }
    c++;
    if (*c != '\0') {
        return false;
    }

    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 60) { /* 60: leap second */
        return false;
    }

    const long long days = days_from_civil(year, (unsigned) month, (unsigned) day);

    *out_epoch = days * 86400 + hour * 3600 + minute * 60 + second;
    return true;
}

long long
indigo_time_now(void)
{
    const time_t now = time(NULL);

    /* (time_t) -1 means the clock is unavailable. Callers treat 0 as "no
     * idea", which makes every expiry read as still active rather than as
     * 1970. */
    return (now == (time_t) -1) ? 0 : (long long) now;
}
