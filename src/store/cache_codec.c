#include "store/cache_codec.h"

#include "store/codec_text.h"

#include <stdio.h>
#include <string.h>

void
indigo_cache_timeline_clear(indigo_cache_timeline *t)
{
    memset(t, 0, sizeof *t);
}

/* Append `n` bytes to out, keeping room for the terminating NUL. */
static bool
put(char *out, size_t cap, size_t *used, const char *s, size_t n)
{
    if (*used + n >= cap) {
        return false;
    }
    memcpy(out + *used, s, n);
    *used += n;
    out[*used] = '\0';
    return true;
}

static bool
put_escaped(char *out, size_t cap, size_t *used, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char two[2] = {'\\', '\0'};
        bool escape = true;

        switch (s[i]) {
        case '\\': two[1] = '\\'; break;
        case '\n': two[1] = 'n'; break;
        case '\r': two[1] = 'r'; break;
        case '\t': two[1] = 't'; break;
        default:   escape = false; break;
        }
        if (escape ? !put(out, cap, used, two, 2)
                   : !put(out, cap, used, &s[i], 1)) {
            return false;
        }
    }
    return true;
}

/* A uri, cid or handle is written as it is, so it must not hold anything that
 * would end its field or its line. */
static bool
plain_ok(const char *v)
{
    return !indigo_codec_has_break(v) && strchr(v, '\t') == NULL;
}

indigo_codec_status
indigo_cache_encode(const indigo_cache_timeline *t, char *out, size_t cap,
                    size_t *len)
{
    size_t used = 0;

    if (t->count > INDIGO_CACHE_POSTS_MAX) {
        return INDIGO_CODEC_TOO_BIG;
    }
    for (size_t i = 0; i < t->count; i++) {
        const indigo_cache_post *p = &t->posts[i];

        if (p->uri[0] == '\0' || !plain_ok(p->uri) || !plain_ok(p->cid) ||
            !plain_ok(p->handle)) {
            return INDIGO_CODEC_INCOMPLETE;
        }
        if (strlen(p->uri) >= INDIGO_CACHE_URI_MAX ||
            strlen(p->cid) >= INDIGO_CACHE_CID_MAX ||
            strlen(p->handle) >= INDIGO_CACHE_HANDLE_MAX) {
            return INDIGO_CODEC_TOO_BIG;
        }
    }

    {
        char head[32];
        int n = snprintf(head, sizeof head, "indigo-cache %d\n", INDIGO_CACHE_VERSION);

        if (n < 0 || !put(out, cap, &used, head, (size_t) n)) {
            return INDIGO_CODEC_TOO_BIG;
        }
    }

    for (size_t i = 0; i < t->count; i++) {
        const indigo_cache_post *p = &t->posts[i];

        if (!put(out, cap, &used, "post\t", 5) ||
            !put(out, cap, &used, p->uri, strlen(p->uri)) ||
            !put(out, cap, &used, "\t", 1) ||
            !put(out, cap, &used, p->cid, strlen(p->cid)) ||
            !put(out, cap, &used, "\t", 1) ||
            !put(out, cap, &used, p->handle, strlen(p->handle)) ||
            !put(out, cap, &used, "\t", 1) ||
            !put_escaped(out, cap, &used, p->display_name, strlen(p->display_name)) ||
            !put(out, cap, &used, "\t", 1) ||
            !put_escaped(out, cap, &used, p->text, strlen(p->text)) ||
            !put(out, cap, &used, "\n", 1)) {
            return INDIGO_CODEC_TOO_BIG;
        }
    }

    if (!put(out, cap, &used, "end\n", 4)) {
        return INDIGO_CODEC_TOO_BIG;
    }
    if (len) {
        *len = used;
    }
    return INDIGO_CODEC_OK;
}

/* Copy the escaped `v` (length `n`) into dst, undoing indigo_cache_encode's
 * escapes. Any escape other than the four the encoder writes, or a trailing
 * backslash, means the file was not written by this codec. */
