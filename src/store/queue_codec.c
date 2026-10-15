#include "store/queue_codec.h"

#include "store/codec_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Append `n` bytes to out, keeping room for the terminating NUL. */
static bool
put(char *out, size_t cap, size_t *used, const char *s, size_t n)
{
    if (*used + n + 1 > cap) {
        return false;
    }
    memcpy(out + *used, s, n);
    *used += n;
    out[*used] = '\0';
    return true;
}

static bool
put_escaped(char *out, size_t cap, size_t *used, const char *s)
{
    for (const char *p = s; *p; p++) {
        const char *rep = NULL;

        switch (*p) {
        case '\\': rep = "\\\\"; break;
        case '\n': rep = "\\n";  break;
        case '\r': rep = "\\r";  break;
        case '\t': rep = "\\t";  break;
        default:   break;
        }
        if (rep ? !put(out, cap, used, rep, 2) : !put(out, cap, used, p, 1)) {
            return false;
        }
    }
    return true;
}

/* Copy the escaped text in [s, s + n) into dst, undoing put_escaped. */
static bool
unescape(const char *s, size_t n, char *dst, size_t cap)
{
    size_t o = 0;

    for (size_t i = 0; i < n; i++) {
        char c = s[i];

        if (c == '\\') {
            if (++i >= n) {
                return false;
            }
            switch (s[i]) {
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

indigo_codec_status
indigo_queue_encode(const indigo_post_queue *q, char *out, size_t cap, size_t *len)
{
    size_t used = 0;
    char head[32];

    if (q->count > INDIGO_QUEUE_MAX) {
        return INDIGO_CODEC_TOO_BIG;
    }
    snprintf(head, sizeof head, "indigo-queue %d\n", INDIGO_QUEUE_FORMAT_VERSION);
    if (!put(out, cap, &used, head, strlen(head))) {
        return INDIGO_CODEC_TOO_BIG;
    }
    for (unsigned i = 0; i < q->count; i++) {
        char stamp[32];

        if (q->items[i].text[0] == '\0') {
            return INDIGO_CODEC_INCOMPLETE;
        }
        snprintf(stamp, sizeof stamp, "%lld\t", q->items[i].queued_at);
        if (!put(out, cap, &used, stamp, strlen(stamp)) ||
            !put_escaped(out, cap, &used, q->items[i].text) ||
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

indigo_codec_status
indigo_queue_decode(const char *data, size_t len, indigo_post_queue *out)
{
    static const char header[] = "indigo-queue ";
    const char *p = data;
    const char *end = data + len;
    bool saw_end = false;

    indigo_queue_init(out);
    if (len == 0) {
        return INDIGO_CODEC_EMPTY;
    }
    if (len < sizeof header - 1 || memcmp(p, header, sizeof header - 1) != 0) {
        return INDIGO_CODEC_CORRUPT;
    }
    p += sizeof header - 1;
    {
        const char *nl = memchr(p, '\n', (size_t)(end - p));

        if (!nl) {
            return INDIGO_CODEC_CORRUPT;
        }
        if (nl - p != 1 || *p != '0' + INDIGO_QUEUE_FORMAT_VERSION) {
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
        if (out->count >= INDIGO_QUEUE_MAX) {
            indigo_queue_init(out);
            return INDIGO_CODEC_TOO_BIG;
        }
        {
            const char *lend = line + line_len;
            const char *tab = memchr(line, '\t', line_len);
            char *stop = NULL;
            indigo_queued_post *post = &out->items[out->count];
            long long when;

            if (!tab) {
                indigo_queue_init(out);
                return INDIGO_CODEC_CORRUPT;
            }
            when = strtoll(line, &stop, 10);
            if (stop != tab || !unescape(tab + 1, (size_t)(lend - tab - 1), post->text,
                                         sizeof post->text) ||
                post->text[0] == '\0') {
                indigo_queue_init(out);
                return INDIGO_CODEC_CORRUPT;
            }
            post->queued_at = when;
            out->count++;
        }
    }

    if (!saw_end) {
        indigo_queue_init(out);
        return INDIGO_CODEC_INCOMPLETE;
    }
    return INDIGO_CODEC_OK;
}
