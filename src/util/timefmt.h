#ifndef INDIGO_TIMEFMT_H
#define INDIGO_TIMEFMT_H

#include <stdbool.h>
#include <stddef.h>

/* Format a Unix timestamp as RFC 3339 in UTC, e.g.
 * "2026-10-03T19:31:42Z". The lexicons take this shape for `seenAt` and
 * similar fields, so it is produced here rather than by Wolfram.
 *
 * Returns false and leaves `out` empty when the timestamp cannot be
 * converted or would not fit. */
bool indigo_time_format_rfc3339(long unix_seconds, char *out, size_t n);

#endif