static bool
unescape(const char *v, size_t n, char *dst, size_t cap)
{
    size_t o = 0;

    for (size_t i = 0; i < n; i++) {
        char c = v[i];

        if (c == '\\') {
            if (++i >= n) {
                return false;
            }
            switch (v[i]) {
            case '\\': c = '\\'; break;
            case 'n':  c = '\n'; break;
            case 'r':  c = '\r'; break;
            case 't':  c = '\t'; break;
            default:   return false;
            }
        }
        if (o + 1 >= cap) {
            return false;
        }
        dst[o++] = c;
    }
    dst[o] = '\0';
    return true;
}

/* Take the field that starts at *cur and ends at the next tab, copying it
 * (unescaped when `escaped`) into dst. Moves *cur past the tab. */
static bool
take_field(const char **cur, const char *end, char *dst, size_t cap, bool escaped)
{
    const char *tab = memchr(*cur, '\t', (size_t) (end - *cur));
    size_t n;

    if (!tab) {
        return false;
    }
    n = (size_t) (tab - *cur);
    if (escaped) {
        if (!unescape(*cur, n, dst, cap)) {
            return false;
        }
    } else {
        if (n >= cap) {
            return false;
        }
        memcpy(dst, *cur, n);
        dst[n] = '\0';
    }
    *cur = tab + 1;
    return true;
}

/* Parse one "post" line (without its newline) into `p`. The text is the last
 * field, so it is the rest of the line. */
static bool
parse_post(const char *line, size_t n, indigo_cache_post *p)
{
    const char *end = line + n;
    const char *cur;

    if (n < 5 || memcmp(line, "post\t", 5) != 0) {
        return false;
    }
    cur = line + 5;
    if (!take_field(&cur, end, p->uri, sizeof p->uri, false) || p->uri[0] == '\0' ||
        !take_field(&cur, end, p->cid, sizeof p->cid, false) ||
        !take_field(&cur, end, p->handle, sizeof p->handle, false) ||
        !take_field(&cur, end, p->display_name, sizeof p->display_name, true)) {
        return false;
    }
    return unescape(cur, (size_t) (end - cur), p->text, sizeof p->text);
}

indigo_codec_status
indigo_cache_decode(const char *data, size_t len, indigo_cache_timeline *out)
{
    static const char header[] = "indigo-cache ";
    const char *p = data;
    const char *end = data + len;
    bool saw_end = false;

    indigo_cache_timeline_clear(out);
    if (len == 0) {
        return INDIGO_CODEC_EMPTY;
    }
    if (len > INDIGO_CACHE_FILE_MAX) {
        return INDIGO_CODEC_TOO_BIG;
    }
    if (len < sizeof header - 1 || memcmp(p, header, sizeof header - 1) != 0) {
        return INDIGO_CODEC_CORRUPT;
    }
    p += sizeof header - 1;
    {
        const char *nl = memchr(p, '\n', (size_t) (end - p));

        if (!nl) {
            return INDIGO_CODEC_CORRUPT;
        }
        if (nl - p != 1 || *p != '0' + INDIGO_CACHE_VERSION) {
            return INDIGO_CODEC_BAD_VERSION;
        }
        p = nl + 1;
    }

    while (p < end) {
        const char *line;
        size_t line_len;

        if (!indigo_codec_next_line(&p, end, &line, &line_len)) {
            break;
        }
        if (line_len == 3 && memcmp(line, "end", 3) == 0) {
            saw_end = true;
            break;
        }
        if (out->count >= INDIGO_CACHE_POSTS_MAX) {
            indigo_cache_timeline_clear(out);
            return INDIGO_CODEC_TOO_BIG;
        }
        if (!parse_post(line, line_len, &out->posts[out->count])) {
            indigo_cache_timeline_clear(out);
            return INDIGO_CODEC_CORRUPT;
        }
        out->count++;
    }

    if (!saw_end) {
        indigo_cache_timeline_clear(out);
        return INDIGO_CODEC_INCOMPLETE;
    }
    return INDIGO_CODEC_OK;
}
