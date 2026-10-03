#include "ui/wrap.h"

#include <string.h>

/* Width of the UTF-8 character at s, and its byte length. */
static unsigned
char_units(const unsigned char *s, unsigned *bytes)
{
    unsigned b = 1;
    unsigned cp = s[0];

    if (s[0] >= 0xF0) {
        b = 4;
        cp = 0x10000;
    } else if (s[0] >= 0xE0) {
        b = 3;
        cp = ((unsigned) (s[0] & 0x0F) << 12) | ((unsigned) (s[1] & 0x3F) << 6);
    } else if (s[0] >= 0xC0) {
        b = 2;
    }
    for (unsigned i = 1; i < b; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            b = i;
            break;
        }
    }
    *bytes = b;
    return cp >= 0x2E80 ? 2 : 1;
}

unsigned
indigo_wrap(const char *text, unsigned max_units, indigo_line *lines, unsigned max_lines,
            int *truncated)
{
    const unsigned char *t = (const unsigned char *) text;
    size_t len = strlen(text);
    size_t pos = 0;
    unsigned n = 0;

    *truncated = 0;
    if (max_units == 0 || max_lines == 0) {
        return 0;
    }

    while (pos < len) {
        size_t i = pos;
        unsigned units = 0;
        size_t break_at = 0;
        size_t next = 0;
        int have_break = 0;
        int hard = 0;

        if (n == max_lines) {
            *truncated = 1;
            break;
        }

        while (i < len) {
            unsigned bytes;
            unsigned u;

            if (t[i] == '\n') {
                hard = 1;
                break;
            }
            u = char_units(&t[i], &bytes);
            if (units + u > max_units) {
                break;
            }
            if (t[i] == ' ') {
                break_at = i;
                have_break = 1;
            }
            units += u;
            i += bytes;
        }

        /* Stopped on a space: everything before it fits, so break there. */
        if (!hard && i < len && t[i] == ' ') {
            break_at = i;
            have_break = 1;
        }

        if (hard) {
            lines[n++] = (indigo_line) {(unsigned) pos, (unsigned) (i - pos)};
            pos = i + 1;
            continue;
        }
        if (i >= len) {
            lines[n++] = (indigo_line) {(unsigned) pos, (unsigned) (i - pos)};
            pos = len;
            break;
        }
        if (have_break && break_at > pos) {
            next = break_at + 1;
            lines[n++] = (indigo_line) {(unsigned) pos, (unsigned) (break_at - pos)};
        } else {
            /* One word wider than the line: split it. */
            next = i > pos ? i : pos + 1;
            lines[n++] = (indigo_line) {(unsigned) pos, (unsigned) (next - pos)};
        }
        pos = next;
        while (pos < len && t[pos] == ' ') {
            pos++;
        }
    }

    return n;
}
