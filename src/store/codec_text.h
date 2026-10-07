#ifndef INDIGO_STORE_CODEC_TEXT_H
#define INDIGO_STORE_CODEC_TEXT_H

/* The pieces the key=value codecs (session, settings) share: refusing a value
 * that would break its line, and taking a file one line at a time. Kept
 * inline so each codec stays a single translation unit. */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* True when `v` holds a line break, which a value written to a line may not. */
static inline bool
indigo_codec_has_break(const char *v)
{
    return strchr(v, '\n') != NULL || strchr(v, '\r') != NULL;
}

/* Take the line starting at *p, which ends before `end`, and move *p past its
 * newline. The line excludes the newline. Returns false, leaving *p alone,
 * when no newline remains: a truncated final line is not a line. */
static inline bool
indigo_codec_next_line(const char **p, const char *end, const char **line, size_t *len)
{
    const char *nl = memchr(*p, '\n', (size_t) (end - *p));

    if (!nl) {
        return false;
    }
    *line = *p;
    *len = (size_t) (nl - *p);
    *p = nl + 1;
    return true;
}

#endif
