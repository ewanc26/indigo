#include "store/session_codec.h"

#include "store/codec_text.h"

#include <stdio.h>
#include <string.h>

indigo_codec_status
indigo_session_encode(const indigo_saved_session *s, char *out, size_t cap,
                      size_t *len)
{
    const char *vals[] = {s->service, s->session};

    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        if (indigo_codec_has_break(vals[i])) {
            return INDIGO_CODEC_CORRUPT;
        }
    }
    if (!s->service[0] || !s->session[0]) {
        return INDIGO_CODEC_INCOMPLETE;
    }

    int n = snprintf(out, cap,
                     "indigo-session %d\nservice=%s\nsession=%s\nend\n",
                     INDIGO_SESSION_FORMAT_VERSION, s->service, s->session);

    if (n < 0 || (size_t) n >= cap) {
        return INDIGO_CODEC_TOO_BIG;
    }
    if (len) {
        *len = (size_t) n;
    }
    return INDIGO_CODEC_OK;
}

static bool
assign(char *dst, size_t cap, const char *v, size_t n)
{
    if (n >= cap) {
        return false;
    }
    memcpy(dst, v, n);
    dst[n] = '\0';
    return true;
}

indigo_codec_status
indigo_session_decode(const char *data, size_t len, indigo_saved_session *out)
{
    static const char header[] = "indigo-session ";
    const char *p = data;
    const char *end = data + len;
    bool saw_end = false;

    memset(out, 0, sizeof *out);
    if (len == 0) {
        return INDIGO_CODEC_EMPTY;
    }
    if (len > INDIGO_SESSION_FILE_MAX) {
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
        if (nl - p != 1 || *p != '0' + INDIGO_SESSION_FORMAT_VERSION) {
            return INDIGO_CODEC_BAD_VERSION;
        }
        p = nl + 1;
    }

    while (p < end) {
        const char *line;
        size_t line_len;

        if (!indigo_codec_next_line(&p, end, &line, &line_len)) {
            break; /* truncated final line */
        }
        if (line_len == 3 && memcmp(line, "end", 3) == 0) {
            saw_end = true;
            break;
        }

        const char *eq = memchr(line, '=', line_len);

        if (!eq) {
            indigo_session_wipe(out);
            return INDIGO_CODEC_CORRUPT;
        }

        size_t klen = (size_t) (eq - line);
        const char *v = eq + 1;
        size_t vlen = line_len - klen - 1;
        bool ok = true;

        if (klen == 7 && memcmp(line, "service", 7) == 0) {
            ok = assign(out->service, sizeof out->service, v, vlen);
        } else if (klen == 7 && memcmp(line, "session", 7) == 0) {
            ok = assign(out->session, sizeof out->session, v, vlen);
        } /* unknown keys are skipped so a newer file still loads */
        if (!ok) {
            indigo_session_wipe(out);
            return INDIGO_CODEC_TOO_BIG;
        }
    }

    if (!saw_end) {
        indigo_session_wipe(out);
        return INDIGO_CODEC_CORRUPT;
    }
    if (!out->service[0] || !out->session[0]) {
        indigo_session_wipe(out);
        return INDIGO_CODEC_INCOMPLETE;
    }
    return INDIGO_CODEC_OK;
}

void
indigo_session_wipe(indigo_saved_session *s)
{
    volatile char *p = (volatile char *) s;

    for (size_t i = 0; i < sizeof *s; i++) {
        p[i] = 0;
    }
}
