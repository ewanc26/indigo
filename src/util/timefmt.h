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

/* Parse an RFC 3339 UTC timestamp -- "2026-10-03T19:31:35Z", with optional
 * fractional seconds, which the PDS does emit. Only the UTC form is
 * accepted: ATProto requires it, and silently reading a numeric offset as
 * UTC would put a mute's expiry hours out. Returns false on anything else.
 * The out parameter is long long rather than long because the 3DS's long is
 * 32 bits and an expiry far in the future would not fit. */
bool indigo_time_parse_rfc3339(const char *text, long long *out_epoch);

/* Seconds since the Unix epoch, or 0 if the platform clock is unusable. */
long long indigo_time_now(void);

#endif
