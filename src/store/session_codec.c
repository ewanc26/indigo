#include "store/session_codec.h"

#include <stdio.h>
#include <string.h>

static bool
has_break(const char *v)
{
    return strchr(v, '\n') != NULL || strchr(v, '\r') != NULL;
}

indigo_codec_status
indigo_session_encode(const indigo_saved_session *s, char *out, size_t cap,
                      size_t *len)
{
    const char *vals[] = {s->service, s->handle, s->did, s->pds_url,
                          s->access_jwt, s->refresh_jwt};

    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        if (has_break(vals[i])) {
            return INDIGO_CODEC_CORRUPT;
        }
    }
    if (!s->service[0] || !s->handle[0] || !s->refresh_jwt[0]) {
        return INDIGO_CODEC_INCOMPLETE;
    }

    int n = snprintf(out, cap,
                     "indigo-session %d\nservice=%s\nhandle=%s\ndid=%s\npds=%s\n"
                     "access=%s\nrefresh=%s\nend\n",
                     INDIGO_SESSION_FORMAT_VERSION, s->service, s->handle,
                     s->did, s->pds_url, s->access_jwt, s->refresh_jwt);

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
        const char *nl = memchr(p, '\n', (size_t) (end - p));
        size_t line_len;

        if (!nl) {
            break; /* truncated final line */
        }
        line_len = (size_t) (nl - p);
        if (line_len == 3 && memcmp(p, "end", 3) == 0) {
            saw_end = true;
            break;
        }

        const char *eq = memchr(p, '=', line_len);

        if (!eq) {
            indigo_session_wipe(out);
            return INDIGO_CODEC_CORRUPT;
        }

        size_t klen = (size_t) (eq - p);
        const char *v = eq + 1;
        size_t vlen = line_len - klen - 1;
        bool ok = true;

        if (klen == 7 && memcmp(p, "service", 7) == 0) {
            ok = assign(out->service, sizeof out->service, v, vlen);
        } else if (klen == 6 && memcmp(p, "handle", 6) == 0) {
            ok = assign(out->handle, sizeof out->handle, v, vlen);
        } else if (klen == 3 && memcmp(p, "did", 3) == 0) {
            ok = assign(out->did, sizeof out->did, v, vlen);
        } else if (klen == 3 && memcmp(p, "pds", 3) == 0) {
            ok = assign(out->pds_url, sizeof out->pds_url, v, vlen);
        } else if (klen == 6 && memcmp(p, "access", 6) == 0) {
            ok = assign(out->access_jwt, sizeof out->access_jwt, v, vlen);
        } else if (klen == 7 && memcmp(p, "refresh", 7) == 0) {
            ok = assign(out->refresh_jwt, sizeof out->refresh_jwt, v, vlen);
        } /* unknown keys are skipped so a newer file still loads */
        if (!ok) {
            indigo_session_wipe(out);
            return INDIGO_CODEC_TOO_BIG;
        }
        p = nl + 1;
    }

    if (!saw_end) {
        indigo_session_wipe(out);
        return INDIGO_CODEC_CORRUPT;
    }
    if (!out->service[0] || !out->handle[0] || !out->refresh_jwt[0]) {
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
