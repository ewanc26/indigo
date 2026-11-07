#ifndef INDIGO_WRAP_H
#define INDIGO_WRAP_H

#include <stddef.h>

typedef struct {
    unsigned start;
    unsigned len;
} indigo_line;

/* Average advance of one character at scale 1.0, in pixels. Layout has to wrap
 * before it can ask a font, so this is a deliberately cautious estimate. */
#define INDIGO_CHAR_WIDTH 13.5f

/*
 * Break `text` into at most `max_lines` lines of at most `max_units` width
 * units (an ASCII character is 1, a wide character 2). Breaks at spaces where
 * possible, honours newlines, never splits a UTF-8 sequence. Spaces at a break
 * are dropped from the line. `*truncated` is set when text remained after the
 * last line; the caller then appends "...".
 * Returns the number of lines.
 */
unsigned indigo_wrap(const char *text, unsigned max_units, indigo_line *lines,
                     unsigned max_lines, int *truncated);

/*
 * Copy `text` into `out`, truncating to at most `max_units` width units with
 * a trailing "..." when it does not fit. The ellipsis counts towards the
 * limit, so this is for one-line labels: the result never runs past the width
 * the caller measured. Cuts only at a UTF-8 boundary and always NUL-terminates
 * (an empty string is copied when `max_units` is too small for an ellipsis).
 */
void indigo_fit_text(char *out, size_t cap, const char *text, unsigned max_units);

#endif
